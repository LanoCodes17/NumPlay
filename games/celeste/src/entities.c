#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("Os")   /* not drawn every frame: smaller over faster */
#endif
/* The entities of Prologue and Forsaken City and the ones every chapter
 * uses: spikes, springs, refills, strawberries, jumpthrus, zip movers,
 * crumbling and falling blocks, dash blocks, fake walls, camera triggers. */
#include "entities.h"
#include "npc.h"

/* ---------------------------------------------------------------- records */
float ea_num(const EData *d, int off, char kind) {
  const uint8_t *p = d->attrs + off;
  switch (kind) {
    case 'b': return p[0] ? 1.f : 0.f;
    case 'i': return (float)rds16(p);
    case 'I': return (float)rds32(p);
    case 'f': return rdf(p);
  }
  return 0;
}
const char *ea_str(const EData *d, int off) { return str(rd16(d->attrs + off)); }
V2 ed_node(const EData *d, int i) {
  if (i >= d->nnodes) return v2(d->x, d->y);
  return v2(d->rx + rds16(d->nodes + 4 * i), d->ry + rds16(d->nodes + 4 * i + 2));
}
uint32_t level_entity_hash(const EData *d) { return level_entity_id(d->room, d->id) | (d->trig ? 1u << 31 : 0); }

const char *area_jumpthru(void) {
  /* AreaData.Jumpthru */
  static const char *J[] = {"wood", "wood", "wood", "wood", "cliffside", "temple", "reflection", "temple", "wood", "core", "wood"};
  int a = g_session.area;
  return a < 11 ? J[a] : "wood";
}
const char *area_crumble(void) {
  int a = g_session.area;
  return a == 4 ? "cliffside" : "default";
}
static int area_spike(void) {   /* AreaData.Spike: 0 default, 1 outline, 2 cliffside, 3 reflection */
  int a = g_session.area;
  return a == 4 ? 2 : a == 6 ? 3 : a == 7 ? 1 : 0;
}

static uint16_t tex_by_name(const char *path);

/* ---------------------------------------------------------------- Wiggler, SineWave */
void wiggler_init(Wiggler *w, float duration, float freq) {
  w->counter = w->sine_counter = 0;
  w->increment = 1 / duration;
  w->sine_add = PI_F * 2 * freq;
  w->active = false;
}
void wiggler_restart(Wiggler *w) {
  w->counter = 1;
  if (w->start_zero) w->sine_counter = PI_F / 2, w->value = 0;
  else w->sine_counter = 0, w->value = 1;
  w->active = true;
}
void wiggler_start(Wiggler *w, float duration, float freq) {
  w->increment = 1 / duration;
  w->sine_add = PI_F * 2 * freq;
  wiggler_restart(w);
}
void wiggler_update(Wiggler *w) {
  if (!w->active) return;
  w->sine_counter += w->sine_add * DT;
  w->counter -= w->increment * DT;
  if (w->counter <= 0) {
    w->counter = 0;
    w->active = false;
  }
  w->value = cosf(w->sine_counter) * w->counter;
}
void sine_set(SineWave *s, float counter) {
  s->counter = fmodf(counter + PI_F * 8, PI_F * 8);
  s->value = sinf(s->counter);
  s->value_over_two = sinf(s->counter / 2);
  s->two_value = sinf(s->counter * 2);
}
void sine_randomize(SineWave *s) { sine_set(s, rndf() * (PI_F * 2) * 2); }
void sine_update(SineWave *s) { sine_set(s, s->counter + PI_F * 2 * s->freq * DT); }
void tween_start(Tween *t, float duration) {
  t->duration = duration;
  t->time_left = duration;
  t->percent = 0;
  t->active = true;
}
bool tween_update(Tween *t) {
  if (!t->active) return false;
  t->time_left -= DT;
  t->percent = 1 - fmaxf(0, t->time_left) / t->duration;
  if (t->time_left <= 0) {
    t->time_left = 0;
    t->active = false;
    return true;
  }
  return false;
}

/* ---------------------------------------------------------------- spawn points */
#define MAX_SPAWNS 48
static V2 spawns[2][MAX_SPAWNS];
static int nspawns[2];

V2 level_closest_spawn(V2 at) {
  int s = g_level.room_slot;
  if (!nspawns[s]) return at;
  V2 best = spawns[s][0];
  float bd = 1e30f;
  for (int i = 0; i < nspawns[s]; i++) {
    float dx = spawns[s][i].x - at.x, dy = spawns[s][i].y - at.y, d = dx * dx + dy * dy;
    if (d < bd) bd = d, best = spawns[s][i];
  }
  return best;
}

/* DashListener.OnDash, for each that listens */
void level_dash_listeners(V2 dir) {
  for (int i = 0; i < g_nents; i++) {
    Ent *e = &g_ents[i];
    if (e->cls && e->dead != 1 && e->cls->on_dash) e->cls->on_dash(e, dir);
  }
}
WEAK void level_booster_boosted(Ent *b, V2 dir) { (void)b, (void)dir; }
/* Holdables (Theo's crystal, src/ch5.c): the player picks up, carries, throws */
WEAK Ent *level_holdable_check(void) { return NULL; }
WEAK void holdable_carry(Ent *h, V2 at) { (void)h, (void)at; }
WEAK void holdable_release(Ent *h, V2 force) { (void)h, (void)force; }
bool level_is_dash_block(Ent *e) { return e->cls && !strcmp(e->cls->name, "dashBlock"); }

/* ---------------------------------------------------------------- JumpthruPlatform */
typedef struct { uint16_t tex; uint8_t cols, n, first_row, last_row; uint32_t seed; } JumpThru;
/* the middle columns' pieces were Calc.Random picks: a hash of the place */
static void jt_piece(JumpThru *j, int i, int *col, int *row) {
  if (i == 0) *col = 0, *row = j->first_row;
  else if (i == j->cols - 1) *col = j->n - 1, *row = j->last_row;
  else {
    uint32_t h = (j->seed + (uint32_t)i * 2654435761u) ^ 0x9E3779B9u;
    h ^= h >> 15, h *= 0x2C1B3C6Du, h ^= h >> 12;
    *col = 1 + (int)(h % (uint32_t)(j->n > 2 ? j->n - 2 : 1));
    *row = (int)((h >> 8) & 1);
  }
}
static void jumpthru_awake(Ent *e) {
  JumpThru *j = ST(e, JumpThru);
  Tex t;
  j->n = tex_get(j->tex, &t) ? (uint8_t)(t.fw / 8) : 3;
  j->first_row = !collide_solid(e, e->x - 1, e->y);
  j->last_row = !collide_solid(e, e->x + 1, e->y);
  j->seed = (uint32_t)(int)e->x * 73856093u ^ (uint32_t)(int)e->y * 19349663u;
}
static void jumpthru_render(Ent *e) {
  JumpThru *j = ST(e, JumpThru);
  for (int i = 0; i < j->cols; i++) {
    int col, row;
    jt_piece(j, i, &col, &row);
    gfx_tex_part(j->tex, e->x + i * 8, e->y, col * 8, row * 8, 8, 8, 0, 0xFFFF, 255);
  }
}
static void jumpthru_update(Ent *e) { plat_update(e); }
static const EntClass JUMPTHRU = {.size = sizeof(JumpThru), .name = "jumpThru", .update = jumpthru_update, .render = jumpthru_render,
                                  .awake = jumpthru_awake, .kind = KIND_JUMPTHRU};

static void new_jumpthru(const EData *d) {
  int w = (int)EA(d, jumpThru, width);
  Ent *e = ent_new(&JUMPTHRU, d->x, d->y);
  if (!e) return;
  ent_box(e, (float)w, 5, 0, 0);
  e->safe = 1;
  e->depth = -60;
  JumpThru *j = ST(e, JumpThru);
  j->cols = (uint8_t)(w / 8);
  const char *tx = EAS(d, jumpThru, texture);
  char path[48];
  path2(path, "objects/jumpthru/", (*tx && strcmp(tx, "default")) ? tx : area_jumpthru(), -1);
  j->tex = tex_by_name(path);
}

