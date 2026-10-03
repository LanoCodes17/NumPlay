/* Scenery tile coding for pack.py (the model: src/tilecode.h): trains the starting probabilities and encodes blocks.
 *   tilecode train IN PRIOR      (PRIOR: the tables as the game starts them, TC_N4 then TC_N2 uint16)
 *   tilecode encode IN PRIOR OUT
 * IN: u32 blocks, then per block: u8 bits (4 or 2), u8 0, u16 tiles, per tile the one to its left and the one above in
 * the block (u8 each, 255: none), then tiles x 256 (or 512, 2 bits) texels (one a byte).
 * OUT: per block, u32 size and its bytes. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/tilecode.h"

typedef struct {
  int bits, n;
  uint8_t *nb, *tex;
} Block;

static Block *blocks;
static int nblocks;

static void load(const char *path) {
  FILE *f = fopen(path, "rb");
  if (!f) exit(1);
  uint32_t n;
  if (fread(&n, 4, 1, f) != 1) exit(1);
  nblocks = (int)n;
  blocks = calloc(n, sizeof *blocks);
  for (uint32_t i = 0; i < n; i++) {
    uint8_t h[4];
    if (fread(h, 1, 4, f) != 4) exit(1);
    blocks[i].bits = h[0];
    blocks[i].n = h[2] | h[3] << 8;
    size_t sz = blocks[i].bits == 2 ? 512 : 256;
    blocks[i].nb = malloc((size_t)blocks[i].n * 2);
    blocks[i].tex = malloc((size_t)blocks[i].n * sz);
    if (fread(blocks[i].nb, 2, (size_t)blocks[i].n, f) != (size_t)blocks[i].n) exit(1);
    if (fread(blocks[i].tex, sz, (size_t)blocks[i].n, f) != (size_t)blocks[i].n) exit(1);
  }
  fclose(f);
}

/* ---------------------------------------------------------------- the model: a tile's binary steps */
static void (*code_bit)(int slot, int bit);   /* (training: noted; encoding: coded) */

static void tree4(int base, int v) {
  for (int k = 3, node = 1; k >= 0; k--) {
    int bit = v >> k & 1;
    code_bit(base + node, bit);
    node = node * 2 + bit;
  }
}
static void tree2(int base, int v) {
  code_bit(base, v >> 1);
  code_bit(base + 1 + (v >> 1), v & 1);
}

static void code_tile(const Block *B, int t) {
  int w = B->bits == 2 ? 32 : 16, s = TC_STRIDE(w);
  const uint8_t *tex = B->tex + (size_t)(w * 16) * t;
  const uint8_t *lt = B->nb[2 * t] != 255 ? B->tex + (size_t)(w * 16) * B->nb[2 * t] : NULL;
  const uint8_t *ut = B->nb[2 * t + 1] != 255 ? B->tex + (size_t)(w * 16) * B->nb[2 * t + 1] : NULL;
  /* the tile in its borders (as the decoder has them: the row above and the copied last column as each row is done) */
  uint8_t img[17 * TC_STRIDE(32)];
  memset(img, 0, sizeof img);
  for (int y = 0; y < 16; y++) {
    uint8_t *r = img + (y + 1) * s + 2;
    if (lt) r[-2] = lt[y * w + w - 2], r[-1] = lt[y * w + w - 1];
    memcpy(r, tex + y * w, (size_t)w);
    r[w] = r[w - 1];
  }
  if (ut) {
    uint8_t *a = img + 2;
    memcpy(a, ut + 15 * w, (size_t)w);
    a[-1] = a[s - 1], a[w] = a[w - 1];
  }
  for (int y = 0; y < 16; y++) {
    const uint8_t *r = img + (y + 1) * s + 2, *a = r - s;
    for (int x = 0; x < w; x++) {
      int v = r[x], l = r[x - 1], ll = r[x - 2];
      if (!y && !ut) {   /* the top row on its own */
        if (w == 32) tree2(tc_c20(l, ll), v);
        else {
          code_bit(tc_f0(l, ll), v != l);
          if (v != l) tree4(tc_tree(l, l), v);
        }
        continue;
      }
      int u = a[x], ul = a[x - 1], ur = a[x + 1];
      if (w == 32) {
        tree2(tc_c2(l, u, ul, ur), v);
        continue;
      }
      code_bit(tc_fl(l, u, ul, ur, ll), v != l);
      if (v == l) continue;
      if (u != l) {
        code_bit(tc_fu(l, u, ul), v != u);
        if (v == u) continue;
      }
      tree4(tc_tree(l, u), v);
    }
  }
}

