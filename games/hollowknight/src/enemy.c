/* Enemies (tools/ents.py: OK_ENEMY): HealthManager, Recoil, SpriteFlash, DamageHero and EnemyDeathEffects, each kind's
 * FSM, the corpses they leave and the geo they drop. */
#include <math.h>
#include "game.h"

#define DT 0.02f
#define MAX_ENEMIES 16
#define MAX_GEO 24
#define TERRAIN_FRICTION 0.2f   /* (the Terrain material; with another, their geometric mean) */

/* ---------------------------------------------------------------- what each kind is */
/* (data.h: EK_* each kind, KIND_TABLE: the FSM each runs and the clips its roles play) */
enum { EF_CRAWLER = 1, EF_BUZZER, EF_SHADE, EF_HUSK, EF_CLIMBER, EF_BOUNCER, EF_SPITTER };
enum { R_IDLE, R_TURN, R_WALK, R_A1, R_A2, R_A3, R_A4, R_A5, R_A6, R_A7, R_A8, R_DEATH_AIR, R_DEATH_LAND, NUM_ROLES };
typedef struct {
  uint8_t fsm;
  int16_t clip[NUM_ROLES];
} Kind;
static const Kind kinds[NUM_KINDS] = KIND_TABLE;
#define CLIP(e, role) (kinds[(e)->kind].clip[role])
#define FSM(e) (kinds[(e)->kind].fsm)
#define NO_ENT 0xFFFF   /* (an enemy spawned, not one of the room's) */
/* the records after an enemy's (tools/ents.py: ET_*) */
enum { ET_COLLIDER, ET_ALERT, ET_RANGE, ET_WALKER, ET_RECOIL, ET_CORPSE, ET_VARS };
enum { CF_BREAKER = 1, CF_FACES_RIGHT = 2, CF_LOW_ARC = 4, CF_NO_COLLIDER = 32 };   /* a corpse's (ET_CORPSE) */
enum { WF_PAUSES = 1, WF_IGNORE_HOLES = 2, WF_NO_TURN_TO_HERO = 4, WF_START_INACTIVE = 8, WF_AMBUSH = 16, WF_WAIT_HERO_X = 32,
       WF_PREVENT_TURN = 64, WF_NO_SCALE = 128, WF_RIGHT_NEG = 256 };   /* a Walker's (ET_WALKER) */

/* ---------------------------------------------------------------- an enemy */
enum { EM_OFF, EM_ALIVE, EM_CORPSE };
enum { RC_READY, RC_RECOILING, RC_FROZEN };
enum { CS_AIR, CS_DEATH_ANIM, CS_LANDED };
typedef struct {
  uint8_t mode, kind, st, flags;
  uint16_t ent;                   /* its record (NO_ENT: spawned) */
  int8_t damage;                  /* DamageHero's */
  uint8_t geo_s, geo_m, geo_l;    /* the geo it drops */
  float z;
  Body body;
  Anim anim;
  float sx;   /* its transform's x scale: which way it faces */
  int16_t hp;
  float evasion;
  /* Recoil */
  uint8_t rc_state, rc_flags;
  int8_t rc_dir;
  bool rc_sweep;
  float rc_time, rc_speed, rc_base, rc_dur;
  /* Walker */
  uint8_t wk_state, wk_stop;
  int8_t wk_facing, wk_turning;
  float wk_walk, wk_pause, wk_turn_cd;
  uint16_t wk_rec;   /* its record (0: none) */
  /* SpriteFlash (flashInfected) */
  float flash_t;
  bool flashing;
  /* the FSM's state */
  float t0, t1, wait, start_x, start_y, ax, ay, pause0, pause1, jx, jy;
  float ang, qx, qy, tx, ty;   /* (its rotation, degrees; a tween's from and to) */
  bool b0, b1;
  /* alert range (local circle, or box: ar_hy >= 0) and sight */
  float ar_x, ar_y, ar_r, ar_hy;
  bool in_alert, can_see;
  float cbox_ox, cbox_oy, cbox_hx, cbox_hy;   /* its collider */
} Enemy;
static Enemy en[MAX_ENEMIES];
static uint16_t swing_bits;   /* (a bit an enemy: hit this swing) */

/* its transform's x scale (its collider's offset with it) */
static void set_scale_x(Enemy *e, float sx) {
  if ((sx < 0) != (e->sx < 0)) e->body.ox = -e->body.ox, e->ar_x = -e->ar_x;
  e->sx = sx;
}

static const Ent *ent_at(int i) {
  int n;
  return &room_ents(&n)[i];
}

/* an enemy's record of a tag (ET_*), or NULL */
static const Ent *enemy_rec(const Enemy *e, int tag) {
  if (e->ent == NO_ENT) return NULL;
  const Ent *d = ent_at(e->ent);
  for (int i = 1; i <= d->s0; i++)
    if (d[i].type == ENT_BOX && d[i].flags == tag) return &d[i];
  return NULL;
}

/* the collider's world box */
static void enemy_box(const Enemy *e, float *x0, float *y0, float *x1, float *y1) {
  float cx = e->body.x + e->body.ox, cy = e->body.y + e->body.oy;
  *x0 = cx - e->body.hx, *x1 = cx + e->body.hx, *y0 = cy - e->body.hy, *y1 = cy + e->body.hy;
}

/* ---------------------------------------------------------------- geo (GeoControl) */
typedef struct {
  bool on, landed;
  uint8_t type;   /* 0 small (1), 1 medium (5), 2 large (25) */
  Body body;
  Anim anim;
  float pickup_t, bounce_speed;
} Geo;
static Geo geo[MAX_GEO];
static const struct {
  int air, idle, value;
  float gravity, bounce, ox, oy, hx, hy;
} geo_kinds[3] = {
    {CLIP_GEO_SMALL_AIR, CLIP_GEO_SMALL_IDLE, 1, 1.5f, 0.7f, -0.0078f, -0.0391f, 0.2969f / 2, 0.2969f / 2},
    {CLIP_GEO_MED_AIR, CLIP_GEO_MED_IDLE, 5, 1.5f, 0.6f, -0.0078f, 0, 0.3281f / 2, 0.5312f / 2},
    {CLIP_GEO_LARGE_AIR, CLIP_GEO_LARGE_IDLE, 25, 1.75f, 0.4f, 0.0078f, 0, 0.4531f / 2, 0.4688f / 2},
};
#define GEO_SCALE 1.5f

/* FlingUtils.SpawnAndFling: n of a kind, each at a random speed and angle */
static void geo_fling(int type, int n, float x, float y, float smin, float smax, float amin, float amax) {
  geo_fling_at(type, n, x, y, smin, smax, amin, amax, 0);
}

void geo_fling_at(int type, int n, float x, float y, float smin, float smax, float amin, float amax, float spread) {
  for (int k = 0; k < n; k++) {
    Geo *g = NULL;
    for (int i = 0; i < MAX_GEO && !g; i++)
      if (!geo[i].on) g = &geo[i];
    if (!g) return;
    memset(g, 0, sizeof *g);
    g->on = true, g->type = (uint8_t)type;
    Body *b = &g->body;
    b->x = x + rand_range(-spread, spread), b->y = y + rand_range(-spread, spread);
    b->ox = geo_kinds[type].ox * GEO_SCALE, b->oy = geo_kinds[type].oy * GEO_SCALE;
    b->hx = geo_kinds[type].hx * GEO_SCALE, b->hy = geo_kinds[type].hy * GEO_SCALE;
    b->gravity_scale = geo_kinds[type].gravity, b->friction = 0.2f, b->mask = CF_TERRAIN;
    float s = rand_range(smin, smax), a = rand_range(amin, amax) * (float)M_PI / 180;
    b->vx = cosf(a) * s, b->vy = sinf(a) * s;
    anim_play(&g->anim, geo_kinds[type].air);
    g->pickup_t = 0.25f;
  }
}

/* ObjectBounce: off what it hit, at its speed before, times the bounce factor (and a little chance) */
static void bounce(Body *b, float pvx, float pvy, int had, float factor) {
  if (factor < 0 || b->ncontacts <= had) return;
  float speed = sqrtf(pvx * pvx + pvy * pvy);
  if (speed <= 1) return;
  float nx = b->cnx[b->ncontacts - 1], ny = b->cny[b->ncontacts - 1];
  float ux = pvx / speed, uy = pvy / speed, d = ux * nx + uy * ny;
  float rx = ux - 2 * d * nx, ry = uy - 2 * d * ny, k = speed * factor * rand_range(0.8f, 1.2f);
  b->vx = rx * k, b->vy = ry * k;
}

static void geo_tick(void) {
  const Hero *h = &g_hero;
  float k = h->cs.facing_right ? -1.0f : 1.0f;
  float hx = h->body.x + 0.0055733f * k, hy = h->body.y - 0.6942673f;   /* (the HeroBox) */
  for (int i = 0; i < MAX_GEO; i++) {
    Geo *g = &geo[i];
    if (!g->on) continue;
    float pvx = g->body.vx, pvy = g->body.vy;
    int had = g->body.ncontacts;
    body_step(&g->body, DT);
    if (g->body.ncontacts > had) {
      bounce(&g->body, pvx, pvy, had, geo_kinds[g->type].bounce);
      /* (OnCollisionEnter2D: the idle animation from a random frame) */
      anim_play_from_frame(&g->anim, geo_kinds[g->type].idle, (int)rand_range(0, (float)clip_frames_count(geo_kinds[g->type].idle) - 0.001f));
    }
    g->anim.events = 0;
    anim_update(&g->anim, DT);
    if (g->pickup_t > 0) g->pickup_t -= DT;
    float x0 = g->body.x + g->body.ox - g->body.hx, x1 = g->body.x + g->body.ox + g->body.hx;
    float y0 = g->body.y + g->body.oy - g->body.hy, y1 = g->body.y + g->body.oy + g->body.hy;
    if (g->pickup_t <= 0 && !h->hidden && x1 > hx - 0.2277069f && x0 < hx + 0.2277069f && y1 > hy - 0.5848932f &&
        y0 < hy + 0.5848932f) {
      hero_add_geo(geo_kinds[g->type].value);
      g->on = false;
    }
    if (g->body.y < -10) g->on = false;
  }
}

/* ---------------------------------------------------------------- SpriteFlash */
static uint8_t flash_tint(const Enemy *e, int slot) {
  if (!e->flashing) return 0;
  /* flashInfected: up 0.01 s, stays 0.01 s, down over 0.25 s, to 0.9 of orange */
  float t = e->flash_t, k;
  if (t < 0.01f) k = t / 0.01f;
  else if (t < 0.02f) k = 1;
  else k = 1 - (t - 0.02f) / 0.25f;
  if (k < 0) k = 0;
  return gfx_dyn_flash(slot, 255, 255, 255, 255, 255, 79, 0, (uint8_t)(0.9f * k * 255));
}

