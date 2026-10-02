#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("Os")   /* not drawn every frame: smaller over faster */
#endif
/* The Prologue: IntroCar, IntroCrusher, the bridge, Granny and her cutscene,
 * and the ending with the bird (CS00_Granny, CS00_Ending). Line by line from
 * the game's code. */
#include "npc.h"

/* ---------------------------------------------------------------- IntroCar */
typedef struct {
  float start_y;
  uint8_t had_rider;
} Car;
/* the car's two hitboxes: Hitbox(25, 4, -15, -17) and Hitbox(19, 4, 8, -11) */
static bool car_rect(const Ent *e, float l, float t, float r, float b) {
  float x = e->x, y = e->y;
  if (r > x - 15 && b > y - 17 && l < x + 10 && t < y - 13) return true;
  return r > x + 8 && b > y - 11 && l < x + 27 && t < y - 7;
}
static void car_update(Ent *e) {
  Car *c = ST(e, Car);
  plat_update(e);
  bool rider = jumpthru_has_player_rider(e);
  if (e->y > c->start_y && (!rider || e->y > c->start_y + 1)) plat_move_v(e, -10 * DT);
  if (e->y <= c->start_y && !c->had_rider && rider) plat_move_v(e, 2);
  c->had_rider = rider;
}
static void car_render(Ent *e) {
  Tex t;
  if (tex_get(T_scenery_car_body, &t)) gfx_tex_ex(T_scenery_car_body, e->x, e->y, t.fw / 2.f, (float)t.fh, 1, 1, 0, 0xFFFF, 255, 0);
}
static const EntClass CAR = {.size = sizeof(Car), .name = "introCar", .update = car_update, .render = car_render,
                             .collide_rect = car_rect, .kind = KIND_JUMPTHRU};
typedef struct { uint16_t tex, tint; uint8_t origin_bottom, center; } Pic;
static void pic_render(Ent *e) {
  Pic *p = ST(e, Pic);
  Tex t;
  if (!tex_get(p->tex, &t)) return;
  gfx_tex_ex(p->tex, e->x, e->y, p->center ? t.fw / 2.f : 0, p->origin_bottom ? (float)t.fh : 0, 1, 1, 0, p->tint, 255, 0);
}
static const EntClass PIC = {.size = sizeof(Pic), .name = "image", .render = pic_render};
static Ent *pic_new(uint16_t tex, float x, float y, int depth, uint16_t tint, bool center) {
  Ent *e = ent_new(&PIC, x, y);
  if (!e) return NULL;
  Pic *p = ST(e, Pic);
  p->tex = tex, p->tint = tint, p->origin_bottom = 1, p->center = center;
  e->depth = depth;
  e->collidable = 0;
  return e;
}

/* IntroPavement: a solid of 8 x 8 pieces, random but for the last two */
typedef struct { uint8_t cols, piece[48]; } Pavement;
static void pavement_render(Ent *e) {
  Pavement *p = ST(e, Pavement);
  for (int i = 0; i < p->cols; i++) gfx_tex_part(T_scenery_car_pavement, e->x + i * 8, e->y, p->piece[i] * 8, 0, 8, 8, 0, 0xFFFF, 255);
}
static const EntClass PAVEMENT = {.size = sizeof(Pavement), .name = "introPavement", .update = plat_update, .render = pavement_render,
                                  .kind = KIND_SOLID};

