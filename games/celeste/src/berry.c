#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("Os")   /* not drawn every frame: smaller over faster */
#endif
/* Strawberries: Strawberry, StrawberrySeed, StrawberryPoints and the seeds'
 * cutscene (CSGEN_StrawberrySeeds), and what makes things follow the player
 * (Leader, Follower). Line by line from the game's code. */
#include "entities.h"

/* ---------------------------------------------------------------- Leader and Follower */
/* The game keeps 350 past points; followers only use every 5th, one each */
#define MAX_FOLLOWERS 14
#define LEADER_POINTS (5 * MAX_FOLLOWERS + 1)
typedef struct {
  Ent *e;
  uint32_t id;           /* ParentEntityID */
  float delay;           /* DelayTimer */
  uint8_t move, persistent;
} Fol;
static V2 past[LEADER_POINTS];
static int npast;
static Fol fol[MAX_FOLLOWERS];
static int nfol;

static int find(const Ent *e) {
  for (int i = 0; i < nfol; i++)
    if (fol[i].e == e) return i;
  return -1;
}

/* Leader.Update: the player's component, at Position (0, -8) */
void leader_update(V2 at) {
  if (level_on_interval(0.02f) && (npast == 0 || v2len(v2sub(at, past[0])) >= 3)) {
    memmove(past + 1, past, sizeof(V2) * (LEADER_POINTS - 1));
    past[0] = at;
    if (npast < LEADER_POINTS) npast++;
  }
  int k = 5;
  for (int i = 0; i < nfol; i++) {
    if (k >= npast) break;
    Fol *f = &fol[i];
    if (f->delay <= 0 && f->move) {
      float m = 1 - powf(0.01f, DT);
      f->e->x += (past[k].x - f->e->x) * m;
      f->e->y += (past[k].y - f->e->y) * m;
    }
    k += 5;
  }
}
/* a new player: a new Leader */
void leader_reset(void) { npast = nfol = 0; }

/* Leader.GainFollower and Follower.OnGainLeaderUtil */
bool leader_gain(Ent *e, float follow_delay, bool persistent) {
  if (nfol >= MAX_FOLLOWERS || find(e) >= 0) return false;
  Fol *f = &fol[nfol++];
  f->e = e;
  f->id = level_entity_id(g_level.rooms[e->room].index, e->eid);
  f->delay = follow_delay;
  f->move = 1;
  f->persistent = persistent;
  if (persistent) e->tags |= TAG_PERSISTENT;
  if (e->cls->on_gain_leader) e->cls->on_gain_leader(e);
  return true;
}
/* Follower.OnLoseLeaderUtil */
static void lost(Fol *f) {
  Ent *e = f->e;
  if (f->persistent) {
    e->tags &= (uint8_t)~TAG_PERSISTENT;
    e->room = (uint8_t)g_level.room_slot;   /* it stays in this room now */
  }
  if (e->cls->on_lose_leader) e->cls->on_lose_leader(e);
}
void leader_lose(Ent *e) {
  int i = find(e);
  if (i < 0) return;
  Fol f = fol[i];
  memmove(fol + i, fol + i + 1, sizeof(Fol) * (size_t)(nfol - i - 1));
  nfol--;
  lost(&f);
}
/* LoseFollowers: each is told while the list is still whole */
void leader_lose_all(void) {
  Fol copy[MAX_FOLLOWERS];
  int n = nfol;
  memcpy(copy, fol, sizeof(Fol) * (size_t)n);
  for (int i = 0; i < n; i++) lost(&copy[i]);
  nfol = 0;
}
/* TransferFollowers: those that do not follow between rooms let go */
void leader_transfer(void) {
  for (int i = 0; i < nfol; i++)
    if (!(fol[i].e->tags & TAG_PERSISTENT)) leader_lose(fol[i--].e);
}
int leader_index(const Ent *e) { return find(e); }        /* FollowIndex, -1 without a leader */
int leader_count(void) { return nfol; }
Ent *leader_follower(int i) { return i < nfol ? fol[i].e : NULL; }
bool leader_has(const Ent *e) { return find(e) >= 0; }
bool leader_has_id(uint32_t id) {
  for (int i = 0; i < nfol; i++)
    if (fol[i].id == id) return true;
  return false;
}
float leader_delay(const Ent *e) {
  int i = find(e);
  return i < 0 ? 0 : fol[i].delay;
}
/* Follower.Update */
void leader_tick(const Ent *e) {
  int i = find(e);
  if (i >= 0 && fol[i].delay > 0) fol[i].delay -= DT;
}
void leader_set_move(const Ent *e, bool move) {
  int i = find(e);
  if (i >= 0) fol[i].move = move;
}

