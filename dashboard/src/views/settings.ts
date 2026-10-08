import { device } from '../device';
import { READER_MODES, type Config, type DeepPartial, type ReaderMode } from '../protocol';
import { WsTransport } from '../transport/ws';
import { store } from './connect';
import { BackupError, canEncrypt, download, encryptBackup, fingerprint, loadBackup, plainBackup } from '../secretfile';
import { action, field, h, toast } from '../ui';

export function mount(root: HTMLElement) {
  const section = h('section', { class: 'stack' }, h('article', { class: 'card empty' }, 'Loading settings…'));
  root.replaceChildren(section);
  let alive = true;

  // Live webhook results (event `webhook`): newest first, last 8.
  type WhResult = { at: Date; status: number; ms: number; body: string };
  const whResults: WhResult[] = [];
  const whLog = h('div', { class: 'stack small' });
  const paintWhLog = () => {
    whLog.replaceChildren(
      ...(whResults.length
        ? whResults.map((r) =>
            h('p', { class: 'small mono' },
              `${r.at.toLocaleTimeString()}  ${r.status > 0 ? `HTTP ${r.status}` : `error ${r.status}`}  ${r.ms} ms${r.body ? `  ${r.body}` : ''}`))
        : [h('p', { class: 'muted small' }, 'No webhook calls yet. Tap a card to see the server reply here.')]),
    );
  };
  const offWebhook = device.on('webhook', (w) => {
    whResults.unshift({ at: new Date(), status: w.status, ms: w.ms, body: w.body });
    whResults.length = Math.min(whResults.length, 8);
    paintWhLog();
  });
  paintWhLog();

  // Card security status, switch and buttons come from config_get: reload after
  // a security change so the page never shows a stale "No secret set" or "Off".
  const load = () =>
    device.request('config_get').then(
      (c) => alive && render(c),
      (e: Error) => alive && section.replaceChildren(h('article', { class: 'card empty' }, e.message)),
    );
  void load();

  function render(c: Config) {
    const name = h('input', { value: c.name, maxLength: 24 });
    const beep = h('input', { type: 'checkbox', checked: c.beep, class: 'switch' });
    const bright = h('input', { type: 'range', min: 0, max: 100, value: c.led.brightness });
    const brightOut = h('output', {}, `${c.led.brightness}%`);
    bright.addEventListener('input', () => (brightOut.textContent = `${bright.value}%`));
    const lowPct = h('input', { type: 'number', min: 5, max: 50, value: c.battery.lowPct });
    const cap = h('input', { type: 'number', min: 100, max: 20000, step: 100, value: c.battery.capacityMah });
    const ssid = h('input', { value: c.wifi.ssid, maxLength: 32, autocomplete: 'off' });
    const pass = h('input', { type: 'password', placeholder: c.wifi.pass ? 'Unchanged' : 'No password', maxLength: 64, autocomplete: 'new-password' });
    const pw = h('input', { type: 'password', placeholder: 'Unchanged', minLength: 8, maxLength: 64, autocomplete: 'new-password' });
    const pw2 = h('input', { type: 'password', placeholder: 'Repeat new password', maxLength: 64, autocomplete: 'new-password' });
    const passkey = h('input', { value: String(c.ble.passkey), inputMode: 'numeric', pattern: '[0-9]{6}', maxLength: 6, class: 'mono' });

    const saveDevice: HTMLButtonElement = h('button', {
      class: 'btn btn-primary',
      onclick: () => save(saveDevice, {
        name: name.value.trim(),
        beep: beep.checked,
        led: { brightness: Number(bright.value) },
        battery: { lowPct: Number(lowPct.value), capacityMah: Number(cap.value) },
      }, 'Settings saved'),
    }, 'Save');

    // Wi-Fi and the Bluetooth passkey save separately, so one can change without the other.
    const saveNet: HTMLButtonElement = h('button', {
      class: 'btn btn-primary',
      onclick: () => {
        const name = ssid.value.trim();
        if (!name) {
          ssid.setCustomValidity('Enter the network name');
          ssid.reportValidity();
          return;
        }
        ssid.setCustomValidity('');
        const patch: DeepPartial<Config> = { wifi: { ssid: name } };
        if (pass.value) patch.wifi!.pass = pass.value;
        void save(saveNet, patch, 'Saved. Restart the reader to apply.');
      },
    }, 'Save Wi-Fi');

    const savePasskey: HTMLButtonElement = h('button', {
      class: 'btn btn-primary',
      onclick: () => {
        if (!/^\d{6}$/.test(passkey.value)) {
          passkey.setCustomValidity('6 digits');
          passkey.reportValidity();
          return;
        }
        passkey.setCustomValidity('');
        void save(savePasskey, { ble: { passkey: Number(passkey.value) } }, 'Passkey saved. Restart the reader to apply.');
      },
    }, 'Save passkey');

    const savePw: HTMLButtonElement = h('button', {
      class: 'btn btn-primary',
      onclick: () => {
        pw.setCustomValidity(pw.value.length < 8 ? 'At least 8 characters' : pw.value !== pw2.value ? 'Passwords do not match' : '');
        if (!pw.reportValidity()) return;
        void action(savePw, async () => {
          await device.request('config_set', { token: pw.value });
          store.set('token', pw.value); // used on the next Wi-Fi connect
        }, 'Password changed. Use it next time you connect over Wi-Fi.');
      },
    }, 'Change password');

    const mode = h('select', {}, h('option', { value: 'app' }, 'App (dashboard and nRF over Bluetooth)'), h('option', { value: 'keyboard' }, 'Keyboard (types the card text)'), h('option', { value: 'off' }, 'Off (no Bluetooth, more memory for an HTTPS webhook)'));
    mode.value = c.mode;
    const kbSource = h('select', {}, h('option', { value: 'block' }, 'Text in a card block'), h('option', { value: 'uid' }, 'Card UID'), h('option', { value: 'credential' }, 'Credential text (secured)'));
    kbSource.value = c.keyboard.source;
    const kbBlock = h('input', { type: 'number', min: 1, max: 62, value: c.keyboard.block });
    const kbKey = h('input', { type: 'password', placeholder: 'Unchanged (default FFFFFFFFFFFF)', maxLength: 12, autocomplete: 'off', class: 'mono' });
    const kbType = h('select', {}, h('option', { value: 'A' }, 'Key A'), h('option', { value: 'B' }, 'Key B'));
    kbType.value = c.keyboard.keyType;
    const kbEnter = h('input', { type: 'checkbox', checked: c.keyboard.enter, class: 'switch' });
    const saveMode: HTMLButtonElement = h('button', {
      class: 'btn btn-primary',
      onclick: () => {
        const k = kbKey.value.trim();
        if (k && !/^[0-9a-fA-F]{12}$/.test(k)) {
          kbKey.setCustomValidity('12 hex characters');
          kbKey.reportValidity();
          return;
        }
        kbKey.setCustomValidity('');
        const keyboard: NonNullable<DeepPartial<Config>['keyboard']> = {
          source: kbSource.value as 'block' | 'uid' | 'credential',
          block: Number(kbBlock.value),
          keyType: kbType.value as 'A' | 'B',
          enter: kbEnter.checked,
        };
        if (k) keyboard.key = k.toUpperCase();
        void save(saveMode, { mode: mode.value as 'app' | 'keyboard' | 'off', keyboard }, 'Saved. Restart the reader to apply.');
      },
    }, 'Save');

    const readerMode = h('select', {}, ...READER_MODES.map((m) => h('option', { value: m.mode }, m.label)));
    readerMode.value = c.reader.mode;
    const readerHint = h('p', { class: 'muted small' });
    const paintReaderHint = () => {
      const m = readerMode.value as ReaderMode;
      readerHint.textContent = m === 'rdm6300'
        ? '125 kHz EM4100 tags: the UID only. Block read/write and card security need a 13.56 MHz reader.'
        : m.includes('+')
          ? 'Both readers take turns (one antenna on at a time), so a tag is seen within about a second. Card security works on 13.56 MHz cards; 125 kHz tags send their UID.'
          : 'MIFARE Classic and NTAG cards: UID, blocks and card security.';
    };
    readerMode.addEventListener('change', paintReaderHint);
    paintReaderHint();
    const saveReader: HTMLButtonElement = h('button', {
      class: 'btn btn-primary',
      onclick: () => save(saveReader, { reader: { mode: readerMode.value as ReaderMode } }, 'Saved. Restart the reader to apply.'),
    }, 'Save');

    const whUrl = h('input', { value: c.webhook.url, placeholder: 'https://server.example/api/gapura/tag', maxLength: 200, autocomplete: 'off', spellcheck: false });
    const whToken = h('input', { type: 'password', placeholder: c.webhook.token ? 'Unchanged' : 'Optional', maxLength: 128, autocomplete: 'new-password' });
    const saveWh: HTMLButtonElement = h('button', {
      class: 'btn btn-primary',
      onclick: () => {
        const u = whUrl.value.trim();
        if (u && !/^https?:\/\//.test(u)) {
          whUrl.setCustomValidity('Start with http:// or https://');
          whUrl.reportValidity();
          return;
        }
        whUrl.setCustomValidity('');
        const webhook: { url: string; token?: string } = { url: u };
        if (whToken.value) webhook.token = whToken.value;
        void save(saveWh, { webhook }, 'Webhook saved');
      },
    }, 'Save');

    const testWh: HTMLButtonElement = h('button', {
      class: 'btn',
      onclick: () => void action(testWh, () => device.request('webhook_test'), 'Test sent. The reply appears below.'),
    }, 'Send test');

    const secStatus = !c.security.set
      ? 'No secret set: cards are read with their plain block.'
      : c.security.enabled
        ? `On. Secret id ${c.security.id}: only cards locked with this secret are accepted.`
        : `Off. Secret id ${c.security.id} is kept, but cards are read with their plain block.`;
    const secEnabled = h('input', { type: 'checkbox', checked: c.security.enabled, disabled: !c.security.set, class: 'switch' });
    secEnabled.addEventListener('change', () => void save(saveSec, { security: { enabled: secEnabled.checked } } as DeepPartial<Config>, secEnabled.checked ? 'Card security on' : 'Card security off', true));
    const secInput = h('input', { class: 'mono', maxLength: 32, placeholder: c.security.set ? 'Unchanged' : '32 hex characters', spellcheck: false, autocomplete: 'off' });
    const secFactory = h('input', { class: 'mono', maxLength: 12, placeholder: 'Unchanged (default FFFFFFFFFFFF)', spellcheck: false, autocomplete: 'off' });
    const secShown = h('p', { class: 'mono small warn' });
    const genBtn: HTMLButtonElement = h('button', {
      class: 'btn',
      onclick: () => {
        const bytes = crypto.getRandomValues(new Uint8Array(16));
        const hex = Array.from(bytes, (b) => b.toString(16).padStart(2, '0')).join('').toUpperCase();
        secInput.value = hex;
        secShown.textContent = `New secret: ${hex}. Press Download backup (or save it in a password manager), then Save. The reader never shows it again.`;
      },
    }, 'Generate');
    // Backup file: the device never returns its secret, so a backup is made from
    // the value in the field (just generated, pasted, or loaded from a file).
    const bkPass = h('input', { type: 'password', placeholder: canEncrypt() ? 'Passphrase to encrypt the file' : 'Encryption needs HTTPS or localhost', maxLength: 128, autocomplete: 'new-password', disabled: !canEncrypt() });
    const fileInput = h('input', { type: 'file', accept: '.gapura-key,.txt,application/json,text/plain', hidden: true });
    const downloadBtn: HTMLButtonElement = h('button', {
      class: 'btn',
      onclick: async () => {
        const s = secInput.value.trim();
        if (!/^[0-9a-fA-F]{32}$/.test(s)) {
          toast('Generate or paste a 32-character secret first', 'error');
          return;
        }
        const backup = { secret: s, factoryKey: (secFactory.value.trim() || 'FFFFFFFFFFFF').toUpperCase() };
        const pass = bkPass.value;
        if (canEncrypt() && pass) {
          if (pass.length < 8) {
            bkPass.setCustomValidity('At least 8 characters');
            bkPass.reportValidity();
            return;
          }
          bkPass.setCustomValidity('');
          download('gapura-card-security.gapura-key', await encryptBackup(backup, pass));
          toast('Encrypted backup downloaded. Keep the passphrase safe too.', 'ok');
        } else {
          if (!confirm('Save the secret as an unencrypted text file? Anyone who gets the file can make cards for your readers.')) return;
          download('gapura-card-security.txt', await plainBackup(backup));
          toast('Plain text backup downloaded. Store it somewhere private.', 'ok');
        }
      },
    }, 'Download backup');
    const loadBtn: HTMLButtonElement = h('button', { class: 'btn', onclick: () => fileInput.click() }, 'Load from file');
    fileInput.addEventListener('change', async () => {
      const f = fileInput.files?.[0];
      fileInput.value = '';
      if (!f) return;
      try {
        const b = await loadBackup(await f.text(), bkPass.value);
        secInput.value = b.secret;
        secFactory.value = b.factoryKey;
        const id = canEncrypt() ? await fingerprint(b.secret) : '';
        const same = id && c.security.set && id === c.security.id;
        secShown.textContent = `Loaded${id ? ` secret id ${id}` : ''} from ${f.name}. ${same ? 'It matches this reader. ' : ''}Press Save to apply it.`;
      } catch (e) {
        toast(e instanceof BackupError ? e.message : 'Could not read the file', 'error');
      }
    });

    const saveSec: HTMLButtonElement = h('button', {
      class: 'btn btn-primary',
      onclick: () => {
        const s = secInput.value.trim();
        const f = secFactory.value.trim();
        if (s && !/^[0-9a-fA-F]{32}$/.test(s)) {
          secInput.setCustomValidity('32 hex characters');
          secInput.reportValidity();
          return;
        }
        secInput.setCustomValidity('');
        if (f && !/^[0-9a-fA-F]{12}$/.test(f)) {
          secFactory.setCustomValidity('12 hex characters');
          secFactory.reportValidity();
          return;
        }
        secFactory.setCustomValidity('');
        const security: { secret?: string; factoryKey?: string } = {};
        if (s) security.secret = s.toUpperCase();
        if (f) security.factoryKey = f.toUpperCase();
        if (!s && !f) return;
        void save(saveSec, { security } as DeepPartial<Config>, 'Card security saved', true);
      },
    }, 'Save');
    const forgetSec: HTMLButtonElement = h('button', {
      class: 'btn btn-danger',
      disabled: !c.security.set,
      onclick: () => {
        if (!confirm('Forget the secret on this reader? To use secured cards again you must enter the same secret.')) return;
        void save(forgetSec, { security: { secret: '' } } as DeepPartial<Config>, 'Secret removed', true);
      },
    }, 'Forget secret');

    // Hardware test: LED colours and status looks, buzzer.
    const hwMsg = h('p', { class: 'muted small' }, 'Each test lasts three seconds, then the LED returns to its normal status.');
    const run = (btn: HTMLButtonElement, cmd: 'led_test' | 'buzzer_test', args: Record<string, unknown>, text: string) =>
      action(btn, () => device.request(cmd, args as never), text);
    const colorBtns = (
      [['Red', 255, 0, 0], ['Green', 0, 255, 0], ['Blue', 0, 0, 255], ['White', 255, 255, 255], ['Off', 0, 0, 0]] as const
    ).map(([label, r, g, b]) => {
      const btn: HTMLButtonElement = h('button', { class: 'btn', onclick: () => void run(btn, 'led_test', { r, g, b }, `LED ${label.toLowerCase()}`) }, label);
      return btn;
    });
    const looks = h('select', {},
      ...([['idle', 'Idle (green breathing)'], ['client', 'Client connected'], ['wifi', 'Wi-Fi connecting'], ['setup', 'Setup hotspot'], ['charging', 'Charging'], ['low', 'Low battery'], ['critical', 'Critical battery'], ['flash_ok', 'Tag read'], ['flash_error', 'Error']] as const)
        .map(([v, label]) => h('option', { value: v }, label)));
    const lookBtn: HTMLButtonElement = h('button', { class: 'btn', onclick: () => void run(lookBtn, 'led_test', { status: looks.value }, 'Status look shown') }, 'Show');
    const beepBtn: HTMLButtonElement = h('button', { class: 'btn', onclick: () => void run(beepBtn, 'buzzer_test', { ms: 150 }, 'Beep') }, 'Short beep');
    const longBtn: HTMLButtonElement = h('button', { class: 'btn', onclick: () => void run(longBtn, 'buzzer_test', { ms: 1000 }, 'Long beep') }, 'Long beep');
    // Wi-Fi networks: scan, pick, connect. Works over USB, Bluetooth and Wi-Fi.
    const wifiList = h('div', { class: 'stack small' });
    const wifiSsid = h('input', { maxLength: 32, placeholder: 'Network name', autocomplete: 'off', spellcheck: false });
    const wifiPass = h('input', { type: 'password', maxLength: 63, placeholder: 'Password (empty for an open network)', autocomplete: 'new-password' });
    const wifiMsg = h('p', { class: 'muted small' });
    const sleep = (ms: number) => new Promise((r) => setTimeout(r, ms));
    const scanBtn: HTMLButtonElement = h('button', {
      class: 'btn',
      onclick: async () => {
        scanBtn.disabled = true;
        wifiMsg.textContent = 'Scanning both bands, this takes up to 30 seconds. Over Wi-Fi the reader leaves its network meanwhile and this page reconnects afterwards.';
        try {
          for (let i = 0; i < 40 && alive; i++) {
            const r = await device.request('wifi_scan').catch(() => ({ scanning: true, networks: undefined }));
            if (!r.scanning) {
              const nets = r.networks ?? [];
              wifiMsg.textContent = nets.length ? `${nets.length} networks found` : 'No networks found';
              wifiList.replaceChildren(...nets.map((n) =>
                h('button', {
                  class: 'dump-row',
                  type: 'button',
                  onclick: () => {
                    wifiSsid.value = n.ssid;
                    wifiPass.focus();
                  },
                }, h('span', {}, `${n.secure ? '🔒 ' : ''}${n.ssid}`), h('span', { class: 'muted' }, `${n.rssi} dBm · ${n.band} GHz`))));
              return;
            }
            await sleep(1500);
          }
          wifiMsg.textContent = 'The scan did not finish. Try again.';
        } catch (e) {
          wifiMsg.textContent = (e as Error).message;
        } finally {
          scanBtn.disabled = false;
        }
      },
    }, 'Scan');
    const connectBtn: HTMLButtonElement = h('button', {
      class: 'btn btn-primary',
      onclick: async () => {
        const ssid = wifiSsid.value.trim();
        if (!ssid) {
          wifiSsid.setCustomValidity('Choose or type a network');
          wifiSsid.reportValidity();
          return;
        }
        wifiSsid.setCustomValidity('');
        if (wifiPass.value && wifiPass.value.length < 8) {
          wifiPass.setCustomValidity('At least 8 characters');
          wifiPass.reportValidity();
          return;
        }
        wifiPass.setCustomValidity('');
        connectBtn.disabled = true;
        wifiMsg.textContent = `Connecting to ${ssid}…`;
        let lost = 0;
        try {
          await device.request('wifi_connect', { ssid, pass: wifiPass.value });
          for (let i = 0; i < 25 && alive; i++) {
            await sleep(1500);
            try {
              const s = await device.request('wifi_status');
              lost = 0;
              if (s.state === 'connected') {
                wifiMsg.textContent = `Connected to ${s.ssid}. IP address ${s.ip}. The network is saved.`;
                toast('Wi-Fi connected', 'ok');
                return;
              }
              if (s.state === 'failed') {
                wifiMsg.textContent = `${s.error}. The reader kept its previous network.`;
                return;
              }
            } catch {
              if (++lost >= 3) {
                wifiMsg.textContent = 'The connection to the reader was lost. If it joined the new network, open it at its new address (gapura.local or its IP).';
                return;
              }
            }
          }
          wifiMsg.textContent = 'No answer yet. Check the reader.';
        } catch (e) {
          wifiMsg.textContent = (e as Error).message;
        } finally {
          connectBtn.disabled = false;
        }
      },
    }, 'Connect');

    const rebootBtn: HTMLButtonElement = h('button', {
      class: 'btn btn-danger',
      onclick: () => {
        if (!confirm('Restart the reader now? The connection will drop.')) return;
        void action(rebootBtn, () => device.request('reboot'), 'Restarting…');
      },
    }, 'Restart reader');

    const sleepBtn: HTMLButtonElement = h('button', {
      class: 'btn',
      onclick: () => {
        if (!confirm('Put the reader to sleep? It stops all links. Press the RESET button on the board to wake it.')) return;
        void action(sleepBtn, () => device.request('sleep'), 'Going to sleep. Press RESET on the board to wake it.');
      },
    }, 'Sleep');

    const info = device.state.info;
    const t = device.state.transport;
    section.replaceChildren(
      ...(info?.defaultPassword
        ? [h('div', { class: 'banner banner-error' }, 'The reader still uses the default password. Change it below.')]
        : []),
      h('div', { class: 'grid-2' },
        h('article', { class: 'card' },
          h('div', { class: 'card-head' }, h('h2', {}, 'Device')),
          field('Name', name, 'Used for Bluetooth and the network hostname'),
          h('label', { class: 'field field-inline' }, beep, h('span', {}, 'Beep on scan')),
          h('label', { class: 'field' }, h('span', { class: 'field-label' }, 'LED brightness ', brightOut), bright),
          h('div', { class: 'row' }, field('Low battery at (%)', lowPct), field('Battery capacity (mAh)', cap)),
          h('div', { class: 'actions' }, saveDevice)),
        h('article', { class: 'card' },
          h('div', { class: 'card-head' }, h('h2', {}, 'Wi-Fi (manual)')),
          field('Wi-Fi network', ssid),
          field('Wi-Fi password', pass),
          h('div', { class: 'actions' }, saveNet)),
        h('article', { class: 'card' },
          h('div', { class: 'card-head' }, h('h2', {}, 'Bluetooth passkey')),
          field('Passkey', passkey, 'Six digits, entered on the phone or PC when pairing. Saved apart from Wi-Fi.'),
          h('div', { class: 'actions' }, savePasskey))),
      h('article', { class: 'card' },
        h('div', { class: 'card-head' }, h('h2', {}, 'Hardware test')),
        hwMsg,
        h('p', { class: 'field-label' }, 'LED colour (onboard RGB and external LED)'),
        h('div', { class: 'actions' }, ...colorBtns),
        h('div', { class: 'row' }, field('Status look', looks)),
        h('div', { class: 'actions' }, lookBtn),
        h('p', { class: 'field-label' }, 'Buzzer'),
        h('div', { class: 'actions' }, beepBtn, longBtn)),
      h('article', { class: 'card' },
        h('div', { class: 'card-head' }, h('h2', {}, 'Card reader')),
        h('p', { class: 'muted small' }, 'Pick the reader modules wired to the board (wiring: docs/wiring). Applied after a restart.'),
        field('Reader', readerMode),
        readerHint,
        h('div', { class: 'actions' }, saveReader)),
      h('article', { class: 'card' },
        h('div', { class: 'card-head' }, h('h2', {}, 'Wi-Fi networks'), scanBtn),
        h('p', { class: 'muted small' }, 'Scan, pick a network and connect. The reader saves it and goes back to its previous network if this one fails. Scanning and switching network over Wi-Fi interrupt this connection; use USB or Bluetooth to follow the result.'),
        wifiMsg,
        wifiList,
        h('div', { class: 'row' }, field('Network', wifiSsid), field('Password', wifiPass)),
        h('div', { class: 'actions' }, connectBtn)),
      h('article', { class: 'card' },
        h('div', { class: 'card-head' }, h('h2', {}, 'Bluetooth mode')),
        h('p', { class: 'muted small' }, 'Keyboard mode pairs like a Bluetooth keyboard (passkey from above) and types the card text into any app. The dashboard then works over Wi-Fi or USB only.'),
        field('Mode', mode),
        h('div', { class: 'row' }, field('Type', kbSource), field('Block (1–62)', kbBlock)),
        h('div', { class: 'row' }, field('Key (12 hex)', kbKey), field('Key type', kbType)),
        h('label', { class: 'field field-inline' }, kbEnter, h('span', {}, 'Press Enter after the text')),
        h('div', { class: 'actions' }, saveMode)),
      h('article', { class: 'card' },
        h('div', { class: 'card-head' }, h('h2', {}, 'Card security')),
        h('p', { class: 'muted small' }, secStatus),
        h('div', { class: 'row' }, field('Secret (32 hex)', secInput, 'Same on every reader and your server. Never stored on a card.'), field('Factory key of blank cards', secFactory)),
        secShown,
        h('label', { class: 'field field-inline' }, secEnabled, h('span', {}, 'Accept only secured cards')),
        h('div', { class: 'row' }, field('Backup passphrase', bkPass, 'Encrypts the backup file. Leave empty for a plain text file.')),
        fileInput,
        h('div', { class: 'actions' }, forgetSec, loadBtn, downloadBtn, genBtn, saveSec)),
      h('article', { class: 'card' },
        h('div', { class: 'card-head' }, h('h2', {}, 'Webhook')),
        h('p', { class: 'muted small' }, 'For fixed readers: every scanned card is sent to your server as JSON (uid and the ID text from the block above). Leave empty to turn off.'),
        h('div', { class: 'row' }, field('Server URL', whUrl), field('Token', whToken)),
        h('div', { class: 'card-head' }, h('h3', {}, 'Server replies')),
        whLog,
        h('div', { class: 'actions' }, testWh, saveWh)),
      h('article', { class: 'card' },
        h('div', { class: 'card-head' }, h('h2', {}, 'Device password')),
        h('p', { class: 'muted small' }, 'Protects Wi-Fi access (dashboard, API, updates) and the setup hotspot. Default: rfid1234.'),
        h('div', { class: 'row' }, field('New password', pw), field('Confirm', pw2)),
        h('div', { class: 'actions' }, savePw)),
      h('article', { class: 'card' },
        h('div', { class: 'card-head' }, h('h2', {}, 'About')),
        h('dl', { class: 'facts' },
          ...fact('Firmware', info?.fw),
          ...fact('IP address', info?.ip || '–'),
          ...fact('Wi-Fi signal', info?.rssi ? `${info.rssi} dBm` : '–'),
          ...fact('Free memory', info ? `${Math.round(info.heap / 1024)} KB` : '–'),
          ...fact('Reader mode', info?.reader ? READER_MODES.find((m) => m.mode === info.reader.mode)?.label ?? info.reader.mode : '–'),
          ...(info?.reader && !info.reader.hf ? [] : fact(info?.reader?.hf ?? 'RC522', info?.rc522 ? 'OK' : 'Not detected')),
          ...fact('INA219 (optional)', info?.ina219 ? 'OK' : 'Not fitted')),
        h('p', { class: 'muted small' }, 'On the board: press BOOT twice quickly to sleep, press RESET to wake. To forget the Wi-Fi network, hold BOOT for 10 seconds until two beeps, release, then press BOOT once within 5 seconds.'),
        h('div', { class: 'actions' }, sleepBtn, rebootBtn)),
      ...(t instanceof WsTransport ? [updateCard(t)] : []),
    );
  }

  async function save(btn: HTMLButtonElement, patch: DeepPartial<Config>, msg: string, reload = false) {
    const ok = await action(btn, () => device.request('config_set', patch).then(() => true), msg);
    if (reload) await load();
    return ok;
  }

  return () => {
    alive = false;
    offWebhook();
  };
}