static void new_car(const EData *d) {
  Ent *e = ent_new(&CAR, d->x, d->y);
  if (!e) return;
  ST(e, Car)->start_y = d->y;
  e->depth = 1;
  e->ctype = COL_LIST;
  e->cx = -15, e->cy = -17, e->cw = 42, e->ch = 10;   /* the bounds of the two */
  e->safe = 1;
  Tex t;
  pic_new(T_scenery_car_wheels, d->x, d->y, 3, 0xFFFF, true);
  (void)t;
  if (g_session.area == 0) {
    float left = (float)g_level.room->x;
    int width = (int)(d->x - left - 48);
    Ent *pv = ent_new(&PAVEMENT, left, d->y);
    if (pv) {
      ent_box(pv, (float)width, 8, 0, 0);
      pv->depth = -10001;
      pv->safe = 1;
      Pavement *p = ST(pv, Pavement);
      p->cols = (uint8_t)(width / 8 > 48 ? 48 : width / 8);
      for (int i = 0; i < p->cols; i++) p->piece[i] = (uint8_t)(i >= p->cols - 2 ? (i != p->cols - 2 ? 3 : 2) : rndi(2));
    }
    pic_new(T_scenery_car_barrier, d->x + 32, d->y, -10, 0xFFFF, false);
    pic_new(T_scenery_car_barrier, d->x + 41, d->y, 5, 0xAD55 /* DarkGray */, false);
  }
}

/* ---------------------------------------------------------------- IntroCrusher */
typedef struct {
  V2 start, end;
  Co co;
  float time;
  int8_t shakex, shakey;
  float shake_timer;
  TileQ tiles[7 * 4];
  uint8_t tw, th;
} Crusher;
static void crusher_shake(Crusher *c, float t) { c->shake_timer = t; }
static void crusher_seq(Ent *e, Crusher *c) {
  Player *p;
  CO_BEGIN(&c->co);
  do {
    CO_YIELD(&c->co);
    p = level_player();
  } while (!p || !(p->ent->x >= e->x + 30) || !(p->ent->x <= e_right(e) + 8));
  crusher_shake(c, 1.2f);
  c->time = 1.2f;
  while (c->time > 0) {
    p = level_player();
    if (p && (p->ent->x >= e->x + e->cw - 8 || p->ent->x < e->x + 28)) {
      c->shake_timer = 0, c->shakex = c->shakey = 0;
      break;
    }
    CO_YIELD(&c->co);
    c->time -= DT;
  }
  for (int i = 2; i < e->cw; i += 4) {
    particles_emit(PL_MID, &P_FallingBlock_P_FallDustA, 2, v2(e->x + i, e->y), v2(4, 4), PI_F / 2);
    particles_emit(PL_MID, &P_FallingBlock_P_FallDustB, 2, v2(e->x + i, e->y), v2(4, 4), P_FallingBlock_P_FallDustB.direction);
  }
  c->time = 0;
  do {
    CO_YIELD(&c->co);
    c->time = approach(c->time, 1, 2 * DT);
    {
      float k = ease_cube_in(c->time);
      plat_move_to(e, c->start.x + (c->end.x - c->start.x) * k, c->start.y + (c->end.y - c->start.y) * k);
    }
  } while (!(c->time >= 1));
  for (int i = 0; i <= e->cw; i += 4) {
    particles_emit(PL_FG, &P_FallingBlock_P_FallDustA, 1, v2(e->x + i, e_bottom(e)), v2(4, 4), -PI_F / 2);
    particles_emit(PL_FG, &P_FallingBlock_P_LandDust, 1, v2(e->x + i, e_bottom(e)), v2(4, 4), i < e->cw / 2 ? PI_F : 0);
  }
  level_shake(0.3f);
  crusher_shake(c, 0.25f);
  CO_END(&c->co);
}
static void crusher_update(Ent *e) {
  Crusher *c = ST(e, Crusher);
  plat_update(e);
  if (c->shake_timer > 0) {   /* Shaker: a new shake every 0.04 s */
    c->shake_timer -= DT;
    if (level_on_interval(0.04f)) c->shakex = (int8_t)(rndi(3) - 1), c->shakey = (int8_t)(rndi(3) - 1);
    if (c->shake_timer <= 0) c->shakex = c->shakey = 0;
  }
  if (c->co.co >= 0) crusher_seq(e, c);
}
static void crusher_render(Ent *e) {
  Crusher *c = ST(e, Crusher);
  tiles_draw(c->tiles, c->tw, c->th, e->x + c->shakex, e->y + c->shakey, 0xFFFF, 255);
}
static const EntClass CRUSHER = {.size = sizeof(Crusher), .name = "introCrusher", .update = crusher_update, .render = crusher_render,
                                 .kind = KIND_SOLID};
