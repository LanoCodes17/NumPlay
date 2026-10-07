/* The renderer: the screen is described each frame as a list of items
 * (rectangles, rounded panels, sprites, text), then drawn strip by strip into
 * a small buffer that is sent to the display. There is no frame buffer. */
#ifndef PROF_NOINLINE
#define PROF_NOINLINE
#endif
#include "gfx.h"
#include <eadk.h>
#include <stddef.h>
#include <string.h>
#include "art.h"
#include "assets.h"
#include "bg.h"
#include "flame.h"
#include "fx.h"

#define SH 16      /* strip height */
#define MAXI 360   /* items per frame */
#define MAXCLIP 16

enum { I_RECT, I_RRECT, I_SHADE, I_SPRITE, I_TEXT, I_HLINE, I_FLAME, I_RSQ };

typedef struct {
  int16_t x, y, w, h;
  uint8_t type, clip;
  uint8_t r;    /* radius / text flags high */
  uint8_t fx;   /* sprite effects */
  uint16_t a;   /* colour / sprite id / alpha */
  uint16_t b;   /* sprite scale / text flags */
  const char *s;
} item_t;

static item_t items[MAXI];
static char strbuf[NP_TEXT_EXTRA ? 6144 : 4096]; /* texts are copied: callers reuse their buffers */
static int strpos;
static int nitems;
static int16_t clips[MAXCLIP][4];
static int nclips, cur_clip;
static C strip[320 * SH];
void *g_scratch(int *size) { *size = (int)sizeof strip; return strip; }
static int bg_swirl, bg_cover_x;
void g_bg_cover(int x) { bg_cover_x = x; }
static C bg_solid;
uint32_t g_time;

/* corner coverage for rounded rectangles, 0..32, radius up to 12 */
#define MAXR 12
static uint8_t cov[MAXR + 1][MAXR][MAXR];

C darken(C a, int t) { return mix565(a, 0, t); }
C lighten(C a, int t) { return mix565(a, 0xFFFF, t); }

void g_init(void) {
  for (int r = 1; r <= MAXR; r++)
    for (int y = 0; y < r; y++)
      for (int x = 0; x < r; x++) {
        /* 4x4 supersampling of the quarter circle centred at (r, r) */
        int n = 0;
        for (int sy = 0; sy < 4; sy++)
          for (int sx = 0; sx < 4; sx++) {
            float dx = r - (x + (sx + 0.5f) / 4), dy = r - (y + (sy + 0.5f) / 4);
            if (dx * dx + dy * dy <= (float)r * r) n++;
          }
        cov[r][y][x] = (uint8_t)(n * 2);
      }
}

void g_begin(void) {
  nitems = 0;
  bg_cover_x = 0;
  strpos = 0;
  nclips = 1;
  cur_clip = 0;
  clips[0][0] = 0, clips[0][1] = 0, clips[0][2] = 320, clips[0][3] = 240;
}

void g_clip(int x, int y, int w, int h) {
  if (nclips >= MAXCLIP) return;
  clips[nclips][0] = (int16_t)(x < 0 ? 0 : x);
  clips[nclips][1] = (int16_t)(y < 0 ? 0 : y);
  clips[nclips][2] = (int16_t)(x + w > 320 ? 320 : x + w);
  clips[nclips][3] = (int16_t)(y + h > 240 ? 240 : y + h);
  cur_clip = nclips++;
}
void g_noclip(void) { cur_clip = 0; }

static item_t *add(int type, int x, int y, int w, int h) {
  if (nitems >= MAXI || w <= 0 || h <= 0) return 0;
  const int16_t *c = clips[cur_clip];
  if (x >= c[2] || y >= c[3] || x + w <= c[0] || y + h <= c[1]) return 0;
  item_t *it = &items[nitems++];
  it->type = (uint8_t)type;
  it->x = (int16_t)x, it->y = (int16_t)y, it->w = (int16_t)w, it->h = (int16_t)h;
  it->clip = (uint8_t)cur_clip;
  it->fx = 0, it->r = 0, it->a = 0, it->b = 0, it->s = 0;
  return it;
}

