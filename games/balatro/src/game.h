/* Game state and rules (no drawing). The rules follow Balatro's own code
 * (game.lua, card.lua, blind.lua, tag.lua, state_events.lua), ported to C.
 * Logic runs instantly and records what happened as events (ev.h) that the
 * interface then plays back with animations. */
#ifndef GAME_H
#define GAME_H
#include <stdint.h>
#include "data.h"

#define MAXCARDS 160
#define MAXJ 20
#define MAXCONS 10
#define MAXHAND 30
#define MAXTAGS 12
#define MAXSHOP 6
#define MAXPACK 5
#define NHANDS 12
#define WIN_ANTE 8

/* suits: Balatro's order in 8BitDeck.png rows is H C D S; ours follows the
 * game's sort order (suit_nominal): Spades > Hearts > Clubs > Diamonds */
enum { S_SPADES, S_HEARTS, S_CLUBS, S_DIAMONDS };
enum { E_NONE, E_BONUS, E_MULT, E_WILD, E_GLASS, E_STEEL, E_STONE, E_GOLD, E_LUCKY };
enum { ED_NONE, ED_FOIL, ED_HOLO, ED_POLY, ED_NEG };
enum { SEAL_NONE, SEAL_GOLD, SEAL_RED, SEAL_BLUE, SEAL_PURPLE };
/* poker hands, in Balatro's order (strongest first) */
enum { H_FLUSH_FIVE, H_FLUSH_HOUSE, H_FIVE_KIND, H_STRAIGHT_FLUSH, H_FOUR_KIND, H_FULL_HOUSE, H_FLUSH, H_STRAIGHT,
       H_THREE_KIND, H_TWO_PAIR, H_PAIR, H_HIGH_CARD };

/* card flags */
enum { CF_DEBUFF = 1, CF_DOWN = 2, CF_PLAYED_ANTE = 4, CF_FORCED = 8, CF_USED = 16, CF_SHATTER = 32, CF_DESTROY = 64,
       CF_LUCKY = 128 };
typedef struct {
  uint8_t rank; /* 2..14 */
  uint8_t suit, enh, ed, seal, flags;
  uint16_t perma; /* extra chips (Hiker) */
} card_t;

/* joker flags */
enum { JF_DEBUFF = 1, JF_DOWN = 2, JF_SLICED = 4 };
typedef struct {
  uint8_t id, ed, flags, c;
  uint16_t uid;
  int16_t extra_value;
  int32_t a, b;
  float x;
} joker_t;

typedef struct {
  uint8_t id, ed;
  uint16_t uid;
  int16_t extra_value;
} cons_t;

/* areas */
enum { A_DECK, A_HAND, A_PLAY, A_DISCARD, A_NONE };

/* shop / pack items */
enum { IT_NONE, IT_JOKER, IT_CONS, IT_CARD, IT_VOUCHER, IT_PACK };
typedef struct {
  uint8_t kind, id, ed, flags; /* flags: 1 = couponed (free) */
  card_t card;                 /* for playing cards */
  int16_t cost;
  uint16_t uid;
  int32_t a, b;                /* joker state preset (To Do List hand) */
  float x;
} sitem_t;

/* packs: kind * 3 + size */
enum { PK_ARCANA, PK_CELESTIAL, PK_STANDARD, PK_BUFFOON, PK_SPECTRAL };

/* phases */
enum { PH_BLIND_SELECT, PH_ROUND, PH_CASHOUT, PH_SHOP, PH_PACK, PH_GAMEOVER, PH_WIN };
enum { BS_SELECT, BS_UPCOMING, BS_CURRENT, BS_DEFEATED, BS_SKIPPED };

typedef struct {
  int16_t level;
  int16_t played, played_round;
  uint8_t visible;
} hand_t;

