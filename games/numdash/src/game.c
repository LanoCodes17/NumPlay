/* Geometry Dash 2.x physics at 240 Hz.
 *
 * Constants and collision rules follow the reverse-engineered values used by
 * the gd3ds project (itself based on the Pathfinder mod's physics) for the
 * cube, ship, ball, UFO, wave and mini sizes, and the gdsolver project's
 * measurements of GD 2.2 for the robot, spider, swing and the ring / pad
 * table. Velocities are in GD units per second, "vy" is relative to the
 * current gravity. */
#include "game.h"
#include <math.h>
#include <string.h>

enum { SPEED_SLOW, SPEED_NORMAL, SPEED_FAST, SPEED_FASTER };
static const float SPEEDS[4] = {251.16007972f, 311.58009371f, 387.42014040f, 468.00013884f};
static const float SPEED_MULT[4] = {0.7f, 0.9f, 1.1f, 1.3f};
static const float CUBE_JUMP[4] = {573.481728f, 603.7217172f, 616.681728f, 606.421728f};
static const float CUBE_GRAV[4] = {-2747.52f, -2794.1082f, -2786.4f, -2799.36f};
static const float VEL_THRESH[4] = {101.541492f, 103.485494592f, 103.377492f, 103.809492f};
enum { J_YPAD, J_YORB, J_BPAD, J_BORB, J_PPAD, J_PORB };
/* [speed][jump type][cube, ship, ball, UFO][normal, mini] (gd3ds; the ship
   and the UFO cap what they receive) */
static const float JUMPS[4][6][4][2] = {
  {{{864, 691.2f}, {432, 508.248f}, {518.4f, 414.72002f}, {573.48f, 458.784f}},
   {{573.48f, 458.784f}, {573.48f, 458.784f}, {401.436f, 321.148795f}, {573.48f, 458.784f}},
   {{-345.6f, -276.48f}, {-229.392f, -183.519f}, {-160.5744f, -128.463298f}, {-229.392f, -183.519f}},
   {{-229.392f, -183.519f}, {-229.392f, -183.519f}, {-160.5744f, -128.463298f}, {-229.392f, -183.519f}},
   {{561.6f, 449.28f}, {302.4f, 241.92f}, {362.88f, 290.30401f}, {345.6f, 276.4f}},
   {{412.884f, 330.318f}, {212.166f, 169.776f}, {309.0906f, 247.287596f}, {240.84f, 192.672f}}},
  {{{864, 691.2f}, {432, 508.248f}, {518.4f, 414.72002f}, {432, 691.2f}},
   {{603.72f, 482.976f}, {603.72f, 482.976f}, {422.604f, 338.08319f}, {603.72f, 482.976f}},
   {{-345.6f, -276.48f}, {-345.6f, -276.48f}, {-207.36f, -165.88801f}, {-345.6f, -276.48f}},
   {{-241.488f, -193.185f}, {-241.488f, -193.18f}, {-169.0416f, -135.2295f}, {-241.488f, -193.185f}},
   {{561.6f, 449.28f}, {302.4f, 241.92f}, {362.88f, 290.30401f}, {345.6f, 276.4f}},
   {{434.7f, 347.76f}, {223.398f, 178.686f}, {325.4202f, 260.3286f}, {258.984f, 207.198f}}},
  {{{864, 691.2f}, {432, 508.248f}, {518.4f, 414.72002f}, {432, 691.2f}},
   {{616.68f, 481.734f}, {616.68f, 481.734f}, {431.676f, 345.34079f}, {616.68f, 481.734f}},
   {{-345.6f, -276.48f}, {-345.6f, -276.48f}, {-207.36f, -165.88801f}, {-345.6f, -276.48f}},
   {{-246.672f, -197.343f}, {-246.672f, -197.343f}, {-172.6704f, -138.1401f}, {-246.672f, -197.343f}},
   {{561.6f, 449.28f}, {302.4f, 241.92f}, {362.88f, 290.30401f}, {345.6f, 276.4f}},
   {{443.988f, 355.212f}, {228.15f, 182.52f}, {332.3754f, 265.923f}, {258.984f, 207.198f}}},
  {{{864, 691.2f}, {432, 508.248f}, {518.4f, 414.72002f}, {432, 691.2f}},
   {{606.42f, 485.136f}, {606.42f, 485.136f}, {424.494f, 339.59519f}, {606.42f, 485.136f}},
   {{-345.6f, -276.48f}, {-345.6f, -276.48f}, {-207.36f, -165.88801f}, {-345.6f, -276.48f}},
   {{-242.568f, -194.049f}, {-242.568f, -194.049f}, {-169.7976f, -135.8343f}, {-242.568f, -194.049f}},
   {{561.6f, 449.28f}, {302.4f, 241.92f}, {362.88f, 290.30401f}, {345.6f, 276.4f}},
   {{436.644f, 349.272f}, {224.37f, 179.496f}, {326.8566f, 261.5004f}, {254.718f, 203.742f}}},
};
/* ball: gravity, the push when it flips, and how fast it rolls (degrees per second) */
#define BALL_GRAV (-1676.46672f)
#define BALL_HEIGHT 240.0f
#define BALL_AIR_SPIN 0.7f
static const float BALL_JUMP[4] = {-172.044007f, -181.11601f, -185.00401f, -181.92601f};
static const float BALL_ROLL[4] = {120 / (0.2f * 1.2405638f), 120 / 0.2f, 120 / (0.2f * 0.80424345f), 120 / (0.2f * 0.6657693f)};
/* UFO: flap strength and the two gravities (weak while rising fast) */
static const float UFO_FLAP[2] = {371.034f, 358.992f};
static const float UFO_GRAV_HI[2] = {-1676.84f, -1969.92f}, UFO_GRAV_LO[2] = {-1117.56f, -1308.96f};
/* ship in mini size */
#define SHIP_MINI_MIN (-406.566f)
#define SHIP_MINI_MAX 508.248f
#define ROT_SPEED 415.3848f
#define ROT_SPEED_MINI 540.0f
#define CEILING_INVUL 0.1f
#define DRAG_TIME 0.1f
#define SHIP_MIN (-345.6f)
#define SHIP_MAX 432.0f
#define END_START (10 * 30.0f)

static float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }
static float grav(const Player *p, float v) { return p->upside ? -v : v; }
/* half the player's size: 15, or 9 when mini */
static float phalf(const Player *p) { return p->mode == MODE_WAVE ? (p->mini ? 3.0f : 5.0f) : p->mini ? 9.0f : 15.0f; }
static float grav_bottom(const Player *p) { return p->upside ? -(p->y + phalf(p)) : p->y - phalf(p); }
static float grav_top(const Player *p) { return p->upside ? -(p->y - phalf(p)) : p->y + phalf(p); }
static float grav_floor(const Player *p) { return p->upside ? -p->ceiling_y : p->ground_y; }
/* How far above the ground the player rests (the wave rides 10 above it). */
static float rest_half(const Player *p) { return p->mode == MODE_WAVE ? (p->mini ? 6.0f : 10.0f) : phalf(p); }

