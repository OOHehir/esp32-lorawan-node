/* 5x7 ASCII font for the OLED. Most glyphs leave their right column blank as the gap;
 * a few, such as '#' and '+', fill all five and touch their neighbour. */
#pragma once

#include <stdint.h>

#define FONT5X7_FIRST 0x20
#define FONT5X7_LAST  0x7E
#define FONT5X7_W     5

/* One byte per column, bit 0 at the top. */
extern const uint8_t font5x7[FONT5X7_LAST - FONT5X7_FIRST + 1][FONT5X7_W];
