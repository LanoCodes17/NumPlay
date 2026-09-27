/* The interface core: the visual state, playing back the rules' events with
 * Balatro's timing and animations, the HUD sidebar and the card areas. */
#include "ui.h"
#include <eadk.h>
#include <string.h>
#include "assets.h"
#include "bg.h"
#include "ev.h"
#include "jokers.h"
#include "save.h"
#include "util.h"
#include "flame.h"

vis_t V;

/* ------------------------------------------------------------ popups */
typedef struct {
  char txt[20];
  uint8_t col, tgt;
  uint16_t id;
  int16_t t, life;
  int16_t x, y;
} popup_t;
static popup_t pops[10];
static int popup_over(int x0, int y0, int x1, int y1);
static char banner[40];
static int banner_t;
static int wait_ms;
static int levelup_t, levelup_len, levelup_hand, levelup_level, levelup_old;
static int shake;

int ui_hold_ms;

float speed_factor(void) {
  static const float sp[4] = {0.5f, 1, 2, 4};
  float f = sp[S.speed & 3];
  if (ui_hold_ms > 300) f *= 3; /* holding OK or EXE speeds things up */
  return f;
}

int ui_busy(void) { return ev_head < ev_tail || wait_ms > 0 || levelup_t > 0; }

/* ------------------------------------------------------------ sync */
static void card_from_logic(int ci) {
  vcard_t *v = &V.cards[ci];
  const card_t *c = &R.cards[ci];
  v->rank = c->rank, v->suit = c->suit, v->enh = c->enh, v->ed = c->ed, v->seal = c->seal, v->flags = c->flags;
  v->perma = c->perma;
  v->down = (c->flags & CF_DOWN) != 0;
}

static void item_from_joker(vitem_t *v, const joker_t *j) {
  v->uid = j->uid, v->id = j->id, v->ed = j->ed, v->flags = j->flags;
}

void ui_sync_all(int instant) {
  /* the areas */
  for (int i = 0; i < R.ncards_alloc; i++) {
    vcard_t *v = &V.cards[i];
    card_from_logic(i);
    int area = A_NONE;
    if (R.cards[i].flags & CF_USED) area = A_DECK;
    uint8_t old = v->area;
    v->area = (uint8_t)area;
    if (instant || old == A_NONE) v->x = DECK_X, v->y = DECK_Y;
    v->fade = 0, v->shatter = 0;
  }
  V.nhand = R.nhand;
  for (int i = 0; i < R.nhand; i++) {
    V.hand[i] = R.hand[i];
    V.cards[R.hand[i]].area = A_HAND;
    V.cards[R.hand[i]].raise = R.sel[i] ? 1 : 0;
  }
  V.nplay = R.nplay;
  for (int i = 0; i < R.nplay; i++) V.play[i] = R.play[i], V.cards[R.play[i]].area = A_PLAY;
  for (int i = 0; i < R.ndiscard; i++) V.cards[R.discard[i]].area = A_DISCARD;
  /* jokers and consumables, keeping positions of the ones still there */
  vitem_t old[MAXJ + 4];
  int nold = V.njokers;
  memcpy(old, V.jokers, sizeof old);
  V.njokers = R.njokers;
  for (int i = 0; i < R.njokers; i++) {
    vitem_t *v = &V.jokers[i];
    float x = JOKER_X + JOKER_W / 2, y = -CH;
    for (int k = 0; k < nold; k++)
      if (old[k].uid == R.jokers[i].uid) x = old[k].x, y = old[k].y;
    memset(v, 0, sizeof *v);
    item_from_joker(v, &R.jokers[i]);
    v->x = x, v->y = y;
  }
  vitem_t oldc[MAXCONS + 2];
  int noldc = V.ncons;
  memcpy(oldc, V.cons, sizeof oldc);
  V.ncons = R.ncons;
  for (int i = 0; i < R.ncons; i++) {
    vitem_t *v = &V.cons[i];
    float x = CONS_X + CONS_W / 2, y = -CH;
    for (int k = 0; k < noldc; k++)
      if (oldc[k].uid == R.cons[i].uid) x = oldc[k].x, y = oldc[k].y;
    memset(v, 0, sizeof *v);
    v->uid = R.cons[i].uid, v->id = R.cons[i].id, v->ed = R.cons[i].ed;
    v->x = x, v->y = y;
  }
  V.money_target = R.money;
  if (instant) V.money = R.money;
  V.hands = R.hands_left, V.discards = R.discards_left;
  V.score_target = R.chips;
  if (instant) V.score = R.chips;
  V.deck_count = R.ndeck;
}

/* ------------------------------------------------------------ targets */
static vitem_t *find_joker_v(int uid) {
  for (int i = 0; i < V.njokers; i++)
    if (V.jokers[i].uid == uid) return &V.jokers[i];
  return 0;
}
static vitem_t *find_cons_v(int uid) {
  for (int i = 0; i < V.ncons; i++)
    if (V.cons[i].uid == uid) return &V.cons[i];
  return 0;
}

