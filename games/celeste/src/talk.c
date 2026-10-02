#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("Os")   /* not drawn every frame: smaller over faster */
#endif
/* TalkComponent and TalkComponentUI, line by line from the game's code. */
#include "talk.h"

Ent *g_talk_over;

void talk_init(Talk *t, int x, int y, int w, int h, V2 draw_at) {
  memset(t, 0, sizeof *t);
  t->bx = (int16_t)x, t->by = (int16_t)y, t->bw = (int16_t)w, t->bh = (int16_t)h;
  t->draw_at = draw_at;
  t->enabled = t->must_face = true;
  t->alpha = 1;
  wiggler_init(&t->wig, 0.25f, 4);
}

/* a fake wall over the point (the prompt behind it shows once it is gone) */
static bool in_fake_wall(float x, float y) {
  for (int i = 0; i < g_nents; i++) {
    Ent *o = &g_ents[i];
    if (o->cls && o->dead != 1 && o->collidable && o->cls->name && !strcmp(o->cls->name, "fakeWall") &&
        collide_rect(o, x, y, x + 1, y + 1))
      return true;
  }
  return false;
}

static bool textbox_up(void) {
  for (int i = 0; i < g_nents; i++)
    if (g_ents[i].cls && g_ents[i].dead != 1 && g_ents[i].cls->name && !strcmp(g_ents[i].cls->name, "textbox")) return true;
  return false;
}
/* TalkComponentUI.Display */
static bool display(const Talk *t) {
  Player *p = level_player();
  return t->enabled && !textbox_up() && p && !p->dead && p->state != ST_DUMMY && !g_level.frozen && !g_level.paused;
}

bool talk_update(Talk *t, Ent *e) {
  Player *p = level_player();
  bool talked = false;
  if (!t->inited) {   /* the UI comes with the first update (and starts hidden behind a fake wall) */
    t->inited = true;
    if (in_fake_wall(e->x, e->y)) t->alpha = 0;
  }
  bool over = false;
  if (t->disable_delay < 0.05f && p && !p->dead) {
    Ent *pe = p->ent;
    float l = (float)(int)(e->x + t->bx), tp = (float)(int)(e->y + t->by);
    over = collide_rect(pe, l, tp, l + t->bw, tp + t->bh) && p->on_ground && e_bottom(pe) < e->y + t->by + t->bh + 4 &&
           p->state == ST_NORMAL &&
           (!t->must_face || fabsf(pe->x - e->x) <= 16 || p->facing == (int)signf(e->x - pe->x)) &&
           (!g_talk_over || g_talk_over == e);
  }
  if (over) t->hover_timer += DT;
  else if (display(t)) t->hover_timer = 0;
  if (g_talk_over == e && !over) g_talk_over = NULL;
  else if (over) g_talk_over = e;
  if (over && t->cooldown <= 0 && p->state == ST_NORMAL && btn_pressed(&g_in.talk) && t->enabled && !g_level.paused) {
    t->cooldown = 0.1f;
    talked = true;
  }
  if (over && p->state == ST_NORMAL) t->cooldown -= DT;
  if (!t->enabled) t->disable_delay += DT;
  else t->disable_delay = 0;
  bool hl = over && t->hover_timer > 0.1f;
  if (hl != t->highlighted && display(t)) {   /* Highlighted's setter */
    t->highlighted = hl;
    wiggler_restart(&t->wig);
  }
  /* the UI's update */
  t->timer += DT;
  t->slide = approach(t->slide, display(t) ? 1.f : 0.f, DT * 4);
  if (t->alpha < 1 && !in_fake_wall(e->x, e->y)) t->alpha = approach(t->alpha, 1, 2 * DT);
  wiggler_update(&t->wig);
  return talked;
}

void talk_removed(Ent *e) {
  if (g_talk_over == e) g_talk_over = NULL;
}

/* the talk key's cap (the calculator's Backspace) */
static void key_icon(float x, float y, float s, uint8_t a) {
  /* a backspace key: an arrow-shaped outline, an x in it */
  float w = 9 * s, h = 6 * s;
  float l = x - w / 2, r = x + w / 2, t = y - h / 2, b = y + h / 2, tip = l - 3 * s;
  gfx_line(tip, y, l, t, 0, a), gfx_line(tip, y, l, b, 0, a);
  gfx_line(l, t, r, t, 0, a), gfx_line(l, b, r, b, 0, a), gfx_line(r, t, r, b, 0, a);
  gfx_line(tip + 1, y, l + 1, t + 1, 0xFFFF, a), gfx_line(tip + 1, y, l + 1, b - 1, 0xFFFF, a);
  gfx_line(l + 1, t + 1, r - 1, t + 1, 0xFFFF, a), gfx_line(l + 1, b - 1, r - 1, b - 1, 0xFFFF, a);
  gfx_line(r - 1, t + 1, r - 1, b - 1, 0xFFFF, a);
  float cx = x + 0.5f * s;
  gfx_line(cx - 1.5f * s, y - 1.5f * s, cx + 1.5f * s, y + 1.5f * s, 0xFFFF, a);
  gfx_line(cx - 1.5f * s, y + 1.5f * s, cx + 1.5f * s, y - 1.5f * s, 0xFFFF, a);
}

void talk_render(Talk *t, Ent *e) {
  if (g_level.frozen || g_level.paused || !(t->slide > 0)) return;
  float x = (e->x + t->draw_at.x - floorf(g_level.cam.x)) * 6, y = (e->y + t->draw_at.y - floorf(g_level.cam.y)) * 6;
  y += sinf(t->timer * 4) * 12 + 64 * (1 - ease_cube_out(t->slide));
  float k = !t->highlighted ? 1 + t->wig.value * 0.5f : 1 - t->wig.value * 0.5f;
  float a = ease_cube_inout(t->slide) * t->alpha;
  uint8_t al = (uint8_t)(a * t->alpha * 255);
  uint16_t tex = t->highlighted ? T__hover_highlight : T__hover_idle;
  Tex tx;
  gfx_hud(true);
  if (tex_get(tex, &tx)) gfx_tex_ex(tex, x / 6, y / 6, tx.fw * tx.scale * 0.5f, (float)tx.fh * tx.scale, k, k, 0, 0xFFFF, al, 0);
  if (t->highlighted) key_icon(x / 6, (y - 75 * k) / 6, k, (uint8_t)(a * 255));
  gfx_hud(false);
}
