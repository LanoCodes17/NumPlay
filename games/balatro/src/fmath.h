/* Small float helpers for the effects (no libm): sin/cos to about 1e-3. */
#ifndef FMATH_H
#define FMATH_H

static inline __attribute__((always_inline)) float fm_abs(float x) { return x < 0 ? -x : x; }
static inline __attribute__((always_inline)) float fm_floor(float x) {
  int k = (int)x;
  return (float)(x < (float)k ? k - 1 : k);
}
static __attribute__((noinline, unused)) float fm_cos(float x) {
  x *= 1.f / (2.f * 3.14159265f);
  x -= .25f + fm_floor(x + .25f);
  x *= 16.f * (fm_abs(x) - .5f);
  x += .225f * x * (fm_abs(x) - 1.f);
  return x;
}
static inline float fm_sin(float x) { return fm_cos(x - 1.5707963f); }
static inline __attribute__((always_inline)) float fm_sqrt(float x) { return __builtin_sqrtf(x); }
static inline __attribute__((always_inline)) float fm_clamp(float x, float a, float b) {
  return x < a ? a : x > b ? b : x;
}
#endif
