#pragma once

/* UI state machine: handles all events, drives display, USB HID and the host daemon link.
 * Starts its own task. Call before input_init() and before host_link data can arrive. */
void ui_init(void);
