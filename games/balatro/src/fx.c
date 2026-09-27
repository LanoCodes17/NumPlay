/* The editions, after Balatro's shaders (resources/shaders/foil.fs, holo.fs,
 * polychrome.fs, negative.fs, negative_shine.fs), worked out per pixel of the
 * card art. uv is the position on the card (0..1). On a card this small the
 * shaders' colour would drown the art, so each edition keeps the brightness
 * of the art underneath (only the hue and tint move) and is laid over it a
 * little lighter than in the game. */
#ifndef PROF_NOINLINE
#define PROF_NOINLINE
#endif
#include "fx.h"
#include "fmath.h"

static float T; /* seconds */


/* ------------------------------------------------------------ colours */
static inline void unpack(C c, float *r, float *g, float *b) {
  *r = (float)(c >> 11) * (1.f / 31), *g = (float)((c >> 5) & 63) * (1.f / 63), *b = (float)(c & 31) * (1.f / 31);
}
static inline C pack(float r, float g, float b) {
  r = fm_clamp(r, 0, 1), g = fm_clamp(g, 0, 1), b = fm_clamp(b, 0, 1);
  return (C)((int)(r * 31.f + .5f) << 11 | (int)(g * 63.f + .5f) << 5 | (int)(b * 31.f + .5f));
}

/* the edition's colour (r, g, b) over the art's (or, og, ob): the art's
   brightness is kept, so its lines and shading stay readable; then the
   edition shows at strength k (0..1) */
static C over_art(float or_, float og, float ob, float r, float g, float b, float k) {
  float d = (.3f * or_ + .59f * og + .11f * ob) - (.3f * r + .59f * g + .11f * b);
  r += d, g += d, b += d;
  return pack(or_ + (r - or_) * k, og + (g - og) * k, ob + (b - ob) * k);
}

/* the shaders' HSL() and RGB() */
static void hsl(float r, float g, float b, float *h, float *s, float *l) {
  float lo = r < g ? (r < b ? r : b) : (g < b ? g : b), hi = r > g ? (r > b ? r : b) : (g > b ? g : b);
  float d = hi - lo, sum = hi + lo;
  *l = .5f * sum, *h = 0, *s = 0;
  if (d <= 0) return;
  *s = *l < .5f ? d / sum : d / (2.f - sum);
  float x = hi == r ? (g - b) / d : hi == g ? (b - r) / d + 2.f : (r - g) / d + 4.f;
  x *= 1.f / 6;
  *h = x - fm_floor(x);
}
static inline float hue(float s, float t, float h) {
  float hs = (h - fm_floor(h)) * 6.f;
  if (hs < 1) return (t - s) * hs + s;
  if (hs < 3) return t;
  if (hs < 4) return (t - s) * (4.f - hs) + s;
  return s;
}
static void rgb(float h, float s, float l, float *r, float *g, float *b) {
  if (s < 0.0001f) {
    *r = *g = *b = l;
    return;
  }
  float t = l < .5f ? s * l + l : -s * l + (s + l), q = 2.f * l - t;
  *r = hue(q, t, h + 1.f / 3), *g = hue(q, t, h), *b = hue(q, t, h - 1.f / 3);
}

/* the moving field of holo.fs and polychrome.fs, at a point scaled by k */
static float field(float u, float v, float k, float t) {
  float x = (u - .5f) * k, y = (v - .5f) * k;
  float x1 = x + 50.f * fm_sin(-t / 143.634f), y1 = y + 50.f * fm_cos(-t / 99.4324f);
  float x2 = x + 50.f * fm_cos(t / 53.1532f), y2 = y + 50.f * fm_cos(t / 61.4532f);
  float x3 = x + 50.f * fm_sin(-t / 87.53218f), y3 = y + 50.f * fm_sin(-t / 49.f);
  return (1.f + fm_cos(fm_sqrt(x1 * x1 + y1 * y1) / 19.483f) +
          fm_sin(fm_sqrt(x2 * x2 + y2 * y2) / 33.155f) * fm_cos(y2 / 15.73f) +
          fm_cos(fm_sqrt(x3 * x3 + y3 * y3) / 27.193f) * fm_sin(x3 / 21.92f)) * .5f;
}