/* ---------------------------------------------------------------- Recoil */
static void climber_stun(Enemy *e);
static void recoil_by_direction(Enemy *e, int dir, float magnitude) {
  if (e->rc_state != RC_READY) return;
  if (e->rc_flags & 2) {
    /* freezeInPlace: a climber is stunned (its own handling); others stop still a while */
    if (FSM(e) == EF_CLIMBER) climber_stun(e);
    else e->rc_state = RC_FROZEN, e->rc_time = e->rc_dur, e->body.vx = e->body.vy = 0;
    return;
  }
  if (dir == 1 && (e->rc_flags & 4)) return;   /* (preventRecoilUp) */
  e->rc_state = RC_RECOILING;
  e->rc_speed = e->rc_base * magnitude;
  e->rc_dir = (int8_t)dir;
  e->rc_sweep = true;
  e->rc_time = e->rc_dur;
  if (dir == 0 || dir == 2) e->flags |= 2;   /* (RECOIL HORIZONTAL to its FSM) */
}

/* Sweep.Check: three rays from the collider's side, the distance clipped by the terrain */
static void recoil_fixed(Enemy *e) {
  if (e->rc_state == RC_FROZEN) {
    e->body.vx = e->body.vy = 0;
    if ((e->rc_time -= DT) <= 0) e->rc_state = RC_READY;
    return;
  }
  if (e->rc_state != RC_RECOILING) return;
  if (e->rc_sweep) {
    float dx = (float)(e->rc_dir == 0) - (float)(e->rc_dir == 2), dy = (float)(e->rc_dir == 1) - (float)(e->rc_dir == 3);
    float dist = e->rc_speed * DT, d = dist;
    float cx = e->body.x + e->body.ox + e->body.hx * dx, cy = e->body.y + e->body.oy + e->body.hy * dy;
    for (int i = 0; i < 3; i++) {
      float f = (float)i - 1;
      float x = cx + e->body.hx * fabsf(dy) * f - dx * 0.1f, y = cy + e->body.hy * fabsf(dx) * f - dy * 0.1f;
      PhysHit hit;
      if (phys_ray(x, y, dx, dy, d + 0.1f, CF_TERRAIN, &hit) && hit.dist - 0.1f < d) d = hit.dist - 0.1f;
    }
    if (dist - d > 1e-6f) e->rc_sweep = false;
    if (d > 1e-6f) e->body.x += dx * d, e->body.y += dy * d;
  }
  e->rc_time -= DT;
  if (e->rc_time <= 0) e->rc_state = RC_READY;
}

/* ---------------------------------------------------------------- death */
static void corpse_start(Enemy *e, float direction, bool has_direction) {
  const Ent *c = enemy_rec(e, ET_CORPSE);
  if (!c || kinds[e->kind].clip[R_DEATH_AIR] < 0) {
    e->mode = EM_OFF;   /* (no corpse) */
    return;
  }
  e->mode = EM_CORPSE, e->st = CS_AIR;
  e->body.y += c->p3;
  e->body.ox = c->x0, e->body.oy = c->y0, e->body.hx = c->x1, e->body.hy = c->y1;
  e->body.gravity_scale = c->p0, e->body.friction = 0.2f, e->body.mask = (c->a & CF_NO_COLLIDER) ? 0 : CF_SOLID;
  e->body.ncontacts = 0;
  float speed = c->p2, angle = 90, sign = e->sx < 0 ? -1.0f : 1.0f;
  switch (cardinal(direction)) {
    case 0: angle = 60, e->sx = sign; break;    /* (corpseFacesRight: no) */
    case 2: angle = 120, e->sx = -sign; break;
    case 3: angle = 270; break;
    default: angle = rand_range(75, 105), speed *= 1.3f; break;
  }
  if (!has_direction) speed = 0;
  float a = angle * (float)M_PI / 180;
  e->body.vx = cosf(a) * speed, e->body.vy = sinf(a) * speed;
  anim_play(&e->anim, kinds[e->kind].clip[R_DEATH_AIR]);
  e->flashing = true, e->flash_t = 0;   /* (EmitInfectedEffects: the corpse flashes) */
}

/* HealthManager.Die, EnemyDeathEffects */
static void shade_killed(Enemy *e);
static void enemy_die(Enemy *e, float direction, bool has_direction) {
  if (FSM(e) == EF_SHADE) {
    shade_killed(e);
    return;
  }
  /* the geo: SpawnAndFling, small, medium then large */
  float x = e->body.x, y = e->body.y;
  geo_fling(0, e->geo_s, x, y, 15, 30, 80, 100);
  geo_fling(1, e->geo_m, x, y, 15, 30, 80, 100);
  geo_fling(2, e->geo_l, x, y, 15, 30, 80, 100);
  if (e->ent != NO_ENT) persist_set(ent_at(e->ent)->persist);
  corpse_start(e, direction, has_direction);
  cam_shake(SHAKE_ENEMY_KILL);
  FREEZE_MOMENT_1();
}

/* HealthManager.Hit (a nail's): evasion, damage, recoil, flash, soul */
static void enemy_hit(Enemy *e, float direction, int damage) {
  if (e->mode != EM_ALIVE || e->evasion > 0 || damage <= 0) return;
  int dir = cardinal(direction);
  recoil_by_direction(e, dir, 1);
  if (FSM(e) != EF_SHADE) hero_soul_gain();   /* (enemyType 3, a shade: no soul) */
  e->flashing = true, e->flash_t = 0;
  e->hp = (int16_t)(e->hp - damage < -50 ? -50 : e->hp - damage);
  if ((FSM(e) == EF_BUZZER || FSM(e) == EF_SHADE || FSM(e) == EF_HUSK) && e->st == 0) e->b1 = true;   /* (TOOK DAMAGE, in Idle / Ready) */
  if (e->hp > 0)
    e->evasion = 0.2f;
  else
    enemy_die(e, direction, true);
}

/* ---------------------------------------------------------------- PlayMaker actions */
/* the hero's position (its transform) */
static float hero_x(void) { return g_hero.body.x; }
static float hero_y(void) { return g_hero.body.y; }

/* FaceDirection (sprite facing left): turns to its velocity, playing a clip, pausing between turns */
static void face_direction(Enemy *e, float *pause, int clip) {
  if (*pause > 0) {
    *pause -= DT;
    return;
  }
  float want = e->body.vx > 0 ? -fabsf(e->sx) : fabsf(e->sx);
  if (e->sx != want) {
    *pause = 0.5f;
    set_scale_x(e, want);
    if (clip >= 0) anim_play_from_frame(&e->anim, clip, 0);
  }
}

/* LineOfSightDetector, AlertRange: the hero in the circle, and no terrain between */
static void sight_update(Enemy *e) {
  float cx = g_hero.body.x + g_hero.body.ox, cy = g_hero.body.y + g_hero.body.oy;
  float ax = e->body.x + e->ar_x, ay = e->body.y + e->ar_y;
  if (e->ar_hy >= 0) {
    /* a box against the Knight's */
    e->in_alert = !g_hero.hidden && fabsf(cx - ax) < e->ar_r + g_hero.body.hx && fabsf(cy - ay) < e->ar_hy + g_hero.body.hy;
  } else {
    /* a circle against the Knight's box */
    float qx = ax < cx - g_hero.body.hx ? cx - g_hero.body.hx : ax > cx + g_hero.body.hx ? cx + g_hero.body.hx : ax;
    float qy = ay < cy - g_hero.body.hy ? cy - g_hero.body.hy : ay > cy + g_hero.body.hy ? cy + g_hero.body.hy : ay;
    e->in_alert = !g_hero.hidden && e->ar_r > 0 && (qx - ax) * (qx - ax) + (qy - ay) * (qy - ay) < e->ar_r * e->ar_r;
  }
  if (!e->in_alert) {
    e->can_see = false;
    return;
  }
  float dx = hero_x() - e->body.x, dy = hero_y() - e->body.y, l = sqrtf(dx * dx + dy * dy);
  e->can_see = l < 1e-4f || !phys_ray(e->body.x, e->body.y, dx / l, dy / l, l, CF_TERRAIN, NULL);
}

/* ---------------------------------------------------------------- Crawler: WalkLeftRight (sprite facing left) */
enum { CR_WALK, CR_WAIT, CR_TURN };
static float crawl_dir(const Enemy *e) { return e->sx < 0 ? 1.0f : -1.0f; }

static bool crawl_ray(const Enemy *e, float x, float dx, float dy, float len) {
  float y0 = e->body.y + e->body.oy - e->body.hy;
  return phys_ray(x, y0 + 0.5f, dx, dy, len, CF_TERRAIN, NULL);
}

static void crawler_start(Enemy *e, const Ent *d) {
  if (d->s1 & 1) {
    e->st = CR_WAIT;   /* (First Crawler: waits for GO LEFT or GO RIGHT) */
    anim_play(&e->anim, CLIP_CRAWLER_WALK);
    return;
  }
  e->st = CR_WALK;
  e->b0 = rand_range(0, 100) <= 50;   /* (shouldTurn: half the time) */
  anim_play(&e->anim, CLIP_CRAWLER_WALK);
}

static void crawler_fixed(Enemy *e) {
  if (e->st == CR_TURN) {
    e->t0 -= DT;
    if (e->t0 <= 0) {
      set_scale_x(e, -e->sx);
      anim_play(&e->anim, CLIP_CRAWLER_WALK);
      e->st = CR_WALK;
    }
    return;
  }
  if (e->st != CR_WALK) return;
  float dir = crawl_dir(e);
  e->body.vx = 4 * dir;
  float cx = e->body.x + e->body.ox, w = e->body.hx;
  bool grounded = crawl_ray(e, cx, 0, -1, 1);
  bool wall = crawl_ray(e, cx, dir, 0, w + 0.1f);
  bool edge = !crawl_ray(e, cx + (w + 0.1f) * dir, 0, -1, 1);
  if (e->b0 || (grounded && (wall || edge) && g_game.time >= e->t1)) {
    e->b0 = false;
    e->t1 = g_game.time + 1;   /* (turnDelay) */
    e->body.vx = 0;
    anim_play(&e->anim, CLIP_CRAWLER_TURN);
    e->t0 = clip_duration(CLIP_CRAWLER_TURN);
    e->st = CR_TURN;
  }
}

