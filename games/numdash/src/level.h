#ifndef NUMDASH_LEVEL_H
#define NUMDASH_LEVEL_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "objdefs.h"

#define LEVEL_COUNT 10
#define THEME_COUNT 9          /* editor themes: the colours of the first nine levels */
#define WIN_CAP 2600           /* objects resident at once */
#define WIN_CHUNKS 24
#define MAX_LEVEL_OBJS 20480   /* objects in the biggest level */
#define CUSTOM_MAX 800

/* Custom level object: GD units, sorted by x. xf: rotation (90 degree steps,
 * clockwise) in bits 0-1, flip x bit 2, flip y bit 3. */
typedef struct { uint16_t x; int16_t y; uint8_t type, xf; } LObj;

/* Resident object, 12 bytes: x and y in 1/32 units (biased), a scale index
 * in the top byte of xs; rotation (1024 steps, clockwise) and flips in ys. */
typedef struct { uint32_t xs, ys; uint16_t type, paint; } RObj;
#define OBJ_XBIAS (2048 * 32)
#define OBJ_YBIAS (1 << 19)
static inline float obj_x(const RObj *o) { return (float)((int32_t)(o->xs & 0xffffff) - OBJ_XBIAS) * (1.0f / 32); }
static inline float obj_y(const RObj *o) { return (float)((int32_t)(o->ys & 0xfffff) - OBJ_YBIAS) * (1.0f / 32); }
static inline unsigned obj_rot(const RObj *o) { return (o->ys >> 20) & 1023; }
static inline unsigned obj_flips(const RObj *o) { return o->ys >> 30; }
static inline unsigned obj_scale(const RObj *o) { return o->xs >> 24; }
/* Quarter turns in bits 0-1, flip x 4, flip y 8 (as LObj), or -1 when the
 * rotation is not a multiple of 90 degrees. */
static inline int obj_xf(const RObj *o) {
  unsigned r = obj_rot(o);
  return (r & 255) ? -1 : (int)((r >> 8) | obj_flips(o) << 2);
}
RObj obj_make(float x, float y, unsigned type, unsigned xf);

typedef struct { uint16_t x; int16_t y; uint8_t kind, arg, r, g, b, flags; uint16_t dur; } LevelEvent;
enum { EVF_TOUCH = 1, EVF_TINT_GROUND = 2, EVF_BLEND = 4 };
/* Colour channels, renumbered densely: the special GD channels first (BG
 * 1000, G1 1001, Line 1002, Obj 1004, 3DL 1003, P1 1005, P2 1006, LBG 1007,
 * G2 1009, black 1010, white 1011, lighter 1012), then the level's own. */
enum { CH_BG = 0, CH_G1 = 1, CH_LINE = 2, CH_OBJ = 3, CH_COUNT = 4, CH_3DL = 4, CH_P1 = 5, CH_P2 = 6, CH_LBG = 7,
       CH_G2 = 8, CH_BLACK = 9, CH_WHITE = 10, CH_LIGHTER = 11, CH_SPECIAL = 12 };

/* 2.0+ levels: initial channels, per-object styles (colours, z layer,
 * groups), group sets and a trigger stream. */
typedef struct { uint8_t rgb[3], flags, opacity, copy; int16_t h, s, v; } LChan;   /* s, v: x64 */
enum { CHF_BLEND = 1, CHF_P1 = 2, CHF_P2 = 4, CHF_SADD = 8, CHF_VADD = 16, CHF_COPY = 64 };
typedef struct { uint8_t main, detail, zlayer, flags; uint16_t groups; int16_t arg; } LStyle;
enum { STF_DONT_FADE = 1, STF_DONT_ENTER = 2, STF_HIDE = 4 };
typedef struct {
  const LChan *chans;
  const LStyle *styles;
  const uint16_t *gsets;          /* at a set's offset: count, then dense group indices */
  const uint16_t (*scales)[2];    /* x, y scale x1000 by scale index */
  const uint8_t *trig;            /* position triggers in x order, then the touch ones */
  const uint16_t *touch;          /* stream offsets of the touch triggers */
  uint16_t nchans, nstyles, ngroups, nscales;
  uint32_t trig_len;
  uint16_t ntouch;
  uint8_t bg, ground;             /* GD background and ground styles */
} LevelExt;
enum { TR_COLOR = 1, TR_MOVE, TR_ALPHA, TR_TOGGLE, TR_PULSE, TR_FADE, TR_TRAIL };

