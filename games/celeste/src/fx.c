/* Particles (Monocle.ParticleSystem), afterimages (TrailManager), Dust. */
#include "fx.h"
#include "level.h"

typedef struct {
  const PType *type;
  uint8_t active, layer;
  uint16_t src;
  V2 pos, speed;
  float life, start_life, size, rot, spin;
  uint32_t color, start_color;
} Particle;

#define MAX_PARTICLES 128
static Particle parts[MAX_PARTICLES];
static int next_part;

void particles_clear(void) { memset(parts, 0, sizeof parts); }

static uint32_t lerp_rgb(uint32_t a, uint32_t b, float t) {
  int r = (int)(((a >> 16) & 255) + (((int)((b >> 16) & 255) - (int)((a >> 16) & 255)) * t));
  int g = (int)(((a >> 8) & 255) + (((int)((b >> 8) & 255) - (int)((a >> 8) & 255)) * t));
  int bl = (int)((a & 255) + (((int)(b & 255) - (int)(a & 255)) * t));
  return (uint32_t)(r << 16 | g << 8 | bl);
}

static void create(int layer, const PType *t, V2 pos, uint32_t color, float dir) {
  Particle *p = &parts[next_part];
  next_part = (next_part + 1) % MAX_PARTICLES;
  p->type = t;
  p->active = 1;
  p->layer = (uint8_t)layer;
  p->pos = pos;
  p->src = t->nsrc ? t->src[t->nsrc > 1 ? rndi(t->nsrc) : 0] : 0xFFFF;
  p->size = t->size_range != 0 ? t->size - t->size_range * .5f + rndf() * t->size_range : t->size;
  p->start_color = t->color_mode == PC_CHOOSE ? (rndi(2) ? t->color2 : color) : color;
  p->color = p->start_color;
  float a = dir - t->dir_range / 2 + rndf() * t->dir_range;
  float sp = rnd_rangef(t->speed_min, t->speed_max);
  p->speed = v2(cosf(a) * sp, sinf(a) * sp);
  p->start_life = p->life = rnd_rangef(t->life_min, t->life_max);
  p->rot = t->rot_mode == PR_RANDOM ? rndf() * 2 * PI_F : t->rot_mode == PR_SAMEASDIR ? a : 0;
  p->spin = rnd_rangef(t->spin_min, t->spin_max);
  if (t->spin_flip && rndi(2)) p->spin = -p->spin;
}

void particles_emit_c(int layer, const PType *t, int n, V2 pos, V2 range, uint32_t color, float dir) {
  for (int i = 0; i < n; i++)
    create(layer, t, v2(pos.x + rnd_rangef(-range.x, range.x), pos.y + rnd_rangef(-range.y, range.y)), color, dir);
}
void particles_emit(int layer, const PType *t, int n, V2 pos, V2 range, float dir) {
  particles_emit_c(layer, t, n, pos, range, t->color, dir);
}
void particles_emit1(int layer, const PType *t, V2 pos, float dir) { create(layer, t, pos, t->color, dir); }

/* Monocle's Particle.Update; the color keeps its alpha in the top byte, times the fade */
void particles_update(void) {
  for (int i = 0; i < MAX_PARTICLES; i++) {
    Particle *p = &parts[i];
    if (!p->active) continue;
    const PType *t = p->type;
    float dt = DT;
    float k = p->life / p->start_life;
    p->life -= dt;
    if (p->life <= 0) {
      p->active = 0;
      continue;
    }
    if (t->rot_mode == PR_SAMEASDIR) {
      if (p->speed.x || p->speed.y) p->rot = atan2f(p->speed.y, p->speed.x);
    } else
      p->rot += p->spin * dt;
    float fade = t->fade_mode == PF_LINEAR ? k : t->fade_mode == PF_LATE ? fminf(1, k / .25f)
               : t->fade_mode == PF_INOUT ? (k > .75f ? 1 - (k - .75f) / .25f : k < .25f ? k / .25f : 1) : 1;
    uint32_t c;
    if (t->color_mode == PC_FADE) c = lerp_rgb(t->color2, p->start_color, k) | (p->start_color & 0xFF000000u);
    else if (t->color_mode == PC_BLINK) c = fmodf(p->life, .2f) < .1f ? p->start_color : t->color2;   /* Calc.BetweenInterval(Life, 0.1) */
    else c = p->start_color;
    uint32_t a = (uint32_t)((c >> 24) * fade);
    p->color = (c & 0xFFFFFF) | a << 24;
    p->pos = v2add(p->pos, v2mul(p->speed, dt));
    p->speed = v2add(p->speed, v2mul(t->accel, dt));
    p->speed.x = approach(p->speed.x, 0, t->friction * dt);   /* Calc.Approach(Vector2) */
    p->speed.y = approach(p->speed.y, 0, t->friction * dt);
    if (t->speed_mult != 1) p->speed = v2mul(p->speed, powf(t->speed_mult, dt));
    if (t->scale_out) p->size = t->size * ease_cube_out(k);
  }
}