/* ---------------------------------------------------------------- Buzzer: the chaser FSM */
enum { BZ_IDLE, BZ_STARTLE, BZ_CHASE, BZ_STOP };

static void buzzer_idle_enter(Enemy *e) {
  e->st = BZ_IDLE;
  anim_play(&e->anim, CLIP_BUZZER_IDLE);
  e->start_x = e->body.x, e->start_y = e->body.y;   /* (IdleBuzz) */
  e->wait = 0, e->ax = e->ay = 0;
}

static void chase_start(Enemy *e) {
  anim_play_from_frame(&e->anim, CLIP_BUZZER_CHASE, 3);
  e->st = BZ_CHASE, e->t0 = 0;
}

static void buzzer_alerted(Enemy *e, const Ent *d) {
  if (d->s1 & 2) {
    /* Startle: still, facing the hero, till the clip ends */
    e->st = BZ_STARTLE;
    e->body.vx = e->body.vy = 0;
    anim_play(&e->anim, CLIP_BUZZER_STARTLE);
    set_scale_x(e, hero_x() > e->body.x ? -fabsf(e->sx) : fabsf(e->sx));
  } else
    chase_start(e);
}

static void buzzer_start(Enemy *e, const Ent *d) {
  if (d->s1 & 1) chase_start(e);
  else buzzer_idle_enter(e);
}

/* IdleBuzz: drifts about its start, accelerating at random */
static void idle_buzz(Enemy *e) {
  const float wmin = 0.75f, wmax = 1, vmax = 1.75f, amax = 15, range = 1;
  float vx = e->body.vx, vy = e->body.vy, x = e->body.x, y = e->body.y;
  if (y < e->start_y - range) {
    if (vy < 0) e->ay = amax / 2000, vy /= 1.125f, e->wait = rand_range(wmin, wmax);
  } else if (y > e->start_y + range && vy > 0)
    e->ay = -amax / 2000, vy /= 1.125f, e->wait = rand_range(wmin, wmax);
  if (x < e->start_x - range) {
    if (vx < 0) e->ax = amax / 2000, vx /= 1.125f, e->wait = rand_range(wmin, wmax);
  } else if (x > e->start_x + range && vx > 0)
    e->ax = -amax / 2000, vx /= 1.125f, e->wait = rand_range(wmin, wmax);
  if (e->wait <= 1.1920929e-7f) {
    e->ay = y < e->start_y - range ? rand_range(0, amax) : y > e->start_y + range ? rand_range(-amax, 0) : rand_range(-amax, amax);
    e->ax = x < e->start_x - range ? rand_range(0, amax) : x > e->start_x + range ? rand_range(-amax, 0) : rand_range(-amax, amax);
    e->ay /= 2000, e->ax /= 2000;
    e->wait = rand_range(wmin, wmax);
  }
  if (e->wait > 1.1920929e-7f) e->wait -= DT;
  vx += e->ax, vy += e->ay;
  e->body.vx = vx > vmax ? vmax : vx < -vmax ? -vmax : vx;
  e->body.vy = vy > vmax ? vmax : vy < -vmax ? -vmax : vy;
}

/* ChaseObject: towards the hero, a little faster each step */
static void chase_object(Enemy *e, float vmax, float accel) {
  float vx = e->body.vx + (e->body.x < hero_x() ? accel : -accel);
  float vy = e->body.vy + (e->body.y < hero_y() ? accel : -accel);
  e->body.vx = vx > vmax ? vmax : vx < -vmax ? -vmax : vx;
  e->body.vy = vy > vmax ? vmax : vy < -vmax ? -vmax : vy;
}

static void decelerate(Enemy *e, float d) {
  float vx = e->body.vx, vy = e->body.vy;
  vx = vx < 0 ? (vx + d > 0 ? 0 : vx + d) : vx > 0 ? (vx - d < 0 ? 0 : vx - d) : 0;
  vy = vy < 0 ? (vy + d > 0 ? 0 : vy + d) : vy > 0 ? (vy - d < 0 ? 0 : vy - d) : 0;
  e->body.vx = vx, e->body.vy = vy;
}

static void buzzer_fixed(Enemy *e) {
  if (e->st == BZ_IDLE) idle_buzz(e);
  else if (e->st == BZ_CHASE) chase_object(e, 5, 0.045f);
  else if (e->st == BZ_STOP) decelerate(e, 0.12f);
}

static void buzzer_update(Enemy *e, const Ent *d) {
  switch (e->st) {
    case BZ_IDLE:
      face_direction(e, &e->pause0, CLIP_BUZZER_TURNTOIDLE);
      if ((e->can_see && e->in_alert) || e->b1) {
        e->b1 = false;
        buzzer_alerted(e, d);
      }
      break;
    case BZ_STARTLE:
      if (e->anim.events & ANIM_DONE) chase_start(e);
      break;
    case BZ_CHASE:
      /* (In Sight and Out of Sight in turn: the attention span counts while it cannot see the hero) */
      face_direction(e, &e->pause1, CLIP_BUZZER_TURNTOFLY);
      if (e->can_see && e->in_alert) e->t0 = 0;
      else if ((e->t0 += DT) >= 10) {
        e->st = BZ_STOP, e->t0 = 0;
        anim_play_from_frame(&e->anim, CLIP_BUZZER_IDLE, 3);
      }
      break;
    case BZ_STOP:
      if ((e->t0 += DT) >= 1) buzzer_idle_enter(e);
      break;
  }
}

/* ---------------------------------------------------------------- Walker */
enum { WK_NOT_READY, WK_WAITING, WK_STOPPED, WK_WALKING, WK_TURNING };
enum { STOP_BORED, STOP_CONTROLLED };

static const Ent *walker_rec(const Enemy *e) {
  int n;
  return e->wk_rec ? &room_ents(&n)[e->wk_rec] : NULL;
}

/* (its transform's x scale: facing times rightScale) */
static void walker_scale(Enemy *e, int facing) {
  const Ent *w = walker_rec(e);
  if (w->a & WF_NO_SCALE) return;
  float rs = (w->a & WF_RIGHT_NEG) ? -1.0f : 1.0f;
  set_scale_x(e, (float)facing * rs * fabsf(e->sx));
}

static void walker_begin_walking(Enemy *e, int facing) {
  const Ent *w = walker_rec(e);
  e->wk_state = WK_WALKING;
  anim_play(&e->anim, CLIP(e, R_WALK));
  walker_scale(e, facing);
  e->wk_walk = rand_range(w->p0, w->p1);
  e->body.vx = facing > 0 ? w->y0 : w->x0;
}

static void walker_begin_turning(Enemy *e, int facing) {
  const Ent *w = walker_rec(e);
  e->wk_state = WK_TURNING;
  e->wk_turning = (int8_t)facing;
  if (w->a & WF_PREVENT_TURN) {
    e->wk_facing = e->wk_turning;
    walker_begin_walking(e, e->wk_facing);
    return;
  }
  e->wk_turn_cd = w->p2;
  e->body.vx = 0;
  anim_play_from_frame(&e->anim, CLIP(e, R_TURN), 0);
}

static void walker_walk_or_turn(Enemy *e, int facing) {
  if (e->wk_facing == facing) walker_begin_walking(e, facing);
  else walker_begin_turning(e, facing);
}

static void walker_end_stopping(Enemy *e) {
  const Ent *w = walker_rec(e);
  if (e->wk_facing == 0) walker_walk_or_turn(e, rand_range(0, 2) < 1 ? 1 : -1);
  else if (rand_range(0, 100) < w->group) walker_begin_turning(e, -e->wk_facing);
  else walker_begin_walking(e, e->wk_facing);
}

static void walker_stop(Enemy *e, int reason) {
  const Ent *w = walker_rec(e);
  e->wk_state = WK_STOPPED, e->wk_stop = (uint8_t)reason;
  if (reason == STOP_BORED) {
    anim_play(&e->anim, CLIP(e, R_IDLE));
    e->body.vx = 0;
    if (w->a & WF_PAUSES) e->wk_pause = rand_range(w->x1, w->y1);
    else walker_end_stopping(e);
  }
}

static void walker_update(Enemy *e);
/* StartMoving */
static void walker_start(Enemy *e) {
  if (e->wk_state == WK_STOPPED || e->wk_state == WK_WAITING) {
    int facing = e->wk_facing ? e->wk_facing : (rand_range(0, 2) < 1 ? 1 : -1);
    walker_walk_or_turn(e, facing);
  }
  walker_update(e);
}

/* (Start: its facing from its scale, then waiting for its conditions) */
static void walker_init(Enemy *e) {
  const Ent *w = walker_rec(e);
  float rs = (w->a & WF_RIGHT_NEG) ? -1.0f : 1.0f;
  e->wk_facing = e->sx * rs >= 0 ? 1 : -1;
  e->wk_turn_cd = -1e-6f;
  e->wk_state = WK_WAITING;
}

/* Sweep.Check: rays from a side of the collider, from (x, y), along a cardinal direction: is there terrain within? */
static bool sweep(const Enemy *e, float x, float y, int dir, float dist) {
  float dx = (float)(dir == 0) - (float)(dir == 2), dy = (float)(dir == 1) - (float)(dir == 3);
  float bx = e->body.ox + e->body.hx * dx, by = e->body.oy + e->body.hy * dy;
  for (int i = 0; i < 3; i++) {
    float f = (float)i - 1;
    float rx = x + bx + e->body.hx * fabsf(dy) * f - dx * 0.1f, ry = y + by + e->body.hy * fabsf(dx) * f - dy * 0.1f;
    PhysHit hit;
    if (phys_ray(rx, ry, dx, dy, dist + 0.1f, CF_TERRAIN, &hit) && hit.dist - 0.1f < dist) return true;
  }
  return false;
}

