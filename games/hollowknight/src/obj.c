/* The room's objects that the Knight acts on (tools/ents.py: ENT_OBJ and the records after each): what the nail hits,
 * breakables and the debris they fling. */
#include <math.h>
#include "game.h"
#ifdef HOST
#include <stdio.h>
#include <stdlib.h>
#endif

#define DT 0.02f
#define MAX_OBJS 64
#define MAX_PIECES 24

typedef struct {
  uint8_t kind, state;
  uint16_t ent;   /* its record */
  int8_t hits;
  bool shown;
  int16_t sprite;
  float t, t2, jx, jy;   /* (timers; a jitter) */
  Anim anim;
} Obj;
static Obj objs[MAX_OBJS];
static int nobjs;
static uint32_t swing_hit[MAX_OBJS / 32];   /* (LimitSendEvents: an object is hit once a swing) */

enum { BR_WHOLE, BR_BROKEN };

/* ---------------------------------------------------------------- Unity's Random (an xorshift here) */
static uint32_t rng = 0x2545F491u;
float rand_range(float lo, float hi) {
  rng ^= rng << 13, rng ^= rng >> 17, rng ^= rng << 5;
  return lo + (hi - lo) * (float)(rng >> 8) * (1.0f / 16777216.0f);
}

static const Ent *ent_at(int i) {
  int n;
  return &room_ents(&n)[i];
}

/* ---------------------------------------------------------------- debris (Rigidbody2D, ObjectBounce, SpinSelf) */
typedef struct {
  int16_t sprite;
  uint8_t layer, steps;
  uint16_t order;
  bool on, resting, spin;
  float x, y, z, vx, vy, ang, w;   /* (w: degrees a second) */
  float gravity, bounce, spin_factor, mirror, speed;
} Piece;
static Piece pieces[MAX_PIECES];

static void piece_fling(const Ent *p, float ox, float oy, float angle_offset, float amin, float amax, float smin, float smax,
                        float mult) {
  Piece *q = NULL;
  for (int i = 0; i < MAX_PIECES && !q; i++)
    if (!pieces[i].on) q = &pieces[i];
  if (!q) return;   /* (all in use: this one is not seen) */
  q->on = true, q->resting = false, q->steps = 0;
  q->sprite = (int16_t)p->s0, q->layer = p->group, q->order = p->a;
  q->x = ox + p->x0, q->y = oy + p->y0, q->z = p->y1;
  q->spin = p->flags & 1;
  /* (Break turns it by the angle offset; SpinSelf.Start, after, at random) */
  q->ang = q->spin ? rand_range(0, 360) : p->x1 + angle_offset;
  q->gravity = p->p0, q->bounce = p->p1, q->spin_factor = p->p2, q->mirror = p->p3;
  float a = rand_range(amin, amax) * (float)M_PI / 180, s = rand_range(smin, smax) * mult;
  q->vx = cosf(a) * s, q->vy = sinf(a) * s;
  q->w = 0, q->speed = s;
}

static void pieces_tick(void) {
  for (int i = 0; i < MAX_PIECES; i++) {
    Piece *q = &pieces[i];
    if (!q->on || q->resting) continue;
    /* SpinSelf: one push of torque on its second step (a polygon of about a unit: this many degrees a second) */
    if (q->spin && q->steps == 1) q->w = q->vx * q->spin_factor * 60;
    if (q->steps < 255) q->steps++;
    q->vy -= 60 * q->gravity * DT;
    q->w *= 1 - 0.05f * DT;
    float dx = q->vx * DT, dy = q->vy * DT, len = sqrtf(dx * dx + dy * dy);
    PhysHit hit;
    if (len > 0 && phys_ray(q->x, q->y, dx / len, dy / len, len + 0.05f, CF_TERRAIN, &hit)) {
      q->x = hit.x + hit.nx * 0.05f, q->y = hit.y + hit.ny * 0.05f;
      float speed = sqrtf(q->vx * q->vx + q->vy * q->vy);
      if (q->bounce >= 0 && speed > 1) {
        /* ObjectBounce: off the normal, at its speed times the bounce factor */
        float d = q->vx * hit.nx + q->vy * hit.ny;
        float rx = q->vx - 2 * d * hit.nx, ry = q->vy - 2 * d * hit.ny, k = speed * q->bounce * rand_range(0.8f, 1.2f) / speed;
        q->vx = rx * k, q->vy = ry * k;
        q->w *= 0.5f;
        if (hit.ny > 0.5f && fabsf(q->vy) < 1.5f) q->resting = true;
      } else if (hit.ny > 0.5f)
        q->resting = true;
      else
        q->vx = 0;
    } else
      q->x += dx, q->y += dy;
    q->ang += q->w * DT;
    if (q->y < -20) q->on = false;
  }
}

