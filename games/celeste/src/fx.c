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

void particles_render(int layer) {
  for (int i = 0; i < MAX_PARTICLES; i++) {
    Particle *p = &parts[i];
    uint8_t a = (uint8_t)(p->color >> 24);
    if (!p->active || p->layer != layer || !a) continue;
    float x = (float)(int)p->pos.x, y = (float)(int)p->pos.y;
    if (p->src == 0xFFFF) {
      int s = (int)(p->size + 0.5f);
      if (s < 1) continue;
      gfx_rect(x - s / 2, y - s / 2, (float)s, (float)s, rgb(p->color & 0xFFFFFF), a);
    } else {
      Tex t;
      if (!tex_get(p->src, &t)) continue;
      gfx_tex_ex(p->src, x, y, t.fw / 2.f, t.fh / 2.f, p->size, p->size, p->rot, rgb(p->color & 0xFFFFFF), a, 0);
    }
  }
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
typedef struct {
  float x, y, ox, oy, sx, sy, t, dur;
  uint16_t tex;
  uint32_t color;
  uint8_t on;
} Trail;
static Trail trails[12];

void trail_add(float x, float y, uint16_t tex, float ox, float oy, float sx, float sy, uint32_t color, float dur) {
  for (int i = 0; i < 12; i++)
    if (!trails[i].on) {
      trails[i] = (Trail){x, y, ox, oy, sx, sy, 0, dur, tex, color, 1};
      return;
    }
}
void trail_update(void) {
  for (int i = 0; i < 12; i++)
    if (trails[i].on && (trails[i].t += DT) >= trails[i].dur) trails[i].on = 0;
}
void trail_render(void) {
  for (int i = 0; i < 12; i++) {
    Trail *t = &trails[i];
    if (!t->on) continue;
    float a = 1 - t->t / t->dur;
    gfx_tex_ex(t->tex, t->x, t->y, t->ox, t->oy, t->sx, t->sy, 0, rgb(t->color), (uint8_t)(a * 255 * 0.75f), GF_SILHOUETTE);
  }
}
void trail_clear(void) { memset(trails, 0, sizeof trails); }
void fx_update(void) {
  particles_update();
  trail_update();
}
