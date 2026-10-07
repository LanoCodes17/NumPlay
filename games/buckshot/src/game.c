/* The match: rules, the Dealer, and the order of events, following the
 * original's round manager, item manager and Dealer "intelligence".
 *
 * Every action is settled first (shells, charges, items, whose turn is next),
 * the match is saved, and only then is it shown: quitting in the middle of an
 * animation resumes after the action, never before it. */
#include "game.h"
#include "scene.h"
#include "story.h"
#include <eadk.h>

match_t G;
profile_t P;

const char *const ITEM_NAME[IT_COUNT] = {T("HAND SAW"), T("MAGNIFYING GLASS"), T("BEER"), T("CIGARETTE PACK"), T("HANDCUFFS"),
                                         T("EXPIRED MEDICINE"), T("BURNER PHONE"), T("ADRENALINE"), T("INVERTER")};
const char *const ITEM_DESC[IT_COUNT] = {
    T("SHOTGUN DEALS 2 DAMAGE."),
    T("CHECK THE CURRENT ROUND\nIN THE CHAMBER."),
    T("RACKS THE SHOTGUN.\nEJECTS CURRENT SHELL."),
    T("TAKES THE EDGE OFF.\nREGAIN 1 CHARGE."),
    T("DEALER SKIPS THE\nNEXT TURN."),
    T("50% CHANCE TO REGAIN 2 CHARGES.\nIF NOT, LOSE 1 CHARGE."),
    T("A MYSTERIOUS VOICE GIVES\nYOU INSIGHT FROM THE FUTURE."),
    T("STEAL AN ITEM AND\nUSE IT IMMEDIATELY."),
    T("SWAPS THE POLARITY OF THE\nCURRENT SHELL IN THE CHAMBER."),
};

/* Story mode: live and blank shells of the five loads of each stage (the
 * last one repeats), starting charges, items drawn before each load. */
static const uint8_t STORY_LOADS[3][5][2] = {{{1, 2}, {3, 2}, {3, 2}, {3, 3}, {5, 2}},
                                             {{1, 1}, {2, 2}, {3, 2}, {3, 3}, {5, 2}},
                                             {{1, 2}, {4, 4}, {3, 2}, {4, 2}, {5, 3}}};
static const uint8_t STORY_HP[3] = {2, 4, 6}, STORY_ITEMS[3] = {0, 2, 4};
/* how many of each item a player may hold: story, Double or Nothing */
static const uint8_t CAP[2][IT_COUNT] = {{8, 8, 8, 2, 8, 0, 0, 0, 0}, {3, 3, 2, 1, 1, 1, 1, 2, 8}};

enum { WHO_PLAYER, WHO_DEALER };

uint32_t rnd(uint32_t n) {
  uint32_t x = G.rng ? G.rng : 0x9E3779B9u;
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  G.rng = x;
  return n ? x % n : x;
}

/* ------------------------------------------------------------------ helpers */

static int count(int side, int it) {
  int n = 0;
  for (int g = 0; g < 8; g++) n += G.items[side][g] == it;
  return n;
}

static int free_slots(int side) { return count(side, IT_NONE); }

static void load_counts(int *live, int *blank, int *nitems, int *shuffle) {
  int k = G.load < 5 ? G.load : 4;
  if (G.mode == MODE_STORY) {
    *live = STORY_LOADS[G.stage][k][0];
    *blank = STORY_LOADS[G.stage][k][1];
    *nitems = STORY_ITEMS[G.stage];
    *shuffle = G.stage == 2 && k > 0;
  } else {
    *live = G.don[k][0];
    *blank = G.don[k][1];
    *nitems = G.don[k][2];
    *shuffle = G.don[k][3];
  }
}

static void eject(void) {
  for (int i = 1; i < G.nshell; i++) G.shell[i - 1] = G.shell[i], G.dknown[i - 1] = G.dknown[i];
  if (G.nshell) G.nshell--;
  G.dknown[G.nshell] = 0;
  G.ejected++;
}

/* round 3: at 2 charges or fewer the defibrillator's wire is cut */
static void check_wires(void) {
  if (G.mode != MODE_STORY || G.stage != 2) return;
  for (int s = 0; s < 2; s++)
    if (G.hp[s] > 0 && G.hp[s] <= 2 && !G.wire[s]) {
      G.wire[s] = 2; /* cut requested: done at the start of the next turn */
      G.hp[s] = 1;
    }
}