static void new_crusher(const EData *d) {
  int w = (int)EA(d, introCrusher, width), h = (int)EA(d, introCrusher, height);
  if (w / 8 * (h / 8) > 7 * 4) return;
  Ent *e = ent_new(&CRUSHER, d->x, d->y);
  if (!e) return;
  ent_box(e, (float)w, (float)h, 0, 0);
  e->safe = 1;
  e->depth = -10501;
  Crusher *c = ST(e, Crusher);
  c->start = v2(d->x, d->y);
  c->end = ed_node(d, 0);
  c->tw = (uint8_t)(w / 8), c->th = (uint8_t)(h / 8);
  tiles_box('3', c->tw, c->th, c->tiles);
  if (level_visited("1") || level_visited("0b")) {
    e->x = c->end.x, e->y = c->end.y;
    c->co.co = -1;
  }
}

/* ---------------------------------------------------------------- Bridge */
#define MAX_TILES 100
typedef struct {
  uint8_t img;               /* its piece of scenery/bridge: 0..10 */
  uint8_t fallen;
  float shake_timer, speed_y, color_lerp;
  int8_t shakex, shakey;
  float rot[5], dy[5];       /* the 16-wide pieces: their images falling */
} BTile;
static const uint8_t TILE_X[11] = {0, 16, 24, 32, 40, 48, 56, 64, 72, 80, 96}, TILE_W[11] = {16, 8, 8, 8, 8, 8, 8, 8, 8, 16, 8};
static void btile_update(Ent *e) {
  BTile *t = ST(e, BTile);
  plat_update(e);
  bool wide = TILE_W[t->img] == 16;
  if (!t->fallen) return;
  if (t->shake_timer > 0) {
    t->shake_timer -= DT;
    if (level_on_interval(0.02f)) t->shakex = (int8_t)(rndi(3) - 1), t->shakey = (int8_t)(rndi(3) - 1);
    if (t->shake_timer <= 0) {
      e->collidable = 0;
      level_shake(0.1f);
      if (wide)
        for (int i = 0, y = 0; y < 52; y += i ? 12 : 24, i++)
          if (e->y - 8 + y > e->y + 4) dust_burst(v2(e->x + 8, e->y - 8 + y), -PI_F / 2, 8);
    }
    return;
  }
  t->color_lerp = approach(t->color_lerp, 1, 10 * DT);
  t->shakex = t->shakey = 0;
  if (wide) {
    for (int i = 0; i < 5; i++) {
      t->rot[i] -= (i % 2 ? 1.f : -1.f) * DT * i * 2;
      t->dy[i] += i * DT * 16;
    }
    t->speed_y = approach(t->speed_y, 120, 600 * DT);
  } else
    t->speed_y = approach(t->speed_y, 200, 900 * DT);
  plat_move_v(e, t->speed_y * DT);
  if (e_top(e) > 220) ent_remove(e);
}
static void btile_render(Ent *e) {
  BTile *t = ST(e, BTile);
  int x = TILE_X[t->img], w = TILE_W[t->img];
  /* the first image greys as it falls (Color.Lerp(White, Gray)) */
  int g = (int)(255 - (255 - 128) * t->color_lerp);
  uint16_t tint = rgb((uint32_t)(g << 16 | g << 8 | g));
  if (w == 16) {
    for (int i = 0, y = 0, h = 24; y < 52; y += h, h = 12, i++) {
      float ox = i == 0 ? t->shakex : 0, oy = i == 0 ? t->shakey : 0;
      gfx_tex_part_ex(T_scenery_bridge, e->x + 8 + ox, e->y + y - 8 + t->dy[i] + oy, x, y, w, h < 52 - y ? h : 52 - y, 8, 0, 1,
                      1, t->rot[i], i == 0 ? tint : 0xFFFF, 255, 0);
    }
  } else
    gfx_tex_part_ex(T_scenery_bridge, e->x + w / 2.f + t->shakex, e->y - 8 + t->shakey, x, 0, w, 52, w / 2.f, 0, 1, 1, 0, tint, 255, 0);
}
static const EntClass BTILE = {.size = sizeof(BTile), .name = "bridgeTile", .update = btile_update, .render = btile_render,
                               .kind = KIND_JUMPTHRU};
