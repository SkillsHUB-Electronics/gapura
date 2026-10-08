// Types for docs/PROTOCOL.md. Keep in sync with firmware/src/commands.cpp.

export type ErrorCode =
  | 'NO_CARD'
  | 'AUTH_FAILED'
  | 'READ_FAILED'
  | 'WRITE_FAILED'
  | 'FORBIDDEN_BLOCK'
  | 'UNSUPPORTED_CARD'
  | 'BAD_ARGS'
  | 'UNAUTHORIZED'
  | 'BUSY'
  | 'UNKNOWN_CMD'
  | 'LOW_BATTERY'
  | 'BAD_SIGNATURE'
  | 'NO_READER'
  | 'TIMEOUT'; // client-side only

export type ChargeState = 'charging' | 'full' | 'discharging' | 'fault';

export interface Battery {
  v: number;
  mA: number;
  mW: number;
  pct: number;
  state: ChargeState;
}

// Reader hardware fitted; combined modes take turns, one antenna on at a time.
export type ReaderMode = 'rc522' | 'pn532' | 'rdm6300' | 'rc522+rdm6300' | 'pn532+rdm6300';
export const READER_MODES: { mode: ReaderMode; label: string }[] = [
  { mode: 'rc522', label: 'RC522 (13.56 MHz)' },
  { mode: 'pn532', label: 'PN532 (13.56 MHz)' },
  { mode: 'rdm6300', label: 'RDM6300 (125 kHz, UID only)' },
  { mode: 'rc522+rdm6300', label: 'RC522 + RDM6300' },
  { mode: 'pn532+rdm6300', label: 'PN532 + RDM6300' },
];

export interface Tag {
  uid: string;
  type?: string; // 'MIFARE 1KB', 'EM4100', …
  reader?: 'rc522' | 'pn532' | 'rdm6300';
  ts?: number;
}

export interface Info {
  fw: string;
  name: string;
  heap: number;
  maxBlock: number; // largest free heap block (a TLS handshake needs about 40 KB in one piece)
  psramKB: number; // 0 if no PSRAM is enabled
  uptimeS: number; // seconds since boot
  rc522: boolean; // 13.56 MHz chip answering (RC522 or PN532)
  ina219: boolean;
  reader: { mode: ReaderMode; hf: 'RC522' | 'PN532' | null; hfOk: boolean; lf: 'RDM6300' | null };
  scanning: boolean;
  ip: string;
  rssi: number;
  webhook: number; // last webhook HTTP status, 0 = none
  defaultPassword: boolean; // token is still the factory default
  links: { usb: boolean; wifi: boolean; wsClients: number; ble: boolean };
  battery: Battery;
}

export interface KeyArgs {
  key?: string; // 12 hex chars, default FFFFFFFFFFFF
  keyType?: 'A' | 'B';
}

export interface WifiNetwork {
  ssid: string;
  rssi: number;
  channel: number;
  band: '2.4' | '5';
  secure: boolean;
}

export interface WifiStatus {
  state: 'idle' | 'connecting' | 'connected' | 'failed';
  ssid: string;
  ip: string;
  rssi: number;
  error: string;
}

export interface Config {
  wifi: { ssid: string; pass: string };
  token: string;
  name: string;
  beep: boolean;
  led: { brightness: number };
  battery: { lowPct: number; capacityMah: number };
  ble: { passkey: number };
  webhook: { url: string; token: string };
  security: { set: boolean; id: string; enabled: boolean; factoryKey: string; secret?: string }; // secret is write-only, empty turns it off
  mode: 'app' | 'keyboard' | 'off'; // applied after reboot; off = no Bluetooth
  reader: { mode: ReaderMode }; // applied after reboot
  keyboard: { source: 'block' | 'uid' | 'credential'; block: number; key: string; keyType: 'A' | 'B'; enter: boolean };
}

export type SectorDump = { sector: number; blocks: string[] } | { sector: number; error: ErrorCode };

// Command name -> [args, data]
export interface Commands {
  ping: [void, { fw: string }];
  info: [void, Info];
  battery: [void, Battery];
  scan_start: [void, void];
  scan_stop: [void, void];
  read_uid: [{ timeoutMs?: number }, { uid: string; type: string }];
  read_block: [{ block: number } & KeyArgs, { block: number; hex: string }];
  write_block: [{ block: number; hex: string } & KeyArgs, { block: number }];
  read_sector: [{ sector: number } & KeyArgs, { sector: number; blocks: string[] }];
  dump: [KeyArgs, { sectors: SectorDump[] }];
  write_text: [{ startBlock: number; text: string } & KeyArgs, { blocks: number[] }];
  config_get: [void, Config];
  config_set: [DeepPartial<Config>, void];
  led_test: [{ status?: string; r?: number; g?: number; b?: number; ms?: number }, void];
  buzzer_test: [{ ms?: number }, void];
  webhook_test: [void, void];
  wifi_scan: [void, { scanning: boolean; networks?: WifiNetwork[] }];
  wifi_connect: [{ ssid: string; pass: string }, WifiStatus];
  wifi_status: [void, WifiStatus];
  card_write_credential: [{ text: string; factoryKey?: string }, { uid: string; text: string; keyed: boolean }];
  card_read_credential: [void, { uid: string; text: string }];
  card_reset: [{ factoryKey?: string; secret?: string }, { uid: string }];
  sleep: [void, void];
  reboot: [void, void];
}

export type Cmd = keyof Commands;
export type ArgsOf<C extends Cmd> = Commands[C][0];
export type DataOf<C extends Cmd> = Commands[C][1];

export interface Events {
  boot: { fw: string; name: string; rc522: string; reader: ReaderMode; ina219: boolean; ok: boolean };
  tag: Tag;
  tag_removed: Tag;
  battery: Battery;
  webhook: { status: number; ms: number; body: string };
}
export type EventName = keyof Events;

export interface Request {
  id: number;
  cmd: Cmd;
  args?: unknown;
}

export type Response =
  | { id: number | null; ok: true; data?: unknown }
  | { id: number | null; ok: false; error: { code: ErrorCode; msg: string } };

export interface EventMessage {
  event: EventName;
  data: unknown;
}

export type DeepPartial<T> = { [K in keyof T]?: T[K] extends object ? DeepPartial<T[K]> : T[K] };

export const isTrailer = (block: number) => (block & 3) === 3;
export const isWritable = (block: number) => block > 0 && block < 64 && !isTrailer(block);
export const HEX_BLOCK = /^[0-9a-fA-F]{32}$/;
export const HEX_KEY = /^[0-9a-fA-F]{12}$/;