bool game_used(const Game *g, const RObj *o, unsigned gi) {
  return (gi < MAX_LEVEL_OBJS && (g->used[gi >> 3] >> (gi & 7) & 1)) || obj_x(o) < g->used_below;
}
static void set_used(Game *g, unsigned gi) { if (gi < MAX_LEVEL_OBJS) g->used[gi >> 3] |= (uint8_t)(1u << (gi & 7)); }
static void fx(Game *g, uint8_t kind, uint8_t arg, int obj, float x, float y) {
  if (g->fx_count < sizeof(g->fx) / sizeof(g->fx[0])) g->fx[g->fx_count++] = (FxEvent){kind, arg, (int16_t)obj, x, y};
}
static void kill(Game *g, int obj) {
  if (g->dead) return;
  g->dead = true;
  g->death_obj = obj < 0 ? 0xffff : (uint16_t)obj;
  fx(g, FX_DEATH, 0, obj, g->p.x, g->p.y);
}

unsigned game_coin_index(const Level *L, unsigned obj) {
  unsigned k = 0;
  while (k < L->coin_count && L->coin_obj[k] < obj) k++;
  return k;
}

static void set_velocity(Player *p, float v, bool override) { p->vel_override = override; p->vy = p->mini ? v * 0.8f : v; }
static void landing(Player *p) { if (p->mode == MODE_CUBE) p->jumped = false; }
static void update_rot_dir(Player *p) { p->rot_dir_neg = p->upside; }
static float closest_rotation(float rot) {
  float r = fmodf(rot, 360.0f);
  if (r < 0) r += 360;
  float s = roundf(r / 90) * 90;
  s = fmodf(s, 360);
  return s < 0 ? s + 360 : s;
}
static float slerp_angle(float from, float to, float t) {
  float fx = cosf(from * .5f), fy = sinf(from * .5f), tx = cosf(to * .5f), ty = sinf(to * .5f);
  float dot = fx * tx + fy * ty;
  if (dot < 0) { dot = -dot; tx = -tx; ty = -ty; }
  float w0 = 1 - t, w1 = t;
  if (dot < 0.9999f) {
    float b = acosf(dot), sb = sinf(b);
    w0 = sinf(w0 * b) / sb;
    w1 = sinf(w1 * b) / sb;
  }
  return atan2f(w0 * fy + w1 * ty, w0 * fx + w1 * tx) * 2;
}
#define DEG (3.14159265f / 180.0f)

static void set_bounds_for_portal(Game *g, float portal_y, float height) {
  Player *p = &g->p;
  float v = (portal_y - (height + 60) / 2.0f) / 30.0f, c = ceilf(v);
  if (fabsf(v - roundf(v)) < 1e-6f) c += 1;
  p->ground_y = c > 0 ? c * 30 : 0;
  p->ceiling_y = p->ground_y + height;
  g->cam_intended_y = (p->ground_y + p->ceiling_y) / 2 - (VIEW_H / 2 - GROUND_OFFSET);
}

void game_start(Game *g, const Level *L, bool first_attempt) {
  memset(g, 0, sizeof(*g));
  g->L = L;
  Player *p = &g->p;
  p->x = 0;
  p->y = 15;
  p->speed = SPEED_NORMAL;
  p->ceiling_y = 999999;
  p->coyote = 1 << 30;
  p->on_ground = true;
  if (L->start_mode == MODE_SHIP) {
    p->mode = MODE_SHIP;
    set_bounds_for_portal(g, 150, 300);
  }
  for (int c = 0; c < CH_COUNT; c++) {
    memcpy(g->ch[c].cur, L->colors[c], 3);
    memcpy(g->ch[c].to, L->colors[c], 3);
  }
  trig_start(g);
  g->attempt_camera = first_attempt;
  g->cam_x = first_attempt ? 15 : p->x - PLAYER_SCREEN_X;
  g->ground_x = g->bg_x = g->cam_x;
  g->death_obj = 0xffff;
  g->used_below = -1e9f;
}

float game_progress(const Game *g) {
  if (g->complete) return 100;
  float v = g->p.x / g->L->end_x * 100;
  return clampf(v, 0, 100);
}

static void clamp_ground(Game *g) {
  Player *p = &g->p;
  float h = rest_half(p);
  if (p->y - h < p->ground_y) {
    if (p->ceil_inv <= 0 && p->mode == MODE_CUBE && p->upside) kill(g, -1);
    if (grav(p, p->vy) <= 0) set_velocity(p, 0, p->mode == MODE_BALL);
    p->y = p->ground_y + h;
    p->snap_frame = 0;
  }
  if (p->y + h > p->ceiling_y) {
    if (p->ceil_inv <= 0 && p->mode == MODE_CUBE && !p->upside) kill(g, -1);
    if (grav(p, p->vy) >= 0) set_velocity(p, 0, p->mode == MODE_BALL);
    p->y = p->ceiling_y - h;
  }
}

/* Does the player's box touch a round hazard? GD tests the circle's centre
   inside the box, or a box corner inside the circle (so the reach is a
   little shorter straight above a saw than at its corners), as measured by
   gdsolver. */
static bool box_circle(float px, float py, float h, float cx, float cy, float r) {
  float ex = fabsf(cx - px) - h, ey = fabsf(cy - py) - h;
  return (ex < 0 && ey < 0) || ex * ex + ey * ey < r * r;
}

static bool overlap(float ax, float ay, float ahw, float ahh, float bx, float by, float bhw, float bhh) {
  return fabsf(ax - bx) < ahw + bhw && fabsf(ay - by) < ahh + bhh;
}

/* Separating axis test of two boxes turned by a and b radians (clockwise). */
static bool obb_overlap(float ax, float ay, float ahw, float ahh, float a, float bx, float by, float bhw, float bhh, float b) {
  float ca = cosf(a), sa = sinf(a), cb = cosf(b), sb = sinf(b), dx = bx - ax, dy = by - ay;
  /* axes of both boxes: (cos, -sin) and (sin, cos) in y-up space */
  float axes[4][2] = {{ca, -sa}, {sa, ca}, {cb, -sb}, {sb, cb}};
  for (int k = 0; k < 4; k++) {
    float ux = axes[k][0], uy = axes[k][1];
    float ra = ahw * fabsf(ux * ca - uy * sa) + ahh * fabsf(ux * sa + uy * ca);
    float rb = bhw * fabsf(ux * cb - uy * sb) + bhh * fabsf(ux * sb + uy * cb);
    if (fabsf(dx * ux + dy * uy) >= ra + rb) return false;
  }
  return true;
}

