/* Characters and props of many chapters (npc.c). */
#ifndef NPC_H
#define NPC_H
#include "cutscene.h"

/* ---------------------------------------------------------------- BirdTutorialGui */
enum { GC_TEXT, GC_BUTTON, GC_DIR };
enum { IN_JUMP, IN_DASH, IN_GRAB, IN_TALK, IN_PAUSE };
typedef struct {
  uint8_t kind, v;   /* GC_BUTTON: an IN_ action */
  V2 dir;            /* GC_DIR */
  char text[16];     /* GC_TEXT */
} GuiControl;
typedef struct {
  uint16_t bird;     /* g_ents index of what it points at */
  V2 offset;
  char info[32];
  float info_w, controls_w, scale;
  bool open, added;
  uint8_t n;
  GuiControl controls[4];
} Tutorial;
extern Tutorial g_tuts[2];
void tutorial_init(Tutorial *t, Ent *bird, V2 offset, const char *info_key, const GuiControl *controls, int n);
void tutorial_update(Tutorial *t);
void tutorial_render(Tutorial *t);

/* ---------------------------------------------------------------- BirdNPC */
enum { BIRD_CLIMBING, BIRD_DASHING, BIRD_DREAMJUMP, BIRD_SUPERWALLJUMP, BIRD_HYPERJUMP, BIRD_FLYAWAY, BIRD_NONE,
       BIRD_SLEEPING, BIRD_MOVETONODES, BIRD_WAITLIGHTNING };
typedef struct {
  Sprite spr;
  V2 start, speed;
  Co co;
  Tween tween;
  Tutorial *gui;
  void (*routine)(Ent *e);
  int8_t facing;
  uint8_t mode, auto_fly, first, will_end, sub_started, sub_step, fly_step, tween_new;
  uint8_t hide_then_fly, fly_now;   /* coroutines added from outside (CS00_Ending) */
} Bird;
Ent *bird_new(V2 at, int mode);
bool bird_caw(Ent *e);
bool bird_show_tutorial(Ent *e, Tutorial *t, bool caw);
bool bird_hide_tutorial(Ent *e);
bool bird_startle(Ent *e, float duration, V2 mult);
bool bird_fly_away(Ent *e, float up);
bool bird_startle_and_fly(Ent *e);
void (*bird_mode_routine(int mode))(Ent *e);   /* the other chapters' modes */
void prologue_ending_start(Player *p, Ent *bird);   /* ch0.c: CS00_Ending */

/* ---------------------------------------------------------------- NPC */
typedef struct { uint8_t step; int8_t side; Walk walk; } NpcWalk;
bool npc_approach(NpcWalk *w, Ent *npc, Sprite *spr, float spacing, int side);   /* PlayerApproach */

Ent *hahaha_new(V2 at, const char *ifset);
void hahaha_enable(Ent *e, bool on);
bool props_create(const EData *d);
V2 *wire_curve_begin(Ent *wire);   /* its curve's start (the payphone's cutscene drops it) */   /* lamp, wire, flutterbird, bird, hahaha */
#endif
