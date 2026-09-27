/* The fire behind chips and mult when a hand beats the blind: Balatro's
 * flame shader (resources/shaders/flame.fs) worked out on the CPU per
 * pixel cells, with the game's intensity (log5 of the score, minus 2) and
 * its springy easing (G.FUNCS.flame_handler). */
#ifndef PROF_NOINLINE
#define PROF_NOINLINE
#endif
#include "flame.h"
#include <stdint.h>
#include <string.h>


typedef struct {
  float intensity, real, vel, timer;
} flame_t;
static flame_t fl[2];

static inline float fabsf_(float x) { return x < 0 ? -x : x; }
#define fabsf fabsf_
#define sqrtf __builtin_sqrtf

/* log base 5 of v >= 1 (to about 1e-3), from the bits of the double */
static float log5(double v) {
  uint64_t b;
  memcpy(&b, &v, 8);
  int e = (int)((b >> 52) & 0x7FF) - 1023;
  uint32_t mb = 0x3F800000u | (uint32_t)((b >> 29) & 0x7FFFFF);
  float m;
  memcpy(&m, &mb, 4); /* 1 <= m < 2 */
  float z = (m - 1) / (m + 1), z2 = z * z;
  float ln = 2.f * z * (1.f + z2 * (1.f / 3 + z2 * (1.f / 5 + z2 * (1.f / 7))));
  return (e * 0.69314718f + ln) * 0.62133493f; /* ln(v) / ln 5 */
}

/* sin/cos good to about 1e-3, fast enough for a few thousand cells a frame */
static __attribute__((noinline)) float fcos(float x) {
  const float tp = 1.f / (2.f * 3.14159265f);
  x *= tp;
  float y = x + .25f;
  int k = (int)y;
  if (y < (float)k) k--;
  x -= .25f + (float)k;
  x *= 16.f * (fabsf_(x) - .5f);
  x += .225f * x * (fabsf_(x) - 1.f);
  return x;
}
static inline __attribute__((always_inline)) float fsin(float x) { return fcos(x - 1.5707963f); }

void flame_update(int dt_ms, double earned, double required, int allowed, int calm) {
  float target = 0;
  if (allowed && required > 0 && earned >= required && earned > 1) {
    target = log5(earned) - 2.f;
    if (target < 0) target = 0;
  }
  /* the game's easing, in 60 Hz steps */
  for (int k = 0; k < 2; k++) {
    flame_t *f = &fl[k];
    f->intensity = target;
    int left = dt_ms;
    while (left > 0) {
      int step = left > 17 ? 17 : left;
      left -= step;
      float dt = step / 1000.f;
      float ex = 1.f - 0.4f * dt; /* exp(-0.4 dt) */
      f->timer += dt * (1 + f->intensity * 0.2f);
      if (f->vel < 0) f->vel *= 1 - 10 * dt;
      f->vel = (1 - ex) * (f->intensity - f->real) * dt * 25 + ex * f->vel;
      f->real += f->vel;
      if (calm) f->real -= f->real * 8 * dt + 0.02f; /* the round is over: die down within a second */
      if (f->real < 0) f->real = 0;
    }
    if (f->timer > 2000) f->timer -= 2000;
  }
}

int flame_on(void) { return fl[0].real >= 0.1f || fl[1].real >= 0.1f; }

static C rgb(float r, float g, float b) {
  if (r < 0) r = 0;
  if (g < 0) g = 0;
  if (b < 0) b = 0;
  if (r > 1) r = 1;
  if (g > 1) g = 1;
  if (b > 1) b = 1;
  return (C)(((int)(r * 31.f + .5f) << 11) | ((int)(g * 63.f + .5f) << 5) | (int)(b * 31.f + .5f));
}

/* colour_1: the box colour; colour_2: ((colour + yellow) / 2 + 0.1)^2 */
static const float col1[2][3] = {{0.f, 0.616f, 1.f}, {0.996f, 0.373f, 0.333f}};
static const float col2[2][3] = {{0.36f, 0.824f, 0.36f}, {1.f, 0.618f, 0.1f}};

/* the shader's "smoke" at a point of the sprite (uv in -0.5..0.5); the flame
   shows where it is under 1 */
typedef struct {
  float I, up, scale, sp1, sp2, sp3, sp4, top_cut, damp, w1, w2;
  const float *lt; /* the shader's length term by length, from l0, 8 steps a unit */
  float l0;
} fparams_t;
#define LT_N 193 /* +-12 around the frame's |up|: the lengths stay within 10 of it */

