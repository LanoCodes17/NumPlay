/* Scenery textures: 16x16 tiles (32x16 at 2 bits), decoded from their LZMA blocks into a cache when first drawn. */
#include "hk.h"
#include "tilecode.h"

#ifndef NSLOTS
#define NSLOTS 400
#endif
#define NHASH 512
#define NO 0xFFFF
#define BLOCK_TILES 16

static uint8_t slot_px[NSLOTS][128];
static uint32_t slot_key[NSLOTS];
static uint16_t slot_next[NSLOTS], slot_frame[NSLOTS];
static uint16_t head[NHASH];
static uint16_t frame = 1, hand;
static uint8_t block_buf[BLOCK_TILES * 128];
#define tcp g_scratch   /* TC_CTX4 * 16 */

/* ---------------------------------------------------------------- the tile decoder (tilecode.h) */
/* one binary step: the range coder's state in locals (rng, code, src), the probability at pr */
#define RC_BIT(pr, bit)                                                                \
  do {                                                                                 \
    uint16_t *pr_ = (pr);                                                              \
    uint32_t pv_ = *pr_, bound_ = (rng >> TC_PROB_BITS) * (pv_ >> 4);                  \
    uint32_t n_ = pv_ & 15, sh_ = n_ + 1 < TC_LIM ? n_ + 1 : TC_LIM, p_ = pv_ >> 4;     \
    if (code < bound_) rng = bound_, bit = 0, p_ += (4096 - p_) >> sh_;                \
    else code -= bound_, rng -= bound_, bit = 1, p_ -= p_ >> sh_;                      \
    while (rng < (1u << 24)) rng <<= 8, code = code << 8 | *src++;                     \
    *pr_ = (uint16_t)(p_ << 4 | (n_ < 15 ? n_ + 1 : 15));                              \
  } while (0)

/* n tiles of a block, packed (4 bits a texel, or 2 in tiles twice as wide) into out, 128 bytes each */
static void decode_block(const uint8_t *src, int n, bool two, uint8_t *out) {
  /* the trained probabilities, already packed (pack.py) */
  const uint8_t *prior = section(SEC_PRIOR);
  if (two) memcpy(tcp, prior + TC_CTX4 * 16 * 2, TC_CTX2 * 4 * 2);
  else memcpy(tcp, prior, TC_CTX4 * 16 * 2);
  uint32_t rng = 0xFFFFFFFFu, code = 0;
  for (int i = 0; i < 5; i++) code = code << 8 | *src++;
  uint8_t img[512];
  for (int t = 0; t < n; t++) {
    if (two) {
      for (int y = 0; y < 16; y++)
        for (int x = 0; x < 32; x++) {
          int l = tc_left(img, x, y, 32), u = tc_up(img, x, y, l, 32), b0, b1;
          uint16_t *p = tcp + (l * 4 + u) * 4;
          RC_BIT(p + 1, b0);
          RC_BIT(p + 2 + b0, b1);
          img[y * 32 + x] = (uint8_t)(b0 << 1 | b1);
        }
      for (int i = 0; i < 128; i++)
        out[t * 128 + i] = (uint8_t)(img[4 * i] | img[4 * i + 1] << 2 | img[4 * i + 2] << 4 | img[4 * i + 3] << 6);
    } else {
      for (int y = 0; y < 16; y++)
        for (int x = 0; x < 16; x++) {
          int l = tc_left(img, x, y, 16), u = tc_up(img, x, y, l, 16), bit, node = 1;
          uint16_t *p = tcp + (l * 16 + u) * 16;
          RC_BIT(p + node, bit);
          node = node * 2 + bit;
          RC_BIT(p + node, bit);
          node = node * 2 + bit;
          RC_BIT(p + node, bit);
          node = node * 2 + bit;
          RC_BIT(p + node, bit);
          node = node * 2 + bit;
          img[y * 16 + x] = (uint8_t)(node - 16);
        }
      for (int i = 0; i < 128; i++) out[t * 128 + i] = (uint8_t)(img[2 * i] | img[2 * i + 1] << 4);
    }
  }
}
static bool inited;
uint32_t g_tex_decodes, g_tex_misses, g_tex_calls;
bool g_tex_overload;

static const uint8_t *sec_tex, *sec_tmap, *sec_bidx, *sec_blk;
const TexRec *tex_rec(uint16_t t) {
  if (!sec_tex) sec_tex = section(SEC_TEX) + 4;
  return (const TexRec *)(sec_tex + sizeof(TexRec) * t);
}

static const uint8_t *row_base(const TexRec *r, int ty) { return sec_tmap + r->map_off + ty * (2 + (r->tw + 3) / 4); }
const uint8_t *tex_row_codes(uint16_t t, int ty) {
  if (!sec_tmap) sec_tmap = section(SEC_TMAP);
  return row_base(tex_rec(t), ty) + 2;
}

static int code_at(const uint8_t *codes, int tx) { return codes[tx >> 2] >> ((tx & 3) * 2) & 3; }

/* how many of a code byte's 4 tiles are kept */
static uint8_t kept4[256];
static void init_kept4(void) {
  for (int i = 0; i < 256; i++) kept4[i] = (uint8_t)(((i & 3) != 0) + ((i >> 2 & 3) != 0) + ((i >> 4 & 3) != 0) + ((i >> 6 & 3) != 0));
}

/* the tile's number among the texture's kept tiles */
static int tile_rank(const TexRec *r, int tx, int ty) {
  const uint8_t *b = row_base(r, ty), *c = b + 2;
  int n = rd16(b), i = 0;
  for (; i + 4 <= tx; i += 4) n += kept4[c[i >> 2]];
  for (; i < tx; i++) n += code_at(c, i) != 0;
  return n;
}
static int tex_tiles(const TexRec *r) {
  const uint8_t *b = row_base(r, r->th - 1), *c = b + 2;
  int n = rd16(b);
  for (int i = 0; i < r->tw; i++) n += code_at(c, i) != 0;
  return n;
}

