/* The game: a scene, the Knight in it, the camera; ticks of 1/50 s. */
#include "game.h"

PlayerData g_pd;
Game g_game;
static uint8_t inside[MAX_ENTS / 8];   /* the triggers the Knight is in */
static uint8_t ent_off[MAX_ENTS / 8];  /* (triggers whose objects are off) */

bool room_flag(int flag) { return flag >= 0 && flag < PDF_COUNT && pd_flag(flag); }

void ent_set_enabled(int i, bool on) {
  if (i < 0 || i >= MAX_ENTS) return;
  if (on) ent_off[i >> 3] &= (uint8_t)~(1 << (i & 7));
  else ent_off[i >> 3] |= (uint8_t)(1 << (i & 7));
}

void game_new(void) {
  memset(&g_pd, 0, sizeof g_pd);
  memset(&g_game, 0, sizeof g_game);
  g_game.time_scale = 1;
  g_pd.health = g_pd.max_health = 5;
  g_pd.max_mp = 99;
  g_pd.nail_damage = 5;
  g_pd.can_dash = false;
  g_pd.charm_slots = 3;
  strcpy(g_pd.respawn_scene, "Tutorial_01");
  strcpy(g_pd.respawn_marker, "Death Respawn Marker");
  dialogue_reset();
  prompts_reset();
  death_reset();
}

bool game_enter(int room, float x, float y, bool facing_right) {
  if (!room_load(room)) return false;
  memset(inside, 0, sizeof inside);
  memset(ent_off, 0, sizeof ent_off);
  prompts_reset();
  world_enter();
  hero_init(x, y, facing_right);
  cam_init();
  world_hero_in_position();
  g_game.hazard_x = x, g_game.hazard_y = y, g_game.hazard_facing_right = facing_right;
  return true;
}

/* HeroController.Respawn, as GameManager starts it: the respawn scene, the Knight at its marker (on the ground below),
 * health back, soul gone; on a bench he wakes on it, else on the ground */
bool game_respawn(void) {
  int room = -1;
  for (int i = 0; i < NUM_ROOMS && room < 0; i++)
    if (!strcmp(room_name(i), g_pd.respawn_scene)) room = i;
  if (room < 0 || !room_load(room)) return false;
  memset(inside, 0, sizeof inside);
  memset(ent_off, 0, sizeof ent_off);
  prompts_reset();
  world_enter();
  float x = g_room.h->w / 2, y = g_room.h->h / 2;
  bool right = g_pd.respawn_facing_right;
  int n;
  const Ent *es = room_ents(&n);
  for (int i = 0; i < n; i++)
    if (es[i].type == ENT_RESPAWN && !strcmp(str_at(es[i].s0), g_pd.respawn_marker)) {
      x = es[i].x0, y = es[i].y0;
      if (g_pd.respawn_type != 1) right = es[i].flags & FACING_RIGHT;
      break;
    }
  hero_init(x, y, right);
  g_hero.body.y = hero_ground_y(x, y);
  g_pd.health = g_pd.max_health;
  g_pd.mp = 0;   /* (ClearMP) */
  g_game.hazard_x = g_hero.body.x, g_game.hazard_y = g_hero.body.y, g_game.hazard_facing_right = right;
  world_hero_in_position();
  if (g_pd.respawn_type == 1) {
    /* (FinishedEnteringScene, then the bench's RESPAWN) */
    hero_finished_entering_scene(true);
    if (!bench_respawn(g_pd.respawn_marker)) hero_wake_up_ground();
  } else
    hero_wake_up_ground();
  cam_init();
  /* (the camera's RESPAWN: from black) */
  g_game.fade = 1, g_screen_fade = 255;
  game_fade(0, 0.5f, 0.1f);
  return true;
}

const Ent *room_ents(int *n) {
  *n = g_room.h->nent;
  return (const Ent *)(const void *)(section(SEC_RBLOB) + g_room.h->ents);
}

/* ---------------------------------------------------------------- triggers: the Knight's box in the objects' */

/* ---------------------------------------------------------------- scene transitions (TransitionPoint, BeginSceneTransition) */
enum { SP_NONE, SP_LEAVING };

/* TransitionPoint.GetGatePosition: by its name */
static int gate_kind(const Ent *e) {
  if (e->flags & G_DOOR) return GATE_DOOR;
  const char *n = str_at(e->s0);
  if (strstr(n, "top")) return GATE_TOP;
  if (strstr(n, "right")) return GATE_RIGHT;
  if (strstr(n, "left")) return GATE_LEFT;
  if (strstr(n, "bot")) return GATE_BOTTOM;
  return strstr(n, "door") ? GATE_DOOR : GATE_UNKNOWN;
}

void game_fade_scene_in(void) { game_fade(0, 0.5f, 0.1f); }

