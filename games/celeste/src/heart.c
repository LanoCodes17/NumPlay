#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("Os")   /* not drawn every frame: smaller over faster */
#endif
/* Crystal hearts: HeartGem, its Poem and the AbsorbOrbs it bursts into.
 * Line by line from the game's code (the fake heart is Farewell's: not here). */
#include "cutscene.h"
#include "text.h"


/* ---------------------------------------------------------------- AbsorbOrb */
typedef struct {
  V2 a, b, c;              /* SimpleCurve: begin, end, control */
  V2 burst_dir, scale;
  float duration, percent, consume_delay, burst_speed, alpha, rot;
  Ent *into;
  uint8_t curve;           /* the curve is set (consumeDelay ran out) */
} Orb;

static V2 curve_at(const Orb *o, float t) {
  float u = 1 - t;
  return v2(u * u * o->a.x + 2 * u * t * o->c.x + t * t * o->b.x, u * u * o->a.y + 2 * u * t * o->c.y + t * t * o->b.y);
}
static void orb_scale(Orb *o, float k) {
  o->scale = v2(fminf(2, 0.5f + k * 0.02f), fmaxf(0.05f, 0.5f - k * 0.004f));
}
static void orb_update(Ent *e) {
  Orb *o = ST(e, Orb);
  V2 to = v2(0, 0);
  bool gone;
  if (o->into) {
    to = v2(e_cxm(o->into), e_cym(o->into));
    gone = o->into->dead || (o->into == level_player_ent() && g_player.dead);
  } else {
    Ent *p = level_player_ent();
    if (p) to = player_center(&g_player);
    gone = !p || g_player.dead;
  }
  V2 pos = v2(e->x, e->y);
  if (gone) {
    e->x += o->burst_dir.x * o->burst_speed * RAW_DT, e->y += o->burst_dir.y * o->burst_speed * RAW_DT;
    o->burst_speed = approach(o->burst_speed, 800, RAW_DT * 200);
    o->rot = atan2f(o->burst_dir.y, o->burst_dir.x);
    orb_scale(o, o->burst_speed);
    o->alpha = approach(o->alpha, 0, DT);
  } else if (o->consume_delay > 0) {
    e->x += o->burst_dir.x * o->burst_speed * RAW_DT, e->y += o->burst_dir.y * o->burst_speed * RAW_DT;
    o->burst_speed = approach(o->burst_speed, 0, RAW_DT * 120);
    o->rot = atan2f(o->burst_dir.y, o->burst_dir.x);
    orb_scale(o, o->burst_speed);
    o->consume_delay -= RAW_DT;
    if (o->consume_delay <= 0) {
      pos = v2(e->x, e->y);
      V2 mid = v2((pos.x + to.x) / 2, (pos.y + to.y) / 2);
      V2 n = safe_norm(v2sub(to, pos), 1), perp = v2(n.y, -n.x);
      float len = v2len(v2sub(pos, to)) * (0.05f + rndf() * 0.45f);
      V2 off = v2(perp.x * len, perp.y * len);
      float dx = to.x - pos.x, dy = to.y - pos.y;
      /* (the game compares |dy| with itself: only the first test can pass) */
      if (fabsf(dx) > fabsf(dy) && signf(off.x) != signf(dx)) off = v2(-off.x, -off.y);
      o->a = pos, o->b = to, o->c = v2add(mid, off);
      o->curve = 1;
      o->duration = 0.3f + rndf() * 0.25f;
    }
  } else {
    o->b = to;
    if (o->percent >= 1) {
      ent_remove(e);
      return;
    }
    o->percent = approach(o->percent, 1, RAW_DT / o->duration);
    float t = ease_cube_in(o->percent);
    V2 p = curve_at(o, t);
    e->x = p.x, e->y = p.y;
    float len = 0;   /* GetLengthParametric(10) */
    V2 q = o->a;
    for (int i = 1; i <= 10; i++) {
      V2 r = curve_at(o, i / 10.f);
      len += v2len(v2sub(r, q));
      q = r;
    }
    orb_scale(o, (t <= 0.5f ? t * 2 : 1 - (t - 0.5f) * 2) * len);
    o->alpha = 1 - t;
    V2 nx = curve_at(o, ease_cube_in(o->percent + 0.01f));
    o->rot = atan2f(nx.y - e->y, nx.x - e->x);
  }
}
static void orb_render(Ent *e) {
  Orb *o = ST(e, Orb);
  Tex t;
  if (tex_get(T_collectables_heartGem_orb, &t))
    gfx_tex_ex(T_collectables_heartGem_orb, e->x, e->y, t.fw / 2.f, t.fh / 2.f, o->scale.x, o->scale.y, o->rot, 0xFFFF,
               (uint8_t)(o->alpha * 255), 0);
  bloom_add(e->x, e->y, 1, 16);
}
static const EntClass ORB = {.name = "absorbOrb", .size = sizeof(Orb), .update = orb_update, .render = orb_render};