static void pieces_draw(void) {
  for (int i = 0; i < MAX_PIECES; i++) {
    const Piece *q = &pieces[i];
    if (!q->on) continue;
    Inst in;
    sprite_inst_rot(q->sprite, q->x, q->y, q->z, q->mirror, 1, q->ang, 0, &in);
    gfx_actor(&in, SORT_KEY(q->layer, (int)q->order - 32768));
  }
}

/* ---------------------------------------------------------------- Breakable */
/* SetStaticPartsActivation (broken) */
static void breakable_set_broken(const Obj *o, const Ent *e) {
  group_fade(e->group, 0, 0);              /* (the whole renderer and parts off) */
  if (e->group2) group_fade(e->group2, 1, 0);   /* (the remnants on) */
  for (int c = e->a; c < e->a + e->s0; c++) phys_collider_enable(c, false);
  if (e->y1 > 0) world_send_hit((int)e->y1 - 1);   /* (its hitEventReciever) */
  (void)o;
}

static void breakable_enter(Obj *o, const Ent *e) {
  if (e->group2) group_fade(e->group2, 0, 0);   /* (Start: the remnants off) */
  if (persist_get(e->persist)) {
    o->state = BR_BROKEN;
    breakable_set_broken(o, e);
  }
}

/* Breakable.Hit: which way the debris flies, then Break */
static void breakable_hit(Obj *o, const Ent *e, float direction) {
  if (o->state == BR_BROKEN) return;
  int dir = cardinal(direction);
  float angle_offset = e->p2, amin, amax, mult = 1;
  switch (dir) {
    case 2: angle_offset = -angle_offset, amin = 120, amax = 160; break;
    case 0: amin = 30, amax = 70; break;
    case 1: angle_offset = 0, amin = 70, amax = 110, mult = 1.5f; break;
    default: angle_offset = 0, amin = 160, amax = 380; break;
  }
  o->state = BR_BROKEN;
  breakable_set_broken(o, e);
  persist_set(e->persist);
  const Ent *p = e + 1 + e->s1;
  for (int i = 0; i < (int)e->p3; i++, p++)
    if (p->type == ENT_PIECE) piece_fling(p, e->x0, e->y0, angle_offset, amin, amax, e->p0, e->p1, mult);
  cam_shake(SHAKE_ENEMY_KILL);
}

/* ---------------------------------------------------------------- the Great Door (King's Pass): 13 blows open it */
enum { GD_IDLE, GD_WAIT, GD_BREAK, GD_OPEN };

static void great_door_enter(Obj *o, const Ent *e) {
  o->sprite = (int16_t)e->p0, o->shown = true;
  if (persist_get(e->persist)) {
    /* Activate: its collider and sprite off */
    o->state = GD_OPEN, o->shown = false;
    for (int c = e->a; c < e->a + e->group; c++) phys_collider_enable(c, false);
  }
}

static void great_door_hit(Obj *o, const Ent *e) {
  if (o->state != GD_IDLE) return;
  cam_shake(SHAKE_ENEMY_KILL);
  /* Check Hits */
  o->hits++;
  if (o->hits == 4 || o->hits == 8) {
    o->sprite = (int16_t)(o->hits == 4 ? e->p1 : e->p2);
    cam_shake(SHAKE_AVERAGE);
  }
  if (o->hits == 13) {
    /* Break: the screen black at once, then (a frame later) the Knight, without control, to Dirtmouth */
    o->state = GD_BREAK, o->t = DT;
    persist_set(e->persist);
    game_fade(1, 0, 0);
    return;
  }
  o->state = GD_WAIT, o->t = 0.15f;
}

