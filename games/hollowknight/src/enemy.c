/* Enemies (tools/ents.py: OK_ENEMY): HealthManager, Recoil, SpriteFlash, DamageHero and EnemyDeathEffects, each kind's
 * FSM, the corpses they leave and the geo they drop. */
#include <math.h>
#include "game.h"

#define DT 0.02f
#define MAX_ENEMIES 16
#define MAX_GEO 24
#define TERRAIN_FRICTION 0.2f   /* (the Terrain material; with another, their geometric mean) */

/* ---------------------------------------------------------------- what each kind is */
enum { EK_CRAWLER = 1, EK_BUZZER };
typedef struct {
  /* Recoil */
  float recoil_speed, recoil_time;
  bool stop_vx_up;
  /* the corpse (EnemyDeathEffects, its Corpse prefab) */
  float corpse_spawn_y, corpse_fling, corpse_gravity, corpse_ox, corpse_oy, corpse_hx, corpse_hy, corpse_bounce;
  bool breaker;
  int clip_death_air, clip_death_land;
  float gravity, friction;   /* its Rigidbody2D, its collider's material with the terrain's */
} Kind;
static const Kind kinds[] = {
    [EK_CRAWLER] = {15, 0.15f, true, 0.5f, 15, 0.8f, 0.0078f, -0.3906f, 1.4219f / 2, 0.9062f / 2, 0.3f, false,
                    CLIP_CRAWLER_DEATH_AIR, CLIP_CRAWLER_DEATH_LAND, 1, 0.2828f},
    [EK_BUZZER] = {15, 0.25f, false, 0, 15, 0.7f, 0.1016f, -0.2188f, 1.1406f / 2, 1.3125f / 2, -1, true,
                   CLIP_BUZZER_DEATH_AIR, CLIP_BUZZER_DEATH_LAND, 0, 0.2828f},
};

/* ---------------------------------------------------------------- an enemy */
enum { EM_OFF, EM_ALIVE, EM_CORPSE };
enum { RC_READY, RC_RECOILING };
enum { CS_AIR, CS_DEATH_ANIM, CS_LANDED };
typedef struct {
  uint8_t mode, kind, st, flags;
  uint16_t ent;
  Body body;
  Anim anim;
  float sx;   /* its transform's x scale: which way it faces */
  int16_t hp;
  float evasion;
  /* Recoil */
  uint8_t rc_state;
  int8_t rc_dir;
  bool rc_sweep;
  float rc_time, rc_speed;
  /* SpriteFlash (flashInfected) */
  float flash_t;
  bool flashing;
  /* the FSM's state */
  float t0, t1, wait, start_x, start_y, ax, ay, pause0, pause1;
  bool b0, b1;
  /* alert range (local circle) and sight */
  float ar_x, ar_y, ar_r;
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
  const Kind *k = &kinds[e->kind];
  if (e->rc_state != RC_READY) return;
  e->rc_state = RC_RECOILING;
  e->rc_speed = k->recoil_speed * magnitude;
  e->rc_dir = (int8_t)dir;
  e->rc_sweep = true;
  e->rc_time = k->recoil_time;
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
  e->mode = EM_CORPSE, e->st = CS_AIR;
  e->body.y += k->corpse_spawn_y;
  e->body.ox = k->corpse_ox, e->body.oy = k->corpse_oy, e->body.hx = k->corpse_hx, e->body.hy = k->corpse_hy;
  e->body.gravity_scale = k->corpse_gravity, e->body.friction = 0.2f, e->body.mask = CF_SOLID;
  e->body.ncontacts = 0;
  float speed = k->corpse_fling, angle = 90, sign = e->sx < 0 ? -1.0f : 1.0f;
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
static void enemy_die(Enemy *e, float direction, bool has_direction) {
  const Ent *d = ent_at(e->ent);
  /* the geo: SpawnAndFling, small, medium then large */
  float x = e->body.x, y = e->body.y;
  geo_fling(0, (int)d->p2, x, y, 15, 30, 80, 100);
  geo_fling(1, (int)d->p3, x, y, 15, 30, 80, 100);
  geo_fling(2, d->group, x, y, 15, 30, 80, 100);
  persist_set(d->persist);
  corpse_start(e, direction, has_direction);
  cam_shake(SHAKE_ENEMY_KILL);
  FREEZE_MOMENT_1();
}

/* HealthManager.Hit (a nail's): evasion, damage, recoil, flash, soul */
static void enemy_hit(Enemy *e, float direction, int damage) {
  if (e->mode != EM_ALIVE || e->evasion > 0 || damage <= 0) return;
  int dir = cardinal(direction);
  recoil_by_direction(e, dir, 1);
  hero_soul_gain();
  e->flashing = true, e->flash_t = 0;
  e->hp = (int16_t)(e->hp - damage < -50 ? -50 : e->hp - damage);
  if (e->kind == EK_BUZZER && e->st == 0) e->b1 = true;   /* (TOOK DAMAGE, in Idle) */
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
  /* circle against the Knight's box */
  float qx = ax < cx - g_hero.body.hx ? cx - g_hero.body.hx : ax > cx + g_hero.body.hx ? cx + g_hero.body.hx : ax;
  float qy = ay < cy - g_hero.body.hy ? cy - g_hero.body.hy : ay > cy + g_hero.body.hy ? cy + g_hero.body.hy : ay;
  e->in_alert = !g_hero.hidden && e->ar_r > 0 && (qx - ax) * (qx - ax) + (qy - ay) * (qy - ay) < e->ar_r * e->ar_r;
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
    e->sx = d->y1 ? d->y1 : 1;
    e->hp = (int16_t)d->p0;
    Body *b = &e->body;
    b->x = d->x0, b->y = d->y0;
    const Ent *box = d + 1;
    b->ox = box->x0 * (e->sx < 0 ? -1 : 1), b->oy = box->y0, b->hx = box->x1, b->hy = box->y1;
    b->gravity_scale = kinds[e->kind].gravity, b->friction = kinds[e->kind].friction, b->mask = CF_SOLID;
    if (d->s0 > 1) e->ar_x = (d + 2)->x0 * (e->sx < 0 ? -1 : 1), e->ar_y = (d + 2)->y0, e->ar_r = (d + 2)->x1;
    if (e->kind == EK_CRAWLER) crawler_start(e, d);
    else if (e->kind == EK_BUZZER) buzzer_start(e, d);
  }
}

