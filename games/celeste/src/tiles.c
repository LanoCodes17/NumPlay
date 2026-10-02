/* Tiles: Celeste's autotiler (ForegroundTiles.xml, BackgroundTiles.xml, as
 * converted by tools/pack.py) run on the room's tile types, the visible tiles
 * cached between frames, and the tile layers and decals as entities. */
#include "level.h"

/* ---------------------------------------------------------------- terrain tables */
static const uint8_t *terrain(int t) {
  const uint8_t *s = section(SEC_TILESETS);
  return s + rd32(s + 260 + 4 * t);
}
int terrain_of(int layer, char c) {
  if ((uint8_t)c >= 128) return -1;
  uint8_t t = section(SEC_TILESETS)[layer * 128 + (uint8_t)c];
  return t == 255 ? -1 : t;
}
uint16_t terrain_tex(int t) { return rd16(terrain(t)); }
/* the cache keeps a layer's own terrain numbers (5 bits): the bg's start after the fg's */
static int layer_base(int layer) {
  static int fg_count = -1;
  if (!layer) return 0;
  if (fg_count < 0) {
    const uint8_t *tab = section(SEC_TILESETS);
    fg_count = 0;
    for (int i = 0; i < 128; i++)
      if (tab[i] != 255 && tab[i] + 1 > fg_count) fg_count = tab[i] + 1;
  }
  return fg_count;
}

static bool ignores(const uint8_t *T, char self_type, char c) {
  /* TerrainType.Ignore: other types in the list, or all with '*' */
  int n = T[2];
  if (c == self_type) return false;
  bool all = false;
  for (int i = 0; i < n; i++) {
    if (T[3 + i] == (uint8_t)c) return true;
    if (T[3 + i] == '*') all = true;
  }
  return all;
}

static inline uint32_t hash3(int x, int y, int z) {
  uint32_t h = (uint32_t)x * 73856093u ^ (uint32_t)y * 19349663u ^ (uint32_t)z * 83492791u;
  h ^= h >> 13;
  h *= 0x5bd1e995u;
  return h ^ (h >> 15);
}

/* tile types around (x, y) come from get(); in_level(x, y) tells whether a cell
 * belongs to the same room (PaddingIgnoreOutOfLevel). Returns the quad or -1,
 * and the overlay sprite (0: none) in *ov. */
typedef char (*GetFn)(int x, int y, void *ctx);
typedef struct { GetFn get; void *ctx; bool (*in_level)(int x, int y, void *ctx); bool edges_ignore_out; } AutoCtx;

static int autotile_at(int layer, char c, const AutoCtx *a, int x, int y, int *ov, int salt) {
  int t = terrain_of(layer, c);
  if (t < 0) return -1;
  const uint8_t *T = terrain(t);
  int nign = T[2];
  const uint8_t *p = T + 3 + nign;
  int nmask = p[0], npad = p[1], ncen = p[2];
  p += 3;
  static const int8_t dx[8] = {-1, 0, 1, -1, 1, -1, 0, 1}, dy[8] = {-1, -1, -1, 0, 0, 1, 1, 1};
  uint8_t adj = 0;
  for (int k = 0; k < 8; k++) {
    char n = a->get(x + dx[k], y + dy[k], a->ctx);
    bool on = n != '0' && n != 0 && !ignores(T, c, n);
    if (!on && a->edges_ignore_out && a->in_level && !a->in_level(x + dx[k], y + dy[k], a->ctx)) on = true;
    if (on) adj |= (uint8_t)(1 << (7 - k));
  }
  *ov = 0;
  uint32_t h = hash3(x, y, layer * 7 + salt);
  if (adj == 0xFF) {
    /* padding where a tile two away (in this room) is empty */
    static const int8_t px[4] = {-2, 2, 0, 0}, py[4] = {0, 0, -2, 2};
    bool padded = false;
    for (int k = 0; k < 4 && !padded; k++) {
      char n = a->get(x + px[k], y + py[k], a->ctx);
      bool on = n != '0' && n != 0 && !ignores(T, c, n);
      if (!on && (!a->in_level || a->in_level(x + px[k], y + py[k], a->ctx))) padded = true;
    }
    const uint8_t *m = p;
    for (int i = 0; i < nmask; i++) m += 4 + m[2];
    if (padded) return npad ? m[h % npad] : -1;
    return ncen ? m[npad + h % ncen] : -1;
  }
  for (int i = 0; i < nmask; i++) {
    uint8_t bits = p[0], care = p[1], n = p[2];
    if (((bits ^ adj) & care) == 0) {
      *ov = p[3];
      return n ? p[4 + h % n] : -1;
    }
    p += 4 + n;
  }
  return -1;
}

