/* Poker hands and the round: playing, scoring, discarding, drawing and the
 * end of the round, following state_events.lua (evaluate_play, end_round,
 * evaluate_round) and misc_functions.lua (evaluate_poker_hand). */
#include "ev.h"
#include "game.h"
#include "jokers.h"
#include "util.h"

/* ------------------------------------------------------------ poker hands */
static int nominal_order(int ci) {
  /* get_nominal(): rank chips, then suit, then face; stones last */
  const card_t *c = &R.cards[ci];
  static const uint8_t suitn[4] = {4, 3, 2, 1}; /* S H C D */
  int v = card_chip_nominal(c->rank) * 1000 + suitn[c->suit] * 10 + (c->rank > 10 ? c->rank - 10 : 0);
  if (c->enh == E_STONE) v = card_chip_nominal(c->rank) * 1000 - suitn[c->suit] * 10000;
  return v;
}

/* groups of exactly `num` cards of the same rank, highest rank first */
static int x_same(const uint8_t *cards, int n, int num, uint8_t out[][5], int *outn) {
  int cnt = 0;
  for (int r = 14; r >= 2; r--) {
    int k = 0, idx[8];
    for (int i = 0; i < n; i++)
      if (card_get_id(cards[i]) == r) idx[k++] = i;
    if (k == num) {
      for (int i = 0; i < k; i++) out[cnt][i] = (uint8_t)idx[i];
      outn[cnt++] = k;
    }
  }
  return cnt;
}

void evaluate_hand(const uint8_t *cards, int n, handinfo_t *hi) {
  int ff = has_joker(J_FOUR_FINGERS) ? 1 : 0, need = 5 - ff;
  uint8_t g5[4][5], g4[4][5], g3[4][5], g2[4][5];
  int n5[4], n4[4], n3[4], n2[4];
  int c5 = x_same(cards, n, 5, g5, n5), c4 = x_same(cards, n, 4, g4, n4);
  int c3 = x_same(cards, n, 3, g3, n3), c2 = x_same(cards, n, 2, g2, n2);
  /* flush */
  uint8_t fl[8];
  int nfl = 0;
  if (n <= 5 && n >= need) {
    static const uint8_t order[4] = {S_SPADES, S_HEARTS, S_CLUBS, S_DIAMONDS};
    for (int s = 0; s < 4 && !nfl; s++) {
      int k = 0;
      uint8_t t[8];
      for (int i = 0; i < n; i++)
        if (card_is_suit(cards[i], order[s], 0, 1)) t[k++] = (uint8_t)i;
      if (k >= need) {
        for (int i = 0; i < k; i++) fl[i] = t[i];
        nfl = k;
      }
    }
  }
  /* straight */
  uint8_t st[16];
  int nst = 0;
  if (n <= 5 && n >= need) {
    int can_skip = has_joker(J_SHORTCUT) != 0, len = 0, straight = 0, skipped = 0, k = 0;
    uint8_t t[16];
    for (int j = 1; j <= 14; j++) {
      int r = j == 1 ? 14 : j, any = 0;
      for (int i = 0; i < n; i++)
        if (card_get_id(cards[i]) == r) any = 1;
      if (any) {
        len++;
        skipped = 0;
        for (int i = 0; i < n; i++)
          if (card_get_id(cards[i]) == r && k < 16) t[k++] = (uint8_t)i;
      } else if (can_skip && !skipped && j != 14) {
        skipped = 1;
      } else {
        len = 0;
        skipped = 0;
        if (!straight) k = 0;
        if (straight) break;
      }
      if (len >= need) straight = 1;
    }
    if (straight)
      for (int i = 0; i < k; i++) st[nst++] = t[i];
  }
  uint16_t has = 0;
  int best = -1;
  uint8_t sc[16];
  int nsc = 0;
#define SET(h, arr, cnt)                                   \
  do {                                                     \
    has |= 1 << (h);                                       \
    if (best < 0) {                                        \
      best = (h);                                          \
      for (int q = 0; q < (cnt); q++) sc[nsc++] = (arr)[q]; \
    }                                                      \
  } while (0)
  if (c5 && nfl) SET(H_FLUSH_FIVE, g5[0], n5[0]);
  if (c3 && c2 && nfl) {
    uint8_t t[5];
    int k = 0;
    for (int i = 0; i < n3[0]; i++) t[k++] = g3[0][i];
    for (int i = 0; i < n2[0]; i++) t[k++] = g2[0][i];
    SET(H_FLUSH_HOUSE, t, k);
  }
  if (c5) SET(H_FIVE_KIND, g5[0], n5[0]);
  if (nfl && nst) {
    uint8_t t[16];
    int k = 0;
    for (int i = 0; i < nfl; i++) t[k++] = fl[i];
    for (int i = 0; i < nst; i++) {
      int in = 0;
      for (int q = 0; q < nfl; q++) in |= fl[q] == st[i];
      if (!in && k < 16) t[k++] = st[i];
    }
    SET(H_STRAIGHT_FLUSH, t, k);
  }
  if (c4) SET(H_FOUR_KIND, g4[0], n4[0]);
  if (c3 && c2) {
    uint8_t t[5];
    int k = 0;
    for (int i = 0; i < n3[0]; i++) t[k++] = g3[0][i];
    for (int i = 0; i < n2[0]; i++) t[k++] = g2[0][i];
    SET(H_FULL_HOUSE, t, k);
  }
  if (nfl) SET(H_FLUSH, fl, nfl);
  if (nst) SET(H_STRAIGHT, st, nst);
  if (c3) SET(H_THREE_KIND, g3[0], n3[0]);
  if (c2 == 2 || (c3 == 1 && c2 == 1)) {
    uint8_t t[5];
    int k = 0;
    for (int i = 0; i < n2[0]; i++) t[k++] = g2[0][i];
    if (c2 == 2)
      for (int i = 0; i < n2[1]; i++) t[k++] = g2[1][i];
    else
      for (int i = 0; i < n3[0]; i++) t[k++] = g3[0][i];
    SET(H_TWO_PAIR, t, k);
  }
  if (c2) SET(H_PAIR, g2[0], n2[0]);
  if (n > 0) {
    int hi_i = 0;
    for (int i = 1; i < n; i++)
      if (nominal_order(cards[i]) > nominal_order(cards[hi_i])) hi_i = i;
    uint8_t t[1] = {(uint8_t)hi_i};
    SET(H_HIGH_CARD, t, 1);
  }
#undef SET
  /* larger groups contain the smaller ones */
  if (has >> H_FIVE_KIND & 1) has |= 1 << H_FOUR_KIND;
  if (has >> H_FOUR_KIND & 1) has |= 1 << H_THREE_KIND;
  if (has >> H_THREE_KIND & 1) has |= 1 << H_PAIR;
  hi->hand = best < 0 ? H_HIGH_CARD : best;
  hi->contains = has;
  /* scoring cards in play order, without duplicates */
  hi->nscoring = 0;
  for (int i = 0; i < n; i++) {
    int in = 0;
    for (int q = 0; q < nsc; q++) in |= sc[q] == i;
    if (in) hi->scoring[hi->nscoring++] = (uint8_t)i;
  }
}

