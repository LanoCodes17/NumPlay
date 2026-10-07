/* A small LZ77 (LZSS) packer, for the copies of the saves kept in Python
 * scripts: the calculator's file system holds 42 KB, so a copy that takes
 * less room leaves more of it to the saves themselves.
 *
 * The packed stream: a flag byte for each 8 items, lowest bit first; a set bit
 * is a literal byte, a clear one a match of two bytes: the distance back
 * minus 1 (12 bits, the low 8 in the first byte) and the length minus 3 (the
 * high 4 bits of the second byte), so 3 to 18 bytes up to 4096 back.
 *
 * Include in one file. */
#ifndef NP_LZ_H
#define NP_LZ_H
#include <stdint.h>

#define NP_LZ_HASH 4096 /* entries of the packer's table (uint16_t each) */
#define NP_LZ_BOUND(n) ((n) + (n) / 8 + 1) /* the packed size is never more */

/* Packs n bytes into out (out NULL: only measures); returns the packed size.
 * `table` holds NP_LZ_HASH entries; inputs up to 64 KB. */
__attribute__((unused)) static uint32_t np_lz_pack(const uint8_t *in, uint32_t n, uint8_t *out, uint16_t *table) {
  for (int i = 0; i < NP_LZ_HASH; i++) table[i] = 0xFFFF;
  uint32_t o = 0, flag_at = 0, items = 8, i = 0;
  while (i < n) {
    if (items == 8) {
      flag_at = o++;
      if (out) out[flag_at] = 0;
      items = 0;
    }
    uint32_t len = 0, dist = 0;
    if (i + 3 <= n) {
      uint32_t h = ((uint32_t)in[i] << 8 ^ (uint32_t)in[i + 1] << 4 ^ in[i + 2]) & (NP_LZ_HASH - 1);
      uint32_t cand = table[h];
      table[h] = (uint16_t)i;
      if (cand != 0xFFFF && cand < i && i - cand <= 4096) {
        uint32_t max = n - i < 18 ? n - i : 18;
        while (len < max && in[cand + len] == in[i + len]) len++;
        dist = i - cand;
      }
    }
    if (len >= 3) {
      if (out) {
        out[o] = (uint8_t)(dist - 1);
        out[o + 1] = (uint8_t)((dist - 1) >> 8 | (len - 3) << 4);
      }
      o += 2;
      /* (the bytes inside the match go into the table too, so later matches find them) */
      for (uint32_t k = 1; k < len && i + k + 3 <= n; k++) {
        uint32_t j = i + k;
        table[((uint32_t)in[j] << 8 ^ (uint32_t)in[j + 1] << 4 ^ in[j + 2]) & (NP_LZ_HASH - 1)] = (uint16_t)j;
      }
      i += len;
    } else {
      if (out) {
        out[flag_at] |= (uint8_t)(1 << items);
        out[o] = in[i];
      }
      o++, i++;
    }
    items++;
  }
  return o;
}

/* Unpacks into out (cap bytes at most); returns the size unpacked, or -1 if
 * the stream is damaged. */
__attribute__((unused)) static int32_t np_lz_unpack(const uint8_t *in, uint32_t n, uint8_t *out, uint32_t cap) {
  uint32_t i = 0, o = 0;
  while (i < n) {
    uint8_t flags = in[i++];
    for (int b = 0; b < 8 && i < n; b++) {
      if (flags >> b & 1) {
        if (o >= cap) return -1;
        out[o++] = in[i++];
      } else {
        if (i + 2 > n) return -1;
        uint32_t dist = ((uint32_t)in[i] | (uint32_t)(in[i + 1] & 15) << 8) + 1, len = (uint32_t)(in[i + 1] >> 4) + 3;
        i += 2;
        if (dist > o || o + len > cap) return -1;
        for (uint32_t k = 0; k < len; k++, o++) out[o] = out[o - dist];
      }
    }
  }
  return (int32_t)o;
}
#endif