/* ------------------------------------------------------------ editions */
/* Each edition is a pattern over the card (a number per position, slow to
   work out, the same for the 2x2 pixels around it) applied to each pixel's
   colour (quick). */
static float foil_pattern(float u, float v, float seed) {
  float fr = T / 28.f + seed, fg = T;
  float ax = (u - .5f) * (35.f / 47.f), ay = v - .5f, L = fm_sqrt(ax * ax + ay * ay) + 1e-4f;
  float fac = fm_clamp(2.f * fm_sin(L * 90.f + fr * 2.f + 3.f * (1.f + .8f * fm_cos(L * 113.1121f - fr * 3.121f))) - 1.f -
                           (5.f - L * 90.f > 0 ? 5.f - L * 90.f : 0),
                       0, 1);
  float rx = fm_cos(fr * .1221f), ry = fm_sin(fr * .3512f);
  float ang = (rx * ax + ry * ay) / (fm_sqrt(rx * rx + ry * ry) * L);
  float fac2 = fm_clamp(5.f * fm_cos(fg * .3f + ang * 3.14f * (2.2f + .9f * fm_sin(fr * 1.65f + .2f * fg))) - 4.f -
                            (2.f - L * 20.f > 0 ? 2.f - L * 20.f : 0),
                        0, 1);
  float fac3 = .3f * fm_clamp(2.f * fm_sin(fr * 5.f + u * 3.f + 3.f * (1.f + .5f * fm_cos(fr * 7.f))) - 1.f, -1, 1);
  float fac4 = .3f * fm_clamp(2.f * fm_sin(fr * 6.66f + v * 3.8f + 3.f * (1.f + .5f * fm_cos(fr * 3.414f))) - 1.f, -1, 1);
  float mx = fac > fac2 ? fac : fac2;
  if (fac3 > mx) mx = fac3;
  if (fac4 > mx) mx = fac4;
  if (mx < 0) mx = 0;
  float m = mx + 2.2f * (fac + fac2 + fac3 + fac4);
  return m < 0 ? 0 : m;
}
static C foil(C c, float m) {
  float r, g, b;
  unpack(c, &r, &g, &b);
  float lo = r < g ? (r < b ? r : b) : (g < b ? g : b), hi = r > g ? (r > b ? r : b) : (g > b ? g : b);
  float d = 1.f - lo > .5f ? 1.f - lo : .5f;
  if (d > hi) d = hi;
  float fr_ = r - d + d * m * .3f, fg_ = g - d + d * m * .3f, fb_ = b + d * m * 1.9f;
  float a = .3f + .9f * (m * .1f < .5f ? m * .1f : .5f);
  a *= .8f; /* lighter than the game */
  fr_ = fm_clamp(fr_, 0, 1), fg_ = fm_clamp(fg_, 0, 1), fb_ = fm_clamp(fb_, 0, 1);
  return pack(r + (fr_ - r) * a, g + (fg_ - g) * a, b + (fb_ - b) * a);
}

static float holo_pattern(float u, float v, float seed) {
  float t = T * 8.221f;
  /* the field and the grid at half the game's frequency: the card is small */
  float res = .5f + .5f * fm_cos((T / 28.f + seed) * 2.612f + (field(u, v, 125.f, t) - .5f) * 3.14f);
  const float gs = .79f * .5f;
  float f1 = 7.f * fm_abs(fm_cos(u * gs * 20.f)) - 6.f, f2 = 7.f * fm_cos(v * gs * 45.f + u * gs * 20.f) - 6.f,
        f3 = 7.f * fm_cos(v * gs * 45.f - u * gs * 20.f) - 6.f;
  float fac = f1 > f2 ? f1 : f2;
  if (f3 > fac) fac = f3;
  return res + (fac > 0 ? .5f * fac : 0);
}
static C holo(C c, float shift) {
  float r, g, b, h, s, l;
  unpack(c, &r, &g, &b);
  hsl(.5f * r, .5f * g, .5f * b + .5f, &h, &s, &l);
  float lo = r < g ? (r < b ? r : b) : (g < b ? g : b), hi = r > g ? (r > b ? r : b) : (g > b ? g : b);
  float d = .2f + .3f * (hi - lo) + .1f * hi;
  float hr, hg, hb;
  rgb(h + shift, s * 1.3f > 1 ? 1 : s * 1.3f, l * .6f + .4f, &hr, &hg, &hb);
  float nr = (1 - d) * r + d * hr * .9f, ng = (1 - d) * g + d * hg * .8f, nb = (1 - d) * b + d * hb * 1.2f;
  return over_art(r, g, b, fm_clamp(nr, 0, 1), fm_clamp(ng, 0, 1), fm_clamp(nb, 0, 1), .8f);
}