static void walker_update(Enemy *e) {
  const Ent *w = walker_rec(e);
  if (!w) return;
  e->wk_turn_cd -= DT;
  switch (e->wk_state) {
    case WK_WAITING: {
      /* (near the camera; at the hero's x if it waits for that; not an ambush nor inactive) */
      bool hero_x_ok = !(w->a & WF_WAIT_HERO_X) || fabsf(hero_x() - w->s1 / 16.0f) < 1;
      if (hero_x_ok && !(w->a & (WF_START_INACTIVE | WF_AMBUSH))) {
        walker_stop(e, STOP_BORED);
        walker_start(e);
      }
      break;
    }
    case WK_STOPPED:
      if (e->wk_stop == STOP_BORED && (e->wk_pause -= DT) <= 0) walker_end_stopping(e);
      break;
    case WK_WALKING:
      if (e->wk_turn_cd <= 0) {
        float ext = e->body.hx;
        if (sweep(e, e->body.x, e->body.y, 1 - e->wk_facing, ext + 0.5f)) {
          walker_begin_turning(e, -e->wk_facing);
          return;
        }
        if (!(w->a & WF_NO_TURN_TO_HERO) && (hero_x() > e->body.x) != (e->wk_facing > 0) && e->can_see && e->in_alert) {
          walker_begin_turning(e, -e->wk_facing);
          return;
        }
        if (!(w->a & WF_IGNORE_HOLES) &&
            !sweep(e, e->body.x + (ext + 0.5f + w->p3) * e->wk_facing, e->body.y, 3, 0.25f)) {
          walker_begin_turning(e, -e->wk_facing);
          return;
        }
      }
      if ((w->a & WF_PAUSES) && (e->wk_walk -= DT) <= 0) {
        walker_stop(e, STOP_BORED);
        return;
      }
      e->body.vx = e->wk_facing > 0 ? w->y0 : w->x0;
      break;
    case WK_TURNING:
      e->body.vx = 0;
      if (!e->anim.playing) {
        e->wk_facing = e->wk_turning;
        walker_begin_walking(e, e->wk_facing);
      }
      break;
  }
}

/* ---------------------------------------------------------------- Tiktiks: Climber, round the terrain's edges */
enum { CL_WALK, CL_TURN, CL_STUN };
enum { DIR_RIGHT, DIR_DOWN, DIR_LEFT, DIR_UP };

/* its collider at its rotation (quarter turns) */
static void climber_box(Enemy *e) {
  const Ent *c = enemy_rec(e, ET_COLLIDER);
  if (!c) return;
  float ox = c->x0 * (e->sx < 0 ? -1 : 1), oy = c->y0;
  int q = ((int)lrintf(e->ang / 90) % 4 + 4) % 4;
  float rx = q == 0 ? ox : q == 1 ? -oy : q == 2 ? -ox : oy, ry = q == 0 ? oy : q == 1 ? ox : q == 2 ? -oy : -ox;
  e->body.ox = rx, e->body.oy = ry;
  e->body.hx = q & 1 ? c->y1 : c->x1, e->body.hy = q & 1 ? c->x1 : c->y1;
}

/* FireRayLocal: from its collider's center along a direction of its own */
static bool climber_ray(const Enemy *e, float lx, float ly, float len) {
  const Ent *c = enemy_rec(e, ET_COLLIDER);
  float a = e->ang * (float)M_PI / 180, ca = cosf(a), sa = sinf(a);
  float ox = c ? c->x0 * (e->sx < 0 ? -1 : 1) : 0, oy = c ? c->y0 : 0;
  float x = e->body.x + ox * ca - oy * sa, y = e->body.y + ox * sa + oy * ca;
  return phys_ray(x, y, lx * ca - ly * sa, lx * sa + ly * ca, len, CF_TERRAIN, NULL);
}

static void climber_velocity(Enemy *e) {
  const Ent *v = enemy_rec(e, ET_VARS);
  float sp = v ? v->p0 : 2;
  int d = e->wk_facing;
  e->body.vx = d == DIR_RIGHT ? sp : d == DIR_LEFT ? -sp : 0;
  e->body.vy = d == DIR_UP ? sp : d == DIR_DOWN ? -sp : 0;
}

static void climber_walk(Enemy *e) {
  e->st = CL_WALK;
  anim_play(&e->anim, CLIP(e, R_WALK));
  climber_velocity(e);
}

static void climber_start(Enemy *e) {
  const Ent *v = enemy_rec(e, ET_VARS);
  e->ang = v ? 90.0f * v->s1 : 0;
  climber_box(e);
  /* StickToGround: onto the ground below, within 2 */
  const Ent *c = enemy_rec(e, ET_COLLIDER);
  float a = e->ang * (float)M_PI / 180, ca = cosf(a), sa = sinf(a);
  float ox = c ? c->x0 * (e->sx < 0 ? -1 : 1) : 0, oy = c ? c->y0 : 0;
  PhysHit hit;
  if (phys_ray(e->body.x + ox * ca - oy * sa, e->body.y + ox * sa + oy * ca, sa, -ca, 2, CF_TERRAIN, &hit))
    e->body.x = hit.x, e->body.y = hit.y;
  float k = e->sx < 0 ? -1.0f : 1.0f;
  if (v && !(v->a & 1)) k = -k;
  e->b0 = k > 0;   /* (clockwise) */
  float r = fmodf(e->ang, 360);
  if (r < 0) r += 360;
  if (r >= 45 && r <= 135) e->wk_facing = e->b0 ? DIR_UP : DIR_DOWN;
  else if (r >= 135 && r <= 225) e->wk_facing = e->b0 ? DIR_LEFT : DIR_RIGHT;
  else if (r >= 225 && r <= 315) e->wk_facing = e->b0 ? DIR_DOWN : DIR_UP;
  else e->wk_facing = e->b0 ? DIR_RIGHT : DIR_LEFT;
  e->start_x = e->body.x, e->start_y = e->body.y;
  e->ax = e->body.x, e->ay = e->body.y;   /* (previousTurnPos: none yet, its place) */
  e->ax = 1e9f;
  climber_walk(e);
}

static void climber_turn(Enemy *e, bool cw, bool tween) {
  const Ent *c = enemy_rec(e, ET_COLLIDER), *v = enemy_rec(e, ET_VARS);
  float w = c ? c->x1 : 0.5f, h = c ? c->y1 : 0.5f, pad = v ? v->p2 : 0.1f;   /* (half sizes) */
  e->st = CL_TURN, e->t0 = 0;
  e->body.vx = e->body.vy = 0;
  e->pause0 = e->ang, e->pause1 = e->ang + (cw ? -90.0f : 90.0f);
  e->qx = e->body.x, e->qy = e->body.y, e->b1 = tween;
  float tx = 0, ty = 0;   /* GetTweenPos */
  switch (e->wk_facing) {
    case DIR_RIGHT: tx = w + pad, ty = e->b0 ? h : -h; break;
    case DIR_UP: tx = e->b0 ? -w : w, ty = h + pad; break;
    case DIR_DOWN: tx = e->b0 ? w : -w, ty = -h - pad; break;
    case DIR_LEFT: tx = -w - pad, ty = e->b0 ? -h : h; break;
  }
  e->tx = e->body.x + tx, e->ty = e->body.y + ty;
  e->wait = cw ? 1 : -1;
}

static void climber_stun(Enemy *e) {
  if (e->st == CL_TURN) return;
  e->st = CL_STUN;
  e->body.vx = e->body.vy = 0;
  anim_play_from_frame(&e->anim, CLIP(e, R_A1), 0);
}

static void climber_update(Enemy *e) {
  const Ent *v = enemy_rec(e, ET_VARS);
  switch (e->st) {
    case CL_WALK: {
      /* (constrain: held to its line) */
      bool moved = false;
      if (fabsf(e->body.x - e->start_x) > 0.1f) e->body.x = e->start_x, moved = true;
      if (fabsf(e->body.y - e->start_y) > 0.1f) e->body.y = e->start_y, moved = true;
      if (!moved) e->start_x = e->body.x, e->start_y = e->body.y;
      float dx = e->body.x - e->ax, dy = e->body.y - e->ay;
      if (e->ax > 1e8f || dx * dx + dy * dy >= (v ? v->p3 * v->p3 : 0.0625f)) {
        const Ent *c = enemy_rec(e, ET_COLLIDER);
        float w = c ? c->x1 : 0.5f, pad = v ? v->p2 : 0.1f;
        if (!climber_ray(e, 0, -1, 1)) climber_turn(e, e->b0, false);
        else if (climber_ray(e, e->b0 ? 1 : -1, 0, w + pad)) climber_turn(e, !e->b0, true);
      }
      break;
    }
    case CL_TURN: {
      float spin = v ? v->p1 : 0.25f;
      e->t0 += DT;
      float t = e->t0 >= spin ? 1 : e->t0 / spin;
      e->ang = e->pause0 + (e->pause1 - e->pause0) * t;
      if (e->b1) e->body.x = e->qx + (e->tx - e->qx) * t, e->body.y = e->qy + (e->ty - e->qy) * t;
      if (e->t0 >= spin) {
        e->ang = e->pause1;
        e->wk_facing = (int8_t)((e->wk_facing + (e->wait > 0 ? 1 : -1) + 4) % 4);
        climber_box(e);
        e->start_x = e->ax = e->body.x, e->start_y = e->ay = e->body.y;
        climber_walk(e);
      }
      break;
    }
    case CL_STUN:
      if (e->anim.events & ANIM_DONE) climber_walk(e);
      break;
  }
}

/* ---------------------------------------------------------------- Gruzzers: the Bouncer Control FSM */
enum { BO_WAIT, BO_FLY, BO_STOPPED };

static void bouncer_aim(Enemy *e, float lo, float hi) {
  e->wait = rand_range(lo, hi);   /* (Angle) */
  float a = fmodf(e->wait, 360);
  if (a < 0) a += 360;
  e->b0 = a < 90 || a >= 270;     /* (Left or Right?: Facing Right) */
  e->st = BO_FLY;
}

static void bouncer_start(Enemy *e) {
  const Ent *v = enemy_rec(e, ET_VARS);
  e->st = v && (v->a & 1) ? BO_STOPPED : BO_WAIT;
  anim_play(&e->anim, CLIP(e, R_A1));
}

/* CheckCollisionSide: short rays from its sides, top first: the side it bumped */
static int bouncer_side(const Enemy *e) {
  float x0, y0, x1, y1;
  enemy_box(e, &x0, &y0, &x1, &y1);
  float cx = (x0 + x1) / 2, cy = (y0 + y1) / 2;
  const float d = 0.08f;
  if (phys_ray(x0, y1, 0, 1, d, CF_TERRAIN, NULL) || phys_ray(cx, y1, 0, 1, d, CF_TERRAIN, NULL) || phys_ray(x1, y1, 0, 1, d, CF_TERRAIN, NULL)) return 1;
  if (phys_ray(x1, y1, 1, 0, d, CF_TERRAIN, NULL) || phys_ray(x1, cy, 1, 0, d, CF_TERRAIN, NULL) || phys_ray(x1, y0, 1, 0, d, CF_TERRAIN, NULL)) return 0;
  if (phys_ray(x1, y0, 0, -1, d, CF_TERRAIN, NULL) || phys_ray(cx, y0, 0, -1, d, CF_TERRAIN, NULL) || phys_ray(x0, y0, 0, -1, d, CF_TERRAIN, NULL)) return 3;
  if (phys_ray(x0, y0, -1, 0, d, CF_TERRAIN, NULL) || phys_ray(x0, cy, -1, 0, d, CF_TERRAIN, NULL) || phys_ray(x0, y1, -1, 0, d, CF_TERRAIN, NULL)) return 2;
  return -1;
}