static void heal(int side, int n) {
  if (G.wire[side]) return;
  G.hp[side] += n;
  if (G.hp[side] > G.maxhp) G.hp[side] = G.maxhp;
}

/* damage to someone: returns 1 if that ended the stage */
static int hurt(int side, int n) {
  G.hp[side] -= n;
  if (G.hp[side] <= 0) {
    G.hp[side] = 0;
    G.phase = side ? PH_STAGE_WON : PH_REVIVE;
    return 1;
  }
  check_wires();
  return 0;
}

/* The end of a turn: the saw wears off, then whoever plays next (or a new load). */
static void end_turn(int player_next) {
  G.sawed = 0;
  G.dturn = 0;
  if (!G.nshell) G.phase = PH_LOAD, G.loaded = 0;
  else G.phase = player_next ? PH_PLAYER : PH_DEALER;
}

static void do_wire_cuts(void) {
  for (int s = 0; s < 2; s++)
    if (G.wire[s] == 2) {
      G.wire[s] = 1;
      int say_it = !P.cut_said;
      P.cut_said = 1;
      save_match();
      anim_wire_cut(s, say_it);
    }
}

/* ------------------------------------------------------------- the player */

static void cursor_desc(void) {
  T.title = 0;
  T.desc = 0;
  if (T.cursor == 8) {
    T.title = T("SHOTGUN");
    T.desc = T("SHOOTING YOURSELF WITH A BLANK\nSKIPS THE DEALER'S TURN.");
  } else if (T.cursor >= 0) {
    int it = G.items[T.cursor >= 16][T.cursor & 7];
    if (it >= 0) T.title = ITEM_NAME[it], T.desc = ITEM_DESC[it];
  }
}

/* Arrow navigation: the nearest target in that direction (screen positions of
 * the table view; the Dealer's slots sit behind the player's). */
static int nav(int cur, int key, const int *cand, int n) {
  int cx, cy, best = cur, bd = 1 << 30;
  target_pos(cur, &cx, &cy);
  for (int i = 0; i < n; i++) {
    if (cand[i] == cur) continue;
    int x, y;
    target_pos(cand[i], &x, &y);
    int dx = x - cx, dy = y - cy, main, side;
    if (key == KEY_LEFT) main = -dx, side = dy;
    else if (key == KEY_RIGHT) main = dx, side = dy;
    else if (key == KEY_UP) main = -dy, side = dx;
    else main = dy, side = dx;
    if (main <= 0) continue;
    if (side < 0) side = -side;
    int d = main + side * 2;
    if (d < bd) bd = d, best = cand[i];
  }
  /* the shotgun lies in the middle: going from one of your sides to the
   * other stops on it (otherwise it could be out of reach of the arrows) */
  if ((key == KEY_LEFT || key == KEY_RIGHT) && cur < 8 && best < 8 && best != cur) {
    for (int i = 0; i < n; i++)
      if (cand[i] == 8) {
        int gx, gy, bx, by;
        target_pos(8, &gx, &gy);
        target_pos(best, &bx, &by);
        if ((gx - cx) * (gx - bx) < 0) best = 8;
      }
  }
  return best;
}

static int usable(int it) {
  if (it == IT_SAW && G.sawed) return 0;
  if (it == IT_CUFFS && G.dcuffed) return 0;
  return 1;
}

/* The player's choice: returns a slot (0..7) or 8 for the shotgun. The
 * Dealer's items (16+) can be looked at, not used. */
static int choose(int *cur) {
  for (;;) {
    int cand[17], n = 0;
    for (int g = 0; g < 8; g++)
      if (G.items[0][g] >= 0) cand[n++] = g;
    cand[n++] = 8;
    for (int g = 0; g < 8; g++)
      if (G.items[1][g] >= 0) cand[n++] = 16 + g;
    if (*cur < 0 || (*cur != 8 && G.items[*cur >= 16][*cur & 7] < 0)) *cur = 8;
    T.cursor = *cur;
    T.view = *cur >= 16 ? V_DEALER : V_TABLE;
    cursor_desc();
    T.hint = *cur >= 16 ? T("THE DEALER'S ITEM   DOWN: BACK") : T("OK: USE   BACK: PAUSE");
    table_show();
    int k = key_wait(-1);
    if (IS_OK(k)) {
      if (*cur == 8 || (*cur < 8 && usable(G.items[0][*cur]))) {
        T.cursor = -1, T.title = T.desc = T.hint = 0;
        return *cur;
      }
    } else if (k == KEY_BACK) {
      app_pause_menu();
    } else if (k >= 0 && k <= KEY_RIGHT) {
      *cur = nav(*cur, k, cand, n);
    }
  }
}

