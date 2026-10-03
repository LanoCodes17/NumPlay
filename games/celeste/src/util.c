#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("Os")   /* not drawn every frame: smaller over faster */
#endif
/* Calc.Random (.NET's System.Random), Monocle's eases, the input buttons,
 * and sprite animation (Monocle.Sprite). */
#include "celeste.h"

/* ---------------------------------------------------------------- System.Random */
#define MBIG 2147483647
#define MSEED 161803398
NetRandom g_rnd;

void rnd_seed(NetRandom *r, int32_t seed) {
  int32_t sub = seed == INT32_MIN ? INT32_MAX : (seed < 0 ? -seed : seed);
  int32_t mj = MSEED - sub, mk = 1;
  r->seed[55] = mj;
  for (int i = 1; i < 55; i++) {
    int ii = (21 * i) % 55;
    r->seed[ii] = mk;
    mk = mj - mk;
    if (mk < 0) mk += MBIG;
    mj = r->seed[ii];
  }
  for (int k = 1; k < 5; k++)
    for (int i = 1; i < 56; i++) {
      r->seed[i] -= r->seed[1 + (i + 30) % 55];
      if (r->seed[i] < 0) r->seed[i] += MBIG;
    }
  r->inext = 0;
  r->inextp = 21;
}

int32_t rnd_next(NetRandom *r) {
  int a = r->inext, b = r->inextp;
  if (++a >= 56) a = 1;
  if (++b >= 56) b = 1;
  int32_t v = r->seed[a] - r->seed[b];
  if (v == MBIG) v--;
  if (v < 0) v += MBIG;
  r->seed[a] = v;
  r->inext = a, r->inextp = b;
  return v;
}

int32_t rnd_range(NetRandom *r, int32_t max) { return (int32_t)(rnd_next(r) * (1.0 / MBIG) * max); }
float rnd_float(NetRandom *r) { return (float)(rnd_next(r) * (1.0 / MBIG)); }

/* ---------------------------------------------------------------- Ease */
float ease_sine_in(float t) { return -cosf(PI_F / 2 * t) + 1; }
float ease_sine_out(float t) { return sinf(PI_F / 2 * t); }
float ease_sine_inout(float t) { return -cosf(PI_F * t) / 2 + .5f; }
float ease_quad_in(float t) { return t * t; }
float ease_quad_out(float t) { return 1 - (1 - t) * (1 - t); }
float ease_cube_in(float t) { return t * t * t; }
float ease_cube_out(float t) { float u = 1 - t; return 1 - u * u * u; }
float ease_cube_inout(float t) { return t <= .5f ? ease_cube_in(t * 2) / 2 : ease_cube_out(t * 2 - 1) / 2 + .5f; }
float ease_back_in(float t) { return t * t * (2.70158f * t - 1.70158f); }
float ease_back_out(float t) { return 1 - ease_back_in(1 - t); }
float ease_big_back_in(float t) { return t * t * (4 * t - 3); }
float ease_expo_out(float t) { return 1 - powf(2, 10 * ((1 - t) - 1)); }
static float elastic_in(float t) {
  float ts = t * t, tc = ts * t;
  return 33 * tc * ts + -59 * ts * ts + 32 * tc + -5 * ts;
}
float ease_elastic_out(float t) { return 1 - elastic_in(1 - t); }
float ease_bounce_out(float t) {
  const float b1 = 1 / 2.75f, b2 = 2 / 2.75f, b3 = 1.5f / 2.75f, b4 = 2.5f / 2.75f, b5 = 2.25f / 2.75f, b6 = 2.625f / 2.75f;
  if (t < b1) return 7.5625f * t * t;
  if (t < b2) { t -= b3; return 7.5625f * t * t + .75f; }
  if (t < b4) { t -= b5; return 7.5625f * t * t + .9375f; }
  t -= b6;
  return 7.5625f * t * t + .984375f;
}

/* ---------------------------------------------------------------- Input */
Input g_in;
static int last_x, last_y;   /* TakeNewer: the direction pressed last wins */

static void button(Button *b, bool down, bool was) {
  b->consumed = false;
  b->buffer -= DT;
  b->edge = down && !was;
  b->released = !down && was;
  b->check = down;
  if (b->edge) b->buffer = b->buffer_time;
  if (!down) b->buffer = 0;
}

