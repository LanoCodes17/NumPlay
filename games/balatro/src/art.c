/* Sprite decoding (see tools/rc.py) and the cache of decoded sprites.
 *
 * Decoded sprites are palette indices, one byte per pixel, kept in an arena.
 * Before each frame is drawn, every sprite it uses is requested once; when
 * the arena is full it is emptied and the frame's sprites are decoded again,
 * so pointers stay valid while the frame is drawn. */
#include "art.h"
#include <string.h>

#define PROB_BITS 11
#define TOP (1u << 24)
enum { C_T = 0, C_L = 16, C_U = 32, C_R = 36, C_LIT = 40, NPROBS = C_LIT + 256 };

typedef struct {
  const uint8_t *p;
  uint32_t range, code;
} rc_t;

static inline void rc_norm(rc_t *r) {
  while (r->range < TOP) {
    r->range <<= 8;
    r->code = (r->code << 8) | *r->p++;
  }
}

static inline int rc_bit(rc_t *r, uint16_t *pr) {
  uint32_t p = *pr, bound = (r->range >> PROB_BITS) * p;
  int b;
  if (r->code < bound) {
    r->range = bound;
    *pr = (uint16_t)(p + (((1u << PROB_BITS) - p) >> 4));
    b = 0;
  } else {
    r->code -= bound;
    r->range -= bound;
    *pr = (uint16_t)(p - (p >> 4));
    b = 1;
  }
  rc_norm(r);
  return b;
}

static uint32_t rc_direct(rc_t *r, int n) {
  uint32_t v = 0;
  while (n--) {
    r->range >>= 1;
    uint32_t b = r->code >= r->range;
    if (b) r->code -= r->range;
    v = v << 1 | b;
    rc_norm(r);
  }
  return v;
}

static void decode(int id, uint8_t *out, const uint8_t *tmpl) {
  const art_sprite_t *s = &art_sprites[id];
  int w = s->w, h = s->h;
  if (w != ART_CW) tmpl = 0;
  static uint16_t probs[NPROBS];
  for (int i = 0; i < NPROBS; i++) probs[i] = 1 << (PROB_BITS - 1);
  rc_t r = {art_blob + s->off, 0xFFFFFFFFu, 0};
  for (int i = 0; i < 4; i++) r.code = (r.code << 8) | *r.p++;
  int n = (int)rc_direct(&r, 8) + 1;
  uint8_t pal[256];
  for (int i = 0; i < n; i++) pal[i] = (uint8_t)rc_direct(&r, 8);
  int lb = 0;
  while ((1 << lb) < n) lb++;
  for (int y = 0; y < h; y++) {
    uint8_t *row = out + y * w, *up = row - w;
    const uint8_t *trow = tmpl ? tmpl + y * w : 0;
    for (int x = 0; x < w; x++) {
      int L = x > 0 ? row[x - 1] : -1;
      int U = y > 0 ? up[x] : -1;
      int UL = (x > 0 && y > 0) ? up[x - 1] : -1;
      int UR = (y > 0 && x < w - 1) ? up[x + 1] : -1;
      int seen[4], ns = 0, v = -1;
      if (trow) {
        int t = trow[x];
        int lt = x > 0 ? trow[x - 1] == L : 1;
        int ut = y > 0 ? (trow - w)[x] == U : 1;
        if (rc_bit(&r, &probs[C_T + (lt | ut << 1 | (t == L) << 2 | (t == U) << 3)])) v = t;
        seen[ns++] = t;
      }
#define SEEN(c) (ns > 0 && (seen[0] == (c) || (ns > 1 && (seen[1] == (c) || (ns > 2 && seen[2] == (c))))))
      if (v < 0 && L >= 0 && !SEEN(L)) {
        if (rc_bit(&r, &probs[C_L + ((U == L) | (UL == L) << 1 | (UR == U) << 2 | (UL == U) << 3)])) v = L;
        seen[ns++] = L;
      }
      if (v < 0 && U >= 0 && !SEEN(U)) {
        if (rc_bit(&r, &probs[C_U + ((UL == U) | (UR == U) << 1)])) v = U;
        seen[ns++] = U;
      }
      if (v < 0 && UR >= 0 && !SEEN(UR)) {
        if (rc_bit(&r, &probs[C_R])) v = UR;
        seen[ns++] = UR;
      }
#undef SEEN
      if (v < 0) {
        int node = 1;
        for (int k = 0; k < lb; k++) node = node * 2 + rc_bit(&r, &probs[C_LIT + node]);
        v = pal[node - (1 << lb)];
      }
      row[x] = (uint8_t)v;
    }
  }
}