static void use_item(int side, int it, int from);

/* Adrenaline: take one of the Dealer's items within a few seconds and use it. */
static void steal(void) {
  int cand[8], n = 0;
  for (int g = 0; g < 8; g++) {
    int it = G.items[1][g];
    if (it < 0 || it == IT_ADREN || (it == IT_CUFFS && G.dcuffed) || (it == IT_SAW && G.sawed)) continue;
    cand[n++] = 16 + g;
  }
  if (!n) {
    save_match();
    pause_ms(800);
    return;
  }
  int cur = cand[0];
  uint32_t left = 7000;
  T.view = V_DEALER;
  T.rush = 1;
  while (left) {
    T.cursor = cur;
    cursor_desc();
    T.hint = T("OK: STEAL");
    table_show();
    int k = key_wait(left < 100 ? left : 100);
    left = left < 100 ? 0 : left - 100;
    if (IS_OK(k)) {
      int g = cur & 7, it = G.items[1][g];
      G.items[1][g] = IT_NONE;
      T.cursor = -1, T.title = T.desc = T.hint = 0;
      T.rush = 0;
      use_item(WHO_PLAYER, it, 8 + g);
      return;
    }
    if (k >= 0 && k <= KEY_RIGHT) cur = nav(cur, k, cand, n);
  }
  /* too slow: the adrenaline is spent */
  T.cursor = -1, T.title = T.desc = T.hint = 0;
  T.rush = 0;
  save_match();
  T.view = V_TABLE;
  table_show();
}

static const char *const SEQ[7] = {"", T("SECOND SHELL ..."), T("THIRD SHELL ..."), T("FOURTH SHELL ..."),
                                   T("FIFTH SHELL ..."), T("SIXTH SHELL ..."), T("SEVENTH SHELL ...")};

static const char *phone_message(void) {
  static char msg[NP_TEXT_EXTRA ? 96 : 48];
  if (G.nshell == 1) return T("HOW UNFORTUNATE ...");
  int k = 1 + rnd(G.nshell - 1);
  if (k >= 7) k--;
  int n = 0;
  for (const char *p = SEQ[k]; *p; p++) msg[n++] = *p;
  for (const char *p = G.shell[k] ? T("\n... LIVE ROUND.") : T("\n... BLANK."); *p; p++) msg[n++] = *p;
  msg[n] = 0;
  return msg;
}

/* An item used by the player (side 0) or by the Dealer (side 1), taken from
 * slot `from` (side*8+slot, or -1). The effect is settled and saved, then shown. */
static void use_item(int side, int it, int from) {
  act_t a = {0};
  a.from = from;
  a.live = G.shell[0];
  switch (it) {
    case IT_SAW:
      G.sawed = 1;
      break;
    case IT_BEER:
      if (!side) G.beer_ml += 330;
      eject();
      if (!G.nshell) {
        if (!side) G.phase = PH_LOAD, G.loaded = 0, G.sawed = 0;
        /* the Dealer's turn ends by itself when no shell is left */
      }
      break;
    case IT_CIGS:
      if (!side) G.cigs++;
      heal(side, 1);
      break;
    case IT_CUFFS:
      if (side) G.pcuffed = 1, G.pbreak = 0;
      else G.dcuffed = 1, G.dbreak = 0;
      break;
    case IT_MEDS:
      a.good = rnd(100) < 50;
      if (a.good) {
        heal(side, 2);
      } else if (!G.wire[side] || !side) {
        a.dead = hurt(side, 1);
      }
      break;
    case IT_PHONE:
      if (!side) {
        a.text = phone_message();
      } else if (G.nshell > 2) {
        int k = 1 + rnd(G.nshell - 1);
        if (k == 8) k--;
        G.dknown[k] = 1;
      }
      break;
    case IT_INVERT:
      G.shell[0] = !G.shell[0];
      break;
    case IT_GLASS:
    case IT_ADREN:
      break;
  }
  if (it != IT_ADREN) save_match(); /* adrenaline: settled with what it steals */
  anim_item(side, it, &a);
  if (it == IT_ADREN && !side) steal();
}