static void great_door_tick(Obj *o, const Ent *e) {
  if (o->state == GD_WAIT && (o->t -= DT) <= 0) o->state = GD_IDLE;
  else if (o->state == GD_BREAK && (o->t -= DT) <= 0) {
    o->state = GD_OPEN;
    game_transition((int)e->p3, e->s0, GATE_UNKNOWN, 2.5f, false);   /* (its EnterWithoutInput goes to an object without it) */
  }
}

/* ---------------------------------------------------------------- geo rocks (the Geo Rock FSM; GeoRock keeps its hits) */
enum { GR_IDLE, GR_HIT, GR_RETURN, GR_DESTROY, GR_BROKEN };

static int georock_clip(const Ent *e, int which) {   /* 0 gleam, 1 broken */
  static const int clips[2][2] = {{CLIP_GEOROCK_GLEAM_1, CLIP_GEOROCK_BROKEN_1}, {CLIP_GEOROCK_GLEAM_2, CLIP_GEOROCK_BROKEN_2}};
  return clips[e->p3 == 2][which];
}

static void georock_broken(Obj *o, const Ent *e) {
  anim_play(&o->anim, georock_clip(e, 1));
  o->state = GR_BROKEN;
  o->jx = o->jy = 0;
}

static void georock_save(const Obj *o, const Ent *e) {
  /* (4 bits: the hits left, plus one; none: as the room has it) */
  for (int b = 0; b < 4; b++)
    if ((o->hits + 1) >> b & 1) persist_set(e->persist + b);
    else persist_clear(e->persist + b);
}

static void georock_enter(Obj *o, const Ent *e) {
  int saved = 0;
  for (int b = 0; b < 4; b++) saved |= persist_get(e->persist + b) << b;
  o->hits = (int8_t)(saved ? saved - 1 : (int)e->p0);
  anim_play(&o->anim, e->p3 == 2 ? CLIP_GEOROCK_IDLE_2 : CLIP_GEOROCK_IDLE_1);
  if (o->hits < 1) georock_broken(o, e);
  else o->state = GR_IDLE, o->t = rand_range(1.5f, 4);
}

static void georock_hit(Obj *o, const Ent *e, float direction) {
  if (o->state != GR_IDLE) return;
  geo_fling_at(0, (int)e->p1, e->x0, e->y0, 23, 30, 80, 100, 0.25f);
  o->hits--;
  georock_save(o, e);
  if (o->hits <= 0) {
    o->state = GR_DESTROY, o->t = DT;   /* (Pause Frame) */
    return;
  }
  o->state = GR_HIT, o->t = 0.1f;
  (void)direction;
}

static void georock_tick(Obj *o, const Ent *e) {
  switch (o->state) {
    case GR_IDLE:
      if ((o->t -= DT) <= 0) {
        /* Gleam, then Idle again */
        anim_play_from_frame(&o->anim, georock_clip(e, 0), 0);
        o->t = rand_range(1.5f, 4);
      }
      break;
    case GR_HIT:
    case GR_RETURN:
      /* ObjectJitter, as it recoils and comes back */
      o->jx = rand_range(-0.05f, 0.05f), o->jy = rand_range(-0.05f, 0.05f);
      if ((o->t -= DT) <= 0) {
        if (o->state == GR_HIT) o->state = GR_RETURN, o->t = 0.1f;
        else o->state = GR_IDLE, o->t = rand_range(1.5f, 4), o->jx = o->jy = 0;
      }
      break;
    case GR_DESTROY:
      if ((o->t -= DT) <= 0) {
        cam_shake(SHAKE_ENEMY_KILL);
        geo_fling_at(0, (int)e->p2, e->x0, e->y0, 23, 30, 80, 100, 0.25f);
        georock_broken(o, e);
      }
      break;
  }
}

/* ---------------------------------------------------------------- chests (Chest Control) */
enum { CH_IDLE, CH_OPEN, CH_SPAWNED, CH_OPENED };