static void init(void) {
  init_kept4();
  sec_tex = section(SEC_TEX) + 4, sec_tmap = section(SEC_TMAP), sec_bidx = section(SEC_BIDX), sec_blk = section(SEC_BLK);
  for (int i = 0; i < NHASH; i++) head[i] = NO;
  for (int i = 0; i < NSLOTS; i++) slot_key[i] = 0xFFFFFFFF, slot_next[i] = NO, slot_frame[i] = 0;
  inited = true;
}

static unsigned hash(uint32_t k) { return (k * 2654435761u) >> 23 & (NHASH - 1); }

static int find(uint32_t key) {
  for (int s = head[hash(key)]; s != NO; s = slot_next[s])
    if (slot_key[s] == key) return s;
  return -1;
}

static void unlink_slot(int s) {
  if (slot_key[s] == 0xFFFFFFFF) return;
  uint16_t *p = &head[hash(slot_key[s])];
  while (*p != NO && *p != s) p = &slot_next[*p];
  if (*p == s) *p = slot_next[s];
  slot_key[s] = 0xFFFFFFFF;
}

/* a slot to reuse: one unused for a while, else one not drawn this frame, else any */
static int victim(void) {
  int last = -1;
  for (int n = 0; n < NSLOTS; n++) {
    int s = hand;
    hand = (uint16_t)((hand + 1) % NSLOTS);
    uint16_t age = (uint16_t)(frame - slot_frame[s]);
    if (age > 1) return s;
    if (age == 1 && last < 0) last = s;
  }
  if (last >= 0) return last;
  g_tex_overload = true;
  int s = hand;   /* the view needs more tiles than the cache holds */
  hand = (uint16_t)((hand + 1) % NSLOTS);
  return s;
}

static int insert(uint32_t key, const uint8_t *px, int bytes) {
  int s = victim();
  unlink_slot(s);
  slot_key[s] = key;
  unsigned h = hash(key);
  slot_next[s] = head[h];
  head[h] = (uint16_t)s;
  memcpy(slot_px[s], px, (size_t)bytes);
  return s;
}

/* a slot not drawn this frame or the last one, for the rest of a decoded block (-1: none, keep what is used) */
static int stale_slot(void) {
  for (int n = 0; n < 64; n++) {
    int s = hand;
    hand = (uint16_t)((hand + 1) % NSLOTS);
    if ((uint16_t)(frame - slot_frame[s]) > 1) return s;
  }
  return -1;
}

const uint8_t *tex_slot_px(int s) { return slot_px[s]; }


const uint8_t *tex_tile(uint16_t t, int tx, int ty) {
  int s = tex_slot(t, tx, ty);
  return s < 0 ? NULL : slot_px[s];
}

int tex_slot(uint16_t t, int tx, int ty) {
  if (!inited) init();
  g_tex_calls++;
  const TexRec *r = tex_rec(t);
  if ((unsigned)tx >= r->tw || (unsigned)ty >= r->th) return -1;
  if (!code_at(row_base(r, ty) + 2, tx)) return -1;
  int rank = tile_rank(r, tx, ty);
  uint32_t key = (uint32_t)t << 16 | (uint32_t)rank;
  int s = find(key);
  if (s >= 0) {
    slot_frame[s] = frame;
    return s;
  }
  g_tex_misses++;
  /* decode the block */
  int bytes = 128;
  int blk = rank / BLOCK_TILES, first = blk * BLOCK_TILES;
  int ntiles = tex_tiles(r) - first;
  if (ntiles > BLOCK_TILES) ntiles = BLOCK_TILES;
  const uint8_t *bidx = sec_bidx + 4 * (r->blk_first + blk);
  decode_block(sec_blk + rd32(bidx), ntiles, r->fmt == FMT_ALPHA2, block_buf);
  g_tex_decodes++;
  int want = insert((uint32_t)t << 16 | (uint32_t)rank, block_buf + (rank - first) * bytes, bytes);
  slot_frame[want] = frame;
  /* the block's other tiles, where they push out nothing in use */
  for (int i = 0; i < ntiles; i++) {
    uint32_t k = (uint32_t)t << 16 | (uint32_t)(first + i);
    if (first + i == rank || find(k) >= 0) continue;
    int ns = stale_slot();
    if (ns < 0) break;
    unlink_slot(ns);
    slot_key[ns] = k;
    unsigned hh = hash(k);
    slot_next[ns] = head[hh];
    head[hh] = (uint16_t)ns;
    memcpy(slot_px[ns], block_buf + i * bytes, (size_t)bytes);
    slot_frame[ns] = (uint16_t)(frame - 2);   /* not drawn yet: the first to go */
  }
  return want;
}

uint32_t g_tex_used;   /* tiles drawn last frame */
void tex_frame(void) {
  if (!inited) init();
  g_tex_overload = false;
  g_tex_used = 0;
  for (int i = 0; i < NSLOTS; i++) g_tex_used += slot_frame[i] == frame;
  frame++;
  if (!frame) frame = 1;
}

#ifdef HOST
#include <stdio.h>
void tex_dump_used(const char *path) {
  FILE *f = fopen(path, "w");
  for (int i = 0; i < NSLOTS; i++)
    if (slot_frame[i] == frame && slot_key[i] != 0xFFFFFFFF) fprintf(f, "%u\n", slot_key[i] >> 16);
  fclose(f);
}
#endif

