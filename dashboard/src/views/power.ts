import { device } from '../device';
import type { ChargeState } from '../protocol';
import { h, icon } from '../ui';

const STATE_LABEL: Record<ChargeState, string> = {
  charging: 'Charging',
  full: 'Fully charged',
  discharging: 'On battery',
  fault: 'Battery fault',
};

export const levelClass = (pct: number, state: ChargeState) =>
  state === 'charging' || state === 'full' ? 'lvl-charge' : pct < 5 ? 'lvl-critical' : pct < 15 ? 'lvl-low' : 'lvl-ok';

function ring(pct: number, cls: string) {
  const r = 52;
  const c = 2 * Math.PI * r;
  const wrap = h('div', { class: `ring ${cls}` });
  wrap.innerHTML = `<svg viewBox="0 0 120 120" role="img" aria-label="Battery ${pct}%">
    <circle cx="60" cy="60" r="${r}" class="ring-track"/>
    <circle cx="60" cy="60" r="${r}" class="ring-value" stroke-dasharray="${c}" stroke-dashoffset="${c * (1 - pct / 100)}"/></svg>`;
  wrap.append(h('div', { class: 'ring-label' }, h('span', { class: 'ring-pct' }, `${pct}`), h('span', { class: 'ring-unit' }, '%')));
  return wrap;
}

function sparkline(values: number[], label: string, unit: string, digits: number) {
  const w = 320;
  const ht = 72;
  const card = h('div', { class: 'spark' });
  const last = values.at(-1);
  card.append(h('div', { class: 'spark-head' }, h('span', { class: 'muted small' }, label), h('span', { class: 'spark-value' }, last === undefined ? '–' : `${last.toFixed(digits)} ${unit}`)));
  if (values.length < 2) {
    card.append(h('div', { class: 'spark-empty muted small' }, 'Collecting data…'));
    return card;
  }
  const min = Math.min(...values);
  const max = Math.max(...values);
  const span = max - min || 1;
  const pts = values.map((v, i) => `${((i / (values.length - 1)) * w).toFixed(1)},${(ht - 4 - ((v - min) / span) * (ht - 8)).toFixed(1)}`).join(' ');
  const svg = h('div', { class: 'spark-chart' });
  svg.innerHTML = `<svg viewBox="0 0 ${w} ${ht}" preserveAspectRatio="none" aria-hidden="true">
    <polyline points="0,${ht} ${pts} ${w},${ht}" class="spark-area"/><polyline points="${pts}" class="spark-line"/></svg>`;
  card.append(svg, h('div', { class: 'spark-axis muted small' }, h('span', {}, `${min.toFixed(digits)}`), h('span', {}, '10 min'), h('span', {}, `${max.toFixed(digits)}`)));
  return card;
}

export function mount(root: HTMLElement) {
  const section = h('section', { class: 'stack' });
  root.replaceChildren(section);

  function render() {
    const b = device.state.battery;
    const info = device.state.info;
    if (!b) {
      section.replaceChildren(h('article', { class: 'card empty' }, 'Waiting for the first battery reading…'));
      return;
    }
    if (b.state === 'fault') {
      section.replaceChildren(h('article', { class: 'card empty' }, `No battery detected (${b.v.toFixed(2)} V). Plug a 3.7 V cell into the MX1.25 battery header.`));
      return;
    }
    const hist = device.state.batteryHistory;
    const cls = levelClass(b.pct, b.state);
    // Voltage comes from the board ADC; current and power need the optional INA219.
    const haveCurrent = info?.ina219 === true;
    const direction = b.mA < 0 ? 'into the battery' : 'from the battery';
    section.replaceChildren(
      h('article', { class: 'card power-hero' },
        ring(b.pct, cls),
        h('div', { class: 'power-meta' },
          h('p', { class: `state-pill ${cls}` }, b.state === 'charging' ? icon('bolt') : null, STATE_LABEL[b.state]),
          h('div', { class: 'stats' },
            stat('Voltage', `${b.v.toFixed(2)} V`),
            ...(haveCurrent
              ? [stat('Current', `${Math.abs(b.mA).toFixed(0)} mA`, direction), stat('Power', `${Math.abs(b.mW / 1000).toFixed(2)} W`)]
              : [stat('Charge', STATE_LABEL[b.state], 'estimated from the voltage trend')])))),
      h('div', { class: haveCurrent ? 'grid-2' : 'stack' },
        h('article', { class: 'card' }, sparkline(hist.map((p) => p.v), 'Voltage', 'V', 2)),
        ...(haveCurrent ? [h('article', { class: 'card' }, sparkline(hist.map((p) => p.mA), 'Current (+ discharge / − charge)', 'mA', 0))] : [])),
    );
  }

  render();
  return device.subscribe(render);
}

const stat = (label: string, value: string, hint?: string) =>
  h('div', { class: 'stat' }, h('span', { class: 'muted small' }, label), h('span', { class: 'stat-value' }, value), hint ? h('span', { class: 'muted small' }, hint) : null);