void g_rect(int x, int y, int w, int h, C c) {
  item_t *it = add(I_RECT, x, y, w, h);
  if (it) it->a = c;
}
void g_line_h(int x, int y, int w, C c) { g_rect(x, y, w, 1, c); }
void g_rrect(int x, int y, int w, int h, int r, C c) {
  if (r > MAXR) r = MAXR;
  if (r > w / 2) r = w / 2;
  if (r > h / 2) r = h / 2;
  item_t *it = add(r > 0 ? I_RRECT : I_RECT, x, y, w, h);
  if (it) it->a = c, it->r = (uint8_t)r;
}
void g_shade(int x, int y, int w, int h, int r, int alpha) {
  if (r > MAXR) r = MAXR;
  if (r > w / 2) r = w / 2;
  if (r > h / 2) r = h / 2;
  item_t *it = add(I_SHADE, x, y, w, h);
  if (it) it->a = (uint16_t)alpha, it->r = (uint8_t)r;
}
void g_sprite(int id, int x, int y, int fx, int scale) {
  if (id < 0 || id >= ART_NSPRITES) return;
  const art_sprite_t *s = &art_sprites[id];
  int w = s->w, h = s->h;
  if (scale != 256) {
    int nw = (w * scale + 128) >> 8, nh = (h * scale + 128) >> 8;
    x += (w - nw) / 2, y += (h - nh) / 2;
    w = nw, h = nh;
  }
  item_t *it = add(I_SPRITE, x, y, w, h);
  if (it) it->a = (uint16_t)id, it->fx = (uint8_t)fx, it->b = (uint16_t)scale;
}
/* a square turned by angle (256 = a full turn) around (cx, cy), half side hs */
void g_rsquare(int cx, int cy, int hs, int angle, C c) {
  int R = hs * 3 / 2 + 1;
  item_t *it = add(I_RSQ, cx - R, cy - R, 2 * R + 1, 2 * R + 1);
  if (it) it->a = c, it->r = (uint8_t)hs, it->fx = (uint8_t)angle;
}
PROF_NOINLINE static void draw_rsq(item_t *it, int y0, int ra, int rb, int cx0, int cx1) {
  /* cos and sin of the angle, x256, from a quarter table */
  static const int16_t q[17] = {256, 255, 251, 245, 237, 226, 213, 198, 181, 162, 142, 121, 98, 74, 50, 25, 0};
  /* a square looks the same after a quarter turn: 64 angle steps per quarter */
  int i = (it->fx & 63) >> 2, co = q[i], si = q[16 - i];
  int hs = it->r * 256 + 128, R = it->w / 2, cx = it->x + R, cy = it->y + R;
  int ya = it->y > ra ? it->y : ra, yb = it->y + it->h < rb ? it->y + it->h : rb;
  int xa = it->x > cx0 ? it->x : cx0, xb = it->x + it->w < cx1 ? it->x + it->w : cx1;
  for (int y = ya; y < yb; y++) {
    C *d = strip + (y - y0) * 320;
    int dy = y - cy;
    for (int x = xa; x < xb; x++) {
      int dx = x - cx;
      int u = dx * co + dy * si, v = dy * co - dx * si;
      if (u < 0) u = -u;
      if (v < 0) v = -v;
      if (u <= hs && v <= hs) d[x] = it->a;
    }
  }
}

void g_flame(int k, int x, int y, int w, int h, int top) {
  if (!flame_on()) return;
  item_t *it = add(I_FLAME, x, y, w, h);
  if (it) it->a = (uint16_t)k, it->b = (uint16_t)top;
}
void g_sprite_wh(int id, int x, int y, int w, int h, int fx) {
  item_t *it = add(I_SPRITE, x, y, w, h);
  if (it) it->a = (uint16_t)id, it->fx = (uint8_t)fx, it->b = 0;
}

/* ------------------------------------------------------------------ text */
static const C code_col[17] = {
    0,
    0xFFFF,              /* 1 white */
    HEXC(0x009DFF),      /* 2 chips */
    HEXC(0xFE5F55),      /* 3 mult */
    HEXC(0xFF9A00),      /* 4 attention */
    HEXC(0xF3B958),      /* 5 money */
    HEXC(0x4BC292),      /* 6 green */
    HEXC(0x88888A),      /* 7 inactive */
    HEXC(0xA782D1),      /* 8 tarot */
    HEXC(0x13AFCE),      /* 9 planet */
    0,                   /* 10 newline */
    HEXC(0x4584FA),      /* 11 spectral */
    HEXC(0x4F6367),      /* 12 dark text */
    0,                   /* 13 */
    0xFFFF,              /* 14 white on red badge */
    HEXC(0x4CA893),      /* 15 edition */
    0,                   /* 16 reset */
};

