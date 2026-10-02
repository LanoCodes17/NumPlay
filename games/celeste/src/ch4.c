#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("Os")   /* not drawn every frame: smaller over faster */
#endif
/* Golden Ridge (chapter 4), ported from the game's classes: boosters (the
 * green and red bubbles), move blocks (MoveBlock and its debris), clouds,
 * ridge gates, the white block, the gondola, the cliffside flags (CliffsideWindFlag,
 * CliffFlags), and the wind: WindController (which also does the player's
 * WindMove), WindTrigger, WindAttackTrigger and its Snowball. The story: Granny
 * (NPC04_Granny, CS04_Granny), Theo and the gondola's ride (NPC04_Theo, CS04_Gondola) with its
 * breathing (BreathingMinigame). */
#include <stdlib.h>
#include "npc.h"
#include "talk.h"

/* the story's shared pieces (ch1.c, ch2.c) */
void ch1_talk_add(Ent *owner, Talk *t, int x, int y, int w, int h, V2 draw_at);
void ch1_talk_remove(Ent *owner);
void ch1_flash(uint32_t color);
typedef struct { V2 target; float speed, alpha; int8_t turn; uint8_t step, flags, pad; } Move;
bool ch1_move_to(Ent *e, Sprite *s, Move *m, float maxspeed, int move_anim, int idle_anim);
Ent *ch2_bdummy_new(V2 at);
void ch2_bdummy_appear(Ent *e);
void ch2_bdummy_vanish(Ent *e);
void ch2_bdummy_floatness(Ent *e, float f);
Ent *ch2_selfie_new(int mode);
bool ch2_selfie_running(Ent *e);

/* ---------------------------------------------------------------- helpers */
static uint16_t tex(const char *p) { return res_tex_by_name(p); }
static const Room *room_ptr(const Ent *e) { return &g_level.rooms[e->room]; }
static int16_t ent_ref(const Ent *e) { return e ? (int16_t)(e - g_ents) : -1; }
static Ent *ent_at(int16_t i, const EntClass *cls) { return i >= 0 && g_ents[i].cls == cls && g_ents[i].dead != 1 ? &g_ents[i] : NULL; }
static float quad_curve(float a, float c, float b, float t) { return (1 - t) * (1 - t) * a + 2 * (1 - t) * t * c + t * t * b; }
static V2 curve_point(V2 a, V2 b, V2 c, float t) { return v2(quad_curve(a.x, c.x, b.x, t), quad_curve(a.y, c.y, b.y, t)); }
static uint32_t hash32(uint32_t x) {
  x ^= x >> 16, x *= 0x7feb352du, x ^= x >> 15, x *= 0x846ca68bu, x ^= x >> 16;
  return x;
}
static uint32_t lerp_color(uint32_t a, uint32_t b, float t) {   /* Color.Lerp, on bytes */
  t = clampf(t, 0, 1);
  int r = (int)(((a >> 16) & 255) + (((int)((b >> 16) & 255) - (int)((a >> 16) & 255)) * t));
  int g = (int)(((a >> 8) & 255) + (((int)((b >> 8) & 255) - (int)((a >> 8) & 255)) * t));
  int bl = (int)((a & 255) + (((int)(b & 255) - (int)(a & 255)) * t));
  return (uint32_t)(r << 16 | g << 8 | bl);
}

/* drawing straight into a strip (gfx_custom layers): view coordinates */
static inline void sp_plot(uint16_t *strip, int sy0, int sy1, int x, int y, uint16_t c, int a) {
  if (y < sy0 || y >= sy1 || (unsigned)x >= VIEW_W) return;
  uint16_t *d = strip + (y - sy0) * VIEW_W + x;
  *d = a >= 256 ? c : blend565(*d, scale565(c, a), a);
}
static void sp_rect(uint16_t *strip, int sy0, int sy1, int x, int y, int w, int h, uint16_t c, int a) {
  int x0 = x < 0 ? 0 : x, x1 = x + w > VIEW_W ? VIEW_W : x + w;
  int y0 = y < sy0 ? sy0 : y, y1 = y + h > sy1 ? sy1 : y + h;
  if (x0 >= x1 || y0 >= y1 || a <= 0) return;
  uint16_t pc = a >= 256 ? c : scale565(c, a);
  for (int r = y0; r < y1; r++) {
    uint16_t *d = strip + (r - sy0) * VIEW_W + x0;
    if (a >= 256)
      for (int i = x0; i < x1; i++) *d++ = c;
    else
      for (int i = x0; i < x1; i++, d++) *d = blend565(*d, pc, a);
  }
}
static void sp_line(uint16_t *strip, int sy0, int sy1, int x0, int y0, int x1, int y1, uint16_t c, int a) {
  if ((y0 < sy0 && y1 < sy0) || (y0 >= sy1 && y1 >= sy1)) return;
  int dx = abs(x1 - x0), dy = -abs(y1 - y0), sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, err = dx + dy;
  for (int guard = 0; guard < 2048; guard++) {
    sp_plot(strip, sy0, sy1, x0, y0, c, a);
    if (x0 == x1 && y0 == y1) break;
    int e2 = 2 * err;
    if (e2 >= dy) err += dy, x0 += sx;
    if (e2 <= dx) err += dx, y0 += sy;
  }
}
static inline int vx(float x) { return (int)floorf(x + 0.5f) - g_camx; }
static inline int vy(float y) { return (int)floorf(y + 0.5f) - g_camy; }
enum { OFF_rowbuf = 0, END_rowbuf = OFF_rowbuf + (int)sizeof(uint8_t[256]) };
#define rowbuf (*(uint8_t(*)[256])(void *)(g_chram[4] + OFF_rowbuf))
static void tex_row_idx(const Tex *t, int r, uint8_t *out) { tex_row(t, r, out); }
static inline void sp_texel(uint16_t *d, const uint16_t *col, const uint8_t *al, uint8_t v, uint16_t tint, int ga, bool sil) {
  int aa = al[v - 1] + (al[v - 1] >> 7);
  uint16_t c = sil ? (aa >= 256 ? tint : scale565(tint, aa)) : tint != 0xFFFF ? mul565(col[v - 1], tint) : col[v - 1];
  if (ga < 256) c = scale565(c, ga), aa = (aa * ga) >> 8;
  *d = aa >= 256 ? c : blend565(*d, c, aa);
}
static bool layer_once(uint32_t *mark) {
  if (*mark == g_frame + 1) return false;
  *mark = g_frame + 1;
  return true;
}

/* ---------------------------------------------------------------- Booster */
typedef struct {
  Sprite spr;
  Wiggler wig;
  V2 spr_pos, dir;
  float respawn, cannot_use;
  int16_t outline;
  uint8_t red, boosting, routine, was_current, leaving;
} Booster;
static const EntClass BOOSTER, BOOSTER_OUTLINE;
/* (the sprites exist in data.bin when some map's rooms ask for them: hence the #ifdefs) */
static int booster_anim(const Booster *b, int which) {   /* 0 loop, 1 inside, 2 spin, 3 pop */
#ifdef SB_boosterRed
  if (b->red)
    return which == 0 ? A_boosterRed_loop : which == 1 ? A_boosterRed_inside : which == 2 ? A_boosterRed_spin : A_boosterRed_pop;
#endif
#ifdef SB_booster
  if (!b->red) return which == 0 ? A_booster_loop : which == 1 ? A_booster_inside : which == 2 ? A_booster_spin : A_booster_pop;
#endif
  (void)b, (void)which;
  return 0;
}
static int booster_bank(bool red) {
#ifdef SB_boosterRed
  if (red) return SB_boosterRed;
#endif
#ifdef SB_booster
  if (!red) return SB_booster;
#endif
  (void)red;
  return -1;
}
static void booster_appear_particles(Ent *e, Booster *b) {
  for (int i = 0; i < 360; i += 30)
    particles_emit(PL_BG, b->red ? &P_Booster_P_RedAppear : &P_Booster_P_Appear, 1, v2(e->x, e->y + 2), v2(2, 2), i * (PI_F / 180));
}
static void booster_released(Ent *e, Booster *b) {   /* PlayerReleased */
  (void)e;
  spr_play(&b->spr, booster_anim(b, 3), false);
  b->cannot_use = 0;
  b->respawn = 1;
  b->boosting = 0;
  b->wig.active = false;
}
static void booster_on_player(Ent *e, Player *p) {
  Booster *b = ST(e, Booster);
  if (b->respawn <= 0 && b->cannot_use <= 0 && !b->boosting) {
    b->cannot_use = 0.45f;
    player_boost(p, e, b->red);
    p->boost_target = v2(e->x, e->y + 2);   /* Booster.Center: its Circle(10) is at (0, 2) */
    wiggler_restart(&b->wig);
    spr_play(&b->spr, booster_anim(b, 1), false);
    b->spr.flipx = p->facing == -1;
  }
}
static void booster_on_dash(Ent *e, V2 dir) {   /* DashListener */
  (void)dir;
  Booster *b = ST(e, Booster);
  if (b->boosting) b->boosting = 0;
}
static void booster_boosted(Ent *e, Booster *b, Player *p) {   /* PlayerBoosted, from the player's dash */
  b->boosting = 1;
  e->tags = TAG_PERSISTENT | TAG_TRANSITION_UPDATE;
  spr_play(&b->spr, booster_anim(b, 2), false);
  b->spr.flipx = p->facing == -1;
  Ent *o = ent_at(b->outline, &BOOSTER_OUTLINE);
  if (o) o->visible = 1;
  wiggler_restart(&b->wig);
  b->dir = p->dash_dir;
  b->routine = 1;   /* BoostRoutine */
}
static void booster_update(Ent *e) {
  Booster *b = ST(e, Booster);
  Player *p = level_player();
  /* base.Update(): sprite, wiggler, the dash routine */
  spr_update(&b->spr);
  wiggler_update(&b->wig);
  if (b->routine == 1) {
    if (p && (p->state == ST_DASH || p->state == ST_REDDASH) && b->boosting) {
      b->spr_pos = v2sub(v2add(player_center(p), v2(0, -2)), v2(e->x, e->y));
      if (level_on_interval(0.02f))
        particles_emit(PL_BG, b->red ? &P_Booster_P_BurstRed : &P_Booster_P_Burst, 2,
                       v2add(v2sub(player_center(p), v2mul(b->dir, 3)), v2(0, -2)), v2(3, 3), atan2f(-b->dir.y, -b->dir.x));
    } else {
      booster_released(e, b);
      if (p && p->state == ST_BOOST) b->spr.visible = 0;
      b->routine = 2;
    }
  } else if (b->routine == 2) {
    if (!g_level.transitioning) e->tags = 0, b->routine = 0;
  }
  /* the player's dash out of it: Player.CallDashEvents -> PlayerBoosted (player.c's level_booster_boosted does nothing) */
  bool cur = p && p->current_booster == e;
  if (b->was_current && !cur && p && !p->dead && (p->state == ST_DASH || p->state == ST_REDDASH)) booster_boosted(e, b, p);
  b->was_current = cur;
  if (p && p->dead && b->boosting) {   /* PlayerDied */
    booster_released(e, b);
    b->routine = 0;
    e->tags = 0;
  }
  /* Update's own */
  if (b->cannot_use > 0) b->cannot_use -= DT;
  if (b->respawn > 0) {
    b->respawn -= DT;
    if (b->respawn <= 0) {   /* Respawn */
      b->spr_pos = v2(0, 0);
      spr_play(&b->spr, booster_anim(b, 0), true);
      wiggler_restart(&b->wig);
      b->spr.visible = 1;
      Ent *o = ent_at(b->outline, &BOOSTER_OUTLINE);
      if (o) o->visible = 0;
      booster_appear_particles(e, b);
    }
  }
  if (!b->routine && b->respawn <= 0) {
    V2 target = v2(0, 0);
    if (p && collide_ent_at(e, e->x, e->y, p->ent)) target = v2sub(v2add(player_center(p), v2(0, -2)), v2(e->x, e->y));
    V2 d = v2sub(target, b->spr_pos);
    float l = v2len(d), m = 80 * DT;
    b->spr_pos = l <= m ? target : v2add(b->spr_pos, v2mul(d, m / l));
  }
  if (spr_is(&b->spr, booster_anim(b, 1)) && !b->boosting && !(p && collide_ent_at(e, e->x, e->y, p->ent)))
    spr_play(&b->spr, booster_anim(b, 0), false);
}
static void booster_render(Ent *e) {
  Booster *b = ST(e, Booster);
  if (!b->spr.visible) return;
  Sprite s = b->spr;
  s.sx = s.sy = 1 + b->wig.value * 0.25f;
  float x = e->x + floorf(b->spr_pos.x), y = e->y + floorf(b->spr_pos.y);
  if (!spr_is(&s, booster_anim(b, 3))) {   /* DrawOutline */
    Sprite o = s;
    o.tint = 0;
    for (int i = -1; i < 2; i++)
      for (int j = -1; j < 2; j++)
        if (i || j) spr_draw(&o, x + i, y + j);
  }
  spr_draw(&s, x, y);
  light_add(e->x, e->y, 0xFFFFFF, 1, 16, 32);
  bloom_add(e->x, e->y, 0.1f, 16);
}
static void outline_render(Ent *e) { gfx_tex_ex(tex("objects/booster/outline"), e->x, e->y, 16, 16, 1, 1, 0, 0xFFFF, 191, 0); }
static const EntClass BOOSTER_OUTLINE = {.name = "boosterOutline", .size = 0, .render = outline_render};
static const EntClass BOOSTER = {.name = "booster", .size = sizeof(Booster), .update = booster_update, .render = booster_render,
                                 .on_player = booster_on_player, .kind = KIND_PCOLLIDE,
                                 .more = &(const EntMore){.on_dash = booster_on_dash}};