/* GD's stair snap (checkSnapJumpToObject, as measured by gdsolver on 2.2):
   on every tick the cube stands on a solid, when that solid is another one
   than the last and sits one stair step away (one block up, one down or two
   up at a speed dependent distance), x is nudged by up to a unit or two so
   the player keeps its offset to the stairs. */
static void snap_to(Game *g, float ox, float oy) {
  Player *p = &g->p;
  /* [speed] {threshold, one up, one down, two up}; the mini one-up is 90 */
  static const float stairs[4][4] = {{1, 90, 120, 60}, {1, 120, 150, 90}, {2, 150, 195, 120}, {2, 180, 225, 135}};
  const float *st = stairs[p->speed];
  if (p->has_snap && (ox != p->snap_ox || oy != p->snap_oy)) {
    float dx = ox - p->snap_ox, dy = grav(p, oy - p->snap_oy), th = st[0], up = p->mini && p->speed ? 90 : st[1];
    if ((fabsf(dx - up) <= th && fabsf(dy - 30) <= th) || (fabsf(dx - st[2]) <= th && fabsf(dy + 30) <= th) ||
        (fabsf(dx - st[3]) <= th && fabsf(dy - 60) <= th))
      p->x += clampf(ox + p->snap_diff - p->x, -th, th);
  }
  p->has_snap = true;
  p->snap_ox = ox;
  p->snap_oy = oy;
  p->snap_diff = p->x - ox;
}

static void solid(Game *g, unsigned i, float ox, float oy, float hw, float hh) {
  Player *p = &g->p;
  float clip = (p->mode == MODE_SHIP || p->mode == MODE_UFO ? 7.0f : 10.0f) + fabsf(p->vy) * ND_DT;
  float o_bottom = p->upside ? -(oy + hh) : oy - hh, o_top = p->upside ? -(oy - hh) : oy + hh;
  float in = p->mode == MODE_WAVE ? 1.5f : 4.5f;
  bool internal = overlap(p->x, p->y, in, in, ox, oy, hw, hh), grav_snap = false;
  if (p->ceil_inv > 0) {
    float in_bottom = p->upside ? -(p->y + in) : p->y - in, diff = o_bottom - in_bottom;
    grav_snap = internal && diff >= 0 && diff <= clip;
  }
  /* a mini player about to land on or touch a face is not crushed */
  bool safe = p->mini && (o_top - grav_bottom(p) <= clip || grav_top(p) - o_bottom <= clip);
  if ((p->mode == MODE_WAVE || (!grav_snap && !safe)) && internal) { kill(g, (int)i); return; }
  if (p->mode == MODE_WAVE) return;
  float bottom = grav_bottom(p);
  if (o_top - bottom <= clip && p->vy <= 0) {
    p->y = grav(p, o_top) + grav(p, phalf(p));
    if (p->vy <= 0) p->vy = 0;
    p->on_ground = true;
    p->inverse_rot = false;
    p->time_since_ground = 0;
    landing(p);
  } else if (p->mode != MODE_CUBE || grav_snap) {
    if ((grav_top(p) - o_bottom <= clip && p->vy >= 0) || grav_snap) {
      if (!grav_snap) p->on_ceiling = true; else p->vy = 0;
      p->inverse_rot = false;
      p->time_since_ground = 0;
      p->y = grav(p, o_bottom) - grav(p, phalf(p));
      if (p->vy >= 0) p->vy = 0;
    }
  }
}

/* What a pad or a ring gives the player in its mode and size. The robot
   takes the cube's pads and 0.9 of its rings (GD 2.2's ring table, read by
   gdsolver); the wave keeps its own speed. */
static float jump_value(const Player *p, int type) {
  int m = p->mode;
  if (m <= MODE_UFO) return JUMPS[p->speed][type][m][p->mini];
  if (m == MODE_ROBOT) {
    float v = JUMPS[p->speed][type][MODE_CUBE][p->mini];
    return (type & 1) ? v * 0.9f : v;   /* the odd types are rings */
  }
  return p->vy;
}

static void enter_mode(Game *g, int mode) {
  Player *p = &g->p;
  p->mode = (uint8_t)mode;
  p->hover = 0;
}

/* Dual mode: the other body (held in g->p2 while one is being stepped). */
static const uint8_t DUAL_HEIGHT[8] = {9, 10, 9, 10, 10, 9, 9, 10};
static void set_dual_bounds(Game *g) {
  int h = DUAL_HEIGHT[g->p.mode] > DUAL_HEIGHT[g->p2.mode] ? DUAL_HEIGHT[g->p.mode] : DUAL_HEIGHT[g->p2.mode];
  float in_block = fmodf(g->dual_y, 30), off = (ceilf((h + 1) / 2.f) - 1) * 30;
  float ground = fmaxf(0, floorf((g->dual_y - off) / 30)) * 30;
  if ((h & 1) && in_block < 15) ground = fmaxf(0, ground - 30);
  g->p.ground_y = g->p2.ground_y = ground;
  g->p.ceiling_y = g->p2.ceiling_y = ground + h * 30;
}
/* Modes whose gravity stays linked in dual (cube and wave, and each with itself). */
static bool linked(int a, int b) {
  bool ca = a == MODE_CUBE || a == MODE_WAVE, cb = b == MODE_CUBE || b == MODE_WAVE;
  return a == b || (ca && cb);
}
static void flip_other(Game *g) {
  Player *p = &g->p, *o = &g->p2;
  if (!g->dual || !linked(p->mode, o->mode) || p->upside != o->upside) return;
  o->upside = !p->upside;
  o->vy /= -2;
  o->ceil_inv = CEILING_INVUL;
}
static void portal_bounds(Game *g, float oy, float height) {
  if (g->dual) set_dual_bounds(g);
  else set_bounds_for_portal(g, oy, height);
}