int autotile(int layer, char (*get)(int x, int y, void *ctx), void *ctx, int x, int y, uint8_t *quad, int variant) {
  AutoCtx a = {get, ctx, NULL, false};
  int ov;
  int q = autotile_at(layer, get(x, y, ctx), &a, x, y, &ov, variant);
  if (q < 0) return 0;
  *quad = (uint8_t)q;
  return 1;
}

/* ---------------------------------------------------------------- the room's tiles, cached */
#define TC_W 42
#define TC_H 25
typedef struct {
  int cx, cy;         /* top-left cell */
  bool valid;
  uint16_t cell[TC_H][TC_W];   /* terrain (5 bits; 31 none, 30 scenery) | quad << 5 (9 bits) | overlay << 14 */
} TileCache;
#define C_TERR(v) ((v) & 31)
#define C_QUAD(v) (((v) >> 5) & 511)
#define C_OV(v) ((v) >> 14)
#define TERR_SCENERY 30
static TileCache tcache[2];

void tiles_invalidate(void) { tcache[0].valid = tcache[1].valid = false; }

/* the tile types of the window and 2 cells around it (what the autotiler looks at), read a row at a time */
#define WIN_W (TC_W + 4)
#define WIN_H (TC_H + 4)
typedef struct { int layer; const Room *room; char (*win)[WIN_W]; int wx, wy; } RoomCtx;
static char room_get(int x, int y, void *ctx) {
  RoomCtx *rc = ctx;
  unsigned u = (unsigned)(x - rc->wx), v = (unsigned)(y - rc->wy);
  return u < WIN_W && v < WIN_H ? rc->win[v][u] : level_tile_type(rc->layer, x, y);
}
static bool in_room(const Room *rm, int x, int y) {
  int px = x * 8, py = y * 8;
  return rm && px >= rm->x && px < rm->x + rm->w && py >= rm->y && py < rm->y + rm->h;
}
/* CheckForSameLevel: the other cell is in the room of the cell being tiled */
static bool room_in_level(int x, int y, void *ctx) { return in_room(((RoomCtx *)ctx)->room, x, y); }

/* the cache's window at cell (cx, cy): the cells already there move along, only the new ones are tiled */
static void fill_cache(int layer, int cx, int cy) {
  TileCache *c = &tcache[layer];
  int dx = cx - c->cx, dy = cy - c->cy, ox = c->cx, oy = c->cy;
  bool keep = c->valid && dx > -TC_W && dx < TC_W && dy > -TC_H && dy < TC_H;
  if (keep) {
    int ady = dy < 0 ? -dy : dy, adx = dx < 0 ? -dx : dx;
    if (dy) memmove(&c->cell[dy < 0 ? ady : 0], &c->cell[dy > 0 ? ady : 0], sizeof c->cell[0] * (size_t)(TC_H - ady));
    if (dx)
      for (int r = 0; r < TC_H; r++) memmove(&c->cell[r][dx < 0 ? adx : 0], &c->cell[r][dx > 0 ? adx : 0], 2 * (size_t)(TC_W - adx));
  }
  c->cx = cx, c->cy = cy, c->valid = true;
  char win[WIN_H][WIN_W];
  for (int r = 0; r < WIN_H; r++) level_tile_row(layer, cx - 2, cy - 2 + r, WIN_W, win[r]);
  RoomCtx rc = {layer, NULL, win, cx - 2, cy - 2};
  /* padding looks only inside the cell's room for the fg (PaddingIgnoreOutOfLevel), anywhere for the bg */
  AutoCtx a = {room_get, &rc, layer == 0 ? room_in_level : NULL, false};
  for (int r = 0; r < TC_H; r++)
    for (int k = 0; k < TC_W; k++) {
      int x = cx + k, y = cy + r;
      if (keep && x >= ox && x < ox + TC_W && y >= oy && y < oy + TC_H) continue;
      char t = win[r + 2][k + 2];
      c->cell[r][k] = 31;
      if (t == '0') continue;
      rc.room = in_room(g_level.room, x, y) ? g_level.room : &g_level.rooms[g_level.room_slot ^ 1];
      if (rc.room->index < 0 || !in_room(rc.room, x, y)) rc.room = g_level.room;
      int ov;
      int q = autotile_at(layer, t, &a, x, y, &ov, 0);
      if (q < 0) continue;
      c->cell[r][k] = (uint16_t)((terrain_of(layer, t) - layer_base(layer)) | q << 5 | (ov & 3) << 14);
    }
  /* scenery tiles over the autotiled ones (fgtiles, bgtiles) */
  for (int s = 0; s < 2; s++) {
    Room *rm = &g_level.rooms[s];
    if (rm->index < 0) continue;
    const uint8_t *p = rm->scen[layer];
    for (int i = 0; i < rm->nscen[layer]; i++, p += 6) {
      int x = rm->x / 8 + rd16(p) - cx, y = rm->y / 8 + rd16(p + 2) - cy;
      if ((unsigned)x < TC_W && (unsigned)y < TC_H) c->cell[y][x] = (uint16_t)(TERR_SCENERY | rd16(p + 4) << 5);
    }
  }
}

