/* Sprites: LZMA decoding into a cache of run-length rows.
 *
 * data.bin keeps the doodle's images as 8-bit palette indices, compressed
 * with LZMA: small ones in banks of up to 16 KB, large ones (backgrounds) as
 * streams of their own. One decoder with an 8 KB ring dictionary serves both.
 * A sprite about to be drawn is decoded into the cache as rows of runs of
 * visible pixels (transparent ones cost nothing), and stays there until the
 * least recently used sprites have to make room. */
#include "spr.h"
#include "lzma/LzmaDec.h"

/* ---------------------------------------------------------------- LZMA */
static CLzmaProb probs[1984 + 0x300];
static uint8_t ring[LZMA_DICT];
static CLzmaDec dec;
static const uint8_t *z_src;
static uint32_t z_in, z_len, z_out, z_total;

void z_open(uint32_t off, uint32_t clen, uint32_t rawlen) {
  memset(&dec, 0, sizeof dec);
  dec.prop.lc = 0;
  dec.prop.lp = 0;
  dec.prop.pb = 0;
  dec.prop.dicSize = LZMA_DICT;
  dec.probs = probs;
  dec.probs_1664 = probs + 1664;
  dec.numProbs = sizeof probs / sizeof probs[0];
  dec.dic = ring;
  dec.dicBufSize = LZMA_DICT;
  LzmaDec_Init(&dec);
  z_src = ci_data + off;
  z_in = 0;
  z_len = clen;
  z_out = 0;
  z_total = rawlen;
}

/* Decodes up to n more bytes; they sit in ring[] from *at (never wrapping:
 * a read ends at the end of the ring). Returns how many (0 at the end). */
uint32_t z_read(uint32_t n, const uint8_t **at) {
  if (z_out >= z_total) return 0;
  if (dec.dicPos == LZMA_DICT) dec.dicPos = 0;
  uint32_t start = (uint32_t)dec.dicPos, room = LZMA_DICT - start;
  if (n > room) n = room;
  if (n > z_total - z_out) n = z_total - z_out;
  SizeT inlen = z_len - z_in;
  ELzmaStatus st;
  if (LzmaDec_DecodeToDic(&dec, start + n, z_src + z_in, &inlen, LZMA_FINISH_ANY, &st) != SZ_OK) {
    z_out = z_total;
    return 0;
  }
  z_in += (uint32_t)inlen;
  uint32_t got = (uint32_t)dec.dicPos - start;
  z_out += got;
  *at = ring + start;
  return got;
}

/* Copies the next n decoded bytes into dst (NULL: skips them). */
bool z_get(uint8_t *dst, uint32_t n) {
  while (n) {
    const uint8_t *p;
    uint32_t got = z_read(n, &p);
    if (!got) return false;
    if (dst) {
      memcpy(dst, p, got);
      dst += got;
    }
    n -= got;
  }
  return true;
}

/* the decoder's ring when no stream is being read: scratch memory for others */
uint8_t *z_scratch(uint32_t *size) { *size = sizeof ring; return ring; }

/* ---------------------------------------------------------------- the cache */
#define ENTRIES 160
typedef struct { uint16_t spr; uint16_t pad; uint32_t off, size, used; } Entry;
static uint8_t *mem;
static uint32_t cache_bytes;
static Entry ent[ENTRIES];
static uint16_t nent;
static uint8_t hint[64];            /* sprite & 63 -> an entry to try first */
static uint32_t top, tick;

void spr_setup(uint8_t *m, uint32_t size) {
  mem = m;
  cache_bytes = size;
  nent = 0;
  top = 0;
}

void spr_reset(void) {
  nent = 0;
  top = 0;
}

/* entry + 1 of a cached sprite, 0 if not cached */
static unsigned find(uint16_t sp) {
  unsigned h = hint[sp & 63];
  if (h < nent && ent[h].spr == sp) return h + 1;
  for (unsigned i = 0; i < nent; i++)
    if (ent[i].spr == sp) { hint[sp & 63] = (uint8_t)i; return i + 1; }
  return 0;
}

void spr_tick(void) { tick++; }

