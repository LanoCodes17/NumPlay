/* The edit log: one entry per block the player changed, kept in chunk order
 * so a chunk's entries are found by a binary search. */
#include "edits.h"
#include "nb.h"

typedef struct {
  int16_t cx, cz;
  uint8_t xz, y, b, pad;   /* xz: x | z << 4 */
} Edit;

static Edit ed[EDITS_MAX];
static int ned;

void edits_clear(void) { ned = 0; }
int edits_count(void) { return ned; }

static inline int floordiv16(int a) { return a >= 0 ? a >> 4 : -((-a + 15) >> 4); }

static inline int64_t key(int cx, int cz, int xz, int y) {
  return ((int64_t)(cz + 32768) << 40) | ((int64_t)(cx + 32768) << 24) | (int64_t)(xz << 8) | y;
}
static inline int64_t ekey(const Edit *e) { return key(e->cx, e->cz, e->xz, e->y); }

/* the first entry with a key >= k */
static int lower(int64_t k) {
  int lo = 0, hi = ned;
  while (lo < hi) {
    int m = (lo + hi) / 2;
    if (ekey(&ed[m]) < k) lo = m + 1;
    else hi = m;
  }
  return lo;
}

bool edits_put(int x, int y, int z, int b) {
  int cx = floordiv16(x), cz = floordiv16(z), xz = (x - cx * 16) | (z - cz * 16) << 4;
  int64_t k = key(cx, cz, xz, y);
  int i = lower(k);
  if (i < ned && ekey(&ed[i]) == k) {
    ed[i].b = (uint8_t)b;
    return true;
  }
  if (ned >= EDITS_MAX) return false;
  memmove(&ed[i + 1], &ed[i], (size_t)(ned - i) * sizeof ed[0]);
  ed[i] = (Edit){(int16_t)cx, (int16_t)cz, (uint8_t)xz, (uint8_t)y, (uint8_t)b, 0};
  ned++;
  return true;
}

void edits_apply(int cx, int cz, int y0, int h, uint8_t *slab) {
  for (int i = lower(key(cx, cz, 0, 0)); i < ned && ed[i].cx == cx && ed[i].cz == cz; i++) {
    int y = ed[i].y - y0;
    if (y >= 0 && y < h) slab[(y * 16 + (ed[i].xz >> 4)) * 16 + (ed[i].xz & 15)] = ed[i].b;
  }
}

int edits_top(int x, int z, int top) {
  int cx = floordiv16(x), cz = floordiv16(z), xz = (x - cx * 16) | (z - cz * 16) << 4;
  for (int i = lower(key(cx, cz, xz, 0)); i < ned && ed[i].cx == cx && ed[i].cz == cz && ed[i].xz == xz; i++)
    if ((blk_flags[ed[i].b] & BF_OPAQUE) && ed[i].y + 1 > top) top = ed[i].y + 1;
  return top;
}
