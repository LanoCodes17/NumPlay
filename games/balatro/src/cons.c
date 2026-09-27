/* Tarot, Planet and Spectral cards: Card:use_consumeable and
 * Card:can_use_consumeable from card.lua. */
#include "ev.h"
#include "game.h"
#include "jokers.h"
#include "util.h"

void apply_disable_request(void);
void blind_debuff_card(int ci);

static const uint8_t planet_hand[12] = {H_PAIR, H_THREE_KIND, H_FULL_HOUSE, H_FOUR_KIND, H_FLUSH, H_STRAIGHT,
                                        H_TWO_PAIR, H_STRAIGHT_FLUSH, H_HIGH_CARD, H_FIVE_KIND, H_FLUSH_HOUSE,
                                        H_FLUSH_FIVE};

/* max selected cards for the card-targeting consumables (0 = none) */
static int max_sel(int id) {
  switch (id) {
    case C_MAGICIAN: case C_EMPRESS: case C_HEIROPHANT: case C_STRENGTH: case C_HANGED_MAN: case C_DEATH: return 2;
    case C_LOVERS: case C_CHARIOT: case C_JUSTICE: case C_DEVIL: case C_TOWER: return 1;
    case C_STAR: case C_MOON: case C_SUN: case C_WORLD: return 3;
    case C_TALISMAN: case C_DEJA_VU: case C_TRANCE: case C_MEDIUM: case C_CRYPTID: case C_AURA: return 1;
  }
  return 0;
}

static int editionless_jokers(int *out) {
  int n = 0;
  for (int i = 0; i < R.njokers; i++)
    if (!R.jokers[i].ed) out[n++] = i;
  return n;
}

/* can_use_consumeable; in_pack: used straight from a booster pack */
int cons_usable(int id, int in_pack) {
  int tmp[MAXJ];
  int hand_ok = R.phase == PH_ROUND || (R.phase == PH_PACK && (R.pack_kind == PK_ARCANA || R.pack_kind == PK_SPECTRAL));
  if (id == C_HERMIT || (id >= C_MERCURY && id <= C_ERIS) || id == C_TEMPERANCE || id == C_BLACK_HOLE) return 1;
  if (id == C_WHEEL_OF_FORTUNE) return editionless_jokers(tmp) > 0;
  if (id == C_ANKH) return R.njokers > 0 && joker_slots() > 1;
  if (id == C_ECTOPLASM || id == C_HEX) return editionless_jokers(tmp) > 0;
  if (id == C_EMPEROR || id == C_HIGH_PRIESTESS) return R.ncons < cons_slots() + (in_pack ? 0 : 1);
  if (id == C_FOOL)
    return (R.ncons < cons_slots() + (in_pack ? 0 : 1)) && R.last_tarot_planet >= 0 && R.last_tarot_planet != C_FOOL;
  if (id == C_JUDGEMENT || id == C_SOUL || id == C_WRAITH) return R.njokers < joker_slots();
  if (!hand_ok) return 0;
  int ms = max_sel(id);
  if (ms) {
    int n = nselected();
    if (id == C_AURA) {
      if (n != 1) return 0;
      for (int i = 0; i < R.nhand; i++)
        if (R.sel[i] && R.cards[R.hand[i]].ed) return 0;
      return 1;
    }
    return n >= (id == C_DEATH ? 2 : 1) && n <= ms;
  }
  if (id == C_FAMILIAR || id == C_GRIM || id == C_INCANTATION || id == C_IMMOLATE || id == C_SIGIL || id == C_OUIJA)
    return R.nhand > 1;
  return 0;
}

int cons_can_use(int idx, int from_pack, int id) {
  (void)idx;
  return cons_usable(id, from_pack);
}

static int sel_cards(uint8_t *out) {
  int n = 0;
  for (int i = 0; i < R.nhand; i++)
    if (R.sel[i]) out[n++] = R.hand[i];
  return n;
}

static void set_card(int ci) {
  blind_debuff_card(ci);
  ev_card_set(ci);
  ev_delay(1);
}