/* one 8x8 tile of a 4-bit tile sheet; pal: the sheet's 16 colors, opaque: no
 * see-through colors besides index 0 */
static void blit_tile(uint16_t *strip, int sy0, int sy1, const Tex *t, const uint16_t *pal, const uint8_t *alpha, bool opaque,
                      int q, int x, int y) {
  int tx = (q & 7) * 8, ty = (q >> 3) * 8;
  int r0 = sy0 - y, r1 = sy1 - y;
  if (r0 < 0) r0 = 0;
  if (r1 > 8) r1 = 8;
  int stride = t->w >> 1;
  const uint8_t *src = t->px + (ty + r0) * stride + (tx >> 1);
  uint16_t *d = strip + (y + r0 - sy0) * VIEW_W + x;
  bool clip = x < 0 || x > VIEW_W - 8;
  for (int r = r0; r < r1; r++, src += stride, d += VIEW_W) {
    uint32_t v = src[0] | src[1] << 8 | src[2] << 16 | (uint32_t)src[3] << 24;
    if (!v) continue;
    if (!clip && opaque) {
      for (int i = 0; i < 8; i++, v >>= 4)
        if (v & 15) d[i] = pal[v & 15];
      continue;
    }
    for (int i = 0; i < 8; i++, v >>= 4) {
      int c = v & 15;
      if (!c || (unsigned)(x + i) >= VIEW_W) continue;
      if (alpha[c] == 255) d[i] = pal[c];
      else d[i] = blend565(d[i], pal[c], alpha[c] + 1);
    }
  }
}

void blit_part_ex(uint16_t *strip, int sy0, int sy1, const Tex *t, int x, int y, int sx, int sy, int w, int h, uint8_t flags,
                  uint16_t tint, uint8_t alpha);
static void scenery_tile(uint16_t *strip, int y0, int y1, const Tex *t, int sx, int sy, int x, int y) {
  blit_part_ex(strip, y0, y1, t, x, y, sx, sy, 8, 8, 0, 0xFFFF, 255);
}

static void layer_strip(uint16_t *strip, int y0, int y1, void *ctx) {
  int layer = (int)(intptr_t)ctx;
  TileCache *c = &tcache[layer];
  int ox = c->cx * 8 - g_camx, oy = c->cy * 8 - g_camy;
  Tex texs[4];
  uint16_t pals[4][16];
  uint8_t alphas[4][16];
  bool opaque[4];
  int tids[4] = {-1, -1, -1, -1};
  for (int r = 0; r < TC_H; r++) {
    int y = oy + r * 8;
    if (y + 8 <= y0 || y >= y1) continue;
    for (int k = 0; k < TC_W; k++) {
      uint16_t v = c->cell[r][k];
      uint8_t t = C_TERR(v);
      if (t == 31) continue;
      if (t == TERR_SCENERY) {
        Tex sc;
        if (tex_get(T_tilesets_scenery, &sc)) {
          /* Tileset: Texture.Width / 8 tiles a row, the frame's width (256: 32), not the trimmed one */
          int q = C_QUAD(v), cols = sc.fw >> 3;
          scenery_tile(strip, y0, y1, &sc, (q % cols) * 8, (q / cols) * 8, ox + k * 8, y);
        }
        continue;
      }
      int slot = t & 3;
      if (tids[slot] != t) {
        if (!tex_get(terrain_tex(t + layer_base(layer)), &texs[slot])) continue;
        tids[slot] = t;
        int n;
        const uint16_t *pc = pal_colors(texs[slot].pal, &n);
        const uint8_t *pa = pal_alpha(texs[slot].pal);
        opaque[slot] = true;
        for (int i = 0; i < 15; i++) {
          pals[slot][i + 1] = i < n ? pc[i] : 0;
          alphas[slot][i + 1] = i < n ? pa[i] : 255;
          if (i < n && pa[i] != 255) opaque[slot] = false;
        }
      }
      blit_tile(strip, y0, y1, &texs[slot], pals[slot], alphas[slot], opaque[slot], C_QUAD(v), ox + k * 8, y);
    }
  }
}