static void chest_opened(Obj *o, const Ent *e) {
  o->state = CH_OPENED;
  for (int c = e->a; c < e->a + e->group; c++) phys_collider_enable(c, false);
}

static void chest_enter(Obj *o, const Ent *e) {
  o->sprite = (int16_t)e->s0;
  anim_play(&o->anim, CLIP_CHEST_IDLE);
  o->state = CH_IDLE;
  if (persist_get(e->persist)) chest_opened(o, e);
}

static void chest_hit(Obj *o, const Ent *e) {
  if (o->state != CH_IDLE) return;
  /* Open */
  FREEZE_MOMENT_1();
  cam_shake(SHAKE_ENEMY_KILL);
  persist_set(e->persist);
  anim_play(&o->anim, CLIP_CHEST_OPEN);
  o->state = CH_OPEN;
}

static void chest_tick(Obj *o, const Ent *e) {
  if (o->state == CH_OPEN && (o->anim.events & ANIM_TRIGGER)) {
    /* Spawn Items: the geo, the item */
    geo_fling_at(0, (int)e->p0, e->x0, e->y0, 25, 38, 78, 102, 1);
    geo_fling_at(1, (int)e->p1, e->x0, e->y0, 25, 38, 78, 102, 1);
    geo_fling_at(2, (int)e->p2, e->x0, e->y0, 25, 38, 78, 102, 1);
    o->state = CH_SPAWNED;
  } else if (o->state == CH_SPAWNED && (o->anim.events & ANIM_DONE))
    chest_opened(o, e);
}

static void chest_draw(const Obj *o, const Ent *e) {
  Inst in;
  if (o->state != CH_OPENED) {
    sprite_inst(o->anim.sprite, e->x0, e->y0, e->x1, 1, 1, 0, &in);
    gfx_actor(&in, SORT_KEY(0, 0));
    return;
  }
  const Ent *p = e + 1 + e->s1;
  for (int i = 0; i < (int)e->p3; i++, p++) {
    sprite_inst(p->s0, e->x0 + p->x0, e->y0 + p->y0, p->y1, 1, 1, 0, &in);
    gfx_actor(&in, SORT_KEY(0, 0));
  }
}

/* ---------------------------------------------------------------- battle gates (BG Control; or Control: closed till a
 * PlayerData bool is set) */
enum { BGF_START_CLOSED = 1, BGF_BONE = 2, BGF_PD = 4 };
enum { GT_OPENED, GT_CLOSE1, GT_CLOSE2, GT_OPEN, GT_QUICK_CLOSE, GT_DOUBLE_CLOSE, GT_GONE };
enum { GC_OPENED, GC_CLOSE1, GC_CLOSE2, GC_OPEN, GC_CLOSED };

static int gate_clip(const Ent *e, int which) {
  static const int16_t clips[2][5] = {
      {CLIP_BGATE_BG_OPENED, CLIP_BGATE_BG_CLOSE_1, CLIP_BGATE_BG_CLOSE_2, CLIP_BGATE_BG_OPEN, CLIP_BGATE_BG_CLOSED},
      {CLIP_BGATE_BONE_GATE_OPENED, CLIP_BGATE_BONE_GATE_CLOSE, CLIP_BGATE_BONE_GATE_CLOSED, CLIP_BGATE_BONE_GATE_OPEN,
       CLIP_BGATE_BONE_GATE_CLOSED}};
  return clips[(e->s0 & BGF_BONE) != 0][which];
}

static void gate_collider(const Ent *e, bool on) {
  for (int c = e->a; c < e->a + e->group; c++) phys_collider_enable(c, on);
}

static void gate_set(Obj *o, const Ent *e, int state, int clip, bool solid) {
  anim_play_from_frame(&o->anim, gate_clip(e, clip), 0);
  gate_collider(e, solid);
  o->state = (uint8_t)state;
}

static void gate_gone(Obj *o, const Ent *e) {
  gate_collider(e, false);
  o->state = GT_GONE;
}