/* ---------------------------------------------------------------- Strawberry */
/* "collected_seeds_of_" + its EntityID ("room:id") */
static void seeds_flag(char *buf, int room, int id) {
  char num[8];
  int n = 0;
  do num[n++] = (char)('0' + id % 10), id /= 10;
  while (id);
  path2(buf, "collected_seeds_of_", level_room_name_of(room), -1);
  char *p = buf + strlen(buf);
  *p++ = ':';
  while (n) *p++ = num[--n];
  *p = 0;
}

typedef struct {
  Sprite spr;
  Wiggler wig, rot_wig;
  Tween pulse, home, flap1, flap2;
  V2 start, home_from, home_ctrl;
  float wobble, collect_timer, flap_speed, wings_alarm, home_alarm, co_wait;
  int8_t index;           /* in the chapter's strawberries (the save keeps them by it) */
  uint8_t golden, moon, winged, ghost, collected, flying, waiting_seeds, return_home;
  uint8_t fly_step, collect_step, collect_index, nseeds;
  uint8_t room;           /* its EntityID: the room and the id */
  uint16_t eid;
} Berry;
static const EntClass BERRY;

static const PType *glow_type(const Berry *b) {
  return b->ghost ? &P_Strawberry_P_GhostGlow : b->golden ? &P_Strawberry_P_GoldGlow
                                                          : !b->moon ? &P_Strawberry_P_Glow : &P_Strawberry_P_MoonGlow;
}
static int anim(const Berry *b, int which) {   /* 0 idle, 1 collect, 2 flap */
  switch (b->spr.bank) {
    case SB_goldberry: return which == 0 ? A_goldberry_idle : which == 1 ? A_goldberry_collect : A_goldberry_flap;
    case SB_goldghostberry: return which == 0 ? A_goldghostberry_idle : which == 1 ? A_goldghostberry_collect : A_goldghostberry_flap;
    case SB_ghostberry: return which == 0 ? A_ghostberry_idle : which == 1 ? A_ghostberry_collect : A_ghostberry_flap;
    default: return which == 0 ? A_strawberry_idle : which == 1 ? A_strawberry_collect : A_strawberry_flap;
  }
}

/* IsFirstStrawberry: no other (not golden) strawberry ahead of it */
static bool first_berry(const Ent *e) {
  for (int i = leader_index(e) - 1; i >= 0; i--) {
    Ent *o = leader_follower(i);
    if (o->cls == &BERRY && !ST(o, Berry)->golden) return false;
  }
  return true;
}

static void points_new(V2 at, bool ghost, int index, bool moon);

/* OnCollect */
static void berry_collect(Ent *e) {
  Berry *b = ST(e, Berry);
  if (b->collected) return;
  int index = 0;
  b->collected = 1;
  if (leader_has(e)) {
    Player *p = level_player();
    index = p->strawb_index++;
    p->strawb_reset_timer = 2.5f;
    leader_lose(e);
  }
  save_add_berry(b->index, b->golden);
  if (b->index >= 0) g_session.berries |= (uint64_t)1 << b->index;   /* Strawberries, DoNotLoad */
  g_session.dashes_at_level_start = g_session.dashes;               /* UpdateLevelStartDashes */
  b->collect_index = (uint8_t)index;
  b->collect_step = 1;   /* CollectRoutine, from the components' turn */
}

/* OnAnimate: the sprite's frame changed */
static void berry_on_frame(Ent *e, Berry *b) {
  int id = b->spr.anim, frame = b->spr.frame;
  bool flap = id == anim(b, 2);
  if (!b->flying && flap && frame % 9 == 4) b->flap_speed = -50;
  int num = flap ? 25 : b->golden ? 30 : !b->moon ? 35 : 30;
  if (frame == num) {
    tween_start(&b->pulse, 0.5f);   /* the light pulses; Displacement bursts are not drawn here */
    (void)e;
  }
}

/* FlyAwayRoutine: a Monocle coroutine, `yield return t` waits t then a frame more */
static void fly_away(Berry *b) {
  if (b->co_wait > 0) {
    b->co_wait -= DT;
    return;
  }
  switch (b->fly_step) {
    case 1:
      wiggler_restart(&b->rot_wig);
      b->flap_speed = -200;
      tween_start(&b->flap1, 0.25f);
      b->co_wait = 0.1f;
      b->fly_step = 2;
      break;
    case 2:
      b->co_wait = 0.2f;
      b->fly_step = 3;
      break;
    case 3:
      tween_start(&b->flap2, 0.5f);
      b->fly_step = 0;
      break;
  }
}