static void bouncer_fixed(Enemy *e) {
  if (e->st != BO_FLY) return;
  const Ent *v = enemy_rec(e, ET_VARS);
  float sp = v ? v->p0 : 5.2f, a = e->wait * (float)M_PI / 180;
  e->body.vx = cosf(a) * sp, e->body.vy = sinf(a) * sp;   /* (SetVelocityAsAngle) */
}

static void bouncer_update(Enemy *e) {
  switch (e->st) {
    case BO_WAIT: {
      float dx = e->body.x - g_cam_x, dy = e->body.y - g_cam_y;
      if (dx * dx + dy * dy < 44 * 44) {
        const Ent *v = enemy_rec(e, ET_VARS);
        if (v && (v->a & 2)) bouncer_aim(e, 90, 90);   /* (Start Up) */
        else bouncer_aim(e, 0, 360);
      }
      break;
    }
    case BO_FLY: {
      /* FaceDirection (sprite facing left), no pause */
      float want = e->body.vx > 0 ? -fabsf(e->sx) : fabsf(e->sx);
      if (e->body.vx != 0 && e->sx != want) set_scale_x(e, want);
      if (!e->body.ncontacts) break;
      float a = fmodf(e->wait, 360);
      if (a < 0) a += 360;
      switch (bouncer_side(e)) {
        case 1: if (e->b0) bouncer_aim(e, 320, 350); else bouncer_aim(e, 190, 220); break;   /* (Hit Up) */
        case 3: if (e->b0) bouncer_aim(e, 10, 40); else bouncer_aim(e, 140, 170); break;     /* (Hit Down) */
        case 0: if (a < 180) bouncer_aim(e, 140, 170); else bouncer_aim(e, 190, 220); break;  /* (Hit Right) */
        case 2: if (a < 180) bouncer_aim(e, 10, 40); else bouncer_aim(e, 320, 350); break;    /* (Hit Left) */
      }
      break;
    }
  }
}

/* DistanceFly: keeps a distance from the hero (targeting its height, or its distance in y too) */
static void distance_fly(Enemy *e, float dist, float vmax, float accel, bool height) {
  float vx = e->body.vx, vy = e->body.vy;
  float dx = e->body.x - hero_x(), dy = e->body.y - hero_y(), d = sqrtf(dx * dx + dy * dy);
  bool left = e->body.x < hero_x(), below = e->body.y < hero_y();
  vx += (d > dist) == left ? accel : -accel;
  if (!height) vy += (d > dist) == below ? accel : -accel;
  else {
    if (e->body.y < hero_y()) vy += accel;
    if (e->body.y > hero_y()) vy -= accel;
  }
  e->body.vx = vx > vmax ? vmax : vx < -vmax ? -vmax : vx;
  e->body.vy = vy > vmax ? vmax : vy < -vmax ? -vmax : vy;
}

/* ---------------------------------------------------------------- enemies' shots (EnemyBullet) */
#define MAX_BULLETS 8
typedef struct {
  bool on, active;
  float x, y, vx, vy, scale, ang;
  Anim anim;
} Bullet;
static Bullet bullets[MAX_BULLETS];
#define BULLET_HX (0.6406f / 2)
#define BULLET_HY (0.5625f / 2)

/* FireAtTarget: from (x, y) at the hero, at a speed */
static void bullet_fire(float x, float y, float speed) {
  Bullet *b = NULL;
  for (int i = 0; i < MAX_BULLETS && !b; i++)
    if (!bullets[i].on) b = &bullets[i];
  if (!b) return;
  memset(b, 0, sizeof *b);
  b->on = b->active = true;
  b->x = x, b->y = y;
  b->scale = rand_range(1.15f, 1.45f) * 0.7f;   /* (its random scale, on the prefab's) */
  float dx = hero_x() - x, dy = hero_y() - y, l = sqrtf(dx * dx + dy * dy);
  if (l < 1e-4f) l = 1, dx = 1;
  b->vx = dx / l * speed, b->vy = dy / l * speed;
  anim_play(&b->anim, CLIP_BULLET_IDLE);
}

static void bullet_impact(Bullet *b) {
  b->active = false;
  b->vx = b->vy = 0;
  anim_play_from_frame(&b->anim, CLIP_BULLET_IMPACT, 0);
}

static void bullets_tick(void) {
  for (int i = 0; i < MAX_BULLETS; i++) {
    Bullet *b = &bullets[i];
    if (!b->on) continue;
    b->anim.events = 0;
    anim_update(&b->anim, DT);
    if (!b->active) {
      if (b->anim.events & ANIM_DONE) b->on = false;
      continue;
    }
    b->vy += -60 * 0.05f * DT;   /* (its Rigidbody2D: gravity 0.05) */
    float dx = b->vx * DT, dy = b->vy * DT, l = sqrtf(dx * dx + dy * dy);
    PhysHit hit;
    if (l > 0 && phys_ray(b->x, b->y, dx / l, dy / l, l + BULLET_HX, CF_SOLID, &hit)) {
      b->x = hit.x - dx / l * BULLET_HX, b->y = hit.y - dy / l * BULLET_HX;
      b->ang = atan2f(hit.ny, hit.nx) * 180 / (float)M_PI + 90;
      bullet_impact(b);
      continue;
    }
    b->x += dx, b->y += dy;
    b->ang = atan2f(b->vy, b->vx) * 180 / (float)M_PI;
    if (b->y < -10) b->on = false;
  }
}

/* (a shot that touched the HeroBox bursts there) */
static int bullets_touch_hero(float x0, float y0, float x1, float y1, int *side) {
  for (int i = 0; i < MAX_BULLETS; i++) {
    Bullet *b = &bullets[i];
    if (!b->on || !b->active) continue;
    float hx = BULLET_HX * b->scale, hy = BULLET_HY * b->scale;
    if (x1 > b->x - hx && x0 < b->x + hx && y1 > b->y - hy && y0 < b->y + hy) {
      *side = b->vx < 0 ? SIDE_RIGHT : SIDE_LEFT;
      bullet_impact(b);
      return 1;
    }
  }
  return 0;
}

static void bullets_draw(void) {
  for (int i = 0; i < MAX_BULLETS; i++) {
    const Bullet *b = &bullets[i];
    if (!b->on) continue;
    /* (stretched along its flight) */
    float sp = sqrtf(b->vx * b->vx + b->vy * b->vy), sy = 1 - sp * 1.2f * 0.01f, sx = 1 + sp * 1.2f * 0.01f;
    if (!b->active) sx = sy = 1;
    Inst in;
    sprite_inst_rot(b->anim.sprite, b->x, b->y, -0.01f, sx * b->scale, sy * b->scale, b->ang, 0, &in);
    gfx_actor(&in, SORT_KEY(0, 0));
  }
}

/* ---------------------------------------------------------------- Aspid Hunters: the spitter FSM */
enum { SP_IDLE, SP_DISTANCE_FLY, SP_RAYCAST, SP_FLY_BACK, SP_FIRE_ANTIC, SP_FIRE, SP_DRIBBLE };

static bool spitter_unalert_range(const Enemy *e) {
  /* the hero in its Unalert Range (its circle) */
  const Ent *d = ent_at(e->ent);
  for (int i = 1; i <= d->s0; i++) {
    const Ent *r = &d[i];
    if (r->type != ENT_BOX || r->flags != ET_RANGE) continue;
    float dx = hero_x() - (e->body.x + r->x0 * (e->sx < 0 ? -1 : 1)), dy = hero_y() - (e->body.y + r->y0);
    return dx * dx + dy * dy < r->x1 * r->x1;
  }
  return true;
}

static void spitter_idle(Enemy *e) {
  e->st = SP_IDLE;
  anim_play_from_frame(&e->anim, CLIP(e, R_A1), 2);
  e->start_x = e->body.x, e->start_y = e->body.y;
  e->wait = 0, e->ax = e->ay = 0;
}

static void spitter_distance_fly(Enemy *e) {
  e->st = SP_DISTANCE_FLY;
  anim_play_from_frame(&e->anim, CLIP(e, R_A1), 2);
  e->t0 = rand_range(1.5f, 2.25f);
}

static void spitter_start(Enemy *e, const Ent *d) {
  spitter_idle(e);
  if (d->s1 & 1) spitter_distance_fly(e);   /* (startAlert) */
}

static void spitter_face(Enemy *e, bool turn_clip) {
  float want = hero_x() > e->body.x ? -fabsf(e->sx) : fabsf(e->sx);
  if (e->sx != want) {
    set_scale_x(e, want);
    if (turn_clip) anim_play_from_frame(&e->anim, CLIP(e, R_A2), 0);
  }
}

static void spitter_fixed(Enemy *e) {
  switch (e->st) {
    case SP_IDLE: idle_buzz(e); break;
    case SP_DISTANCE_FLY:
    case SP_RAYCAST: distance_fly(e, 7, 4, 0.1f, false); break;
    case SP_FLY_BACK: distance_fly(e, 8.25f, 4, 0.1f, false); break;
    case SP_FIRE_ANTIC: distance_fly(e, 9, 2, 0.1f, false); break;
    default: break;
  }
}

static void spitter_update(Enemy *e) {
  /* the range out timer: out of its unalert range 8 s, it gives up */
  if (e->st >= SP_DISTANCE_FLY && e->st <= SP_RAYCAST) {
    if (spitter_unalert_range(e)) e->t1 = 0;
    else if ((e->t1 += DT) > 8) {
      spitter_idle(e);
      return;
    }
  }
  switch (e->st) {
    case SP_IDLE:
      face_direction(e, &e->pause0, CLIP(e, R_A2));
      if (e->can_see && e->in_alert) spitter_distance_fly(e);
      break;
    case SP_DISTANCE_FLY:
      spitter_face(e, true);
      if ((e->t0 -= DT) <= 0) {
        /* Raycast: near enough, nothing between: fire */
        float dx = hero_x() - e->body.x, dy = hero_y() - e->body.y, l = sqrtf(dx * dx + dy * dy);
        if (l > 14) spitter_distance_fly(e);
        else {
          bool blocked = l > 1e-4f && phys_ray(e->body.x, e->body.y, dx / l, dy / l, l, CF_TERRAIN, NULL);
          if (blocked) spitter_distance_fly(e);
          else e->st = SP_FLY_BACK, e->t0 = 0.5f;
        }
      }
      break;
    case SP_FLY_BACK:
      spitter_face(e, true);
      if ((e->t0 -= DT) <= 0) {
        e->st = SP_FIRE_ANTIC;
        anim_play_from_frame(&e->anim, CLIP(e, R_A3), 0);
      }
      break;
    case SP_FIRE_ANTIC:
      spitter_face(e, false);
      if (e->anim.events & ANIM_TRIGGER) {
        /* Fire: its shot at the hero */
        bullet_fire(e->body.x, e->body.y, 15);
        spitter_face(e, false);
        e->st = SP_DRIBBLE;
      }
      break;
    case SP_DRIBBLE:
      if ((e->anim.events & ANIM_DONE) || !e->anim.playing) spitter_distance_fly(e);
      break;
  }
}