void input_update(uint32_t keys) {
  uint32_t prev = g_in.keys;
  g_in.prev = prev;
  g_in.keys = keys;
  if (!g_in.jump.buffer_time) g_in.jump.buffer_time = g_in.dash.buffer_time = g_in.talk.buffer_time = 0.08f;
  button(&g_in.jump, keys & K_JUMP, prev & K_JUMP);
  button(&g_in.dash, keys & K_DASH, prev & K_DASH);
  button(&g_in.grab, keys & K_GRAB, prev & K_GRAB);
  button(&g_in.talk, keys & K_TALK, prev & K_TALK);
  button(&g_in.pause, keys & K_PAUSE, prev & K_PAUSE);
  button(&g_in.confirm, keys & K_OK, prev & K_OK);
  button(&g_in.back, keys & K_BACK, prev & K_BACK);
  bool l = keys & K_LEFT, r = keys & K_RIGHT, u = keys & K_UP, d = keys & K_DOWN;
  if (l && !(prev & K_LEFT)) last_x = -1;
  if (r && !(prev & K_RIGHT)) last_x = 1;
  if (u && !(prev & K_UP)) last_y = -1;
  if (d && !(prev & K_DOWN)) last_y = 1;
  g_in.move_x = l && r ? last_x : l ? -1 : r ? 1 : 0;
  g_in.move_y = u && d ? last_y : u ? -1 : d ? 1 : 0;
  g_in.aim_x = g_in.move_x, g_in.aim_y = g_in.move_y;
  g_in.feather_x = g_in.move_x, g_in.feather_y = g_in.move_y;
}

/* ---------------------------------------------------------------- Sprite */
static const uint8_t *bank_at(int b) {
  const uint8_t *s = section(SEC_BANKS);
  return s + rd32(s + 4 + 4 * b);
}
/* an animation: f32 delay, u8 loop, u8 ngoto, u16 nframes, (u16 anim, u16 weight) * ngoto, u16 frames */
static const uint8_t *anim_at(int b, int a) {
  const uint8_t *p = bank_at(b);
  if (a >= rd16(p + 2)) return NULL;
  const uint8_t *tab = p + 16;
  return tab + rd16(tab + 2 * a);
}
int bank_anim_count(int b) { return rd16(bank_at(b) + 2); }
/* the textures an animation shows and where it goes after: for loading them */
int bank_anim_textures(int b, int a, uint16_t *out, int max, uint16_t *gotos, int *ngotos) {
  const uint8_t *an = anim_at(b, a);
  if (!an) return 0;
  int n = rd16(an + 6), k = 0;
  for (int i = 0; i < n && k < max; i++) out[k++] = rd16(an + 8 + 4 * an[5] + 2 * i);
  *ngotos = an[5];
  for (int i = 0; i < an[5]; i++) gotos[i] = rd16(an + 8 + 4 * i);
  return k;
}

static uint16_t frame_tex(const uint8_t *an, int f) {
  return rd16(an + 8 + 4 * an[5] + 2 * f);
}

static void set_origin(Sprite *s) {
  if (!s->justify) return;
  Tex t;
  uint16_t tex = spr_tex(s);
  if (tex == 0xFFFF) return;
  /* the frame size is known without loading the pixels for direct textures; cached ones too */
  if (tex_get(tex, &t)) s->ox = t.fw * t.scale * s->jx, s->oy = t.fh * t.scale * s->jy;
}

void spr_init(Sprite *s, int bank) {
  memset(s, 0, sizeof *s);
  s->bank = (uint8_t)bank;
  s->anim = 255;
  s->rate = 1;
  s->sx = s->sy = 1;
  s->tint = 0xFFFF;
  s->alpha = 255;
  s->visible = 1;
  const uint8_t *p = bank_at(bank);
  if (p[0] == 1) s->ox = rdf(p + 4), s->oy = rdf(p + 8);
  else if (p[0] == 2) s->justify = 1, s->jx = rdf(p + 4), s->jy = rdf(p + 8);
  uint16_t start = rd16(p + 12);
  s->last_goto = -1;
  if (start != 0xFFFF) spr_play(s, start, false);
}

/* the current texture: kept in the frame field as a texture once an animation ends */
static uint16_t tex_of(const Sprite *s, int anim, int frame) {
  const uint8_t *an = anim_at(s->bank, anim);
  if (!an || !rd16(an + 6)) return 0xFFFF;
  if (frame >= rd16(an + 6)) frame = rd16(an + 6) - 1;
  return frame_tex(an, frame);
}