/* ---------------------------------------------------------------- Spikes */
enum { DIR_UP, DIR_DOWN, DIR_LEFT, DIR_RIGHT };
typedef struct {
  uint8_t dir, n, cassette, on;
  uint16_t tex, tint, tint_off;   /* on a cassette block: its colors (enabled, disabled) */
  int16_t gox, goy;               /* and its group's origin (SetOrigins), from the spikes */
} Spikes;
static void spikes_on_player(Ent *e, Player *p) {
  Spikes *s = ST(e, Spikes);
  switch (s->dir) {
    case DIR_UP: if (p->speed.y >= 0 && e_bottom(p->ent) <= e_bottom(e)) player_die(p, v2(0, -1), false); break;
    case DIR_DOWN: if (p->speed.y <= 0) player_die(p, v2(0, 1), false); break;
    case DIR_LEFT: if (p->speed.x >= 0) player_die(p, v2(-1, 0), false); break;
    case DIR_RIGHT: if (p->speed.x <= 0) player_die(p, v2(1, 0), false); break;
  }
}
static bool spikes_riding(Ent *e, Ent *p) {
  Spikes *s = ST(e, Spikes);
  if (p->kind & KIND_JUMPTHRU) return s->dir == DIR_UP && collide_ent_at(e, e->x, e->y + 1, p);
  float dx = 0, dy = 0;
  switch (s->dir) {
    case DIR_UP: dy = 1; break;
    case DIR_DOWN: dy = -1; break;
    case DIR_LEFT: dx = 1; break;
    case DIR_RIGHT: dx = -1; break;
  }
  return collide_ent_at(e, e->x + dx, e->y + dy, p) && !collide_ent_at(e, e->x, e->y, p);
}
static void spikes_render(Ent *e) {
  Spikes *s = ST(e, Spikes);
  Tex t;
  if (!tex_get(s->tex, &t)) return;
  for (int j = 0; j < s->n; j++) {
    float x = e->x, y = e->y, ox = 0, oy = 0;
    switch (s->dir) {
      case DIR_UP: ox = t.fw * .5f, oy = (float)t.fh, x += (j + .5f) * 8, y += 1; break;
      case DIR_DOWN: ox = t.fw * .5f, oy = 0, x += (j + .5f) * 8, y -= 1; break;
      case DIR_RIGHT: ox = 0, oy = t.fh * .5f, y += (j + .5f) * 8, x -= 1; break;
      case DIR_LEFT: ox = (float)t.fw, oy = t.fh * .5f, y += (j + .5f) * 8, x += 1; break;
    }
    if (s->cassette) {   /* scaled with the block's group around its origin */
      V2 o, k;
      if (!cassette_block_scale(ent_platform(e), &o, &k)) k = v2(1, 1);
      float gx = e->x + s->gox, gy = e->y + s->goy;
      gfx_tex_ex(s->tex, gx + e->shakex, gy + e->shakey, ox + gx - x, oy + gy - y, k.x, k.y, 0, s->on ? s->tint : s->tint_off,
                 255, 0);
    } else
      gfx_tex_ex(s->tex, x + e->shakex, y + e->shakey, ox, oy, 1, 1, 0, 0xFFFF, 255, 0);
  }
}
/* StaticMover.OnEnable / OnDisable (VisibleWhenDisabled on cassette blocks) */
static void spikes_sm_enable(Ent *e, bool on) {
  Spikes *s = ST(e, Spikes);
  e->active = e->collidable = on;
  if (on) e->visible = 1;
  else if (!s->cassette) e->visible = 0;
  s->on = on;   /* the color: enabled or disabled */
}
void spikes_set_cassette(Ent *e, uint16_t on, uint16_t off, V2 origin) {
  Spikes *s = ST(e, Spikes);
  s->cassette = 1;
  s->tint = on, s->tint_off = off;
  s->on = e->collidable;
  s->gox = (int16_t)(origin.x - e->x), s->goy = (int16_t)(origin.y - e->y);
}
static void spikes_sm_shake(Ent *e, V2 a) { e->shakex = (int8_t)(e->shakex + a.x), e->shakey = (int8_t)(e->shakey + a.y); }
static const EntClass SPIKES = {.size = sizeof(Spikes), .name = "spikes", .render = spikes_render, .on_player = spikes_on_player,
                                .sm_riding = spikes_riding, .sm_shake = spikes_sm_shake, .sm_enable = spikes_sm_enable,
                                .kind = KIND_PCOLLIDE | KIND_STATICMOVER | KIND_SPIKES};

static void new_spikes(const EData *d, int dir) {
  int size;
  const char *type;
  if (dir == DIR_UP) size = (int)EA(d, spikesUp, width), type = EAS(d, spikesUp, type);
  else if (dir == DIR_DOWN) size = (int)EA(d, spikesDown, width), type = EAS(d, spikesDown, type);
  else if (dir == DIR_LEFT) size = (int)EA(d, spikesLeft, height), type = EAS(d, spikesLeft, type);
  else size = (int)EA(d, spikesRight, height), type = EAS(d, spikesRight, type);
  Ent *e = ent_new(&SPIKES, d->x, d->y);
  if (!e) return;
  e->depth = -1;
  switch (dir) {
    case DIR_UP: ent_box(e, (float)size, 3, 0, -3); break;
    case DIR_DOWN: ent_box(e, (float)size, 3, 0, 0); break;
    case DIR_LEFT: ent_box(e, 3, (float)size, -3, 0); break;
    case DIR_RIGHT: ent_box(e, 3, (float)size, 0, 0); break;
  }
  Spikes *s = ST(e, Spikes);
  s->dir = (uint8_t)dir;
  s->n = (uint8_t)(size / 8);
  static const char *names[] = {"default", "outline", "cliffside", "reflection"};
  static const char *dirs[] = {"up", "down", "left", "right"};
  const char *t = (*type && strcmp(type, "default")) ? type : names[area_spike()];
  char path[48], tmp[24];
  path2(tmp, t, "_", -1);
  path2(path, "danger/spikes/", path2(tmp + 12 - 12, tmp, dirs[dir], -1), 0);
  s->tex = tex_by_name(path);
}

/* ---------------------------------------------------------------- Spring */
typedef struct { uint8_t orient, can_use; Sprite spr; Wiggler wig; } Spring;
enum { SPRING_FLOOR, SPRING_WALLLEFT, SPRING_WALLRIGHT };
static uint16_t spring_tex[7];

static void spring_on_player(Ent *e, Player *p) {
  Spring *s = ST(e, Spring);
  if (p->state == ST_DREAMDASH || !s->can_use) return;
  bool bounce = false;
  if (s->orient == SPRING_FLOOR) {
    if (p->speed.y >= 0) bounce = true, player_super_bounce(p, e_top(e));
  } else if (s->orient == SPRING_WALLLEFT) {
    bounce = true;
    player_side_bounce(p, 1, e_right(e), e_cym(e));
  } else {
    bounce = true;
    player_side_bounce(p, -1, e_left(e), e_cym(e));
  }
  if (bounce) {
    if (ent_platform(e)) plat_static_movers_trigger(ent_platform(e));
    s->spr.frame = 0;
    s->spr.timer = 0;
    s->spr.playing = 1;
    wiggler_start(&s->wig, 1, 4);
  }
}
static const uint8_t SPRING_SEQ[] = {0, 1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 3, 4, 5};
static void spring_update(Ent *e) {
  Spring *s = ST(e, Spring);
  if (s->spr.playing) {
    s->spr.timer += DT;
    while (s->spr.timer >= 0.07f && s->spr.playing) {
      s->spr.timer -= 0.07f;
      if (++s->spr.frame >= sizeof SPRING_SEQ) s->spr.frame = 0, s->spr.playing = 0;
    }
  }
  wiggler_update(&s->wig);
}
static void spring_render(Ent *e) {
  Spring *s = ST(e, Spring);
  int f = s->spr.playing ? SPRING_SEQ[s->spr.frame] : 0;
  uint16_t t = s->can_use ? spring_tex[f] : spring_tex[6];
  float sy = 1 + s->wig.value * 0.2f;
  float rot = s->orient == SPRING_WALLLEFT ? PI_F / 2 : s->orient == SPRING_WALLRIGHT ? -PI_F / 2 : 0;
  gfx_tex_ex(t, e->x, e->y, 8, 16, 1, sy, rot, 0xFFFF, 255, 0);
}
static bool spring_riding(Ent *e, Ent *p) {
  Spring *s = ST(e, Spring);
  float dx = s->orient == SPRING_WALLLEFT ? -1 : s->orient == SPRING_WALLRIGHT ? 1 : 0;
  float dy = s->orient == SPRING_FLOOR ? 1 : 0;
  return collide_ent_at(e, e->x + dx, e->y + dy, p);
}
static void spring_attach(Ent *e, Ent *p) {
  e->depth = (int16_t)(p->depth + 1);
  ents_mark_unsorted();
}
static const EntClass SPRING = {.size = sizeof(Spring), .name = "spring", .update = spring_update, .render = spring_render,
                                .on_player = spring_on_player, .sm_riding = spring_riding, .sm_attach = spring_attach,
                                .kind = KIND_PCOLLIDE | KIND_STATICMOVER};