/* ---------------------------------------------------------------- Husks: the Zombie Swipe FSM, a Walker */
enum { ZS_READY, ZS_ANTICIPATE, ZS_LUNGE, ZS_COOLDOWN, ZS_IDLE };
#define ZS_LUNGE_SPEED 6.0f
#define ZS_IDLE_TIME 0.25f

static void husk_start(Enemy *e) {
  walker_init(e);
  e->st = ZS_READY;
}

static void husk_ready(Enemy *e) {
  /* Reset: StartWalker; (Coward: no) Ready */
  e->st = ZS_READY;
  walker_start(e);
}

static void husk_attack(Enemy *e) {
  /* Left or Right?: the walker stopped, facing the hero (Face Left / Face Right) */
  walker_stop(e, STOP_CONTROLLED);
  bool right = hero_x() > e->body.x;
  const Ent *v = enemy_rec(e, ET_VARS);
  float speed = v ? v->p0 : ZS_LUNGE_SPEED;
  e->ax = right ? speed : -speed;
  set_scale_x(e, right ? -fabsf(e->sx) : fabsf(e->sx));
  e->wk_facing = right ? 1 : -1;   /* (SetWalkerFacing: ChangeFacing) */
  /* Anticipate */
  e->body.vx = e->body.vy = 0;
  anim_play_from_frame(&e->anim, CLIP(e, R_A1), 0);
  e->st = ZS_ANTICIPATE;
}

static void husk_update(Enemy *e) {
  bool done = (e->anim.events & ANIM_DONE) != 0;
  if (e->flags & 2) {   /* (RECOIL HORIZONTAL: Reset) */
    e->flags &= (uint8_t)~2;
    if (e->st != ZS_READY) {
      husk_ready(e);
      return;
    }
  }
  switch (e->st) {
    case ZS_READY:
      walker_update(e);
      if ((e->can_see && e->in_alert) || e->b1) {
        e->b1 = false;
        husk_attack(e);
      }
      break;
    case ZS_ANTICIPATE:
      if (done) {
        e->st = ZS_LUNGE;
        anim_play_from_frame(&e->anim, CLIP(e, R_A2), 0);
      }
      break;
    case ZS_LUNGE:
      if (done) {
        e->st = ZS_COOLDOWN;
        anim_play_from_frame(&e->anim, CLIP(e, R_A3), 0);
        e->body.vx = 0;
      }
      break;
    case ZS_COOLDOWN:
      if (done) {
        e->st = ZS_IDLE, e->t0 = 0;
        anim_play(&e->anim, CLIP(e, R_IDLE));
      }
      break;
    case ZS_IDLE: {
      const Ent *v = enemy_rec(e, ET_VARS);
      if ((e->t0 += DT) >= (v ? v->p1 : ZS_IDLE_TIME)) husk_ready(e);
      break;
    }
      break;
  }
}

static void husk_fixed(Enemy *e) {
  if (e->st == ZS_LUNGE) e->body.vx = e->ax;   /* (SetVelocity2d every frame) */
  else if (e->st == ZS_READY && e->wk_state == WK_WALKING) {
    const Ent *w = walker_rec(e);
    if (w) e->body.vx = e->wk_facing > 0 ? w->y0 : w->x0;
  }
}

/* ---------------------------------------------------------------- the Hollow Shade: its Shade Control FSM */
enum {
  SH_IDLE, SH_STARTLE, SH_FLY, SH_POSITION, SH_SLASH_ANTIC, SH_SLASH, SH_SLASH_BOX, SH_SLASH_CD, SH_FIREBALL_POS,
  SH_CAST_ANTIC, SH_CAST_CHARGE, SH_CHARGE_WAIT, SH_CAST, SH_DECEL, SH_COOLDOWN, SH_RETREAT_START, SH_RETREAT,
  SH_RETREAT_END, SH_DEATH_START, SH_DEATH, SH_DEATH_END, SH_DISSIPATE, SH_LEAVE_PAUSE, SH_DEPART, SH_GONE
};
#define SHADE_MAX_ROAM 25.0f
#define MAX_BALLS 3
/* the shade's (Shadow Ball): flies 1 s, then slows and fades */
typedef struct {
  bool on, ending;
  float x, y, vx, t;
  Anim anim;
} Ball;
static Ball balls[MAX_BALLS];
static struct {
  bool on, slash_on;   /* (its Slash child's collider) */
  Anim slash;
  int8_t sp;           /* its soul: casts left */
  float qx, qy;        /* (the tween back to its start) */
} sh;

/* the Slash's polygon (Slash's local points, at its place and x scale), on the shade facing its way */
static const float shade_slash_pts[5][2] = {{-1.8682f, -0.2095f}, {0.8395f, 0.0287f}, {-0.9918f, -0.9513f}, {-2.0697f, -0.8551f},
                                            {-2.4798f, -0.5897f}};

static void shade_face_hero(Enemy *e, int clip) {
  /* FaceObject (sprite facing left), playing its turn clip on turning */
  float want = hero_x() > e->body.x ? -1.0f : 1.0f;
  if (e->sx != want) {
    set_scale_x(e, want);
    if (clip >= 0) anim_play_from_frame(&e->anim, clip, 0);
  }
}

static float hero_dist(const Enemy *e) {
  float dx = e->body.x - hero_x(), dy = e->body.y - hero_y();
  return sqrtf(dx * dx + dy * dy);
}

static bool roamed_off(const Enemy *e) {
  float dx = e->body.x - e->start_x, dy = e->body.y - e->start_y;
  return dx * dx + dy * dy > SHADE_MAX_ROAM * SHADE_MAX_ROAM;
}

static void shade_retreat(Enemy *e) {
  /* Retreat Start: no collider, slowing */
  e->st = SH_RETREAT_START;
  e->flags |= 1;   /* (collider off) */
  anim_play_from_frame(&e->anim, CLIP_SHADE_RETREAT_START, 0);
}

static void shade_fly(Enemy *e) {
  e->st = SH_FLY;
  anim_play(&e->anim, CLIP_SHADE_FLY);
  e->wait = rand_range(1, 2);
}

/* Attack Choice (after Quake? and Scream?: the shade has neither here): a fireball a fifth of the time, if it can */
static void shade_attack_choice(Enemy *e) {
  for (;;) {
    if (rand_range(0, 1) < 0.2f) {
      /* Sp Check */
      if (g_pd.shade_fireball_level >= 1 && sh.sp >= 1) {
        sh.sp--;
        e->st = SH_FIREBALL_POS, e->t0 = 0;
        return;
      }
      continue;   /* (RETURN: chooses again) */
    }
    /* Q Other?: nothing else to choose; Position */
    e->st = SH_POSITION, e->t0 = 0;
    return;
  }
}

static void shade_spawn(void) {
  Enemy *e = NULL;
  for (int i = 0; i < MAX_ENEMIES && !e; i++)
    if (en[i].mode == EM_OFF) e = &en[i];
  if (!e) return;
  memset(e, 0, sizeof *e);
  memset(&sh, 0, sizeof sh);
  memset(balls, 0, sizeof balls);
  sh.on = true;
  e->mode = EM_ALIVE, e->kind = EK_SHADE, e->ent = NO_ENT;
  e->damage = 1, e->z = 0.006f;
  e->rc_base = 15, e->rc_dur = 0.15f;
  /* (Init: its hp the shade's health in nail hits) */
  e->hp = (int16_t)(g_pd.nail_damage * g_pd.shade_health);
  sh.sp = (int8_t)g_pd.shade_mp;
  Body *b = &e->body;
  b->x = g_pd.shade_x, b->y = g_pd.shade_y;
  b->ox = 0, b->oy = -0.37f, b->hx = 0.25f, b->hy = 0.57f;
  b->gravity_scale = 0, b->mask = CF_SOLID;
  e->ar_r = 7.27f, e->ar_hy = -1;
  e->sx = 1;
  e->start_x = b->x, e->start_y = b->y;
  shade_face_hero(e, -1);
  e->st = SH_IDLE;
  anim_play(&e->anim, CLIP_SHADE_IDLE);
}

void shade_spawn_check(void) {
  if (g_pd.soul_limited && !strcmp(g_pd.shade_scene, room_name(g_room.id))) shade_spawn();
}

/* (Killed: the Hollow Shade Death in its place) */
static void shade_killed(Enemy *e) {
  /* Death Start: the soul unlimited, the geo back, the shade gone from the save */
  g_pd.soul_limited = false;
  g_pd.max_mp = 99;
  int geo_back = g_pd.geo_pool;
  g_pd.geo += geo_back;
  g_pd.geo_pool = 0;
  strcpy(g_pd.shade_scene, "None");
  e->body.vx = e->body.vy = 0;
  e->flags |= 1;
  sh.slash_on = false;
  cam_shake(SHAKE_AVERAGE);
  anim_play(&e->anim, CLIP_SHADE_DEATH_START);
  e->st = SH_DEATH_START, e->t0 = 0;
  e->mode = EM_CORPSE;
}

/* HERO LEAVE: the shade departs (Hollow Shade Depart) */
void enemies_hero_leave(void) {
  for (int i = 0; i < MAX_ENEMIES; i++) {
    Enemy *e = &en[i];
    if (FSM(e) != EF_SHADE || e->mode != EM_ALIVE) continue;
    e->mode = EM_CORPSE, e->flags |= 1, sh.slash_on = false;
    anim_play(&e->anim, CLIP_SHADE_FLY);
    shade_face_hero(e, CLIP_SHADE_TURNTOFLY);
    e->st = SH_LEAVE_PAUSE, e->t0 = 0;
  }
}

