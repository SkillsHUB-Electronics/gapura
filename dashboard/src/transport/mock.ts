import type { Battery, Config, Request } from '../protocol';
import { isWritable } from '../protocol';
import type { Transport } from './index';

const hexToText = (hex: string) => {
  const bytes = hex.match(/../g)?.map((x) => parseInt(x, 16)).filter((x) => x >= 0x20 && x < 0x7f) ?? [];
  return String.fromCharCode(...bytes);
};

const DEFAULT_KEY = 'FFFFFFFFFFFF';
const CARDS = [
  { uid: 'DE AD BE EF', type: 'MIFARE 1KB' },
  { uid: '04 A2 19 5C', type: 'MIFARE 1KB' },
  { uid: '7B 11 C0 3E', type: 'MIFARE 1KB' },
];

/** Simulated device for UI work without hardware (`npm run dev`, "Demo"). */
export class MockTransport implements Transport {
  readonly kind = 'mock' as const;
  readonly label = 'Demo device';
  onLine: (line: string) => void = () => {};
  onClose: (reason?: string) => void = () => {};

  private timers: number[] = [];
  private scanning = true;
  private present: (typeof CARDS)[number] | null = null;
  private memory = new Map<number, string>();
  private battery: Battery = { v: 3.95, mA: 142, mW: 561, pct: 71, state: 'discharging' };
  private config: Config = {
    wifi: { ssid: 'Office', pass: '********' },
    token: 'rfid1234',
    mode: 'app',
    reader: { mode: 'rc522' },
    webhook: { url: '', token: '' },
    security: { set: false, id: '', enabled: true, factoryKey: '************' },
    keyboard: { source: 'block', block: 4, key: '************', keyType: 'A', enter: true },
    name: 'Gapura-DEMO',
    beep: true,
    led: { brightness: 60 },
    battery: { lowPct: 15, capacityMah: 2000 },
    ble: { passkey: 482913 },
  };

  async connect() {
    for (let b = 0; b < 64; b++) this.memory.set(b, '00'.repeat(16));
    this.memory.set(0, 'DEADBEEF22080400628C3A1A2B3C4D1D');
    for (let s = 0; s < 16; s++) this.memory.set(s * 4 + 3, 'FFFFFFFFFFFFFF078069FFFFFFFFFFFF');
    this.memory.set(4, '48656C6C6F2052464944210000000000'); // "Hello RFID!"

    let i = 0;
    this.every(4000, () => {
      if (this.present) {
        this.event('tag_removed', { uid: this.present.uid });
        this.present = null;
      } else {
        this.present = CARDS[i++ % CARDS.length];
        if (this.scanning) this.event('tag', { ...this.present, ts: Math.round(performance.now()) });
      }
    });
    this.every(5000, () => {
      const t = Date.now() / 60000;
      const charging = Math.floor(t) % 4 === 0;
      this.battery = {
        v: +(3.9 + 0.05 * Math.sin(t)).toFixed(2),
        mA: charging ? -480 : Math.round(130 + 30 * Math.random()),
        mW: charging ? -1900 : 540,
        pct: Math.min(100, Math.max(0, this.battery.pct + (charging ? 1 : -0.2))),
        state: charging ? 'charging' : 'discharging',
      };
      this.battery.pct = Math.round(this.battery.pct);
      this.event('battery', this.battery);
    });
  }

  async disconnect() {
    this.timers.forEach(clearInterval);
    this.timers = [];
  }

  async send(line: string) {
    const req = JSON.parse(line) as Request;
    setTimeout(() => this.onLine(JSON.stringify(this.handle(req))), 60 + Math.random() * 120);
  }

  private every(ms: number, fn: () => void) {
    this.timers.push(window.setInterval(fn, ms));
  }

  private event(event: string, data: unknown) {
    this.onLine(JSON.stringify({ event, data }));
  }

