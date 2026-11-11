#!/usr/bin/env python3
"""Generates the project footprints and the (unrouted) board from design.py.

    python3 gen_pcb.py <output dir>

Written against the KiCad 7 pcbnew Python API (FP_SHAPE/FP_TEXT); newer KiCad versions
renamed those classes. The committed KiCad files are the source of truth, this script is
how they were produced.
"""
import json
import math
import os
import sys
import uuid

import pcbnew

sys.path.insert(0, os.path.dirname(__file__))
import design as D  # noqa: E402

NAME = 'deskdeck'
OUT = sys.argv[1] if len(sys.argv) > 1 else 'out'
LIB_DIR = os.path.join(OUT, f'{NAME}.pretty')
SYS_FP = '/usr/share/kicad/footprints'
ORIGIN = (50.0, 50.0)  # the board sits at (50, 50) mm on the sheet


def mm(v):
    return pcbnew.FromMM(v)


def pt(x, y):
    return pcbnew.VECTOR2I(mm(ORIGIN[0] + x), mm(ORIGIN[1] + y))


def local(x, y):
    return pcbnew.VECTOR2I(mm(x), mm(y))


# --------------------------------------------------------------------------- footprints

def fp_line(fp, layer, x1, y1, x2, y2, w=0.12):
    s = pcbnew.FP_SHAPE(fp, pcbnew.SHAPE_T_SEGMENT)
    s.SetLayer(layer)
    s.SetStart0(local(x1, y1))
    s.SetEnd0(local(x2, y2))
    s.SetWidth(mm(w))
    fp.Add(s)


def fp_rect(fp, layer, x1, y1, x2, y2, w=0.12):
    for a, b, c, d in ((x1, y1, x2, y1), (x2, y1, x2, y2), (x2, y2, x1, y2), (x1, y2, x1, y1)):
        fp_line(fp, layer, a, b, c, d, w)


def fp_text(fp, layer, text, x, y, size=1.0):
    t = pcbnew.FP_TEXT(fp)
    t.SetText(text)
    t.SetLayer(layer)
    t.SetPos0(local(x, y))
    t.SetTextSize(pcbnew.VECTOR2I(mm(size), mm(size)))
    t.SetTextThickness(mm(max(0.15, size * 0.15)))  # JLCPCB: line width >= 0.15 mm
    fp.Add(t)


def tht_pad(fp, num, x, y, square=False, drill=1.0, size=1.7):
    p = pcbnew.PAD(fp)
    p.SetNumber(num)
    p.SetAttribute(pcbnew.PAD_ATTRIB_PTH)
    p.SetShape(pcbnew.PAD_SHAPE_RECT if square else pcbnew.PAD_SHAPE_CIRCLE)
    p.SetSize(local(size, size))
    p.SetDrillSize(local(drill, drill))
    p.SetLayerSet(pcbnew.PAD.PTHMask())
    p.SetPos0(local(x, y))
    p.SetPosition(local(x, y))
    fp.Add(p)


def make_supermini():
    """ESP32-S3 SuperMini on two 1x9 female headers, module top view, USB up."""
    fp = pcbnew.FOOTPRINT(None)
    fp.SetFPID(pcbnew.LIB_ID(NAME, 'ESP32-S3_SuperMini_Socket'))
    fp.SetDescription('ESP32-S3 SuperMini (Tenstar/diymore) on 2x 1x9 female headers, row pitch 15.24 mm. '
                      'MEASURE YOUR ACTUAL MODULE BEFORE ORDERING.')
    fp.Reference().SetText('U**')
    fp.Reference().SetPos0(local(0, 13.2))
    fp.Reference().SetLayer(pcbnew.F_SilkS)
    fp.Value().SetText('ESP32-S3 SuperMini')
    fp.Value().SetPos0(local(0, 0))
    fp.Value().SetLayer(pcbnew.F_Fab)
    left = ['TX', 'RX', '1', '2', '3', '4', '5', '6', '7']
    right = ['5V', 'G', '3V3', '13', '12', '11', '10', '9', '8']
    for i in range(9):
        y = -10.16 + i * 2.54
        tht_pad(fp, str(i + 1), -7.62, y, square=(i == 0))
        tht_pad(fp, str(i + 10), 7.62, y)
        fp_text(fp, pcbnew.F_SilkS, left[i], -10.9, y, 0.8)
        fp_text(fp, pcbnew.F_SilkS, right[i], 10.9, y, 0.8)
    # Module outline, USB-C socket and courtyard
    fp_rect(fp, pcbnew.F_SilkS, -9.0, -11.8, 9.0, 11.8)
    fp_line(fp, pcbnew.F_SilkS, -4.5, -11.8, -4.5, -13.0)
    fp_line(fp, pcbnew.F_SilkS, -4.5, -13.0, 4.5, -13.0)
    fp_line(fp, pcbnew.F_SilkS, 4.5, -13.0, 4.5, -11.8)
    fp_text(fp, pcbnew.F_Fab, 'USB', 0, -9.5, 0.8)
    fp_rect(fp, pcbnew.F_Fab, -9.0, -11.8, 9.0, 11.8, 0.1)
    fp_rect(fp, pcbnew.F_CrtYd, -12.5, -13.5, 12.5, 12.3, 0.05)
    return fp


