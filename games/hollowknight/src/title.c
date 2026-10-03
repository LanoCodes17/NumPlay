/* The titles (Area Title: its Area Title Control FSM) and what shows them: a room's AreaTitleController, bosses and
 * characters (TITLE_*). On a first visit to an area its large title: the fleurs drawn in above and below, the lines faded
 * up; else the small one at the bottom left (or right). Places in HUD units from the screen's center. */
#include <math.h>
#include "game.h"

#define DT 0.02f
#define HUD_PX (FOCAL / (-1.342f - CAM_Z))
#define TITLE_Y 6.1f           /* (Area Title's place; Fleur Top's, from it) */
#define FLEUR_TOP_Y 0.35f
#define SMALL_X (-13.68f)      /* (Init: Small Title X Pos, Small Title X Pos R) */
#define SMALL_X_R 13.0f

/* the FSM's states (those that take time) */
enum { T_OFF, T_PAUSE, T_TITLES_UP, T_FLEURS_UP, T_TITLES_DOWN, T_SMALL_FRAME, T_VISITED_APPEAR, T_NPC_WAIT,
       T_VISITED_DISAPPEAR, T_QUICK_DISAPPEAR };

static const int16_t table[NUM_TITLES][4] = TITLE_TABLE;   /* (main line large, main line small, below, above) */
static const float fleur_rects[2][FLEUR_FRAMES][4] = {FLEUR_TOP_RECTS, FLEUR_BOT_RECTS};

static struct {
  uint8_t st, flags;
  int8_t title;
  int8_t next, next_flags;   /* (Area Event set again while it quickly disappears: -1 none) */
  bool large;
  float t;                   /* (the state's Wait) */
  /* FadeGroup: the shown folder's alpha, fading up (1) or down (-1) over its time */
  float alpha, fade_time;
  int8_t fade;
  /* the large title's folder and bottom fleur (kept where the last title put them), the small one's place */
  float folder_y, fleur_bot_y, small_x, small_y;
  /* the fleurs: their Appear clips' time (-1: Idle), their alpha (color_fader) */
  bool fleurs;
  float fleur_t, fleur_alpha, fleur_fade;
} at = {.folder_y = -3.03f, .fleur_bot_y = -5.92f};

/* AreaTitleController: a room's; its coroutine's pause, then the title */
#define MAX_AREAS 2
static struct {
  uint16_t ent;
  bool played, waiting, visited;
  float t;
} areas[MAX_AREAS];
static int nareas;

static void fade_group(int dir, float time) {
  /* (FadeUp from 0; FadeDown from full) */
  at.fade = (int8_t)dir, at.fade_time = time, at.alpha = dir > 0 ? 0 : 1;
}

static void start(void) {
  /* Pause: the folders off, then next frame Init, Other, the checks: Visited Check */
  at.st = T_PAUSE, at.t = 0, at.fleurs = false, at.alpha = 0, at.fade = 0;
}

void title_show(int title, int flags) {
  if (title < 0 || title >= NUM_TITLES) return;
  if (at.st == T_OFF || at.st == T_PAUSE) {
    at.title = (int8_t)title, at.flags = (uint8_t)flags;
    if (at.st == T_OFF) start();
  } else if (at.st == T_QUICK_DISAPPEAR)
    at.next = (int8_t)title, at.next_flags = (int8_t)flags;   /* (Reappear?: CANCEL) */
}

static void quick_disappear(void) {
  at.next = -1;
  if (!at.large) fade_group(-1, 0.5f);   /* (the small folder's FadeDownFast) */
  at.st = T_QUICK_DISAPPEAR, at.t = 0;
}

void title_npc_convo_start(void) {
  if (at.st == T_TITLES_UP) at.t = 1e9f;   /* (straight to Fleurs Up) */
  else if (at.st == T_VISITED_APPEAR || at.st == T_NPC_WAIT) quick_disappear();
}

void title_npc_down(void) {
  if (at.st == T_NPC_WAIT) quick_disappear();
}

static float width_of(int text, int style) {
  if (text < 0) return 0;
  const uint8_t *s = text_get(text);
  return text_width(style, s, (int)strlen((const char *)s)) / HUD_PX;
}

