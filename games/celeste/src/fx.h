/* Particles (Monocle's ParticleSystem and Celeste's ParticleTypes), the dash
 * trail, and Dust. */
#ifndef FX_H
#define FX_H
#include "celeste.h"

enum { PC_STATIC, PC_CHOOSE, PC_BLINK, PC_FADE };
enum { PF_NONE, PF_LINEAR, PF_LATE, PF_INOUT };
enum { PR_NONE, PR_RANDOM, PR_SAMEASDIR };
typedef struct {
  uint32_t color, color2;     /* 0xAARRGGBB, unpremultiplied */
  uint8_t color_mode, fade_mode, rot_mode, scale_out;
  float size, size_range;
  float speed_min, speed_max, speed_mult;
  float life_min, life_max;
  float direction, dir_range;
  float spin_min, spin_max;
  uint8_t spin_flip;
  V2 accel;
  float friction;
  uint16_t src[4];            /* textures; none: a 1x1 pixel */
  uint8_t nsrc, raw_dt;
} PType;
enum { PL_BG, PL_MID, PL_FG };   /* ParticlesBG, Particles, ParticlesFG */
void particles_clear(void);
void particles_emit(int layer, const PType *t, int n, V2 pos, V2 range, float dir);
void particles_emit_c(int layer, const PType *t, int n, V2 pos, V2 range, uint32_t color, float dir);
void particles_emit1(int layer, const PType *t, V2 pos, float dir);
void particles_update(void);
void particles_render(int layer);

void dust_burst(V2 pos, float dir, int n);
void dust_burst_fg(V2 pos, float dir, int n, float range);

/* TrailManager: afterimages */
void trail_add(float x, float y, uint16_t tex, float ox, float oy, float sx, float sy, uint32_t color, float duration);
void trail_update(void);
void trail_render(void);
void trail_clear(void);

/* SlashFx and the like: short sprite effects */
void fx_update(void);
#include "ptypes.h"
#endif
