// Every link to the device implements Transport. UI code never touches
// WebSocket, Web Bluetooth or Web Serial directly.

export type LinkKind = 'wifi' | 'ble' | 'usb' | 'mock';

export interface Transport {
  readonly kind: LinkKind;
  readonly label: string;
  connect(): Promise<void>;
  disconnect(): Promise<void>;
  /** Sends one JSON message (no trailing newline). */
  send(line: string): Promise<void>;
  /** Called once per complete JSON message received. */
  onLine: (line: string) => void;
  /** Called when the link drops without disconnect(). */
  onClose: (reason?: string) => void;
}

/** Splits a byte/text stream into '\n'-terminated lines. */
export class LineSplitter {
  private buf = '';
  constructor(private readonly emit: (line: string) => void) {}
  push(text: string) {
    this.buf += text;
    let i: number;
    while ((i = this.buf.indexOf('\n')) >= 0) {
      const line = this.buf.slice(0, i).trim();
      this.buf = this.buf.slice(i + 1);
      if (line) this.emit(line);
    }
    if (this.buf.length > 64 * 1024) this.buf = ''; // runaway garbage
  }
}

export const supports = {
  ble: typeof navigator !== 'undefined' && 'bluetooth' in navigator,
  usb: typeof navigator !== 'undefined' && 'serial' in navigator,
};