static void compact(void) {
  /* entries sorted by offset: slide each down */
  for (int i = 1; i < nent; i++) {
    Entry e = ent[i];
    int j = i - 1;
    while (j >= 0 && ent[j].off > e.off) { ent[j + 1] = ent[j]; j--; }
    ent[j + 1] = e;
  }
  uint32_t at = 0;
  for (int i = 0; i < nent; i++) {
    if (ent[i].off != at) memmove(mem + at, mem + ent[i].off, ent[i].size);
    ent[i].off = at;
    at += (ent[i].size + 3) & ~3u;
  }
  top = at;
}

static void evict_one(void) {
  int old = 0;
  for (int i = 1; i < nent; i++)
    if (ent[i].used < ent[old].used) old = i;
  ent[old] = ent[--nent];
}

/* Room for n bytes; evicts only entries not used this tick unless forced. */
static uint8_t *alloc(uint32_t n, bool force) {
  n = (n + 3) & ~3u;
  if (n > cache_bytes) return NULL;
  if (top + n > cache_bytes || nent >= ENTRIES) {
    uint32_t live = 0;
    for (int i = 0; i < nent; i++) live += (ent[i].size + 3) & ~3u;
    while (nent && (live + n > cache_bytes || nent >= ENTRIES)) {
      int old = 0;
      for (int i = 1; i < nent; i++)
        if (ent[i].used < ent[old].used) old = i;
      if (!force && ent[old].used + 1 >= tick) return NULL;
      live -= (ent[old].size + 3) & ~3u;
      evict_one();
    }
    compact();
  }
  if (top + n > cache_bytes) return NULL;
  uint8_t *p = mem + top;
  top += n;
  return p;
}

/* Encodes rows of palette indices as runs of visible pixels:
 * u16 w, u16 h, u16 row offsets[h] (from the start), then per row: u16 nruns,
 * and per run: skip (transparent pixels before it), then either a fill
 * (0x80 | (length - 1), one colour: 1 to 128 pixels of one colour) or
 * literal pixels (length 0..127, then the colours). pack.py's rle_size()
 * computes the same sizes. */
typedef struct { uint8_t *base, *p, *end; uint16_t row; bool ok; } Rle;
static void rle_row(Rle *r, const uint8_t *px, int w, const uint8_t *alpha) {
  if (!r->ok) return;
  uint32_t off = (uint32_t)(r->p - r->base);
  r->base[4 + 2 * r->row] = (uint8_t)off;
  r->base[5 + 2 * r->row] = (uint8_t)(off >> 8);
  r->row++;
  if (r->p + 2 > r->end) { r->ok = false; return; }
  uint8_t *count = r->p;
  r->p += 2;
  int n = 0, x = 0;
  while (x < w) {
    int skip = 0;
    while (x < w && !alpha[px[x]]) { x++; skip++; }
    if (x >= w) break;
    while (skip > 255) {         /* a long gap: runs of nothing */
      if (r->p + 2 > r->end) { r->ok = false; return; }
      *r->p++ = 255; *r->p++ = 0; skip -= 255; n++;
    }
    int run = 1;
    while (x + run < w && run < 128 && px[x + run] == px[x]) run++;
    if (run >= 3) {
      if (r->p + 3 > r->end) { r->ok = false; return; }
      *r->p++ = (uint8_t)skip;
      *r->p++ = (uint8_t)(0x80 | (run - 1));
      *r->p++ = px[x];
      x += run;
    } else {
      int len = 0;
      while (x + len < w && len < 127 && alpha[px[x + len]] &&
             !(x + len + 2 < w && px[x + len] == px[x + len + 1] && px[x + len] == px[x + len + 2]))
        len++;
      if (!len) len = 1;
      if (r->p + 2 + len > r->end) { r->ok = false; return; }
      *r->p++ = (uint8_t)skip;
      *r->p++ = (uint8_t)len;
      memcpy(r->p, px + x, (size_t)len);
      r->p += len;
      x += len;
    }
    n++;
  }
  count[0] = (uint8_t)n;
  count[1] = (uint8_t)(n >> 8);
}
/* Worst-case size of a sprite's runs. */
static uint32_t rle_bound(int w, int h) { return 4 + 2u * h + (uint32_t)h * (2 + 3 * ((uint32_t)w / 2 + 2) + (uint32_t)w); }
static uint8_t rowbuf[SPRITE_W_MAX];

