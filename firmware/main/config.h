#pragma once
/*
 * Pin assignment, key map and behaviour. This is the file to adapt.
 *
 * ESP32-S3 SuperMini pin usage:
 *   1/2/4  encoder 1       5-8    MX keys       9/10/11  encoder 2
 *   12/13  I2C (OLED + VL53L1X)                 3 / 44   desk DOWN / UP (optocouplers)
 *   48     on-board LED    0      BOOT button   19/20    USB      43  UART0 TX (boot log)
 *
 * Every key and encoder contact switches to GND; the internal pull-ups are enabled.
 */
#include "tusb.h"
#include "actions.h"

/* ---------- Status LED (WS2812 on the board) ---------- */
#define PIN_STATUS_LED 48
/* The SuperMini's LED expects RGB instead of the usual WS2812 GRB order. */
#define STATUS_LED_COLOR_FORMAT LED_STRIP_COLOR_COMPONENT_FMT_RGB

/* ---------- I2C bus: OLED (0x3C) + VL53L1X distance sensor (0x29) ---------- */
#define PIN_I2C_SDA 12
#define PIN_I2C_SCL 13

/* ---------- OLED ---------- */
#define OLED_I2C_ADDR 0x3C
#define OLED_I2C_HZ 400000
/* SH1106 (1.3"): 132 columns of RAM, visible from column 2.  SSD1306 (0.96"): offset 0. */
#define OLED_CONTROLLER_SH1106 1
#define OLED_COL_OFFSET 2
#define OLED_CONTRAST 0x80 // 0x00-0xFF

/* ---------- MX keys ----------
 * GNOME/Ubuntu: Super+<n> focuses or launches the n-th app in the dock.
 * A key can also drive the desk: DESK(0) = sit, DESK(1) = stand, e.g. { 7, DESK(0), "Sitzen" }.
 * DESK(DESK_TOGGLE) moves to the other profile (sit <-> stand). HA(n) reports the key to
 * Home Assistant (light scene).
 */
typedef struct
{
    int gpio;
    action_t action;
    const char *label; // shown briefly on the display
} key_binding_t;

#define KEYMAP {                                               \
    {5, KEY(KEYBOARD_MODIFIER_LEFTGUI, HID_KEY_1), "Super+1"}, \
    {6, KEY(KEYBOARD_MODIFIER_LEFTGUI, HID_KEY_2), "Super+2"}, \
    {7, DESK(DESK_TOGGLE), "Tisch"},                           \
    {8, HA(4), "Licht"},                                       \
}

/* ---------- Rotary encoders (EC11) ----------
 * Turn  = volume of the assigned target
 * Press = the menu encoder opens the main menu, the other one mutes its target
 */
typedef struct
{
    int pin_a, pin_b, pin_sw;
    const char *default_target; // initial assignment; persisted in NVS afterwards
} encoder_binding_t;

#define ENCODERS {        \
    {1, 2, 4, "Discord"}, \
    {9, 10, 11, "CS2"},   \
}

#define ENC_COUNTS_PER_DETENT 4 // EC11: 4 edges per detent
#define ENC_REVERSE 1           // 1 = invert the direction of all encoders
#define MENU_ENCODER 0          // index into ENCODERS: this (upper) encoder drives all menus
#define VOLUME_STEP_PERCENT 2   // volume change per detent

/* Targets selectable in the menu even without the host daemon.
 * Once the daemon is connected it sends its own list (configured + currently playing apps). */
#define DEFAULT_TARGETS {"CS2", "Discord", "Gesamt", "Brave"}

/* Name of the "overall volume" target. Without the host daemon this one still works through
 * the regular USB media keys; the daemon maps it to the default sink. */
#define MASTER_TARGET_NAME "Gesamt"

/* ---------- VL53L1X distance sensor ----------
 * Region of interest in SPADs (4..16): 16 = full ~27 deg cone, 4 = ~15 deg. Narrower = less
 * interference from table legs or a chair next to the beam, but a weaker signal. */
#define TOF_ROI_SIZE 8

/* ---------- Height-adjustable desk ----------
 * Two PC817 optocouplers in parallel with the mechanical UP/DOWN buttons.
 * GPIO high = LED on = button "pressed". 470 ohm series resistor and 10 kohm pull-down to
 * ESP GND on each, so nothing moves during boot or reset.
 */
#define PIN_DESK_UP 44
#define PIN_DESK_DOWN 3

/* 1 = dry run: the complete logic incl. sensor runs, but the outputs NEVER switch.
 * Set to 0 only after the optocouplers are wired and tested. */
#define DESK_DRY_RUN 0

#define DESK_MIN_MM 600 // plausibility limits for the desk height
#define DESK_MAX_MM 1300
#define DESK_DEFAULT_OFFSET_MM 25 // height = sensor-to-floor distance + offset (calibrated via menu)
#define DESK_DEFAULT_SIT_MM 720
#define DESK_DEFAULT_STAND_MM 1100
#define DESK_TOLERANCE_MM 5          // this close to the target counts as "arrived"
#define DESK_MAX_DRIVE_MS 20000      // hard upper limit per profile move
#define DESK_MANUAL_MAX_MS 60000     // manual up/down: no sensor check, only this time limit
#define DESK_STARTUP_MS 2000         // spin-up time before movement is monitored
#define DESK_PROGRESS_WINDOW_MS 1000 // within this window the desk must move ...
#define DESK_MIN_PROGRESS_MM 8       // ... at least this far in the right direction
#define DESK_WRONG_DIR_MM 15         // this far in the wrong direction -> abort
#define DESK_SETTLE_MS 1200          // after release: measure the coasting distance

/* ---------- Timing ---------- */
#define INPUT_POLL_MS 2   // key/encoder sampling interval
#define DEBOUNCE_MS 10    // level must be stable for this long
#define LONG_PRESS_MS 600 // an encoder press counts as long from here on
#define MENU_TIMEOUT_MS 10000
#define HOST_TIMEOUT_MS 5000 // no sign of life from the host daemon -> considered disconnected
