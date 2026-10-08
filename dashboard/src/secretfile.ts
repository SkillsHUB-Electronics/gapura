// Backup file for the card security secret and factory key.
// Two forms: encrypted (AES-GCM, key from a passphrase with PBKDF2) and plain
// text. The device never returns its secret, so a backup can only be made from a
// value the dashboard knows (just generated, pasted, or loaded from a file).

export interface Backup {
  secret: string; // 32 hex
  factoryKey: string; // 12 hex
}

const FORMAT = 'gapura-card-security';
const ITERATIONS = 310_000;

export const canEncrypt = () => typeof crypto !== 'undefined' && !!crypto.subtle;

const b64 = (bytes: Uint8Array) => btoa(String.fromCharCode(...bytes));
const unb64 = (s: string) => Uint8Array.from(atob(s), (c) => c.charCodeAt(0));
const hex = (bytes: Uint8Array) => Array.from(bytes, (b) => b.toString(16).padStart(2, '0')).join('').toUpperCase();
const fromHex = (s: string) => Uint8Array.from(s.match(/../g) ?? [], (x) => parseInt(x, 16));

const isHex = (s: unknown, n: number): s is string => typeof s === 'string' && new RegExp(`^[0-9a-fA-F]{${n}}$`).test(s);

/** Short public id, same as the device shows: HMAC-SHA256(secret, "gapura-id")[0..3]. */
export async function fingerprint(secretHex: string): Promise<string> {
  const key = await crypto.subtle.importKey('raw', fromHex(secretHex), { name: 'HMAC', hash: 'SHA-256' }, false, ['sign']);
  const mac = new Uint8Array(await crypto.subtle.sign('HMAC', key, new TextEncoder().encode('gapura-id')));
  return hex(mac.slice(0, 3));
}

async function deriveKey(passphrase: string, salt: Uint8Array<ArrayBuffer>, iterations: number) {
  const base = await crypto.subtle.importKey('raw', new TextEncoder().encode(passphrase), 'PBKDF2', false, ['deriveKey']);
  return crypto.subtle.deriveKey({ name: 'PBKDF2', salt, iterations, hash: 'SHA-256' }, base, { name: 'AES-GCM', length: 256 }, false, ['encrypt', 'decrypt']);
}

/** Encrypted backup, as the text of a `.gapura-key` file. */
export async function encryptBackup(b: Backup, passphrase: string): Promise<string> {
  const salt = crypto.getRandomValues(new Uint8Array(16));
  const iv = crypto.getRandomValues(new Uint8Array(12));
  const key = await deriveKey(passphrase, salt, ITERATIONS);
  const plain = new TextEncoder().encode(JSON.stringify({ secret: b.secret.toUpperCase(), factoryKey: b.factoryKey.toUpperCase() }));
  const data = new Uint8Array(await crypto.subtle.encrypt({ name: 'AES-GCM', iv }, key, plain));
  return JSON.stringify({
    format: FORMAT,
    version: 1,
    encrypted: true,
    id: await fingerprint(b.secret),
    kdf: { name: 'PBKDF2-SHA256', iterations: ITERATIONS, salt: b64(salt) },
    cipher: { name: 'AES-256-GCM', iv: b64(iv) },
    data: b64(data),
  }, null, 2);
}

/** Plain text backup: readable by anyone who gets the file. */
export async function plainBackup(b: Backup): Promise<string> {
  const id = canEncrypt() ? await fingerprint(b.secret) : '';
  return [
    'GAPURA CARD SECURITY BACKUP (plain text, keep private)',
    `format: ${FORMAT} v1`,
    id ? `id: ${id}` : '',
    `secret: ${b.secret.toUpperCase()}`,
    `factoryKey: ${b.factoryKey.toUpperCase()}`,
    '',
  ].filter((l, i, a) => l !== '' || i === a.length - 1).join('\n');
}

export class BackupError extends Error {}

/** Reads either form. Encrypted files need the passphrase. */
export async function loadBackup(text: string, passphrase: string): Promise<Backup> {
  const t = text.trim();
  if (t.startsWith('{')) {
    let f: any;
    try {
      f = JSON.parse(t);
    } catch {
      throw new BackupError('Not a Gapura backup file');
    }
    if (f.format !== FORMAT || f.version !== 1 || !f.encrypted) throw new BackupError('Not a Gapura backup file');
    if (!canEncrypt()) throw new BackupError('Decrypting needs HTTPS or localhost. Open the dashboard from a secure address.');
    if (!passphrase) throw new BackupError('Enter the backup passphrase');
    try {
      const key = await deriveKey(passphrase, unb64(f.kdf.salt), f.kdf.iterations);
      const plain = await crypto.subtle.decrypt({ name: 'AES-GCM', iv: unb64(f.cipher.iv) }, key, unb64(f.data));
      const o = JSON.parse(new TextDecoder().decode(plain));
      if (!isHex(o.secret, 32) || !isHex(o.factoryKey, 12)) throw new Error('bad content');
      return { secret: o.secret.toUpperCase(), factoryKey: o.factoryKey.toUpperCase() };
    } catch {
      throw new BackupError('Wrong passphrase or damaged file');
    }
  }
  const secret = /^secret:\s*([0-9a-fA-F]{32})\s*$/m.exec(t)?.[1];
  const factoryKey = /^factoryKey:\s*([0-9a-fA-F]{12})\s*$/m.exec(t)?.[1] ?? 'FFFFFFFFFFFF';
  if (!secret) throw new BackupError('Not a Gapura backup file');
  return { secret: secret.toUpperCase(), factoryKey: factoryKey.toUpperCase() };
}

export function download(filename: string, text: string) {
  const url = URL.createObjectURL(new Blob([text], { type: 'text/plain' }));
  const a = document.createElement('a');
  a.href = url;
  a.download = filename;
  a.click();
  setTimeout(() => URL.revokeObjectURL(url), 1000);
}
