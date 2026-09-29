/* The scene manager, input and scoring (see game.h). */
#include "game.h"
#include <math.h>
#include <stdio.h>
#include "font.h"
#include "gfx.h"
#include "spr.h"

Input in;
Game game;
bool game_running = true;

/* the big memory: the background layer (if the scene has one) and the sprite cache */
static uint8_t arena[ARENA_BYTES] __attribute__((aligned(4)));

/* the arena: the scene's state, then its background layer, then the sprite cache */
static uint32_t state_bytes;
static struct { int w, h; uint8_t sheet; BgPaint paint; } bg_def;

static void relayout(void) {
  uint32_t bg = bg_def.w > 0 && bg_def.h > 0 ? (uint32_t)(bg_def.w * bg_def.h + 3) & ~3u : 0;
  if (state_bytes + bg > ARENA_BYTES - 16 * 1024) bg = 0;
  if (bg) bg_setup(arena + state_bytes, bg_def.w, bg_def.h, bg_def.sheet, bg_def.paint); else bg_off();
  spr_setup(arena + state_bytes + bg, ARENA_BYTES - state_bytes - bg);
}

void mem_layout(int bg_w, int bg_h, uint8_t sheet, BgPaint paint) {
  bg_def.w = bg_w;
  bg_def.h = bg_h;
  bg_def.sheet = sheet;
  bg_def.paint = paint;
  relayout();
}

void *scene_state(uint32_t size) {
  size = (size + 7) & ~7u;
  if (size > SCENE_STATE_MAX) size = SCENE_STATE_MAX;
  state_bytes = size;
  memset(arena, 0, size);
  relayout();
  return arena;
}

/* ---------------------------------------------------------------- input */
static uint32_t prev_keys;

void input_tick(void) {
  uint32_t k = plat_keys();
  float x = (float)((k & K_RIGHT ? 1 : 0) - (k & K_LEFT ? 1 : 0)), y = (float)((k & K_DOWN ? 1 : 0) - (k & K_UP ? 1 : 0));
  float len = sqrtf(x * x + y * y);
  if (len > 1) { x /= len; y /= len; }
  in.jx = x;
  in.jy = y;
  bool now[A_COUNT] = {x < -.1f, x > .1f, y < -.1f, y > .1f, (k & K_ACTION) != 0, (k & K_BACK) != 0};
  for (int i = 0; i < A_COUNT; i++) {
    in.pressed[i] = now[i] && !in.held[i];
    in.released[i] = !now[i] && in.held[i];
    in.held[i] = now[i];
  }
  in.home = (k & K_HOME) && !(prev_keys & K_HOME);
  in.pause = (k & K_PAUSE) && !(prev_keys & K_PAUSE);
  prev_keys = k;
}

void input_consume(int a) {
  in.pressed[a] = false;
  in.released[a] = false;
}

/* ---------------------------------------------------------------- scoring */
static const ScoreRule rules[] = {
  {"archery", false, -4000, -2000, 0},
  {"climbing", false, 100, 200, 300}, {"climbing:hard", false, 100, 200, 300},
  {"marathon", true, 100, 200, 300}, {"marathon:400m", true, 100, 200, 300}, {"marathon:800m", true, 100, 200, 300},
  {"marathon:1500m", true, 100, 200, 300}, {"marathon:5000m", true, 100, 200, 300},
  {"pingpong", false, 25, 15, 1}, {"pingpong:game", false, 25, 15, 1}, {"pingpong:tutorial", false, 3, 2, 1},
  {"pingpong:hard", false, 50, 40, 30}, {"pingpong:ultra", false, 100, 90, 60},
  {"rugby", false, 5000, 1400, 800},
  {"skate", false, 3500, 2000, 800}, {"skate:park1", false, 3500, 2000, 800}, {"skate:park2", false, 20000, 10000, 5000},
  {"skate:park3", false, 30000, 20000, 10000},
  {"swim", false, 5000, 2000, 1000}, {"swim:ballad", false, 35000, 20000, 10000}, {"swim:disco", false, 35000, 20000, 10000},
  {"swim:rock", false, 35000, 20000, 10000},
};

const ScoreRule *score_rule(const char *key) {
  for (unsigned i = 0; i < sizeof rules / sizeof rules[0]; i++)
    if (!strcmp(rules[i].key, key)) return &rules[i];
  return NULL;
}

/* the doodle's Ao: 3 = gold ... 0 = none */
int score_rating(const char *key, float g) {
  const ScoreRule *r = score_rule(key);
  if (!r) return 0;
  if (r->time) return g < r->gold ? 3 : g < r->silver ? 2 : g < r->bronze ? 1 : 0;
  return g > r->gold ? 3 : g > r->silver ? 2 : g > r->bronze ? 1 : 0;
}

float score_best(const char *key, bool *has) {
  char k[40];
  snprintf(k, sizeof k, "%s_score", key);
  Value v = store_get(k);
  *has = v.type == SV_NUM;
  return v.type == SV_NUM ? v.num : 0;
}

int rating_of(const char *g) {
  char k[40];
  snprintf(k, sizeof k, "%s_rating", g);
  return (int)store_num(k, 0);
}

