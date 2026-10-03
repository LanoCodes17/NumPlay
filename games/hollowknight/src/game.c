/* The game: a scene, the Knight in it, the camera; ticks of 1/50 s. */
#include "game.h"

PlayerData g_pd;
Game g_game;
static uint8_t inside[MAX_ENTS / 8];   /* the triggers the Knight is in */

void game_new(void) {
  memset(&g_pd, 0, sizeof g_pd);
  g_pd.health = g_pd.max_health = 5;
  g_pd.max_mp = 99;
  g_pd.can_dash = false;
}

bool game_enter(int room, float x, float y, bool facing_right) {
  if (!room_load(room)) return false;
  memset(inside, 0, sizeof inside);
  hero_init(x, y, facing_right);
  cam_init();
  return true;
}

const Ent *room_ents(int *n) {
  *n = g_room.h->nent;
  return (const Ent *)(const void *)(section(SEC_RBLOB) + g_room.h->ents);
}

/* ---------------------------------------------------------------- triggers: the Knight's box in the objects' */

static void trigger_event(int i, const Ent *e, int kind) {
  Hero *h = &g_hero;
  switch (e->type) {
    case ENT_CAMLOCK:
      if (kind == EV_EXIT) cam_release(i);
      else cam_lock(i);   /* (Enter and Stay) */
      break;
    case ENT_HAZARD_TRIGGER:
      if (kind == EV_ENTER) {
        int n;
        const Ent *m = &room_ents(&n)[e->a];
        g_game.hazard_x = m->x0, g_game.hazard_y = m->y0, g_game.hazard_facing_right = m->flags & FACING_RIGHT;
      }
      break;
    default:
      break;
  }
  (void)h;
}

static void triggers_tick(void) {
  const Body *b = &g_hero.body;
  float x0 = b->x + b->ox - b->hx, x1 = b->x + b->ox + b->hx, y0 = b->y + b->oy - b->hy, y1 = b->y + b->oy + b->hy;
  int n;
  const Ent *es = room_ents(&n);
  if (n > MAX_ENTS) n = MAX_ENTS;
  for (int i = 0; i < n; i++) {
    const Ent *e = &es[i];
    if (e->type != ENT_CAMLOCK && e->type != ENT_GATE && e->type != ENT_HAZARD_TRIGGER) continue;
    bool in = x1 > e->x0 && x0 < e->x1 && y1 > e->y0 && y0 < e->y1;
    bool was = inside[i >> 3] >> (i & 7) & 1;
    if (in) inside[i >> 3] |= (uint8_t)(1 << (i & 7));
    else inside[i >> 3] &= (uint8_t)~(1 << (i & 7));
    if (in || was) trigger_event(i, e, in && was ? EV_STAY : in ? EV_ENTER : EV_EXIT);
  }
}

void game_tick(uint32_t keys) {
  g_game.time += 0.02f;
  hero_fixed(keys);
  triggers_tick();
  hero_update();
  cam_tick();
}

void game_draw(void) {
  hero_draw();
  gfx_frame();
}