int preview_hand(void) {
  uint8_t cards[8];
  int n = 0;
  for (int i = 0; i < R.nhand && n < 5; i++)
    if (R.sel[i]) cards[n++] = R.hand[i];
  if (!n) return -1;
  handinfo_t hi;
  evaluate_hand(cards, n, &hi);
  return hi.hand;
}

/* ------------------------------------------------------------ helpers */
static double chips, mult;

static void hud(void) { ev_hud(chips, mult); }

static void text_num(char *o, const char *pre, double v, const char *suf) {
  o = str_cat(o, pre);
  o = fmt_short(o, v);
  str_cat(o, suf);
}

static const char *j_msg(int ji, eff_t *e) {
  /* the default message of an effect without its own text */
  static char t[20];
  if (e->msg && e->msg[0]) return e->msg;
  joker_t *j = &R.jokers[ji];
  double x;
  joker_x_display(j, &x);
  if (j->id == J_FORTUNE_TELLER) {
    text_num(t, "+", R.tarots_used, " Mult");
    return t;
  }
  text_num(t, "X", j->id == J_CAINO ? j->x : x, " Mult");
  return t;
}

/* shows an effect message (on a joker, or on a card) */
static void show_msg(int target, int idx, eff_t *e) {
  if (!(e->has & EF_MSG)) return;
  const char *m = (target == TG_JOKER) ? j_msg(idx, e) : e->msg;
  if (!m || !m[0]) return;
  ev_popup(target, idx, m, e->col);
}

static void money(int d) {
  R.money += d;
  ev_money();
}

void apply_disable_request(void);

static int run_simple(int kind, cx_t *base) {
  int any = 0;
  for (int i = 0; i < R.njokers; i++) {
    cx_t cx = *base;
    cx.kind = kind;
    cx.bp = -1;
    eff_t e = {0};
    if (jcalc(i, &cx, &e)) {
      any = 1;
      int who = e.card;
      if (e.has & EF_MSG) show_msg(TG_JOKER, e.focus ? e.focus : who, &e);
      if (e.has & EF_DOLLARS) {
        char t[12];
        t[0] = '$';
        fmt_int(t + 1, e.dollars);
        money(e.dollars);
        ev_popup(TG_JOKER, who, t, PC_MONEY);
      }
      if (e.has & EF_LEVELUP) level_up(e.reps, 1, who);
    }
  }
  return any;
}

/* removes the jokers marked for removal during an effect loop */
static void remove_marked(uint8_t *mark) {
  for (int i = R.njokers - 1; i >= 0; i--)
    if (mark[i]) joker_remove(i, 0);
}

/* ------------------------------------------------------------ draw */
static int stay_flipped(int ci) {
  if (R.blind_disabled) return 0;
  switch (R.blind) {
    case BL_WHEEL: return prob(7);
    case BL_HOUSE: return R.hands_played_round == 0 && R.discards_used_round == 0;
    case BL_MARK: return card_is_face(ci, 1);
    case BL_FISH: return R.blind_prepped;
  }
  return 0;
}

static void draw_to_hand(int n) {
  for (int i = 0; i < n && R.ndeck > 0 && R.nhand < MAXHAND; i++) {
    int ci = R.deck[--R.ndeck];
    R.cards[ci].flags &= (uint8_t)~CF_DOWN;
    if (stay_flipped(ci)) R.cards[ci].flags |= CF_DOWN;
    R.sel[R.nhand] = 0;
    R.hand[R.nhand++] = (uint8_t)ci;
    ev_card(EV_CARD_MOVE, ci, A_HAND, (R.cards[ci].flags & CF_DOWN) ? 1 : 0);
  }
  sort_hand(R.sort_suit);
}