typedef struct {
  /* run */
  char seed[9];
  uint32_t rng[4];
  int32_t money;
  int16_t ante, round;
  uint8_t phase, won, endless;
  uint8_t pack_return;  /* phase to go back to after a pack */
  /* blinds */
  uint8_t blind_state[3];  /* small, big, boss */
  uint8_t boss;            /* boss blind id for this ante */
  uint8_t skip_tag[2];     /* tags offered for skipping small / big */
  uint8_t boss_rerolled;
  uint8_t bosses_used[N_BLIND];
  uint8_t blind;           /* current blind id (valid in round and cashout) */
  uint8_t blind_on;        /* 0 small 1 big 2 boss: the one being played / next */
  uint8_t blind_disabled, blind_triggered, blind_prepped;
  uint16_t blind_hands;    /* The Eye: bit per hand type used */
  int8_t mouth_hand;       /* The Mouth */
  int8_t water_sub, needle_sub;
  double chips;            /* round score */
  double blind_chips;      /* target */
  /* round */
  int16_t hands_left, discards_left, hands_played_round, discards_used_round;
  int16_t base_hands, base_discards, hand_size_mod, joker_slots, cons_slots;
  int16_t temp_handsize;   /* Juggle Tag */
  int16_t ecto_minus;
  uint8_t sort_suit;
  /* economy */
  int16_t interest_cap, discount, reroll_base, reroll_cost, reroll_inc, free_rerolls, temp_reroll;
  int16_t shop_jokers_max;
  float tarot_rate, planet_rate, spectral_rate, playing_card_rate, edition_rate;
  uint32_t vouchers;       /* bit per voucher */
  uint8_t shop_voucher;    /* voucher offered this ante (0xFF none) */
  uint8_t voucher_tag_extra; /* extra vouchers in the shop from Voucher Tags */
  uint8_t d6, coupon;      /* shop tag effects applied */
  /* global counters */
  int32_t hands_played, skips, unused_discards, tarots_used;
  uint16_t planets_used_mask;     /* Satellite: distinct planets */
  int16_t starting_deck;
  uint8_t last_hand;              /* last poker hand played (for Blue Seal) */
  int8_t last_tarot_planet;       /* The Fool (-1 none) */
  uint8_t gros_michel_extinct;
  uint8_t most_played;
  uint8_t first_shop_buffoon;
  uint8_t idol_rank, idol_suit, mail_rank, ancient_suit, castle_suit;
  uint16_t next_uid;
  hand_t hands[NHANDS];
  /* cards */
  card_t cards[MAXCARDS];
  uint8_t ncards_alloc;
  uint8_t deck[MAXCARDS], hand[MAXHAND], play[8], discard[MAXCARDS];
  uint8_t ndeck, nhand, nplay, ndiscard;
  uint8_t sel[MAXHAND];    /* selected (highlighted) cards in hand, by hand position */
  /* jokers and consumables */
  joker_t jokers[MAXJ];
  uint8_t njokers;
  cons_t cons[MAXCONS];
  uint8_t ncons;
  /* tags */
  uint8_t tags[MAXTAGS];
  int8_t tag_orbital[MAXTAGS];
  uint8_t ntags;
  /* shop */
  sitem_t shop[MAXSHOP];
  uint8_t nshop;
  sitem_t shop_v[3];
  uint8_t nshop_v;
  sitem_t shop_p[2];
  uint8_t nshop_p;
  /* pack being opened */
  uint8_t pack_kind, pack_size, pack_picks, pack_from_tag;
  sitem_t pack[MAXPACK];
  uint8_t npack;
  /* round eval (cash out) lines */
  int16_t cash_blind, cash_hands, cash_interest, cash_total;
  int16_t cash_joker[MAXJ];
  uint8_t cash_joker_id[MAXJ], ncash_joker;
  int16_t cash_tag;
  /* stats */
  double best_hand;
  int32_t cards_played, cards_discarded, cards_bought, rerolls;
  int16_t max_ante_reached;
  uint8_t end_marker;
} run_t;

extern run_t R;

