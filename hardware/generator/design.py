"""
DeskDeck - the circuit as data.
Schematic (.kicad_sch) and board (.kicad_pcb) are generated from this file, so both are
guaranteed to match. The enclosure script (enclosure/enclosure.py) imports it as well, so the
3D-printed parts follow the board layout automatically.

Coordinates in mm, origin top left of the board, y pointing down (KiCad convention).
"""

BOARD_W = 119.0
BOARD_H = 62.0
CORNER_R = 2.0

ROW1_Y = 26.0
ROW2_Y = ROW1_Y + 19.05
ENC_X = 49.5
KEY_X = [68.5, 68.5 + 19.05, 68.5 + 2 * 19.05]
OLED_CX, OLED_CY = 21.5, (ROW1_Y + ROW2_Y) / 2

# SuperMini pinout (top view, USB up):
#   left column  (pads 1-9,   top to bottom): TX(43) RX(44) 1 2 3 4 5 6 7
#   right column (pads 10-18, top to bottom): 5V GND 3V3 13 12 11 10 9 8
SUPERMINI_PINS = {
    '1': 'IO43_TX', '2': 'IO44_RX', '3': 'IO1', '4': 'IO2', '5': 'IO3', '6': 'IO4', '7': 'IO5', '8': 'IO6', '9': 'IO7',
    '10': '5V', '11': 'GND', '12': '3V3', '13': 'IO13', '14': 'IO12', '15': 'IO11', '16': 'IO10', '17': 'IO9', '18': 'IO8',
}