static void new_booster(const EData *d) {
  Ent *e = ent_new(&BOOSTER, d->x, d->y);
  if (!e) return;
  e->depth = -8500;
  ent_circle(e, 10, 0, 2);
  Booster *b = ST(e, Booster);
  b->red = EAB(d, booster, red);
  int bank = booster_bank(b->red);
  if (bank >= 0) spr_init(&b->spr, bank), spr_play(&b->spr, booster_anim(b, 0), true);
  else b->spr.visible = 0;
  wiggler_init(&b->wig, 0.5f, 4);
  Ent *o = ent_new(&BOOSTER_OUTLINE, d->x, d->y);   /* Added: the outline, shown while it is away */
  if (o) o->depth = 8999, o->visible = 0, o->collidable = 0;
  b->outline = ent_ref(o);
}

/* ---------------------------------------------------------------- MoveBlock */
enum { MB_LEFT, MB_RIGHT, MB_UP, MB_DOWN };
enum { MS_IDLE, MS_MOVING, MS_BREAKING };
typedef struct {
  V2 start;
  float speed, target_speed, angle, target_angle, home_angle, flash, crash, crash_reset, no_steer, wait, particle_rem;
  uint32_t fill;
  int16_t border, nosquish;
  int8_t steer_sign;
  uint8_t dir, can_steer, fast, state, step, triggered, left_p, right_p, top_p;
} MoveB;
static const EntClass MOVEBLOCK, MB_DEBRIS;
static uint16_t mb_base, mb_base_h, mb_base_v, mb_button, mb_arrow[8], mb_x, mb_debris_tex[3];
#define MB_IDLE_FILL 0x474070
#define MB_PRESSED_FILL 0x30b335
#define MB_BREAKING_FILL 0xcc2541
static bool mb_horizontal(const MoveB *m) { return m->dir == MB_LEFT || m->dir == MB_RIGHT; }
/* MoveHExact / MoveVExact, overridden: not into the player it moves along with (noSquish) */
static void mb_move_exact(Ent *e, MoveB *m, int move, bool vertical) {
  Ent *ns = m->nosquish ? level_player_ent() : NULL;
  if (ns) {
    if (!vertical && ((move < 0 && ns->x < e->x) || (move > 0 && ns->x > e->x)))
      while (move != 0 && collide_solid(ns, ns->x + move, ns->y)) move -= signi(move);
    if (vertical && move < 0 && ns->y <= e->y)
      while (move != 0 && collide_solid(ns, ns->x, ns->y + move)) move -= signi(move);
  }
  if (vertical) plat_move_v_exact(e, move);
  else plat_move_h_exact(e, move);
}
/* Platform.MoveH/VCollideSolids (no dash blocks) with those exact moves */
static bool mb_move_collide(Ent *e, MoveB *m, float amount, bool vertical) {
  if (vertical) e->lift.y = amount / DT;
  else e->lift.x = amount / DT;
  float *rem = vertical ? &e->remy : &e->remx;
  *rem += amount;
  int n = roundi(*rem);
  if (!n) return false;
  *rem -= n;
  int s = signi(n), moved = 0;
  float was = vertical ? e->y : e->x;
  Ent *hit = NULL;
  while (n) {
    hit = collide_first_solid(e, e->x + (vertical ? 0 : s), e->y + (vertical ? s : 0));
    if (hit) break;
    moved += s, n -= s;
    if (vertical) e->y += s;
    else e->x += s;
  }
  if (vertical) e->y = was;
  else e->x = was;
  mb_move_exact(e, m, moved, vertical);
  return hit != NULL;
}
/* MoveCheck: into a wall, or around a corner of up to 3 px */
static bool mb_move_check(Ent *e, MoveB *m, V2 sp) {
  if (sp.x != 0) {
    if (!plat_move_h_collide_solids(e, sp.x, false)) return false;
    for (int i = 1; i <= 3; i++)
      for (int k = 1; k >= -1; k -= 2)
        if (!collide_solid(e, e->x + signf(sp.x), e->y + i * k)) {
          mb_move_exact(e, m, i * k, true);
          mb_move_exact(e, m, (int)signf(sp.x), false);
          return false;
        }
    return true;
  }
  if (sp.y != 0) {
    if (!plat_move_v_collide_solids(e, sp.y, false)) return false;
    for (int j = 1; j <= 3; j++)
      for (int k = 1; k >= -1; k -= 2)
        if (!collide_solid(e, e->x + j * k, e->y + signf(sp.y))) {
          mb_move_exact(e, m, j * k, false);
          mb_move_exact(e, m, (int)signf(sp.y), true);
          return false;
        }
    return true;
  }
  return false;
}
static void mb_activate_particles(Ent *e, MoveB *m) {
  Ent *p = level_player_ent();
  bool vert = m->dir == MB_DOWN || m->dir == MB_UP;
  bool a = (!m->can_steer || !vert) && !(p && collide_ent_at(e, e->x - 1, e->y, p));
  bool b = (!m->can_steer || !vert) && !(p && collide_ent_at(e, e->x + 1, e->y, p));
  bool c = (!m->can_steer || vert) && !(p && collide_ent_at(e, e->x, e->y - 1, p));
  float w = e->cw, h = e->ch;
  if (a) particles_emit(PL_BG, &P_MoveBlock_P_Activate, (int)(h / 2), v2(e->x, e->y + h / 2), v2(0, (h - 4) * 0.5f), PI_F);
  if (b) particles_emit(PL_BG, &P_MoveBlock_P_Activate, (int)(h / 2), v2(e->x + w, e->y + h / 2), v2(0, (h - 4) * 0.5f), 0);
  if (c) particles_emit(PL_BG, &P_MoveBlock_P_Activate, (int)(w / 2), v2(e->x + w / 2, e->y), v2((w - 4) * 0.5f, 0), -PI_F / 2);
  particles_emit(PL_BG, &P_MoveBlock_P_Activate, (int)(w / 2), v2(e->x + w / 2, e->y + h), v2((w - 4) * 0.5f, 0), PI_F / 2);
}
static void mb_move_particles(Ent *e, MoveB *m) {
  V2 pos, range;
  float dir, num;
  float w = e->cw, h = e->ch;
  switch (m->dir) {
    case MB_RIGHT: pos = v2(e->x + 1, e->y + h / 2), range = v2(0, h - 4), dir = PI_F, num = h / 32; break;
    case MB_LEFT: pos = v2(e->x + w, e->y + h / 2), range = v2(0, h - 4), dir = 0, num = h / 32; break;
    case MB_DOWN: pos = v2(e->x + w / 2, e->y + 1), range = v2(w - 4, 0), dir = -PI_F / 2, num = w / 32; break;
    default: pos = v2(e->x + w / 2, e->y + h), range = v2(w - 4, 0), dir = PI_F / 2, num = w / 32; break;
  }
  m->particle_rem += num;
  int n = (int)m->particle_rem;
  m->particle_rem -= n;
  if (n > 0) particles_emit(PL_BG, &P_MoveBlock_P_Move, n, pos, v2mul(range, 0.5f), dir);
}
static void mb_scrape(Ent *e, V2 d) {
  uint8_t was = e->collidable;
  e->collidable = 0;
  if (d.x != 0) {
    float x = d.x > 0 ? e_right(e) : e_left(e) - 1;
    for (int i = 0; (float)i < e->ch; i += 8)
      if (point_solid(x, e_top(e) + 4 + i)) particles_emit1(PL_FG, &P_ZipMover_P_Scrape, v2(x, e_top(e) + 4 + i), P_ZipMover_P_Scrape.direction);
  } else {
    float y = d.y > 0 ? e_bottom(e) : e_top(e) - 1;
    for (int j = 0; (float)j < e->cw; j += 8)
      if (point_solid(e_left(e) + 4 + j, y)) particles_emit1(PL_FG, &P_ZipMover_P_Scrape, v2(e_left(e) + 4 + j, y), P_ZipMover_P_Scrape.direction);
  }
  e->collidable = was;
}

/* MoveBlock.Debris (an Actor) */
typedef struct {
  ActorExt ext;
  V2 home, speed, from, ctrl;
  float rot, spin, alpha, ret, ret_dur;
  int16_t parent;
  int8_t ox, oy;
  uint8_t tex, flip, shaking, returning, first_hit, moving;
} MDebris;
static void mdebris_collide_h(Ent *e, Collision *c) { (void)c, ST(e, MDebris)->speed.x *= -0.5f; }
static void mdebris_collide_v(Ent *e, Collision *c) {
  (void)c;
  MDebris *d = ST(e, MDebris);
  if (d->speed.y > 0 && d->speed.y < 40) d->speed.y = 0;
  else d->speed.y = -d->speed.y * 0.25f;
  d->first_hit = 0;
}
static void mdebris_squish(Ent *e, Collision *c) { (void)e, (void)c; }
static void mdebris_update(Ent *e) {
  MDebris *d = ST(e, MDebris);
  actor_update_lift(e);
  if (!d->returning) {
    if (d->moving) {
      d->speed.x = approach(d->speed.x, 0, DT * 100);
      if (!actor_on_ground(e, 1)) d->speed.y += 400 * DT;
      actor_move_h(e, d->speed.x * DT, mdebris_collide_h, NULL);
      actor_move_v(e, d->speed.y * DT, mdebris_collide_v, NULL);
    }
    if (d->shaking && level_on_interval(0.05f)) d->ox = (int8_t)(rndi(3) - 1), d->oy = (int8_t)(rndi(3) - 1);
  } else {
    V2 at = curve_point(d->from, d->home, d->ctrl, ease_cube_out(d->ret));
    e->x = at.x, e->y = at.y;
    d->ret = approach(d->ret, 1, DT / d->ret_dur);
  }
  if (g_level.transitioning) d->alpha = approach(d->alpha, 0, DT * 4);
  d->rot += d->spin * clamped_map(fabsf(d->speed.y), 50, 150, 0, 1) * DT;
}
static void mdebris_render(Ent *e) {
  MDebris *d = ST(e, MDebris);
  float s = d->returning ? 1 + d->ret * 0.5f : 1;
  gfx_tex_ex(mb_debris_tex[d->tex], e->x + d->ox, e->y + d->oy, 4, 4, d->flip ? -s : s, s, d->rot, 0xFFFF, (uint8_t)(d->alpha * 255), 0);
}
static const EntClass MB_DEBRIS = {.name = "moveBlockDebris", .size = sizeof(MDebris), .update = mdebris_update,
                                   .render = mdebris_render, .kind = KIND_ACTOR,
                                   .more = &(const EntMore){.on_squish = mdebris_squish}};

/* MoveBlock.Border */
typedef struct { int16_t parent; } MBorder;
static void mborder_render(Ent *e) {
  Ent *p = ent_at(ST(e, MBorder)->parent, &MOVEBLOCK);
  if (!p) {
    ent_remove(e);
    return;
  }
  if (!p->visible) return;
  V2 sh = e_shake(p);
  gfx_rect(p->x + sh.x - 1, p->y + sh.y - 1, p->cw + 2, p->ch + 2, 0, 255);
}
static const EntClass MB_BORDER = {.name = "moveBlockBorder", .size = sizeof(MBorder), .render = mborder_render};

