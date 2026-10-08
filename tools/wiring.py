"""Generates docs/wiring/*.svg, the reader wiring of each reader mode.

Usage: python tools/wiring.py docs/wiring
"""
import sys
from html import escape

OUT = sys.argv[1]

C = {
    '3V3': '#e53935', 'GND': '#212121', '5V': '#ad1457',
    'SS': '#ef6c00', 'SCK': '#f9a825', 'MOSI': '#1e88e5', 'MISO': '#43a047',
    'RST': '#6d4c41', 'RX': '#00897b', 'EN': '#7b1fa2', 'NC': '#9e9e9e',
}
STEP = 34
FONT = "font-family='system-ui, -apple-system, Segoe UI, Roboto, sans-serif'"


class Svg:
    def __init__(self):
        self.parts = []

    def add(self, s):
        self.parts.append(s)

    def text(self, x, y, s, size=13, anchor='start', weight='normal', fill='#212121', italic=False):
        style = " font-style='italic'" if italic else ''
        self.add(f"<text x='{x}' y='{y}' font-size='{size}' text-anchor='{anchor}' "
                 f"font-weight='{weight}' fill='{fill}'{style}>{escape(s)}</text>")

    def wire(self, pts, color, width=3, dash=None):
        d = ' '.join(f'{x},{y}' for x, y in pts)
        da = f" stroke-dasharray='{dash}'" if dash else ''
        self.add(f"<polyline points='{d}' fill='none' stroke='{color}' stroke-width='{width}' "
                 f"stroke-linejoin='round' stroke-linecap='round'{da}/>")

    def dot(self, x, y, color='#212121', r=4.5):
        self.add(f"<circle cx='{x}' cy='{y}' r='{r}' fill='{color}'/>")

    def box(self, x, y, w, h, fill, stroke='#37474f', rx=10):
        self.add(f"<rect x='{x}' y='{y}' width='{w}' height='{h}' rx='{rx}' fill='{fill}' "
                 f"stroke='{stroke}' stroke-width='2'/>")

    def pin(self, x, y):
        self.add(f"<circle cx='{x}' cy='{y}' r='5' fill='#fff' stroke='#37474f' stroke-width='2'/>")

    def resistor(self, x, y, horizontal, label, color, left=False):
        """Zigzag resistor, 60 px long, starting at (x, y)."""
        n, amp, seg = 6, 7, 60 / 6
        pts = [(x, y)]
        for i in range(n):
            off = amp if i % 2 == 0 else -amp
            if horizontal:
                pts.append((x + seg * i + seg / 2, y + off))
            else:
                pts.append((x + off, y + seg * i + seg / 2))
        pts.append((x + 60, y) if horizontal else (x, y + 60))
        self.wire(pts, color, width=2.5)
        if horizontal:
            self.text(x + 30, y - 13, label, 12, 'middle', 'bold')
        else:
            if left:
                self.text(x - 14, y + 34, label, 12, 'end', 'bold')
            else:
                self.text(x + 14, y + 34, label, 12, 'start', 'bold')

    def gnd(self, x, y):
        self.wire([(x, y), (x, y + 10)], C['GND'], 2.5)
        for i, w in enumerate((14, 9, 4)):
            self.wire([(x - w, y + 10 + i * 5), (x + w, y + 10 + i * 5)], C['GND'], 2.5)

    def net(self, x, y, label, color, anchor='end'):
        w = 9 * len(label) + 14
        bx = x - w if anchor == 'end' else x
        self.add(f"<rect x='{bx}' y='{y - 11}' width='{w}' height='22' rx='11' fill='#fff' "
                 f"stroke='{color}' stroke-width='2'/>")
        self.text(bx + w / 2, y + 4.5, label, 12, 'middle', 'bold', color)

    def render(self, w, h, title):
        return (f"<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 {w} {h}' width='{w}' height='{h}' {FONT}>"
                f"<title>{escape(title)}</title><rect width='{w}' height='{h}' fill='#ffffff'/>"
                + ''.join(self.parts) + '</svg>')


BOARD_X, BOARD_W = 30, 250
PIN_X = BOARD_X + BOARD_W
MOD_X, MOD_W = 690, 230