/* new AbsorbOrb(position, into): into the player when NULL */
Ent *absorb_orb_new(V2 at, Ent *into) {
  Ent *e = ent_new(&ORB, at.x, at.y);
  if (!e) return NULL;
  Orb *o = ST(e, Orb);
  e->tags = TAG_FROZEN_UPDATE;
  e->depth = -2000000;
  o->into = into;
  o->consume_delay = 0.7f + rndf() * 0.3f;
  o->burst_speed = 80 + rndf() * 40;
  o->burst_dir = angle_vec(rndf() * PI_F * 2, 1);
  o->alpha = 1;
  orb_scale(o, o->burst_speed);
  return e;
}

void absorb_orbs_remove(void) {
  for (int i = 0; i < g_nents; i++)
    if (g_ents[i].cls == &ORB) ent_remove(&g_ents[i]);
}

/* ---------------------------------------------------------------- the collection: one at a time */
#define POEM_PARTS 80
typedef struct {
  Co co;
  float t;
  Ent *heart, *poem, *walls[3];
  bool complete;
  uint8_t mode;
  /* Poem */
  float alpha, text_alpha, timer;
  Sprite spr;
  struct { float ang, pct, dur; } parts[POEM_PARTS];
  char text[160];
  TextDraw td[2];
} HeartRam;
#define HR (*(HeartRam *)(void *)g_chram[15])
uint32_t heart_ram(void) { return sizeof(HeartRam); }

/* ---------------------------------------------------------------- Poem */
static void part_reset(int i, float pct) {
  HR.parts[i].ang = rndf() * PI_F * 2;
  HR.parts[i].pct = pct;
  HR.parts[i].dur = 0.5f + rndf() * 0.5f;
}
static void poem_update(Ent *e) {
  (void)e;
  HR.timer += DT;
  for (int i = 0; i < POEM_PARTS; i++) {
    HR.parts[i].pct += DT / HR.parts[i].dur;
    if (HR.parts[i].pct > 1) part_reset(i, 0);
  }
  spr_update(&HR.spr);
}
static void poem_render(Ent *e) {
  (void)e;
  static const uint32_t colors[3] = {0x8cc7fa, 0xff668a, 0xfffc24};
  if (g_level.paused || HR.alpha <= 0) return;
  uint16_t col = rgb(colors[HR.mode]);
  gfx_hud(true);
  Tex t;
  /* the streaks into the heart (OVR "snow", stretched) */
  if (tex_get(T__snow, &t))
    for (int i = 0; i < POEM_PARTS; i++) {
      float n = ease_sine_in(HR.parts[i].pct), r = (1 - n) * 1920 / 6;
      V2 d = angle_vec(HR.parts[i].ang, 1);
      float a = (1 - n) * HR.alpha;
      if (a <= 0.01f) continue;
      gfx_tex_ex(T__snow, 160 + d.x * r, 90 + d.y * r, t.fw * t.scale / 2.f, t.fh * t.scale / 2.f, 1 + n * 2,
                 0.25f * (0.25f + (1 - n) * 0.75f), HR.parts[i].ang + PI_F, col, (uint8_t)(a * 255), 0);
    }
  Sprite s = HR.spr;
  s.alpha = (uint8_t)(s.alpha * HR.alpha);
  spr_draw(&s, 160, 90);
  if (HR.text[0] && HR.text_alpha > 0) {
    float w = text_width(HR.text, 1.5f);
    uint8_t a = (uint8_t)(HR.text_alpha * HR.alpha * 255);
    if (tex_get(T__poemside, &t))
      for (int side = -1; side <= 1; side += 2)
        gfx_tex_ex(T__poemside, (960 + side * (w / 2 + 64)) / 6, 90, t.fw * t.scale / 2.f, t.fh * t.scale / 2.f, 1, 1, 0,
                   col, a, 0);
    HR.td[0] = (TextDraw){HR.text, 960 + 2, 540 + 2, 0.5f, 0.5f, 1.5f, 0, (uint8_t)(a / 2)};
    HR.td[1] = (TextDraw){HR.text, 960, 540, 0.5f, 0.5f, 1.5f, col, a};
    text_draw(&HR.td[0]);
    text_draw(&HR.td[1]);
  }
  gfx_hud(false);
}
static const EntClass POEM = {.name = "poem", .update = poem_update, .render = poem_render};