static void btile_fall(Ent *e, float timer) {
  BTile *t = ST(e, BTile);
  if (!t->fallen) t->fallen = 1, t->shake_timer = timer;
}

typedef struct {
  uint16_t tiles[MAX_TILES];   /* g_ents indices, in order */
  int n;
  float width, collapse_timer;
  uint8_t can_collapse, end_a, end_b, ending;
} BridgeS;
static void bridge_take(BridgeS *b, int i, float timer) {
  btile_fall(&g_ents[b->tiles[i]], timer);
  memmove(b->tiles + i, b->tiles + i + 1, sizeof(uint16_t) * (size_t)(b->n - i - 1));
  b->n--;
}
static void bridge_update(Ent *e) {
  BridgeS *b = ST(e, BridgeS);
  Player *p = level_player();
  if (p && p->dead) p = NULL;
  if (!b->can_collapse) {
    if (p && p->ent->x >= e->x + 112) {
      b->can_collapse = b->end_a = b->end_b = 1;
      for (int i = 0; i < 11 && b->n; i++) bridge_take(b, 0, rnd_rangef(0.1f, 0.5f));
    }
  } else if (b->n > 0) {
    if (!p) return;
    if (b->end_a && p->ent->x > e->x + b->width - 216) {
      b->end_a = 0;
      for (int j = 0; j < 5 && b->n >= 8; j++) bridge_take(b, b->n - 8, rnd_rangef(0.1f, 0.5f));
    } else if (b->end_b && p->ent->x > e->x + b->width - 104) {
      b->end_b = 0;
      for (int k = 0; k < 7 && b->n > 0; k++) bridge_take(b, b->n - 1, rnd_rangef(0.1f, 0.3f));
    } else if (b->collapse_timer > 0) {
      b->collapse_timer -= DT;
      if (b->n >= 5 && p->ent->x >= g_ents[b->tiles[4]].x) bridge_take(b, 0, 0.2f);
    } else {
      bridge_take(b, 0, 0.2f);
      b->collapse_timer = 0.2f;
    }
  } else if (!b->ending)
    b->ending = 1;
}
static const EntClass BRIDGE = {.size = sizeof(BridgeS), .name = "bridge", .update = bridge_update};
static Ent *g_bridge;
static void new_bridge(const EData *d) {
  Ent *e = ent_new(&BRIDGE, d->x, d->y);
  if (!e) return;
  g_bridge = e;
  e->collidable = 0;
  e->visible = 0;
  BridgeS *b = ST(e, BridgeS);
  b->width = EA(d, bridge, width);
  float gap0 = ed_node(d, 0).x, gap1 = ed_node(d, 1).x;   /* Added: the nodes' x in the level (its Bounds.Left) */
  /* Calc.PushRandom(1): the same pieces as the game's */
  NetRandom r;
  rnd_seed(&r, 1);
  float x = d->x;
  int k = 0;
  while (x < d->x + b->width) {
    int img = (k < 2 || k > 7) ? k : 2 + rnd_range(&r, 6);
    if ((x < gap0 || x >= gap1) && b->n < MAX_TILES) {
      Ent *t = ent_new(&BTILE, x, d->y);
      if (t) {
        ent_box(t, (float)TILE_W[img], 5, 0, 0);
        ST(t, BTile)->img = (uint8_t)img;
        b->tiles[b->n++] = (uint16_t)(t - g_ents);
      }
    }
    x += TILE_W[img];
    k = (k + 1) % 11;
  }
}