def make_oled():
    """1.3" OLED module (SH1106, I2C) on a 1x4 header. Outline 35.4 x 33.5 mm."""
    fp = pcbnew.FOOTPRINT(None)
    fp.SetFPID(pcbnew.LIB_ID(NAME, 'OLED_1.3in_I2C_Socket'))
    fp.SetDescription('1.3" OLED 128x64 I2C (SH1106) on a 1x4 header. Pin order GND VCC SCL SDA - '
                      'compare with your module!')
    fp.Reference().SetText('J**')
    fp.Reference().SetPos0(local(-12, -14.5))
    fp.Reference().SetLayer(pcbnew.F_SilkS)
    fp.Value().SetText('OLED')
    fp.Value().SetPos0(local(0, 0))
    fp.Value().SetLayer(pcbnew.F_Fab)
    names = ['GND', 'VCC', 'SCL', 'SDA']
    for i in range(4):
        x = -3.81 + i * 2.54
        tht_pad(fp, str(i + 1), x, -14.75, square=(i == 0))
        fp_text(fp, pcbnew.F_SilkS, names[i], x, -12.3, 0.8)
    fp_rect(fp, pcbnew.F_Fab, -17.7, -16.75, 17.7, 16.75, 0.1)
    fp_rect(fp, pcbnew.F_Fab, -15.0, -9.0, 15.0, 9.0, 0.1)  # visible area (approximate)
    # Corners only on the silkscreen, so no line runs through pads underneath the module
    for sx in (-1, 1):
        for sy in (-1, 1):
            fp_line(fp, pcbnew.F_SilkS, sx * 17.7, sy * 16.75, sx * 14.7, sy * 16.75)
            fp_line(fp, pcbnew.F_SilkS, sx * 17.7, sy * 16.75, sx * 17.7, sy * 13.75)
    fp_rect(fp, pcbnew.F_CrtYd, -18.0, -17.0, 18.0, 17.0, 0.05)
    return fp


def build_library():
    os.makedirs(LIB_DIR, exist_ok=True)
    for f in (make_supermini(), make_oled()):
        pcbnew.FootprintSave(LIB_DIR, f)


def load_fp(fpid):
    lib, name = fpid.split(':')
    path = LIB_DIR if lib == NAME else os.path.join(SYS_FP, lib + '.pretty')
    f = pcbnew.FootprintLoad(path, name)
    if f is None:
        raise RuntimeError(f'footprint missing: {fpid}')
    f.SetFPID(pcbnew.LIB_ID(lib, name))
    return f


# --------------------------------------------------------------------------- board

def edge_cuts(board):
    W, H, R = D.BOARD_W, D.BOARD_H, D.CORNER_R

    def seg(x1, y1, x2, y2):
        s = pcbnew.PCB_SHAPE(board, pcbnew.SHAPE_T_SEGMENT)
        s.SetLayer(pcbnew.Edge_Cuts)
        s.SetWidth(mm(0.1))
        s.SetStart(pt(x1, y1))
        s.SetEnd(pt(x2, y2))
        board.Add(s)

    def arc(cx, cy, sx, sy):
        s = pcbnew.PCB_SHAPE(board, pcbnew.SHAPE_T_ARC)
        s.SetLayer(pcbnew.Edge_Cuts)
        s.SetWidth(mm(0.1))
        s.SetCenter(pt(cx, cy))
        s.SetStart(pt(sx, sy))
        s.SetArcAngleAndEnd(pcbnew.EDA_ANGLE(90, pcbnew.DEGREES_T))
        board.Add(s)

    seg(R, 0, W - R, 0)
    seg(W, R, W, H - R)
    seg(W - R, H, R, H)
    seg(0, H - R, 0, R)
    arc(W - R, R, W - R, 0)
    arc(W - R, H - R, W, H - R)
    arc(R, H - R, R, H)
    arc(R, R, 0, R)