static void berry_update(Ent *e) {
  Berry *b = ST(e, Berry);
  if (b->waiting_seeds) return;
  if (!b->collected) {
    if (!b->winged) b->wobble += DT * 4;
    int follow = leader_index(e);
    if (follow >= 0 && leader_delay(e) <= 0 && first_berry(e)) {
      Player *p = level_player();
      bool ok = false;
      if (p && !p->dead && !p->strawberries_blocked) {
        if (b->golden) {
          if (level_gold_collect_check(p->ent) || g_level.completed) ok = true;
        } else if (p->on_safe_ground && (!b->moon || p->state != ST_INTROJUMP))
          ok = true;
      }
      if (ok) {
        b->collect_timer += DT;
        if (b->collect_timer > 0.15f) berry_collect(e);
      } else
        b->collect_timer = fminf(b->collect_timer, 0);
    } else {
      if (follow > 0) b->collect_timer = -0.15f;
      if (b->winged) {
        e->y += b->flap_speed * DT;
        if (b->flying) {
          if (e->y < g_level.room->y - 16) {
            ent_remove(e);
            return;
          }
        } else {
          b->flap_speed = approach(b->flap_speed, 20, 170 * DT);
          if (e->y < b->start.y - 5) e->y = b->start.y - 5;
          else if (e->y > b->start.y + 5) e->y = b->start.y + 5;
        }
      }
    }
  }
  /* base.Update(): its components in order */
  leader_tick(e);
  int anim_was = b->spr.anim, frame_was = b->spr.frame;
  spr_update(&b->spr);
  if (b->spr.anim != anim_was || b->spr.frame != frame_was) berry_on_frame(e, b);
  wiggler_update(&b->wig);
  wiggler_update(&b->rot_wig);
  tween_update(&b->pulse);
  /* tweens added during a turn start on the next one: they go before what starts them */
  if (b->flap1.active) {
    tween_update(&b->flap1);
    b->flap_speed = -200 + 200 * ease_cube_out(b->flap1.percent);
  }
  if (b->flap2.active) {
    tween_update(&b->flap2);
    b->flap_speed = -200 * b->flap2.percent;
  }
  if (b->home.active) {
    bool done = tween_update(&b->home);
    float t = ease_sine_out(b->home.percent), u = 1 - t;   /* SimpleCurve.GetPoint */
    e->x = u * u * b->home_from.x + 2 * u * t * b->home_ctrl.x + t * t * b->start.x;
    e->y = u * u * b->home_from.y + 2 * u * t * b->home_ctrl.y + t * t * b->start.y;
    if (done) e->depth = 0, ents_mark_unsorted();
  }
  if (b->wings_alarm > 0 && (b->wings_alarm -= DT) <= 0) {   /* Alarm: the wings come off */
    b->spr.rate = 1;
    spr_play(&b->spr, anim(b, 0), false);
    particles_emit(PL_MID, &P_Strawberry_P_WingsBurst, 8, v2(e->x + 8, e->y), v2(4, 2), P_Strawberry_P_WingsBurst.direction);
    particles_emit(PL_MID, &P_Strawberry_P_WingsBurst, 8, v2(e->x - 8, e->y), v2(4, 2), P_Strawberry_P_WingsBurst.direction);
  }
  if (b->home_alarm > 0 && (b->home_alarm -= DT) <= 0) {   /* OnLoseLeader's alarm: a curve home */
    V2 d = v2sub(b->start, v2(e->x, e->y));
    float dist = v2len(d);
    V2 n = dist > 0 ? v2mul(d, 1 / dist) : v2(0, 0);
    float side = clamped_map(dist, 16, 120, 16, 96) * (rndi(2) ? -1.f : 1.f);
    b->home_from = v2(e->x, e->y);
    b->home_ctrl = v2add(v2add(b->start, v2mul(n, 16)), v2mul(v2(-n.y, n.x), side));
    tween_start(&b->home, fmaxf(dist / 100, 0.4f));
  }
  if (b->collect_step == 1) {   /* CollectRoutine */
    e->tags = TAG_TRANSITION_UPDATE;
    e->depth = -2000010;
    ents_mark_unsorted();
    spr_play(&b->spr, anim(b, 1), false);
    b->collect_step = 2;
  } else if (b->collect_step == 2 && !b->spr.playing) {
    points_new(v2(e->x, e->y), b->ghost, b->collect_index, b->moon);
    ent_remove(e);
    return;
  }
  if (b->fly_step) fly_away(b);
  if (leader_has(e) && level_on_interval(0.08f)) {
    const PType *t = glow_type(b);
    particles_emit1(PL_FG, t, v2(e->x + rnd_rangef(-6, 6), e->y + rnd_rangef(-6, 6)), t->direction);
  }
}

static void berry_render(Ent *e) {
  Berry *b = ST(e, Berry);
  if (b->waiting_seeds) return;
  Sprite s = b->spr;
  float y = b->winged ? 0 : sinf(b->wobble) * 2;
  s.sx = s.sy = 1 + b->wig.value * 0.35f;
  s.rot = b->rot_wig.value * 30 * (PI_F / 180);
  spr_draw(&s, e->x, e->y + y);
  float bloom = (b->golden || b->moon || b->ghost) ? 0.5f : 1;
  if (g_session.bloom_base_add > 0.1f) bloom *= 0.5f;
  bloom_add(e->x, e->y + y, bloom, 12);
  /* VertexLight(white, 1, 16, 24) and its pulse: the radii grow by 6 and 12 and come back */
  float k = b->pulse.active ? 1 - b->pulse.percent : 0;
  light_add(e->x, e->y + y, 0xFFFFFF, 1, (float)(int)(16 + 6 * k), (float)(int)(24 + 12 * k));
}