static void gate_enter(Obj *o, const Ent *e) {
  if (e->s0 & BGF_PD) {
    gate_set(o, e, GT_DOUBLE_CLOSE, GC_CLOSED, true);
    if (pd_flag((int)e->p1)) gate_gone(o, e);   /* (Check) */
  } else if (e->s0 & BGF_START_CLOSED) {
    gate_set(o, e, GT_QUICK_CLOSE, GC_CLOSED, true), o->t = 0.2f;
  } else
    gate_set(o, e, GT_OPENED, GC_OPENED, false);
}

static void gate_event(Obj *o, const Ent *e, int ev) {
  if (o->state == GT_GONE) return;
  if (e->s0 & BGF_PD) {
    if (ev == BG_OPEN || ev == BG_QUICK_OPEN) gate_gone(o, e);
    return;
  }
  if (ev == BG_DESTROY) gate_gone(o, e);
  else if (ev == BG_QUICK_OPEN) gate_set(o, e, GT_OPENED, GC_OPENED, false);   /* (Quick Open: as Opened) */
  else if (o->state == GT_OPENED || o->state == GT_OPEN) {
    if (ev == BG_CLOSE) gate_set(o, e, GT_CLOSE1, GC_CLOSE1, true);
    else if (ev == BG_QUICK_CLOSE) gate_set(o, e, GT_QUICK_CLOSE, GC_CLOSED, true), o->t = 0.2f;
  } else if (ev == BG_OPEN && (o->state == GT_CLOSE2 || o->state == GT_QUICK_CLOSE || o->state == GT_DOUBLE_CLOSE))
    gate_set(o, e, GT_OPEN, GC_OPEN, false);
}

static void gate_tick(Obj *o, const Ent *e) {
  if (o->state == GT_CLOSE1 && (o->anim.events & ANIM_DONE)) {
    /* Close 2: slammed shut */
    anim_play_from_frame(&o->anim, gate_clip(e, GC_CLOSE2), 0);
    cam_shake(SHAKE_ENEMY_KILL);
    o->state = GT_CLOSE2;
  } else if (o->state == GT_QUICK_CLOSE && (o->t -= DT) <= 0) {
    anim_play_from_frame(&o->anim, gate_clip(e, GC_CLOSED), 0);   /* (Double Close) */
    o->state = GT_DOUBLE_CLOSE;
  }
}

void gates_event(int ev) {
#ifdef HOST
  if (getenv("HKOBJ")) fprintf(stderr, "gates event %d\n", ev);
#endif
  for (int k = 0; k < nobjs; k++)
    if (objs[k].kind == OK_BGATE) gate_event(&objs[k], ent_at(objs[k].ent), ev);
}

/* ---------------------------------------------------------------- arenas (Battle Control; the False Knight's is enemy.c's) */
enum { ARF_TRIGGER = 1, ARF_DESTROY_GATES = 2, ARF_QUICK_OPEN = 4, ARF_WAVES = 8, ARF_NO_START = 16 };
enum { AR_DETECT, AR_FIGHT, AR_WAVE_PAUSE, AR_WAVE2, AR_END_WAIT, AR_DONE };
static struct {
  int16_t ent;   /* (-1: none) */
  uint8_t st;
  int16_t count;   /* Battle Enemies */
  float t;
} arena;

static const Ent *arena_rec(void) {
  int n;
  const Ent *es = room_ents(&n);
  for (int i = 0; i < n; i++)
    if (es[i].type == ENT_OBJ && es[i].flags == OK_ARENA) return &es[i];
  return NULL;
}

bool arena_done(void) {
  const Ent *e = arena_rec();
  return e && persist_get(e->persist);
}

static void arena_enter(void) {
  const Ent *e = arena_rec();
  int n;
  arena.ent = e ? (int16_t)(e - room_ents(&n)) : -1;
  if (!e) return;
  arena.count = (int16_t)e->p2;
  /* Init: its camera lock off; Activate if its fight is over */
  if (e->p0 >= 0) ent_set_enabled((int)e->p0, false);
  if (persist_get(e->persist)) {
    arena.st = AR_DONE;
    if (e->a & ARF_DESTROY_GATES) gates_event(BG_DESTROY);
    else if (e->a & ARF_QUICK_OPEN) gates_event(BG_QUICK_OPEN);
  } else
    arena.st = (e->a & ARF_NO_START) ? AR_FIGHT : AR_DETECT;   /* (no start: counting at once) */
}

