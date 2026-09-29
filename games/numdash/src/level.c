/* Level data: built-in levels are streamed chunk by chunk into a window of
 * resident objects; custom levels are copied into it whole. */
#include "level.h"
#include <string.h>

uint8_t level_scratch[LEVEL_SCRATCH];

typedef struct { uint16_t chunk, first, count, rmax; float x0, x1; uint32_t base; } WinChunk;
static struct {
  const Level *owner;
  const LObj *flat;       /* the flat array the window was built from */
  unsigned flat_count;
  unsigned count;
  WinChunk wc[WIN_CHUNKS];
  uint8_t nwc;
} win;
static RObj win_objs[WIN_CAP];
/* The editor's level shares the window's memory: its end is free while a
 * custom level (at most CUSTOM_MAX objects) is resident, and the editor
 * reloads its level from storage after a built-in level was played. */
LObj *const level_objs = (LObj *)(void *)((uint8_t *)win_objs + sizeof(win_objs) - CUSTOM_MAX * sizeof(LObj));
_Static_assert(CUSTOM_MAX * sizeof(RObj) + CUSTOM_MAX * sizeof(LObj) <= sizeof(win_objs), "window too small for the editor");

RObj obj_make(float x, float y, unsigned type, unsigned xf) {
  uint32_t xq = (uint32_t)((int32_t)(x * 32 + (x >= 0 ? .5f : -.5f)) + OBJ_XBIAS);
  uint32_t yq = (uint32_t)((int32_t)(y * 32 + (y >= 0 ? .5f : -.5f)) + OBJ_YBIAS);
  RObj o;
  o.xs = xq & 0xffffff;
  o.ys = (yq & 0xfffff) | (uint32_t)(xf & 3) << 28 | (uint32_t)((xf >> 2) & 3) << 30;
  o.type = (uint16_t)type;
  o.paint = 0;
  return o;
}

/* ------------------------------------------------------------ decoding */

static const uint8_t *rd_p;
static unsigned rd_varint(void) {
  unsigned v = 0;
  for (int s = 0; s < 28; s += 7) {
    uint8_t b = *rd_p++;
    v |= (unsigned)(b & 127) << s;
    if (!(b & 128)) break;
  }
  return v;
}
static int rd_zz(void) { unsigned v = rd_varint(); return (v & 1) ? -(int)((v + 1) >> 1) : (int)(v >> 1); }

/* Sorts n objects by x (stable; chunks come almost sorted). */
static void sort_x(RObj *o, unsigned n) {
  for (unsigned i = 1; i < n; i++) {
    RObj t = o[i];
    uint32_t x = t.xs & 0xffffff;
    unsigned j = i;
    while (j && (o[j - 1].xs & 0xffffff) > x) { o[j] = o[j - 1]; j--; }
    o[j] = t;
  }
}

/* Inflates chunk c into out (room for c->count objects). */
static bool decode_chunk(const LevelDef *d, const LChunk *c, RObj *out) {
  int n = inflate_raw(level_scratch, LEVEL_SCRATCH, d->data + c->off, c->len);
  if (n < 0) return false;
  if (c->fmt == 1) {
    if ((unsigned)n != c->count * 6u) return false;
    int32_t x = c->xq0 / 32;
    for (unsigned i = 0; i < c->count; i++) {
      const uint8_t *r = level_scratch + i * 6;
      x += r[0] | r[1] << 8;
      out[i] = obj_make((float)x, (float)(int16_t)(r[2] | r[3] << 8), r[4], r[5]);
    }
    return true;
  }
  /* column-coded records, sorted by type; runs expand afterwards */
  unsigned nrec, cnt = c->count;
  rd_p = level_scratch;
  nrec = rd_varint();
  if (nrec != c->nrec || nrec > cnt) return false;
  RObj *rec = out + (cnt - nrec);           /* records sit at the end, expanded forward */
  uint8_t *props = level_scratch + LEVEL_SCRATCH - nrec;
  if ((unsigned)n + nrec > LEVEL_SCRATCH) return false;
  unsigned t = 0;
  for (unsigned i = 0; i < nrec; i++) { t += (unsigned)rd_zz(); rec[i].type = (uint16_t)t; rec[i].paint = 0; }
  for (unsigned i = 0; i < nrec; i++) props[i] = *rd_p++;
  int32_t px = c->xq0, py = 0;
  unsigned pt = 0;
  for (unsigned i = 0; i < nrec; i++) {
    if (rec[i].type != pt) { px = c->xq0; py = 0; pt = rec[i].type; }
    int u = (props[i] & 64) ? 240 : 1;
    px += rd_zz() * u;
    rec[i].xs = (uint32_t)(px + OBJ_XBIAS) & 0xffffff;
    rec[i].ys = 0;
  }
  pt = 0;
  for (unsigned i = 0; i < nrec; i++) {
    if (rec[i].type != pt) { py = 0; pt = rec[i].type; }
    int u = (props[i] & 64) ? 240 : 1;
    py += rd_zz() * u;
    rec[i].ys = (uint32_t)(py + OBJ_YBIAS) & 0xfffff;
  }
  for (unsigned i = 0; i < nrec; i++) if (props[i] & 1) rec[i].ys |= (rd_varint() & 1023) << 20;
  for (unsigned i = 0; i < nrec; i++) if (props[i] & 2) rec[i].ys |= (uint32_t)(*rd_p++ & 3) << 30;
  for (unsigned i = 0; i < nrec; i++) if (props[i] & 4) rec[i].xs |= (rd_varint() & 255) << 24;
  for (unsigned i = 0; i < nrec; i++) if (props[i] & 8) rec[i].paint = (uint16_t)rd_varint();
  /* expand runs: records move to the front, each followed by its copies */
  unsigned w = 0;
  for (unsigned i = 0; i < nrec; i++) {
    RObj r = rec[i];
    unsigned k = 1;
    int32_t step = 0;
    bool col = false;
    if (props[i] & 48) {
      k = 2u + *rd_p++;
      step = (int32_t)*rd_p++ * 240;
      col = (props[i] & 32) != 0;
    }
    if (w + k > cnt || (w > cnt - nrec + i)) return false;
    for (unsigned j = 0; j < k; j++) {
      RObj o = r;
      if (col) o.ys = (r.ys & ~0xfffffu) | ((r.ys + (uint32_t)(step * (int32_t)j)) & 0xfffff);
      else o.xs = (r.xs & ~0xffffffu) | ((r.xs + (uint32_t)(step * (int32_t)j)) & 0xffffff);
      out[w++] = o;
    }
  }
  if (w != cnt || rd_p != level_scratch + n) return false;
  sort_x(out, cnt);
  return true;
}

