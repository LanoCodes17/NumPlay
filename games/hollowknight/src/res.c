/* data.bin: its sections, LZMA streams, half floats. */
#include "hk.h"
#include "lzma/LzmaDec.h"

const uint8_t *hk_bin;

const uint8_t *section(int id) { return hk_bin + rd32(hk_bin + 8 + 8 * id); }
uint32_t section_size(int id) { return rd32(hk_bin + 12 + 8 * id); }

uint16_t g_scratch[4096];
#define LZ_PROBS ((CLzmaProb *)g_scratch)   /* NUM_BASE_PROBS + LZMA_LIT_SIZE << (lc + lp) = 2752, lc = lp = 0 */

/* a raw LZMA stream (lc = lp = pb = 0) decoded whole: the output is its own dictionary */
void lz_decode(const uint8_t *src, uint32_t comp, uint8_t *dst, uint32_t raw) {
  CLzmaDec dec;
  memset(&dec, 0, sizeof dec);
  dec.prop.lc = 0, dec.prop.lp = 0, dec.prop.pb = 0, dec.prop.dicSize = raw;
  dec.probs = LZ_PROBS;
  dec.probs_1664 = LZ_PROBS + 1664;
  dec.numProbs = 1984 + 768;
  dec.dic = dst;
  dec.dicBufSize = raw;
  LzmaDec_Init(&dec);
  SizeT inlen = comp;
  ELzmaStatus st;
  if (LzmaDec_DecodeToDic(&dec, raw, src, &inlen, LZMA_FINISH_ANY, &st) != SZ_OK || dec.dicPos != raw)
    memset(dst + dec.dicPos, 0, raw - dec.dicPos);   /* (never: the data is ours) */
}

float f16(uint16_t h) {
  uint32_t s = (uint32_t)(h & 0x8000) << 16, e = (h >> 10) & 31, m = h & 1023, f;
  if (e == 0) {
    if (!m) f = s;
    else {   /* subnormal */
      e = 113;
      while (!(m & 1024)) m <<= 1, e--;
      f = s | e << 23 | (m & 1023) << 13;
    }
  } else if (e == 31) f = s | 0x7F800000 | m << 13;
  else f = s | (e + 112) << 23 | m << 13;
  float r;
  memcpy(&r, &f, 4);
  return r;
}

const char *str_at(int id) {
  const uint8_t *s = section(SEC_STR);
  if ((uint32_t)id >= rd32(s)) return "";
  return (const char *)s + 4 + 4 * rd32(s) + rd32(s + 4 + 4 * id);
}