static void target_pos(int tgt, int id, int *x, int *y) {
  *x = 160, *y = 100;
  /* jokers and consumables: on the card's lower edge (the game's 'bm');
     playing cards: over it */
  if (tgt == TG_JOKER) {
    vitem_t *v = find_joker_v(id);
    if (v) *x = (int)v->x + CW / 2, *y = (int)v->y + CH - 3;
  } else if (tgt == TG_CONS) {
    vitem_t *v = find_cons_v(id);
    if (v) *x = (int)v->x + CW / 2, *y = (int)v->y + CH - 3;
  } else if (tgt == TG_PLAY || tgt == TG_HAND) {
    if (id < MAXCARDS) *x = (int)V.cards[id].x + CW / 2, *y = (int)V.cards[id].y - 8;
  } else if (tgt == TG_CENTER) {
    *x = SIDE_W + (320 - SIDE_W) / 2, *y = 100;
  }
}

static void add_popup(const ev_t *e) {
  int slot = 0;
  for (int i = 0; i < 10; i++)
    if (pops[i].t <= 0) {
      slot = i;
      break;
    } else if (pops[i].t < pops[slot].t)
      slot = i;
  popup_t *p = &pops[slot];
  const char *src = (e->txt[0] == 1 && !e->txt[1]) ? e->p.s : e->txt;
  int n = 0;
  while (src[n] && n < 15) p->txt[n] = src[n], n++;
  p->txt[n] = 0;
  if (e->c & 1) {
    static const char suf[] = " Mult";
    for (int k = 0; suf[k] && n < 19; k++) p->txt[n++] = suf[k];
    p->txt[n] = 0;
  }
  p->col = e->b, p->tgt = e->t, p->id = e->a;
  p->life = p->t = 850;
  int x, y;
  target_pos(e->t, e->a, &x, &y);
  p->x = (int16_t)x, p->y = (int16_t)y;
  /* one popup at a time in a place: a neighbour's older one makes way */
  int w = g_textw(p->txt, T_MED) + 8;
  for (int i = 0; i < 10; i++) {
    popup_t *q = &pops[i];
    if (q == p || q->t <= 0) continue;
    int dx = q->x - x, dy = q->y - y, qw = g_textw(q->txt, T_MED) + 8;
    if (dx < 0) dx = -dx;
    if (dy < 0) dy = -dy;
    if (dy < 16 && 2 * dx < w + qw + 4) q->t = 0;
  }
}

static void juice(int tgt, int id) {
  if (tgt == TG_JOKER) {
    vitem_t *v = find_joker_v(id);
    if (v) v->juice = 12;
  } else if (tgt == TG_CONS) {
    vitem_t *v = find_cons_v(id);
    if (v) v->juice = 12;
  } else if (id < MAXCARDS)
    V.cards[id].juice = 12;
}

/* ------------------------------------------------------------ playback */
static void remove_from_list(uint8_t *arr, uint8_t *n, int ci) {
  for (int i = 0; i < *n; i++)
    if (arr[i] == ci) {
      for (int k = i; k < *n - 1; k++) arr[k] = arr[k + 1];
      (*n)--;
      return;
    }
}

