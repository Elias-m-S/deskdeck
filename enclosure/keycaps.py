# -*- coding: utf-8 -*-
"""
DeskDeck - MX keycaps with an inlaid legend for FreeCAD 1.x (two-colour print, e.g. AMS).

Shape: the cap from keycaps/CAP_STL (standard profile, 16.9 x 16.9 x 8.8 mm). The legend is inlaid LEGEND_DEPTH deep,
flush with the top face (second colour).

Legends:
  arrow up/down, terminal, folder   from the Lucide legends in keycaps/*.3mf (part "Legend"; only the outline is taken,
                                    not the cap itself)
  bulb, sit/stand                   drawn here, same stroke width as the Lucide legends

Usage:   freecadcmd keycaps.py            regenerate all caps (needs the source files, see README)
         freecadcmd keycaps.py preview    only redraw ../docs/images/keycaps.png from the committed 3MF files
Result:  keycaps/<name>.3mf (two parts: cap = extruder 1, legend = extruder 2), in print orientation
         (top face on the bed), plus ../docs/images/keycaps.png
"""
import math
import os
import re
import sys
import zipfile
from collections import defaultdict

import FreeCAD as App
import Part

try:
    HERE = os.path.dirname(os.path.abspath(__file__))
except NameError:
    HERE = os.path.expanduser('~/Dev/deskdeck/enclosure')

V = App.Vector

# =========================================================================== parameters

CAP_STL = 'Symmetrical_Keycaps(2).stl'    # blank cap, lying upside down (top face at z = 0)

LEGEND_DEPTH = 0.6      # 3 layers of 0.2
LEGEND_SCALE = 1.0      # Lucide legends are ~8 mm

LUCIDE = 0.345          # mm per Lucide unit (24 grid), matching the legends from the 3MF files
STROKE = 2.0            # Lucide stroke width in units

SOURCE = os.path.join(HERE, 'keycaps')
OUTPUT = SOURCE
PREVIEW = os.path.join(HERE, '..', 'docs', 'images', 'keycaps.png')

# name -> (Lucide 3MF file, rotation) or None for a drawn legend
LEGENDS = {
    'up': ('keycap-arrow-big-down-standard-profile.3mf', 180),
    'down': ('keycap-arrow-big-down-standard-profile.3mf', 0),
    'terminal': ('keycap-square-terminal-standard-profile.3mf', 0),
    'folder': ('keycap-folder-standard-profile.3mf', 0),
    'bulb': None,
    'sit_stand': None,
}

# =========================================================================== cap


def mesh_to_solid(mesh):
    s = Part.Shape()
    s.makeShapeFromMesh(mesh.Topology, 0.01)
    solid = Part.makeSolid(Part.makeShell(s.Faces))
    try:
        return solid.removeSplitter()
    except Part.OCCError:
        return solid  # very fine meshes (the written 3MF files) sometimes refuse; the raw solid slices fine


def cap():
    """Cap from the STL, turned upright: stem underside at z = 0, centred on the z axis."""
    import Mesh
    s = mesh_to_solid(Mesh.Mesh(os.path.join(SOURCE, CAP_STL)))
    s.rotate(V(0, 0, 0), V(1, 0, 0), 180)
    bb = s.BoundBox
    s.translate(V(-bb.Center.x, -bb.Center.y, -bb.ZMin))
    return s


# =========================================================================== legends from the 3MF files


def load_3mf(path):
    """{'Keycap'|'Legend'|'Stem': (points, triangles)} from a Vostok Labs cap."""
    with zipfile.ZipFile(path) as z:
        s = z.read('3D/3dmodel.model').decode()
    names = ['Keycap', 'Legend', 'Stem']
    parts = {}
    for m in re.finditer(r'<object id="\d+"[^>]*pindex="(\d+)"[^>]*>(.*?)</object>', s, re.S):
        p = [tuple(map(float, t)) for t in re.findall(r'<vertex x="([^"]+)" y="([^"]+)" z="([^"]+)"', m.group(2))]
        d = [tuple(map(int, t)) for t in re.findall(r'<triangle v1="(\d+)" v2="(\d+)" v3="(\d+)"', m.group(2))]
        parts[names[int(m.group(1))]] = (p, d)
    return parts