static void new_spring(const EData *d, int orient, bool can_use) {
  if (!spring_tex[0]) {
    char p[32];
    for (int i = 0; i < 6; i++) path2(p, "objects/spring/", NULL, i), spring_tex[i] = tex_by_name(p);
    spring_tex[6] = tex_by_name("objects/spring/white");
  }
  Ent *e = ent_new(&SPRING, d->x, d->y);
  if (!e) return;
  e->depth = -8501;
  Spring *s = ST(e, Spring);
  s->orient = (uint8_t)orient;
  s->can_use = can_use;
  if (orient == SPRING_FLOOR) ent_box(e, 16, 6, -8, -6);
  else if (orient == SPRING_WALLLEFT) ent_box(e, 6, 16, 0, -8);
  else ent_box(e, 6, 16, -6, -8);
}

/* ---------------------------------------------------------------- Refill */
typedef struct {
  uint8_t two, one_use, visible, outline, flash_on, step;
  float respawn, flash_t, co_wait, frame_t;
  int frame, flash_frame;
  SineWave sine;
  Wiggler wig;
  uint16_t idle[5], flash[6], outline_tex;
} Refill;

static void refill_on_player(Ent *e, Player *p) {
  Refill *r = ST(e, Refill);
  if (player_use_refill(p, r->two)) {
    e->collidable = 0;
    r->respawn = 2.5f;
    r->step = 1;   /* RefillRoutine */
    level_freeze(0.05f);
  }
}
static void refill_update(Ent *e) {
  Refill *r = ST(e, Refill);
  if (r->step == 1) {   /* yield null after the freeze */
    r->step = 2;
  } else if (r->step == 2) {
    level_shake(.3f);
    r->visible = 0;
    r->flash_on = 0;
    if (!r->one_use) r->outline = 1;
    e->depth = 8999;
    ents_mark_unsorted();
    r->co_wait = 0.05f;
    r->step = 3;
  } else if (r->step == 3) {
    if ((r->co_wait -= DT) <= 0) {
      Player *p = level_player();
      float a = p ? atan2f(p->speed.y, p->speed.x) : 0;
      particles_emit(PL_FG, (r->two ? &P_Refill_P_ShatterTwo : &P_Refill_P_Shatter), 5, v2(e->x, e->y), v2(4, 4), a - PI_F / 2);
      particles_emit(PL_FG, (r->two ? &P_Refill_P_ShatterTwo : &P_Refill_P_Shatter), 5, v2(e->x, e->y), v2(4, 4), a + PI_F / 2);
      r->step = 0;
      if (r->one_use) ent_remove(e);
    }
  }
  if (r->respawn > 0) {
    r->respawn -= DT;
    if (r->respawn <= 0 && !e->collidable) {
      e->collidable = 1;
      r->visible = 1;
      r->outline = 0;
      e->depth = -100;
      ents_mark_unsorted();
      wiggler_start(&r->wig, 1, 4);
      particles_emit(PL_FG, (r->two ? &P_Refill_P_RegenTwo : &P_Refill_P_Regen), 16, v2(e->x, e->y), v2(2, 2), 0);
    }
  } else if (level_on_interval(0.1f) && r->visible)
    particles_emit(PL_FG, (r->two ? &P_Refill_P_GlowTwo : &P_Refill_P_Glow), 1, v2(e->x, e->y), v2(5, 5), 0);
  sine_update(&r->sine);
  wiggler_update(&r->wig);
  r->frame_t += DT;
  if (r->frame_t >= 0.1f) r->frame_t -= 0.1f, r->frame = (r->frame + 1) % 5;
  if (r->flash_on) {
    r->flash_t += DT;
    if (r->flash_t >= 0.05f) {
      r->flash_t -= 0.05f;
      if (++r->flash_frame >= 6) r->flash_on = 0;
    }
  }
  if (level_on_interval(2) && r->visible) r->flash_on = 1, r->flash_frame = 0, r->flash_t = 0;
}
static void refill_render(Ent *e) {
  Refill *r = ST(e, Refill);
  float y = e->y + r->sine.value * 2, s = 1 + r->wig.value * 0.2f;
  if (r->outline) gfx_tex_ex(r->outline_tex, e->x, e->y, 8, 8, 1, 1, 0, 0xFFFF, 255, 0);
  if (r->visible) gfx_tex_ex(r->idle[r->frame], e->x, y, 8, 8, s, s, 0, 0xFFFF, 255, 0);
  if (r->visible && r->flash_on) gfx_tex_ex(r->flash[r->flash_frame], e->x, y, 8, 8, s, s, 0, 0xFFFF, 255, 0);
}
static const EntClass REFILL = {.size = sizeof(Refill), .name = "refill", .update = refill_update, .render = refill_render,
                                .on_player = refill_on_player, .kind = KIND_PCOLLIDE};
static void new_refill(const EData *d) {
  Ent *e = ent_new(&REFILL, d->x, d->y);
  if (!e) return;
  ent_box(e, 16, 16, -8, -8);
  e->depth = -100;
  Refill *r = ST(e, Refill);
  r->two = EAB(d, refill, twoDash);
  r->one_use = EAB(d, refill, oneUse);
  r->visible = 1;
  r->sine.freq = 0.6f;
  sine_randomize(&r->sine);
  const char *base = r->two ? "objects/refillTwo/" : "objects/refill/";
  char p[48];
  for (int i = 0; i < 5; i++) path2(p, base, "idle", i), r->idle[i] = tex_by_name(p);
  for (int i = 0; i < 6; i++) path2(p, base, "flash", i), r->flash[i] = tex_by_name(p);
  path2(p, base, "outline", -1);
  r->outline_tex = tex_by_name(p);
}

/* ---------------------------------------------------------------- level-wide entities */
/* effects that live as long as the level (SlashFx and the like) */
static void fx_entity_update(Ent *e) {
  (void)e;
  fx_update();
}
static const EntClass FX = {.name = "fx", .update = fx_entity_update};

/* FormationBackdrop: darkens the level behind hearts, cassettes, seeds being put together */
static void formation_update(Ent *e) {
  (void)e;
  g_level.formation_fade = approach(g_level.formation_fade, g_level.formation ? 1.f : 0.f, DT * 3);
}
static void formation_render(Ent *e) {
  (void)e;
  float a = g_level.formation_fade * g_level.formation_alpha * 0.85f;
  if (a > 0) gfx_rect(g_level.cam.x - 1, g_level.cam.y - 1, 322, 182, 0, (uint8_t)(a * 255));
}
static const EntClass FORMATION = {.name = "formationBackdrop", .update = formation_update, .render = formation_render};

/* ---------------------------------------------------------------- CrumblePlatform */
#define CRUMBLE_MAX 40
typedef struct {
  uint8_t n, state, on_top, fell;   /* fell: 0 standing, 1 falling out, 2 coming back */
  float timer, outline_a, fade_from, fade_to, fade_t, wait, tiles_t;
  int step;
  uint16_t tex, outline;
  uint8_t order[CRUMBLE_MAX];       /* fall order: tile k falls (order[k] % 4) * 0.05 s late */
} Crumble;

