#!/usr/bin/env python3
"""iso.py <pcb> [...]: measure the smallest copper distance between the isolated desk nets
and anything on the ESP side (tracks, pads, GND pours). Run with KiCad's Python."""
import os
import sys

import pcbnew

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import design as D  # noqa: E402

F = pcbnew.FromMM


def poly(item, layer):
    return item.GetEffectiveShape(layer)


def bbox(shape):
    bb = shape.BBox()
    return (bb.GetX(), bb.GetY(), bb.GetRight(), bb.GetBottom())


def near(a, b, margin):
    return not (a[2] + margin < b[0] or b[2] + margin < a[0] or a[3] + margin < b[1] or b[3] + margin < a[1])


def min_dist(pa, pb):
    """Binary search on the collision clearance, in mm (capped at 3 mm)."""
    lo, hi = 0.0, 3.0
    if not pa.Collide(pb, F(hi)):
        return hi
    for _ in range(10):
        mid = (lo + hi) / 2
        if pa.Collide(pb, F(mid)):
            hi = mid
        else:
            lo = mid
    return hi


def stats(path):
    b = pcbnew.LoadBoard(path)
    length = sum(pcbnew.ToMM(t.GetLength()) for t in b.GetTracks() if t.GetClass() == 'PCB_TRACK')
    vias = sum(1 for t in b.GetTracks() if t.GetClass() == 'PCB_VIA')
    items = list(b.GetTracks()) + [p for f in b.GetFootprints() for p in f.Pads()]
    best = (9.0, None)
    for layer in (pcbnew.F_Cu, pcbnew.B_Cu):
        iso = [(i, poly(i, layer)) for i in items if i.IsOnLayer(layer) and i.GetNetname() in D.ISOLATED_NETS]
        other = [(i.GetNetname(), poly(i, layer)) for i in items
                 if i.IsOnLayer(layer) and i.GetNetname() and i.GetNetname() not in D.ISOLATED_NETS]
        other += [('GND pour', pcbnew.SHAPE_POLY_SET(z.GetFilledPolysList(layer)))
                  for z in b.Zones() if z.IsOnLayer(layer) and not z.GetIsRuleArea()]
        other = [(n, p, bbox(p)) for n, p in other]
        for ia, pa in iso:
            ba = bbox(pa)
            for nb, pb, bb in other:
                if not near(ba, bb, F(3)):
                    continue
                d = min_dist(pa, pb)
                if d < best[0]:
                    best = (d, (ia.GetNetname(), nb, 'F' if layer == pcbnew.F_Cu else 'B'))
    return length, vias, best


if __name__ == '__main__':
    for p in sys.argv[1:]:
        length, vias, best = stats(p)
        print(f'{os.path.basename(p)}: track length {length:.0f} mm, vias {vias}, '
              f'min. distance desk<->ESP {best[0]:.2f} mm {best[1]}')
