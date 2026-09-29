#ifndef NUMDASH_GAME_H
#define NUMDASH_GAME_H
#include <stdbool.h>
#include <stdint.h>
#include "level.h"

#define ND_HZ 240
#define ND_DT (1.0f / ND_HZ)
enum { MODE_CUBE = 0, MODE_SHIP = 1, MODE_BALL = 2, MODE_UFO = 3, MODE_WAVE = 4, MODE_ROBOT = 5, MODE_SPIDER = 6, MODE_SWING = 7 };
enum { BUF_NONE = 0, BUF_READY = 1, BUF_END = 2 };
/* Visible world: 480 x 360 GD units (20 px blocks); ground sits 90 units
 * above the bottom edge when the camera is at rest, as in GD. */
#define VIEW_W 480.0f
#define VIEW_H 360.0f
#define GROUND_OFFSET 90.0f
#define PLAYER_SCREEN_X 150.0f

typedef struct {
  float x, y, vy, gravity, rot, target_rot, time_since_ground, ceil_inv;
  float ground_y, ceiling_y, snap_diff;
  float spin;       /* ball: rolling speed, 1 on a surface, -0.7 in the air */
  float snap_ox, snap_oy;   /* the block the cube last landed on (x snapping) */
  int32_t snap_frame, frame, coyote;
  uint8_t mode, speed, buffer;
  bool upside, on_ground, on_ceiling, vel_override, snap_rot, left_ground, inverse_rot, jumped, rot_dir_neg, has_snap, mini, ufo_buf;
  bool second;          /* the dual mode's second body */
  bool old_on_ground, orb_touching;
  uint8_t hover;        /* robot: hover ticks left while the button stays held */
} Player;

/* A colour channel: an RGB fade (t, dur in steps) plus, for 2.0+ levels,
 * opacity, blending and an optional copy of another channel with HSV. */
typedef struct {
  uint8_t cur[3], from[3], to[3], op, op_from, op_to, flags, copy;
  uint16_t t, dur;
  int16_t h, s, v;
} Channel;
#define MAX_CHANNELS 128
#define MAX_GROUPS 128
/* Group state: offset from moves, opacity (fading), on / off. */
typedef struct { float dx, dy; uint8_t alpha, a_from, a_to, on; uint16_t at, adur; } GroupState;
typedef struct { uint16_t group; uint8_t easing, flags; float tx, ty, done_x, done_y, rate; uint16_t t, dur; } MoveAction;
typedef struct { uint16_t target; uint8_t flags, rgb[3], copy; int16_t h, s, v; uint16_t t, tin, hold, tout; } PulseAction;
#define MAX_MOVES 32
#define MAX_PULSES 24

enum { FX_JUMP, FX_LAND, FX_PAD, FX_ORB, FX_PORTAL, FX_COIN, FX_DEATH, FX_GRAVITY, FX_ORB_TOUCH, FX_WALL, FX_SIZE, FX_SPEED };
typedef struct { uint8_t kind, arg; int16_t obj; float x, y; } FxEvent;

typedef struct {
  Player p;
  Player p2;            /* dual mode */
  const Level *L;
  float cam_x, cam_y, ground_x, bg_x, ground_gfx, cam_intended_y, wall_y, end_t, end_x0, end_y0, cam_wall_t;
  float cam_wall_y0, bg_wall_x0, ground_wall_x0, shake_t, shake_amp;
  Channel ch[MAX_CHANNELS];
  uint32_t tick;
  uint16_t next_event, jumps, death_obj;
  /* 2.0+ levels */
  uint32_t trig_pos;            /* next position trigger in the stream */
  float trig_x;                 /* its x */
  GroupState gr[MAX_GROUPS];
  MoveAction mv[MAX_MOVES];
  PulseAction pu[MAX_PULSES];
  uint8_t nmv, npu, touch_bits[16];
  uint8_t fade_effect, coins, attempt_camera, touch_done[8];
  bool trail, dead, complete, ending, hold_prev, pressed_prev, dual;
  float dual_y;
  bool menu_camera;   /* fixed camera, ground scrolling on its own (main menu) */
  float used_below;   /* objects left of this x count as used (practice) */
  uint8_t used[MAX_LEVEL_OBJS / 8];
  FxEvent fx[24];
  uint8_t fx_count;
} Game;

/* Practice checkpoint: everything needed to resume from a point. */
typedef struct {
  Player p;
  float cam_x, cam_y, ground_x, bg_x, ground_gfx, cam_intended_y;
  Channel ch[CH_COUNT];
  uint32_t tick;
  uint16_t next_event, jumps;
  uint8_t fade_effect, touch_done[8];
  bool trail;
} Checkpoint;

void game_start(Game *g, const Level *L, bool first_attempt);
void game_step(Game *g, bool hold);
float game_progress(const Game *g);
bool game_used(const Game *g, const RObj *o, unsigned gi);
void game_save_checkpoint(const Game *g, Checkpoint *c);
void game_load_checkpoint(Game *g, const Checkpoint *c);
unsigned game_coin_index(const Level *L, unsigned obj);
/* trig.c: 2.0+ triggers, groups and channels */
void trig_start(Game *g);
void trig_step(Game *g, float player_dx);
void trig_touch(Game *g, unsigned idx);
bool gset_on(const Game *g, unsigned set);
void gset_state(const Game *g, unsigned set, float *dx, float *dy, unsigned *alpha, bool *on);
float ease(int kind, float rate, float u);
float pulse_level(const PulseAction *p);
void hsv_shift(const uint8_t in[3], int h, int s64, int v64, unsigned flags, uint8_t out[3]);
/* Where an object is now (its groups' moves applied); false when its
   groups are switched off. */
static inline bool obj_where(const Game *g, const RObj *o, float *x, float *y) {
  *x = obj_x(o);
  *y = obj_y(o);
  if (!g->L->ext || !o->paint) return true;
  const LStyle *st = obj_style(g->L, o);
  if (!st->groups) return true;
  float dx, dy;
  unsigned a;
  bool on;
  gset_state(g, st->groups, &dx, &dy, &a, &on);
  *x += dx;
  *y += dy;
  return on;
}
/* World -> screen helpers (floating point pixels). */
static inline float wx_to_sx(const Game *g, float x) { return (x - g->cam_x) * (2.0f / 3.0f); }
static inline float wy_to_sy(const Game *g, float y) { return 240.0f - (GROUND_OFFSET + y - g->cam_y) * (2.0f / 3.0f); }
#endif