void blind_debuff_all(void);

static void drawn_to_hand(void) {
  if (!R.blind_disabled) {
    if (R.blind == BL_FINAL_BELL) {
      int any = 0;
      for (int i = 0; i < R.nhand; i++) any |= (R.cards[R.hand[i]].flags & CF_FORCED) != 0;
      if (!any && R.nhand) {
        for (int i = 0; i < R.nhand; i++) R.sel[i] = 0;
        int p = rng_int(0, R.nhand - 1);
        R.cards[R.hand[p]].flags |= CF_FORCED;
        R.sel[p] = 1;
        ev_card(EV_CARD_SELECT, R.hand[p], 1, 0);
      }
    }
    if (R.blind == BL_FINAL_HEART && R.blind_prepped && R.njokers) {
      int pick[MAXJ], n = 0;
      for (int i = 0; i < R.njokers; i++) {
        if (!(R.jokers[i].flags & JF_DEBUFF) || R.njokers < 2) pick[n++] = i;
        R.jokers[i].flags &= (uint8_t)~JF_DEBUFF;
      }
      if (n) R.jokers[pick[rng_int(0, n - 1)]].flags |= JF_DEBUFF;
      for (int i = 0; i < R.njokers; i++) {
        ev_t *v = ev_push(EV_JOKER_SET);
        if (v) v->a = R.jokers[i].uid, v->b = R.jokers[i].ed, v->c = R.jokers[i].flags;
      }
    }
  }
  R.blind_prepped = 0;
}

void end_round(void);
/* G.STATE = DRAW_TO_HAND */
void draw_phase(void) {
  for (int i = 0; i < R.ntags; i++)
    if (R.tags[i] == TAG_JUGGLE) {
      R.temp_handsize += 3;
      ev_popup(TG_CENTER, 0, "+3 Hand Size", PC_ATTN);
      for (int k = i; k < R.ntags - 1; k++) R.tags[k] = R.tags[k + 1], R.tag_orbital[k] = R.tag_orbital[k + 1];
      R.ntags--;
      i--;
    }
  int space = hand_size() - R.nhand;
  if (R.blind == BL_SERPENT && !R.blind_disabled && (R.hands_played_round > 0 || R.discards_used_round > 0)) space = 3;
  if (space > R.ndeck) space = R.ndeck;
  if (space > 0) draw_to_hand(space);
  if (R.hands_played_round == 0 && R.discards_used_round == 0) {
    cx_t cx = {0};
    run_simple(CX_FIRST_HAND_DRAWN, &cx);
  }
  drawn_to_hand();
  ev_sync();
  /* no cards left at all: the round ends (lost unless the score is reached) */
  if (R.nhand < 1 && R.ndeck < 1 && R.phase == PH_ROUND) end_round();
}

/* ------------------------------------------------------------ playing */
static int card_has_effect_held(int ci) {
  /* eval_card for a held card: steel (h_x_mult) */
  return !(R.cards[ci].flags & CF_DEBUFF) && R.cards[ci].enh == E_STEEL;
}

