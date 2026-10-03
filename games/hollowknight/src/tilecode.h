/* The scenery tiles' coding: each texel coded in binary steps with the texel to its left and the one above as
 * context, by a range coder. Probabilities start from tables trained on all tiles and adapt quickly within a block.
 * Shared by the game (decoding) and tools/tilecode.c (training, encoding). */
#pragma once
#include <stdint.h>

/* tiles are 16 texels high, 16 wide (4 bits) or 32 (2 bits) */
#define TC_TILE 16
#define TC_PROB_BITS 12
#define TC_LIM 4                 /* the slowest adaptation (shift) */
#define TC_N0 1                  /* how sure the trained probabilities are at first */
/* 4-bit tiles: 256 contexts x 16 nodes (1..15 used); 2-bit tiles: 16 contexts x 4 nodes (1..3 used) */
#define TC_CTX4 256
#define TC_CTX2 16

/* a probability: 12 bits (of a 0) and a 4-bit count of updates */
static inline uint16_t tc_pack(int p, int n) { return (uint16_t)(p << 4 | n); }

static inline int tc_left(const uint8_t *img, int x, int y, int w) {
  return x ? img[y * w + x - 1] : (y ? img[(y - 1) * w] : 0);
}
static inline int tc_up(const uint8_t *img, int x, int y, int left, int w) { return y ? img[(y - 1) * w + x] : left; }

static inline void tc_update(uint16_t *pr, int bit) {
  int p = *pr >> 4, n = *pr & 15, sh = n + 1 < TC_LIM ? n + 1 : TC_LIM;
  if (!bit) p += (4096 - p) >> sh;
  else p -= p >> sh;
  if (n < 15) n++;
  *pr = tc_pack(p, n);
}