static void special(Game *g, unsigned i, const RObj *o, bool hold, float ox, float oy) {
  Player *p = &g->p;
  const ObjDef *d = &objdefs[o->type];
  bool used = game_used(g, o, i);
  switch (d->special) {
    case SP_PAD_Y: case SP_PAD_P:
      if (used) break;
      p->vy = jump_value(p, d->special == SP_PAD_Y ? J_YPAD : J_PPAD);
      p->on_ground = false; p->inverse_rot = false; p->left_ground = true; p->jumped = true; p->hover = 0;
      set_used(g, i);
      update_rot_dir(p);
      fx(g, FX_PAD, d->special == SP_PAD_Y ? 0 : 2, (int)i, ox, oy);
      break;
    case SP_PAD_B: {
      if (used) break;
      int xf = obj_xf(o), rot = (xf & 3) * 90;
      if (xf < 0) rot = (int)(obj_rot(o) * 360 / 1024);
      if (xf >= 0 && (xf & 8)) rot = (rot + 180) % 360;
      bool down = rot > 90 && rot < 270;
      if ((!down && p->upside) || (down && !p->upside)) break;
      p->left_ground = true;
      update_rot_dir(p);
      p->vy = jump_value(p, J_BPAD);
      p->upside = !p->upside;
      flip_other(g);
      p->on_ground = false; p->inverse_rot = false; p->ceil_inv = CEILING_INVUL; p->jumped = true; p->hover = 0;
      set_used(g, i);
      fx(g, FX_PAD, 1, (int)i, ox, oy);
      break;
    }
    case SP_ORB_Y: case SP_ORB_P: case SP_ORB_B: case SP_ORB_G:
      if (!used && hold && p->buffer == BUF_READY) {
        int j = d->special == SP_ORB_P ? J_PORB : d->special == SP_ORB_B ? J_BORB : J_YORB;
        if (d->special == SP_ORB_G) {
          /* the green ring flips first, then launches (not halved) */
          p->upside = !p->upside;
          p->ceil_inv = CEILING_INVUL;
          flip_other(g);
        }
        float v = jump_value(p, j);
        if (d->special == SP_ORB_G && p->mode == MODE_SHIP) v *= 0.7f;
        if (p->mode != MODE_WAVE) p->vy = v;
        if (d->special == SP_ORB_B) { p->upside = !p->upside; p->ceil_inv = CEILING_INVUL; flip_other(g); }
        p->on_ground = false; p->on_ceiling = false; p->inverse_rot = false; p->left_ground = true;
        p->buffer = BUF_END; p->jumped = true; p->hover = 0;
        p->spin = -BALL_AIR_SPIN;
        update_rot_dir(p);
        g->jumps++;
        set_used(g, i);
        fx(g, FX_ORB, (uint8_t)(d->special == SP_ORB_G ? 3 : d->special - SP_ORB_Y), (int)i, ox, oy);
      }
      break;
    case SP_GRAV_N: case SP_GRAV_F:
      if (used) break;
      p->ceil_inv = CEILING_INVUL;
      if (p->upside != (d->special == SP_GRAV_F)) {
        p->vy /= -2;
        p->upside = d->special == SP_GRAV_F;
        p->inverse_rot = false;
        if (p->mode == MODE_UFO) p->rot = 0; else p->snap_rot = true;
        flip_other(g);
        p->left_ground = true;
        fx(g, FX_GRAVITY, d->special == SP_GRAV_F, (int)i, ox, oy);
      }
      set_used(g, i);
      break;
    case SP_PORTAL_CUBE: case SP_PORTAL_ROBOT: {
      if (used) break;
      int want = d->special == SP_PORTAL_CUBE ? MODE_CUBE : MODE_ROBOT;
      if (g->dual) set_dual_bounds(g);
      else { p->ground_y = 0; p->ceiling_y = 999999; }
      if (p->mode != want) {
        if (p->mode != MODE_BALL && p->mode != MODE_CUBE && p->mode != MODE_ROBOT) p->vy /= 2;
        if (p->mode == MODE_WAVE) p->vy *= 0.9f;
        p->ceil_inv = CEILING_INVUL;
        p->snap_rot = true;
        enter_mode(g, want);
        if (g->dual) set_dual_bounds(g);
        flip_other(g);
        update_rot_dir(p);
        fx(g, FX_PORTAL, want == MODE_CUBE ? 0 : 5, (int)i, ox, oy);
      }
      set_used(g, i);
      break;
    }
    case SP_PORTAL_BALL:
      if (used) break;
      portal_bounds(g, oy, BALL_HEIGHT);
      if (p->mode != MODE_BALL) {
        if (p->mode == MODE_WAVE) p->vy *= 0.45f;
        if (p->mode == MODE_SHIP || p->mode == MODE_UFO || p->mode == MODE_WAVE) {
          p->vy /= 2;
          if (hold && p->mode == MODE_SHIP) p->buffer = BUF_READY; /* holding through the portal flips at once */
        }
        enter_mode(g, MODE_BALL);
        if (g->dual) set_dual_bounds(g);
        p->spin = -BALL_AIR_SPIN;
        p->inverse_rot = false;
        p->snap_rot = true;
        flip_other(g);
        fx(g, FX_PORTAL, 2, (int)i, ox, oy);
      }
      set_used(g, i);
      break;
    case SP_PORTAL_SHIP:
      if (used) break;
      portal_bounds(g, oy, 300);
      if (p->mode != MODE_SHIP) {
        if (p->mode == MODE_WAVE) p->vy *= 0.9f;
        p->vy /= p->mode == MODE_UFO || p->mode == MODE_WAVE ? 4 : 2;
        enter_mode(g, MODE_SHIP);
        if (g->dual) set_dual_bounds(g);
        p->inverse_rot = false;
        p->snap_rot = true;
        flip_other(g);
        p->vy = p->mini ? clampf(p->vy, SHIP_MINI_MIN, SHIP_MINI_MAX) : clampf(p->vy, SHIP_MIN, SHIP_MAX);
        fx(g, FX_PORTAL, 1, (int)i, ox, oy);
      }
      set_used(g, i);
      break;
    case SP_PORTAL_UFO:
      if (used) break;
      portal_bounds(g, oy, 300);
      if (p->mode != MODE_UFO) {
        int was = p->mode;
        if (was == MODE_WAVE) p->vy *= 0.9f;
        p->vy /= was == MODE_SHIP || was == MODE_WAVE ? 4 : 2;
        enter_mode(g, MODE_UFO);
        if (g->dual) set_dual_bounds(g);
        p->inverse_rot = false;
        p->rot = 0;
        flip_other(g);
        /* coming from a flying mode, a held button flaps at once */
        if (was == MODE_SHIP || was == MODE_WAVE) p->buffer = BUF_READY;
        fx(g, FX_PORTAL, 3, (int)i, ox, oy);
      }
      set_used(g, i);
      break;
    case SP_PORTAL_WAVE:
      if (used) break;
      portal_bounds(g, oy, 300);
      if (p->mode != MODE_WAVE) {
        enter_mode(g, MODE_WAVE);
        if (g->dual) set_dual_bounds(g);
        p->inverse_rot = false;
        p->snap_rot = true;
        flip_other(g);
        fx(g, FX_PORTAL, 4, (int)i, ox, oy);
      }
      set_used(g, i);
      break;
    case SP_SIZE_MINI: case SP_SIZE_NORMAL:
      if (used) break;
      if (p->mini != (d->special == SP_SIZE_MINI)) {
        p->mini = d->special == SP_SIZE_MINI;
        fx(g, FX_SIZE, p->mini, (int)i, ox, oy);
      }
      set_used(g, i);
      break;
    case SP_SPEED_0: case SP_SPEED_1: case SP_SPEED_2: case SP_SPEED_3:
      if (used) break;
      p->speed = (uint8_t)(d->special - SP_SPEED_0);
      if (g->dual) g->p2.speed = p->speed;
      set_used(g, i);
      fx(g, FX_SPEED, p->speed, (int)i, ox, oy);
      break;
    case SP_DUAL_ON:
      if (used) break;
      set_used(g, i);
      if (!g->dual) {
        p->ceil_inv = CEILING_INVUL;
        g->dual = true;
        g->dual_y = oy;
        g->p2 = *p;
        g->p2.upside = !p->upside;
        g->p2.second = true;
        fx(g, FX_PORTAL, 6, (int)i, ox, oy);
      }
      set_dual_bounds(g);
      break;
    case SP_DUAL_OFF:
      if (used) break;
      set_used(g, i);
      if (g->dual) {
        g->dual = false;
        if (p->second) { bool sec = p->second; *p = g->p2; p->second = sec; }  /* the first body goes on */
        g->p2.second = true;
        if (p->mode == MODE_CUBE || p->mode == MODE_ROBOT) { p->ground_y = 0; p->ceiling_y = 999999; }
        else set_bounds_for_portal(g, g->dual_y, p->mode == MODE_BALL ? BALL_HEIGHT : 300);
        fx(g, FX_PORTAL, 7, (int)i, ox, oy);
      }
      break;
    case SP_TELEPORT: {
      if (used) break;
      set_used(g, i);
      const LStyle *st = obj_style(g->L, o);
      p->y = oy + st->arg;
      p->has_snap = false;
      p->left_ground = true;
      fx(g, FX_PORTAL, 8, (int)i, ox, oy);
      break;
    }
    case SP_KEY:
      if (used) break;
      set_used(g, i);
      fx(g, FX_COIN, 1, (int)i, ox, oy);
      break;
    case SP_TOUCH:
      trig_touch(g, (unsigned)obj_style(g->L, o)->arg);
      break;
    case SP_COIN:
      if (used) break;
      set_used(g, i);
      g->coins |= (uint8_t)(1u << (game_coin_index(g->L, i) & 7));
      fx(g, FX_COIN, 0, (int)i, ox, oy);
      break;
    default: break;
  }
}