/* ---------------------------------------------------------------- HeartGem */
typedef struct {
  Sprite spr;
  Wiggler scale_wig, move_wig;
  float timer;
  int8_t mdx, mdy;         /* moveWiggleDir x 127 */
  uint8_t ghost, collected, white, remove_cam;
} Heart;

static void heart_end(Ent *e) {   /* EndCutscene */
  g_level.frozen = false;
  g_level.no_retry = false;
  g_level.formation = false;
  g_time_rate = 1;
  if (HR.poem) ent_remove(HR.poem);
  for (int i = 0; i < 3; i++)
    if (HR.walls[i]) ent_remove(HR.walls[i]);
  HR.poem = NULL;
  memset(HR.walls, 0, sizeof HR.walls);
  HR.heart = NULL;
  ent_remove(e);
}

static const EntClass WALL = {.name = "invisibleBarrier", .update = plat_update, .kind = KIND_SOLID};
static Ent *wall_new(float x, float y, float w, float h) {   /* InvisibleBarrier */
  Ent *e = ent_new(&WALL, x, y);
  if (!e) return NULL;
  ent_box(e, w, h, 0, 0);
  e->visible = 0;
  e->safe = 1;
  e->tags = TAG_TRANSITION_UPDATE;
  return e;
}

/* CollectRoutine (raw time) */
static void heart_collect_routine(Ent *e) {
  Heart *h = ST(e, Heart);
  Co *c = &HR.co;
  Player *p = &g_player;
  Ent *pe = level_player_ent();
  CO_BEGIN_DT(c, RAW_DT);
  HR.complete = g_session.mode != M_A || g_session.area == 9;
  g_level.no_retry = true;
  if (HR.complete) berries_collect_all();
  {
    const Room *rm = g_level.room;
    HR.walls[0] = wall_new(rm->x + rm->w, rm->y, 8, rm->h);
    HR.walls[1] = wall_new(rm->x - 8, rm->y, 8, rm->h);
    HR.walls[2] = wall_new(rm->x, rm->y - 8, rm->w, 8);
  }
  h->white = 1;
  e->depth = -2000000;
  CO_YIELD(c);
  level_freeze(0.2f);
  CO_YIELD(c);
  g_time_rate = 0.5f;
  if (pe) pe->depth = -2000000;
  for (int i = 0; i < 10; i++) absorb_orb_new(v2(e->x, e->y), NULL);
  level_shake(0.3f);
  level_flash(0xFFFF, false);
  g_level.formation = true;
  g_level.formation_alpha = 1;
  e->visible = 0;
  for (HR.t = 0; HR.t < 2; HR.t += RAW_DT) {
    g_time_rate = approach(g_time_rate, 0, RAW_DT * 0.25f);
    CO_YIELD(c);
  }
  CO_YIELD(c);
  if (p->dead) CO_WAIT(c, 100);
  g_time_rate = 1;
  e->tags = TAG_FROZEN_UPDATE;
  g_level.frozen = true;
  /* RegisterAsCollected */
  g_session.heart = 1;
  g_session.dashes_at_level_start = g_session.dashes;
  {
    int before = save_unlocked_modes();
    save_register_heart();
    if (before < 3 && save_unlocked_modes() >= 3) g_session.unlocked_cside = 1;
  }
  if (HR.complete) {
    g_level.timer_stopped = true;
    level_register_complete();
  }
  /* the Poem */
  {
    static const char *const ids[10][3] = {
        {0}, {"fc", "fcr", "fcz"}, {"os", "osr", "osz"}, {"cr", "crr", "crz"}, {"cs", "csr", "csz"},
        {"t", "tr", "tz"}, {"tf", "tfr", "tfz"}, {"ts", "tsr", "tsz"}, {0}, {"mc", "mcr", "mcz"}};
    const char *id = g_session.area < 10 ? ids[g_session.area][g_session.mode] : NULL;
    HR.text[0] = 0;
    if (id) {
      char key[16] = "poem_";
      strcat(key, id);
      dialog_clean(key, HR.text, sizeof HR.text);
      text_auto_newline(HR.text, sizeof HR.text, 1024);
    }
    HR.mode = (uint8_t)g_session.mode;
    HR.alpha = 0;
    HR.text_alpha = 1;
    spr_init(&HR.spr, SB_gui_heartgem0 + HR.mode);
    spr_play(&HR.spr, A_gui_heartgem0_spin, true);
    HR.spr.alpha = (uint8_t)((g_session.mode == M_C ? 1 : 0.6f) * 255);
    for (int i = 0; i < POEM_PARTS; i++) part_reset(i, rndf());
    HR.poem = ent_new(&POEM, 0, 0);
    if (HR.poem) HR.poem->tags = TAG_HUD | TAG_FROZEN_UPDATE;
  }
  for (HR.t = 0; HR.t < 1; HR.t += RAW_DT) {
    HR.alpha = ease_cube_out(HR.t);
    CO_YIELD(c);
  }
  while (!btn_pressed(&g_in.confirm) && !btn_pressed(&g_in.back)) CO_YIELD(c);
  if (!HR.complete) {
    g_level.formation = false;
    for (HR.t = 0; HR.t < 1; HR.t += RAW_DT * 2) {
      HR.alpha = ease_cube_in(1 - HR.t);
      CO_YIELD(c);
    }
    if (pe) pe->depth = 0;
    heart_end(e);
    return;
  }
  wipe_start(WIPE_FADE, false, NULL);
  g_wipe.duration = 3.25f;
  CO_WAIT(c, 3.25f);
  level_complete_area(false, true);
  CO_END(c);
}

