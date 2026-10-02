#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("Os")   /* not drawn every frame: smaller over faster */
#endif
/* Forsaken City (chapter 1): Theo at his campfire (NPC01_Theo), the bonfire
 * (Bonfire), the ending (CS01_Ending), the memorial (Memorial, MemorialText).
 * Line by line from the game's code.
 *
 * Also the pieces chapters 1 to 4 share (ch1_ functions, used by ch2.c to
 * ch4.c): the talk prompt's interface entity, Session counters, SaveData's
 * Theo flags, and NPC.MoveTo. */
#include "npc.h"
#include "talk.h"

/* ---------------------------------------------------------------- shared: TalkComponentUI */
/* the prompt is an entity of the interface (TAG_HUD): it draws its owner's Talk */
typedef struct { const EntClass *cls; int16_t owner; uint16_t off; } TalkUi;
static Ent *talkui_owner(Ent *e) {
  TalkUi *u = ST(e, TalkUi);
  Ent *o = &g_ents[u->owner];
  return o->cls == u->cls && o->dead != 1 ? o : NULL;
}
static void talkui_update(Ent *e) {
  if (!talkui_owner(e)) talk_removed(&g_ents[ST(e, TalkUi)->owner]), ent_remove(e);
}
static void talkui_render(Ent *e) {
  Ent *o = talkui_owner(e);
  if (o) talk_render((Talk *)(void *)(g_ent_arena + (uint32_t)o->data * 4 + ST(e, TalkUi)->off), o);
}
static const EntClass TALKUI = {.size = sizeof(TalkUi), .name = "talkUI", .update = talkui_update, .render = talkui_render};
/* Add(Talker = new TalkComponent(...)): t is in owner's state */
void ch1_talk_add(Ent *owner, Talk *t, int x, int y, int w, int h, V2 draw_at) {
  talk_init(t, x, y, w, h, draw_at);
  Ent *e = ent_new(&TALKUI, 0, 0);
  if (!e) return;
  e->tags = TAG_HUD;
  e->depth = owner->depth;
  e->collidable = 0;
  TalkUi *u = ST(e, TalkUi);
  u->cls = owner->cls;
  u->owner = (int16_t)(owner - g_ents);
  u->off = (uint16_t)((uint8_t *)t - (g_ent_arena + (uint32_t)owner->data * 4));
}
/* Remove(Talker) */
void ch1_talk_remove(Ent *owner) {
  for (int i = 0; i < g_nents; i++)
    if (g_ents[i].cls == &TALKUI && ST(&g_ents[i], TalkUi)->owner == owner - g_ents) ent_remove(&g_ents[i]);
  talk_removed(owner);
}

/* ---------------------------------------------------------------- shared: Session counters, SaveData flags */
static uint32_t name_hash(const char *s) {
  uint32_t h = 2166136261u;
  while (*s) h = (h ^ (uint8_t)*s++) * 16777619u;
  return h;
}
int ch1_counter(const char *name) {   /* Session.GetCounter */
  uint32_t h = name_hash(name);
  for (int i = 0; i < 3; i++)
    if (g_session.counters[i].key == h) return g_session.counters[i].value;
  return 0;
}
void ch1_set_counter(const char *name, int v) {   /* Session.SetCounter */
  uint32_t h = name_hash(name);
  int k = 2;
  for (int i = 2; i >= 0; i--)
    if (g_session.counters[i].key == h || !g_session.counters[i].key) k = i;
  for (int i = 0; i < 3; i++)
    if (g_session.counters[i].key == h) k = i;
  g_session.counters[k].key = h, g_session.counters[k].value = v;
}

/* Dialog.Clean, its {n} and {break} new lines */
int ch1_clean_lines(const char *key, char *out, int cap) { return dialog_clean(key, out, cap); }

