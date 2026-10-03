/* The camera: CameraTarget follows the Knight (looking ahead, catching falls), CameraController follows the target
 * (looking up and down), both inside the scene and the room's lock areas. */
#include <math.h>
#include "game.h"

#define DT 0.02f
#define X_MIN 14.6f
#define Y_MIN 8.3f
/* CameraTarget */
#define DAMP_NORMAL 0.075f
#define DAMP_SLOW 0.5f
#define SLOW_TIME 0.5f
#define X_LOOK_AHEAD 1.0f
#define DASH_LOOK_AHEAD 1.5f
#define SNAP 0.15f
/* CameraController */
#define DAMP_TIME 0.15f

enum { TM_FOLLOW_HERO, TM_LOCK_ZONE, TM_BOSS, TM_FREE };
enum { CM_FROZEN, CM_FOLLOWING, CM_LOCKED, CM_PANNING, CM_FADEOUT, CM_FADEIN, CM_PREVIOUS };

static struct {
  /* the target */
  int tmode;
  float tx, ty, vx_x, vy_y, x_offset, dash_offset, damp_x, damp_y, slow_timer, fall_catcher, prev_hx, prev_hy;
  float lock_x0, lock_x1, lock_y0, lock_y1;
  bool stick_x, stick_y, fall_stick;
  bool entered_left, entered_right, entered_top, entered_bot, exited_left, exited_right, exited_top, exited_bot;
  /* the camera */
  int mode;
  float cx, cy, cvx, cvy, cdamp_x, cdamp_y, look_offset, x_limit, y_limit, start_locked_timer;
  float x_lock0, x_lock1, y_lock0, y_lock1;
  int8_t locks[8], nlocks, cur_lock;   /* lockZoneList, currentLockArea (entities) */
} c;

/* Mathf.SmoothDamp */
static float smooth_damp(float cur, float target, float *vel, float t, float dt) {
  if (t < 0.0001f) t = 0.0001f;
  float omega = 2 / t, x = omega * dt, e = 1 / (1 + x + 0.48f * x * x + 0.235f * x * x * x);
  float change = cur - target, orig = target;
  target = cur - change;
  float temp = (*vel + omega * change) * dt;
  *vel = (*vel - omega * temp) * e;
  float out = target + (change + temp) * e;
  if ((orig - cur > 0) == (out > orig)) out = orig, *vel = (out - orig) / dt;
  return out;
}

static void set_damp_time(void) {
  if (c.slow_timer > 0) {
    c.slow_timer -= DT;
    return;
  }
  if (c.damp_x > DAMP_NORMAL) c.damp_x -= 0.007f;
  else if (c.damp_x < DAMP_NORMAL) c.damp_x = DAMP_NORMAL;
  if (c.damp_y > DAMP_NORMAL) c.damp_y -= 0.007f;
  else if (c.damp_y < DAMP_NORMAL) c.damp_y = DAMP_NORMAL;
}

static void keep_in_scene(float *x, float *y) {
  if (*x < X_MIN) *x = X_MIN;
  if (*x > c.x_limit) *x = c.x_limit;
  if (*y < Y_MIN) *y = Y_MIN;
  if (*y > c.y_limit) *y = c.y_limit;
}

/* the CameraShake FSM (on the camera's parent): a shake of higher priority replaces the one going on; ShakePositionV2
 * moves the camera at random, less and less */
static struct {
  float ex, time, t, priority;
  float dx, dy;
  uint8_t rumble;   /* (Rumbling*: a looping shake, whenever no other) */
} sh;

void cam_shake(int kind) {
  /* extents, time, priority: EnemyKillShake, AverageShake, BigShake, SmallShake */
  static const float shakes[][3] = {{0, 0, 0}, {0.105f, 0.5f, 6}, {0.15f, 1, 7}, {0.5f, 1, 10}, {0.08f, 0.5f, 3}};
  if (kind <= 0 || kind > SHAKE_SMALL || shakes[kind][2] <= sh.priority) return;
  sh.ex = shakes[kind][0], sh.time = shakes[kind][1], sh.priority = shakes[kind][2], sh.t = 0;
}

void cam_rumble(int kind) { sh.rumble = (uint8_t)kind; }