static void heart_update(Ent *e) {
  Heart *h = ST(e, Heart);
  h->timer += DT;
  /* sprite.Position: the bob and the bounce */
  if (h->collected && (!level_player_ent() || g_player.dead) && HR.heart == e) {
    heart_end(e);
    return;
  }
  int before = h->spr.frame;
  spr_update(&h->spr);
  if (h->spr.frame < before && h->spr.anim == A_heartgem0_spin && e->visible) wiggler_restart(&h->scale_wig);   /* OnLoop */
  wiggler_update(&h->scale_wig);
  h->spr.sx = h->spr.sy = 1 + h->scale_wig.value * 0.25f;
  if (h->collected && HR.heart == e) heart_collect_routine(e);
  if (e->dead) return;
  wiggler_update(&h->move_wig);
  if (!h->collected && level_on_interval(0.1f)) {
    static const PType *const shine[3] = {&P_HeartGem_P_BlueShine, &P_HeartGem_P_RedShine, &P_HeartGem_P_GoldShine};
    particles_emit(PL_MID, shine[g_session.mode % 3], 1, v2(e->x, e->y), v2(8, 8), 0);
  }
}

static void heart_collect(Ent *e) {
  Heart *h = ST(e, Heart);
  memset(&HR.co, 0, sizeof HR.co);
  HR.heart = e;
  h->collected = 1;
  if (h->remove_cam) level_remove_camera_offset_triggers();
}

static void heart_on_player(Ent *e, Player *p) {
  Heart *h = ST(e, Heart);
  if (h->collected || g_level.frozen) return;
  if (player_dash_attacking(p)) {
    heart_collect(e);
    return;
  }
  player_point_bounce(p, v2(e->x, e->y));
  wiggler_restart(&h->move_wig);
  wiggler_restart(&h->scale_wig);
  V2 d = safe_norm(v2sub(v2(e->x, e->y), player_center(p)), 1);
  if (d.x == 0 && d.y == 0) d = v2(0, 1);
  h->mdx = (int8_t)(d.x * 127), h->mdy = (int8_t)(d.y * 127);
}

