#include "ev.h"
#include "game.h"
#include <string.h>

ev_t evq[MAXEV];
int ev_head, ev_tail;

void ev_reset(void) {
  /* events not shown yet are dropped: the interface resyncs */
  ev_head = ev_tail = 0;
}

ev_t *ev_push(int type) {
  if (ev_tail >= MAXEV - 1) {
    /* full: keep the most important ones by overwriting the last slot with a sync */
    evq[MAXEV - 1].type = EV_SYNC;
    ev_tail = MAXEV;
    return 0;
  }
  ev_t *e = &evq[ev_tail++];
  e->type = (uint8_t)type;
  e->t = e->b = e->c = 0;
  e->a = 0;
  e->d = 0;
  e->txt[0] = 0;
  e->p.w[0] = e->p.w[1] = 0;
  return e;
}

void ev_delay(int tenths) {
  ev_t *e = ev_push(EV_DELAY);
  if (e) e->a = (uint16_t)tenths;
}

double ev_getd(const ev_t *e) {
  double v;
  memcpy(&v, e->p.w, sizeof v);
  return v;
}
void ev_setd(ev_t *e, double v) { memcpy(e->p.w, &v, sizeof v); }

static void copy(ev_t *e, const char *s) {
  int i = 0;
  while (s && s[i]) i++;
  if (i > 11 && i - 5 <= 11 && s[i - 5] == ' ' && s[i - 4] == 'M' && s[i - 1] == 't') {
    /* a number and " Mult" (built in a buffer): keep the number, flag the suffix */
    int k;
    for (k = 0; k < i - 5; k++) e->txt[k] = s[k];
    e->txt[k] = 0;
    e->c |= 1;
    return;
  }
  if (i > 11) {
    /* long texts are constant strings: keep the pointer */
    e->txt[0] = 1;
    e->txt[1] = 0;
    e->p.s = s;
    return;
  }
  for (i = 0; s && s[i]; i++) e->txt[i] = s[i];
  e->txt[i] = 0;
}

static uint16_t target_id(int target, int idx) {
  if (target == TG_JOKER) return (idx >= 0 && idx < R.njokers) ? R.jokers[idx].uid : 0;
  if (target == TG_CONS) return (idx >= 0 && idx < R.ncons) ? R.cons[idx].uid : 0;
  return (uint16_t)idx;
}

void ev_popup(int target, int idx, const char *txt, int colour) {
  ev_t *e = ev_push(EV_POPUP);
  if (!e) return;
  e->t = (uint8_t)target;
  e->a = target_id(target, idx);
  e->b = (uint8_t)colour;
  copy(e, txt);
}

void ev_juice(int target, int idx) {
  ev_t *e = ev_push(EV_JUICE);
  if (!e) return;
  e->t = (uint8_t)target;
  e->a = target_id(target, idx);
}

void ev_hud(double chips, double mult) {
  ev_t *e = ev_push(EV_HUD);
  if (!e) return;
  ev_setd(e, chips);
  memcpy(e->txt, &mult, sizeof mult); /* mult travels in the text field */
}

void ev_money(void) {
  ev_t *e = ev_push(EV_MONEY);
  if (e) e->p.i = R.money;
}

void ev_card(int type, int ci, int b, int c) {
  ev_t *e = ev_push(type);
  if (!e) return;
  e->a = (uint16_t)ci;
  e->b = (uint8_t)b;
  e->c = (uint8_t)c;
}

void ev_card_set(int ci) {
  ev_t *e = ev_push(EV_CARD_SET);
  if (!e) return;
  const card_t *c = &R.cards[ci];
  e->a = (uint16_t)ci;
  e->p.card.rank = c->rank, e->p.card.suit = c->suit, e->p.card.enh = c->enh;
  e->p.card.ed = c->ed, e->p.card.seal = c->seal, e->p.card.flags = c->flags;
  e->p.card.perma = c->perma;
}

void ev_message(const char *s) {
  ev_t *e = ev_push(EV_MESSAGE);
  if (e) e->p.s = s;
}

void ev_sync(void) { ev_push(EV_SYNC); }
