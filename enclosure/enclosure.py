# -*- coding: utf-8 -*-
"""
DeskDeck - parametric enclosure for FreeCAD 1.x (mounted under the edge of the desk).

Parts:
  Lid             top shell with integrated switch plate (1.5 mm, MX standard), prints top side down
  Tray            bottom shell with a bevel at the front bottom edge holding the distance sensor (points straight down)
  Display_Bezel   only with OLED_FLUSH = False: hood over the OLED on the 8.5 mm riser socket
  OLED_Retainer   U-frame pushed into the lid pocket behind the OLED module, presses it against the ceiling with spring ribs
  Bracket_left/right  angles screwed under the desktop, fixed tilt (or adjustable via slot with TILT_RANGE > 0)

Usage:
  - FreeCAD: Macro -> Macros ... -> run this file   (or: freecadcmd enclosure.py)
  - Change the parameters below and run again. Results land in stl/ (FCStd with assembly view, STL in print orientation).
  - Board dimensions and part positions come from ../hardware/generator/design.py, so they stay in sync with the PCB.

Coordinates (enclosure): x = board x, y = BOARD_H - board y (y points to the rear edge with the USB port),
z = 0 on the top face of the PCB. World (assembly): enclosure tilted by TILT about the x axis, underside of the desk at
z = Z_DESK, the user sits on the -y side. On top of that the whole assembly is yawed by YAW about the vertical, towards the user.
"""
import math
import os
import sys

import FreeCAD as App
import Part

try:
    HERE = os.path.dirname(os.path.abspath(__file__))
except NameError:
    HERE = os.path.expanduser('~/Dev/deskdeck/enclosure')
sys.path.insert(0, os.path.join(HERE, '..', 'hardware', 'generator'))
sys.dont_write_bytecode = True  # no __pycache__ in the generator folder
import design as D  # noqa: E402

V = App.Vector

# =========================================================================== parameters

TILT = 45.0             # degrees. 0 = keys face up, 90 = keys face the user horizontally
TILT_RANGE = 0.0        # +- degrees of slot in the bracket. 0 = fixed tilt, two round screw holes per bracket
YAW = 10.0              # degrees about the vertical: key field looks left towards the user (DeskDeck right of the seat)

WALL = 2.5
GAP = 0.5               # PCB edge <-> inner wall
CORNER_R = 4.0

PLATE_Z = 5.0           # top of the switch plate above the PCB top face (MX: 5.0)
PLATE_T = 1.5           # MX clips need 1.5
SW_HOLE = 14.0
# EC11 clone, measured: body 14 (along the 3 legs A-C-B, x) x 12 (y), 6.1 high, shaft assumed centred.
# The legs and a small nub on top of the body protrude 1.4 mm further on the side facing the OLED.
# (Genuine Alps would be 12.0 x 11.7 x 4.5, tabs 12.5, lugs up to 7.5 in front of the axis.)
# The cut-out is rectangular up over the body; only above it the round pocket for the knob begins.
ENC_BODY = (14.0, 12.0)
ENC_BODY_H = 6.1
ENC_CLEARANCE = 0.15    # clearance per side, as tight as for the switches
ENC_LEGS = (1.4, 7.0)   # protrusion of legs/nub towards the OLED side, width of the notch for them

# Pockets (only when the top is raised, OLED_FLUSH and OLED_RISER > 0)
KEYCAP = 15.85          # keycap outer size. 19.05 pitch - 2 x clearance - 1.2 web
KEYCAP_CLEARANCE = 1.0
KNOB_D = 14.5           # encoder knob
KNOB_CLEARANCE = 0.5
POCKET_R = 1.0          # corner radius of the key pockets (<= clearance, otherwise it gets tight at the cap corners)
POCKET_CHAMFER = 0.4    # chamfer on the pocket rims
TOP_CHAMFER = 1.0       # chamfer on the outer edge of the top

DEPTH = 19.0            # interior below the PCB underside (SuperMini on sockets + USB-C ~ 15.2)
FLOOR = 2.0
BEVEL_W = 20.0          # width of the sensor bevel on the outside

# Screws: lid <-> tray (through the 5 PCB holes), bracket <-> tray
M3_TAP = 2.6            # tap hole, M3 cuts its own thread in PLA/PETG
M3_CLEAR = 3.4
M3_HEAD = 6.0           # counterbore for the screw head (ISO 7380 / DIN 912)
BOSS_TOP_D = 6.5
BOSS_BOTTOM_D = 8.0
BOSS_BOTTOM_MEAT = 6.5  # material above the screw head -> M3x12 fits everywhere

# OLED 1.3" SH1106
# OLED_FLUSH = True: glass flush with the lid top, module PCB sits in a pocket in the lid.
#                    With OLED_RISER > 0 the whole lid top is raised by OLED_RISER; keys and encoders then sit in
#                    pockets whose floor is the switch plate.
# OLED_FLUSH = False: lid top stays on the switch plate, display bezel as a hood.
OLED_FLUSH = True
OLED_RISER = 8.5                 # spacer (female header) under the OLED pin header, 0 = soldered in directly
OLED_W, OLED_H = 35.4, 33.5
OLED_PCB_T = 1.2                 # OLED PCB thickness - measure
OLED_GLASS = (35.0, 22.8, 1.4)   # glass W x H x thickness (incl. tape). Measured 23.3, but fits the 22.9 window per test print
OLED_GLASS_TOP = 5.75            # OLED PCB edge at the pins to top edge of the glass (glass centre as measured at 17.15)
OLED_ACTIVE = (35.0, 18.5)       # lit area W x H, starts 5.5 below the pin edge (check only)
OLED_GLASS_DY = OLED_GLASS_TOP + OLED_GLASS[1] / 2 - OLED_H / 2   # offset glass centre vs module centre, + = away from pins
WINDOW = (35.1, 22.9)            # window for the glass W x H, centred on the glass. Tightened after a test print
NOTCH = (12.5, 4.0)              # recess above the solder joints of the OLED pin header W x H
NOTCH_ROOF = 0.4                 # material above the notch (2 layers of 0.2). 0 = through slot
OLED_HEADER_Z = 2.5 + OLED_RISER # underside of OLED PCB above the board (plastic spacer 2.5 + riser)
OLED_PIN_TOP = 0.6               # pin ends + solder on top of the OLED PCB (clip flush)
OLED_PART_H = 1.2                # tallest component on the back of the OLED module - measure
PIN_PROTRUSION = 1.0             # clipped SuperMini socket pins at the front, under the OLED
OLED_BEZEL_WINDOW = (30.4, 15.7) # bezel only: visible area 29.4 x 14.7 + margin
OLED_BEZEL_WINDOW_DY = 0.0
OLED_PCB_Z = OLED_HEADER_Z
OLED_GLASS_Z = OLED_PCB_Z + OLED_PCB_T + OLED_GLASS[2]
BEZEL_WALL = 1.6
BEZEL_ROOF = 1.2