static float poly_pattern(float u, float v, float seed) {
  float t = T * 3.221f;
  return .5f + .5f * fm_cos((T / 28.f + seed) * 2.612f + (field(u, v, 50.f, t) - .5f) * 3.14f) + T * .04f;
}
static C poly(C c, float shift) {
  float r, g, b, h, s, l;
  unpack(c, &r, &g, &b);
  float lo = r < g ? (r < b ? r : b) : (g < b ? g : b), hi = r > g ? (r > b ? r : b) : (g > b ? g : b);
  float sf = 1.f - (1.1f - (hi - lo) > 0 ? .05f * (1.1f - (hi - lo)) : 0);
  hsl(r * sf, g * sf, b, &h, &s, &l);
  float nr, ng, nb;
  rgb(h + shift, s + .5f < .6f ? s + .5f : .6f, l, &nr, &ng, &nb);
  return over_art(r, g, b, nr, ng, nb, .7f);
}

static float neg_pattern(float u, float v) {
  /* negative_shine: a faint moving light */
  float sh = .5f + .5f * fm_sin(11.f * u + 4.32f * v + T * .43f * 12.f + fm_cos(T * .19f + v * 4.2f - u * 4.f));
  return sh * sh * .12f;
}
static C negative(C c, float sh) {
  float r, g, b, h, s, l;
  unpack(c, &r, &g, &b);
  hsl(r, g, b, &h, &s, &l);
  float nr, ng, nb;
  rgb(.2f - h, s, 1.f - l, &nr, &ng, &nb);
  nr += .8f * 79.f / 255, ng += .8f * 99.f / 255, nb += .8f * 103.f / 255;
  return pack(nr + sh * .6f, ng + sh * .6f, nb + sh);
}

/* the pattern of the current pair of rows, per pair of columns */
static struct {
  int ed, row, sw;
  float seed;
  uint8_t gen;
} key;
static float pat[24];
static uint8_t pat_gen[24];

PROF_NOINLINE C fx_edition(C c, int ed, int sx, int sy, int sw, int sh, float seed) {
  if (key.ed != ed || key.row != (sy >> 1) || key.sw != sw || key.seed != seed) {
    key.ed = ed, key.row = sy >> 1, key.sw = sw, key.seed = seed;
    if (++key.gen == 0) { /* wrapped: forget all */
      for (int k = 0; k < 24; k++) pat_gen[k] = 0;
      key.gen = 1;
    }
  }
  int i = sx >> 1;
  if (i > 23) i = 23;
  if (pat_gen[i] != key.gen) {
    float u = ((float)(sx & ~1) + 1.f) / (float)sw, v = ((float)(sy & ~1) + 1.f) / (float)sh;
    pat[i] = ed == FX_FOIL ? foil_pattern(u, v, seed) : ed == FX_HOLO ? holo_pattern(u, v, seed)
           : ed == FX_POLY ? poly_pattern(u, v, seed) : neg_pattern(u, v);
    pat_gen[i] = key.gen;
  }
  switch (ed) {
    case FX_FOIL: return foil(c, pat[i]);
    case FX_HOLO: return holo(c, pat[i]);
    case FX_POLY: return poly(c, pat[i]);
    case FX_NEG: return negative(c, pat[i]);
  }
  return c;
}

/* a new frame: the patterns move */
void fx_frame(uint32_t ms) {
  T = (float)(ms % 2000000u) / 1000.f;
  key.ed = -1;
}