  private handle({ id, cmd, args }: Request) {
    const a = (args ?? {}) as Record<string, any>;
    const ok = (data?: unknown) => ({ id, ok: true, ...(data === undefined ? {} : { data }) });
    const err = (code: string, msg: string) => ({ id, ok: false, error: { code, msg } });
    const needCard = () => (this.present ? null : err('NO_CARD', 'No card in the field'));
    const badKey = () => ((a.key ?? DEFAULT_KEY).toUpperCase() !== DEFAULT_KEY ? err('AUTH_FAILED', 'Authentication failed') : null);

    switch (cmd) {
      case 'ping':
        return ok({ fw: '0.1.0-demo' });
      case 'info':
        return ok({
          fw: '0.1.0-demo', name: this.config.name, heap: 182340, maxBlock: 110000, psramKB: 0, uptimeS: 3600, rc522: true, ina219: true, defaultPassword: this.config.token === 'rfid1234', webhook: 0,
          scanning: this.scanning, ip: '192.168.1.50', rssi: -54,
          reader: {
            mode: this.config.reader.mode,
            hf: this.config.reader.mode.startsWith('pn532') ? 'PN532' : this.config.reader.mode === 'rdm6300' ? null : 'RC522',
            hfOk: this.config.reader.mode !== 'rdm6300',
            lf: this.config.reader.mode.includes('rdm6300') ? 'RDM6300' : null,
          },
          links: { usb: true, wifi: true, wsClients: 1, ble: false }, battery: this.battery,
        });
      case 'battery':
        return ok(this.battery);
      case 'scan_start':
      case 'scan_stop':
        this.scanning = cmd === 'scan_start';
        return ok();
      case 'read_uid':
        return needCard() ?? ok({ uid: this.present!.uid, type: this.present!.type });
      case 'read_block':
        return needCard() ?? badKey() ?? ok({ block: a.block, hex: this.memory.get(a.block) });
      case 'write_block':
        if (!isWritable(a.block)) return err('FORBIDDEN_BLOCK', 'Block 0 and sector trailers are write-protected');
        return needCard() ?? badKey() ?? (this.memory.set(a.block, a.hex.toUpperCase()), ok({ block: a.block }));
      case 'read_sector':
        return needCard() ?? badKey() ?? ok({ sector: a.sector, blocks: [0, 1, 2, 3].map((i) => this.memory.get(a.sector * 4 + i)) });
      case 'dump':
        return needCard() ?? badKey() ?? ok({
          sectors: Array.from({ length: 16 }, (_, s) =>
            s === 15 ? { sector: s, error: 'AUTH_FAILED' } : { sector: s, blocks: [0, 1, 2, 3].map((i) => this.memory.get(s * 4 + i)) }),
        });
      case 'led_test':
      case 'buzzer_test':
      case 'webhook_test':
        return ok();
      case 'wifi_scan':
        return ok({
          scanning: false,
          networks: [
            { ssid: 'Office', rssi: -48, channel: 6, band: '2.4', secure: true },
            { ssid: 'Office-5G', rssi: -55, channel: 36, band: '5', secure: true },
            { ssid: 'Guest', rssi: -71, channel: 11, band: '2.4', secure: false },
          ],
        });
      case 'wifi_connect':
      case 'wifi_status':
        return ok({ state: 'connected', ssid: a?.ssid ?? 'Office', ip: '192.168.1.50', rssi: -54, error: '' });
      case 'card_write_credential':
        return needCard() ?? (this.memory.set(8, Array.from(new TextEncoder().encode(a.text), (x) => x.toString(16).padStart(2, '0')).join('').padEnd(32, '0').toUpperCase()), ok({ uid: this.present!.uid, text: a.text, keyed: true }));
      case 'card_read_credential':
        return needCard() ?? ok({ uid: this.present!.uid, text: hexToText(this.memory.get(8) ?? '') });
      case 'card_reset':
        return needCard() ?? (this.memory.delete(4), ok({ uid: this.present!.uid }));
      case 'write_text': {
        const blocks: number[] = [];
        const bytes = new TextEncoder().encode(a.text);
        let b = a.startBlock;
        for (let pos = 0; pos < Math.max(bytes.length, 1); pos += 16, b++) {
          while (b < 64 && !isWritable(b)) b++;
          const chunk = Array.from(bytes.slice(pos, pos + 16), (x) => x.toString(16).padStart(2, '0')).join('');
          this.memory.set(b, chunk.padEnd(32, '0').toUpperCase());
          blocks.push(b);
        }
        return ok({ blocks });
      }
      case 'config_get':
        return ok({ ...this.config, token: '********' });
      case 'config_set':
        Object.assign(this.config, { ...a, led: { ...this.config.led, ...a.led }, battery: { ...this.config.battery, ...a.battery }, reader: { ...this.config.reader, ...a.reader } });
        return ok();
      case 'sleep':
        return ok();
      case 'reboot':
        return ok();
      default:
        return err('UNKNOWN_CMD', 'Unknown command');
    }
  }
}