#define MAX_NEAR 192
typedef struct { const RObj *o; uint32_t gi; float x, y; } Near;

/* Hitbox centre and half extents of an object at (x, y): its box turns with
   it, and a few objects carry their box off centre. */
static void hit_box(const Level *L, const RObj *o, float x, float y, float *cx, float *cy, float *hw, float *hh) {
  const ObjDef *d = &objdefs[o->type];
  obj_hitbox(o, hw, hh);
  if (obj_scale(o)) {
    float sx, sy;
    obj_scale_xy(L, o, &sx, &sy);
    if (obj_rot(o) & 256) { float t = sx; sx = sy; sy = t; }
    *hw *= fabsf(sx);
    *hh *= fabsf(sy);
  }
  *cx = x;
  *cy = y;
  if (d->hx2) {
    float off = d->hx2 * 0.5f, a = obj_rot(o) * (6.2831853f / 1024);
    if (obj_flips(o) & 1) off = -off;
    *cx += off * cosf(a);
    *cy -= off * sinf(a);
  }
}

static void collide(Game *g, const Player *old, bool hold) {
  Player *p = &g->p;
  /* objects around the player, in level order, where their groups put them */
  Near near[MAX_NEAR];
  unsigned n = 0;
  LIter it;
  level_iter(g->L, p->x - 15, p->x + 15, &it);
  for (const RObj *o; (o = level_next(&it)) && n < MAX_NEAR;) {
    if (objdefs[o->type].hit == HIT_NONE) continue;
    float x, y;
    if (!obj_where(g, o, &x, &y) || x < p->x - 60 || x >= p->x + 60) continue;
    near[n++] = (Near){o, it.gi, x, y};
  }
  bool touching_orb = false;
  float snap_x = p->x;   /* contacts are judged before any nudge */
  /* GD resolves special objects, then solids, then hazards. */
  for (int pass = 0; pass < 3 && !g->dead; pass++) {
    if (pass == 2 && (p->mode == MODE_CUBE || p->mode == MODE_ROBOT) && p->on_ground) {
      /* stair snap against every solid under the cube, in GD's order: by
         100 unit section, right to left then top to bottom, then newest */
      int touch[8], nt = 0;
      float ph = phalf(p), foot = p->y - grav(p, ph);
      for (unsigned k = 0; k < n && nt < 8; k++) {
        const Near *e = &near[k];
        if (objdefs[e->o->type].hit != HIT_SOLID || objdefs[e->o->type].shape == SHAPE_SLOPE) continue;
        float hw, hh, ox, oy;
        hit_box(g->L, e->o, e->x, e->y, &ox, &oy, &hw, &hh);
        if (fabsf(snap_x - ox) > hw + ph + 0.01f || fabsf(foot - (p->upside ? oy - hh : oy + hh)) > 0.6f) continue;
        int j = nt++;
        for (; j > 0; j--) {
          const Near *b = &near[touch[j - 1]];
          int sa = (int)floorf(e->x / 100), sb = (int)floorf(b->x / 100);
          if (sa == sb) { sa = (int)floorf(e->y / 100); sb = (int)floorf(b->y / 100); }
          if (sa == sb ? e->gi < b->gi : sa < sb) break;
          touch[j] = touch[j - 1];
        }
        touch[j] = (int)k;
      }
      for (int j = 0; j < nt; j++) snap_to(g, near[touch[j]].x, near[touch[j]].y);
    }
    for (unsigned k = 0; k < n && !g->dead; k++) {
      const RObj *o = near[k].o;
      unsigned i = near[k].gi;
      const ObjDef *d = &objdefs[o->type];
      int want = pass == 0 ? HIT_SPECIAL : pass == 1 ? HIT_SOLID : HIT_HAZARD;
      if (d->hit != want) continue;
      if ((d->special == SP_COIN || d->special == SP_KEY) && game_used(g, o, i)) continue;
      float hw, hh, ox, oy, ph = phalf(p);
      hit_box(g->L, o, near[k].x, near[k].y, &ox, &oy, &hw, &hh);
      if (d->shape == SHAPE_CIRCLE) {   /* w is the radius */
        if (!box_circle(p->x, p->y, ph, ox, oy, d->w10 * 0.1f * scale_of(g->L, o))) continue;
      } else if (obj_xf(o) < 0 && pass != 1) {
        /* turned by a free angle: the player's box turns too, and must also
           touch unturned (as GD) */
        float a = obj_rot(o) * (6.2831853f / 1024);
        float sx, sy;
        obj_scale_xy(g->L, o, &sx, &sy);
        hw = d->w10 * 0.05f * fabsf(sx); hh = d->h10 * 0.05f * fabsf(sy);
        if (!overlap(p->x, p->y, ph, ph, ox, oy, hw + hh, hw + hh) ||
            !obb_overlap(p->x, p->y, ph, ph, 0, ox, oy, hw, hh, a) ||
            !obb_overlap(p->x, p->y, ph, ph, p->rot * DEG, ox, oy, hw, hh, a)) continue;
      } else if (!overlap(p->x, p->y, ph, ph, ox, oy, hw, hh)) {
        /* a ring still counts where the player was a tick ago (gdsolver) */
        bool ring = (d->special >= SP_ORB_Y && d->special <= SP_ORB_B) || d->special == SP_ORB_G;
        if (!ring || !overlap(old->x, old->y, ph, ph, ox, oy, hw, hh)) continue;
      }
      if (pass == 0) {
        if ((d->special >= SP_ORB_Y && d->special <= SP_ORB_B) || d->special == SP_ORB_G) {
          touching_orb = true;
          if (!p->orb_touching && !game_used(g, o, i)) fx(g, FX_ORB_TOUCH, 0, (int)i, ox, oy);
        }
        special(g, i, o, hold, near[k].x, near[k].y);
      } else if (pass == 1) {
        if (d->shape == SHAPE_SLOPE) continue;   /* slopes: see slope_pass */
        solid(g, i, ox, oy, hw, hh);
      } else {
        kill(g, (int)i);
      }
    }
  }
  p->orb_touching = touching_orb;
}

