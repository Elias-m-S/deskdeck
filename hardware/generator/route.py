#!/usr/bin/env python3
"""route.py <pcb>: export DSN -> Freerouting -> import SES -> add GND pours -> fill -> save

Expects freerouting.jar next to this directory in ../fr/ (or set FREEROUTING_JAR).
Run with KiCad's Python.
"""
import os
import subprocess
import sys

import pcbnew

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import sexp  # noqa: E402
from gen_pcb import add_gnd_zones  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
JAR = os.environ.get('FREEROUTING_JAR', os.path.join(HERE, '..', 'fr', 'freerouting.jar'))
NEAR = 1000  # nm, "same point" tolerance


def autoroute(board, base):
    assert pcbnew.ExportSpecctraDSN(board, base + '.dsn'), 'DSN export failed'
    r = subprocess.run(['java', '-jar', JAR, '-de', base + '.dsn', '-do', base + '.ses', '-mp', '60',
                        '--gui.enabled=false'], capture_output=True, text=True, timeout=1200)
    last = None
    for line in r.stdout.splitlines():
        if 'Auto-router pass' in line or 'unrouted' in line.lower():
            last = line
    print(last.split('] ', 1)[-1] if last else r.stdout[-500:])


def import_ses(board, base):
    ses = sexp.parse(open(base + '.ses').read())
    routes = sexp.first(ses, 'routes')
    res = sexp.first(routes, 'resolution')
    scale = {'um': 0.001, 'mm': 1.0, 'mil': 0.0254, 'inch': 25.4}[res[1]] / float(res[2])  # -> mm

    def P(x, y):
        return pcbnew.VECTOR2I(pcbnew.FromMM(float(x) * scale), pcbnew.FromMM(-float(y) * scale))

    layers = {'F.Cu': pcbnew.F_Cu, 'B.Cu': pcbnew.B_Cu}
    nets = {n.GetNetname(): n for n in board.GetNetInfo().NetsByName().values()}
    ntracks = nvias = 0
    for net in sexp.find(sexp.first(routes, 'network_out'), 'net'):
        ni = nets[net[1]]
        for w in sexp.find(net, 'wire'):
            path = sexp.first(w, 'path')
            layer, width, coords = path[1], float(path[2]) * scale, path[3:]
            pts = [P(coords[i], coords[i + 1]) for i in range(0, len(coords), 2)]
            for a, c in zip(pts, pts[1:]):
                t = pcbnew.PCB_TRACK(board)
                t.SetStart(a)
                t.SetEnd(c)
                t.SetWidth(pcbnew.FromMM(width))
                t.SetLayer(layers[layer])
                t.SetNet(ni)
                board.Add(t)
                ntracks += 1
        for v in sexp.find(net, 'via'):
            via = pcbnew.PCB_VIA(board)
            via.SetPosition(P(v[2], v[3]))
            via.SetWidth(pcbnew.FromMM(0.7))
            via.SetDrill(pcbnew.FromMM(0.35))
            via.SetLayerPair(pcbnew.F_Cu, pcbnew.B_Cu)
            via.SetNet(ni)
            board.Add(via)
            nvias += 1
    return nets, ntracks, nvias


# ---- Clean-up: remove duplicate segments and dead ends (Freerouting artefacts)

def tracks(board):
    return [t for t in board.GetTracks() if t.GetClass() == 'PCB_TRACK']


def key(t):
    a, c = t.GetStart(), t.GetEnd()
    return (t.GetLayer(), t.GetNetCode()) + tuple(sorted([(a.x, a.y), (c.x, c.y)]))


def remove_duplicates(board):
    seen, dup = set(), []
    for t in tracks(board):
        k = key(t)
        if k in seen or k[2] == k[3]:
            dup.append(t)
        seen.add(k)
    for t in dup:
        board.Remove(t)
    return len(dup)


def connected(board, point, layer, net, me):
    my_id = me.m_Uuid.AsString()
    for v in board.GetTracks():
        if v.GetNetCode() != net or v.m_Uuid.AsString() == my_id:
            continue
        if v.GetClass() == 'PCB_VIA':
            if (v.GetPosition() - point).EuclideanNorm() < NEAR:
                return True
        elif v.GetLayer() == layer and v.HitTest(point, 0):
            return True
    for f in board.GetFootprints():
        for p in f.Pads():
            if p.GetNetCode() == net and p.IsOnLayer(layer) and p.HitTest(point):
                return True
    return False


def remove_contained(board):
    """Segments lying completely on another segment (both ends on the same neighbour)."""
    contained = []
    ts = tracks(board)
    for t in ts:
        tid = t.m_Uuid.AsString()
        for u in ts:
            if u.m_Uuid.AsString() == tid or u.GetNetCode() != t.GetNetCode() or u.GetLayer() != t.GetLayer():
                continue
            if u.GetLength() > t.GetLength() and u.HitTest(t.GetStart(), 0) and u.HitTest(t.GetEnd(), 0):
                contained.append(t)
                break
    for t in contained:
        board.Remove(t)
    return len(contained)


def remove_dead_ends(board):
    pruned = 0
    while True:
        dead = [t for t in tracks(board) if not (
            connected(board, t.GetStart(), t.GetLayer(), t.GetNetCode(), t) and
            connected(board, t.GetEnd(), t.GetLayer(), t.GetNetCode(), t))]
        if not dead:
            return pruned
        for t in dead:
            board.Remove(t)
        pruned += len(dead)


def remove_orphan_vias(board):
    """Vias with a track on only one side are superfluous as well."""
    removed = 0
    for v in [v for v in board.GetTracks() if v.GetClass() == 'PCB_VIA']:
        lay = {t.GetLayer() for t in tracks(board) if t.GetNetCode() == v.GetNetCode() and
               ((t.GetStart() - v.GetPosition()).EuclideanNorm() < NEAR or
                (t.GetEnd() - v.GetPosition()).EuclideanNorm() < NEAR)}
        if len(lay) < 2:
            board.Remove(v)
            removed += 1
    return removed


if __name__ == '__main__':
    pcb = sys.argv[1]
    base = os.path.splitext(pcb)[0]
    board = pcbnew.LoadBoard(pcb)
    autoroute(board, base)
    nets, ntracks, nvias = import_ses(board, base)
    dup = remove_duplicates(board)
    pruned = remove_contained(board) + remove_dead_ends(board)
    nvias -= remove_orphan_vias(board)
    print(f'cleaned up: {dup} duplicate, {pruned} dead segments')
    add_gnd_zones(board, nets)
    pcbnew.ZONE_FILLER(board).Fill(board.Zones())
    board.Save(pcb)
    print(f'imported: {ntracks} track segments, {nvias} vias')