# 13.56 MHz modules: pin name, board pin, net (None = not connected), in physical order.
HF = {
    'rc522': ('RC522 module', '13.56 MHz · 3.3 V only', [
        ('SDA', 'GPIO 23', 'SS'), ('SCK', 'GPIO 10', 'SCK'), ('MOSI', 'GPIO 8', 'MOSI'),
        ('MISO', 'GPIO 9', 'MISO'), ('IRQ', None, 'NC'), ('GND', 'GND', 'GND'),
        ('RST', 'GPIO 24', 'RST'), ('3.3V', '3V3', '3V3')]),
    'pn532': ('PN532 module (SPI)', '13.56 MHz · DIP 1 OFF, 2 ON', [
        ('SCK', 'GPIO 10', 'SCK'), ('MISO', 'GPIO 9', 'MISO'), ('MOSI', 'GPIO 8', 'MOSI'),
        ('SS', 'GPIO 23', 'SS'), ('VCC', '3V3', '3V3'), ('GND', 'GND', 'GND'),
        ('IRQ', None, 'NC'), ('RSTO', None, 'NC')]),
}


def hf_section(s, y0, kind):
    title, sub, pins = HF[kind]
    top = y0
    s.box(MOD_X, top - 46, MOD_W, STEP * len(pins) + 50, '#e3f2fd')
    s.text(MOD_X + MOD_W / 2, top - 24, title, 15, 'middle', 'bold')
    s.text(MOD_X + MOD_W / 2, top - 7, sub, 11.5, 'middle', fill='#455a64')
    rows = []
    for i, (name, board, net) in enumerate(pins):
        y = top + 16 + i * STEP
        s.pin(MOD_X, y)
        s.text(MOD_X + 14, y + 4.5, name, 13, weight='bold')
        if board:
            s.wire([(PIN_X, y), (MOD_X, y)], C[net])
            rows.append((y, board, net))
        else:
            s.text(MOD_X - 12, y + 4.5, 'not connected', 11, 'end', fill=C['NC'], italic=True)
    if kind == 'pn532':
        # DIP switch sketch: SPI = switch 1 OFF, switch 2 ON
        dx, dy = MOD_X + MOD_W - 70, top + 16 + 6 * STEP - 18
        s.add(f"<rect x='{dx}' y='{dy}' width='56' height='36' rx='4' fill='#c62828'/>")
        for j, on in enumerate((False, True)):
            kx = dx + 8 + j * 24
            s.add(f"<rect x='{kx}' y='{dy + 6}' width='16' height='24' rx='2' fill='#fff'/>")
            ky = dy + 6 if on else dy + 18
            s.add(f"<rect x='{kx + 2}' y='{ky}' width='12' height='12' rx='2' fill='#424242'/>")
        s.text(dx + 28, dy + 50, '1 OFF · 2 ON', 10.5, 'middle', fill='#455a64')
    return rows, top + STEP * len(pins) + 10