static void shake_tick(void) {
  sh.dx = sh.dy = 0;
  if (sh.priority <= 0) {
    static const float rumbles[] = {0, 0.08f, 0.15f, 0.5f};   /* SmallShake, AverageShake, BigShake */
    if (sh.rumble) sh.dx = rumbles[sh.rumble] * rand_range(-1, 1), sh.dy = rumbles[sh.rumble] * rand_range(-1, 1);
    return;
  }
  sh.t += 0.02f * g_game.time_scale;
  float k = 1 - sh.t / sh.time;
  if (k < 0) k = 0;
  sh.dx = sh.ex * rand_range(-1, 1) * k, sh.dy = sh.ex * rand_range(-1, 1) * k;
  if (sh.t > sh.time) sh.priority = 0, sh.dx = sh.dy = 0;
}

void cam_init(void) {
  const Hero *h = &g_hero;
  memset(&c, 0, sizeof c);
  memset(&sh, 0, sizeof sh);   /* (New Scene Reset) */
  c.x_limit = g_room.h->w - X_MIN, c.y_limit = g_room.h->h - Y_MIN;
  if (c.x_limit < X_MIN) c.x_limit = X_MIN;
  if (c.y_limit < Y_MIN) c.y_limit = Y_MIN;
  c.tmode = TM_FOLLOW_HERO;
  c.stick_x = c.stick_y = true;
  c.lock_x0 = 0, c.lock_x1 = c.x_limit, c.lock_y0 = 0, c.lock_y1 = c.y_limit;
  c.damp_x = c.damp_y = DAMP_NORMAL;
  c.mode = CM_FOLLOWING;
  c.cdamp_x = c.cdamp_y = DAMP_TIME;
  c.start_locked_timer = 0.5f;
  c.cur_lock = -1;
  c.x_lock0 = 0, c.x_lock1 = c.x_limit, c.y_lock0 = 0, c.y_lock1 = c.y_limit;
  /* PositionToStart */
  c.x_offset = h->cs.facing_right ? 1.0f : -1.0f;
  c.tx = h->body.x, c.ty = h->body.y;
  keep_in_scene(&c.tx, &c.ty);
  c.cx = c.tx + c.x_offset, c.cy = c.ty;
  keep_in_scene(&c.cx, &c.cy);
  c.prev_hx = h->body.x, c.prev_hy = h->body.y;
  g_cam_x = c.cx, g_cam_y = c.cy;
}

/* ---------------------------------------------------------------- lock areas */
static void lock_bounds(const Ent *e, float *x0, float *y0, float *x1, float *y1) {
  /* (CameraLockArea.ValidateBounds) */
  *x0 = e->p0 == -1 ? X_MIN : e->p0, *y0 = e->p1 == -1 ? Y_MIN : e->p1;
  *x1 = e->p2 == -1 ? c.x_limit : e->p2, *y1 = e->p3 == -1 ? c.y_limit : e->p3;
}

static void target_enter_lock_zone(float x0, float x1, float y0, float y1) {
  const Hero *h = &g_hero;
  c.lock_x0 = x0, c.lock_x1 = x1, c.lock_y0 = y0, c.lock_y1 = y1;
  c.tmode = TM_LOCK_ZONE;
  if ((!c.entered_left || x0 != 14.6f) && (!c.entered_right || x1 != c.x_limit)) c.damp_x = DAMP_SLOW;
  if ((!c.entered_bot || y0 != 8.3f) && (!c.entered_top || y1 != c.y_limit)) c.damp_y = DAMP_SLOW;
  c.slow_timer = SLOW_TIME;
  c.stick_x = c.tx >= h->body.x - SNAP && c.tx <= h->body.x + SNAP;
  c.stick_y = c.ty >= h->body.y - SNAP && c.ty <= h->body.y + SNAP;
}

static void target_enter_lock_zone_instant(float x0, float x1, float y0, float y1) {
  c.lock_x0 = x0, c.lock_x1 = x1, c.lock_y0 = y0, c.lock_y1 = y1;
  c.tmode = TM_LOCK_ZONE;
  if (c.tx < x0) c.tx = x0;
  if (c.tx > x1) c.tx = x1;
  if (c.ty < y0) c.ty = y0;
  if (c.ty > y1) c.ty = y1;
  c.stick_x = c.stick_y = true;
}