void arena_start(void) {
  if (arena.ent < 0 || arena.st != AR_DETECT) return;
  const Ent *e = ent_at(arena.ent);
  /* Start (Wave 1): BATTLE START, the gates shut, its camera lock on */
  arena.count = (int16_t)e->p2;
  enemies_battle_start();
  gates_event(BG_CLOSE);
  if (e->p0 >= 0) ent_set_enabled((int)e->p0, true);
  arena.st = AR_FIGHT;
}

void arena_enemy_died(void) {
  if (arena.ent >= 0) arena.count--;
}

void arena_set_activated(void) {
  if (arena.ent >= 0) persist_set(ent_at(arena.ent)->persist);
}

static void arena_tick(void) {
  if (arena.ent < 0) return;
  const Ent *e = ent_at(arena.ent);
  switch (arena.st) {
    case AR_DETECT:
      if (e->a & ARF_TRIGGER) {
        const Body *k = &g_hero.body;
        float x0 = k->x + k->ox - k->hx, x1 = k->x + k->ox + k->hx, y0 = k->y + k->oy - k->hy, y1 = k->y + k->oy + k->hy;
        if (!g_hero.hidden && x1 > e->x0 && x0 < e->x1 && y1 > e->y0 && y0 < e->y1) arena_start();
      }
      break;
    case AR_FIGHT:
      if ((e->a & ARF_WAVES) && arena.count <= (int)e->s1) arena.st = AR_WAVE_PAUSE, arena.t = 0;
      else if (!(e->a & ARF_WAVES) && arena.count <= 0) {
        if (e->a & ARF_NO_START) persist_set(e->persist);   /* (Complete: Activated, the gates open after a while) */
        arena.st = AR_END_WAIT, arena.t = 0;
      }
      break;
    case AR_WAVE_PAUSE:
      if ((arena.t += DT) >= e->s0 * 0.01f) enemies_summon(), arena.st = AR_WAVE2;   /* (Wave 2: SUMMON) */
      break;
    case AR_WAVE2:
      if (arena.count <= 0) arena.st = AR_END_WAIT, arena.t = 0;
      break;
    case AR_END_WAIT:
      if ((arena.t += DT) >= e->p3) {
        /* End: Activated, the gates open, its camera lock off */
        persist_set(e->persist);
        gates_event(BG_OPEN);
        if (e->p0 >= 0) ent_set_enabled((int)e->p0, false);
        arena.st = AR_DONE;
      }
      break;
  }
}

/* ---------------------------------------------------------------- the room */
void obj_enter(void) {
  nobjs = 0;
  phys_colliders_reset();
  memset(pieces, 0, sizeof pieces);
  memset(swing_hit, 0, sizeof swing_hit);
  int n;
  const Ent *es = room_ents(&n);
  for (int i = 0; i < n && nobjs < MAX_OBJS; i++) {
    if (es[i].type != ENT_OBJ) continue;
    if (es[i].flags == OK_ENEMY || es[i].flags == OK_BENCH || es[i].flags == OK_ARENA || es[i].flags == OK_EVENT ||
        es[i].flags == OK_SUMMON)
      continue;   /* (enemy.c's, npc.c's; below) */
    Obj *o = &objs[nobjs++];
    memset(o, 0, sizeof *o);
    o->kind = es[i].flags, o->ent = (uint16_t)i;
    if (o->kind == OK_BREAKABLE) breakable_enter(o, &es[i]);
    else if (o->kind == OK_GREAT_DOOR) great_door_enter(o, &es[i]);
    else if (o->kind == OK_GEO_ROCK) georock_enter(o, &es[i]);
    else if (o->kind == OK_CHEST) chest_enter(o, &es[i]);
    else if (o->kind == OK_BGATE) gate_enter(o, &es[i]);
  }
  /* (then what sends the gates their events: SendPlaymakerEventOnEnable, the arenas) */
  for (int i = 0; i < n; i++)
    if (es[i].type == ENT_OBJ && es[i].flags == OK_EVENT) gates_event(es[i].a);
  enemies_enter();
  benches_enter();
  arena_enter();
#ifdef HOST
  if (getenv("HKOBJ"))
    for (int k = 0; k < nobjs; k++) {
      const Ent *e = ent_at(objs[k].ent);
      fprintf(stderr, "obj %d kind %d state %d at %.2f,%.2f sprite %d\n", k, objs[k].kind, objs[k].state, e->x0, e->y0, objs[k].anim.sprite);
    }
#endif
}