def outline(points, triangles):
    """Boundary loops of the top face of a mesh (2D, counter-clockwise = material)."""
    zo = max(p[2] for p in points)
    nr = {}
    idx = [nr.setdefault((round(p[0], 4), round(p[1], 4)), len(nr)) for p in points]
    xy = {idx[i]: points[i][:2] for i in range(len(points))}
    count = defaultdict(int)
    direction = {}
    for d in triangles:
        if any(abs(points[i][2] - zo) > 1e-3 for i in d):
            continue
        for a, b in ((d[0], d[1]), (d[1], d[2]), (d[2], d[0])):
            a, b = idx[a], idx[b]
            if a != b:
                k = (min(a, b), max(a, b))
                count[k] += 1
                direction[k] = (a, b)
    nxt = defaultdict(list)
    for k, n in count.items():
        if n == 1:
            a, b = direction[k]
            nxt[a].append(b)
    loops, used = [], set()
    for start in list(nxt):
        for b0 in nxt[start]:
            if (start, b0) in used:
                continue
            s, a, b = [start], start, b0
            while True:
                used.add((a, b))
                s.append(b)
                if b == start:
                    break
                free = [c for c in nxt[b] if (b, c) not in used]
                if not free:
                    break
                a, b = b, free[0]
            if s[-1] == start and len(s) > 3:
                loops.append([xy[i] for i in s[:-1]])
    return loops


def simplify(loop, tol=0.004):
    """Ramer-Douglas-Peucker for a closed loop (the mesh has thousands of points, which slows the booleans down)."""
    def rdp(p):
        if len(p) < 3:
            return p
        (x0, y0), (x1, y1) = p[0], p[-1]
        lx, ly = x1 - x0, y1 - y0
        l = math.hypot(lx, ly) or 1e-12
        d = [abs((x - x0) * ly - (y - y0) * lx) / l for x, y in p[1:-1]]
        i = max(range(len(d)), key=d.__getitem__)
        if d[i] <= tol:
            return [p[0], p[-1]]
        return rdp(p[:i + 2])[:-1] + rdp(p[i + 1:])
    k = max(range(len(loop)), key=lambda i: (loop[i][0] - loop[0][0]) ** 2 + (loop[i][1] - loop[0][1]) ** 2)
    return rdp(loop[:k + 1])[:-1] + rdp(loop[k:] + [loop[0]])[:-1]


def area(loop):
    return 0.5 * sum(loop[i - 1][0] * loop[i][1] - loop[i][0] * loop[i - 1][1] for i in range(len(loop)))


def legend_from_3mf(path, rotation=0.0):
    """Legend as a 2D face (z = 0), centred on the stem."""
    parts = load_3mf(os.path.join(SOURCE, path))
    sp = parts['Stem'][0]
    cx = (min(p[0] for p in sp) + max(p[0] for p in sp)) / 2
    cy = (min(p[1] for p in sp) + max(p[1] for p in sp)) / 2
    loops = sorted(outline(*parts['Legend']), key=lambda s: -abs(area(s)))
    f = None
    for s in loops:
        s = simplify(s)
        w = Part.makePolygon([V(x - cx, y - cy, 0) for x, y in s] + [V(s[0][0] - cx, s[0][1] - cy, 0)])
        g = Part.Face(w)
        if f is None:
            f = g
        elif area(s) > 0:
            f = f.fuse(g)
        else:
            f = f.cut(g)
    f = f.removeSplitter()
    if rotation:
        f.rotate(V(0, 0, 0), V(0, 0, 1), rotation)
    return f


# =========================================================================== drawn legends (Lucide grid)


def lp(x, y):
    """Lucide coordinate (24 grid, y down) -> mm, centre (12, 12)."""
    return V((x - 12) * LUCIDE, (12 - y) * LUCIDE, 0)


def disc(x, y, r):
    return Part.Face(Part.Wire(Part.makeCircle(r * LUCIDE, lp(x, y))))


def line(*pts, stroke=STROKE):
    """Polyline with round ends and corners."""
    r = stroke / 2
    parts = [disc(x, y, r) for x, y in pts]
    for (x0, y0), (x1, y1) in zip(pts, pts[1:]):
        p, q = lp(x0, y0), lp(x1, y1)
        n = (q - p).normalize()
        n = V(-n.y, n.x, 0) * (r * LUCIDE)
        parts.append(Part.Face(Part.makePolygon([p + n, q + n, q - n, p - n, p + n])))
    return parts