static void titles_tick_fsm(void) {
  const int16_t *row = table[at.title];
  bool sub = row[2] >= 0, sup = row[3] >= 0;
  at.t += DT;
  if (at.fade) {
    float k = at.fade_time > 0 ? DT / at.fade_time : 1;
    at.alpha += at.fade * k;
    if (at.alpha >= 1) at.alpha = 1, at.fade = 0;
    if (at.alpha <= 0) at.alpha = 0, at.fade = 0;
  }
  if (at.fleurs) {
    if (at.fleur_t >= 0) at.fleur_t += DT;
    if (at.fleur_fade > 0) {
      at.fleur_alpha -= DT / at.fleur_fade;
      if (at.fleur_alpha <= 0) at.fleur_alpha = 0, at.fleur_fade = 0;
    }
  }
  switch (at.st) {
    case T_PAUSE:
      if (at.t < DT) break;
      if ((at.flags & (TF_NPC | TF_VISITED))) {
        /* Set Text Small: its place; a frame on, its lines' widths (Bounds, Set Margin) */
        at.large = false, at.small_y = TITLE_Y - 12.41f;
        at.st = T_SMALL_FRAME, at.t = 0;
      } else {
        /* Set Text Large: the fleurs on (Idle), then where the lines and the bottom fleur go */
        at.large = true, at.fleurs = true, at.fleur_t = -1, at.fleur_alpha = 1, at.fleur_fade = 0;
        if (sub && !sup) at.fleur_bot_y = -5.24f, at.folder_y = -2.24f;
        else if (!sub && sup) at.fleur_bot_y = -5.24f, at.folder_y = -3.03f;
        else if (!sub && !sup) at.fleur_bot_y = -4.38f, at.folder_y = -2.24f;
        at.st = T_TITLES_UP, at.t = 0;
      }
      break;
    case T_TITLES_UP:
      if (at.t >= 0.6f) {
        /* Fleurs Up: the lines fade up (4 s), the fleurs drawn in */
        fade_group(1, 4);
        at.fleur_t = 0;
        vm_broadcast(VMEV_BIG_TITLE_START);
        at.st = T_FLEURS_UP, at.t = 0;
      }
      break;
    case T_FLEURS_UP:
      if (at.t >= 4.75f) {
        fade_group(-1, 1.5f);
        at.fleur_fade = 1.5f;   /* (color_fader's DOWN) */
        at.st = T_TITLES_DOWN, at.t = 0;
      }
      break;
    case T_TITLES_DOWN:
      if (at.t >= 2) {
        /* Big Title End, Done */
        at.st = T_OFF, at.fleurs = false;
        vm_broadcast(VMEV_BIG_TITLE_END);
      }
      break;
    case T_SMALL_FRAME: {
      float half = width_of(row[1], STYLE_TITLE_S) / 2, w = width_of(row[2], STYLE_TITLE_SUB) / 2,
            u = width_of(row[3], STYLE_TITLE_SUB) / 2;
      if (w > half) half = w;
      if (u > half) half = u;
      at.small_x = (at.flags & TF_RIGHT) ? SMALL_X_R - half : SMALL_X + half;
      if (!sub) at.small_y = TITLE_Y - 13.15f;   /* (Super only 2: no line below) */
      fade_group(1, 1.5f);
      at.st = (at.flags & TF_NPC) ? T_NPC_WAIT : T_VISITED_APPEAR, at.t = 0;
      break;
    }
    case T_VISITED_APPEAR:
      if (at.t >= 6) fade_group(-1, 1.5f), at.st = T_VISITED_DISAPPEAR, at.t = 0;
      break;
    case T_VISITED_DISAPPEAR:
      if (at.t >= 1.5f) at.st = T_OFF;
      break;
    case T_QUICK_DISAPPEAR:
      if (at.t >= 0.5f) {
        if (at.next >= 0) {
          at.title = at.next, at.flags = (uint8_t)at.next_flags;
          start();
        } else
          at.st = T_OFF;
      }
      break;
  }
}

/* ---------------------------------------------------------------- AreaTitleController */
static const Ent *ent_at(int i) {
  int n;
  return &room_ents(&n)[i];
}

void titles_enter(void) {
  nareas = 0;
  int n;
  const Ent *es = room_ents(&n);
  for (int i = 0; i < n && nareas < MAX_AREAS; i++)
    if (es[i].type == ENT_OBJ && es[i].flags == OK_AREA) {
      memset(&areas[nareas], 0, sizeof areas[0]);
      areas[nareas++].ent = (uint16_t)i;
    }
}

/* Play: by its gate only (else off); CheckArea, Finish: a sub area's title is the small one; an area's large on a
 * first visit, small after, none while still in it */
static void area_play(int k) {
  const Ent *e = ent_at(areas[k].ent);
  int fl = (int)e->p3;
  areas[k].played = true;
  if (fl & AF_DOOR) {
    int n;
    const Ent *es = room_ents(&n);
    int g = g_hero.entry_gate;
    if (g < 0 || g >= n || strcmp(str_at(es[g].s0), str_at(e->s0))) return;
  }
  if ((fl & AF_AFTER_CROSSROADS) && !pd_flag(PDF_VISITED_CROSSROADS)) return;
  bool visited = true;
  if (!(fl & AF_SUB)) {
    visited = e->group != 255 && pd_flag(e->group);
    if ((!visited && (fl & AF_REVISIT)) || (int)e->p2 == g_pd.current_area) return;
  }
  areas[k].waiting = true, areas[k].visited = visited, areas[k].t = 0;
}