static void score_played_card(int ci, handinfo_t *hi, cx_t *base) {
  card_t *c = &R.cards[ci];
  if (c->flags & CF_DEBUFF) {
    R.blind_triggered = 1;
    ev_popup(TG_PLAY, ci, "Debuffed", PC_MULT);
    return;
  }
  /* repetitions: red seal, then jokers */
  int nreps = 1;
  struct {
    int src_joker, src_card;
    const char *msg;
  } reps[24];
  reps[0].src_joker = -1;
  if (c->seal == SEAL_RED) {
    reps[nreps].src_joker = -1, reps[nreps].src_card = ci, reps[nreps].msg = "Again!";
    nreps++;
  }
  for (int j = 0; j < R.njokers; j++) {
    cx_t cx = *base;
    cx.kind = CX_REPETITION, cx.area = A_PLAY, cx.other = ci, cx.bp = -1;
    eff_t e = {0};
    if (jcalc(j, &cx, &e) && (e.has & EF_REPS))
      for (int h = 0; h < e.reps && nreps < 24; h++) {
        reps[nreps].src_joker = e.card, reps[nreps].src_card = -1, reps[nreps].msg = e.msg;
        nreps++;
      }
  }
  for (int r = 0; r < nreps; r++) {
    if (r > 0) {
      if (reps[r].src_joker >= 0) ev_popup(TG_JOKER, reps[r].src_joker, "Again!", PC_ATTN);
      else ev_popup(TG_PLAY, reps[r].src_card, "Again!", PC_ATTN);
    }
    c->flags &= (uint8_t)~CF_LUCKY;
    /* the card's own effects: chips, mult, x_mult, dollars, edition */
    int chip_bonus = c->enh == E_STONE ? 50 + c->perma : card_chip_nominal(c->rank) + (c->enh == E_BONUS ? 30 : 0) + c->perma;
    char t[20];
    if (chip_bonus > 0) {
      chips += chip_bonus;
      text_num(t, "+", chip_bonus, "");
      ev_juice(TG_PLAY, ci);
      ev_popup(TG_PLAY, ci, t, PC_CHIPS);
      hud();
    }
    int m = 0;
    if (c->enh == E_MULT) m = 4;
    if (c->enh == E_LUCKY && prob(5)) {
      m = 20;
      c->flags |= CF_LUCKY;
    }
    if (m) {
      mult += m;
      text_num(t, "+", m, " Mult");
      ev_popup(TG_PLAY, ci, t, PC_MULT);
      hud();
    }
    int pd = 0;
    if (c->seal == SEAL_GOLD) pd += 3;
    if (c->enh == E_LUCKY && prob(15)) {
      pd += 20;
      c->flags |= CF_LUCKY;
    }
    if (pd) {
      money(pd);
      t[0] = '$';
      fmt_int(t + 1, pd);
      ev_popup(TG_PLAY, ci, t, PC_MONEY);
    }
    if (c->enh == E_GLASS) {
      mult *= 2;
      ev_popup(TG_PLAY, ci, "X2 Mult", PC_XMULT);
      hud();
    }
    if (c->ed) {
      if (c->ed == ED_FOIL) chips += 50, ev_popup(TG_PLAY, ci, "+50", PC_EDITION);
      if (c->ed == ED_HOLO) mult += 10, ev_popup(TG_PLAY, ci, "+10 Mult", PC_EDITION);
      if (c->ed == ED_POLY) mult *= 1.5, ev_popup(TG_PLAY, ci, "X1.5 Mult", PC_EDITION);
      hud();
    }
    /* jokers on this card */
    for (int j = 0; j < R.njokers; j++) {
      cx_t cx = *base;
      cx.kind = CX_INDIVIDUAL, cx.area = A_PLAY, cx.other = ci, cx.bp = -1;
      eff_t e = {0};
      if (!jcalc(j, &cx, &e)) continue;
      int who = e.card;
      if (e.has & EF_CHIPS) {
        chips += e.chips;
        ev_juice(TG_JOKER, who);
        text_num(t, "+", e.chips, "");
        ev_popup(TG_PLAY, ci, t, PC_CHIPS);
        hud();
      }
      if (e.has & EF_MULT) {
        mult += e.mult;
        ev_juice(TG_JOKER, who);
        text_num(t, "+", e.mult, " Mult");
        ev_popup(TG_PLAY, ci, t, PC_MULT);
        hud();
      }
      if (e.has & EF_DOLLARS) {
        money(e.dollars);
        ev_juice(TG_JOKER, who);
        t[0] = '$';
        fmt_int(t + 1, e.dollars);
        ev_popup(TG_PLAY, ci, t, PC_MONEY);
      }
      if (e.has & EF_MSG) {
        ev_juice(TG_JOKER, who);
        if (e.focus) ev_popup(TG_JOKER, e.focus, e.msg, e.col);
        else ev_popup(TG_PLAY, ci, e.msg, e.col);
      }
      if (e.has & EF_XMULT) {
        mult *= e.x;
        ev_juice(TG_JOKER, who);
        text_num(t, "X", e.x, " Mult");
        ev_popup(TG_PLAY, ci, t, PC_XMULT);
        hud();
      }
    }
  }
}

static void score_held_card(int ci, cx_t *base) {
  card_t *c = &R.cards[ci];
  int nreps = 1, src[24];
  char t[20];
  for (int r = 0; r < nreps; r++) {
    if (r > 0) {
      if (src[r] >= 0) ev_popup(TG_JOKER, src[r], "Again!", PC_ATTN);
      else ev_popup(TG_HAND, ci, "Again!", PC_ATTN);
    }
    int own = card_has_effect_held(ci), any = own;
    eff_t effs[MAXJ];
    int ne = 0;
    for (int j = 0; j < R.njokers; j++) {
      cx_t cx = *base;
      cx.kind = CX_INDIVIDUAL, cx.area = A_HAND, cx.other = ci, cx.bp = -1;
      eff_t e = {0};
      if (jcalc(j, &cx, &e)) effs[ne++] = e, any = 1;
    }
    if (r == 0) {
      if (c->seal == SEAL_RED && !(c->flags & CF_DEBUFF) && any && nreps < 24) src[nreps++] = -1;
      for (int j = 0; j < R.njokers; j++) {
        cx_t cx = *base;
        cx.kind = CX_REPETITION, cx.area = A_HAND, cx.other = ci, cx.bp = -1, cx.card_effects = any;
        eff_t e = {0};
        if (jcalc(j, &cx, &e) && (e.has & EF_REPS))
          for (int h = 0; h < e.reps && nreps < 24; h++) src[nreps++] = e.card;
      }
    }
    if (own) {
      mult *= 1.5;
      ev_juice(TG_HAND, ci);
      ev_popup(TG_HAND, ci, "X1.5 Mult", PC_XMULT);
      hud();
    }
    for (int k = 0; k < ne; k++) {
      eff_t *e = &effs[k];
      ev_juice(TG_JOKER, e->card);
      if (e->has & EF_DOLLARS) {
        money(e->dollars);
        t[0] = '$';
        fmt_int(t + 1, e->dollars);
        ev_popup(TG_HAND, ci, t, PC_MONEY);
      }
      if (e->has & EF_HMULT) {
        mult += e->mult;
        text_num(t, "+", e->mult, " Mult");
        ev_popup(TG_HAND, ci, t, PC_MULT);
        hud();
      }
      if (e->has & EF_XMULT) {
        mult *= e->x;
        text_num(t, "X", e->x, " Mult");
        ev_popup(TG_HAND, ci, t, PC_XMULT);
        hud();
      }
      if ((e->has & EF_MSG) && !(e->has & (EF_DOLLARS | EF_HMULT | EF_XMULT))) ev_popup(TG_HAND, ci, e->msg, e->col);
    }
  }
}