/* the three sizes: large (values, titles), medium (names), small (labels, texts) */
static const glyph_t *font(int f) { return (f & T_LARGE) ? font_large : (f & T_MED) ? font_med : font_small; }
static const uint8_t *font_bits(int f) {
  return (f & T_LARGE) ? font_large_bits : (f & T_MED) ? font_med_bits : font_small_bits;
}
static int glyph_rows(int f) { return (f & T_LARGE) ? FONT_LARGE_H : (f & T_MED) ? FONT_MED_H : FONT_SMALL_H; }
int g_cap(int f) { return ((f & T_LARGE) ? 11 : (f & T_MED) ? 9 : 7) * ((f & T_X2) ? 2 : 1); }
int g_pitch(int f) { return ((f & T_LARGE) ? 15 : (f & T_MED) ? 13 : LINE_H) * ((f & T_X2) ? 2 : 1); }

/* the glyph of a character (condensed digits for long numbers) */
static const glyph_t *glyph(int f, unsigned ch, const uint8_t **bits) {
  if ((f & (T_COND | T_LARGE)) == (T_COND | T_LARGE) && ch >= '0' && ch <= '9') {
    *bits = font_large_digits_bits;
    return &font_large_digits[ch - '0'];
  }
  *bits = font_bits(f);
  return &font(f)[ch - 32];
}
/* The condensed medium size: each medium glyph one column narrower (a 2
   pixel stroke on the right becomes 1 pixel; centred stems give up their
   left column instead), for long names and numbers before the small size.
   The column dropped, or -1. */
static int drop_col(int f, unsigned ch, const glyph_t *g) {
  if ((f & (T_COND | T_MED)) != (T_COND | T_MED) || f & T_LARGE) return -1;
  if (ch == 'T' || ch == 'I' || ch == '1') return 0;
  if (ch == 'f' || ch == 't') return 4;
  switch (g->w) {
    case 5: return 3;
    case 6: return 2;
    case 7: return 3;
    case 8: return 4;
  }
  return ch == ' ' ? 99 : -1; /* a narrower space */
}

static int advance(int f, unsigned ch, const glyph_t *g) {
  int a = g->adv;
  if (drop_col(f, ch, g) >= 0) a--;
  if (f & T_TIGHT) a -= ch == ' ' ? 2 : 1;
  if ((f & T_COND) && (ch == ',' || ch == '.')) a--;
  return a;
}

#if NP_TEXT_EXTRA
/* ---- letters beyond ASCII, in NumPlay's language builds (games/common/np_text.h): an accented
   letter is the font's plain letter with its accent (a cedilla under it), Chinese comes from the
   12-pixel font, its letters' middle on the capitals' */

/* the letter at *s (a UTF-8 lead byte), *s left at its last byte: its code point; *base its plain
   letter and *second one after it (ligatures), or *base 0 when the 12-pixel font has it */
static uint32_t xletter(const char **s, char *base, char *second, int *acc) {
  const char *q = *s;
  uint32_t cp = np_utf8(&q);
  *s = q - 1;
  *acc = np_latin(cp, base, second);
  if (!*base && !np_xglyph(cp)) *base = '?';
  return cp;
}

static int xadvance(int f, const char **s) {
  char base, second;
  int acc;
  uint32_t cp = xletter(s, &base, &second, &acc);
  if (!base) return np_xadvance(cp, 1);
  const uint8_t *bits;
  int a = advance(f, (unsigned char)base, glyph(f, (unsigned char)base, &bits));
  if (second) a += advance(f, (unsigned char)second, glyph(f, (unsigned char)second, &bits));
  return a;
}

/* pixels of those letters, into the strip: the text's colour or its shadow; turned a quarter left
   for T_ROT (x along the text from the pen, y down from the top of the glyphs) */
typedef struct {
  int y0, ra, rb, cx0, cx1, shadow, rot, ox, oy;
  C col;
} xplot_t;
static void xplot(int x, int y, void *ctx) {
  xplot_t *p = ctx;
  if (p->rot) {
    int t = x;
    x = p->ox + y, y = p->oy - 1 - t;
  }
  if (y < p->ra || y >= p->rb || x < p->cx0 || x >= p->cx1) return;
  C *d = strip + (y - p->y0) * 320 + x;
  *d = p->shadow ? shade565(*d, 12) : p->col;
}