static void target_exit_lock_zone(void) {
  const Hero *h = &g_hero;
  if (c.tmode == TM_FREE) return;
  c.tmode = h->cs.hazard_death || h->cs.dead ||
                    (h->transition_state != TS_WAITING_TO_TRANSITION && h->transition_state != TS_WAITING_TO_ENTER_LEVEL)
                ? TM_FREE
                : TM_FOLLOW_HERO;
  if ((!c.exited_left || c.lock_x0 != 14.6f) && (!c.exited_right || c.lock_x1 != c.x_limit)) c.damp_x = DAMP_SLOW;
  if ((!c.exited_bot || c.lock_y0 != 8.3f) && (!c.exited_top || c.lock_y1 != c.y_limit)) c.damp_y = DAMP_SLOW;
  c.slow_timer = SLOW_TIME;
  c.fall_stick = false;
  c.lock_x0 = 0, c.lock_x1 = c.x_limit, c.lock_y0 = 0, c.lock_y1 = c.y_limit;
  c.stick_x = c.tx >= h->body.x - SNAP && c.tx <= h->body.x + SNAP;
  c.stick_y = c.ty >= h->body.y - SNAP && c.ty <= h->body.y + SNAP;
}

static float clampf(float v, float a, float b) { return v < a ? a : v > b ? b : v; }

void cam_lock(int ent) {
  int n;
  const Ent *es = room_ents(&n), *e = &es[ent];
  for (int i = 0; i < c.nlocks; i++)
    if (c.locks[i] == ent) return;   /* (already in the list: Stay) */
  /* (OnTriggerEnter2D: which side the Knight came in by) */
  const Hero *h = &g_hero;
  float hx = h->body.x, hy = h->body.y;
  c.entered_left = hx > e->x0 - 1 && hx < e->x0 + 1;
  c.entered_right = hx > e->x1 - 1 && hx < e->x1 + 1;
  c.entered_top = hy > e->y1 - 2 && hy < e->y1 + 2;
  c.entered_bot = hy > e->y0 - 1 && hy < e->y0 + 1;
  if (c.nlocks < 8) c.locks[c.nlocks++] = (int8_t)ent;
  if (c.cur_lock >= 0 && (es[c.cur_lock].flags & CL_MAX_PRIORITY) && !(e->flags & CL_MAX_PRIORITY)) return;
  c.cur_lock = (int8_t)ent;
  c.mode = CM_LOCKED;
  lock_bounds(e, &c.x_lock0, &c.y_lock0, &c.x_lock1, &c.y_lock1);
  if (c.start_locked_timer > 0) {
    c.tx = clampf(hx, c.x_lock0, c.x_lock1), c.ty = clampf(hy, c.y_lock0, c.y_lock1);
    target_enter_lock_zone_instant(c.x_lock0, c.x_lock1, c.y_lock0, c.y_lock1);
    c.cx = c.tx, c.cy = c.ty;
  } else
    target_enter_lock_zone(c.x_lock0, c.x_lock1, c.y_lock0, c.y_lock1);
}

void cam_release(int ent) {
  int n;
  const Ent *es = room_ents(&n), *e = &es[ent];
  int j = 0;
  for (int i = 0; i < c.nlocks; i++)
    if (c.locks[i] != ent) c.locks[j++] = c.locks[i];
  bool was_listed = j != c.nlocks;
  c.nlocks = (int8_t)j;
  if (!was_listed) return;
  const Hero *h = &g_hero;
  float hx = h->body.x, hy = h->body.y;
  c.exited_left = hx > e->x0 - 1 && hx < e->x0 + 1;
  c.exited_right = hx > e->x1 - 1 && hx < e->x1 + 1;
  c.exited_top = hy > e->y1 - 2 && hy < e->y1 + 2;
  c.exited_bot = hy > e->y0 - 1 && hy < e->y0 + 1;
  if (ent != c.cur_lock) return;
  if (c.nlocks > 0) {
    c.cur_lock = c.locks[c.nlocks - 1];
    const Ent *l = &es[c.cur_lock];
    /* (as the game: the raw values) */
    c.x_lock0 = l->p0, c.y_lock0 = l->p1, c.x_lock1 = l->p2, c.y_lock1 = l->p3;
    target_enter_lock_zone(c.x_lock0, c.x_lock1, c.y_lock0, c.y_lock1);
    return;
  }
  target_exit_lock_zone();
  c.cur_lock = -1;
  if (!h->cs.hazard_death && !h->cs.dead) c.mode = CM_FOLLOWING;
}