static void play_event(ev_t *e) {
  float sp = speed_factor();
  switch (e->type) {
    case EV_DELAY: wait_ms = (int)(e->a * 100 / sp); break;
    case EV_HUD: {
      double m;
      memcpy(&m, e->txt, sizeof m);
      V.chips = ev_getd(e), V.mult = m;
      break;
    }
    case EV_HANDNAME:
      if (e->a == 0xFFFF) V.hand_name = -1, V.chips = 0, V.mult = 0, V.flame = 0;
      else V.hand_name = (int16_t)e->a, V.hand_level = e->d;
      break;
    case EV_ROUNDSCORE:
      V.score_target = ev_getd(e);
      wait_ms = (int)(350 / sp);
      break;
    case EV_MONEY: V.money_target = e->p.i; break;
    case EV_HANDS: V.hands = (int16_t)e->a; break;
    case EV_DISCARDS: V.discards = (int16_t)e->a; break;
    case EV_POPUP:
      add_popup(e);
      juice(e->t, e->a);
      wait_ms = (int)(480 / sp);
      break;
    case EV_JUICE: juice(e->t, e->a); break;
    case EV_CARD_MOVE: {
      int ci = e->a;
      vcard_t *v = &V.cards[ci];
      remove_from_list(V.hand, &V.nhand, ci);
      remove_from_list(V.play, &V.nplay, ci);
      if (v->area == A_DECK && V.deck_count > 0 && e->b != A_DECK) V.deck_count--;
      if (v->area != A_DECK && e->b == A_DECK) V.deck_count++;
      if (v->area == A_NONE || v->area == A_DECK) v->x = DECK_X, v->y = DECK_Y;
      v->area = e->b;
      v->down = e->c;
      v->raise = 0;
      if (e->b == A_HAND && V.nhand < MAXHAND) V.hand[V.nhand++] = (uint8_t)ci;
      if (e->b == A_PLAY && V.nplay < 8) V.play[V.nplay++] = (uint8_t)ci;
      wait_ms = (int)(70 / sp);
      break;
    }
    case EV_CARD_SET: {
      vcard_t *v = &V.cards[e->a];
      v->rank = e->p.card.rank, v->suit = e->p.card.suit, v->enh = e->p.card.enh;
      v->ed = e->p.card.ed, v->seal = e->p.card.seal, v->flags = e->p.card.flags, v->perma = e->p.card.perma;
      v->juice = 10;
      break;
    }
    case EV_CARD_DESTROY: {
      vcard_t *v = &V.cards[e->a];
      remove_from_list(V.hand, &V.nhand, e->a);
      remove_from_list(V.play, &V.nplay, e->a);
      v->area = A_NONE;
      v->fade = 20;
      v->shatter = e->b;
      wait_ms = (int)(200 / sp);
      break;
    }
    case EV_CARD_SELECT:
      V.cards[e->a].raise = e->b;
      if (e->b == 2) wait_ms = (int)(60 / sp);
      break;
    case EV_JOKER_ADD: {
      if (V.njokers >= MAXJ + 4) break;
      int pos = e->d < 0 ? V.njokers : (e->d > V.njokers ? V.njokers : e->d);
      for (int i = V.njokers; i > pos; i--) V.jokers[i] = V.jokers[i - 1];
      vitem_t *v = &V.jokers[pos];
      memset(v, 0, sizeof *v);
      v->uid = e->a, v->id = e->b, v->ed = e->c;
      v->x = JOKER_X + JOKER_W / 2 - CW / 2, v->y = 60;
      v->juice = 12;
      V.njokers++;
      wait_ms = (int)(200 / sp);
      break;
    }
    case EV_JOKER_REMOVE:
      for (int i = 0; i < V.njokers; i++)
        if (V.jokers[i].uid == e->a) {
          for (int k = i; k < V.njokers - 1; k++) V.jokers[k] = V.jokers[k + 1];
          V.njokers--;
          break;
        }
      wait_ms = (int)(150 / sp);
      break;
    case EV_JOKER_SET: {
      vitem_t *v = find_joker_v(e->a);
      if (v) v->ed = e->b, v->flags = e->c, v->juice = 12;
      break;
    }
    case EV_CONS_ADD: {
      if (V.ncons >= MAXCONS + 2) break;
      vitem_t *v = &V.cons[V.ncons++];
      memset(v, 0, sizeof *v);
      v->uid = e->a, v->id = e->b, v->ed = e->c;
      v->x = CONS_X + CONS_W / 2 - CW / 2, v->y = 60;
      v->juice = 12;
      wait_ms = (int)(150 / sp);
      break;
    }
    case EV_CONS_REMOVE:
      for (int i = 0; i < V.ncons; i++)
        if (V.cons[i].uid == e->a) {
          for (int k = i; k < V.ncons - 1; k++) V.cons[k] = V.cons[k + 1];
          V.ncons--;
          break;
        }
      break;
    case EV_LEVELUP:
      levelup_hand = e->a, levelup_level = e->d, levelup_old = e->p.i;
      levelup_len = levelup_t = (int)(1300 / sp);
      V.hand_name = (int16_t)e->a, V.hand_level = e->d;
      if (e->b) {
        int uid = e->c < R.njokers ? R.jokers[e->c].uid : 0;
        juice(TG_JOKER, uid);
      }
      break;
    case EV_MESSAGE: {
      const char *s = e->p.s;
      int i = 0;
      for (; s && s[i] && i < 39; i++) banner[i] = s[i];
      banner[i] = 0;
      banner_t = 1400;
      wait_ms = (int)(1000 / sp);
      break;
    }
    case EV_BLIND_FLASH: V.blind_flash = 16; break;
    case EV_SORT: break;
    case EV_FLAME: V.flame = 1; break;
    case EV_SYNC: break;
  }
}

static float ease(float cur, float target, int dt) {
  float k = dt * 0.012f;
  if (k > 1) k = 1;
  return cur + (target - cur) * k;
}