static float crumble_delay(Crumble *c, int img) {
  for (int k = 0; k < c->n; k++)
    if (c->order[k] == img) return (k % 4) * 0.05f;
  return 0;
}
static void crumble_emit(Ent *e, Crumble *c) {
  for (int i = 0; i < c->n; i++) particles_emit(PL_MID, &P_CrumblePlatform_P_Crumble, 2, v2(e->x + 4 + i * 8, e->y + 6), v2(3, 3), 0);
}
static void crumble_update(Ent *e) {
  Crumble *c = ST(e, Crumble);
  plat_update(e);
  if (c->fade_t < 1) {
    c->fade_t += DT * 2;
    c->outline_a = c->fade_from + (c->fade_to - c->fade_from) * ease_cube_inout(fminf(c->fade_t, 1));
  }
  c->tiles_t += DT;
  switch (c->step) {
    case 0: {   /* waiting for the player */
      bool top = solid_has_player_on_top(e);
      if (!top && !solid_has_player_climbing(e)) return;
      c->on_top = top;
      c->timer = top ? 0.6f : 1.f;   /* ShakeFor */
      crumble_emit(e, c);
      c->state = top ? 1 : 3;
      c->wait = 0.2f;
      c->step = 1;
      break;
    }
    case 1:   /* bursts every 0.2 s */
      c->timer -= DT;
      if ((c->wait -= DT) <= 0) {
        crumble_emit(e, c);
        if (--c->state == 0) c->step = 2, c->wait = 0.4f;
        else c->wait = 0.2f;
      }
      break;
    case 2:   /* 0.4 s more (while the player stays on top) */
      c->timer -= DT;
      c->wait -= DT;
      if (c->wait <= 0 || (c->on_top && !solid_has_player_on_top(e))) {
        c->fade_from = 0, c->fade_to = 1, c->fade_t = 0;
        e->collidable = 0;
        c->fell = 1;
        c->tiles_t = 0;
        c->step = 3;
        c->wait = 2;
      }
      break;
    case 3:
      if ((c->wait -= DT) > 0) break;
      {
        bool blocked = false;
        e->collidable = 1;
        for (int i = 0; i < g_nents && !blocked; i++) {
          Ent *o = &g_ents[i];
          if (o != e && o->cls && o->dead != 1 && (o->kind & (KIND_ACTOR | KIND_SOLID)) && o->collidable && collide_ent_at(e, e->x, e->y, o))
            blocked = true;
        }
        e->collidable = 0;
        if (blocked) break;
      }
      c->fade_from = 1, c->fade_to = 0, c->fade_t = 0;
      e->collidable = 1;
      c->fell = 2;
      c->tiles_t = 0;
      c->step = 0;
      break;
  }
}
static void crumble_render(Ent *e) {
  Crumble *c = ST(e, Crumble);
  if (c->outline_a > 0) {
    uint8_t a = (uint8_t)(c->outline_a * 255);
    for (int i = 0; i < c->n; i++) {
      int col = c->n == 1 ? 3 : i == 0 ? 0 : i < c->n - 1 ? 1 : 2;
      gfx_tex_part(c->outline, e->x + i * 8, e->y, col * 8, 0, 8, 8, 0, 0xFFFF, a);
    }
  }
  bool shaking = c->timer > 0 && (c->step == 1 || c->step == 2);
  for (int i = 0; i < c->n; i++) {
    int col = (int)((fabsf(e->x) + i * 8) / 8) % 4;
    float x = e->x + i * 8, y = e->y;
    uint16_t tint = 0xFFFF;
    uint8_t a = 255;
    float t = c->tiles_t - crumble_delay(c, i);
    if (c->fell == 1) {   /* TileOut: grey, then falls away */
      tint = 0x8410;
      if (t > 0) {
        float k = t / 0.25f;
        if (k >= 1) continue;
        a = (uint8_t)(255 * (1 - k));
        y += k * k * 12;
      }
    } else if (c->fell == 2) {   /* TileIn */
      if (t < 0) continue;
    }
    if (shaking) x += (float)(rndi(3) - 1), y += (float)(rndi(3) - 1);
    if (c->fell == 2 && t < 0.25f) {
      float sc = 1 + ease_bounce_out(1 - t / 0.25f) * 0.2f;
      gfx_tex_ex(c->tex, x + 4, y + 4, (float)(col * 8 + 4), 4, sc, sc, 0, tint, a, 0);
    } else
      gfx_tex_part(c->tex, x, y, col * 8, 0, 8, 8, 0, tint, a);
  }
}
static const EntClass CRUMBLE = {.size = sizeof(Crumble), .name = "crumbleBlock", .update = crumble_update, .render = crumble_render, .kind = KIND_SOLID};
static void new_crumble(const EData *d) {
  Ent *e = ent_new(&CRUMBLE, d->x, d->y);
  if (!e) return;
  int w = (int)EA(d, crumbleBlock, width);
  ent_box(e, (float)w, 8, 0, 0);
  Crumble *c = ST(e, Crumble);
  c->n = (uint8_t)(w / 8 > CRUMBLE_MAX ? CRUMBLE_MAX : w / 8);
  char p[48];
  path2(p, "objects/crumbleBlock/", area_crumble(), -1);
  c->tex = tex_by_name(p);
  c->outline = tex_by_name("objects/crumbleBlock/outline");
  c->fade_t = 1;
  for (int i = 0; i < c->n; i++) c->order[i] = (uint8_t)i;
  for (int i = c->n - 1; i > 0; i--) {
    int j = rndi(i + 1);
    uint8_t t = c->order[i];
    c->order[i] = c->order[j], c->order[j] = t;
  }
}

/* ---------------------------------------------------------------- ZipMover */
typedef struct {
  V2 start, target;
  float percent, at, wait;
  int step, light;
  uint16_t cog, block, light_tex[4], inner[12];
  int ninner;
} Zip;

static void zip_update(Ent *e) {
  Zip *z = ST(e, Zip);
  plat_update(e);
  switch (z->step) {
    case 0:
      if (!solid_has_player_rider(e)) break;
      plat_start_shaking(e, 0.1f);
      z->wait = 0.1f;
      z->step = 1;
      break;
    case 1:
      if ((z->wait -= DT) > 0) break;
      z->light = 3;
      z->at = 0;
      z->step = 2;
      break;
    case 2:   /* yield null, then move */
      z->at = approach(z->at, 1, 2 * DT);
      z->percent = ease_sine_in(z->at);
      plat_move_to(e, lerpf(z->start.x, z->target.x, z->percent), lerpf(z->start.y, z->target.y, z->percent));
      if (z->at >= 1) {
        plat_start_shaking(e, 0.2f);
        level_shake(.3f);
        z->wait = 0.5f;
        z->step = 3;
      }
      break;
    case 3:
      if ((z->wait -= DT) > 0) break;
      z->light = 2;
      z->at = 0;
      z->step = 4;
      break;
    case 4:
      z->at = approach(z->at, 1, 0.5f * DT);
      z->percent = 1 - ease_sine_in(z->at);
      plat_move_to(e, lerpf(z->target.x, z->start.x, ease_sine_in(z->at)), lerpf(z->target.y, z->start.y, ease_sine_in(z->at)));
      if (z->at >= 1) {
        plat_start_shaking(e, 0.2f);
        z->light = 1;
        z->wait = 0.5f;
        z->step = 5;
      }
      break;
    case 5:
      if ((z->wait -= DT) > 0) break;
      z->step = 0;
      break;
  }
}
static float fmod1(float x) { return x - floorf(x); }
static void zip_render(Ent *e) {
  Zip *z = ST(e, Zip);
  float x = e->x + e->shakex, y = e->y + e->shakey, w = e->cw, h = e->ch;
  gfx_rect(x + 1, y + 1, w - 2, h - 2, 0, 255);
  int num = 1;
  float num2 = 0;
  for (int i = 4; i <= h - 4; i += 8) {
    int num3 = num;
    for (int j = 4; j <= w - 4; j += 8) {
      int idx = (int)(fmod1((num2 + num * z->percent * PI_F * 4) / (PI_F / 2)) * z->ninner);
      if (idx >= z->ninner) idx = z->ninner - 1;
      uint16_t t = z->inner[idx];
      int sx = 0, sy = 0, sw = 12, sh = 12;
      float zx = 0, zy = 0;
      if (j <= 4) zx = 2, sx = 2, sw -= 2;
      else if (j >= w - 4) zx = -2, sw -= 2;
      if (i <= 4) zy = 2, sy = 2, sh -= 2;
      else if (i >= h - 4) zy = -2, sh -= 2;
      /* DrawCentered of the sub-rect */
      gfx_tex_part(t, x + j + zx - sw / 2.f, y + i + zy - sh / 2.f, sx, sy, sw, sh, 0, num < 0 ? 0x8410 : 0xFFFF, 255);
      num = -num;
      num2 += PI_F / 3;
    }
    if (num3 == num) num = -num;
  }
  int cw = (int)(w / 8), ch = (int)(h / 8);
  for (int k = 0; k < cw; k++)
    for (int l = 0; l < ch; l++) {
      int a = k == 0 ? 0 : k != cw - 1 ? 1 : 2, b = l == 0 ? 0 : l != ch - 1 ? 1 : 2;
      if (a != 1 || b != 1) gfx_tex_part(z->block, x + k * 8, y + l * 8, a * 8, b * 8, 8, 8, 0, 0xFFFF, 255);
    }
  gfx_tex(z->light_tex[z->light], x + w / 2 - 4, y, 0, 0xFFFF, 255);
}

