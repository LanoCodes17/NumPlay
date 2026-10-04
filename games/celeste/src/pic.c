#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("Os")   /* decoded once per picture: smaller over faster */
#endif
/* The big pictures of the menus: the chapters' end screens (CompleteRenderer) and the mountain behind the chapter
 * select (MountainRenderer), half size as baseline JPEG (tools/pictures.py: its markers taken out). One is decoded
 * into the cache at a time and drawn twice as big, smoothly. */
#include "celeste.h"

/* PICS: u16 n, u16 0, then per picture u16 w, h, u32 offset (from the section), u32 bytes; u8 quant[2][64] (zigzag
 * order); the Huffman tables DC0, AC0, DC1, AC1 (16 counts, then the symbols); the pictures' entropy coded data */
static const float COS[64] = {
    0.353553f, 0.353553f, 0.353553f, 0.353553f, 0.353553f, 0.353553f, 0.353553f, 0.353553f, 0.490393f, 0.415735f,
    0.277785f, 0.097545f, -0.097545f, -0.277785f, -0.415735f, -0.490393f, 0.461940f, 0.191342f, -0.191342f, -0.461940f,
    -0.461940f, -0.191342f, 0.191342f, 0.461940f, 0.415735f, -0.097545f, -0.490393f, -0.277785f, 0.277785f, 0.490393f,
    0.097545f, -0.415735f, 0.353553f, -0.353553f, -0.353553f, 0.353553f, 0.353553f, -0.353553f, -0.353553f, 0.353553f,
    0.277785f, -0.490393f, 0.097545f, 0.415735f, -0.415735f, -0.097545f, 0.490393f, -0.277785f, 0.191342f, -0.461940f,
    0.461940f, -0.191342f, -0.191342f, 0.461940f, -0.461940f, 0.191342f, 0.097545f, -0.277785f, 0.415735f, -0.490393f,
    0.490393f, -0.415735f, 0.277785f, -0.097545f};
static const uint8_t ZIGZAG[64] = {0,  1,  8,  16, 9,  2,  3,  10, 17, 24, 32, 25, 18, 11, 4,  5,  12, 19, 26, 33, 40, 48,
                                   41, 34, 27, 20, 13, 6,  7,  14, 21, 28, 35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23,
                                   30, 37, 44, 51, 58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63};

typedef struct {
  const uint8_t *p, *end;
  uint32_t acc;
  int n;
} Bits;
static int bit(Bits *b) {
  if (!b->n) b->acc = b->p < b->end ? *b->p++ : 0, b->n = 8;
  return b->acc >> --b->n & 1;
}
static int receive(Bits *b, int s) {   /* s bits, then their sign (EXTEND) */
  int v = 0;
  for (int i = 0; i < s; i++) v = v << 1 | bit(b);
  return s && v < 1 << (s - 1) ? v - (1 << s) + 1 : v;
}
static int huff(Bits *b, const uint8_t *t) {   /* t: 16 counts, then the symbols (canonical codes) */
  int code = 0, first = 0, index = 0;
  for (int len = 0; len < 16; len++) {
    code |= bit(b);
    int n = t[len];
    if (code - first < n) return t[16 + index + code - first];
    index += n, first = (first + n) << 1, code <<= 1;
  }
  return 0;
}

/* one 8x8 block: its coefficients, dequantized, then the inverse DCT into out (stride bytes a row), level shifted */
static void block(Bits *b, const uint8_t *dc, const uint8_t *ac, const uint8_t *q, int *pred, uint8_t *out, int stride) {
  float f[64], t[64];
  memset(f, 0, sizeof f);
  *pred += receive(b, huff(b, dc));
  f[0] = (float)(*pred * q[0]);
  for (int k = 1; k < 64;) {
    int rs = huff(b, ac), r = rs >> 4, s = rs & 15;
    if (!s) {
      if (r != 15) break;   /* end of block */
      k += 16;
      continue;
    }
    k += r;
    if (k > 63) break;
    f[ZIGZAG[k]] = (float)(receive(b, s) * q[k]);
    k++;
  }
  for (int v = 0; v < 8; v++)   /* rows: t[v][x] = sum over u of f[v][u] C[u][x] */
    for (int x = 0; x < 8; x++) {
      float s = 0;
      for (int u = 0; u < 8; u++) s += f[v * 8 + u] * COS[u * 8 + x];
      t[v * 8 + x] = s;
    }
  for (int y = 0; y < 8; y++)
    for (int x = 0; x < 8; x++) {
      float s = 128.5f;
      for (int v = 0; v < 8; v++) s += t[v * 8 + x] * COS[v * 8 + y];
      out[y * stride + x] = (uint8_t)clampi((int)s, 0, 255);
    }
}