# OLED retainer (riser only): U-frame pushed into the pocket behind the module (crush ribs hold it there).
# Its ribs sit under the free edge and both side edges of the module and press it against the ceiling with spring
# force; the four feet stand on the PCB. The pin side carries the header in the riser.
RETAINER_RIB_W = 2.0       # width of the ribs (1.6 mm of it under the module)
RETAINER_RIB_T = 1.6       # thickness of the spring ribs
RETAINER_FOOT = 3.0        # feet / corner posts 3 x 3
RETAINER_PIN_CLEAR = 9.5   # side ribs end this far in front of the pin edge (components on the back of the module)
RETAINER_PRELOAD = 0.2     # spring travel: relaxed ribs stand this far above the contact face of the module
RETAINER_CLEARANCE = 0.05  # clearance to the pocket wall per side
RETAINER_CRUSH_RIB = 0.2   # crush ribs protrude this far

# Distance sensor TOF400C (VL53L1X), measured: PCB 20 x 12 x 1.6. At the front (towards the floor) an oval cap
# 10 x 6.5 x 1.5 over the chip, at the edge opposite the pins (centre 3.25 from that edge); the pin row protrudes 1.5 too.
# The module lies flat on the inside of the bevel, the cap fits snugly into an oval hole, the pin row into a groove.
# Two spring snap hooks at the short edges hold it - just press in from the inside.
# Orientation: pin row towards the tray floor (cables run along the floor to the back to J2), cap towards the front wall.
SENSOR_X = 85.0         # board x of the sensor centre
SENSOR_W, SENSOR_H, SENSOR_T = 20.0, 12.0, 1.6
SENSOR_STEP = (12.0, 8.5, 1.0)    # lower step of the cap L x W x height, flush with the edge opposite the pins
SENSOR_STEP_R = 2.0     # corner radius of the pocket for the lower step
SENSOR_GLASS = (10.0, 6.5, 1.5)   # upper oval (glass) L x W x height, inset ~1 mm all round
SENSOR_POCKET = 1.5     # depth of the pocket for the lower step (>= step height; glass ends flush with the outside)
SENSOR_CAP_DY = SENSOR_H / 2 - SENSOR_STEP[1] / 2   # offset of the cap from the module centre towards the edge opposite the pins
SENSOR_CLEARANCE = 0.1  # clearance of the cap in pocket and oval hole
SENSOR_GROOVE = (16.0, 3.6, 1.8)  # groove for the pin row: length, width from the pin edge, depth into the wall
HOOK_W, HOOK_T, HOOK_NOSE = 4.0, 1.0, 0.4   # snap hooks: width, thickness, overhang of the nose over the module edge
HOOK_Y = -1.4           # hooks at the height of the mounting holes (widest point), from the module centre towards the cap edge
HOOK_Z = -0.05          # underside of the nose above the module back (negative = presses the module in slightly)
HOOK_CLEARANCE = 0.05   # clearance between hook and module edge at the widest point
HOOK_ROOT = 1.8         # hook roots this deep in the wall (relief around it) so it can flex

# Openings in the rear wall
USB_W, USB_H = 13.0, 9.0     # enough for the cable strain relief
USB_Z = -15.2                # centre of USB-C (socket 8.5 + header 2.5 + module 1.0 + half socket)
TERMINAL_DEPTH = 12.0        # slot for the desk cable below the PCB

# Brackets
BRACKET_T = 4.0
FLANGE_W = 18.0
FLANGE_T = 5.0
BRACKET_Y = (48.0, 14.0)  # enclosure y of the pivot screw and the slot screw
WOOD_SCREW_D = 4.5
DESK_GAP = 0.0            # additional distance enclosure <-> desktop (room for a straight USB plug)

# Collision check only (MX: housing up to 11.6 above the PCB, travel 4.0)
KEYCAP_Z = 12.0         # underside of the keycap stem at rest
KEYCAP_SKIRT = 4.0      # skirt ends this far above the stem underside (stays above the switch housing when pressed)
KEYCAP_H = 11.0
KEY_TRAVEL = 4.0

# =========================================================================== derived

PW, PH, PT = D.BOARD_W, D.BOARD_H, 1.6
O = GAP + WALL
XA0, XA1, YA0, YA1 = -O, PW + O, -O, PH + O
XI0, XI1, YI0, YI1 = -GAP, PW + GAP, -GAP, PH + GAP
Z_PCB_BOT = -PT
Z_FLOOR = Z_PCB_BOT - DEPTH - FLOOR
Z_PLATE_BOT = PLATE_Z - PLATE_T
Z_TOP = PLATE_Z + (OLED_RISER if OLED_FLUSH else 0.0)   # lid top face
POCKETS = Z_TOP > PLATE_Z + 0.5
Z_BOSS = (Z_FLOOR + Z_PCB_BOT) / 2
TH = math.radians(TILT)
N_BEVEL = V(0, -math.sin(TH), -math.cos(TH))     # outwards, in world = straight down


def board_xy(x, y):
    """Board coordinate -> enclosure (x, y)."""
    return x, PH - y


PARTS = {p[0]: p for p in D.PARTS}
HOLES = [board_xy(*PARTS[r][5][:2]) for r in ('H1', 'H2', 'H3', 'H4', 'H5')]
KEYS = [board_xy(x, y) for x in D.KEY_X for y in (D.ROW1_Y, D.ROW2_Y)]
ENCODERS = [board_xy(D.ENC_X, y) for y in (D.ROW1_Y, D.ROW2_Y)]
OLED_C = board_xy(D.OLED_CX, D.OLED_CY)
OLED_PINS = board_xy(PARTS['J1'][5][0], D.OLED_CY - 14.75)
USB_X = PARTS['U1'][5][0]
J3_X = PARTS['J3'][5][0]

