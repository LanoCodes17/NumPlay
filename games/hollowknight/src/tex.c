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
#define tcp g_scratch   /* TC_N4 */
_Static_assert(TC_N4 <= sizeof g_scratch / 2, "the tile decoder's probabilities: in g_scratch");
#define NONE_T 255

/* ---------------------------------------------------------------- the tile decoder (tilecode.h) */
/* one binary step: the range coder's state in locals (rng, code, src), the probability at pr */
#define RC_BIT(pr, bit)                                                                \
  do {                                                                                 \
    uint16_t *pr_ = (pr);                                                              \
    uint32_t pv_ = *pr_, p_ = pv_ >> 4, bound_ = (rng >> TC_PROB_BITS) * p_;           \
    uint32_t st_ = tc_state[pv_ & 7], sh_ = st_ >> 4;                                  \
    if (code < bound_) rng = bound_, bit = 0, p_ += (4096 - p_) >> sh_;                \
    else code -= bound_, rng -= bound_, bit = 1, p_ -= p_ >> sh_;                      \
    while (rng < (1u << 24)) rng <<= 8, code = code << 8 | *src++;                     \
    *pr_ = (uint16_t)(p_ << 4 | (st_ & 15));                                           \
  } while (0)

/* a 4-bit value in 4 binary steps, nodes 1..15 at p + node */
#define RC_TREE4(p, v)                                                                 \
  do {                                                                                 \
    uint16_t *pt_ = (p);                                                               \
    int b_, n_ = 1;                                                                    \
    RC_BIT(pt_ + n_, b_);                                                              \
    n_ = n_ * 2 + b_;                                                                  \
    RC_BIT(pt_ + n_, b_);                                                              \
    n_ = n_ * 2 + b_;                                                                  \
    RC_BIT(pt_ + n_, b_);                                                              \
    n_ = n_ * 2 + b_;                                                                  \
    RC_BIT(pt_ + n_, b_);                                                              \
    n_ = n_ * 2 + b_;                                                                  \
    v = n_ - 16;                                                                       \
  } while (0)

/* a texel of a packed tile */
static int texel_at(const uint8_t *t, int x, int y, bool two) {
  return two ? t[y * 8 + (x >> 2)] >> (x & 3) * 2 & 3 : t[y * 8 + (x >> 1)] >> (x & 1) * 4 & 15;
}

/* n tiles of a block, packed (4 bits a texel, or 2 in tiles twice as wide) into out, 128 bytes each; left, up: each
 * tile's neighbors in the block (NONE_T: not there) */