void spr_play(Sprite *s, int anim, bool restart) {
  if (s->anim == anim && !restart) return;
  const uint8_t *an = anim_at(s->bank, anim);
  if (!an) return;
  s->anim = (uint8_t)anim;
  s->last_goto = (int8_t)anim;
  s->playing = rdf(an) > 0;
  s->timer = 0;
  s->frame = 0;
  set_origin(s);
}

void spr_play_offset(Sprite *s, int anim, int frame) {
  spr_play(s, anim, true);
  const uint8_t *an = anim_at(s->bank, anim);
  if (an && frame < rd16(an + 6)) s->frame = (uint8_t)frame;
}

bool spr_is(const Sprite *s, int anim) { return s->anim == anim; }

int spr_frames(const Sprite *s) {
  const uint8_t *an = anim_at(s->bank, s->anim == 255 ? (s->last_goto < 0 ? 0 : s->last_goto) : s->anim);
  return an ? rd16(an + 6) : 0;
}

uint16_t spr_tex(const Sprite *s) {
  int a = s->anim != 255 ? s->anim : s->last_goto;
  if (a < 0) return 0xFFFF;
  return tex_of(s, a, s->frame);
}

float spr_anim_duration(int bank, int anim) {
  const uint8_t *an = anim_at(bank, anim);
  return an ? rdf(an) * rd16(an + 6) : 0;
}

int uitoa(int v, char *out) {
  char t[12];
  int n = 0;
  if (v < 0) v = 0;
  do t[n++] = (char)('0' + v % 10), v /= 10;
  while (v);
  for (int i = 0; i < n; i++) out[i] = t[n - 1 - i];
  out[n] = 0;
  return n;
}
void time_text(uint32_t frames, char *out) {   /* TimeSpan: [h:]mm:ss.fff, as the stats show it */
  uint32_t ms = frames * 1000u / 60u, s = ms / 1000, m = s / 60, h = m / 60;
  char *p = out;
  if (h) p += uitoa((int)h, p), *p++ = ':';
  if (h || m >= 10) *p++ = (char)('0' + m % 60 / 10);
  *p++ = (char)('0' + m % 10);
  *p++ = ':', *p++ = (char)('0' + s % 60 / 10), *p++ = (char)('0' + s % 10), *p++ = '.';
  *p++ = (char)('0' + ms % 1000 / 100), *p++ = (char)('0' + ms % 100 / 10), *p++ = (char)('0' + ms % 10);
  *p = 0;
}

void spr_update(Sprite *s) {
  if (!s->playing || s->anim == 255) return;
  const uint8_t *an = anim_at(s->bank, s->anim);
  float delay = rdf(an);
  int n = rd16(an + 6);
  s->timer += (s->raw ? RAW_DT : DT) * s->rate;
  if (fabsf(s->timer) < delay) return;
  int dir = s->timer > 0 ? 1 : -1;
  int f = s->frame + dir;
  s->timer -= dir * delay;
  if (f >= 0 && f < n) {
    s->frame = (uint8_t)f;
    set_origin(s);
    return;
  }
  int ngoto = an[5];
  if (!ngoto && an[4]) {   /* a Loop: back to its first frame */
    s->frame = (uint8_t)(f < 0 ? n - 1 : 0);
    set_origin(s);
    return;
  }
  if (ngoto) {
    int pick = 0;
    if (ngoto > 1) {
      int total = 0;
      for (int i = 0; i < ngoto; i++) total += rd16(an + 8 + 4 * i + 2);
      float r = rndf() * total;
      for (int i = 0; i < ngoto; i++) {
        r -= rd16(an + 8 + 4 * i + 2);
        if (r < 0) { pick = i; break; }
      }
    }
    uint16_t next = rd16(an + 8 + 4 * pick);
    if (next == 0xFFFF) next = s->anim;
    s->anim = (uint8_t)next;
    s->last_goto = (int8_t)next;
    const uint8_t *an2 = anim_at(s->bank, next);
    s->frame = (uint8_t)(f < 0 ? rd16(an2 + 6) - 1 : 0);
    s->playing = rdf(an2) > 0;
    set_origin(s);
  } else {
    s->frame = (uint8_t)(f < 0 ? 0 : n - 1);
    s->playing = 0;
    s->last_goto = (int8_t)s->anim;
    s->anim = 255;   /* CurrentAnimationID = "": the last frame stays */
  }
}