/* ---------------------------------------------------------------- Granny: NPC00_Granny and CS00_Granny */
typedef struct {
  Sprite spr;
  uint16_t haha;    /* its Hahaha */
  uint8_t talking;
} Granny;
static const EntClass GRANNY;

typedef struct {
  Cutscene cs;
  Ent *granny, *tb;
  float end_x;
  Step step, zoom;
  Walk walk;
  Co ev;            /* the textbox's events, one at a time */
  int ev_index;
  uint8_t zooming;
} GrannyScene;

static Ent *granny_scene;
/* Textbox.Say's events: Meet, RunAlong, LaughAndAirQuotes, Laugh, StopLaughing, OminousZoom, PanToMaddy */
static bool granny_event(void *ctx, int index) {
  Ent *e = ctx;
  GrannyScene *s = ST(e, GrannyScene);
  Granny *g = ST(s->granny, Granny);
  Player *p = &g_player;
  Co *c = &s->ev;
  if (s->ev_index != index) {
    s->ev_index = index;
    memset(c, 0, sizeof *c);
  }
#define EV_BEGIN                    \
  if (c->wait > 0) {                \
    c->wait -= DT;                  \
    return false;                   \
  }                                 \
  switch (c->co) {                  \
    case 0:
#define EV_YIELD_(n)   \
  do {                 \
    c->co = (n);       \
    return false;      \
    case (n):;         \
  } while (0)
#define EV_YIELD EV_YIELD_(__COUNTER__ + 1)
#define EV_WAIT(t)    \
  do {                \
    c->wait = (t);    \
    EV_YIELD;         \
  } while (0)
#define EV_NEST(call)              \
  do {                             \
    EV_YIELD;                      \
    while (call) EV_YIELD;         \
    EV_YIELD;                      \
  } while (0)
#define EV_END \
  default:;    \
  }            \
  return true
  switch (index) {
    case 0: {   /* Meet */
      EV_BEGIN;
      EV_WAIT(0.25f);
      g->spr.sx = signf(p->ent->x - s->granny->x);
      s->walk = walk_to(s->granny->x - 20);
      EV_NEST(player_walk(p, &s->walk));
      p->facing = 1;
      EV_WAIT(0.8f);
      EV_END;
    }
    case 1: {   /* RunAlong */
      EV_BEGIN;
      s->walk = walk_exact((int)s->end_x);
      EV_NEST(player_walk(p, &s->walk));
      EV_WAIT(0.8f);
      p->facing = -1;
      EV_WAIT(0.4f);
      g->spr.sx = 1;
      step_reset(&s->step);
      EV_NEST(zoom_to(&s->step, v2(210, 90), 2, 0.5f));
      EV_WAIT(0.2f);
      EV_END;
    }
    case 2: {   /* LaughAndAirQuotes */
      EV_BEGIN;
      EV_WAIT(0.6f);
      spr_play(&g->spr, A_granny_laugh, false);
      EV_WAIT(2);
      spr_play(&g->spr, A_granny_airQuotes, false);
      EV_END;
    }
    case 3: {   /* Laugh */
      EV_BEGIN;
      EV_YIELD;
      spr_play(&g->spr, A_granny_laugh, false);
      EV_END;
    }
    case 4:   /* StopLaughing */
      spr_play(&g->spr, A_granny_idle, false);
      return true;
    case 5: {   /* OminousZoom: the zoom goes on by itself */
      EV_BEGIN;
      step_reset(&s->zoom);
      s->zooming = 1;
      spr_play(&g->spr, A_granny_idle, false);
      EV_WAIT(0.2f);
      EV_END;
    }
    case 6: {   /* PanToMaddy */
      EV_BEGIN;
      while (s->zooming) EV_YIELD;
      EV_WAIT(0.2f);
      step_reset(&s->step);
      EV_NEST(zoom_across(&s->step, v2(210, 90), 2, 0.5f));
      EV_WAIT(0.2f);
      EV_END;
    }
  }
  return true;
}

