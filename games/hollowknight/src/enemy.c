/* Enemies (tools/ents.py: OK_ENEMY): HealthManager, Recoil, SpriteFlash, DamageHero and EnemyDeathEffects, each kind's
 * FSM, the corpses they leave and the geo they drop. */
#include <math.h>
#include "game.h"

#define DT 0.02f
#define MAX_ENEMIES 16
#define MAX_GEO 24
#define TERRAIN_FRICTION 0.2f   /* (the Terrain material; with another, their geometric mean) */

/* ---------------------------------------------------------------- what each kind is */
enum { EK_CRAWLER = 1, EK_BUZZER, EK_SHADE, EK_HUSK };
#define NO_ENT 0xFFFF   /* (an enemy spawned, not one of the room's) */
/* the records after an enemy's (tools/ents.py: ET_*) */
enum { ET_COLLIDER, ET_ALERT, ET_RANGE, ET_WALKER, ET_RECOIL, ET_CORPSE };
enum { CF_BREAKER = 1, CF_FACES_RIGHT = 2, CF_LOW_ARC = 4 };   /* a corpse's (ET_CORPSE) */
enum { WF_PAUSES = 1, WF_IGNORE_HOLES = 2, WF_NO_TURN_TO_HERO = 4, WF_START_INACTIVE = 8, WF_AMBUSH = 16, WF_WAIT_HERO_X = 32,
       WF_PREVENT_TURN = 64, WF_NO_SCALE = 128, WF_RIGHT_NEG = 256 };   /* a Walker's (ET_WALKER) */
typedef struct {
  int clip_death_air, clip_death_land;   /* its corpse's (Corpse: Death Air, Death Land) */
  int clip_idle, clip_turn, clip_walk;   /* (Walker's) */
} Kind;
static const Kind kinds[] = {
    [EK_CRAWLER] = {CLIP_CRAWLER_DEATH_AIR, CLIP_CRAWLER_DEATH_LAND, -1, -1, -1},
    [EK_BUZZER] = {CLIP_BUZZER_DEATH_AIR, CLIP_BUZZER_DEATH_LAND, -1, -1, -1},
    [EK_SHADE] = {-1, -1, -1, -1, -1},
    [EK_HUSK] = {CLIP_HUSK_DEATH_AIR, CLIP_HUSK_DEATH_LAND, CLIP_HUSK_IDLE, CLIP_HUSK_TURN, CLIP_HUSK_WALK},
};

/* ---------------------------------------------------------------- an enemy */
enum { EM_OFF, EM_ALIVE, EM_CORPSE };
enum { RC_READY, RC_RECOILING };
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
static void recoil_by_direction(Enemy *e, int dir, float magnitude) {
  if (e->rc_state != RC_READY) return;
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
  const Kind *k = &kinds[e->kind];
  const Ent *c = enemy_rec(e, ET_CORPSE);
  if (!c || k->clip_death_air < 0) {
    e->mode = EM_OFF;   /* (no corpse) */
    return;
  }
  e->mode = EM_CORPSE, e->st = CS_AIR;
  e->body.y += c->p3;
  e->body.ox = c->x0, e->body.oy = c->y0, e->body.hx = c->x1, e->body.hy = c->y1;
  e->body.gravity_scale = c->p0, e->body.friction = 0.2f, e->body.mask = CF_SOLID;
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
  anim_play(&e->anim, k->clip_death_air);
  e->flashing = true, e->flash_t = 0;   /* (EmitInfectedEffects: the corpse flashes) */
}