static void mb_each_debris(Ent *e, int what, float arg) {
  for (int i = 0; i < g_nents; i++) {
    Ent *o = &g_ents[i];
    if (o->cls != &MB_DEBRIS || o->dead == 1) continue;
    MDebris *d = ST(o, MDebris);
    if (d->parent != ent_ref(e)) continue;
    switch (what) {
      case 0: o->collidable = 0, d->moving = 0; break;   /* StopMoving */
      case 1: d->shaking = 1; break;                     /* StartShaking */
      case 2: {                                          /* ReturnHome */
        float cx = g_level.cam.x, cy = g_level.cam.y;
        if (o->x < cx) o->x = cx - 8;
        if (o->y < cy) o->y = cy - 8;
        if (o->x > cx + 320) o->x = cx + 320 + 8;
        if (o->y > cy + 180) o->y = cy + 180 + 8;
        d->returning = 1, d->ret = 0, d->ret_dur = arg;
        V2 n = v2norm(v2sub(d->home, v2(o->x, o->y)));
        d->from = v2(o->x, o->y);
        d->ctrl = v2add(v2mul(v2add(d->from, d->home), 0.5f), v2mul(v2(n.y, -n.x), (rndf() * 16 + 16) * (rndi(2) ? 1 : -1)));
        break;
      }
      case 3: ent_remove(o); break;
    }
  }
}
static void mb_controller(Ent *e, MoveB *m) {
  if (m->wait > 0) {
    m->wait -= DT;
    return;
  }
  const Room *rm = room_ptr(e);
  switch (m->step) {
    case 0:
      m->triggered = 0;
      m->state = MS_IDLE;
      m->step = 1;
      /* fall through */
    case 1:
      if (!m->triggered && !solid_has_player_rider(e)) return;
      m->state = MS_MOVING;
      plat_start_shaking(e, 0.2f);
      mb_activate_particles(e, m);
      m->wait = 0.2f, m->step = 2;
      return;
    case 2:
      m->target_speed = m->fast ? 75 : 60;
      m->crash = 0.15f, m->crash_reset = 0.1f, m->no_steer = 0.2f;
      m->step = 3;
      /* fall through */
    case 3: {
      bool horiz = mb_horizontal(m);
      if (m->can_steer) {
        m->target_angle = m->home_angle;
        bool flag = horiz ? solid_has_player_on_top(e) : solid_has_player_climbing(e);
        if (flag && m->no_steer > 0) m->no_steer -= DT;
        if (flag) {
          if (m->no_steer <= 0) m->target_angle = m->home_angle + PI_F / 4 * m->steer_sign * (horiz ? g_in.move_y : g_in.move_x);
        } else
          m->no_steer = 0.2f;
      }
      if (level_on_interval(0.02f)) mb_move_particles(e, m);
      m->speed = approach(m->speed, m->target_speed, 300 * DT);
      m->angle = approach(m->angle, m->target_angle, PI_F * 16 * DT);
      V2 v = v2mul(angle_vec(m->angle, m->speed), DT);
      bool stop;
      if (horiz) {
        stop = mb_move_check(e, m, v2(v.x, 0));
        m->nosquish = 1;
        mb_move_collide(e, m, v.y, true);
        m->nosquish = 0;
        if (level_on_interval(0.03f)) {
          if (v.y > 0) mb_scrape(e, v2(0, 1));
          else if (v.y < 0) mb_scrape(e, v2(0, -1));
        }
      } else {
        stop = mb_move_check(e, m, v2(0, v.y));
        m->nosquish = 1;
        mb_move_collide(e, m, v.x, false);
        m->nosquish = 0;
        if (level_on_interval(0.03f)) {
          if (v.x > 0) mb_scrape(e, v2(1, 0));
          else if (v.x < 0) mb_scrape(e, v2(-1, 0));
        }
        if (m->dir == MB_DOWN && e_top(e) > rm->y + rm->h + 32) stop = true;
      }
      bool brk = false;
      if (stop) {
        m->crash_reset = 0.1f;
        if (!(m->crash > 0)) brk = true;
        else m->crash -= DT;
      } else {
        if (m->crash_reset > 0) m->crash_reset -= DT;
        else m->crash = 0.15f;
      }
      if (e_left(e) < rm->x || e_top(e) < rm->y || e_right(e) > rm->x + rm->w) brk = true;
      if (!brk) return;
      m->state = MS_BREAKING;   /* the break */
      m->speed = m->target_speed = 0;
      m->angle = m->target_angle = m->home_angle;
      plat_start_shaking(e, 0.2f);
      m->wait = 0.2f, m->step = 4;
      return;
    }
    case 4: {
      V2 c = e_center(e);   /* BreakParticles */
      for (int i = 0; (float)i < e->cw; i += 4)
        for (int j = 0; (float)j < e->ch; j += 4) {
          V2 at = v2(e->x + 2 + i, e->y + 2 + j);
          particles_emit(PL_MID, &P_MoveBlock_P_Break, 1, at, v2(2, 2), atan2f(at.y - c.y, at.x - c.x));
        }
      for (int i = 0; (float)i < e->cw; i += 8)
        for (int j = 0; (float)j < e->ch; j += 8) {
          V2 at = v2(e->x + i + 4, e->y + j + 4);
          Ent *d = ent_new(&MB_DEBRIS, at.x, at.y);
          if (!d) continue;
          d->tags = TAG_TRANSITION_UPDATE;
          d->collidable = 1;
          ent_box(d, 4, 4, -2, -2);
          MDebris *s = ST(d, MDebris);
          s->parent = ent_ref(e);
          s->home = v2(m->start.x + i + 4, m->start.y + j + 4);
          V2 dir = v2norm(v2sub(at, c));
          s->speed = v2mul(dir, 60 + rndf() * 60);
          s->tex = (uint8_t)rndi(3);
          s->flip = rndf() < 0.5f;
          s->rot = rndf() * 2 * PI_F;
          s->alpha = 1;
          s->moving = 1;
          s->spin = rnd_rangef(3.4906585f, 10.471975f) * (rndi(2) ? 1 : -1);
        }
      plat_static_movers_move(e, v2(m->start.x - e->x, m->start.y - e->y));
      plat_static_movers_enable(e, false);
      e->x = m->start.x, e->y = m->start.y;
      e->visible = e->collidable = 0;
      m->wait = 2.2f, m->step = 5;
      return;
    }
    case 5:
      mb_each_debris(e, 0, 0);
      m->step = 6;
      /* fall through */
    case 6: {   /* while (CollideCheck<Actor>() || CollideCheck<Solid>()) */
      bool blocked = collide_first_solid(e, e->x, e->y) != NULL;
      for (int i = 0; i < g_nents && !blocked; i++) {
        Ent *o = &g_ents[i];
        if ((o->kind & KIND_ACTOR) && o->cls && o->dead != 1 && o->collidable && collide_ent_at(e, e->x, e->y, o)) blocked = true;
      }
      if (blocked) return;
      e->collidable = 1;
      mb_each_debris(e, 1, 0);
      m->wait = 0.2f, m->step = 7;
      return;
    }
    case 7:
      mb_each_debris(e, 2, 0.65f);
      m->wait = 0.6f, m->step = 8;
      return;
    case 8:
      mb_each_debris(e, 3, 0);
      e->visible = 1;
      plat_static_movers_enable(e, true);
      m->speed = m->target_speed = 0;
      m->angle = m->target_angle = m->home_angle;
      m->fill = MB_IDLE_FILL;
      m->flash = 1;
      m->step = 0;
      return;
  }
}
static void mb_update(Ent *e) {
  MoveB *m = ST(e, MoveB);
  mb_controller(e, m);   /* the Controller coroutine (a component) */
  plat_update(e);
  if (m->can_steer) {
    Ent *p = level_player_ent();
    bool vert = m->dir == MB_UP || m->dir == MB_DOWN;
    m->left_p = vert && p && collide_ent_at(e, e->x - 1, e->y, p);
    m->right_p = vert && p && collide_ent_at(e, e->x + 1, e->y, p);
    m->top_p = !vert && p && collide_ent_at(e, e->x, e->y - 1, p);
  }
  Ent *b = ent_at(m->border, &MB_BORDER);
  if (b) b->visible = e->visible;
  m->flash = approach(m->flash, 0, DT * 5);
  uint32_t target = m->state == MS_MOVING ? MB_PRESSED_FILL : m->state == MS_BREAKING ? MB_BREAKING_FILL : MB_IDLE_FILL;   /* UpdateColors */
  m->fill = lerp_color(m->fill, target, 10 * DT);
}
static void mb_trigger(Ent *e, Ent *sm) { (void)sm, ST(e, MoveB)->triggered = 1; }
/* the side buttons of the up/down blocks are turned a quarter: drawn in a strip layer, not as affine draws */
enum { OFF_mb_btn_px = ((END_rowbuf + 7) & ~7), END_mb_btn_px = OFF_mb_btn_px + (int)sizeof(uint8_t[24 * 8]) };
#define mb_btn_px (*(uint8_t(*)[24 * 8])(void *)(g_chram[4] + OFF_mb_btn_px))
static const uint16_t *mb_btn_col;
static const uint8_t *mb_btn_al;
static uint32_t mb_btn_frame = 0xFFFFFFFF;
static void mb_buttons_layer(uint16_t *strip, int sy0, int sy1, void *ctx) {
  Ent *e = ctx;
  MoveB *m = ST(e, MoveB);
  V2 sh = e_shake(e);
  uint16_t tint = rgb(m->fill);
  int n = (int)(e->ch / 8);
  for (int side = 0; side < 2; side++) {
    float cx = side ? e->cw + (m->right_p ? -2 : 0) : (m->left_p ? 2 : 0);
    for (int j = 0; j < n; j++) {
      int piece = j != 0 ? (j < n - 1 ? 1 : 2) : 0;
      float px = e->x + sh.x + cx, py = e->y + sh.y + j * 8 + 4;
      int X0 = vx(px) - 4, Y0 = vy(py) - 4;
      for (int dy = 0; dy < 8; dy++) {
        int Y = Y0 + dy;
        if (Y < sy0 || Y >= sy1) continue;
        for (int dx = 0; dx < 8; dx++) {
          /* rotation pi/2 then scale (1, -1) on the left, (1, 1) on the right */
          int u = dy, v = side ? 7 - dx : dx;
          uint8_t idx = mb_btn_px[v * 24 + piece * 8 + u];
          int X = X0 + dx;
          if (idx && (unsigned)X < VIEW_W) sp_texel(strip + (Y - sy0) * VIEW_W + X, mb_btn_col, mb_btn_al, idx, tint, 256, false);
        }
      }
    }
  }
}
static void mb_render(Ent *e) {
  MoveB *m = ST(e, MoveB);
  V2 sh = e_shake(e);
  float x = e->x + sh.x, y = e->y + sh.y, w = e->cw, h = e->ch;
  int nx = (int)(w / 8), ny = (int)(h / 8);
  uint16_t fill = rgb(m->fill);
  if (m->can_steer && !mb_horizontal(m)) {
    if (mb_btn_frame != g_frame) {
      mb_btn_frame = g_frame;
      Tex t;
      mb_btn_col = NULL;
      if (tex_get(mb_button, &t) && t.fw == 24 && t.fh == 8) {
        memset(mb_btn_px, 0, sizeof mb_btn_px);
        for (int r = 0; r < t.h; r++) {
          tex_row_idx(&t, r, rowbuf);
          for (int i = 0; i < t.w; i++) mb_btn_px[(t.oy + r) * 24 + t.ox + i] = rowbuf[i];
        }
        int n;
        mb_btn_col = pal_colors(t.pal, &n);
        mb_btn_al = pal_alpha(t.pal);
      }
    }
    if (mb_btn_col) gfx_custom(mb_buttons_layer, e, vy(y) - 1, vy(y + h) + 1);
  } else if (m->can_steer)
    for (int i = 0; i < nx; i++) {
      int piece = i != 0 ? (i < nx - 1 ? 1 : 2) : 0;
      gfx_tex_part(mb_button, x + i * 8, y - 4 + (m->top_p ? 2 : 0), piece * 8, 0, 8, 8, 0, fill, 255);
    }
  gfx_rect(x + 3, y + 3, w - 6, h - 6, fill, 255);
  uint16_t body = !m->can_steer ? mb_base : mb_horizontal(m) ? mb_base_h : mb_base_v;
  for (int k = 0; k < nx; k++)
    for (int l = 0; l < ny; l++) {
      int a = k != 0 ? (k < nx - 1 ? 1 : 2) : 0, b = l != 0 ? (l < ny - 1 ? 1 : 2) : 0;
      gfx_tex_part(body, x + k * 8, y + l * 8, a * 8, b * 8, 8, 8, 0, 0xFFFF, 255);
    }
  gfx_rect(x + w / 2 - 4, y + h / 2 - 4, 8, 8, fill, 255);
  if (m->state != MS_BREAKING) {
    int v = (int)floorf(fmodf(-m->angle + PI_F * 2, PI_F * 2) / (PI_F * 2) * 8 + 0.5f);
    gfx_tex_ex(mb_arrow[clampi(v, 0, 7)], x + w / 2, y + h / 2, 5, 5, 1, 1, 0, 0xFFFF, 255, 0);
  } else
    gfx_tex_ex(mb_x, x + w / 2, y + h / 2, 5, 5, 1, 1, 0, 0xFFFF, 255, 0);
  if (m->flash > 0) {
    float n = m->flash * 4;
    gfx_rect(e->x - n, e->y - n, w + n * 2, h + n * 2, 0xFFFF, (uint8_t)(m->flash * 255));
  }
}
static void mb_awake(Ent *e) {
  plat_static_movers_attach(e);
  Ent *b = ent_new(&MB_BORDER, e->x, e->y);
  if (b) b->depth = 1, b->collidable = 0, ST(b, MBorder)->parent = ent_ref(e);
  ST(e, MoveB)->border = ent_ref(b);
}
static const EntClass MOVEBLOCK = {.name = "moveBlock", .size = sizeof(MoveB), .update = mb_update, .render = mb_render,
                                   .awake = mb_awake, .kind = KIND_SOLID,
                                   .more = &(const EntMore){.on_staticmover_trigger = mb_trigger}};
static void new_moveblock(const EData *d) {
  Ent *e = ent_new(&MOVEBLOCK, d->x, d->y);
  if (!e) return;
  ent_box(e, EA(d, moveBlock, width), EA(d, moveBlock, height), 0, 0);
  e->depth = -1;
  MoveB *m = ST(e, MoveB);
  m->start = v2(d->x, d->y);
  m->can_steer = EAB(d, moveBlock, canSteer);
  m->fast = EAB(d, moveBlock, fast);
  m->border = -1;
  const char *dir = EAS(d, moveBlock, direction);
  m->dir = !strcmp(dir, "Right") ? MB_RIGHT : !strcmp(dir, "Up") ? MB_UP : !strcmp(dir, "Down") ? MB_DOWN : MB_LEFT;
  switch (m->dir) {
    case MB_LEFT: m->home_angle = PI_F, m->steer_sign = -1; break;
    case MB_UP: m->home_angle = -PI_F / 2, m->steer_sign = 1; break;
    case MB_DOWN: m->home_angle = PI_F / 2, m->steer_sign = -1; break;
    default: m->home_angle = 0, m->steer_sign = 1; break;
  }
  m->angle = m->target_angle = m->home_angle;
  m->fill = MB_IDLE_FILL;
  mb_base = tex("objects/moveBlock/base");
  mb_base_h = tex("objects/moveBlock/base_h");
  mb_base_v = tex("objects/moveBlock/base_v");
  mb_button = tex("objects/moveBlock/button");
  mb_x = tex("objects/moveBlock/x");
  char p[40];
  for (int i = 0; i < 8; i++) mb_arrow[i] = tex(path2(p, "objects/moveBlock/arrow", NULL, i));
  for (int i = 0; i < 3; i++) mb_debris_tex[i] = tex(path2(p, "objects/moveBlock/debris", NULL, i));
}