static void shade_fixed(Enemy *e) {
  switch (e->st) {
    case SH_FLY: {
      chase_object(e, 4, 0.2f);
      /* ChaseObjectV2: a force towards the hero, the speed clamped (the force acts in the step) */
      float dx = hero_x() - e->body.x, dy = hero_y() - e->body.y, l = sqrtf(dx * dx + dy * dy);
      if (l > 1) dx /= l, dy /= l;
      float sp = sqrtf(e->body.vx * e->body.vx + e->body.vy * e->body.vy);
      if (sp > 4) e->body.vx *= 4 / sp, e->body.vy *= 4 / sp;
      e->body.vx += dx * 8 * DT, e->body.vy += dy * 8 * DT;
      break;
    }
    case SH_POSITION:
    case SH_SLASH_ANTIC: distance_fly(e, 3, 4, 0.2f, true); break;
    case SH_FIREBALL_POS: distance_fly(e, 12, 4, 0.2f, true); break;
    case SH_CAST_ANTIC: e->body.vx *= 0.8f, e->body.vy *= 0.8f; break;
    case SH_DECEL:
    case SH_COOLDOWN:
    case SH_LEAVE_PAUSE: e->body.vx *= 0.9f, e->body.vy *= 0.9f; break;
    case SH_RETREAT_START: e->body.vx *= 0.85f, e->body.vy *= 0.85f; break;
    default: break;
  }
}

static void shade_update(Enemy *e) {
  bool done = (e->anim.events & ANIM_DONE) != 0;
  e->t0 += DT;
  switch (e->st) {
    case SH_IDLE:
      if ((e->in_alert && e->can_see) || e->b1) {
        e->b1 = false;
        e->st = SH_STARTLE;
        anim_play_from_frame(&e->anim, CLIP_SHADE_STARTLE, 0);
      }
      break;
    case SH_STARTLE:
      if (done) shade_fly(e);
      break;
    case SH_FLY:
      shade_face_hero(e, CLIP_SHADE_TURNTOFLY);
      if (roamed_off(e)) shade_retreat(e);
      else if ((e->wait -= DT) <= 0) shade_attack_choice(e);
      break;
    case SH_POSITION: {
      shade_face_hero(e, CLIP_SHADE_TURNTOFLY);
      bool same_y = fabsf(hero_y() - e->body.y) <= 0.2f;
      if (hero_dist(e) < 5 && same_y) {
        e->st = SH_SLASH_ANTIC;
        anim_play_from_frame(&e->anim, CLIP_SHADE_SLASH_ANTIC, 0);
      } else if (roamed_off(e))
        shade_retreat(e);
      else if (e->t0 >= 6)
        shade_attack_choice(e);   /* (END: Quake?, Scream?, Attack Choice) */
      break;
    }
    case SH_SLASH_ANTIC:
      if (done) {
        /* Check Dir, Right / Left: a lunge its way; Slash */
        e->body.vx = e->sx < 0 ? 8 : -8;
        sh.slash_on = true;
        anim_play_from_frame(&sh.slash, CLIP_SHADE_SLASH_EFFECT, 0);
        anim_play_from_frame(&e->anim, CLIP_SHADE_SLASH, 0);
        e->st = SH_SLASH, e->t0 = 0;
      }
      break;
    case SH_SLASH:
      if (e->t0 >= 0.083f) e->st = SH_SLASH_BOX;   /* (its clip may have ended meanwhile: Slash Box sees that) */
      break;
    case SH_SLASH_BOX:
      if (done || !e->anim.playing) {
        sh.slash_on = false;
        e->st = SH_SLASH_CD;
        anim_play_from_frame(&e->anim, CLIP_SHADE_SLASH_CD, 0);
      }
      break;
    case SH_SLASH_CD:
      if (done) shade_fly(e);
      break;
    case SH_FIREBALL_POS: {
      shade_face_hero(e, CLIP_SHADE_TURNTOFLY);
      bool same_y = fabsf(hero_y() - e->body.y) <= 0.2f, at = fabsf(hero_dist(e) - 12) <= 1;
      if ((at && same_y) || e->t0 >= 3) {
        e->st = SH_CAST_ANTIC, e->t0 = 0;
        anim_play(&e->anim, CLIP_SHADE_CAST_ANTIC);
      } else if (roamed_off(e))
        shade_retreat(e);
      break;
    }
    case SH_CAST_ANTIC:
      if (e->t0 >= 0.5f) {
        e->st = SH_CAST_CHARGE, e->t0 = 0;
        anim_play(&e->anim, CLIP_SHADE_CAST_CHARGE);
        e->body.vx = e->body.vy = 0;
        /* Check Dir 2: it will be pushed back as it casts */
        e->ax = e->sx < 0 ? -10 : 10;
        e->st = SH_CHARGE_WAIT;
      }
      break;
    case SH_CHARGE_WAIT:
      if (e->t0 >= 0.25f) {
        /* Shoot R / L: a Shadow Ball, fast its way */
        for (int i = 0; i < MAX_BALLS; i++)
          if (!balls[i].on) {
            Ball *b = &balls[i];
            float k = e->sx < 0 ? 1.0f : -1.0f;
            b->on = true, b->ending = false, b->t = 0;
            b->x = e->body.x + 1.764f * k, b->y = e->body.y + 0.12f, b->vx = 20 * k;
            anim_play_from_frame(&b->anim, CLIP_SHADE_FIREBALL, 0);
            break;
          }
        cam_shake(SHAKE_AVERAGE);
        anim_play_from_frame(&e->anim, CLIP_SHADE_CAST, 0);
        e->body.vx = e->ax;
        e->st = SH_DECEL, e->t0 = 0;
      }
      break;
    case SH_DECEL:
      if (done) {
        e->st = SH_COOLDOWN, e->t0 = 0;
        anim_play(&e->anim, CLIP_SHADE_FLY);
      }
      break;
    case SH_COOLDOWN:
      if (e->t0 >= 0.25f) shade_fly(e);
      break;
    case SH_RETREAT_START:
      if (done) {
        e->body.vx = e->body.vy = 0;
        sh.qx = e->body.x, sh.qy = e->body.y;
        e->st = SH_RETREAT, e->t0 = 0;
      }
      break;
    case SH_RETREAT: {
      /* iTweenMoveTo its start, 1 s, easeInOutCubic */
      float t = e->t0 >= 1 ? 1 : e->t0, k = t < 0.5f ? 4 * t * t * t : 1 - powf(-2 * t + 2, 3) / 2;
      e->body.x = sh.qx + (e->start_x - sh.qx) * k, e->body.y = sh.qy + (e->start_y - sh.qy) * k;
      if (e->t0 >= 1) {
        e->st = SH_RETREAT_END;
        anim_play_from_frame(&e->anim, CLIP_SHADE_RETREAT_END, 0);
      }
      break;
    }
    case SH_RETREAT_END:
      if (done) {
        anim_play(&e->anim, CLIP_SHADE_IDLE);
        e->flags &= (uint8_t)~1;
        e->st = SH_IDLE;
      }
      break;
    /* (Hollow Shade Death) */
    case SH_DEATH_START:
      e->jx = rand_range(-0.1f, 0.1f), e->jy = rand_range(-0.1f, 0.1f);   /* (ObjectJitter) */
      if (e->t0 >= 0.5f) {
        e->jx = e->jy = 0;
        anim_play_from_frame(&e->anim, CLIP_SHADE_DEATH, 0);
        cam_shake(SHAKE_AVERAGE);
        e->st = SH_DEATH_END, e->t0 = 0;
      }
      break;
    case SH_DEATH_END:
      if (e->t0 >= 0.75f) e->st = SH_DISSIPATE, e->t0 = 0;
      break;
    case SH_DISSIPATE:
      if (e->t0 >= 1) {
        hud_soul_limiter(false);   /* (Give Geo: SOUL LIMITER DOWN) */
        e->mode = EM_OFF, sh.on = false;
      }
      break;
    case SH_LEAVE_PAUSE:
      if (e->t0 >= 2) {
        e->body.vx = e->body.vy = 0;
        anim_play_from_frame(&e->anim, CLIP_SHADE_DEPART, 0);
        e->st = SH_DEPART, e->t0 = 0;
      }
      break;
    case SH_DEPART:
      if (e->t0 >= 0.6f) e->mode = EM_OFF, sh.on = false;
      break;
  }
  if (sh.slash_on) anim_update(&sh.slash, DT);
}

static void balls_tick(void) {
  for (int i = 0; i < MAX_BALLS; i++) {
    Ball *b = &balls[i];
    if (!b->on) continue;
    b->t += DT;
    b->anim.events = 0;
    anim_update(&b->anim, DT);
    if (!b->ending && b->t >= 1) {
      b->ending = true;
      anim_play_from_frame(&b->anim, CLIP_SHADE_FIREBALL_END, 0);
    }
    if (b->ending) {
      b->vx *= 0.1f;
      if (b->anim.events & ANIM_DONE) b->on = false;
    }
    b->x += b->vx * DT;
  }
}

/* ---------------------------------------------------------------- the room's */
void enemies_enter(void) {
  memset(en, 0, sizeof en);
  memset(geo, 0, sizeof geo);
  int n, k = 0;
  const Ent *es = room_ents(&n);
  for (int i = 0; i < n && k < MAX_ENEMIES; i++) {
    const Ent *d = &es[i];
    if (d->type != ENT_OBJ || d->flags != OK_ENEMY || persist_get(d->persist)) continue;
    Enemy *e = &en[k++];
    e->mode = EM_ALIVE, e->kind = (uint8_t)d->a, e->ent = (uint16_t)i;
    e->damage = (int8_t)d->p1, e->geo_s = (uint8_t)d->p2, e->geo_m = (uint8_t)d->p3, e->geo_l = (uint8_t)d->group;
    e->z = d->x1;
    e->sx = d->y1 ? d->y1 : 1;
    e->hp = (int16_t)d->p0;
    Body *b = &e->body;
    b->x = d->x0, b->y = d->y0;
    const Ent *box = enemy_rec(e, ET_COLLIDER);
    if (box) b->ox = box->x0 * (e->sx < 0 ? -1 : 1), b->oy = box->y0, b->hx = box->x1, b->hy = box->y1, b->gravity_scale = box->p0;
    b->friction = 0.2828f, b->mask = CF_SOLID;   /* (the default material with the terrain's) */
    /* its sight: the alert range, else its first other range */
    const Ent *ar = enemy_rec(e, ET_ALERT);
    if (!ar) ar = enemy_rec(e, ET_RANGE);
    if (ar) e->ar_x = ar->x0 * (e->sx < 0 ? -1 : 1), e->ar_y = ar->y0, e->ar_r = ar->x1, e->ar_hy = ar->y1;
    const Ent *rc = enemy_rec(e, ET_RECOIL);
    e->rc_base = rc ? rc->x0 : 15, e->rc_dur = rc ? rc->y0 : 0.5f, e->rc_flags = rc ? (uint8_t)rc->a : 0;
    const Ent *wk = enemy_rec(e, ET_WALKER);
    e->wk_rec = wk ? (uint16_t)(wk - room_ents(&n)) : 0;
    if (FSM(e) == EF_CRAWLER) crawler_start(e, d);
    else if (FSM(e) == EF_BUZZER) buzzer_start(e, d);
    else if (FSM(e) == EF_HUSK) husk_start(e);
    else if (FSM(e) == EF_CLIMBER) climber_start(e);
    else if (FSM(e) == EF_BOUNCER) bouncer_start(e);
    else if (FSM(e) == EF_SPITTER) spitter_start(e, d);
  }
  memset(&sh, 0, sizeof sh);
  memset(balls, 0, sizeof balls);
  memset(bullets, 0, sizeof bullets);
  shade_spawn_check();
}