/* The Hook: two random cards from the hand are discarded */
static void discard_cards(const uint8_t *cards, int n, int hook);

static int debuff_hand(handinfo_t *hi, int check) {
  if (R.blind_disabled) return 0;
  int h = hi->hand, trig = 0;
  switch (R.blind) {
    case BL_PSYCHIC: trig = R.nplay < 5; break;
    case BL_EYE:
      trig = (R.blind_hands >> h) & 1;
      if (!check) R.blind_hands |= (uint16_t)(1 << h);
      break;
    case BL_MOUTH:
      trig = R.mouth_hand >= 0 && R.mouth_hand != h;
      if (!check) R.mouth_hand = (int8_t)h;
      break;
    case BL_ARM:
      if (R.hands[h].level > 1) {
        R.blind_triggered = 1;
        if (!check) {
          ev_push(EV_BLIND_FLASH);
          level_up(h, -1, -1);
        }
      }
      return 0;
    case BL_OX:
      if (h == R.most_played) {
        R.blind_triggered = 1;
        if (!check) {
          ev_push(EV_BLIND_FLASH);
          R.money = 0;
          ev_money();
        }
      }
      return 0;
    default: return 0;
  }
  R.blind_triggered = trig;
  return trig;
}

int blind_debuffs_hand(void) {
  uint8_t cards[8];
  int n = 0;
  for (int i = 0; i < R.nhand && n < 5; i++)
    if (R.sel[i]) cards[n++] = R.hand[i];
  if (!n) return 0;
  handinfo_t hi;
  evaluate_hand(cards, n, &hi);
  int save_t = R.blind_triggered, np = R.nplay;
  R.nplay = (uint8_t)n;
  int r = (R.blind == BL_EYE || R.blind == BL_MOUTH || R.blind == BL_PSYCHIC) ? debuff_hand(&hi, 1) : 0;
  R.nplay = (uint8_t)np;
  R.blind_triggered = (uint8_t)save_t;
  return r;
}

void end_round(void);