/* ---------------------------------------------------------------- Cloud */
typedef struct {
  Sprite spr;
  Wiggler wig;
  V2 spr_pos, scale;
  float speed, start_y, respawn, timer;
  uint8_t fragile, waiting, returning, last_frame;
} Cloud;
static int cloud_anim(const Cloud *c, int which) {   /* 0 idle, 1 fade, 2 spawn */
  bool remix = g_session.mode != M_A;
  (void)remix, (void)which;
  if (!c->fragile) {
#ifdef SB_cloudRemix
    if (remix) return A_cloudRemix_idle;
#endif
#ifdef SB_cloud
    return A_cloud_idle;
#endif
  } else {
#ifdef SB_cloudFragileRemix
    if (remix) return which == 0 ? A_cloudFragileRemix_idle : which == 1 ? A_cloudFragileRemix_fade : A_cloudFragileRemix_spawn;
#endif
#ifdef SB_cloudFragile
    return which == 0 ? A_cloudFragile_idle : which == 1 ? A_cloudFragile_fade : A_cloudFragile_spawn;
#endif
  }
  return 0;
}
static int cloud_bank(bool fragile, bool remix) {
  (void)fragile, (void)remix;
#ifdef SB_cloudFragileRemix
  if (fragile && remix) return SB_cloudFragileRemix;
#endif
#ifdef SB_cloudFragile
  if (fragile) return SB_cloudFragile;
#endif
#ifdef SB_cloudRemix
  if (!fragile && remix) return SB_cloudRemix;
#endif
#ifdef SB_cloud
  if (!fragile) return SB_cloud;
#endif
  return -1;
}
static void cloud_update(Ent *e) {
  Cloud *c = ST(e, Cloud);
  /* base.Update(): the sprite (spawn's 7th frame starts the wiggler), the wiggler, the lift speed */
  spr_update(&c->spr);
  if (c->fragile && spr_is(&c->spr, cloud_anim(c, 2)) && c->spr.frame == 6 && c->last_frame != 6) wiggler_restart(&c->wig);
  c->last_frame = c->spr.frame;
  wiggler_update(&c->wig);
  plat_update(e);
  c->scale.x = approach(c->scale.x, 1, DT);
  c->scale.y = approach(c->scale.y, 1, DT);
  c->timer += DT;
  Player *p = level_player();
  bool rider = p && jumpthru_has_player_rider(e);
  if (rider) c->spr_pos = v2(0, 0);
  else {
    V2 t = v2(0, sinf(c->timer * 2)), d = v2sub(t, c->spr_pos);
    float l = v2len(d), m = DT * 4;
    c->spr_pos = l <= m ? t : v2add(c->spr_pos, v2mul(d, m / l));
  }
  if (c->respawn > 0) {
    c->respawn -= DT;
    if (c->respawn <= 0) {
      c->waiting = 1;
      e->y = c->start_y, e->remy = 0;
      c->speed = 0;
      c->scale = v2(1, 1);
      e->collidable = 1;
      spr_play(&c->spr, cloud_anim(c, 2), false);
    }
    return;
  }
  if (c->waiting) {
    if (rider && p->speed.y >= 0) {
      c->speed = 180;
      c->scale = v2(1.3f, 0.7f);
      c->waiting = 0;
    }
    return;
  }
  if (c->returning) {
    c->speed = approach(c->speed, 180, 600 * DT);
    plat_move_towards_y(e, c->start_y, c->speed * DT);
    if (e->y + e->remy == c->start_y) c->returning = 0, c->waiting = 1, c->speed = 0;
    return;
  }
  if (c->fragile && e->collidable && !rider) {
    e->collidable = 0;
    spr_play(&c->spr, cloud_anim(c, 1), false);
  }
  if (c->speed < 0 && level_on_interval(0.02f))
    particles_emit(PL_BG, c->fragile ? &P_Cloud_P_FragileCloud : &P_Cloud_P_Cloud, 1, v2(e->x, e->y + 2), v2(e->cw / 2, 1), PI_F / 2);
  if (c->fragile && c->speed < 0) c->spr.sy = approach(c->spr.sy, 0, DT * 4);
  if (e->y >= c->start_y) c->speed -= 1200 * DT;
  else {
    c->speed += 1200 * DT;
    if (c->speed >= -100) {
      if (rider && p->speed.y >= 0) p->speed.y = -200;
      if (c->fragile) {
        e->collidable = 0;
        spr_play(&c->spr, cloud_anim(c, 1), false);
        c->respawn = 2.5f;
      } else {
        c->scale = v2(0.7f, 1.3f);
        c->returning = 1;
      }
    }
  }
  float lift = c->speed < 0 ? -220 : c->speed;
  plat_move_v_lift(e, c->speed * DT, lift);
}
static void cloud_render(Ent *e) {
  Cloud *c = ST(e, Cloud);
  Sprite s = c->spr;
  Tex t;
  uint16_t id = spr_tex(&s);
  s.justify = 0;
  s.ox = id != 0xFFFF && tex_get(id, &t) ? t.fw / 2.f : 16, s.oy = 8;   /* sprite.Origin = (Width / 2, 8) */
  float k = 1 + 0.1f * c->wig.value;
  s.sx = c->scale.x * k, s.sy = c->scale.y * k * (c->fragile ? c->spr.sy : 1);
  spr_draw(&s, e->x + c->spr_pos.x, e->y + c->spr_pos.y);
}
static const EntClass CLOUD = {.name = "cloud", .size = sizeof(Cloud), .update = cloud_update, .render = cloud_render, .kind = KIND_JUMPTHRU};
static void new_cloud(const EData *d) {
  Ent *e = ent_new(&CLOUD, d->x, d->y);
  if (!e) return;
  Cloud *c = ST(e, Cloud);
  c->fragile = EAB(d, cloud, fragile);
  if (g_session.mode != M_A) ent_box(e, 26, 5, -14, 0);
  else ent_box(e, 32, 5, -16, 0);
  c->start_y = d->y;
  c->timer = rndf() * 4;
  c->waiting = 1;
  c->scale = v2(1, 1);
  wiggler_init(&c->wig, 0.3f, 4);
  int bank = cloud_bank(c->fragile, g_session.mode != M_A);
  if (bank >= 0) spr_init(&c->spr, bank), spr_play(&c->spr, cloud_anim(c, 0), true);
  else c->spr.visible = 0;
}

/* ---------------------------------------------------------------- RidgeGate */
typedef struct { V2 move_to; Tween tw; V2 tw_from; float wait; uint8_t step, has_node; V2 node; } Ridge;
static void ridge_awake(Ent *e) {
  Ridge *r = ST(e, Ridge);
  plat_static_movers_attach(e);
  Ent *p = level_player_ent();
  if (r->has_node && p && collide_ent_at(e, e->x, e->y, p)) {
    e->visible = e->collidable = 0;
    r->move_to = v2(e->x, e->y);
    e->x = r->node.x, e->y = r->node.y;
    r->step = 1;   /* EnterSequence */
  }
}
static void ridge_update(Ent *e) {
  Ridge *r = ST(e, Ridge);
  if (r->tw.active) {
    tween_update(&r->tw);
    float t = ease_cube_out(r->tw.percent);
    plat_move_to(e, lerpf(r->tw_from.x, r->move_to.x, t), lerpf(r->tw_from.y, r->move_to.y, t));
  }
  if (r->wait > 0) r->wait -= DT;
  else switch (r->step) {
      case 1:
        e->visible = e->collidable = 1;
        r->wait = 0.25f, r->step = 2;
        break;
      case 2:
        r->wait = 0.25f, r->step = 3;
        break;
      case 3:
        r->tw_from = v2(e->x, e->y);
        tween_start(&r->tw, 1);
        r->step = 0;
        break;
    }
  plat_update(e);
}
static void ridge_render(Ent *e) {
  V2 sh = e_shake(e);
  gfx_tex(tex("objects/ridgeGate"), e->x + sh.x, e->y + sh.y, 0, 0xFFFF, 255);
}
static const EntClass RIDGE = {.name = "ridgeGate", .size = sizeof(Ridge), .update = ridge_update, .render = ridge_render,
                               .awake = ridge_awake, .kind = KIND_SOLID};
/* "room:id,room:id": every one collected (strawberries) or held (keys) this session */
static bool ridge_ids_ok(const char *list, bool keys) {
  while (*list) {
    char room[24];
    int n = 0;
    while (*list && *list != ':' && n < 23) room[n++] = *list++;
    room[n] = 0;
    if (*list != ':') return true;
    list++;
    int id = 0;
    while (*list >= '0' && *list <= '9') id = id * 10 + (*list++ - '0');
    if (*list == ',') list++;
    int ri = chapter_find_room(g_session.chapter, room);
    if (ri < 0) return false;
    if (keys) {
      bool have = false;
      for (int i = 0; i < g_session.nkeys && i < 4; i++)
        if (g_session.keys[i] == level_entity_id(ri, id)) have = true;
      if (!have) return false;
    } else {
      int bi = session_berry_index(ri, id);
      if (bi < 0 || !(g_session.berries >> bi & 1)) return false;
    }
  }
  return true;
}
static void new_ridge(const EData *d) {
  if (!ridge_ids_ok(EAS(d, ridgeGate, strawberries), false) || !ridge_ids_ok(EAS(d, ridgeGate, keys), true)) return;
  Ent *e = ent_new(&RIDGE, d->x, d->y);
  if (!e) return;
  ent_box(e, EA(d, ridgeGate, width), EA(d, ridgeGate, height), 0, 0);
  e->safe = 1;
  Ridge *r = ST(e, Ridge);
  r->has_node = d->nnodes > 0;
  r->node = ed_node(d, 0);
}

/* ---------------------------------------------------------------- WhiteBlock */
/* once the player has ducked on it for 3 s, the background's tiles turn solid (a Solid with their Grid) */
typedef struct { int16_t room; } BgSolid;
static bool bgsolid_collide_rect(const Ent *e, float l, float t, float r, float b) {
  (void)e;
  int x0 = (int)floorf(l), y0 = (int)floorf(t), x1 = (int)ceilf(r) - 1, y1 = (int)ceilf(b) - 1;
  if (x1 < x0 || y1 < y0) return false;
  const Room *rm = g_level.room;
  for (int cy = y0 >> 3; cy <= y1 >> 3; cy++)
    for (int cx = x0 >> 3; cx <= x1 >> 3; cx++) {
      int px = cx * 8, py = cy * 8;
      if (px < rm->x || py < rm->y || px >= rm->x + rm->w || py >= rm->y + rm->h) continue;
      if (level_tile_type(1, cx, cy) != '0') return true;
    }
  return false;
}
static const EntClass BGSOLID = {.name = "bgSolidTiles", .size = sizeof(BgSolid), .kind = KIND_SOLID,
                                 .more = &(const EntMore){.collide_rect = bgsolid_collide_rect}};
typedef struct { float duck_timer; int16_t bg; uint8_t enabled, activated; } White;
static void white_disable(Ent *e) {
  White *w = ST(e, White);
  w->enabled = 0;
  e->collidable = 0;
}
static void white_awake(Ent *e) {
  if (g_session.heart) white_disable(e);
}
static bool heart_gem_here(void) {   /* Tracker.GetEntity<HeartGem>() */
  for (int i = 0; i < g_nents; i++)
    if (g_ents[i].cls && g_ents[i].dead != 1 && g_ents[i].cls->name && !strcmp(g_ents[i].cls->name, "blackGem")) return true;
  return false;
}
static void white_update(Ent *e) {
  White *w = ST(e, White);
  plat_update(e);
  if (!w->enabled) return;
  Player *p = level_player();
  if (!w->activated) {
    if (p && jumpthru_has_player_rider(e) && player_ducking(p)) {
      w->duck_timer += DT;
      if (w->duck_timer >= 3) {   /* Activate */
        w->activated = 1;
        e->collidable = 0;
        p->ent->depth = 10001;
        ents_mark_unsorted();
        Ent *bg = ent_new(&BGSOLID, 0, 0);
        if (bg) bg->ctype = COL_LIST, bg->safe = 1, bg->visible = 0;
        w->bg = ent_ref(bg);
      }
    } else
      w->duck_timer = 0;
    if (g_session.heart) white_disable(e);
  } else if (!heart_gem_here() && p) {
    white_disable(e);
    p->ent->depth = 0;
    ents_mark_unsorted();
    ent_remove(ent_at(w->bg, &BGSOLID));
  }
}
static void white_render(Ent *e) {
  White *w = ST(e, White);
  gfx_tex(tex("objects/whiteblock"), e->x, e->y, 0, 0xFFFF, w->enabled ? 255 : 64);
}
static const EntClass WHITE = {.name = "whiteblock", .size = sizeof(White), .update = white_update, .render = white_render,
                               .awake = white_awake, .kind = KIND_JUMPTHRU};
static void new_white(const EData *d) {
  Ent *e = ent_new(&WHITE, d->x, d->y);
  if (!e) return;
  ent_box(e, 48, 5, 0, 0);
  e->safe = 1;
  e->depth = -9000;
  White *w = ST(e, White);
  w->enabled = 1;
  w->bg = -1;
}

/* ---------------------------------------------------------------- Gondola */
typedef struct {
  Sprite front;
  V2 start, dest;
  V2 lever_pos, lever_speed;   /* the Lever's offset and its fall once broken off */
  float rot, rot_speed, lever_t, lever_rot;
  uint8_t active, lever_frame, lever_broken, front_pull;
} Gondola;
static uint16_t gd_top, gd_back, gd_left, gd_right, gd_lever[2];
static void gondola_update(Ent *e) {
  Gondola *g = ST(e, Gondola);
  if (g->active) {   /* inCliffside: it swings back like a lamp */
    float num = signf(g->rot) == signf(g->rot_speed) ? 8 : 6;
    if (fabsf(g->rot) < 0.5f) num *= 0.5f;
    if (fabsf(g->rot) < 0.25f) num *= 0.5f;
    g->rot_speed += -signf(g->rot) * num * DT;
    g->rot += g->rot_speed * DT;
    g->rot = clampf(g->rot, -0.4f, 0.4f);
    if (fabsf(g->rot) < 0.02f && fabsf(g->rot_speed) < 0.2f) g->rot = g->rot_speed = 0;
  }
  spr_update(&g->front);
  if (g->lever_t > 0) g->lever_t -= DT;   /* Lever "pulled": frame 1, twice 0.5 s, then idle */
  g->lever_frame = g->lever_t > 0;
  if (g->lever_broken) {   /* BreakLeverRoutine */
    g->lever_pos = v2add(g->lever_pos, v2mul(g->lever_speed, DT));
    g->lever_rot += 2 * DT;
    g->lever_speed.y += 400 * DT;
  } else
    g->lever_rot = g->rot;
  plat_update(e);
}
static void gondola_render(Ent *e) {
  Gondola *g = ST(e, Gondola);
  float top_rot = atan2f(g->dest.y - g->start.y, g->dest.x - g->start.x);
  Sprite s = g->front;
  s.justify = 0, s.ox = 40, s.oy = 12, s.rot = g->rot;
  spr_draw(&s, e->x, e->y - 52);
  gfx_tex_ex(gd_top, e->x, e->y - 52, 40, 12, 1, 1, top_rot, 0xFFFF, 255, 0);
  if (g->active) gfx_tex_ex(gd_lever[g->lever_frame], e->x + g->lever_pos.x, e->y - 52 + g->lever_pos.y, 40, 12, 1, 1, g->lever_rot, 0xFFFF, 255, 0);
}
/* the gondola's back (depth 9000), the two cliffsides (8998) and the rope (8999) */
typedef struct { int16_t g; uint8_t part; } GPart;
static const EntClass GONDOLA;
static void gpart_render(Ent *e) {
  GPart *p = ST(e, GPart);
  Ent *ge = ent_at(p->g, &GONDOLA);
  if (!ge) return;
  Gondola *g = ST(ge, Gondola);
  V2 left = v2add(g->start, v2(-124, 0)), right = v2add(g->dest, v2(144, -104));
  switch (p->part) {
    case 0: gfx_tex_ex(gd_back, ge->x, ge->y - 52, 40, 12, 1, 1, g->rot, 0xFFFF, 255, 0); break;
    case 1: gfx_tex(gd_left, left.x, left.y - 32, 0, 0xFFFF, 255); break;   /* JustifyOrigin(0, 1) */
    case 2: gfx_tex_ex(gd_right, right.x, right.y, 0, 24, -1, 1, 0, 0xFFFF, 255, 0); break;   /* (0, 0.5), Scale.X -1 */
    default: {
      V2 a = v2(floorf(left.x + 40), floorf(left.y - 12)), b = v2(floorf(right.x - 40), floorf(right.y - 4));
      V2 n = v2norm(v2sub(b, a));
      V2 c4 = v2sub(v2add(v2(ge->x, ge->y), v2(0, -55)), v2mul(n, 6)), c5 = v2add(v2add(v2(ge->x, ge->y), v2(0, -55)), v2mul(n, 6));
      for (int i = 0; i < 2; i++) {
        gfx_line(a.x, a.y + i, c4.x, c4.y + i, 0, 255);
        gfx_line(c5.x, c5.y + i, b.x, b.y + i, 0, 255);
      }
    }
  }
}
static const EntClass GPART = {.name = "gondolaPart", .size = sizeof(GPart), .render = gpart_render};
static const EntClass PLAIN_JUMPTHRU = {.name = "jumpThruPlain", .size = 0, .update = plat_update, .kind = KIND_JUMPTHRU};
static const EntClass GONDOLA = {.name = "gondola", .size = sizeof(Gondola), .update = gondola_update, .render = gondola_render,
                                 .kind = KIND_SOLID};
