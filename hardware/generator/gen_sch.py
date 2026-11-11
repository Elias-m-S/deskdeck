#!/usr/bin/env python3
"""Generates the schematic (.kicad_sch) and the project symbol library from design.py (KiCad 7 format).

    python3 gen_sch.py <output dir>
"""
import datetime
import math
import os
import sys
import uuid

sys.path.insert(0, os.path.dirname(__file__))
import design as D  # noqa: E402
import sexp  # noqa: E402
from sexp import Sym  # noqa: E402

NAME = 'deskdeck'
OUT = sys.argv[1] if len(sys.argv) > 1 else 'out'
SYMDIR = '/usr/share/kicad/symbols'
NS = uuid.NAMESPACE_URL
ROOT = str(uuid.uuid5(NS, f'{NAME}/root-sheet'))
_cnt = [0]


def uid(tag=None):
    _cnt[0] += 1
    return str(uuid.uuid5(NS, f'{NAME}/sch/{tag or _cnt[0]}'))


def font(size=1.27, hide=False, justify=None):
    e = [Sym('effects'), [Sym('font'), [Sym('size'), size, size]]]
    if justify:
        e.append([Sym('justify')] + [Sym(j) for j in justify])
    if hide:
        e.append(Sym('hide'))
    return e


# --------------------------------------------------------------------------- symbols

def lib_symbol(libname, name):
    root = sexp.parse(open(f'{SYMDIR}/{libname}.kicad_sym').read())
    for s in sexp.find(root, 'symbol'):
        if s[1] == name:
            s = list(s)
            s[1] = f'{libname}:{name}'
            return s
    raise KeyError(f'{libname}:{name}')


def supermini_symbol(full_name):
    """Custom symbol: ESP32-S3 SuperMini, left column pins 1-9, right column pins 10-18."""
    names_l = ['IO43/TX', 'IO44/RX', 'IO1', 'IO2', 'IO3', 'IO4', 'IO5', 'IO6', 'IO7']
    names_r = ['5V', 'GND', '3V3', 'IO13', 'IO12', 'IO11', 'IO10', 'IO9', 'IO8']
    body = [Sym('symbol'), 'ESP32-S3_SuperMini_0_1',
            [Sym('rectangle'), [Sym('start'), -10.16, 12.7], [Sym('end'), 10.16, -12.7],
             [Sym('stroke'), [Sym('width'), 0.254], [Sym('type'), Sym('default')]], [Sym('fill'), [Sym('type'), Sym('background')]]]]
    pins = [Sym('symbol'), 'ESP32-S3_SuperMini_1_1']
    for i in range(9):
        y = 10.16 - i * 2.54
        for num, name, x, ang in ((str(i + 1), names_l[i], -15.24, 0), (str(i + 10), names_r[i], 15.24, 180)):
            pins.append([Sym('pin'), Sym('passive'), Sym('line'), [Sym('at'), x, y, ang], [Sym('length'), 5.08],
                         [Sym('name'), name, font()], [Sym('number'), num, font()]])
    return [Sym('symbol'), full_name, [Sym('in_bom'), Sym('yes')], [Sym('on_board'), Sym('yes')],
            [Sym('property'), 'Reference', 'U', [Sym('at'), 0, 15.24, 0], font()],
            [Sym('property'), 'Value', 'ESP32-S3 SuperMini', [Sym('at'), 0, -15.24, 0], font()],
            [Sym('property'), 'Footprint', f'{NAME}:ESP32-S3_SuperMini_Socket', [Sym('at'), 0, -17.78, 0], font(hide=True)],
            [Sym('property'), 'Datasheet', '~', [Sym('at'), 0, 0, 0], font(hide=True)],
            [Sym('property'), 'ki_description', 'ESP32-S3 SuperMini (4 MB flash, 2 MB PSRAM), on female headers', [Sym('at'), 0, 0, 0], font(hide=True)],
            body, pins]


def get_symbol(lib_id):
    lib, name = lib_id.split(':')
    if lib == NAME:
        return supermini_symbol(lib_id)
    return lib_symbol(lib, name)