/* ---------------------------------------------------------------- shared: Level.Flash */
/* a color over the gameplay that comes in fast and fades (drawn under the interface's other entities) */
typedef struct { uint32_t color; float flash; uint8_t doflash; } Flash;
static void flash_update(Ent *e) {
  Flash *f = ST(e, Flash);
  if (f->doflash) {
    f->flash = approach(f->flash, 1, DT * 10);
    if (f->flash >= 1) f->doflash = 0;
  } else if (f->flash > 0)
    f->flash = approach(f->flash, 0, DT * 3);
  else
    ent_remove(e);
}
static void flash_render(Ent *e) {
  Flash *f = ST(e, Flash);
  gfx_hud(true);
  gfx_rect(-1, -1, 322, 182, rgb(f->color), (uint8_t)(f->flash * 255));
  gfx_hud(false);
}
static const EntClass FLASH = {.size = sizeof(Flash), .name = "flash", .update = flash_update, .render = flash_render};
void ch1_flash(uint32_t color) {
  Ent *e = NULL;
  for (int i = 0; i < g_nents && !e; i++)
    if (g_ents[i].cls == &FLASH && g_ents[i].dead != 1) e = &g_ents[i];
  if (!e && (e = ent_new(&FLASH, 0, 0))) e->tags = TAG_HUD | TAG_GLOBAL, e->depth = 2000000, e->collidable = 0;
  if (!e) return;
  Flash *f = ST(e, Flash);
  f->doflash = 1, f->flash = 1, f->color = color;
}

/* ---------------------------------------------------------------- shared: NPC.MoveTo */
/* a nested step (true while it runs); m->step starts at 0. flags: MV_FADEIN, MV_REMOVE (MoveToAndRemove),
 * MV_NOY (MoveY false), MV_TURN (turnAtEndTo: m->turn) */
typedef struct { V2 target; float speed, alpha; int8_t turn; uint8_t step, flags, pad; } Move;
enum { MV_FADEIN = 1, MV_REMOVE = 2, MV_NOY = 4, MV_TURN = 8 };
bool ch1_move_to(Ent *e, Sprite *s, Move *m, float maxspeed, int move_anim, int idle_anim) {
  switch (m->step) {
    case 0:
      if (m->flags & MV_REMOVE) e->tags |= TAG_TRANSITION_UPDATE;
      if (signf(m->target.x - e->x) != 0 && s) s->sx = signf(m->target.x - e->x);
      m->alpha = m->flags & MV_FADEIN ? 0 : 1;
      if (s && move_anim >= 0) spr_play(s, move_anim, false);
      m->speed = 0;
      m->step = 1;
      /* fall through */
    case 1:
      if (!(m->flags & MV_NOY) ? (e->x != m->target.x || e->y != m->target.y) : e->x != m->target.x) {
        m->speed = approach(m->speed, maxspeed, 160 * DT);
        if (!(m->flags & MV_NOY)) {   /* Calc.Approach(Vector2...) */
          V2 d = v2sub(m->target, v2(e->x, e->y));
          float l = v2len(d), k = m->speed * DT;
          if (l <= k) e->x = m->target.x, e->y = m->target.y;
          else e->x += d.x / l * k, e->y += d.y / l * k;
        } else
          e->x = approach(e->x, m->target.x, m->speed * DT);
        if (s) s->alpha = (uint8_t)(m->alpha * 255);
        m->alpha = approach(m->alpha, 1, DT);
        return true;
      }
      if (s && idle_anim >= 0) spr_play(s, idle_anim, false);
      m->step = 2;
      /* fall through */
    case 2:
      if (m->alpha < 1) {
        if (s) s->alpha = (uint8_t)(m->alpha * 255);
        m->alpha = approach(m->alpha, 1, DT);
        return true;
      }
      if ((m->flags & MV_TURN) && s) s->sx = m->turn;
      m->step = 3;
      if (m->flags & MV_REMOVE) ent_remove(e);
      return true;
    default:
      m->step = 0;
      return false;
  }
}
/* a sprite whose Scale.X turns it (flipped instead: the frames' origin is kept) */
static void draw_npc(const Sprite *spr, float x, float y) {
  Sprite s = *spr;
  s.flipx = s.sx < 0;
  s.sx = fabsf(s.sx);
  spr_draw(&s, x, y);
}