static void new_gondola(const EData *d) {
  Ent *e = ent_new(&GONDOLA, d->x, d->y);
  if (!e) return;
  ent_box(e, 64, 8, -32, 0);
  e->safe = 1;
  e->depth = -10500;
  Gondola *g = ST(e, Gondola);
  g->start = v2(d->x, d->y);
  g->dest = ed_node(d, 0);
  g->active = EAB(d, gondola, active);
  spr_init(&g->front, SB_gondola);
  spr_play(&g->front, A_gondola_idle, true);
  gd_top = tex("objects/gondola/top");
  gd_back = tex("objects/gondola/back");
  gd_left = tex("objects/gondola/cliffsideLeft");
  gd_right = tex("objects/gondola/cliffsideRight");
  gd_lever[0] = tex("objects/gondola/lever00");
  gd_lever[1] = tex("objects/gondola/lever01");
  static const int depths[4] = {9000, 8998, 8998, 8999};
  for (int i = 0; i < 4; i++) {
    Ent *p = ent_new(&GPART, 0, 0);
    if (!p) continue;
    p->depth = depths[i];
    p->collidable = 0;
    ST(p, GPart)->g = ent_ref(e);
    ST(p, GPart)->part = (uint8_t)i;
  }
  if (!g->active) {   /* already across: a (plain, unseen) JumpThru in it */
    e->x = g->dest.x, e->y = g->dest.y;
    Ent *j = ent_new(&PLAIN_JUMPTHRU, e->x - 32, e->y - 36);
    if (j) ent_box(j, 64, 5, 0, 0), j->safe = 1, j->visible = 0;
  }
}

/* ---------------------------------------------------------------- CliffsideWindFlag */
/* every flag's columns in one strip layer: each a 1-px slice of its texture, moved by the wind */
typedef struct { float sine, random; int8_t sign; uint8_t index; } WFlag;
static const EntClass WFLAG;
static uint16_t wflag_tex[11];
static float wflag_wind(void) { return clamped_map(fabsf(g_level.wind.x), 0, 800, 0, 1); }
static void wflag_offset(const WFlag *f, int i, int n, float *ox, float *oy) {   /* SetFlagSegmentPosition (snapped) */
  float wind = wflag_wind();
  float value = (float)(i * f->sign) * (0.2f + wind * 0.8f * (0.8f + f->random * 0.2f)) * (0.9f + sinf(-f->sine) * 0.1f);
  float a = sinf(-(f->sine * 0.5f - i * 0.1f)) * ((float)i / n) * i * 0.2f;
  *ox = ceilf(wind) >= 1 ? value : a;
  *oy = (float)i / n * fmaxf(0.1f, 1 - wind) * 16;
}
static void wflag_layer(uint16_t *strip, int sy0, int sy1, void *ctx) {
  (void)ctx;
  for (int q = 0; q < g_nents; q++) {
    Ent *e = &g_ents[q];
    if (e->cls != &WFLAG || e->dead == 1) continue;
    WFlag *f = ST(e, WFlag);
    Tex t;
    if (!tex_get(wflag_tex[f->index], &t) || t.w > (int)sizeof rowbuf) continue;
    if (vy(e->y) > sy1 + 20 || vy(e->y) + t.fh + 20 < sy0) continue;
    int n;
    const uint16_t *col = pal_colors(t.pal, &n);
    const uint8_t *al = pal_alpha(t.pal);
    int segs = t.fw;
    for (int r = 0; r < t.h; r++) {
      tex_row_idx(&t, r, rowbuf);
      for (int k = 0; k < t.w; k++) {
        if (!rowbuf[k]) continue;
        int i = t.ox + k;
        float ox, oy;
        wflag_offset(f, i, segs, &ox, &oy);
        float bob = (float)i / segs * sinf(-(-i * 0.1f + f->sine)) * 2;
        int X = vx(e->x + ox + i), Y = vy(e->y + oy + bob) + t.oy + r;
        if (Y < sy0 || Y >= sy1 || (unsigned)X >= VIEW_W) continue;
        sp_texel(strip + (Y - sy0) * VIEW_W + X, col, al, rowbuf[k], 0xFFFF, 256, false);
      }
    }
  }
}
static uint32_t wflag_mark;
static void wflag_render(Ent *e) {
  (void)e;
  if (!layer_once(&wflag_mark)) return;
  /* its textures loaded now (and kept): nothing loads while the strips are drawn */
  for (int q = 0; q < g_nents; q++)
    if (g_ents[q].cls == &WFLAG && g_ents[q].dead != 1) {
      Tex t;
      tex_get(wflag_tex[ST(&g_ents[q], WFlag)->index], &t);
    }
  gfx_custom(wflag_layer, NULL, 0, VIEW_H);
}
static void wflag_update(Ent *e) {
  WFlag *f = ST(e, WFlag);
  float wind = wflag_wind();
  if (wind != 0) f->sign = (int8_t)signf(g_level.wind.x);
  f->sine += DT * (4 + wind * 4) * (0.8f + f->random * 0.2f);
}
static const EntClass WFLAG = {.name = "cliffside_flag", .size = sizeof(WFlag), .update = wflag_update, .render = wflag_render};
static void new_wflag(const EData *d) {
  Ent *e = ent_new(&WFLAG, d->x, d->y);
  if (!e) return;
  e->depth = 8999;
  e->tags = TAG_TRANSITION_UPDATE;
  e->collidable = 0;
  WFlag *f = ST(e, WFlag);
  int idx = (int)EA(d, cliffside_flag, index);
  f->index = (uint8_t)(idx < 0 ? 0 : idx > 10 ? 10 : idx);
  f->sine = rndf() * 2 * PI_F;
  f->random = rndf();
  f->sign = 1;
  if (wflag_wind() != 0) f->sign = (int8_t)signf(g_level.wind.x);
  char p[40];
  for (int i = 0; i < 11; i++) wflag_tex[i] = tex(path2(p, "scenery/cliffside/flag", NULL, i));
}

/* ---------------------------------------------------------------- CliffFlags (Flagline) */
typedef struct { V2 from, to; float wave; uint32_t seed; } Flagline;
static const EntClass CLIFFFLAGS;
static int cloth_val(uint32_t seed, int j, int k, int lo, int hi) {
  return hi <= lo ? lo : lo + (int)(hash32(seed + (uint32_t)(j * 4 + k)) % (uint32_t)(hi - lo));
}
static void flagline_draw(uint16_t *strip, int sy0, int sy1, const Flagline *f, const uint32_t *colors, int ncol, uint32_t line,
                          uint32_t pin, int minh, int maxh, int minl, int maxl, int mins, int maxs, float droop) {
  V2 a = f->from.x < f->to.x ? f->from : f->to, b = f->from.x < f->to.x ? f->to : f->from;
  float num = v2len(v2sub(a, b)), num2 = num / 8;
  V2 ctrl = v2add(v2mul(v2add(a, b), 0.5f), v2(0, num2 + sinf(f->wave) * num2 * 0.3f));
  V2 p4 = a;
  float t = 0;
  int k = 0;
  bool flag = false;
  uint16_t lc = rgb(line), pc = rgb(pin);
  for (int guard = 0; t < 1 && guard < 400; guard++) {
    int j = k % 10;
    int length = cloth_val(f->seed, j, 2, minl, maxl), step = cloth_val(f->seed, j, 3, mins, maxs);
    t += (float)(flag ? length : step) / num;
    V2 p5 = curve_point(a, b, ctrl, t);
    sp_line(strip, sy0, sy1, vx(p4.x), vy(p4.y), vx(p5.x), vy(p5.y), lc, 256);
    if (t < 1 && flag) {
      int col = cloth_val(f->seed, j, 0, 0, ncol), height = cloth_val(f->seed, j, 1, minh, maxh);
      float n5 = length * droop;
      V2 c2 = v2add(v2mul(v2add(p4, p5), 0.5f), v2(0, n5 + sinf(f->wave * 2 + t) * n5 * 0.4f));
      V2 p6 = p4;
      uint16_t cc = rgb(colors[col]), hc = rgb(lerp_color(colors[col], 0xFFFFFF, 0.1f));
      for (float n6 = 1; n6 <= (float)length; n6 += 1) {
        V2 pt = curve_point(p4, p5, c2, n6 / length);
        if (pt.x != p6.x) {
          sp_rect(strip, sy0, sy1, vx(p6.x), vy(p6.y), (int)floorf(pt.x - p6.x + 1 + 0.5f), height, cc, 256);
          p6 = pt;
        }
      }
      sp_rect(strip, sy0, sy1, vx(p4.x), vy(p4.y), 1, height, hc, 256);
      sp_rect(strip, sy0, sy1, vx(p5.x), vy(p5.y), 1, height, hc, 256);
      sp_rect(strip, sy0, sy1, vx(p4.x), vy(p4.y - 1), 1, 3, pc, 256);
      sp_rect(strip, sy0, sy1, vx(p5.x), vy(p5.y - 1), 1, 3, pc, 256);
      k++;
    }
    p4 = p5;
    flag = !flag;
  }
}
static void cliffflags_layer(uint16_t *strip, int sy0, int sy1, void *ctx) {
  (void)ctx;
  static const uint32_t COLORS[4] = {0xd85f2f, 0xd82f63, 0x2fd8a2, 0xd8d62f};
  for (int i = 0; i < g_nents; i++)
    if (g_ents[i].cls == &CLIFFFLAGS && g_ents[i].dead != 1)
      flagline_draw(strip, sy0, sy1, ST(&g_ents[i], Flagline), COLORS, 4, 0x606082, 0x808080, 10, 10, 10, 10, 2, 8, 0.2f);
}
static uint32_t cliffflags_mark;
static void cliffflags_render(Ent *e) {
  (void)e;
  if (layer_once(&cliffflags_mark)) gfx_custom(cliffflags_layer, NULL, 0, VIEW_H);
}
static void cliffflags_update(Ent *e) { ST(e, Flagline)->wave += DT; }
static const EntClass CLIFFFLAGS = {.name = "cliffflag", .size = sizeof(Flagline), .update = cliffflags_update, .render = cliffflags_render};
static void new_cliffflags(const EData *d) {
  Ent *e = ent_new(&CLIFFFLAGS, d->x, d->y);
  if (!e) return;
  e->depth = 8999;
  e->collidable = 0;
  Flagline *f = ST(e, Flagline);
  f->from = v2(d->x, d->y);
  f->to = ed_node(d, 0);
  f->wave = rndf() * 2 * PI_F;
  f->seed = (uint32_t)rnd_next(&g_rnd);
}

/* ---------------------------------------------------------------- WindController */
enum { WP_NONE, WP_LEFT, WP_RIGHT, WP_LEFTSTRONG, WP_RIGHTSTRONG, WP_LEFTONOFF, WP_RIGHTONOFF, WP_LEFTONOFFFAST, WP_RIGHTONOFFFAST,
       WP_ALTERNATING, WP_LEFTGEMSONLY, WP_RIGHTCRAZY, WP_DOWN, WP_UP, WP_SPACE };
