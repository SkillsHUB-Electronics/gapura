import { device } from '../device';
import { supports, type Transport } from '../transport';
import { BleTransport } from '../transport/ble';
import { MockTransport } from '../transport/mock';
import { SerialTransport } from '../transport/serial';
import { WsTransport } from '../transport/ws';
import { action, field, h, icon } from '../ui';

export const store = {
  get: (k: string) => {
    try {
      return localStorage.getItem(k) ?? '';
    } catch {
      return '';
    }
  },
  set: (k: string, v: string) => {
    try {
      localStorage.setItem(k, v);
    } catch {
      /* private mode */
    }
  },
};

// Served from the device itself -> its own host; otherwise the mDNS name.
const servedByDevice = !['localhost', '127.0.0.1'].includes(location.hostname) && location.protocol === 'http:';

export function mount(root: HTMLElement) {
  const host = h('input', { value: store.get('host') || (servedByDevice ? location.host : 'gapura.local'), autocomplete: 'off', spellcheck: false });
  const token = h('input', { type: 'password', value: store.get('token'), autocomplete: 'off', placeholder: 'Default: rfid1234' });

  const go = (btn: HTMLButtonElement, make: () => Transport) =>
    action(btn, async () => {
      await device.connect(make());
    });

  const wifiBtn: HTMLButtonElement = h('button', {
    class: 'btn btn-primary',
    onclick: () => {
      store.set('host', host.value.trim());
      store.set('token', token.value.trim());
      void go(wifiBtn, () => new WsTransport(host.value.trim(), token.value.trim()));
    },
  }, 'Connect');

  const option = (name: 'ble' | 'usb' | 'demo', title: string, text: string, available: boolean, make: () => Transport, note?: string) => {
    const btn: HTMLButtonElement = h('button', { class: 'btn', disabled: !available, onclick: () => void go(btn, make) }, available ? 'Connect' : 'Not supported');
    return h('article', { class: 'card link-card' },
      h('div', { class: 'link-head' }, icon(name, 'icon icon-lg'), h('div', {}, h('h3', {}, title), h('p', { class: 'muted' }, text))),
      !available && note ? h('p', { class: 'muted small' }, note) : null,
      btn);
  };

  root.replaceChildren(
    h('section', { class: 'connect' },
      h('header', { class: 'connect-hero' },
        h('h1', {}, 'Connect to your reader'),
        h('p', { class: 'muted' }, 'Choose how this device talks to the Gapura reader. All three links speak the same protocol.')),
      device.state.error ? h('div', { class: 'banner banner-error' }, device.state.error) : null,
      h('div', { class: 'link-grid' },
        h('article', { class: 'card link-card link-card-wide' },
          h('div', { class: 'link-head' }, icon('wifi', 'icon icon-lg'), h('div', {}, h('h3', {}, 'Wi-Fi'), h('p', { class: 'muted' }, 'Same network as the reader. Default password is rfid1234.'))),
          h('div', { class: 'row' }, field('Address', host), field('Device password', token)),
          wifiBtn),
        option('ble', 'Bluetooth', 'Pair with the 6-digit passkey (default 123456).', supports.ble, () => new BleTransport(), 'Needs Chrome or Edge. On iPhone use the Bluefy browser.'),
        option('usb', 'USB cable', 'Plug the reader into this computer.', supports.usb, () => new SerialTransport(), 'Needs Chrome or Edge on a desktop.'),
        option('demo', 'Demo', 'Try the dashboard with a simulated reader.', true, () => new MockTransport()))),
  );
  return () => {};
}