/* HealthManager.Die, EnemyDeathEffects */
static void shade_killed(Enemy *e);
static void enemy_die(Enemy *e, float direction, bool has_direction) {
  if (e->kind == EK_SHADE) {
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
  if (e->kind != EK_SHADE) hero_soul_gain();   /* (enemyType 3, a shade: no soul) */
  e->flashing = true, e->flash_t = 0;
  e->hp = (int16_t)(e->hp - damage < -50 ? -50 : e->hp - damage);
  if ((e->kind == EK_BUZZER || e->kind == EK_SHADE || e->kind == EK_HUSK) && e->st == 0) e->b1 = true;   /* (TOOK DAMAGE, in Idle / Ready) */
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
  anim_play(&e->anim, kinds[e->kind].clip_walk);
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
  anim_play_from_frame(&e->anim, kinds[e->kind].clip_turn, 0);
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
    anim_play(&e->anim, kinds[e->kind].clip_idle);
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
  e->ax = right ? ZS_LUNGE_SPEED : -ZS_LUNGE_SPEED;
  set_scale_x(e, right ? -fabsf(e->sx) : fabsf(e->sx));
  e->wk_facing = right ? 1 : -1;   /* (SetWalkerFacing: ChangeFacing) */
  /* Anticipate */
  e->body.vx = e->body.vy = 0;
  anim_play_from_frame(&e->anim, CLIP_HUSK_ATTACK_ANTICIPATE, 0);
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
        anim_play_from_frame(&e->anim, CLIP_HUSK_ATTACK_LUNGE, 0);
      }
      break;
    case ZS_LUNGE:
      if (done) {
        e->st = ZS_COOLDOWN;
        anim_play_from_frame(&e->anim, CLIP_HUSK_ATTACK_COOLDOWN, 0);
        e->body.vx = 0;
      }
      break;
    case ZS_COOLDOWN:
      if (done) {
        e->st = ZS_IDLE, e->t0 = 0;
        anim_play(&e->anim, CLIP_HUSK_IDLE);
      }
      break;
    case ZS_IDLE:
      if ((e->t0 += DT) >= ZS_IDLE_TIME) husk_ready(e);
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

/* DistanceFly (targets height): keeps a distance from the hero, at its height */
static void distance_fly(Enemy *e, float dist, float vmax, float accel) {
  float vx = e->body.vx, vy = e->body.vy, d = hero_dist(e);
  bool left = e->body.x < hero_x();
  vx += (d > dist) == left ? accel : -accel;
  if (e->body.y < hero_y()) vy += accel;
  if (e->body.y > hero_y()) vy -= accel;
  e->body.vx = vx > vmax ? vmax : vx < -vmax ? -vmax : vx;
  e->body.vy = vy > vmax ? vmax : vy < -vmax ? -vmax : vy;
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
    if (e->kind != EK_SHADE || e->mode != EM_ALIVE) continue;
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
    case SH_SLASH_ANTIC: distance_fly(e, 3, 4, 0.2f); break;
    case SH_FIREBALL_POS: distance_fly(e, 12, 4, 0.2f); break;
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
    if (e->kind == EK_CRAWLER) crawler_start(e, d);
    else if (e->kind == EK_BUZZER) buzzer_start(e, d);
    else if (e->kind == EK_HUSK) husk_start(e);
  }
  memset(&sh, 0, sizeof sh);
  memset(balls, 0, sizeof balls);
  shade_spawn_check();
}

/* FixedUpdate and the physics step */
void enemies_fixed(void) {
  for (int i = 0; i < MAX_ENEMIES; i++) {
    Enemy *e = &en[i];
    if (e->mode == EM_OFF) continue;
    if (e->kind == EK_SHADE) {
      if (e->mode == EM_ALIVE) shade_fixed(e), recoil_fixed(e);
      else shade_fixed(e);
      if (e->st != SH_RETREAT) body_step(&e->body, DT);
      continue;
    }
    if (e->mode == EM_ALIVE) {
      if (e->kind == EK_CRAWLER) crawler_fixed(e);
      else if (e->kind == EK_BUZZER) buzzer_fixed(e);
      else if (e->kind == EK_HUSK) husk_fixed(e);
      recoil_fixed(e);
      body_step(&e->body, DT);
    } else {
      /* Corpse: falls, lands (or smashes, a breaker) and stays */
      const Kind *k = &kinds[e->kind];
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
            anim_play(&e->anim, k->clip_death_land);
            e->body.vx = e->body.vy = 0;
            e->st = CS_DEATH_ANIM;
          } else {
            bounce(&e->body, pvx, pvy, had, cbounce);
            anim_play(&e->anim, k->clip_death_land);
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
      if (e->kind == EK_BUZZER) buzzer_update(e, ent_at(e->ent));
      else if (e->kind == EK_SHADE) shade_update(e);
      else if (e->kind == EK_HUSK) husk_update(e);
    } else if (e->kind == EK_SHADE)
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
    float z = e->mode == EM_CORPSE && e->kind != EK_SHADE ? 0.0085f : e->z;
    if (e->kind == EK_SHADE && e->st == SH_DISSIPATE) continue;   /* (its renderer off) */
    sprite_inst(e->anim.sprite, e->body.x + e->jx, e->body.y + e->jy, z, e->sx, 1, flash_tint(e, 2 + i % 5), &in);
    gfx_actor(&in, SORT_KEY(0, 0));
    if (e->kind == EK_SHADE && sh.slash_on) {
      sprite_inst(sh.slash.sprite, e->body.x - 0.0486f * e->sx, e->body.y - 0.0117f, z - 0.001f, 1.27f * e->sx, 1, 0, &in);
      gfx_actor(&in, SORT_KEY(0, 0));
    }
  }
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
    if (e->kind == EK_SHADE && sh.slash_on) {
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