def arc(x, y, rad, a0, a1, stroke=STROKE):
    """Arc around (x, y) from a0 to a1 (degrees, counter-clockwise, y up), round ends."""
    r = stroke / 2
    ring = disc(x, y, rad + r).cut(disc(x, y, rad - r))
    n = max(2, int(abs(a1 - a0) / 20) + 1)
    wedge = [lp(x, y)]
    for i in range(n + 1):
        a = math.radians(a0 + (a1 - a0) * i / n)
        wedge.append(lp(x + 2 * rad * math.cos(a), y - 2 * rad * math.sin(a)))
    wedge.append(lp(x, y))
    parts = [ring.common(Part.Face(Part.makePolygon(wedge)))]
    for a in (a0, a1):
        a = math.radians(a)
        parts.append(disc(x + rad * math.cos(a), y - rad * math.sin(a), r))
    return parts


def union(parts):
    return parts[0].fuse(parts[1:]).removeSplitter()


def bulb():
    # Glass: circle around (12, 8), r 6, over the top; slanting down to the base; two base strokes
    a = 35.0
    rad = 6.0
    ex, ey = rad * math.cos(math.radians(a)), rad * math.sin(math.radians(a))
    t = arc(12, 8, rad, -a, 180 + a)
    t += line((12 + ex, 8 + ey), (15, 14.5), (15, 17))
    t += line((12 - ex, 8 + ey), (9, 14.5), (9, 17))
    t += line((9, 17), (15, 17))
    t += line((10, 21), (14, 21))
    return union(t)


def sit_stand():
    # A sitting figure on the left, a standing one on the right, two arrows back and forth in between
    s = 1.7
    t = [disc(4.2, 4.6, 1.9)]                                        # head, sitting
    t += line((4.2, 8.2), (4.2, 14.2), (8.6, 14.2), (8.6, 20.5), stroke=s)   # torso, thigh, shin
    t += line((4.2, 10), (7.6, 12.2), stroke=s)                      # arm
    t += line((1.6, 9.5), (1.6, 20.5), stroke=s)                     # chair back
    t += line((1.6, 16.6), (6.2, 16.6), stroke=s)                    # seat
    t += [disc(19.8, 3.0, 1.9)]                                      # head, standing
    t += line((19.8, 6.6), (19.8, 13.6), stroke=s)                   # torso
    t += line((19.8, 13.6), (17.8, 20.5), stroke=s)                  # legs
    t += line((19.8, 13.6), (21.8, 20.5), stroke=s)
    t += line((17.4, 11.6), (19.8, 8.4), (22.2, 11.6), stroke=s)     # arms
    t += line((10.8, 8.0), (15.4, 8.0), stroke=s)                    # arrow right
    t += line((13.7, 6.3), (15.4, 8.0), (13.7, 9.7), stroke=s)
    t += line((15.4, 13.2), (10.8, 13.2), stroke=s)                  # arrow left
    t += line((12.5, 11.5), (10.8, 13.2), (12.5, 14.9), stroke=s)
    return union(t)


DRAWN = {'bulb': bulb, 'sit_stand': sit_stand}


def legends():
    out = {}
    for name, src in LEGENDS.items():
        out[name] = DRAWN[name]() if src is None else legend_from_3mf(*src)
    return out


# =========================================================================== output


def mesh(s):
    import MeshPart
    m = MeshPart.meshFromShape(Shape=s, LinearDeflection=0.01, AngularDeflection=0.1, Relative=False)
    return m.Topology