def text(board, s, x, y, layer, size=1.2):
    t = pcbnew.PCB_TEXT(board)
    t.SetText(s)
    t.SetLayer(layer)
    t.SetPosition(pt(x, y))
    t.SetTextSize(pcbnew.VECTOR2I(mm(size), mm(size)))
    t.SetTextThickness(mm(max(0.15, size * 0.15)))
    if layer in (pcbnew.B_SilkS, pcbnew.B_Cu):
        t.SetMirrored(True)
    board.Add(t)


def label_pads(board, ref, names, dx, dy, size):
    """Labels the pads of a back-side part right next to their actual position."""
    f = board.FindFootprintByReference(ref)
    for p in f.Pads():
        if p.GetNumber() in names:
            pos = p.GetPosition()
            x = pcbnew.ToMM(pos.x) - ORIGIN[0] + dx
            y = pcbnew.ToMM(pos.y) - ORIGIN[1] + dy
            text(board, names[p.GetNumber()], x, y, pcbnew.B_SilkS, size)


def build_board(uuids):
    board = pcbnew.BOARD()
    ds = board.GetDesignSettings()
    ds.SetCopperLayerCount(2)
    ds.m_TrackMinWidth = mm(0.2)
    ds.m_MinClearance = mm(0.2)
    ds.m_ViasMinSize = mm(0.6)
    ds.m_MinThroughDrill = mm(0.3)
    ds.m_CopperEdgeClearance = mm(0.5)
    nc = ds.m_NetSettings.m_DefaultNetClass
    nc.SetClearance(mm(0.25))
    nc.SetTrackWidth(mm(0.3))
    nc.SetViaDiameter(mm(0.7))
    nc.SetViaDrill(mm(0.35))

    edge_cuts(board)

    nets = {}

    def net(name):
        if name not in nets:
            n = pcbnew.NETINFO_ITEM(board, name)
            board.Add(n)
            nets[name] = n
        return nets[name]

    for ref, val, sym, fpid, side, (x, y, rot), pins in D.PARTS:
        f = load_fp(fpid)
        f.SetReference(ref)
        f.SetValue(val)
        f.SetPath(pcbnew.KIID_PATH('/' + uuids[ref]))
        ox, oy = D.FP_CENTER.get(fpid, (0.0, 0.0))
        # Place the origin so that the part centre lands on (x, y), taking rotation into account
        a = math.radians(rot)
        rx = ox * math.cos(a) + oy * math.sin(a)
        ry = -ox * math.sin(a) + oy * math.cos(a)
        f.SetPosition(pt(x - rx, y - ry))
        f.SetOrientationDegrees(rot)
        if ref.startswith('H') or ref == 'J3':
            f.Reference().SetVisible(False)
        board.Add(f)
        if side == 'B':
            # Flip to the back side around the part centre
            f.Flip(pt(x, y), True)  # mirror left/right, USB stays at the top
        for p in f.Pads():
            n = pins.get(p.GetNumber())
            if n:
                p.SetNet(net(n))

    # Labels
    text(board, 'DeskDeck v2 (ESP32-S3)', 95.0, 59.5, pcbnew.B_SilkS, 1.2)
    text(board, 'Elias Sehmsdorf 2026', 95.0, 57.6, pcbnew.B_SilkS, 0.9)
    text(board, 'DESK', 103.0, 2.2, pcbnew.B_SilkS, 0.9)
    text(board, 'SENSOR', 91.5, 14.6, pcbnew.B_SilkS, 0.8)
    text(board, 'isolated from ESP', 73.0, 15.0, pcbnew.B_SilkS, 0.8)
    label_pads(board, 'J3', {'1': 'UP', '2': 'DOWN', '3': 'VCC'}, 0.0, 6.3, 0.8)
    label_pads(board, 'J2', {'1': '3V3', '2': 'GND', '3': 'SDA', '4': 'SCL'}, -2.9, 0.0, 0.8)

    return board, nets