function updateCard(t: WsTransport) {
  const file = h('input', { type: 'file', accept: '.bin' });
  const target = h('select', {}, h('option', { value: 'fw' }, 'Firmware (firmware.bin)'), h('option', { value: 'fs' }, 'Dashboard (littlefs.bin)'));
  const progress = h('progress', { max: 100, value: 0, hidden: true, class: 'progress' });
  const btn: HTMLButtonElement = h('button', {
    class: 'btn btn-primary',
    onclick: () => {
      const f = file.files?.[0];
      if (!f) return file.click();
      void action(btn, () => upload(t, f, target.value === 'fs', progress), 'Update installed. The reader is restarting.');
    },
  }, 'Upload');
  return h('article', { class: 'card' },
    h('div', { class: 'card-head' }, h('h2', {}, 'Update over Wi-Fi')),
    h('p', { class: 'muted small' }, 'Build with PlatformIO, then pick the .bin from firmware/.pio/build/esp32c5/. Keep the reader powered during the update.'),
    h('div', { class: 'row' }, field('File', file), field('Target', target)),
    progress,
    h('div', { class: 'actions' }, btn));
}

// XMLHttpRequest instead of fetch: it reports upload progress.
function upload(t: WsTransport, f: File, fs: boolean, progress: HTMLProgressElement) {
  return new Promise<void>((resolve, reject) => {
    const xhr = new XMLHttpRequest();
    xhr.open('POST', `${location.protocol === 'https:' ? 'https' : 'http'}://${t.host}/api/ota${fs ? '?target=fs' : ''}`);
    xhr.setRequestHeader('Authorization', `Bearer ${t.token}`);
    progress.hidden = false;
    xhr.upload.onprogress = (e) => e.lengthComputable && (progress.value = (e.loaded / e.total) * 100);
    xhr.onload = () => (xhr.status === 200 ? resolve() : reject(new Error(`Update failed (HTTP ${xhr.status})`)));
    xhr.onerror = () => reject(new Error('Upload failed'));
    const body = new FormData();
    body.append('file', f);
    xhr.send(body);
  });
}

const fact = (k: string, v?: string) => [h('dt', {}, k), h('dd', {}, v ?? '–')];
