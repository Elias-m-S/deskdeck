# Firmware

ESP-IDF project for the ESP32-S3 SuperMini. The device enumerates on the PC as **keyboard + media controller (HID)** and as a **serial console (CDC)** that carries the logs and the text protocol to the [host daemon](../host/).

Requirements: ESP-IDF 5.3 or newer (developed and CI-built with 6.0), the managed components in [`main/idf_component.yml`](main/idf_component.yml) are fetched automatically.

```bash
cp main/secrets.h.example main/secrets.h   # Wi-Fi + MQTT, leave WIFI_SSID empty to disable
idf.py set-target esp32s3
idf.py build
./flash.sh            # build, reboot the board into the bootloader via software, flash, restart
./flash.sh monitor    # same, then open the serial monitor
```

`flash.sh` pauses a running host daemon (it holds the port), sends `@BOOTLOADER` to the firmware, waits for the ROM bootloader port and flashes. It looks for ESP-IDF via `idf.py` in `PATH`, else `IDF_PATH`/`IDF_EXPORT_SH`. On a board without this firmware yet: hold **BOOT**, tap **RST**, release **BOOT**, then run it. On the device, the same reboot is available under *Einstellungen -> Firmware flashen*.

## Controls

| Input | Normal | In a menu |
|---|---|---|
| Turn an encoder | Volume of its target, +-2 % per detent | Upper encoder: clockwise = move selection down |
| Upper encoder, short press | Main menu | Confirm |
| Upper encoder, long press | Main menu | One level back |
| Lower encoder, press | Mute / unmute its target | - |
| MX key 1-2 | `Super+1` / `Super+2` (GNOME: app 1-2 in the dock) | Cancel |
| MX key 3 | Desk: sit <-> stand (moves to the other profile) | Cancel |
| MX key 4 | Home Assistant: next light scene | Cancel |

**Main menu:** *Sound* (back to the volume screen), *Tisch* (desk), *Einstellungen* (settings). Lists wrap around. Under *Einstellungen* you assign the application of each encoder, see the HA connection status and can reboot into the bootloader. Menus close after 10 s without input.

Encoder assignments are stored in NVS and survive restarts. Without the host daemon, an encoder set to the master target (`MASTER_TARGET_NAME`, "Gesamt") still works via the normal media keys.

**Display:** per encoder its name, percentage and a bar. The status line at the top only shows problems (`USB getrennt`, `kein PC-Dienst`) and short confirmations. Filled bar = the app is playing, dithered bar = the app is not running (the level is remembered and applied when it starts), empty bar + `STUMM` = muted.

**Status LED** (on-board WS2812): red = no USB host, green = connected, off = host suspended, blue blink = input processed.

## Desk (height adjustable)

Two extra MX switches on the board are wired **purely mechanically** to the desk lines (VCC <-> M1/M2), so the desk can always be operated, even without PC or firmware. In parallel the ESP switches two **PC817 optocouplers** (galvanically isolated) to drive to profiles. The height is measured by the VL53L1X pointing down to the floor.

Menu: long press -> **Tisch** -> show height / move to sit / move to stand / up / down (manual) / sit = now / stand = now / calibrate height. While the desk moves, **any input stops it**.

**Manual up/down** drives without target and without sensor, like holding the button, until the next input or after 60 s (`DESK_MANUAL_MAX_MS`). The desk controller handles its end stops. Useful to test the optocouplers.

The sensor uses a narrowed region of interest (`TOF_ROI_SIZE 8` in `config.h`, roughly a 20 deg cone instead of 27 deg) so that a chair or table leg next to the beam interferes less. Every 5 s the log prints the spread of the raw readings (`tof: 5 s: ... spread ... signal ...`). Measured at ~117 cm: ROI 16 +-5 mm, ROI 8 +-8 mm, ROI 4 +-20 mm and biased by 2-3 cm (too little signal).

Safety (in [`main/desk_logic.c`](main/desk_logic.c), tested with [`test/desk_sim.c`](test/desk_sim.c)):

- at most 20 s per move
- after a 2 s spin-up it must move >= 8 mm per second in the right direction, otherwise abort
- >= 15 mm in the wrong direction -> abort
- no fresh sensor reading (300 ms) -> abort
- outputs off immediately at boot, never both on at the same time
- the coasting distance is learned per direction and stored in NVS

**Commissioning:** set `DESK_DRY_RUN` in `config.h` to `1`: the complete logic runs but the outputs never switch. Set it to `0` only after wiring and testing the optocouplers. Then **calibrate the height** once (measure with a tape, enter with the encoder).