/* a glyph of the font, its shadow first; rows above skip left out (an i's dot under an accent) */
static void xglyph(int f, unsigned ch, int x, int y, int skip, xplot_t *p) {
  int sc = (f & T_X2) ? 2 : 1, gh = glyph_rows(f);
  const uint8_t *b;
  const glyph_t *g = glyph(f, ch, &b);
  int dc = drop_col(f, ch, g);
  for (int pass = (f & (T_SHADOW | T_OUTLINE)) ? 0 : 1; pass < 2; pass++) {
    uint32_t bit = g->off;
    p->shadow = !pass;
    for (int gx = 0; gx < g->w; gx++)
      for (int gy = 0; gy < gh; gy++, bit++) {
        if (gx == dc || gy < skip || !((b[bit >> 3] >> (bit & 7)) & 1)) continue;
        int px = x + (gx - (dc >= 0 && gx > dc)) * sc, py = y + gy * sc + (pass ? 0 : sc);
        for (int k = 0; k < sc * sc; k++) xplot(px + k % sc, py + k / sc, p);
      }
  }
  p->shadow = 0;
}

/* draws the letter at *s (see xletter), the top of its capitals at y; returns its advance */
static int xdraw(int f, const char **s, int x, int y, C col, xplot_t *p) {
  char base, second;
  int acc, sc = (f & T_X2) ? 2 : 1;
  uint32_t cp = xletter(s, &base, &second, &acc);
  p->col = col;
  if (!base) {
    int top = y - ((f & T_LARGE) ? 0 : (f & T_MED) ? 1 : 2) * sc;
    if (f & (T_SHADOW | T_OUTLINE)) {
      p->shadow = 1;
      np_xdraw(cp, x, top + sc, sc, xplot, p);
      p->shadow = 0;
    }
    return np_xdraw(cp, x, top, sc, xplot, p);
  }
  unsigned ch = (unsigned char)base;
  const uint8_t *bits;
  const glyph_t *g = glyph(f, ch, &bits);
  int xh = (f & T_LARGE) ? 3 : 2; /* where lowercase letters start */
  xglyph(f, ch, x, y, acc && (ch == 'i' || ch == 'j') ? xh : 0, p);
  if (acc) {
    int ay = acc == NP_ACC_CEDIL ? y + g_cap(f) : ch >= 'a' && ch <= 'z' ? y + (xh - 2) * sc : y - 2 * sc;
    np_accent(acc, x + (g->w - 5) / 2 * sc, ay, sc, xplot, p);
  }
  int a = advance(f, ch, g) * sc;
  if (second) {
    xglyph(f, (unsigned char)second, x + a, y, 0, p);
    a += advance(f, (unsigned char)second, glyph(f, (unsigned char)second, &bits)) * sc;
  }
  return a;
}
#endif

/* width of the text, or of its first line only */
static int textw(const char *s, int f, int one_line) {
  int w = 0, best = 0, badge = 0;
  const uint8_t *bits;
  for (; *s; s++) {
    unsigned char ch = (unsigned char)*s;
    if (ch == '\n') {
      if (badge) w += 2, badge = 0;
      if (w > best) best = w;
      if (one_line) break;
      w = 0;
      continue;
    }
    if (ch < 32) {
      if (ch == 14 && !badge) w += 2, badge = 1;
      else if (badge && ch != 14) w += 2, badge = 0;
      continue;
    }
#if NP_TEXT_EXTRA
    if (ch >= 0xC0) {
      w += xadvance(f, &s);
      continue;
    }
#endif
    if (ch > (f & (T_LARGE | T_MED) ? 126 : 130)) ch = '?';
    w += advance(f, ch, glyph(f, ch, &bits));
  }
  if (badge) w += 2;
  if (w > best) best = w;
  if (best > 0) best--; /* no spacing after the last glyph */
  return best * ((f & T_X2) ? 2 : 1);
}
int g_textw(const char *s, int f) { return textw(s, f, 0); }

int g_lines(const char *s) {
  int n = 1;
  for (; *s; s++) n += *s == '\n';
  return n;
}