/* A chunk of a built-in level: one DEFLATE stream of objects whose x
 * (plus reach) spans [x0, x1]. fmt 1 = legacy rows. */
typedef struct {
  uint32_t off, base;
  int32_t xq0;
  float x0, x1;
  uint16_t len, count, nrec, rmax;
  uint8_t fmt;
} LChunk;

typedef struct {
  const char *name;
  const uint8_t *data;
  const LChunk *chunks;
  const LevelEvent *events;
  const LevelExt *ext;      /* 2.0+ levels, else NULL */
  uint32_t count;
  uint16_t nchunks, event_count, end_x, wall_x;
  uint32_t coins[3];
  uint8_t bg[3], g1[3], line[3], obj[3];
  uint8_t difficulty, stars, bpm, start_mode, coin_count;
} LevelDef;
extern const LevelDef level_defs[LEVEL_COUNT];

typedef struct Level {
  const char *name;
  const LevelDef *def;      /* built-in level streamed from flash, or NULL */
  const LevelExt *ext;      /* channels, styles, groups and triggers of 2.0+ levels */
  const LObj *flat;         /* custom level: every object resident */
  uint16_t flat_count;
  const LevelEvent *events;
  uint16_t event_count, end_x, wall_x;
  uint32_t count;           /* objects in the whole level */
  uint8_t colors[CH_COUNT][3];
  uint8_t bpm, start_mode, difficulty, stars;
  uint32_t coin_obj[3];     /* object indices of the secret coins, in order */
  uint8_t coin_count;
} Level;

/* Style of an object of a 2.0+ level (the default style otherwise). */
static inline const LStyle *obj_style(const Level *L, const RObj *o) {
  static const LStyle none = {255, 255, 0, 0, 0, 0};
  return L->ext && o->paint < L->ext->nstyles ? &L->ext->styles[o->paint] : &none;
}

/* Scale of an object (x and y; 1 unless a 2.0+ level scales it). */
static inline void obj_scale_xy(const Level *L, const RObj *o, float *sx, float *sy) {
  unsigned k = obj_scale(o);
  if (!k || !L->ext || k >= L->ext->nscales) { *sx = *sy = 1; return; }
  *sx = L->ext->scales[k][0] * 0.001f;
  *sy = L->ext->scales[k][1] * 0.001f;
}
static inline float scale_of(const Level *L, const RObj *o) {
  float sx, sy;
  obj_scale_xy(L, o, &sx, &sy);
  sx = sx < 0 ? -sx : sx;
  sy = sy < 0 ? -sy : sy;
  return sx > sy ? sx : sy;
}

/* Objects near the camera live in one shared window. Iterating a range makes
 * it resident first, so the physics never depends on what was loaded
 * before. Objects come in level order (chunk by chunk, each sorted by x). */
typedef struct {
  const Level *L;
  uint8_t c;               /* current window chunk */
  unsigned i, end, first;
  float lo, hi;
  unsigned gi;             /* level-wide index of the object last returned */
} LIter;
void level_iter(const Level *L, float x0, float x1, LIter *it);
const RObj *level_next(LIter *it);
/* Makes objects whose reach overlaps [x0, x1] resident. */
void level_stream(const Level *L, float x0, float x1);

int inflate_raw(uint8_t *out, size_t cap, const uint8_t *in, size_t len);
bool level_load_builtin(Level *L, unsigned index);
/* Custom levels: objs stays owned by the caller and is re-read on changes. */
void level_load_flat(Level *L, const LObj *objs, unsigned count);
void level_index_coins(Level *L);
bool level_valid(const Level *L);
/* Decodes every chunk of a built-in level and checks its objects (tests). */
bool level_check_all(const Level *L, unsigned *coins);
/* Rotated hitbox half extents in GD units. */
void obj_hitbox(const RObj *o, float *hw, float *hh);
/* Scratch memory shared by chunk decoding and custom level saving. */
#define LEVEL_SCRATCH 6400
extern uint8_t level_scratch[LEVEL_SCRATCH];
/* The window's memory as LObj scratch (it is emptied), for old saves. */
LObj *level_big_scratch(unsigned *cap);
/* The editor's custom level. */
extern LObj *const level_objs;
#endif