# =========================================================================== helpers


def box(x0, x1, y0, y1, z0, z1):
    return Part.makeBox(x1 - x0, y1 - y0, z1 - z0, V(x0, y0, z0))


def cbox(cx, cy, w, h, z0, z1):
    return box(cx - w / 2, cx + w / 2, cy - h / 2, cy + h / 2, z0, z1)


def zcyl(cx, cy, r, z0, z1):
    return Part.makeCylinder(r, z1 - z0, V(cx, cy, z0))


def rbox(x0, x1, y0, y1, z0, z1, r):
    if r < 0.05:
        return box(x0, x1, y0, y1, z0, z1)
    parts = [box(x0, x1, y0 + r, y1 - r, z0, z1)]
    parts += [zcyl(cx, cy, r, z0, z1) for cx in (x0 + r, x1 - r) for cy in (y0 + r, y1 - r)]
    return box(x0 + r, x1 - r, y0, y1, z0, z1).fuse(parts).removeSplitter()


def crbox(cx, cy, w, h, z0, z1, r):
    return rbox(cx - w / 2, cx + w / 2, cy - h / 2, cy + h / 2, z0, z1, r)


def rounded_wire(cx, cy, w, h, r, z):
    """Outline of a rounded rectangle at height z (wire for a loft)."""
    s = crbox(cx, cy, w, h, z, z + 1, r)
    return max(s.Faces, key=lambda f: f.CenterOfMass.z).OuterWire.translated(V(0, 0, -1))


def loft(cx, cy, w0, h0, r0, z0, w1, h1, r1, z1):
    """Loft between two rounded rectangles (chamfers)."""
    return Part.makeLoft([rounded_wire(cx, cy, w0, h0, r0, z0), rounded_wire(cx, cy, w1, h1, r1, z1)], True)


def pocket_rect(cx, cy, w, h, r, z0, z1, chamfer):
    """Pocket from z0 up past z1 with a chamfer on the rim at z1."""
    return crbox(cx, cy, w, h, z0, z1 + 1, r).fuse(
        [loft(cx, cy, w, h, r, z1 - chamfer, w + 2 * chamfer, h + 2 * chamfer, r + chamfer, z1 + 0.01),
         crbox(cx, cy, w + 2 * chamfer, h + 2 * chamfer, z1 + 0.01, z1 + 1, r + chamfer)])


def pocket_round(cx, cy, rad, z0, z1, chamfer):
    return zcyl(cx, cy, rad, z0, z1 + 1).fuse(
        [Part.makeCone(rad, rad + chamfer, chamfer + 0.01, V(cx, cy, z1 - chamfer)), zcyl(cx, cy, rad + chamfer, z1 + 0.01, z1 + 1)])


def oval(cx, cy, l, w, z0, z1):
    """Slot shape (two half circles + rectangle), l along x."""
    r = w / 2
    return fuse([box(cx - l / 2 + r, cx + l / 2 - r, cy - r, cy + r, z0, z1),
                 zcyl(cx - l / 2 + r, cy, r, z0, z1), zcyl(cx + l / 2 - r, cy, r, z0, z1)])


def moved(s, pl):
    s = s.copy()
    s.Placement = pl.multiply(s.Placement)
    return s


def half_space(point, normal):
    """Large block containing everything with normal . (p - point) > 0."""
    B = 1000.0
    return moved(Part.makeBox(2 * B, 2 * B, B, V(-B, -B, 0)), App.Placement(point, App.Rotation(V(0, 0, 1), normal)))


def wall_opening(cx, cz, w, h, y0, y1, r):
    """Rounded opening through a wall perpendicular to y."""
    s = crbox(cx, cz, w, h, 0, y1 - y0, r)
    return moved(s, App.Placement(V(0, y1, 0), App.Rotation(V(1, 0, 0), 90)))


def fuse(parts):
    return parts[0].fuse(parts[1:]).removeSplitter() if len(parts) > 1 else parts[0]


# =========================================================================== lid

def lid():
    if POCKETS:
        outer_w, outer_h = XA1 - XA0, YA1 - YA0
        mx, my = (XA0 + XA1) / 2, (YA0 + YA1) / 2
        s = rbox(XA0, XA1, YA0, YA1, Z_PCB_BOT, Z_TOP - TOP_CHAMFER, CORNER_R).fuse(
            loft(mx, my, outer_w, outer_h, CORNER_R, Z_TOP - TOP_CHAMFER,
                 outer_w - 2 * TOP_CHAMFER, outer_h - 2 * TOP_CHAMFER, CORNER_R - TOP_CHAMFER, Z_TOP)).removeSplitter()
    else:
        s = rbox(XA0, XA1, YA0, YA1, Z_PCB_BOT, PLATE_Z, CORNER_R)
    s = s.cut(rbox(XI0, XI1, YI0, YI1, Z_PCB_BOT - 1, Z_PLATE_BOT, CORNER_R - WALL))
    s = s.fuse([zcyl(x, y, BOSS_TOP_D / 2, 0, Z_PLATE_BOT + 0.01) for x, y in HOLES])
    cuts = [cbox(x, y, SW_HOLE, SW_HOLE, Z_PLATE_BOT - 1, PLATE_Z + 1) for x, y in KEYS]
    ew, eh = ENC_BODY[0] / 2 + ENC_CLEARANCE, ENC_BODY[1] / 2 + ENC_CLEARANCE
    ez = max(PLATE_Z + 1, ENC_BODY_H + 0.3)
    for x, y in ENCODERS:
        cuts += [box(x - ew, x + ew, y - eh, y + eh, Z_PLATE_BOT - 1, ez),
                 box(x - ew - ENC_LEGS[0], x, y - ENC_LEGS[1] / 2, y + ENC_LEGS[1] / 2, Z_PLATE_BOT - 1, ez)]
    if POCKETS:
        # Pocket floor = top of the switch plate, the switches still clip into 1.5 mm
        m = KEYCAP + 2 * KEYCAP_CLEARANCE
        cuts += [pocket_rect(x, y, m, m, POCKET_R, PLATE_Z, Z_TOP, POCKET_CHAMFER) for x, y in KEYS]
        cuts += [pocket_round(x, y, KNOB_D / 2 + KNOB_CLEARANCE, PLATE_Z, Z_TOP, POCKET_CHAMFER) for x, y in ENCODERS]
    if OLED_FLUSH:
        # Pocket for the OLED PCB, recess for the pin ends, window for the glass
        cuts += [cbox(*OLED_C, OLED_W + 0.8, OLED_H + 0.8, Z_PLATE_BOT - 1, OLED_PCB_Z + OLED_PCB_T + 0.1)]
        cuts += [cbox(*OLED_PINS, *NOTCH, Z_PLATE_BOT - 1, Z_TOP - NOTCH_ROOF if NOTCH_ROOF > 0 else Z_TOP + 1)]
        cuts += [cbox(OLED_C[0], OLED_C[1] - OLED_GLASS_DY, *WINDOW, Z_PLATE_BOT - 1, Z_TOP + 1)]
    else:
        cuts += [cbox(*OLED_C, OLED_W + 0.8, OLED_H + 0.8, Z_PLATE_BOT - 1, PLATE_Z + 1)]
    cuts += [zcyl(x, y, M3_TAP / 2, -1, PLATE_Z - 0.8) for x, y in HOLES]
    return s.cut(cuts).removeSplitter()