void g_text(const char *s, int x, int y, int f, C c) {
  if (!s || !*s) return;
  if (f & T_ROT) {
    /* one line turned: glyph rows go right, the text goes up from y */
    int len = g_textw(s, f & ~T_ROT), n = 0;
    while (s[n]) n++;
    if (strpos + n + 1 > (int)sizeof strbuf) return;
    if (f & T_CENTER) y += len / 2;
    item_t *it = add(I_TEXT, x - 2, y - len - 2, glyph_rows(f) + 4, len + 4);
    if (!it) return;
    char *d = strbuf + strpos;
    for (int i = 0; i <= n; i++) d[i] = s[i];
    strpos += n + 1;
    it->s = d, it->a = c, it->b = (uint16_t)f;
    return;
  }
  int w = g_textw(s, f) + 2;
  if (f & T_CENTER) x -= (w - 2) / 2;
  if (f & T_RIGHT) x -= w - 2;
  int h = g_pitch(f) * (g_lines(s) - 1) + glyph_rows(f) * ((f & T_X2) ? 2 : 1) + 3;
  int n = 0;
  while (s[n]) n++;
  if (strpos + n + 1 > (int)sizeof strbuf) return;
  /* (two rows more above in a language build, for accents over capitals) */
  item_t *it = add(I_TEXT, x - 2, y - 2 - XROWS, w + 4, h + 2 + XROWS);
  if (!it) return;
  char *d = strbuf + strpos;
  for (int i = 0; i <= n; i++) d[i] = s[i];
  strpos += n + 1;
  it->s = d, it->a = c, it->b = (uint16_t)f;
}

/* centred in a box, both ways, by the height of the capitals */
void g_text_box(const char *s, int x, int y, int w, int h, int f, C c) {
  int th = g_cap(f) + g_pitch(f) * (g_lines(s) - 1);
  g_text(s, x + w / 2, y + (h - th + 1) / 2, (f & ~(T_RIGHT)) | T_CENTER, c);
}

static void draw_text_rot(item_t *it, int y0, int ra, int rb, int cx0, int cx1) {
  int f = it->b, gh = glyph_rows(f);
  int x = it->x + 2, pen = it->y + it->h - 2; /* bottom */
#if NP_TEXT_EXTRA
  xplot_t xp = {y0, ra, rb, cx0, cx1, 0, 1, x, 0, it->a};
#endif
  for (const char *s = it->s; *s; s++) {
    unsigned char ch = (unsigned char)*s;
#if NP_TEXT_EXTRA
    if (ch >= 0xC0) {
      xp.oy = pen;
      pen -= xdraw(f, &s, 0, 0, it->a, &xp);
      continue;
    }
#endif
    if (ch < 32 || ch > 126) continue;
    const uint8_t *b;
    const glyph_t *g = glyph(f, ch, &b);
    int dc = drop_col(f, ch, g);
    for (int pass = (f & T_SHADOW) ? 0 : 1; pass < 2; pass++) {
      uint32_t bit = g->off;
      for (int gx = 0; gx < g->w; gx++)
        for (int gy = 0; gy < gh; gy++, bit++) {
          if (gx == dc || !((b[bit >> 3] >> (bit & 7)) & 1)) continue;
          int X = x + gy + (pass ? 0 : 1), Y = pen - 1 - (gx - (dc >= 0 && gx > dc));
          if (Y < ra || Y >= rb || X < cx0 || X >= cx1) continue;
          C *d = strip + (Y - y0) * 320 + X;
          *d = pass ? it->a : shade565(*d, 12);
        }
    }
    pen -= advance(f, ch, g);
  }
}