/* PlayerCollider */
static void berry_on_player(Ent *e, Player *p) {
  Berry *b = ST(e, Berry);
  if (leader_has(e) || b->collected || b->waiting_seeds) return;
  b->return_home = 1;
  if (b->winged) {
    b->winged = 0;
    b->spr.rate = 0;
    b->wings_alarm = 0.3f;   /* FollowDelay */
  }
  if (b->golden) g_session.grabbed_golden = 1;
  leader_gain(e, 0.3f, true);
  wiggler_restart(&b->wig);
  e->depth = -1000000;
  ents_mark_unsorted();
  (void)p;
}

/* OnLoseLeader */
static void berry_lose_leader(Ent *e) {
  Berry *b = ST(e, Berry);
  if (b->collected || !b->return_home) return;
  b->home_alarm = 0.15f;
}

/* DashListener */
static void berry_on_dash(Ent *e, V2 dir) {
  Berry *b = ST(e, Berry);
  (void)dir;
  if (!b->flying && b->winged && !b->waiting_seeds) {
    e->depth = -1000000;
    ents_mark_unsorted();
    b->fly_step = 1;
    b->flying = 1;
  }
}

static const EntClass BERRY = {.size = sizeof(Berry), .name = "strawberry", .update = berry_update, .render = berry_render,
                               .on_player = berry_on_player, .on_lose_leader = berry_lose_leader,
                               .on_dash = berry_on_dash, .kind = KIND_PCOLLIDE};

int berry_golden_room(void) {
  for (int i = 0; i < leader_count(); i++) {
    Ent *o = leader_follower(i);
    if (o->cls == &BERRY && ST(o, Berry)->golden && !ST(o, Berry)->winged) return ST(o, Berry)->room;
  }
  return -1;
}

/* the player reached the end: every strawberry it carries is collected (Level.RegisterAreaComplete) */
void berries_collect_all(void) {
  Ent *list[MAX_FOLLOWERS];
  int n = 0;
  for (int i = 0; i < leader_count(); i++)
    if (leader_follower(i)->cls == &BERRY) list[n++] = leader_follower(i);
  for (int i = 0; i < n; i++) berry_collect(list[i]);
}

/* ---------------------------------------------------------------- StrawberrySeed */
typedef struct {
  Sprite spr;
  Wiggler wig;
  SineWave sine;
  Tween pulse, a, b;        /* spin: a (spinLerp) and b; combine: b */
  V2 start, from, avg, center;
  float can_lose, lose, light_alpha, co_wait, angle_off, spin, start_angle;
  uint16_t berry;           /* its strawberry: g_ents index */
  uint8_t index, ghost, finished, losing, attached, step, anim;   /* anim: 1 spinning, 2 combining */
  int8_t shakex, shakey;
  float shake_timer;
} Seed;
static const EntClass SEED;

static void seed_lose_leader(Ent *e) {
  Seed *s = ST(e, Seed);
  if (!s->finished) s->step = 1;   /* the ReturnRoutine coroutine */
}
static void seed_gain_leader(Ent *e) {
  Seed *s = ST(e, Seed);
  wiggler_restart(&s->wig);
  s->can_lose = 0.25f;
  s->lose = 0.15f;
}

static void seeds_cutscene_start(Ent *berry);

static void seed_on_player(Ent *e, Player *p) {
  Seed *s = ST(e, Seed);
  (void)p;
  leader_gain(e, 0.2f, false);
  e->collidable = 0;
  e->depth = -1000000;
  ents_mark_unsorted();
  /* all of its strawberry's seeds following: the cutscene */
  for (int i = 0; i < g_nents; i++) {
    Ent *o = &g_ents[i];
    if (o->cls == &SEED && o->dead != 1 && ST(o, Seed)->berry == s->berry && !leader_has(o)) return;
  }
  seeds_cutscene_start(&g_ents[s->berry]);
}

/* ReturnRoutine */
static void seed_return(Ent *e, Seed *s) {
  if (s->co_wait > 0) {
    s->co_wait -= DT;
    return;
  }
  if (s->step == 1) {
    e->collidable = 0;
    s->spr.sx = s->spr.sy = 2;
    s->co_wait = 0.05f;
    s->step = 2;
  } else if (s->step == 2) {
    for (int i = 0; i < 6; i++) {
      float a = rndf() * PI_F * 2;
      particles_emit(PL_FG, &P_StrawberrySeed_P_Burst, 1, v2add(v2(e->x, e->y), angle_vec(a, 4)), v2(0, 0), a);
    }
    e->visible = 0;
    s->co_wait = 0.3f + s->index * 0.1f;
    s->step = 3;
  } else if (s->step == 3) {
    e->x = s->start.x, e->y = s->start.y;
    if (s->attached && ent_platform(e)) e->x += ent_platform(e)->x, e->y += ent_platform(e)->y;
    s->shake_timer = 0.4f;   /* Shaker.ShakeFor(0.4) */
    s->spr.sx = s->spr.sy = 1;
    e->visible = 1;
    e->collidable = 1;
    s->step = 0;
  }
}

