/* The 150 Jokers: Card:calculate_joker from Balatro's card.lua, context by
 * context, in the same order, with the same checks. */
#include "jokers.h"
#include "ev.h"

#define J (&R.jokers[ji])
#define ID (R.jokers[ji].id)
#define NOT_BP (!cx->blueprint)
#define HAS(h) (cx->hi && (cx->hi->contains >> (h) & 1))

static const char K_UPGRADE[] = "Upgrade!", K_AGAIN[] = "Again!", K_RESET[] = "Reset", K_SAVED[] = "Saved!",
                  K_EATEN[] = "Eaten!", K_EXTINCT[] = "Extinct!", K_SAFE[] = "Safe!", K_MELTED[] = "Melted!",
                  K_DRANK[] = "Drank!", K_PLUS_TAROT[] = "+1 Tarot", K_PLUS_SPECTRAL[] = "+1 Spectral",
                  K_COPIED[] = "Copied!", K_GOLD[] = "Gold", K_LEVELUP[] = "Level Up!", K_VALUP[] = "Value Up!",
                  K_DEBUFFED[] = "Debuffed", K_ACTIVE[] = "Active!";

void joker_init(joker_t *j) {
  j->a = j->b = 0;
  j->c = 0;
  j->x = 1;
  j->extra_value = 0;
  switch (j->id) {
    case J_ICE_CREAM: j->a = 100; break;
    case J_ROCKET: j->a = 1; break;
    case J_TURTLE_BEAN: j->a = 5; break;
    case J_POPCORN: j->a = 20; break;
    case J_RAMEN: j->x = 2; break;
    case J_SELZER: j->a = 10; break;
    case J_YORICK: j->b = 23; break;
    case J_LOYALTY_CARD: j->b = R.hands_played; j->c = 5; break;
    case J_TODO_LIST: {
      /* a random visible hand, not the old one */
      int n = 0, pick[NHANDS];
      for (int h = 0; h < NHANDS; h++)
        if (R.hands[h].visible) pick[n++] = h;
      j->c = (uint8_t)pick[rng_int(0, n - 1)];
      break;
    }
    case J_DUO: j->x = 2; break;
    case J_TRIO: j->x = 3; break;
    case J_FAMILY: j->x = 4; break;
    case J_ORDER: j->x = 3; break;
    case J_TRIBE: j->x = 2; break;
  }
}

/* the poker hand a "type" joker needs, or -1 */
static int jtype(int id) {
  switch (id) {
    case J_JOLLY: case J_SLY: case J_DUO: return H_PAIR;
    case J_ZANY: case J_WILY: case J_TRIO: return H_THREE_KIND;
    case J_MAD: case J_CLEVER: return H_TWO_PAIR;
    case J_CRAZY: case J_DEVIOUS: case J_ORDER: return H_STRAIGHT;
    case J_DROLL: case J_CRAFTY: case J_TRIBE: return H_FLUSH;
    case J_FAMILY: return H_FOUR_KIND;
  }
  return -1;
}

static int cons_room(void) { return R.ncons < cons_slots(); }

static void give_cons(int set) {
  add_cons(create_cons_id(set), 0);
}

static int count_enh(int enh) {
  int n = 0;
  for (int i = 0; i < R.ncards_alloc; i++)
    if ((R.cards[i].flags & CF_USED) && R.cards[i].enh == enh) n++;
  return n;
}

static int count_cards(void) {
  int n = 0;
  for (int i = 0; i < R.ncards_alloc; i++) n += (R.cards[i].flags & CF_USED) != 0;
  return n;
}

/* Joker Stencil's value, as in Card:update */
static double stencil_x(void) {
  double x = joker_slots() - R.njokers;
  for (int i = 0; i < R.njokers; i++)
    if (R.jokers[i].id == J_STENCIL) x += 1;
  return x;
}

static int swash_mult(int ji) {
  int s = 0;
  for (int i = 0; i < R.njokers; i++)
    if (i != ji) s += joker_sell_value(&R.jokers[i]);
  return s;
}

int joker_x_display(const joker_t *j, double *out) {
  switch (j->id) {
    case J_STENCIL: *out = stencil_x(); return 1;
    case J_THROWBACK: *out = 1 + R.skips * 0.25; return 1;
    case J_STEEL_JOKER: *out = 1 + 0.2 * count_enh(E_STEEL); return 1;
  }
  *out = j->x;
  return 0;
}

static int set_eff_x(eff_t *e, double x) {
  e->has |= EF_XMULT;
  e->x = x;
  return 1;
}
static int set_eff_mult(eff_t *e, double m) {
  e->has |= EF_MULT;
  e->mult = m;
  return 1;
}
static int set_eff_chips(eff_t *e, double c) {
  e->has |= EF_CHIPS;
  e->chips = c;
  return 1;
}
static int msg(eff_t *e, const char *m, int col) {
  e->has |= EF_MSG;
  e->msg = m;
  e->col = col;
  return 1;
}

/* is card ci (a playing card) of rank r, ignoring stone cards */
#define CID(ci) card_get_id(ci)