/* the path between the two places (ZipMoverPathRenderer, depth 5000) */
typedef struct { Ent *zip; } ZipPath;
static void zip_cogs(Zip *z, Ent *zip, V2 off, bool black) {
  V2 from = v2(z->start.x + zip->cw / 2, z->start.y + zip->ch / 2), to = v2(z->target.x + zip->cw / 2, z->target.y + zip->ch / 2);
  V2 d = v2norm(v2sub(to, from)), perp = v2(d.y, -d.x);
  V2 v1 = v2mul(perp, 3), v2_ = v2mul(perp, -4);
  uint16_t rope = black ? 0 : rgb(0x663931), light = black ? 0 : rgb(0x9B6157);
  gfx_line(from.x + v1.x + off.x, from.y + v1.y + off.y, to.x + v1.x + off.x, to.y + v1.y + off.y, rope, 255);
  gfx_line(from.x + v2_.x + off.x, from.y + v2_.y + off.y, to.x + v2_.x + off.x, to.y + v2_.y + off.y, rope, 255);
  float len = v2len(v2sub(to, from));
  for (float n = 4 - fmodf(z->percent * PI_F * 8, 4); n < len; n += 4) {
    V2 a = v2add(v2add(v2add(from, v1), perp), v2mul(d, n));
    V2 b = v2sub(v2add(to, v2_), v2mul(d, n));
    gfx_line(a.x + off.x, a.y + off.y, a.x + d.x * 2 + off.x, a.y + d.y * 2 + off.y, light, 255);
    gfx_line(b.x + off.x, b.y + off.y, b.x - d.x * 2 + off.x, b.y - d.y * 2 + off.y, light, 255);
  }
  float rot = z->percent * PI_F * 2;
  uint8_t f = black ? GF_SILHOUETTE : 0;
  gfx_tex_ex(z->cog, from.x + off.x, from.y + off.y, 6, 6, 1, 1, rot, black ? 0 : 0xFFFF, 255, f);
  gfx_tex_ex(z->cog, to.x + off.x, to.y + off.y, 6, 6, 1, 1, rot, black ? 0 : 0xFFFF, 255, f);
}
static void zippath_render(Ent *e) {
  Ent *zip = ST(e, ZipPath)->zip;
  if (!zip->cls) return;
  Zip *z = ST(zip, Zip);
  zip_cogs(z, zip, v2(0, 1), true);
  zip_cogs(z, zip, v2(0, 0), false);
  gfx_rect(floorf(zip->x + zip->shakex - 1), floorf(zip->y + zip->shakey - 1), zip->cw + 2, zip->ch + 2, 0, 255);
}
static const EntClass ZIPPATH = {.size = sizeof(ZipPath), .name = "zipPath", .render = zippath_render};
static const EntClass ZIP = {.size = sizeof(Zip), .name = "zipMover", .update = zip_update, .render = zip_render, .kind = KIND_SOLID};
static void new_zip(const EData *d) {
  Ent *e = ent_new(&ZIP, d->x, d->y);
  if (!e) return;
  ent_box(e, EA(d, zipMover, width), EA(d, zipMover, height), 0, 0);
  e->depth = -9999;
  Zip *z = ST(e, Zip);
  z->start = v2(d->x, d->y);
  z->target = ed_node(d, 0);
  z->light = 1;
  z->cog = tex_by_name("objects/zipmover/cog");
  z->block = tex_by_name("objects/zipmover/block");
  char p[40];
  for (int i = 0; i < 4; i++) path2(p, "objects/zipmover/light", NULL, i), z->light_tex[i] = tex_by_name(p);
  for (int i = 0; i < 12; i++) path2(p, "objects/zipmover/innercog", NULL, i), z->inner[i] = tex_by_name(p);
  z->ninner = 12;
  Ent *path = ent_new(&ZIPPATH, 0, 0);
  if (path) {
    path->depth = 5000;
    path->collidable = 0;
    ST(path, ZipPath)->zip = e;
  }
}

/* ---------------------------------------------------------------- tile blocks */
/* the tiles of blocks come from a pool, one half per room slot */
#define POOL_TILES 400
static TileQ tile_pool[2][POOL_TILES];
static int tile_pool_n[2];
static TileQ *pool_tiles(int slot, int n) {
  if (tile_pool_n[slot] + n > POOL_TILES) return NULL;
  TileQ *q = tile_pool[slot] + tile_pool_n[slot];
  tile_pool_n[slot] += n;
  return q;
}
typedef struct {
  char type;
  uint8_t w, h, mode;
  uint8_t flags;      /* fallingBlock: climbFall; dashBlock: permanent, canDash */
  int step;
  float timer, speed, alpha;
  bool triggered;
  uint32_t hash;
  TileQ *q;
} Block;

static void block_render(Ent *e) {
  Block *b = ST(e, Block);
  if (!b->q) return;
  tiles_draw(b->q, b->w, b->h, e->x + e->shakex, e->y + e->shakey, 0xFFFF, (uint8_t)(b->alpha * 255));
}

/* FallingBlock */
static bool fall_player_check(Ent *e, Block *b) { return (b->flags & 1) ? solid_has_player_rider(e) : solid_has_player_on_top(e); }
static bool fall_wait_check(Ent *e, Block *b) {
  if (b->triggered || fall_player_check(e, b)) return true;
  if (b->flags & 1) {
    Ent *p = level_player_ent();
    return p && (collide_ent_at(e, e->x - 1, e->y, p) || collide_ent_at(e, e->x + 1, e->y, p));
  }
  return false;
}
static void falling_update(Ent *e) {
  Block *b = ST(e, Block);
  plat_update(e);
  switch (b->step) {
    case 0:
      if (b->triggered || fall_player_check(e, b)) b->step = 1;
      break;
    case 1:   /* shake 0.2 s */
      plat_start_shaking(e, 0);
      b->timer = 0.2f;
      b->step = 2;
      break;
    case 2:
      if ((b->timer -= DT) > 0) break;
      b->timer = 0.4f;
      b->step = 3;
      /* fall through */
    case 3:
      if (b->timer > 0 && fall_wait_check(e, b)) {
        b->timer -= DT;
        break;
      }
      plat_stop_shaking(e);
      for (int i = 2; i < e->cw; i += 4) particles_emit(PL_MID, &P_FallingBlock_P_FallDustB, 2, v2(e->x + i, e->y), v2(4, 4), PI_F / 2);
      b->speed = 0;
      b->step = 4;
      break;
    case 4: {
      Room *rm = g_level.room;
      b->speed = approach(b->speed, 160, 500 * DT);
      if (plat_move_v_collide_solids(e, b->speed * DT, true)) {
        level_dir_shake(v2(0, 1), 0.3f);
        plat_start_shaking(e, 0);
        b->timer = 0.2f;
        b->step = 5;
        break;
      }
      if (e_top(e) > rm->y + rm->h + 16 || (e_top(e) > rm->y + rm->h - 1 && collide_solid(e, e->x, e->y + 1))) {
        e->collidable = e->visible = 0;
        plat_static_movers_destroy(e);
        ent_remove(e);
      }
      break;
    }
    case 5:
      if ((b->timer -= DT) > 0) break;
      plat_stop_shaking(e);
      if (tiles_solid_rect(e_left(e), e_bottom(e), e_right(e), e_bottom(e) + 1)) {
        b->step = 7;
        e->safe = 1;
        break;
      }
      b->step = 6;
      b->timer = 0;
      /* fall through */
    case 6:
      if ((b->timer -= DT) > 0) break;
      if (collide_solid(e, e->x, e->y + 1) || collide_first_jumpthru_outside(e, e->x, e->y + 1)) {
        b->timer = 0.1f;
        break;
      }
      b->step = 1;
      break;
    default: break;
  }
}
static void falling_trigger(Ent *e, Ent *m) {
  (void)m;
  ST(e, Block)->triggered = true;
}
static void block_shake(Ent *e, V2 a) { plat_static_movers_shake(e, a); }
static const EntClass FALLING = {.size = sizeof(Block), .name = "fallingBlock", .update = falling_update, .render = block_render,
                                 .on_staticmover_trigger = falling_trigger, .on_shake = block_shake, .kind = KIND_SOLID};