int play_hand(void) {
  int n = nselected();
  if (n <= 0 || n > 5) return 0;
  ev_reset();
  R.blind_triggered = 0;
  /* move the selected cards to the play area, in hand order */
  R.nplay = 0;
  int w = 0;
  for (int i = 0; i < R.nhand; i++) {
    int ci = R.hand[i];
    if (R.sel[i]) {
      R.play[R.nplay++] = (uint8_t)ci;
      R.cards[ci].flags |= CF_PLAYED_ANTE;
      R.cards[ci].flags &= (uint8_t)~(CF_DOWN | CF_FORCED);
      ev_card(EV_CARD_MOVE, ci, A_PLAY, 0);
    } else {
      R.hand[w] = (uint8_t)ci;
      R.sel[w++] = 0;
    }
  }
  R.nhand = (uint8_t)w;
  for (int i = 0; i < MAXHAND; i++) R.sel[i] = 0;
  R.cards_played += R.nplay;
  R.hands_left--;
  ev_t *v = ev_push(EV_HANDS);
  if (v) v->a = (uint16_t)R.hands_left;
  ev_delay(3);
  /* the blind reacts to the play (Blind:press_play) */
  if (!R.blind_disabled) {
    if (R.blind == BL_HOOK && R.nhand) {
      uint8_t pool[MAXHAND], pick[2];
      int np = 0, k = 0;
      for (int i = 0; i < R.nhand; i++) pool[np++] = R.hand[i];
      for (int i = 0; i < 2 && np; i++) {
        int r = rng_int(0, np - 1);
        pick[k++] = pool[r];
        pool[r] = pool[--np];
      }
      ev_push(EV_BLIND_FLASH);
      discard_cards(pick, k, 1);
      R.blind_triggered = 1;
    }
    if (R.blind == BL_FINAL_HEART && R.njokers) R.blind_triggered = 1, R.blind_prepped = 1;
    if (R.blind == BL_FISH) R.blind_prepped = 1;
    if (R.blind == BL_TOOTH) {
      ev_push(EV_BLIND_FLASH);
      for (int i = 0; i < R.nplay; i++) {
        R.money -= 1;
        ev_money();
        ev_popup(TG_PLAY, R.play[i], "-$1", PC_MULT);
      }
      R.blind_triggered = 1;
    }
  }
  /* ---- evaluate_play */
  handinfo_t hi;
  evaluate_hand(R.play, R.nplay, &hi);
  int h = hi.hand;
  R.hands[h].played++;
  R.hands[h].played_round++;
  R.last_hand = (uint8_t)h;
  R.hands[h].visible = 1;
  /* every played card scores with Splash; stone cards always do */
  if (has_joker(J_SPLASH)) {
    hi.nscoring = 0;
    for (int i = 0; i < R.nplay; i++) hi.scoring[hi.nscoring++] = (uint8_t)i;
  } else {
    for (int i = 0; i < R.nplay; i++)
      if (R.cards[R.play[i]].enh == E_STONE) {
        int in = 0;
        for (int q = 0; q < hi.nscoring; q++) in |= hi.scoring[q] == i;
        if (!in) hi.scoring[hi.nscoring++] = (uint8_t)i;
      }
    /* play order */
    for (int a = 0; a < hi.nscoring; a++)
      for (int b = a + 1; b < hi.nscoring; b++)
        if (hi.scoring[b] < hi.scoring[a]) {
          uint8_t t = hi.scoring[a];
          hi.scoring[a] = hi.scoring[b], hi.scoring[b] = t;
        }
  }
  for (int i = 0; i < hi.nscoring; i++) {
    ev_t *s = ev_push(EV_CARD_SELECT); /* raise the scoring cards */
    if (s) s->a = R.play[hi.scoring[i]], s->b = 2;
  }
  v = ev_push(EV_HANDNAME);
  if (v) v->a = (uint16_t)h, v->d = R.hands[h].level;
  chips = hand_chips(h), mult = hand_mult(h);
  hud();
  ev_delay(3);
  uint8_t full[8];
  for (int i = 0; i < R.nplay; i++) full[i] = R.play[i];
  cx_t base = {0};
  base.hi = &hi, base.full = full, base.nfull = R.nplay, base.bp = -1, base.other_joker = -1;
  if (!debuff_hand(&hi, 0)) {
    /* before */
    run_simple(CX_BEFORE, &base);
    chips = hand_chips(h), mult = hand_mult(h);
    if (!R.blind_disabled && R.blind == BL_FLINT) {
      R.blind_triggered = 1;
      double m2 = (double)(long long)(mult * 0.5 + 0.5), c2 = (double)(long long)(chips * 0.5 + 0.5);
      mult = m2 < 1 ? 1 : m2;
      chips = c2 < 0 ? 0 : c2;
      ev_push(EV_BLIND_FLASH);
    }
    hud();
    /* scoring cards, left to right */
    for (int i = 0; i < hi.nscoring; i++) score_played_card(R.play[hi.scoring[i]], &hi, &base);
    ev_delay(3);
    /* cards held in hand */
    for (int i = 0; i < R.nhand; i++) score_held_card(R.hand[i], &base);
    /* jokers (and consumables), left to right */
    char t[20];
    int nj = R.njokers;
    for (int i = 0; i < nj + R.ncons; i++) {
      int is_j = i < nj;
      int ed = is_j ? R.jokers[i].ed : R.cons[i - nj].ed;
      int deb = is_j && (R.jokers[i].flags & JF_DEBUFF);
      int tg = is_j ? TG_JOKER : TG_CONS, idx = is_j ? i : i - nj;
      if (!deb && ed == ED_FOIL) {
        chips += 50;
        ev_juice(tg, idx);
        ev_popup(tg, idx, "+50", PC_EDITION);
        hud();
      }
      if (!deb && ed == ED_HOLO) {
        mult += 10;
        ev_juice(tg, idx);
        ev_popup(tg, idx, "+10 Mult", PC_EDITION);
        hud();
      }
      eff_t e = {0};
      int got = 0;
      if (is_j) {
        cx_t cx = base;
        cx.kind = CX_MAIN;
        got = jcalc(i, &cx, &e);
      } else if (R.cons[idx].id >= C_MERCURY && R.cons[idx].id <= C_ERIS && has_voucher(V_OBSERVATORY)) {
        static const uint8_t ph[12] = {H_PAIR, H_THREE_KIND, H_FULL_HOUSE, H_FOUR_KIND, H_FLUSH, H_STRAIGHT,
                                       H_TWO_PAIR, H_STRAIGHT_FLUSH, H_HIGH_CARD, H_FIVE_KIND, H_FLUSH_HOUSE,
                                       H_FLUSH_FIVE};
        if (ph[R.cons[idx].id - C_MERCURY] == h) {
          e.has = EF_XMULT, e.x = 1.5, e.card = idx;
          got = 1;
        }
      }
      if (got) {
        int who = is_j ? e.card : idx;
        if (e.has & EF_MULT) {
          mult += e.mult;
          text_num(t, "+", e.mult, " Mult");
          ev_juice(tg, who);
          ev_popup(tg, who, t, PC_MULT);
        }
        if (e.has & EF_CHIPS) {
          chips += e.chips;
          text_num(t, "+", e.chips, "");
          ev_juice(tg, who);
          ev_popup(tg, who, t, PC_CHIPS);
        }
        if (e.has & EF_XMULT) {
          mult *= e.x;
          text_num(t, "X", e.x, " Mult");
          ev_juice(tg, who);
          ev_popup(tg, who, t, PC_XMULT);
        }
        if (e.has & EF_DOLLARS) {
          money(e.dollars);
          t[0] = '$';
          fmt_int(t + 1, e.dollars);
          ev_juice(tg, who);
          ev_popup(tg, who, t, PC_MONEY);
        }
        if ((e.has & EF_MSG) && !(e.has & (EF_MULT | EF_CHIPS | EF_XMULT | EF_DOLLARS))) {
          ev_juice(tg, who);
          show_msg(tg, who, &e);
        }
        hud();
      }
      /* jokers reacting to this joker (Baseball Card) */
      for (int k = 0; k < R.njokers; k++) {
        if (!is_j) break;
        cx_t cx = base;
        cx.kind = CX_OTHER_JOKER, cx.other_joker = i;
        eff_t e2 = {0};
        if (jcalc(k, &cx, &e2) && (e2.has & EF_XMULT)) {
          mult *= e2.x;
          text_num(t, "X", e2.x, " Mult");
          ev_juice(TG_JOKER, i);
          ev_popup(TG_JOKER, e2.card, t, PC_XMULT);
          hud();
        }
      }
      if (!deb && ed == ED_POLY) {
        mult *= 1.5;
        ev_juice(tg, idx);
        ev_popup(tg, idx, "X1.5 Mult", PC_EDITION);
        hud();
      }
    }
    /* destroyed cards: Sixth Sense, glass */
    int nfaces = 0, nglass = 0, ndest = 0;
    for (int i = 0; i < hi.nscoring; i++) {
      int ci = R.play[hi.scoring[i]], d = 0;
      for (int j = 0; j < R.njokers && !d; j++) {
        cx_t cx = base;
        cx.kind = CX_DESTROYING_CARD, cx.other = ci;
        eff_t e = {0};
        if (jcalc(j, &cx, &e) && (e.has & EF_REMOVE)) {
          d = 1;
          if (e.has & EF_MSG) ev_popup(TG_JOKER, e.card, e.msg, e.col);
        }
      }
      if (R.cards[ci].enh == E_GLASS && !(R.cards[ci].flags & CF_DEBUFF) && prob(4)) d = 2;
      if (d) {
        R.cards[ci].flags |= d == 2 ? CF_SHATTER : CF_DESTROY;
        ndest++;
        if (card_is_face(ci, 0)) nfaces++;
        if (d == 2) nglass++;
      }
    }
    if (ndest) {
      cx_t cx = base;
      cx.nfaces_removed = nfaces, cx.nglass_removed = nglass;
      run_simple(CX_REMOVE_CARDS, &cx);
    }
  } else {
    chips = 0, mult = 0;
    ev_push(EV_BLIND_FLASH);
    ev_message("Not Allowed!");
    hud();
    run_simple(CX_DEBUFFED_HAND, &base);
  }
  double score = dfloor(chips * mult);
  if (score > R.best_hand) R.best_hand = score;
  if (score >= R.blind_chips - R.chips && score > 0) ev_push(EV_FLAME);
  R.chips += score;
  ev_delay(8);
  v = ev_push(EV_ROUNDSCORE);
  if (v) ev_setd(v, R.chips);
  ev_delay(3);
  /* after */
  {
    uint8_t mark[MAXJ] = {0};
    for (int i = 0; i < R.njokers; i++) {
      cx_t cx = base;
      cx.kind = CX_AFTER;
      eff_t e = {0};
      if (jcalc(i, &cx, &e)) {
        show_msg(TG_JOKER, e.card, &e);
        if (e.has & EF_REMOVE) mark[i] = 1;
      }
    }
    remove_marked(mark);
  }
  /* played cards go to the discard pile; destroyed ones are gone */
  uint8_t played[8];
  int np = R.nplay;
  for (int i = 0; i < np; i++) played[i] = R.play[i];
  for (int i = 0; i < np; i++) {
    int ci = played[i];
    if (R.cards[ci].flags & (CF_SHATTER | CF_DESTROY)) {
      ev_card(EV_CARD_DESTROY, ci, (R.cards[ci].flags & CF_SHATTER) ? 1 : 0, 0);
      destroy_card(ci);
    } else {
      R.discard[R.ndiscard++] = (uint8_t)ci;
      ev_card(EV_CARD_MOVE, ci, A_DISCARD, 0);
    }
  }
  R.nplay = 0;
  R.hands_played++;
  R.hands_played_round++;
  v = ev_push(EV_HANDNAME);
  if (v) v->a = 0xFFFF;
  if (R.chips >= R.blind_chips || R.hands_left < 1) end_round();
  else draw_phase();
  return 1;
}