PROF_NOINLINE static void draw_text(item_t *it, int y0, int ra, int rb, int cx0, int cx1) {
  int y1 = rb;
  int f = it->b;
  if (f & T_ROT) {
    draw_text_rot(it, y0, ra, rb, cx0, cx1);
    return;
  }
  int sc = (f & T_X2) ? 2 : 1, gh = glyph_rows(f), cap = g_cap(f) / sc, pitch = g_pitch(f);
  int x0 = it->x + 2, x = x0, y = it->y + 2 + XROWS;
  int multi = 0;
#if NP_TEXT_EXTRA
  xplot_t xp = {y0, ra, y1, cx0, cx1, 0, 0, 0, 0, 0};
#endif
  for (const char *p = it->s; *p; p++) multi |= *p == '\n';
  C col = it->a, base = it->a;
  int badge = 0;
  /* centred lines: each line is centred on its own */
  int align = (f & (T_CENTER | T_RIGHT)) && multi, total = it->w - 4;
  if (align) {
    int lw = textw(it->s, f, 1);
    x = x0 + ((f & T_RIGHT) ? total - lw : (total - lw) / 2);
  }
  for (const char *s = it->s; *s; s++) {
    unsigned char ch = (unsigned char)*s;
    if (ch == '\n') {
      if (badge) badge = 0;
      y += pitch;
      x = x0;
      if (align) {
        int lw = textw(s + 1, f, 1);
        x = x0 + ((f & T_RIGHT) ? total - lw : (total - lw) / 2);
      }
      continue;
    }
    if (ch < 32) {
      if (badge && ch != 14) x += 2 * sc, badge = 0;
      if (ch == 16) col = base;
      else if (ch == 14) {
        /* red badge behind the following text, until the next code */
        const char *e = s + 1;
        int bw = 0;
        const uint8_t *bb;
        while (*e && (unsigned char)*e >= 32) {
#if NP_TEXT_EXTRA
          if ((unsigned char)*e >= 0xC0) {
            bw += xadvance(f, &e), e++;
            continue;
          }
#endif
          bw += advance(f, (unsigned char)*e, glyph(f, (unsigned char)*e, &bb)), e++;
        }
        bw = bw * sc + 3;
        int by0 = y - 2, by1 = y + cap * sc + 2;
        for (int yy = by0 > ra ? by0 : ra; yy < by1 && yy < y1; yy++) {
          C *d = strip + (yy - y0) * 320;
          int a = x, b = x + bw;
          if (yy == by0 || yy == by1 - 1) a++, b--;
          for (int xx = a < cx0 ? cx0 : a; xx < b && xx < cx1; xx++) d[xx] = HEXC(0xFE5F55);
        }
        x += 2 * sc;
        col = 0xFFFF;
        badge = 1;
      } else if (code_col[ch]) col = code_col[ch];
      continue;
    }
#if NP_TEXT_EXTRA
    if (ch >= 0xC0) {
      x += xdraw(f, &s, x, y, col, &xp);
      continue;
    }
#endif
    if (ch > (f & (T_LARGE | T_MED) ? 126 : 130)) ch = '?';
    const uint8_t *b;
    const glyph_t *g = glyph(f, ch, &b);
    uint32_t bit;
    int dc = drop_col(f, ch, g);
    if (f & (T_SHADOW | T_OUTLINE)) {
      bit = g->off;
      for (int gx = 0; gx < g->w; gx++) {
        if (gx == dc) {
          bit += gh;
          continue;
        }
        for (int gy = 0; gy < gh; gy++, bit++) {
          if (!((b[bit >> 3] >> (bit & 7)) & 1)) continue;
          int px = x + (gx - (dc >= 0 && gx > dc)) * sc, py = y + gy * sc + sc;
          for (int k = 0; k < sc * sc; k++) {
            int X = px + (k % sc), Y = py + (k / sc);
            if (Y >= ra && Y < y1 && X >= cx0 && X < cx1) {
              C *d = strip + (Y - y0) * 320 + X;
              *d = shade565(*d, 12);
            }
          }
        }
      }
    }
    bit = g->off;
    for (int gx = 0; gx < g->w; gx++) {
      if (gx == dc) {
        bit += gh;
        continue;
      }
      for (int gy = 0; gy < gh; gy++, bit++) {
        if (!((b[bit >> 3] >> (bit & 7)) & 1)) continue;
        int px = x + (gx - (dc >= 0 && gx > dc)) * sc, py = y + gy * sc;
        for (int k = 0; k < sc * sc; k++) {
          int X = px + (k % sc), Y = py + (k / sc);
          if (Y >= ra && Y < y1 && X >= cx0 && X < cx1) strip[(Y - y0) * 320 + X] = col;
        }
      }
    }
    x += advance(f, ch, g) * sc;
  }
}

/* ---------------------------------------------------------------- sprites */
static C fx_pixel(C c, int fx, int x, int y, int w, int h, float seed) {
  int ed = fx & FX_EDMASK;
  if (ed) c = fx_edition(c, ed, x, y, w, h, seed);
  if (fx & FX_GREY) {
    int l = ((c >> 11) * 2 + ((c >> 5) & 63) + (c & 31) * 2) / 5;
    c = (C)(l << 11 | (l * 2) << 5 | l);
  }
  if (fx & FX_DIM) c = shade565(c, 14);
  return c;
}

