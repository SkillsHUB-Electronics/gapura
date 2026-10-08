import { LineSplitter, type Transport } from './index';

/** USB link via Web Serial (Chrome/Edge desktop), 115200 baud NDJSON. */
export class SerialTransport implements Transport {
  readonly kind = 'usb' as const;
  readonly label = 'USB';
  onLine: (line: string) => void = () => {};
  onClose: (reason?: string) => void = () => {};
  private port?: SerialPort;
  private reader?: ReadableStreamDefaultReader<Uint8Array>;
  private closing = false;
  private readonly splitter = new LineSplitter((l) => {
    // The ESP32 boot ROM prints plain text; only JSON lines matter.
    if (l.startsWith('{')) this.onLine(l);
  });
  private readonly encoder = new TextEncoder();

  async connect() {
    this.port = await navigator.serial.requestPort();
    await this.port.open({ baudRate: 115200 });
    // On the C5 board DTR drives BOOT (and RTS drives RESET): release both so
    // an open port is never read as a held BOOT button.
    await this.port.setSignals({ dataTerminalReady: false, requestToSend: false }).catch(() => {});
    void this.readLoop();
  }

  private async readLoop() {
    const decoder = new TextDecoder();
    try {
      while (this.port?.readable) {
        this.reader = this.port.readable.getReader();
        for (;;) {
          const { value, done } = await this.reader.read();
          if (done) break;
          this.splitter.push(decoder.decode(value, { stream: true }));
        }
        this.reader.releaseLock();
        if (this.closing) break;
      }
    } catch {
      // device unplugged
    }
    if (!this.closing) this.onClose('USB cable disconnected');
  }

  async disconnect() {
    this.closing = true;
    await this.reader?.cancel().catch(() => {});
    await this.port?.close().catch(() => {});
  }

  async send(line: string) {
    const writer = this.port?.writable?.getWriter();
    if (!writer) throw new Error('Not connected');
    try {
      await writer.write(this.encoder.encode(line + '\n'));
    } finally {
      writer.releaseLock();
    }
  }
}