static Ent *new_block(const EntClass *cls, const EData *d, float w, float h, char type) {
  Ent *e = ent_new(cls, d->x, d->y);
  if (!e) return NULL;
  ent_box(e, w, h, 0, 0);
  Block *b = ST(e, Block);
  b->type = type;
  b->w = (uint8_t)(w / 8), b->h = (uint8_t)(h / 8);
  b->alpha = 1;
  b->hash = level_entity_hash(d);
  b->q = pool_tiles(g_level.room_slot, b->w * b->h);
  if (b->q) tiles_box(type, b->w, b->h, b->q);
  return e;
}
/* data.Char("tiletype", '3'): a number in some maps, a letter in others (data.bin keeps those as text) */
static char tile_attr(const EData *d, int off, char kind) {
  if (kind == 's') {
    const char *s = ea_str(d, off);
    return *s ? s[0] : '3';
  }
  return (char)('0' + (int)ea_num(d, off, kind));
}
#define TILE_ATTR(d, ent, attr) tile_attr(d, EA_##ent##_##attr, EK_##ent##_##attr)

/* DashBlock */
static int dash_block_hit(Ent *e, Player *p, V2 dir);
static const EntClass DASHBLOCK = {.size = sizeof(Block), .name = "dashBlock", .update = plat_update, .render = block_render,
                                   .on_dash_collide = dash_block_hit, .kind = KIND_SOLID};
void level_break_dash_block(Ent *e, V2 dir) {
  (void)dir;
  Block *b = ST(e, Block);
  for (int i = 0; i < b->w; i++)
    for (int j = 0; j < b->h; j++) level_debris(e->x + 4 + i * 8, e->y + 4 + j * 8, b->type, player_center(&g_player));
  e->collidable = 0;
  if (b->flags & 1) level_set_do_not_load(b->hash);
  ent_remove(e);
}
static int dash_block_hit(Ent *e, Player *p, V2 dir) {
  Block *b = ST(e, Block);
  if (!(b->flags & 2) && p->state != ST_REDDASH && p->state != ST_SUMMITLAUNCH) return DASH_NORMAL;
  level_break_dash_block(e, dir);
  return DASH_REBOUND;
}

/* FakeWall, CoverupWall, ExitBlock: tiles that fade */
static void fakewall_update(Ent *e) {
  Block *b = ST(e, Block);
  Ent *p = level_player_ent();
  if (b->mode == 1) {   /* fading */
    b->alpha = approach(b->alpha, 0, 2 * DT);
    if (b->alpha <= 0) ent_remove(e);
    return;
  }
  if (p && collide_ent_at(e, e->x, e->y, p) && g_player.state != ST_DREAMDASH) {
    level_set_do_not_load(b->hash);
    b->mode = 1;
  }
}
static const EntClass FAKEWALL = {.size = sizeof(Block), .name = "fakeWall", .update = fakewall_update, .render = block_render};
static void coverup_update(Ent *e) {
  Block *b = ST(e, Block);
  Ent *p = level_player_ent();
  bool in = p && collide_ent_at(e, e->x, e->y, p);
  b->alpha = approach(b->alpha, in ? 0 : 1, 4 * DT);
}
static const EntClass COVERUP = {.size = sizeof(Block), .name = "coverupWall", .update = coverup_update, .render = block_render};
static void exit_update(Ent *e) {
  Block *b = ST(e, Block);
  Ent *p = level_player_ent();
  if (!e->collidable) {
    e->collidable = 1;
    bool in = p && collide_ent_at(e, e->x, e->y, p);
    e->collidable = !in;
    if (!in) b->mode = 1;
  }
  if (b->mode == 1) b->alpha = approach(b->alpha, 1, 2 * DT);
  plat_update(e);
}
static const EntClass EXITBLOCK = {.size = sizeof(Block), .name = "exitBlock", .update = exit_update, .render = block_render, .kind = KIND_SOLID};

/* Debris: bits of broken blocks */
typedef struct { uint16_t tex; V2 speed; float life, rot, spin; } Debris;
static void debris_update(Ent *e) {
  Debris *d = ST(e, Debris);
  d->speed.y = approach(d->speed.y, 200, 600 * DT);
  d->speed.x = approach(d->speed.x, 0, 200 * DT);
  e->x += d->speed.x * DT, e->y += d->speed.y * DT;
  d->rot += d->spin * DT;
  if ((d->life -= DT) <= 0) ent_remove(e);
}
static void debris_render(Ent *e) {
  Debris *d = ST(e, Debris);
  gfx_tex_ex(d->tex, e->x, e->y, 4, 4, 1, 1, d->rot, 0xFFFF, (uint8_t)(255 * fminf(1, d->life * 2)), 0);
}
static const EntClass DEBRIS = {.size = sizeof(Debris), .name = "debris", .update = debris_update, .render = debris_render};
void level_debris(float x, float y, char type, V2 from) {
  Ent *e = ent_new(&DEBRIS, x, y);
  if (!e) return;
  e->collidable = 0;
  e->depth = -9000;
  Debris *d = ST(e, Debris);
  char p[16], t[2] = {type, 0};
  path2(p, "debris/", t, -1);
  d->tex = tex_by_name(p);
  if (d->tex == 0xFFFF) d->tex = tex_by_name("debris/1");
  V2 dir = v2norm(v2sub(v2(x, y), from));
  float sp = rnd_rangef(60, 120);
  d->speed = v2add(v2mul(dir, sp), v2(0, -60));
  d->life = rnd_rangef(1, 1.5f);
  d->spin = rnd_rangef(-6, 6);
}

/* ---------------------------------------------------------------- simple scenery */
typedef struct { Sprite spr; float t; uint16_t tex; uint8_t flag; } Simple;
static void simple_update(Ent *e) {
  Simple *s = ST(e, Simple);
  s->t += DT;
  spr_update(&s->spr);
}
static void simple_render(Ent *e) { spr_draw(&ST(e, Simple)->spr, e->x, e->y); }
static const EntClass SIMPLE_SPRITE = {.size = sizeof(Simple), .name = "sprite", .update = simple_update, .render = simple_render};
static Ent *new_sprite_ent(const EData *d, int bank, int anim, int depth) {
  Ent *e = ent_new(&SIMPLE_SPRITE, d->x, d->y);
  if (!e) return NULL;
  e->collidable = 0;
  e->depth = (int16_t)depth;
  spr_init(&ST(e, Simple)->spr, bank);
  if (anim >= 0) spr_play(&ST(e, Simple)->spr, anim, true);
  return e;
}

/* Checkpoint's background picture */
static void checkpoint_render(Ent *e) { gfx_tex_ex(ST(e, Simple)->tex, e->x, e->y, 12, 24, 1, 1, 0, 0xFFFF, 255, 0); }
static const EntClass CHECKPOINT = {.size = sizeof(Simple), .name = "checkpoint", .render = checkpoint_render};

