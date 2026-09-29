/* Text in the doodle's PixelMplus10 at its native 10 px, with CreateJS-like
 * word wrapping (lineWidth) and alignment. */
#include "font.h"
#include <stddef.h>

extern const uint16_t font_code[];
extern const uint8_t font_adv[];
extern const uint16_t font_bits[][11];
extern const unsigned font_glyphs;

static uint32_t utf8(const char **ps) {
  const uint8_t *s = (const uint8_t *)*ps;
  uint32_t c = *s++;
  if (c >= 0xF0) { c = (c & 7) << 18 | (s[0] & 63) << 12 | (s[1] & 63) << 6 | (s[2] & 63); s += 3; }
  else if (c >= 0xE0) { c = (c & 15) << 12 | (s[0] & 63) << 6 | (s[1] & 63); s += 2; }
  else if (c >= 0xC0) { c = (c & 31) << 6 | (s[0] & 63); s += 1; }
  *ps = (const char *)s;
  return c;
}

static int glyph(uint32_t c) {
  if (c >= 32 && c < 127) return (int)c - 32;
  for (unsigned i = 95; i < font_glyphs; i++)
    if (font_code[i] == c) return (int)i;
  return c == 0x2014 ? 104 : -1;    /* em dash as en dash */
}

static int K = 1;
void font_scale(int k) { K = k < 1 ? 1 : k; }

int font_width(const char *s, const char *end) {
  int w = 0;
  while (s < end && *s) {
    int g = glyph(utf8(&s));
    w += (g < 0 ? 5 : font_adv[g]) * K;
  }
  return w;
}

/* Calls fn for each line of s wrapped at lw pixels (lw <= 0: no wrapping). */
typedef void (*LineFn)(const char *a, const char *b, int w, int index, void *ctx);
static int lines(const char *s, int lw, LineFn fn, void *ctx) {
  int n = 0;
  while (*s || n == 0) {
    const char *nl = s;
    while (*nl && *nl != '\n') nl++;
    /* one paragraph: break at spaces */
    const char *a = s;
    do {
      const char *b = nl;
      if (lw > 0 && font_width(a, nl) > lw) {
        const char *last = NULL;
        for (const char *p = a; p < nl; p++)
          if (*p == ' ' && font_width(a, p) <= lw) last = p;
        if (last) b = last;
        else {   /* one long word: cut it where it overflows */
          const char *p = a;
          while (p < nl) {
            const char *q = p;
            utf8(&q);
            if (font_width(a, q) > lw && p > a) break;
            p = q;
          }
          b = p;
        }
      }
      fn(a, b, font_width(a, b), n++, ctx);
      a = b;
      while (a < nl && *a == ' ') a++;
    } while (a < nl);
    if (!*nl) break;
    s = nl + 1;
  }
  return n;
}

static void measure_line(const char *a, const char *b, int w, int index, void *ctx) {
  int *mw = ctx;
  (void)a; (void)b; (void)index;
  if (w > *mw) *mw = w;
}

void font_measure(const char *s, int lw, int lh, int *w, int *h) {
  *w = 0;
  int n = lines(s, lw, measure_line, w);
  int l = lh > 0 ? lh : FONT_LINE * K;
  *h = n * l - l + FONT_HEIGHT * K;
}

typedef struct {
  int ox, oy, lh, by, rows, stride;
  uint8_t align;
  uint16_t *band, color;
  unsigned ga;
} DrawCtx;

static inline uint16_t blend(uint16_t fg, uint16_t bg, unsigned a) {
  uint32_t f = (fg | (uint32_t)fg << 16) & 0x07E0F81Fu, b = (bg | (uint32_t)bg << 16) & 0x07E0F81Fu;
  uint32_t r = (b + (((f - b) * a) >> 5)) & 0x07E0F81Fu;
  return (uint16_t)(r | r >> 16);
}

static void draw_line(const char *a, const char *b, int w, int index, void *vctx) {
  DrawCtx *c = vctx;
  int y = c->oy + index * c->lh;
  if (y + FONT_HEIGHT * K <= c->by || y >= c->by + c->rows) return;
  int x = c->ox - (c->align == 1 ? w / 2 : c->align == 2 ? w : 0);
  while (a < b) {
    int g = glyph(utf8(&a));
    if (g < 0) { x += 5 * K; continue; }
    for (int r = 0; r < FONT_HEIGHT * K; r++) {
      int sy = y + r - c->by;
      if (sy < 0 || sy >= c->rows) continue;
      uint16_t bits = font_bits[g][r / K];
      uint16_t *d = c->band + sy * c->stride;
      for (int i = 0; bits; i++, bits >>= 1)
        if (bits & 1)
          for (int q = 0; q < K; q++) {
            int sx = x + i * K + q;
            if ((unsigned)sx < (unsigned)c->stride) d[sx] = c->ga >= 32 ? c->color : blend(c->color, d[sx], c->ga);
          }
    }
    x += font_adv[g] * K;
  }
}

void font_draw(const char *s, uint8_t align, int lw, int lh, int ox, int oy, uint16_t *band, int by, int rows, int stride,
               uint16_t color, unsigned ga) {
  DrawCtx c = {ox, oy, lh > 0 ? lh : FONT_LINE * K, by, rows, stride, align, band, color, ga};
  lines(s, lw, draw_line, &c);
}

int font_lines(const char *s, int lw) {
  int dummy = 0;
  return lines(s, lw, measure_line, &dummy);
}
