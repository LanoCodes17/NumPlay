/* The game: a scene, the Knight in it, the camera; ticks of 1/50 s. */
#include "game.h"

PlayerData g_pd;
Game g_game;
static uint8_t inside[MAX_ENTS / 8];   /* the triggers the Knight is in */

void game_new(void) {
  memset(&g_pd, 0, sizeof g_pd);
  memset(&g_game, 0, sizeof g_game);
  g_game.time_scale = 1;
  g_pd.health = g_pd.max_health = 5;
  g_pd.max_mp = 99;
  g_pd.nail_damage = 5;
  g_pd.can_dash = false;
}

bool game_enter(int room, float x, float y, bool facing_right) {
  if (!room_load(room)) return false;
  memset(inside, 0, sizeof inside);
  world_enter();
  hero_init(x, y, facing_right);
  cam_init();
  world_hero_in_position();
  g_game.hazard_x = x, g_game.hazard_y = y, g_game.hazard_facing_right = facing_right;
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
      world_trigger(i, kind);
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
    if (e->type != ENT_CAMLOCK && e->type != ENT_GATE && e->type != ENT_HAZARD_TRIGGER && e->type != ENT_MASK) continue;
    bool in = false;
    for (int j = i; j < n && (j == i || es[j].type == ENT_BOX); j++)
      in |= x1 > es[j].x0 && x0 < es[j].x1 && y1 > es[j].y0 && y0 < es[j].y1;
    bool was = inside[i >> 3] >> (i & 7) & 1;
    if (in) inside[i >> 3] |= (uint8_t)(1 << (i & 7));
    else inside[i >> 3] &= (uint8_t)~(1 << (i & 7));
    if (in || was) trigger_event(i, e, in && was ? EV_STAY : in ? EV_ENTER : EV_EXIT);
  }
}

/* ---------------------------------------------------------------- GameManager's coroutines */
enum { FZ_NONE, FZ_DOWN, FZ_WAIT, FZ_UP };

/* FreezeMoment(rampDownTime, waitTime, rampUpTime, targetSpeed): time slows to the target (in unscaled time), stays,
 * then comes back; hero: the Knight's recoil goes on after (StartRecoil waits for it) */
void game_freeze(float down, float wait, float up, float target, bool hero) {
  Game *g = &g_game;
  g->freeze_phase = FZ_DOWN, g->freeze_t = 0, g->freeze_from = g->time_scale;
  g->freeze_down = down, g->freeze_wait = wait, g->freeze_up = up, g->freeze_target = target;
  g->freeze_hero = g->freeze_hero || hero;
}

void game_freeze_moment(void) { game_freeze(0.001f, 0.25f, 0.05f, 0.0001f, true); }   /* (DAMAGE_FREEZE_*) */

static void set_time_scale(float s) { g_game.time_scale = s > 0.01f ? s : 0; }

static void freeze_tick(float real_dt) {
  Game *g = &g_game;
  if (g->freeze_phase == FZ_NONE) return;
  /* (SetTimeScale's loop: the scale for this frame, then the timer) */
  if (g->freeze_phase == FZ_DOWN) {
    if (g->freeze_t >= g->freeze_down) {
      set_time_scale(g->freeze_target);
      g->freeze_phase = FZ_WAIT, g->freeze_t = 0;
    } else
      set_time_scale(g->freeze_from + (g->freeze_target - g->freeze_from) * (g->freeze_t / g->freeze_down));
  }
  if (g->freeze_phase == FZ_WAIT) {
    if (g->freeze_t >= g->freeze_wait) g->freeze_phase = FZ_UP, g->freeze_t = 0, g->freeze_from = g->time_scale;
  }
  if (g->freeze_phase == FZ_UP) {
    if (g->freeze_t >= g->freeze_up) {
      set_time_scale(1);
      g->freeze_phase = FZ_NONE;
      if (g->freeze_hero) g->freeze_hero = false, hero_recoil_unfreeze();
      return;
    }
    set_time_scale(g->freeze_from + (1 - g->freeze_from) * (g->freeze_t / g->freeze_up));
  }
  g->freeze_t += real_dt;
}

/* the camera's fades (its Blanker): to black and back */
void game_fade(float to, float time, float delay) {
  Game *g = &g_game;
  g->fade_from = g->fade, g->fade_to = to, g->fade_t = 0, g->fade_time = time, g->fade_delay = delay;
}

static void fade_tick(void) {
  Game *g = &g_game;
  if (g->fade_delay > 0) {
    g->fade_delay -= 0.02f;
    return;
  }
  if (g->fade == g->fade_to) return;
  g->fade_t += 0.02f;
  float k = g->fade_time <= 0 || g->fade_t >= g->fade_time ? 1 : g->fade_t / g->fade_time;
  g->fade = g->fade_from + (g->fade_to - g->fade_from) * k;
  g_screen_fade = (uint8_t)(g->fade * 255 + 0.5f);
}

/* PlayerDeadFromHazard: the camera stops, the screen goes black, the Knight comes back at the hazard marker */
enum { HZ_NONE, HZ_NEXT_FRAME, HZ_FADING };
void game_player_dead_from_hazard(void) { g_game.hazard_phase = HZ_NEXT_FRAME, g_game.hazard_t = 0; }

static void hazard_tick(void) {
  Game *g = &g_game;
  if (g->hazard_phase == HZ_NEXT_FRAME) {
    cam_freeze();
    game_fade(1, 0.75f, 0);   /* (HAZARD FADE) */
    g->hazard_phase = HZ_FADING, g->hazard_t = 0;
  } else if (g->hazard_phase == HZ_FADING) {
    g->hazard_t += 0.02f;
    if (g->hazard_t >= 0.8f) {
      g->hazard_phase = HZ_NONE;
      hero_hazard_respawn();
    }
  }
}

/* ---------------------------------------------------------------- a tick: 1/50 s of real time */
static void step(uint32_t keys) {
  g_game.time += 0.02f;
  hero_fixed(keys);
  enemies_fixed();
  triggers_tick();
  hero_check_damage();
  hero_update();
  world_tick();
  hazard_tick();
  fade_tick();
  hero_late_update();
  cam_tick();
}

void game_tick(uint32_t keys) {
  Game *g = &g_game;
  freeze_tick(0.02f);
  /* (game time runs at the time scale: frozen, no steps) */
  g->step_acc += 0.02f * g->time_scale;
  if (g->step_acc >= 0.02f - 1e-6f) {
    g->step_acc -= 0.02f;
    step(keys);
  }
}

void game_draw(void) {
  obj_draw();
  hero_draw();
  gfx_frame();
}