static void notify_added(int n) {
  cx_t cx = {0};
  cx.kind = CX_CARDS_ADDED, cx.bp = -1, cx.n = n;
  for (int i = 0; i < R.njokers; i++) {
    eff_t e = {0};
    jcalc(i, &cx, &e);
  }
}

static void remove_cards(uint8_t *cards, int n, int *glass) {
  int faces = 0, g = 0;
  for (int i = 0; i < n; i++) {
    int ci = cards[i];
    if (card_is_face(ci, 0)) faces++;
    if (R.cards[ci].enh == E_GLASS) g++;
    ev_card(EV_CARD_DESTROY, ci, R.cards[ci].enh == E_GLASS, 0);
    destroy_card(ci);
  }
  cx_t cx = {0};
  /* the glass ones break later in the game (not "shattered" yet): only
     The Hanged Man counts them, through using_consumeable */
  cx.kind = CX_REMOVE_CARDS, cx.bp = -1, cx.nfaces_removed = faces, cx.nglass_removed = 0;
  for (int i = 0; i < R.njokers; i++) {
    eff_t e = {0};
    if (jcalc(i, &cx, &e)) ev_juice(TG_JOKER, i);
  }
  if (glass) *glass = g;
}

static void random_from_hand(uint8_t *out, int k) {
  uint8_t pool[MAXHAND];
  int n = R.nhand;
  for (int i = 0; i < n; i++) pool[i] = R.hand[i];
  for (int i = 0; i < k && n; i++) {
    int r = rng_int(0, n - 1);
    out[i] = pool[r];
    pool[r] = pool[--n];
  }
}

static void destroy_other_jokers(int keep_uid) {
  for (int i = R.njokers - 1; i >= 0; i--)
    if (R.jokers[i].uid != keep_uid) joker_remove(i, 0);
}