PROF_NOINLINE static void draw_sprite(item_t *it, int y0, int ra, int rb, int cx0, int cx1) {
  int id = it->a;
  const uint8_t *px = art_peek(id);
  if (!px) return;
  const art_sprite_t *s = &art_sprites[id];
  const uint16_t *pal = art_pal + art_pal_off[s->group];
  int w = it->w, h = it->h, sw = s->w, sh = s->h, fx = it->fx;
  int ya = it->y > ra ? it->y : ra, yb = it->y + h < rb ? it->y + h : rb;
  int xa = it->x > cx0 ? it->x : cx0, xb = it->x + w < cx1 ? it->x + w : cx1;
  if (xa >= xb) return;
  int same = (w == sw && h == sh);
  float seed = (float)it->x * 0.013f; /* the sheen differs along the row, the same on a card's layers */
  uint32_t stepx = ((uint32_t)sw << 16) / (uint32_t)w, stepy = ((uint32_t)sh << 16) / (uint32_t)h;
  for (int y = ya; y < yb; y++) {
    int sy = same ? y - it->y : (int)(((uint32_t)(y - it->y) * stepy) >> 16);
    const uint8_t *row = px + sy * sw;
    C *d = strip + (y - y0) * 320;
    if (same && !fx) { /* most sprites: a straight copy */
      const uint8_t *r = row + (xa - it->x);
      for (int x = xa; x < xb; x++, r++)
        if (*r) d[x] = pal[*r];
      continue;
    }
    if (same && fx == FX_SHADOW) { /* card shadows */
      const uint8_t *r = row + (xa - it->x);
      for (int x = xa; x < xb; x++, r++)
        if (*r) d[x] = shade565(d[x], 11);
      continue;
    }
    for (int x = xa; x < xb; x++) {
      int sx = same ? x - it->x : (int)(((uint32_t)(x - it->x) * stepx) >> 16);
      if (fx & FX_FLIPX) sx = sw - 1 - sx;
      int v = row[sx];
      if (!v) continue;
      if (fx & FX_SHADOW) {
        d[x] = shade565(d[x], 11);
        continue;
      }
      if (fx & FX_WHITE) {
        d[x] = 0xFFFF;
        continue;
      }
      d[x] = fx_pixel(pal[v], fx, sx, sy, sw, sh, seed);
    }
  }
}

/* ------------------------------------------------------------------ shapes */
static inline C shade_px(C d, int a) { return shade565(d, a); }

PROF_NOINLINE static void draw_rrect(item_t *it, int y0, int ra, int rb, int cx0, int cx1, int shade) {
  int r = it->r, x = it->x, w = it->w, h = it->h;
  int ya = it->y > ra ? it->y : ra, yb = it->y + h < rb ? it->y + h : rb;
  C c = it->a;
  int xa = x > cx0 ? x : cx0, xb = x + w < cx1 ? x + w : cx1;
  for (int yy = ya; yy < yb; yy++) {
    int j = yy - it->y, cy = -1;
    if (j < r) cy = j;
    else if (j >= h - r) cy = h - 1 - j;
    C *d = strip + (yy - y0) * 320;
    /* the solid middle, then the rounded ends */
    int ma = xa, mb = xb;
    if (cy >= 0) {
      if (ma < x + r) ma = x + r;
      if (mb > x + w - r) mb = x + w - r;
    }
    if (shade)
      for (int xx = ma; xx < mb; xx++) d[xx] = shade_px(d[xx], c);
    else
      for (int xx = ma; xx < mb; xx++) d[xx] = c;
    if (cy < 0) continue;
    for (int side = 0; side < 2; side++) {
      int ea = side ? (mb > xa ? mb : xa) : xa, eb = side ? xb : (ma < xb ? ma : xb);
      for (int xx = ea; xx < eb; xx++) {
        int i = xx - x;
        int cx = i < r ? i : w - 1 - i;
        int a = cov[r][cy][cx];
        if (!a) continue;
        if (shade) d[xx] = shade_px(d[xx], (a * c) >> 5);
        else d[xx] = a == 32 ? c : mix565(d[xx], c, a);
      }
    }
  }
}