| PC817 | Connection |
|---|---|
| Pin 1 (anode, dot) | GPIO44 (UP) or GPIO3 (DOWN) through 470 R, plus 10 k from GPIO to ESP GND |
| Pin 2 (cathode) | ESP GND |
| Pin 4 (collector) | desk VCC |
| Pin 3 (emitter) | desk line M1 (UP) or M2 (DOWN) |

The desk switches high side (the original hand switch connects VCC to M1/M2). The mechanical buttons are in parallel with the optocoupler outputs. Desk VCC has **no** connection to the ESP.

## Home Assistant (Wi-Fi + MQTT, no PC needed)

The ESP connects to your MQTT broker and registers via MQTT discovery as the device **DeskDeck** with a height sensor, sit/stand buttons and a trigger for key 4. Topics are defined in [`main/ha_link.h`](main/ha_link.h):

| Topic | Direction | Payload |
|---|---|---|
| `deskdeck/status` | device -> HA | `online` / `offline` (last will) |
| `deskdeck/desk/height` | device -> HA | height in cm, on change >= 5 mm or once a minute |
| `deskdeck/key/4` | device -> HA | `press` |
| `deskdeck/cmd/desk` | HA -> device | `sit` / `stand` |
| `deskdeck/cmd/toast` | HA -> device | short text shown on the display (max 21 chars) |

Credentials: `main/secrets.h` (template `secrets.h.example`). Empty Wi-Fi SSID = link disabled. Status: *Einstellungen -> HA*. The ESP32-S3 only does 2.4 GHz Wi-Fi.

## Wiring

Everything switches to **GND**, internal pull-ups are enabled, no resistors needed.

| Part | Pin on the part | GPIO |
|---|---|---|
| MX key 1-4 | one contact | 5, 6, 7, 8 |
|  | other contact | GND |
| Encoder 1 | A (outer) / B (outer) | 1 / 2 |
|  | C (centre of the 3-pin side) | GND |
|  | switch (2-pin side) | 4 and GND |
| Encoder 2 | A / B | 9 / 10 |
|  | C | GND |
|  | switch | 11 and GND |
| OLED SH1106 | VCC / GND | 3V3 / GND |
|  | SDA / SCL | 12 / 13 |
| VL53L1X (TOF400C) | VIN / GND | 3V3 / GND |
|  | SDA / SCL | 12 / 13 (parallel to the OLED) |
|  | INT, SHUT | unconnected |

Encoder direction: `ENC_REVERSE` in `main/config.h`. All pins, the key map and the volume step live in `main/config.h`.

## Structure

```
main/
├── config.h       pins, key map, timings - the file to adapt
├── main.c         initialisation
├── input.c        keys + encoders (polling/debounce, PCNT) -> events
├── ui.c           state machine NORMAL/MENU, display layout, NVS
├── display.c      SH1106/SSD1306 driver, frame buffer, text
├── i2c_bus.c      shared I2C bus (OLED + sensor)
├── tof.c          VL53L1X driver (port of the ST Ultra Lite Driver), median filter
├── desk_logic.c   drive logic as a pure state machine (no hardware, testable)
├── desk.c         desk: GPIOs, profiles/calibration in NVS, control task (20 ms)
├── font5x7.h      5x7 font (Adafruit GFX, BSD)
├── host_link.c    text protocol to the host daemon
├── ha_link.c      Wi-Fi + MQTT to Home Assistant (discovery, key 4, desk height)
├── usb_hid.c      USB descriptors, HID reports, CDC console
├── bootloader.c   software reboot into the ROM bootloader
└── status_led.c   on-board LED
test/
├── desk_sim.c     simulated desk: target accuracy + every abort case
└── Makefile       make -C test
```

Protocol to the host (lines starting with `@`, everything else is log output): see the header of [`main/host_link.h`](main/host_link.h).

## Tests

```bash
make -C test        # compiles desk_logic.c for the host and runs the simulation
```

The simulation models a desk with spin-up, terminal speed and 3 mm sensor noise, and checks: convergence to within 8 mm after the coasting distance is learned, "already at target", a blocked motor, swapped outputs, a sensor dying mid-move, the 20 s time limit, abort by input, manual drive without sensor, and the manual 60 s limit. CI runs it on every push.

## Display language

All strings rendered on the OLED are German (the author's UI language). They are plain literals in `ui.c`, `desk_logic.c`, `tof.c` and `ha_link.c`; the display driver maps UTF-8 umlauts to code page 437.
