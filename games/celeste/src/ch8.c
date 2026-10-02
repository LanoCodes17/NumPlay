#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("Os")   /* not drawn every frame: smaller over faster */
#endif
/* The Epilogue (chapter 8): the fixed bridge (the Summit's credits have it too),
 * Granny and Theo in the cabin (NPC08_Granny, NPC08_Theo), the door
 * (CS08_EnterDoor) and the ending (CS08_Ending: the cabin, the pie and the
 * totals). Line by line from the game's code. */
#include "badeline.h"

/* ---------------------------------------------------------------- BridgeFixed */
static void bridgefixed_render(Ent *e) {
  for (int i = 0; i < e->cw; i += 208) gfx_tex(T_scenery_bridge_fixed, e->x + i, e->y - 8, 0, 0xFFFF, 255);
}
static const EntClass BRIDGEFIXED = {.name = "bridgeFixed", .update = plat_update, .render = bridgefixed_render, .kind = KIND_SOLID};
static void new_bridge_fixed(const EData *d) {
  Ent *e = ent_new(&BRIDGEFIXED, d->x, d->y);
  if (!e) return;
  ent_box(e, EA(d, bridgeFixed, width), 8, 0, 0);
  e->safe = 1;
}

/* ---------------------------------------------------------------- CS08_Ending */
typedef struct {
  Cutscene cs;
  Ent *granny, *theo, *badeline, *oshiro, *tb;
  Walk walk;
  Co ev;
  int8_t ev_i;
  uint8_t ending, vignette, steps, show_version, started;
  float fade, version_alpha, p, p2, scale_from, rot_from;
  V2 pos_from;
  float vg_alpha, vg_scale, vg_rot, bg_white;   /* the vignette (its position: vg_pos), finalbg's color */
  V2 vg_pos, counters;                          /* the totals' place, interface pixels */
} Ending8;
static const char *const PIE[5] = {"EP_PIE_DISAPPOINTED", "EP_PIE_GROSSED_OUT", "EP_PIE_OKAY", "EP_PIE_REALLY_GOOD", "EP_PIE_AMAZING"};

/* the Oshiro that comes in: an Entity with an OshiroSprite(1) */
typedef struct { Sprite spr; Wiggler wig; } Oshiro8;
static void oshiro8_update(Ent *e) {
  Oshiro8 *o = ST(e, Oshiro8);
  oshiro_sprite_update(&o->spr, &o->wig);
  Room *rm = g_level.room;   /* AllowTurnInvisible */
  o->spr.visible = e->x > rm->x - 8 && e->y > rm->y - 8 && e->x < rm->x + rm->w + 8 && e->y < rm->y + rm->h + 16;
}
static void oshiro8_render(Ent *e) { spr_draw(&ST(e, Oshiro8)->spr, e->x, e->y); }
static const EntClass OSHIRO8 = {.name = "oshiroSprite", .size = sizeof(Oshiro8), .update = oshiro8_update, .render = oshiro8_render};