def symbol_pins(sym):
    pins = {}

    def walk(e):
        for x in e:
            if isinstance(x, list) and x and x[0] == 'pin':
                at = sexp.first(x, 'at')
                pins[sexp.first(x, 'number')[1]] = (float(at[1]), float(at[2]), int(float(at[3])) if len(at) > 3 else 0)
            elif isinstance(x, list):
                walk(x)
    walk(sym)
    return pins


# --------------------------------------------------------------------------- placement

# Schematic position (mm, 1.27 grid) per part
SCH_POS = {
    'U1': (63.5, 76.2),
    'J1': (50.8, 127.0), 'J2': (50.8, 152.4),
    'SW1': (139.7, 50.8), 'SW2': (139.7, 76.2),
    'SW3': (127.0, 104.14), 'SW4': (127.0, 119.38), 'SW5': (127.0, 134.62), 'SW6': (127.0, 149.86),
    'R1': (205.74, 55.88), 'R3': (205.74, 68.58), 'U2': (254.0, 58.42),
    'R2': (205.74, 88.9), 'R4': (205.74, 101.6), 'U3': (254.0, 91.44),
    'J3': (264.16, 129.54), 'SW7': (215.9, 129.54), 'SW8': (215.9, 142.24),
    'H1': (25.4, 190.5), 'H2': (38.1, 190.5), 'H3': (50.8, 190.5), 'H4': (63.5, 190.5), 'H5': (76.2, 190.5),
}

STUB = 2.54
SCH_ROT = {'R1': 90, 'R2': 90, 'R3': 90, 'R4': 90}  # resistors horizontal

NOTES = [
    ('Microcontroller', 38.1, 38.1, 2.5),
    ('Display + distance sensor (I2C)', 25.4, 111.76, 2.5),
    ('OLED 0x3C, VL53L1X 0x29', 25.4, 116.84, 1.5),
    ('Encoders + MX keys', 116.84, 38.1, 2.5),
    ('Desk (galvanically isolated)', 187.96, 38.1, 2.5),
    ('DESK_* nets have NO connection to the ESP!', 187.96, 43.18, 1.5),
    ('GPIO high = LED on = button pressed.', 187.96, 111.76, 1.5),
    ('10k keeps the output off during boot.', 187.96, 115.57, 1.5),
    ('The desk switches VCC to M1/M2. Buttons and optocouplers', 187.96, 153.67, 1.5),
    ('are in parallel: the buttons work without the ESP.', 187.96, 157.48, 1.5),
    ('Mounting M3', 25.4, 182.88, 2.0),
]