/* ------------------------------------------------------------ window */

static void win_reset(const Level *L) {
  win.owner = L;
  win.count = 0;
  win.nwc = 0;
  win.flat = NULL;
  if (L && !L->def) {
    unsigned n = L->flat_count < WIN_CAP ? L->flat_count : WIN_CAP;
    for (unsigned i = 0; i < n; i++) {
      const LObj *o = &L->flat[i];
      win_objs[i] = obj_make(o->x, o->y, o->type, o->xf);
    }
    win.count = n;
    win.flat = L->flat;
    win.flat_count = L->flat_count;
    if (n) {
      win.wc[0] = (WinChunk){0, 0, (uint16_t)n, 45, -1e9f, 1e9f, 0};
      win.nwc = 1;
    }
  }
}

static void win_unload(unsigned k) {
  WinChunk c = win.wc[k];
  memmove(win_objs + c.first, win_objs + c.first + c.count, (win.count - c.first - c.count) * sizeof(RObj));
  win.count -= c.count;
  for (unsigned j = 0; j < win.nwc; j++)
    if (win.wc[j].first > c.first) win.wc[j].first = (uint16_t)(win.wc[j].first - c.count);
  memmove(win.wc + k, win.wc + k + 1, (win.nwc - k - 1) * sizeof(WinChunk));
  win.nwc--;
}

static void win_load(const LevelDef *d, unsigned ci, float x0, float x1) {
  const LChunk *c = &d->chunks[ci];
  /* make room: drop the chunks farthest from the wanted range */
  while (win.count + c->count > WIN_CAP || win.nwc >= WIN_CHUNKS) {
    int best = -1;
    float far = -1;
    for (unsigned j = 0; j < win.nwc; j++) {
      const WinChunk *w = &win.wc[j];
      float dist = w->x1 < x0 ? x0 - w->x1 : w->x0 > x1 ? w->x0 - x1 : -1;
      if (dist > far) { far = dist; best = (int)j; }
    }
    if (best < 0) return;   /* everything is needed: the window is too small */
    win_unload((unsigned)best);
  }
  if (!decode_chunk(d, c, win_objs + win.count)) return;
  unsigned k = win.nwc;
  while (k && win.wc[k - 1].chunk > ci) { win.wc[k] = win.wc[k - 1]; k--; }
  win.wc[k] = (WinChunk){(uint16_t)ci, (uint16_t)win.count, c->count, c->rmax, c->x0, c->x1, c->base};
  win.nwc++;
  win.count += c->count;
}

void level_stream(const Level *L, float x0, float x1) {
  if (win.owner != L || (L && !L->def && (win.flat != L->flat || win.flat_count != L->flat_count))) win_reset(L);
  if (!L || !L->def) return;
  const LevelDef *d = L->def;
  for (unsigned ci = 0; ci < d->nchunks; ci++) {
    const LChunk *c = &d->chunks[ci];
    if (c->x0 > x1) break;
    if (c->x1 < x0) continue;
    bool have = false;
    for (unsigned j = 0; j < win.nwc && !have; j++) have = win.wc[j].chunk == ci;
    if (!have) win_load(d, ci, x0, x1);
  }
}

void level_iter(const Level *L, float x0, float x1, LIter *it) {
  level_stream(L, x0, x1);
  it->L = L;
  it->c = 0;
  it->i = it->end = it->first = 0;
  it->lo = x0;
  it->hi = x1;
}