/* ------------------------------------------------------------ discarding */
static void discard_cards(const uint8_t *cards, int n, int hook) {
  cx_t base = {0};
  base.full = cards, base.nfull = n, base.bp = -1, base.hook = hook, base.other_joker = -1;
  run_simple(CX_PRE_DISCARD, &base);
  int nd = 0, nfaces = 0, nglass = 0;
  uint8_t mark[MAXJ] = {0};
  for (int i = 0; i < n; i++) {
    int ci = cards[i], removed = 0;
    /* purple seal */
    if (R.cards[ci].seal == SEAL_PURPLE && !(R.cards[ci].flags & CF_DEBUFF) && R.ncons < cons_slots()) {
      add_cons(create_cons_id(0), 0);
      ev_popup(TG_HAND, ci, "+1 Tarot", PC_TAROT);
    }
    for (int j = 0; j < R.njokers; j++) {
      cx_t cx = base;
      cx.kind = CX_DISCARD, cx.other = ci;
      eff_t e = {0};
      if (!jcalc(j, &cx, &e)) continue;
      if (e.has & EF_REMOVE) {
        if (R.jokers[j].id == J_RAMEN) mark[j] = 1;
        else removed = 1;
      }
      if (e.has & EF_DOLLARS) {
        char t[12];
        t[0] = '$';
        fmt_int(t + 1, e.dollars);
        money(e.dollars);
        ev_popup(TG_JOKER, e.card, t, PC_MONEY);
      } else
        show_msg(TG_JOKER, e.card, &e);
    }
    /* take it out of the hand */
    for (int k = 0; k < R.nhand; k++)
      if (R.hand[k] == ci) {
        for (int q = k; q < R.nhand - 1; q++) R.hand[q] = R.hand[q + 1], R.sel[q] = R.sel[q + 1];
        R.nhand--;
        break;
      }
    R.cards[ci].flags &= (uint8_t)~(CF_FORCED | CF_DOWN);
    if (removed) {
      if (card_is_face(ci, 0)) nfaces++;
      if (R.cards[ci].enh == E_GLASS) nglass++, R.cards[ci].flags |= CF_SHATTER;
      ev_card(EV_CARD_DESTROY, ci, R.cards[ci].enh == E_GLASS, 0);
      destroy_card(ci);
      nd++;
    } else {
      R.discard[R.ndiscard++] = (uint8_t)ci;
      ev_card(EV_CARD_MOVE, ci, A_DISCARD, 0);
    }
  }
  remove_marked(mark);
  if (nd) {
    cx_t cx = base;
    cx.nfaces_removed = nfaces, cx.nglass_removed = nglass;
    run_simple(CX_REMOVE_CARDS, &cx);
  }
  R.cards_discarded += n;
}

