/* Scenes, input and the overlays shared by every scene (dialogue, menus, HUD).
 *
 * Like the doodle, the stage holds the current scene at the bottom, then the
 * HUD, the dialogue box, the menus and a toast. Each tick: input, dialogue,
 * menus, HUD, then the scene's systems unless an overlay is using the keys. */
#ifndef CI_GAME_H
#define CI_GAME_H
#include "ci.h"
#include "node.h"
#include "store.h"

/* ---------------------------------------------------------------- input */
/* the doodle's actions: 0 left, 1 right, 2 up, 3 down, 4 action, 5 back */
enum { A_LEFT, A_RIGHT, A_UP, A_DOWN, A_ACTION, A_BACK, A_COUNT };
typedef struct {
  bool held[A_COUNT];       /* Ca: down now (cleared for everyone once used by an overlay) */
  bool pressed[A_COUNT];    /* went down this tick */
  bool released[A_COUNT];
  float jx, jy;             /* the move vector (length <= 1) */
  bool enabled;             /* false while an overlay owns the keys */
  bool home, pause;
} Input;
extern Input in;
void input_tick(void);
void input_consume(int action);     /* an overlay used this press */

/* ---------------------------------------------------------------- scenes */
typedef struct {
  const char *name;
  void (*start)(void);          /* build the scene under game.root */
  void (*tick)(void);           /* the scene's systems */
  void (*end)(void);
  void (*draw_under)(void);     /* optional: before the node tree (e.g. background layer upkeep) */
  void (*draw_over)(void);      /* optional: after the scene's nodes, before the overlays */
  void (*goto_frame)(const char *label);   /* scene "name:label" */
} SceneDef;

typedef struct {
  const SceneDef *def;
  char name[16], variant[24], location[32];   /* "name:variant@location" */
  NodeId root;                  /* the scene's own display tree */
  bool enabled;                 /* systems run */
  bool paused;                  /* scene clips do not advance (menus open) */
  uint32_t ticks;
  int fade;                     /* fade from black after a scene change */
} Game;
extern Game game;

#define ARENA_BYTES (84 * 1024)
/* a scene with a background layer of bg_w x bg_h (0: none) calls this in start() */
void mem_layout(int bg_w, int bg_h, uint8_t sheet, BgPaint paint);
/* the scene's own state (zeroed), taken from the arena until the next scene:
 * scenes keep their big state here, not in static variables (RAM is shared) */
#define SCENE_STATE_MAX (12 * 1024)
void *scene_state(uint32_t size);

void game_go(const char *spec);             /* "overworld", "pingpong:hard", "overworld@hub" */
void game_replay(void);                     /* same scene again */
bool game_is_sport(const char *name);
bool game_is_world(const char *name);       /* overworld or interior */
void game_tick(void);
void game_draw(void);
void game_quit(void);                       /* saves and leaves */
extern bool game_running;

/* overlays (menus.c, dialog.c, hud.c) */
void dialog_start(uint16_t npc_name, uint16_t node);  /* string ids */
bool dialog_active(void);
void dialog_tick(void);
void dialog_draw(void);
void dialog_init(void);

void menus_init(void);
bool menus_active(void);
bool menus_open(const char *label);         /* push a menu (frame label of the menus clip) */
void menus_close(void);
void menus_tick(void);
void menus_draw(void);
/* end of a sport: rates the score, records it, plays the outro or shows the results */
void menus_game_over(float score);
void menus_game_over_rated(float score, int rating);   /* the sport's own rating (0..3), -1: the table's */

void hud_init(void);
void hud_tick(void);
void hud_draw(void);

void toast(const char *text);               /* the doodle's centred message */
void toast_countdown(const char *text);     /* 3, 2, 1, GO */

/* sounds are not played (the calculator has no speaker) */
#define SOUND(x) ((void)0)

/* scoring (the doodle's table) */
typedef struct { const char *key; bool time; float gold, silver, bronze; } ScoreRule;
const ScoreRule *score_rule(const char *key);
int score_rating(const char *key, float score);
float score_best(const char *key, bool *has);
int rating_of(const char *game);            /* 0..3 stars earned */
#endif
