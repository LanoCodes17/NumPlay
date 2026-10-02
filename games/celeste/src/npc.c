#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("Os")   /* not drawn every frame: smaller over faster */
#endif
/* Characters and props found in many chapters: BirdNPC (and its tutorial
 * bubbles), FlutterBird, Lamp, Wire, Hahaha, and NPC's helpers. Line by line
 * from the game's code. */
#include "npc.h"

/* ---------------------------------------------------------------- Lamp */
typedef struct { uint8_t broken; } Lamp;
static void lamp_render(Ent *e) {
  Lamp *l = ST(e, Lamp);
  /* scenery/lamp, a 16 x 80 half: the image's origin at its bottom middle */
  gfx_tex_part_ex(T_scenery_lamp, e->x, e->y, l->broken ? 16 : 0, 0, 16, 80, 8, 80, 1, 1, 0, 0xFFFF, 255, 0);
  if (!l->broken) bloom_add(e->x, e->y - 66, 1, 16);
}
static const EntClass LAMP = {.size = sizeof(Lamp), .name = "lamp", .render = lamp_render};

/* ---------------------------------------------------------------- Wire */
typedef struct { V2 from, to; float sine_x, sine_y; } WireS;
static void wire_render(Ent *e) {
  WireS *w = ST(e, WireS);
  float k = 8 * level_visual_wind();
  V2 sway = v2(sinf(w->sine_x + g_level.wind_sine_timer * 2) * k, sinf(w->sine_y + g_level.wind_sine_timer * 2.8f) * k);
  V2 c = v2add(v2add(v2mul(v2add(w->from, w->to), 0.5f), v2(0, 24)), sway);
  V2 prev = w->from;
  for (int i = 1; i <= 16; i++) {
    float t = i / 16.f, u = 1 - t;
    V2 p = v2(u * u * w->from.x + 2 * u * t * c.x + t * t * w->to.x, u * u * w->from.y + 2 * u * t * c.y + t * t * w->to.y);
    gfx_line(prev.x, prev.y, p.x, p.y, 0x5ACC /* 595866 */, 255);
    prev = p;
  }
}
static const EntClass WIRE = {.size = sizeof(WireS), .name = "wire", .render = wire_render};
V2 *wire_curve_begin(Ent *e) { return e && e->cls == &WIRE ? &ST(e, WireS)->from : NULL; }   /* Wire.Curve.Begin */

