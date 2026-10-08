// App shell: top bar, navigation, hash routing. Views live in ./views.
import './style.css';
import { device } from './device';
import { MockTransport } from './transport/mock';
import { h, icon } from './ui';
import * as cardView from './views/card';
import * as connectView from './views/connect';
import * as powerView from './views/power';
import { levelClass } from './views/power';
import * as scanView from './views/scan';
import * as settingsView from './views/settings';

const ROUTES = {
  scan: { label: 'Scan', icon: 'scan', view: scanView },
  card: { label: 'Card', icon: 'card', view: cardView },
  power: { label: 'Power', icon: 'power', view: powerView },
  settings: { label: 'Settings', icon: 'settings', view: settingsView },
} as const;
type Route = keyof typeof ROUTES;

const app = document.getElementById('app')!;
const bar = h('div', { class: 'topbar-status' });
const nav = h('nav', { class: 'nav', 'aria-label': 'Sections' });
const main = h('main', { class: 'main', tabIndex: -1 });
app.replaceChildren(
  h('header', { class: 'topbar' },
    h('div', { class: 'brand' }, h('span', { class: 'brand-mark' }, icon('scan')), h('span', {}, 'Gapura')),
    bar),
  nav,
  main,
);

let cleanup: () => void = () => {};
let mounted = '';

const route = (): Route => {
  const r = location.hash.replace('#/', '') as Route;
  return r in ROUTES ? r : 'scan';
};

function renderShell() {
  const { transport, info, battery } = device.state;
  document.body.classList.toggle('is-connected', !!transport);

  bar.replaceChildren(
    ...(transport
      ? [
          battery
            ? h('span', { class: `chip ${levelClass(battery.pct, battery.state)}`, title: `${battery.v.toFixed(2)} V` },
                battery.state === 'charging' ? icon('bolt') : icon('power'), `${battery.pct}%`)
            : null,
          h('span', { class: 'chip chip-link', title: info?.name ?? '' },
            icon(transport.kind === 'mock' ? 'demo' : transport.kind), transport.label),
          h('button', { class: 'icon-btn', title: 'Disconnect', 'aria-label': 'Disconnect', onclick: () => void device.disconnect() }, icon('logout')),
        ].filter((el) => el !== null)
      : [h('span', { class: 'chip chip-off' }, 'Not connected')]),
  );

  const current = route();
  nav.replaceChildren(
    ...(Object.entries(ROUTES) as [Route, (typeof ROUTES)[Route]][]).map(([key, r]) =>
      h('a', { href: `#/${key}`, class: `nav-item ${key === current ? 'is-active' : ''}`, 'aria-current': key === current ? 'page' : undefined },
        icon(r.icon), h('span', {}, r.label))),
  );
  nav.hidden = !transport;
}

function renderView() {
  const key = device.connected ? route() : 'connect';
  if (key === mounted) return;
  cleanup();
  mounted = key;
  const view = key === 'connect' ? connectView : ROUTES[key].view;
  cleanup = view.mount(main) as () => void;
  main.focus({ preventScroll: true });
}

device.subscribe(() => {
  renderShell();
  renderView();
});
window.addEventListener('hashchange', () => {
  renderShell();
  renderView();
});
renderShell();
renderView();

// Development convenience: ?demo connects to the simulated reader.
if (new URLSearchParams(location.search).has('demo')) {
  void device.connect(new MockTransport());
}