static bool ending_nb(Ending8 *s, Co *c, int i) {
  Player *p = &g_player;
  switch (i) {
    case 0:   /* BadelineEmerges */
      NB_BEGIN(c);
      g_session.inv_dashes = 1;   /* (the displacement burst is not drawn) */
      p->dashes = p->inventory_dashes = 1;
      s->badeline = bd_new(v2(p->ent->x, p->ent->y));
      if (!s->badeline) NB_YIELD(c);
      else {
        BD(s->badeline)->spr.sx = 1;
        bd_float_to(s->badeline, v2(p->ent->x - 12, p->ent->y - 16), 1, false, false, false);
        NB_AWAIT(c, &BD(s->badeline)->job);
      }
      NB_END(c);
    case 1:   /* OshiroEnters */
      NB_BEGIN(c);
      fade_wipe(false, 1.5f, NULL);
      NB_NEST(c, wipe_busy());
      s->fade = 1;
      NB_WAIT(c, 0.25f);
      {
        float x = p->ent->x;
        p->ent->x = s->granny->x + 8;
        if (s->badeline) s->badeline->x = p->ent->x + 12, BD(s->badeline)->spr.sx = -1;
        p->facing = -1;
        s->granny->x = x + 8;
        s->theo->x += 16;
      }
      s->oshiro = ent_new(&OSHIRO8, s->granny->x - 24, s->granny->y + 4);
      if (s->oshiro) {
        Oshiro8 *o = ST(s->oshiro, Oshiro8);
        spr_init(&o->spr, SB_oshiro);
        o->spr.sx = 1;
        wiggler_init(&o->wig, 0.3f, 2);
      }
      s->fade = 0;
      fade_wipe(true, 1, NULL);
      NB_WAIT(c, 0.25f);
      while (s->oshiro && s->oshiro->y > s->granny->y - 4) {
        s->oshiro->y -= DT * 32;
        NB_YIELD(c);
      }
      NB_END(c);
    case 2:   /* OshiroSettles */
      NB_BEGIN(c);
      if (s->oshiro) {
        s->pos_from = v2(s->oshiro->x, s->oshiro->y);
        for (s->p = 0; s->p < 1; s->p += DT) {
          {
            V2 at = v2lerp(s->pos_from, v2add(s->pos_from, v2(40, 8)), s->p);
            s->oshiro->x = at.x, s->oshiro->y = at.y;
          }
          NB_YIELD(c);
        }
      }
      NPC_(s->granny)->spr.sx = 1;
      NB_YIELD(c);
      NB_END(c);
    default:   /* MaddyTurns */
      NB_BEGIN(c);
      NB_WAIT(c, 0.1f);
      p->facing = -p->facing;
      NB_WAIT(c, 0.1f);
      NB_END(c);
  }
}
static bool ending_ev(void *ctx, int i) {
  Ending8 *s = ST((Ent *)ctx, Ending8);
  return !ending_nb(s, ev_co(&s->ev, &s->ev_i, i), i);
}
static void ending_update(Ent *e) {
  Ending8 *s = ST(e, Ending8);
  Player *p = &g_player;
  Co *c = &s->cs.co;
  s->version_alpha = approach(s->version_alpha, s->show_version ? 1 : 0, DT * 5);
  CO_BEGIN(c);
  zoom_snap(v2(164, 120), 2);
  g_wipe.active = false;   /* Wipe.Cancel() */
  fade_wipe(true, 0.5f, NULL);
  player_intro_none(p);
  while (!s->granny || !s->theo) {
    s->granny = npc_find(NPC_GRANNY08);
    s->theo = npc_find(NPC_THEO08);
    CO_YIELD(c);
  }
  player_set_state(p, ST_DUMMY);
  s->started = 1;
  CO_WAIT(c, 1);
  CO_WALK(c, s->walk, walk_exact((int)p->ent->x + 16));
  CO_WAIT(c, 0.25f);
  s->ev_i = -1;
  CO_SAY(c, s->tb, "EP_CABIN", ending_ev, e);
  fade_wipe(false, 1.5f, NULL);
  CO_NEST(c, wipe_busy());
  s->fade = 1;
  CO_SAY(c, s->tb, "EP_PIE_START", NULL, NULL);
  CO_WAIT(c, 0.5f);
  s->vignette = 1, s->bg_white = 0, s->vg_alpha = 0, s->vg_pos = v2(960, 540);
  for (s->p = 0; s->p < 1; s->p += DT) {
    s->vg_alpha = ease_cube_in(s->p);
    s->vg_scale = 1 + 0.25f * (1 - s->p);
    s->vg_rot = 0.05f * (1 - s->p);
    CO_YIELD(c);
  }
  s->vg_alpha = 1, s->bg_white = 1;
  CO_WAIT(c, 2);
  for (s->p2 = 0; s->p2 < 1; s->p2 += DT / 1) {
    {
      float k = ease_cube_out(s->p2);
      s->vg_pos = v2lerp(v2(960, 540), v2(960, 680), k);
      s->vg_scale = 0.65f + 0.35f * (1 - k);
      s->vg_rot = -0.025f * k;
    }
    CO_YIELD(c);
  }
  CO_SAY(c, s->tb, PIE[s->ending], NULL, NULL);
  CO_WAIT(c, 0.25f);
  s->pos_from = s->vg_pos, s->rot_from = s->vg_rot, s->scale_from = s->vg_scale;
  for (s->p = 0; s->p < 1; s->p += DT / 2) {
    {
      float k = ease_cube_out(s->p);
      s->vg_pos = v2lerp(s->pos_from, v2(960, 540), k);
      s->vg_scale = lerpf(s->scale_from, 1, k);
      s->vg_rot = lerpf(s->rot_from, 0, k);
    }
    CO_YIELD(c);
  }
  cutscene_end(e);   /* EndCutscene(level, removeSelf: false): the totals go on in another entity */
  return;
  CO_END(c);
}
/* Dialog.FileTime: hours:mm:ss.fff */
static int file_time(char *b, uint32_t frames) {
  uint32_t ms = (uint32_t)(frames * 1000.0 / 60), h = ms / 3600000, m = ms / 60000 % 60, sec = ms / 1000 % 60, f = ms % 1000;
  char *o = b;
  char tmp[12];
  int k = 0;
  do tmp[k++] = (char)('0' + h % 10), h /= 10;
  while (h);
  while (k) *o++ = tmp[--k];
  *o++ = ':', *o++ = (char)('0' + m / 10), *o++ = (char)('0' + m % 10), *o++ = ':', *o++ = (char)('0' + sec / 10);
  *o++ = (char)('0' + sec % 10), *o++ = '.', *o++ = (char)('0' + f / 100), *o++ = (char)('0' + f / 10 % 10);
  *o++ = (char)('0' + f % 10), *o = 0;
  return (int)(o - b);
}
static void ending_render(Ent *e) {
  Ending8 *s = ST(e, Ending8);
  if (g_level.paused) return;
  gfx_hud(true);
  if (s->fade > 0) gfx_rect(-2, -2, 324, 184, 0, (uint8_t)(clampf(s->fade, 0, 1) * 255));
  if (s->vignette) {   /* Portraits "finalbg" and "final1".."final5" (kept at half size, drawn twice as big) */
    static const uint16_t FINAL[5] = {T__final1, T__final2, T__final3, T__final4, T__final5};
    uint16_t bg = T__finalbg, vg = FINAL[s->ending];
    Tex t;
    if (tex_get(bg, &t)) gfx_tex(bg, 0, 0, 0, s->bg_white ? 0xFFFF : 0, 255);
    if (tex_get(vg, &t))
      gfx_tex_ex(vg, s->vg_pos.x / 6, s->vg_pos.y / 6, t.fw * t.scale / 2.f, t.fh * t.scale / 2.f, s->vg_scale, s->vg_scale, s->vg_rot,
                 0xFFFF, (uint8_t)(clampf(s->vg_alpha, 0, 1) * 255), 0);
  }
  if (s->steps) {   /* StrawberriesCounter (of 175), DeathsCounter, TimeDisplay */
    char b[24];
    int n = uitoa(save_total_berries(), b);
    memcpy(b + n, "/175", 5);
    float y = s->counters.y / 6;
    font_draw_outline(b, (int)strlen(b), (s->counters.x - 170) / 6 - font_measure(b, (int)strlen(b), FONT_S) / 2, y - 4, FONT_S, 0xFFFF, 0, 255);
    uitoa((int)g_save.total_deaths, b);
    font_draw_outline(b, (int)strlen(b), (s->counters.x + 170) / 6 - font_measure(b, (int)strlen(b), FONT_S) / 2, y - 4, FONT_S, 0xFFFF, 0, 255);
    n = file_time(b, g_save.time);
    font_draw_outline(b, n, s->counters.x / 6 - font_measure(b, n, FONT_S) / 2, y + 100 / 6.f - 4, FONT_S, 0xFFFF, 0, 255);
  }
  gfx_hud(false);
}
/* EndingRoutine: the totals come up, then a press ends the chapter */
static void endtotals_update(Ent *e) {
  Ending8 *s = ST(e, Ending8);
  Co *c = &s->ev;
  s->version_alpha = approach(s->version_alpha, s->show_version ? 1 : 0, DT * 5);
  CO_BEGIN(c);
  g_level.in_cutscene = true;
  g_level.pause_lock = true;
  CO_WAIT(c, 0.5f);
  s->steps = 1;
  for (s->p = 0; s->p < 1; s->p += DT / 0.5f) {
    s->counters = v2lerp(v2(960, 1180), v2(960, 940), ease_cube_out(s->p));
    CO_YIELD(c);
  }
  s->show_version = 1;   /* (the version number: none) */
  CO_WAIT(c, 0.25f);
  while (!btn_pressed(&g_in.confirm) && !btn_pressed(&g_in.jump)) CO_YIELD(c);
  s->show_version = 0;
  CO_WAIT(c, 0.25f);
  level_complete_area(false, false);
  CO_END(c);
}
static const EntClass ENDTOTALS = {.name = "CS08_EndingRoutine", .size = sizeof(Ending8), .update = endtotals_update, .render = ending_render};
static void ending_end(Ent *e, bool skipped) {
  Ending8 *s = ST(e, Ending8);
  (void)skipped;
  s->vignette = 1, s->vg_alpha = 1, s->bg_white = 1, s->vg_pos = v2(960, 540), s->vg_scale = 1, s->vg_rot = 0;
  g_player.speed = v2(0, 0);
  if (s->tb && textbox_opened(s->tb)) ent_remove(s->tb);
  Ent *t = ent_new(&ENDTOTALS, 0, 0);
  if (!t) return;
  t->tags = TAG_HUD | TAG_PAUSE_UPDATE;
  t->collidable = 0;
  Ending8 *n = ST(t, Ending8);
  *n = *s;
  memset(&n->ev, 0, sizeof n->ev);
}
static const EntClass ENDING = {.name = "CS08_Ending", .size = sizeof(Ending8), .update = ending_update, .render = ending_render};
static void ending_start(void) {
  Ent *e = scene_new(&ENDING, ending_end, false, true);
  if (!e) return;
  e->tags = TAG_HUD | TAG_PAUSE_UPDATE;
  int n = save_total_berries();
  ST(e, Ending8)->ending = (uint8_t)(n < 20 ? 0 : n < 50 ? 1 : n < 90 ? 2 : n < 150 ? 3 : 4);
}