/* from: 0 consumable slot, 1 booster pack, 2 bought and used in the shop */
int use_cons_id(int id, int ed, int from) {
  (void)ed, (void)from;
  uint8_t sel[8];
  int ns = sel_cards(sel), glass = 0;
  char t[20];
  ev_delay(2);
  if (id <= C_WORLD) R.tarots_used++;
  if (id >= C_MERCURY && id <= C_ERIS) R.planets_used_mask |= (uint16_t)(1 << (id - C_MERCURY));
  switch (id) {
    case C_MAGICIAN: case C_EMPRESS: case C_HEIROPHANT: case C_LOVERS: case C_CHARIOT: case C_JUSTICE: case C_DEVIL:
    case C_TOWER: {
      int enh = id == C_MAGICIAN ? E_LUCKY : id == C_EMPRESS ? E_MULT : id == C_HEIROPHANT ? E_BONUS : id == C_LOVERS ? E_WILD
                : id == C_CHARIOT ? E_STEEL : id == C_JUSTICE ? E_GLASS : id == C_DEVIL ? E_GOLD : E_STONE;
      for (int i = 0; i < ns; i++) R.cards[sel[i]].enh = (uint8_t)enh, set_card(sel[i]);
      break;
    }
    case C_STAR: case C_MOON: case C_SUN: case C_WORLD: {
      int s = id == C_STAR ? S_DIAMONDS : id == C_MOON ? S_CLUBS : id == C_SUN ? S_HEARTS : S_SPADES;
      for (int i = 0; i < ns; i++) R.cards[sel[i]].suit = (uint8_t)s, set_card(sel[i]);
      break;
    }
    case C_STRENGTH:
      for (int i = 0; i < ns; i++) {
        card_t *c = &R.cards[sel[i]];
        c->rank = (uint8_t)(c->rank == 14 ? 2 : c->rank + 1);
        set_card(sel[i]);
      }
      break;
    case C_DEATH: {
      int right = sel[ns - 1];
      for (int i = 0; i < ns - 1; i++) {
        card_t *c = &R.cards[sel[i]];
        uint8_t f = c->flags;
        *c = R.cards[right];
        c->flags = (uint8_t)((f & (CF_USED | CF_PLAYED_ANTE)) | (R.cards[right].flags & 0));
        set_card(sel[i]);
      }
      break;
    }
    case C_HANGED_MAN: remove_cards(sel, ns, &glass); break;
    case C_TEMPERANCE: {
      int m = 0;
      for (int i = 0; i < R.njokers; i++) m += joker_sell_value(&R.jokers[i]);
      if (m > 50) m = 50;
      R.money += m;
      ev_money();
      break;
    }
    case C_HERMIT: {
      int m = R.money < 0 ? 0 : R.money > 20 ? 20 : R.money;
      R.money += m;
      ev_money();
      break;
    }
    case C_WHEEL_OF_FORTUNE: case C_ECTOPLASM: case C_HEX: {
      int pool[MAXJ], n = editionless_jokers(pool);
      if (id == C_WHEEL_OF_FORTUNE && !prob(4)) {
        ev_message("Nope!");
        break;
      }
      if (!n) break;
      int ji = pool[rng_int(0, n - 1)];
      joker_t *j = &R.jokers[ji];
      j->ed = (uint8_t)(id == C_ECTOPLASM ? ED_NEG : id == C_HEX ? ED_POLY : poll_edition(1, 1, 1));
      if (!j->ed) j->ed = ED_FOIL;
      ev_t *v = ev_push(EV_JOKER_SET);
      if (v) v->a = j->uid, v->b = j->ed, v->c = j->flags;
      if (id == C_HEX) destroy_other_jokers(j->uid);
      if (id == C_ECTOPLASM) {
        R.ecto_minus++;
        R.hand_size_mod -= R.ecto_minus;
      }
      break;
    }
    case C_JUDGEMENT: joker_add(create_joker_id(-1, 0), -1, 0); break;
    case C_SOUL: joker_add(create_joker_id(4, 1), -1, 0); break;
    case C_WRAITH:
      joker_add(create_joker_id(3, 0), -1, 0);
      if (R.money != 0) {
        R.money = 0;
        ev_money();
      }
      break;
    case C_EMPEROR: case C_HIGH_PRIESTESS:
      for (int k = 0; k < 2 && R.ncons < cons_slots(); k++) add_cons(create_cons_id(id == C_EMPEROR ? 0 : 1), 0);
      break;
    case C_FOOL:
      if (R.ncons < cons_slots() && R.last_tarot_planet >= 0) add_cons(R.last_tarot_planet, 0);
      break;
    case C_BLACK_HOLE:
      for (int h = 0; h < NHANDS; h++) level_up(h, 1, -1);
      break;
    case C_FAMILIAR: case C_GRIM: case C_INCANTATION: {
      uint8_t d[1];
      random_from_hand(d, 1);
      remove_cards(d, 1, 0);
      int k = id == C_FAMILIAR ? 3 : id == C_GRIM ? 2 : 4;
      for (int i = 0; i < k; i++) {
        card_t c = random_card();
        if (id == C_FAMILIAR) c.rank = (uint8_t)rng_int(11, 13);
        if (id == C_GRIM) c.rank = 14;
        if (id == C_INCANTATION) c.rank = (uint8_t)rng_int(2, 10);
        c.enh = (uint8_t)random_enhancement(1);
        add_card_to_deck(c, A_HAND);
      }
      notify_added(k);
      sort_hand(R.sort_suit);
      break;
    }
    case C_TALISMAN: case C_DEJA_VU: case C_TRANCE: case C_MEDIUM: {
      int seal = id == C_TALISMAN ? SEAL_GOLD : id == C_DEJA_VU ? SEAL_RED : id == C_TRANCE ? SEAL_BLUE : SEAL_PURPLE;
      if (ns) R.cards[sel[0]].seal = (uint8_t)seal, set_card(sel[0]);
      break;
    }
    case C_AURA:
      if (ns) {
        int e = poll_edition(1, 1, 1);
        R.cards[sel[0]].ed = (uint8_t)(e ? e : ED_FOIL);
        set_card(sel[0]);
      }
      break;
    case C_SIGIL: case C_OUIJA: {
      int s = rng_int(0, 3), r = rng_int(2, 14);
      for (int i = 0; i < R.nhand; i++) {
        card_t *c = &R.cards[R.hand[i]];
        if (id == C_SIGIL) c->suit = (uint8_t)s;
        else c->rank = (uint8_t)r;
        set_card(R.hand[i]);
      }
      if (id == C_OUIJA) R.hand_size_mod -= 1;
      sort_hand(R.sort_suit);
      break;
    }
    case C_IMMOLATE: {
      uint8_t d[5];
      int k = R.nhand < 5 ? R.nhand : 5;
      random_from_hand(d, k);
      remove_cards(d, k, 0);
      R.money += 20;
      ev_money();
      break;
    }
    case C_ANKH: {
      if (!R.njokers) break;
      int ci = rng_int(0, R.njokers - 1);
      joker_t copy = R.jokers[ci];
      destroy_other_jokers(copy.uid);
      if (R.njokers < MAXJ) {
        copy.uid = (uint16_t)joker_new_uid();
        if (copy.ed == ED_NEG) copy.ed = 0;
        copy.flags = 0;
        R.jokers[R.njokers++] = copy;
        joker_on_added(&R.jokers[R.njokers - 1]);
        ev_t *v = ev_push(EV_JOKER_ADD);
        if (v) v->a = copy.uid, v->b = copy.id, v->c = copy.ed, v->d = (int16_t)(R.njokers - 1);
      }
      break;
    }
    case C_CRYPTID:
      if (ns) {
        card_t c = R.cards[sel[0]];
        c.flags &= CF_DOWN;
        for (int i = 0; i < 2; i++) add_card_to_deck(c, A_HAND);
        notify_added(2);
        sort_hand(R.sort_suit);
      }
      break;
    default:
      if (id >= C_MERCURY && id <= C_ERIS) level_up(planet_hand[id - C_MERCURY], 1, -1);
      break;
  }
  /* like the game's deferred event: The Fool copies the card used before it */
  if ((id <= C_WORLD) || (id >= C_MERCURY && id <= C_ERIS)) R.last_tarot_planet = (int8_t)id;
  /* using_consumeable */
  cx_t cx = {0};
  cx.kind = CX_USING_CONS, cx.bp = -1, cx.cons_id = id, cx.nglass_removed = glass;
  cx.cons_set = id <= C_WORLD ? 0 : id <= C_ERIS ? 1 : 2;
  for (int i = 0; i < R.njokers; i++) {
    eff_t e = {0};
    if (jcalc(i, &cx, &e)) {
      ev_juice(TG_JOKER, e.card);
      if (e.msg && e.msg[0]) ev_popup(TG_JOKER, e.card, e.msg, e.col);
      else if (R.jokers[i].id == J_FORTUNE_TELLER) {
        char *p = str_cat(t, "+");
        p = fmt_int(p, R.tarots_used);
        str_cat(p, " Mult");
        ev_popup(TG_JOKER, i, t, PC_MULT);
      } else {
        char *p = str_cat(t, "X");
        p = fmt_short(p, R.jokers[i].x);
        str_cat(p, " Mult");
        ev_popup(TG_JOKER, i, t, PC_MULT);
      }
    }
  }
  for (int i = 0; i < MAXHAND; i++) R.sel[i] = 0;
  apply_disable_request();
  return 1;
}

int use_cons(int idx) {
  if (idx < 0 || idx >= R.ncons) return 0;
  int id = R.cons[idx].id;
  if (!cons_usable(id, 0)) return 0;
  ev_reset();
  int ed = R.cons[idx].ed;
  ev_t *v = ev_push(EV_CONS_REMOVE);
  if (v) v->a = R.cons[idx].uid, v->b = 1;
  for (int i = idx; i < R.ncons - 1; i++) R.cons[i] = R.cons[i + 1];
  R.ncons--;
  use_cons_id(id, ed, 0);
  ev_sync();
  return 1;
}
