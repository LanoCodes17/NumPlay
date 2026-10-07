#ifndef NP_FONT_H
#define NP_FONT_H
#include <stdint.h>

typedef struct {
  uint8_t w, h;
  int8_t x, y;      /* offset of the top left pixel from the pen (y from the baseline) */
  uint8_t advance;
  uint16_t offset;  /* into data: 4-bit coverage, two pixels per byte, row major */
} np_glyph_t;

typedef struct {
  uint8_t first, count, ascent, descent;
  const np_glyph_t *glyphs;
  const uint8_t *data;
} np_font_t;

extern const np_font_t np_font_title, np_font_body, np_font_small;
/* Letters beyond ASCII in a language build (tools/lang.py runs tools/fontgen.py --extra): their code
 * points, in the order of their glyphs after the 95 ASCII ones; none in English. */
extern const uint16_t np_font_extra[];
extern const int np_font_extra_count;
#endif