/* ---------------------------------------------------------------- CS08_EnterDoor */
static uint8_t ending_due;   /* the room "inside" loads with CS08_Ending */
typedef struct {
  Cutscene cs;
  Walk walk;
  Step zoom;
  float target_x;
  uint8_t walking, zooming;
} EnterDoor;
static void door_update(Ent *e) {
  EnterDoor *s = ST(e, EnterDoor);
  Player *p = &g_player;
  Co *c = &s->cs.co;
  CO_BEGIN(c);
  player_set_state(p, ST_DUMMY);
  s->walk = walk_mult((int)s->target_x, 0.7f);   /* two coroutines of their own: the walk and the zoom */
  s->walking = 2;
  step_reset(&s->zoom);
  s->zooming = 2;
  fade_wipe(false, 2, NULL);
  CO_NEST(c, wipe_busy());
  cutscene_end(e);
  return;
  CO_END(c);
}
static void door_tick(Ent *e) {
  EnterDoor *s = ST(e, EnterDoor);
  door_update(e);
  if (e->dead == 1) return;
  if (s->walking == 2) s->walking = 1;
  else if (s->walking && !player_walk(&g_player, &s->walk)) s->walking = 0;
  if (s->zooming == 2) s->zooming = 1;
  else if (s->zooming && !zoom_to(&s->zoom, v2(s->target_x - g_level.cam.x, 90), 2, 2)) s->zooming = 0;
}
static void door_end(Ent *e, bool skipped) {
  (void)e, (void)skipped;
  ending_due = 1;
  level_goto("inside");
}
static const EntClass ENTERDOOR = {.name = "CS08_EnterDoor", .size = sizeof(EnterDoor), .update = door_tick};