int pic_count(void) { return rd16(section(SEC_PICS)); }

/* picture `id` into out (w * h colors, its size): false if there is none */
bool pic_decode(int id, uint16_t *out, int *w, int *h) {
  const uint8_t *s = section(SEC_PICS);
  int n = rd16(s);
  if (id < 0 || id >= n) return false;
  const uint8_t *e = s + 4 + 12 * id, *q = s + 4 + 12 * n, *t = q + 128, *tab[4];
  for (int i = 0; i < 4; i++) {
    tab[i] = t;
    int k = 16;
    for (int j = 0; j < 16; j++) k += t[j];
    t += k;
  }
  *w = rd16(e), *h = rd16(e + 2);
  Bits b = {s + rd32(e + 4), s + rd32(e + 4) + rd32(e + 8), 0, 0};
  int pred[3] = {0, 0, 0};
  uint8_t y[256], cb[64], cr[64];
  for (int my = 0; my < *h; my += 16)
    for (int mx = 0; mx < *w; mx += 16) {   /* 4:2:0: four luma blocks, then one of each chroma */
      for (int k = 0; k < 4; k++) block(&b, tab[0], tab[1], q, &pred[0], y + (k >> 1) * 128 + (k & 1) * 8, 16);
      block(&b, tab[2], tab[3], q + 64, &pred[1], cb, 8);
      block(&b, tab[2], tab[3], q + 64, &pred[2], cr, 8);
      for (int py = 0; py < 16 && my + py < *h; py++)
        for (int px = 0; px < 16 && mx + px < *w; px++) {
          int Y = y[py * 16 + px], c = (py >> 1) * 8 + (px >> 1), u = cb[c] - 128, v = cr[c] - 128;
          int r = clampi(Y + ((91881 * v) >> 16), 0, 255), g = clampi(Y - ((22554 * u + 46802 * v) >> 16), 0, 255),
              bl = clampi(Y + ((116130 * u) >> 16), 0, 255);
          out[(my + py) * *w + mx + px] = (uint16_t)((r >> 3) << 11 | (g >> 2) << 5 | bl >> 3);
        }
    }
  return true;
}

/* drawn twice as big with bilinear filtering (a strip layer: ctx is a PicLayer) */
static inline uint32_t spread(uint16_t c) { return (c | (uint32_t)c << 16) & 0x07E0F81Fu; }
void pic_strip(uint16_t *strip, int sy0, int sy1, void *ctx) {
  const PicLayer *l = ctx;
  int w = l->w, h = l->h;
  for (int Y = sy0; Y < sy1; Y++) {
    int ly = Y - l->y, j = ly >> 1, j2 = ly & 1 ? j + 1 : j - 1;   /* the nearer row (3/4), then the other (1/4) */
    j = clampi(j, 0, h - 1), j2 = clampi(j2, 0, h - 1);
    const uint16_t *ra = l->px + j * w, *rb = l->px + j2 * w;
    uint16_t *row = strip + (Y - sy0) * VIEW_W;
    for (int X = 0; X < VIEW_W; X++) {
      int lx = X - l->x, i = lx >> 1, i2 = lx & 1 ? i + 1 : i - 1;
      i = clampi(i, 0, w - 1), i2 = clampi(i2, 0, w - 1);
      uint32_t a = 3 * spread(ra[i]) + spread(rb[i]), c = 3 * spread(ra[i2]) + spread(rb[i2]);
      uint32_t v = ((3 * a + c) >> 4) & 0x07E0F81Fu;
      uint16_t px = (uint16_t)(v | v >> 16);
      row[X] = l->alpha >= 255 ? px : blend565(row[X], px, l->alpha + 1);
    }
  }
}
