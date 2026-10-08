import { device } from '../device';
import { HEX_BLOCK, HEX_KEY, isTrailer, isWritable, type KeyArgs, type SectorDump } from '../protocol';
import { action, field, h, hexToText, toast } from '../ui';

export function mount(root: HTMLElement) {
  const key = h('input', { value: 'FFFFFFFFFFFF', maxLength: 12, class: 'mono', spellcheck: false });
  const keyType = h('select', {}, h('option', { value: 'A' }, 'Key A'), h('option', { value: 'B' }, 'Key B'));
  const keyArgs = (): KeyArgs | null => {
    const k = key.value.trim();
    if (!HEX_KEY.test(k)) {
      key.setCustomValidity('12 hex characters');
      key.reportValidity();
      return null;
    }
    key.setCustomValidity('');
    return { key: k.toUpperCase(), keyType: keyType.value as 'A' | 'B' };
  };

  // Read / write one block
  const block = h('input', { type: 'number', min: 0, max: 63, value: 4 });
  const data = h('input', { class: 'mono', maxLength: 32, placeholder: '32 hex characters', spellcheck: false });
  const ascii = h('p', { class: 'muted small mono ascii' });
  const warn = h('p', { class: 'warn small' });
  const readBtn: HTMLButtonElement = h('button', { class: 'btn', onclick: () => void read() }, 'Read');
  const writeBtn: HTMLButtonElement = h('button', { class: 'btn btn-primary', onclick: () => void write() }, 'Write');

  const updateBlockState = () => {
    const b = Number(block.value);
    const ok = isWritable(b);
    writeBtn.disabled = !ok;
    warn.textContent = b === 0 ? 'Block 0 holds the UID and is read-only.' : isTrailer(b) ? 'Sector trailer: holds keys and access bits. Writing is blocked to protect the card.' : '';
    ascii.textContent = HEX_BLOCK.test(data.value) ? `Text: ${hexToText(data.value)}` : '';
  };
  block.addEventListener('input', updateBlockState);
  data.addEventListener('input', updateBlockState);

  async function read() {
    const k = keyArgs();
    if (!k) return;
    const r = await action(readBtn, () => device.request('read_block', { block: Number(block.value), ...k }));
    if (r) {
      data.value = r.hex;
      updateBlockState();
    }
  }

  async function write() {
    const k = keyArgs();
    if (!k) return;
    if (!HEX_BLOCK.test(data.value)) {
      data.setCustomValidity('Exactly 32 hex characters (16 bytes)');
      data.reportValidity();
      return;
    }
    data.setCustomValidity('');
    await action(writeBtn, () => device.request('write_block', { block: Number(block.value), hex: data.value.toUpperCase(), ...k }), `Block ${block.value} written`);
  }

  // Write text
  const startBlock = h('input', { type: 'number', min: 1, max: 62, value: 4 });
  const text = h('textarea', { rows: 2, maxLength: 720, placeholder: 'Text to store on the card' });
  const textBtn: HTMLButtonElement = h('button', {
    class: 'btn btn-primary',
    onclick: async () => {
      const k = keyArgs();
      if (!k) return;
      const r = await action(textBtn, () => device.request('write_text', { startBlock: Number(startBlock.value), text: text.value, ...k }));
      if (r) toast(`Written to blocks ${r.blocks.join(', ')}`, 'ok');
    },
  }, 'Write text');

  // Secured card (card security: per-card keys + signed credential, docs/PROTOCOL.md)
  const secFactory = h('input', { class: 'mono', maxLength: 12, placeholder: 'Default from Settings', spellcheck: false, autocomplete: 'off' });
  const secOld = h('input', { class: 'mono', maxLength: 32, placeholder: '32 hex characters', spellcheck: false, autocomplete: 'off', type: 'password' });
  const secOut = h('p', { class: 'muted small mono' });
  const credText = h('input', { maxLength: 95, placeholder: 'Credential, up to 95 characters', autocomplete: 'off', spellcheck: false });
  const credWriteBtn: HTMLButtonElement = h('button', {
    class: 'btn btn-primary',
    onclick: async () => {
      const t = credText.value.trim();
      if (!t) {
        credText.setCustomValidity('Enter the credential');
        credText.reportValidity();
        return;
      }
      credText.setCustomValidity('');
      const fk = secFactory.value.trim();
      if (fk && !HEX_KEY.test(fk)) {
        secFactory.setCustomValidity('12 hex characters');
        secFactory.reportValidity();
        return;
      }
      secFactory.setCustomValidity('');
      const r = await action(credWriteBtn, () => device.request('card_write_credential', fk ? { text: t, factoryKey: fk.toUpperCase() } : { text: t }));
      if (r) {
        secOut.textContent = `Credential written: ${r.text} (UID ${r.uid})`;
        toast('Credential written', 'ok');
      }
    },
  }, 'Write credential');
  const credReadBtn: HTMLButtonElement = h('button', {
    class: 'btn',
    onclick: async () => {
      const r = await action(credReadBtn, () => device.request('card_read_credential'));
      if (r) {
        credText.value = r.text;
        secOut.textContent = `Valid credential: ${r.text} (UID ${r.uid})`;
      }
    },
  }, 'Read credential');
  const secResetBtn: HTMLButtonElement = h('button', {
    class: 'btn btn-danger',
    onclick: async () => {
      if (!confirm('Reset this card to factory keys? Its ID and signature are erased, and any reader can write it again.')) return;
      const fk = secFactory.value.trim();
      if (fk && !HEX_KEY.test(fk)) {
        secFactory.setCustomValidity('12 hex characters');
        secFactory.reportValidity();
        return;
      }
      secFactory.setCustomValidity('');
      const os = secOld.value.trim();
      if (os && !/^[0-9a-fA-F]{32}$/.test(os)) {
        secOld.setCustomValidity('32 hex characters');
        secOld.reportValidity();
        return;
      }
      secOld.setCustomValidity('');
      const args: { factoryKey?: string; secret?: string } = {};
      if (fk) args.factoryKey = fk.toUpperCase();
      if (os) args.secret = os.toUpperCase();
      const r = await action(secResetBtn, () => device.request('card_reset', args));
      if (r) {
        secOut.textContent = `Card reset to factory keys (UID ${r.uid})`;
        toast('Card reset', 'ok');
      }
    },
  }, 'Reset to factory');

  // Dump
  const grid = h('div', { class: 'dump' });
  const dumpBtn: HTMLButtonElement = h('button', { class: 'btn', onclick: () => void dump() }, 'Read whole card');

  async function dump() {
    const k = keyArgs();
    if (!k) return;
    const r = await action(dumpBtn, () => device.request('dump', k));
    if (r) renderDump(r.sectors);
  }

  function renderDump(sectors: SectorDump[]) {
    grid.replaceChildren(
      ...sectors.map((s) =>
        h('div', { class: 'sector' },
          h('div', { class: 'sector-head' }, `Sector ${s.sector}`),
          'error' in s
            ? h('div', { class: 'sector-error' }, s.error === 'AUTH_FAILED' ? 'Different key' : s.error)
            : h('div', {},
                ...s.blocks.map((hex, i) => {
                  const n = s.sector * 4 + i;
                  return h('button', {
                    class: `dump-row ${isTrailer(n) ? 'is-trailer' : ''} ${n === 0 ? 'is-uid' : ''}`,
                    title: `Block ${n}: ${hexToText(hex)}`,
                    onclick: () => {
                      block.value = String(n);
                      data.value = hex;
                      updateBlockState();
                      block.scrollIntoView({ behavior: 'smooth', block: 'center' });
                    },
                  }, h('span', { class: 'dump-n' }, String(n)), h('span', { class: 'mono' }, hex.replace(/(.{8})/g, '$1 ').trim()));
                }))),
      ),
    );
  }

  root.replaceChildren(
    h('section', { class: 'stack' },
      h('div', { class: 'banner' }, 'Hold the card on the reader during every operation. Only MIFARE Classic cards support block access.'),
      h('article', { class: 'card' },
        h('div', { class: 'card-head' }, h('h2', {}, 'Key')),
        h('div', { class: 'row' }, field('Key (hex)', key, 'Factory default is FFFFFFFFFFFF'), field('Type', keyType))),
      h('div', { class: 'grid-2' },
        h('article', { class: 'card' },
          h('div', { class: 'card-head' }, h('h2', {}, 'Block')),
          h('div', { class: 'row' }, field('Block (0–63)', block), field('Data', data)),
          ascii, warn,
          h('div', { class: 'actions' }, readBtn, writeBtn)),
        h('article', { class: 'card' },
          h('div', { class: 'card-head' }, h('h2', {}, 'Text')),
          h('div', { class: 'row' }, field('Start block', startBlock, 'Trailers are skipped'), field('Text', text)),
          h('div', { class: 'actions' }, textBtn))),
      h('article', { class: 'card' },
        h('div', { class: 'card-head' }, h('h2', {}, 'Secured card')),
        h('p', { class: 'muted small' }, 'Needs a card secret in Settings. The credential is locked with its own per-card keys and signed, so only readers with the same secret accept it. The webhook sends it so your server can compare it with its database. Prefer an opaque identifier (employee or student number) over personal data. A blank card is opened with the factory key.'),
        field('Credential', credText, 'Up to 95 characters'),
        h('div', { class: 'row' }, field('Factory key (optional)', secFactory, 'Only for a blank card; default from Settings'), field('Old secret (optional)', secOld, 'Reset only: the secret that locked the card, if it is not the one set on this reader')),
        secOut,
        h('div', { class: 'actions' }, secResetBtn, credReadBtn, credWriteBtn)),
      h('article', { class: 'card' },
        h('div', { class: 'card-head' }, h('h2', {}, 'Card memory'), dumpBtn),
        h('p', { class: 'muted small legend' }, h('span', { class: 'chip-uid' }, 'UID'), h('span', { class: 'chip-trailer' }, 'Trailer'), 'Click a block to edit it.'),
        grid)),
  );
  updateBlockState();
  return () => {};
}