# =========================================================================== display bezel

def display_bezel():
    ow, oh = OLED_W + 0.8, OLED_H + 0.8
    z_roof = OLED_GLASS_Z + 0.2
    s = crbox(*OLED_C, ow + 2 * BEZEL_WALL, oh + 2 * BEZEL_WALL, PLATE_Z, z_roof + BEZEL_ROOF, 2.0)
    s = s.cut(cbox(*OLED_C, ow, oh, PLATE_Z - 1, z_roof))
    # Lip engages the cut-out in the lid
    lip = cbox(*OLED_C, ow - 0.3, oh - 0.3, Z_PLATE_BOT + 0.3, PLATE_Z + 0.01)
    lip = lip.cut(cbox(*OLED_C, ow - 1.9, oh - 1.9, Z_PLATE_BOT, PLATE_Z + 1))
    web = cbox(*OLED_C, ow + 0.2, oh + 0.2, PLATE_Z, PLATE_Z + 0.8).cut(cbox(*OLED_C, ow - 1.9, oh - 1.9, 0, 20))
    s = s.fuse([lip, web])
    fx, fy = OLED_C[0], OLED_C[1] - OLED_BEZEL_WINDOW_DY   # pins are at +y
    s = s.cut(cbox(fx, fy, *OLED_BEZEL_WINDOW, z_roof - 1, z_roof + BEZEL_ROOF + 1))
    return s.removeSplitter()


# =========================================================================== OLED retainer

OLED_POCKET = (OLED_W + 0.8, OLED_H + 0.8)


def oled_retainer():
    """U-frame in the OLED pocket: ribs under the free edge and the side edges, feet on the PCB."""
    ox, oy = OLED_C
    x0, x1 = ox - OLED_POCKET[0] / 2 + RETAINER_CLEARANCE, ox + OLED_POCKET[0] / 2 - RETAINER_CLEARANCE
    y0 = oy - OLED_POCKET[1] / 2 + RETAINER_CLEARANCE          # free edge (pins are at +y)
    y1 = oy + OLED_H / 2 - RETAINER_PIN_CLEAR
    z1 = OLED_PCB_Z + 0.1 + RETAINER_PRELOAD                   # module rests against the pocket ceiling
    z0 = z1 - RETAINER_RIB_T
    w, f = RETAINER_RIB_W, RETAINER_FOOT
    parts = [box(x0, x1, y0, y0 + w, z0, z1), box(x0, x0 + w, y0, y1, z0, z1), box(x1 - w, x1, y0, y1, z0, z1)]
    parts += [box(xa, xa + f, ya, ya + f, 0, z1) for xa in (x0, x1 - f) for ya in (y0, y1 - f)]
    # Tabs under the side ribs, protruding RETAINER_CRUSH_RIB outwards: crushed when inserted, they hold the frame
    r = RETAINER_CRUSH_RIB
    for ya in (y0 + f + 3, y1 - f - 3):
        parts += [box(x0 - r, x0 + 1.5, ya - 1.0, ya + 1.0, Z_PLATE_BOT + 0.5, z1),
                  box(x1 - 1.5, x1 + r, ya - 1.0, ya + 1.0, Z_PLATE_BOT + 0.5, z1)]
    return fuse(parts)


# =========================================================================== tray

BEVEL_A = V(0, YA0 + BEVEL_W * math.cos(TH), Z_FLOOR)
BEVEL_B = V(0, YA0, Z_FLOOR + BEVEL_W * math.sin(TH))
SENSOR_PL = App.Placement(V(SENSOR_X, 0, 0) + (BEVEL_A + BEVEL_B) * 0.5, App.Rotation(V(1, 0, 0), -TILT))


def snap_hooks():
    """Spring hooks at the short edges of the sensor, local (inside of the bevel at z = WALL)."""
    parts = []
    z_nose = WALL + SENSOR_T + HOOK_Z
    for sx in (-1, 1):
        x_inner = SENSOR_W / 2 + HOOK_CLEARANCE                # inner face of the hook
        x0, x1 = sorted((sx * x_inner, sx * (x_inner + HOOK_T)))
        finger = box(x0, x1, HOOK_Y - HOOK_W / 2, HOOK_Y + HOOK_W / 2, WALL - HOOK_ROOT, z_nose + 0.8)
        # Nose with lead-in chamfer: HOOK_NOSE wide at the bottom (at the PCB), 0 at the top
        xi = sx * x_inner
        y0 = HOOK_Y - HOOK_W / 2
        pts = [V(xi, y0, z_nose), V(xi - sx * HOOK_NOSE, y0, z_nose), V(xi, y0, z_nose + 0.8)]
        nose = Part.Face(Part.makePolygon(pts + [pts[0]])).extrude(V(0, HOOK_W, 0))
        parts.append(finger.fuse(nose))
    return parts