/* The shotgun is picked up: aim at the Dealer (up) or yourself (down), fire. */
static void player_shoot(void) {
  int self = anim_player_aim();
  int live = G.shell[0], dmg = G.sawed ? 2 : 1, ended = 0;
  if (live) G.shots++;
  eject();
  if (live) ended = hurt(self ? 0 : 1, dmg);
  if (!ended) end_turn(self && !live);
  save_match();
  anim_player_shot(self, live, dmg, ended);
}

static void player_turn(void) {
  if (G.pcuffed) {
    if (!G.pbreak) {
      /* cuffed: this turn goes to the Dealer */
      G.pbreak = 1;
      G.phase = PH_DEALER;
      save_match();
      anim_player_cuffed(0);
      return;
    }
    G.pcuffed = 0;
    G.pbreak = 0;
    save_match();
    anim_player_cuffed(1);
  }
  do_wire_cuts();
  static int cur = 8;
  for (;;) {
    G.phase = PH_PLAYER;
    save_match();
    view_table();
    int sel = choose(&cur);
    if (sel == 8) {
      player_shoot();
      return;
    }
    int it = G.items[0][sel];
    G.items[0][sel] = IT_NONE;
    use_item(WHO_PLAYER, it, sel);
    if (G.phase != PH_PLAYER) return;
  }
}

/* --------------------------------------------------------------- the Dealer */

enum { UNKNOWN = -1, BLANK = 0, LIVE = 1, SELF = 0, PLAYER = 1 };

/* Double or Nothing: what the Dealer can tell from counting and his phone. */
static int figure_out(void) {
  if (G.dknown[0]) return 1;
  int live = 0, blank = 0;
  for (int i = 0; i < G.nshell; i++) live += G.shell[i], blank += !G.shell[i];
  if (!live || !blank) return 1;
  for (int i = 0; i < G.nshell; i++)
    if (G.dknown[i]) G.shell[i] ? live-- : blank--;
  return !live || !blank;
}

static int coin(void) {
  if (G.mode == MODE_STORY) return rnd(2);
  int live = 0, blank = 0;
  for (int i = 0; i < G.nshell; i++) live += G.shell[i], blank += !G.shell[i];
  if (live == blank) return rnd(2);
  return live > blank;
}

/* the items in the order they were laid on the table */
static int list_items(int side, int *slots) {
  int n = 0;
  for (int s = 0; s < 64 && n < 8; s++)
    for (int g = 0; g < 8; g++)
      if (G.items[side][g] >= 0 && G.seq[side][g] == s) slots[n++] = g;
  /* anything without an order (should not happen) */
  for (int g = 0; g < 8; g++) {
    int in = 0;
    for (int i = 0; i < n; i++) in |= slots[i] == g;
    if (!in && G.items[side][g] >= 0) slots[n++] = g;
  }
  return n;
}

static void dealer_shoot(int who) {
  int live = G.shell[0], dmg = G.sawed ? 2 : 1, ended = 0;
  if (live) G.shots++;
  eject();
  if (live) {
    ended = hurt(who == SELF ? 1 : 0, dmg);
    if (!ended) end_turn(1);
  } else if (who == SELF && G.nshell) {
    /* a blank on himself: he goes again (and a sawn-off barrel stays sawn off) */
    G.phase = PH_DEALER;
    G.dturn = 0;
  } else {
    end_turn(1);
  }
  save_match();
  anim_dealer_shot(who == SELF, live, dmg, ended);
}

