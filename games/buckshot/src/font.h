#ifndef BR_FONT_H
#define BR_FONT_H
#include <stdint.h>
/* A 1-bit font (tools/font.py): glyphs 32..95, rows of w bits packed LSB first. */
typedef struct {
  uint8_t h;
  const uint8_t *bits;
  const uint16_t *off;
  const uint8_t *w, *adv;
} font_t;
extern const font_t font_small, font_big, font_dot;
#endif