/* FixedUpdate and the physics step */
void enemies_fixed(void) {
  for (int i = 0; i < MAX_ENEMIES; i++) {
    Enemy *e = &en[i];
    if (e->mode == EM_OFF) continue;
    if (FSM(e) == EF_SHADE) {
      if (e->mode == EM_ALIVE) shade_fixed(e), recoil_fixed(e);
      else shade_fixed(e);
      if (e->st != SH_RETREAT) body_step(&e->body, DT);
      continue;
    }
    if (e->mode == EM_ALIVE && FSM(e) == EF_CLIMBER) {
      /* (a kinematic body: its velocity, nothing in its way) */
      recoil_fixed(e);
      e->body.x += e->body.vx * DT, e->body.y += e->body.vy * DT;
      continue;
    }
    if (e->mode == EM_ALIVE) {
      if (FSM(e) == EF_CRAWLER) crawler_fixed(e);
      else if (FSM(e) == EF_BUZZER) buzzer_fixed(e);
      else if (FSM(e) == EF_HUSK) husk_fixed(e);
      else if (FSM(e) == EF_BOUNCER) bouncer_fixed(e);
      else if (FSM(e) == EF_SPITTER) spitter_fixed(e);
      recoil_fixed(e);
      body_step(&e->body, DT);
    } else {
      /* Corpse: falls, lands (or smashes, a breaker) and stays */
      const Ent *cr = enemy_rec(e, ET_CORPSE);
      bool breaker = cr && (cr->a & CF_BREAKER);
      float cbounce = cr ? cr->p1 : -1;
      float pvx = e->body.vx, pvy = e->body.vy;
      int had = e->body.ncontacts;
      if (e->st != CS_LANDED || e->body.vx || e->body.vy) body_step(&e->body, DT);
      if (e->st == CS_AIR) {
        bool ground = false;
        for (int c = 0; c < e->body.ncontacts; c++) ground |= e->body.cny[c] > 0.5f;
        if (ground) {
          if (breaker) {
            anim_play(&e->anim, kinds[e->kind].clip[R_DEATH_LAND]);
            e->body.vx = e->body.vy = 0;
            e->st = CS_DEATH_ANIM;
          } else {
            bounce(&e->body, pvx, pvy, had, cbounce);
            anim_play(&e->anim, kinds[e->kind].clip[R_DEATH_LAND]);
            e->st = CS_LANDED;
          }
        } else
          bounce(&e->body, pvx, pvy, had, cbounce);
        if (e->body.y < -10) e->mode = EM_OFF;
      }
    }
  }
  geo_tick();
  balls_tick();
  bullets_tick();
}

/* Update: the FSMs' every-frame actions, the animations, the timers */
void enemies_update(void) {
  for (int i = 0; i < MAX_ENEMIES; i++) {
    Enemy *e = &en[i];
    if (e->mode == EM_OFF) continue;
    e->anim.events = 0;
    anim_update(&e->anim, DT);
    if (e->flashing && (e->flash_t += DT) > 0.27f) e->flashing = false;
    if (e->mode == EM_ALIVE) {
      if (e->evasion > 0) e->evasion -= DT;
      if (e->ar_r > 0) sight_update(e);
      if (FSM(e) == EF_BUZZER) buzzer_update(e, ent_at(e->ent));
      else if (FSM(e) == EF_SHADE) shade_update(e);
      else if (FSM(e) == EF_HUSK) husk_update(e);
      else if (FSM(e) == EF_CLIMBER) climber_update(e);
      else if (FSM(e) == EF_BOUNCER) bouncer_update(e);
      else if (FSM(e) == EF_SPITTER) spitter_update(e);
    } else if (FSM(e) == EF_SHADE)
      shade_update(e);
    else if (e->st == CS_DEATH_ANIM && !e->anim.playing)
      e->mode = EM_OFF;
  }
}

void enemies_draw(void) {
  for (int i = 0; i < MAX_ENEMIES; i++) {
    const Enemy *e = &en[i];
    if (e->mode == EM_OFF) continue;
    Inst in;
    float z = e->mode == EM_CORPSE && FSM(e) != EF_SHADE ? 0.0085f : e->z;
    if (FSM(e) == EF_SHADE && e->st == SH_DISSIPATE) continue;   /* (its renderer off) */
    if (e->ang != 0 && e->mode == EM_ALIVE)
      sprite_inst_rot(e->anim.sprite, e->body.x, e->body.y, z, e->sx, 1, e->ang, flash_tint(e, 2 + i % 5), &in);
    else
      sprite_inst(e->anim.sprite, e->body.x + e->jx, e->body.y + e->jy, z, e->sx, 1, flash_tint(e, 2 + i % 5), &in);
    gfx_actor(&in, SORT_KEY(0, 0));
    if (FSM(e) == EF_SHADE && sh.slash_on) {
      sprite_inst(sh.slash.sprite, e->body.x - 0.0486f * e->sx, e->body.y - 0.0117f, z - 0.001f, 1.27f * e->sx, 1, 0, &in);
      gfx_actor(&in, SORT_KEY(0, 0));
    }
  }
  bullets_draw();
  for (int i = 0; i < MAX_BALLS; i++) {
    const Ball *b = &balls[i];
    if (!b->on) continue;
    Inst in;
    sprite_inst(b->anim.sprite, b->x, b->y, -0.1f, b->vx < 0 ? -1.0f : 1.0f, 1, 0, &in);
    gfx_actor(&in, SORT_KEY(0, 0));
  }
  for (int i = 0; i < MAX_GEO; i++) {
    const Geo *g = &geo[i];
    if (!g->on) continue;
    Inst in;
    sprite_inst(g->anim.sprite, g->body.x, g->body.y, 0.0015f, GEO_SCALE, GEO_SCALE, 0, &in);
    gfx_actor(&in, SORT_KEY(0, 0));
  }
}

/* ---------------------------------------------------------------- what touches them */
void enemies_swing_start(void) { swing_bits = 0; }

/* the slash's shape: enemies it touches are hit, once a swing -> HB_* */
int enemies_nail(const float *pts, int npts, float direction, int damage) {
  int out = 0;
  for (int i = 0; i < MAX_ENEMIES; i++) {
    Enemy *e = &en[i];
    if (e->mode != EM_ALIVE || (e->flags & 1)) continue;
    float x0, y0, x1, y1;
    enemy_box(e, &x0, &y0, &x1, &y1);
    if (!box_meets_shape(x0, y0, x1, y1, pts, npts)) continue;
    out |= HB_BOUNCE | HB_RECOIL;
    if (swing_bits >> i & 1) continue;
    swing_bits |= (uint16_t)(1 << i);
    enemy_hit(e, direction, damage);
  }
  return out;
}

/* DamageHero: the first enemy touching the HeroBox -> its damage (0: none), which side it is on */
int enemies_touch_hero(float x0, float y0, float x1, float y1, int *side) {
  for (int i = 0; i < MAX_ENEMIES; i++) {
    const Enemy *e = &en[i];
    if (e->mode != EM_ALIVE || e->damage <= 0 || (e->flags & 1)) continue;
    float a0, b0, a1, b1;
    enemy_box(e, &a0, &b0, &a1, &b1);
    if (x1 > a0 && x0 < a1 && y1 > b0 && y0 < b1) {
      *side = e->body.x > g_hero.body.x ? SIDE_RIGHT : SIDE_LEFT;
      return e->damage;
    }
    if (FSM(e) == EF_SHADE && sh.slash_on) {
      /* its Slash's polygon */
      float pts[10];
      for (int k = 0; k < 5; k++)
        pts[2 * k] = e->body.x + (-0.0486f + shade_slash_pts[k][0] * 1.27f) * e->sx, pts[2 * k + 1] = e->body.y - 0.0117f + shade_slash_pts[k][1];
      if (box_meets_shape(x0, y0, x1, y1, pts, 5)) {
        *side = e->body.x > g_hero.body.x ? SIDE_RIGHT : SIDE_LEFT;
        return 1;
      }
    }
  }
  int dmg = bullets_touch_hero(x0, y0, x1, y1, side);
  if (dmg) return dmg;
  for (int i = 0; i < MAX_BALLS; i++) {
    const Ball *b = &balls[i];
    if (!b->on || b->ending) continue;
    float k = b->vx < 0 ? -1.0f : 1.0f, cx = b->x + 0.26f * k, cy = b->y - 0.0496f;
    if (x1 > cx - 0.99f && x0 < cx + 0.99f && y1 > cy - 0.6204f && y0 < cy + 0.6204f) {
      *side = b->x > g_hero.body.x ? SIDE_RIGHT : SIDE_LEFT;
      return 1;
    }
  }
  return 0;
}

#ifdef HOST
#include <stdio.h>
void enemies_debug(void) {
  for (int i = 0; i < MAX_ENEMIES; i++) {
    const Enemy *e = &en[i];
    if (e->mode == EM_OFF) continue;
    printf("   enemy %d kind %d mode %d st %d hp %d pos %.2f,%.2f v %.2f,%.2f sx %.0f rc %d see %d/%d\n", i, e->kind, e->mode,
           e->st, e->hp, e->body.x, e->body.y, e->body.vx, e->body.vy, e->sx, e->rc_state, e->in_alert, e->can_see);
  }
  int ng = 0;
  for (int i = 0; i < MAX_GEO; i++) ng += geo[i].on;
  printf("   geo %d pieces, %d in purse, mp %d\n", ng, g_pd.geo, g_pd.mp);
}
#endif