/* ---------------------------------------------------------------- FlutterBird */
typedef struct {
  Sprite spr;
  V2 start, from, to, ctl;
  float delay, p, fly_delay;
  Co co;
  int8_t dir;
  uint8_t flying, routine;   /* routine 0: IdleRoutine, 1: FlyAwayRoutine */
} Flutter;
static const EntClass FLUTTER;
static void flutter_fly_away(Ent *e, int dir, float delay) {
  Flutter *f = ST(e, Flutter);
  if (f->flying) return;
  f->flying = 1;
  f->routine = 1;
  f->dir = (int8_t)dir;
  f->fly_delay = delay;
  memset(&f->co, 0, sizeof f->co);   /* the idle routine is removed, the fly away one added */
}
static void flutter_idle(Ent *e, Flutter *f) {
  Player *pl;
  CO_BEGIN(&f->co);
  for (;;) {
    f->delay = 0.25f + rndf();
    for (f->p = 0; f->p < f->delay; f->p += DT) {
      pl = level_player();
      if (pl && !pl->dead && fabsf(pl->ent->x - e->x) < 48 && pl->ent->y > e->y - 40 && pl->ent->y < e->y + 8)
        flutter_fly_away(e, (int)signf(e->x - pl->ent->x), rndf() * 0.2f);
      if (f->routine) return;
      CO_YIELD(&f->co);
    }
    f->to = v2add(f->start, v2(-4 + rndf() * 8, 0));
    f->from = v2(e->x, e->y);
    f->spr.sx = signf(f->to.x - e->x);
    f->ctl = v2sub(v2mul(v2add(f->from, f->to), 0.5f), v2(0, 14));
    for (f->p = 0; f->p < 1; f->p += DT * 4) {
      {
        float t = f->p, u = 1 - t;
        e->x = u * u * f->from.x + 2 * u * t * f->ctl.x + t * t * f->to.x;
        e->y = u * u * f->from.y + 2 * u * t * f->ctl.y + t * t * f->to.y;
      }
      CO_YIELD(&f->co);
    }
    f->spr.sx = signf(f->spr.sx) * 1.4f;
    f->spr.sy = 0.6f;
    e->x = f->to.x, e->y = f->to.y;
  }
  CO_END(&f->co);
}
static void flutter_away(Ent *e, Flutter *f) {
  CO_BEGIN(&f->co);
  CO_WAIT(&f->co, f->fly_delay);
  spr_play(&f->spr, A_flutterBird_fly, false);
  f->spr.sx = -f->dir * 1.25f;
  f->spr.sy = 1.25f;
  {
    const PType *d = rndi(2) ? &P_Dust : &P_Dust;   /* Calc.Random.Choose(ParticleTypes.Dust) */
    particles_emit1(PL_FG, d, v2(e->x, e->y), -PI_F / 2);
  }
  f->from = v2(e->x, e->y);
  f->to = v2add(f->from, v2(f->dir * 4.f, -8));
  for (f->p = 0; f->p < 1; f->p += DT * 3) {
    e->x = f->from.x + (f->to.x - f->from.x) * ease_cube_out(f->p);
    e->y = f->from.y + (f->to.y - f->from.y) * ease_cube_out(f->p);
    CO_YIELD(&f->co);
  }
  e->depth = -10001;
  ents_mark_unsorted();
  f->spr.sx = -f->spr.sx;
  f->to = v2(f->dir * 8.f, -32);   /* the speed now */
  while (e->y + 8 > g_level.room->y) {
    f->to = v2add(f->to, v2(f->dir * 64 * DT, -128 * DT));
    e->x += f->to.x * DT, e->y += f->to.y * DT;
    if (level_on_interval(0.1f) && e->y > g_level.cam.y + 32)
      for (int i = 0; i < g_nents; i++) {
        Ent *o = &g_ents[i];
        if (o->cls == &FLUTTER && o->dead != 1 && fabsf(e->x - o->x) < 48 && fabsf(e->y - o->y) < 48 && !ST(o, Flutter)->flying)
          flutter_fly_away(o, f->dir, rndf() * 0.25f);
      }
    CO_YIELD(&f->co);
  }
  ent_remove(e);
  CO_END(&f->co);
}
static void flutter_update(Ent *e) {
  Flutter *f = ST(e, Flutter);
  f->spr.sx = approach(f->spr.sx, signf(f->spr.sx), 4 * DT);
  f->spr.sy = approach(f->spr.sy, 1, 4 * DT);
  spr_update(&f->spr);
  if (f->routine) flutter_away(e, f);
  else flutter_idle(e, f);
}
static void flutter_render(Ent *e) {
  Flutter *f = ST(e, Flutter);
  Sprite s = f->spr;
  s.flipx = s.sx < 0;
  s.sx = fabsf(s.sx);
  spr_draw(&s, e->x, e->y);
}
static const EntClass FLUTTER = {.size = sizeof(Flutter), .name = "flutterbird", .update = flutter_update, .render = flutter_render};