/* 0.3 (cos(0.411 len) + 0.3344 sin(len) - 0.23 cos(len)) */
static float len_term(float len) { return 0.3f * (fcos(len * 0.411f) + 0.3344f * fsin(len) - 0.23f * fcos(len)); }

static float smoke_at(const fparams_t *q, float fx, float fy) {
  if (q->top_cut * 2.f * (fy - 0.5f) * (fy - 0.5f) > 1.02f) return 9.f; /* cut off at the top */
  float wob = 0.01f * fsin(-1.123f * fx + q->w1) * fcos(5.3332f * fy + q->w2);
  float ucx = fx + fx * wob, ucy = fy + fy * wob;
  float svx = ucx * q->scale, svy = ucy * q->scale + q->up;
  float s2x = 0, s2y = 0;
  for (int i = 0; i < 5; i++) {
    float len = sqrtf(svx * svx + svy * svy), p = (len - q->l0) * 8.f, add;
    int n = (int)p;
    if (p >= 0 && n < LT_N - 1) add = q->lt[n] + (q->lt[n + 1] - q->lt[n]) * (p - n);
    else add = len_term(len);
    float nx = s2x + svx + 0.05f * s2y + add, ny = s2y + svy + 0.05f * s2x + add;
    s2x = nx, s2y = ny;
    svx += 0.5f * fcos(fcos(s2y) + q->sp1) * fsin(3.22f + s2x - q->sp2);
    svy += 0.5f * fsin(-s2x * 1.21222f + q->sp3) * fcos(s2y * 0.91213f - q->sp4);
  }
  float dx = svx / q->scale * 5.f, dy = (svy - q->up) / q->scale * 5.f;
  float sm = (sqrtf(dx * dx + dy * dy) + 0.1f * (sqrtf(ucx * ucx + ucy * ucy) - 0.5f)) * q->damp;
  if (sm < 0) sm = 0;
  float e = ucy - 0.5f;
  sm += q->top_cut * 2.f * e * e;
  if (fabsf(fx) > 0.4f) sm += 10.f * (fabsf(fx) - 0.4f);
  return sm;
}

/* The smoke over the whole flame, once a frame, on a grid every 2 pixels
   (g, FLAME_GRID values, 1/2048 units, capped): first every 4 pixels, then
   the points in between only where the flame's edge can pass (elsewhere
   they are blended from their neighbours). */
#define FG 2 /* grid step, pixels */
#define FCAP 65535
static void setup(fparams_t *q, int k) {
  const flame_t *f = &fl[k];
  q->I = f->real > 10 ? 10 : f->real;
  const float id = k ? 91.f : 37.f;
  float t = f->timer;
  /* mod(4 t, 10000) - 5000 + mod(1.781 id, 1000): the timer stays under 2000 */
  q->up = 4.f * t - 5000.f + 1.781f * id;
  q->scale = 7.5f + 3.f / (2.f + 2.f * q->I);
  float speed = (k ? 91.071f : 68.897f) + fsin(t + id) * fcos(t * 0.151f + id); /* mod(20.781 id, 100) */
  q->sp1 = speed * 0.0812f, q->sp2 = speed * 0.1531f, q->sp3 = 0.113785f * speed, q->sp4 = 0.13582f * speed;
  q->top_cut = 2.f - 0.3f * q->I;
  if (q->top_cut < 0) q->top_cut = 0;
  q->damp = 2.f / (2.f + q->I * 0.2f);
  q->w1 = 0.2f * t, q->w2 = 0.931f * t;
}

static inline uint16_t q16(float v) { return v >= 31.f ? FCAP : (uint16_t)(v * 2048.f); }