# (reference, value, symbol, footprint, side, (x, y, rotation), {pin: net})
# Position = desired centre of the part (switch/encoder axis, module centre); the conversion to
# the footprint origin is done by gen_pcb.py.
PARTS = [
    # --- ESP32-S3 SuperMini on female headers, back side, USB towards the top edge ---
    ('U1', 'ESP32-S3 SuperMini', 'deskdeck:ESP32-S3_SuperMini', 'deskdeck:ESP32-S3_SuperMini_Socket', 'B',
     (21.5, 14.2, 0), {
         '1': None, '2': 'DESK_UP_CTRL', '3': 'ENC1_A', '4': 'ENC1_B', '5': 'DESK_DN_CTRL', '6': 'ENC1_SW',
         '7': 'KEY1', '8': 'KEY2', '9': 'KEY3',
         '10': None, '11': 'GND', '12': '+3V3', '13': 'SCL', '14': 'SDA', '15': 'ENC2_SW', '16': 'ENC2_B',
         '17': 'ENC2_A', '18': 'KEY4'}),

    # --- Front side: display, encoders, keys ---
    ('J1', 'OLED 1.3" SH1106', 'Connector_Generic:Conn_01x04', 'deskdeck:OLED_1.3in_I2C_Socket', 'F',
     (OLED_CX, OLED_CY, 0), {'1': 'GND', '2': '+3V3', '3': 'SCL', '4': 'SDA'}),

    ('SW1', 'EC11', 'Device:RotaryEncoder_Switch', 'Rotary_Encoder:RotaryEncoder_Alps_EC11E-Switch_Vertical_H20mm', 'F',
     (ENC_X, ROW1_Y, 0), {'A': 'ENC1_A', 'B': 'ENC1_B', 'C': 'GND', 'S1': 'ENC1_SW', 'S2': 'GND'}),
    ('SW2', 'EC11', 'Device:RotaryEncoder_Switch', 'Rotary_Encoder:RotaryEncoder_Alps_EC11E-Switch_Vertical_H20mm', 'F',
     (ENC_X, ROW2_Y, 0), {'A': 'ENC2_A', 'B': 'ENC2_B', 'C': 'GND', 'S1': 'ENC2_SW', 'S2': 'GND'}),

    ('SW3', 'MX key 1', 'Switch:SW_Push', 'Button_Switch_Keyboard:SW_Cherry_MX_1.00u_PCB', 'F',
     (KEY_X[0], ROW1_Y, 0), {'1': 'KEY1', '2': 'GND'}),
    ('SW4', 'MX key 2', 'Switch:SW_Push', 'Button_Switch_Keyboard:SW_Cherry_MX_1.00u_PCB', 'F',
     (KEY_X[0], ROW2_Y, 0), {'1': 'KEY2', '2': 'GND'}),
    ('SW5', 'MX key 3', 'Switch:SW_Push', 'Button_Switch_Keyboard:SW_Cherry_MX_1.00u_PCB', 'F',
     (KEY_X[1], ROW1_Y, 0), {'1': 'KEY3', '2': 'GND'}),
    ('SW6', 'MX key 4', 'Switch:SW_Push', 'Button_Switch_Keyboard:SW_Cherry_MX_1.00u_PCB', 'F',
     (KEY_X[1], ROW2_Y, 0), {'1': 'KEY4', '2': 'GND'}),
    # Desk buttons: purely mechanical on the desk wires, they work without the ESP.
    # The desk switches high side: a button connects VCC to M1 (up) or M2 (down).
    ('SW7', 'MX desk UP', 'Switch:SW_Push', 'Button_Switch_Keyboard:SW_Cherry_MX_1.00u_PCB', 'F',
     (KEY_X[2], ROW1_Y, 0), {'1': 'DESK_UP', '2': 'DESK_VCC'}),
    ('SW8', 'MX desk DOWN', 'Switch:SW_Push', 'Button_Switch_Keyboard:SW_Cherry_MX_1.00u_PCB', 'F',
     (KEY_X[2], ROW2_Y, 0), {'1': 'DESK_DN', '2': 'DESK_VCC'}),

    # --- Back side, top strip: optocouplers, resistors, connectors ---
    # PC817: collector (4) to desk VCC, emitter (3) to M1/M2 - in parallel with the respective desk button
    ('U2', 'PC817 UP', 'Isolator:PC817', 'Package_DIP:DIP-4_W7.62mm', 'B',
     (67.8, 7.5, 0), {'1': 'OPTO_UP_A', '2': 'GND', '3': 'DESK_UP', '4': 'DESK_VCC'}),
    ('U3', 'PC817 DOWN', 'Isolator:PC817', 'Package_DIP:DIP-4_W7.62mm', 'B',
     (81.0, 7.5, 0), {'1': 'OPTO_DN_A', '2': 'GND', '3': 'DESK_DN', '4': 'DESK_VCC'}),
    ('R1', '470', 'Device:R', 'Resistor_THT:R_Axial_DIN0207_L6.3mm_D2.5mm_P7.62mm_Horizontal', 'B',
     (56.5, 4.5, 0), {'1': 'DESK_UP_CTRL', '2': 'OPTO_UP_A'}),
    ('R2', '470', 'Device:R', 'Resistor_THT:R_Axial_DIN0207_L6.3mm_D2.5mm_P7.62mm_Horizontal', 'B',
     (56.5, 12.0, 0), {'1': 'DESK_DN_CTRL', '2': 'OPTO_DN_A'}),
    ('R3', '10k', 'Device:R', 'Resistor_THT:R_Axial_DIN0207_L6.3mm_D2.5mm_P7.62mm_Horizontal', 'B',
     (42.0, 4.5, 0), {'1': 'DESK_UP_CTRL', '2': 'GND'}),
    ('R4', '10k', 'Device:R', 'Resistor_THT:R_Axial_DIN0207_L6.3mm_D2.5mm_P7.62mm_Horizontal', 'B',
     (42.0, 12.0, 0), {'1': 'DESK_DN_CTRL', '2': 'GND'}),
    ('J2', 'VL53L1X sensor', 'Connector_Generic:Conn_01x04', 'Connector_PinHeader_2.54mm:PinHeader_1x04_P2.54mm_Vertical', 'B',
     (91.5, 8.0, 0), {'1': '+3V3', '2': 'GND', '3': 'SDA', '4': 'SCL'}),
    ('J3', 'Desk', 'Connector_Generic:Conn_01x03', 'TerminalBlock_Phoenix:TerminalBlock_Phoenix_MKDS-1,5-3-5.08_1x03_P5.08mm_Horizontal', 'B',
     (103.0, 8.5, 180), {'1': 'DESK_UP', '2': 'DESK_DN', '3': 'DESK_VCC'}),

    # --- Mounting ---
    ('H1', 'M3', 'Mechanical:MountingHole', 'MountingHole:MountingHole_3.2mm_M3', 'F', (4.0, 4.0, 0), {}),
    ('H2', 'M3', 'Mechanical:MountingHole', 'MountingHole:MountingHole_3.2mm_M3', 'F', (115.0, 4.0, 0), {}),
    ('H3', 'M3', 'Mechanical:MountingHole', 'MountingHole:MountingHole_3.2mm_M3', 'F', (4.0, 58.0, 0), {}),
    ('H4', 'M3', 'Mechanical:MountingHole', 'MountingHole:MountingHole_3.2mm_M3', 'F', (59.0, 58.0, 0), {}),
    ('H5', 'M3', 'Mechanical:MountingHole', 'MountingHole:MountingHole_3.2mm_M3', 'F', (115.0, 58.0, 0), {}),
]

POWER_NETS = {'GND', '+3V3'}
# Nets on the desk side of the optocouplers - galvanically isolated from the ESP
ISOLATED_NETS = {'DESK_UP', 'DESK_DN', 'DESK_VCC'}

# Offset footprint origin -> part centre (for footprints whose origin is not in the centre)
FP_CENTER = {
    'Button_Switch_Keyboard:SW_Cherry_MX_1.00u_PCB': (-2.54, 5.08),
    'Rotary_Encoder:RotaryEncoder_Alps_EC11E-Switch_Vertical_H20mm': (7.5, 2.5),
    'Package_DIP:DIP-4_W7.62mm': (3.81, 1.27),
    'Resistor_THT:R_Axial_DIN0207_L6.3mm_D2.5mm_P7.62mm_Horizontal': (3.81, 0.0),
    'Connector_PinHeader_2.54mm:PinHeader_1x04_P2.54mm_Vertical': (0.0, 3.81),
    'TerminalBlock_Phoenix:TerminalBlock_Phoenix_MKDS-1,5-3-5.08_1x03_P5.08mm_Horizontal': (5.08, 0.0),
}