/* CameraTarget.Update */
static void target_update(void) {
  const Hero *h = &g_hero;
  float hx = h->body.x, hy = h->body.y;
  if (c.tmode == TM_FOLLOW_HERO || c.tmode == TM_LOCK_ZONE) {
    set_damp_time();
    float dx = hx, dy = hy;
    bool lock = c.tmode == TM_LOCK_ZONE;
    if (lock) {
      if (dx < c.lock_x0) dx = c.lock_x0;
      if (dx > c.lock_x1) dx = c.lock_x1;
      if (dy < c.lock_y0) dy = c.lock_y0;
      if (dy > c.lock_y1) dy = c.lock_y1;
    }
    float nx = smooth_damp(c.tx, dx, &c.vx_x, c.damp_x, DT);
    if (!c.fall_stick && c.fall_catcher <= 0) c.ty = smooth_damp(c.ty, dy, &c.vy_y, c.damp_y, DT);
    c.tx = nx;
    float x = c.tx, y = c.ty;
    if ((c.prev_hx < x && hx > x) || (c.prev_hx > x && hx < x) || (x >= hx - SNAP && x <= hx + SNAP)) c.stick_x = true;
    if ((c.prev_hy < y && hy > y) || (c.prev_hy > y && hy < y) || (y >= hy - SNAP && y <= hy + SNAP)) c.stick_y = true;
    if (c.stick_x && (!lock || (hx >= c.lock_x0 && hx <= c.lock_x1) || (hx <= c.lock_x1 && hx >= x) || (hx >= c.lock_x0 && hx <= x)))
      c.tx = hx;
    if (c.stick_y && (!lock || (hy >= c.lock_y0 && hy <= c.lock_y1) || (hy <= c.lock_y1 && hy >= y) || (hy >= c.lock_y0 && hy <= y)))
      c.ty = hy;
  }
  /* looking ahead */
  if (h->cs.facing_right) {
    if (c.x_offset < X_LOOK_AHEAD) c.x_offset += DT * 6;
  } else if (c.x_offset > -X_LOOK_AHEAD)
    c.x_offset -= DT * 6;
  if (c.x_offset < -X_LOOK_AHEAD) c.x_offset = -X_LOOK_AHEAD;
  if (c.x_offset > X_LOOK_AHEAD) c.x_offset = X_LOOK_AHEAD;
  if (c.tmode == TM_LOCK_ZONE) {
    if (hx < c.lock_x0 && h->cs.facing_right) c.x_offset = hx - c.tx + 1;
    if (hx > c.lock_x1 && !h->cs.facing_right) c.x_offset = hx - c.tx - 1;
    if (c.tx + c.x_offset > c.lock_x1) c.x_offset = c.lock_x1 - c.tx;
    if (c.tx + c.x_offset < c.lock_x0) c.x_offset = c.lock_x0 - c.tx;
  }
  if (c.x_offset < -X_LOOK_AHEAD) c.x_offset = -X_LOOK_AHEAD;
  if (c.x_offset > X_LOOK_AHEAD) c.x_offset = X_LOOK_AHEAD;
  if (h->cs.dashing && fabsf(h->body.vx) > 5) {
    c.dash_offset = h->cs.facing_right ? DASH_LOOK_AHEAD : -DASH_LOOK_AHEAD;
    if (c.tmode == TM_LOCK_ZONE &&
        (c.tx + c.dash_offset > c.lock_x1 || c.tx + c.dash_offset < c.lock_x0 || hx > c.lock_x1 || hx < c.lock_x0))
      c.dash_offset = 0;
  } else
    c.dash_offset = 0;
  c.prev_hx = hx, c.prev_hy = hy;
  if (!h->cs.falling) c.fall_catcher = 0, c.fall_stick = false;
  /* catching up with a fall */
  if (c.tmode == TM_FOLLOW_HERO || c.tmode == TM_LOCK_ZONE) {
    if (h->cs.falling && c.cy > hy + 0.1f && !c.fall_stick && !h->cs.transitioning &&
        (c.cy - 0.1f >= c.lock_y0 || c.tmode != TM_LOCK_ZONE)) {
      c.cy -= c.fall_catcher * DT;
      if (c.tmode == TM_LOCK_ZONE && c.cy < c.lock_y0) c.cy = c.lock_y0;
      if (c.cy < Y_MIN) c.cy = Y_MIN;
      if (c.fall_catcher < 25) c.fall_catcher += 80 * DT;
      if (c.cy < hy + 0.1f) c.fall_stick = true;
      c.ty = c.cy;
    }
    if (c.fall_stick) {
      c.fall_catcher = 0;
      if (hy + 0.1f >= c.lock_y0 || c.tmode != TM_LOCK_ZONE) c.cy = hy + 0.1f, c.ty = c.cy;
      if (c.tmode == TM_LOCK_ZONE && c.cy < c.lock_y0) c.cy = c.lock_y0;
      if (c.cy < Y_MIN) c.cy = Y_MIN;
    }
  }
}