/* the textbox's events, each a coroutine run by its Co until it ends (as in ch0.c) */
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
/* a new event: its coroutine starts over */
static Co *ev_start(Co *c, int8_t *at, int index) {
  if (*at != index) *at = (int8_t)index, memset(c, 0, sizeof *c);
  return c;
}

/* ---------------------------------------------------------------- Bonfire */
enum { BF_UNLIT, BF_LIT, BF_SMOKING };
typedef struct {
  Sprite spr;
  Wiggler wig;
  float brightness, mult, light;
  uint8_t mode, activated;
} Bonfire;
static void bonfire_set_mode(Ent *e, int mode) {   /* SetMode */
  Bonfire *b = ST(e, Bonfire);
  b->mode = (uint8_t)mode;
  if (mode == BF_LIT)
    spr_play(&b->spr, b->activated ? (g_session.dreaming ? A_campfire_startDream : A_campfire_start)
                                   : (g_session.dreaming ? A_campfire_burnDream : A_campfire_burn), false);
  else if (mode == BF_SMOKING)
    spr_play(&b->spr, A_campfire_smoking, false);
  else {
    spr_play(&b->spr, A_campfire_idle, false);
    b->light = b->brightness = 0;
  }
  b->activated = 1;
}
static void bonfire_update(Ent *e) {
  Bonfire *b = ST(e, Bonfire);
  if (b->mode == BF_LIT) {
    b->mult = approach(b->mult, 1, DT * 2);
    if (level_on_interval(0.25f)) {
      b->brightness = 0.5f + rndf() * 0.5f;
      wiggler_restart(&b->wig);
      b->light = fminf(1, b->brightness + b->wig.value * 0.25f) * b->mult;
    }
  }
  spr_update(&b->spr);
  if (b->wig.active) {
    wiggler_update(&b->wig);
    b->light = fminf(1, b->brightness + b->wig.value * 0.25f) * b->mult;
  }
}
static void bonfire_render(Ent *e) {
  Bonfire *b = ST(e, Bonfire);
  spr_draw(&b->spr, e->x, e->y);
  light_add(e->x, e->y - 6, 0xDB7093, b->light, 32, 64);   /* PaleVioletRed */
  bloom_add(e->x, e->y - 6, b->light, 32);
}
static const EntClass BONFIRE = {.size = sizeof(Bonfire), .name = "bonfire", .update = bonfire_update, .render = bonfire_render};
static void new_bonfire(const EData *d) {
  Ent *e = ent_new(&BONFIRE, d->x, d->y);
  if (!e) return;
  e->tags = TAG_TRANSITION_UPDATE;
  e->depth = -5;
  e->collidable = 0;
  Bonfire *b = ST(e, Bonfire);
  spr_init(&b->spr, SB_campfire);
  wiggler_init(&b->wig, 0.2f, 4);
  b->light = 1;
  const char *m = EAS(d, bonfire, mode);
  bonfire_set_mode(e, !strcmp(m, "Lit") ? BF_LIT : !strcmp(m, "Smoking") ? BF_SMOKING : BF_UNLIT);   /* Added */
  b->activated = 0;
  bonfire_set_mode(e, b->mode);
}
static Ent *find_class(const EntClass *cls) {
  for (int i = 0; i < g_nents; i++)
    if (g_ents[i].cls == cls && g_ents[i].dead != 1) return &g_ents[i];
  return NULL;
}