static void dealer_turn(void) {
  G.phase = PH_DEALER;
  if (!G.dturn) {
    G.dturn = 1;
    G.d_saw = G.d_meds = 0;
    G.d_knows = 0, G.d_known = UNKNOWN, G.d_target = -1;
  }
  if (G.dcuffed) {
    if (!G.dbreak) {
      /* he tries his handcuffs: this turn is lost */
      G.dbreak = 1;
      G.dturn = 0;
      G.phase = PH_PLAYER;
      save_match();
      anim_dealer_cuffed(0);
      return;
    }
    G.dcuffed = 0;
    save_match();
    anim_dealer_cuffed(1);
  }
  save_match();
  view_dealer();
  pause_ms(600);
  for (;;) {
    do_wire_cuts();
    if (!G.nshell) {
      end_turn(1);
      G.phase = PH_LOAD;
      return;
    }
    if (G.mode == MODE_DON && !G.d_knows) {
      G.d_knows = figure_out();
      if (G.d_knows) G.d_known = G.shell[0], G.d_target = G.shell[0] ? PLAYER : SELF;
    }
    if (G.nshell == 1) G.d_known = G.shell[0], G.d_target = G.d_known ? PLAYER : SELF, G.d_knows = 1;
    int has_cigs = count(1, IT_CIGS) > 0, adren = count(1, IT_ADREN) > 0;
    int slots[16], sides[16], n = list_items(1, slots);
    for (int i = 0; i < n; i++) sides[i] = 1;
    if (adren) {
      int m = list_items(0, slots + n);
      for (int i = 0; i < m; i++) sides[n + i] = 0;
      n += m;
    }
    int want = -1, cuff = 0;
    for (int i = 0; i < n && want < 0; i++) {
      int it = G.items[sides[i]][slots[i]];
      switch (it) {
        case IT_GLASS:
          if (!G.d_knows && G.nshell != 1)
            want = it, G.d_known = G.shell[0], G.d_target = G.d_known ? PLAYER : SELF, G.d_knows = 1;
          break;
        case IT_CIGS:
          if (G.hp[1] < G.maxhp) want = it, has_cigs = 0;
          break;
        case IT_MEDS:
          if (G.hp[1] < G.maxhp && !has_cigs && !G.d_meds && G.hp[1] != 1) want = it, G.d_meds = 1;
          break;
        case IT_BEER:
          if (G.d_known != LIVE && G.nshell != 1) {
            want = it;
            if (G.mode == MODE_DON) G.d_knows = 0, G.d_known = UNKNOWN;
          }
          break;
        case IT_CUFFS:
          if (!G.pcuffed && G.nshell != 1) want = it, cuff = 1;
          break;
        case IT_SAW:
          if (!G.sawed && G.d_known == LIVE) want = it, G.d_saw = 1;
          break;
        case IT_PHONE:
          if (G.nshell > 2) want = it;
          break;
        case IT_INVERT:
          if (G.d_knows && G.d_known == BLANK) want = it, G.d_known = LIVE, G.d_target = PLAYER;
          break;
      }
    }
    (void)cuff;
    int has_saw = 0;
    for (int i = 0; i < n; i++) has_saw |= G.items[sides[i]][slots[i]] == IT_SAW;
    if (want < 0 && !G.d_saw && has_saw && !G.sawed && G.d_known != BLANK) {
      if (coin() == 0) G.d_target = SELF;
      else want = IT_SAW, G.d_saw = 1, G.d_target = PLAYER;
    }
    if (want < 0) break;
    /* his own item if he has one, otherwise the player's, with adrenaline */
    int g = -1, from = 1;
    for (int i = 0; i < 8 && g < 0; i++)
      if (G.items[1][i] == want) g = i;
    if (g < 0) {
      from = 0;
      for (int i = 0; i < 8 && g < 0; i++)
        if (G.items[0][i] == want) g = i;
      if (g >= 0) G.items[0][g] = IT_NONE;
      for (int i = 0; i < 8; i++)
        if (G.items[1][i] == IT_ADREN) {
          G.items[1][i] = IT_NONE;
          use_item(WHO_DEALER, IT_ADREN, 8 + i);
          break;
        }
    } else {
      G.items[1][g] = IT_NONE;
    }
    use_item(WHO_DEALER, want, g >= 0 ? from * 8 + g : -1);
    if (G.phase != PH_DEALER) return;
  }
  if (G.d_target < 0) G.d_target = coin() ? PLAYER : SELF;
  dealer_shoot(G.d_target);
}

/* ---------------------------------------------------------------- loading */

static int deal_one(int side, int from_don, int no_saw) {
  int avail[IT_COUNT], na = 0;
  for (int it = 0; it < IT_COUNT; it++)
    if (CAP[from_don][it] && count(side, it) < CAP[from_don][it] && !(it == IT_SAW && no_saw)) avail[na++] = it;
  return na ? avail[rnd(na)] : -1;
}

/* The items: the player takes them out of the box one at a time. Each one
 * placed is saved, so quitting only replays the ones not taken yet. */