static float anim_time;

void tiles_render_layer(int layer, int depth) {
  (void)depth;
  int cx = (int)floorf(g_camx / 8.f), cy = (int)floorf(g_camy / 8.f);
  TileCache *c = &tcache[layer];
  if (!c->valid || c->cx != cx || c->cy != cy) fill_cache(layer, cx, cy);
  /* the tile sheets in view, loaded now (the strips are drawn from the cache, where nothing can load) */
  uint32_t terrains = 0;
  for (int r = 0; r < TC_H; r++)
    for (int k = 0; k < TC_W; k++) terrains |= 1u << C_TERR(c->cell[r][k]);
  Tex t;
  for (int k = 0; k < 30; k++)
    if (terrains >> k & 1) tex_get(terrain_tex(k + layer_base(layer)), &t);
  if (terrains >> TERR_SCENERY & 1) tex_get(T_tilesets_scenery, &t);
  gfx_custom(layer_strip, (void *)(intptr_t)layer, 0, VIEW_H);
  /* animated overlays (grass) */
  const uint8_t *at = section(SEC_ANIMTILES);
  int n = rd16(at);
  if (!n) return;
  for (int r = 0; r < TC_H; r++)
    for (int k = 0; k < TC_W; k++) {
      int ov = C_OV(c->cell[r][k]);
      if (!ov || C_TERR(c->cell[r][k]) == 31 || ov > n) continue;
      const uint8_t *a = at + 4;
      for (int i = 1; i < ov; i++) a += 10 + 2 * rd16(a + 8);
      float delay = rdf(a);
      int frames = rd16(a + 8);
      if (!frames) continue;
      uint32_t h = hash3(c->cx + k, c->cy + r, 99);
      int f = ((int)(anim_time / delay) + (int)(h % frames)) % frames;
      uint16_t tex = rd16(a + 10 + 2 * f);
      float x = (c->cx + k) * 8.f + (int8_t)a[4] + 4, y = (c->cy + r) * 8.f + (int8_t)a[5] + 4;
      gfx_tex_ex(tex, x, y, (float)(int8_t)a[6], (float)(int8_t)a[7], (h & 1) ? -1.f : 1.f, 1, 0, 0xFFFF, 255, 0);
    }
}

/* ---------------------------------------------------------------- boxes of tiles (blocks) */
typedef struct { char type; int w, h; } BoxCtx;
static char box_get(int x, int y, void *ctx) {
  BoxCtx *b = ctx;
  return x >= 0 && y >= 0 && x < b->w && y < b->h ? b->type : '0';
}
void tiles_box(char type, int w, int h, TileQ *out) {
  BoxCtx b = {type, w, h};
  AutoCtx a = {box_get, &b, NULL, false};
  for (int y = 0; y < h; y++)
    for (int x = 0; x < w; x++) {
      int ov;
      int q = autotile_at(0, type, &a, x, y, &ov, 3);
      out[y * w + x] = q < 0 ? 255 : (uint8_t)q;
    }
}

typedef struct { char type; int x, y, w, h; } OverCtx;
static char over_get(int x, int y, void *ctx) {
  OverCtx *o = ctx;
  if (x >= o->x && y >= o->y && x < o->x + o->w && y < o->y + o->h) return o->type;
  return level_tile_type(0, x, y);
}
static bool over_in(int x, int y, void *ctx) {
  (void)ctx;
  return in_room(g_level.room, x, y);
}
void tiles_overlay(char type, int x, int y, int w, int h, TileQ *out) {
  OverCtx o = {type, x, y, w, h};
  AutoCtx a = {over_get, &o, over_in, true};
  for (int j = 0; j < h; j++)
    for (int i = 0; i < w; i++) {
      int ov;
      int q = autotile_at(0, type, &a, x + i, y + j, &ov, 0);
      out[j * w + i] = q < 0 ? 255 : (uint8_t)q;
    }
}