static void seed_update(Ent *e) {
  Seed *s = ST(e, Seed);
  /* base.Update(): Follower, StaticMover, PlayerCollider, Wiggler, SineWave, Shaker, lights, coroutines */
  leader_tick(e);
  wiggler_update(&s->wig);
  sine_update(&s->sine);
  V2 shake = v2(0, 0);
  if (s->shake_timer > 0) {
    if (level_on_interval(0.04f)) s->shakex = (int8_t)(rndi(3) - 1), s->shakey = (int8_t)(rndi(3) - 1);
    s->shake_timer -= DT;
    if (s->shake_timer <= 0) s->shakex = s->shakey = 0;
    shake = v2(s->shakex, s->shakey);
  }
  int was = s->spr.frame;
  spr_update(&s->spr);
  if (s->spr.frame != was && e->visible && s->spr.anim == 0 && s->spr.frame == 19) tween_start(&s->pulse, 0.5f);
  tween_update(&s->pulse);
  if (s->step) seed_return(e, s);
  if (s->anim == 1) {   /* StartSpinAnimation */
    if (s->a.active) {
      tween_update(&s->a);
      s->spin = ease_cube_in(s->a.percent);
    }
    bool done = tween_update(&s->b);
    float ang = PI_F / 2 + s->angle_off - 32.201324f * ease_cube_inout(s->b.percent);
    V2 c = v2add(v2lerp(s->avg, s->center, s->spin), angle_vec(ang, 25));
    V2 p = v2lerp(s->from, c, s->spin);
    e->x = p.x, e->y = p.y;
    if (done) s->anim = 0;
  } else if (s->anim == 2) {   /* StartCombineAnimation */
    bool done = tween_update(&s->b);
    float ang = s->start_angle - PI_F * 2 * ease_cube_in(s->b.percent);
    float len = 25 * (1 - ease_big_back_in(s->b.percent));
    V2 p = v2add(s->center, angle_vec(ang, len));
    e->x = p.x, e->y = p.y;
    if (done) {
      e->visible = 0;
      for (int i = 0; i < 6; i++) {
        float a = rndf() * PI_F * 2;
        particles_emit(PL_FG, &P_StrawberrySeed_P_Burst, 1, v2add(v2(e->x, e->y), angle_vec(a, 4)), v2(0, 0), a);
      }
      ent_remove(e);
      return;
    }
  }
  if (!s->finished) {
    Player *p = level_player();
    if (s->can_lose > 0) s->can_lose -= DT;
    else if (leader_has(e) && p && p->on_ground) s->losing = 1;
    if (s->losing && p) {
      if (s->lose <= 0 || p->speed.y < 0) {
        leader_lose(e);
        s->losing = 0;
      } else if (p->on_ground)
        s->lose -= DT;
      else {
        s->lose = 0.15f;
        s->losing = 0;
      }
    }
  } else
    s->light_alpha = approach(s->light_alpha, 0, DT * 4);
  (void)shake;
}

static void seed_render(Ent *e) {
  Seed *s = ST(e, Seed);
  Sprite sp = s->spr;
  float k = 1 + 0.2f * s->wig.value;
  sp.sx *= k, sp.sy *= k;
  float ox = s->finished ? 0 : s->sine.value * 2 + s->shakex, oy = s->finished ? 0 : s->sine.value_over_two + s->shakey;
  spr_draw(&sp, e->x + ox, e->y + oy);
  bloom_add(e->x, e->y, 1, 12);
  float pk = s->pulse.active ? 1 - s->pulse.percent : 0;
  light_add(e->x, e->y, 0xFFFFFF, s->light_alpha, (float)(int)(16 + 6 * pk), (float)(int)(24 + 12 * pk));
}

/* StaticMover: SolidChecker = the solid overlaps the seed */
static bool seed_riding(Ent *e, Ent *solid) { return (solid->kind & KIND_SOLID) && collide_ent_at(solid, solid->x, solid->y, e); }
static void seed_attach(Ent *e, Ent *solid) {
  Seed *s = ST(e, Seed);
  e->depth = -1000000;
  ents_mark_unsorted();
  ent_box(e, 24, 24, -12, -12);
  s->attached = 1;
  s->start = v2(e->x - solid->x, e->y - solid->y);
}
static void seed_sm_move(Ent *e, V2 amount) { e->x += amount.x, e->y += amount.y; }

