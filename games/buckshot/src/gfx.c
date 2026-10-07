#include "gfx.h"
#include <eadk.h>

uint8_t gfx_fb[GFX_W * GFX_H];
static uint16_t lut[256];
static uint8_t dimmap[3][256]; /* index -> index of the nearest color at 3/4, 1/2, 1/4 brightness */
static int shake_x, shake_y;

static int nearest(int r, int g, int b) {
  int best = 1, bd = 1 << 30;
  for (int i = 1; i < PAL_COUNT; i++) {
    uint32_t c = art_pal[i];
    int dr = (int)(c >> 16) - r, dg = (int)(c >> 8 & 255) - g, db = (int)(c & 255) - b;
    int d = dr * dr * 3 + dg * dg * 4 + db * db * 2;
    if (d < bd) bd = d, best = i;
  }
  return best;
}

void gfx_init(void) {
  for (int k = 0; k < 3; k++)
    for (int i = 1; i < PAL_COUNT; i++) {
      uint32_t c = art_pal[i];
      int m = 3 - k;
      dimmap[k][i] = nearest((c >> 16) * m / 4, (c >> 8 & 255) * m / 4, (c & 255) * m / 4);
    }
  gfx_light(256, 0, 0);
}

void gfx_light(int light, int flash, int red) {
  for (int i = 0; i < PAL_COUNT; i++) {
    uint32_t c = art_pal[i];
    int r = c >> 16, g = c >> 8 & 255, b = c & 255;
    r = r * light >> 8, g = g * light >> 8, b = b * light >> 8;
    r += (255 - r) * flash >> 8, g += (255 - g) * flash >> 8, b += (255 - b) * flash >> 8;
    if (red) {
      int l = (r * 3 + g * 5 + b * 2) / 10;
      r += ((l * 2 > 255 ? 255 : l * 2) - r) * red >> 8;
      g += (l / 6 - g) * red >> 8;
      b += (l / 6 - b) * red >> 8;
    }
    lut[i] = (uint16_t)((r & 0xF8) << 8 | (g & 0xFC) << 3 | b >> 3);
  }
}

void gfx_shake(int dx, int dy) {
  shake_x = dx;
  shake_y = dy;
}

void gfx_clear(uint8_t c) {
  for (int i = 0; i < GFX_W * GFX_H; i++) gfx_fb[i] = c;
}

static void draw(int img, int dx, int dy, int opaque, const uint8_t *map) {
  const img_t *m = &art_img[img];
  gfx_decode(art_data + (m->off & 0x7FFFFFFF), gfx_fb, m->x + dx, m->y + dy, m->w, m->h, opaque, map, m->off >> 31 ? 2 : 1);
}

void gfx_plate(int img) { draw(img, 0, 0, 1, 0); }

/* A copy of a background (a plate and what lies still on it), run-length
 * coded in RAM: the table views are drawn every frame, and putting their
 * background back from here is much faster than decoding it again. */
#define CACHE_MAX (26 * 1024)
static uint8_t cache[CACHE_MAX];
static uint32_t cache_len, cache_key;

int gfx_cached(uint32_t key) {
  if (!key || key != cache_key) return 0;
  uint8_t *o = gfx_fb;
  const uint8_t *p = cache, *end = cache + cache_len;
  while (p < end) {
    int b = *p++;
    if (b < 128) { /* b + 1 pixels as they are */
      for (int n = b + 1; n--;) *o++ = *p++;
    } else { /* b - 126 pixels of one color */
      uint8_t c = *p++;
      for (int n = b - 126; n--;) *o++ = c;
    }
  }
  return 1;
}