def hook_reliefs():
    """Slot around the hooks in the wall so they flex freely from HOOK_ROOT on."""
    parts = []
    for sx in (-1, 1):
        x_inner = SENSOR_W / 2 + HOOK_CLEARANCE
        x0, x1 = sorted((sx * (x_inner - 0.4), sx * (x_inner + HOOK_T + 0.4)))
        slot = box(x0, x1, HOOK_Y - HOOK_W / 2 - 0.4, HOOK_Y + HOOK_W / 2 + 0.4, WALL - HOOK_ROOT, WALL + 0.5)
        xf0, xf1 = sorted((sx * x_inner, sx * (x_inner + HOOK_T)))
        parts.append(slot.cut(box(xf0, xf1, HOOK_Y - HOOK_W / 2, HOOK_Y + HOOK_W / 2, WALL - HOOK_ROOT - 1, WALL + 1)))
    return parts


def tray_outer():
    return rbox(XA0, XA1, YA0, YA1, Z_FLOOR, Z_PCB_BOT, CORNER_R).cut(half_space(BEVEL_A, N_BEVEL))


def tray():
    outer = tray_outer()
    inner = rbox(XI0, XI1, YI0, YI1, Z_FLOOR + FLOOR, Z_PCB_BOT + 1, CORNER_R - WALL)
    inner = inner.cut(half_space(BEVEL_A - N_BEVEL * WALL, N_BEVEL))
    s = outer.cut(inner)

    parts = [zcyl(x, y, BOSS_BOTTOM_D / 2, Z_FLOOR, Z_PCB_BOT).common(outer) for x, y in HOLES]
    # Bosses for the bracket screws in the end walls, with a web down to the floor (prints without supports)
    for y in BRACKET_Y:
        for x0, x1 in ((XA0, XI0 + 5), (XI1 - 5, XA1)):
            boss = Part.makeCylinder(4.5, x1 - x0, V(x0, y, Z_BOSS), V(1, 0, 0))
            parts.append(boss.fuse(box(x0, x1, y - 4.5, y + 4.5, Z_FLOOR, Z_BOSS)).common(outer))
    # Snap hooks for the sensor at the short edges (local: z = inwards, bevel at z = 0..WALL)
    parts += [moved(h, SENSOR_PL) for h in snap_hooks()]
    s = s.fuse(parts)

    cuts = []
    for x, y in HOLES:
        cuts.append(zcyl(x, y, M3_CLEAR / 2, Z_FLOOR - 50, Z_PCB_BOT + 1))
        cuts.append(zcyl(x, y, M3_HEAD / 2, Z_FLOOR - 50, Z_PCB_BOT - BOSS_BOTTOM_MEAT))
    for y in BRACKET_Y:
        cuts.append(Part.makeCylinder(M3_TAP / 2, WALL + 4.5, V(XA0 - 1, y, Z_BOSS), V(1, 0, 0)))
        cuts.append(Part.makeCylinder(M3_TAP / 2, WALL + 4.5, V(XA1 + 1, y, Z_BOSS), V(-1, 0, 0)))
    sl, sw, _ = SENSOR_STEP
    gl, gw, _ = SENSOR_GLASS
    cuts.append(moved(crbox(0, -SENSOR_CAP_DY, sl + 2 * SENSOR_CLEARANCE, sw + 2 * SENSOR_CLEARANCE,
                            WALL - SENSOR_POCKET, WALL + 0.5, SENSOR_STEP_R), SENSOR_PL))
    cuts.append(moved(oval(0, -SENSOR_CAP_DY, gl + 2 * SENSOR_CLEARANCE, gw + 2 * SENSOR_CLEARANCE, -5, WALL + 0.5), SENSOR_PL))
    nl, nw, nt = SENSOR_GROOVE
    cuts.append(moved(box(-nl / 2, nl / 2, SENSOR_H / 2 - nw, SENSOR_H / 2 + 0.3, WALL - nt, WALL + 0.5), SENSOR_PL))
    cuts += [moved(f, SENSOR_PL) for f in hook_reliefs()]
    cuts.append(wall_opening(USB_X, USB_Z, USB_W, USB_H, YI1 - 1, YA1 + 1, 2.0))
    cuts.append(box(J3_X - 8.5, J3_X + 8.5, YI1 - 1, YA1 + 1, Z_PCB_BOT - TERMINAL_DEPTH, Z_PCB_BOT + 1))
    return s.cut(cuts).removeSplitter()


# =========================================================================== brackets (world coordinates)

ENCLOSURE_PL = App.Placement(V(0, 0, 0), App.Rotation(V(1, 0, 0), TILT))


def about_pivot(angle):
    """Enclosure placement at TILT + angle (rotation about the bracket pivot screw)."""
    p = ENCLOSURE_PL.multVec(V(0, BRACKET_Y[0], Z_BOSS))
    rot = App.Placement(p, App.Rotation()).multiply(App.Placement(V(0, 0, 0), App.Rotation(V(1, 0, 0), angle))).multiply(
        App.Placement(-p, App.Rotation()))
    return rot.multiply(ENCLOSURE_PL)


def convex_hull(points):
    pts = sorted(set(points))
    if len(pts) < 3:
        return pts

    def cross(o, a, b):
        return (a[0] - o[0]) * (b[1] - o[1]) - (a[1] - o[1]) * (b[0] - o[0])
    lower, upper = [], []
    for p in pts:
        while len(lower) >= 2 and cross(lower[-2], lower[-1], p) <= 0:
            lower.pop()
        lower.append(p)
    for p in reversed(pts):
        while len(upper) >= 2 and cross(upper[-2], upper[-1], p) <= 0:
            upper.pop()
        upper.append(p)
    return lower[:-1] + upper[:-1]