static const EntClass SEED = {.size = sizeof(Seed), .name = "strawberrySeed", .update = seed_update, .render = seed_render,
                              .on_player = seed_on_player, .on_gain_leader = seed_gain_leader,
                              .on_lose_leader = seed_lose_leader, .sm_riding = seed_riding, .sm_attach = seed_attach,
                              .sm_move = seed_sm_move, .kind = KIND_PCOLLIDE | KIND_STATICMOVER};

/* ---------------------------------------------------------------- the strawberry entity */
static void berry_new(const EData *d, bool golden, bool memorial) {
  int index = session_berry_index(d->room, d->id);
  if (index >= 0 && (g_session.berries >> index & 1)) return;   /* DoNotLoad */
  Ent *e = ent_new(&BERRY, d->x, d->y);
  if (!e) return;
  Berry *b = ST(e, Berry);
  b->index = (int8_t)index;
  b->room = (uint8_t)d->room;
  b->eid = (uint16_t)d->id;
  b->start = v2(d->x, d->y);
  b->golden = golden || memorial;
  b->winged = memorial || (!golden && EAB(d, strawberry, winged));
#ifdef EA_strawberry_moon
  b->moon = !golden && EAB(d, strawberry, moon);
#endif
  b->ghost = save_check_berry(index);
  b->return_home = 1;
  e->depth = -100;
  ent_box(e, 14, 14, -7, -7);
  wiggler_init(&b->wig, 0.4f, 4);
  wiggler_init(&b->rot_wig, 0.5f, 4);
  /* Added(): the sprite */
  spr_init(&b->spr, b->ghost ? (b->golden ? SB_goldghostberry : SB_ghostberry) : (b->golden ? SB_goldberry : SB_strawberry));
  if (b->ghost) b->spr.tint = 0xCE79, b->spr.alpha = 204;   /* Color.White * 0.8 */
  if (b->winged) spr_play(&b->spr, anim(b, 2), false);
  /* seeds */
  if (d->nnodes && !golden && !memorial) {
    char flag[48];
    seeds_flag(flag, d->room, d->id);
    b->eid = (uint16_t)d->id;
    if (!level_get_flag(flag)) {
      b->nseeds = (uint8_t)d->nnodes;
      b->waiting_seeds = 1;
      e->visible = 0;
      e->collidable = 0;
      int count = 0;
      for (int i = 0; i < d->nnodes; i++) {
        V2 at = ed_node(d, i);
        Ent *se = ent_new(&SEED, at.x, at.y);
        if (!se) break;
        Seed *s = ST(se, Seed);
        se->depth = -100;
        ent_box(se, 12, 12, -6, -6);
        s->start = at;
        s->index = (uint8_t)i;
        s->ghost = b->ghost;
        s->berry = (uint16_t)(e - g_ents);
        s->light_alpha = 1;
        wiggler_init(&s->wig, 0.5f, 4);
        s->sine.freq = 0.5f;
        sine_randomize(&s->sine);
        spr_init(&s->spr, s->ghost ? SB_ghostberrySeed : g_session.mode == M_C ? SB_goldberrySeed : SB_strawberrySeed);
        if (s->ghost) s->spr.tint = 0xCE79, s->spr.alpha = 204;
        count++;
      }
      /* Awake: the idle animation starts later for later seeds */
      for (int i = 0; i < g_nents; i++) {
        Ent *o = &g_ents[i];
        if (o->cls != &SEED || ST(o, Seed)->berry != (uint16_t)(e - g_ents)) continue;
        Seed *s = ST(o, Seed);
        float k = 0.25f + (1 - s->index / (count + 1.f)) * 0.75f;
        spr_play(&s->spr, 0, true);
        s->spr.frame = (uint8_t)(k * spr_frames(&s->spr));   /* PlayOffset */
        if (s->spr.frame >= spr_frames(&s->spr)) s->spr.frame = (uint8_t)(spr_frames(&s->spr) - 1);
      }
    }
  }
}
bool berry_create(const EData *d) {
  switch (d->type) {
    case ET_strawberry: berry_new(d, false, false); return true;
    case ET_goldenBerry: berry_new(d, true, false); return true;
    case ET_memorialTextController: berry_new(d, false, true); return true;
  }
  return false;
}

/* GoldBerryCollectTrigger: golden strawberries are collected only here (or at the end) */
static const EntClass GOLDTRIG = {.name = "goldenBerryCollectTrigger", .kind = KIND_TRIGGER};
bool berry_trigger(const EData *d) {
  if (d->type != TT_goldenBerryCollectTrigger) return false;
  Ent *e = ent_new(&GOLDTRIG, d->x, d->y);
  if (e) ent_box(e, EA(d, goldenBerryCollectTrigger, width), EA(d, goldenBerryCollectTrigger, height), 0, 0), e->visible = 0;
  return true;
}
bool level_gold_collect_check(const Ent *player) {
  for (int i = 0; i < g_nents; i++) {
    Ent *o = &g_ents[i];
    if (o->cls == &GOLDTRIG && o->dead != 1 && collide_ent_at(player, player->x, player->y, o)) return true;
  }
  return false;
}

