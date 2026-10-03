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
void particles_clear_outside(float l, float t, float r, float b);   /* (a transition's end) */

void dust_burst(V2 pos, float dir, int n);
void dust_burst_fg(V2 pos, float dir, int n, float range);

/* TrailManager: afterimages */
#define MAX_TRAILS 12
#define TRAIL_HAIR 7
typedef struct {
  float ox, oy, sx, sy, pct, dur;   /* sx: the facing's sign in it */
  int16_t x, y, depth;              /* Position (rounded), Depth */
  uint16_t tex, color;              /* the frame; Color (RGB565) */
  int8_t sdy;                       /* the sprite's offset */
  uint8_t born, nhair, bangs;       /* the frame added; PlayerHair's nodes, its bangs frame (no hair: the color's alpha) */
  int8_t hair[TRAIL_HAIR][2];       /* the nodes (as drawn: rounded) from Position */
} Trail;
Trail *trail_add(float x, float y, uint16_t tex, float ox, float oy, float sx, float sy, uint32_t color, float duration, int depth);
void trail_update(void);
void trail_render_between(int hi, int lo);
void trail_clear(void);

/* SlashFx and the like: short sprite effects */
void fx_update(void);
#include "ptypes.h"
#endif