/* ParticleSystem.Render: a layer's particles drawn into the strips by one command (as pictures they were up to 128
 * commands, most of them scaled or turned: the frame has 48 of those) */
static void particles_strip(uint16_t *strip, int sy0, int sy1, void *ctx) {
  int layer = (int)(intptr_t)ctx;
  for (int i = 0; i < MAX_PARTICLES; i++) {
    Particle *p = &parts[i];
    uint8_t a = (uint8_t)(p->color >> 24);
    if (!p->active || p->layer != layer || !a) continue;
    float x = (float)(int)p->pos.x, y = (float)(int)p->pos.y;   /* (Particle.Render: the position truncated) */
    if (p->src == 0xFFFF) {
      int s = (int)(p->size + 0.5f);
      if (s >= 1) blit_rect(strip, sy0, sy1, x - s / 2, y - s / 2, (float)s, (float)s, rgb(p->color & 0xFFFFFF), a);
    } else {
      Tex t;
      if (tex_get(p->src, &t))
        blit_tex_ex(strip, sy0, sy1, p->src, x, y, t.fw * t.scale / 2.f, t.fh * t.scale / 2.f, p->size, p->size, p->rot,
                    rgb(p->color & 0xFFFFFF), a, 0);
    }
  }
}
void particles_render(int layer) {
  float y0 = 1e9f, y1 = -1e9f;
  for (int i = 0; i < MAX_PARTICLES; i++) {
    Particle *p = &parts[i];
    if (!p->active || p->layer != layer || !(p->color >> 24)) continue;
    Tex t;
    if (p->src != 0xFFFF) tex_get(p->src, &t);   /* (loaded now: nothing loads while the strips are drawn) */
    y0 = fminf(y0, p->pos.y), y1 = fmaxf(y1, p->pos.y);
  }
  if (y0 <= y1) gfx_custom(particles_strip, (void *)(intptr_t)layer, (int)floorf(y0) - 24 - g_camy, (int)ceilf(y1) + 25 - g_camy);
}

/* Celeste's Dust.Burst and BurstFG */
void dust_burst(V2 pos, float dir, int n) {
  V2 v = v2(fabsf(cosf(dir - PI_F / 2) * 4), fabsf(sinf(dir - PI_F / 2) * 4));
  particles_emit(PL_MID, &P_Dust, n, pos, v, dir);
}
void dust_burst_fg(V2 pos, float dir, int n, float range) {
  V2 v = v2(fabsf(cosf(dir - PI_F / 2) * range), fabsf(sinf(dir - PI_F / 2) * range));
  particles_emit(PL_FG, &P_Dust, n, pos, v, dir);
}

/* ---------------------------------------------------------------- trails */
#pragma GCC push_options
#pragma GCC optimize("Os")   /* a few a frame: smaller over faster */
/* TrailManager.Snapshot: what was drawn into its 64x64 cell (the sprite's frame, and PlayerHair's nodes with
 * their outline) is kept as what to draw again, not as pixels; drawn as one silhouette in Color * 0.75 *
 * (1 - CubeOut(Percent)), at Depth (the entity's + 1), oldest first */
static Trail trails[MAX_TRAILS];
static int ntrails;

Trail *trail_add(float x, float y, uint16_t tex, float ox, float oy, float sx, float sy, uint32_t color, float dur, int depth) {
  if (tex == 0xFFFF) return NULL;
  if (ntrails >= MAX_TRAILS) {   /* (not the game's 64: the oldest goes, another's before Madeline's) */
    int k = 0;
    while (k < ntrails - 1 && trails[k].nhair) k++;
    if (trails[k].nhair) k = 0;
    memmove(trails + k, trails + k + 1, (size_t)(--ntrails - k) * sizeof *trails);
  }
  Trail *t = &trails[ntrails++];
  memset(t, 0, sizeof *t);
  t->x = (int16_t)floorf(x + 0.5f), t->y = (int16_t)floorf(y + 0.5f);
  t->ox = ox, t->oy = oy, t->sx = sx, t->sy = sy, t->dur = dur;
  t->tex = tex, t->color = rgb(color & 0xFFFFFF), t->depth = (int16_t)depth;
  t->bangs = color >> 24 ? (uint8_t)(color >> 24) : 255;   /* (no hair: the color's own alpha, premultiplied) */
  t->born = (uint8_t)g_frame;
  return t;
}
void trail_update(void) {   /* Snapshot.Update (Tags.Global: not while between rooms, frozen or paused) */
  if (g_level.transitioning) return;
  for (int i = 0; i < ntrails; i++) {
    Trail *t = &trails[i];
    if (t->born == (uint8_t)g_frame && t->pct == 0) continue;   /* added this frame: in the scene from the next */
    if ((t->pct += DT / t->dur) >= 1) {
      memmove(t, t + 1, (size_t)(ntrails - i - 1) * sizeof *t);
      ntrails--, i--;
    }
  }
}

/* what of view row Y a texture drawn at (PX, PY) (origin o, scale s, as gfx_tex_ex) covers, as bits from
 * column cx on */