static void deal_items(int n) {
  int from_don = G.mode == MODE_DON;
  int no_saw = G.load == 0 && G.maxhp == 2;
  if (G.loaded == 0) G.deal_left = n, G.deal_spook = 0, G.loaded = 1;
  view_table();
  if (!P.read_items) {
    say(T("LET'S MAKE THIS A LITTLE\nMORE INTERESTING ..."), 3000);
  }
  T.box = 1;
  table_show();
  pause_ms(800);
  if (!P.read_items) {
    static char s[NP_TEXT_EXTRA ? 64 : 24];
    s[0] = '0' + n;
    const char *t = T(" ITEMS EACH.");
    int k = 1;
    while (*t) s[k++] = *t++;
    s[k] = 0;
    say(s, 2500);
    say(T("MORE ITEMS BEFORE\nEVERY LOAD."), 2500);
    P.read_items = 1;
    save_profile();
  } else if (G.mode == MODE_STORY && G.stage == 2 && G.load == 0 && !P.read_items4) {
    say(T("4 ITEMS EACH."), 2500);
    P.read_items4 = 1;
    save_profile();
  }
  while (G.deal_left) {
    T.hint = T("OK: TAKE AN ITEM");
    T.held = -1;
    table_show();
    for (;;) {
      int e = key_wait(-1);
      if (IS_OK(e)) break;
      if (e == KEY_BACK) app_pause_menu(), table_show();
    }
    if (G.mode == MODE_STORY && G.stage == 1 && G.load == 1 && !P.seen_god && G.deal_spook++ == 1) {
      /* a bloody waiver, already signed */
      P.seen_god = 1;
      save_profile();
      anim_god_waiver();
      continue;
    }
    int it = deal_one(0, from_don, no_saw);
    if (it < 0) break;
    T.hint = 0;
    if (!free_slots(0)) {
      say(T("OUT OF SPACE."), 1800);
      say(T("HOW UNFORTUNATE ..."), 2200);
      break;
    }
    T.held = it;
    /* choose a free slot */
    int cand[8], nc = 0;
    for (int g = 0; g < 8; g++)
      if (G.items[0][g] < 0) cand[nc++] = g;
    int cur = cand[0];
    for (;;) {
      T.cursor = cur;
      T.hint = T("OK: PLACE");
      table_show();
      int e;
      while ((e = idle_key()) < 0) {}
      if (IS_OK(e)) break;
      if (e == KEY_BACK) app_pause_menu();
      else if (e >= 0 && e <= KEY_RIGHT) cur = nav(cur, e, cand, nc);
    }
    T.cursor = -1;
    T.hint = 0;
    T.held = -1;
    G.items[0][cur] = it;
    G.seq[0][cur] = G.seqn++;
    G.deal_left--;
    save_match();
    table_show();
    pause_ms(150);
  }
  G.deal_left = 0;
  T.hint = 0;
  T.held = -1;
  /* the Dealer's items */
  for (int k = 0; k < n; k++) {
    if (!free_slots(1)) break;
    int it = deal_one(1, from_don, no_saw);
    if (it < 0) break;
    int fr[8], nf = 0;
    for (int g = 0; g < 8; g++)
      if (G.items[1][g] < 0) fr[nf++] = g;
    int g = fr[rnd(nf)];
    G.items[1][g] = it;
    G.seq[1][g] = G.seqn++;
  }
  pause_ms(450);
  T.box = 0;
  table_show();
  pause_ms(900);
}

static const char *const RULES[INTRO_LINES] = {
    T("THE RULES ARE SIMPLE:"),
    T("SHOOTING YOURSELF WITH\nA BLANK SKIPS MY TURN."),
    T("SHOOTING THE DEALER WITH\nA LIVE TAKES THEIR LIFE."),
    T("WE WILL PLAY A SERIES\nOF ROUNDS ..."),
    T("UNTIL WE REACH\nTHE FINAL SHOWDOWN."),
    T("THERE, WE WILL DANCE ON\nTHE EDGE OF LIFE AND DEATH."),
    T("WIN, AND WALK AWAY WITH\n400,000$ IN CASH."),
    T("LOSE, AND WE SELL YOUR\nMANGLED CORPSE FOR PROFIT."),
    T("SHALL WE BEGIN?")};
static const uint16_t RULES_MS[INTRO_LINES] = {1900, 3000, 3000, 3000, 3000, 3700, 3700, 3700, 2500};