/* ---------------------------------------------------------------- scenes */
/* weak, so a test build can leave scenes out (make host SPORTS=...) */
extern const SceneDef scene_overworld, scene_interior, scene_cutscene, scene_video;
extern const SceneDef scene_archery __attribute__((weak)), scene_climbing __attribute__((weak)),
    scene_marathon __attribute__((weak)), scene_pingpong __attribute__((weak)), scene_rugby __attribute__((weak)),
    scene_skate __attribute__((weak)), scene_swim __attribute__((weak));
static const SceneDef *const scenes[] = {&scene_overworld, &scene_interior, &scene_archery, &scene_climbing, &scene_marathon,
                                         &scene_pingpong, &scene_rugby, &scene_skate, &scene_swim, &scene_cutscene, &scene_video};

static const char *const sports[] = {"archery", "climbing", "marathon", "pingpong", "rugby", "skate", "swim"};
bool game_is_sport(const char *name) {
  for (unsigned i = 0; i < 7; i++)
    if (!strcmp(sports[i], name)) return true;
  return false;
}
bool game_is_world(const char *name) { return !strcmp(name, "overworld") || !strcmp(name, "interior"); }

static char pending[80];
static bool has_pending;

void game_go(const char *spec) {
  snprintf(pending, sizeof pending, "%s", spec);
  has_pending = true;
}

void game_replay(void) {
  char s[80];
  snprintf(s, sizeof s, "%s%s%s", game.name, game.variant[0] ? ":" : "", game.variant);
  game_go(s);
}

static void switch_scene(void) {
  has_pending = false;
  char name[16] = "", variant[24] = "", location[32] = "";
  /* "name:variant@location" */
  const char *at = strchr(pending, '@'), *colon = strchr(pending, ':');
  size_t nl = strcspn(pending, ":@");
  memcpy(name, pending, nl < sizeof name ? nl : sizeof name - 1);
  if (colon && (!at || colon < at)) {
    size_t vl = (size_t)((at ? at : pending + strlen(pending)) - colon - 1);
    memcpy(variant, colon + 1, vl < sizeof variant ? vl : sizeof variant - 1);
  }
  if (at) snprintf(location, sizeof location, "%s", at + 1);
  const SceneDef *def = NULL;
  for (unsigned i = 0; i < sizeof scenes / sizeof scenes[0]; i++)
    if (scenes[i] && !strcmp(scenes[i]->name, name)) def = scenes[i];
  if (!def) return;
  if (game.def && game.def->end) game.def->end();
  if (game.root) node_free(game.root);
  state_bytes = 0;
  mem_layout(0, 0, 0, NULL);
  memset(&in.pressed, 0, sizeof in.pressed);
  game.def = def;
  snprintf(game.name, sizeof game.name, "%s", name);
  snprintf(game.variant, sizeof game.variant, "%s", variant);
  snprintf(game.location, sizeof game.location, "%s", location);
  game.root = node_new(NK_CONT);
  game.enabled = true;
  game.paused = false;
  game.ticks = 0;
  game.fade = 10;
  def->start();
  if (variant[0] && def->goto_frame) def->goto_frame(variant);
  store_save();
}

void game_tick(void) {
  if (has_pending) switch_scene();
  input_tick();
  if (in.home) { game_quit(); return; }
  dialog_tick();
  menus_tick();
  hud_tick();
  bool overlay = dialog_active() || menus_active();
  in.enabled = !overlay;
  if (!game.paused && !overlay) node_tick(game.root);
  if (game.def && game.enabled && !overlay && game.def->tick) game.def->tick();
  node_update(game.root);
  game.ticks++;
  if (game.fade > 0) game.fade--;
  /* progress is saved as it changes, at most every few seconds */
  static uint32_t last_save;
  if (store_dirty() && game.ticks - last_save > 90) {
    store_save();
    last_save = game.ticks;
  }
}

/* ---------------------------------------------------------------- toast */
static char toast_text[64];
static int toast_t;
static bool toast_big;
void toast(const char *t) {
  snprintf(toast_text, sizeof toast_text, "%s", t);
  toast_t = 75;
  toast_big = false;
}
void toast_countdown(const char *t) {
  toast(t);
  toast_big = true;
  toast_t = 45;
}

void game_draw(void) {
  gfx_begin();
  if (game.def && game.def->draw_under) game.def->draw_under();
  node_draw(game.root, (Mat){1.0f / 3, 0, 0, 1.0f / 3, 0, 0});   /* the stage is 960 x 540 */
  if (game.def && game.def->draw_over) game.def->draw_over();
  hud_draw();
  dialog_draw();
  menus_draw();
  if (toast_t > 0) {
    toast_t--;
    Mat m = MAT_ID;
    m.tx = VIEW_W / 2;
    m.ty = 50;
    uint8_t a = toast_t < 10 ? (uint8_t)(toast_t * 25) : 255;
    Mat s = m;
    s.tx += 1; s.ty += 1;
    int k = toast_big ? 3 : 2;
    gfx_text_k(toast_text, s, rgb565(0x22, 0x22, 0x22), 1, 0, 0, a, k);
    gfx_text_k(toast_text, m, rgb565(0xff, 0xff, 0xff), 1, 0, 0, a, k);
  }
  if (game.fade > 0 && game.fade <= 8) gfx_rect(0, 0, VIEW_W, VIEW_H, 0, (uint8_t)(game.fade * 255 / 8));
  else if (game.fade > 8) gfx_rect(0, 0, VIEW_W, VIEW_H, 0, 255);
  gfx_end();
}

void game_quit(void) {
  store_save();
  game_running = false;
}