/* ------------------------------------------------------------ rng */
void rng_seed_str(const char *s);
void rng_new_seed(void);
uint32_t rng_u32(void);
float rng_f(void);                 /* [0,1) */
int rng_int(int a, int b);         /* [a,b] */
int prob(float odds);              /* 1 in odds, scaled by Oops! All 6s */
float probs_normal(void);

/* ------------------------------------------------------------ helpers */
int has_joker(int id);             /* count of non-debuffed */
int has_voucher(int v);
int card_chip_nominal(int rank);
int card_is_face(int ci, int from_boss);
int card_get_id(int ci);            /* rank or negative for stone */
int card_is_suit(int ci, int suit, int bypass_debuff, int flush_calc);
int hand_size(void);
int joker_slots(void);
int cons_slots(void);
int joker_sell_value(const joker_t *j);
int cons_sell_value(const cons_t *c);
int item_cost(const sitem_t *it);
double hand_chips(int h);
double hand_mult(int h);
void level_up(int h, int amount, int anim_src);
double blind_amount(int ante);
const char *blind_name(int b);

/* ------------------------------------------------------------ flow */
void run_new(const char *seed);
void run_start_blind_select(void);
void blind_select(void);           /* play the blind on deck */
void blind_skip(void);
void boss_reroll(void);            /* Director's Cut / Retcon */
int play_hand(void);               /* selected cards; returns 0 if nothing selected */
int discard_hand(void);
void sort_hand(int by_suit);
void toggle_select(int pos);       /* returns via R.sel */
int nselected(void);
void cash_out(void);
void shop_enter(void);
void shop_leave(void);
int shop_reroll(void);
int shop_buy(int area, int idx, int use_now); /* area: 0 shop cards, 1 vouchers, 2 packs */
void pack_open(int kind, int size, int from_tag);
int pack_choose(int idx);
void pack_skip(void);
void pack_close(void);
int sell_joker(int idx);
int sell_cons(int idx);
int use_cons(int idx);             /* 0 if it cannot be used now */
int cons_can_use(int idx, int from_pack, int id);
void move_joker(int from, int to);
void move_hand_card(int from, int to);
void continue_endless(void);

/* joker engine (jokers.c) */
void joker_add(int id, int ed, int silent);
void joker_remove(int idx, int sold);
void joker_on_added(joker_t *j);
int joker_new_uid(void);
void jokers_setting_blind(void);
void jokers_first_hand_drawn(void);
void jokers_end_of_round(int *saved);
int jokers_dollar_bonus(int *per, uint8_t *ids);
void jokers_simple_context(int ctx, int arg);
enum { JC_SELL_CARD, JC_REROLL, JC_END_SHOP, JC_SKIP_BLIND, JC_SKIP_BOOSTER, JC_OPEN_BOOSTER, JC_BUY, JC_USE_CONS,
       JC_CARDS_ADDED, JC_CARDS_REMOVED };

/* creation */
int create_joker_id(int rarity_force, int legendary);  /* rarity_force: -1 random */
int create_cons_id(int set);    /* set: 0 tarot, 1 planet, 2 spectral */
int poll_edition(float mod, int no_neg, int guaranteed);
void add_cons(int id, int ed);
int add_card_to_deck(card_t c, int area); /* returns card index */
void destroy_card(int ci);
card_t random_card(void);
int random_enhancement(int no_stone);
void add_tag(int t);
int tag_for_skip(void);

/* scoring (score.c) */
typedef struct {
  int hand;               /* poker hand type */
  uint8_t scoring[8];     /* indices into R.play of scoring cards */
  uint8_t nscoring;
  uint16_t contains;      /* bit per hand type contained */
} handinfo_t;
void evaluate_hand(const uint8_t *cards, int n, handinfo_t *hi);
int preview_hand(void);  /* poker hand of the selected cards, -1 if none */

extern const char *const rank_names[15];
extern const char *const suit_names[4];
extern const char *const suit_names_plural[4];
#endif