static void load_start(void) {
  int live, blank, nitems, shuffle;
  load_counts(&live, &blank, &nitems, &shuffle);
  if (G.loaded < 2) {
    /* everyone is uncuffed before a new load */
    G.pcuffed = G.pbreak = G.dcuffed = G.dbreak = 0;
    G.sawed = 0;
    G.dturn = 0;
    view_table();
    table_show();
    pause_ms(600);
    if (nitems) deal_items(nitems);
    /* the shells: shown in the compartment in this order, then shuffled in */
    int n = live + blank;
    for (int i = 0; i < n; i++) G.shown[i] = i < live;
    if (shuffle)
      for (int i = n - 1; i > 0; i--) {
        int j = rnd(i + 1);
        uint8_t t = G.shown[i];
        G.shown[i] = G.shown[j], G.shown[j] = t;
      }
    for (int i = 0; i < n; i++) G.shell[i] = G.shown[i];
    for (int r = 0; r < 2; r++)
      for (int i = n - 1; i > 0; i--) {
        int j = rnd(i + 1);
        uint8_t t = G.shell[i];
        G.shell[i] = G.shell[j], G.shell[j] = t;
      }
    G.nshell = G.nshown = n;
#ifdef BR_DEMO_LIVE
    for (int i = 1; i < n; i++) /* development: a live shell first, for demo recordings */
      if (G.shell[i] && !G.shell[0]) G.shell[i] = 0, G.shell[0] = 1;
#endif
    for (int i = 0; i < 8; i++) G.dknown[i] = 0;
    G.loaded = 2;
    save_match();
  }
  static char text[NP_TEXT_EXTRA ? 96 : 40];
  const char *msg = text;
  int k = 0;
  text[k++] = '0' + live;
  for (const char *p = live == 1 ? T(" LIVE ROUND. ") : T(" LIVE ROUNDS. "); *p; p++) text[k++] = *p;
  text[k++] = '0' + blank;
  for (const char *p = blank == 1 ? T(" BLANK.") : T(" BLANKS."); *p; p++) text[k++] = *p;
  text[k] = 0;
  int ms = 2500;
  if (G.mode == MODE_DON || (G.stage == 2)) {
    msg = 0;
    ms = 1300;
    if (G.mode == MODE_STORY && !P.drill_said) {
      msg = T("YOU KNOW THE DRILL.");
      ms = 2500;
      P.drill_said = 1;
      save_profile();
    }
  }
  shells_view(G.shown, G.nshown, msg, ms);
  /* the Dealer loads the shotgun */
  view_dealer();
  int told = -1;
  if (P.loads_told < 2 && G.mode == MODE_STORY) told = P.loads_told++, save_profile();
  anim_dealer_load(G.nshown, told, G.stage > 0 || G.mode == MODE_DON);
  if (P.intro_line < INTRO_LINES && G.mode == MODE_STORY) {
    /* the rules, the first time: each line is remembered once shown */
    while (P.intro_line < INTRO_LINES) {
      int i = P.intro_line;
      say(RULES[i], RULES_MS[i]);
      P.intro_line = i + 1;
      save_profile();
    }
  }
  anim_dealer_rack();
  G.load++;
  G.loaded = 0;
  G.phase = PH_PLAYER;
  view_table();
  table_show();
  pause_ms(300);
}

/* ----------------------------------------------------------------- stages */

static void new_stage(void) {
  int s = G.stage;
  if (!G.keep_items) {
    for (int g = 0; g < 8; g++) G.items[0][g] = G.items[1][g] = IT_NONE;
    G.seqn = 0;
  }
  G.keep_items = 0;
  G.load = 0;
  G.loaded = 0;
  G.deal_left = 0;
  G.nshell = G.nshown = 0;
  G.wire[0] = G.wire[1] = 0;
  G.pcuffed = G.pbreak = G.dcuffed = G.dbreak = 0;
  G.sawed = 0;
  G.dturn = 0;
  G.careful_said = 0;
  if (G.mode == MODE_STORY) {
    G.maxhp = STORY_HP[s];
  } else {
    for (int k = 0; k < 5; k++) {
      int total = 2 + rnd(7), live = total / 2 < 1 ? 1 : total / 2;
      G.don[k][0] = live;
      G.don[k][1] = total - live;
      G.don[k][2] = 2 + rnd(4);
      G.don[k][3] = rnd(2);
    }
    G.maxhp = 2 + rnd(3);
  }
  G.hp[0] = G.hp[1] = G.maxhp;
#ifdef BR_DEALER_HP
  G.hp[1] = BR_DEALER_HP; /* development: a quick win */
#endif
#ifdef BR_PLAYER_HP
  G.hp[0] = BR_PLAYER_HP; /* development: a quick loss */
#endif
}