void game_transition(int room, int entry, int gate, float delay, bool without_input) {
  Game *g = &g_game;
  if (g->scene_phase != SP_NONE || room < 0) return;
  g->scene_phase = SP_LEAVING, g->scene_t = 0;
  g->next_room = (int16_t)room, g->next_entry = (uint16_t)entry, g->next_delay = delay;
  g_hero.enter_without_input = without_input;
  hero_leave_scene(gate);
  cam_freeze();
  if (g->fade < 1) game_fade(1, 0.33f, 0);   /* (the camera's FADE OUT) */
}

/* TryDoTransition: the Knight through a gate (facing it, not recoiling), or pushed back out of it */
static void gate_touched(const Ent *e) {
  Hero *h = &g_hero;
  if ((e->flags & G_DOOR) || g_game.scene_phase != SP_NONE) return;
  int g = gate_kind(e);
  bool back = h->cs.recoiling || (g == GATE_RIGHT && !h->cs.facing_right) || (g == GATE_LEFT && h->cs.facing_right) ||
              e->a == 0xFFFF;   /* (or to a room this game leaves out) */
  const Body *b = &h->body;
  if (back && (g == GATE_RIGHT || g == GATE_LEFT)) {
    h->body.vx = 0;
    h->body.x += g == GATE_RIGHT ? e->x0 - (b->x + b->ox + b->hx) : e->x1 - (b->x + b->ox - b->hx);
  } else if (back && (g == GATE_TOP || g == GATE_BOTTOM)) {
    h->body.vy = 0;
    h->body.y += g == GATE_TOP ? e->y0 - (b->y + b->oy + b->hy) : e->y1 - (b->y + b->oy - b->hy);
  } else if (!back)
    game_transition(e->a, e->s1, g, e->p0, false);
}

/* the new room, the Knight at its entry gate */
static void scene_load(void) {
  Game *g = &g_game;
  g->scene_phase = SP_NONE;
  if (!room_load(g->next_room)) return;
  memset(inside, 0, sizeof inside);
  memset(ent_off, 0, sizeof ent_off);
  prompts_reset();   /* (PromptMarker.RecycleOnLevelLoad) */
  world_enter();
  int n;
  const Ent *es = room_ents(&n);
  const char *want = str_at(g->next_entry);
  for (int i = 0; i < n; i++) {
    const Ent *e = &es[i];
    if (e->type != ENT_GATE || strcmp(str_at(e->s0), want)) continue;
    float gx = (e->x0 + e->x1) / 2, gy = (e->y0 + e->y1) / 2;
    for (int j = i + 1; j < n && es[j].type == ENT_BOX; j++)
      if (es[j].flags & 1) gx = es[j].x0, gy = es[j].y0;
    hero_enter_scene(i, gate_kind(e), gx, gy, e->p1, e->p2, e->flags, g->next_delay + e->p0);
    return;
  }
  /* (no such gate: in the middle of the room) */
  hero_enter_scene(-1, GATE_DOOR, g_room.h->w / 2, g_room.h->h / 2, 0, 0, 0, g->next_delay);
}

static void scene_tick(float real_dt) {
  Game *g = &g_game;
  if (g->scene_phase != SP_LEAVING) return;
  /* (the scene loads once the camera has faded: half a second) */
  g->scene_t += real_dt;
  if (g->scene_t >= 0.5f) scene_load();
}

static void trigger_event(int i, const Ent *e, int kind) {
  Hero *h = &g_hero;
  switch (e->type) {
    case ENT_CAMLOCK:
      if (kind == EV_EXIT) cam_release(i);
      else cam_lock(i);   /* (Enter and Stay) */
      break;
    case ENT_GATE:
      if (kind == EV_ENTER) gate_touched(e);
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
    bool in = false, off = ent_off[i >> 3] >> (i & 7) & 1;
    for (int j = i; j < n && (j == i || es[j].type == ENT_BOX); j++)
      if (j == i || !(es[j].flags & 1)) in |= x1 > es[j].x0 && x0 < es[j].x1 && y1 > es[j].y0 && y0 < es[j].y1;
    bool was = inside[i >> 3] >> (i & 7) & 1;
    if (off) in = false;
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
  fireballs_tick();
  death_tick();
  world_tick();
  benches_tick();
  titles_tick();
  prompts_tick();
  dialogue_tick();
  hazard_tick();
  fade_tick();
  hero_late_update();
  cam_tick();
  hud_tick();
}

void game_tick(uint32_t keys) {
  Game *g = &g_game;
  freeze_tick(0.02f);
  scene_tick(0.02f);
  /* (game time runs at the time scale: frozen, no steps) */
  g->step_acc += 0.02f * g->time_scale;
  if (g->step_acc >= 0.02f - 1e-6f) {
    g->step_acc -= 0.02f;
    step(keys);
  }
}

void game_draw(void) {
  hud_draw();
  titles_draw();
  dialogue_draw();
  prompts_draw();
  obj_draw();
  hero_draw();
  fireballs_draw();
  death_draw();
  gfx_frame();
}