def bracket_left(z_desk):
    p = ENCLOSURE_PL.multVec(V(0, BRACKET_Y[0], Z_BOSS))
    s = ENCLOSURE_PL.multVec(V(0, BRACKET_Y[1], Z_BOSS))
    r = math.hypot(s.y - p.y, s.z - p.z)
    a0 = math.atan2(s.z - p.z, s.y - p.y)

    def on_arc(angle_deg, radius):
        a = a0 + math.radians(angle_deg)
        return (p.y + radius * math.cos(a), p.z + radius * math.sin(a))

    def circle(c, rad, n=24):
        return [(round(c[0] + rad * math.cos(2 * math.pi * i / n), 4), round(c[1] + rad * math.sin(2 * math.pi * i / n), 4))
                for i in range(n)]

    yf0, yf1 = p.y - 15, p.y + 25
    pts = circle((p.y, p.z), 7)
    w = -TILT_RANGE
    while w <= TILT_RANGE + 1e-6:
        pts += circle(on_arc(w, r), 7)
        w += 2.0
    pts += [(yf0, z_desk - FLANGE_T), (yf1, z_desk - FLANGE_T), (yf0, z_desk), (yf1, z_desk)]
    x = XA0 - BRACKET_T
    outline = [V(x, y, z) for y, z in convex_hull(pts)]
    plate = Part.Face(Part.makePolygon(outline + [outline[0]])).extrude(V(BRACKET_T, 0, 0))

    flange = box(XA0 - BRACKET_T - FLANGE_W, XA0, yf0, yf1, z_desk - FLANGE_T, z_desk)
    part = plate.fuse(flange)

    cuts = [Part.makeCylinder(M3_CLEAR / 2, 20, V(x - 5, p.y, p.z), V(1, 0, 0))]
    # Slot: arc around the pivot screw
    rr = M3_CLEAR / 2

    def pv(yz):
        return V(x - 1, yz[0], yz[1])
    if TILT_RANGE > 0:
        outer = Part.Arc(pv(on_arc(-TILT_RANGE, r + rr)), pv(on_arc(0, r + rr)), pv(on_arc(TILT_RANGE, r + rr)))
        inner = Part.Arc(pv(on_arc(TILT_RANGE, r - rr)), pv(on_arc(0, r - rr)), pv(on_arc(-TILT_RANGE, r - rr)))
        ends = []
        for w, fwd in ((TILT_RANGE, 1), (-TILT_RANGE, -1)):
            c = on_arc(w, r)
            a = a0 + math.radians(w)
            t = (-math.sin(a) * fwd, math.cos(a) * fwd)
            ends.append(Part.Arc(pv(on_arc(w, r + rr) if fwd > 0 else on_arc(w, r - rr)),
                                 pv((c[0] + rr * t[0], c[1] + rr * t[1])),
                                 pv(on_arc(w, r - rr) if fwd > 0 else on_arc(w, r + rr))))
        wire = Part.Wire([outer.toShape(), ends[0].toShape(), inner.toShape(), ends[1].toShape()])
        cuts.append(Part.Face(wire).extrude(V(BRACKET_T + 2, 0, 0)))
    else:
        cuts.append(Part.makeCylinder(M3_CLEAR / 2, 20, V(x - 5, s.y, s.z), V(1, 0, 0)))   # fixed second screw

    xs = XA0 - BRACKET_T - FLANGE_W / 2
    for y in (yf0 + 7, yf1 - 7):
        cuts.append(zcyl(xs, y, WOOD_SCREW_D / 2, z_desk - FLANGE_T - 1, z_desk + 1))
        cuts.append(Part.makeCone(WOOD_SCREW_D + 0.1, WOOD_SCREW_D / 2, WOOD_SCREW_D / 2 + 0.1,
                                  V(xs, y, z_desk - FLANGE_T - 0.01)))
    return part.cut(cuts).removeSplitter()


# =========================================================================== board + components (check/view only)

def dummies():
    d = {}
    d['Board'] = rbox(0, PW, 0, PH, Z_PCB_BOT, 0, 2.0).cut([zcyl(x, y, 1.6, -5, 5) for x, y in HOLES])
    for i, (x, y) in enumerate(KEYS):
        d[f'MX{i}'] = fuse([cbox(x, y, 13.9, 13.9, 0, PLATE_Z - 0.01), cbox(x, y, 15.6, 15.6, PLATE_Z, 11.6)])
        for name, z0 in (('Keycap', KEYCAP_Z), ('Keycap_pressed', KEYCAP_Z - KEY_TRAVEL)):
            d[f'{name}{i}'] = fuse([zcyl(x, y, 2.75, z0, z0 + KEYCAP_SKIRT + 0.01),
                                    cbox(x, y, KEYCAP, KEYCAP, z0 + KEYCAP_SKIRT, z0 + KEYCAP_H)])
    for i, (x, y) in enumerate(ENCODERS):
        # Body as measured, legs/nub towards the OLED side, M7 bushing 7 mm, shaft d6
        d[f'EC11_{i}'] = fuse([cbox(x, y, *ENC_BODY, 0, ENC_BODY_H),
                               box(x - ENC_BODY[0] / 2 - ENC_LEGS[0], x, y - 3.0, y + 3.0, 0, ENC_BODY_H),
                               zcyl(x, y, 3.5, ENC_BODY_H, ENC_BODY_H + 7.0), zcyl(x, y, 3.0, ENC_BODY_H + 7.0, 24.5)])
        d[f'Knob{i}'] = zcyl(x, y, KNOB_D / 2, 12.0, 26.0)
    ox, oy = OLED_C
    sx, sy = OLED_PINS
    z_top = OLED_PCB_Z + OLED_PCB_T
    d['OLED'] = fuse([cbox(sx, sy, 10.16, 2.54, 0, OLED_PCB_Z), cbox(ox, oy, OLED_W, OLED_H, OLED_PCB_Z, z_top),
                      cbox(ox, oy - OLED_GLASS_DY, OLED_GLASS[0], OLED_GLASS[1], z_top, OLED_GLASS_Z),
                      cbox(sx, sy, 10.16, 0.8, z_top, z_top + OLED_PIN_TOP)])
    # Components on the back of the OLED (assumption: strip along the pin edge, next to the header)
    d['OLED_parts'] = cbox(ox, oy + OLED_H / 2 - 5, OLED_W - 2, 8, OLED_PCB_Z - OLED_PART_H, OLED_PCB_Z).cut(
        cbox(sx, sy, 11.0, 3.4, -1, 20))
    ux, uy = board_xy(*PARTS['U1'][5][:2])
    parts = [cbox(board_xy(cx, 0)[0], uy, 2.54, 22.86, -10.1, Z_PCB_BOT) for cx in (13.88, 29.12)]
    parts += [cbox(board_xy(cx, 0)[0], uy, 2.54, 22.86, -12.6, -10.1) for cx in (13.88, 29.12)]
    parts += [cbox(ux, uy, 18.0, 22.52, -13.6, -12.6), cbox(ux, uy - 1, 16.0, 18.0, -15.0, -13.6)]
    usb_y = uy + 22.52 / 2 + 0.5
    parts += [box(ux - 4.5, ux + 4.5, usb_y - 7.4, usb_y, -16.8, -13.6)]
    d['SuperMini'] = fuse(parts)
    # SuperMini socket pins protruding from the front of the board (clip flush under the OLED)
    d['SuperMini_Pins'] = fuse([cbox(*board_xy(px, 4.04 + 2.54 * i), 0.8, 0.8, 0, PIN_PROTRUSION)
                                for px in (13.88, 29.12) for i in range(9)])
    d['USB_plug'] = box(ux - 6, ux + 6, usb_y + 0.3, usb_y + 40, USB_Z - 3.25, USB_Z + 3.25)
    jx, jy = board_xy(*PARTS['J3'][5][:2])
    d['Terminal'] = box(jx - 7.62, jx + 7.62, jy - 5.0, jy + 5.0, Z_PCB_BOT - 10.0, Z_PCB_BOT)
    d['Desk_cable'] = fuse([Part.makeCylinder(1.3, 30, V(jx + dx, jy + 5.0, Z_PCB_BOT - 5.0), V(0, 1, 0)) for dx in (-5.08, 0, 5.08)])
    j2x, j2y = board_xy(*PARTS['J2'][5][:2])
    d['Sensor_header'] = fuse([cbox(j2x, j2y, 2.54, 10.16, -4.1, Z_PCB_BOT), cbox(j2x, j2y, 2.6, 10.2, -18.1, -4.1)])
    for ref in ('U2', 'U3'):
        x, y = board_xy(*PARTS[ref][5][:2])
        d[ref] = cbox(x, y, 6.5, 4.6, -5.1, Z_PCB_BOT)
    for ref in ('R1', 'R2', 'R3', 'R4'):
        x, y = board_xy(*PARTS[ref][5][:2])
        d[ref] = cbox(x, y, 6.3, 2.5, -4.1, Z_PCB_BOT)
    sl, sw, sh = SENSOR_STEP
    gl, gw, gh = SENSOR_GLASS
    d['Sensor'] = moved(fuse([cbox(0, 0, SENSOR_W, SENSOR_H, WALL, WALL + SENSOR_T),
                              crbox(0, -SENSOR_CAP_DY, sl, sw, WALL - sh, WALL + 0.01, SENSOR_STEP_R),
                              oval(0, -SENSOR_CAP_DY, gl, gw, WALL - sh - gh, WALL - sh + 0.01),
                              box(-5.1, 5.1, SENSOR_H / 2 - 2.4, SENSOR_H / 2 - 0.4, WALL - 1.5, WALL + 0.01)]), SENSOR_PL)
    # Room for the cables behind the sensor at the pin edge: wires stick out ~6 mm before they are bent over
    d['Sensor_cable'] = moved(box(-5.1, 5.1, SENSOR_H / 2 - 3.0, SENSOR_H / 2, WALL + SENSOR_T, WALL + SENSOR_T + 6.0), SENSOR_PL)
    return d


