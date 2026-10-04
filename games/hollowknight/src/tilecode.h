/* The scenery tiles' coding: a range coder (LZMA's) with adaptive probabilities that start from tables trained on all
 * tiles, each with how fast it adapts at first. Blocks of tiles are coded on their own.
 * A texel's context is its neighbors: two to its left, three above (up-left, up, up-right). Across a tile's edges they
 * are those of the tiles to its left and above when those are in the block (before it), else 0 to the left and above
 * nothing: the top row is then coded on its own, with the two to the left.
 *   4 bits: is it the texel to its left? (context: that one, how the others compare), else the one above? (context: the
 *   two, how the one up-left compares), else its value in 4 binary steps (context: the two, in either order).
 *   2 bits: its value in 2 binary steps, the 4 neighbors as context.
 * Shared by the game (decoding: tex.c) and tools/tilecode.c (training, encoding). */
#pragma once
#include <stdint.h>

#define TC_PROB_BITS 12
/* the probabilities (uint16), 4-bit tiles */
#define TC_FL 0        /* "the one to the left": 16 x 64 */
#define TC_F0 1024     /* ... in a top row on its own: 16 x 2 */
#define TC_FU 1056     /* "the one above": 240 pairs x 4 */
#define TC_TREE 2016   /* the value: 136 pairs x 15 nodes */
#define TC_N4 4056
/* 2-bit tiles: 256 x 3, then the top row on its own 16 x 3 */
#define TC_C20 768
#define TC_N2 816

/* a tile as coded: rows 3 texels wider than it (2 of the tile to the left, 1 more: a copy of the last), and a row of
 * the tile above first */
#define TC_STRIDE(w) ((w) + 3)

/* a probability: 12 bits (of a 0), then its state: how much a bit moves it (a shift), and the state after. States 0
 * and 3 settle at 1/8 and 1/16 */
static const uint8_t tc_state[8] = {0x11, 0x22, 0x32, 0x14, 0x25, 0x36, 0x46, 0x46};

static inline uint16_t tc_update(uint16_t pv, int bit) {
  int p = pv >> 4, st = tc_state[pv & 7], sh = st >> 4;
  if (!bit) p += (4096 - p) >> sh;
  else p -= p >> sh;
  return (uint16_t)(p << 4 | (st & 15));
}

/* contexts, from the neighbors: l, ll to the left, u above, ul, ur above to the left and right */
static inline int tc_fl(int l, int u, int ul, int ur, int ll) {
  return TC_FL + (l << 6 | (l == u) | (ul == u) << 1 | (ur == l) << 2 | (ll == l) << 3 | (u == 0) << 4 | (ur == 0) << 5);
}
static inline int tc_f0(int l, int ll) { return TC_F0 + (l << 1 | (ll == l)); }
static inline int tc_fu(int l, int u, int ul) {   /* (u != l) */
  return TC_FU + ((l * 15 + u - (u > l)) << 2 | (ul == l) | (ul == u) << 1);
}
static inline int tc_tree(int l, int u) {   /* + the node (1..15) */
  int a = l > u ? l : u, b = l + u - a;
  return TC_TREE - 1 + (a * (a + 1) / 2 + b) * 15;
}
/* 2 bits: + 0, then + 1 + the first bit */
static inline int tc_c2(int l, int u, int ul, int ur) { return (l << 6 | u << 4 | ul << 2 | ur) * 3; }
static inline int tc_c20(int l, int ll) { return TC_C20 + (l << 2 | ll) * 3; }