void ui_update(int dt) {
  float sp = speed_factor();
  /* events */
  if (levelup_t > 0) {
    levelup_t -= dt;
    /* old values first, then the new ones pop in */
    int newer = levelup_t < levelup_len * 6 / 10;
    int save = R.hands[levelup_hand].level;
    R.hands[levelup_hand].level = (int16_t)(newer ? levelup_level : levelup_old);
    V.chips = hand_chips(levelup_hand), V.mult = hand_mult(levelup_hand);
    R.hands[levelup_hand].level = (int16_t)save;
    V.hand_name = (int16_t)levelup_hand;
  }
  if (wait_ms > 0) wait_ms -= dt;
  while (wait_ms <= 0 && levelup_t <= 0 && ev_head < ev_tail) play_event(&evq[ev_head++]);
  if (ev_head >= ev_tail && wait_ms <= 0 && levelup_t <= 0 && ev_tail > 0) {
    ev_head = ev_tail = 0;
    /* everything shown: the display follows the rules again */
    ui_sync_all(0);
    if (R.phase != PH_ROUND) V.hand_name = -1, V.chips = 0, V.mult = 0;
  }
  /* popups and effects */
  for (int i = 0; i < 10; i++)
    if (pops[i].t > 0) pops[i].t -= (int16_t)(dt * (sp > 2 ? 2 : sp));
  if (banner_t > 0) banner_t -= dt;
  if (shake > 0) shake--;
  if (V.blind_flash) V.blind_flash--;
  /* the fire: while chips x mult on display beats the blind */
  flame_update(dt, V.chips * V.mult, R.blind_chips, levelup_t <= 0 && R.phase != PH_PACK && R.phase != PH_SHOP &&
                                                       R.phase != PH_BLIND_SELECT && V.hand_name >= 0,
               R.phase == PH_CASHOUT && !ui_busy());
  /* money and score ease */
  if (V.money != V.money_target) {
    int d = V.money_target - V.money;
    int step = d / 4;
    if (!step) step = d > 0 ? 1 : -1;
    V.money += step;
  }
  if (V.score != V.score_target) {
    double d = V.score_target - V.score;
    V.score += d * (dt * 0.008 > 1 ? 1 : dt * 0.008);
    if (d > -1 && d < 1) V.score = V.score_target;
    if (V.score_target - V.score < V.score_target * 0.002) V.score = V.score_target;
  }
  /* juice timers */
  for (int i = 0; i < V.njokers; i++)
    if (V.jokers[i].juice) V.jokers[i].juice--;
  for (int i = 0; i < V.ncons; i++)
    if (V.cons[i].juice) V.cons[i].juice--;
  for (int i = 0; i < R.ncards_alloc; i++) {
    vcard_t *c = &V.cards[i];
    if (c->juice) c->juice--;
    if (c->fade) c->fade--;
  }
}

/* juice: a quick pop in size */
static int juice_scale(int j) {
  static const int16_t s[13] = {256, 262, 270, 278, 286, 292, 296, 300, 296, 288, 278, 268, 260};
  return j > 0 && j < 13 ? s[12 - j] : 256;
}

/* ------------------------------------------------------------ sidebar */
/* Laid out like the game's HUD (create_UIBox_HUD): one panel down the left
   edge with the blind's colour along its right side; boxes on a 3 pixel grid;
   the two bottom columns end on the same line. */
#define SB_X 3              /* content left */
#define SB_W 97             /* content width */
#define SB_FILL HEXC(0x2F3A3C)  /* BOSS_DARK, as the game shows it */
#define SB_CELL HEXC(0x1B2628)  /* BOSS_MAIN */
#define SB_HAND HEXC(0x182325)
#define SB_BLACK HEXC(0x1D2B2D)
#define SB_EMBOSS HEXC(0x0E181A)

static int shown_phase(void) { return R.phase == PH_PACK ? R.pack_return : R.phase; }

static C blind_main(void) {
  if (shown_phase() == PH_SHOP) return mix565(C_RED, C_BLACK, 3);
  int b = R.phase == PH_ROUND || R.phase == PH_CASHOUT || R.phase == PH_WIN || R.phase == PH_GAMEOVER ? R.blind : -1;
  if (R.phase == PH_PACK) return mix565(C_WHITE, C_BLACK, 3);
  if (b < 0) return C_BLACK;
  if (b == BL_SMALL) return mix565(C_BLACK, C_BLUE, 19);
  if (b == BL_BIG) return mix565(C_BLACK, C_ORANGE, 19);
  return HEXC(blind_info[b].colour);
}

/* pale boss colours (The Water, The Club...) darkened until white labels
   read on them */
C readable(C c, int base) {
  int l = ((c >> 11) * 2 * 77 + ((c >> 5) & 63) * 150 + (c & 31) * 2 * 29) >> 8; /* 0..63 */
  int t = base + (l > 34 ? (l - 34) / 2 : 0);
  return darken(c, t > 20 ? 20 : t);
}

/* a box of the HUD with the game's emboss (a darker edge under it) */
static void cell(int x, int y, int w, int h, C c) {
  g_rrect(x, y + 1, w, h, 3, SB_EMBOSS);
  g_rrect(x, y, w, h, 3, c);
}

/* a number, as large as fits in the width, centred at cx, capitals from y */
static void num_at(double v, int cx, int y, int w, int h, int big, C col, int align) {
  char t[32];
  int f = num_fit(t, v, w, big);
  int yy = y + (h - g_cap(f) + 1) / 2;
  g_text(t, cx, yy, f | T_SHADOW | align, col);
}