# =========================================================================== checks

def check_collisions(parts, dum, brackets):
    problems = []
    intended = {('OLED_Retainer', 'OLED')}   # preload of the spring ribs
    for tn, t in parts.items():
        for dn, s in dum.items():
            if (tn, dn) in intended:
                continue
            v = t.common(s).Volume
            if (tn, dn) == ('Tray', 'Sensor'):   # subtract the preload of the snap noses (HOOK_Z < 0)
                v -= s.common(fuse([moved(h, SENSOR_PL) for h in snap_hooks()])).Volume
            if v > 0.01:
                problems.append(f'{tn} <-> {dn}: {v:.2f} mm³')
    for tn, t in parts.items():
        for w in sorted({-TILT_RANGE, 0.0, TILT_RANGE}):
            g = moved(t, about_pivot(w))
            for hn, h in brackets.items():
                v = g.common(h).Volume
                if v > 0.01:
                    problems.append(f'{tn} <-> {hn} at {TILT + w:.0f}°: {v:.2f} mm³')
    for a, b in (('OLED', 'SuperMini_Pins'), ('OLED_parts', 'SuperMini_Pins')):
        v = dum[a].common(dum[b]).Volume
        if v > 0.01:
            problems.append(f'{a} <-> {b}: {v:.2f} mm³')
    names = list(parts)
    for i, a in enumerate(names):
        for b in names[i + 1:]:
            v = parts[a].common(parts[b]).Volume
            if v > 0.01 and {a, b} != {'Lid', 'OLED_Retainer'}:   # crush ribs
                problems.append(f'{a} <-> {b}: {v:.2f} mm³')
    return problems


# =========================================================================== output

def print_orientation(name, s):
    if name in ('Lid', 'Display_Bezel'):
        s = moved(s, App.Placement(V(0, 0, 0), App.Rotation(V(1, 0, 0), 180)))
    elif name == 'OLED_Retainer':
        s = moved(s, App.Placement(V(0, 0, 0), App.Rotation(V(1, 0, 0), 180)))   # ribs on the bed, feet up
    elif name == 'Bracket_left':
        s = moved(s, App.Placement(V(0, 0, 0), App.Rotation(V(0, 1, 0), 90)))
    elif name == 'Bracket_right':
        s = moved(s, App.Placement(V(0, 0, 0), App.Rotation(V(0, 1, 0), -90)))
    bb = s.BoundBox
    return moved(s, App.Placement(V(-bb.Center.x, -bb.Center.y, -bb.ZMin), App.Rotation()))