static void heart_render(Ent *e) {
  Heart *h = ST(e, Heart);
  float m = h->move_wig.value * -8 / 127.f;
  float x = e->x + h->mdx * m, y = e->y + sinf(h->timer * 2) * 2 + h->mdy * m;
  spr_draw(&h->spr, x, y);
  if (h->white) {
    Sprite w = h->spr;
    w.bank = SB_heartGemWhite;
    spr_draw(&w, x, y);
  }
  if (!h->collected) {
    static const uint32_t light[3] = {0x7FFFFF, 0xFF7F7F, 0xFFEB7F};
    light_add(e->x, e->y, light[g_session.mode % 3], 1, 32, 64);
    bloom_add(e->x, e->y, 0.75f, 16);
  }
}

static const EntClass HEART = {.name = "blackGem", .size = sizeof(Heart), .update = heart_update, .render = heart_render,
                               .on_player = heart_on_player, .kind = KIND_PCOLLIDE};

/* new HeartGem(position) */
Ent *heart_spawn(V2 at) {
  Ent *e = ent_new(&HEART, at.x, at.y);
  if (!e) return NULL;
  Heart *h = ST(e, Heart);
  /* Awake */
  h->ghost = (save_mode()->flags & MS_HEART) != 0;
  static const uint8_t banks[3] = {SB_heartgem0, SB_heartgem1, SB_heartgem2};
  spr_init(&h->spr, h->ghost ? SB_heartGemGhost : banks[g_session.mode % 3]);
  spr_play(&h->spr, h->ghost ? A_heartGemGhost_spin : A_heartgem0_spin, true);
  if (h->ghost) h->spr.alpha = 204;
  ent_box(e, 16, 16, -8, -8);
  wiggler_init(&h->scale_wig, 0.5f, 4);
  wiggler_init(&h->move_wig, 0.8f, 2);
  h->move_wig.start_zero = true;
  return e;
}
static bool heart_new(const EData *d) {
  if (g_session.heart && g_session.mode == M_A) return true;   /* Level.LoadLevel */
  Ent *e = heart_spawn(v2(d->x, d->y));
  if (e) ST(e, Heart)->remove_cam = EAB(d, blackGem, removeCameraTriggers);
  return true;
}

/* ---------------------------------------------------------------- ForsakenCitySatellite */
enum { C_U, C_L, C_DR, C_UR, C_UL, NCODES };
static const uint32_t CODE_COLORS[NCODES] = {0xf0f0f0, 0x9171f2, 0x0a44e0, 0xb32d00, 0xffcd37};
static const uint8_t CODE[6] = {C_U, C_L, C_DR, C_UR, C_L, C_UL};
static const int8_t CODE_DIR[NCODES][2] = {{0, -1}, {-1, 0}, {1, 1}, {1, -1}, {-1, -1}};