/* ---------------------------------------------------------------- NPC01_Theo */
typedef struct {
  Talk talk;
  Sprite spr;
  int8_t conv;
  uint8_t has_talk;
} Theo1;
typedef struct {
  Cutscene cs;
  Ent *theo, *tb;
  NpcWalk nw;
  Step step;
  Co ev;
  int8_t evi;
} Theo1Talk;
static const EntClass THEO1;

static bool theo1_event(void *ctx, int index) {   /* CH1_THEO_A's PlayerApproach48px, CH1_THEO_F's Yolo */
  Theo1Talk *s = ST((Ent *)ctx, Theo1Talk);
  Ent *te = s->theo;
  Theo1 *t = ST(te, Theo1);
  Co *c = ev_start(&s->ev, &s->evi, index);
  if (t->conv == 0) {   /* PlayerApproach48px */
    EV_BEGIN;
    EV_NEST(npc_approach(&s->nw, te, &t->spr, 48, 0));
    EV_END;
  }
  EV_BEGIN;   /* Yolo */
  step_reset(&s->step);
  EV_NEST(zoom_to(&s->step, v2(128, 128), 2, 0.5f));
  EV_WAIT(0.2f);
  spr_play(&t->spr, A_theo_yolo, false);
  EV_WAIT(0.1f);
  level_dir_shake(v2(0, -1), 0.3f);
  particles_emit(PL_FG, &P_NPC01_Theo_P_YOLO, 6, v2(te->x - 3, te->y - 24), v2(4, 4), P_NPC01_Theo_P_YOLO.direction);
  EV_WAIT(0.5f);
  EV_END;
}
static void theo1_talk_end(Ent *e, bool skipped) {   /* OnTalkEnd */
  (void)skipped;
  Theo1Talk *s = ST(e, Theo1Talk);
  Ent *te = s->theo;
  Theo1 *t = ST(te, Theo1);
  if (t->conv == 0) save_set_flag(0);
  else if (t->conv == 1) save_set_flag(1);
  else if (t->conv == 5) {
    level_set_flag("theoDoneTalking", true);
    if (t->has_talk) t->has_talk = 0, ch1_talk_remove(te);
  }
  Player *p = level_player();
  if (p) p->state_locked = false, player_set_state(p, ST_NORMAL);
  ch1_set_counter("theo", ch1_counter("theo") + 1);
  t->conv++;
  spr_play(&t->spr, A_theo_idle, false);
}
static void theo1_talk_update(Ent *e) {   /* Talk */
  static const char *const KEYS[6] = {"CH1_THEO_A", "CH1_THEO_B", "CH1_THEO_C", "CH1_THEO_D", "CH1_THEO_E", "CH1_THEO_F"};
  Theo1Talk *s = ST(e, Theo1Talk);
  Ent *te = s->theo;
  Theo1 *t = ST(te, Theo1);
  Co *c = &s->cs.co;
  CO_BEGIN(c);
  if (t->conv > 5) goto done;
  /* PlayerApproachRightSide(player, turnToFace, the 48 px from the third talk on) */
  CO_NEST(c, npc_approach(&s->nw, te, &t->spr, t->conv >= 2 ? 48 : 0, 1));
  if (t->conv == 1) {
    CO_WAIT(c, 0.2f);
    CO_NEST(c, npc_approach(&s->nw, te, &t->spr, 48, 0));
  }
  s->evi = -1;
  CO_SAY(c, s->tb, KEYS[t->conv], theo1_event, e);
  if (t->conv == 5) {
    spr_play(&t->spr, A_theo_yoloEnd, false);
    if (t->has_talk) t->has_talk = 0, ch1_talk_remove(te);
    step_reset(&s->step);
    CO_NEST(c, zoom_back(&s->step, 0.5f));
  }
done:
  cutscene_end(e);
  return;
  CO_END(c);
}
static const EntClass THEO1TALK = {.size = sizeof(Theo1Talk), .name = "NPC01_Theo.Talk", .update = theo1_talk_update};
static void theo1_update(Ent *e) {
  Theo1 *t = ST(e, Theo1);
  spr_update(&t->spr);
  if (t->has_talk && talk_update(&t->talk, e)) {   /* OnTalk: Level.StartCutscene(OnTalkEnd), the Talk routine */
    Ent *c = ent_new(&THEO1TALK, 0, 0);
    if (!c) return;
    c->collidable = 0, c->visible = 0;
    ST(c, Theo1Talk)->theo = e;
    cutscene_start(c, theo1_talk_end, true, false);
  }
}
static void theo1_render(Ent *e) { draw_npc(&ST(e, Theo1)->spr, e->x, e->y); }
static const EntClass THEO1 = {.size = sizeof(Theo1), .name = "NPC01_Theo", .update = theo1_update, .render = theo1_render};
static void new_theo1(const EData *d) {
  Ent *e = ent_new(&THEO1, d->x, d->y);
  if (!e) return;
  e->depth = 1000;
  ent_box(e, 8, 8, -4, -8);
  Theo1 *t = ST(e, Theo1);
  spr_init(&t->spr, SB_theo);
  spr_play(&t->spr, A_theo_idle, false);
  t->conv = (int8_t)ch1_counter("theo");
  if (!level_get_flag("theoDoneTalking")) t->has_talk = 1, ch1_talk_add(e, &t->talk, -8, -8, 88, 8, v2(0, -24));
}