static void stage_start(void) {
  new_stage();
  G.phase = PH_LOAD;
  G.from_death = 0;
  save_match();
  view_dealer();
  table_show();
  pause_ms(600);
  if (G.mode == MODE_STORY && G.stage == 2) {
    /* the final showdown: no more defibrillators */
    if (!P.read_final) {
      say(T("LONG LAST, WE ARRIVE\nAT THE FINAL SHOWDOWN."), 4000);
      say(T("NO MORE DEFIBRILLATORS.\nNO MORE BLOOD TRANSFUSIONS."), 4000);
      say(T("NOW, ME AND YOU, WE ARE DANCING\nON THE EDGE OF LIFE AND DEATH."), 4800);
      P.read_final = 1;
      save_profile();
    } else {
      say(T("I BETTER NOT\nSEE YOU AGAIN."), 3000);
    }
  }
  round_indicator();
  health_bootup();
}

static void stage_won(void) {
  if (G.mode == MODE_DON) G.rounds_beat++;
  if (G.stage < 2) {
    G.stage++;
    G.phase = PH_STAGE;
    save_match();
    pause_ms(800);
    health_wins();
    view_dealer();
    anim_dealer_return();
    return;
  }
  if (G.mode == MODE_DON) {
    /* the winnings of these three rounds, settled now: the machine counts them up */
    G.score_before = G.score;
    if (!G.doubled) {
      uint32_t loss = (uint32_t)((uint64_t)G.batch_ms * 40000 / 600000);
      G.score = loss > 69990 ? 10 : 70000 - loss;
    } else {
      G.score = G.score * 2;
    }
    G.doubled = 1;
    G.phase = PH_DON_CHOICE;
  } else {
    G.phase = PH_ENDING;
  }
}

/* ------------------------------------------------------------------- run */

void game_new(int mode) {
  uint32_t seed = G.rng;
  char name[7];
  for (int i = 0; i < 7; i++) name[i] = G.name[i];
  G = (match_t){0};
  for (int i = 0; i < 7; i++) G.name[i] = name[i];
  G.rng = seed ? seed : 1;
  G.mode = mode;
  G.doors = 2;
  for (int g = 0; g < 8; g++) G.items[0][g] = G.items[1][g] = IT_NONE;
  G.phase = PH_STAGE;
}

void game_run(void) {
  table_reset();
  uint64_t t_last = eadk_timing_millis();
  for (;;) {
    uint64_t now = eadk_timing_millis();
    if (G.mode == MODE_DON && !G.doubled) G.batch_ms += (uint32_t)(now - t_last);
    t_last = now;
    save_match(); /* a safe point: resume here */
    switch (G.phase) {
      case PH_STAGE:
        stage_start();
        break;
      case PH_LOAD:
        load_start();
        break;
      case PH_PLAYER:
        player_turn();
        break;
      case PH_DEALER:
        dealer_turn();
        break;
      case PH_STAGE_WON:
        stage_won();
        break;
      case PH_REVIVE:
        if (G.mode == MODE_STORY && G.stage < 2) {
          /* the defibrillator: the same round starts over */
          if (!G.revived) {
            G.revived = 1;
            G.deaths++;
            G.doors += 2;
            for (int g = 0; g < 8; g++) G.items[0][g] = G.items[1][g] = IT_NONE;
            save_match();
          }
          story_revive();
          G.revived = 0;
          G.from_death = 1;
          G.phase = PH_STAGE;
        } else {
          /* true death: the run is over, unless "retry" in heaven */
          G.phase = PH_NONE;
          save_match();
          int retry = story_death();
          if (G.mode == MODE_DON) {
            G.phase = PH_RETRY; /* back to the bathroom, a new run */
            return;
          }
          if (!retry) return;
          for (int g = 0; g < 8; g++) G.items[0][g] = G.items[1][g] = IT_NONE;
          G.phase = PH_REDO;
        }
        break;
      case PH_REDO:
        story_retry();
        G.from_death = 2;
        G.phase = PH_STAGE;
        break;
      case PH_DON_CHOICE:
        if (!story_double_or_nothing()) {
          G.phase = PH_ENDING;
          break;
        }
        G.stage = 0;
        G.keep_items = 1; /* the items stay on the table for the next three stages */
        G.phase = PH_STAGE;
        break;
      case PH_ENDING:
        story_ending();
        G.phase = PH_NONE;
        save_match();
        return;
      default:
        return;
    }
  }
}