typedef struct { V2 target; float wait; uint8_t pattern, start, ever_set, step; } Wind;
static const EntClass WIND;
static void wind_set_pattern(Wind *w, int pattern) {
  if (w->pattern == pattern && w->ever_set) return;
  w->ever_set = 1;
  w->pattern = (uint8_t)pattern;
  w->step = 0, w->wait = 0;   /* the old sequence goes */
  switch (pattern) {
    case WP_NONE: w->target = v2(0, 0); break;
    case WP_LEFT: w->target.x = -400; break;
    case WP_RIGHT: w->target.x = 400; break;
    case WP_LEFTSTRONG: w->target.x = -800; break;
    case WP_RIGHTSTRONG: w->target.x = 800; break;
    case WP_RIGHTCRAZY: w->target.x = 1200; break;
    case WP_DOWN: w->target.y = 300; break;
    case WP_UP: w->target.y = -400; break;
    case WP_SPACE: w->target.y = -600; break;
    default: w->step = 1; break;   /* the on/off sequences */
  }
}
/* the sequences (each a coroutine): their steps of (target x, time) */
static void wind_sequence(Wind *w) {
  if (!w->step) return;
  if (w->wait > 0) {
    w->wait -= DT;
    return;
  }
  static const float ALT[4][2] = {{-400, 3}, {0, 2}, {400, 3}, {0, 2}};
  int k = w->step - 1;
  float x, t;
  switch (w->pattern) {
    case WP_ALTERNATING: x = ALT[k % 4][0], t = ALT[k % 4][1]; break;
    case WP_RIGHTONOFF: x = k % 2 ? 0 : 800, t = 3; break;
    case WP_LEFTONOFF: x = k % 2 ? 0 : -800, t = 3; break;
    case WP_RIGHTONOFFFAST: x = k % 2 ? 0 : 800, t = 2; break;
    case WP_LEFTONOFFFAST: x = k % 2 ? 0 : -800, t = 2; break;
    default: return;
  }
  w->target.x = x;
  w->wait = t;
  w->step = (uint8_t)(1 + (k + 1) % 4);
}
/* Player.WindMove (WindMover): the wind pushes the player */
static void wind_move_player(Player *p, V2 move) {
  Ent *pe = p->ent;
  if (p->just_respawned || p->no_wind_timer > 0 || !player_in_control(p) || p->state == ST_BOOST || p->state == ST_DASH ||
      p->state == ST_SUMMITLAUNCH)
    return;
  const Room *rm = g_level.room;
  if (move.x != 0 && p->state != ST_CLIMB) {
    p->wind_timeout = 0.2f;
    p->wind_dir.x = signf(move.x);
    if (!collide_solid(pe, pe->x - signf(move.x) * 3, pe->y)) {
      if (player_ducking(p) && p->on_ground) move.x *= 0;
      if (move.x < 0) move.x = fmaxf(move.x, rm->x - (pe->x + pe->remx + pe->cx));
      else move.x = fminf(move.x, rm->x + rm->w - (pe->x + pe->remx + pe->cx + pe->cw));
      actor_move_h(pe, move.x, NULL, NULL);
    }
  }
  if (move.y == 0) return;
  p->wind_timeout = 0.2f;
  p->wind_dir.y = signf(move.y);
  if (!(e_bottom(pe) > rm->y) || (!(p->speed.y < 0) && actor_on_ground(pe, 1))) return;
  if (p->state == ST_CLIMB) {
    if (!(move.y > 0) || !(p->climb_no_move_timer <= 0)) return;
    move.y *= 0.4f;
  }
  /* (windMovedUp: not in player.c) */
  actor_move_v(pe, move.y, NULL, NULL);
}
static const EntClass WINDTRIG;
static void wind_transition_pattern(Wind *w);
static void wind_update(Ent *e) {
  Wind *w = ST(e, Wind);
  if (w->pattern == 0xFF) wind_transition_pattern(w);
  wind_sequence(w);   /* base.Update(): the sequence's coroutine */
  if (w->pattern == WP_LEFTGEMSONLY) {   /* a collected StrawberrySeed (berry.c) */
    bool any = false;
    for (int i = 0; i < g_nents && !any; i++)
      if (g_ents[i].cls && g_ents[i].dead != 1 && g_ents[i].cls->name && !strcmp(g_ents[i].cls->name, "strawberrySeed") && leader_has(&g_ents[i]))
        any = true;
    w->target.x = any ? -400 : 0;
  }
  V2 d = v2sub(w->target, g_level.wind);
  float l = v2len(d), m = 1000 * DT;
  g_level.wind = l <= m ? w->target : v2add(g_level.wind, v2mul(d, m / l));
  if ((g_level.wind.x == 0 && g_level.wind.y == 0) || g_level.transitioning) return;
  Player *p = level_player();
  if (p && !p->dead) wind_move_player(p, v2mul(g_level.wind, 0.1f * DT));
}
static const EntClass WIND = {.name = "windController", .size = sizeof(Wind), .update = wind_update};
static Ent *wind_controller(void) {
  for (int i = 0; i < g_nents; i++)
    if (g_ents[i].cls == &WIND && g_ents[i].dead != 1) return &g_ents[i];
  return NULL;
}
/* Level.LoadLevel: the old controller goes, a new one with the room's pattern */
static int16_t wind_room = -1;
static int8_t wind_slot = -1;
static void wind_room_load(const EData *d) {
  if (d->room == wind_room && g_level.room_slot == wind_slot && wind_controller()) return;
  wind_room = (int16_t)d->room, wind_slot = (int8_t)g_level.room_slot;
  ent_remove(wind_controller());
  Ent *e = ent_new(&WIND, 0, 0);
  if (!e) return;
  e->tags = TAG_TRANSITION_UPDATE;
  e->collidable = 0, e->visible = 0;
  Wind *w = ST(e, Wind);
  const Room *rm = &g_level.rooms[g_level.room_slot];
  w->start = rm->info ? rm->info[25] : 0;   /* the room's windPattern */
  if (level_player_ent()) w->pattern = 0xFF;   /* a transition: its wind triggers or SetStartPattern (in its awake) */
  else {
    wind_set_pattern(w, w->start);   /* SetStartPattern */
    g_level.wind = w->target;        /* (a reload snaps the wind: SnapWind) */
  }
}

/* WindTrigger, WindAttackTrigger */
typedef struct { uint8_t pattern; } WindTrig;
static void windtrig_enter(Ent *e, Player *p) {
  (void)p;
  Ent *c = wind_controller();
  if (!c) {
    c = ent_new(&WIND, 0, 0);
    if (!c) return;
    c->tags = TAG_TRANSITION_UPDATE, c->collidable = 0, c->visible = 0;
    ST(c, Wind)->start = ST(e, WindTrig)->pattern;
  }
  wind_set_pattern(ST(c, Wind), ST(e, WindTrig)->pattern);
}
static const EntClass WINDTRIG = {.name = "windTrigger", .size = sizeof(WindTrig), .kind = KIND_TRIGGER,
                                  .more = &(const EntMore){.on_enter = windtrig_enter}};
/* Level.TransitionRoutine: the wind trigger under the player's destination sets the pattern, else the room's */
static void wind_transition_pattern(Wind *w) {
  w->pattern = 0, w->ever_set = 0;
  Ent *p = level_player_ent();
  for (int i = 0; i < g_nents && p; i++) {
    Ent *t = &g_ents[i];
    if (t->cls == &WINDTRIG && t->dead != 1 && t->room == g_level.room_slot &&
        collide_ent_at(p, g_level.tr_player_to.x, g_level.tr_player_to.y, t)) {
      wind_set_pattern(w, ST(t, WindTrig)->pattern);
      return;
    }
  }
  wind_set_pattern(w, w->start);
}

/* Snowball */
typedef struct { Sprite spr; SineWave sine; float reset, at_y; uint8_t broken; } Snowball;
static const EntClass SNOWBALL;
static void snowball_reset(Ent *e) {   /* ResetPosition */
  Snowball *s = ST(e, Snowball);
  Ent *p = level_player_ent();
  if (p && e_right(p) < g_level.room->x + g_level.room->w - 64) {
    e->collidable = e->visible = 1;
    s->reset = 0;
    e->x = g_level.cam.x + 320 + 10;
    e->y = s->at_y = e_cym(p);
    sine_set(&s->sine, 0);
#ifdef SB_snowball
    spr_play(&s->spr, A_snowball_spin, false);
#endif
    s->broken = 0;
    return;
  }
  s->reset = 0.05f;
}
static void snowball_destroy(Ent *e) {
  e->collidable = 0;
#ifdef SB_snowball
  spr_play(&ST(e, Snowball)->spr, A_snowball_break, false);
#endif
}
static void snowball_on_player(Ent *e, Player *p) {
  Ent *pe = p->ent;
  /* PlayerCollider(OnPlayer) on Hitbox(12, 9, -5, -2) first, then the bounce one, Hitbox(16, 6, -6, -8) */
  if (collide_rect(pe, e->x - 5, e->y - 2, e->x + 7, e->y + 7)) {
    player_die(p, v2(-1, 0), false);
    snowball_destroy(e);
    return;
  }
  if (collide_rect(pe, e->x - 6, e->y - 8, e->x + 10, e->y - 2)) {
    level_freeze(0.1f);
    player_bounce(p, e->y - 2 - 2);
    snowball_destroy(e);
  }
}
static void snowball_update(Ent *e) {
  Snowball *s = ST(e, Snowball);
  sine_update(&s->sine);
  spr_update(&s->spr);
  e->x -= 200 * DT;
  e->y = s->at_y + 4 * s->sine.value;
  if (e->x < g_level.cam.x - 60) {
    s->reset += DT;
    if (s->reset >= 0.8f) snowball_reset(e);
  }
}
static void snowball_render(Ent *e) {
  Snowball *s = ST(e, Snowball);
  Sprite o = s->spr;
  o.tint = 0;
  for (int i = -1; i < 2; i++)
    for (int j = -1; j < 2; j++)
      if (i || j) spr_draw(&o, e->x + i, e->y + j);
  spr_draw(&s->spr, e->x, e->y);
}
static const EntClass SNOWBALL = {.name = "snowball", .size = sizeof(Snowball), .update = snowball_update, .render = snowball_render,
                                  .on_player = snowball_on_player, .kind = KIND_PCOLLIDE};
static void windattack_enter(Ent *e, Player *p) {
  (void)p;
  bool have = false;
  for (int i = 0; i < g_nents; i++)
    if (g_ents[i].cls == &SNOWBALL && g_ents[i].dead != 1) have = true;
  if (!have) {
    Ent *s = ent_new(&SNOWBALL, 0, 0);
    if (s) {
      s->depth = -12500;
      ent_box(s, 16, 15, -6, -8);   /* both of its colliders */
      Snowball *b = ST(s, Snowball);
#ifdef SB_snowball   /* (the sprite is in data.bin once pack.py gathers triggers' assets) */
      spr_init(&b->spr, SB_snowball);
      spr_play(&b->spr, A_snowball_spin, true);
#else
      b->spr.visible = 0;
#endif
      b->sine.freq = 0.5f;
      snowball_reset(s);
    }
  }
  ent_remove(e);
}
static const EntClass WINDATTACK = {.name = "windAttackTrigger", .size = 0, .kind = KIND_TRIGGER,
                                    .more = &(const EntMore){.on_enter = windattack_enter}};

/* ---------------------------------------------------------------- the factories */
/* ---------------------------------------------------------------- the story: helpers */
#define EV_BEGIN       \
  if (c->wait > 0) {   \
    c->wait -= DT;     \
    return false;      \
  }                    \
  switch (c->co) {     \
    case 0:
#define EV_YIELD_(n) \
  do {               \
    c->co = (n);     \
    return false;    \
    case (n):;       \
  } while (0)
#define EV_YIELD EV_YIELD_(__COUNTER__ + 1)
#define EV_WAIT(t)   \
  do {               \
    c->wait = (t);   \
    EV_YIELD;        \
  } while (0)
#define EV_NEST(call)      \
  do {                     \
    EV_YIELD;              \
    while (call) EV_YIELD; \
    EV_YIELD;              \
  } while (0)
#define EV_END \
  default:;    \
  }            \
  return true
static Co *ev_start(Co *c, int8_t *at, int index) {
  if (*at != index) *at = (int8_t)index, memset(c, 0, sizeof *c);
  return c;
}
static void draw_npc(const Sprite *spr, float x, float y) {   /* Scale.X turns it */
  Sprite s = *spr;
  s.flipx = s.sx < 0;
  s.sx = fabsf(s.sx);
  spr_draw(&s, x, y);
}
static void player_free(void) {   /* StateMachine.Locked = false, State = 0 */
  Player *p = level_player();
  if (p) p->state_locked = false, player_set_state(p, ST_NORMAL);
}