def write_3mf(path, parts):
    """parts: [(name, shape, extruder)]. Bambu Studio / Orca: one object made of several parts."""
    objects, comps = [], []
    for i, (name, s, _) in enumerate(parts):
        pts, tris = mesh(s)
        oid = i + 2
        vs = ''.join(f'<vertex x="{p.x:.5f}" y="{p.y:.5f}" z="{p.z:.5f}"/>' for p in pts)
        ts = ''.join(f'<triangle v1="{a}" v2="{b}" v3="{c}"/>' for a, b, c in tris)
        objects.append(f'<object id="{oid}" type="model" pid="1" pindex="{i}"><mesh><vertices>{vs}</vertices>'
                       f'<triangles>{ts}</triangles></mesh></object>')
        comps.append(f'<component objectid="{oid}"/>')
    gid = len(parts) + 2
    colours = ['#161616FF', '#F7F7F5FF']
    bases = ''.join(f'<base name="{n}" displaycolor="{colours[e - 1]}"/>' for n, _, e in parts)
    model = ('<?xml version="1.0" encoding="UTF-8"?>\n'
             '<model unit="millimeter" xml:lang="en-US" xmlns="http://schemas.microsoft.com/3dmanufacturing/core/2015/02">'
             f'<metadata name="Title">{os.path.basename(path)[:-4]}</metadata>'
             f'<resources><basematerials id="1">{bases}</basematerials>{"".join(objects)}'
             f'<object id="{gid}" type="model"><components>{"".join(comps)}</components></object></resources>'
             f'<build><item objectid="{gid}" transform="1 0 0 0 1 0 0 0 1 128 128 0" printable="1"/></build></model>')
    part_cfg = ''.join(f'<part id="{i + 2}" subtype="normal_part"><metadata key="name" value="{n}"/>'
                       f'<metadata key="extruder" value="{e}"/></part>' for i, (n, _, e) in enumerate(parts))
    cfg = ('<?xml version="1.0" encoding="UTF-8"?>\n'
           f'<config><object id="{gid}"><metadata key="name" value="{os.path.basename(path)[:-4]}"/>'
           f'<metadata key="extruder" value="1"/>{part_cfg}</object></config>')
    types = ('<?xml version="1.0" encoding="UTF-8"?>\n'
             '<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">'
             '<Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>'
             '<Default Extension="model" ContentType="application/vnd.ms-package.3dmanufacturing-3dmodel+xml"/>'
             '<Default Extension="config" ContentType="text/xml"/></Types>')
    rels = ('<?xml version="1.0" encoding="UTF-8"?>\n'
            '<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">'
            '<Relationship Target="/3D/3dmodel.model" Id="rel0" '
            'Type="http://schemas.microsoft.com/3dmanufacturing/2013/01/3dmodel"/></Relationships>')
    with zipfile.ZipFile(path, 'w', zipfile.ZIP_DEFLATED) as z:
        z.writestr('[Content_Types].xml', types)
        z.writestr('_rels/.rels', rels)
        z.writestr('3D/3dmodel.model', model)
        z.writestr('Metadata/model_settings.config', cfg)


def read_3mf(path):
    """Reads a cap written by write_3mf back: (cap, legend) as upright solids (top face at z = height)."""
    import Mesh
    with zipfile.ZipFile(path) as z:
        s = z.read('3D/3dmodel.model').decode()
    shapes = []
    for m in re.finditer(r'<object id="\d+" type="model" pid="1" pindex="(\d+)"><mesh>(.*?)</mesh>', s, re.S):
        pts = [tuple(map(float, t)) for t in re.findall(r'<vertex x="([^"]+)" y="([^"]+)" z="([^"]+)"', m.group(2))]
        tris = [tuple(map(int, t)) for t in re.findall(r'<triangle v1="(\d+)" v2="(\d+)" v3="(\d+)"', m.group(2))]
        mm = Mesh.Mesh()
        mm.addFacets([(pts[a], pts[b], pts[c]) for a, b, c in tris])
        shapes.append(mesh_to_solid(mm))
    cap_s, legend_s = shapes
    height = cap_s.BoundBox.ZMax
    return print_orientation(cap_s, height), print_orientation(legend_s, height)   # the transform is its own inverse


def print_orientation(s, height):
    """Top face on the bed (legend in the first layers, skirt widens upwards), like the STL."""
    s = s.copy()
    s.rotate(V(0, 0, 0), V(1, 0, 0), 180)
    s.translate(V(0, 0, height))
    return s


