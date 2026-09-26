/* Raw DEFLATE (RFC 1951) decoder for the screenshots. Huffman codes of up to
 * 9 bits are decoded with one table lookup; longer ones bit by bit. The tables
 * live in memory the caller provides (the arena), not in static RAM. */
#include "inflate.h"
#include <string.h>

typedef struct {
  uint16_t fast[512];   /* (length << 9) | symbol for codes of up to 9 bits, 0 otherwise */
  uint16_t count[16];
  uint16_t symbol[288];
} huff_t;

struct np_inflate_tables {
  huff_t lit, dist;
};

typedef struct {
  const uint8_t *in, *end;
  uint32_t bits;
  int nbits, overrun;
  uint8_t *out;
  uint32_t n, cap;
} Z;

static inline void need(Z *z, int k) {
  while (z->nbits < k) {
    uint32_t b = 0;
    if (z->in < z->end) b = *z->in++;
    else z->overrun++;
    z->bits |= b << z->nbits;
    z->nbits += 8;
  }
}

static inline uint32_t take(Z *z, int k) {
  if (!k) return 0;
  need(z, k);
  uint32_t v = z->bits & ((1u << k) - 1);
  z->bits >>= k;
  z->nbits -= k;
  return v;
}

static bool build(huff_t *h, const uint8_t *len, int n) {
  uint16_t offs[16], next[16];
  memset(h->count, 0, sizeof h->count);
  memset(h->fast, 0, sizeof h->fast);
  for (int i = 0; i < n; i++) h->count[len[i]]++;
  h->count[0] = 0;
  int left = 1;
  for (int l = 1; l < 16; l++) {
    left = (left << 1) - h->count[l];
    if (left < 0) return false;
  }
  offs[1] = 0;
  for (int l = 1; l < 15; l++) offs[l + 1] = (uint16_t)(offs[l] + h->count[l]);
  for (int i = 0; i < n; i++)
    if (len[i]) h->symbol[offs[len[i]]++] = (uint16_t)i;
  /* canonical codes, bit-reversed for the fast table */
  int code = 0;
  for (int l = 1; l < 16; l++) {
    code = (code + (l > 1 ? h->count[l - 1] : 0)) << 1;
    next[l] = (uint16_t)code;
  }
  for (int i = 0; i < n; i++) {
    int l = len[i];
    if (!l) continue;
    int c = next[l]++;
    if (l > 9) continue;
    int rev = 0;
    for (int b = 0; b < l; b++) rev |= ((c >> b) & 1) << (l - 1 - b);
    for (int k = rev; k < 512; k += 1 << l) h->fast[k] = (uint16_t)(l << 9 | i);
  }
  return true;
}

static int decode(Z *z, const huff_t *h) {
  need(z, 9);
  uint16_t e = h->fast[z->bits & 511];
  if (e) {
    int l = e >> 9;
    z->bits >>= l;
    z->nbits -= l;
    return e & 511;
  }
  int code = 0, first = 0, index = 0;
  for (int l = 1; l < 16; l++) {
    code |= (int)take(z, 1);
    int count = h->count[l];
    if (code - count < first) return h->symbol[index + (code - first)];
    index += count;
    first = (first + count) << 1;
    code <<= 1;
  }
  return -1;
}

static const uint16_t len_base[29] = {3,  4,  5,  6,  7,  8,  9,  10, 11,  13,  15,  17,  19,  23, 27,
                                      31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
static const uint8_t len_extra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
static const uint16_t dist_base[30] = {1,   2,   3,   4,   5,   7,    9,    13,   17,   25,   33,   49,   65,    97,    129,
                                       193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
static const uint8_t dist_extra[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

static bool codes(Z *z, const huff_t *lit, const huff_t *dist) {
  for (;;) {
    int sym = decode(z, lit);
    if (sym < 0 || z->overrun > 4) return false;
    if (sym < 256) {
      if (z->n >= z->cap) return false;
      z->out[z->n++] = (uint8_t)sym;
    } else if (sym == 256) {
      return true;
    } else {
      sym -= 257;
      if (sym >= 29) return false;
      uint32_t len = len_base[sym] + take(z, len_extra[sym]);
      int d = decode(z, dist);
      if (d < 0 || d >= 30) return false;
      uint32_t back = dist_base[d] + take(z, dist_extra[d]);
      if (back > z->n || z->n + len > z->cap) return false;
      uint8_t *o = z->out + z->n, *from = o - back;
      for (uint32_t i = 0; i < len; i++) o[i] = from[i];
      z->n += len;
    }
  }
}

uint32_t np_inflate_tables_size(void) { return sizeof(struct np_inflate_tables); }

int np_inflate(uint8_t *out, uint32_t cap, const uint8_t *in, uint32_t len, np_inflate_tables_t *t) {
  static const uint8_t order[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
  Z z = {in, in + len, 0, 0, 0, out, 0, cap};
  uint8_t lengths[320];
  int last;
  do {
    last = (int)take(&z, 1);
    int type = (int)take(&z, 2);
    if (type == 0) {
      z.bits = 0;
      z.nbits = 0;
      if (z.in + 4 > z.end) return -1;
      unsigned n = z.in[0] | (unsigned)z.in[1] << 8, nn = z.in[2] | (unsigned)z.in[3] << 8;
      z.in += 4;
      if ((n ^ 0xFFFFu) != nn || z.in + n > z.end || z.n + n > z.cap) return -1;
      memcpy(z.out + z.n, z.in, n);
      z.in += n;
      z.n += n;
    } else if (type == 1) {
      int i = 0;
      for (; i < 144; i++) lengths[i] = 8;
      for (; i < 256; i++) lengths[i] = 9;
      for (; i < 280; i++) lengths[i] = 7;
      for (; i < 288; i++) lengths[i] = 8;
      build(&t->lit, lengths, 288);
      for (i = 0; i < 30; i++) lengths[i] = 5;
      build(&t->dist, lengths, 30);
      if (!codes(&z, &t->lit, &t->dist)) return -1;
    } else if (type == 2) {
      int nlen = (int)take(&z, 5) + 257, ndist = (int)take(&z, 5) + 1, ncode = (int)take(&z, 4) + 4;
      if (nlen > 286 || ndist > 30) return -1;
      for (int i = 0; i < 19; i++) lengths[order[i]] = i < ncode ? (uint8_t)take(&z, 3) : 0;
      if (!build(&t->lit, lengths, 19)) return -1;
      int i = 0;
      while (i < nlen + ndist) {
        int sym = decode(&z, &t->lit);
        if (sym < 0 || z.overrun > 4) return -1;
        if (sym < 16) {
          lengths[i++] = (uint8_t)sym;
          continue;
        }
        int rep, val = 0;
        if (sym == 16) {
          if (!i) return -1;
          val = lengths[i - 1];
          rep = 3 + (int)take(&z, 2);
        } else if (sym == 17) {
          rep = 3 + (int)take(&z, 3);
        } else {
          rep = 11 + (int)take(&z, 7);
        }
        if (i + rep > nlen + ndist) return -1;
        while (rep--) lengths[i++] = (uint8_t)val;
      }
      if (!lengths[256]) return -1;
      if (!build(&t->lit, lengths, nlen) || !build(&t->dist, lengths + nlen, ndist)) return -1;
      if (!codes(&z, &t->lit, &t->dist)) return -1;
    } else {
      return -1;
    }
  } while (!last);
  return (int)z.n;
}