/* ---------------------------------------------------------------- NPC04_Granny and CS04_Granny */
typedef struct {
  Talk talk;
  Sprite spr;
  int16_t haha;
  uint8_t has_talk, cutscene;
} Granny4;
typedef struct {
  Cutscene cs;
  Ent *granny, *tb;
  Walk walk;
  NpcWalk nw;
  Step step;
  Co ev;
  int8_t evi;
  uint8_t talk;
} Gran4Cs;
static bool gran4_event(void *ctx, int index) {
  /* Laughs, StopLaughing, WaitABeat, ZoomIn, MaddyTurnsAround, MaddyApproaches, MaddyWalksPastGranny */
  Gran4Cs *s = ST((Ent *)ctx, Gran4Cs);
  Ent *ge = s->granny;
  Granny4 *g = ST(ge, Granny4);
  Player *p = &g_player;
  Co *c = ev_start(&s->ev, &s->evi, index);
  EV_BEGIN;
  switch (index) {
    case 0: spr_play(&g->spr, A_granny_laugh, false), c->wait = 1; break;
    case 1: spr_play(&g->spr, A_granny_idle, false), c->wait = 0.25f; break;
    case 2: c->wait = 1.2f; break;
    case 4: c->wait = 0.2f; break;
  }
  EV_YIELD;
  if (index == 3) {
    step_reset(&s->step);
    EV_NEST(zoom_to(&s->step, v2(123, 116), 2, 0.5f));
  } else if (index == 4) {
    p->facing = -1;
    EV_WAIT(0.1f);
  } else if (index >= 5) {
    s->walk = index == 5 ? walk_to(ge->x - 20) : walk_exact((int)ge->x + 30);
    EV_NEST(player_walk(p, &s->walk));
  }
  EV_END;
}
static void gran4_end(Ent *e, bool skipped) {
  Gran4Cs *s = ST(e, Gran4Cs);
  Ent *ge = s->granny;
  Granny4 *g = ST(ge, Granny4);
  Player *p = level_player();
  if (s->talk) {   /* NPC04_Granny.TalkEnd */
    if (!level_get_flag("granny_2")) level_set_flag("granny_2", true);
    else if (!level_get_flag("granny_3")) {
      level_set_flag("granny_3", true);
      if (g->has_talk) g->has_talk = 0, ch1_talk_remove(ge);
    }
    player_free();
    if (p) p->force_camera_update = false;
    return;
  }
  if (p) {
    p->ent->x = ge->x + 30;
    p->state_locked = false;
    player_set_state(p, ST_NORMAL);
    p->force_camera_update = false;
    if (skipped) g_level.cam = player_camera_target(p);
  }
  spr_play(&g->spr, A_granny_laugh, false);
  level_set_flag("granny_1", true);
}
static void gran4_update(Ent *e) {
  Gran4Cs *s = ST(e, Gran4Cs);
  Ent *ge = s->granny;
  Granny4 *g = ST(ge, Granny4);
  Player *p = &g_player;
  Co *c = &s->cs.co;
  CO_BEGIN(c);
  s->evi = -1;
  if (s->talk) {   /* TalkRoutine */
    spr_play(&g->spr, A_granny_idle, false);
    p->force_camera_update = true;
    CO_NEST(c, npc_approach(&s->nw, ge, &g->spr, 20, -1));
    step_reset(&s->step);
    CO_NEST(c, zoom_to(&s->step, v2((p->ent->x + ge->x) / 2 - g_level.cam.x, 116), 2, 0.5f));
    CO_SAY(c, s->tb, level_get_flag("granny_2") ? "CH4_GRANNY_3" : "CH4_GRANNY_2", NULL, NULL);
  } else {
    player_set_state(p, ST_DUMMY);
    p->state_locked = true;
    p->force_camera_update = true;
    s->walk = walk_to(ge->x - 30);
    CO_NEST(c, player_walk(p, &s->walk));
    p->facing = 1;
    CO_SAY(c, s->tb, "CH4_GRANNY_1", gran4_event, e);
  }
  step_reset(&s->step);
  CO_NEST(c, zoom_back(&s->step, 0.5f));
  cutscene_end(e);
  return;
  CO_END(c);
}
static const EntClass GRAN4CS = {.size = sizeof(Gran4Cs), .name = "CS04_Granny", .update = gran4_update};
static void granny4_update(Ent *e) {
  Granny4 *g = ST(e, Granny4);
  Player *p = level_player();
  if (p && !level_get_flag("granny_1") && !g->cutscene && p->ent->x > e->x - 40) {
    g->cutscene = 1;
    Ent *c = ent_new(&GRAN4CS, 0, 0);
    if (c) c->collidable = 0, c->visible = 0, ST(c, Gran4Cs)->granny = e, cutscene_start(c, gran4_end, true, false);
    g->talk.enabled = true;
  }
  if (g->haha >= 0 && g_ents[g->haha].cls) hahaha_enable(&g_ents[g->haha], g->spr.anim == A_granny_laugh);
  spr_update(&g->spr);
  if (g->has_talk && talk_update(&g->talk, e) && p) {   /* OnTalk */
    Ent *c = ent_new(&GRAN4CS, 0, 0);
    if (c) c->collidable = 0, c->visible = 0, ST(c, Gran4Cs)->granny = e, ST(c, Gran4Cs)->talk = 1, cutscene_start(c, gran4_end, true, false);
  }
}
static void granny4_render(Ent *e) { draw_npc(&ST(e, Granny4)->spr, e->x, e->y); }
static const EntClass GRANNY4 = {.size = sizeof(Granny4), .name = "NPC04_Granny", .update = granny4_update, .render = granny4_render};
static void new_granny4(const EData *d) {
  Ent *e = ent_new(&GRANNY4, d->x, d->y);
  if (!e) return;
  e->depth = 1000;
  ent_box(e, 8, 8, -4, -8);
  Granny4 *g = ST(e, Granny4);
  spr_init(&g->spr, SB_granny);
  g->spr.sx = -1;
  spr_play(&g->spr, A_granny_idle, false);
  Ent *h = hahaha_new(v2(d->x + 8, d->y - 4), NULL);
  g->haha = h ? (int16_t)(h - g_ents) : -1;
  if (h) hahaha_enable(h, false);
  if (level_get_flag("granny_1") && !level_get_flag("granny_2")) spr_play(&g->spr, A_granny_laugh, false);
  if (!level_get_flag("granny_3")) {
    g->has_talk = 1;
    ch1_talk_add(e, &g->talk, -20, -16, 40, 16, v2(0, -24));
    if (!level_get_flag("granny_1")) g->talk.enabled = false;
  }
}

/* NPC04_Theo's state, and NPC.MoveTo with its walk (Maxspeed 48) */
typedef struct { Sprite spr; uint8_t started; } Theo4;
static bool theo4_move(Ent *e, Move *m) { return ch1_move_to(e, &ST(e, Theo4)->spr, m, 48, A_theo_walk, A_theo_idle); }

/* ---------------------------------------------------------------- CS04_Gondola */
/* the clouds it adds behind the level (Parallax bgs/04/bgCloudLoop and bgCloud), and the background's fade */
typedef struct { V2 loop_pos, bottom_pos, offset; float fade; uint8_t loop_y, on; } Clouds;
static void clouds_render(Ent *e) {
  Clouds *k = ST(e, Clouds);
  gfx_screen(true);
  if (k->on) {
    for (int w = 0; w < 2; w++) {   /* Parallax.Render, Scroll (1, 1), LoopX */
      uint16_t id = w ? T_bgs_04_bgCloud : T_bgs_04_bgCloudLoop;
      Tex t;
      if (!tex_get(id, &t)) continue;
      V2 cam = w ? g_level.cam : v2add(g_level.cam, k->offset);
      V2 at = v2sub(w ? k->bottom_pos : k->loop_pos, v2(floorf(cam.x), floorf(cam.y)));
      at = v2(floorf(at.x), floorf(at.y));
      while (at.x < 0) at.x += t.fw;
      while (at.x > 0) at.x -= t.fw;
      if (!w && k->loop_y) {
        while (at.y < 0) at.y += t.fh;
        while (at.y > 0) at.y -= t.fh;
      }
      for (float x = at.x; x < 320; x += t.fw)
        for (float y = at.y; y < 180; y += t.fh) {
          gfx_tex(id, x, y, 0, 0xFFFF, 255);
          if (w || !k->loop_y) break;
        }
    }
  }
  if (k->fade > 0) gfx_rect(0, 0, 320, 180, 0, (uint8_t)(clampf(k->fade, 0, 1) * 255));   /* Level.Background.Fade */
  gfx_screen(false);
}
static const EntClass CLOUDS = {.size = sizeof(Clouds), .name = "gondolaClouds", .render = clouds_render};