void titles_hero_in_position(void) {
  for (int k = 0; k < nareas; k++)
    if (!areas[k].played && !((int)ent_at(areas[k].ent)->p3 & AF_TRIGGER)) area_play(k);
}

void titles_tick(void) {
  const Body *b = &g_hero.body;
  float x0 = b->x + b->ox - b->hx, x1 = b->x + b->ox + b->hx, y0 = b->y + b->oy - b->hy, y1 = b->y + b->oy + b->hy;
  for (int k = 0; k < nareas; k++) {
    const Ent *e = ent_at(areas[k].ent);
    if (!areas[k].played && ((int)e->p3 & AF_TRIGGER) && x1 > e->x0 && x0 < e->x1 && y1 > e->y0 && y0 < e->y1)
      area_play(k);
    if (!areas[k].waiting) continue;
    /* VisitPause, UnvisitPause: the area is the current one; on a first visit, visited from now on */
    areas[k].t += DT;
    if (areas[k].t < (areas[k].visited ? e->p1 : e->p0)) continue;
    areas[k].waiting = false;
    g_pd.current_area = (uint8_t)e->p2;
    title_show(e->a, areas[k].visited ? TF_VISITED : 0);
    if (!areas[k].visited && e->group != 255) pd_set_flag(e->group, true);
  }
  if (at.st != T_OFF) titles_tick_fsm();
}

/* ---------------------------------------------------------------- drawing */
static void line(int text, int style, float x, float y, uint32_t a) {
  if (text < 0 || !a) return;
  const uint8_t *s = text_get(text), *st = font_style(style);
  int n = (int)strlen((const char *)s);
  /* (centered on its place: the baseline below it by half the font's ascender and descender) */
  float px = VIEW_W / 2 + x * HUD_PX - text_width(style, s, n) / 2;
  float py = VIEW_H / 2 - y * HUD_PX + (font_asc(st) - font_desc(st)) / 2;
  gfx_text(style, px, py, s, n, a << 24 | 0xFFFFFF, 0);
}

void titles_draw(void) {
  if (at.st == T_OFF) return;
  const int16_t *row = table[at.title];
  uint32_t a = (uint32_t)(at.alpha * 255 + 0.5f);
  bool shown = at.alpha > 0 || at.fade > 0;
  if (at.large && shown) {
    float y = TITLE_Y + at.folder_y;
    line(row[0], STYLE_TITLE_L, 0, y, a);
    line(row[2], STYLE_TITLE_SUB, 0, y - 1.11f, a);
    line(row[3], STYLE_TITLE_SUB, 0, y + 1.4f, a);
  } else if (!at.large && at.st != T_PAUSE && at.st != T_SMALL_FRAME && shown) {
    line(row[1], STYLE_TITLE_S, at.small_x, at.small_y, a);
    line(row[2], STYLE_TITLE_SUB, at.small_x, at.small_y - 0.84f, a);
    line(row[3], STYLE_TITLE_SUB, at.small_x, at.small_y + 1.09f, a);
  }
  if (at.fleurs && at.fleur_t >= 0 && at.fleur_alpha > 0) {
    /* (each frame of their Appear clips: the last one, as much of it as that frame shows) */
    int f = (int)(at.fleur_t * FLEUR_FPS);
    if (f >= FLEUR_FRAMES) f = FLEUR_FRAMES - 1;
    uint8_t tint = gfx_dyn_tint(21, 255, 255, 255, (uint8_t)(at.fleur_alpha * 255 + 0.5f));
    for (int i = 0; i < 2; i++) {
      float y = TITLE_Y + (i ? at.fleur_bot_y : FLEUR_TOP_Y);
      const float *r = fleur_rects[i][f];
      gfx_hud_rect(2 + i, r[0], y + r[1], r[2], y + r[3]);
      Inst in;
      sprite_inst(i ? SPRITE_FLEUR_BOT : SPRITE_FLEUR_TOP, 0, y, 0, 1, 1, tint, &in);
      gfx_hud(&in, 2 + i);
    }
  }
}

#ifdef HOST
#include <stdio.h>
void titles_debug(void) {
  if (at.st != T_OFF) fprintf(stderr, "title %d state %d large %d alpha %.2f fleur %.2f\n", at.title, at.st, at.large, at.alpha, at.fleur_t);
}
#endif