/* ---------------------------------------------------------------- Hahaha */
#define MAX_HA 6
/* each "ha": characters/oldlady/ha, frames 0,1,0,1,0,1,0,1,0,1,2..9 at 0.15 s, rising as it goes */
static const uint8_t HA_FRAMES[18] = {0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
typedef struct {
  float has[MAX_HA];   /* each one's Percent */
  int nhas, counter;
  float timer;
  const char *ifset;   /* the flag that turns it on */
  uint8_t enabled;
} Haha;
void hahaha_enable(Ent *e, bool on) {
  Haha *h = ST(e, Haha);
  if (!h->enabled && on) h->timer = 0, h->counter = 0;
  h->enabled = on;
}
static void haha_update(Ent *e) {
  Haha *h = ST(e, Haha);
  if (h->enabled) {
    h->timer -= DT;
    if (h->timer <= 0) {
      if (h->nhas < MAX_HA) h->has[h->nhas++] = 0;
      h->counter++;
      if (h->counter >= 3) h->counter = 0, h->timer = 1.5f;
      else h->timer = 0.6f;
    }
  }
  for (int i = h->nhas - 1; i >= 0; i--) {
    if (h->has[i] > 1) {
      memmove(&h->has[i], &h->has[i + 1], sizeof h->has[0] * (size_t)(h->nhas - i - 1));
      h->nhas--;
    } else
      h->has[i] += DT / (18 * 0.15f);   /* Duration: its 18 frames */
  }
  if (!h->enabled && h->ifset && *h->ifset && level_get_flag(h->ifset)) hahaha_enable(e, true);
}
static void haha_render(Ent *e) {
  Haha *h = ST(e, Haha);
  for (int i = 0; i < h->nhas; i++) {
    float p = h->has[i];
    int f = (int)(p * 18);
    uint16_t tex = (uint16_t)(T_characters_oldlady_ha00 + HA_FRAMES[f > 17 ? 17 : f]);
    Tex t;
    if (!tex_get(tex, &t)) continue;
    gfx_tex_ex(tex, e->x + p * 60, e->y - 10 - sinf(p * 13) * 4 + p * -16, t.fw / 2.f, t.fh / 2.f, 1, 1, 0, 0xFFFF, 255, 0);
  }
}
static const EntClass HAHAHA = {.size = sizeof(Haha), .name = "hahaha", .update = haha_update, .render = haha_render};
Ent *hahaha_new(V2 at, const char *ifset) {
  Ent *e = ent_new(&HAHAHA, at.x, at.y);
  if (!e) return NULL;
  e->depth = -10001;
  e->collidable = 0;
  ST(e, Haha)->ifset = ifset;
  ST(e, Haha)->enabled = 1;
  if (ifset && *ifset && !level_get_flag(ifset)) ST(e, Haha)->enabled = 0;
  return e;
}

/* ---------------------------------------------------------------- BirdTutorialGui */
Tutorial g_tuts[2];   /* (one bird shows its bubbles at a time) */
/* the calculator's keys (Input.GuiButton): a key cap with its name */
static const char *key_name(int action) {
  switch (action) {
    case IN_JUMP: return "OK";
    case IN_DASH: return "Backspace";
    case IN_GRAB: return "Toolbox";
    case IN_TALK: return "Backspace";
    default: return "Back";
  }
}
/* the width of a control in interface pixels */
static float control_width(const GuiControl *c) {
  switch (c->kind) {
    case GC_BUTTON: return font_measure(key_name(c->v), (int)strlen(key_name(c->v)), FONT_S) * 6 + 48 + 16;
    case GC_DIR: return 64 + 16;
    case GC_TEXT: return font_measure(c->text, (int)strlen(c->text), FONT_S) * 6;
    default: return 0;
  }
}
void tutorial_init(Tutorial *t, Ent *bird, V2 offset, const char *info_key, const GuiControl *controls, int n) {
  memset(t, 0, sizeof *t);
  t->bird = (uint16_t)(bird - g_ents);
  t->offset = offset;
  dialog_clean(info_key, t->info, sizeof t->info);
  t->info_w = font_measure(t->info, (int)strlen(t->info), FONT_S) * 6;
  t->n = (uint8_t)(n > 4 ? 4 : n);
  memcpy(t->controls, controls, sizeof(GuiControl) * t->n);
  for (int i = 0; i < t->n; i++) t->controls_w += control_width(&t->controls[i]);
}
void tutorial_update(Tutorial *t) {
  if (!t->added) return;
  t->scale = approach(t->scale, t->open ? 1.f : 0.f, RAW_DT * 8);
}

/* a key cap: a light frame, dark inside, its name (as tall as the text, and a margin) */
static float key_h(void) { return font_line_height(FONT_S) * 6 + 16; }
static void draw_key(float x, float y, const char *name, float sx) {
  float w = (font_measure(name, (int)strlen(name), FONT_S) * 6 + 48) * sx;
  float h = key_h();
  gfx_rect((x) / 6, (y - h / 2) / 6, w / 6, h / 6, 0xFFFF, 255);
  gfx_rect((x + 6) / 6, (y - h / 2 + 6) / 6, (w - 12) / 6, (h - 12) / 6, 0x2104, 255);
  if (sx > 0.5f) font_draw(name, (int)strlen(name), (x + w / 2) / 6 - font_measure(name, (int)strlen(name), FONT_S) / 2,
                           (y) / 6 - font_line_height(FONT_S) / 2, FONT_S, 0xFFFF, 255);
}
/* an arrow (Input.GuiDirection) */
static void draw_dir(float x, float y, V2 d, float sx) {
  float cx = (x + 32 * sx) / 6, cy = y / 6;
  V2 n = v2norm(d), p = v2(-n.y, n.x);
  for (int i = -1; i <= 1; i++) {
    float o = i * 0.5f;
    gfx_line(cx - n.x * 4 + p.x * o, cy - n.y * 4 + p.y * o, cx + n.x * 4 + p.x * o, cy + n.y * 4 + p.y * o, 0xFFFF, 255);
  }
  V2 tip = v2(cx + n.x * 5, cy + n.y * 5);
  gfx_line(tip.x, tip.y, tip.x - n.x * 3 + p.x * 3, tip.y - n.y * 3 + p.y * 3, 0xFFFF, 255);
  gfx_line(tip.x, tip.y, tip.x - n.x * 3 - p.x * 3, tip.y - n.y * 3 - p.y * 3, 0xFFFF, 255);
}

void tutorial_render(Tutorial *t) {
  if (!t->added || t->scale <= 0 || g_level.frozen || g_level.paused) return;
  Ent *b = &g_ents[t->bird];
  float px = (b->x + t->offset.x - floorf(g_level.cam.x)) * 6, py = (b->y + t->offset.y - floorf(g_level.cam.y)) * 6;
  float lh = font_line_height(FONT_S) * 6, kh = key_h();
  float w = (fmaxf(t->controls_w, t->info_w) + 64) * t->scale;
  float h = lh + kh + 48;   /* the info's line, the controls' row (the game's: two lines and 32) */
  float x = px - w / 2, y = py - h - 32;
  gfx_hud(true);
  gfx_rect((x - 6) / 6, (y - 6) / 6, (w + 12) / 6, (h + 12) / 6, 0xFFFF, 255);
  gfx_rect(x / 6, y / 6, w / 6, h / 6, 0x00A4 /* 061526 */, 255);
  for (int i = 0; i <= 36; i += 6) {   /* the bubble's tail, a screen pixel at a time */
    float n5 = (73 - i * 2) * t->scale;
    gfx_rect((px - n5 / 2) / 6, (y + h + i) / 6, n5 / 6, 1, 0xFFFF, 255);
    if (n5 > 12) gfx_rect((px - n5 / 2 + 6) / 6, (y + h + i) / 6, (n5 - 12) / 6, 1, 0x00A4, 255);
  }
  if (w > 3) {
    float cx = px, cy = y + 16;
    if (t->scale > 0.5f) font_draw(t->info, (int)strlen(t->info), cx / 6 - t->info_w / 12 * t->scale, cy / 6, FONT_S, 0x63DC, 255);
    cy += lh + 16 + kh / 2;
    float ox = -t->controls_w / 2;
    for (int i = 0; i < t->n; i++) {
      GuiControl *c = &t->controls[i];
      if (c->kind == GC_BUTTON) {
        ox += 8;
        draw_key(cx + ox * t->scale, cy, key_name(c->v), t->scale);
        ox += control_width(c) - 16 + 8;
      } else if (c->kind == GC_DIR) {
        ox += 8;
        draw_dir(cx + ox * t->scale, cy, c->dir, t->scale);
        ox += 64 + 8;
      } else if (c->kind == GC_TEXT) {
        float tw = control_width(c);
        if (t->scale > 0.5f) {
          float ty = cy / 6 - font_line_height(FONT_S) / 2;
          font_draw(c->text, (int)strlen(c->text), (cx + ox * t->scale) / 6, ty + 0.33f, FONT_S, 0x63DC, 255);
          font_draw(c->text, (int)strlen(c->text), (cx + ox * t->scale) / 6, ty - 0.33f, FONT_S, 0xFFFF, 255);
        }
        ox += tw + 1;
      }
    }
  }
  gfx_hud(false);
}

/* ---------------------------------------------------------------- BirdNPC */
static const EntClass BIRD;
static void bird_set_mode(Ent *e, int mode);

/* nested coroutines of the bird: each returns true while it runs (its state in Bird.sub) */
bool bird_caw(Ent *e) {   /* Caw */
  Bird *b = ST(e, Bird);
  if (!b->sub_started) {
    b->sub_started = 1;
    spr_play(&b->spr, A_bird_croak, false);
  }
  if (b->spr.anim == A_bird_croak && b->spr.frame < 9) return true;
  b->sub_started = 0;
  return false;
}
/* ShowTutorial: caw first if asked, then the bubble opens */
bool bird_show_tutorial(Ent *e, Tutorial *t, bool caw) {
  Bird *b = ST(e, Bird);
  if (b->sub_step == 0) {
    b->sub_step = caw ? 1 : 2;
    if (caw) {
      b->sub_started = 0;
      bird_caw(e);
      return true;
    }
  }
  if (b->sub_step == 1) {
    if (bird_caw(e)) return true;
    b->sub_step = 2;
    return true;   /* the nested Caw ends: an update */
  }
  if (b->sub_step == 2) {
    b->gui = t;
    t->open = true;
    t->added = true;
    b->sub_step = 3;
  }
  if (t->scale < 1) return true;
  b->sub_step = 0;
  return false;
}
bool bird_hide_tutorial(Ent *e) {
  Bird *b = ST(e, Bird);
  if (!b->gui) return false;
  b->gui->open = false;
  if (b->gui->scale > 0) return true;
  b->gui->added = false;
  b->gui = NULL;
  return false;
}
/* Startle: dust, a jump back, feathers */
bool bird_startle(Ent *e, float duration, V2 mult) {
  Bird *b = ST(e, Bird);
  if (!b->sub_started) {
    b->sub_started = 1;
    dust_burst(v2(e->x, e->y), -PI_F / 2, 8);
    spr_play(&b->spr, A_bird_jump, false);
    tween_start(&b->tween, duration);
    b->tween_new = 1;
    return true;
  }
  if (b->tween.active) return true;
  b->sub_started = 0;
  return false;
}
/* FlyAway: up and away, until above the room */
bool bird_fly_away(Ent *e, float up) {
  Bird *b = ST(e, Bird);
  if (!b->sub_started) {
    b->sub_started = 1;
    spr_play(&b->spr, A_bird_fly, false);
    b->facing = -b->facing;
    b->speed = v2(b->facing * 20.f, -40 * up);
  }
  if (e->y > g_level.room->y) {
    b->speed = v2add(b->speed, v2(b->facing * 140 * DT, -120 * up * DT));
    e->x += b->speed.x * DT, e->y += b->speed.y * DT;
    return true;
  }
  ent_remove(e);
  return false;
}
/* StartleAndFlyAway */
bool bird_startle_and_fly(Ent *e) {
  Bird *b = ST(e, Bird);
  switch (b->fly_step) {
    case 0:
      e->depth = -1000000;
      ents_mark_unsorted();
      {
        char flag[40];
        path2(flag, "bird_fly_away_", level_room_name(), -1);
        level_set_flag(flag, true);
      }
      b->sub_started = 0;
      b->fly_step = 1;
      return true;   /* yield return Startle(): it starts on the next update */
    case 1:
      if (bird_startle(e, 0.8f, v2(1, 1))) return true;
      b->fly_step = 2;
      return true;
    case 2:
      b->fly_step = 3;
      b->sub_started = 0;
      return true;
    default:
      return bird_fly_away(e, 1);
  }
}

static void bird_update(Ent *e) {
  Bird *b = ST(e, Bird);
  b->spr.sx = (float)b->facing;
  /* base.Update(): the sprite (raw time), then the routine, then tweens added before */
  spr_update(&b->spr);
  if (b->tween.active && !b->tween_new) {   /* Startle's tween */
    tween_update(&b->tween);
    float k = ease_cube_out(b->tween.percent);
    if (k < 0.5f && level_on_interval(0.05f)) particles_emit(PL_MID, &P_BirdNPC_P_Feather, 2, v2(e->x, e->y - 6), v2(4, 4), P_BirdNPC_P_Feather.direction);
    V2 v = v2(100 + (20 - 100) * k, -100 + (-20 + 100) * k);
    v.x *= -b->facing;
    e->x += v.x * DT, e->y += v.y * DT;
  }
  b->tween_new = 0;
  if (b->routine) b->routine(e);
  if (b->hide_then_fly && !bird_hide_tutorial(e)) b->hide_then_fly = 0;
  if (b->fly_now) {
    if (b->fly_now == 1) b->fly_step = 0, b->sub_started = 0, b->fly_now = 2;
    if (!bird_startle_and_fly(e)) b->fly_now = 0;
  }
  if (b->gui) tutorial_update(b->gui);
}
static void bird_render(Ent *e) {
  Bird *b = ST(e, Bird);
  Sprite s = b->spr;
  s.flipx = s.sx < 0;
  s.sx = fabsf(s.sx);
  spr_draw(&s, e->x, e->y);
  light_add(e->x, e->y - 8, 0xFFFFFF, 1, 8, 32);
  if (b->gui) tutorial_render(b->gui);
}
static const EntClass BIRD = {.size = sizeof(Bird), .name = "bird", .update = bird_update, .render = bird_render, .kind = KIND_ACTOR};

/* ClimbingTutorial */
static void climbing_tutorial(Ent *e) {
  Bird *b = ST(e, Bird);
  Player *p = &g_player;
  CO_BEGIN(&b->co);
  CO_WAIT(&b->co, 0.25f);
  while (fabsf(p->ent->x - e->x) > 120) CO_YIELD(&b->co);
  {
    /* tut1: "Climb", then "Hold" and the grab key; tut2: "Climb", the grab key + up */
    GuiControl a[2] = {{GC_TEXT, 0, {0, 0}, ""}, {GC_BUTTON, IN_GRAB, {0, 0}, ""}};
    dialog_clean("tutorial_hold", a[0].text, sizeof a[0].text);
    tutorial_init(&g_tuts[0], e, v2(0, -16), "tutorial_climb", a, 2);
    GuiControl c[3] = {{GC_BUTTON, IN_GRAB, {0, 0}, ""}, {GC_TEXT, 0, {0, 0}, "+"}, {GC_DIR, 0, {0, -1}, ""}};
    tutorial_init(&g_tuts[1], e, v2(0, -16), "tutorial_climb", c, 3);
  }
  b->first = 1;
  do {
    b->sub_step = 0;
    CO_NEST(&b->co, bird_show_tutorial(e, &g_tuts[0], b->first));
    b->first = 0;
    while (p->state != ST_CLIMB && p->ent->y > e->y) CO_YIELD(&b->co);
    if (p->ent->y > e->y) {
      CO_NEST(&b->co, bird_hide_tutorial(e));
      b->sub_step = 0;
      CO_NEST(&b->co, bird_show_tutorial(e, &g_tuts[1], false));
    }
    while (!p->dead && (!p->on_ground || p->state == ST_CLIMB)) CO_YIELD(&b->co);
    b->will_end = p->ent->y <= e->y + 4;
    CO_NEST(&b->co, bird_hide_tutorial(e));
  } while (!b->will_end);
  b->fly_step = 0;
  CO_NEST(&b->co, bird_startle_and_fly(e));
  CO_END(&b->co);
}

/* WaitRoutine (FlyAway) */
static void wait_routine(Ent *e) {
  Bird *b = ST(e, Bird);
  Player *p;
  CO_BEGIN(&b->co);
  for (;;) {
    if (b->auto_fly) break;
    p = level_player();
    if (p && !p->dead && fabsf(p->ent->x - e->x) < 120) break;
    CO_YIELD(&b->co);
  }
  b->sub_started = 0;
  CO_NEST(&b->co, bird_caw(e));
  for (;;) {
    if (b->auto_fly) break;
    p = level_player();
    if (p && !p->dead && v2len(v2sub(player_center(p), v2(e->x, e->y))) < 32) break;
    CO_YIELD(&b->co);
  }
  b->fly_step = 0;
  CO_NEST(&b->co, bird_startle_and_fly(e));
  CO_END(&b->co);
}

/* DashingTutorial: from the top of the room, until Madeline comes; then the prologue's end */
static void dashing_tutorial(Ent *e) {
  Bird *b = ST(e, Bird);
  Player *p;
  CO_BEGIN(&b->co);
  e->y = (float)g_level.room->y;
  e->x += 32;
  CO_WAIT(&b->co, 1);
  for (;;) {
    p = level_player();
    if (p && p->ent->x > b->start.x - 92 && p->ent->y > b->start.y - 20 && p->ent->y < b->start.y - 10) break;
    CO_YIELD(&b->co);
  }
  prologue_ending_start(level_player(), e);
  CO_END(&b->co);
}

static void bird_set_mode(Ent *e, int mode) {
  Bird *b = ST(e, Bird);
  b->mode = (uint8_t)mode;
  memset(&b->co, 0, sizeof b->co);
  b->routine = NULL;
  switch (mode) {
    case BIRD_CLIMBING: b->routine = climbing_tutorial; break;
    case BIRD_DASHING: b->routine = dashing_tutorial; break;
    case BIRD_FLYAWAY: b->routine = wait_routine; break;
    case BIRD_SLEEPING:
      spr_play(&b->spr, A_bird_sleep, false);
      b->facing = 1;
      break;
    default: b->routine = bird_mode_routine(mode); break;
  }
}

Ent *bird_new(V2 at, int mode) {
  Ent *e = ent_new(&BIRD, at.x, at.y);
  if (!e) return NULL;
  Bird *b = ST(e, Bird);
  b->facing = -1;
  spr_init(&b->spr, SB_bird);
  b->spr.raw = 1;
  b->spr.sx = (float)b->facing;
  b->start = at;
  ent_box(e, 0, 0, 0, 0);
  e->ctype = COL_NONE;
  bird_set_mode(e, mode);
  return e;
}

/* ---------------------------------------------------------------- NPC helpers */
/* NPC.PlayerApproach: Madeline walks up to `npc`, `spacing` away on `side` (0: hers), and faces it */
bool npc_approach(NpcWalk *w, Ent *npc, Sprite *spr, float spacing, int side) {
  Player *p = &g_player;
  switch (w->step) {
    case 0:
      w->side = (int8_t)(side ? side : (int)signf(p->ent->x - npc->x));
      if (!w->side) w->side = 1;
      player_set_state(p, ST_DUMMY);
      p->state_locked = true;
      if (spacing > 0) w->walk = walk_exact((int)(npc->x + w->side * spacing));
      else if (fabsf(npc->x - p->ent->x) < 12 || signf(p->ent->x - npc->x) != w->side)
        w->walk = walk_exact((int)(npc->x + w->side * 12));
      else {
        w->step = 3;
        goto face;
      }
      w->step = 1;
      return true;   /* yield return the walk */
    case 1:
      if (player_walk(p, &w->walk)) return true;
      w->step = 2;
      return true;
    case 2:
      w->step = 3;
    face:
      p->facing = -w->side;
      if (spr) spr->sx = (float)w->side;
      return true;   /* yield return null */
    default:
      w->step = 0;
      return false;
  }
}

/* the modes of later chapters (DreamJumpTutorial and the rest): ported with them */
WEAK void (*bird_mode_routine(int mode))(Ent *e) {
  (void)mode;
  return NULL;
}

/* ---------------------------------------------------------------- the factory */
bool props_create(const EData *d) {
  Ent *e;
  switch (d->type) {
    case ET_lamp:
      e = ent_new(&LAMP, d->x, d->y);
      if (e) e->depth = 5, e->collidable = 0, ST(e, Lamp)->broken = EAB(d, lamp, broken);
      return true;
    case ET_wire: {
      if (!d->nnodes) return true;
      e = ent_new(&WIRE, d->x, d->y);
      if (!e) return true;
      WireS *w = ST(e, WireS);
      w->from = v2(d->x, d->y);
      w->to = ed_node(d, 0);
      e->depth = EAB(d, wire, above) ? -8500 : 2000;
      e->collidable = 0;
      NetRandom r;   /* new Random((int)Math.Min(from.X, to.X)) */
      rnd_seed(&r, (int)fminf(w->from.x, w->to.x));
      w->sine_x = rnd_float(&r) * 4;
      w->sine_y = rnd_float(&r) * 4;
      return true;
    }
    case ET_flutterbird: {
      e = ent_new(&FLUTTER, d->x, d->y);
      if (!e) return true;
      Flutter *f = ST(e, Flutter);
      e->depth = -9999;
      e->collidable = 0;
      f->start = v2(d->x, d->y);
      spr_init(&f->spr, SB_flutterBird);
      static const uint32_t cols[4] = {0x89fbff, 0xf0fc6c, 0xf493ff, 0x93baff};
      f->spr.tint = rgb(cols[rndi(4)]);
      return true;
    }
    case ET_bird: {
      static const char *modes[] = {"ClimbingTutorial", "DashingTutorial", "DreamJumpTutorial", "SuperWallJumpTutorial",
                                    "HyperJumpTutorial", "FlyAway", "None", "Sleeping", "MoveToNodes", "WaitForLightningOff"};
      const char *m = EAS(d, bird, mode);
      int mode = BIRD_NONE;
      for (int i = 0; i < 10; i++)
        if (!strcmp(m, modes[i])) mode = i;
      /* Added: gone if Madeline has been past the climbing tutorial's room, or it flew away already */
      if (mode == BIRD_CLIMBING && level_visited("2")) return true;
      if (mode == BIRD_FLYAWAY) {
        char flag[40];
        path2(flag, "bird_fly_away_", level_room_name(), -1);
        if (level_get_flag(flag)) return true;
      }
      bird_new(v2(d->x, d->y), mode);
      return true;
    }
    case ET_hahaha:
      hahaha_new(v2(d->x, d->y), EAS(d, hahaha, ifset));
      return true;
  }
  return false;
}
