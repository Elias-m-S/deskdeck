#pragma once

#include <stdbool.h>
#include <stdint.h>

#define DISP_W 128
#define DISP_H 64

/* Minimal SSD1306/SH1106 driver over I2C with a 1 KB frame buffer.
 * All drawing goes to RAM; display_flush() transfers the buffer. */
bool display_init(void); // false = no display found (the rest keeps running)
void display_flush(void);
const char *display_status(void); // plain text for diagnostic logs

void display_clear(void);
void display_pixel(int x, int y, bool on);
void display_fill_rect(int x, int y, int w, int h, bool on);
void display_rect(int x, int y, int w, int h);

/* Text in the 5x7 font (6 px advance), scale 1..3. UTF-8 umlauts are mapped to CP437.
 * invert: white on black -> black on white. Returns the width in pixels. */
int display_text(int x, int y, const char *utf8, int scale, bool invert);
int display_text_width(const char *utf8, int scale);
