/* Buckshot: the match state, the rules, and what the screens need to know. */
#ifndef BR_GAME_H
#define BR_GAME_H
#include <stdbool.h>
#include <stdint.h>

/* items, in the original's order */
enum { IT_NONE = -1, IT_SAW, IT_GLASS, IT_BEER, IT_CIGS, IT_CUFFS, IT_MEDS, IT_PHONE, IT_ADREN, IT_INVERT, IT_COUNT };
enum { MODE_STORY, MODE_DON };
/* where a saved match resumes. Every action is settled in the match state and
 * saved before it is shown, so quitting in the middle of one resumes after it. */
enum {
  PH_NONE,
  PH_STAGE,      /* a round (three per run) starts */
  PH_LOAD,       /* items, then the shells of a new load */
  PH_PLAYER,
  PH_DEALER,
  PH_STAGE_WON,
  PH_DON_CHOICE, /* Double or Nothing: winnings counted, the offer */
  PH_ENDING,
  PH_REVIVE,     /* died in round 1 or 2: the defibrillator, then the same round again */
  PH_REDO,       /* "retry" in heaven: back through the bathroom to the final round */
  PH_RETRY,      /* not saved: a new run from the bathroom */
  PH_LAST = PH_REDO
};
/* keys (event numbers); letters typed with Alpha come back as KEY_LETTER + 0..25 */
enum { KEY_LEFT = 0, KEY_UP = 1, KEY_DOWN = 2, KEY_RIGHT = 3, KEY_OK = 4, KEY_BACK = 5, KEY_BACKSPACE = 17, KEY_EXE = 52,
       KEY_LETTER = 100 };
#define IS_OK(k) ((k) == KEY_OK || (k) == KEY_EXE)

typedef struct {
  uint64_t score;                      /* Double or Nothing: winnings so far */
  uint64_t score_before;               /* ... before the last count (the machine counts up from it) */
  uint32_t batch_ms;                   /* Double or Nothing: time spent on the current three stages */
  uint32_t rng;
  uint8_t mode, stage, load, phase;
  int8_t hp[2], maxhp;                 /* 0 = player, 1 = Dealer */
  uint8_t wire[2];                     /* round 3: defibrillator cut (2 = cut pending) */
  uint8_t nshell, shell[8];            /* remaining shells, shell[0] in the chamber; 1 = live */
  uint8_t shown[8], nshown;            /* the load as it was shown in the compartment */
  uint8_t dknown[8];                   /* shells the Dealer learned from a burner phone */
  int8_t items[2][8];                  /* table slots */
  uint8_t seq[2][8], seqn;             /* the order the items were laid on the table */
  uint8_t don[5][4];                   /* Double or Nothing: live, blank, items, shown shuffled, per load */
  uint8_t doubled, keep_items;
  uint8_t pcuffed, pbreak, dcuffed, dbreak;
  uint8_t sawed;
  uint8_t loaded;                      /* PH_LOAD: 0 new, 1 items being dealt, 2 shells in (the Dealer loads) */
  uint8_t deal_left, deal_spook;       /* PH_LOAD: items the player still takes from the box */
  uint8_t dturn;                       /* the Dealer is in the middle of his turn: keeps what he knows */
  int8_t d_knows, d_known, d_target, d_saw, d_meds;
  uint8_t careful_said, revived, ended;
  uint8_t from_death;                  /* 1 back from a revival, 2 back from heaven */
  char name[7];
  /* statistics for the results */
  uint16_t shots, ejected, doors, cigs, beer_ml, rounds_beat, deaths;
} match_t;

typedef struct {
  uint64_t best;                       /* best Double or Nothing winnings */
  uint8_t don_unlocked, intro_line, read_items, read_items4, read_final, seen_god, loads_told, cut_said, drill_said;
  uint16_t wins;
  char last_name[7];
} profile_t;

#define INTRO_LINES 9

extern match_t G;
extern profile_t P;
extern const char *const ITEM_NAME[IT_COUNT];
extern const char *const ITEM_DESC[IT_COUNT];

uint32_t rnd(uint32_t n);             /* 0..n-1 from the match's generator */
void game_new(int mode);
void game_run(void);                  /* plays from G.phase until the run ends */
void save_match(void);
void save_profile(void);

/* app services (main.c) */
int key_wait(int ms);                 /* next key event, or -1 after ms; Home quits */
void pause_ms(int ms);                /* wait, handling Home and the pause menu */
int pause_skip(int ms);               /* the same, but OK cuts it short: returns 1 if it did */
void app_pause_menu(void);            /* Back during play */
#endif
