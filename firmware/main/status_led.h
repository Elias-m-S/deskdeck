#pragma once

/* On-board WS2812: red = no USB host, green = connected, off = PC in standby,
 * short blue = input processed. */
void status_led_init(void);
void status_led_flash(void);