/* CameraController.LateUpdate */
static void controller_late_update(void) {
  const Hero *h = &g_hero;
  if (c.mode != CM_FROZEN) {
    if (h->cs.looking_up) c.look_offset = h->body.y - c.ty + 6;
    else if (h->cs.looking_down) c.look_offset = h->body.y - c.ty - 6;
    else c.look_offset = 0;
    float dx = c.tx + c.x_offset + c.dash_offset, dy = c.ty + c.look_offset;
    if (c.mode == CM_LOCKED && c.cur_lock >= 0) {
      int n;
      const Ent *l = &room_ents(&n)[c.cur_lock];
      float x0, y0, x1, y1;
      lock_bounds(l, &x0, &y0, &x1, &y1);
      if (c.look_offset > 0 && (l->flags & CL_PREVENT_UP) && dy > y1) dy = c.cy > y1 ? dy - c.look_offset : y1;
      if (c.look_offset < 0 && (l->flags & CL_PREVENT_DOWN) && dy < y0) dy = c.cy < y0 ? dy - c.look_offset : y0;
    }
    if (c.mode == CM_FOLLOWING || c.mode == CM_LOCKED) keep_in_scene(&dx, &dy);
    float nx = smooth_damp(c.cx, dx, &c.cvx, c.cdamp_x, DT);
    float ny = smooth_damp(c.cy, dy, &c.cvy, c.cdamp_y, DT);
    c.cx = nx, c.cy = ny;
  }
  keep_in_scene(&c.cx, &c.cy);
}

void cam_tick(void) {
  target_update();
  controller_late_update();
  shake_tick();
  g_cam_x = c.cx + sh.dx, g_cam_y = c.cy + sh.dy;
}

/* after a respawn: the camera with the Knight (PositionToHero) */
void cam_snap_to_hero(void) {
  const Hero *h = &g_hero;
  c.tmode = c.cur_lock >= 0 ? TM_LOCK_ZONE : TM_FOLLOW_HERO;
  c.mode = c.cur_lock >= 0 ? CM_LOCKED : CM_FOLLOWING;
  c.start_locked_timer = 0.5f;
  c.tx = h->body.x, c.ty = h->body.y;
  if (c.tmode == TM_LOCK_ZONE) c.tx = clampf(c.tx, c.lock_x0, c.lock_x1), c.ty = clampf(c.ty, c.lock_y0, c.lock_y1);
  c.x_offset = h->cs.facing_right ? 1.0f : -1.0f;
  c.vx_x = c.vy_y = c.cvx = c.cvy = 0;
  c.cx = c.tx + c.x_offset, c.cy = c.ty;
  keep_in_scene(&c.cx, &c.cy);
  c.stick_x = c.stick_y = true;
  c.prev_hx = h->body.x, c.prev_hy = h->body.y;
  g_cam_x = c.cx, g_cam_y = c.cy;
}

void cam_freeze(void) {
  c.mode = CM_FROZEN;
  c.tmode = TM_FREE;
}