/* ---------------------------------------------------------------- StrawberryPoints */
typedef struct {
  Sprite spr;
  float light;
  uint8_t ghost, moon;
} Points;
static void points_update(Ent *e) {
  Points *p = ST(e, Points);
  if (g_level.frozen) return;
  spr_update(&p->spr);
  if (!p->spr.playing) {   /* OnFinish */
    ent_remove(e);
    return;
  }
  e->y -= 8 * DT;
  e->x = clampf(e->x, g_level.cam.x + 8, g_level.cam.x + 320 - 8);
  e->y = clampf(e->y, g_level.cam.y + 8, g_level.cam.y + 180 - 8);
  p->light = approach(p->light, 0, DT * 4);
  const PType *t = p->ghost ? &P_Strawberry_P_GhostGlow : &P_Strawberry_P_Glow;
  if (p->moon && !p->ghost) t = &P_Strawberry_P_MoonGlow;
  if (level_on_interval(0.05f)) {
    uint16_t c1 = (uint16_t)rgb(t->color & 0xFFFFFF), c2 = (uint16_t)rgb(t->color2 & 0xFFFFFF);
    p->spr.tint = p->spr.tint == c2 ? c1 : c2;
  }
  if (level_on_interval(0.06f) && p->spr.frame > 11) particles_emit(PL_FG, t, 1, v2(e->x, e->y - 2), v2(8, 4), t->direction);
}
static void points_render(Ent *e) {
  Points *p = ST(e, Points);
  spr_draw(&p->spr, e->x, e->y);
  light_add(e->x, e->y, 0xFFFFFF, p->light, 16, 24);
  bloom_add(e->x, e->y, p->light, 12);
}
static const EntClass POINTS = {.size = sizeof(Points), .name = "strawberryPoints", .update = points_update, .render = points_render};

static void points_new(V2 at, bool ghost, int index, bool moon) {
  /* those already shown within 16 px go */
  for (int i = 0; i < g_nents; i++) {
    Ent *o = &g_ents[i];
    if (o->cls == &POINTS && o->dead != 1 && (o->x - at.x) * (o->x - at.x) + (o->y - at.y) * (o->y - at.y) <= 256) ent_remove(o);
  }
  Ent *e = ent_new(&POINTS, at.x, at.y);
  if (!e) return;
  Points *p = ST(e, Points);
  e->depth = -2000100;
  e->tags = TAG_PERSISTENT | TAG_TRANSITION_UPDATE | TAG_FROZEN_UPDATE;
  p->ghost = ghost, p->moon = moon;
  p->light = 1;
  spr_init(&p->spr, SB_strawberry);
  if (index > 5) index = 5;
  spr_play(&p->spr, moon ? A_strawberry_fade_wow : A_strawberry_fade0 + index, true);
}

/* ---------------------------------------------------------------- CSGEN_StrawberrySeeds */
typedef struct {
  uint16_t berry;
  uint8_t step, cam_on, cam_ease;
  float wait, cam_p, cam_dur, dist;
  V2 cam_start, cam_from, cam_to;
} SeedsScene;

/* CutsceneEntity.CameraTo: true once there */
static bool camera_to(SeedsScene *c) {
  if (c->cam_p < 1) {
    float k = c->cam_ease ? ease_cube_inout(c->cam_p) : c->cam_p;
    g_level.cam = v2add(c->cam_from, v2mul(v2sub(c->cam_to, c->cam_from), k));
    c->cam_p += DT / c->cam_dur;
    return false;
  }
  g_level.cam = c->cam_to;
  return true;
}
static void camera_start(SeedsScene *c, V2 to, float dur, bool ease) {
  c->cam_from = g_level.cam;
  c->cam_to = to;
  c->cam_dur = dur;
  c->cam_p = dur > 0 ? 0 : 1;
  c->cam_ease = ease;
  c->cam_on = 1;
}

