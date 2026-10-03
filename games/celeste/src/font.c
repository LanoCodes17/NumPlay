#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("Os")   /* not drawn every frame: smaller over faster */
#endif
/* Renogare, the game's font, from data.bin's FONTS (tools/textdata.py): 4-bit
 * glyphs at the sizes the 1920x1080 interface has on this screen (1/6). */
#include "celeste.h"

/* per size: u16 nglyphs, u16 line height x16, u16 base x16, u16 nkern,
 * glyphs (u16 code, s16 xo x16, s16 yo, u8 w, u8 h, u16 advance x16, u32 bitmap), kerning (u16 a, u16 b, s16 x16) */
static const uint8_t *face(int size) {
  const uint8_t *s = section(SEC_FONTS);
  return s + rd32(s + 4 + 4 * size);
}
static const uint8_t *glyph(const uint8_t *f, uint32_t code) {
  int n = rd16(f), lo = 0, hi = n - 1;
  while (lo <= hi) {
    int m = (lo + hi) / 2;
    const uint8_t *g = f + 8 + 14 * m;
    uint32_t c = rd16(g);
    if (c == code) return g;
    if (c < code) lo = m + 1;
    else hi = m - 1;
  }
  return NULL;
}
static int kerning(const uint8_t *f, uint32_t a, uint32_t b) {
  int n = rd16(f), nk = rd16(f + 6);
  const uint8_t *k = f + 8 + 14 * n;
  for (int i = 0; i < nk; i++, k += 6)
    if (rd16(k) == a && rd16(k + 2) == b) return rds16(k + 4);
  return 0;
}

uint32_t utf8_next(const char **sp, const char *end) {
  const uint8_t *s = (const uint8_t *)*sp;
  uint32_t c = *s++;
  if (c >= 0xC0 && (const char *)s < end) {
    int extra = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : 1;
    c &= 0x3F >> extra;
    while (extra-- && (const char *)s < end) c = c << 6 | (*s++ & 0x3F);
  }
  *sp = (const char *)s;
  return c;
}

float font_line_height(int size) { return rd16(face(size) + 2) / 16.f; }

/* raw access for FancyText: the glyph record, its kerning with b (both x16 screen pixels), its bitmap */
const uint8_t *font_glyph_rec(int size, uint32_t code) { return glyph(face(size), code); }
int font_kerning_x16(int size, uint32_t a, uint32_t b) { return kerning(face(size), a, b); }
const uint8_t *font_glyph_bits(int size, const uint8_t *g) { return face(size) + rd32(g + 10); }

float font_glyph_advance(int size, uint32_t code, uint32_t next) {
  const uint8_t *f = face(size), *g = glyph(f, code);
  if (!g) return 0;
  return (rd16(g + 8) + (next ? kerning(f, code, next) : 0)) / 16.f;
}

float font_measure(const char *s, int n, int size) {
  const char *end = s + n;
  float w = 0;
  while (s < end) {
    uint32_t c = utf8_next(&s, end);
    const char *t = s;
    uint32_t next = s < end ? utf8_next(&t, end) : 0;
    w += font_glyph_advance(size, c, next);
  }
  return w;
}

void font_draw_glyph(int size, uint32_t code, float x, float y, uint16_t col, uint8_t alpha) {
  const uint8_t *f = face(size), *g = glyph(f, code);
  if (!g || !g[6]) return;
  gfx_mask4(f + rd32(g + 10), g[6], g[7], floorf(x + rds16(g + 2) / 16.f + 0.5f), y + rds16(g + 4), col, alpha);
}

float font_draw(const char *s, int n, float x, float y, int size, uint16_t col, uint8_t alpha) {
  const char *end = s + n;
  float x0 = x;
  x = floorf(x + 0.5f);   /* (from a whole pixel: the glyphs' rounding, and so their gaps, the same wherever it starts) */
  y = floorf(y + 0.5f);
  while (s < end) {
    uint32_t c = utf8_next(&s, end);
    const char *t = s;
    uint32_t next = s < end ? utf8_next(&t, end) : 0;
    font_draw_glyph(size, c, x, y, col, alpha);
    x += font_glyph_advance(size, c, next);
  }
  return x - x0;
}

void font_draw_outline(const char *s, int n, float x, float y, int size, uint16_t col, uint16_t outline, uint8_t alpha) {
  font_draw(s, n, x - 1, y, size, outline, alpha);
  font_draw(s, n, x + 1, y, size, outline, alpha);
  font_draw(s, n, x, y - 1, size, outline, alpha);
  font_draw(s, n, x, y + 1, size, outline, alpha);
  font_draw(s, n, x, y, size, col, alpha);
}

void font_draw_justified(const char *s, float x, float y, float jx, float jy, int size, uint16_t col, uint8_t alpha) {
  int n = (int)strlen(s);
  float w = font_measure(s, n, size), h = font_line_height(size);
  font_draw(s, n, x - w * jx, y - h * jy, size, col, alpha);
}