static void granny_scene_end(Ent *e, bool skipped) {
  (void)skipped;
  GrannyScene *s = ST(e, GrannyScene);
  Granny *g = ST(s->granny, Granny);
  hahaha_enable(&g_ents[g->haha], true);
  spr_play(&g->spr, A_granny_laugh, false);
  g->spr.sx = 1;
  g_player.ent->x = s->end_x;
  g_player.facing = -1;
  player_set_state(&g_player, ST_NORMAL);
  level_set_flag("granny", true);
  zoom_reset();
  granny_scene = NULL;
}
static void granny_scene_update(Ent *e) {
  GrannyScene *s = ST(e, GrannyScene);
  Player *p = &g_player;
  if (s->zooming && !zoom_across(&s->zoom, v2(210, 100), 4, 3)) s->zooming = 0;   /* its own coroutine */
  Co *c = &s->cs.co;
  CO_BEGIN(c);
  player_set_state(p, ST_DUMMY);
  if (fabsf(p->ent->x - s->granny->x) < 20) {
    s->walk = walk_to(s->granny->x - 48);
    CO_NEST(c, player_walk(p, &s->walk));
  }
  p->facing = 1;
  CO_WAIT(c, 0.5f);
  s->ev_index = -1;
  CO_SAY(c, s->tb, "CH0_GRANNY", granny_event, e);
  step_reset(&s->step);
  CO_NEST(c, zoom_back(&s->step, 0.5f));
  cutscene_end(e);
  return;
  CO_END(c);
}
static const EntClass GRANNY_SCENE = {.size = sizeof(GrannyScene), .name = "CS00_Granny", .update = granny_scene_update};

static void granny_update(Ent *e) {
  Granny *g = ST(e, Granny);
  Player *p = level_player();
  if (p && !p->dead && !level_get_flag("granny") && !g->talking) {
    float left = (float)(g_level.room->x + 96);
    if (p->on_ground && p->ent->x >= left && p->ent->x <= e->x + 16 && fabsf(p->ent->y - e->y) < 4 &&
        p->facing == (int)signf(e->x - p->ent->x)) {
      g->talking = 1;
      Ent *cs = ent_new(&GRANNY_SCENE, 0, 0);
      if (cs) {
        cs->collidable = 0, cs->visible = 0;
        GrannyScene *s = ST(cs, GrannyScene);
        s->granny = e;
        s->end_x = e->x + 48;
        granny_scene = cs;
        cutscene_start(cs, granny_scene_end, true, false);
      }
    }
  }
  hahaha_enable(&g_ents[g->haha], g->spr.anim == A_granny_laugh);
  spr_update(&g->spr);
}
static void granny_render(Ent *e) {
  Granny *g = ST(e, Granny);
  Sprite s = g->spr;
  s.flipx = s.sx < 0;
  s.sx = fabsf(s.sx);
  spr_draw(&s, e->x, e->y);
}
static const EntClass GRANNY = {.size = sizeof(Granny), .name = "granny", .update = granny_update, .render = granny_render};
static void new_granny(const EData *d) {
  Ent *e = ent_new(&GRANNY, d->x, d->y);
  if (!e) return;
  e->depth = 1000;
  ent_box(e, 8, 8, -4, -8);
  Granny *g = ST(e, Granny);
  spr_init(&g->spr, SB_granny);
  spr_play(&g->spr, level_get_flag("granny") ? A_granny_laugh : A_granny_idle, false);
  Ent *h = hahaha_new(v2(d->x + 8, d->y - 4), NULL);
  if (h) {
    g->haha = (uint16_t)(h - g_ents);
    hahaha_enable(h, false);
  }
}