void spr_draw(const Sprite *s, float x, float y) {
  if (!s->visible) return;
  uint16_t t = spr_tex(s);
  if (t == 0xFFFF) return;
  if (!res_cached(t)) {   /* an animation not loaded yet: all its frames at once */
    uint16_t ids[32], gotos[8];
    int ng, a = s->anim != 255 ? s->anim : s->last_goto;
    int n = bank_anim_textures(s->bank, a, ids, 32, gotos, &ng);
    res_load_frames(ids, n, t);
  }
  uint8_t f = (s->flipx ? GF_FLIPX : 0) | (s->flipy ? GF_FLIPY : 0);
  float ox = s->ox, oy = s->oy;
  Tex tx;
  if (s->justify && tex_get(t, &tx)) ox = tx.fw * tx.scale * s->jx, oy = tx.fh * tx.scale * s->jy;   /* Justify */
  gfx_tex_ex(t, x, y, ox, oy, s->sx, s->sy, s->rot, s->tint, s->alpha, f);
}

char *path2(char *buf, const char *a, const char *b, int n) {
  char *o = buf;
  while (*a) *o++ = *a++;
  while (b && *b) *o++ = *b++;
  if (n >= 0) {
    if (n >= 100) *o++ = (char)('0' + n / 100);
    *o++ = (char)('0' + (n / 10) % 10);
    *o++ = (char)('0' + n % 10);
  }
  *o = 0;
  return buf;
}

/* ---------------------------------------------------------------- sinf, cosf, powf */
/* to a quarter turn's eighth: x = k pi/2 + r, |r| <= pi/4 (pi/2 in two parts for the products' rounding) */
static float quarter(float x, int *k) {
  float n = rintf(x * 0.636619772f);
  *k = (int)n;
  return (x - n * 1.57079637f) - n * -4.37113883e-08f;
}
static float sin_r(float r) {
  float r2 = r * r;
  return r + r * r2 * (-0.166666667f + r2 * (8.33333333e-3f + r2 * -1.98412698e-4f));
}
static float cos_r(float r) {
  float r2 = r * r;
  return 1 + r2 * (-0.5f + r2 * (4.16666667e-2f + r2 * (-1.38888889e-3f + r2 * 2.48015873e-5f)));
}
void c_sincosf(float x, float *s, float *c) {
  int k;
  float r = quarter(x, &k), sr = sin_r(r), cr = cos_r(r);
  switch (k & 3) {
    case 0: *s = sr, *c = cr; break;
    case 1: *s = cr, *c = -sr; break;
    case 2: *s = -sr, *c = -cr; break;
    default: *s = -cr, *c = sr; break;
  }
}
float c_sinf(float x) {
  int k;
  float r = quarter(x, &k), v = (k & 1) ? cos_r(r) : sin_r(r);
  return (k & 2) ? -v : v;
}
float c_cosf(float x) {
  int k;
  float r = quarter(x, &k), v = (k & 1) ? sin_r(r) : cos_r(r);
  return ((k + 1) & 2) ? -v : v;
}
/* a^b = 2^(b log2 a), for a >= 0 */
float c_powf(float a, float b) {
  if (a <= 0) return b > 0 ? 0 : 1;
  if (b == 0 || a == 1) return 1;
  union { float f; uint32_t u; } v = {a};
  int e = (int)(v.u >> 23) - 127;
  v.u = (v.u & 0x007FFFFF) | 0x3F800000;   /* the mantissa, in [1, 2) */
  float m = v.f;
  if (m > 1.41421356f) m *= 0.5f, e++;     /* in [sqrt(1/2), sqrt(2)) */
  float t = (m - 1) / (m + 1), t2 = t * t;
  float l2 = e + 2.88539008f * t * (1 + t2 * (0.333333333f + t2 * (0.2f + t2 * 0.142857143f)));   /* log2 */
  float y = b * l2;
  if (y > 127) return INFINITY;
  if (y < -126) return 0;
  float n = rintf(y), f = (y - n) * 0.693147181f;
  float p = 1 + f * (1 + f * (0.5f + f * (0.166666667f + f * (4.16666667e-2f + f * (8.33333333e-3f + f * 1.38888889e-3f)))));
  v.u = (uint32_t)((int)n + 127) << 23;   /* 2^n */
  return p * v.f;
}