void gfx_cache_store(uint32_t key) {
  cache_key = 0;
  uint32_t n = 0, lit = 0; /* lit: where the current run of single pixels was started */
  int nlit = 0;
  const uint8_t *p = gfx_fb, *end = gfx_fb + GFX_W * GFX_H;
  while (p < end) {
    uint8_t c = *p;
    int run = 1;
    while (p + run < end && run < 129 && p[run] == c) run++;
    if (n + 3 > CACHE_MAX) return; /* too busy a picture: not kept */
    if (run >= 2) {
      cache[n++] = (uint8_t)(run + 126);
      cache[n++] = c;
      nlit = 0;
    } else {
      if (!nlit || nlit == 128) lit = n++, nlit = 0;
      cache[lit] = (uint8_t)nlit++;
      cache[n++] = c;
    }
    p += run;
  }
  cache_len = n;
  cache_key = key;
}

void gfx_cache_clear(void) { cache_key = 0; }

void gfx_sprite(int img, int dx, int dy) {
  if (img >= 0) draw(img, dx, dy, 0, 0);
}

void gfx_darken(int x, int y, int w, int h, int dim) {
  if (dim <= 0) return;
  if (dim > 3) {
    gfx_fill(x, y, w, h, C_BLACK);
    return;
  }
  for (int j = y < 0 ? 0 : y; j < y + h && j < GFX_H; j++)
    for (int i = x < 0 ? 0 : x; i < x + w && i < GFX_W; i++) {
      uint8_t *p = &gfx_fb[j * GFX_W + i];
      *p = dimmap[dim - 1][*p];
    }
}

void gfx_sprite_dim(int img, int dx, int dy, int dim) {
  if (img < 0) return;
  if (dim > 3) dim = 3;
  draw(img, dx, dy, 0, dim > 0 ? dimmap[dim - 1] : 0);
}

void gfx_fill(int x, int y, int w, int h, uint8_t c) {
  for (int j = y < 0 ? 0 : y; j < y + h && j < GFX_H; j++)
    for (int i = x < 0 ? 0 : x; i < x + w && i < GFX_W; i++) gfx_fb[j * GFX_W + i] = c;
}

/* The original's selection brackets: four corners around the item. */
void gfx_brackets(int x, int y, int w, int h, uint8_t c) {
  int l = w < h ? w / 4 : h / 4;
  if (l < 3) l = 3;
  if (l > 7) l = 7;
  int x1 = x + w - 1, y1 = y + h - 1;
  gfx_fill(x, y, l, 1, c), gfx_fill(x, y, 1, l, c);
  gfx_fill(x1 - l + 1, y, l, 1, c), gfx_fill(x1, y, 1, l, c);
  gfx_fill(x, y1, l, 1, c), gfx_fill(x, y1 - l + 1, 1, l, c);
  gfx_fill(x1 - l + 1, y1, l, 1, c), gfx_fill(x1, y1 - l + 1, 1, l, c);
}

static int glyph(const font_t *f, int ch) {
  if (ch >= 'a' && ch <= 'z') ch -= 32;
  if (ch < 32 || ch > 95) ch = '?';
  return ch - 32;
}

static void draw_glyph(const font_t *f, int g, int x, int y, uint8_t c);

#if NP_TEXT_EXTRA
/* Letters beyond ASCII, in NumPlay's language builds (games/common/np_text.h): an accented
   letter is the font's capital with its accent above (a cedilla under it); Chinese comes from
   the 12-pixel font, its middle on the capitals', twice as big for the dot-matrix font. */
static void xplot(int x, int y, void *ctx) {
  if ((unsigned)x < GFX_W && (unsigned)y < GFX_H) gfx_fb[y * GFX_W + x] = *(const uint8_t *)ctx;
}

/* the letter at *s (a UTF-8 lead byte), *s left at its last byte: its advance, and it is drawn
   at (x, y) when draw */
static int xletter(const font_t *f, const char **s, int x, int y, uint8_t c, int draw) {
  const char *q = *s;
  uint32_t cp = np_utf8(&q);
  *s = q - 1;
  char base, second;
  int acc = np_latin(cp, &base, &second), dot = f->h > 16, sc = dot ? 2 : 1;
  if (!base && np_xglyph(cp))
    return draw ? np_xdraw(cp, x, y - (f->h > 10 ? dot : 1), sc, xplot, &c) : np_xadvance(cp, sc);
  if (!base) base = '?';
  int g = glyph(f, base), w = f->adv[g];
  if (draw) {
    draw_glyph(f, g, x, y, c);
    if (acc)
      np_accent(acc, x + (f->w[g] - 5 * sc) / 2, acc == NP_ACC_CEDIL ? y + f->h - (dot ? 5 : 1) : y - 3, sc, xplot, &c);
  }
  if (second) {
    g = glyph(f, second);
    if (draw) draw_glyph(f, g, x + w, y, c);
    w += f->adv[g];
  }
  return w;
}
#endif