/* ---------------------------------------------------------------- CS00_Ending */
typedef struct {
  Cutscene cs;
  Ent *bird, *text;
  float t, percent;
  V2 from, to;
  uint8_t key_offed;
} Ending;
static bool ending_wait(Ending *s, float time) {   /* WaitFor: raw time */
  s->t += RAW_DT;
  if (s->t < time) return true;
  s->t = 0;
  return false;
}
/* PrologueEndingText: "You can do this.", a character every 1/20 s after 4 s, sliding in as it fades */
typedef struct { Co co; float fade[24]; int n, i; } EndText;
static void endtext_update(Ent *e) {
  EndText *t = ST(e, EndText);
  CO_BEGIN(&t->co);
  if (!e->eid) CO_WAIT(&t->co, 4);
  for (t->i = 0; t->i < t->n; t->i++)
    while ((t->fade[t->i] += DT * 20) < 1) CO_YIELD(&t->co);
  CO_END(&t->co);
}
static void endtext_render(Ent *e) {
  EndText *t = ST(e, EndText);
  char text[32];
  int n = dialog_clean("CH0_END", text, sizeof text);
  float w = font_measure(text, n, FONT_S);
  float x = 160 - w / 2, y = g_level.ending_text_y / 6 - font_line_height(FONT_S) / 2;
  gfx_hud(true);
  int k = 0;
  for (const char *s = text, *end = text + n; s < end; k++) {
    uint32_t c = utf8_next(&s, end);
    const char *nx = s;
    uint32_t next = s < end ? utf8_next(&nx, end) : 0;
    float f = k < 24 ? fminf(t->fade[k], 1) : 1;
    if (f > 0) font_draw_glyph(FONT_S, c, x, y - 8 * (1 - f) / 6, 0xD69A, (uint8_t)(f * 255));
    x += font_glyph_advance(FONT_S, c, next);
  }
  gfx_hud(false);
}
static const EntClass ENDTEXT = {.size = sizeof(EndText), .name = "PrologueEndingText", .update = endtext_update,
                                 .render = endtext_render};
static Ent *endtext_new(bool instant) {
  Ent *e = ent_new(&ENDTEXT, 0, 0);
  if (!e) return NULL;
  e->tags = TAG_HUD | TAG_PERSISTENT;
  e->collidable = 0;
  e->eid = instant;
  char text[32];
  ST(e, EndText)->n = dialog_clean("CH0_END", text, sizeof text);
  if (instant)
    for (int i = 0; i < 24; i++) ST(e, EndText)->fade[i] = 1;
  return e;
}