void draw_sidebar(void) {
  char t[32];
  C main = blind_main();
  int boss = (R.phase == PH_ROUND || R.phase == PH_CASHOUT) && R.blind >= BL_OX;
  C fill = boss ? mix565(SB_BLACK, main, 7) : SB_FILL;  /* mix(blind, black, 0.2) */
  C cellc = boss ? readable(main, 4) : SB_CELL;
  /* the panel, its dark edge and the blind's colour */
  g_rect(0, 0, SIDE_W - 2, 240, fill);
  g_bg_cover(SIDE_W - 2);
  g_rect(SIDE_W - 2, 0, 1, 240, SB_EMBOSS);
  g_rect(SIDE_W - 1, 0, 1, 240, main);
  int in_round = R.phase == PH_ROUND || R.phase == PH_GAMEOVER || R.phase == PH_WIN ||
                 (R.phase == PH_CASHOUT && ui_busy()); /* the blind leaves once it is beaten */
  /* top: the blind (y 3..64) */
  if (R.phase == PH_CASHOUT && !in_round) {
  } else if (shown_phase() == PH_SHOP) {
    /* the sign in a box framed in red, as in the game */
    g_rrect(SB_X, 4, SB_W, 62, 4, SB_EMBOSS);
    g_rrect(SB_X, 3, SB_W, 62, 4, C_RED);
    g_rrect(SB_X + 2, 5, SB_W - 4, 58, 3, SB_BLACK);
    g_sprite(SPR_SHOP, SB_X + (SB_W - 72) / 2, 7, 0, 256);
    g_text("Improve your run!", SB_X + SB_W / 2, 50, F_LABEL | T_CENTER, C_WHITE);
  } else if (in_round) {
    int b = R.blind;
    cell(SB_X, 3, SB_W, 62, SB_BLACK);
    g_rrect(SB_X + 1, 4, SB_W - 2, 15, 3, readable(main, 0));
    g_text_box(blind_name(b), SB_X + 1, 4, SB_W - 2, 15, text_fit(blind_name(b), SB_W - 8, T_MED) | T_SHADOW, C_WHITE);
    g_rrect(SB_X + 1, 20, SB_W - 2, 44, 3, mix565(main, SB_BLACK, 19)); /* mix(blind, black, 0.4) */
    int flash = V.blind_flash & 2;
    draw_blind_chip(b, SB_X + 4, 29 + (flash ? -2 : 0), 256);
    /* score at least / chips / to earn */
    int bx = SB_X + 31, bw = SB_W - 33;
    cell(bx, 23, bw, 38, SB_BLACK);
    g_text("Score at least", bx + bw / 2, 26, F_LABEL | T_CENTER, C_WHITE);
    char n[24];
    int f = num_fit(n, R.blind_chips, bw - 19, T_LARGE);
    int tw = g_textw(n, f) + 14, x0 = bx + (bw - tw) / 2;
    g_sprite(SPR_CHIP, x0, 36, 0, 256);
    g_text(n, x0 + 14, 36 + (11 - g_cap(f) + 1) / 2, f | T_SHADOW, C_RED);
    label_dollars("to earn", blind_info[b].dollars, bx + bw / 2, 51, bw - 4);
  } else {
    cell(SB_X, 3, SB_W, 62, SB_BLACK);
    g_text_box("Choose your\nnext Blind", SB_X, 3, SB_W, 62, F_NAME, C_WHITE);
  }
  /* round score (y 68..88) */
  cell(SB_X, 68, SB_W, 21, cellc);
  g_text("Round\nscore", SB_X + 13, 70, F_LABEL | T_CENTER, C_WHITE);
  g_rrect(SB_X + 25, 70, SB_W - 27, 17, 3, fill);
  {
    char n[24];
    int f = num_fit(n, V.score, SB_W - 27 - 20, T_LARGE);
    int tw = g_textw(n, f) + 14, x0 = SB_X + 25 + (SB_W - 27 - tw) / 2;
    g_sprite(SPR_CHIP, x0, 73, 0, 256);
    g_text(n, x0 + 14, 73 + (11 - g_cap(f) + 1) / 2, f | T_SHADOW, C_WHITE);
  }
  /* poker hand, chips x mult (y 92..139) */
  int hy = 92;
  cell(SB_X, hy, SB_W, 48, SB_HAND);
  if (V.hand_name >= 0) {
    int lv = levelup_t > 0 ? (levelup_t < levelup_len * 6 / 10 ? levelup_level : levelup_old) : V.hand_level;
    const char *hn = hand_names[V.hand_name];
    char *o = str_cat(t, "lvl.");
    fmt_int(o, lv);
    int lw = g_textw(t, F_LABEL);
    /* long names are narrower, as in the game (hand_text_UI_set) */
    int fl = text_fit(hn, SB_W - 6 - 3 - lw, T_MED);
    int total = g_textw(hn, fl) + 3 + lw;
    int x0 = SB_X + (SB_W - total) / 2, base = hy + 5 + 9; /* capitals end on one line */
    g_text(hn, x0, base - g_cap(fl), fl | T_SHADOW, C_WHITE);
    g_text(t, x0 + total - lw, base - 7, F_LABEL, levelup_t > 0 ? C_ATTN : C_WHITE);
  }
  int by = hy + 21, bh = 21, cw = 42;
  int cx0 = SB_X + 2, mx0 = SB_X + SB_W - 2 - cw;
  cell(cx0, by, cw, bh, C_BLUE);
  cell(mx0, by, cw, bh, C_RED);
  /* the flames stand on the boxes (the game's 2.5 unit sprite, bottom
     aligned) and rise a little over the panel's top edge, as in the game */
  g_clip(SB_X - 2, hy - 5, SB_W + 4, 48 + 5);
  g_flame(0, cx0 + cw / 2 - 27, hy - 5, 54, by + bh - hy + 5, hy - 5);
  g_flame(1, mx0 + cw / 2 - 27, hy - 5, 54, by + bh - hy + 5, hy - 5);
  g_noclip();
  num_at(V.chips, cx0 + cw - 2, by, cw - 4, bh, T_LARGE, C_WHITE, T_RIGHT);
  g_text("X", SB_X + SB_W / 2, by + 5, T_LARGE | T_CENTER | T_SHADOW, C_RED);
  num_at(V.mult, mx0 + 2, by, cw - 4, bh, T_LARGE, C_WHITE, 0);
  /* buttons and counters (y 143..235): two columns ending on one line */
  int y0 = 143, lw = 30, rx = SB_X + lw + 2, rw = SB_W - lw - 2, hw = 30, dw = rw - hw - 2;
  cell(SB_X, y0, lw, 45, C_RED);
  g_text_box("Run\nInfo", SB_X, y0, lw, 45, F_LABEL, C_WHITE);
  cell(SB_X, y0 + 48, lw, 45, C_ORANGE);
  g_text_box("Options", SB_X, y0 + 48, lw, 45, F_LABEL, C_WHITE);
  static const char *const lab[4] = {"Hands", "Discards", "Ante", "Round"};
  for (int k = 0; k < 4; k++) {
    int x = k & 1 ? rx + hw + 2 : rx, w = k & 1 ? dw : hw, y = k < 2 ? y0 : y0 + 64;
    cell(x, y, w, 29, cellc);
    g_text(lab[k], x + w / 2, y + 2, F_LABEL | T_CENTER, C_WHITE);
    g_rrect(x + 2, y + 12, w - 4, 15, 3, fill);
    if (k == 2) {
      /* ante: "2" and a small "/8" */
      fmt_int(t, R.ante);
      char w8[8];
      fmt_int(str_cat(w8, "/"), WIN_ANTE);
      int aw = g_textw(t, T_LARGE), sw = g_textw(w8, F_LABEL), ax = x + (w - aw - sw - 1) / 2;
      g_text(t, ax, y + 14, F_VALUE, C_ATTN);
      g_text(w8, ax + aw + 1, y + 18, F_LABEL, C_WHITE);
    } else {
      int v = k == 0 ? V.hands : k == 1 ? V.discards : R.round;
      num_at(v, x + w / 2, y + 12, w - 6, 15, T_LARGE, k == 0 ? C_BLUE : k == 1 ? C_RED : C_ATTN, T_CENTER);
    }
  }
  cell(rx, y0 + 32, rw, 29, cellc);
  g_rrect(rx + 2, y0 + 34, rw - 4, 25, 3, fill);
  /* tags waiting to be used, small, at the left of the money box */
  int nt = R.ntags < 3 ? R.ntags : 3, tx = rx + 4;
  for (int i = 0; i < nt; i++, tx += 11) g_sprite(SPR_T + R.tags[i], tx - 2, y0 + 39, 0, 192);
  if (nt) tx += 3;
  else tx = rx + 2;
  fmt_money(t, V.money);
  int mw = rx + rw - 2 - tx;
  g_text_box(t, tx, y0 + 34, mw, 25, text_fit(t, mw - 6, T_LARGE) | T_SHADOW, V.money < 0 ? C_RED : C_MONEY);
}

