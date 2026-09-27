/* Events: what the rules did, in order, for the interface to animate. */
#ifndef EV_H
#define EV_H
#include <stdint.h>

enum {
  EV_NONE,
  EV_DELAY,        /* a = tenths of a second (scaled by game speed) */
  EV_HUD,          /* chips, mult shown in the hand box */
  EV_HANDNAME,     /* a = hand (-1 clears), b = level shown */
  EV_ROUNDSCORE,   /* v = round score */
  EV_MONEY,        /* i = new amount (eased) */
  EV_HANDS,        /* a = hands left */
  EV_DISCARDS,     /* a = discards left */
  EV_POPUP,        /* target (t, a), text (s or kind+value), colour */
  EV_JUICE,        /* target (t, a): wiggle/pop */
  EV_CARD_MOVE,    /* card a to area b (at the end), c = face down */
  EV_CARD_SET,     /* card a takes attributes from payload */
  EV_CARD_DESTROY, /* card a, b = shatter */
  EV_CARD_SELECT,  /* card a raised (b = 1) or lowered */
  EV_JOKER_ADD,    /* uid, id, ed, position */
  EV_JOKER_REMOVE, /* uid, b = how (0 dissolve, 1 eaten/extinct fall, 2 sold) */
  EV_JOKER_SET,    /* uid, ed, flags */
  EV_JOKER_ORDER,  /* the logic order, in payload uids (up to 8 per event, a = offset) */
  EV_CONS_ADD,     /* uid, id, ed */
  EV_CONS_REMOVE,  /* uid */
  EV_LEVELUP,      /* a = hand, b = new level, v/w chips/mult */
  EV_MESSAGE,      /* big text in the middle: s */
  EV_BLIND_FLASH,  /* the blind chip reacts (boss effect triggered) */
  EV_SORT,         /* hand re-sorted: sync the visual hand order */
  EV_SYNC,         /* everything shown takes the rules' state */
  EV_FLAME,        /* score beats the blind: fire on chips x mult */
  EV_SHAKE,
};

/* popup targets */
enum { TG_PLAY, TG_HAND, TG_JOKER, TG_CONS, TG_DECK, TG_BLIND, TG_CENTER };
/* popup colours */
enum { PC_CHIPS, PC_MULT, PC_XMULT, PC_MONEY, PC_ATTN, PC_GREEN, PC_EDITION, PC_TAROT, PC_PLANET, PC_SPECTRAL,
       PC_GREY };

typedef struct {
  uint8_t type, t, b, c;
  uint16_t a;
  int16_t d;
  char txt[12]; /* popup text; longer ones point to a constant string in p.s */
  union {
    uint32_t w[2]; /* a double, copied in and out (no alignment needed) */
    int32_t i;
    const char *s;
    struct {
      uint8_t rank, suit, enh, ed, seal, flags;
      uint16_t perma;
    } card;
  } p;
} ev_t;
double ev_getd(const ev_t *e);
void ev_setd(ev_t *e, double v);

#define MAXEV 320
extern ev_t evq[MAXEV];
extern int ev_head, ev_tail;
void ev_reset(void);
ev_t *ev_push(int type);
void ev_delay(int tenths);
void ev_popup(int target, int idx, const char *txt, int colour);
void ev_popup_num(int target, int idx, const char *prefix, double v, const char *suffix, int colour);
void ev_juice(int target, int idx);
void ev_hud(double chips, double mult);
void ev_money(void);
void ev_card(int type, int ci, int b, int c);
void ev_card_set(int ci);
void ev_message(const char *s);
void ev_sync(void);
#endif
