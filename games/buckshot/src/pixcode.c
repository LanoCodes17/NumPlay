/* Buckshot's picture decoder (tools/pixcode.py encodes).
 *
 * A binary range coder with adaptive probabilities: each pixel is the same
 * as its left neighbour, or else the same as the one above, or else a
 * palette index read bit by bit. The contexts are how the neighbours agree
 * and how long the current run is. Pictures are decoded row by row straight
 * into the frame buffer (two rows of history), transparent index 0 skipped
 * for sprites, optionally darkened through a map or drawn doubled.
 *
 * This runs for every picture of every frame, so it is written for speed:
 * the coder's state lives in registers, the neighbours slide along the row,
 * and rows that lie wholly on screen are written without clipping. */
#include "gfx.h"

#define PROB_BITS 12
#define ONE (1u << PROB_BITS)
#define FLAG_SHIFT 4
#define LIT_SHIFT 5
#define LIT_BITS 7

static uint8_t rows[2][GFX_W];
static uint8_t vert[GFX_W]; /* how each pixel above agrees with its neighbours */

/* one binary decision with probability p (of a 0), adapted by `shift` */
#define BIT(p, shift, out)                        \
  do {                                            \
    uint32_t bound_ = (range >> PROB_BITS) * (p); \
    if (code < bound_) {                          \
      range = bound_;                             \
      (p) += (ONE - (p)) >> (shift);              \
      (out) = 0;                                  \
    } else {                                      \
      range -= bound_;                            \
      code -= bound_;                             \
      (p) -= (p) >> (shift);                      \
      (out) = 1;                                  \
    }                                             \
    while (range < (1u << 24)) {                  \
      range <<= 8;                                \
      code = code << 8 | *in++;                   \
    }                                             \
  } while (0)

__attribute__((optimize("O2"))) void gfx_decode(const uint8_t *src, uint8_t *fb, int x0, int y0, int w, int h, int opaque,
                                                const uint8_t *map, int scale) {
  if (w <= 0 || w > GFX_W) return;
  const uint8_t *in = src + 4;
  uint32_t code = (uint32_t)src[0] << 24 | (uint32_t)src[1] << 16 | (uint32_t)src[2] << 8 | src[3];
  uint32_t range = 0xFFFFFFFFu;
  uint16_t pa[16], pb[8], pl[1 << LIT_BITS];
  for (int i = 0; i < 16; i++) pa[i] = ONE / 2;
  for (int i = 0; i < 8; i++) pb[i] = ONE / 2;
  for (int i = 0; i < (1 << LIT_BITS); i++) pl[i] = ONE / 2;
  unsigned run = 0; /* 8 when the pixel before was the same as its left */
  for (int y = 0; y < h; y++) {
    uint8_t *row = rows[y & 1];
    const uint8_t *prev = rows[(y & 1) ^ 1];
    /* above the picture, every neighbour is the pixel on the left (see
     * tools/pixcode.py); below its first row: up == ul, up == ur of each
     * pixel, worked out once for the row */
    int left;
    if (y) {
      left = prev[0];
      unsigned pl_ = prev[0];
      for (int x = 0; x < w; x++) {
        unsigned c = prev[x], r = x + 1 < w ? prev[x + 1] : c;
        vert[x] = (uint8_t)((c == pl_) << 1 | (c == r) << 2);
        pl_ = c;
      }
    } else {
      left = 0;
    }
    for (int x = 0; x < w; x++) {
      int up = y ? prev[x] : left;
      unsigned ctx = (y ? vert[x] : 6u) | (left == up) | run, b;
      BIT(pa[ctx], FLAG_SHIFT, b);
      if (b) {
        run = 8;
        row[x] = (uint8_t)left;
        continue;
      }
      run = 0;
      b = 0;
      if (up != left) {
        int ul = x ? prev[x - 1] : up;
        BIT(pb[(vert[x] >> 1 & 3) | (left == ul) << 2], FLAG_SHIFT, b);
      }
      if (b) {
        left = up;
      } else {
        unsigned node = 1;
        for (int i = 0; i < LIT_BITS; i++) {
          unsigned bit;
          BIT(pl[node], LIT_SHIFT, bit);
          node = node << 1 | bit;
        }
        left = (int)node - (1 << LIT_BITS);
      }
      row[x] = (uint8_t)left;
    }
    /* the row, into the frame buffer */
    for (int k = 0; k < scale; k++) {
      int py = y0 + y * scale + k;
      if ((unsigned)py >= GFX_H) continue;
      uint8_t *d = fb + py * GFX_W;
      if (scale == 1 && x0 >= 0 && x0 + w <= GFX_W) {
        d += x0;
        if (opaque && !map) {
          for (int x = 0; x < w; x++) d[x] = row[x];
        } else {
          for (int x = 0; x < w; x++) {
            uint8_t c = row[x];
            if (c || opaque) d[x] = map ? map[c] : c;
          }
        }
        continue;
      }
      for (int x = 0; x < w; x++) {
        uint8_t c = row[x];
        if (!c && !opaque) continue;
        if (map) c = map[c];
        for (int i = 0; i < scale; i++) {
          int px = x0 + x * scale + i;
          if ((unsigned)px < GFX_W) d[px] = c;
        }
      }
    }
  }
}