/* ---------------------------------------------------------------- training */
/* every binary step, with its table (4-bit tiles' first, then the 2-bit tiles' after TC_N4) */
#define NSLOTS (TC_N4 + TC_N2)
static uint16_t *st_slot;
static uint8_t *st_bit;
static int *st_block;
static size_t nst, capst;
static int cur_block, cur_base;

static void note_bit(int slot, int bit) {
  if (nst == capst) {
    capst = capst ? capst * 2 : 1 << 20;
    st_slot = realloc(st_slot, capst * sizeof *st_slot), st_bit = realloc(st_bit, capst);
    st_block = realloc(st_block, capst * sizeof *st_block);
  }
  st_slot[nst] = (uint16_t)(cur_base + slot), st_bit[nst] = (uint8_t)bit, st_block[nst] = cur_block, nst++;
}

static double lg[4097];

/* the cost (bits) of a table's steps (bits, each block's first marked) started at p0 in state n0 */
static double run_cost(const uint8_t *bits, const uint8_t *first, size_t n, int p0, int n0) {
  double c = 0;
  uint16_t pv = 0;
  for (size_t i = 0; i < n; i++) {
    if (first[i]) pv = (uint16_t)(p0 << 4 | n0);
    int p = pv >> 4;
    c += bits[i] ? lg[4096 - p] : lg[p];
    pv = tc_update(pv, bits[i]);
  }
  return c;
}

static uint16_t prior[NSLOTS];

/* the best p0 for a state n0 (the cost is about convex in p0: a golden section search), and its cost */
static int best_p0(const uint8_t *bits, const uint8_t *first, size_t n, int n0, double *cost) {
  int a = 1, b = 4095, c = b - (b - a) * 618 / 1000, d = a + (b - a) * 618 / 1000;
  double fc = run_cost(bits, first, n, c, n0), fd = run_cost(bits, first, n, d, n0);
  while (b - a > 8 && c < d) {
    if (fc < fd) b = d, d = c, fd = fc, c = b - (b - a) * 618 / 1000, fc = run_cost(bits, first, n, c, n0);
    else a = c, c = d, fc = fd, d = a + (b - a) * 618 / 1000, fd = run_cost(bits, first, n, d, n0);
  }
  int best = a;
  *cost = 1e300;
  for (int p0 = a; p0 <= b; p0++) {
    double f = run_cost(bits, first, n, p0, n0);
    if (f < *cost) *cost = f, best = p0;
  }
  return best;
}

/* each table's start: the probability and state that code its steps in every block best */
static void train(void) {
  code_bit = note_bit;
  for (int b = 0; b < nblocks; b++) {
    cur_block = b, cur_base = blocks[b].bits == 2 ? TC_N4 : 0;
    for (int t = 0; t < blocks[b].n; t++) code_tile(&blocks[b], t);
  }
  for (int i = 1; i <= 4096; i++) lg[i] = -log2(i / 4096.0);
  /* the steps by table, in order */
  size_t *at = calloc(NSLOTS + 1, sizeof *at);
  for (size_t i = 0; i < nst; i++) at[st_slot[i] + 1]++;
  for (int s = 0; s < NSLOTS; s++) at[s + 1] += at[s];
  size_t *pos = malloc(NSLOTS * sizeof *pos);
  memcpy(pos, at, NSLOTS * sizeof *pos);
  uint8_t *bits = malloc(nst), *first = malloc(nst);
  int *last = malloc(NSLOTS * sizeof *last), *k = calloc(NSLOTS, sizeof *k);
  for (int s = 0; s < NSLOTS; s++) last[s] = -1;
  for (size_t i = 0; i < nst; i++) {
    int s = st_slot[i];
    if (last[s] != st_block[i]) last[s] = st_block[i], k[s] = 0;
    if (k[s]++ >= 1024) continue;   /* (the start long forgotten: the rest costs the same whatever it was) */
    bits[pos[s]] = st_bit[i], first[pos[s]] = k[s] == 1, pos[s]++;
  }
  for (int s = 0; s < NSLOTS; s++) {
    size_t a = at[s], n = pos[s] - a;
    prior[s] = (uint16_t)(2048 << 4);
    if (!n) continue;
    double best = 1e300, c;
    for (int n0 = 0; n0 < 7; n0++) {
      int p0 = best_p0(bits + a, first + a, n, n0, &c);
      if (c < best) best = c, prior[s] = (uint16_t)(p0 << 4 | n0);
    }
  }
  free(at), free(pos), free(bits), free(first), free(last), free(k);
}