/* ------------------------------------------------------------ areas */
int ui_hand_x(int i, int n) {
  int step = n > 1 ? (HAND_W - CW) / (n - 1) : 0;
  if (step > CW + 2) step = CW + 2;
  int total = step * (n - 1) + CW;
  return HAND_X + (HAND_W - total) / 2 + i * step;
}

void layout_cards(int dt, int show_hand, int hand_y) {
  /* targets */
  for (int i = 0; i < V.nhand; i++) {
    vcard_t *c = &V.cards[V.hand[i]];
    int x = ui_hand_x(i, V.nhand);
    int arc = (2 * i - (V.nhand - 1)) * (2 * i - (V.nhand - 1)) / 16;
    c->tx = (int16_t)x;
    c->ty = (int16_t)(hand_y + arc / 2 - (c->raise ? 12 : 0) + (show_hand ? 0 : 90));
  }
  for (int i = 0; i < V.nplay; i++) {
    vcard_t *c = &V.cards[V.play[i]];
    int step = CW + 3, total = step * (V.nplay - 1) + CW;
    c->tx = (int16_t)(SIDE_W + (320 - SIDE_W - total) / 2 + i * step);
    c->ty = (int16_t)(PLAY_Y - (c->raise == 2 ? 10 : 0));
  }
  for (int i = 0; i < R.ncards_alloc; i++) {
    vcard_t *c = &V.cards[i];
    if (c->area == A_DECK) c->tx = DECK_X, c->ty = DECK_Y;
    if (c->area == A_DISCARD) c->tx = 330, c->ty = HAND_Y - 20;
    if (c->area == A_HAND || c->area == A_PLAY || c->area == A_DECK || c->area == A_DISCARD) {
      c->x = ease(c->x, c->tx, dt);
      c->y = ease(c->y, c->ty, dt);
    }
  }
  /* jokers */
  int n = V.njokers;
  for (int i = 0; i < n; i++) {
    int step = n > 1 ? (JOKER_W - CW) / (n - 1) : 0;
    if (step > CW + 2) step = CW + 2;
    int total = step * (n - 1) + CW;
    int x = JOKER_X + (JOKER_W - total) / 2 + i * step;
    V.jokers[i].x = ease(V.jokers[i].x, x, dt);
    V.jokers[i].y = ease(V.jokers[i].y, ROW_Y, dt);
  }
  n = V.ncons;
  for (int i = 0; i < n; i++) {
    int step = n > 1 ? (CONS_W - CW) / (n - 1) : 0;
    if (step > CW + 2) step = CW + 2;
    int total = step * (n - 1) + CW;
    int x = CONS_X + (CONS_W - total) / 2 + i * step;
    V.cons[i].x = ease(V.cons[i].x, x, dt);
    V.cons[i].y = ease(V.cons[i].y, ROW_Y, dt);
  }
}

