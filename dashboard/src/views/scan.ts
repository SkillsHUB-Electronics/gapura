import { device } from '../device';
import { action, h, icon, time, toast } from '../ui';

export function mount(root: HTMLElement) {
  const current = h('div', { class: 'card tag-hero' });
  const list = h('ul', { class: 'tag-list' });
  const count = h('span', { class: 'badge' });
  const toggle: HTMLButtonElement = h('button', { class: 'btn', onclick: () => void setScanning(!device.state.info?.scanning) });

  async function setScanning(on: boolean) {
    await action(toggle, async () => {
      await device.request(on ? 'scan_start' : 'scan_stop');
      if (device.state.info) device.state.info.scanning = on;
      render();
    });
  }

  const copy = (uid: string) =>
    navigator.clipboard?.writeText(uid).then(
      () => toast(`Copied ${uid}`, 'ok'),
      () => toast('Clipboard not available', 'error'),
    );

  function render() {
    const { present, tags, info } = device.state;
    const scanning = info?.scanning ?? true;
    toggle.textContent = scanning ? 'Pause scanning' : 'Resume scanning';

    current.className = `card tag-hero ${present ? 'is-present' : ''}`;
    current.replaceChildren(
      h('div', { class: 'tag-pulse' }, icon('scan', 'icon icon-xl')),
      present
        ? h('div', {},
            h('p', { class: 'eyebrow' }, 'Card on reader'),
            h('p', { class: 'uid' }, present.uid),
            h('p', { class: 'muted' }, present.type ?? ''),
            h('button', { class: 'btn btn-ghost', onclick: () => copy(present.uid) }, icon('copy'), 'Copy UID'))
        : h('div', {},
            h('p', { class: 'eyebrow' }, scanning ? 'Waiting' : 'Paused'),
            h('p', { class: 'uid muted' }, scanning ? 'Tap a card' : 'Scanning is paused'),
            h('p', { class: 'muted' }, 'MIFARE Classic 1K, Ultralight and NTAG (13.56 MHz)')),
    );

    count.textContent = String(tags.length);
    list.replaceChildren(
      ...(tags.length
        ? tags.map((t) =>
            h('li', { class: 'tag-row' },
              h('span', { class: 'mono' }, t.uid),
              h('span', { class: 'muted' }, t.type ?? ''),
              h('span', { class: 'muted small' }, time(t.at)),
              h('button', { class: 'icon-btn', title: 'Copy UID', 'aria-label': `Copy ${t.uid}`, onclick: () => copy(t.uid) }, icon('copy'))))
        : [h('li', { class: 'empty' }, 'No cards yet. Scans appear here in real time.')]),
    );
  }

  root.replaceChildren(
    h('section', { class: 'stack' },
      current,
      h('article', { class: 'card' },
        h('div', { class: 'card-head' },
          h('h2', {}, 'Scan history ', count),
          h('div', { class: 'actions' },
            toggle,
            h('button', { class: 'btn btn-ghost', onclick: () => { device.state.tags = []; render(); } }, 'Clear'))),
        list)),
  );
  render();
  return device.subscribe(render);
}