/* Adds sprite sp from the decoder, whose next bytes are its pixels. */
static bool add_from_decoder(uint16_t sp, const Sprite *s, bool force) {
  const uint8_t *alpha = palalpha(s->sheet);
  uint32_t bound = s->rle ? s->rle : rle_bound(s->w, s->h);   /* the packer measured it */
  if (bound > cache_bytes) { z_get(NULL, (uint32_t)s->w * s->h); return false; }
  uint8_t *p = alloc(bound, force);
  if (!p) { z_get(NULL, (uint32_t)s->w * s->h); return false; }
  Rle r = {p, p + 4 + 2 * s->h, p + bound, 0, true};
  p[0] = (uint8_t)s->w; p[1] = (uint8_t)(s->w >> 8); p[2] = (uint8_t)s->h; p[3] = (uint8_t)(s->h >> 8);
  for (int y = 0; y < s->h; y++) {
    if (!z_get(rowbuf, s->w)) r.ok = false;
    rle_row(&r, rowbuf, s->w, alpha);
  }
  uint32_t used = (uint32_t)(r.p - p);
  top -= (bound + 3) & ~3u;          /* give back what the runs did not need */
  if (!r.ok) return false;
  top += (used + 3) & ~3u;
  /* sprites decoded on the way (bank neighbours) count as older than any in use */
  ent[nent++] = (Entry){sp, 0, (uint32_t)(p - mem), used, force || !tick ? tick : tick - 1};
  return true;
}

const uint8_t *spr_get(uint16_t sp) {
  if (sp >= SPRITE_COUNT || !mem) return NULL;
  unsigned f = find(sp);
  if (f) {
    Entry *e = &ent[f - 1];
    e->used = tick;
    return mem + e->off;
  }
  Sprite s;
  sprite_info(sp, &s);
  if (s.kind == 1) {
    const uint8_t *st = spr_stream(sp);
    if (!st) return NULL;
    z_open(rd32(st + 2), rd32(st + 6), rd32(st + 10));
    add_from_decoder(sp, &s, true);
  } else {
    const uint8_t *b = ci_data + HDR(H_BANKS) + 12u * s.bank;
    z_open(rd32(b), rd32(b + 4), rd32(b + 8));
    const uint8_t *m = ci_data + rd32(ci_data + HDR(H_BANKSPR) + 4u * s.bank);
    unsigned n = rd16(m), pos = 0;
    for (unsigned i = 0; i < n; i++) {
      uint16_t o = rd16(m + 2 + 2 * i);
      Sprite t;
      sprite_info(o, &t);
      if (t.off > pos) z_get(NULL, t.off - pos);
      pos = t.off + (uint32_t)t.w * t.h;
      if (find(o) || (o != sp && top + (t.rle ? t.rle : rle_bound(t.w, t.h)) > cache_bytes)) {
        z_get(NULL, (uint32_t)t.w * t.h);
        continue;
      }
      add_from_decoder(o, &t, o == sp);
      if (o == sp && !find(sp)) break;
    }
  }
  unsigned f2 = find(sp);
  return f2 ? mem + ent[f2 - 1].off : NULL;
}

/* the cached runs if the sprite is in the cache now (never decodes) */
const uint8_t *spr_peek(uint16_t sp) {
  unsigned f = sp < SPRITE_COUNT && mem ? find(sp) : 0;
  if (!f) return NULL;
  Entry *e = &ent[f - 1];
  e->used = tick;
  return mem + e->off;
}

const uint8_t *spr_stream(uint16_t sp) {
  uint32_t n = HDR(H_NSTREAMS);
  const uint8_t *t = ci_data + HDR(H_STREAMS);
  for (uint32_t i = 0; i < n; i++, t += 14)
    if (rd16(t) == sp) return t;
  return NULL;
}

uint32_t spr_cache_used(void) { return top; }
uint32_t spr_capacity(void) { return cache_bytes; }