enum { GS_STOPPED, GS_TO_CENTER, GS_CENTER, GS_SHAKING, GS_TO_END };
typedef struct {
  Cutscene cs;
  Ent *theo, *gondola, *tb, *evil, *breath, *clouds;
  Walk walk, pwalk;
  Step step, zoom2;
  Move move;
  Co ev;
  Tween tw;
  V2 from, to;
  float percent, speed, theo_x, player_x, shake, p, sp, loop_at, start_x, dur;
  int8_t evi;
  uint8_t state, snap, pwalking, zooming, panning, walk_anim;
} Gond;
static Gondola *gond(Gond *s) { return ST(s->gondola, Gondola); }
static V2 gond_floor(Gond *s, float x, float y) {   /* Gondola.GetRotatedFloorPositionAt */
  Ent *ge = s->gondola;
  float r = gond(s)->rot;
  return v2(ge->x - sinf(r) * y + cosf(r) * x, ge->y - 52 + cosf(r) * y + sinf(r) * x);
}
static bool gond_pan(Gond *s, V2 to, float duration, float (*ease)(float)) {   /* PanCamera */
  if (s->p < 0) s->from = g_level.cam, s->p = 0, s->dur = duration;
  if (s->p >= 1) return false;
  s->p += DT / s->dur;
  g_level.cam = v2lerp(s->from, to, (ease ? ease : ease_cube_inout)(fminf(s->p, 1)));
  return true;
}
static bool gond_move(Gond *s, bool theo, float x, bool face) {   /* MovePlayerOnGondola / MoveTheoOnGondola */
  float *at = theo ? &s->theo_x : &s->player_x;
  Sprite *spr = theo ? &ST(s->theo, Theo4)->spr : &g_player.spr;
  if (!s->walk_anim) {
    s->walk_anim = 1;
    spr_play(spr, theo ? A_theo_walk : A_player_walk, false);
    if (theo) {
      if (face) spr->sx = signf(x - *at);
    } else
      g_player.facing = (int)signf(x - *at);
  }
  if (*at != x) {
    *at = approach(*at, x, 48 * DT);
    return true;
  }
  spr_play(spr, theo ? A_theo_idle : A_player_idle, false);
  s->walk_anim = 0;
  return false;
}
static void gond_towards(Gond *s, float percent) {   /* MoveGondolaTowards */
  Gondola *g = gond(s);
  float len = v2len(v2sub(g->start, g->dest));
  s->speed = approach(s->speed, 64, 120 * DT);
  s->percent = approach(s->percent, percent, s->speed / len * DT);
  V2 at = v2lerp(g->start, g->dest, s->percent);
  s->gondola->x = floorf(at.x), s->gondola->y = floorf(at.y);
  g_level.cam = v2add(v2(s->gondola->x, s->gondola->y), v2(-160, -120));
}
static bool gond_event(void *ctx, int index) {
  Gond *s = ST((Ent *)ctx, Gond);
  Ent *te = s->theo, *ge = s->gondola;
  Theo4 *t = ST(te, Theo4);
  Gondola *g = gond(s);
  Player *p = &g_player;
  Co *c = ev_start(&s->ev, &s->evi, index);
  switch (index) {
    case 0: {   /* EnterTheo */
      EV_BEGIN;
      p->facing = -1;
      EV_WAIT(0.2f);
      s->p = -1;
      EV_NEST(gond_pan(s, v2((float)g_level.room->x, te->y - 90), 1, NULL));
      te->visible = 1;
      s->start_x = te->x;
      memset(&s->move, 0, sizeof s->move), s->move.target = v2(s->start_x + 35, te->y);
      EV_NEST(theo4_move(te, &s->move));
      EV_WAIT(0.6f);
      memset(&s->move, 0, sizeof s->move), s->move.target = v2(s->start_x + 60, te->y);
      EV_NEST(theo4_move(te, &s->move));
      spr_play(&t->spr, A_theo_idleEdge, false);
      EV_WAIT(1);
      spr_play(&t->spr, A_theo_falling, false);
      te->x += 4;
      te->depth = -10010, ents_mark_unsorted();
      for (s->sp = 80; te->y < p->ent->y;) {
        te->y += s->sp * DT;
        s->sp += 120 * DT;
        EV_YIELD;
      }
      level_dir_shake(v2(0, 1), 0.3f);
      te->y = p->ent->y;
      spr_play(&t->spr, A_theo_hitGround, false);
      t->spr.rate = 0;
      te->depth = 1000, ents_mark_unsorted();
      t->spr.sx = 1.3f, t->spr.sy = 0.8f;
      EV_WAIT(0.5f);
      tween_start(&s->tw, 2);   /* Theo's scale back to 1 */
      s->p = -1;
      EV_NEST(gond_pan(s, v2((float)g_level.room->x, te->y - 120), 1, NULL));
      EV_WAIT(0.6f);
      EV_END;
    }
    case 1: {   /* CheckOnTheo */
      EV_BEGIN;
      s->walk = walk_to(ge->x - 18);
      EV_NEST(player_walk(p, &s->walk));
      EV_END;
    }
    case 2: {   /* GetUpTheo */
      EV_BEGIN;
      EV_WAIT(1.4f);
      t->spr.rate = 1;
      spr_play(&t->spr, A_theo_recoverGround, false);
      EV_WAIT(1.6f);
      memset(&s->move, 0, sizeof s->move), s->move.target = v2(ge->x - 50, p->ent->y);
      EV_NEST(theo4_move(te, &s->move));
      EV_WAIT(0.2f);
      EV_END;
    }
    case 3: {   /* LookAtLever */
      EV_BEGIN;
      memset(&s->move, 0, sizeof s->move), s->move.target = v2(ge->x + 7, te->y);
      EV_NEST(theo4_move(te, &s->move));
      p->facing = 1;
      t->spr.sx = -1;
      EV_END;
    }
    case 4: {   /* PullLever */
      EV_BEGIN;
      s->pwalk = walk_exact((int)ge->x - 7), s->pwalking = 1;
      t->spr.sx = -1;
      EV_WAIT(0.2f);
      spr_play(&t->spr, A_theo_pullVent, false);
      EV_WAIT(1);
      g->lever_t = 0.5f;   /* Lever.Play("pulled") */
      spr_play(&t->spr, A_theo_fallVent, false);
      EV_WAIT(0.6f);
      level_shake(0.3f);
      EV_WAIT(0.5f);
      s->p = -1;
      EV_NEST(gond_pan(s, v2add(v2(ge->x, ge->y), v2(-160, -120)), 1, NULL));
      EV_WAIT(0.5f);
      if (s->clouds) {   /* the two Parallax: above the camera, the loop one above the bottom one */
        Clouds *k = ST(s->clouds, Clouds);
        Tex a, b;
        float ha = tex_get(T_bgs_04_bgCloudLoop, &a) ? a.fh : 200, hb = tex_get(T_bgs_04_bgCloud, &b) ? b.fh : 29;
        k->loop_pos = v2(0, g_level.cam.y - ha - hb);
        k->bottom_pos = v2(0, g_level.cam.y - hb);
        s->loop_at = k->bottom_pos.y;
        k->on = 1;
      }
      s->snap = 1;
      s->theo_x = te->x - ge->x;
      s->player_x = p->ent->x - ge->x;
      s->pwalking = 0;
      player_set_state(p, ST_FROZEN);
      level_shake(0.3f);
      s->speed = 32;
      g->rot_speed = 1;
      s->state = GS_TO_CENTER;
      EV_WAIT(1);
      EV_NEST(gond_move(s, true, 12, false));
      EV_WAIT(0.2f);
      t->spr.sx = -1;
      EV_END;
    }
    case 5:
    case 15: {   /* WaitABit */
      EV_BEGIN;
      EV_WAIT(1);
      EV_END;
    }
    case 6: {   /* WaitForCenter */
      EV_BEGIN;
      while (s->state != GS_CENTER) EV_YIELD;
      t->spr.sx = 1;
      EV_WAIT(1);
      EV_NEST(gond_move(s, false, -20, false));
      EV_WAIT(0.5f);
      EV_END;
    }
    case 7: {   /* SelfieThenStallsOut */
      EV_BEGIN;
      step_reset(&s->zoom2), s->zooming = 1;   /* Add(new Coroutine(Level.ZoomTo((160, 110), 2, 0.5))) */
      EV_WAIT(0.3f);
      t->spr.sx = 1;
      EV_WAIT(0.2f);
      s->to.x = s->theo_x - 8, s->panning = 2;   /* Add(new Coroutine(MovePlayerOnGondola(theoXOffset - 8))) */
      EV_WAIT(0.4f);
      spr_play(&t->spr, A_theo_holdOutPhone, false);
      EV_WAIT(1.5f);
      s->theo_x += 4, s->player_x += 4;
      g->rot_speed = -1;
      s->state = GS_STOPPED;
      spr_play(&t->spr, A_theo_takeSelfieImmediate, false);
      {   /* Add(new Coroutine(PanCamera(... 0.3, CubeOut))) */
        V2 d = v2norm(v2sub(g->dest, v2(ge->x, ge->y)));
        s->to = v2add(v2add(v2(ge->x, ge->y), v2mul(d, 32)), v2(-160, -120));
        s->p = -1, s->panning = 1;
      }
      EV_WAIT(0.5f);
      ch1_flash(0xFFFFFF);
      s->evil = ch2_bdummy_new(v2(0, 0));
      if (s->evil) {
        V2 at = gond_floor(s, -24, 20);
        s->evil->x = at.x, s->evil->y = at.y;
        ch2_bdummy_appear(s->evil);
        ch2_bdummy_floatness(s->evil, 0);
        s->evil->depth = -1000000, ents_mark_unsorted();
      }
      s->state = GS_SHAKING;
      s->panning = 0;
      s->p = -1;
      EV_NEST(gond_pan(s, v2add(v2(ge->x, ge->y), v2(-160, -120)), 1, NULL));
      EV_WAIT(1);
      EV_END;
    }
    case 8: {   /* MovePlayerLeft */
      EV_BEGIN;
      EV_NEST(gond_move(s, false, -20, false));
      t->spr.sx = -1;
      EV_WAIT(0.5f);
      EV_NEST(gond_move(s, false, 20, false));
      EV_WAIT(0.5f);
      EV_NEST(gond_move(s, false, -10, false));
      EV_WAIT(0.5f);
      p->facing = 1;
      EV_END;
    }
    case 9: {   /* SnapLeverOff */
      EV_BEGIN;
      EV_NEST(gond_move(s, true, 7, true));
      spr_play(&t->spr, A_theo_pullVent, false);
      EV_WAIT(1);
      spr_play(&t->spr, A_theo_fallVent, false);
      EV_WAIT(1);
      g->lever_broken = 1, g->lever_speed = v2(240, -130);   /* BreakLever */
      level_shake(0.3f);
      EV_WAIT(2.5f);
      EV_END;
    }
    case 10: {   /* DarknessAppears (the tentacles of the dark are left out) */
      EV_BEGIN;
      EV_WAIT(0.25f);
      spr_play(&p->spr, A_player_tired, false);
      EV_WAIT(0.25f);
      if (s->evil) ch2_bdummy_vanish(s->evil);
      s->evil = NULL;
      EV_WAIT(0.3f);
      level_shake(0.3f);
      for (s->sp = 0; s->sp < 1; s->sp += DT / 2) {
        EV_YIELD;
        if (s->clouds) ST(s->clouds, Clouds)->fade = s->sp;
      }
      EV_WAIT(0.25f);
      EV_END;
    }
    case 11: {   /* DarknessConsumes */
      EV_BEGIN;
      level_shake(0.3f);
      EV_NEST(gond_move(s, true, 0, true));
      spr_play(&t->spr, A_theo_comfortStart, false);
      EV_END;
    }
    case 12: {   /* CantBreath */
      EV_BEGIN;
      level_shake(0.3f);
      EV_YIELD;
      EV_END;
    }
    case 13: {   /* StartBreathing */
      EV_BEGIN;
      s->breath = breath_new(true);
      while (!breath_done(s->breath)) EV_YIELD;
      if (s->clouds) ST(s->clouds, Clouds)->fade = 0;
      g->front_pull = 0, spr_play(&g->front, A_gondola_idle, false);   /* CancelPullSides */
      zoom_reset();
      EV_WAIT(0.5f);
      EV_WAIT(1);
      level_shake(0.3f);
      s->state = GS_CENTER;
      g->rot_speed = 0.5f;
      EV_WAIT(1.2f);
      EV_END;
    }
    case 14: {   /* Ascend */
      EV_BEGIN;
      s->state = GS_TO_END;
      while (s->state != GS_STOPPED) EV_YIELD;
      level_shake(0.3f);
      g->rot_speed = 0.5f;
      EV_WAIT(0.1f);
      while (g->rot > 0) EV_YIELD;
      g->rot = g->rot_speed = 0;
      level_shake(0.3f);
      s->snap = 0;
      p->state_locked = false;
      player_set_state(p, ST_DUMMY);
      p->ent->x = floorf(p->ent->x), p->ent->y = floorf(p->ent->y);
      for (int i = 0; i < 64 && collide_solid(p->ent, p->ent->x, p->ent->y); i++) p->ent->y--;
      te->y = p->ent->y;
      spr_play(&t->spr, A_theo_comfortRecover, false);
      t->spr.sx = 1;
      s->walk = walk_to(ge->x + 80);
      EV_NEST(player_walk(p, &s->walk));
      p->dummy_auto_animate = false;
      spr_play(&p->spr, A_player_tired, false);
      memset(&s->move, 0, sizeof s->move), s->move.target = v2(ge->x + 64, te->y);
      EV_NEST(theo4_move(te, &s->move));
      EV_WAIT(0.5f);
      EV_END;
    }
    case 16: {   /* TheoTakesOutPhone */
      EV_BEGIN;
      p->facing = 1;
      EV_WAIT(0.25f);
      spr_play(&t->spr, A_theo_usePhone, false);
      EV_WAIT(2);
      EV_END;
    }
    default: {   /* FaceTheo */
      EV_BEGIN;
      p->dummy_auto_animate = true;
      EV_WAIT(0.2f);
      p->facing = -1;
      EV_WAIT(0.2f);
      EV_END;
    }
  }
}
static void gond_end(Ent *e, bool skipped) {
  (void)e;
  level_complete_area(true, false);
  if (!skipped) g_wipe.modifier = 120, g_wipe.focus = v2(160, 90);
}
static void gond_update(Ent *e) {
  Gond *s = ST(e, Gond);
  Ent *te = s->theo, *ge = s->gondola;
  Theo4 *t = ST(te, Theo4);
  Gondola *g = gond(s);
  Player *p = &g_player;
  /* its parallel coroutines and tween */
  if (s->pwalking && !player_walk(p, &s->pwalk)) s->pwalking = 0;
  if (s->zooming && !zoom_to(&s->zoom2, v2(160, 110), 2, 0.5f)) s->zooming = 0;
  if (s->panning == 1 && !gond_pan(s, s->to, 0.3f, ease_cube_out)) s->panning = 0;
  if (s->panning == 2 && !gond_move(s, false, s->to.x, false)) s->panning = 0;
  if (s->tw.active) {
    tween_update(&s->tw);
    t->spr.sx = lerpf(1.3f, 1, s->tw.percent), t->spr.sy = lerpf(0.8f, 1, s->tw.percent);
  }
  /* Update: the gondola's ride */
  if (s->state == GS_TO_CENTER) {
    gond_towards(s, 0.5f);
    if (s->percent >= 0.5f) s->state = GS_CENTER;
  } else if (s->state == GS_CENTER) {
    if (s->clouds) {
      Clouds *k = ST(s->clouds, Clouds);
      V2 d = v2mul(v2norm(v2sub(g->dest, v2(ge->x, ge->y))), s->speed);
      k->offset = v2add(k->offset, v2mul(d, DT));
      k->loop_y = 1;
    }
  } else if (s->state == GS_SHAKING) {
    g_level.wind.x = -400;
    if (s->shake <= 0 && (g->rot == 0 || g->rot < -0.25f)) s->shake = 1, g->rot_speed = 0.5f;
    s->shake -= DT;
  } else if (s->state == GS_TO_END) {
    gond_towards(s, 1);
    if (s->percent >= 1) s->state = GS_STOPPED;
  }
  if (s->clouds && !ST(s->clouds, Clouds)->loop_y && g_level.cam.y + 180 < s->loop_at) ST(s->clouds, Clouds)->loop_y = 1;
  if (s->snap) {   /* AutoSnapCharacters */
    V2 a = gond_floor(s, s->theo_x, 52), b = gond_floor(s, s->player_x, 52);
    te->x = a.x, te->y = a.y;
    p->ent->x = b.x, p->ent->y = b.y;
    if (s->evil && s->evil->cls) {
      V2 c = gond_floor(s, -24, 20);
      s->evil->x = c.x, s->evil->y = c.y;
    }
  }
  Co *c = &s->cs.co;
  CO_BEGIN(c);
  player_set_state(p, ST_DUMMY);
  s->walk = walk_exact((int)ge->x + 16);
  CO_NEST(c, player_walk(p, &s->walk));
  while (!actor_on_ground(p->ent, 1)) CO_YIELD(c);
  s->evi = -1;
  CO_SAY(c, s->tb, "CH4_GONDOLA", gond_event, e);
  t->spr.sx = -1;   /* ShowPhoto */
  CO_WAIT(c, 0.25f);
  s->walk = walk_to(te->x + 5);
  CO_NEST(c, player_walk(p, &s->walk));
  CO_WAIT(c, 1);
  s->breath = ch2_selfie_new(2);   /* OpenRoutine("selfieGondola"), WaitForInput */
  CO_NEST(c, ch2_selfie_running(s->breath));
  cutscene_end(e);
  return;
  CO_END(c);
}
static const EntClass GOND = {.size = sizeof(Gond), .name = "CS04_Gondola", .update = gond_update};

/* NPC04_Theo */
static void theo4_update(Ent *e) {
  Theo4 *t = ST(e, Theo4);
  spr_update(&t->spr);
  if (t->started) return;
  Ent *ge = NULL;
  for (int i = 0; i < g_nents && !ge; i++)
    if (g_ents[i].cls == &GONDOLA && g_ents[i].dead != 1) ge = &g_ents[i];
  Player *p = level_player();
  if (ge && p && p->ent->x > e_left(ge) - 16) {
    t->started = 1;
    Ent *c = ent_new(&GOND, 0, 0);
    if (!c) return;
    c->collidable = 0, c->visible = 0;
    Gond *s = ST(c, Gond);
    s->theo = e, s->gondola = ge;
    s->clouds = ent_new(&CLOUDS, 0, 0);
    if (s->clouds) s->clouds->depth = 1 << 29, s->clouds->collidable = 0;
    level_register_complete();   /* OnBegin */
    cutscene_start(c, gond_end, false, true);
  }
}
static void theo4_render(Ent *e) { draw_npc(&ST(e, Theo4)->spr, e->x, e->y); }
static const EntClass THEO4 = {.size = sizeof(Theo4), .name = "NPC04_Theo", .update = theo4_update, .render = theo4_render};
static void new_theo4(const EData *d) {
  Ent *e = ent_new(&THEO4, d->x, d->y);
  if (!e) return;
  e->depth = 1000;
  e->visible = 0;
  ent_box(e, 8, 8, -4, -8);
  Theo4 *t = ST(e, Theo4);
  spr_init(&t->spr, SB_theo);
  spr_play(&t->spr, A_theo_idle, false);
}

bool ents_ch4(const EData *d) {
  wind_room_load(d);
  switch (d->type) {
    case ET_booster: new_booster(d); return true;
    case ET_moveBlock: new_moveblock(d); return true;
    case ET_cloud: new_cloud(d); return true;
    case ET_ridgeGate: new_ridge(d); return true;
    case ET_whiteblock: new_white(d); return true;
    case ET_gondola: new_gondola(d); return true;
    case ET_cliffside_flag: new_wflag(d); return true;
    case ET_cliffflag: new_cliffflags(d); return true;
    case ET_npc: {
      const char *n = EAS(d, npc, npc);
      if (!strcmp(n, "granny_04_cliffside")) new_granny4(d);
      else if (!strcmp(n, "theo_04_cliffside")) new_theo4(d);
      else return false;
      return true;
    }
  }
  return false;
}

bool trigs_ch4(const EData *d) {
  switch (d->type) {
    case TT_windTrigger: {
      Ent *e = ent_new(&WINDTRIG, d->x, d->y);
      if (!e) return true;
      ent_box(e, EA(d, windTrigger, width), EA(d, windTrigger, height), 0, 0);
      e->visible = 0;
      static const char *P[15] = {"None", "Left", "Right", "LeftStrong", "RightStrong", "LeftOnOff", "RightOnOff", "LeftOnOffFast",
                                  "RightOnOffFast", "Alternating", "LeftGemsOnly", "RightCrazy", "Down", "Up", "Space"};
      const char *s = EAS(d, windTrigger, pattern);
      for (int i = 0; i < 15; i++)
        if (!strcmp(s, P[i])) ST(e, WindTrig)->pattern = (uint8_t)i;
      return true;
    }
    case TT_windAttackTrigger: {
      Ent *e = ent_new(&WINDATTACK, d->x, d->y);
      if (e) ent_box(e, EA(d, windAttackTrigger, width), EA(d, windAttackTrigger, height), 0, 0), e->visible = 0;
      return true;
    }
  }
  return false;
}

/* this chapter's tables: in the memory it is given while it is played (res_chapter_ram) */
uint32_t ch4_ram(void) { return END_mb_btn_px; }
