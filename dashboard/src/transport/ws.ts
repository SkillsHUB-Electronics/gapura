import type { Transport } from './index';

/** Wi-Fi link: WebSocket carries commands, responses and events. */
export class WsTransport implements Transport {
  readonly kind = 'wifi' as const;
  onLine: (line: string) => void = () => {};
  onClose: (reason?: string) => void = () => {};
  private ws?: WebSocket;
  private closing = false;

  constructor(
    readonly host: string,
    readonly token: string,
  ) {}

  get label() {
    return this.host;
  }

  connect(): Promise<void> {
    return new Promise((resolve, reject) => {
      const proto = location.protocol === 'https:' ? 'wss' : 'ws';
      const ws = new WebSocket(`${proto}://${this.host}/ws?token=${encodeURIComponent(this.token)}`);
      let opened = false;
      ws.onopen = () => {
        opened = true;
        this.ws = ws;
        resolve();
      };
      ws.onmessage = (e) => typeof e.data === 'string' && this.onLine(e.data);
      ws.onclose = () => {
        if (!opened) reject(new Error('Could not connect. Check the address and token.'));
        else if (!this.closing) this.onClose('Wi-Fi connection lost');
      };
    });
  }

  async disconnect() {
    this.closing = true;
    this.ws?.close();
  }

  async send(line: string) {
    if (this.ws?.readyState !== WebSocket.OPEN) throw new Error('Not connected');
    this.ws.send(line);
  }
}