def lf_section(s, y0, switched):
    """RDM6300 with TX divider, 5 V boost and (combined modes) a power switch."""
    rows = []
    ytx = y0 + 16
    # RDM6300 module
    pins = [('TX', 'RX'), ('RX', 'NC'), ('GND', 'GND'), ('+5V', '5V')]
    s.box(MOD_X, y0 - 46, MOD_W, STEP * len(pins) + 50, '#fff3e0')
    s.text(MOD_X + MOD_W / 2, y0 - 24, 'RDM6300 module', 15, 'middle', 'bold')
    s.text(MOD_X + MOD_W / 2, y0 - 7, '125 kHz EM4100 · 5 V · P1 header', 11.5, 'middle', fill='#455a64')
    py = {}
    for i, (name, _) in enumerate(pins):
        y = ytx + i * STEP
        py[name] = y
        s.pin(MOD_X, y)
        s.text(MOD_X + 14, y + 4.5, name, 13, weight='bold')
    s.text(MOD_X - 12, py['RX'] + 4.5, 'not connected', 11, 'end', fill=C['NC'], italic=True)
    # antenna coil on the right
    ax = MOD_X + MOD_W
    s.pin(ax, py['TX'] + 10)
    s.pin(ax, py['GND'] - 6)
    s.text(ax - 12, py['TX'] + 14, 'ANT', 11, 'end', fill='#455a64')
    s.wire([(ax, py['TX'] + 10), (ax + 30, py['TX'] + 10)], '#8d6e63', 2.5)
    s.wire([(ax, py['GND'] - 6), (ax + 30, py['GND'] - 6)], '#8d6e63', 2.5)
    cy0, cy1 = py['TX'] + 10, py['GND'] - 6
    s.add(f"<ellipse cx='{ax + 42}' cy='{(cy0 + cy1) / 2}' rx='14' ry='{(cy1 - cy0) / 2 + 8}' fill='none' "
          f"stroke='#8d6e63' stroke-width='5'/>")
    s.text(ax + 42, cy1 + 34, 'coil', 11, 'middle', fill='#455a64')

    # TX -> 1k -> node -> GPIO 4, node -> 2k -> GND
    node_x = 440
    s.wire([(PIN_X, ytx), (node_x, ytx)], C['RX'])
    s.dot(node_x, ytx, C['RX'])
    s.wire([(node_x, ytx), (node_x + 50, ytx)], C['RX'])
    s.resistor(node_x + 50, ytx, True, 'R1 1 kΩ', C['RX'])
    s.wire([(node_x + 110, ytx), (MOD_X, ytx)], C['RX'])
    s.wire([(node_x, ytx), (node_x, ytx + 14)], C['RX'])
    s.resistor(node_x, ytx + 14, False, 'R2 2 kΩ', C['RX'])
    s.wire([(node_x, ytx + 74), (node_x, ytx + 84)], C['GND'], 2.5)
    s.gnd(node_x, ytx + 84)
    s.text(node_x - 60, ytx - 10, '5 V → 3.3 V', 11, 'middle', fill='#455a64', italic=True)
    rows.append((ytx, 'GPIO 4', 'RX'))

    # module GND
    s.wire([(MOD_X, py['GND']), (MOD_X - 40, py['GND']), (MOD_X - 40, py['GND'] + 6)], C['GND'], 2.5)
    s.gnd(MOD_X - 40, py['GND'] + 6)

    # 5 V boost from 3V3
    by = py['+5V'] + 70
    bx = 340
    s.box(bx, by, 150, 92, '#fce4ec', rx=8)
    s.text(bx + 75, by + 22, 'Boost 5 V', 14, 'middle', 'bold')
    s.text(bx + 75, by + 40, 'MT3608, set to 5.0 V', 11, 'middle', fill='#455a64')
    s.text(bx + 10, by + 66, 'IN+', 12, weight='bold')
    s.text(bx + 10, by + 84, 'IN−', 12, weight='bold')
    s.text(bx + 140, by + 66, 'OUT+', 12, 'end', weight='bold')
    s.text(bx + 140, by + 84, 'OUT−', 12, 'end', weight='bold')
    if switched:
        s.wire([(bx, by + 62), (bx - 20, by + 62)], C['3V3'])
        s.net(bx - 20, by + 62, '3V3', C['3V3'])
        s.wire([(bx, by + 80), (bx - 12, by + 80), (bx - 12, by + 104)], C['GND'], 2.5)
        s.gnd(bx - 12, by + 104)
    else:  # RDM6300 alone: power straight from the board pins
        s.wire([(PIN_X, by + 62), (bx, by + 62)], C['3V3'])
        s.wire([(PIN_X, by + 80), (bx, by + 80)], C['GND'])
        rows += [(by + 62, '3V3', '3V3'), (by + 80, 'GND', 'GND')]
    s.wire([(bx + 150, by + 80), (bx + 162, by + 80), (bx + 162, by + 104)], C['GND'], 2.5)
    s.gnd(bx + 162, by + 104)
    out = (bx + 150, by + 62)

    if not switched:
        s.wire([out, (620, out[1]), (620, py['+5V']), (MOD_X, py['+5V'])], C['5V'])
        s.text(628, (out[1] + py['+5V']) / 2 + 4, '5 V', 12, weight='bold', fill=C['5V'])
        return rows, by + 140

    # High-side switch: Q2 P-MOSFET (S from 5 V, D to RDM6300 +5V); Q1 NPN pulls
    # the gate low when GPIO 5 is HIGH, R4 keeps it off otherwise.
    qx, qy = 550, out[1] - 96
    s.box(qx, qy, 100, 70, '#ede7f6', rx=8)
    s.text(qx + 50, qy + 20, 'Q2 AO3401', 12.5, 'middle', 'bold')
    s.text(qx + 50, qy + 36, 'P-MOSFET', 11, 'middle', fill='#455a64')
    s.text(qx + 12, qy + 62, 'S', 12, 'middle', weight='bold')
    s.text(qx + 50, qy + 62, 'G', 12, 'middle', weight='bold')
    s.text(qx + 88, qy + 62, 'D', 12, 'middle', weight='bold')
    sx, gx, dx = qx + 12, qx + 50, qx + 88
    s.wire([out, (sx, out[1]), (sx, qy + 70)], C['5V'])
    s.text(out[0] + 10, out[1] - 8, '5 V', 12, weight='bold', fill=C['5V'])
    s.wire([(dx, qy + 70), (dx, qy + 84), (MOD_X - 20, qy + 84), (MOD_X - 20, py['+5V']), (MOD_X, py['+5V'])], C['5V'])
    # R4 pull-up from the 5 V line to the gate
    gy = out[1] + 84
    rx = out[0] + 40
    s.dot(rx, out[1], C['5V'])
    s.wire([(rx, out[1]), (rx, out[1] + 12)], C['5V'])
    s.resistor(rx, out[1] + 12, False, 'R4 10 kΩ', C['EN'])
    s.wire([(rx, out[1] + 72), (rx, gy), (gx, gy)], C['EN'])
    s.wire([(gx, qy + 70), (gx, gy)], C['EN'])
    s.dot(gx, gy, C['EN'])
    # Q1 NPN under the gate node
    q1x, q1y = gx - 50, gy + 24
    s.wire([(gx, gy), (gx, q1y)], C['EN'])
    s.box(q1x, q1y, 100, 78, '#ede7f6', rx=8)
    s.text(q1x + 50, q1y + 18, 'C', 12, 'middle', weight='bold')
    s.text(q1x + 54, q1y + 42, 'Q1 BC547', 12.5, 'middle', 'bold')
    s.text(q1x + 54, q1y + 58, 'NPN', 11, 'middle', fill='#455a64')
    s.text(q1x + 8, q1y + 34, 'B', 12, weight='bold')
    s.text(q1x + 88, q1y + 72, 'E', 12, 'middle', weight='bold')
    s.wire([(q1x + 88, q1y + 78), (q1x + 88, q1y + 94)], C['GND'], 2.5)
    s.gnd(q1x + 88, q1y + 94)
    # base: GPIO 5 -> R3 1k -> B, R5 100k from B to GND
    en_y = q1y + 40
    rows.append((en_y, 'GPIO 5', 'EN'))
    s.wire([(PIN_X, en_y), (q1x - 160, en_y)], C['EN'])
    s.resistor(q1x - 160, en_y, True, 'R3 1 kΩ', C['EN'])
    s.wire([(q1x - 100, en_y), (q1x, en_y)], C['EN'])
    s.dot(q1x - 50, en_y, C['EN'])
    s.wire([(q1x - 50, en_y), (q1x - 50, en_y + 10)], C['EN'])
    s.resistor(q1x - 50, en_y + 10, False, 'R5 100 kΩ', C['EN'], left=True)
    s.wire([(q1x - 50, en_y + 70), (q1x - 50, en_y + 78)], C['GND'], 2.5)
    s.gnd(q1x - 50, en_y + 78)
    s.text(PIN_X + 18, en_y + 22, 'HIGH = RDM6300 on', 11, fill='#455a64', italic=True)
    return rows, max(by + 140, en_y + 110)