/* ------------------------------------------------------------------ cache */
static uint8_t arena[ART_ARENA];
static uint32_t used;
static uint16_t where[ART_NSPRITES]; /* offset/4 + 1 into the arena, 0 = not decoded */
static uint8_t lasty[ART_NSPRITES];  /* this frame: bottom row of the sprite's last use */
static uint8_t firsty[ART_NSPRITES]; /* this frame: top row of its first use */
static uint8_t tmpl_buf[3][ART_CW * ART_CH];
static int8_t tmpl_slot[ART_NGROUPS];
static uint8_t tmpl_ready;

static void templates(void) {
  if (tmpl_ready) return;
  int k = 0;
  for (int g = 0; g < ART_NGROUPS; g++) {
    tmpl_slot[g] = -1;
    if (art_tmpl_of_group[g] >= 0 && k < 3) {
      decode(art_tmpl_of_group[g], tmpl_buf[k], 0);
      tmpl_slot[g] = (int8_t)k++;
    }
  }
  tmpl_ready = 1;
}

static uint32_t size_of(int id) { return ((uint32_t)art_sprites[id].w * art_sprites[id].h + 3) & ~3u; }

void art_flush(void) {
  used = 0;
  memset(where, 0, sizeof where);
}

void art_begin_frame(void) {
  memset(lasty, 0, sizeof lasty);
  memset(firsty, 255, sizeof firsty);
}

void art_use(int id, int top, int bottom) {
  if (id < 0 || id >= ART_NSPRITES) return;
  if (bottom > 255) bottom = 255;
  if (top < 0) top = 0;
  if (bottom > lasty[id]) lasty[id] = (uint8_t)bottom;
  if (top < firsty[id]) firsty[id] = (uint8_t)top;
}

/* Frees the sprites not needed from row y down, keeping the others packed
 * at the start of the arena (in order, so nothing overlaps while moving). */
static void evict(int y, int later) {
  uint32_t w = 0;
  for (;;) {
    /* the decoded sprite with the lowest offset at or after w */
    int best = -1;
    uint32_t bo = 0xFFFFFFFF;
    for (int i = 0; i < ART_NSPRITES; i++)
      if (where[i]) {
        uint32_t o = (where[i] - 1) * 4u;
        if (o >= w && o < bo) bo = o, best = i;
      }
    if (best < 0) break;
    if (lasty[best] <= y || (later && firsty[best] >= later)) {
      where[best] = 0; /* not needed any more this frame, or not before row `later` */
      continue;
    }
    uint32_t n = size_of(best);
    if (bo != w) memmove(arena + w, arena + bo, n);
    where[best] = (uint16_t)(w / 4 + 1);
    w += n;
  }
  used = w;
}

const uint8_t *art_peek(int id) {
  if (id < 0 || id >= ART_NSPRITES || !where[id]) return 0;
  return arena + (where[id] - 1) * 4u;
}

/* decodes the sprite if needed; y: the row being drawn (for eviction) */
const uint8_t *art_get_at(int id, int y) {
  if (id < 0 || id >= ART_NSPRITES) return 0;
  templates();
  if (where[id]) return arena + (where[id] - 1) * 4u;
  uint32_t n = size_of(id);
  if (n > sizeof arena) return 0;
  if (used + n > sizeof arena) evict(y, 0);
  if (used + n > sizeof arena && y >= 0) evict(y, y + 16);
  if (used + n > sizeof arena) return 0;
  const art_sprite_t *s = &art_sprites[id];
  int g = s->group;
  const uint8_t *tm = (tmpl_slot[g] >= 0 && art_tmpl_of_group[g] != id) ? tmpl_buf[tmpl_slot[g]] : 0;
  uint8_t *out = arena + used;
  decode(id, out, tm);
  where[id] = (uint16_t)(used / 4 + 1);
  used += n;
  return out;
}

const uint8_t *art_get(int id) { return art_get_at(id, -1); }
