#pragma once

#include <stdint.h>

/* Everything the UI needs to know arrives as an event through one queue.
 * That way the complete UI state belongs to exactly one task - no mutexes needed. */
typedef enum {
    EV_KEY,         // MX key pressed             (index)
    EV_ENC_STEP,    // encoder turned             (index, delta in detents)
    EV_ENC_CLICK,   // encoder short press        (index)
    EV_ENC_LONG,    // encoder long press         (index)
    EV_HOST,        // host daemon sent new data
    EV_HA_TOAST,    // Home Assistant: message for the display (ha_link_take_toast)
    EV_HA_DESK,     // Home Assistant: move desk to profile (index = profile)
} event_type_t;

typedef struct {
    event_type_t type;
    uint8_t index;
    int8_t delta;
} event_t;

/* Implemented in ui.c, may be called from any task. */
void ui_post(const event_t *ev);