PROF_NOINLINE void flame_grid(int k, int w, int h, uint16_t *g) {
  if (fl[k].real < 0.1f) return;
  fparams_t q;
  setup(&q, k);
  float lt[LT_N];
  q.l0 = fabsf(q.up) - 12.f;
  if (q.l0 < 0) q.l0 = 0;
  for (int n = 0; n < LT_N; n++) lt[n] = len_term(q.l0 + n * 0.125f);
  q.lt = lt;
  int gc = w / FG + 1, gr = (int)(0.62f * h) / FG + 2;
  if (gc > FLAME_GC) gc = FLAME_GC;
  if (gr > FLAME_GR) gr = FLAME_GR;
  float ix = (float)FG / w, iy = (float)FG / h;
  /* every other point */
  for (int j = 0; j < gr; j += 2)
    for (int i = 0; i < gc; i += 2) g[j * FLAME_GC + i] = q16(smoke_at(&q, i * ix - 0.5f, j * iy - 0.5f));
  /* the others: worked out near the edge (the corners around it between
     0.75 and 1.35), blended elsewhere */
  for (int j = 0; j < gr; j++)
    for (int i = (j & 1) ? 0 : 1; i < gc; i += (j & 1) ? 1 : 2) {
      int i0 = i & ~1, i1 = i0 + 2 < gc ? i0 + 2 : i0, j0 = j & ~1, j1 = j0 + 2 < gr ? j0 + 2 : j0;
      if (!(i & 1)) i1 = i0;
      if (!(j & 1)) j1 = j0;
      uint32_t a = g[j0 * FLAME_GC + i0], b = g[j0 * FLAME_GC + i1], c = g[j1 * FLAME_GC + i0], d = g[j1 * FLAME_GC + i1];
      uint32_t lo = a < b ? a : b, hi = a < b ? b : a;
      lo = c < lo ? c : lo, hi = c > hi ? c : hi;
      lo = d < lo ? d : lo, hi = d > hi ? d : hi;
      if (lo > 2765 || hi < 1536) g[j * FLAME_GC + i] = (uint16_t)((a + b + c + d) >> 2);
      else g[j * FLAME_GC + i] = q16(smoke_at(&q, i * ix - 0.5f, j * iy - 0.5f));
    }
}

/* Draws rows ya..yb (screen) of flame k whose sprite is at (x, y), w x h,
   from its grid g, blended between the grid points so the edges are as fine
   as single pixels. */
PROF_NOINLINE void flame_rows(int k, int x, int y, int s, int sh, int top, const uint16_t *g, C *strip, int y0, int ya,
                              int yb, int cx0, int cx1) {
  const flame_t *f = &fl[k];
  float I = f->real > 10 ? 10 : f->real;
  if (I < 0.1f) return;
  /* rows: in the sprite, on screen, and above uv.y 0.12 (lower, the flame is
     the box's own colour and the box is there) */
  int r0 = ya - y, r1 = yb - y, rmax = (int)(0.62f * sh);
  if (r0 < 0) r0 = 0;
  if (r1 > rmax) r1 = rmax;
  if (r0 >= r1) return;
  int c0 = cx0 - x, c1 = cx1 - x;
  if (c0 < 0) c0 = 0;
  if (c1 > s) c1 = s;
  if (c1 > (FLAME_GC - 1) * FG) c1 = (FLAME_GC - 1) * FG;
  if (c0 >= c1) return;
  const float *c1c = col1[k], *c2c = col2[k];
  float invy = 1.f / sh;
  for (int r = r0; r < r1; r++) {
    int j = r / FG;
    if (j + 1 >= FLAME_GR) break;
    const uint16_t *ra = g + j * FLAME_GC, *rb = ra + FLAME_GC;
    int wy = r - j * FG; /* 0..FG-1 */
    float uy = (r + 0.5f) * invy - 0.5f;
    /* the tips thin out in the last 3 pixels under the top limit */
    int fade = y + r - top < 3 ? (3 - (y + r - top)) * 410 : 0;
    /* the colour at this height (the shader's gradient to colour_2), as a
       table over the smoke (16 steps from 0 to 1) */
    float d = 0.12f - uy, base[3];
    for (int ch = 0; ch < 3; ch++) base[ch] = c1c[ch] * (1 - 0.5f * d) + 2.5f * d * c2c[ch];
    C lut[17];
    for (int n = 0; n <= 16; n++) {
      float m = 1.f + (-2.f + 0.5f * I * (n * (1.f / 16))) * d;
      lut[n] = rgb(base[0] * m, base[1] * m, base[2] * m);
    }
    C *dst = strip + (y + r - y0) * 320 + x;
    for (int cx = c0; cx < c1; cx++) {
      int i = cx / FG, wx = cx - i * FG;
      /* bilinear, in 1/2048 units (FG = 2: weights 0 or 1/2) */
      int a = ra[i], b = rb[i];
      if (wx) a = (a + ra[i + 1]) >> 1, b = (b + rb[i + 1]) >> 1;
      int sm = (wy ? (a + b) >> 1 : a) + fade;
      if (sm > 2048) continue;
      dst[cx] = lut[sm >> 7];
    }
  }
}