/* FixedUpdate and the physics step */
void enemies_fixed(void) {
  for (int i = 0; i < MAX_ENEMIES; i++) {
    Enemy *e = &en[i];
    if (e->mode == EM_OFF) continue;
    if (e->mode == EM_ALIVE) {
      if (e->kind == EK_CRAWLER) crawler_fixed(e);
      else if (e->kind == EK_BUZZER) buzzer_fixed(e);
      recoil_fixed(e);
      body_step(&e->body, DT);
    } else {
      /* Corpse: falls, lands (or smashes, a breaker) and stays */
      const Kind *k = &kinds[e->kind];
      float pvx = e->body.vx, pvy = e->body.vy;
      int had = e->body.ncontacts;
      if (e->st != CS_LANDED || e->body.vx || e->body.vy) body_step(&e->body, DT);
      if (e->st == CS_AIR) {
        bool ground = false;
        for (int c = 0; c < e->body.ncontacts; c++) ground |= e->body.cny[c] > 0.5f;
        if (ground) {
          if (k->breaker) {
            anim_play(&e->anim, k->clip_death_land);
            e->body.vx = e->body.vy = 0;
            e->st = CS_DEATH_ANIM;
          } else {
            bounce(&e->body, pvx, pvy, had, k->corpse_bounce);
            anim_play(&e->anim, k->clip_death_land);
            e->st = CS_LANDED;
          }
        } else
          bounce(&e->body, pvx, pvy, had, k->corpse_bounce);
        if (e->body.y < -10) e->mode = EM_OFF;
      }
    }
  }
  geo_tick();
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
    } else if (e->st == CS_DEATH_ANIM && !e->anim.playing)
      e->mode = EM_OFF;
  }
}

void enemies_draw(void) {
  for (int i = 0; i < MAX_ENEMIES; i++) {
    const Enemy *e = &en[i];
    if (e->mode == EM_OFF) continue;
    Inst in;
    const Ent *d = ent_at(e->ent);
    float z = e->mode == EM_CORPSE ? 0.0085f : d->x1;
    sprite_inst(e->anim.sprite, e->body.x, e->body.y, z, e->sx, 1, flash_tint(e, 2 + i % 6), &in);
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
    if (e->mode != EM_ALIVE) continue;
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
    if (e->mode != EM_ALIVE) continue;
    const Ent *d = ent_at(e->ent);
    if (d->p1 <= 0) continue;
    float a0, b0, a1, b1;
    enemy_box(e, &a0, &b0, &a1, &b1);
    if (x1 > a0 && x0 < a1 && y1 > b0 && y0 < b1) {
      *side = e->body.x > g_hero.body.x ? SIDE_RIGHT : SIDE_LEFT;
      return (int)d->p1;
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