def main():
    import MeshPart
    out = os.path.join(HERE, 'stl')
    os.makedirs(out, exist_ok=True)

    parts = {'Lid': lid(), 'Tray': tray()}
    if not OLED_FLUSH:
        parts['Display_Bezel'] = display_bezel()
    if OLED_FLUSH and OLED_RISER > 0:
        parts['OLED_Retainer'] = oled_retainer()
    z_desk = max(moved(t, about_pivot(w)).BoundBox.ZMax for t in parts.values()
                 for w in sorted({-TILT_RANGE, 0.0, TILT_RANGE})) + 1.0 + DESK_GAP
    bl = bracket_left(z_desk)
    mid = (XA0 + XA1) / 2
    brackets = {'Bracket_left': bl, 'Bracket_right': bl.mirror(V(mid, 0, 0), V(1, 0, 0))}
    dum = dummies()

    for n, s in list(parts.items()) + list(brackets.items()):
        if not s.isValid() or len(s.Solids) != 1:
            print(f'WARNING: {n} is not a single valid solid ({len(s.Solids)} solids)')

    problems = check_collisions(parts, dum, brackets)

    # Yaw towards the user about the vertical; then shifted so the front-most point sits at the same place
    # relative to the desk edge as without yaw (nothing sticks out further to the front)
    yaw = App.Placement(V(0, 0, 0), App.Rotation(V(0, 0, 1), -YAW))
    front_old = min(moved(t, ENCLOSURE_PL).BoundBox.YMin for t in parts.values())
    front_new = min(moved(t, yaw.multiply(ENCLOSURE_PL)).BoundBox.YMin for t in parts.values())
    world = App.Placement(V(0, front_old - front_new, 0), App.Rotation()).multiply(yaw)

    doc = App.newDocument('Enclosure')
    for n, s in parts.items():
        doc.addObject('Part::Feature', n).Shape = moved(s, world.multiply(ENCLOSURE_PL))
    for n, s in brackets.items():
        doc.addObject('Part::Feature', n).Shape = moved(s, world)
    grp = doc.addObject('App::DocumentObjectGroup', 'Board_Dummy')
    for n, s in dum.items():
        if 'pressed' in n:
            continue  # check only
        o = doc.addObject('Part::Feature', 'D_' + n.replace('-', '_'))
        o.Shape = moved(s, world.multiply(ENCLOSURE_PL))
        grp.addObject(o)
    desktop = doc.addObject('Part::Feature', 'Desktop')
    desktop.Shape = box(XA0 - 60, XA1 + 60, -8, 200, z_desk, z_desk + 25)
    doc.recompute()
    if App.GuiUp:
        import FreeCADGui as Gui
        colours = {'Lid': (0.15, 0.15, 0.17), 'Tray': (0.15, 0.15, 0.17), 'Display_Bezel': (0.3, 0.3, 0.33),
                   'OLED_Retainer': (0.85, 0.45, 0.1), 'Bracket_left': (0.6, 0.6, 0.62), 'Bracket_right': (0.6, 0.6, 0.62),
                   'Desktop': (0.75, 0.6, 0.4)}
        for o in doc.Objects:
            if o.Name in colours:
                o.ViewObject.ShapeColor = colours[o.Name]
            elif o.Name.startswith('D_'):
                o.ViewObject.ShapeColor = (0.2, 0.5, 0.3)
        doc.getObject('Desktop').ViewObject.Transparency = 60
        Gui.SendMsgToActiveView('ViewFit')
    fcstd = os.path.join(out, 'enclosure.FCStd')
    if os.path.exists(fcstd):
        os.remove(fcstd)  # otherwise FreeCAD creates a .FCBak on every run
    doc.saveAs(fcstd)

    for n in ('Display_Bezel', 'OLED_Retainer'):
        stale = os.path.join(out, n + '.stl')
        if n not in parts and os.path.exists(stale):
            os.remove(stale)
    for n, s in list(parts.items()) + list(brackets.items()):
        d = print_orientation(n, s)
        m = MeshPart.meshFromShape(Shape=d, LinearDeflection=0.05, AngularDeflection=0.2, Relative=False)
        m.write(os.path.join(out, n + '.stl'))

    print(f'TILT {TILT}° ({f"±{TILT_RANGE}°" if TILT_RANGE > 0 else "fixed"}), YAW {YAW}° towards the user, '
          f'desk underside {z_desk:.1f} mm above the enclosure origin')
    # Wood screws in the flanges, in world coordinates: x along the desk edge, y = distance behind the edge
    xs = XA0 - BRACKET_T - FLANGE_W / 2
    p = ENCLOSURE_PL.multVec(V(0, BRACKET_Y[0], Z_BOSS))
    points = [world.multVec(V(x, y, z_desk)) for x in (xs, 2 * mid - xs) for y in (p.y - 15 + 7, p.y + 25 - 7)]
    ref = min(points, key=lambda q: (q.x, q.y))
    edge = front_old
    print('Wood screw drill positions (x from the front left hole to the right, y behind the front-most enclosure point):')
    for q in sorted(points, key=lambda q: (round(q.x), q.y)):
        print(f'  x {q.x - ref.x:6.1f}   y {q.y - edge:6.1f}')
    usb = ENCLOSURE_PL.multVec(V(USB_X, YA1, USB_Z))
    direction = ENCLOSURE_PL.Rotation.multVec(V(0, 1, 0))
    if direction.z > 1e-6:
        room = (z_desk - usb.z) / direction.z
        print(f'Room for the USB plug up to the desktop: {room:.0f} mm '
              f'({"enough for a straight plug" if room >= 40 else "use a right-angle USB-C cable or increase DESK_GAP"})')
    print(f'Enclosure {XA1 - XA0:.1f} x {YA1 - YA0:.1f} mm, height {Z_TOP - Z_FLOOR:.1f} mm')
    if OLED_FLUSH:
        print(f'OLED glass {OLED_GLASS_Z - Z_TOP:+.1f} mm relative to the lid top, '
              f'clearance under the OLED module {OLED_PCB_Z - OLED_PART_H - PIN_PROTRUSION:.1f} mm')
    if POCKETS:
        print(f'Pockets {Z_TOP - PLATE_Z:.1f} mm deep, web between keys {19.05 - KEYCAP - 2 * KEYCAP_CLEARANCE:.2f} mm, '
              f'keycap skirt when pressed {KEYCAP_Z - KEY_TRAVEL + KEYCAP_SKIRT - 11.6:.1f} mm above the switch housing')
    else:
        print(f'Display bezel up to {OLED_GLASS_Z + 0.2 + BEZEL_ROOF - PLATE_Z:.1f} mm above the plate')
    if 'OLED_Retainer' in parts:
        st = parts['OLED_Retainer']
        print(f'OLED retainer {st.BoundBox.XLength:.1f} x {st.BoundBox.YLength:.1f} x {st.BoundBox.ZLength:.2f} mm, '
              f'spring travel {RETAINER_PRELOAD:.2f} mm (presses the module against the pocket ceiling)')
    print('Collisions:', 'none' if not problems else '')
    for p in problems:
        print('  ' + p)
    return doc, parts, brackets, dum, z_desk, world


RESULT = main()