def add_gnd_zones(board, nets):
    """GND pour on both layers (ESP side only; the desk side gets a keep-out)."""
    W, H = D.BOARD_W, D.BOARD_H
    for layer in (pcbnew.F_Cu, pcbnew.B_Cu):
        z = pcbnew.ZONE(board)
        z.SetLayer(layer)
        z.SetNet(nets['GND'])
        z.SetLocalClearance(mm(0.3))
        z.SetMinThickness(mm(0.25))
        z.SetPadConnection(pcbnew.ZONE_CONNECTION_THERMAL)
        z.SetThermalReliefGap(mm(0.4))
        z.SetThermalReliefSpokeWidth(mm(0.5))
        o = z.Outline()
        o.NewOutline()
        for x, y in ((0.4, 0.4), (W - 0.4, 0.4), (W - 0.4, H - 0.4), (0.4, H - 0.4)):
            o.Append(pt(x, y))
        board.Add(z)

    # Keep-out around the desk side of the optocouplers: no GND pour near the isolated nets
    ko = pcbnew.ZONE(board)
    ko.SetIsRuleArea(True)
    ko.SetDoNotAllowCopperPour(True)
    ko.SetDoNotAllowTracks(False)
    ko.SetDoNotAllowVias(False)
    ko.SetDoNotAllowPads(False)
    ls = pcbnew.LSET()
    ls.AddLayer(pcbnew.F_Cu)
    ls.AddLayer(pcbnew.B_Cu)
    ko.SetLayerSet(ls)
    o = ko.Outline()
    o.NewOutline()
    # Strip at the top right (optocoupler outputs, terminal block) + the desk button column
    for x, y in ((63.0, 0.6), (W - 0.6, 0.6), (W - 0.6, H - 0.6), (96.8, H - 0.6), (96.8, 16.3), (63.0, 16.3)):
        o.Append(pt(x, y))
    board.Add(ko)


ISO_CLEARANCE_MM = 0.8  # minimum distance between the desk side and the ESP side


def write_isolation_rules(out_dir):
    """Net class 'Desk' in the project (passed to Freerouting via the DSN export) and a
    DRC rule that checks the clearance across the isolation gap."""
    pro = os.path.join(out_dir, f'{NAME}.kicad_pro')
    j = json.load(open(pro))
    ns = j.setdefault('net_settings', {})
    classes = [c for c in ns.get('classes', []) if c.get('name') != 'Desk']
    default = next(c for c in classes if c['name'] == 'Default')
    desk = dict(default, name='Desk', clearance=ISO_CLEARANCE_MM, track_width=0.4)
    ns['classes'] = classes + [desk]
    ns['netclass_patterns'] = [{'netclass': 'Desk', 'pattern': n} for n in sorted(D.ISOLATED_NETS)]
    json.dump(j, open(pro, 'w'), indent=2)

    iso = ' || '.join(f"{{X}}.NetName == '{n}'" for n in sorted(D.ISOLATED_NETS))
    a, b = iso.replace('{X}', 'A'), iso.replace('{X}', 'B')
    with open(os.path.join(out_dir, f'{NAME}.kicad_dru'), 'w') as f:
        f.write('(version 1)\n'
                '# Galvanic isolation: desk nets (DESK_*) keep their distance from everything on the ESP side\n'
                f'(rule "Desk-ESP isolation"\n'
                f'  (condition "({a}) && !({b}) && B.NetName != \'\'")\n'
                f'  (constraint clearance (min {ISO_CLEARANCE_MM}mm)))\n')


if __name__ == '__main__':
    os.makedirs(OUT, exist_ok=True)
    build_library()
    uuids = {p[0]: str(uuid.uuid5(uuid.NAMESPACE_URL, f'{NAME}/' + p[0])) for p in D.PARTS}
    board, nets = build_board(uuids)
    path = os.path.join(OUT, f'{NAME}.kicad_pcb')  # still without zones - they are added after routing
    board.Save(path)
    write_isolation_rules(OUT)
    print('saved:', path, '| parts:', len(D.PARTS), '| nets:', len(nets))
