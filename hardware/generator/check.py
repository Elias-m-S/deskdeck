#!/usr/bin/env python3
"""check.py <dir>: DRC + verify that schematic netlist == board == design.py

Run with KiCad's Python (the one that can `import pcbnew`).
"""
import os
import re
import subprocess
import sys
from collections import Counter

import pcbnew

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import design as D  # noqa: E402
import sexp  # noqa: E402

NAME = 'deskdeck'


def drc(d):
    board = pcbnew.LoadBoard(os.path.join(d, f'{NAME}.kicad_pcb'))
    rpt = os.path.join(d, 'drc.rpt')
    pcbnew.WriteDRCReport(board, rpt, pcbnew.EDA_UNITS_MILLIMETRES, True)
    txt = open(rpt).read()
    for line in txt.splitlines():
        if line.startswith('**'):
            print('DRC', line)
    print('DRC entries:', Counter(re.findall(r'^\[(\w+)\]', txt, re.M)))
    return board


def board_nets(board):
    nets = {}
    for f in board.GetFootprints():
        for p in f.Pads():
            if p.GetNetname():
                nets[(f.GetReference(), p.GetNumber())] = p.GetNetname().lstrip('/')
    return nets


def design_nets():
    nets = {}
    for ref, _, _, _, _, _, pins in D.PARTS:
        for pin, n in pins.items():
            if n:
                nets[(ref, pin)] = n
    return nets


def schematic_nets(d):
    net = os.path.join(d, f'{NAME}.net')
    subprocess.run(['kicad-cli', 'sch', 'export', 'netlist', '-o', net, os.path.join(d, f'{NAME}.kicad_sch')],
                   check=True, capture_output=True)
    root = sexp.parse(open(net).read())
    nets = {}
    for n in sexp.find(sexp.first(root, 'nets'), 'net'):
        name = sexp.first(n, 'name')[1].lstrip('/')
        nodes = sexp.find(n, 'node')
        for node in nodes:
            ref = sexp.first(node, 'ref')[1]
            pin = sexp.first(node, 'pin')[1]
            if (name.startswith('unconnected') or name.startswith('Net-(')) and len(nodes) < 2:
                continue
            nets[(ref, pin)] = name
    return nets


def diff(a, b, an, bn):
    bad = 0
    for k in sorted(set(a) | set(b)):
        if a.get(k) != b.get(k):
            print(f'  MISMATCH {k}: {an}={a.get(k)} {bn}={b.get(k)}')
            bad += 1
    return bad


if __name__ == '__main__':
    d = sys.argv[1]
    board = drc(d)
    des = design_nets()
    print('board vs design.py:', 'identical' if not diff(board_nets(board), des, 'pcb', 'design') else 'DIFFERENT')
    print('schematic vs design.py:', 'identical' if not diff(schematic_nets(d), des, 'sch', 'design') else 'DIFFERENT')