typedef struct { float left; uint8_t triggered; } Door8;
static void door_enter(Ent *e, Player *p) {
  Door8 *d = ST(e, Door8);
  (void)p;
  if (d->triggered) return;
  d->triggered = 1;
  Ent *cs = scene_new(&ENTERDOOR, door_end, true, false);
  if (cs) ST(cs, EnterDoor)->target_x = d->left;
}
static const EntClass DOORTRIG = {.name = "eventTrigger", .size = sizeof(Door8), .kind = KIND_TRIGGER,
                                  .more = &(const EntMore){.on_enter = door_enter}};

/* ---------------------------------------------------------------- the factories */
bool ents_ch8(const EData *d) {
  if (ending_due && g_session.area == 8) {   /* LoadLevel(IntroTypes.None), then CS08_Ending is added */
    ending_due = 0;
    ending_start();
  }
  switch (d->type) {
    case ET_bridgeFixed: new_bridge_fixed(d); return true;
    case ET_npc: {
      const char *who = EAS(d, npc, npc);
      bool granny = !strcmp(who, "granny_08_inside");
      if (!granny && strcmp(who, "theo_08_inside")) return false;
      Ent *e = npc_new(d->x, d->y, granny ? SB_granny : SB_theo, granny ? A_granny_idle : A_theo_idle, NULL);
      if (!e) return true;
      Npc *n = NPC_(e);
      n->spr.sx = -1;
      n->idle_anim = granny ? A_granny_idle : A_theo_idle, n->move_anim = granny ? A_granny_walk : A_theo_walk;
      n->maxspeed = 30;
      n->which = granny ? NPC_GRANNY08 : NPC_THEO08;
      if (granny) e->depth = -10;
      return true;
    }
  }
  return false;
}
bool trigs_ch8(const EData *d) {
  if (d->type != TT_eventTrigger || strcmp(EAS(d, eventTrigger, event), "ch8_door")) return false;
  Ent *e = ent_new(&DOORTRIG, d->x, d->y);
  if (e) ent_box(e, EA(d, eventTrigger, width), EA(d, eventTrigger, height), 0, 0), e->visible = 0, ST(e, Door8)->left = d->x;
  return true;
}