static void cube_mode(Game *g, bool hold, bool pressed) {
  Player *p = &g->p;
  float mult = p->rot_dir_neg ? -1.0f : 1.0f, spin = p->mini ? ROT_SPEED_MINI : ROT_SPEED;
  p->gravity = CUBE_GRAV[p->speed];
  if (p->vy < -810) p->vy = -810;
  if (p->vy > 1080) p->vy = 1080;
  if (p->y > 2794) kill(g, -1);
  if (p->snap_rot) p->target_rot = p->rot;
  if (!p->on_ground) {
    if (p->inverse_rot) p->target_rot -= spin / 2 * ND_DT * mult;
    else p->target_rot += spin * ND_DT * mult;
  }
  if (p->on_ground) update_rot_dir(p);
  bool coyote = p->upside && hold && p->coyote < 10;
  if ((p->on_ground || coyote) && hold) {
    set_velocity(p, CUBE_JUMP[p->speed], g->hold_prev);
    p->inverse_rot = false;
    p->buffer = BUF_END;
    p->on_ground = false;
    p->jumped = true;
    g->jumps++;
    if (!pressed) p->time_since_ground = DRAG_TIME;
    fx(g, FX_JUMP, 0, -1, p->x, p->y);
  }
  if (p->on_ground) p->target_rot = closest_rotation(p->rot);
}

static void ship_mode(Game *g, bool hold) {
  Player *p = &g->p;
  float t = grav(p, VEL_THRESH[p->speed]);
  bool m = p->mini;
  if (hold) {
    p->buffer = BUF_END;
    p->gravity = p->vy <= t ? (m ? 1643.5872f : 1397.0491f) : (m ? 1314.86976f : 1117.64328f);
  } else {
    p->gravity = p->vy >= t ? (m ? -1577.85408f : -1341.1719f) : (m ? -1051.8984f : -894.11464f);
  }
  float lo = m ? SHIP_MINI_MIN : SHIP_MIN, hi = m ? SHIP_MINI_MAX : SHIP_MAX;
  if (p->gravity < 0 && p->vy < lo) p->vy = lo;
  else if (p->gravity > 0 && p->vy > hi) p->vy = hi;
}

/* UFO: a press flaps up to a fixed speed (a held button also flaps right
   after entering from the cube, ship or wave, or after a respawn). */
static void ufo_mode(Game *g, const Player *old, bool pressed, bool hold) {
  Player *p = &g->p;
  bool m = p->mini;
  bool carry = hold && (old->mode == MODE_CUBE || old->mode == MODE_SHIP || old->mode == MODE_WAVE || p->ufo_buf);
  if (p->buffer == BUF_READY && (pressed || carry)) {
    p->vy = fmaxf(p->vy, UFO_FLAP[m]);
    p->buffer = BUF_END;
    p->vel_override = true;
    g->jumps++;
    fx(g, FX_JUMP, 1, -1, p->x, p->y);
  } else {
    p->gravity = p->vy > grav(p, VEL_THRESH[p->speed]) ? UFO_GRAV_HI[m] : UFO_GRAV_LO[m];
  }
  float lo = m ? SHIP_MINI_MIN : SHIP_MIN, hi = m ? SHIP_MINI_MAX : SHIP_MAX;
  p->vy = clampf(p->vy, lo, hi);
}

/* Robot (GD 2.2, read by gdsolver): 0.9 of the cube's gravity, half its
   jump, and a hover while the button stays held after a jump (at most 67
   ticks; letting go ends it). Only a fresh press jumps. */
static void robot_mode(Game *g, bool hold, bool pressed) {
  Player *p = &g->p;
  p->gravity = CUBE_GRAV[p->speed] * 0.9f;
  if (p->vy < -810) p->vy = -810;
  if (p->vy > 1080) p->vy = 1080;
  if (!hold) p->hover = 0;
  bool coyote = p->upside && hold && p->coyote < 10;
  if ((p->on_ground || coyote) && p->buffer == BUF_READY) {
    set_velocity(p, CUBE_JUMP[p->speed] * 0.5f, g->hold_prev);
    p->hover = 67;
    p->buffer = BUF_END;
    p->on_ground = false;
    p->jumped = true;
    g->jumps++;
    if (!pressed) p->time_since_ground = DRAG_TIME;
    fx(g, FX_JUMP, 2, -1, p->x, p->y);
  } else if (p->hover && hold && !p->on_ground) {
    p->hover--;
    p->gravity = 0;
  }
  p->rot = 0;
}

/* Wave: straight lines at 45 degrees (twice as steep when mini). */
static void wave_mode(Game *g, bool hold) {
  Player *p = &g->p;
  if (p->buffer == BUF_READY) p->buffer = BUF_END;
  p->gravity = 0;
  p->vy = (hold ? 1.f : -1.f) * SPEEDS[p->speed] * (p->mini ? 2 : 1);
}

/* A press on a floor or ceiling flips gravity, with a small push towards the
   other side (as in GD, a press just before landing counts: the buffer). */
static void ball_mode(Game *g, const Player *old) {
  Player *p = &g->p;
  p->gravity = BALL_GRAV;
  if (p->on_ground || p->on_ceiling) p->spin = 1;
  bool coyote = p->upside && p->coyote < 16;
  if ((p->on_ground || p->on_ceiling || coyote) && p->buffer == BUF_READY) {
    p->upside = !p->upside;
    set_velocity(p, BALL_JUMP[p->speed], old->buffer == BUF_READY);
    p->buffer = BUF_END;
    p->spin = -BALL_AIR_SPIN;
    p->on_ground = false;
    p->jumped = true;
    g->jumps++;
    fx(g, FX_JUMP, 0, -1, p->x, p->y);
  }
  if (p->vy < -810) p->vy = -810;
  if (p->vy > 810) p->vy = 810;
}