static void decode_block(const uint8_t *src, int n, bool two, const uint8_t *left, const uint8_t *up, uint8_t *out) {
  /* the trained probabilities, ready to use (pack.py) */
  const uint8_t *prior = section(SEC_PRIOR);
  if (two) memcpy(tcp, prior + TC_N4 * 2, TC_N2 * 2);
  else memcpy(tcp, prior, TC_N4 * 2);
  uint32_t rng = 0xFFFFFFFFu, code = 0;
  for (int i = 0; i < 4; i++) code = code << 8 | *src++;
  int w = two ? 32 : 16, s = TC_STRIDE(w);
  uint8_t img[17 * TC_STRIDE(32)];   /* the row above, then the tile's: 2 texels to the left, 1 to the right */
  for (int t = 0; t < n; t++) {
    /* the borders: the last 2 columns of the tile to the left, the last row of the one above */
    const uint8_t *lt = left[t] == NONE_T ? NULL : out + left[t] * 128;
    const uint8_t *ut = up[t] == NONE_T ? NULL : out + up[t] * 128;
    for (int y = 0; y < 16; y++) {
      uint8_t *r = img + (y + 1) * s;
      r[0] = lt ? (uint8_t)texel_at(lt, w - 2, y, two) : 0;
      r[1] = lt ? (uint8_t)texel_at(lt, w - 1, y, two) : 0;
    }
    int y = 0;
    if (ut) {
      for (int x = 0; x < w; x++) img[2 + x] = (uint8_t)texel_at(ut, x, 15, two);
      img[1] = img[s + 1], img[w + 2] = img[w + 1];
    } else {
      /* the top row on its own */
      uint8_t *r = img + s + 2;
      for (int x = 0; x < w; x++) {
        int l = r[x - 1], v, bit;
        if (two) {
          uint16_t *p = tcp + tc_c20(l, r[x - 2]);
          RC_BIT(p, bit);
          RC_BIT(p + 1 + bit, v);
          v |= bit << 1;
        } else {
          RC_BIT(tcp + tc_f0(l, r[x - 2]), bit);
          if (!bit) v = l;
          else RC_TREE4(tcp + tc_tree(l, l), v);
        }
        r[x] = (uint8_t)v;
      }
      r[w] = r[w - 1];
      y = 1;
    }
    for (; y < 16; y++) {
      uint8_t *r = img + (y + 1) * s + 2;
      const uint8_t *a = r - s;
      if (two)
        for (int x = 0; x < 32; x++) {
          uint16_t *p = tcp + tc_c2(r[x - 1], a[x], a[x - 1], a[x + 1]);
          int b0, b1;
          RC_BIT(p, b0);
          RC_BIT(p + 1 + b0, b1);
          r[x] = (uint8_t)(b0 << 1 | b1);
        }
      else
        for (int x = 0; x < 16; x++) {
          int l = r[x - 1], u = a[x], v, bit;
          RC_BIT(tcp + tc_fl(l, u, a[x - 1], a[x + 1], r[x - 2]), bit);
          if (!bit) v = l;
          else {
            if (u != l) RC_BIT(tcp + tc_fu(l, u, a[x - 1]), bit);
            if (!bit) v = u;
            else RC_TREE4(tcp + tc_tree(l, u), v);
          }
          r[x] = (uint8_t)v;
        }
      r[w] = r[w - 1];
    }
    for (y = 0; y < 16; y++) {
      const uint8_t *r = img + (y + 1) * s + 2;
      uint8_t *o = out + t * 128 + y * 8;
      if (two)
        for (int i = 0; i < 8; i++) o[i] = (uint8_t)(r[4 * i] | r[4 * i + 1] << 2 | r[4 * i + 2] << 4 | r[4 * i + 3] << 6);
      else
        for (int i = 0; i < 8; i++) o[i] = (uint8_t)(r[2 * i] | r[2 * i + 1] << 4);
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


/* where a block's tiles are (by rank: row by row), and so which of them is each one's left and upper neighbor */
static void block_geometry(const TexRec *r, int first, int n, uint8_t *left, uint8_t *up) {
  uint8_t tx[BLOCK_TILES], ty[BLOCK_TILES];
  int y = 0, i = 0;
  while (y + 1 < r->th && rd16(row_base(r, y + 1)) <= first) y++;
  for (int k = rd16(row_base(r, y)); i < n; y++)
    for (int x = 0; x < r->tw && i < n; x++)
      if (code_at(row_base(r, y) + 2, x) && k++ >= first) tx[i] = (uint8_t)x, ty[i] = (uint8_t)y, i++;
  for (i = 0; i < n; i++) {
    left[i] = i && ty[i - 1] == ty[i] && tx[i - 1] + 1 == tx[i] ? (uint8_t)(i - 1) : NONE_T;
    up[i] = NONE_T;
    for (int j = 0; j < i; j++)
      if (tx[j] == tx[i] && ty[j] + 1 == ty[i]) up[i] = (uint8_t)j;
  }
}

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
  uint8_t left[BLOCK_TILES], up[BLOCK_TILES];
  block_geometry(r, first, ntiles, left, up);
  decode_block(sec_blk + rd32(bidx), ntiles, r->fmt == FMT_ALPHA2, left, up, block_buf);
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