def preview(results, path, width, height):
    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt
    from matplotlib.patches import PathPatch
    from matplotlib.path import Path

    def draw_faces(ax, faces, **kw):
        verts, codes = [], []
        for f in faces:
            for w in f.Wires:
                pts = [(p.x, p.y) for p in w.discretize(Deflection=0.01)]
                verts += pts + [pts[0]]
                codes += [Path.MOVETO] + [Path.LINETO] * (len(pts) - 1) + [Path.CLOSEPOLY]
        ax.add_patch(PathPatch(Path(verts, codes), **kw))

    n = len(results)
    fig, axs = plt.subplots(2, n, figsize=(2.6 * n, 5.6), gridspec_kw={'height_ratios': [1, 0.7]})
    for i, (name, (k, sym)) in enumerate(results.items()):
        ax = axs[0][i]
        draw_faces(ax, [Part.Face(max(k.slice(V(0, 0, 1), 0.05), key=lambda w: w.BoundBox.XLength))],
                   fc='#555', ec='none')                                                # skirt at the bottom
        draw_faces(ax, k.slice(V(0, 0, 1), height - 0.07), fc='#222', ec='none')       # top face
        draw_faces(ax, sym.slice(V(0, 0, 1), height - 0.07), fc='#f4f4f0', ec='none')
        ax.set_xlim(-width / 2 - 0.5, width / 2 + 0.5)
        ax.set_ylim(-width / 2 - 0.5, width / 2 + 0.5)
        ax.set_aspect(1)
        ax.set_title(name)
        ax.axis('off')
        ax = axs[1][i]
        section = k.slice(V(0, 1, 0), 0.0)
        verts, codes = [], []
        for w in section:
            pts = [(p.x, p.z) for p in w.discretize(Deflection=0.01)]
            verts += pts + [pts[0]]
            codes += [Path.MOVETO] + [Path.LINETO] * (len(pts) - 1) + [Path.CLOSEPOLY]
        ax.add_patch(PathPatch(Path(verts, codes), fc='#555', ec='none'))
        for w in sym.slice(V(0, 1, 0), 0.0):
            pts = [(p.x, p.z) for p in w.discretize(Deflection=0.01)]
            ax.add_patch(PathPatch(Path(pts + [pts[0]], [Path.MOVETO] + [Path.LINETO] * (len(pts) - 1) + [Path.CLOSEPOLY]),
                                   fc='#f4f4f0', ec='#999', lw=0.3))
        ax.set_xlim(-width / 2 - 0.5, width / 2 + 0.5)
        ax.set_ylim(-0.5, height + 0.5)
        ax.set_aspect(1)
        ax.set_title('section', fontsize=8)
        ax.tick_params(labelsize=6)
    fig.tight_layout()
    fig.savefig(path, dpi=110, facecolor='white')


def main_preview():
    results = {}
    for name in LEGENDS:
        results[name] = read_3mf(os.path.join(OUTPUT, f'{name}.3mf'))
    k = next(iter(results.values()))[0]
    bb = k.BoundBox
    preview(results, PREVIEW, bb.XLength, bb.ZMax)
    print(f'preview written: {PREVIEW}')


def main():
    os.makedirs(OUTPUT, exist_ok=True)
    blank = cap()
    bb = blank.BoundBox
    width, height = bb.XLength, bb.ZMax
    top = max(blank.slice(V(0, 0, 1), height - 0.01), key=lambda w: w.BoundBox.XLength).BoundBox.XLength
    results = {}
    for name, f in legends().items():
        f = f.copy()
        if LEGEND_SCALE != 1.0:
            f.scale(LEGEND_SCALE)
        bb = f.BoundBox
        margin = top / 2 - max(abs(bb.XMin), abs(bb.XMax), abs(bb.YMin), abs(bb.YMax))
        sym = f.extrude(V(0, 0, LEGEND_DEPTH + 0.01))
        sym.translate(V(0, 0, height - LEGEND_DEPTH))
        sym = sym.common(blank)
        k = blank.cut(sym)
        for n, s in (('cap', k), ('legend', sym)):
            if not s.isValid():
                print(f'WARNING: {name}/{n} invalid')
        results[name] = (k, sym)
        write_3mf(os.path.join(OUTPUT, f'{name}.3mf'),
                  [('Cap', print_orientation(k, height), 1), ('Legend', print_orientation(sym, height), 2)])
        print(f'{name:14s} legend {bb.XLength:4.1f} x {bb.YLength:4.1f} mm, margin to the top edge {margin:4.2f} mm')
    preview(results, PREVIEW, width, height)
    print(f'cap {width:.2f} x {width:.2f} mm, {height:.2f} mm high, top face {top:.2f} mm')


if 'preview' in sys.argv[1:]:
    main_preview()
else:
    main()
