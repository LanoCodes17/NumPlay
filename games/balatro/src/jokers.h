#ifndef JOKERS_H
#define JOKERS_H
#include "game.h"

/* contexts, as in Card:calculate_joker */
enum {
  CX_OPEN_BOOSTER, CX_BUYING_CARD, CX_SELLING_SELF, CX_SELLING_CARD, CX_REROLL, CX_ENDING_SHOP, CX_SKIP_BLIND,
  CX_SKIPPING_BOOSTER, CX_CARDS_ADDED, CX_FIRST_HAND_DRAWN, CX_SETTING_BLIND, CX_DESTROYING_CARD,
  CX_REMOVE_CARDS, CX_USING_CONS, CX_DEBUFFED_HAND, CX_PRE_DISCARD, CX_DISCARD, CX_END_ROUND, CX_REP_END_ROUND,
  CX_INDIVIDUAL, CX_REPETITION, CX_OTHER_JOKER, CX_BEFORE, CX_AFTER, CX_MAIN
};

typedef struct {
  int kind;
  int blueprint;      /* copy depth */
  int bp;             /* the joker showing the effect (Blueprint/Brainstorm), -1 */
  int other;          /* other playing card */
  int area;           /* A_PLAY or A_HAND */
  int other_joker;
  const handinfo_t *hi;
  const uint8_t *full; int nfull;   /* played (or discarded) cards */
  int card_effects;   /* Mime: the held card had effects */
  int game_over;
  int boss;           /* setting_blind: the blind is a boss */
  int cons_set, cons_id;
  int n;              /* cards added */
  int nfaces_removed, nglass_removed;
  int hook;           /* discard caused by The Hook */
} cx_t;

enum { EF_CHIPS = 1, EF_MULT = 2, EF_XMULT = 4, EF_DOLLARS = 8, EF_REPS = 16, EF_HMULT = 32, EF_MSG = 64,
       EF_LEVELUP = 128, EF_REMOVE = 256, EF_SAVED = 512 };
typedef struct {
  int has;
  double chips, mult, x;
  int dollars, reps;
  const char *msg;
  int col;
  int card;          /* joker index shown (after Blueprint) */
  int focus;         /* the message goes on this joker (Wee Joker) */
} eff_t;

int jcalc(int ji, cx_t *cx, eff_t *e);
void joker_init(joker_t *j);
int joker_x_display(const joker_t *j, double *out); /* current values for descriptions */
#endif