typedef struct {   /* CodeBird */
  Co co;
  V2 speed, origin, target, from, to;
  float timer, reset, t, duration, heart_scale, sx, anim;
  uint16_t color, color_from;
  uint8_t code, routine, frame;   /* routine: aimless, dash, transform */
} Bird;
static void bird_render(Ent *e) {
  Bird *b = ST(e, Bird);
  Tex t;
  uint16_t tex = (uint16_t)(T_scenery_flutterbird_flap00 + b->frame);
  float y = e->y + sinf(b->timer * 2);
  if (tex_get(tex, &t))
    gfx_tex_ex(tex, e->x, y, t.fw / 2.f, t.fh / 2.f, b->sx, fabsf(b->sx) > 0 ? 1 - b->heart_scale : 1, 0, b->color, 255, 0);
  if (b->heart_scale > 0 && tex_get(T_collectables_heartGem_shape, &t))
    gfx_tex_ex(T_collectables_heartGem_shape, e->x, e->y, t.fw / 2.f, t.fh / 2.f, b->heart_scale, b->heart_scale, 0, 0xFFFF,
               255, 0);
}
static void bird_aim(Ent *e, Bird *b, V2 d) {
  if (signf(d.x) != 0) b->sx = signf(d.x) * (b->routine == 2 ? 1 - b->heart_scale : 1);
}
static void bird_routine(Ent *e) {
  Bird *b = ST(e, Bird);
  Co *c = &b->co;
  CO_BEGIN(c);
  if (b->routine == 1) goto dash;
  if (b->routine == 2) goto transform;
  /* AimlessFlightRoutine */
  b->speed = v2(0, 0);
  for (;;) {
    b->target = v2add(b->origin, angle_vec(rndf() * PI_F * 2, 16 + rndf() * 40));
    for (b->reset = 0; b->reset < 1;) {
      if (v2len(v2sub(b->target, v2(e->x, e->y))) <= 8) break;
      V2 d = safe_norm(v2sub(b->target, v2(e->x, e->y)), 1);
      b->speed = v2add(b->speed, v2mul(d, 420 * DT));
      if (v2len(b->speed) > 90) b->speed = safe_norm(b->speed, 90);
      e->x += b->speed.x * DT, e->y += b->speed.y * DT;
      b->reset += DT;
      bird_aim(e, b, d);
      CO_YIELD(c);
    }
  }
dash:   /* DashRoutine */
  for (b->t = 0.25f; b->t > 0; b->t -= DT) {
    b->speed = approach_v(b->speed, v2(0, 0), 200 * DT);
    e->x += b->speed.x * DT, e->y += b->speed.y * DT;
    CO_YIELD(c);
  }
  {
    V2 dash = safe_norm(v2(CODE_DIR[b->code][0], CODE_DIR[b->code][1]), 1);
    b->from = v2(e->x, e->y);
    b->to = v2add(b->origin, v2mul(dash, 8));
    if (signf(b->to.x - b->from.x) != 0) b->sx = signf(b->to.x - b->from.x);
  }
  for (b->t = 0; b->t < 1; b->t += DT * 1.5f) {
    float k = ease_cube_inout(b->t);
    e->x = b->from.x + (b->to.x - b->from.x) * k, e->y = b->from.y + (b->to.y - b->from.y) * k;
    CO_YIELD(c);
  }
  e->x = b->to.x, e->y = b->to.y;
  CO_WAIT(c, 0.2f);
  {
    V2 dash = safe_norm(v2(CODE_DIR[b->code][0], CODE_DIR[b->code][1]), 1);
    if (dash.x != 0) b->sx = signf(dash.x);
    b->speed = v2mul(dash, 300);
  }
  for (b->t = 0.4f; b->t > 0; b->t -= DT) {
    V2 dash = safe_norm(v2(CODE_DIR[b->code][0], CODE_DIR[b->code][1]), 1);
    if (b->t > 0.1f && level_on_interval(0.02f))
      particles_emit_c(PL_BG, &P_Player_P_DashA, 1, v2(e->x, e->y), v2(2, 2), CODE_COLORS[b->code] | 0xFF000000u,
                       atan2f(dash.y, dash.x));
    b->speed = approach_v(b->speed, v2(0, 0), 800 * DT);
    e->x += b->speed.x * DT, e->y += b->speed.y * DT;
    CO_YIELD(c);
  }
  CO_WAIT(c, 0.4f);
  b->routine = 0;
  b->co = (Co){0};
  return;
transform:   /* TransformRoutine */
  b->color_from = b->color;
  for (b->t = 0; b->t < 1; b->t += DT / b->duration) {
    V2 d = safe_norm(v2sub(b->origin, v2(e->x, e->y)), 1);
    b->speed = v2add(b->speed, v2mul(d, 400 * DT));
    float mx = fmaxf(20, (1 - b->t) * 200);
    if (v2len(b->speed) > mx) b->speed = safe_norm(b->speed, mx);
    e->x += b->speed.x * DT, e->y += b->speed.y * DT;
    b->color = blend565(b->color_from, scale565(0xFFFF, (int)(b->t * 256)), (int)(b->t * 256));
    b->heart_scale = fmaxf(0, (b->t - 0.75f) * 4);
    if (d.x != 0) b->sx = fabsf(b->sx) * signf(d.x);
    b->sx = signf(b->sx) * (1 - b->heart_scale);
    CO_YIELD(c);
  }
  CO_END(c);
}
static void bird_update(Ent *e) {
  Bird *b = ST(e, Bird);
  b->timer += DT;
  if ((b->anim += DT) >= 0.08f) b->anim -= 0.08f, b->frame ^= 1;   /* "flap" */
  bird_routine(e);
}
static const EntClass CODEBIRD = {.name = "codeBird", .size = sizeof(Bird), .update = bird_update, .render = bird_render};

