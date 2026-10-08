// Protocol client: request/response matching by id, typed events, shared state.
import type { ArgsOf, Battery, Cmd, DataOf, ErrorCode, EventMessage, EventName, Events, Info, Response, Tag } from './protocol';
import type { Transport } from './transport';

export class DeviceError extends Error {
  constructor(
    readonly code: ErrorCode,
    message: string,
  ) {
    super(message);
  }
}

type Pending = { resolve: (d: unknown) => void; reject: (e: Error) => void; timer: number };
type Listener<E extends EventName> = (data: Events[E]) => void;

export interface State {
  transport: Transport | null;
  info: Info | null;
  battery: Battery | null;
  tags: Array<Tag & { at: Date }>;
  present: Tag | null;
  batteryHistory: Array<{ t: number; v: number; mA: number; pct: number }>;
  error: string | null;
}

const HISTORY_MS = 10 * 60 * 1000;
const MAX_TAGS = 200;

class Device {
  state: State = { transport: null, info: null, battery: null, tags: [], present: null, batteryHistory: [], error: null };

  private nextId = 1;
  private pending = new Map<number, Pending>();
  private listeners = new Map<string, Set<(d: any) => void>>();
  private changeListeners = new Set<() => void>();

  get connected() {
    return this.state.transport !== null;
  }

  async connect(t: Transport) {
    await this.disconnect();
    t.onLine = (line) => this.receive(line);
    t.onClose = (reason) => this.dropped(reason);
    await t.connect();
    this.state = { ...this.state, transport: t, error: null, tags: [], present: null, batteryHistory: [] };
    this.changed();
    await this.refresh();
  }

  async disconnect() {
    const t = this.state.transport;
    if (!t) return;
    this.state.transport = null;
    this.failPending('Disconnected');
    await t.disconnect();
    this.changed();
  }

  async refresh() {
    const info = await this.request('info');
    this.state.info = info;
    this.setBattery(info.battery);
  }

  request<C extends Cmd>(cmd: C, ...rest: ArgsOf<C> extends void ? [] : [ArgsOf<C>]): Promise<DataOf<C>>;
  request(cmd: Cmd, args?: unknown, timeoutMs = 15000): Promise<unknown> {
    const t = this.state.transport;
    if (!t) return Promise.reject(new DeviceError('BUSY', 'Not connected'));
    const id = this.nextId++;
    return new Promise((resolve, reject) => {
      const timer = window.setTimeout(() => {
        this.pending.delete(id);
        reject(new DeviceError('TIMEOUT', 'The device did not answer in time'));
      }, timeoutMs);
      this.pending.set(id, { resolve, reject, timer });
      t.send(JSON.stringify(args === undefined ? { id, cmd } : { id, cmd, args })).catch((e: Error) => {
        clearTimeout(timer);
        this.pending.delete(id);
        reject(e);
      });
    });
  }

  on<E extends EventName>(event: E, cb: Listener<E>) {
    if (!this.listeners.has(event)) this.listeners.set(event, new Set());
    this.listeners.get(event)!.add(cb);
    return () => this.listeners.get(event)!.delete(cb);
  }

  /** Re-render hook for any state change. */
  subscribe(cb: () => void) {
    this.changeListeners.add(cb);
    return () => this.changeListeners.delete(cb);
  }

  private receive(line: string) {
    let msg: Response | EventMessage;
    try {
      msg = JSON.parse(line);
    } catch {
      return;
    }
    if ('event' in msg) return this.handleEvent(msg);
    if (msg.id === null) return;
    const p = this.pending.get(msg.id);
    if (!p) return;
    clearTimeout(p.timer);
    this.pending.delete(msg.id);
    if (msg.ok) p.resolve(msg.data);
    else p.reject(new DeviceError(msg.error.code, msg.error.msg));
  }

  private handleEvent({ event, data }: EventMessage) {
    switch (event) {
      case 'tag': {
        const tag = data as Tag;
        this.state.present = tag;
        this.state.tags = [{ ...tag, at: new Date() }, ...this.state.tags].slice(0, MAX_TAGS);
        break;
      }
      case 'tag_removed':
        this.state.present = null;
        break;
      case 'battery':
        this.setBattery(data as Battery);
        break;
      case 'boot':
        void this.refresh().catch(() => {});
        break;
    }
    this.listeners.get(event)?.forEach((cb) => cb(data));
    this.changed();
  }

  private setBattery(b: Battery) {
    const now = Date.now();
    this.state.battery = b;
    this.state.batteryHistory = [...this.state.batteryHistory.filter((p) => now - p.t < HISTORY_MS), { t: now, v: b.v, mA: b.mA, pct: b.pct }];
    this.changed();
  }

  private dropped(reason?: string) {
    this.state.transport = null;
    this.state.error = reason ?? 'Connection lost';
    this.failPending(this.state.error);
    this.changed();
  }

  private failPending(reason: string) {
    for (const p of this.pending.values()) {
      clearTimeout(p.timer);
      p.reject(new DeviceError('BUSY', reason));
    }
    this.pending.clear();
  }

  private changed() {
    this.changeListeners.forEach((cb) => cb());
  }
}

export const device = new Device();