PROF_NOINLINE static void draw_rect(item_t *it, int y0, int ra, int rb, int cx0, int cx1) {
  int ya = it->y > ra ? it->y : ra, yb = it->y + it->h < rb ? it->y + it->h : rb;
  int xa = it->x > cx0 ? it->x : cx0, xb = it->x + it->w < cx1 ? it->x + it->w : cx1;
  C c = it->a;
  for (int y = ya; y < yb; y++) {
    C *d = strip + (y - y0) * 320;
    for (int x = xa; x < xb; x++) d[x] = c;
  }
}

void g_bg(int swirl, C solid) {
  bg_swirl = swirl;
  bg_solid = solid;
}

uint32_t g_bg_version; /* changes whenever the background looks different */
static uint32_t last_hash;

static uint32_t frame_hash(void) {
  uint32_t h = 2166136261u ^ g_bg_version;
  const uint8_t *p = (const uint8_t *)items;
  for (int i = 0; i < nitems * (int)sizeof(item_t); i++) {
    if ((i % sizeof(item_t)) >= offsetof(item_t, s)) continue; /* not the pointers */
    h = (h ^ p[i]) * 16777619u;
  }
  for (int i = 0; i < strpos; i++) h = (h ^ (uint8_t)strbuf[i]) * 16777619u;
  for (int i = 0; i < nitems; i++)
    if ((items[i].type == I_SPRITE && (items[i].fx & FX_EDMASK)) || items[i].type == I_FLAME) {
      h ^= g_time / 33; /* animated editions and flames */
      break;
    }
  return h * 31 + (uint32_t)nitems;
}

void g_end(void) {
  /* nothing changed since the last frame: keep the screen as it is */
  uint32_t h = frame_hash();
  if (h == last_hash) return;
  last_hash = h;
  fx_frame(g_time);
  /* where each sprite is used last, so the cache can free it after */
  art_begin_frame();
  for (int i = 0; i < nitems; i++)
    if (items[i].type == I_SPRITE) art_use(items[i].a, items[i].y, items[i].y + items[i].h);
  /* the flames' smoke, once for the frame (on the stack) */
  uint16_t fgrid[2][FLAME_GR * FLAME_GC];
  for (int i = 0; i < nitems; i++)
    if (items[i].type == I_FLAME) flame_grid(items[i].a & 1, items[i].w, items[i].h, fgrid[items[i].a & 1]);
  for (int y0 = 0; y0 < 240; y0 += SH) {
    int y1 = y0 + SH;
    /* the sprites of this strip (decoded now if needed) */
    for (int i = 0; i < nitems; i++) {
      item_t *it = &items[i];
      if (it->type == I_SPRITE && it->y < y1 && it->y + it->h > y0) art_get_at(it->a, y0);
    }
    if (bg_swirl)
      for (int y = y0; y < y1; y++) bg_row(strip + (y - y0) * 320, y, bg_cover_x, 320);
    else
      for (int i = 0; i < 320 * SH; i++) strip[i] = bg_solid;
    for (int i = 0; i < nitems; i++) {
      item_t *it = &items[i];
      if (it->y >= y1 || it->y + it->h <= y0) continue;
      const int16_t *c = clips[it->clip];
      int cy0 = y0 > c[1] ? y0 : c[1], cy1 = y1 < c[3] ? y1 : c[3];
      if (cy0 >= cy1) continue;
      switch (it->type) {
        case I_RECT: draw_rect(it, y0, cy0, cy1, c[0], c[2]); break;
        case I_RRECT: draw_rrect(it, y0, cy0, cy1, c[0], c[2], 0); break;
        case I_SHADE: draw_rrect(it, y0, cy0, cy1, c[0], c[2], 1); break;
        case I_SPRITE: draw_sprite(it, y0, cy0, cy1, c[0], c[2]); break;
        case I_TEXT: draw_text(it, y0, cy0, cy1, c[0], c[2]); break;
        case I_RSQ: draw_rsq(it, y0, cy0, cy1, c[0], c[2]); break;
        case I_FLAME:
          flame_rows(it->a & 1, it->x, it->y, it->w, it->h, it->b, fgrid[it->a & 1], strip, y0, cy0, cy1, c[0], c[2]);
          break;
      }
    }
    eadk_display_push_rect((eadk_rect_t){0, (uint16_t)y0, 320, SH}, strip);
  }
}