/* ---------------------------------------------------------------- CS01_Ending */
typedef struct {
  Cutscene cs;
  Ent *bonfire, *tb, *bird;
  Walk walk;
  Co ev;
  int8_t evi;
  float percent;
  V2 from, to;
} End1;
static bool end1_event(void *ctx, int index) {   /* EndCityTrigger */
  End1 *s = ST((Ent *)ctx, End1);
  Player *p = &g_player;
  Ent *bf = s->bonfire;
  Co *c = ev_start(&s->ev, &s->evi, index);
  Bird *b;
  EV_BEGIN;
  EV_WAIT(0.2f);
  s->walk = walk_to(bf->x - 12);
  EV_NEST(player_walk(p, &s->walk));
  EV_WAIT(0.2f);
  p->facing = 1;
  p->dummy_auto_animate = false;
  spr_play(&p->spr, A_player_duck, false);
  EV_WAIT(0.5f);
  bonfire_set_mode(bf, BF_LIT);
  EV_WAIT(1);
  spr_play(&p->spr, A_player_idle, false);
  EV_WAIT(0.4f);
  p->dummy_auto_animate = true;
  s->walk = walk_to(bf->x - 24);
  EV_NEST(player_walk(p, &s->walk));
  EV_WAIT(0.4f);
  p->dummy_auto_animate = false;
  p->facing = 1;
  spr_play(&p->spr, A_player_sleep, false);
  EV_WAIT(4);
  s->bird = bird_new(v2add(v2(p->ent->x, p->ent->y), v2(88, -200)), BIRD_NONE);
  if (!s->bird) return true;
  b = ST(s->bird, Bird);
  b->facing = -1;
  spr_play(&b->spr, A_bird_fall, false);
  s->from = v2(s->bird->x, s->bird->y);
  s->to = v2add(v2(p->ent->x, p->ent->y), v2(1, -12));
  for (s->percent = 0; s->percent < 1; s->percent += DT * 0.5f) {
    {
      float k = ease_quad_out(s->percent);
      s->bird->x = s->from.x + (s->to.x - s->from.x) * k, s->bird->y = s->from.y + (s->to.y - s->from.y) * k;
    }
    if (s->percent > 0.5f) spr_play(&ST(s->bird, Bird)->spr, A_bird_fly, false);
    EV_YIELD;
  }
  s->bird->x = s->to.x, s->bird->y = s->to.y;
  spr_play(&ST(s->bird, Bird)->spr, A_bird_idle, false);
  EV_WAIT(0.5f);
  spr_play(&ST(s->bird, Bird)->spr, A_bird_croak, false);
  EV_WAIT(0.6f);
  EV_WAIT(0.9f);
  spr_play(&ST(s->bird, Bird)->spr, A_bird_sleep, false);
  EV_YIELD;
  EV_WAIT(2);
  EV_END;
}
static void end1_end(Ent *e, bool skipped) {
  (void)e, (void)skipped;
  level_complete_area(true, false);
}
static void end1_update(Ent *e) {
  End1 *s = ST(e, End1);
  Player *p = &g_player;
  Co *c = &s->cs.co;
  CO_BEGIN(c);
  player_set_state(p, ST_DUMMY);
  p->dashes = 1;
  CO_WAIT(c, 0.5f);
  s->walk = walk_to(s->bonfire->x + 40);
  CO_NEST(c, player_walk(p, &s->walk));
  CO_WAIT(c, 1.5f);
  p->facing = -1;
  CO_WAIT(c, 0.5f);
  s->evi = -1;
  CO_SAY(c, s->tb, "CH1_END", end1_event, e);
  CO_WAIT(c, 0.3f);
  cutscene_end(e);
  return;
  CO_END(c);
}
static const EntClass END1 = {.size = sizeof(End1), .name = "CS01_Ending", .update = end1_update};

