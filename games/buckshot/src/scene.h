/* What is on screen: the two views of the table composed from the match state
 * (the table, and the Dealer up close), the other views (charge display,
 * shells, cutscenes), the original's dialogue box and item descriptions, and
 * the animations of every action. */
#ifndef BR_SCENE_H
#define BR_SCENE_H
#include "gfx.h"
#include "game.h"

enum { V_TABLE, V_DEALER };

/* How the Dealer looks right now */
typedef struct {
  int16_t head;    /* table view: IMG_T_HEAD0..3; Dealer view: IMG_D_HEAD; -1 none */
  int16_t hands;   /* table view: IMG_T_HANDS or IMG_T_CUFFED; Dealer view: IMG_D_HANDS; -1 none */
  int16_t pose;    /* Dealer view: a pose drawn over him at rest (items, shotgun), -1 none */
  int16_t alone;   /* Dealer view: a picture of him drawn instead (flying back), -1 none */
  int8_t dy;       /* breathing */
  uint8_t gone;    /* not at the table */
  uint8_t dim;     /* 0..3 darker (coming back from the dark) */
} dealer_look_t;

typedef struct {
  uint8_t view;
  dealer_look_t d;
  int16_t pose;       /* first-person picture over everything (the player holding something), or -1 */
  int16_t pose2;      /* second overlay (shell in the chamber, a shell flying out) */
  int8_t pose_dy;     /* the first picture moved down (brought up from below) */
  uint8_t gun_on_table;
  uint8_t box;        /* item box open */
  int8_t held;        /* item taken from the box, shown in the slot under the cursor */
  int16_t cursor;     /* 0..7 player slot, 8 shotgun, 16+i Dealer slot, -1 none */
  const char *title;  /* item name at the bottom */
  const char *desc;
  const char *say;    /* dialogue box */
  int say_len;        /* characters shown (typewriter) */
  const char *hint;   /* small control prompt */
  uint8_t lbl;        /* shotgun targets: 0 off, 1 Dealer chosen, 2 you chosen */
  uint8_t rush;       /* adrenaline: the picture is brighter */
  uint8_t no_charges; /* hide the charges on the table's display */
} table_t;

/* what an action did, for its animation */
typedef struct {
  int from;           /* where the item lay: side*8+slot, or -1 */
  int live;           /* the shell in the chamber (before the action) */
  int good, dead;     /* expired medicine */
  const char *text;   /* burner phone */
} act_t;

extern table_t T;

void table_reset(void);
void table_draw(void);        /* compose the frame buffer */
extern void (*redraw)(void);  /* what to put back after the pause menu */
void table_show(void);        /* compose and present */
void ui_dialogue(const char *s, int len);
void ui_hint(const char *s);
void target_pos(int t, int *x, int *y); /* screen centre of a target in the table view */

void view_table(void);        /* the table view, the Dealer at rest */
void view_dealer(void);       /* the Dealer up close, at rest */
int idle_key(void);           /* a key, or -1 after a step of the Dealer's breathing */

void say(const char *s, int ms);      /* dialogue, typed then held (OK cuts it short) */
void say_on(const char *s);           /* ... and left on screen */
void say_off(void);

void health_view(int blink_side, int ms); /* the charge display; blink_side -1 none */
void health_bootup(void);
void round_indicator(void);
void health_wins(void);
void wire_cut(int side);
void shells_view(const uint8_t *shown, int n, const char *text, int ms);
void fade_to(int from, int to, int ms);
void flash(int live, int shake);

/* animations of the match (anim.c) */
void anim_item(int side, int it, const act_t *a);
int anim_player_aim(void);    /* the player picks up the shotgun and aims: 1 = at himself */
void anim_player_shot(int self, int live, int dmg, int ended);
void anim_dealer_shot(int self, int live, int dmg, int ended);
void anim_player_cuffed(int broke);
void anim_dealer_cuffed(int broke);
void anim_wire_cut(int side, int say_it);
void anim_god_waiver(void);
void anim_dealer_load(int n, int told, int fast);
void anim_dealer_rack(void);
void anim_dealer_return(void);
void anim_dealer_arrives(const char *line);
#endif