static void each_seed(uint16_t berry, void (*fn)(Ent *, void *), void *ctx) {
  for (int i = 0; i < g_nents; i++) {
    Ent *o = &g_ents[i];
    if (o->cls == &SEED && o->dead != 1 && ST(o, Seed)->berry == berry) fn(o, ctx);
  }
}
/* OnAllCollected */
static void seed_all_collected(Ent *o, void *ctx) {
  (void)ctx;
  Seed *s = ST(o, Seed);
  s->finished = 1;
  leader_lose(o);
  o->depth = -2000002;
  o->tags = TAG_FROZEN_UPDATE;
  wiggler_restart(&s->wig);
}
typedef struct { V2 sum; int n; float angle, step; V2 center; } SpinCtx;
static void seed_sum(Ent *o, void *ctx) {
  SpinCtx *c = ctx;
  c->sum = v2add(c->sum, v2(o->x, o->y));
  c->n++;
}
static void seed_spin(Ent *o, void *ctx) {
  SpinCtx *c = ctx;
  Seed *s = ST(o, Seed);
  s->spin = 0;
  s->from = v2(o->x, o->y);
  s->avg = v2mul(c->sum, 1.f / c->n);
  s->center = c->center;
  s->angle_off = c->angle;
  c->angle -= c->step;
  spr_play(&s->spr, 1, true);   /* noFlash */
  tween_start(&s->a, 2);
  tween_start(&s->b, 4);
  s->anim = 1;
}
static void seed_combine(Ent *o, void *ctx) {
  SpinCtx *c = ctx;
  Seed *s = ST(o, Seed);
  s->center = c->center;
  s->start_angle = atan2f(o->y - c->center.y, o->x - c->center.x);
  tween_start(&s->b, 0.6f);
  s->anim = 2;
}
static void seed_remove(Ent *o, void *ctx) {
  (void)ctx;
  ent_remove(o);
}
/* Strawberry.CollectedSeeds */
static void berry_seeds_done(Ent *be) {
  Berry *b = ST(be, Berry);
  b->waiting_seeds = 0;
  be->visible = 1;
  be->collidable = 1;
  char flag[48];
  seeds_flag(flag, b->room, b->eid);
  level_set_flag(flag, true);
}

static void scene_update(Ent *e) {
  SeedsScene *c = ST(e, SeedsScene);
  Ent *be = &g_ents[c->berry];
  if (c->cam_on == 1 && camera_to(c)) c->cam_on = 0;   /* the camera's own coroutine */
  if (c->wait > 0) {   /* yield return t */
    c->wait -= DT;
    return;
  }
  switch (c->step) {
    case 0: {
      Player *p = level_player();
      c->cam_start = p ? player_camera_target(p) : g_level.cam;
      each_seed(c->berry, seed_all_collected, NULL);
      be->depth = -2000002;
      be->tags |= TAG_FROZEN_UPDATE;
      ents_mark_unsorted();
      c->wait = 0.35f;
      c->step = 1;
      break;
    }
    case 1:
      e->tags = TAG_FROZEN_UPDATE | TAG_HUD;
      g_level.frozen = true;
      g_level.formation = true;   /* FormationBackdrop */
      g_level.formation_alpha = 0.5f;
      c->wait = 0.1f;
      c->step = 2;
      break;
    case 2: {
      SpinCtx s = {v2(0, 0), 0, PI_F / 2, 0, v2(be->x, be->y)};
      each_seed(c->berry, seed_sum, &s);
      if (!s.n) s.n = 1;
      s.step = PI_F * 2 / s.n;
      each_seed(c->berry, seed_spin, &s);
      Room *rm = g_level.room;
      camera_start(c, v2(clampf(be->x - 160, (float)rm->x, (float)(rm->x + rm->w - 320)),
                         clampf(be->y - 90, (float)rm->y, (float)(rm->y + rm->h - 180))), 3.5f, true);
      c->wait = 4;
      c->step = 3;
      break;
    }
    case 3: {
      SpinCtx s = {v2(0, 0), 0, 0, 0, v2(be->x, be->y)};
      each_seed(c->berry, seed_combine, &s);
      c->wait = 0.6f;
      c->step = 4;
      break;
    }
    case 4:
      each_seed(c->berry, seed_remove, NULL);
      berry_seeds_done(be);
      c->wait = 0.5f;
      c->step = 5;
      break;
    case 5:   /* yield return CameraTo(cameraStart, dist / 180) */
      c->dist = v2len(v2sub(g_level.cam, c->cam_start));
      camera_start(c, c->cam_start, c->dist / 180, false);
      c->cam_on = 2;
      c->step = 6;
      break;
    case 6:
      if (!camera_to(c)) break;
      c->cam_on = 0;
      c->step = 7;
      if (c->dist > 80) c->wait = 0.25f;
      break;
    case 7:   /* EndCutscene, OnEnd */
      g_level.in_cutscene = false;
      be->depth = -100;
      be->tags &= (uint8_t)~TAG_FROZEN_UPDATE;
      ents_mark_unsorted();
      g_level.frozen = false;
      g_level.formation = false;
      ent_remove(e);
      break;
  }
}
static const EntClass SEEDSCENE = {.size = sizeof(SeedsScene), .name = "CSGEN_StrawberrySeeds", .update = scene_update};
static void seeds_cutscene_start(Ent *berry) {
  Ent *e = ent_new(&SEEDSCENE, 0, 0);
  if (!e) return;
  e->visible = 0;
  e->tags = TAG_FROZEN_UPDATE;
  ST(e, SeedsScene)->berry = (uint16_t)(berry - g_ents);
  g_level.in_cutscene = true;   /* Level.StartCutscene */
}