static void run_player(Game *g, const Player *old, bool hold, bool pressed) {
  Player *p = &g->p;
  float rest = rest_half(p);
  if (!p->left_ground) {
    if (p->y - rest <= p->ground_y) {
      if (p->upside) p->on_ceiling = true; else { p->on_ground = true; landing(p); }
      p->inverse_rot = false;
      p->time_since_ground = 0;
    }
    if (p->y + rest >= p->ceiling_y) {
      if (p->upside) { p->on_ground = true; landing(p); } else p->on_ceiling = true;
      p->inverse_rot = false;
      p->time_since_ground = 0;
    }
  }
  if (!old->on_ground && p->on_ground) fx(g, FX_LAND, 0, -1, p->x, p->y);
  if (grav_bottom(old) > grav_floor(old) && p->upside == old->upside && !p->on_ground && p->vy <= 0) {
    if (old->on_ground && !g->hold_prev) p->coyote = 0;
    p->coyote++;
  } else {
    p->coyote = 1 << 30;
  }
  if (p->mode == MODE_CUBE) cube_mode(g, hold, pressed);
  else if (p->mode == MODE_BALL) ball_mode(g, old);
  else if (p->mode == MODE_UFO) ufo_mode(g, old, pressed, hold);
  else if (p->mode == MODE_ROBOT) robot_mode(g, hold, pressed);
  else if (p->mode == MODE_WAVE) wave_mode(g, hold);
  else ship_mode(g, hold);
  p->time_since_ground += ND_DT;
  if (!p->vel_override) {
    float nv = p->vy + p->gravity * ND_DT;
    if (!(p->on_ground || p->on_ceiling) && (old->on_ground || old->on_ceiling) &&
        ((!hold && g->pressed_prev) || p->buffer == BUF_READY) && grav_bottom(old) > grav_floor(old) && p->mini == old->mini) {
      p->y += grav(old, old->gravity) * ND_DT * ND_DT;
      if (p->vy == 0) nv += old->gravity * ND_DT;
    }
    /* GD 2.2 keeps y velocity on a grid of 0.001 of its own unit (54 units
       per second), rounded inside the gravity step (measured by gdsolver) */
    p->vy = roundf(nv * (1000 / 54.f)) * (54 / 1000.f);
  }
  if (g->ending) return;
  p->rot = fmodf(p->rot, 360.0f);
  p->left_ground = false;
  if (p->ceil_inv > 0) p->ceil_inv -= ND_DT; else p->ceil_inv = 0;
  clamp_ground(g);
  if (p->mode == MODE_CUBE) {
    float lerp = SPEED_MULT[p->speed] * 0.175f;
    if (p->on_ground) {
      lerp *= 3;
      float t = fminf(ND_DT, ND_DT * lerp) * 60;
      p->rot = slerp_angle(p->rot * DEG, p->target_rot * DEG, t) / DEG;
    } else {
      p->rot = p->target_rot;
    }
  } else if (p->mode == MODE_BALL) {
    p->rot += p->spin * BALL_ROLL[p->speed] * (p->mini ? 1.25f : 1) * (p->upside ? -1 : 1) * ND_DT;
  } else if (p->mode == MODE_ROBOT) {
    p->rot = 0;
  } else if (p->mode == MODE_WAVE) {
    /* the dart eases towards its travel angle (gdsolver's fit of GD) */
    float dy = p->y - old->y, target = fabsf(dy) < 1e-4f ? 0 : (p->mini ? 63.435f : 45.f) * (dy < 0 ? 1 : -1);
    float r = fmodf(p->rot + 540, 360) - 180;
    p->rot = r + (p->mini ? 0.1f : 0.0625f) * (target - r);
  } else {
    float dx = p->x - old->x, dy = p->y - old->y, ang = atan2f(-dy, dx);
    if (p->snap_rot) p->rot = ang / DEG;
    else if (ND_DT * 72 <= dx * dx + dy * dy) {
      float k = 0.15f;
      if (p->mode == MODE_UFO) {  /* the UFO only tilts a little */
        k = 0.07f;
        ang = p->on_ground ? 0 : p->upside ? fminf(ang * -0.4f, 0.1f) : fmaxf(ang * -0.4f, -0.1f);
      }
      p->rot = slerp_angle(p->rot * DEG, ang, ND_DT * 60 * k) / DEG;
    }
  }
  p->snap_rot = false;
  p->ufo_buf = false;
}

static void ease_channel(Channel *c) {
  if (c->t >= c->dur) { memcpy(c->cur, c->to, 3); return; }
  c->t++;
  for (int k = 0; k < 3; k++) c->cur[k] = (uint8_t)(c->from[k] + ((int)c->to[k] - c->from[k]) * (int)c->t / (int)c->dur);
}
static void fire_event(Game *g, const LevelEvent *e) {
  if (e->kind == EV_COLOR) {
    int chans[2] = {e->arg, -1};
    if (e->arg == CH_BG && (e->flags & EVF_TINT_GROUND)) chans[1] = CH_G1;
    for (int k = 0; k < 2; k++) {
      if (chans[k] < 0 || chans[k] >= CH_COUNT) continue;
      Channel *c = &g->ch[chans[k]];
      memcpy(c->from, c->cur, 3);
      c->to[0] = e->r; c->to[1] = e->g; c->to[2] = e->b;
      c->t = 0;
      c->dur = (uint16_t)((uint32_t)e->dur * ND_HZ / 1000);
      if (!c->dur) memcpy(c->cur, c->to, 3);
    }
  } else if (e->kind == EV_FADE) {
    g->fade_effect = e->arg;
  } else if (e->kind == EV_TRAIL) {
    g->trail = e->arg != 0;
  }
}
static void triggers(Game *g) {
  const Level *L = g->L;
  Player *p = &g->p;
  while (g->next_event < L->event_count) {
    const LevelEvent *e = &L->events[g->next_event];
    if (e->x >= p->x) break;
    if (!(e->flags & EVF_TOUCH)) fire_event(g, e);
    g->next_event++;
  }
  /* touch-triggered events fire once when the player overlaps them */
  for (unsigned k = 0; k < L->event_count && k < 64; k++) {
    const LevelEvent *e = &L->events[k];
    if (!(e->flags & EVF_TOUCH) || (g->touch_done[k >> 3] >> (k & 7) & 1)) continue;
    if (e->x > p->x + 30 || e->x < p->x - 60) continue;
    if (overlap(p->x, p->y, 15, 15, e->x, e->y, 15, 15)) {
      g->touch_done[k >> 3] |= (uint8_t)(1u << (k & 7));
      fire_event(g, e);
    }
  }
  for (int c = 0; c < CH_COUNT; c++) ease_channel(&g->ch[c]);
}

