/* Enemies (tools/ents.py: OK_ENEMY): HealthManager, Recoil, SpriteFlash, DamageHero and EnemyDeathEffects, each kind's
 * FSM, the corpses they leave and the geo they drop. */
#include <math.h>
#include "game.h"
#ifdef HOST
#include <stdio.h>
#include <stdlib.h>
#endif

#define DT 0.02f
#define MAX_ENEMIES 24
#define MAX_GEO 64
#define TERRAIN_FRICTION 0.2f   /* (the Terrain material; with another, their geometric mean) */

/* ---------------------------------------------------------------- what each kind is */
/* (data.h: EK_* each kind, KIND_TABLE: the FSM each runs and the clips its roles play) */
enum {
  EF_CRAWLER = 1, EF_BUZZER, EF_SHADE, EF_HUSK, EF_CLIMBER, EF_BOUNCER, EF_SPITTER, EF_ROLLER, EF_BLOCKER, EF_LEAPER, EF_GUARD,
  EF_FK, EF_FKHEAD, EF_GFLY
};
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
enum { ET_COLLIDER, ET_ALERT, ET_RANGE, ET_WALKER, ET_RECOIL, ET_CORPSE, ET_VARS, ET_TERRAIN, ET_HITBOX, ET_ZONE };
enum { CF_BREAKER = 1, CF_FACES_RIGHT = 2, CF_LOW_ARC = 4, CF_NO_COLLIDER = 32 };   /* a corpse's (ET_CORPSE) */
enum { WF_PAUSES = 1, WF_IGNORE_HOLES = 2, WF_NO_TURN_TO_HERO = 4, WF_START_INACTIVE = 8, WF_AMBUSH = 16, WF_WAIT_HERO_X = 32,
       WF_PREVENT_TURN = 64, WF_NO_SCALE = 128, WF_RIGHT_NEG = 256 };   /* a Walker's (ET_WALKER) */

/* ---------------------------------------------------------------- an enemy */
enum { EM_OFF, EM_ALIVE, EM_CORPSE };
enum { RC_READY, RC_RECOILING, RC_FROZEN };
enum { CS_AIR, CS_DEATH_ANIM, CS_LANDED };
/* (flags: 1 collider off, 2 RECOIL HORIZONTAL, 4 invincible, 8 TOOK DAMAGE: for its FSM this frame, 16 collider off
 * by its frame, 32 not drawn) */
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
  uint8_t c0, c1;              /* (counters) */
  uint8_t hb_on, sub_hb;       /* its attacks on (ET_HITBOX's bits); the one whose sprite plays (k + 1) */
  int16_t col_sprite;          /* (the frame whose collider it has, + 1) */
  Anim sub;                    /* (that sprite's) */
  /* alert range (local circle, or box: ar_hy >= 0) and sight */
  float ar_x, ar_y, ar_r, ar_hy;
  bool in_alert, can_see;
  float cbox_ox, cbox_oy, cbox_hx, cbox_hy;   /* its collider */
} Enemy;
static Enemy en[MAX_ENEMIES];
static uint32_t swing_bits;   /* (a bit an enemy: hit this swing) */

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

/* the terrain it holds (a child's colliders) gone: it died */
static void enemy_terrain_off(const Ent *d) {
  for (int i = 1; i <= d->s0; i++)
    if (d[i].type == ENT_BOX && d[i].flags == ET_TERRAIN)
      for (int c = d[i].a; c < d[i].a + d[i].group; c++) phys_collider_enable(c, false);
}

/* 2D Toolkit: the frame shown sets the collider (a box: its body's; none: off) */
static void frame_collider(Enemy *e) {
  if (e->anim.sprite + 1 == e->col_sprite) return;
  e->col_sprite = (int16_t)(e->anim.sprite + 1);
  const float *d;
  int n, t = sprite_collider(e->anim.sprite, &d, &n);
  float k = fabsf(e->sx);
  if (t == SC_BOX) {
    e->body.ox = d[0] * e->sx, e->body.oy = d[1] * k, e->body.hx = d[2] * k, e->body.hy = d[3] * k;
    e->flags &= (uint8_t)~16;
  } else if (t == SC_NONE)
    e->flags |= 16;
}

/* the collider's world box */
static void enemy_box(const Enemy *e, float *x0, float *y0, float *x1, float *y1) {
  float cx = e->body.x + e->body.ox, cy = e->body.y + e->body.oy;
  *x0 = cx - e->body.hx, *x1 = cx + e->body.hx, *y0 = cy - e->body.hy, *y1 = cy + e->body.hy;
}

/* ---------------------------------------------------------------- geo (GeoControl) */
/* (kept small, many at once: its body's place, velocity and first two contacts; a whole Body only as it steps) */
#define GEO_CONTACTS 2
typedef struct {
  bool on;
  uint8_t type;   /* 0 small (1), 1 medium (5), 2 large (25) */
  uint8_t ncontacts, ccol[GEO_CONTACTS];
  float x, y, vx, vy;
  float cnx[GEO_CONTACTS], cny[GEO_CONTACTS];
  Anim anim;
  float pickup_t;
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
    g->x = x + rand_range(-spread, spread), g->y = y + rand_range(-spread, spread);
    float s = rand_range(smin, smax), a = rand_range(amin, amax) * (float)M_PI / 180;
    g->vx = cosf(a) * s, g->vy = sinf(a) * s;
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
    Body b;
    memset(&b, 0, sizeof b);
    b.x = g->x, b.y = g->y, b.vx = g->vx, b.vy = g->vy;
    b.ox = geo_kinds[g->type].ox * GEO_SCALE, b.oy = geo_kinds[g->type].oy * GEO_SCALE;
    b.hx = geo_kinds[g->type].hx * GEO_SCALE, b.hy = geo_kinds[g->type].hy * GEO_SCALE;
    b.gravity_scale = geo_kinds[g->type].gravity, b.friction = 0.2f, b.mask = CF_TERRAIN;
    b.ncontacts = g->ncontacts;
    for (int c = 0; c < g->ncontacts; c++) b.ccol[c] = g->ccol[c], b.cnx[c] = g->cnx[c], b.cny[c] = g->cny[c];
    float pvx = b.vx, pvy = b.vy;
    int had = b.ncontacts;
    body_step(&b, DT);
    bool hit = b.ncontacts > had;
    if (hit) bounce(&b, pvx, pvy, had, geo_kinds[g->type].bounce);
    g->x = b.x, g->y = b.y, g->vx = b.vx, g->vy = b.vy;
    g->ncontacts = (uint8_t)(b.ncontacts < GEO_CONTACTS ? b.ncontacts : GEO_CONTACTS);
    for (int c = 0; c < g->ncontacts; c++) g->ccol[c] = b.ccol[c], g->cnx[c] = b.cnx[c], g->cny[c] = b.cny[c];
    if (hit) {
      /* (OnCollisionEnter2D: the idle animation from a random frame) */
      anim_play_from_frame(&g->anim, geo_kinds[g->type].idle, (int)rand_range(0, (float)clip_frames_count(geo_kinds[g->type].idle) - 0.001f));
    }
    g->anim.events = 0;
    anim_update(&g->anim, DT);
    if (g->pickup_t > 0) g->pickup_t -= DT;
    float x0 = b.x + b.ox - b.hx, x1 = b.x + b.ox + b.hx, y0 = b.y + b.oy - b.hy, y1 = b.y + b.oy + b.hy;
    if (g->pickup_t <= 0 && !h->hidden && x1 > hx - 0.2277069f && x0 < hx + 0.2277069f && y1 > hy - 0.5848932f &&
        y0 < hy + 0.5848932f) {
      hero_add_geo(geo_kinds[g->type].value);
      g->on = false;
    }
    if (g->y < -10) g->on = false;
  }
}

/* ---------------------------------------------------------------- SpriteFlash */
static uint8_t flash_tint_at(bool on, float t, int slot) {
  if (!on || t > 0.27f) return 0;
  /* flashInfected: up 0.01 s, stays 0.01 s, down over 0.25 s, to 0.9 of orange */
  float k;
  if (t < 0.01f) k = t / 0.01f;
  else if (t < 0.02f) k = 1;
  else k = 1 - (t - 0.02f) / 0.25f;
  if (k < 0) k = 0;
  return gfx_dyn_flash(slot, 255, 255, 255, 255, 255, 79, 0, (uint8_t)(0.9f * k * 255));
}

static uint8_t flash_tint(const Enemy *e, int slot) { return flash_tint_at(e->flashing, e->flash_t, slot); }