typedef struct {
  Co co, pulse_co;
  V2 bird_at, gem_at;
  float t, bloom, noise_t;
  Ent *gem;
  uint16_t pulse_color;
  uint8_t enabled, pulse_on, inputs[6], ninputs, i, unlocking, noise, birds[NCODES], order[NCODES];
} Sat;

static void sat_unlock(Ent *e) {   /* UnlockGem */
  Sat *s = ST(e, Sat);
  Co *c = &s->co;
  CO_BEGIN(c);
  level_set_flag("unlocked_satellite", true);
  s->enabled = 0;
  CO_WAIT(c, 0.25f);
  CO_YIELD(c);
  g_level.frozen = true;
  e->tags = TAG_FROZEN_UPDATE;
  s->bloom = 0;
  for (int i = 0; i < NCODES; i++) {
    Ent *b = &g_ents[s->birds[i]];
    if (b->cls != &CODEBIRD) continue;
    Bird *bd = ST(b, Bird);
    b->tags = TAG_FROZEN_UPDATE;
    bd->routine = 2, bd->duration = 3, bd->co = (Co){0};
  }
  while (s->bloom < 1) {
    s->bloom += DT / 3;
    CO_YIELD(c);
  }
  CO_WAIT(c, 0.25f);
  for (int i = 0; i < NCODES; i++)
    if (g_ents[s->birds[i]].cls == &CODEBIRD) ent_remove(&g_ents[s->birds[i]]);
  particles_emit(PL_FG, &P_BirdNPC_P_Feather, 24, s->bird_at, v2(4, 4), 0);
  s->gem = heart_spawn(s->bird_at);
  if (s->gem) s->gem->tags = TAG_FROZEN_UPDATE;
  CO_YIELD(c);
  if (s->gem) wiggler_restart(&ST(s->gem, Heart)->scale_wig);
  CO_WAIT(c, 0.85f);
  for (s->t = 0; s->t < 1; s->t += DT) {
    CO_YIELD(c);
    if (s->gem) {   /* SimpleCurve from where it was to its spot, 64 above the middle */
      V2 a = s->bird_at, b = s->gem_at, m = v2((a.x + b.x) / 2, (a.y + b.y) / 2 - 64);
      float k = ease_cube_inout(s->t), u = 1 - k;
      s->gem->x = u * u * a.x + 2 * u * k * m.x + k * k * b.x;
      s->gem->y = u * u * a.y + 2 * u * k * m.y + k * k * b.y;
    }
  }
  CO_WAIT(c, 0.5f);
  s->bloom = 0;
  g_level.frozen = false;
  s->unlocking = 0;
  CO_END(c);
}

static void sat_pulse(Ent *e) {   /* PulseRoutine */
  Sat *s = ST(e, Sat);
  Co *c = &s->pulse_co;
  CO_BEGIN(c);
  s->pulse_on = 0;
  while (s->enabled) {
    CO_WAIT(c, 2);
    for (s->i = 0; s->i < 6 && s->enabled; s->i++) {
      s->pulse_color = rgb(CODE_COLORS[CODE[s->i]]);
      s->pulse_on = 1;
      CO_WAIT(c, 0.5f);
      s->pulse_on = 0;
      CO_WAIT(c, 0.2f);
    }
    {
      uint8_t order[NCODES];   /* birds.Shuffle() */
      for (int i = 0; i < NCODES; i++) order[i] = (uint8_t)i;
      for (int i = NCODES - 1; i > 0; i--) {
        int j = rndi(i + 1);
        uint8_t t = order[i];
        order[i] = order[j], order[j] = t;
      }
      memcpy(s->order, order, NCODES);
    }
    for (s->i = 0; s->i < NCODES && s->enabled; s->i++) {
      Ent *b = &g_ents[s->birds[s->order[s->i]]];
      if (b->cls == &CODEBIRD) ST(b, Bird)->routine = 1, ST(b, Bird)->co = (Co){0};
      CO_WAIT(c, 0.02f);
    }
  }
  s->pulse_on = 0;
  CO_END(c);
}