static float ease_in_out(float t, float rate) {
  t *= 2;
  if (t < 1) return 0.5f * powf(t, rate);
  return 1 - 0.5f * powf(2 - t, rate);
}

static void camera(Game *g) {
  Player *p = &g->p;
  const Level *L = g->L;
  if (g->menu_camera) {
    float v = SPEEDS[SPEED_NORMAL] * ND_DT;
    g->ground_x += v;
    g->bg_x += v;
    g->ground_gfx = 0;
    return;
  }
  float playable = p->ceiling_y - p->ground_y, want_gfx = 0;
  if (p->mode != MODE_CUBE) want_gfx = (VIEW_H - playable) / 2;
  g->ground_gfx += (want_gfx - g->ground_gfx) * 0.02f;
  if (g->wall_y == 0 && g->cam_x + VIEW_W >= L->wall_x - 4.5f * 30) {
    float mid = g->cam_y + VIEW_H / 2 - GROUND_OFFSET, lo = 60 + (VIEW_H / 2 - GROUND_OFFSET);
    g->wall_y = mid > lo ? mid : lo;
  }
  if (g->wall_y > 0 && g->cam_x + VIEW_W >= L->wall_x - 60) {
    if (g->cam_wall_t == 0) { g->cam_wall_y0 = g->cam_y; g->bg_wall_x0 = g->bg_x; g->ground_wall_x0 = g->ground_x; }
    float t = clampf(g->cam_wall_t / 1.0f, 0, 1), e = ease_in_out(t, 2);
    float fx_ = L->wall_x - VIEW_W, fy = g->wall_y - (VIEW_H / 2 - GROUND_OFFSET);
    g->cam_x = fx_ - 60 + 60 * e;
    g->cam_y = g->cam_wall_y0 + (fy - g->cam_wall_y0) * e;
    g->bg_x = g->bg_wall_x0 + 60 * e;
    g->ground_x = g->ground_wall_x0 + 60 * e;
    g->cam_wall_t += ND_DT;
    return;
  }
  float target = g->cam_y;
  if (p->mode == MODE_CUBE) {
    float off = p->upside ? -30.0f : 0;
    if (p->y > g->cam_y + 180 + off) target = p->y - (180 + off);
    else if (p->y < g->cam_y + 30 + off) target = p->y - (30 + off);
  } else {
    target = g->cam_intended_y;
  }
  if (target < 0) target = 0;
  g->cam_y += (target - g->cam_y) / 40.0f;
  if (g->cam_y < 0) g->cam_y = 0;
  float want = p->x - PLAYER_SCREEN_X;
  if (g->attempt_camera && want < 15) want = 15;
  float moved = want - g->cam_x;
  g->cam_x = want;
  if (moved > 0) { g->ground_x += moved; g->bg_x += moved; }
}

void game_step(Game *g, bool hold) {
  if (g->dead || g->complete) return;
  Player *p = &g->p;
  Player old = *p;
  bool pressed = hold && !g->hold_prev;
  p->old_on_ground = p->on_ground;
  if (hold) { if (p->buffer == BUF_NONE) p->buffer = BUF_READY; }
  else p->buffer = BUF_NONE;
  p->on_ground = p->on_ceiling = false;
  p->vel_override = false;
  p->x += SPEEDS[p->speed] * ND_DT;
  p->y += grav(p, p->vy) * ND_DT;
  clamp_ground(g);
  p->frame++;
  g->tick++;
  if (!g->dead && !g->ending) collide(g, &old, hold);
  if (!g->dead) {
    const Level *L = g->L;
    if (p->x >= L->wall_x - END_START) {
      if (!g->ending) {
        g->ending = true;
        g->end_x0 = p->x;
        g->end_y0 = p->y;
        if (g->wall_y == 0) g->wall_y = g->end_y0 > 60 ? g->end_y0 : 60;
      }
      float t = clampf(powf(g->end_t, 1.2f), 0, 1), u = 1 - t;
      float mx = g->end_x0 + 40, my = g->wall_y + 150, ex = L->wall_x + 50, ey = g->wall_y - 20;
      p->x = u * u * u * g->end_x0 + 3 * u * u * t * g->end_x0 + 3 * u * t * t * mx + t * t * t * ex;
      p->y = u * u * u * g->end_y0 + 3 * u * u * t * g->end_y0 + 3 * u * t * t * my + t * t * t * ey;
      float e = g->end_t / 0.5f;
      p->rot += (e > 1 ? 1 : e * e) * ROT_SPEED * ND_DT;
      g->end_t += ND_DT;
      if (p->x > L->wall_x && !g->complete) {
        g->complete = true;
        fx(g, FX_WALL, 0, -1, (float)L->wall_x, g->wall_y);
      }
    }
    run_player(g, &old, hold, pressed);
  }
  g->hold_prev = hold;
  g->pressed_prev = pressed;
  if (!g->dead) {
    camera(g);
    triggers(g);
    trig_step(g, p->x - old.x);
  }
}

void game_save_checkpoint(const Game *g, Checkpoint *c) {
  c->p = g->p;
  c->cam_x = g->cam_x; c->cam_y = g->cam_y; c->ground_x = g->ground_x; c->bg_x = g->bg_x;
  c->ground_gfx = g->ground_gfx; c->cam_intended_y = g->cam_intended_y;
  memcpy(c->ch, g->ch, sizeof(c->ch));
  c->tick = g->tick; c->next_event = g->next_event; c->jumps = g->jumps;
  c->fade_effect = g->fade_effect; memcpy(c->touch_done, g->touch_done, sizeof(c->touch_done)); c->trail = g->trail;
}

void game_load_checkpoint(Game *g, const Checkpoint *c) {
  const Level *L = g->L;
  uint8_t coins = g->coins;
  game_start(g, L, false);
  g->p = c->p;
  g->p.buffer = BUF_NONE;
  g->p.ufo_buf = true;
  g->cam_x = c->cam_x; g->cam_y = c->cam_y; g->ground_x = c->ground_x; g->bg_x = c->bg_x;
  g->ground_gfx = c->ground_gfx; g->cam_intended_y = c->cam_intended_y;
  memcpy(g->ch, c->ch, sizeof(c->ch));
  g->tick = c->tick; g->next_event = c->next_event; g->jumps = c->jumps;
  g->fade_effect = c->fade_effect; g->trail = c->trail;
  memcpy(g->touch_done, c->touch_done, sizeof(g->touch_done));
  g->coins = coins;
  /* Objects behind the checkpoint can no longer be reached; they count as
   * used so coins and pads there do not fire again. */
  g->used_below = c->p.x - 60;
}