def build():
    lib_syms, placed, labels, wires, ncs = {}, [], [], [], []

    for ref, val, lib_id, fpid, side, pos, pins in D.PARTS:
        if lib_id not in lib_syms:
            lib_syms[lib_id] = get_symbol(lib_id)
        sx, sy = SCH_POS[ref]
        rot = SCH_ROT.get(ref, 0)
        ca, sa = round(math.cos(math.radians(rot))), round(math.sin(math.radians(rot)))
        spins = {n: (px * ca - py * sa, px * sa + py * ca, (a + rot) % 360)
                 for n, (px, py, a) in symbol_pins(lib_syms[lib_id]).items()}
        u = str(uuid.uuid5(NS, f'{NAME}/' + ref))  # same UUID as on the board (footprint path)
        ref_dy = -8.89 if lib_id.startswith(('Device:Rotary', NAME, 'Isolator')) else -5.08
        val_dy = -ref_dy
        if lib_id == f'{NAME}:ESP32-S3_SuperMini':
            ref_dy, val_dy = -16.51, 16.51
        if lib_id.startswith('Connector_Generic'):
            ref_dy, val_dy = -5.08, 8.89
        if rot == 90:
            ref_dy, val_dy = -2.54, 2.54
        sym = [Sym('symbol'), [Sym('lib_id'), lib_id], [Sym('at'), sx, sy, rot], [Sym('unit'), 1],
               [Sym('in_bom'), Sym('yes')], [Sym('on_board'), Sym('yes')], [Sym('dnp'), Sym('no')],
               [Sym('uuid'), u],
               [Sym('property'), 'Reference', ref, [Sym('at'), sx, sy + ref_dy, rot], font()],
               [Sym('property'), 'Value', val, [Sym('at'), sx, sy + val_dy, rot], font()],
               [Sym('property'), 'Footprint', fpid, [Sym('at'), sx, sy, 0], font(hide=True)],
               [Sym('property'), 'Datasheet', '~', [Sym('at'), sx, sy, 0], font(hide=True)]]
        for num in spins:
            sym.append([Sym('pin'), num, [Sym('uuid'), uid(f'{ref}/pin/{num}')]])
        sym.append([Sym('instances'), [Sym('project'), NAME,
                                       [Sym('path'), '/' + ROOT, [Sym('reference'), ref], [Sym('unit'), 1]]]])
        placed.append(sym)

        # Stub wire + net label on every pin
        for num, (px, py, ang) in spins.items():
            cx, cy = sx + px, sy - py  # symbol y points up, schematic y points down
            net = pins.get(num)
            if net is None:
                if lib_id != 'Mechanical:MountingHole':
                    ncs.append([Sym('no_connect'), [Sym('at'), cx, cy], [Sym('uuid'), uid(f'{ref}/nc/{num}')]])
                continue
            dx, dy, lang = {0: (-STUB, 0, 180), 180: (STUB, 0, 0), 90: (0, STUB, 270), 270: (0, -STUB, 90)}[ang]
            ex, ey = cx + dx, cy + dy
            wires.append([Sym('wire'), [Sym('pts'), [Sym('xy'), cx, cy], [Sym('xy'), ex, ey]],
                          [Sym('stroke'), [Sym('width'), 0], [Sym('type'), Sym('default')]],
                          [Sym('uuid'), uid(f'{ref}/w/{num}')]])
            just = {0: ['left', 'bottom'], 180: ['right', 'bottom'], 90: ['left', 'bottom'], 270: ['right', 'bottom']}[lang]
            labels.append([Sym('label'), net, [Sym('at'), ex, ey, lang], [Sym('fields_autoplaced')],
                           font(justify=just), [Sym('uuid'), uid(f'{ref}/l/{num}')]])

    texts = [[Sym('text'), s, [Sym('at'), x, y, 0], font(size, justify=['left', 'bottom']), [Sym('uuid'), uid('t/' + s[:20])]]
             for s, x, y, size in NOTES]

    today = datetime.date.today().isoformat()
    sch = [Sym('kicad_sch'), [Sym('version'), 20230121], [Sym('generator'), Sym('eeschema')],
           [Sym('uuid'), ROOT], [Sym('paper'), 'A4'],
           [Sym('title_block'), [Sym('title'), 'DeskDeck (ESP32-S3)'], [Sym('date'), today], [Sym('rev'), '2'],
            [Sym('company'), 'Elias Sehmsdorf'],
            [Sym('comment'), 1, 'Generated from generator/design.py - edit there first']],
           [Sym('lib_symbols')] + list(lib_syms.values())]
    sch += wires + labels + ncs + placed + texts
    sch.append([Sym('sheet_instances'), [Sym('path'), '/', [Sym('page'), '1']]])
    return sch, lib_syms


def write_symbol_lib(lib_syms, path):
    lib = [Sym('kicad_symbol_lib'), [Sym('version'), 20220914], [Sym('generator'), Sym('kicad_symbol_editor')]]
    s = list(lib_syms[f'{NAME}:ESP32-S3_SuperMini'])
    s[1] = 'ESP32-S3_SuperMini'
    lib.append(s)
    open(path, 'w').write(sexp.dump(lib) + '\n')


if __name__ == '__main__':
    os.makedirs(OUT, exist_ok=True)
    sch, lib_syms = build()
    open(os.path.join(OUT, f'{NAME}.kicad_sch'), 'w').write(sexp.dump(sch) + '\n')
    write_symbol_lib(lib_syms, os.path.join(OUT, f'{NAME}.kicad_sym'))
    print('schematic written')