/* focus: which zone and index (screens set these) */
int focus_zone = 0, focus_idx = 0, focus_shake;
enum { FZ_HAND = 1, FZ_JOKERS, FZ_CONS };

void draw_jokers_row(int focus_j, int focus_c, int sel_j, int sel_c) {
  char t[8];
  g_shade(JOKER_X - 2, ROW_Y - 2, JOKER_W + 4, CH + 4, 5, 9);
  g_shade(CONS_X - 2, ROW_Y - 2, CONS_W + 4, CH + 4, 5, 9);
  char *o = fmt_int(t, R.njokers);
  *o++ = '/';
  fmt_int(o, joker_slots());
  if (!popup_over(JOKER_X - 1, ROW_Y + CH + 3, JOKER_X + g_textw(t, F_LABEL) + 1, ROW_Y + CH + 12))
    g_text(t, JOKER_X, ROW_Y + CH + 4, F_LABEL, C_WHITE);
  o = fmt_int(t, R.ncons);
  *o++ = '/';
  fmt_int(o, cons_slots());
  if (!popup_over(CONS_X + CONS_W - g_textw(t, F_LABEL) - 1, ROW_Y + CH + 3, CONS_X + CONS_W + 1, ROW_Y + CH + 12))
    g_text(t, CONS_X + CONS_W, ROW_Y + CH + 4, F_LABEL | T_RIGHT, C_WHITE);
  for (int pass = 0; pass < 2; pass++)
    for (int i = 0; i < V.njokers; i++) {
      vitem_t *j = &V.jokers[i];
      int f = i == focus_j;
      if (f != pass) continue;
      int lift = f ? 3 : 0;
      if (i == sel_j) lift = 7;
      int fl = (j->flags & JF_DEBUFF) ? FX_DIM : 0;
      if (j->flags & JF_DOWN) fl |= 0x100;
      int sx = f ? focus_shake : 0;
      draw_joker_sprite(j->id, j->ed, (int)j->x + sx, (int)j->y - lift, juice_scale(j->juice), fl);
      if (j->flags & JF_DEBUFF) g_sprite(SPR_DEBUFF, (int)j->x + sx, (int)j->y - lift, 0, 256);
    }
  for (int pass = 0; pass < 2; pass++)
    for (int i = 0; i < V.ncons; i++) {
      vitem_t *c = &V.cons[i];
      int f = i == focus_c;
      if (f != pass) continue;
      int lift = f ? 3 : 0;
      if (i == sel_c) lift = 7;
      draw_cons_sprite(c->id, c->ed, (int)c->x + (f ? focus_shake : 0), (int)c->y - lift, juice_scale(c->juice), 0);
    }
}

void draw_hand_cards(int focus_h) {
  for (int pass = 0; pass < 2; pass++)
    for (int i = 0; i < V.nhand; i++) {
      int f = i == focus_h;
      if (f != pass) continue;
      vcard_t *c = &V.cards[V.hand[i]];
      int x = (int)c->x + (f ? focus_shake : 0), y = (int)c->y - (f ? 4 : 0), sc = juice_scale(c->juice);
      if (f && sc == 256 && !V.nplay) g_rrect(x - 2, y - 2, CW + 4, CH + 4, 4, C_FOCUS); /* the D-pad cursor */
      draw_card(c, x, y, sc, f);
    }
}

