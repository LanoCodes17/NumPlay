/* Scenery tile coding for pack.py: trains the starting probabilities and encodes blocks of tiles.
 *   tilecode train IN PRIOR      (PRIOR: 256x16 then 16x4 uint16 probabilities)
 *   tilecode encode IN PRIOR OUT
 * IN: u32 blocks, then per block: u8 bits (4 or 2), u8 0, u16 tiles, tiles x 256 (or 512, 2 bits) texels (one a byte).
 * OUT: per block, u32 size and its bytes. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/tilecode.h"

typedef struct {
  int bits, n;
  uint8_t *tex;
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
    blocks[i].tex = malloc((size_t)blocks[i].n * sz);
    if (fread(blocks[i].tex, sz, (size_t)blocks[i].n, f) != (size_t)blocks[i].n) exit(1);
  }
  fclose(f);
}

static double cnt4[TC_CTX4][16][2], cnt2[TC_CTX2][4][2];
static uint16_t prior4[TC_CTX4][16], prior2[TC_CTX2][4];

static void train(void) {
  for (int b = 0; b < nblocks; b++) {
    Block *B = &blocks[b];
    for (int t = 0; t < B->n; t++) {
      int W = B->bits == 2 ? 32 : 16;
      const uint8_t *img = B->tex + (size_t)(W * 16) * t;
      for (int y = 0; y < 16; y++)
        for (int x = 0; x < W; x++) {
          int l = tc_left(img, x, y, W), u = tc_up(img, x, y, l, W), s = img[y * W + x], node = 1;
          if (B->bits == 4) {
            int c = l * 16 + u;
            for (int k = 3; k >= 0; k--) {
              int bit = s >> k & 1;
              cnt4[c][node][bit] += 1;
              node = node * 2 + bit;
            }
          } else {
            int c = l * 4 + u;
            for (int k = 1; k >= 0; k--) {
              int bit = s >> k & 1;
              cnt2[c][node][bit] += 1;
              node = node * 2 + bit;
            }
          }
        }
    }
  }
}

static int clip_p(double c0, double c1) {
  int p = (int)(4096.0 * (c0 + 0.5) / (c0 + c1 + 1.0) + 0.5);
  return p < 64 ? 64 : p > 4032 ? 4032 : p;
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
static void enc_bit(uint16_t *pr, int bit) {
  uint32_t bound = (range >> TC_PROB_BITS) * (uint32_t)(*pr >> 4);
  if (!bit) range = bound;
  else low += bound, range -= bound;
  while (range < (1u << 24)) range <<= 8, shift_low();
  tc_update(pr, bit);
}

static uint16_t p4[TC_CTX4][16], p2[TC_CTX2][4];

static void encode_block(const Block *B) {
  low = 0, range = 0xFFFFFFFFu, cache = 0, cache_size = 1, outn = 0;
  for (int c = 0; c < TC_CTX4; c++)
    for (int k = 0; k < 16; k++) p4[c][k] = tc_pack(prior4[c][k], TC_N0);
  for (int c = 0; c < TC_CTX2; c++)
    for (int k = 0; k < 4; k++) p2[c][k] = tc_pack(prior2[c][k], TC_N0);
  for (int t = 0; t < B->n; t++) {
    int W = B->bits == 2 ? 32 : 16;
    const uint8_t *img = B->tex + (size_t)(W * 16) * t;
    for (int y = 0; y < 16; y++)
      for (int x = 0; x < W; x++) {
        int l = tc_left(img, x, y, W), u = tc_up(img, x, y, l, W), s = img[y * W + x], node = 1;
        if (B->bits == 4)
          for (int k = 3; k >= 0; k--) {
            int bit = s >> k & 1;
            enc_bit(&p4[l * 16 + u][node], bit);
            node = node * 2 + bit;
          }
        else
          for (int k = 1; k >= 0; k--) {
            int bit = s >> k & 1;
            enc_bit(&p2[l * 4 + u][node], bit);
            node = node * 2 + bit;
          }
      }
  }
  for (int i = 0; i < 5; i++) shift_low();
}

int main(int argc, char **argv) {
  if (argc < 4) return 1;
  load(argv[2]);
  if (!strcmp(argv[1], "train")) {
    train();
    for (int c = 0; c < TC_CTX4; c++)
      for (int k = 0; k < 16; k++) prior4[c][k] = (uint16_t)clip_p(cnt4[c][k][0], cnt4[c][k][1]);
    for (int c = 0; c < TC_CTX2; c++)
      for (int k = 0; k < 4; k++) prior2[c][k] = (uint16_t)clip_p(cnt2[c][k][0], cnt2[c][k][1]);
    FILE *f = fopen(argv[3], "wb");
    fwrite(prior4, sizeof prior4, 1, f);
    fwrite(prior2, sizeof prior2, 1, f);
    fclose(f);
    return 0;
  }
  if (!strcmp(argv[1], "encode") && argc >= 5) {
    FILE *f = fopen(argv[3], "rb");
    if (!f || fread(prior4, sizeof prior4, 1, f) != 1 || fread(prior2, sizeof prior2, 1, f) != 1) return 1;
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