static void ending_end(Ent *e, bool skipped) {
  Ending *s = ST(e, Ending);
  Player *p = &g_player;
  if (skipped) {
    if (s->bird && s->bird->cls) s->bird->visible = 0;
    p->ent->x = 2120, p->ent->y = 40;
    player_set_state(p, ST_DUMMY);
    p->dummy_auto_animate = false;
    spr_play(&p->spr, A_player_tired, false);
    p->speed = v2(0, 0);
    g_level.cam.y = (float)g_level.room->y - 3900;
    if (!s->text) s->text = endtext_new(true);
    else {
      EndText *t = ST(s->text, EndText);
      for (int i = 0; i < 24; i++) t->fade[i] = 1;
    }
    g_level.ending_text_y = 540;
  }
  g_time_rate = 1;
  g_level.pause_lock = true;
  g_level.ending_delay = 3;   /* EndingCutsceneDelay: then CompleteArea(spotlightWipe: false) */
}
static void ending_update(Ent *e) {
  Ending *s = ST(e, Ending);
  Player *p = &g_player;
  Bird *b = ST(s->bird, Bird);
  Co *c = &s->cs.co;
  CO_BEGIN(c);
  while (g_time_rate > 0) {
    CO_YIELD(c);
    g_level.shake_timer = 0;   /* StopShake */
    g_level.shake_vec = v2(0, 0);
    g_time_rate -= RAW_DT * 2;
  }
  g_time_rate = 0;
  player_set_state(p, ST_DUMMY);
  p->facing = 1;
  s->t = 0;
  CO_NEST(c, ending_wait(s, 1));
  b->facing = -1;
  spr_play(&b->spr, A_bird_fall, false);
  s->percent = 0;
  s->from = v2(s->bird->x, s->bird->y);
  s->to = b->start;
  while (s->percent < 1) {
    {
      float k = ease_quad_out(s->percent);
      s->bird->x = s->from.x + (s->to.x - s->from.x) * k, s->bird->y = s->from.y + (s->to.y - s->from.y) * k;
    }
    if (s->percent > 0.5f) spr_play(&b->spr, A_bird_fly, false);
    s->percent += RAW_DT * 0.5f;
    CO_YIELD(c);
  }
  s->bird->x = s->to.x, s->bird->y = s->to.y;
  dust_burst(s->to, -PI_F / 2, 12);
  spr_play(&b->spr, A_bird_idle, false);
  CO_NEST(c, ending_wait(s, 0.5f));
  spr_play(&b->spr, A_bird_peck, false);
  CO_NEST(c, ending_wait(s, 1.1f));
  {
    GuiControl ctl[3] = {{GC_DIR, 0, {1, -1}, ""}, {GC_TEXT, 0, {0, 0}, "+"}, {GC_BUTTON, IN_DASH, {0, 0}, ""}};
    tutorial_init(&g_tuts[0], s->bird, v2(0, -16), "tutorial_dash", ctl, 3);
    b->sub_step = 0;
  }
  CO_NEST(c, bird_show_tutorial(s->bird, &g_tuts[0], true));
  while (!(g_in.aim_x > 0 && g_in.aim_y < 0 && btn_pressed(&g_in.dash))) CO_YIELD(c);
  player_set_state(p, ST_BIRDDASHTUTORIAL);
  p->dashes = 0;
  g_session.inv_dashes = 1;
  p->inventory_dashes = 1;
  g_time_rate = 1;
  s->key_offed = 1;
  b->routine = NULL;   /* its tutorial is over: it hides the bubble, then startles and flies away */
  b->hide_then_fly = 1;
  CO_WAIT(c, 0.25f);
  b->fly_now = 1;
  while (!p->dead && !p->on_ground) CO_YIELD(c);
  CO_WAIT(c, 2);
  CO_WAIT(c, 2);
  /* PrologueEndingText and the hires snow fade in while the camera goes up 3900 */
  s->percent = 0;
  s->text = endtext_new(false);
  g_level.ending_text_y = -540;
  while (s->percent < 1) {
    s->percent += DT * 0.25f;
    g_level.ending_text_y = 540 - 1080 * (1 - ease_cube_inout(s->percent));
    g_level.cam.y = (float)g_level.room->y - 3900 * ease_cube_inout(s->percent);
    CO_YIELD(c);
  }
  cutscene_end(e);
  return;
  CO_END(c);
}
static const EntClass ENDING = {.size = sizeof(Ending), .name = "CS00_Ending", .update = ending_update};
void prologue_ending_start(Player *p, Ent *bird) {
  (void)p;
  Ent *e = ent_new(&ENDING, 0, 0);
  if (!e) return;
  e->collidable = 0, e->visible = 0;
  ST(e, Ending)->bird = bird;
  cutscene_start(e, ending_end, false, true);
}

/* ---------------------------------------------------------------- the factory */
bool ents_ch0(const EData *d) {
  switch (d->type) {
    case ET_introCar: new_car(d); return true;
    case ET_introCrusher: new_crusher(d); return true;
    case ET_bridge: new_bridge(d); return true;
    case ET_npc:
      if (!strncmp(EAS(d, npc, npc), "granny_00", 9)) {
        new_granny(d);
        return true;
      }
      return false;
  }
  return false;
}