def board(s, top, rows, bottom):
    s.box(BOARD_X, top, BOARD_W, bottom - top, '#e8f5e9')
    s.text(BOARD_X + BOARD_W / 2, top + 26, 'Waveshare ESP32-C5', 15, 'middle', 'bold')
    s.text(BOARD_X + BOARD_W / 2, top + 44, 'WIFI6-KIT (header pins)', 11.5, 'middle', fill='#455a64')
    for y, name, net in rows:
        s.pin(PIN_X, y)
        s.text(PIN_X - 14, y + 4.5, name, 13, 'end', 'bold', C[net] if net not in ('GND',) else '#212121')


def notes(s, y, lines):
    s.text(30, y, 'Notes / Catatan', 14, weight='bold')
    y += 22
    for en, idn in lines:
        s.text(30, y, '• ' + en, 12.5)
        s.text(42, y + 17, idn, 12, fill='#546e7a', italic=True)
        y += 40
    return y


def legend(s, y, nets):
    x = 30
    for n in nets:
        s.wire([(x, y), (x + 26, y)], C[n], 4)
        s.text(x + 32, y + 4.5, {'RX': 'RDM TX', 'EN': 'RDM power'}.get(n, n), 12)
        x += 32 + 8 * len({'RX': 'RDM TX', 'EN': 'RDM power'}.get(n, n)) + 30
    return y + 24


