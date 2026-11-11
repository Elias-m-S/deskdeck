#pragma once

#include <stdint.h>

/* What a key or encoder step triggers. */
typedef enum {
    ACTION_NONE = 0,
    ACTION_KEY,      // keyboard: modifier + one key (e.g. Super+1)
    ACTION_CONSUMER, // consumer control / media keys (volume, play, ...)
    ACTION_DESK,     // move desk to profile (profile in .keycode)
    ACTION_HA,       // report to Home Assistant (key number in .keycode, MQTT deskdeck/key/<n>)
} action_type_t;

typedef struct {
    action_type_t type;
    uint8_t modifier; // KEYBOARD_MODIFIER_* (OR-ed)
    uint8_t keycode;  // HID_KEY_*
    uint16_t usage;   // HID_USAGE_CONSUMER_*
} action_t;

/* Initialisers for the tables in config.h */
#define KEY(mod, kc) { .type = ACTION_KEY, .modifier = (mod), .keycode = (kc) }
#define MEDIA(u)     { .type = ACTION_CONSUMER, .usage = (u) }
#define DESK(p)      { .type = ACTION_DESK, .keycode = (p) } // DESK(0) = sit, DESK(1) = stand
#define DESK_TOGGLE  2                                       // DESK(DESK_TOGGLE) = move to the other profile
#define HA(n)        { .type = ACTION_HA, .keycode = (n) }   // an HA automation reacts to key n