void obj_tick(void) {
  for (int k = 0; k < nobjs; k++) {
    Obj *o = &objs[k];
    const Ent *e = ent_at(o->ent);
    o->anim.events = 0;
    anim_update(&o->anim, DT);
    if (o->kind == OK_GREAT_DOOR) great_door_tick(o, e);
    else if (o->kind == OK_GEO_ROCK) georock_tick(o, e);
    else if (o->kind == OK_CHEST) chest_tick(o, e);
    else if (o->kind == OK_BGATE) gate_tick(o, e);
  }
  arena_tick();
  pieces_tick();
  enemies_update();
}

void obj_draw(void) {
  for (int k = 0; k < nobjs; k++) {
    const Obj *o = &objs[k];
    const Ent *e = ent_at(o->ent);
    Inst in;
    if (o->kind == OK_GREAT_DOOR && o->shown && o->state != GD_OPEN) {
      sprite_inst(o->sprite, e->x0, e->y0, e->x1, 1, 1, 0, &in);
      gfx_actor(&in, SORT_KEY(0, 0));
    } else if (o->kind == OK_GEO_ROCK) {
      sprite_inst_rot(o->anim.sprite, e->x0 + o->jx, e->y0 + o->jy, e->x1, 1, 1, e->y1, 0, &in);
      gfx_actor(&in, SORT_KEY(0, 0));
    } else if (o->kind == OK_CHEST)
      chest_draw(o, e);
    else if (o->kind == OK_BGATE && o->state != GT_GONE) {
      sprite_inst(o->anim.sprite, e->x0, e->y0, e->x1, e->y1, e->p0, 0, &in);
      gfx_actor(&in, SORT_KEY(0, 0));
    }
  }
  pieces_draw();
  enemies_draw();
}

/* ---------------------------------------------------------------- the nail */
void obj_swing_start(void) {
  memset(swing_hit, 0, sizeof swing_hit);
  enemies_swing_start();
}

/* the slash's shape this step (world, convex): what it touches is hit (once a swing), the direction (degrees) the
 * blow goes; -> what the Knight does (HB_*: bounce, recoil) */
int obj_nail(const float *pts, int npts, float direction) {
  int out = enemies_nail(pts, npts, direction, g_pd.nail_damage);
  for (int k = 0; k < nobjs; k++) {
    Obj *o = &objs[k];
    const Ent *e = ent_at(o->ent);
    bool touched = false;
    for (int j = 0; j < e->s1; j++) {
      const Ent *b = e + 1 + j;
      if (b->type != ENT_BOX || !box_meets_shape(b->x0, b->y0, b->x1, b->y1, pts, npts)) continue;
      touched = true;
      out |= b->flags;
    }
    if (!touched || (swing_hit[k >> 5] >> (k & 31) & 1)) continue;
    swing_hit[k >> 5] |= 1u << (k & 31);
    if (o->kind == OK_BREAKABLE) breakable_hit(o, e, direction);
    else if (o->kind == OK_GREAT_DOOR) great_door_hit(o, e);
    else if (o->kind == OK_GEO_ROCK) georock_hit(o, e, direction);
    else if (o->kind == OK_CHEST) chest_hit(o, e);
  }
  return out;
}

/* DirectionUtils.GetCardinalDirection: 0 right, 1 up, 2 left, 3 down */
int cardinal(float degrees) {
  int d = (int)lrintf(degrees / 90.0f) % 4;
  return d < 0 ? d + 4 : d;
}