/* ---------------------------------------------------------------- Recoil */
#define RF_NONE 0x80   /* (no Recoil component) */
static void climber_stun(Enemy *e);
static void recoil_by_direction(Enemy *e, int dir, float magnitude) {
  if (e->rc_state != RC_READY || (e->rc_flags & RF_NONE)) return;
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

/* EnemyDeathEffects.EmitEffects: the camera's shake by its enemyDeathType (Infected, LargeInfected; SmallInfected none) */
static void death_shake(const Enemy *e) {
  int type = e->ent != NO_ENT ? ent_at(e->ent)->s1 >> EF_DEATH_SHIFT & 15 : 0;
  if (type == 0) cam_shake(SHAKE_ENEMY_KILL);
  else if (type == 5) cam_shake(SHAKE_AVERAGE);
}

/* HealthManager.Die, EnemyDeathEffects */
static void shade_killed(Enemy *e);
static void blocker_die(Enemy *e);
static void gfly_die(Enemy *e);
static void enemy_die(Enemy *e, float direction, bool has_direction) {
  if (FSM(e) == EF_SHADE) {
    shade_killed(e);
    return;
  }
  if (FSM(e) == EF_BLOCKER) {
    blocker_die(e);
    return;
  }
  /* the geo: SpawnAndFling, small, medium then large */
  float x = e->body.x, y = e->body.y;
  geo_fling(0, e->geo_s, x, y, 15, 30, 80, 100);
  geo_fling(1, e->geo_m, x, y, 15, 30, 80, 100);
  geo_fling(2, e->geo_l, x, y, 15, 30, 80, 100);
  if (e->ent != NO_ENT) {
    const Ent *d = ent_at(e->ent);
    persist_set(d->persist), enemy_terrain_off(d);
    if (d->s1 & EF_BATTLE) arena_enemy_died();   /* (its battleScene's Battle Enemies) */
  }
  if (FSM(e) == EF_GFLY) gfly_die(e);
  else corpse_start(e, direction, has_direction);
  death_shake(e);
  FREEZE_MOMENT_1();
}

/* HealthManager.Hit (a nail's): evasion, damage, recoil, flash, soul */
static void blocker_hit(Enemy *e);
static void fk_head_hit(void);
static void fk_stun(Enemy *e);
static void fk_head_stun_end(Enemy *h);
static void enemy_hit(Enemy *e, float direction, int damage) {
  if (e->mode != EM_ALIVE || e->evasion > 0 || damage <= 0) return;
  int dir = cardinal(direction);
  if (e->flags & 4) {
    /* Invincible: the hit blocked, the Knight recoiling off it */
    if (dir == 0) hero_recoil_left();
    else if (dir == 2) hero_recoil_right();
    FREEZE_MOMENT_1();
    cam_shake(SHAKE_ENEMY_KILL);
    e->evasion = 0.15f;
    return;
  }
  recoil_by_direction(e, dir, 1);
  if (FSM(e) != EF_SHADE && FSM(e) != EF_FK) hero_soul_gain();   /* (enemyType 3, a shade, or 6: no soul) */
  e->flashing = true, e->flash_t = 0;
  e->flags |= 8;
  e->hp = (int16_t)(e->hp - damage < -50 ? -50 : e->hp - damage);
  if ((FSM(e) == EF_BUZZER || FSM(e) == EF_SHADE || FSM(e) == EF_HUSK) && e->st == 0) e->b1 = true;   /* (TOOK DAMAGE, in Idle / Ready) */
  if (FSM(e) == EF_BLOCKER && e->hp > 0) blocker_hit(e);
  if (FSM(e) == EF_FKHEAD) fk_head_hit();   /* (its sendHitTo) */
  if (e->hp > 0)
    e->evasion = 0.2f;
  else if (FSM(e) == EF_FK || FSM(e) == EF_FKHEAD) {
    /* (hasSpecialDeath: ZERO HP to its FSMs, then NonFatalHit) */
    if (FSM(e) == EF_FK) fk_stun(e);
    else fk_head_stun_end(e);
    e->evasion = 0.2f;
  } else
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

/* CheckCollisionSide: three short rays from a side of its collider (0 right, 1 top, 2 left, 3 bottom) */
static bool side_hit(const Enemy *e, int side) {
  float x0, y0, x1, y1;
  enemy_box(e, &x0, &y0, &x1, &y1);
  const float d = 0.08f;
  float dx = side == 0 ? 1 : side == 2 ? -1 : 0, dy = side == 1 ? 1 : side == 3 ? -1 : 0;
  float x = dx > 0 ? x1 : x0, y = dy > 0 ? y1 : y0;
  for (int i = 0; i < 3; i++) {
    float k = (float)i / 2;
    float rx = dx ? x : x0 + (x1 - x0) * k, ry = dy ? y : y0 + (y1 - y0) * k;
    if (phys_ray(rx, ry, dx, dy, d, CF_TERRAIN, NULL)) return true;
  }
  return false;
}

/* (the side it bumped, top first) */
static int bouncer_side(const Enemy *e) {
  static const int order[4] = {1, 0, 3, 2};
  for (int i = 0; i < 4; i++)
    if (side_hit(e, order[i])) return order[i];
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
      /* (GetDistance to the main camera, its depth included) */
      float dx = e->body.x - g_cam_x, dy = e->body.y - g_cam_y, dz = 0.01f - CAM_Z;
      if (dx * dx + dy * dy + dz * dz < 44 * 44) {
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
  float x, y, vx, vy, scale, ang, gravity;
  Anim anim;
} Bullet;
static Bullet bullets[MAX_BULLETS];
#define BULLET_HX (0.6406f / 2)
#define BULLET_HY (0.5625f / 2)

/* a shot from (x, y) at a velocity, falling at a gravity scale */
static Bullet *bullet_new(float x, float y, float vx, float vy, float gravity) {
  Bullet *b = NULL;
  for (int i = 0; i < MAX_BULLETS && !b; i++)
    if (!bullets[i].on) b = &bullets[i];
  if (!b) return NULL;
  memset(b, 0, sizeof *b);
  b->on = b->active = true;
  b->x = x, b->y = y, b->vx = vx, b->vy = vy, b->gravity = gravity;
  b->scale = rand_range(1.15f, 1.45f);
  anim_play(&b->anim, CLIP_BULLET_IDLE);
  return b;
}

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
  b->gravity = 0.05f;
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
    b->vy += -60 * b->gravity * DT;   /* (its Rigidbody2D's gravity) */
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
      /* (ALERT: it sees the Knight in its alert range, or it starts alert) */
      if ((e->can_see && e->in_alert) || (e->ent != NO_ENT && (ent_at(e->ent)->s1 & EF_START))) spitter_distance_fly(e);
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

/* ---------------------------------------------------------------- spawned enemies (no record of the room's) */
static Enemy *enemy_spawn(int kind, float x, float y, int hp) {
  Enemy *e = NULL;
  for (int i = 0; i < MAX_ENEMIES && !e; i++)
    if (en[i].mode == EM_OFF) e = &en[i];
  if (!e) return NULL;
  memset(e, 0, sizeof *e);
  e->mode = EM_ALIVE, e->kind = (uint8_t)kind, e->ent = NO_ENT;
  e->hp = (int16_t)hp, e->damage = 1, e->sx = 1;
  e->rc_base = 15, e->rc_dur = 0.15f;
  e->body.x = x, e->body.y = y;
  e->body.friction = 0.2828f, e->body.mask = CF_SOLID;
  return e;
}

/* the hero in a box range of its (local: center, half size) */
static bool hero_in_box(const Enemy *e, float cx, float cy, float hx, float hy) {
  float x = e->body.x + cx * (e->sx < 0 ? -1 : 1), y = e->body.y + cy;
  float kx = g_hero.body.x + g_hero.body.ox, ky = g_hero.body.y + g_hero.body.oy;
  return !g_hero.hidden && fabsf(kx - x) < hx + g_hero.body.hx && fabsf(ky - y) < hy + g_hero.body.hy;
}

/* ---------------------------------------------------------------- Baldurs: the Roller FSM */
enum { RO_IDLE, RO_START, RO_ROLL, RO_COLLIDE, RO_AIR, RO_STOP, RO_REST };
/* (its variables, or a spawned one's) */
static float roller_var(const Enemy *e, int i) {
  static const float dflt[5] = {0.45f, 11, 2, 3, 0.5f};
  const Ent *v = enemy_rec(e, ET_VARS);
  if (!v) return dflt[i];
  return i == 0 ? v->p0 : i == 1 ? v->p1 : i == 2 ? v->p2 : i == 3 ? v->p3 : v->x0;
}

static void roller_roll(Enemy *e) {
  e->st = RO_ROLL;
  set_scale_x(e, e->b0 ? -fabsf(e->sx) : fabsf(e->sx));
  anim_play(&e->anim, CLIP(e, R_A2));
}

static void roller_start(Enemy *e) {
  e->st = RO_IDLE;
  anim_play(&e->anim, CLIP(e, R_IDLE));
}

/* (a spawned one: from the air, rolling its way) */
static void roller_spawned(Enemy *e, bool right) {
  e->b0 = right;
  set_scale_x(e, right ? -1.0f : 1.0f);
  e->body.gravity_scale = 0.8f;
  e->body.ox = -0.0156f, e->body.oy = -0.1094f, e->body.hx = e->body.hy = 1.0938f / 2;
  e->ar_x = 0, e->ar_y = 0, e->ar_r = 39.35f / 2, e->ar_hy = 1.9f / 2;
  e->wait = rand_range(roller_var(e, 2), roller_var(e, 3));
  anim_play(&e->anim, CLIP(e, R_A2));
  e->st = RO_AIR;
}

static void roller_fixed(Enemy *e) {
  if (e->st == RO_IDLE) e->body.vx = 0;
  else if (e->st == RO_ROLL) {
    float a = roller_var(e, 0), m = roller_var(e, 1), vx = e->body.vx + (e->b0 ? a : -a);
    e->body.vx = vx > m ? m : vx < -m ? -m : vx;
  }
}

static void roller_update(Enemy *e) {
  bool done = (e->anim.events & ANIM_DONE) != 0;
  if (e->flags & 2) {   /* (RECOIL HORIZONTAL: Recoil Decel) */
    e->flags &= (uint8_t)~2;
    if (e->st == RO_ROLL) e->body.vx = 0;
  }
  switch (e->st) {
    case RO_IDLE: {
      float want = hero_x() > e->body.x ? -fabsf(e->sx) : fabsf(e->sx);
      if (e->sx != want) set_scale_x(e, want), anim_play_from_frame(&e->anim, CLIP(e, R_IDLE), 0);
      if (e->can_see && e->in_alert) {
        /* Facing Check, Start */
        e->b0 = hero_x() >= e->body.x;
        set_scale_x(e, e->b0 ? -fabsf(e->sx) : fabsf(e->sx));
        anim_play_from_frame(&e->anim, CLIP(e, R_A1), 0);
        e->wait = rand_range(roller_var(e, 2), roller_var(e, 3));
        e->st = RO_START;
      }
      break;
    }
    case RO_START:
      if (done) roller_roll(e);
      break;
    case RO_ROLL: {
      if (e->body.ncontacts && side_hit(e, e->b0 ? 0 : 2)) {
        /* Collide: off the wall, up and back the other way */
        e->b0 = !e->b0;
        set_scale_x(e, fabsf(e->sx));
        float a = (e->b0 ? 65 : 115) * (float)M_PI / 180;
        e->body.vx = cosf(a) * 12, e->body.vy = sinf(a) * 12;
        e->st = RO_COLLIDE;
        break;
      }
      if ((e->wait -= DT) <= 0) {
        e->body.vx = e->body.vy = 0;
        anim_play_from_frame(&e->anim, CLIP(e, R_A3), 0);
        e->st = RO_STOP;
      }
      break;
    }
    case RO_COLLIDE:
      e->st = RO_AIR;   /* (the next frame) */
      break;
    case RO_AIR:
      e->wait -= DT;
      if (e->body.ncontacts && side_hit(e, 3)) roller_roll(e);   /* (GROUND: Land, Left or right?) */
      break;
    case RO_STOP:
      if (done) {
        anim_play(&e->anim, CLIP(e, R_IDLE));
        e->st = RO_REST, e->t0 = 0;
      }
      break;
    case RO_REST:
      if ((e->t0 += DT) >= roller_var(e, 4)) e->st = RO_IDLE;
      break;
  }
}

/* ---------------------------------------------------------------- Elder Baldurs: the Blocker Control FSM */
enum { BL_DORMANT, BL_OPEN, BL_IDLE, BL_CLOSE1, BL_CLOSE2, BL_CLOSED, BL_SHOT_ANTIC, BL_SHOT_END, BL_HIT_PAUSE, BL_HIT };
#define BL_SHOT_ORIGIN_X 2.76f
#define BL_SHOT_ORIGIN_Y 0.78f

/* the hero in its alert range (ET_ALERT) or its attack range (ET_RANGE), boxes */
static bool blocker_in(const Enemy *e, int tag) {
  const Ent *r = enemy_rec(e, tag);
  return r && hero_in_box(e, r->x0, r->y0, r->x1, r->y1);
}

static void blocker_start(Enemy *e) {
  e->flags |= 4;   /* (SetInvincible) */
  e->start_x = e->body.x, e->start_y = e->body.y;
  anim_play(&e->anim, CLIP(e, R_A1));   /* (Closed) */
  e->st = BL_DORMANT;
}

static void blocker_idle(Enemy *e) {
  e->st = BL_IDLE;
  e->body.x = e->start_x, e->body.y = e->start_y;
  anim_play(&e->anim, CLIP(e, R_IDLE));
  e->wait = rand_range(0.8f, 1.2f);
}

static void blocker_close(Enemy *e) {
  e->flags |= 4;
  e->body.x = e->start_x, e->body.y = e->start_y;
  anim_play_from_frame(&e->anim, CLIP(e, R_A3), 0);
  e->st = BL_CLOSE1;
}

static void blocker_hit(Enemy *e) {
  /* TOOK DAMAGE: Hit Pause, Hit */
  e->st = BL_HIT_PAUSE;
}

static void blocker_fire(Enemy *e) {
  const Ent *v = enemy_rec(e, ET_VARS);
  bool right = v && (v->a & 1);
  float ox = right ? BL_SHOT_ORIGIN_X : -BL_SHOT_ORIGIN_X;
  float vx = right ? rand_range(3, 15) : rand_range(-15, -1), vy = v ? v->p0 : 20;
  if (e->b1) {
    /* a roller, rolling its way */
    Enemy *r = enemy_spawn(EK_BALDUR, e->body.x + ox, e->body.y + BL_SHOT_ORIGIN_Y, 15);
    if (r) roller_spawned(r, right), r->body.vx = vx, r->body.vy = vy, r->rc_base = 25;
  } else {
    /* goop (Shot Mawlek) */
    bullet_new(e->body.x + ox, e->body.y + BL_SHOT_ORIGIN_Y, vx, vy, 0.6f);
  }
  anim_play_from_frame(&e->anim, CLIP(e, R_A6), 0);   /* (Shoot CD) */
  e->st = BL_SHOT_END;
}

static void blocker_update(Enemy *e) {
  bool done = (e->anim.events & ANIM_DONE) != 0;
  switch (e->st) {
    case BL_DORMANT:
      if (blocker_in(e, ET_ALERT)) {
        e->flags &= (uint8_t)~4;
        anim_play_from_frame(&e->anim, CLIP(e, R_A2), 0);   /* (Open) */
        e->st = BL_OPEN;
      }
      break;
    case BL_OPEN:
      if (done) blocker_idle(e);
      break;
    case BL_IDLE:
      if (blocker_in(e, ET_RANGE)) blocker_close(e);
      else if ((e->wait -= DT) <= 0) {
        /* Attack Choose: goop, or a roller if the Knight has his fireball and none is about */
        bool roller = rand_range(0, 1) < 0.5f && g_pd.fireball_level > 0;
        if (roller)
          for (int i = 0; i < MAX_ENEMIES; i++)
            if (en[i].mode == EM_ALIVE && en[i].ent == NO_ENT && FSM(&en[i]) == EF_ROLLER) roller = false;
        e->b1 = roller;
        anim_play_from_frame(&e->anim, CLIP(e, R_A5), 0);   /* (Shoot Antic) */
        e->st = BL_SHOT_ANTIC;
      }
      break;
    case BL_SHOT_ANTIC:
      if (done) blocker_fire(e);
      break;
    case BL_SHOT_END:
      if (blocker_in(e, ET_RANGE)) blocker_close(e);
      else if (done) blocker_idle(e);
      break;
    case BL_CLOSE1:
      if (done) anim_play_from_frame(&e->anim, CLIP(e, R_A4), 0), e->st = BL_CLOSE2;
      break;
    case BL_CLOSE2:
      if (done) anim_play(&e->anim, CLIP(e, R_A1)), e->st = BL_CLOSED;
      break;
    case BL_CLOSED:
      if (!blocker_in(e, ET_RANGE)) {
        e->flags &= (uint8_t)~4;
        anim_play_from_frame(&e->anim, CLIP(e, R_A2), 0);
        e->st = BL_OPEN;
      }
      break;
    case BL_HIT_PAUSE:
      e->st = BL_HIT;
      anim_play_from_frame(&e->anim, CLIP(e, R_A7), 0);
      break;
    case BL_HIT:
      e->jx = rand_range(-0.05f, 0.05f), e->jy = rand_range(-0.05f, 0.05f);
      if (blocker_in(e, ET_RANGE)) e->jx = e->jy = 0, blocker_close(e);
      else if (done) e->jx = e->jy = 0, blocker_idle(e);
      break;
  }
}

/* its corpse (Corpse Blocker): stunned a second, then its death, staying */
static void blocker_die(Enemy *e) {
  geo_fling(0, e->geo_s, e->body.x, e->body.y, 15, 30, 80, 100);
  geo_fling(1, e->geo_m, e->body.x, e->body.y, 15, 30, 80, 100);
  geo_fling(2, e->geo_l, e->body.x, e->body.y, 15, 30, 80, 100);
  if (e->ent != NO_ENT) persist_set(ent_at(e->ent)->persist), enemy_terrain_off(ent_at(e->ent));
  death_shake(e);
  FREEZE_MOMENT_1();
  /* (the Snail Shaman hears of it) */
  if (g_pd.shaman == 3) g_pd.shaman = 4;
  else if (g_pd.shaman == 2) g_pd.shaman = 5;
  e->mode = EM_CORPSE, e->st = 0, e->t0 = 0;
  e->body.vx = e->body.vy = 0, e->body.gravity_scale = 0, e->body.mask = 0;
  anim_play(&e->anim, CLIP(e, R_A8));
}

static void blocker_corpse(Enemy *e) {
  e->t0 += DT;
  if (e->st == 0) {
    e->jx = rand_range(-0.05f, 0.05f), e->jy = rand_range(-0.05f, 0.05f);
    if (e->t0 >= 1) {
      e->jx = e->jy = 0;
      anim_play_from_frame(&e->anim, CLIP(e, R_DEATH_LAND), 0);
      e->st = 1;
    }
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

/* (an older Zombie Swipe: no Coward state, Ready deaf to TOOK DAMAGE, Reset playing Idle) */
static bool husk_old(const Enemy *e) {
  const Ent *v = enemy_rec(e, ET_VARS);
  return v && (v->a & 2);
}

static void husk_ready(Enemy *e) {
  /* Reset: StartWalker; (Coward: no) Ready */
  if (husk_old(e)) anim_play(&e->anim, CLIP(e, R_IDLE));
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
      if ((e->can_see && e->in_alert) || (e->b1 && !husk_old(e))) {
        e->b1 = false;
        husk_attack(e);
      }
      e->b1 = false;
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

/* ---------------------------------------------------------------- attacks: children that hurt the Knight */
/* its hitbox k (ET_HITBOX's, in order), or NULL */
static const Ent *hitbox_rec(const Enemy *e, int k) {
  if (e->ent == NO_ENT) return NULL;
  const Ent *d = ent_at(e->ent);
  for (int i = 1; i <= d->s0; i++)
    if (d[i].type == ENT_BOX && d[i].flags == ET_HITBOX && k-- == 0) return &d[i];
  return NULL;
}

/* (the child on: its collider, and the sprite it plays till its clip ends: DeactivateAfter2dtkAnimation) */
static void hitbox_on(Enemy *e, int k, int clip) {
  e->hb_on |= (uint8_t)(1 << k);
  if (clip >= 0) anim_play_from_frame(&e->sub, clip, 0), e->sub_hb = (uint8_t)(k + 1);
}

static void hitbox_tick(Enemy *e) {
  if (!e->sub_hb) return;
  e->sub.events = 0;
  anim_update(&e->sub, DT);
  if ((e->sub.events & ANIM_DONE) || !e->sub.playing) e->hb_on &= (uint8_t)~(1 << (e->sub_hb - 1)), e->sub_hb = 0;
}

/* the Knight's box and its hitboxes on: the damage of the first it meets */
static int hitbox_touch(const Enemy *e, float x0, float y0, float x1, float y1) {
  float k = e->sx < 0 ? -1.0f : 1.0f;
  for (int i = 0; i < 8 && e->hb_on >> i; i++) {
    if (!(e->hb_on >> i & 1)) continue;
    const Ent *h = hitbox_rec(e, i);
    if (!h) continue;
    if (e->sub_hb == i + 1) {
      /* (its sprite's frame sets its collider) */
      const float *d;
      int n, t = sprite_collider(e->sub.sprite, &d, &n);
      if (t == SC_NONE) continue;
      float ks = fabsf(e->sx);
      if (t == SC_BOX) {
        float cx = e->body.x + d[0] * e->sx, cy = e->body.y + d[1] * ks;
        if (x1 > cx - d[2] * ks && x0 < cx + d[2] * ks && y1 > cy - d[3] * ks && y0 < cy + d[3] * ks) return (int)h->p1;
        continue;
      }
      if (t == SC_SHAPE) {
        float pts[16];
        for (int j = 0; j < n && j < 8; j++) pts[2 * j] = e->body.x + d[2 * j] * e->sx, pts[2 * j + 1] = e->body.y + d[2 * j + 1] * ks;
        if (box_meets_shape(x0, y0, x1, y1, pts, n < 8 ? n : 8)) return (int)h->p1;
        continue;
      }
    }
    float a0 = e->body.x + (k > 0 ? h->x0 : -h->x1), a1 = e->body.x + (k > 0 ? h->x1 : -h->x0);
    if (!(x1 > a0 && x0 < a1 && y1 > e->body.y + h->y0 && y0 < e->body.y + h->y1)) continue;
    float pts[16];
    int np = (int)h->p0;
    for (int j = 0; j < np && j < 8; j++) {
      const float *f = &h[1 + j / 4].x0;
      pts[2 * j] = e->body.x + f[2 * (j & 3)] * k, pts[2 * j + 1] = e->body.y + f[2 * (j & 3) + 1];
    }
    if (box_meets_shape(x0, y0, x1, y1, pts, np < 8 ? np : 8)) return (int)h->p1;
  }
  return 0;
}

/* ---------------------------------------------------------------- shockwaves (the shockwave FSM, its spurts) */
#define MAX_WAVES 4
#define MAX_SPURTS 32
typedef struct {
  bool on, spurting;
  float x, y, speed, inc, scale, t;
  int8_t dir;
} Wave;
typedef struct {
  bool on;
  float x, y, scale, t;
  int8_t dir;
  Anim anim;
} Spurt;
static Wave waves[MAX_WAVES];
static Spurt spurts[MAX_SPURTS];

/* a wave from (x, y) along the ground, its speed and scale (Start, Right / Left, Start Move) */
static void wave_spawn(float x, float y, bool right, float speed, float scale) {
  Wave *w = NULL;
  for (int i = 0; i < MAX_WAVES && !w; i++)
    if (!waves[i].on) w = &waves[i];
  if (!w) return;
  memset(w, 0, sizeof *w);
  w->on = w->spurting = true;
  w->x = x, w->y = y, w->scale = scale, w->dir = right ? 1 : -1;
  w->inc = speed * 2 * w->dir, w->speed = speed * 0.025f * w->dir;
}

static void spurt_spawn(const Wave *w) {
  Spurt *s = NULL;
  for (int i = 0; i < MAX_SPURTS && !s; i++)
    if (!spurts[i].on) s = &spurts[i];
  if (!s) return;
  s->on = true, s->x = w->x, s->y = w->y, s->scale = w->scale, s->t = 0, s->dir = w->dir;
  anim_play_from_frame(&s->anim, CLIP_BULLET_SHOCKWAVE_SPURT, 0);
}

static void waves_tick(void) {
  for (int i = 0; i < MAX_WAVES; i++) {
    Wave *w = &waves[i];
    if (!w->on) continue;
    w->t += DT;
    if (w->spurting) {
      /* Move: faster and faster; into a wall (its trigger meeting the terrain), or past the ground's end: done. (Its
       * trigger: scaled by the wave's x scale, which Left sets to -1; the ground ray from a unit ahead, unscaled) */
      w->speed += w->inc * DT;
      float dx = w->speed * DT, kx = w->dir > 0 ? w->scale : 1;
      float front = w->x + (-0.2374f * w->dir + 0.32855f * w->dir) * kx;
      if (w->t >= 0)
        for (int k = 0; k < 3; k++)
          if (phys_ray(front, w->y + 0.4f + 0.65f * (float)k, w->dir, 0, fabsf(dx), CF_TERRAIN, NULL)) {
            w->t = -0.15f;   /* (End Pause: spurts a moment more) */
            break;
          }
      w->x += dx;
      if (w->t >= 0 && !phys_ray(w->x + w->dir, w->y + 1, 0, -1, 1.6f, CF_TERRAIN, NULL))
        w->spurting = false, w->t = 0;   /* (HIT: End Particle) */
      else if (w->t < 0 && w->t + DT >= 0)
        w->spurting = false, w->t = 0;
      if (w->spurting) spurt_spawn(w);
    } else {
      w->x += w->speed * DT;
      if (w->t >= 1) w->on = false;   /* (End Particle: a second, then recycled) */
    }
  }
  for (int i = 0; i < MAX_SPURTS; i++) {
    Spurt *s = &spurts[i];
    if (!s->on) continue;
    s->t += DT;
    s->anim.events = 0;
    anim_update(&s->anim, DT);
    if ((s->anim.events & ANIM_DONE) || !s->anim.playing) s->on = false;   /* (RecycleAfter2dtkAnimation) */
  }
}

/* (Damage timing: a spurt hurts from 0.05 s to 0.1 s) */
static int spurts_touch(float x0, float y0, float x1, float y1, int *side) {
  for (int i = 0; i < MAX_SPURTS; i++) {
    const Spurt *s = &spurts[i];
    if (!s->on || s->t < 0.05f || s->t >= 0.1f + DT / 2) continue;
    float cx = s->x - 0.0473f * s->dir * s->scale, cy = s->y + 0.87f * s->scale;
    float hx = 0.2022f * s->scale, hy = 0.8718f * s->scale;
    if (x1 > cx - hx && x0 < cx + hx && y1 > cy - hy && y0 < cy + hy) {
      *side = s->x > g_hero.body.x ? SIDE_RIGHT : SIDE_LEFT;
      return 1;
    }
  }
  return 0;
}

static void spurts_draw(void) {
  for (int i = 0; i < MAX_SPURTS; i++) {
    const Spurt *s = &spurts[i];
    if (!s->on || s->anim.sprite < 0) continue;
    Inst in;
    sprite_inst(s->anim.sprite, s->x, s->y, -0.12f, s->scale * s->dir, s->scale, 0, &in);
    gfx_actor(&in, SORT_KEY(0, 0));
  }
}

/* ---------------------------------------------------------------- Leaping Husks: the Zombie Leap FSM, a Walker */
enum { ZL_READY, ZL_ANTICIPATE, ZL_LAUNCH, ZL_LUNGE, ZL_COOLDOWN, ZL_IDLE };

static void leaper_start(Enemy *e) {
  walker_init(e);
  e->st = ZL_READY;
}

static void leaper_update(Enemy *e) {
  bool done = (e->anim.events & ANIM_DONE) != 0;
  switch (e->st) {
    case ZL_READY:
      walker_update(e);
      if (e->can_see && e->in_alert) {
        /* Left or Right?: the walker stopped, its leap aimed a little past the hero, facing it */
        walker_stop(e, STOP_CONTROLLED);
        bool right = hero_x() > e->body.x;
        e->ax = (hero_x() - e->body.x) * 1.25f;
        set_scale_x(e, right ? -1.0f : 1.0f);
        e->wk_facing = right ? 1 : -1;
        /* Anticipate */
        e->body.vx = e->body.vy = 0;
        anim_play_from_frame(&e->anim, CLIP(e, R_A1), 0);
        e->st = ZL_ANTICIPATE;
      }
      break;
    case ZL_ANTICIPATE:
      if (e->anim.events & ANIM_TRIGGER) {
        e->body.vx = e->ax, e->body.vy = 20;   /* (Launch) */
        e->st = ZL_LAUNCH;
      }
      break;
    case ZL_LAUNCH:
      e->st = ZL_LUNGE;
      break;
    case ZL_LUNGE:
      if (e->body.ncontacts && side_hit(e, 3)) {
        anim_play_from_frame(&e->anim, CLIP(e, R_A2), 0);   /* (Cooldown: Land) */
        e->body.vx = 0;
        e->st = ZL_COOLDOWN;
      }
      break;
    case ZL_COOLDOWN:
      if (done) {
        anim_play(&e->anim, CLIP(e, R_IDLE));
        e->st = ZL_IDLE, e->t0 = 0;
      }
      break;
    case ZL_IDLE: {
      const Ent *v = enemy_rec(e, ET_VARS);
      if ((e->t0 += DT) >= (v ? v->p0 : 0.5f)) {
        /* Reset: StartWalker */
        anim_play(&e->anim, CLIP(e, R_IDLE));
        walker_start(e);
        e->st = ZL_READY;
      }
      break;
    }
  }
}

static void leaper_fixed(Enemy *e) {
  if (e->st == ZL_READY && e->wk_state == WK_WALKING) {
    const Ent *w = walker_rec(e);
    if (w) e->body.vx = e->wk_facing > 0 ? w->y0 : w->x0;
  }
}

/* ---------------------------------------------------------------- Husk Guards: the Zombie Guard FSM */
enum {
  GD_DORMANT, GD_WAKE, GD_COOLDOWN, GD_IDLE, GD_WAIT, GD_STARTLE, GD_TURN, GD_WALK, GD_RUN, GD_STOP, GD_TURN_BACK,
  GD_RETURN, GD_ANTIC, GD_ATTACK, GD_RECOIL, GD_ATTACK_END, GD_STOMP_ANTIC, GD_STOMP_AIR, GD_STOMP_LAND, GD_STOMP_CD
};
#define GD_ATTACK_SCALE 10.0f   /* (Wake: its Attack Range's x scale) */
/* (e->b0 facing right, b1 running, wk_stop woken; ax walk speed, ay run speed; qx, qy its roam's ends; tx its idle
 * spot; c0, c1 clubs, stomps in a row) */

static float guard_var(const Enemy *e, int i) {
  const Ent *v = enemy_rec(e, ET_VARS);
  return v ? (i == 0 ? v->p0 : v->p1) : (i == 0 ? 9 : 23.5f);
}

/* in its Attack Range (smaller once woken) */
static bool guard_in_attack(const Enemy *e) {
  const Ent *r = enemy_rec(e, ET_RANGE);
  if (!r) return false;
  float k = e->st == GD_DORMANT || r->p0 <= 0 ? 1 : GD_ATTACK_SCALE / r->p0;
  return hero_in_box(e, r->x0 * k, r->y0, r->x1 * k, r->y1);
}

static void guard_face(Enemy *e, bool right) {
  e->b0 = right;
  set_scale_x(e, right ? 1.0f : -1.0f);
}

static void guard_idle(Enemy *e) {
  e->b1 = false;
  e->body.vx = 0;
  anim_play(&e->anim, CLIP_GUARD_IDLE);
  cam_rumble(RUMBLE_OFF);
  e->st = GD_IDLE, e->t0 = 0;
}

static bool guard_hero_in_roam(const Enemy *e) { return hero_x() >= e->qx && hero_x() <= e->qy; }

/* Face Hero: towards the hero (Turn), else an attack (Attack Choice) */
static void guard_face_hero(Enemy *e) {
  bool right = hero_x() > e->body.x;
  if (hero_x() == e->body.x) return;
  e->ax = right ? 5 : -5, e->ay = right ? 10 : -10;
  cam_rumble(RUMBLE_OFF);
  if (right != e->b0) {
    guard_face(e, right);
    anim_play_from_frame(&e->anim, CLIP_GUARD_TURN, 0);
    e->st = GD_TURN;
    return;
  }
  for (;;) {
    bool club = rand_range(0, 1) < 0.75f;
    if (club && e->c0 < 4) {
      e->c0++, e->c1 = 0;
      e->b1 = false;
      anim_play_from_frame(&e->anim, CLIP_GUARD_ANTICIPATE, 0);
      e->body.vx = 0;
      e->st = GD_ANTIC;
      return;
    }
    if (!club && e->c1 < 2) {
      e->c1++, e->c0 = 0;
      anim_play_from_frame(&e->anim, CLIP_GUARD_STOMP_ANTIC, 0);
      e->body.vx = 0;
      e->st = GD_STOMP_ANTIC, e->t0 = 0;
      return;
    }
  }
}

/* Alert: towards the hero, walking or running (Chase), or turning first */
static void guard_alert(Enemy *e) {
  float dx = hero_x() - e->body.x;
  if (dx == 0) return;
  bool right = dx > 0;
  e->ax = right ? 5 : -5, e->ay = right ? 10 : -10;
  if (right != e->b0) {
    cam_rumble(RUMBLE_OFF);
    guard_face(e, right);
    anim_play_from_frame(&e->anim, CLIP_GUARD_TURN, 0);
    e->st = GD_TURN;
    return;
  }
  float dy = hero_y() - e->body.y, dist = sqrtf(dx * dx + dy * dy);
  if (e->b1 || dist > guard_var(e, 0)) {
    if (e->st != GD_RUN) cam_shake(SHAKE_SMALL), cam_rumble(RUMBLE_SMALL);   /* (SmallRumble) */
    e->b1 = true;
    anim_play(&e->anim, CLIP_GUARD_RUN);
    e->st = GD_RUN;
  } else {
    cam_rumble(RUMBLE_OFF);
    anim_play(&e->anim, CLIP_GUARD_WALK);
    e->st = GD_WALK;
  }
}

static void guard_start(Enemy *e) {
  /* Initiate: its roam (and idle spot) about where it starts; Start Left / Right */
  float roam = guard_var(e, 1);
  e->qx = e->body.x - roam, e->qy = e->body.x + roam, e->tx = e->body.x;
  const Ent *v = enemy_rec(e, ET_VARS);
  guard_face(e, !(v && (v->a & 1)));
  e->wk_stop = 1;   /* (Woken) */
  anim_play(&e->anim, CLIP_GUARD_DORMANT);
  e->st = GD_DORMANT;
}

static bool guard_overhead(const Enemy *e) { return hero_in_box(e, 0, 1.9629f, 1.545f, 1.7664f); }

static void guard_update(Enemy *e) {
  bool done = (e->anim.events & ANIM_DONE) != 0, hit = (e->flags & 8) != 0;
  bool attack = e->can_see && guard_in_attack(e), alert = e->can_see && e->in_alert;
  bool lose = !e->can_see || !e->in_alert || hero_x() > e->qy || hero_x() < e->qx;
  e->t0 += DT;
  switch (e->st) {
    case GD_DORMANT:
      if (attack || hit) anim_play_from_frame(&e->anim, CLIP_GUARD_WAKE, 0), e->st = GD_WAKE;
      break;
    case GD_WAKE:
      if (done) anim_play(&e->anim, CLIP_GUARD_IDLE), e->st = GD_COOLDOWN, e->t0 = 0;
      break;
    case GD_COOLDOWN:
      if (e->t0 >= 0.21f) guard_idle(e);
      break;
    case GD_IDLE:
      if (attack) guard_face_hero(e);
      else if (alert && guard_hero_in_roam(e)) {
        /* In Roam Distance?, Woken?: startled first if it went back to its spot */
        if (e->wk_stop) guard_alert(e);
        else anim_play_from_frame(&e->anim, CLIP_GUARD_STARTLE, 0), e->wk_stop = 1, e->st = GD_STARTLE;
      } else if (e->t0 >= 4) {
        /* Return Check: back to its spot */
        if (e->body.x > e->tx + 1 || e->body.x < e->tx - 1) {
          bool right = e->body.x < e->tx - 1;
          if (right != e->b0) {
            guard_face(e, right);
            anim_play_from_frame(&e->anim, CLIP_GUARD_TURN, 0);
            e->st = GD_TURN_BACK;
          } else
            e->st = GD_RETURN, anim_play(&e->anim, CLIP_GUARD_WALK), e->wk_stop = 0, e->b1 = false;
        } else
          e->st = GD_WAIT;
      }
      break;
    case GD_WAIT:
      guard_idle(e);
      break;
    case GD_STARTLE:
      if (done) guard_alert(e);
      break;
    case GD_TURN:
      if (guard_overhead(e)) {
        e->b1 = false;
        anim_play_from_frame(&e->anim, CLIP_GUARD_ANTICIPATE, 0);
        e->body.vx = 0;
        e->st = GD_ANTIC;
      } else if (done)
        guard_idle(e);
      break;
    case GD_WALK:
    case GD_RUN: {
      float dx = hero_x() - e->body.x, dy = hero_y() - e->body.y;
      if (e->st == GD_WALK && sqrtf(dx * dx + dy * dy) > guard_var(e, 0)) guard_alert(e);
      else if (attack) guard_face_hero(e);
      else if (lose) {
        cam_rumble(RUMBLE_OFF);
        e->body.vx = 0;
        anim_play_from_frame(&e->anim, e->st == GD_RUN ? CLIP_GUARD_STOP_RUN : CLIP_GUARD_STOP_WALK, 0);
        e->b1 = false;
        e->st = GD_STOP;
      } else if (e->t0 > 0)
        guard_alert(e), e->t0 = 0;   /* (WAIT: Alert again, the next frame) */
      break;
    }
    case GD_STOP:
      if (done) guard_idle(e);
      break;
    case GD_TURN_BACK:
      if (done) e->st = GD_RETURN, anim_play(&e->anim, CLIP_GUARD_WALK), e->wk_stop = 0, e->b1 = false;
      break;
    case GD_RETURN:
      if (attack) guard_face_hero(e);
      else if (alert && guard_hero_in_roam(e)) guard_alert(e);
      else if (e->b0 ? e->body.x > e->tx - 1 : e->body.x < e->tx + 1) guard_idle(e);
      break;
    case GD_ANTIC:
      if (done) {
        /* Attack: its club swung (Swipe), then the impact */
        hitbox_on(e, 0, CLIP_GUARD_SWIPE);
        anim_play_from_frame(&e->anim, CLIP_GUARD_ATTACK2, 0);
        e->st = GD_ATTACK, e->t0 = 0;
      }
      break;
    case GD_ATTACK:
      if (e->t0 >= 0.201f) {
        cam_shake(SHAKE_AVERAGE);   /* (Attack Recoil) */
        e->st = GD_RECOIL, e->t0 = 0;
      }
      break;
    case GD_RECOIL:
      if (e->t0 >= 0.14f) e->body.vx = 0, e->st = GD_ATTACK_END;
      break;
    case GD_ATTACK_END:
      if (done || !e->anim.playing) anim_play(&e->anim, CLIP_GUARD_IDLE), e->st = GD_COOLDOWN, e->t0 = 0;
      break;
    case GD_STOMP_ANTIC:
      if (e->t0 >= 0.35f) anim_play_from_frame(&e->anim, CLIP_GUARD_STOMP_JUMP, 0), e->st = GD_STOMP_AIR;
      break;
    case GD_STOMP_AIR:
      if (done) {
        /* Land: two shockwaves, either way */
        e->body.vx = 0;
        anim_play_from_frame(&e->anim, CLIP_GUARD_STOMP_LAND, 0);
        wave_spawn(e->body.x, e->body.y - 4, true, 18, 1.25f);
        wave_spawn(e->body.x, e->body.y - 4, false, 18, 1.25f);
        cam_shake(SHAKE_AVERAGE);
        e->st = GD_STOMP_LAND;
      }
      break;
    case GD_STOMP_LAND:
      if (done) anim_play(&e->anim, CLIP_GUARD_IDLE), e->st = GD_STOMP_CD, e->t0 = 0;
      break;
    case GD_STOMP_CD:
      if (e->t0 >= 0.4f) e->st = GD_COOLDOWN, e->t0 = 0;
      break;
  }
}

static void guard_fixed(Enemy *e) {
  switch (e->st) {
    case GD_WALK: e->body.vx = e->ax; break;
    case GD_RUN: e->body.vx = e->ay; break;
    case GD_RETURN: e->body.vx = e->b0 ? 5 : -5; break;
    case GD_RECOIL: e->body.vx = e->b0 ? -5 : 5; break;            /* (Recoil) */
    case GD_STOMP_AIR: e->body.vx = e->b0 ? -10 : 10; break;       /* (Jump Velocity: back) */
  }
}

/* ---------------------------------------------------------------- the False Knight: FalseyControl, Check Health */
enum {
  FK_DORMANT, FK_START_FALL, FK_STATE1, FK_FIRST_IDLE, FK_IDLE, FK_TURN, FK_JUMP_ANTIC, FK_RISE, FK_FALL,
  FK_JA_ANTIC, FK_JA_RISE, FK_JA_FALL, FK_JA_HIT, FK_JA_SLAM, FK_JA_RECOIL, FK_JA_RECOIL2, FK_JA_END,
  FK_S_ANTIC, FK_S_RISE, FK_S_FALL, FK_S_LAND, FK_S_ATTACK_ANTIC, FK_S_ATTACK, FK_SLAM, FK_S_RECOVER,
  FK_RUN_ANTIC, FK_RUN, FK_STUN_START, FK_STUN_AIR, FK_STUN_LAND, FK_ROLL_END, FK_OPEN, FK_OPENED, FK_HIT,
  FK_STUN_FAIL, FK_RECOVER, FK_IDLE_PAUSE, FK_RAGE_ANTIC, FK_RISE2, FK_FALL2, FK_STATE2, FK_R_ATTACK_ANTIC,
  FK_RAGE_BEGIN, FK_RAGE, FK_PARTICLE_PAUSE, FK_ANIM_END, FK_RAGE_END, FK_JA_ANTIC2, FK_JA_RISE2, FK_JA_FALL2,
  FK_JA_HIT2, FK_FLOOR_BREAK, FK_DEATH_LAND, FK_DEATH_OPEN, FK_OPENED2, FK_HIT2, FK_DEATH_ANIM, FK_STEAM, FK_READY,
  FK_BLOW, FK_HEAD_LAND, FK_DEAD
};
#define FK_SCALE 1.3f
#define FK_RECOVER_HP 65     /* (Check Health: Recover HP, its HP at the start) */
#define FK_HEAD_HP 40
#define FK_HEAD_X 2.29f      /* (the Head's place in the False Knight: shown, or out of the way) */
#define FK_HEAD_Y -3.2709f
#define MAX_BARRELS 12

typedef struct {
  bool on, broken, deflected;
  float x, y, vx, vy, ang, spin, scale, gravity, t;
} Barrel;

static struct {
  int8_t fk, head;              /* (their slots) */
  bool head_shown, first_jump, inert, going_right, kinematic;
  uint8_t turns, jump_count, ja_row, slam_row, stunned, rages;
  float jump_x, recoil, stun_x, idle_min, idle_max, shock_x;
  /* the Barrel Summoner (summon) */
  uint8_t summon_st, spawns;
  float summon_t, summon_x0, summon_x1, summon_y;
  Barrel barrels[MAX_BARRELS];
  /* its arena (Battle Control) and floor (Floor Control) */
  int16_t battle, floor;        /* (their records, -1: none) */
  uint8_t battle_st, floor_st;
  float battle_t;
  /* what is left: the Death Head, the staff */
  bool dhead_on, staff_on;
  Body dhead, staff;
  Anim dhead_anim;
  float staff_ang, staff_w;
  Body bits[3];
  bool bits_on;
} fk;

enum { BT_DETECT, BT_FIGHT, BT_END_WAIT, BT_DONE };
enum { FL_IDLE, FL_CRACKED, FL_BROKEN };

static Enemy *fk_body(void) { return fk.fk >= 0 && en[fk.fk].mode != EM_OFF ? &en[fk.fk] : NULL; }
static Enemy *fk_head(void) { return fk.head >= 0 && en[fk.head].mode != EM_OFF ? &en[fk.head] : NULL; }

static void fk_face(Enemy *e, bool right) {
  fk.stun_x = right ? -10 : 10;
  e->b0 = right;
  set_scale_x(e, right ? FK_SCALE : -FK_SCALE);
}

static bool fk_ground(const Enemy *e) { return e->body.ncontacts && side_hit(e, 3); }

/* (Rise / Fall: the speed up slowed, the speed down quickened, a fixed step at a time) */
static void fk_air_fixed(Enemy *e) {
  switch (e->st) {
    case FK_RISE: case FK_JA_RISE: case FK_S_RISE: case FK_RISE2: case FK_JA_RISE2: e->body.vy *= 0.85f; break;
    case FK_FALL: case FK_JA_FALL: case FK_S_FALL: case FK_FALL2: case FK_JA_FALL2: e->body.vy *= 1.15f; break;
    case FK_RUN: e->body.vx = fk.jump_x; break;   /* (Run Speed) */
  }
}

static void fk_hitter(Enemy *e, int clip) {
  if (clip < 0) {
    e->hb_on = 0, e->sub_hb = 0;
    return;
  }
  e->hb_on |= 1;
  anim_play_from_frame(&e->sub, clip, 0);
  e->sub_hb = 1;
}

static void fk_jump(Enemy *e, float gravity, int clip, float vy, int st) {
  e->body.gravity_scale = gravity;
  anim_play_from_frame(&e->anim, clip, 0);
  e->body.vx = fk.jump_x, e->body.vy = vy;
  cam_shake(SHAKE_ENEMY_KILL);
  e->st = st;
}

static void fk_idle(Enemy *e) {
  e->body.gravity_scale = 0.39f;
  anim_play(&e->anim, CLIP_FK_IDLE);
  fk_hitter(e, -1);
  e->wait = rand_range(fk.idle_min, fk.idle_max);
  e->t0 = 0;
  e->st = FK_IDLE;
}

/* the hero's x, its own */
static bool fk_hero_right(const Enemy *e) { return hero_x() > e->body.x; }

/* JA Check Hero Pos .. JA Antic */
static void fk_jump_attack(Enemy *e) {
  fk.jump_count = 0;
  cam_rumble(RUMBLE_OFF);
  bool right = fk_hero_right(e);
  fk.jump_x = hero_x() + (right ? -3 : 3) - e->body.x;
  fk.recoil = right ? -3 : 3;
  e->body.vx = 0;
  fk.jump_x *= 0.58f;
  fk.jump_x = fk.jump_x > 12 ? 12 : fk.jump_x < -12 ? -12 : fk.jump_x;
  anim_play_from_frame(&e->anim, CLIP_FK_JUMP_ANTIC, 0);
  e->st = FK_JA_ANTIC;
}

static void fk_slam_antic(Enemy *e) {
  anim_play_from_frame(&e->anim, CLIP_FK_ATTACK_ANTIC, 0);
  e->st = FK_S_ATTACK_ANTIC, e->t0 = 0;
}

static void fk_walls_jump(Enemy *e);

/* Move Choice: a run at a far hero, else a slam, a jump attack or a jump */
static void fk_move_choice(Enemy *e) {
  fk.turns = 0;
  for (int guard = 0; guard < 16; guard++) {
    float dx = hero_x() - e->body.x, dy = hero_y() - e->body.y;
    if (dx * dx + dy * dy > 21 * 21) {
      /* Run Antic: at its scale times 14 */
      anim_play_from_frame(&e->anim, CLIP_FK_RUN_ANTIC, 0);
      fk.jump_x = e->sx * 14;
      e->st = FK_RUN_ANTIC;
      return;
    }
    int r = (int)rand_range(0, 3);
    if (r == 0) {
      /* S Check Hero Pos */
      if (fk.slam_row > 2) continue;
      fk.ja_row = 0, fk.slam_row++, fk.jump_count = 0;
      bool right = fk_hero_right(e);
      fk.jump_x = hero_x() + (right ? -1 : 1) * rand_range(12, 18) - e->body.x;
      fk.recoil = right ? -5 : 5, fk.shock_x = right ? 5.5f : -5.5f, fk.going_right = right;
      /* S Antic: a smash from where it is if the hero is far, else a jump back first */
      if (dx * dx + dy * dy >= 12 * 12) {
        fk_slam_antic(e);
        return;
      }
      fk.jump_x *= 0.9f;
      fk.jump_x = fk.jump_x > 12 ? 12 : fk.jump_x < -12 ? -12 : fk.jump_x;
      anim_play_from_frame(&e->anim, CLIP_FK_JUMP_ANTIC, 0);
      e->st = FK_S_ANTIC;
      return;
    }
    if (r == 1) {
      /* Row Check */
      if (fk.ja_row > 3) continue;
      fk.slam_row = 0, fk.ja_row++;
      fk_jump_attack(e);
      return;
    }
    /* Determine Jump */
    if (fk.jump_count > 0 || fk.stunned == 2) continue;
    fk.jump_count++;
    if (rand_range(0, 1) < 0.5f) fk_walls_jump(e);
    else {
      /* Towards */
      float hx = hero_x() < 15 ? 15 : hero_x() > 42 ? 42 : hero_x();
      fk.jump_x = (hx - e->body.x) * 0.9f;
      fk.jump_x = fk.jump_x > 12 ? 12 : fk.jump_x < -12 ? -12 : fk.jump_x;
    }
    anim_play_from_frame(&e->anim, CLIP_FK_JUMP_ANTIC, 0);
    e->st = FK_JUMP_ANTIC;
    return;
  }
  fk_idle(e);
}

/* Walls Check, Random: away from a near wall, else either way */
static void fk_walls_jump(Enemy *e) {
  if (phys_ray(e->body.x, e->body.y, -1, 0, 8, CF_TERRAIN, NULL)) fk.jump_x = 5;          /* (Random R, Random Right) */
  else if (phys_ray(e->body.x, e->body.y, 1, 0, 8, CF_TERRAIN, NULL)) fk.jump_x = -5;     /* (Random L, Random Left) */
  else {
    fk.jump_x = rand_range(-10, 10);
    fk.jump_x = fk.jump_x <= 0 ? (fk.jump_x > -5 ? -5 : fk.jump_x) : (fk.jump_x < 5 ? 5 : fk.jump_x);
  }
}

static void fk_head_show(bool on) {
  Enemy *h = fk_head();
  fk.head_shown = on;
  if (h) {
    if (on) h->flags &= (uint8_t)~1;
    else h->flags |= 1;
  }
}

/* Recover / Stun Fail: up again */
static void fk_recover(Enemy *e, bool stunned) {
  if (stunned) fk.stunned++, cam_shake(SHAKE_ENEMY_KILL);
  fk_head_show(false);
  fk.kinematic = false;
  anim_play_from_frame(&e->anim, CLIP_FK_STUN_RECOVER, 0);
  e->damage = 1;
  if (!stunned) e->flags &= (uint8_t)~4;
  e->st = stunned ? FK_RECOVER : FK_STUN_FAIL;
}

static void barrel_summon(void);
static void fk_floor_event(int ev);
static void fk_battle_end(void);

/* (Check Health: its health gone, STUN, its health back) */
static void fk_stun(Enemy *e) {
  if (fk.inert) return;
  e->hp = FK_RECOVER_HP;
  /* Check Direction: facing the hero; Stun Start */
  fk_hitter(e, -1);
  bool right = fk_hero_right(e);
  if (e->b0 != right) fk_face(e, right), fk.jump_x = right ? 14 : -14;
  e->flags |= 4;
  e->damage = 0;
  e->body.gravity_scale = 1;
  anim_play_from_frame(&e->anim, CLIP_FK_STUN_ROLL, 0);
  e->body.vx = fk.stun_x, e->body.vy = 20;
  cam_shake(SHAKE_BIG);
  cam_rumble(RUMBLE_OFF);
  e->st = FK_STUN_START;
}

static void fk_start(Enemy *e) {
  fk.idle_min = fk.idle_max = 1;
#ifdef HOST
  if (getenv("FKSTUNNED")) fk.stunned = (uint8_t)atoi(getenv("FKSTUNNED"));   /* (tests: the fight further on) */
#endif
  fk.fk = (int8_t)(e - en);
  e->body.gravity_scale = 0;
  e->flags |= 32;   /* (its renderer off till it falls) */
  fk_face(e, e->sx > 0);
  e->st = FK_DORMANT;
  fk_head_show(false);
}

static void fk_head_start(Enemy *e) {
  fk.head = (int8_t)(e - en);
  e->body.gravity_scale = 0;
  e->flags |= 1;
  e->damage = 0;
}

/* the Head, where its FSM puts it in the False Knight */
static void fk_head_place(void) {
  Enemy *h = fk_head(), *e = fk_body();
  if (!h || !e) return;
  h->sx = e->sx;
  h->body.x = e->body.x + FK_HEAD_X * e->sx, h->body.y = e->body.y + FK_HEAD_Y * FK_SCALE;
  h->body.vx = h->body.vy = 0;
}

/* (BATTLE START) */
static void fk_battle_start(void) {
  Enemy *e = fk_body();
  if (!e || e->st != FK_DORMANT) return;
  /* Start Fall */
  e->flags &= (uint8_t)~32;
  cam_shake(SHAKE_BIG);
  e->body.gravity_scale = 1;
  e->st = FK_START_FALL;
}

static void fk_update(Enemy *e) {
  bool done = (e->anim.events & ANIM_DONE) != 0, sub_done = e->sub_hb && ((e->sub.events & ANIM_DONE) || !e->sub.playing);
  bool hit = (e->flags & 8) != 0;
  (void)hit;
  e->t0 += DT;
  switch (e->st) {
    case FK_DORMANT:
      break;
    case FK_START_FALL:
      if (fk_ground(e)) {
        /* Rubble End: KILL ALL ENEMIES; State 1 */
        for (int i = 0; i < MAX_ENEMIES; i++)
          if (en[i].mode == EM_ALIVE && en[i].ent != NO_ENT && (ent_at(en[i].ent)->s1 & EF_PREBATTLE)) {
            en[i].flags &= (uint8_t)~4, en[i].evasion = 0;
            enemy_hit(&en[i], 90, 9999);
          }
        e->body.vx = 0;
        anim_play_from_frame(&e->anim, CLIP_FK_LAND, 0);
        cam_shake(SHAKE_AVERAGE);
        e->st = FK_STATE1;
      }
      break;
    case FK_STATE1:
      if (done) {
        if (fk.first_jump) fk_idle(e);
        else {
          /* Music: (its title shown), First Idle */
          e->body.gravity_scale = 0.39f;
          anim_play(&e->anim, CLIP_FK_IDLE);
          fk_hitter(e, -1);
          fk.first_jump = true;
          e->st = FK_FIRST_IDLE, e->t0 = 0;
        }
      }
      break;
    case FK_FIRST_IDLE:
      if (e->t0 >= 1.5f) {
        fk_walls_jump(e);   /* (Random) */
        fk.jump_x = rand_range(-10, 10);
        fk.jump_x = fk.jump_x <= 0 ? (fk.jump_x > -5 ? -5 : fk.jump_x) : (fk.jump_x < 5 ? 5 : fk.jump_x);
        anim_play_from_frame(&e->anim, CLIP_FK_JUMP_ANTIC, 0);
        e->st = FK_JUMP_ANTIC;
      }
      break;
    case FK_IDLE: {
      bool right = fk_hero_right(e);
      if (e->b0 != right) {
        /* Turn L / R: three turns in a row, then a move */
        if (fk.turns == 3) {
          fk_move_choice(e);
          break;
        }
        fk.turns++;
        fk_face(e, right);
        anim_play_from_frame(&e->anim, CLIP_FK_TURN, 0);
        e->st = FK_TURN;
      } else if (e->t0 >= e->wait)
        fk_move_choice(e);
      break;
    }
    case FK_TURN:
      if (done) fk_idle(e);
      break;
    case FK_JUMP_ANTIC:
      if (done) fk_jump(e, 0.125f, CLIP_FK_JUMP, 90, FK_RISE);
      break;
    case FK_RISE: case FK_S_RISE: case FK_RISE2:
      if (e->body.vy < 0) e->st++;   /* (FALL) */
      break;
    case FK_FALL: case FK_FALL2:
      if (fk_ground(e)) {
        /* State 1 / State 2: landed */
        e->body.vx = 0;
        anim_play_from_frame(&e->anim, CLIP_FK_LAND, 0);
        cam_shake(SHAKE_AVERAGE);
        e->st = e->st == FK_FALL ? FK_STATE1 : FK_STATE2;
      }
      break;
    case FK_JA_ANTIC:
      if (done) fk_jump(e, 0.12f, CLIP_FK_JUMP_ATTACK_UP, 90, FK_JA_RISE);
      break;
    case FK_JA_RISE: case FK_JA_RISE2:
      if (e->body.vy < 0) e->st++;
      break;
    case FK_JA_FALL: case FK_JA_FALL2:
      if (e->st == FK_JA_FALL2) fk.inert = true;   /* (FALLEN: Check Health inert) */
      if (phys_ray(e->body.x, e->body.y, 0, -1, 9.5f, CF_TERRAIN, NULL)) {
        /* JA Hit: the mace down */
        fk_hitter(e, CLIP_FK_JUMP_ATTACK_HIT_1);
        anim_play(&e->anim, CLIP_FK_BLANK);
        e->st = e->st == FK_JA_FALL ? FK_JA_HIT : FK_JA_HIT2;
      }
      break;
    case FK_JA_HIT:
      if (fk_ground(e)) {
        /* JA Slam */
        e->body.vx = 0;
        cam_shake(SHAKE_BIG);
        e->st = FK_JA_SLAM, e->t0 = 0;
      }
      break;
    case FK_JA_SLAM:
      if (e->t0 >= 0.083f || sub_done) {
        /* (Barrels?) JA Recoil: back off, barrels summoned */
        fk_hitter(e, -1);
        anim_play_from_frame(&e->anim, CLIP_FK_JUMP_ATTACK_HIT_2, 0);
        e->body.vx = fk.recoil;
        barrel_summon();
        e->st = FK_JA_RECOIL;
      }
      break;
    case FK_JA_RECOIL:
      if (done) {
        anim_play_from_frame(&e->anim, CLIP_FK_JUMP_ATTACK_HIT_3, 0);
        fk.recoil /= 2;
        e->body.vx = fk.recoil;
        e->st = FK_JA_RECOIL2, e->t0 = 0;
      }
      break;
    case FK_JA_RECOIL2:
      if (e->t0 >= 0.084f) e->body.gravity_scale = 0.39f, e->body.vx = 0, e->st = FK_JA_END;
      break;
    case FK_JA_END:
      if (done || !e->anim.playing) fk_idle(e);
      break;
    case FK_S_ANTIC:
      if (done) {
        /* Check Jump Dir, Walls Check 2: no jump back into a wall */
        float dir = fk.jump_x <= 0 ? -1 : 1;
        if (phys_ray(e->body.x, e->body.y, dir, 0, 8, CF_TERRAIN, NULL)) fk_slam_antic(e);
        else {
          cam_shake(SHAKE_ENEMY_KILL);
          anim_play_from_frame(&e->anim, CLIP_FK_JUMP, 0);
          e->body.vx = fk.jump_x, e->body.vy = 105;
          e->st = FK_S_RISE;
        }
      }
      break;
    case FK_S_FALL:
      if (fk_ground(e)) {
        cam_shake(SHAKE_AVERAGE);
        e->body.vx = 0;
        anim_play_from_frame(&e->anim, CLIP_FK_LAND, 0);
        e->st = FK_S_LAND;
      }
      break;
    case FK_S_LAND:
      if (done) fk_slam_antic(e);
      break;
    case FK_S_ATTACK_ANTIC:
      if (e->t0 >= 1.2f) {
        /* S Attack: the mace swung */
        fk_hitter(e, CLIP_FK_ATTACK);
        anim_play(&e->anim, CLIP_FK_BLANK);
        e->st = FK_S_ATTACK, e->t0 = 0;
      }
      break;
    case FK_S_ATTACK:
      if (e->t0 >= 0.12f) cam_shake(SHAKE_BIG), e->st = FK_SLAM;
      break;
    case FK_SLAM:
      if (sub_done) {
        /* S Attack Recover: barrels, a shockwave the way it faced */
        barrel_summon();
        wave_spawn(e->body.x + fk.shock_x, e->body.y - 5.8f, fk.going_right, 22, 1);
        anim_play_from_frame(&e->anim, CLIP_FK_ATTACK_RECOVER, 0);
        fk_hitter(e, -1);
        e->st = FK_S_RECOVER;
      }
      break;
    case FK_S_RECOVER:
      if (done) fk_idle(e);
      break;
    case FK_RUN_ANTIC:
      if (done) {
        cam_rumble(RUMBLE_MED);
        anim_play(&e->anim, CLIP_FK_RUN);
        e->st = FK_RUN;
      }
      break;
    case FK_RUN: {
      float dx = hero_x() - e->body.x, dy = hero_y() - e->body.y;
      if (dx * dx + dy * dy < 14 * 14) fk_jump_attack(e);
      break;
    }
    case FK_STUN_START:
      e->st = FK_STUN_AIR;
      break;
    case FK_STUN_AIR:
      if (fk_ground(e)) e->st = FK_STUN_LAND, e->t0 = 0;
      break;
    case FK_STUN_LAND:
      /* (END: rolling left fast, or half a second) */
      if (e->body.vx < -3 || e->t0 >= 0.5f) {
        /* Roll End: lying there a while */
        fk.kinematic = true;
        e->body.vx = e->body.vy = 0;
        anim_play_from_frame(&e->anim, CLIP_FK_STUN_ROLL_END, 0);
        e->wait = pd_flag(PDF_FK_FIRST_PLOP) ? 1.2f : 2.5f;
        pd_set_flag(PDF_FK_FIRST_PLOP, true);
        e->st = FK_ROLL_END, e->t0 = 0;
      }
      break;
    case FK_ROLL_END:
      if (e->t0 >= e->wait) anim_play_from_frame(&e->anim, CLIP_FK_STUN_OPEN, 0), e->st = FK_OPEN;
      break;
    case FK_OPEN:
      if (done) {
        /* Head Reset, Opened: the head out, for five seconds */
        Enemy *h = fk_head();
        if (h) anim_play_from_frame(&h->anim, h->anim.clip, 0);
        anim_play(&e->anim, CLIP_FK_STUN_OPENED);
        fk_head_show(true);
        e->st = FK_OPENED, e->t0 = 0;
      }
      break;
    case FK_OPENED:
      if (e->t0 >= 5) fk_recover(e, false);
      break;
    case FK_HIT: case FK_HIT2:
      if (done) anim_play(&e->anim, CLIP_FK_STUN_OPENED), e->st = e->st == FK_HIT ? FK_OPENED : FK_OPENED2, e->t0 = 0;
      break;
    case FK_STUN_FAIL:
      if (done) fk_idle(e);
      break;
    case FK_RECOVER:
      if (done) {
        anim_play(&e->anim, CLIP_FK_IDLE);
        e->flags &= (uint8_t)~4;
        e->st = FK_IDLE_PAUSE, e->t0 = 0;
      }
      break;
    case FK_IDLE_PAUSE:
      if (e->t0 >= 0.5f) {
        /* Rage Jump Antic: to the arena's middle */
        fk.jump_x = (28.9f - e->body.x) * 1.05f;
        anim_play_from_frame(&e->anim, CLIP_FK_JUMP_ANTIC, 0);
        e->st = FK_RAGE_ANTIC;
      }
      break;
    case FK_RAGE_ANTIC:
      if (done) fk_jump(e, 0.3f, CLIP_FK_JUMP, 105, FK_RISE2);
      break;
    case FK_STATE2:
      if (done) {
        fk.rages = 8;
        anim_play_from_frame(&e->anim, CLIP_FK_ATTACK_ANTIC, 0);
        e->st = FK_R_ATTACK_ANTIC, e->t0 = 0;
      }
      break;
    case FK_R_ATTACK_ANTIC:
      if (e->t0 >= 0.7f) {
        fk_hitter(e, CLIP_FK_RAGE);
        anim_play(&e->anim, CLIP_FK_BLANK);
        e->st = FK_RAGE_BEGIN;
      }
      break;
    case FK_RAGE_BEGIN:
      e->st = FK_RAGE, e->t0 = 0;
      fk_hitter(e, CLIP_FK_RAGE);
      break;
    case FK_RAGE:
      if (e->t0 >= 0.249f) {
        /* Rage Slam; Floor Crack? (the last of the second rage's) */
        cam_shake(SHAKE_AVERAGE);
        if (fk.stunned == 2 && fk.rages == 1) fk_floor_event(FL_CRACKED);
        e->st = FK_PARTICLE_PAUSE, e->t0 = 0;
      }
      break;
    case FK_PARTICLE_PAUSE:
      if (e->t0 >= 0.1f) e->st = FK_ANIM_END;
      break;
    case FK_ANIM_END:
      if (sub_done) {
        /* Turn: barrels, the other way */
        barrel_summon();
        fk.rages--;
        set_scale_x(e, -e->sx);
        if (fk.rages <= 0) {
          /* Rage Check */
          if (fk.stunned >= 3) {
            /* JA Antic 2: the last jump, onto the floor's middle */
            e->flags |= 4;
            fk_hitter(e, -1);
            fk.jump_x = (34 - e->body.x) * 0.76f;
            anim_play_from_frame(&e->anim, CLIP_FK_JUMP_ANTIC, 0);
            fk_face(e, true);
            e->st = FK_JA_ANTIC2;
            break;
          }
          /* To Phase 2 / 3, Rage End */
          fk.idle_min = 0.8f, fk.idle_max = 1;
          fk_hitter(e, -1);
          anim_play(&e->anim, CLIP_FK_IDLE);
          e->st = FK_RAGE_END, e->t0 = 0;
        } else
          e->st = FK_RAGE, e->t0 = 0, fk_hitter(e, CLIP_FK_RAGE);
      }
      break;
    case FK_RAGE_END:
      if (e->t0 >= 0.75f) fk_idle(e);
      break;
    case FK_JA_ANTIC2:
      if (done) {
        e->body.gravity_scale = 0.2f;
        anim_play_from_frame(&e->anim, CLIP_FK_JUMP_ATTACK_UP, 0);
        e->body.vx = fk.jump_x, e->body.vy = 90;
        e->st = FK_JA_RISE2;
      }
      break;
    case FK_JA_HIT2:
      if (fk_ground(e)) {
        /* Floor Break: through the floor */
        fk_hitter(e, -1);
        fk_floor_event(FL_BROKEN);
        e->body.gravity_scale = 1;
        anim_play_from_frame(&e->anim, CLIP_FK_DEATH_FALL, 0);
        e->body.vx = 0, e->body.vy = 15;
        cam_shake(SHAKE_BIG);
        e->damage = 0;
        e->st = FK_FLOOR_BREAK;
      }
      break;
    case FK_FLOOR_BREAK:
      if (e->body.vy <= 0 && fk_ground(e)) {
        anim_play_from_frame(&e->anim, CLIP_FK_DEATH_LAND, 0);
        fk.kinematic = true;
        e->body.vx = e->body.vy = 0;
        e->st = FK_DEATH_LAND, e->t0 = 0;
      }
      break;
    case FK_DEATH_LAND:
      if (e->t0 >= 2) anim_play_from_frame(&e->anim, CLIP_FK_STUN_OPEN, 0), e->st = FK_DEATH_OPEN;
      break;
    case FK_DEATH_OPEN:
      if (done) {
        Enemy *h = fk_head();
        if (h) anim_play_from_frame(&h->anim, h->anim.clip, 0);
        anim_play(&e->anim, CLIP_FK_STUN_OPENED);
        fk_head_show(true);
        e->st = FK_OPENED2;
      }
      break;
    case FK_DEATH_ANIM:
      if (e->t0 >= 1) {
        /* Open Map Shop and Journal; Steam */
        pd_set_flag(PDF_MAPPER_SHOP, true);
        pd_set_flag(PDF_CORN_CROSSROADS_LEFT, true);
        e->flags |= 1;
        cam_shake(SHAKE_BIG);
        e->st = FK_STEAM, e->t0 = 0;
      }
      break;
    case FK_STEAM:
      if (e->t0 >= 3) e->st = FK_READY, e->t0 = 0;
      break;
    case FK_READY:
      if (e->t0 >= 1) {
        /* Blow: the maggot out, the staff flung */
        Enemy *h = fk_head();
        if (h) h->mode = EM_OFF;
        fk.head_shown = false;
        cam_shake(SHAKE_BIG);
        anim_play(&e->anim, CLIP_FK_BODY);
        fk.dhead_on = true;
        memset(&fk.dhead, 0, sizeof fk.dhead);
        fk.dhead.x = e->body.x + 3.11f * e->sx, fk.dhead.y = e->body.y - 2.86f * FK_SCALE;
        fk.dhead.vx = e->b0 ? 4 : -4;
        fk.dhead.ox = -0.1094f * 0.975f * (e->sx < 0 ? -1 : 1), fk.dhead.oy = -0.7812f * 0.975f;
        fk.dhead.hx = 2.8125f / 2 * 0.975f, fk.dhead.hy = 2.1562f / 2 * 0.975f;
        fk.dhead.gravity_scale = 1, fk.dhead.friction = 0.2828f, fk.dhead.mask = CF_SOLID;
        anim_play_from_frame(&fk.dhead_anim, CLIP_FK_DEATH_HEAD_1, 0);
        fk.staff_on = true;
        memset(&fk.staff, 0, sizeof fk.staff);
        fk.staff.x = e->body.x - 1.45f * e->sx, fk.staff.y = e->body.y + 0.04f * FK_SCALE;
        fk.staff.vx = cosf(150 * (float)M_PI / 180) * 20, fk.staff.vy = sinf(150 * (float)M_PI / 180) * 20;
        fk.staff.hx = fk.staff.hy = 0.2f;
        fk.staff.gravity_scale = 1, fk.staff.friction = 0.2828f, fk.staff.mask = CF_SOLID;
        fk.staff_ang = 0, fk.staff_w = 700;
        e->st = FK_BLOW;
      }
      break;
    case FK_BLOW:
      if ((fk.dhead_anim.events & ANIM_DONE) || !fk.dhead_anim.playing) {
        fk.dhead.vx = 0;
        anim_play_from_frame(&fk.dhead_anim, CLIP_FK_DEATH_HEAD_2, 0);
        e->st = FK_HEAD_LAND, e->t0 = 0;
      }
      break;
    case FK_HEAD_LAND:
      if (e->t0 >= 1.5f) {
        /* Decrement Battle Enemies, Cough */
        fk_battle_end();
        e->st = FK_DEAD;
      }
      break;
  }
}

static void fk_fixed(Enemy *e) {
  if (e->st == FK_DORMANT) e->body.vx = e->body.vy = 0;
  fk_air_fixed(e);
}

/* the head's HP gone (its Health Check: STUN END, its HP back) */
static void fk_head_stun_end(Enemy *h) {
  h->hp = FK_HEAD_HP;
  anim_play(&h->anim, CLIP_FKHEAD_HEAD_IDLE);
  Enemy *e = fk_body();
  if (!e) return;
  if (e->st == FK_OPENED || e->st == FK_HIT) fk_recover(e, true);
  else if (e->st == FK_OPENED2 || e->st == FK_HIT2) {
    /* Death Anim Start */
    pd_set_flag(PDF_FALSE_KNIGHT_DEFEATED, true);
    cam_shake(SHAKE_ENEMY_KILL);
    anim_play(&h->anim, CLIP_FKHEAD_HEAD_SPAZ);
    h->flags |= 1;
    fk.kinematic = false;
    anim_play(&e->anim, CLIP_FK_DEATH_SPAZ);
    e->st = FK_DEATH_ANIM, e->t0 = 0;
  }
}

/* (the head's HealthManager: HIT to the False Knight) */
static void fk_head_hit(void) {
  Enemy *e = fk_body(), *h = fk_head();
  if (!e) return;
  if (e->st == FK_OPENED || e->st == FK_OPENED2 || e->st == FK_HIT || e->st == FK_HIT2) {
    anim_play_from_frame(&e->anim, CLIP_FK_STUN_HIT, 0);
    if (h) anim_play_from_frame(&h->anim, CLIP_FKHEAD_HEAD_HIT, 0);
    e->st = e->st == FK_OPENED || e->st == FK_HIT ? FK_HIT : FK_HIT2;
  }
}

/* ---------------------------------------------------------------- the barrels the False Knight brings down */
/* (summon: Determine Spawns, Spawn, Check Spawns: six to eight, a fifth of a second or so apart) */
static void barrel_summon(void) {
  if (fk.summon_st) return;
  fk.spawns = (uint8_t)(6 + (int)rand_range(0, 3));
  fk.summon_t = rand_range(0.15f, 0.25f);
  fk.summon_st = 1;
}

static void barrels_tick(void) {
  if (fk.summon_st && (fk.summon_t -= DT) <= 0) {
    if (fk.spawns == 0) fk.summon_st = 0;
    else {
      for (int i = 0; i < MAX_BARRELS; i++) {
        Barrel *b = &fk.barrels[i];
        if (b->on) continue;
        memset(b, 0, sizeof *b);
        b->on = true;
        b->x = rand_range(fk.summon_x0, fk.summon_x1), b->y = fk.summon_y;
        b->scale = rand_range(0.8f, 1);
        b->gravity = 0.325f;
        b->spin = rand_range(0, 1) < 0.5f ? -720 : 720;
        break;
      }
      fk.spawns--;
      fk.summon_t = rand_range(0.15f, 0.25f);
    }
  }
  for (int i = 0; i < MAX_BARRELS; i++) {
    Barrel *b = &fk.barrels[i];
    if (!b->on) continue;
    if (b->broken) {
      if ((b->t += DT) >= 3) b->on = false;
      continue;
    }
    b->vy -= 60 * b->gravity * DT;
    b->ang += b->spin * DT;
    float hx = 0.59f * b->scale, hy = 0.555f * b->scale;
    b->x += b->vx * DT, b->y += b->vy * DT;
    /* (its trigger: the terrain, the hero or an enemy breaks it) */
    bool brk = false;
    for (int k = -1; k <= 1 && !brk; k += 2)
      brk = phys_ray(b->x, b->y, 0, (float)k, hy, CF_TERRAIN, NULL) || phys_ray(b->x, b->y, (float)k, 0, hx, CF_TERRAIN, NULL);
    for (int j = 0; j < MAX_ENEMIES && !brk; j++) {
      Enemy *e = &en[j];
      if (e->mode != EM_ALIVE || (e->flags & 17)) continue;
      float x0, y0, x1, y1;
      enemy_box(e, &x0, &y0, &x1, &y1);
      if (b->x + hx > x0 && b->x - hx < x1 && b->y + hy > y0 && b->y - hy < y1) {
        brk = true;
        if (b->deflected) enemy_hit(e, b->vx >= 0 ? 0 : 180, 10);   /* (damages_enemy: 10, once deflected) */
      }
    }
    if (brk || b->y < -20) b->broken = true, b->t = 0;
  }
}

static int barrels_touch(float x0, float y0, float x1, float y1, int *side) {
  for (int i = 0; i < MAX_BARRELS; i++) {
    Barrel *b = &fk.barrels[i];
    if (!b->on || b->broken) continue;
    float hx = 0.59f * b->scale, hy = 0.555f * b->scale;
    if (x1 > b->x - hx && x0 < b->x + hx && y1 > b->y - hy && y0 < b->y + hy) {
      b->broken = true, b->t = 0;
      *side = b->x > g_hero.body.x ? SIDE_RIGHT : SIDE_LEFT;
      return 1;
    }
  }
  return 0;
}

/* a nail's swing at the barrels: struck the way it swung (Check Direct) */
static void barrels_nail(const float *pts, int npts, float direction) {
  for (int i = 0; i < MAX_BARRELS; i++) {
    Barrel *b = &fk.barrels[i];
    if (!b->on || b->broken || b->deflected) continue;
    float hx = 0.59f * b->scale, hy = 0.555f * b->scale;
    if (!box_meets_shape(b->x - hx, b->y - hy, b->x + hx, b->y + hy, pts, npts)) continue;
    float a = direction * (float)M_PI / 180;
    b->vx = cosf(a) * 45, b->vy = sinf(a) * 45;
    b->gravity = 0.3f;
    b->deflected = true;
  }
}

static void barrels_draw(void) {
  for (int i = 0; i < MAX_BARRELS; i++) {
    const Barrel *b = &fk.barrels[i];
    if (!b->on || b->broken) continue;
    Inst in;
    sprite_inst_rot(SPRITE_FK_BARREL, b->x, b->y, 0.006f, b->scale, b->scale, b->ang, 0, &in);
    gfx_actor(&in, SORT_KEY(0, 0));
  }
}

/* ---------------------------------------------------------------- its arena and floor */
static void fk_floor_event(int ev) {
  if (fk.floor < 0 || fk.floor_st >= ev) return;
  const Ent *f = ent_at(fk.floor);
  fk.floor_st = (uint8_t)ev;
  group_fade(f->group2, 0, 0);   /* (Normal 1, 2 off) */
  if (ev == FL_BROKEN) {
    for (int c = f->a; c < f->a + f->group; c++) phys_collider_enable(c, false);   /* (Break Floor off) */
    fk.bits_on = true;
    for (int k = 0; k < 3; k++) {
      const Ent *r = f + 1;
      for (int j = 1; j <= f->s0; j++)
        if (f[j].flags == 3 + k) r = &f[j];
      Body *b = &fk.bits[k];
      memset(b, 0, sizeof *b);
      b->x = r->x0, b->y = r->y0, b->vx = r->p0, b->vy = r->p1;
      b->gravity_scale = 1;
    }
  }
}

static void fk_battle_end(void) {
  if (fk.battle < 0 || fk.battle_st != BT_FIGHT) return;
  const Ent *b = ent_at(fk.battle);
  persist_set(b->persist);
  if (b->p0 >= 0) ent_set_enabled((int)b->p0, false);
  if (b->p1 >= 0) ent_set_enabled((int)b->p1, false);
  cam_rumble(RUMBLE_OFF);
  fk.battle_st = BT_END_WAIT, fk.battle_t = 0;
}

/* the arena's records: Battle Control (Init; Activate if its fight is over), Floor Control */
static void fk_arena_enter(void) {
  memset(&fk, 0, sizeof fk);
  fk.battle = fk.floor = -1;
  fk.fk = fk.head = -1;
  int n;
  const Ent *es = room_ents(&n);
  for (int i = 0; i < n; i++) {
    if (es[i].type != ENT_OBJ) continue;
    if (es[i].flags == OK_BATTLE) fk.battle = (int16_t)i;
    if (es[i].flags == OK_FK_FLOOR) fk.floor = (int16_t)i;
  }
  if (fk.battle < 0) return;
  const Ent *b = &es[fk.battle];
  fk.summon_x0 = 13.18f, fk.summon_x1 = 44.27f, fk.summon_y = 40.98f;   /* (summon: Summon Min, Max; its y) */
  if (persist_get(b->persist)) {
    /* Activate: the fight over, the gates open, the floor broken, the armour there */
    gates_event(BG_QUICK_OPEN);
    if (b->p0 >= 0) ent_set_enabled((int)b->p0, false);
    if (b->p1 >= 0) ent_set_enabled((int)b->p1, false);
    fk.battle_st = BT_DONE;
    if (fk.floor >= 0) {
      const Ent *f = &es[fk.floor];
      fk.floor_st = FL_BROKEN;
      group_fade(f->group2, 0, 0);
      for (int c = f->a; c < f->a + f->group; c++) phys_collider_enable(c, false);
    }
  } else {
    if (b->p0 >= 0) ent_set_enabled((int)b->p0, false);   /* (Init: CameraLock 1 off) */
    if (b->group) group_fade(b->group, 0, 0);              /* (the armour not there yet) */
    fk.battle_st = BT_DETECT;
    fk.floor_st = FL_IDLE;
  }
}

static void fk_arena_tick(void) {
  if (fk.battle < 0) return;
  const Ent *b = ent_at(fk.battle);
  switch (fk.battle_st) {
    case BT_DETECT: {
      const Body *k = &g_hero.body;
      float x0 = k->x + k->ox - k->hx, x1 = k->x + k->ox + k->hx, y0 = k->y + k->oy - k->hy, y1 = k->y + k->oy + k->hy;
      if (x1 > b->x0 && x0 < b->x1 && y1 > b->y0 && y0 < b->y1) {
        /* Start: the camera locked to the arena, BATTLE START */
        if (b->p0 >= 0) ent_set_enabled((int)b->p0, true);
        if (b->p1 >= 0) ent_set_enabled((int)b->p1, false);
        fk.battle_st = BT_FIGHT;
        gates_event(BG_CLOSE);
        fk_battle_start();
      }
      break;
    }
    case BT_END_WAIT:
      if ((fk.battle_t += DT) >= 2) fk.battle_st = BT_DONE, gates_event(BG_OPEN);   /* (End) */
      break;
  }
  barrels_tick();
  /* what is left of it */
  if (fk.dhead_on) {
    fk.dhead_anim.events = 0;
    anim_update(&fk.dhead_anim, DT);
    body_step(&fk.dhead, DT);
  }
  if (fk.staff_on) {
    body_step(&fk.staff, DT);
    fk.staff_ang += fk.staff_w * DT;
    if (fk.staff.ncontacts) fk.staff_w *= 0.9f;
  }
  if (fk.bits_on)
    for (int k = 0; k < 3; k++) {
      Body *bb = &fk.bits[k];
      bb->vy -= 60 * bb->gravity_scale * DT;
      bb->x += bb->vx * DT, bb->y += bb->vy * DT;
    }
}

static void fk_arena_draw(void) {
  if (fk.battle < 0) return;
  barrels_draw();
  Inst in;
  if (fk.dhead_on && fk.dhead_anim.sprite >= 0) {
    Enemy *e = fk_body();
    float s = 0.975f * (e && e->sx < 0 ? -1 : 1);
    sprite_inst(fk.dhead_anim.sprite, fk.dhead.x, fk.dhead.y, 0.0054f, s, 0.975f, 0, &in);
    gfx_actor(&in, SORT_KEY(0, 0));
  }
  if (fk.staff_on) {
    sprite_inst_rot(SPRITE_FK_STAFF, fk.staff.x, fk.staff.y, 0.005f, 1, 1, fk.staff_ang, 0, &in);
    gfx_actor(&in, SORT_KEY(0, 0));
  }
  if (fk.floor >= 0 && fk.floor_st != FL_IDLE) {
    const Ent *f = ent_at(fk.floor);
    for (int j = 1; j <= f->s0; j++) {
      const Ent *r = &f[j];
      bool show = fk.floor_st == FL_CRACKED ? r->flags <= 1 : r->flags == 2 || (r->flags >= 3 && fk.bits_on);
      if (!show) continue;
      float x = r->x0, y = r->y0;
      if (r->flags >= 3 && fk.bits_on) x = fk.bits[r->flags - 3].x, y = fk.bits[r->flags - 3].y;
      sprite_inst(r->s0, x, y, r->x1, 1, 1, 0, &in);
      gfx_actor(&in, SORT_KEY(r->group, (int)r->a - 32768));
    }
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
/* ---------------------------------------------------------------- Gruz Mother: Big Fly Control (with her bouncer_control);
 * her corpse (corpse) and its burster (burster), which brings out her young */
enum { GF_INVINCIBLE, GF_SLEEP, GF_WAKE, GF_FLY, GF_BUZZ, GF_CHARGE_ANTIC, GF_CHARGE, GF_CHARGE_RECOVER, GF_SUPER_END,
       GF_SLAM_ANTIC, GF_LAUNCH, GF_FLYING, GF_SLAM_HIT, GF_SLAM_END };
#define GF_SCALE 1.25f
#define GF_BUZZ_SPEED 5.0f   /* (bouncer_control's Speed) */
#define GF_SLAM_SPEED 50.0f
/* (her FSM's variables: wait the bouncer's Angle, b1 its Facing Right; ax Charge Angle or Slamming Angle; ay Up Angle,
 * qx Down Angle; qy Super Wait; tx Slam Time; ty Super End Time; t1 Timer; pause0, pause1 Self Vel; c0, c1 Ct Charge,
 * Ct Slam; b0 Slamming Up; start_x, start_y where ObjectJitter jitters about; jy Slam Down's dip) */

/* a box and a polygon (any shape: its outline's points) overlap? */
static bool box_meets_poly(float x0, float y0, float x1, float y1, const float *p, int n) {
  bool in = false;
  float cx = (x0 + x1) / 2, cy = (y0 + y1) / 2;
  for (int i = 0, j = n - 1; i < n; j = i++) {
    float ax = p[2 * j], ay = p[2 * j + 1], bx = p[2 * i], by = p[2 * i + 1];
    if (bx >= x0 && bx <= x1 && by >= y0 && by <= y1) return true;   /* (a corner in the box) */
    if ((ay > cy) != (by > cy) && cx < ax + (bx - ax) * (cy - ay) / (by - ay)) in = !in;
    /* (an edge through the box: clipped to it, something left) */
    float t0 = 0, t1 = 1, dx = bx - ax, dy = by - ay;
    const float q[4][2] = {{-dx, ax - x0}, {dx, x1 - ax}, {-dy, ay - y0}, {dy, y1 - ay}};
    bool cut = true;
    for (int k = 0; k < 4 && cut; k++) {
      if (q[k][0] == 0) {
        if (q[k][1] < 0) cut = false;
        continue;
      }
      float r = q[k][1] / q[k][0];
      if (q[k][0] < 0) t0 = r > t0 ? r : t0;
      else t1 = r < t1 ? r : t1;
      if (t0 > t1) cut = false;
    }
    if (cut) return true;
  }
  return in;   /* (the box's middle in it: the box inside) */
}

/* the Knight in her Battle Range (ET_ZONE: HERO ENTER, HERO EXIT) */
static bool gfly_in_range(const Enemy *e) {
  const Ent *z = enemy_rec(e, ET_ZONE);
  if (!z || g_hero.hidden) return false;
  const Body *k = &g_hero.body;
  float x0 = k->x + k->ox - k->hx, x1 = k->x + k->ox + k->hx, y0 = k->y + k->oy - k->hy, y1 = k->y + k->oy + k->hy;
  float s = e->sx < 0 ? -1.0f : 1.0f, a0 = e->body.x + (s > 0 ? z->x0 : -z->x1), a1 = e->body.x + (s > 0 ? z->x1 : -z->x0);
  if (!(x1 > a0 && x0 < a1 && y1 > e->body.y + z->y0 && y0 < e->body.y + z->y1)) return false;
  float pts[16];
  int np = (int)z->p0 < 8 ? (int)z->p0 : 8;
  for (int j = 0; j < np; j++) {
    const float *f = &z[1 + j / 4].x0;
    pts[2 * j] = e->body.x + f[2 * (j & 3)] * s, pts[2 * j + 1] = e->body.y + f[2 * (j & 3) + 1];
  }
  return box_meets_poly(x0, y0, x1, y1, pts, np);
}

/* FaceObject (sprite facing left) */
static void gfly_face_hero(Enemy *e) {
  if (hero_x() > e->body.x) set_scale_x(e, -GF_SCALE);
  else if (hero_x() < e->body.x) set_scale_x(e, GF_SCALE);
}

static void gfly_velocity(Enemy *e, float degrees, float speed) {
  float a = degrees * (float)M_PI / 180;
  e->body.vx = cosf(a) * speed, e->body.vy = sinf(a) * speed;
}

/* bouncer_control's Aim and the angles it takes off what it bumps (Facing Right) */
static void gfly_aim(Enemy *e, float lo, float hi) {
  e->wait = rand_range(lo, hi);
  float a = fmodf(e->wait, 360);
  if (a < 0) a += 360;
  e->b1 = a < 90 || a >= 270;
}

static void gfly_buzz(Enemy *e) {
  /* Buzz: the bouncer woken (Aim), a while */
  gfly_aim(e, 0, 360);
  e->qy = rand_range(2, 2.8f);
  e->st = GF_BUZZ, e->t0 = 0;
}

static void gfly_super_end(Enemy *e) {
  anim_play(&e->anim, CLIP_GFLY_FLY);
  e->st = GF_SUPER_END, e->t0 = 0;
  if (e->ty <= 0) gfly_buzz(e);   /* (Wait 0: done at once) */
}

static void gfly_launch(Enemy *e, bool up) {
  /* Launch Up / Launch Down: off at its angle; Flying the next frame */
  e->ax = up ? e->ay : e->qx;
  gfly_velocity(e, e->ax, GF_SLAM_SPEED);
  e->b0 = up;
  anim_play(&e->anim, CLIP_GFLY_FLY);
  e->st = GF_LAUNCH;
}

static void gfly_choose(Enemy *e) {
  /* Super Choose: still, the bouncer stopped; a charge or a slam (SendRandomEventV2: three charges, two slams in a
   * row at most) */
  e->body.vx = e->body.vy = 0;
  for (;;) {
    bool charge = rand_range(0, 1) < 0.5f;
    if (charge && e->c0 < 3) {
      e->c0++, e->c1 = 0;
      /* Charge Antic: back from the Knight a little, then at it */
      anim_play_from_frame(&e->anim, CLIP_GFLY_CHARGE_ANTIC, 0);
      gfly_face_hero(e);
      float a = atan2f(hero_y() - e->body.y, hero_x() - e->body.x) * 180 / (float)M_PI;
      if (a < 0) a += 360;
      e->ax = a;
      gfly_velocity(e, a + 180, 3);
      e->st = GF_CHARGE_ANTIC, e->t0 = 0;
      return;
    }
    if (!charge && e->c1 < 2) {
      e->c1++, e->c0 = 0;
      /* Slam Antic: facing the Knight, shaking */
      anim_play_from_frame(&e->anim, CLIP_GFLY_CHARGE_ANTIC, 0);
      gfly_face_hero(e);
      e->qy = hero_x() - e->body.x;
      e->t1 = 0, e->tx = rand_range(2.5f, 3);
      e->start_x = e->body.x, e->start_y = e->body.y;
      e->st = GF_SLAM_ANTIC, e->t0 = 0;
      return;
    }
  }
}

static void gfly_start(Enemy *e) {
  e->st = GF_INVINCIBLE, e->flags |= 4;
  e->sx = GF_SCALE;
  anim_play(&e->anim, CLIP(e, R_IDLE));
}

static int gfly_side(const Enemy *e) {
  static const int order[4] = {1, 0, 3, 2};   /* (CheckCollisionSide: up, right, down, left) */
  for (int i = 0; i < 4; i++)
    if (side_hit(e, order[i])) return order[i];
  return -1;
}

static void gfly_fixed(Enemy *e) {
  switch (e->st) {
    case GF_BUZZ:
      gfly_velocity(e, e->wait, GF_BUZZ_SPEED);   /* (Fly 2: SetVelocityAsAngle) */
      break;
    case GF_SLAM_ANTIC:
      e->body.x = e->start_x + rand_range(-0.1f, 0.1f), e->body.y = e->start_y + rand_range(-0.1f, 0.1f);
      break;
    case GF_FLYING:
      gfly_velocity(e, e->ax, GF_SLAM_SPEED);
      break;
    case GF_SLAM_END:
      e->body.vx *= 0.85f, e->body.vy *= 0.85f;   /* (DecelerateV2) */
      break;
  }
  /* (Slam Down's half unit into the floor, pushed out over a few steps: Box2D's position correction) */
  if (e->jy < 0) e->jy = e->jy * 0.8f > -0.005f ? 0 : e->jy * 0.8f;
}

static void gfly_update(Enemy *e) {
  bool done = (e->anim.events & ANIM_DONE) != 0, hit = (e->flags & 8) != 0;
  e->t0 += DT;
  switch (e->st) {
    case GF_INVINCIBLE:
      if (gfly_in_range(e)) e->st = GF_SLEEP, e->flags &= (uint8_t)~4;
      break;
    case GF_SLEEP:
      if (hit) {
        /* Wake: the arena's fight starts */
        cam_shake(SHAKE_AVERAGE);
        arena_start();
        anim_play_from_frame(&e->anim, CLIP_GFLY_WAKE, 0);
        e->body.vy = 2.5f;
        e->st = GF_WAKE, e->t0 = 0;
      } else if (!gfly_in_range(e))
        e->st = GF_INVINCIBLE, e->flags |= 4;
      break;
    case GF_WAKE:
      if (done) {
        /* Fly: still a moment; she hurts now (Hero Damager) */
        anim_play(&e->anim, CLIP_GFLY_FLY);
        e->body.vx = e->body.vy = 0;
        e->hb_on |= 1;
        e->st = GF_FLY, e->t0 = 0;
      }
      break;
    case GF_FLY:
      if (e->t0 >= 1) gfly_buzz(e);
      break;
    case GF_BUZZ: {
      /* (the bouncer: FaceDirection, sprite facing left; off what it bumps) */
      if (e->body.vx != 0) set_scale_x(e, e->body.vx > 0 ? -GF_SCALE : GF_SCALE);
      if (e->body.ncontacts) {
        float a = fmodf(e->wait, 360);
        if (a < 0) a += 360;
        switch (gfly_side(e)) {
          case 1: if (e->b1) gfly_aim(e, 320, 350); else gfly_aim(e, 190, 220); break;
          case 3: if (e->b1) gfly_aim(e, 10, 40); else gfly_aim(e, 140, 170); break;
          case 0: if (a < 180) gfly_aim(e, 140, 170); else gfly_aim(e, 190, 220); break;
          case 2: if (a < 180) gfly_aim(e, 10, 40); else gfly_aim(e, 320, 350); break;
        }
      }
      if (e->t0 >= e->qy) gfly_choose(e);
      break;
    }
    case GF_CHARGE_ANTIC:
      if (e->t0 >= 0.75f) {
        anim_play(&e->anim, CLIP_GFLY_CHARGE);
        gfly_velocity(e, e->ax, 26);
        e->pause0 = e->body.vx, e->pause1 = e->body.vy;
        e->st = GF_CHARGE, e->t0 = 0;
      }
      break;
    case GF_CHARGE: {
      int side = e->body.ncontacts ? gfly_side(e) : -1;
      if (side < 0) break;
      /* Charge Recover: off the wall, half as fast */
      cam_shake(SHAKE_AVERAGE);
      anim_play_from_frame(&e->anim, CLIP_GFLY_CHARGE_RECOVER, 0);
      if (side == 0 || side == 2) e->body.vx = -e->pause0 / 2, e->body.vy = e->pause1 / 2;
      else e->body.vx = e->pause0 / 2, e->body.vy = -e->pause1 / 2;
      e->st = GF_CHARGE_RECOVER, e->t0 = 0;
      break;
    }
    case GF_CHARGE_RECOVER:
      if (e->t0 >= 0.3f) {
        e->body.vx = e->body.vy = 0;   /* (Recover End) */
        e->ty = 0.5f;
        gfly_super_end(e);
      }
      break;
    case GF_SUPER_END:
      if (e->t0 >= e->ty) gfly_buzz(e);
      break;
    case GF_SLAM_ANTIC:
      if (e->t0 >= 0.5f) {
        /* Check Direction: up and down angles towards the Knight */
        if (e->qy <= 0) e->ay = 100, e->qx = 260;
        else e->ay = 80, e->qx = 280;
        gfly_launch(e, true);
      }
      break;
    case GF_LAUNCH:
      e->st = GF_FLYING;
      break;
    case GF_FLYING: {
      e->t1 += DT;
      int side = e->body.ncontacts ? gfly_side(e) : -1;
      if (side == 1 || side == 3) {
        /* Slam Up / Slam Down: stopped against it a moment */
        e->body.vx = e->body.vy = 0;
        anim_play_from_frame(&e->anim, side == 3 ? CLIP_GFLY_SLAM_DOWN : CLIP_GFLY_SLAM_UP, 0);
        if (side == 3) e->jy = -0.5f;
        cam_shake(SHAKE_AVERAGE);
        e->b0 = side == 3;   /* (then up after a slam down, down after a slam up) */
        e->st = GF_SLAM_HIT;
      } else if (side == 0 || side == 2) {
        /* Turn Left / Turn Right, then on the same way up or down */
        set_scale_x(e, side == 0 ? GF_SCALE : -GF_SCALE);
        if (side == 0) e->ay = 100, e->qx = 260;
        else e->ay = 80, e->qx = 280;
        gfly_launch(e, e->b0);
      } else if (e->t1 > e->tx) {
        /* Slam End */
        anim_play_from_frame(&e->anim, CLIP_GFLY_SLAM_END, 0);
        e->ty = 0;
        e->st = GF_SLAM_END, e->t0 = 0;
      }
      break;
    }
    case GF_SLAM_HIT:
      if (done) gfly_launch(e, e->b0);
      break;
    case GF_SLAM_END:
      if (e->t0 >= 0.75f) gfly_super_end(e);
      break;
  }
}

/* her corpse: Init, Steam, Ready, Blow; its burster: Initiate, Geo, In Air, Landed, Stop Emit, Stop, Gurgles, Burst,
 * Spawn */
enum { GC_NONE, GC_INIT, GC_STEAM, GC_READY };
enum { GB_NONE, GB_INITIATE, GB_IN_AIR, GB_LANDED, GB_STOP_EMIT, GB_STOP, GB_GURG1, GB_GURG2, GB_GURG3, GB_BURST, GB_DONE };
static struct {
  uint8_t corpse, burster;
  float ct, bt, flash_t;
  float x, y, sx;      /* (the corpse) */
  float spawn_x, spawn_y;   /* (Fly Spawn's place) */
  Anim canim, banim;
  Body body;           /* (the burster) */
} gm;

static void gfly_die(Enemy *e) {
  memset(&gm, 0, sizeof gm);
  const Ent *v = enemy_rec(e, ET_VARS);
  gm.spawn_x = v ? v->p0 : e->body.x, gm.spawn_y = v ? v->p1 : e->body.y;
  /* (the corpse where she was, her way; no body: it stays there) */
  gm.x = e->body.x, gm.y = e->body.y, gm.sx = e->sx;
  anim_play(&gm.canim, CLIP_GFLY_FLY);   /* (its default clip, till Steam) */
  gm.corpse = GC_INIT, gm.ct = 0;
  arena_set_activated();
  e->mode = EM_OFF;
}

static void gfly_corpse_tick(void) {
  if (gm.corpse) {
    gm.canim.events = 0;
    anim_update(&gm.canim, DT);
    gm.ct += DT, gm.flash_t += DT;
    if (gm.corpse == GC_INIT && gm.ct >= 0.5f) {
      anim_play_from_frame(&gm.canim, CLIP_GFLY_DEATH, 0);
      cam_shake(SHAKE_BIG);
      cam_rumble(RUMBLE_MED);
      gm.corpse = GC_STEAM, gm.ct = 0;
    } else if (gm.corpse == GC_STEAM && gm.ct >= 3)
      gm.corpse = GC_READY, gm.ct = 0;
    else if (gm.corpse == GC_READY && gm.ct >= 1) {
      /* Blow: the burster out, up and its way */
      Body *b = &gm.body;
      memset(b, 0, sizeof *b);
      float k = fabsf(gm.sx);
      b->x = gm.x, b->y = gm.y;
      b->ox = 0.0469f * gm.sx, b->oy = -0.6484f * GF_SCALE, b->hx = 2.4375f / 2 * k, b->hy = 1.7031f / 2 * GF_SCALE;
      b->vx = gm.sx * 10, b->vy = 20;
      b->gravity_scale = 1, b->friction = 0.2f, b->mask = CF_TERRAIN;
      anim_play(&gm.banim, CLIP_GFLY_FALL);
      gm.burster = GB_INITIATE, gm.bt = 0;
      cam_rumble(RUMBLE_OFF);
      gm.corpse = GC_NONE;
    }
  }
  if (gm.burster) {
    Body *b = &gm.body;
    float pvx = b->vx, pvy = b->vy;
    int had = b->ncontacts;
    body_step(b, DT);
    bounce(b, pvx, pvy, had, 0.5f);   /* (ObjectBounce) */
    gm.banim.events = 0;
    anim_update(&gm.banim, DT);
    gm.bt += DT;
    switch (gm.burster) {
      case GB_INITIATE:
        if (gm.bt >= 0.1f) {
          geo_fling_at(0, 50, b->x, b->y, 15, 30, 80, 100, 0.75f);   /* (Geo) */
          gm.burster = GB_IN_AIR;
        }
        break;
      case GB_IN_AIR: {
        bool ground = false;
        for (int c = 0; c < b->ncontacts; c++) ground |= b->cny[c] > 0.5f;
        if (ground) anim_play_from_frame(&gm.banim, CLIP_GFLY_WIGGLE, 0), gm.burster = GB_LANDED, gm.bt = 0;
        break;
      }
      case GB_LANDED:
        if (gm.bt >= 1) gm.burster = GB_STOP_EMIT, gm.bt = 0;
        break;
      case GB_STOP_EMIT:
        if (gm.bt >= 0.5f) anim_play_from_frame(&gm.banim, CLIP_GFLY_STOP, 0), gm.burster = GB_STOP, gm.bt = 0;
        break;
      case GB_STOP:
      case GB_GURG1:
        if (gm.bt >= 2) anim_play_from_frame(&gm.banim, CLIP_GFLY_GURGLE_ONCE, 0), gm.burster++, gm.bt = 0;
        break;
      case GB_GURG2:
        if (gm.bt >= 2) {
          cam_rumble(RUMBLE_SMALL);
          anim_play_from_frame(&gm.banim, CLIP_GFLY_GURGLE_LOOP, 0);
          gm.burster = GB_GURG3, gm.bt = 0;
        }
        break;
      case GB_GURG3:
        if (gm.bt >= 1.9f) anim_play_from_frame(&gm.banim, CLIP_GFLY_BURST, 0), gm.burster = GB_BURST, gm.bt = 0;
        break;
      case GB_BURST:
        if (gm.bt >= 0.16f) {
          /* Spawn: her young where it is (Fly Spawn moved there) */
          cam_rumble(RUMBLE_OFF);
          cam_shake(SHAKE_AVERAGE);
          for (int i = 0; i < MAX_ENEMIES; i++) {
            Enemy *f = &en[i];
            if (f->mode != EM_ALIVE || f->ent == NO_ENT || !(ent_at(f->ent)->s1 & EF_SPAWNED)) continue;
            const Ent *d = ent_at(f->ent);
            f->body.x = b->x + d->x0 - gm.spawn_x, f->body.y = b->y + d->y0 - gm.spawn_y;
          }
          gm.burster = GB_DONE;
        }
        break;
    }
  }
}

static void gfly_corpse_draw(void) {
  Inst in;
  if (gm.corpse) {
    /* (EmitLargeInfectedEffects: the corpse flashes) */
    sprite_inst(gm.canim.sprite, gm.x, gm.y, 0.0085f, gm.sx, GF_SCALE, flash_tint_at(true, gm.flash_t, 10), &in);
    gfx_actor(&in, SORT_KEY(0, 0));
  }
  if (gm.burster) {
    sprite_inst(gm.banim.sprite, gm.body.x, gm.body.y, 0.0085f, gm.sx, GF_SCALE, 0, &in);
    gfx_actor(&in, SORT_KEY(0, 0));
  }
}

/* its FSMs start */
static void enemy_fsm_start(Enemy *e, const Ent *d) {
  if (FSM(e) == EF_CRAWLER) crawler_start(e, d);
  else if (FSM(e) == EF_BUZZER) buzzer_start(e, d);
  else if (FSM(e) == EF_HUSK) husk_start(e);
  else if (FSM(e) == EF_CLIMBER) climber_start(e);
  else if (FSM(e) == EF_BOUNCER) bouncer_start(e);
  else if (FSM(e) == EF_SPITTER) spitter_start(e, d);
  else if (FSM(e) == EF_ROLLER) roller_start(e);
  else if (FSM(e) == EF_BLOCKER) blocker_start(e);
  else if (FSM(e) == EF_LEAPER) leaper_start(e);
  else if (FSM(e) == EF_GUARD) guard_start(e);
  else if (FSM(e) == EF_FK) fk_start(e);
  else if (FSM(e) == EF_FKHEAD) fk_head_start(e);
  else if (FSM(e) == EF_GFLY) gfly_start(e);
}

/* ActiveRegion (a 50 by 35 box round the camera) meets its collider: FSMActivator turns its FSMs on */
static void enemy_dormant_check(Enemy *e) {
  float x0, y0, x1, y1;
  enemy_box(e, &x0, &y0, &x1, &y1);
  if (x1 > g_cam_x - 25 && x0 < g_cam_x + 25 && y1 > g_cam_y - 17.5f && y0 < g_cam_y + 17.5f) {
    e->flags &= (uint8_t)~64;
    e->anim.paused = false;
    enemy_fsm_start(e, ent_at(e->ent));
  }
}

/* ---------------------------------------------------------------- summoners (summon: an enemy brought in from the
 * background as its wave starts) */
#define MAX_SUMMONS 2
enum { SM_IDLE, SM_PAUSE, SM_ENTER, SM_DONE };
typedef struct {
  uint8_t st;
  uint16_t ent;   /* its record */
  float t, tween, sx;
  Anim anim;
} Summon;
static Summon summons[MAX_SUMMONS];

static void summons_enter(void) {
  memset(summons, 0, sizeof summons);
  int n, k = 0;
  const Ent *es = room_ents(&n);
  for (int i = 0; i < n && k < MAX_SUMMONS; i++)
    if (es[i].type == ENT_OBJ && es[i].flags == OK_SUMMON && es[i].p0 >= 0) summons[k].ent = (uint16_t)i, summons[k++].st = SM_IDLE;
    else if (es[i].type == ENT_OBJ && es[i].flags == OK_SUMMON) summons[k++].st = SM_DONE;
  for (; k < MAX_SUMMONS; k++) summons[k].st = SM_DONE;
}

void enemies_summon(void) {
  for (int k = 0; k < MAX_SUMMONS; k++)
    if (summons[k].st == SM_IDLE) summons[k].st = SM_PAUSE, summons[k].t = rand_range(0.25f, 1);   /* (Random Pause) */
}

void enemies_battle_start(void) {
  /* (BATTLE START: no kind of a plain arena's listens yet; the False Knight's arena is its own) */
}

static void summons_tick(void) {
  for (int k = 0; k < MAX_SUMMONS; k++) {
    Summon *s = &summons[k];
    if (s->st == SM_PAUSE && (s->t -= DT) <= 0) {
      /* Enter: from the background, facing the Knight, out of the dark */
      const Ent *e = ent_at(s->ent), *d = ent_at((int)e->p0);
      s->sx = hero_x() > e->x0 ? -fabsf(e->y1) : fabsf(e->y1);
      s->tween = rand_range(0.75f, 1.2f), s->t = 0;
      anim_play(&s->anim, kinds[d->a].clip[R_A1]);
      s->st = SM_ENTER;
    } else if (s->st == SM_ENTER) {
      s->anim.events = 0;
      anim_update(&s->anim, DT);
      if ((s->t += DT) >= s->tween) {
        /* Summon: the enemy there, its way, alerted; the summoner gone */
        const Ent *e = ent_at(s->ent);
        for (int i = 0; i < MAX_ENEMIES; i++) {
          Enemy *q = &en[i];
          if (q->mode != EM_ALIVE || q->ent != (uint16_t)e->p0) continue;
          set_scale_x(q, s->sx);
          q->body.x = e->x0, q->body.y = e->y0;
          if (FSM(q) == EF_SPITTER && q->st == SP_IDLE && !(q->flags & 64)) spitter_distance_fly(q);   /* (ALERT) */
        }
        s->st = SM_DONE;
      }
    }
  }
}

static void summons_draw(void) {
  for (int k = 0; k < MAX_SUMMONS; k++) {
    if (summons[k].st != SM_ENTER || summons[k].anim.sprite < 0) continue;
    const Ent *e = ent_at(summons[k].ent);
    /* (iTweenMoveBy, easeOutSine, from 8 up and 17 back; its colour from black to white over a second) */
    float f = sinf(summons[k].t / summons[k].tween * (float)M_PI / 2), c = summons[k].t < 1 ? summons[k].t : 1;
    uint8_t v = (uint8_t)(c * 255), tint = gfx_dyn_tint(11 + 4 * k, v, v, v, 255);
    Inst in;
    sprite_inst(summons[k].anim.sprite, e->x0, e->y0 + 8 - 8 * f, e->x1 + 16.996f - 16.99f * f, summons[k].sx, 1, tint, &in);
    gfx_actor(&in, SORT_KEY(0, 0));
  }
}

void enemies_enter(void) {
  memset(en, 0, sizeof en);
  memset(geo, 0, sizeof geo);
  fk_arena_enter();
  int n, k = 0;
  const Ent *es = room_ents(&n);
  for (int i = 0; i < n && k < MAX_ENEMIES; i++) {
    const Ent *d = &es[i];
    if (d->type != ENT_OBJ || d->flags != OK_ENEMY) continue;
    if (persist_get(d->persist) || ((d->s1 & EF_ARENA_GONE) && arena_done()) || ((d->s1 & EF_ARENA_LATER) && !arena_done())) {
      enemy_terrain_off(d);
      continue;
    }
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
    e->rc_base = rc ? rc->x0 : 15, e->rc_dur = rc ? rc->y0 : 0.5f, e->rc_flags = rc ? (uint8_t)rc->a : RF_NONE;
    const Ent *wk = enemy_rec(e, ET_WALKER);
    e->wk_rec = wk ? (uint16_t)(wk - room_ents(&n)) : 0;
    if (d->s1 & EF_DORMANT) {
      /* (its FSMs off till the active region round the camera meets it: FSMActivator; its sprite still) */
      int c = CLIP(e, R_IDLE) >= 0 ? CLIP(e, R_IDLE) : CLIP(e, R_A1) >= 0 ? CLIP(e, R_A1) : CLIP(e, R_WALK);
      if (c >= 0) anim_play(&e->anim, c), e->anim.paused = true;
      e->flags |= 64;
    } else
      enemy_fsm_start(e, d);
    for (int h = 0; h < 8; h++) {
      const Ent *r = hitbox_rec(e, h);
      if (!r) break;
      if (r->a) e->hb_on |= (uint8_t)(1 << h);
    }
  }
#ifdef HOST
  if (getenv("ENEMYCOUNT")) {
    int c = 0;
    for (int i = 0; i < n; i++) c += es[i].type == ENT_OBJ && es[i].flags == OK_ENEMY;
    fprintf(stderr, "enemies %d\n", c);
  }
#endif
  memset(&sh, 0, sizeof sh);
  memset(balls, 0, sizeof balls);
  memset(bullets, 0, sizeof bullets);
  memset(waves, 0, sizeof waves);
  memset(spurts, 0, sizeof spurts);
  memset(&gm, 0, sizeof gm);
  summons_enter();
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
    if (e->mode == EM_ALIVE && FSM(e) == EF_FK) {
      fk_fixed(e);
      if (fk.kinematic) e->body.vx = e->body.vy = 0;
      else body_step(&e->body, DT);
      continue;
    }
    if (e->mode == EM_ALIVE && FSM(e) == EF_FKHEAD) continue;
    if (e->mode == EM_ALIVE && FSM(e) == EF_CLIMBER) {
      /* (a kinematic body: its velocity, nothing in its way) */
      recoil_fixed(e);
      e->body.x += e->body.vx * DT, e->body.y += e->body.vy * DT;
      continue;
    }
    if (e->mode == EM_ALIVE) {
      if (e->flags & 64) {}   /* (dormant: its FSMs off) */
      else if (FSM(e) == EF_CRAWLER) crawler_fixed(e);
      else if (FSM(e) == EF_BUZZER) buzzer_fixed(e);
      else if (FSM(e) == EF_HUSK) husk_fixed(e);
      else if (FSM(e) == EF_BOUNCER) bouncer_fixed(e);
      else if (FSM(e) == EF_SPITTER) spitter_fixed(e);
      else if (FSM(e) == EF_ROLLER) roller_fixed(e);
      else if (FSM(e) == EF_LEAPER) leaper_fixed(e);
      else if (FSM(e) == EF_GUARD) guard_fixed(e);
      else if (FSM(e) == EF_GFLY) gfly_fixed(e);
      recoil_fixed(e);
      body_step(&e->body, DT);
    } else if (FSM(e) != EF_BLOCKER) {
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
  waves_tick();
  fk_arena_tick();
  fk_head_place();
  gfly_corpse_tick();
  summons_tick();
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
      frame_collider(e);
      if (e->evasion > 0) e->evasion -= DT;
      if (e->ar_r > 0) sight_update(e);
      if (e->flags & 64) enemy_dormant_check(e);
      if (e->flags & 64) {}   /* (dormant) */
      else if (FSM(e) == EF_BUZZER) buzzer_update(e, ent_at(e->ent));
      else if (FSM(e) == EF_SHADE) shade_update(e);
      else if (FSM(e) == EF_HUSK) husk_update(e);
      else if (FSM(e) == EF_CLIMBER) climber_update(e);
      else if (FSM(e) == EF_BOUNCER) bouncer_update(e);
      else if (FSM(e) == EF_SPITTER) spitter_update(e);
      else if (FSM(e) == EF_ROLLER) roller_update(e);
      else if (FSM(e) == EF_BLOCKER) blocker_update(e);
      else if (FSM(e) == EF_LEAPER) leaper_update(e);
      else if (FSM(e) == EF_GUARD) guard_update(e);
      else if (FSM(e) == EF_FK) fk_update(e);
      else if (FSM(e) == EF_GFLY) gfly_update(e);
      if (FSM(e) == EF_FK) {
        /* (its Hitter: on till its FSM turns it off) */
        e->sub.events = 0;
        if (e->sub_hb) anim_update(&e->sub, DT);
      } else
        hitbox_tick(e);
      e->flags &= (uint8_t)~8;
    } else if (FSM(e) == EF_SHADE)
      shade_update(e);
    else if (FSM(e) == EF_BLOCKER)
      blocker_corpse(e);
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
    if ((e->flags & 32) || (FSM(e) == EF_FKHEAD && !fk.head_shown && e->mode == EM_ALIVE)) continue;
    float sy = fabsf(e->sx) > 0 ? fabsf(e->sx) : 1;
    if (e->ang != 0 && e->mode == EM_ALIVE)
      sprite_inst_rot(e->anim.sprite, e->body.x, e->body.y, z, e->sx, 1, e->ang, flash_tint(e, 2 + i % 5), &in);
    else
      sprite_inst(e->anim.sprite, e->body.x + e->jx, e->body.y + e->jy, z, e->sx, sy, flash_tint(e, 2 + i % 5), &in);
    gfx_actor(&in, SORT_KEY(0, 0));
    if (e->sub_hb && e->sub.sprite >= 0 && e->mode == EM_ALIVE) {
      sprite_inst(e->sub.sprite, e->body.x, e->body.y, z - 0.001f, e->sx, sy, flash_tint(e, 2 + i % 5), &in);
      gfx_actor(&in, SORT_KEY(0, 0));
    }
    if (FSM(e) == EF_SHADE && sh.slash_on) {
      sprite_inst(sh.slash.sprite, e->body.x - 0.0486f * e->sx, e->body.y - 0.0117f, z - 0.001f, 1.27f * e->sx, 1, 0, &in);
      gfx_actor(&in, SORT_KEY(0, 0));
    }
  }
  bullets_draw();
  spurts_draw();
  fk_arena_draw();
  gfly_corpse_draw();
  summons_draw();
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
    sprite_inst(g->anim.sprite, g->x, g->y, 0.0015f, GEO_SCALE, GEO_SCALE, 0, &in);
    gfx_actor(&in, SORT_KEY(0, 0));
  }
}

/* ---------------------------------------------------------------- what touches them */
void enemies_swing_start(void) { swing_bits = 0; }

/* the slash's shape: enemies it touches are hit, once a swing -> HB_* */
int enemies_nail(const float *pts, int npts, float direction, int damage) {
  int out = 0;
  barrels_nail(pts, npts, direction);
  for (int i = 0; i < MAX_ENEMIES; i++) {
    Enemy *e = &en[i];
    if (e->mode != EM_ALIVE || (e->flags & 17)) continue;
    float x0, y0, x1, y1;
    enemy_box(e, &x0, &y0, &x1, &y1);
    if (!box_meets_shape(x0, y0, x1, y1, pts, npts)) continue;
    out |= HB_BOUNCE | HB_RECOIL;
    if (swing_bits >> i & 1) continue;
    swing_bits |= 1u << i;
    enemy_hit(e, direction, damage);
  }
  return out;
}

/* DamageHero: the first enemy touching the HeroBox -> its damage (0: none), which side it is on */
int enemies_touch_hero(float x0, float y0, float x1, float y1, int *side) {
  for (int i = 0; i < MAX_ENEMIES; i++) {
    const Enemy *e = &en[i];
    if (e->mode != EM_ALIVE || e->damage <= 0 || (e->flags & 17)) continue;
    float a0, b0, a1, b1;
    enemy_box(e, &a0, &b0, &a1, &b1);
    if (x1 > a0 && x0 < a1 && y1 > b0 && y0 < b1) {
      *side = e->body.x > g_hero.body.x ? SIDE_RIGHT : SIDE_LEFT;
      return e->damage;
    }
    int hd = e->hb_on ? hitbox_touch(e, x0, y0, x1, y1) : 0;
    if (hd) {
      *side = e->body.x > g_hero.body.x ? SIDE_RIGHT : SIDE_LEFT;
      return hd;
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
  dmg = spurts_touch(x0, y0, x1, y1, side);
  if (dmg) return dmg;
  dmg = barrels_touch(x0, y0, x1, y1, side);
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

/* (tests: a hit on every enemy near the Knight, invincible or not) */
void enemies_debug_hit(int damage) {
  for (int i = 0; i < MAX_ENEMIES; i++) {
    Enemy *e = &en[i];
    float dx = e->body.x - g_hero.body.x, dy = e->body.y - g_hero.body.y;
    float r = getenv("HKHITR") ? (float)atof(getenv("HKHITR")) : 15;
    if (e->mode != EM_ALIVE || (e->flags & 5) || dx * dx + dy * dy > r * r) continue;
    if (FSM(e) == EF_FK && e->st == FK_DORMANT) continue;
    enemy_hit(e, dx > 0 ? 0 : 180, damage);
  }
}
#endif