/* LightBeam */
typedef struct { float w, h, rot, alpha, t; } Beam;
static void beam_update(Ent *e) {
  Beam *b = ST(e, Beam);
  b->t += DT;
  Ent *p = level_player_ent();
  float target = 1;
  if (p) {
    float d = fabsf(p->x - e->x);
    target = clampf((d - 8) / 32, 0, 1) * 0 + 1;
  }
  b->alpha = approach(b->alpha, target, DT);
}
static void beam_render(Ent *e) {
  Beam *b = ST(e, Beam);
  float a = b->alpha * 0.5f;
  /* stripes of util/lightbeam stretched along the beam */
  for (float x = 0; x < b->w; x += 4) {
    float k = sinf(b->t * 2 + x * 0.2f) * 0.25f + 0.75f;
    float px = e->x + cosf(b->rot + PI_F / 2) * 0 + cosf(b->rot) * (x - b->w / 2);
    float py = e->y + sinf(b->rot) * (x - b->w / 2);
    gfx_tex_ex(T_util_lightbeam, px, py, 0, 0.5f, b->h / 80.f, 4, b->rot + PI_F / 2, 0xFFF7, (uint8_t)(a * k * 120), GF_ADD);
  }
}
static const EntClass BEAM = {.size = sizeof(Beam), .name = "lightbeam", .update = beam_update, .render = beam_render};

/* ---------------------------------------------------------------- triggers */
typedef struct { V2 a; float b; int mode; bool xonly, yonly; char kind; } Trig;
static void camoff_enter(Ent *e, Player *p) {
  (void)p;
  Trig *t = ST(e, Trig);
  g_level.cam_offset = v2(t->a.x * 48, t->a.y * 32);
}
static float pos_lerp(Ent *e, Player *p, int mode) {
  float px = e_cxm(p->ent), py = e_cym(p->ent);
  float l = e_left(e), r = e_right(e), t = e_top(e), b = e_bottom(e), cx = e_cxm(e), cy = e_cym(e);
#define CMAP(v, a, bb) clampf(((v) - (a)) / ((bb) - (a)), 0, 1)
  switch (mode) {
    case 1: return CMAP(px, l, r);
    case 2: return CMAP(px, r, l);
    case 3: return CMAP(py, t, b);
    case 4: return CMAP(py, b, t);
    case 5: return fminf(CMAP(px, l, cx), CMAP(px, r, cx));
    case 6: return fminf(CMAP(py, t, cy), CMAP(py, b, cy));
    default: return 1;
  }
#undef CMAP
}
static void camtarget_stay(Ent *e, Player *p) {
  Trig *t = ST(e, Trig);
  p->cam_anchor = v2sub(t->a, v2(160, 90));
  float l = clampf(t->b * pos_lerp(e, p, t->mode), 0, 1);
  p->cam_anchor_lerp = v2(l, l);
  p->cam_anchor_ignore_x = t->yonly;
  p->cam_anchor_ignore_y = t->xonly;
}
static void camtarget_leave(Ent *e, Player *p) {
  (void)e;
  bool other = false;
  for (int i = 0; i < g_nents; i++) {
    Ent *o = &g_ents[i];
    if (o != e && o->cls && o->cls->on_stay == camtarget_stay && o->triggered) other = true;
  }
  if (!other) {
    p->cam_anchor_lerp = v2(0, 0);
  }
}
static void respawn_enter(Ent *e, Player *p) {
  (void)p;
  Trig *t = ST(e, Trig);
  if (!point_solid(t->a.x, t->a.y - 4)) {
    g_session.rx = (int16_t)t->a.x, g_session.ry = (int16_t)t->a.y;
  }
}
static const EntClass CAMOFF = {.size = sizeof(Trig), .name = "cameraOffsetTrigger", .on_enter = camoff_enter, .kind = KIND_TRIGGER};
static const EntClass CAMTARGET = {.size = sizeof(Trig), .name = "cameraTargetTrigger", .on_stay = camtarget_stay, .on_leave = camtarget_leave, .kind = KIND_TRIGGER};
static const EntClass RESPAWN = {.size = sizeof(Trig), .name = "changeRespawnTrigger", .on_enter = respawn_enter, .kind = KIND_TRIGGER};
static const EntClass KILLBOX = {.size = sizeof(int), .name = "killbox", .kind = KIND_KILLBOX};

/* HeartGem.removeCameraTriggers: every CameraOffsetTrigger goes */
void level_remove_camera_offset_triggers(void) {
  for (int i = 0; i < g_nents; i++)
    if (g_ents[i].cls == &CAMOFF && g_ents[i].dead != 1) ent_remove(&g_ents[i]);
}

static Ent *new_trigger(const EntClass *cls, const EData *d, float w, float h) {
  Ent *e = ent_new(cls, d->x, d->y);
  if (!e) return NULL;
  ent_box(e, w, h, 0, 0);
  e->visible = 0;
  return e;
}

bool trig_create(const EData *d) {
  Ent *e;
  if (berry_trigger(d)) return true;
  if (trigs_ch1(d) || trigs_ch2(d) || trigs_ch3(d) || trigs_ch4(d) || trigs_ch5(d) || trigs_ch6(d) || trigs_ch7(d) ||
      trigs_ch8(d) || trigs_ch9(d))
    return true;
  switch (d->type) {
    case TT_cameraOffsetTrigger:
      e = new_trigger(&CAMOFF, d, EA(d, cameraOffsetTrigger, width), EA(d, cameraOffsetTrigger, height));
      if (e) ST(e, Trig)->a = v2(EA(d, cameraOffsetTrigger, cameraX), EA(d, cameraOffsetTrigger, cameraY));
      return true;
    case TT_cameraTargetTrigger: {
      e = new_trigger(&CAMTARGET, d, EA(d, cameraTargetTrigger, width), EA(d, cameraTargetTrigger, height));
      if (!e) return true;
      Trig *t = ST(e, Trig);
      t->a = ed_node(d, 0);
      t->b = EA(d, cameraTargetTrigger, lerpStrength);
      const char *m = EAS(d, cameraTargetTrigger, positionMode);
      static const char *modes[] = {"NoEffect", "LeftToRight", "RightToLeft", "TopToBottom", "BottomToTop", "HorizontalCenter", "VerticalCenter"};
      t->mode = 0;
      for (int i = 0; i < 7; i++)
        if (!strcmp(m, modes[i])) t->mode = i;
      t->xonly = EAB(d, cameraTargetTrigger, xOnly);
      t->yonly = EAB(d, cameraTargetTrigger, yOnly);
      return true;
    }
    case TT_changeRespawnTrigger: {
      e = new_trigger(&RESPAWN, d, EA(d, changeRespawnTrigger, width), EA(d, changeRespawnTrigger, height));
      if (!e) return true;
      V2 target = d->nnodes ? ed_node(d, 0) : e_center(e);
      ST(e, Trig)->a = level_closest_spawn(target);
      if (d->nnodes) ST(e, Trig)->a = target;
      return true;
    }
  }
  return false;
}

/* ---------------------------------------------------------------- the factory */
static uint16_t tex_by_name(const char *path) { return res_tex_by_name(path); }

/* each chapter's own entities (src/chN.c); absent files: none */
WEAK bool ents_ch0(const EData *d) { (void)d; return false; }
WEAK bool ents_ch1(const EData *d) { (void)d; return false; }
WEAK bool ents_ch8(const EData *d) { (void)d; return false; }
WEAK bool trigs_ch1(const EData *d) { (void)d; return false; }
WEAK bool trigs_ch8(const EData *d) { (void)d; return false; }
WEAK bool ents_ch2(const EData *d) { (void)d; return false; }
WEAK bool ents_ch3(const EData *d) { (void)d; return false; }
WEAK bool ents_ch4(const EData *d) { (void)d; return false; }
WEAK bool ents_ch5(const EData *d) { (void)d; return false; }
WEAK bool ents_ch6(const EData *d) { (void)d; return false; }
WEAK bool ents_ch7(const EData *d) { (void)d; return false; }
WEAK bool ents_ch9(const EData *d) { (void)d; return false; }
WEAK bool trigs_ch2(const EData *d) { (void)d; return false; }
WEAK bool trigs_ch3(const EData *d) { (void)d; return false; }
WEAK bool trigs_ch4(const EData *d) { (void)d; return false; }
WEAK bool trigs_ch5(const EData *d) { (void)d; return false; }
WEAK bool trigs_ch6(const EData *d) { (void)d; return false; }
WEAK bool trigs_ch7(const EData *d) { (void)d; return false; }
WEAK bool trigs_ch9(const EData *d) { (void)d; return false; }