int discard_hand(void) {
  int n = nselected();
  if (n <= 0 || R.discards_left <= 0) return 0;
  ev_reset();
  uint8_t cards[8];
  int k = 0;
  for (int i = 0; i < R.nhand && k < 5; i++)
    if (R.sel[i]) cards[k++] = R.hand[i];
  for (int i = 0; i < MAXHAND; i++) R.sel[i] = 0;
  discard_cards(cards, k, 0);
  R.discards_left--;
  R.discards_used_round++;
  ev_t *v = ev_push(EV_DISCARDS);
  if (v) v->a = (uint16_t)R.discards_left;
  draw_phase();
  return 1;
}

/* ------------------------------------------------------------ end of round */
void blind_defeat(void);
void game_over(void);

void end_round(void) {
  int game_over_ = R.chips < R.blind_chips;
  int saved = 0;
  /* jokers at the end of the round */
  {
    uint8_t mark[MAXJ] = {0};
    for (int i = 0; i < R.njokers; i++) {
      cx_t cx = {0};
      cx.kind = CX_END_ROUND, cx.bp = -1, cx.game_over = game_over_;
      eff_t e = {0};
      if (jcalc(i, &cx, &e)) {
        if (e.has & EF_SAVED) saved = 1;
        show_msg(TG_JOKER, e.card, &e);
        if (e.has & EF_REMOVE) mark[i] = 1;
      }
    }
    remove_marked(mark);
  }
  if (saved) {
    game_over_ = 0;
    ev_message("Saved by Mr. Bones");
  }
  if (game_over_) {
    ev_delay(5);
    game_over();
    return;
  }
  R.unused_discards += R.discards_left;
  if (R.blind >= BL_OX) {
    int best = H_HIGH_CARD, played = -1;
    for (int k = 0; k < NHANDS; k++)
      if (R.hands[k].played > played) played = R.hands[k].played, best = k;
    R.most_played = (uint8_t)best;
  }
  /* held cards: gold cards pay, blue seals make planets */
  for (int i = 0; i < R.nhand; i++) {
    int ci = R.hand[i];
    card_t *c = &R.cards[ci];
    int nreps = 1, src[16];
    for (int r = 0; r < nreps; r++) {
      if (r > 0) {
        if (src[r] >= 0) ev_popup(TG_JOKER, src[r], "Again!", PC_ATTN);
        else ev_popup(TG_HAND, ci, "Again!", PC_ATTN);
      }
      int any = 0;
      if (!(c->flags & CF_DEBUFF)) {
        if (c->enh == E_GOLD) {
          money(3);
          ev_popup(TG_HAND, ci, "$3", PC_MONEY);
          any = 1;
        }
        if (c->seal == SEAL_BLUE && R.ncons < cons_slots()) {
          static const uint8_t planet_of[12] = {C_ERIS, C_CERES, C_PLANET_X, C_NEPTUNE, C_MARS, C_EARTH, C_JUPITER,
                                                C_SATURN, C_VENUS, C_URANUS, C_MERCURY, C_PLUTO};
          add_cons(planet_of[R.last_hand], 0);
          ev_popup(TG_HAND, ci, "+1 Planet", PC_PLANET);
          any = 1;
        }
      }
      if (r == 0) {
        if (c->seal == SEAL_RED && !(c->flags & CF_DEBUFF) && any) src[nreps++] = -1;
        for (int j = 0; j < R.njokers; j++) {
          cx_t cx = {0};
          cx.kind = CX_REP_END_ROUND, cx.area = A_HAND, cx.other = ci, cx.bp = -1, cx.card_effects = any;
          eff_t e = {0};
          if (jcalc(j, &cx, &e) && (e.has & EF_REPS))
            for (int k = 0; k < e.reps && nreps < 16; k++) src[nreps++] = e.card;
        }
      }
    }
  }
  ev_delay(3);
  /* hand to the discard pile, then everything back in the deck */
  for (int i = 0; i < R.nhand; i++) {
    R.discard[R.ndiscard++] = R.hand[i];
    R.cards[R.hand[i]].flags &= (uint8_t)~(CF_DOWN | CF_FORCED);
    ev_card(EV_CARD_MOVE, R.hand[i], A_DISCARD, 0);
  }
  R.nhand = 0;
  blind_defeat();
}