static uint64_t trail_tex_row(const Tex *t, int PX, int PY, float ox, float oy, float sx, float sy, int Y, int cx) {
  int k = t->scale > 1 ? t->scale : 1;
  ox /= k, oy /= k, sx *= k, sy *= k;
  int v = (int)floorf((Y + 0.5f - PY) / sy + oy) - t->oy;
  if ((unsigned)v >= (unsigned)t->h || t->w > 64) return 0;
  uint8_t px[64];
  tex_row(t, v, px);
  float isx = 1 / sx, xa = PX + (t->ox - ox) * sx, xb = PX + (t->ox + t->w - ox) * sx;
  int i0 = (int)floorf(fminf(xa, xb)) - cx, i1 = (int)ceilf(fmaxf(xa, xb)) - cx;
  uint64_t m = 0;
  for (int i = i0 < 0 ? 0 : i0; i < i1 && i < 64; i++) {
    int u = (int)floorf((cx + i + 0.5f - PX) * isx + ox) - t->ox;
    if ((unsigned)u < (unsigned)t->w && px[u]) m |= 1ull << i;
  }
  return m;
}
static const uint16_t TR_HAIR[4] = {T_characters_player_bangs00, T_characters_player_bangs01, T_characters_player_bangs02, T_characters_player_hair00};
/* PlayerHair.Render's node i (the bangs, else hair00) on row Y */
static uint64_t trail_node_row(const Trail *t, const Tex *hair, int i, int Y, int cx) {
  int PY = t->y - g_camy + t->hair[i][1];
  if (Y < PY - 6 || Y > PY + 6) return 0;   /* (10x10 around the node, scaled by 1 at most) */
  float as = fabsf(t->sx), num = 0.25f + (1 - (float)i / t->nhair) * 0.75f;
  return trail_tex_row(&hair[i > 0], t->x - g_camx + t->hair[i][0], PY, 5, 5, i ? num * as : t->sx < 0 ? -as : as, num, Y, cx);
}
static void trail_strip(uint16_t *strip, int sy0, int sy1, void *ctx) {
  uint32_t set = (uint32_t)(uintptr_t)ctx;
  for (int i = 0; i < ntrails; i++) {
    if (!(set >> i & 1)) continue;
    Trail *t = &trails[i];
    Tex spr, hair[2];
    if (!tex_get(t->tex, &spr) || (t->nhair && (!tex_get(TR_HAIR[t->bangs], &hair[0]) || !tex_get(TR_HAIR[3], &hair[1])))) continue;
    float e = 1 - t->pct;
    int a8 = (int)(0.75f * e * e * e * 255), a = a8 + (a8 >> 7);
    if (a <= 0) continue;
    uint16_t pc = scale565(t->color, a);
    int ca = t->nhair ? 256 : t->bangs + (t->bangs >> 7);
    a = a * ca >> 8;
    int cx = t->x - g_camx - 32, cy = t->y - g_camy - 32;   /* the cell */
    int y0 = cy > sy0 ? cy : sy0, y1 = cy + 64 < sy1 ? cy + 64 : sy1;
    for (int Y = y0; Y < y1; Y++) {
      uint64_t m = trail_tex_row(&spr, t->x - g_camx, t->y + t->sdy - g_camy, t->ox, t->oy, t->sx, t->sy, Y, cx);
      for (int n = 0; n < t->nhair; n++) {   /* the node, and its outline (drawn at the 4 neighbours) */
        uint64_t b = trail_node_row(t, hair, n, Y, cx);
        m |= b | b << 1 | b >> 1 | trail_node_row(t, hair, n, Y - 1, cx) | trail_node_row(t, hair, n, Y + 1, cx);
      }
      uint16_t *row = strip + (Y - sy0) * VIEW_W;
      while (m) {
        int X = cx + __builtin_ctzll(m);
        m &= m - 1;
        if ((unsigned)X < VIEW_W) row[X] = blend565(row[X], pc, a);
      }
    }
  }
}
/* the snapshots of depth in (lo, hi] (drawn among the entities: ents_render_between), in one command */
void trail_render_between(int hi, int lo) {
  uint32_t set = 0;
  int y0 = 1 << 30, y1 = -(1 << 30);
  Tex t;
  for (int i = 0; i < ntrails; i++) {
    Trail *tr = &trails[i];
    /* (from the frame after it was added, as Scene.Add: between rooms too, at Percent 0) */
    if (tr->depth > hi || tr->depth <= lo || (tr->pct <= 0 && tr->born == (uint8_t)(g_frame - 1))) continue;
    if (!tex_get(tr->tex, &t) || (tr->nhair && (!tex_get(TR_HAIR[tr->bangs], &t) || !tex_get(TR_HAIR[3], &t)))) continue;   /* (loaded now) */
    set |= 1u << i;
    int cy = tr->y - g_camy - 32;
    if (cy < y0) y0 = cy;
    if (cy + 64 > y1) y1 = cy + 64;
  }
  if (set) gfx_custom(trail_strip, (void *)(uintptr_t)set, y0, y1);
}
void trail_clear(void) { ntrails = 0; }
#pragma GCC pop_options
void fx_update(void) {
  particles_update();
  trail_update();
}
