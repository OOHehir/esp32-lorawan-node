#include "oled_fb.h"

#include <string.h>

#include "font5x7.h"

#define MARGIN ((OLED_W - OLED_COLS * FONT5X7_W * 2) / 2)

void oled_fb_clear(oled_fb_t *fb)
{
    memset(fb->px, 0, sizeof fb->px);
}

/* Doubles each of the 8 bits: bit i becomes bits 2i and 2i+1. */
static uint16_t stretch(uint8_t col)
{
    uint16_t out = 0;
    for (int i = 0; i < 8; i++) {
        if (col & (1u << i)) {
            out |= 3u << (2 * i);
        }
    }
    return out;
}

void oled_fb_text(oled_fb_t *fb, int row, const char *s)
{
    if (row < 0 || row >= OLED_ROWS) {
        return;
    }
    uint8_t *top = fb->px + 2 * row * OLED_W;
    uint8_t *bottom = top + OLED_W;
    memset(top, 0, 2 * OLED_W);
    for (int col = 0; col < OLED_COLS && s[col] != '\0'; col++) {
        unsigned char c = (unsigned char)s[col];
        if (c < FONT5X7_FIRST || c > FONT5X7_LAST) {
            c = '?';
        }
        int x = MARGIN + col * FONT5X7_W * 2;
        for (int i = 0; i < FONT5X7_W; i++) {
            uint16_t tall = stretch(font5x7[c - FONT5X7_FIRST][i]);
            top[x + 2 * i] = top[x + 2 * i + 1] = (uint8_t)tall;
            bottom[x + 2 * i] = bottom[x + 2 * i + 1] = (uint8_t)(tall >> 8);
        }
    }
}
