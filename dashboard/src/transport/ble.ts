import { LineSplitter, type Transport } from './index';

const SERVICE = '6e400001-b5a3-f393-e0a9-e50e24dcca9e';
const CMD = '6e400002-b5a3-f393-e0a9-e50e24dcca9e';
const RESP = '6e400003-b5a3-f393-e0a9-e50e24dcca9e';
const CHUNK = 180;

/** Bluetooth LE link via Web Bluetooth. The OS asks for the passkey on first write. */
export class BleTransport implements Transport {
  readonly kind = 'ble' as const;
  onLine: (line: string) => void = () => {};
  onClose: (reason?: string) => void = () => {};
  private device?: BluetoothDevice;
  private cmd?: BluetoothRemoteGATTCharacteristic;
  private closing = false;
  private readonly splitter = new LineSplitter((l) => this.onLine(l));
  private readonly decoder = new TextDecoder();
  private readonly encoder = new TextEncoder();
  private queue = Promise.resolve();

  get label() {
    return this.device?.name ?? 'Bluetooth';
  }

  async connect() {
    this.device = await navigator.bluetooth.requestDevice({
      filters: [{ services: [SERVICE] }],
    });
    this.device.addEventListener('gattserverdisconnected', () => {
      if (!this.closing) this.onClose('Bluetooth connection lost');
    });
    const server = await this.device.gatt!.connect();
    const service = await server.getPrimaryService(SERVICE);
    this.cmd = await service.getCharacteristic(CMD);
    const resp = await service.getCharacteristic(RESP);
    resp.addEventListener('characteristicvaluechanged', (e) => {
      const value = (e.target as BluetoothRemoteGATTCharacteristic).value!;
      this.splitter.push(this.decoder.decode(value, { stream: true }));
    });
    await resp.startNotifications();
  }

  async disconnect() {
    this.closing = true;
    this.device?.gatt?.disconnect();
  }

  // Writes are serialised: GATT allows one operation at a time.
  send(line: string) {
    const bytes = this.encoder.encode(line + '\n');
    const run = async () => {
      if (!this.cmd) throw new Error('Not connected');
      for (let i = 0; i < bytes.length; i += CHUNK) {
        await this.cmd.writeValueWithResponse(bytes.slice(i, i + CHUNK));
      }
    };
    const p = this.queue.then(run);
    this.queue = p.catch(() => {});
    return p;
  }
}