const RObj *level_next(LIter *it) {
  for (;;) {
    if (it->i < it->end) {
      const RObj *o = &win_objs[it->i];
      const WinChunk *w = &win.wc[it->c - 1];
      if (obj_x(o) > it->hi + w->rmax) { it->i = it->end; continue; }
      it->gi = w->base + (it->i - it->first);
      it->i++;
      return o;
    }
    if (it->c >= win.nwc) return NULL;
    const WinChunk *w = &win.wc[it->c++];
    if (w->x1 < it->lo || w->x0 > it->hi) { it->i = it->end = 0; continue; }
    /* first object whose centre is not left of lo - rmax */
    unsigned a = w->first, b = w->first + w->count;
    float lo = it->lo - w->rmax;
    while (a < b) {
      unsigned m = (a + b) / 2;
      if (obj_x(&win_objs[m]) < lo) a = m + 1; else b = m;
    }
    it->first = w->first;
    it->i = a;
    it->end = w->first + w->count;
  }
}

LObj *level_big_scratch(unsigned *cap) {
  win.owner = NULL;
  win.count = 0;
  win.nwc = 0;
  *cap = (unsigned)(sizeof(win_objs) / (sizeof(LObj)));
  return (LObj *)(void *)win_objs;
}

/* ------------------------------------------------------------ loading */

bool level_load_builtin(Level *L, unsigned index) {
  if (index >= LEVEL_COUNT) return false;
  const LevelDef *d = &level_defs[index];
  if (d->count > MAX_LEVEL_OBJS) return false;
  memset(L, 0, sizeof(*L));
  L->name = d->name;
  L->def = d;
  L->count = d->count;
  L->events = d->events;
  L->event_count = d->event_count;
  L->ext = d->ext;
  L->end_x = d->end_x;
  L->wall_x = d->wall_x;
  memcpy(L->colors[CH_BG], d->bg, 3);
  memcpy(L->colors[CH_G1], d->g1, 3);
  memcpy(L->colors[CH_LINE], d->line, 3);
  memcpy(L->colors[CH_OBJ], d->obj, 3);
  L->bpm = d->bpm;
  L->start_mode = d->start_mode;
  L->difficulty = d->difficulty;
  L->stars = d->stars;
  L->coin_count = d->coin_count;
  memcpy(L->coin_obj, d->coins, sizeof(L->coin_obj));
  win_reset(L);
  return level_valid(L);
}

void level_load_flat(Level *L, const LObj *objs, unsigned count) {
  L->def = NULL;
  L->flat = objs;
  L->flat_count = (uint16_t)count;
  L->count = count;
  level_index_coins(L);
  win_reset(L);
}

void level_index_coins(Level *L) {
  L->coin_count = 0;
  for (unsigned i = 0; i < L->flat_count && L->coin_count < 3; i++)
    if (L->flat[i].type == OT_COIN) L->coin_obj[L->coin_count++] = i;
}

static bool valid_obj(const RObj *o) {
  float y = obj_y(o);
  return o->type && o->type < OT_COUNT && y >= -600 && y <= 3000;
}

bool level_valid(const Level *L) {
  if (!L || L->end_x < 300 || L->wall_x < L->end_x - 30) return false;
  if (L->def) {
    /* every chunk must decode to what its header says */
    const LevelDef *d = L->def;
    for (unsigned ci = 0; ci < d->nchunks; ci++) {
      const LChunk *c = &d->chunks[ci];
      if (c->count > WIN_CAP || (ci && c->base != d->chunks[ci - 1].base + d->chunks[ci - 1].count)) return false;
    }
  } else {
    if (!L->flat || L->flat_count > CUSTOM_MAX) return false;
    for (unsigned i = 0; i < L->flat_count; i++) {
      const LObj *o = &L->flat[i];
      if (!o->type || o->type >= OT_COUNT || o->y < -600 || o->y > 3000 || (o->xf & 0xf0) || (i && o->x < L->flat[i - 1].x))
        return false;
    }
  }
  for (unsigned i = 0; i < L->event_count; i++)
    if (!L->events[i].kind || L->events[i].kind > EV_TRAIL) return false;
  return true;
}

/* Checks every object of a built-in level (tests). */
bool level_check_all(const Level *L, unsigned *coins) {
  if (!L->def) return false;
  const LevelDef *d = L->def;
  unsigned n = 0;
  *coins = 0;
  for (unsigned ci = 0; ci < d->nchunks; ci++) {
    const LChunk *c = &d->chunks[ci];
    level_stream(L, c->x0, c->x1);
    LIter it;
    level_iter(L, -1e9f, 1e9f, &it);
    for (const RObj *o; (o = level_next(&it));) {
      if (it.gi < c->base || it.gi >= c->base + c->count) continue;
      if (!valid_obj(o)) return false;
      if (o->type == OT_COIN) (*coins)++;
      n++;
    }
  }
  return n == d->count;
}

void obj_hitbox(const RObj *o, float *hw, float *hh) {
  const ObjDef *d = &objdefs[o->type];
  float w = d->w10 * 0.05f, h = d->h10 * 0.05f;
  if (obj_rot(o) & 256) { *hw = h; *hh = w; } else { *hw = w; *hh = h; }
}