/* ---------------------------------------------------------------- range coder (LZMA's) */
static uint8_t *out;
static size_t outn, outcap;
static uint64_t low;
static uint32_t range;
static uint8_t cache;
static uint64_t cache_size;

static void put(uint8_t b) {
  if (outn == outcap) out = realloc(out, outcap = outcap ? outcap * 2 : 4096);
  out[outn++] = b;
}
static void shift_low(void) {
  if ((uint32_t)low < 0xFF000000u || (low >> 32)) {
    uint8_t temp = cache;
    do {
      put((uint8_t)(temp + (uint8_t)(low >> 32)));
      temp = 0xFF;
    } while (--cache_size);
    cache = (uint8_t)(low >> 24);
  }
  cache_size++;
  low = (low & 0x00FFFFFFu) << 8;
}

static uint16_t probs[TC_N4];

static void enc_bit(int slot, int bit) {
  uint16_t *pr = &probs[slot];
  uint32_t bound = (range >> TC_PROB_BITS) * (uint32_t)(*pr >> 4);
  if (!bit) range = bound;
  else low += bound, range -= bound;
  while (range < (1u << 24)) range <<= 8, shift_low();
  *pr = tc_update(*pr, bit);
}

static void encode_block(const Block *B) {
  low = 0, range = 0xFFFFFFFFu, cache = 0, cache_size = 1, outn = 0;
  if (B->bits == 2) memcpy(probs, prior + TC_N4, TC_N2 * 2);
  else memcpy(probs, prior, TC_N4 * 2);
  code_bit = enc_bit;
  for (int t = 0; t < B->n; t++) code_tile(B, t);
  /* the end: the value with the most free bytes after it that is in the last range (whatever follows the block
   * then, it decodes the same: the game reads up to 3 bytes past it) */
  for (int k = 1; k <= 4; k++) {
    uint64_t g = 1ull << (32 - 8 * k), v = (low + g - 1) & ~(g - 1);
    if (v + g <= low + range) {
      low = v;
      for (int i = 0; i <= k; i++) shift_low();
      break;
    }
  }
  /* (the first byte is always 0: not kept) */
  if (!outn || out[0]) exit(2);
  memmove(out, out + 1, --outn);
}

int main(int argc, char **argv) {
  if (argc < 4) return 1;
  load(argv[2]);
  if (!strcmp(argv[1], "train")) {
    train();
    FILE *f = fopen(argv[3], "wb");
    fwrite(prior, sizeof prior, 1, f);
    fclose(f);
    return 0;
  }
  if (!strcmp(argv[1], "encode") && argc >= 5) {
    FILE *f = fopen(argv[3], "rb");
    if (!f || fread(prior, sizeof prior, 1, f) != 1) return 1;
    fclose(f);
    FILE *o = fopen(argv[4], "wb");
    for (int b = 0; b < nblocks; b++) {
      encode_block(&blocks[b]);
      uint32_t n = (uint32_t)outn;
      fwrite(&n, 4, 1, o);
      fwrite(out, 1, outn, o);
    }
    fclose(o);
    return 0;
  }
  return 1;
}