/* EventTrigger "end_city" */
static void event1_enter(Ent *e, Player *p) {
  (void)p;
  if (*ST(e, uint8_t)) return;
  *ST(e, uint8_t) = 1;
  Ent *bf = find_class(&BONFIRE);
  Ent *c = bf ? ent_new(&END1, 0, 0) : NULL;
  if (!c) return;
  c->collidable = 0, c->visible = 0;
  ST(c, End1)->bonfire = bf;
  level_register_complete();   /* OnBegin */
  cutscene_start(c, end1_end, false, true);
}
static const EntClass EVENT1 = {.size = 1, .name = "eventTrigger", .on_enter = event1_enter, .kind = KIND_TRIGGER};

/* ---------------------------------------------------------------- Memorial and MemorialText */
#define MEMO_N 96
typedef struct {
  float index, alpha, timer, widest;
  uint8_t show, dreamy, n, first;
  char msg[MEMO_N];
  int16_t memorial;
} MemoText;
typedef struct { uint16_t floaty[3]; int16_t text; float timer; uint8_t dreaming; } Memo;
static const EntClass MEMORIAL;
static int memo_line_len(const MemoText *m, int start) {   /* CountToNewline */
  int i = start;
  while (i < m->n && m->msg[i] != '\n') i++;
  return i - start;
}
static void memotext_message(MemoText *mt);
static void memotext_update(Ent *e) {
  MemoText *m = ST(e, MemoText);
  if (!m->n) memotext_message(m);
  if (g_level.paused) return;
  m->timer += DT;
  if (!m->show) {
    m->alpha = approach(m->alpha, 0, DT);
    if (m->alpha <= 0) m->index = m->first;
  } else {
    m->alpha = approach(m->alpha, 1, DT * 2);
    if (m->alpha >= 1) m->index = approach(m->index, m->n, 32 * DT);
  }
}
/* the glyphs, drawn into the strip (dreamy ones turn over: Scale.X -1) */
typedef struct { MemoText *m; float x0, y0, ease; int count; } MemoDraw;
static MemoDraw memo_draw;
/* a glyph of the 64 px face into a strip (gfx_custom layers: a page of text is one command): (x, top) the
 * left of its advance and the top of its line, view pixels; flip turns it over (Scale.X -1) */