void draw_play_cards(void) {
  for (int i = 0; i < V.nplay; i++) {
    vcard_t *c = &V.cards[V.play[i]];
    draw_card(c, (int)c->x, (int)c->y, juice_scale(c->juice), 0);
  }
  /* cards being destroyed */
  for (int i = 0; i < R.ncards_alloc; i++) {
    vcard_t *c = &V.cards[i];
    if (c->area != A_NONE || !c->fade) continue;
    if (c->shatter) {
      /* glass breaking: pieces flying out */
      int k = 20 - c->fade;
      for (int p = 0; p < 6; p++) {
        int px = (int)c->x + 6 + (p % 3) * 10 + (p % 3 - 1) * k, py = (int)c->y + 8 + (p / 3) * 18 + k * k / 6;
        g_rrect(px, py, 7, 9, 1, HEXC(0xC9E6EE));
      }
    } else {
      /* dissolving */
      int sc = 256 - (20 - c->fade) * 6;
      draw_card(c, (int)c->x, (int)c->y, sc, 0);
      if (c->fade < 12) g_sprite(SPR_E + 1, (int)c->x, (int)c->y, FX_WHITE, sc);
    }
  }
}

void draw_deck_pile(int focused) {
  char t[12];
  int n = V.deck_count;
  if (n > 0) {
    int layers = n > 40 ? 3 : n > 20 ? 2 : 1;
    for (int k = layers - 1; k >= 0; k--) {
      g_sprite(SPR_E, DECK_X + k + 1, DECK_Y + k + 2, FX_SHADOW, 256);
      g_sprite(SPR_E, DECK_X + k, DECK_Y - k - (focused ? 3 : 0), 0, 256);
    }
  }
  int total = 0;
  for (int i = 0; i < R.ncards_alloc; i++) total += (R.cards[i].flags & CF_USED) != 0;
  char *o = fmt_int(t, n);
  *o++ = '/';
  fmt_int(o, total);
  g_text(t, DECK_X + CW, DECK_Y + CH + 5, F_LABEL | T_RIGHT, C_WHITE);
}

/* where popup i is this frame: centre, text size, square's half side */
static void popup_geo(const popup_t *p, int *x, int *y, int *f, int *hs) {
  int age = p->life - p->t;
  target_pos(p->tgt, p->id, x, y);
  *y -= age > 500 ? (age - 500) / 40 : 0;
  /* chips and money in the large size, mult and messages a size down (0.7) */
  *f = p->col == PC_CHIPS || p->col == PC_MONEY ? T_LARGE : T_MED;
  *hs = (age < 150 ? 11 - age / 38 : 7) - (*f == T_MED);
  int tw = g_textw(p->txt, *f);
  if (*x < AREA_X + tw / 2) *x = AREA_X + tw / 2;
  if (*x > 318 - tw / 2) *x = 318 - tw / 2;
}

/* a popup covers part of this box: the counters under the rows make way */
static int popup_over(int x0, int y0, int x1, int y1) {
  for (int i = 0; i < 10; i++) {
    const popup_t *p = &pops[i];
    if (p->t <= 0) continue;
    int x, y, f, hs;
    popup_geo(p, &x, &y, &f, &hs);
    int hw = g_textw(p->txt, f) / 2 + 2, hh = g_cap(f) / 2 + 3;
    if (hs * 3 / 2 + 1 > hw) hw = hs * 3 / 2 + 1;
    if (hs * 3 / 2 + 1 > hh) hh = hs * 3 / 2 + 1;
    if (x + hw > x0 && x - hw < x1 && y + hh > y0 && y - hh < y1) return 1;
  }
  return 0;
}

/* the game's score texts: big white text over a small spinning square of
   the effect's colour (attention_text with a backdrop) */
void draw_popups(void) {
  for (int i = 0; i < 10; i++) {
    popup_t *p = &pops[i];
    if (p->t <= 0) continue;
    int age = p->life - p->t;
    int x, y, f, hs;
    popup_geo(p, &x, &y, &f, &hs);
    C col = popup_col[p->col % 11];
    if (p->col == PC_EDITION) col = HEXC(0x4CA893);
    g_rsquare(x, y, hs, age / 6, col);
    g_text(p->txt, x, y - g_cap(f) / 2, f | T_SHADOW | T_CENTER, C_WHITE);
  }
  if (banner_t > 0) {
    int x = AREA_X + AREA_W / 2, w = g_textw(banner, T_LARGE) + 20;
    panel(x - w / 2, 86, w, 21, 5, C_RED, 2);
    g_text_box(banner, x - w / 2, 86, w, 21, F_VALUE, C_WHITE);
  }
}

/* level up display (planets, Space Joker, Burnt Joker, Orbital Tag) */
int ui_levelup_active(void) { return levelup_t > 0; }