COMMON_NOTES = [
    ('LED, buzzer, battery and INA219 wiring stays as in docs/PLAN.md §1.2.',
     'Wiring LED, buzzer, baterai, dan INA219 tetap seperti docs/PLAN.id.md §1.2.'),
]
HF_NOTES = [
    ('RC522 and PN532 run on 3.3 V only from the board 3V3 pin. Fit one of them, not both.',
     'RC522 dan PN532 pakai 3,3 V dari pin 3V3 board. Pasang salah satu saja.'),
]
PN_NOTES = [
    ('PN532: set the DIP switches to SPI (1 OFF, 2 ON) before power-up.',
     'PN532: set DIP switch ke SPI (1 OFF, 2 ON) sebelum dinyalakan.'),
]
LF_NOTES = [
    ('RDM6300 needs 5 V. Its TX is 5 V: R1/R2 bring it down to 3.3 V for GPIO 4.',
     'RDM6300 butuh 5 V. TX-nya 5 V: R1/R2 menurunkannya ke 3,3 V untuk GPIO 4.'),
    ('Set the MT3608 to 5.0 V before connecting. A reader always on USB may use the board 5V pin instead.',
     'Atur MT3608 ke 5,0 V sebelum disambung. Reader yang selalu pakai USB boleh ambil dari pin 5V board.'),
    ('⏚ and 3V3 labels connect to the board GND and 3V3 pins.',
     'Label ⏚ dan 3V3 disambung ke pin GND dan 3V3 board.'),
]
COMBO_NOTES = [
    ('Readers take turns: GPIO 5 switches the RDM6300 off while the 13.56 MHz field is on.',
     'Reader bergantian: GPIO 5 mematikan RDM6300 saat antena 13,56 MHz aktif.'),
    ('Keep the two antennas at least 3 cm apart; do not stack the coil on the module.',
     'Beri jarak antena minimal 3 cm; jangan tumpuk coil di atas modul.'),
]


def diagram(name, hf, lf):
    s = Svg()
    titles = {'rc522': 'RC522', 'pn532': 'PN532'}
    title = ' + '.join(([titles[hf]] if hf else []) + (['RDM6300'] if lf else []))
    s.text(30, 38, f'Gapura wiring · {title}', 22, weight='bold')
    s.text(30, 60, f"Dashboard → Settings → Card reader: “{name}”, then restart.", 13, fill='#455a64')
    s.text(30, 77, f"Dashboard → Settings → Card reader: pilih “{name}”, lalu restart.", 12, fill='#78909c', italic=True)
    y = 150
    rows = []
    if hf:
        r, y = hf_section(s, y, hf)
        rows += r
        y += 70 if lf else 10
    if lf:
        r, y = lf_section(s, y, switched=bool(hf))
        rows += r
    rows.sort()
    board_top = 96
    board(s, board_top, rows, max(y, rows[-1][0] + 30))
    y = max(y, rows[-1][0] + 30) + 30
    nets = []
    if hf:
        nets += ['3V3', 'GND', 'SS', 'SCK', 'MOSI', 'MISO'] + (['RST'] if hf == 'rc522' else [])
    if lf:
        nets += (['3V3', 'GND'] if not hf else []) + ['5V', 'RX'] + (['EN'] if hf else [])
    y = legend(s, y, nets) + 20
    lines = (HF_NOTES if hf else []) + (PN_NOTES if hf == 'pn532' else []) + \
        (LF_NOTES if lf else []) + (COMBO_NOTES if hf and lf else []) + COMMON_NOTES
    y = notes(s, y, lines)
    return s.render(1000, int(y + 10), f'Gapura wiring: {title}')


for name, hf, lf in [('rc522', 'rc522', False), ('pn532', 'pn532', False), ('rdm6300', None, True),
                     ('rc522+rdm6300', 'rc522', True), ('pn532+rdm6300', 'pn532', True)]:
    path = f"{OUT}/{name.replace('+', '-')}.svg"
    open(path, 'w').write(diagram(name, hf, lf))
    print(path)