void ch1_glyph(uint16_t *strip, int sy0, int sy1, uint32_t ch, float x, float top, bool flip, uint16_t col, uint8_t alpha) {
  const uint8_t *g = font_glyph_rec(FONT_S, ch);
  if (!g || !g[6]) return;
  float adv = rd16(g + 8) / 16.f;
  int w = g[6], h = g[7], stride = (w + 1) / 2;
  const uint8_t *bits = font_glyph_bits(FONT_S, g);
  int ox = (int)floorf(rds16(g + 2) / 16.f + 0.5f), Y0 = (int)floorf(top + 0.5f) + rds16(g + 4);
  int left = (int)floorf(x + 0.5f), mirror = (int)floorf(2 * x + adv + 0.5f);
  int ga = alpha + (alpha >> 7);
  for (int r = 0; r < h; r++) {
    int Y = Y0 + r;
    if (Y < sy0 || Y >= sy1) continue;
    uint16_t *row = strip + (Y - sy0) * VIEW_W;
    for (int i = 0; i < w; i++) {
      int X = flip ? mirror - (left + ox + i) - 1 : left + ox + i;
      if ((unsigned)X >= VIEW_W) continue;
      int v = (bits[r * stride + (i >> 1)] >> ((i & 1) * 4)) & 15;
      if (!v) continue;
      int a = (v * 17 * ga) >> 8;
      uint32_t c = col, pr = ((c >> 11) * a) >> 8, pg = (((c >> 5) & 63) * a) >> 8, pb = ((c & 31) * a) >> 8;
      row[X] = blend565(row[X], (uint16_t)(pr << 11 | pg << 5 | pb), a + 1);
    }
  }
}
static void memo_layer(uint16_t *strip, int sy0, int sy1, void *ctx) {
  MemoDraw *d = ctx;
  MemoText *m = d->m;
  int col = 0, len = memo_line_len(m, 0);
  float y = 64 * (1 - d->ease) / 6;
  for (int i = 0; i < d->count; i++) {
    char c = m->msg[i];
    if (c == '\n') {
      col = 0;
      len = memo_line_len(m, i + 1);
      y += font_line_height(FONT_S) * 1.1f;
      continue;
    }
    bool flip = false;
    float yo = 0;
    float x = (-len * m->widest / 2 + (col + 0.5f) * m->widest) / 6;
    if (m->dreamy && c != ' ' && c != '-') {
      float sn = sinf(m->timer * 2 + i / 8.f);
      c = m->msg[(i + (int)(sn * 4) + m->n) % m->n];
      yo = sn * 8 / 6;
      flip = sinf(m->timer * 4 + i / 16.f) < 0;
    }
    if (c != '\n') {   /* ActiveFont.Draw(c, at, justify (0.5, 1), scale) */
      const uint8_t *g = font_glyph_rec(FONT_S, (uint8_t)c);
      float adv = g ? rd16(g + 8) / 16.f : 0;
      ch1_glyph(strip, sy0, sy1, (uint8_t)c, d->x0 + x - adv / 2, d->y0 + y + yo - font_line_height(FONT_S), flip, 0xFFFF,
                (uint8_t)(d->ease * 255));
    }
    col++;
  }
}
static void memotext_render(Ent *e) {
  MemoText *m = ST(e, MemoText);
  Ent *me = &g_ents[m->memorial];
  if (g_level.frozen || g_level.paused || g_level.completed || !(m->index > 0) || !(m->alpha > 0) || me->cls != &MEMORIAL) return;
  memo_draw.m = m;
  memo_draw.x0 = me->x - g_level.cam.x;
  memo_draw.y0 = me->y - g_level.cam.y - (350 + font_line_height(FONT_S) * 6 * 3.3f) / 6;
  memo_draw.ease = ease_cube_inout(m->alpha);
  memo_draw.count = (int)fminf(m->n, m->index);
  gfx_hud(true);
  gfx_custom(memo_layer, &memo_draw, (int)memo_draw.y0 - 24, (int)memo_draw.y0 + 50);
  gfx_hud(false);
}
static const EntClass MEMOTEXT = {.size = sizeof(MemoText), .name = "MemorialText", .update = memotext_update, .render = memotext_render};
static void memorial_update(Ent *e) {
  Memo *m = ST(e, Memo);
  if (g_level.paused) return;
  Ent *t = &g_ents[m->text];
  Ent *p = level_player_ent();
  if (t->cls == &MEMOTEXT) ST(t, MemoText)->show = p && collide_ent_at(e, e->x, e->y, p);
  m->timer += DT;
}
static void memorial_render(Ent *e) {
  Memo *m = ST(e, Memo);
  Tex t;
  if (tex_get(T_scenery_memorial_memorial, &t)) gfx_tex_ex(T_scenery_memorial_memorial, e->x, e->y, t.fw / 2.f, (float)t.fh, 1, 1, 0, 0xFFFF, 255, 0);
  /* the dreamy text: a Sprite of scenery/memorial/floatytext looping at 0.1, at (-Width / 2, -33) */
  if (m->dreaming) gfx_tex(m->floaty[(int)(m->timer / 0.1f) % 3], e->x - 10, e->y - 33, 0, 0xFFFF, 255);
}
static const EntClass MEMORIAL = {.size = sizeof(Memo), .name = "memorial", .update = memorial_update, .render = memorial_render};
static void new_memorial(const EData *d) {
  Ent *e = ent_new(&MEMORIAL, d->x, d->y);
  if (!e) return;
  e->tags = TAG_PAUSE_UPDATE;
  e->depth = 100;
  ent_box(e, 60, 80, -30, -60);
  Memo *m = ST(e, Memo);
  m->dreaming = g_session.dreaming;
  char pth[40];
  for (int i = 0; i < 3 && m->dreaming; i++) m->floaty[i] = res_tex_by_name(path2(pth, "scenery/memorial/floatytext", NULL, i));
  Ent *t = ent_new(&MEMOTEXT, 0, 0);
  if (!t) return;
  t->tags = TAG_HUD | TAG_PAUSE_UPDATE;
  t->collidable = 0;
  m->text = (int16_t)(t - g_ents);
  MemoText *mt = ST(t, MemoText);
  mt->memorial = (int16_t)(e - g_ents);
  mt->dreamy = g_session.dreaming;
}
/* the constructor's message (read once the room is loaded: the dialog is decoded in free cache memory) */
static void memotext_message(MemoText *mt) {
  int k = ch1_clean_lines("memorial", mt->msg, MEMO_N);
  mt->n = (uint8_t)k;
  mt->first = (uint8_t)memo_line_len(mt, 0);
  mt->index = mt->first;
  for (int i = 0; i < k; i++) {
    const uint8_t *g = font_glyph_rec(FONT_S, (uint8_t)mt->msg[i]);
    float w = g ? rd16(g + 8) * 6 / 16.f : 0;   /* ActiveFont.Measure(c).X, interface pixels */
    if (w > mt->widest) mt->widest = w;
  }
  mt->widest *= 0.9f;
}

/* ---------------------------------------------------------------- the factories */
bool ents_ch1(const EData *d) {
  switch (d->type) {
    case ET_npc:
      if (!strcmp(EAS(d, npc, npc), "theo_01_campfire")) {
        new_theo1(d);
        return true;
      }
      return false;
    case ET_bonfire:
      if (g_session.area < 1 || g_session.area > 4) return false;
      new_bonfire(d);
      return true;
    case ET_memorial: new_memorial(d); return true;
  }
  return false;
}

bool trigs_ch1(const EData *d) {
  if (d->type != TT_eventTrigger || strcmp(EAS(d, eventTrigger, event), "end_city")) return false;
  Ent *e = ent_new(&EVENT1, d->x, d->y);
  if (e) ent_box(e, EA(d, eventTrigger, width), EA(d, eventTrigger, height), 0, 0), e->visible = 0;
  return true;
}