static void sat_update(Ent *e) {
  Sat *s = ST(e, Sat);
  if (s->pulse_co.co != -1) sat_pulse(e);
  if (s->unlocking) {
    sat_unlock(e);
    if (g_level.frozen) particles_update();   /* (the feathers' own system updates while frozen) */
  }
  if ((s->noise_t += DT) >= 0.05f) s->noise_t -= 0.05f, s->noise = (uint8_t)((s->noise + 1) % 3);   /* "static" */
}
static void sat_on_dash(Ent *e, V2 dir) {
  Sat *s = ST(e, Sat);
  int code = dir.y < 0 ? (dir.x < 0 ? C_UL : dir.x > 0 ? C_UR : C_U) : dir.y > 0 ? (dir.x > 0 ? C_DR : 255) : dir.x < 0 ? C_L : 255;
  if (s->unlocking) return;
  /* currentInputs: the last six */
  if (s->ninputs == 6) memmove(s->inputs, s->inputs + 1, 5), s->ninputs = 5;
  s->inputs[s->ninputs++] = (uint8_t)code;
  if (s->ninputs == 6 && !memcmp(s->inputs, CODE, 6) && g_level.cam.x + 32 < s->gem_at.x && s->enabled) {
    s->unlocking = 1;
    s->co = (Co){0};
  }
}
static void sat_render(Ent *e) {
  Sat *s = ST(e, Sat);
  Tex t;
  if (tex_get(T_objects_citysatellite_dish, &t)) gfx_tex(T_objects_citysatellite_dish, e->x - t.fw / 2.f, e->y - t.fh, 0, 0xFFFF, 255);
  if (s->pulse_on && tex_get(T_objects_citysatellite_light, &t))
    gfx_tex(T_objects_citysatellite_light, e->x - t.fw / 2.f, e->y - t.fh, 0, s->pulse_color, 255);
  gfx_tex(T_objects_citysatellite_computer, e->x + 8, e->y + 8, 0, 0xFFFF, 255);
  if (s->pulse_on) gfx_tex(T_objects_citysatellite_computerscreen, e->x + 8, e->y + 8, 0, s->pulse_color, 255);
  else gfx_tex((uint16_t)(T_objects_citysatellite_computerScreenNoise00 + s->noise), e->x + 8, e->y + 8, 0, 0xFFFF, 255);
  gfx_tex(T_objects_citysatellite_computerscreenShine, e->x + 8, e->y + 8, 0, 0xFFFF, 255);
  if (s->pulse_on) bloom_add(e->x - 12, e->y - 44, 1, 8), bloom_add(e->x + 32, e->y + 20, 1, 8);
  if (s->bloom > 0) bloom_add(s->bird_at.x, s->bird_at.y, s->bloom, 32);
}
static const EntClass SATELLITE = {.name = "birdForsakenCityGem", .size = sizeof(Sat), .update = sat_update, .render = sat_render,
                                   .more = &(const EntMore){.on_dash = sat_on_dash}};

static bool sat_new(const EData *d) {
  Ent *e = ent_new(&SATELLITE, d->x, d->y);
  if (!e) return true;
  Sat *s = ST(e, Sat);
  e->depth = 8999;
  s->bird_at = ed_node(d, 0), s->gem_at = ed_node(d, 1);
  /* Added */
  bool unlocked = level_get_flag("unlocked_satellite");
  s->enabled = !g_session.heart && !unlocked;
  if (s->enabled)
    for (int i = 0; i < NCODES; i++) {
      Ent *b = ent_new(&CODEBIRD, s->bird_at.x, s->bird_at.y);
      if (!b) continue;
      Bird *bd = ST(b, Bird);
      bd->code = (uint8_t)i;
      bd->origin = s->bird_at;
      bd->timer = rndf();
      bd->color = rgb(CODE_COLORS[i]);
      bd->sx = 1;
      s->birds[i] = (uint8_t)(b - g_ents);
    }
  if (!g_session.heart && unlocked) heart_spawn(s->gem_at);
  return true;
}

bool heart_create(const EData *d) {
  if (d->type == ET_blackGem) return heart_new(d);
  if (d->type == ET_birdForsakenCityGem) return sat_new(d);
  return false;
}