bool ent_create(const EData *d) {
  if (berry_create(d) || props_create(d) || heart_create(d) || cassette_create(d) || dust_create(d)) return true;
  if (ents_ch0(d) || ents_ch1(d) || ents_ch2(d) || ents_ch3(d) || ents_ch4(d) || ents_ch5(d) || ents_ch6(d) ||
      ents_ch7(d) || ents_ch8(d) || ents_ch9(d))
    return true;
  int slot = g_level.room_slot;
  switch (d->type) {
    case ET_player:
      if (nspawns[slot] < MAX_SPAWNS) spawns[slot][nspawns[slot]++] = v2(d->x, d->y);
      return true;
    case ET_jumpThru: new_jumpthru(d); return true;
    case ET_spikesUp: new_spikes(d, DIR_UP); return true;
    case ET_spikesDown: new_spikes(d, DIR_DOWN); return true;
    case ET_spikesLeft: new_spikes(d, DIR_LEFT); return true;
    case ET_spikesRight: new_spikes(d, DIR_RIGHT); return true;
    case ET_spring: new_spring(d, SPRING_FLOOR, EAB(d, spring, playerCanUse)); return true;
    case ET_wallSpringLeft: new_spring(d, SPRING_WALLLEFT, EAB(d, wallSpringLeft, playerCanUse)); return true;
    case ET_wallSpringRight: new_spring(d, SPRING_WALLRIGHT, EAB(d, wallSpringRight, playerCanUse)); return true;
    case ET_refill: new_refill(d); return true;
    case ET_crumbleBlock: new_crumble(d); return true;
    case ET_zipMover: new_zip(d); return true;
    case ET_fallingBlock: {
      Ent *e = new_block(&FALLING, d, EA(d, fallingBlock, width), EA(d, fallingBlock, height),
                         TILE_ATTR(d, fallingBlock, tiletype));
      if (!e) return true;
      if (EAB(d, fallingBlock, behind)) e->depth = 5000;
      ST(e, Block)->flags = EAB(d, fallingBlock, climbFall) ? 1 : 0;
      return true;
    }
    case ET_dashBlock: {
      char t = TILE_ATTR(d, dashBlock, tiletype);
      Ent *e = new_block(&DASHBLOCK, d, EA(d, dashBlock, width), EA(d, dashBlock, height), t);
      if (!e) return true;
      e->depth = -12999;
      e->safe = 1;
      Block *b = ST(e, Block);
      b->flags = (EAB(d, dashBlock, permanent) ? 1 : 0) | (EAB(d, dashBlock, canDash) ? 2 : 0);
      if (EAB(d, dashBlock, blendin)) {
        e->depth = -10501;
        if (b->q) tiles_overlay(t, (int)(d->x / 8), (int)(d->y / 8), b->w, b->h, b->q);
      }
      return true;
    }
    case ET_fakeWall: {
      char t = TILE_ATTR(d, fakeWall, tiletype);
      Ent *e = new_block(&FAKEWALL, d, EA(d, fakeWall, width), EA(d, fakeWall, height), t);
      if (!e) return true;
      e->depth = -13000;
      Block *b = ST(e, Block);
      if (b->q) tiles_overlay(t, (int)(d->x / 8), (int)(d->y / 8), b->w, b->h, b->q);
      return true;
    }
    case ET_coverupWall: {
      char t = TILE_ATTR(d, coverupWall, tiletype);
      Ent *e = new_block(&COVERUP, d, EA(d, coverupWall, width), EA(d, coverupWall, height), t);
      if (!e) return true;
      e->depth = -13000;
      Block *b = ST(e, Block);
      if (b->q) tiles_overlay(t, (int)(d->x / 8), (int)(d->y / 8), b->w, b->h, b->q);
      return true;
    }
    case ET_exitBlock: {
      char t = TILE_ATTR(d, exitBlock, tileType);
      Ent *e = new_block(&EXITBLOCK, d, EA(d, exitBlock, width), EA(d, exitBlock, height), t);
      if (!e) return true;
      e->depth = -13000;
      e->collidable = 0;
      Block *b = ST(e, Block);
      b->alpha = 0;
      if (b->q) tiles_overlay(t, (int)(d->x / 8), (int)(d->y / 8), b->w, b->h, b->q);
      return true;
    }
    case ET_killbox: {
      Ent *e = ent_new(&KILLBOX, d->x, d->y);
      if (e) ent_box(e, EA(d, killbox, width), 32, 0, 0), e->visible = 0;
      return true;
    }
    case ET_checkpoint: {   /* Level.LoadLevel: not falling into a room (but the first) */
      if (g_level.last_intro == INTRO_FALL && strcmp(level_room_name(), "0")) return true;
      g_level.start_position = ed_node(d, 0), g_level.has_start_position = true;   /* + SpawnOffset */
      Ent *e = ent_new(&CHECKPOINT, d->x, d->y);
      if (!e) return true;
      e->depth = 9990;
      e->collidable = 0;
      const char *bg = EAS(d, checkpoint, bg);
      char p[48];
      char num[4] = {(char)('0' + g_session.area), 0};
      path2(p, "objects/checkpoint/bg/", *bg ? bg : num, -1);
      ST(e, Simple)->tex = tex_by_name(p);
      return true;
    }
    case ET_bonfire: {
      const char *mode = EAS(d, bonfire, mode);
      Ent *e = new_sprite_ent(d, SB_campfire, !strcmp(mode, "Lit") ? A_campfire_burn : !strcmp(mode, "Smoking") ? A_campfire_smoking : A_campfire_idle, -5);
      (void)e;
      return true;
    }
    case ET_lightbeam: {
      Ent *e = ent_new(&BEAM, d->x, d->y);
      if (!e) return true;
      e->collidable = 0;
      e->depth = -9998;
      Beam *b = ST(e, Beam);
      b->w = EA(d, lightbeam, width), b->h = EA(d, lightbeam, height);
      b->rot = EA(d, lightbeam, rotation) * PI_F / 180;
      b->alpha = 1;
      return true;
    }
  }
  return false;
}

void entities_load(Room *rm, const uint8_t *blob, int slot) {
  nspawns[slot] = 0;
  tile_pool_n[slot] = 0;
  EData d;
  d.room = rm->index;
  d.rx = (float)rm->x, d.ry = (float)rm->y;
  for (int pass = 0; pass < 2; pass++) {
    const uint8_t *p = blob + rd16(blob + 2 * (pass ? RB_TRIGS : RB_ENTS));
    int n = rd16(p);
    p += 2;
    for (int i = 0; i < n; i++) {
      d.type = p[0];
      d.nnodes = p[1];
      d.id = rd16(p + 2);
      d.x = rm->x + (float)rds16(p + 4);
      d.y = rm->y + (float)rds16(p + 6);
      d.attrs = p + 8;
      int size = pass ? trig_attr_size(d.type) : ent_attr_size(d.type);
      d.nodes = d.attrs + size;
      g_new_eid = (uint16_t)d.id;
      d.trig = pass;
      uint32_t id = level_entity_hash(&d);
      if (!level_do_not_load(id) && !leader_has_id(id)) {   /* what follows the player is there already */
        if (pass) trig_create(&d);
        else ent_create(&d);
      }
      g_new_eid = 0xFFFF;
      p = d.nodes + 4 * d.nnodes;
      if ((uintptr_t)(p - blob) & 1) p++;
    }
  }
  /* what lives as long as the level */
  bool have = false;
  for (int i = 0; i < g_nents; i++)
    if (g_ents[i].cls == &FX) have = true;
  if (!have) {
    Ent *e = ent_new(&FX, 0, 0);
    if (e) e->tags = TAG_GLOBAL | TAG_TRANSITION_UPDATE, e->collidable = 0, e->visible = 0, e->dead = 0;
    e = ent_new(&FORMATION, 0, 0);
    if (e) e->tags = TAG_GLOBAL | TAG_FROZEN_UPDATE, e->depth = -1999900, e->collidable = 0, e->dead = 0;
  }
}

int ent_attr_size(int type) { return type < (int)sizeof ENT_ATTR_SIZE ? ENT_ATTR_SIZE[type] : 0; }
int trig_attr_size(int type) { return type < (int)sizeof TRIG_ATTR_SIZE ? TRIG_ATTR_SIZE[type] : 0; }