int gfx_text_w(const font_t *f, const char *s) {
  int w = 0, best = 0;
  for (; *s; s++) {
    if (*s == '\n') {
      if (w > best) best = w;
      w = 0;
      continue;
    }
#if NP_TEXT_EXTRA
    if ((uint8_t)*s >= 0xC0) {
      w += xletter(f, &s, 0, 0, 0, 0);
      continue;
    }
#endif
    w += f->adv[glyph(f, *s)];
  }
  return w > best ? w : best;
}

int gfx_lines(const char *s) {
  int n = 1;
  for (; *s; s++) n += *s == '\n';
  return n;
}

static void draw_glyph(const font_t *f, int g, int x, int y, uint8_t c) {
  int w = f->w[g];
  uint32_t b = f->off[g];
  for (int j = 0; j < f->h; j++)
    for (int i = 0; i < w; i++, b++)
      if (f->bits[b >> 3] >> (b & 7) & 1) {
        int px = x + i, py = y + j;
        if ((unsigned)px < GFX_W && (unsigned)py < GFX_H) gfx_fb[py * GFX_W + px] = c;
      }
}

void gfx_text(const font_t *f, const char *s, int x, int y, uint8_t c) {
  int x0 = x;
  for (; *s; s++) {
    if (*s == '\n') {
      x = x0;
      y += f->h + 3;
      continue;
    }
#if NP_TEXT_EXTRA
    if ((uint8_t)*s >= 0xC0) {
      x += xletter(f, &s, x, y, c, 1);
      continue;
    }
#endif
    int g = glyph(f, *s);
    draw_glyph(f, g, x, y, c);
    x += f->adv[g];
  }
}

void gfx_text_c(const font_t *f, const char *s, int cx, int y, uint8_t c) {
  char line[NP_TEXT_EXTRA ? 160 : 64];
  while (*s) {
    int n = 0;
    while (s[n] && s[n] != '\n' && n < (int)sizeof line - 1) line[n] = s[n], n++;
    line[n] = 0;
    gfx_text(f, line, cx - gfx_text_w(f, line) / 2, y, c);
    s += n;
    if (*s == '\n') s++;
    y += f->h + 3;
  }
}

void gfx_text_shadow(const font_t *f, const char *s, int cx, int y, uint8_t c) {
  gfx_text_c(f, s, cx + 1, y + 1, C_BLACK);
  gfx_text_c(f, s, cx, y, c);
}

void gfx_present_rect(int x, int y, int w, int h) {
  static uint16_t line[GFX_W * 8];
  if (x < 0) w += x, x = 0;
  if (y < 0) h += y, y = 0;
  if (x + w > GFX_W) w = GFX_W - x;
  if (y + h > GFX_H) h = GFX_H - y;
  if (w <= 0 || h <= 0) return;
  for (int j0 = y; j0 < y + h; j0 += 8) {
    int n = y + h - j0 < 8 ? y + h - j0 : 8;
    uint16_t *o = line;
    for (int j = j0; j < j0 + n; j++) {
      int sy = j - shake_y;
      for (int i = x; i < x + w; i++) {
        int sx = i - shake_x;
        *o++ = (unsigned)sx < GFX_W && (unsigned)sy < GFX_H ? lut[gfx_fb[sy * GFX_W + sx]] : 0;
      }
    }
    eadk_display_push_rect((eadk_rect_t){x, j0, w, n}, line);
  }
}

void gfx_present(void) { gfx_present_rect(0, 0, GFX_W, GFX_H); }