int jcalc(int ji, cx_t *cx, eff_t *e) {
  joker_t *j = J;
  if (j->flags & JF_DEBUFF) return 0;
  int id = j->id;
  if (id == J_BLUEPRINT || id == J_BRAINSTORM) {
    int other = id == J_BRAINSTORM ? 0 : ji + 1;
    if (other < R.njokers && other != ji) {
      cx_t c2 = *cx;
      c2.blueprint = cx->blueprint + 1;
      c2.bp = cx->bp >= 0 ? cx->bp : ji;
      if (c2.blueprint > R.njokers + 1) return 0;
      if (jcalc(other, &c2, e)) {
        e->card = c2.bp;
        return 1;
      }
    }
    return 0;
  }
  e->card = cx->bp >= 0 ? cx->bp : ji;
  switch (cx->kind) {
    case CX_OPEN_BOOSTER:
      if (id == J_HALLUCINATION && cons_room() && prob(2)) {
        give_cons(0);
        return 0; /* the Lua shows no message here */
      }
      return 0;
    case CX_BUYING_CARD: return 0;
    case CX_SELLING_SELF:
      if (id == J_LUCHADOR && R.phase == PH_ROUND && !R.blind_disabled && R.blind >= BL_OX) {
        R.blind_disabled = 2; /* a request: the caller disables the blind */
        return msg(e, "Boss Disabled!", PC_ATTN);
      }
      if (id == J_DIET_COLA) {
        add_tag(TAG_DOUBLE);
        return 0;
      }
      if (id == J_INVISIBLE && j->a >= 2 && NOT_BP) {
        int n = 0, pick[MAXJ];
        for (int i = 0; i < R.njokers; i++)
          if (i != ji) pick[n++] = i;
        if (n > 0 && R.njokers <= joker_slots()) {
          joker_t copy = R.jokers[pick[rng_int(0, n - 1)]];
          if (copy.ed == ED_NEG) copy.ed = 0;
          if (copy.id == J_INVISIBLE) copy.a = 0;
          copy.uid = (uint16_t)joker_new_uid();
          copy.flags = 0;
          if (R.njokers < MAXJ) {
            R.jokers[R.njokers++] = copy;
            joker_on_added(&R.jokers[R.njokers - 1]);
            ev_t *v = ev_push(EV_JOKER_ADD);
            if (v) v->a = copy.uid, v->b = copy.id, v->c = copy.ed, v->d = (int16_t)(R.njokers - 1);
            ev_popup(TG_JOKER, R.njokers - 1, "Duplicated!", PC_ATTN);
          }
        }
      }
      return 0;
    case CX_SELLING_CARD:
      if (id == J_CAMPFIRE && NOT_BP) {
        j->x += 0.25f;
        return msg(e, K_UPGRADE, PC_MULT);
      }
      return 0;
    case CX_REROLL:
      if (id == J_FLASH && NOT_BP) {
        j->a += 2;
        return msg(e, "+2 Mult", PC_MULT); /* a_mult with the new total in the game: show the gain */
      }
      return 0;
    case CX_ENDING_SHOP:
      if (id == J_PERKEO && R.ncons > 0) {
        cons_t c = R.cons[rng_int(0, R.ncons - 1)];
        if (R.ncons < MAXCONS) {
          c.ed = ED_NEG;
          c.uid = (uint16_t)joker_new_uid();
          c.extra_value = 0;
          R.cons[R.ncons++] = c;
          ev_t *v = ev_push(EV_CONS_ADD);
          if (v) v->a = c.uid, v->b = c.id, v->c = c.ed;
          return msg(e, "Duplicated!", PC_ATTN);
        }
      }
      return 0;
    case CX_SKIP_BLIND:
      if (id == J_THROWBACK && NOT_BP) {
        static char t[12];
        double x = 1 + R.skips * 0.25;
        int w = (int)x, f = (int)((x - w) * 100 + 0.5);
        char *p = t;
        *p++ = 'X';
        if (w >= 10) *p++ = (char)('0' + w / 10);
        *p++ = (char)('0' + w % 10);
        if (f) {
          *p++ = '.';
          *p++ = (char)('0' + f / 10);
          if (f % 10) *p++ = (char)('0' + f % 10);
        }
        *p = 0;
        return msg(e, t, PC_MULT);
      }
      return 0;
    case CX_SKIPPING_BOOSTER:
      if (id == J_RED_CARD && NOT_BP) {
        j->a += 3;
        return msg(e, "+3 Mult", PC_MULT);
      }
      return 0;
    case CX_CARDS_ADDED:
      if (id == J_HOLOGRAM && NOT_BP && cx->n > 0) {
        j->x += 0.25f * cx->n;
        return msg(e, "", PC_MULT) & 0; /* the game only shows the new value */
      }
      return 0;
    case CX_FIRST_HAND_DRAWN:
      if (id == J_CERTIFICATE) {
        card_t c = random_card();
        float s = rng_f();
        c.seal = s > 0.75f ? SEAL_RED : s > 0.5f ? SEAL_BLUE : s > 0.25f ? SEAL_GOLD : SEAL_PURPLE;
        add_card_to_deck(c, A_HAND);
        cx_t c2 = {0};
        c2.kind = CX_CARDS_ADDED, c2.bp = -1, c2.n = 1;
        for (int i = 0; i < R.njokers; i++) {
          eff_t e2 = {0};
          jcalc(i, &c2, &e2);
        }
        return 0;
      }
      return 0;
    case CX_SETTING_BLIND:
      if (id == J_CHICOT && NOT_BP && cx->boss) {
        if (!R.blind_disabled) {
          R.blind_disabled = 2; /* disable request */
          return msg(e, "Boss Disabled!", PC_ATTN);
        }
        return 0;
      }
      if (id == J_MADNESS && NOT_BP && !cx->boss) {
        j->x += 0.5f;
        int n = 0, pick[MAXJ];
        for (int i = 0; i < R.njokers; i++)
          if (i != ji && !(R.jokers[i].flags & JF_SLICED)) pick[n++] = i;
        msg(e, "X1.5 Mult", PC_MULT);
        {
          /* show the new total like the game: "X<new> Mult" */
          static char t[16];
          double x = j->x;
          int w = (int)x, f = (int)((x - w) * 10 + 0.5);
          char *p = t;
          *p++ = 'X';
          if (w >= 10) *p++ = (char)('0' + w / 10 % 10);
          *p++ = (char)('0' + w % 10);
          if (f) *p++ = '.', *p++ = (char)('0' + f);
          const char *s = " Mult";
          while (*s) *p++ = *s++;
          *p = 0;
          e->msg = t;
        }
        if (n > 0) R.jokers[pick[rng_int(0, n - 1)]].flags |= JF_SLICED;
        return 1;
      }
      if (id == J_BURGLAR) {
        R.discards_left = 0;
        R.hands_left += 3;
        return msg(e, "+3 Hands", PC_CHIPS);
      }
      if (id == J_RIFF_RAFF && R.njokers < joker_slots()) {
        int k = joker_slots() - R.njokers;
        if (k > 2) k = 2;
        for (int i = 0; i < k; i++) joker_add(create_joker_id(1, 0), -1, 0);
        return msg(e, "+2 Jokers", PC_CHIPS);
      }
      if (id == J_CARTOMANCER && cons_room()) {
        give_cons(0);
        return msg(e, K_PLUS_TAROT, PC_TAROT);
      }
      if (id == J_CEREMONIAL && NOT_BP) {
        if (ji + 1 < R.njokers && !(R.jokers[ji + 1].flags & JF_SLICED)) {
          joker_t *v = &R.jokers[ji + 1];
          j->a += joker_sell_value(v) * 2;
          v->flags |= JF_SLICED;
          static char t[16];
          int m = j->a;
          char *p = t;
          *p++ = '+';
          char d[8];
          int nd = 0;
          do d[nd++] = (char)('0' + m % 10); while (m /= 10);
          while (nd) *p++ = d[--nd];
          const char *s = " Mult";
          while (*s) *p++ = *s++;
          *p = 0;
          return msg(e, t, PC_MULT);
        }
        return 0;
      }
      if (id == J_MARBLE) {
        card_t c = random_card();
        c.enh = E_STONE;
        add_card_to_deck(c, A_DECK);
        cx_t c2 = {0};
        c2.kind = CX_CARDS_ADDED, c2.bp = -1, c2.n = 1;
        for (int i = 0; i < R.njokers; i++) {
          eff_t e2 = {0};
          jcalc(i, &c2, &e2);
        }
        return msg(e, "+1 Stone", PC_CHIPS);
      }
      return 0;
    case CX_DESTROYING_CARD:
      if (NOT_BP && id == J_SIXTH_SENSE && cx->nfull == 1 && CID(cx->full[0]) == 6 && R.hands_played_round == 0) {
        if (cons_room()) {
          give_cons(2);
          msg(e, K_PLUS_SPECTRAL, PC_SPECTRAL);
        }
        e->has |= EF_REMOVE;
        return 1;
      }
      return 0;
    case CX_REMOVE_CARDS:
      if (id == J_CAINO && NOT_BP && cx->nfaces_removed > 0) {
        j->x += cx->nfaces_removed;
        return msg(e, "", PC_MULT);
      }
      if (id == J_GLASS && NOT_BP && cx->nglass_removed > 0) {
        j->x += 0.75f * cx->nglass_removed;
        return msg(e, "", PC_MULT);
      }
      return 0;
    case CX_USING_CONS:
      if (id == J_GLASS && NOT_BP && cx->cons_id == C_HANGED_MAN && cx->nglass_removed > 0) {
        j->x += 0.75f * cx->nglass_removed;
        return msg(e, "", PC_MULT);
      }
      if (id == J_FORTUNE_TELLER && NOT_BP && cx->cons_set == 0) return msg(e, "", PC_MULT);
      if (id == J_CONSTELLATION && NOT_BP && cx->cons_set == 1) {
        j->x += 0.1f;
        return msg(e, "", PC_MULT);
      }
      return 0;
    case CX_DEBUFFED_HAND:
      if (id == J_MATADOR && R.blind_triggered) {
        e->has |= EF_DOLLARS;
        e->dollars = 8;
        return 1;
      }
      return 0;
    case CX_PRE_DISCARD:
      if (id == J_BURNT && R.discards_used_round <= 0 && !cx->hook) {
        handinfo_t hi;
        evaluate_hand(cx->full, cx->nfull, &hi);
        e->has |= EF_LEVELUP;
        e->reps = hi.hand;
        return msg(e, K_LEVELUP, PC_ATTN);
      }
      return 0;
    case CX_DISCARD: {
      int oc = cx->other, last = cx->full[cx->nfull - 1] == oc, deb = R.cards[oc].flags & CF_DEBUFF;
      if (id == J_RAMEN && NOT_BP) {
        if (j->x - 0.01f <= 1) {
          e->has |= EF_REMOVE; /* eaten */
          return msg(e, K_EATEN, PC_ATTN);
        }
        j->x -= 0.01f;
        return msg(e, "-X0.01 Mult", PC_MULT);
      }
      if (id == J_YORICK && NOT_BP) {
        if (j->b <= 1) {
          j->b = 23;
          j->x += 1;
          return msg(e, "", PC_MULT);
        }
        j->b--;
        return 0;
      }
      if (id == J_TRADING && NOT_BP && R.discards_used_round <= 0 && cx->nfull == 1) {
        e->has |= EF_DOLLARS | EF_REMOVE;
        e->dollars = 3;
        return 1;
      }
      if (id == J_CASTLE && !deb && card_is_suit(oc, R.castle_suit, 0, 0) && NOT_BP) {
        j->a += 3;
        return msg(e, K_UPGRADE, PC_CHIPS);
      }
      if (id == J_MAIL && !deb && CID(oc) == R.mail_rank) {
        e->has |= EF_DOLLARS;
        e->dollars = 5;
        return 1;
      }
      if (id == J_HIT_THE_ROAD && !deb && CID(oc) == 11 && NOT_BP) {
        j->x += 0.5f;
        return msg(e, "", PC_MULT);
      }
      if (id == J_GREEN_JOKER && NOT_BP && last) {
        int prev = j->a;
        j->a = j->a - 1 < 0 ? 0 : j->a - 1;
        if (j->a != prev) return msg(e, "-1 Mult", PC_MULT);
        return 0;
      }
      if (id == J_FACELESS && last) {
        int faces = 0;
        for (int i = 0; i < cx->nfull; i++) faces += card_is_face(cx->full[i], 0) != 0;
        if (faces >= 3) {
          e->has |= EF_DOLLARS;
          e->dollars = 5;
          return 1;
        }
      }
      return 0;
    }
    case CX_REP_END_ROUND:
      if (cx->area == A_HAND && id == J_MIME && cx->card_effects) {
        e->has |= EF_REPS;
        e->reps = 1;
        return msg(e, K_AGAIN, PC_ATTN);
      }
      return 0;
    case CX_END_ROUND:
      if (cx->blueprint) return 0;
      if (id == J_CAMPFIRE && R.blind >= BL_OX && j->x > 1) {
        j->x = 1;
        return msg(e, K_RESET, PC_ATTN);
      }
      if (id == J_ROCKET && R.blind >= BL_OX) {
        j->a += 2;
        return msg(e, K_UPGRADE, PC_MONEY);
      }
      if (id == J_TURTLE_BEAN) {
        if (j->a - 1 <= 0) {
          e->has |= EF_REMOVE;
          return msg(e, K_EATEN, PC_ATTN);
        }
        j->a -= 1;
        return msg(e, "-1 Hand Size", PC_ATTN);
      }
      if (id == J_INVISIBLE) {
        j->a++;
        static char t[6];
        if (j->a < 2) {
          t[0] = (char)('0' + j->a), t[1] = '/', t[2] = '2', t[3] = 0;
          return msg(e, t, PC_ATTN);
        }
        return msg(e, K_ACTIVE, PC_ATTN);
      }
      if (id == J_POPCORN) {
        if (j->a - 4 <= 0) {
          e->has |= EF_REMOVE;
          return msg(e, K_EATEN, PC_ATTN);
        }
        j->a -= 4;
        return msg(e, "-4 Mult", PC_MULT);
      }
      if (id == J_TODO_LIST) {
        int n = 0, pick[NHANDS];
        for (int h = 0; h < NHANDS; h++)
          if (R.hands[h].visible && h != j->c) pick[n++] = h;
        if (n) j->c = (uint8_t)pick[rng_int(0, n - 1)];
        return msg(e, K_RESET, PC_ATTN);
      }
      if (id == J_EGG) {
        j->extra_value += 3;
        return msg(e, K_VALUP, PC_MONEY);
      }
      if (id == J_GIFT) {
        for (int i = 0; i < R.njokers; i++) R.jokers[i].extra_value += 1;
        for (int i = 0; i < R.ncons; i++) R.cons[i].extra_value += 1;
        return msg(e, K_VALUP, PC_MONEY);
      }
      if (id == J_HIT_THE_ROAD && j->x > 1) {
        j->x = 1;
        return msg(e, K_RESET, PC_ATTN);
      }
      if (id == J_GROS_MICHEL || id == J_CAVENDISH) {
        if (prob(id == J_CAVENDISH ? 1000 : 6)) {
          if (id == J_GROS_MICHEL) R.gros_michel_extinct = 1;
          e->has |= EF_REMOVE;
          return msg(e, K_EXTINCT, PC_ATTN);
        }
        return msg(e, K_SAFE, PC_ATTN);
      }
      if (id == J_MR_BONES && cx->game_over && R.chips / R.blind_chips >= 0.25) {
        e->has |= EF_SAVED | EF_REMOVE;
        return msg(e, K_SAVED, PC_ATTN);
      }
      return 0;
    case CX_INDIVIDUAL: {
      int oc = cx->other;
      if (cx->area == A_PLAY) {
        if (id == J_HIKER) {
          R.cards[oc].perma += 5;
          return msg(e, K_UPGRADE, PC_CHIPS);
        }
        if (id == J_LUCKY_CAT && (R.cards[oc].flags & CF_LUCKY) && NOT_BP) {
          j->x += 0.25f;
          e->focus = ji;
          return msg(e, K_UPGRADE, PC_MULT);
        }
        if (id == J_WEE && CID(oc) == 2 && NOT_BP) {
          j->a += 8;
          e->focus = ji;
          return msg(e, K_UPGRADE, PC_CHIPS);
        }
        if (id == J_PHOTOGRAPH) {
          int first = -1;
          for (int i = 0; i < cx->hi->nscoring; i++)
            if (card_is_face(R.play[cx->hi->scoring[i]], 0)) {
              first = R.play[cx->hi->scoring[i]];
              break;
            }
          if (oc == first) return set_eff_x(e, 2);
        }
        if (id == J_8_BALL && cons_room()) {
          if (CID(oc) == 8 && prob(4)) {
            give_cons(0);
            return msg(e, K_PLUS_TAROT, PC_TAROT);
          }
        }
        if (id == J_IDOL && CID(oc) == R.idol_rank && card_is_suit(oc, R.idol_suit, 0, 0)) return set_eff_x(e, 2);
        if (id == J_SCARY_FACE && card_is_face(oc, 0)) return set_eff_chips(e, 30);
        if (id == J_SMILEY && card_is_face(oc, 0)) return set_eff_mult(e, 5);
        if (id == J_TICKET && R.cards[oc].enh == E_GOLD) {
          e->has |= EF_DOLLARS;
          e->dollars = 4;
          return 1;
        }
        if (id == J_SCHOLAR && CID(oc) == 14) {
          set_eff_chips(e, 20);
          return set_eff_mult(e, 4);
        }
        if (id == J_WALKIE_TALKIE && (CID(oc) == 10 || CID(oc) == 4)) {
          set_eff_chips(e, 10);
          return set_eff_mult(e, 4);
        }
        if (id == J_BUSINESS && card_is_face(oc, 0) && prob(2)) {
          e->has |= EF_DOLLARS;
          e->dollars = 2;
          return 1;
        }
        if (id == J_FIBONACCI) {
          int r = CID(oc);
          if (r == 2 || r == 3 || r == 5 || r == 8 || r == 14) return set_eff_mult(e, 8);
        }
        if (id == J_EVEN_STEVEN) {
          int r = CID(oc);
          if (r <= 10 && r >= 0 && r % 2 == 0) return set_eff_mult(e, 4);
        }
        if (id == J_ODD_TODD) {
          int r = CID(oc);
          if ((r <= 10 && r >= 0 && r % 2 == 1) || r == 14) return set_eff_chips(e, 31);
        }
        if (id >= J_GREEDY_JOKER && id <= J_GLUTTENOUS_JOKER) {
          static const uint8_t suit[4] = {S_DIAMONDS, S_HEARTS, S_SPADES, S_CLUBS};
          if (card_is_suit(oc, suit[id - J_GREEDY_JOKER], 0, 0)) return set_eff_mult(e, 3);
        }
        if (id == J_ROUGH_GEM && card_is_suit(oc, S_DIAMONDS, 0, 0)) {
          e->has |= EF_DOLLARS;
          e->dollars = 1;
          return 1;
        }
        if (id == J_ONYX_AGATE && card_is_suit(oc, S_CLUBS, 0, 0)) return set_eff_mult(e, 7);
        if (id == J_ARROWHEAD && card_is_suit(oc, S_SPADES, 0, 0)) return set_eff_chips(e, 50);
        if (id == J_BLOODSTONE && card_is_suit(oc, S_HEARTS, 0, 0) && prob(2)) return set_eff_x(e, 1.5);
        if (id == J_ANCIENT && card_is_suit(oc, R.ancient_suit, 0, 0)) return set_eff_x(e, 1.5);
        if (id == J_TRIBOULET && (CID(oc) == 12 || CID(oc) == 13)) return set_eff_x(e, 2);
        return 0;
      }
      if (cx->area == A_HAND) {
        int deb = R.cards[oc].flags & CF_DEBUFF;
        if (id == J_SHOOT_THE_MOON && CID(oc) == 12) {
          if (deb) return msg(e, K_DEBUFFED, PC_MULT);
          e->has |= EF_HMULT;
          e->mult = 13;
          return 1;
        }
        if (id == J_BARON && CID(oc) == 13) {
          if (deb) return msg(e, K_DEBUFFED, PC_MULT);
          return set_eff_x(e, 1.5);
        }
        if (id == J_RESERVED_PARKING && card_is_face(oc, 0) && prob(2)) {
          if (deb) return msg(e, K_DEBUFFED, PC_MULT);
          e->has |= EF_DOLLARS;
          e->dollars = 1;
          return 1;
        }
        if (id == J_RAISED_FIST) {
          int tm = 15, tid = 15, raised = -1;
          for (int i = 0; i < R.nhand; i++) {
            int c = R.hand[i];
            if (tid >= R.cards[c].rank && R.cards[c].enh != E_STONE) {
              tm = card_chip_nominal(R.cards[c].rank);
              tid = R.cards[c].rank;
              raised = c;
            }
          }
          if (raised == oc) {
            if (deb) return msg(e, K_DEBUFFED, PC_MULT);
            e->has |= EF_HMULT;
            e->mult = 2 * tm;
            return 1;
          }
        }
      }
      return 0;
    }
    case CX_REPETITION: {
      int oc = cx->other;
      if (cx->area == A_PLAY) {
        int r = CID(oc);
        if ((id == J_SOCK_AND_BUSKIN && card_is_face(oc, 0)) ||
            (id == J_HANGING_CHAD && oc == R.play[cx->hi->scoring[0]]) ||
            (id == J_DUSK && R.hands_left == 0) || id == J_SELZER ||
            (id == J_HACK && (r == 2 || r == 3 || r == 4 || r == 5))) {
          e->has |= EF_REPS;
          e->reps = id == J_HANGING_CHAD ? 2 : 1;
          return msg(e, K_AGAIN, PC_ATTN);
        }
      }
      if (cx->area == A_HAND && id == J_MIME && cx->card_effects) {
        e->has |= EF_REPS;
        e->reps = 1;
        return msg(e, K_AGAIN, PC_ATTN);
      }
      return 0;
    }
    case CX_OTHER_JOKER:
      if (id == J_BASEBALL && cx->other_joker != ji &&
          joker_info[R.jokers[cx->other_joker].id].rarity == 2) {
        e->has |= EF_XMULT;
        e->x = 1.5;
        return 1;
      }
      return 0;
    case CX_BEFORE:
      if (id == J_TROUSERS && (HAS(H_TWO_PAIR) || HAS(H_FULL_HOUSE)) && NOT_BP) {
        j->a += 2;
        return msg(e, K_UPGRADE, PC_MULT);
      }
      if (id == J_SPACE && prob(4)) {
        e->has |= EF_LEVELUP;
        e->reps = cx->hi->hand;
        return msg(e, K_LEVELUP, PC_ATTN);
      }
      if (id == J_SQUARE && cx->nfull == 4 && NOT_BP) {
        j->a += 4;
        return msg(e, K_UPGRADE, PC_CHIPS);
      }
      if (id == J_RUNNER && HAS(H_STRAIGHT) && NOT_BP) {
        j->a += 15;
        return msg(e, K_UPGRADE, PC_CHIPS);
      }
      if (id == J_MIDAS_MASK && NOT_BP) {
        int n = 0;
        for (int i = 0; i < cx->hi->nscoring; i++) {
          int c = R.play[cx->hi->scoring[i]];
          if (card_is_face(c, 0)) {
            n++;
            R.cards[c].enh = E_GOLD;
            ev_card_set(c);
          }
        }
        if (n) return msg(e, K_GOLD, PC_MONEY);
        return 0;
      }
      if (id == J_VAMPIRE && NOT_BP) {
        int n = 0;
        for (int i = 0; i < cx->hi->nscoring; i++) {
          int c = R.play[cx->hi->scoring[i]];
          if (R.cards[c].enh != E_NONE && !(R.cards[c].flags & CF_DEBUFF)) {
            n++;
            R.cards[c].enh = E_NONE;
            ev_card_set(c);
          }
        }
        if (n) {
          j->x += 0.1f * n;
          return msg(e, "", PC_MULT);
        }
        return 0;
      }
      if (id == J_TODO_LIST && cx->hi->hand == j->c) {
        e->has |= EF_DOLLARS;
        e->dollars = 4;
        return 1;
      }
      if (id == J_DNA && R.hands_played_round == 0 && cx->nfull == 1) {
        card_t c = R.cards[cx->full[0]];
        c.flags &= CF_DOWN;
        add_card_to_deck(c, A_HAND);
        cx_t c2 = {0};
        c2.kind = CX_CARDS_ADDED, c2.bp = -1, c2.n = 1;
        for (int i = 0; i < R.njokers; i++) {
          eff_t e2 = {0};
          jcalc(i, &c2, &e2);
        }
        return msg(e, K_COPIED, PC_CHIPS);
      }
      if (id == J_RIDE_THE_BUS && NOT_BP) {
        int faces = 0;
        for (int i = 0; i < cx->hi->nscoring; i++) faces |= card_is_face(R.play[cx->hi->scoring[i]], 0);
        if (faces) {
          int last = j->a;
          j->a = 0;
          if (last > 0) return msg(e, K_RESET, PC_ATTN);
        } else
          j->a += 1;
        return 0;
      }
      if (id == J_OBELISK && NOT_BP) {
        int reset = 1, h = cx->hi->hand, more = R.hands[h].played;
        for (int k = 0; k < NHANDS; k++)
          if (k != h && R.hands[k].played >= more && R.hands[k].visible) reset = 0;
        if (reset) {
          if (j->x > 1) {
            j->x = 1;
            return msg(e, K_RESET, PC_ATTN);
          }
        } else
          j->x += 0.2f;
        return 0;
      }
      if (id == J_GREEN_JOKER && NOT_BP) {
        j->a += 1;
        return msg(e, "+1 Mult", PC_MULT);
      }
      return 0;
    case CX_AFTER:
      if (id == J_ICE_CREAM && NOT_BP) {
        if (j->a - 5 <= 0) {
          e->has |= EF_REMOVE;
          return msg(e, K_MELTED, PC_CHIPS);
        }
        j->a -= 5;
        return msg(e, "-5", PC_CHIPS);
      }
      if (id == J_SELZER && NOT_BP) {
        if (j->a - 1 <= 0) {
          e->has |= EF_REMOVE;
          return msg(e, K_DRANK, PC_ATTN);
        }
        j->a -= 1;
        static char t[4];
        t[0] = (char)('0' + j->a % 10);
        t[1] = 0;
        return msg(e, t, PC_ATTN);
      }
      return 0;
    case CX_MAIN: {
      if (id == J_LOYALTY_CARD) {
        int rem = (5 - 1 - (R.hands_played - j->b)) % (5 + 1);
        if (rem < 0) rem += 6;
        if (NOT_BP) j->c = (uint8_t)rem;
        if (rem == 5) return set_eff_x(e, 4);
        return 0;
      }
      int t = jtype(id);
      double x = j->x;
      if (id == J_STENCIL) x = stencil_x();
      if (id == J_THROWBACK) x = 1 + R.skips * 0.25;
      if (id == J_CAINO) x = 1; /* Caino uses its own value below */
      if (id != J_SEEING_DOUBLE && x > 1 && (t < 0 || HAS(t)) && id != J_STEEL_JOKER) return set_eff_x(e, x);
      switch (id) {
        case J_JOLLY: case J_ZANY: case J_MAD: case J_CRAZY: case J_DROLL:
          if (HAS(t)) return set_eff_mult(e, id == J_JOLLY ? 8 : (id == J_MAD || id == J_DROLL) ? 10 : 12);
          return 0;
        case J_SLY: case J_WILY: case J_CLEVER: case J_DEVIOUS: case J_CRAFTY:
          if (HAS(t)) return set_eff_chips(e, id == J_SLY ? 50 : (id == J_CLEVER || id == J_CRAFTY) ? 80 : 100);
          return 0;
        case J_HALF: return cx->nfull <= 3 ? set_eff_mult(e, 20) : 0;
        case J_ABSTRACT: return set_eff_mult(e, 3 * R.njokers);
        case J_ACROBAT: return R.hands_left == 0 ? set_eff_x(e, 3) : 0;
        case J_MYSTIC_SUMMIT: return R.discards_left == 0 ? set_eff_mult(e, 15) : 0;
        case J_MISPRINT: return set_eff_mult(e, rng_int(0, 23));
        case J_BANNER: return R.discards_left > 0 ? set_eff_chips(e, 30 * R.discards_left) : 0;
        case J_STUNTMAN: return set_eff_chips(e, 250);
        case J_MATADOR:
          if (R.blind_triggered) {
            e->has |= EF_DOLLARS;
            e->dollars = 8;
            return 1;
          }
          return 0;
        case J_SUPERNOVA: return set_eff_mult(e, R.hands[cx->hi->hand].played);
        case J_CEREMONIAL: return j->a > 0 ? set_eff_mult(e, j->a) : 0;
        case J_VAGABOND:
          if (cons_room() && R.money <= 4) {
            give_cons(0);
            return msg(e, K_PLUS_TAROT, PC_TAROT);
          }
          return 0;
        case J_SUPERPOSITION:
          if (cons_room()) {
            int aces = 0;
            for (int i = 0; i < cx->hi->nscoring; i++) aces += CID(R.play[cx->hi->scoring[i]]) == 14;
            if (aces >= 1 && HAS(H_STRAIGHT)) {
              give_cons(0);
              return msg(e, K_PLUS_TAROT, PC_TAROT);
            }
          }
          return 0;
        case J_SEANCE:
          if (cons_room() && HAS(H_STRAIGHT_FLUSH)) {
            give_cons(2);
            return msg(e, K_PLUS_SPECTRAL, PC_SPECTRAL);
          }
          return 0;
        case J_FLOWER_POT: {
          int s[4] = {0, 0, 0, 0};
          static const uint8_t order[4] = {S_HEARTS, S_DIAMONDS, S_SPADES, S_CLUBS};
          for (int pass = 0; pass < 2; pass++)
            for (int i = 0; i < cx->hi->nscoring; i++) {
              int c = R.play[cx->hi->scoring[i]];
              if ((R.cards[c].enh == E_WILD) != pass) continue;
              for (int k = 0; k < 4; k++)
                if (card_is_suit(c, order[k], !pass, 0) && s[k] == 0) {
                  s[k] = 1;
                  break;
                }
            }
          return (s[0] && s[1] && s[2] && s[3]) ? set_eff_x(e, 3) : 0;
        }
        case J_SEEING_DOUBLE: {
          int s[4] = {0, 0, 0, 0}; /* H D S C */
          for (int i = 0; i < cx->hi->nscoring; i++) {
            int c = R.play[cx->hi->scoring[i]];
            if (R.cards[c].enh == E_WILD) continue;
            s[0] += card_is_suit(c, S_HEARTS, 0, 0) != 0;
            s[1] += card_is_suit(c, S_DIAMONDS, 0, 0) != 0;
            s[2] += card_is_suit(c, S_SPADES, 0, 0) != 0;
            s[3] += card_is_suit(c, S_CLUBS, 0, 0) != 0;
          }
          for (int i = 0; i < cx->hi->nscoring; i++) {
            int c = R.play[cx->hi->scoring[i]];
            if (R.cards[c].enh != E_WILD) continue;
            if (card_is_suit(c, S_CLUBS, 0, 0) && s[3] == 0) s[3]++;
            else if (card_is_suit(c, S_DIAMONDS, 0, 0) && s[1] == 0) s[1]++;
            else if (card_is_suit(c, S_SPADES, 0, 0) && s[2] == 0) s[2]++;
            else if (card_is_suit(c, S_HEARTS, 0, 0) && s[0] == 0) s[0]++;
          }
          return ((s[0] || s[1] || s[2]) && s[3]) ? set_eff_x(e, 2) : 0;
        }
        case J_WEE: return set_eff_chips(e, j->a);
        case J_CASTLE: return j->a > 0 ? set_eff_chips(e, j->a) : 0;
        case J_BLUE_JOKER: return R.ndeck > 0 ? set_eff_chips(e, 2 * R.ndeck) : 0;
        case J_EROSION: {
          int d = R.starting_deck - count_cards();
          return d > 0 ? set_eff_mult(e, 4 * d) : 0;
        }
        case J_SQUARE: return set_eff_chips(e, j->a);
        case J_RUNNER: return set_eff_chips(e, j->a);
        case J_ICE_CREAM: return set_eff_chips(e, j->a);
        case J_STONE: {
          int n = count_enh(E_STONE);
          return n > 0 ? set_eff_chips(e, 25 * n) : 0;
        }
        case J_STEEL_JOKER: {
          int n = count_enh(E_STEEL);
          return n > 0 ? set_eff_x(e, 1 + 0.2 * n) : 0;
        }
        case J_BULL: return R.money > 0 ? set_eff_chips(e, 2 * R.money) : 0;
        case J_DRIVERS_LICENSE: {
          int n = 0;
          for (int i = 0; i < R.ncards_alloc; i++)
            if ((R.cards[i].flags & CF_USED) && R.cards[i].enh != E_NONE) n++;
          return n >= 16 ? set_eff_x(e, 3) : 0;
        }
        case J_BLACKBOARD: {
          int black = 0;
          for (int i = 0; i < R.nhand; i++)
            black += card_is_suit(R.hand[i], S_CLUBS, 0, 1) || card_is_suit(R.hand[i], S_SPADES, 0, 1);
          return black == R.nhand ? set_eff_x(e, 3) : 0;
        }
        case J_STENCIL: return (joker_slots() - R.njokers) > 0 ? set_eff_x(e, x) : 0;
        case J_SWASHBUCKLER: {
          int m = swash_mult(ji);
          return m > 0 ? set_eff_mult(e, m) : 0;
        }
        case J_JOKER: return set_eff_mult(e, 4);
        case J_TROUSERS: case J_RIDE_THE_BUS: case J_FLASH: case J_POPCORN: case J_GREEN_JOKER: case J_RED_CARD:
          return j->a > 0 ? set_eff_mult(e, j->a) : 0;
        case J_FORTUNE_TELLER: return R.tarots_used > 0 ? set_eff_mult(e, R.tarots_used) : 0;
        case J_GROS_MICHEL: return set_eff_mult(e, 15);
        case J_CAVENDISH: return set_eff_x(e, 3);
        case J_CARD_SHARP: return R.hands[cx->hi->hand].played_round > 1 ? set_eff_x(e, 3) : 0;
        case J_BOOTSTRAPS: {
          int k = R.money / 5;
          return k >= 1 ? set_eff_mult(e, 2 * k) : 0;
        }
        case J_CAINO: return j->x > 1 ? set_eff_x(e, j->x) : 0;
      }
      return 0;
    }
  }
  return 0;
}
