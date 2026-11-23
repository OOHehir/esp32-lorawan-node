/* 128x64 monochrome frame buffer in the SSD1306's page layout, with 4 rows of
 * double-size text. */
#pragma once

#include <stdint.h>

#define OLED_W    128
#define OLED_H    64
#define OLED_ROWS 4      /* text rows, each two 8-pixel pages tall */
#define OLED_COLS 12     /* 10-pixel characters; 8 pixels spare, split as side margins */

/* Byte x + 128 * page holds column x of rows 8*page..8*page+7, bit 0 at the top. */
typedef struct {
    uint8_t px[OLED_W * OLED_H / 8];
} oled_fb_t;

void oled_fb_clear(oled_fb_t *fb);

/* Writes s on text row 0-3 with the 5x7 font at double size, clearing the rest of the
 * row. Text past OLED_COLS is cut; a character outside printable ASCII is drawn as '?'.
 * Rows out of range are ignored. */
void oled_fb_text(oled_fb_t *fb, int row, const char *s);