void tiles_draw(const TileQ *q, char type, int w, int h, float x, float y, uint16_t tint, uint8_t alpha) {
  int t = terrain_of(0, type);
  if (t >= 0) gfx_tiles(terrain_tex(t), q, w, h, x, y, tint, alpha);
}

/* ---------------------------------------------------------------- decals */
static int decal_frames(uint16_t tex) {
  const uint8_t *s = section(SEC_DECALANIM);
  int n = rd16(s), lo = 0, hi = n - 1;
  while (lo <= hi) {
    int m = (lo + hi) / 2;
    uint16_t t = rd16(s + 2 + 4 * m);
    if (t == tex) return rd16(s + 4 + 4 * m);
    if (t < tex) lo = m + 1;
    else hi = m - 1;
  }
  return 1;
}

void decals_render(bool fg) {
  for (int s = 0; s < 2; s++) {
    Room *rm = &g_level.rooms[s];
    if (rm->index < 0) continue;
    int n = fg ? rm->nfg : rm->nbg;
    const uint8_t *tab = fg ? rm->decals_fg : rm->decals_bg;   /* u8 textures, (pad), u16 ids, x[n], y[n], v[n] */
    const uint8_t *ids = tab + 2, *xs = ids + 2 * tab[0], *ys = xs + 2 * n, *vs = ys + 2 * n;
    for (int i = 0; i < n; i++) {
      uint8_t v = vs[i];
      uint16_t tex = rd16(ids + 2 * (v & 63));
      float x = rm->x + (float)rds16(xs + 2 * i), y = rm->y + (float)rds16(ys + 2 * i);
      int frames = decal_frames(tex);
      if (frames > 1) tex = (uint16_t)(tex + (int)(anim_time * 12) % frames);
      /* cull by the frame's size (TEXDIM), before loading it */
      float r = (float)tex_half_extent(tex);
      if (x + r < g_camx || x - r > g_camx + 320 || y + r < g_camy || y - r > g_camy + 180) continue;
      Tex t;
      if (!tex_get(tex, &t)) continue;
      float hw = t.fw * t.scale / 2.f, hh = t.fh * t.scale / 2.f;
      gfx_tex_ex(tex, x, y, hw, hh, (v & 0x40) ? -1.f : 1.f, (v & 0x80) ? -1.f : 1.f, 0, 0xFFFF, 255, 0);
    }
  }
}

/* ---------------------------------------------------------------- the layers as entities */
static void bg_render(Ent *e) { (void)e; tiles_render_layer(1, D_BGTERRAIN); }
static void fg_render(Ent *e) {
  (void)e;
  tiles_render_layer(0, D_FGTERRAIN);
}
static void fgdecal_render(Ent *e) { (void)e; decals_render(true); }
static void bgdecal_render(Ent *e) { (void)e; decals_render(false); }
static void timer_update(Ent *e) { (void)e; anim_time += DT; }

static const EntClass BGTILES = {.size = 0, .name = "bgtiles", .render = bg_render, .update = timer_update};
static const EntClass SOLIDTILES = {.size = 0, .name = "solidtiles", .render = fg_render, .kind = KIND_SOLID};
static const EntClass FGDECALS = {.size = 0, .name = "fgdecals", .render = fgdecal_render};
static const EntClass BGDECALS = {.size = 0, .name = "bgdecals", .render = bgdecal_render};

void level_tiles_entities(void) {
  Ent *e = ent_new(&BGTILES, 0, 0);
  e->depth = D_BGTERRAIN, e->tags = TAG_GLOBAL | TAG_TRANSITION_UPDATE, e->dead = 0, e->collidable = 0;
  e = ent_new(&BGDECALS, 0, 0);
  e->depth = D_BGDECALS, e->tags = TAG_GLOBAL, e->dead = 0, e->collidable = 0;
  e = ent_new(&SOLIDTILES, 0, 0);
  e->depth = D_FGTERRAIN, e->tags = TAG_GLOBAL, e->dead = 0;
  e->ctype = COL_GRID;
  e->safe = 1;
  g_solidtiles = e;
  e = ent_new(&FGDECALS, 0, 0);
  e->depth = D_FGDECALS, e->tags = TAG_GLOBAL, e->dead = 0, e->collidable = 0;
}
