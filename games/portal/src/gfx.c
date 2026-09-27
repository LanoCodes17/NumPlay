/* Drawing, the same way the original does it: opaque sprites and 8x8
 * characters written straight to the screen, x on even pixels. */
#include "portal.h"
#include "assets.h"
#include "inflate.h"

/* the art and levels, unpacked at start */
uint8_t assets[ASSETS_SIZE];
void assets_init(void) { inflate_raw(ASSETS_Z, sizeof ASSETS_Z, assets, ASSETS_SIZE); }

u16 pal[16];
u16 text_fg, text_bg, screen_bg;
static u16 buf[320];

/* While a hint sign is up, nothing is drawn over it: rows SIGN_Y0 to
 * SIGN_Y1 - 1 between clip_x0 and clip_x1 are skipped. */
enum { SIGN_Y0 = 1, SIGN_Y1 = 15 };
static int clip_x0, clip_x1;
void sign_clip(int x0, int x1) { clip_x0 = x0, clip_x1 = x1; }

/* p == NULL: a uniform fill of color c */
static void out(int x, int y, int w, int h, const u16 *p, u16 c) {
  eadk_rect_t r = {x, y, w, h};
  if (p) eadk_display_push_rect(r, p);
  else eadk_display_push_rect_uniform(r, c);
}

static void push_c(int x, int y, int w, int h, const u16 *p, u16 c) {
  if (clip_x1 <= clip_x0 || y >= SIGN_Y1 || y + h <= SIGN_Y0 || x >= clip_x1 || x + w <= clip_x0) {
    out(x, y, w, h, p, c);
    return;
  }
  for (int j = 0; j < h; j++) { /* row by row around the sign */
    int yy = y + j;
    const u16 *q = p ? p + j * w : 0;
    if (yy < SIGN_Y0 || yy >= SIGN_Y1) {
      out(x, yy, w, 1, q, c);
      continue;
    }
    if (x < clip_x0) out(x, yy, clip_x0 - x, 1, q, c);
    if (x + w > clip_x1) out(clip_x1, yy, x + w - clip_x1, 1, q ? q + (clip_x1 - x) : 0, c);
  }
}

static void push(int x, int y, int w, int h, const u16 *p) { push_c(x, y, w, h, p, 0); }

void fill(int x, int y, int w, int h, u16 c) {
  if (x < 0) w += x, x = 0;
  if (y < 0) h += y, y = 0;
  if (x + w > 320) w = 320 - x;
  if (y + h > 240) h = 240 - y;
  if (w > 0 && h > 0) push_c(x, y, w, h, 0, c);
}

void cls(void) { fill(0, 0, 320, 240, screen_bg); }

/* 4 bpp sprite: (height, bytes per row) then rows, left pixel in the high
 * nibble. Clipped to the screen (the original never needs it). */
void spr(const u8 *s, int x2, int y) {
  int h = s[0], wb = s[1], w = wb * 2, x = x2 * 2;
  const u8 *d = s + 2;
  if (x >= 320 || y >= 240 || x + w <= 0) return;
  for (int j = 0; j < h; j++, d += wb) {
    int yy = y + j;
    if (yy < 0) continue;
    if (yy >= 240) break;
    u16 *o = buf;
    for (int i = 0; i < wb; i++) {
      *o++ = pal[d[i] >> 4];
      *o++ = pal[d[i] & 15];
    }
    int x0 = x, n = w, skip = 0;
    if (x0 < 0) skip = -x0, n += x0, x0 = 0;
    if (x0 + n > 320) n = 320 - x0;
    push(x0, yy, n, 1, buf + skip);
  }
}

const u8 *tile_sprite(int t) {
  if (t < 1 || t > 31) t = 2;
  return TILES + (t - 1) * TILE_BYTES;
}

/* A 16x16 tile in one transfer. */
void tile_draw(int t, int col, int row) {
  static u16 tb[256];
  const u8 *d = tile_sprite(t) + 2;
  for (int i = 0; i < 128; i++) {
    tb[2 * i] = pal[d[i] >> 4];
    tb[2 * i + 1] = pal[d[i] & 15];
  }
  push(col * 16, row * 16, 16, 16, tb);
}

/* 1 bpp picture (title art): set bits in the text color. */
void spr1(const u8 *s, int x2, int y) {
  int h = s[0], wb = s[1];
  const u8 *d = s + 2;
  for (int j = 0; j < h; j++, d += wb) {
    for (int i = 0; i < wb * 8; i++) buf[i] = d[i >> 3] >> (7 - (i & 7)) & 1 ? text_fg : text_bg;
    push(x2 * 2, y + j, wb * 8, 1, buf);
  }
}

void glyph(char c, int x2, int y) {
  static u16 gb[64];
  int i = (u8)c - 0x20;
  if (i < 0 || i >= FONT_CHARS) i = 0;
  const u8 *g = FONT + i * 8;
  for (int j = 0; j < 64; j++) gb[j] = g[j >> 3] >> (7 - (j & 7)) & 1 ? text_fg : text_bg;
  push(x2 * 2, y, 8, 8, gb);
}

/* Characters every 10 pixels (5 units), like the original. */
int text(const char *s, int x2, int y) {
  for (; *s; s++, x2 += 5) glyph(*s, x2, y);
  return x2;
}

int number(int v, int x2, int y) {
  char t[6];
  int n = 0;
  do t[n++] = '0' + v % 10; while (v /= 10);
  while (n) glyph(t[--n], x2, y), x2 += 5;
  return x2;
}
