/* Drawing the game's pieces: panels and buttons in Balatro's style, cards
 * with their enhancements, seals and editions, and the info boxes. */
#include <string.h>
#include "assets.h"
#include "jokers.h"
#include "ui.h"
#include "util.h"

const C popup_col[] = {C_BLUE, C_RED, C_RED, C_MONEY, C_ATTN, C_GREEN, HEXC(0x4CA893), C_TAROT, C_PLANET,
                       C_SPECTRAL, C_GREY};

void panel(int x, int y, int w, int h, int r, C c, int emboss) {
  if (emboss) g_rrect(x, y + emboss, w, h, r, darken(c, 13));
  g_rrect(x, y, w, h, r, c);
}

/* a calculator key under a button: small text on a light key */
void keycap(int x, int y, const char *label) {
  int w = g_textw(label, T_SMALL) + 6;
  g_rrect(x - w / 2, y + 1, w, 10, 2, HEXC(0x9AA8AB));
  g_rrect(x - w / 2, y, w, 10, 2, HEXC(0xE8EEEF));
  g_text(label, x - w / 2 + 3, y + 1, T_SMALL, HEXC(0x3A4749));
}

/* a Balatro button: colour, emboss, white outline when focused; the label is
   centred by its capitals. Text size by the caller (F_LABEL, F_NAME...) */
void button(int x, int y, int w, int h, C c, const char *label, int flags, int focused, int enabled) {
  if (!enabled) c = C_INACTIVE;
  if (focused) x += focus_shake;
  if (focused) g_rrect(x - 2, y - 2, w + 4, h + 5, 5, C_WHITE);
  panel(x, y, w, h, 4, c, 2);
  g_text_box(label, x, y, w, h, flags | T_SHADOW, enabled ? C_WHITE : HEXC(0xBBBBBB));
}

/* Balatro keeps its numbers big and only shrinks long ones: large, large
   with condensed digits, medium, condensed medium, small, then a shorter e
   notation */
int text_fit(const char *t, int maxw, int big) {
  /* one step at a time: a pixel narrower, then two pixels lower, ... */
  static const uint16_t steps[5] = {T_LARGE, T_LARGE | T_COND, T_MED, T_MED | T_COND, T_SMALL};
  int i = big & T_LARGE ? 0 : big & T_MED ? 2 : 4;
  for (; i < 5; i++)
    if (g_textw(t, steps[i]) <= maxw) return steps[i];
  return T_SMALL;
}

int num_fit(char *t, double v, int maxw, int big) {
  fmt_commas(t, v);
  int f = text_fit(t, maxw, big);
  if (g_textw(t, f) <= maxw || v < 1000) return f;
  /* 1.2e9 */
  int e = 0;
  double m = v < 0 ? -v : v;
  while (m >= 10) m /= 10, e++;
  for (int dec = 2; dec >= 0; dec--) {
    char *o = t;
    if (v < 0) *o++ = '-';
    long r = (long)(m * (dec == 2 ? 100 : dec == 1 ? 10 : 1) + 0.5);
    int ee = e;
    if (r >= (dec == 2 ? 1000 : dec == 1 ? 100 : 10)) r /= 10, ee++;
    o = fmt_int(o, dec == 2 ? r / 100 : dec == 1 ? r / 10 : r);
    if (dec) {
      *o++ = '.';
      if (dec == 2) *o++ = (char)('0' + r / 10 % 10);
      *o++ = (char)('0' + r % 10);
    }
    *o++ = 'e';
    fmt_int(o, ee);
    f = text_fit(t, maxw, big);
    if (g_textw(t, f) <= maxw) return f;
  }
  return T_SMALL;
}

/* "to earn $$$$" / "Reward: $$$$", centred at cx; the gap before the
   dollars closes up when the row is long (the final bosses' eight) */
void label_dollars(const char *label, int n, int cx, int y, int maxw) {
  char d[12];
  int i = 0;
  for (; i < n && i < 10; i++) d[i] = '$';
  d[i] = 0;
  int lw = g_textw(label, F_LABEL), dw = g_textw(d, F_LABEL), gap = 4;
  while (gap > 2 && lw + gap + dw > maxw) gap--;
  int x = cx - (lw + gap + dw) / 2;
  g_text(label, x, y, F_LABEL, C_WHITE);
  g_text(d, x + lw + gap, y, F_LABEL, C_MONEY);
}

void fmt_money(char *o, int v) {
  if (v < 0) *o++ = '-', v = -v;
  *o++ = '$';
  fmt_int(o, v);
}

/* the price tag over a shop item, x its centre */
void draw_price(int x, int y, int cost, int affordable) {
  char t[12];
  fmt_money(t, cost);
  int w = g_textw(t, T_LARGE) + 10;
  panel(x - w / 2, y, w, 15, 4, C_BLACK, 1);
  g_text_box(t, x - w / 2, y, w, 15, F_VALUE, affordable ? C_MONEY : C_RED);
}

static const uint8_t enh_sprite[9] = {1, 4, 5, 6, 8, 9, 2, 3, 7};
static const uint8_t suit_row[4] = {3, 0, 1, 2};

void draw_card_back(int x, int y, int scale) { g_sprite(SPR_E, x, y, 0, scale); }

void draw_card(const vcard_t *c, int x, int y, int scale, int focused) {
  (void)focused;
  if (c->down) {
    g_sprite(SPR_E, x + 1, y + 2, FX_SHADOW, scale);
    draw_card_back(x, y, scale);
    return;
  }
  int fx = c->ed & 7;
  if (c->flags & CF_DEBUFF) fx |= FX_DIM;
  g_sprite(SPR_E + enh_sprite[c->enh], x + 1, y + 2, FX_SHADOW, scale);
  g_sprite(SPR_E + enh_sprite[c->enh], x, y, fx, scale);
  if (c->enh != E_STONE) g_sprite(SPR_F + suit_row[c->suit & 3] * 13 + c->rank - 2, x, y, fx, scale);
  if (c->seal) g_sprite(SPR_SEAL + c->seal - 1, x, y, 0, scale);
  if (c->flags & CF_DEBUFF) g_sprite(SPR_DEBUFF, x, y, 0, scale);
}

static const uint8_t soul_of[6] = {J_HOLOGRAM, J_CAINO, J_TRIBOULET, J_YORICK, J_CHICOT, J_PERKEO};

void draw_joker_sprite(int id, int ed, int x, int y, int scale, int flags) {
  int fx = (ed & 7) | (flags & (FX_DIM | FX_GREY));
  int spr = SPR_J + id;
  const art_sprite_t *s = &art_sprites[spr];
  if (flags & 0x100) { /* face down */
    g_sprite(SPR_E, x + 1, y + 2, FX_SHADOW, scale);
    g_sprite(SPR_E, x, y, 0, scale);
    return;
  }
  int ox = (CW - s->w) / 2, oy = id == J_WEE ? (CH - s->h) / 2 : 0;
  g_sprite(spr, x + ox + 1, y + oy + 2, FX_SHADOW, scale);
  g_sprite(spr, x + ox, y + oy, fx, scale);
  for (int k = 0; k < 6; k++)
    if (soul_of[k] == id) {
      /* the floating face of the legendary jokers (and Hologram) */
      int bob = (int)((g_time / 90) % 16);
      bob = bob < 8 ? bob / 3 : (16 - bob) / 3;
      g_sprite(SPR_JSOUL + k, x, y - 1 - bob, fx & ~FX_EDMASK, scale);
    }
}

void draw_cons_sprite(int id, int ed, int x, int y, int scale, int flags) {
  int fx = (ed & 7) | (flags & (FX_DIM | FX_GREY));
  g_sprite(SPR_C + id, x + 1, y + 2, FX_SHADOW, scale);
  g_sprite(SPR_C + id, x, y, fx, scale);
  if (id == C_SOUL) {
    int bob = (int)((g_time / 90) % 16);
    bob = bob < 8 ? bob / 3 : (16 - bob) / 3;
    g_sprite(SPR_SOULGEM, x, y - 1 - bob, 0, scale);
  }
}

int item_sprite(const sitem_t *it) {
  switch (it->kind) {
    case IT_JOKER: return SPR_J + it->id;
    case IT_CONS: return SPR_C + it->id;
    case IT_VOUCHER: return SPR_V + it->id;
    case IT_PACK: return SPR_P + it->id;
  }
  return SPR_E + 1;
}

void draw_item(const sitem_t *it, int x, int y, int scale, int flags) {
  switch (it->kind) {
    case IT_JOKER: draw_joker_sprite(it->id, it->ed, x, y, scale, flags); break;
    case IT_CONS: draw_cons_sprite(it->id, it->ed, x, y, scale, flags); break;
    case IT_CARD: {
      vcard_t v = {0};
      v.rank = it->card.rank, v.suit = it->card.suit, v.enh = it->card.enh, v.ed = it->card.ed, v.seal = it->card.seal;
      draw_card(&v, x, y, scale, 0);
      break;
    }
    default:
      g_sprite(item_sprite(it), x + 1, y + 2, FX_SHADOW, scale);
      g_sprite(item_sprite(it), x, y, flags & (FX_DIM | FX_GREY), scale);
  }
}

void draw_blind_chip(int b, int x, int y, int scale) {
  g_sprite(SPR_B + b, x + 1, y + 1, FX_SHADOW, scale);
  g_sprite(SPR_B + b, x, y, 0, scale);
}

/* ------------------------------------------------------------ descriptions */
static char vars[6][24];

static void v_int(int k, long v) { fmt_int(vars[k], v); }
static void v_num(int k, double v) { fmt_short(vars[k], v); }
static void v_str(int k, const char *s) {
  int i = 0;
  for (; s[i] && i < 23; i++) vars[k][i] = s[i];
  vars[k][i] = 0;
}
static const char *hand_name(int h) { return hand_names[h]; }

static int count_used(int enh, int rank9) {
  int n = 0;
  for (int i = 0; i < R.ncards_alloc; i++) {
    if (!(R.cards[i].flags & CF_USED)) continue;
    if (rank9 ? (R.cards[i].enh != E_STONE && R.cards[i].rank == 9) : (enh < 0 ? R.cards[i].enh != E_NONE : R.cards[i].enh == enh)) n++;
  }
  return n;
}

static void joker_vars(int id, const joker_t *j) {
  double p = probs_normal();
  double x = j ? j->x : 1;
  int a = j ? j->a : 0;
  switch (id) {
    case J_JOKER: v_int(0, 4); break;
    case J_GREEDY_JOKER: case J_LUSTY_JOKER: case J_WRATHFUL_JOKER: case J_GLUTTENOUS_JOKER: {
      static const char *const s[4] = {"Diamond", "Heart", "Spade", "Club"};
      v_int(0, 3);
      v_str(1, s[id - J_GREEDY_JOKER]);
      break;
    }
    case J_JOLLY: v_int(0, 8), v_str(1, hand_name(H_PAIR)); break;
    case J_ZANY: v_int(0, 12), v_str(1, hand_name(H_THREE_KIND)); break;
    case J_MAD: v_int(0, 10), v_str(1, hand_name(H_TWO_PAIR)); break;
    case J_CRAZY: v_int(0, 12), v_str(1, hand_name(H_STRAIGHT)); break;
    case J_DROLL: v_int(0, 10), v_str(1, hand_name(H_FLUSH)); break;
    case J_SLY: v_int(0, 50), v_str(1, hand_name(H_PAIR)); break;
    case J_WILY: v_int(0, 100), v_str(1, hand_name(H_THREE_KIND)); break;
    case J_CLEVER: v_int(0, 80), v_str(1, hand_name(H_TWO_PAIR)); break;
    case J_DEVIOUS: v_int(0, 100), v_str(1, hand_name(H_STRAIGHT)); break;
    case J_CRAFTY: v_int(0, 80), v_str(1, hand_name(H_FLUSH)); break;
    case J_HALF: v_int(0, 20), v_int(1, 3); break;
    case J_STENCIL: {
      double s;
      joker_t t = {0};
      t.id = J_STENCIL;
      joker_x_display(&t, &s);
      v_num(0, j ? s : 1);
      break;
    }
    case J_CREDIT_CARD: v_int(0, 20); break;
    case J_CEREMONIAL: v_int(0, a); break;
    case J_BANNER: v_int(0, 30); break;
    case J_MYSTIC_SUMMIT: v_int(0, 15), v_int(1, 0); break;
    case J_LOYALTY_CARD: {
      v_int(0, 4), v_int(1, 5);
      int rem = j ? (5 - 1 - (R.hands_played - j->b)) % 6 : 5;
      if (rem < 0) rem += 6;
      if (rem == 0) v_str(2, "Active!");
      else {
        char *o = fmt_int(vars[2], rem);
        str_cat(o, " remaining");
      }
      break;
    }
    case J_8_BALL: v_num(0, p), v_int(1, 4); break;
    case J_DUSK: v_int(0, 2); break;
    case J_CHAOS: v_int(0, 1); break;
    case J_FIBONACCI: v_int(0, 8); break;
    case J_STEEL_JOKER: v_num(0, 0.2), v_num(1, 1 + 0.2 * count_used(E_STEEL, 0)); break;
    case J_SCARY_FACE: v_int(0, 30); break;
    case J_ABSTRACT: v_int(0, 3), v_int(1, 3 * R.njokers); break;
    case J_DELAYED_GRAT: v_int(0, 2); break;
    case J_HACK: v_int(0, 2); break;
    case J_GROS_MICHEL: v_int(0, 15), v_num(1, p), v_int(2, 6); break;
    case J_EVEN_STEVEN: v_int(0, 4); break;
    case J_ODD_TODD: v_int(0, 31); break;
    case J_SCHOLAR: v_int(0, 4), v_int(1, 20); break;
    case J_BUSINESS: v_num(0, p), v_int(1, 2); break;
    case J_RIDE_THE_BUS: v_int(0, 1), v_int(1, a); break;
    case J_SPACE: v_num(0, p), v_int(1, 4); break;
    case J_EGG: v_int(0, 3); break;
    case J_BURGLAR: v_int(0, 3); break;
    case J_BLACKBOARD: v_int(0, 3), v_str(1, "Spades"), v_str(2, "Clubs"); break;
    case J_RUNNER: v_int(0, a), v_int(1, 15); break;
    case J_ICE_CREAM: v_int(0, j ? a : 100), v_int(1, 5); break;
    case J_BLUE_JOKER: v_int(0, 2), v_int(1, 2 * (R.phase == PH_ROUND ? R.ndeck : 52)); break;
    case J_CONSTELLATION: v_num(0, 0.1), v_num(1, x); break;
    case J_HIKER: v_int(0, 5); break;
    case J_FACELESS: v_int(0, 5), v_int(1, 3); break;
    case J_GREEN_JOKER: v_int(0, 1), v_int(1, 1), v_int(2, a); break;
    case J_TODO_LIST: v_int(0, 4), v_str(1, hand_name(j ? j->c : H_HIGH_CARD)); break;
    case J_CAVENDISH: v_int(0, 3), v_num(1, p), v_int(2, 1000); break;
    case J_CARD_SHARP: v_int(0, 3); break;
    case J_RED_CARD: v_int(0, 3), v_int(1, a); break;
    case J_MADNESS: v_num(0, 0.5), v_num(1, x); break;
    case J_SQUARE: v_int(0, a), v_int(1, 4); break;
    case J_SEANCE: v_str(0, hand_name(H_STRAIGHT_FLUSH)); break;
    case J_RIFF_RAFF: v_int(0, 2); break;
    case J_VAMPIRE: v_num(0, 0.1), v_num(1, x); break;
    case J_HOLOGRAM: v_num(0, 0.25), v_num(1, x); break;
    case J_VAGABOND: v_int(0, 4); break;
    case J_BARON: v_num(0, 1.5); break;
    case J_CLOUD_9: v_int(0, 1), v_int(1, count_used(0, 1)); break;
    case J_ROCKET: v_int(0, j ? a : 1), v_int(1, 2); break;
    case J_OBELISK: v_num(0, 0.2), v_num(1, x); break;
    case J_PHOTOGRAPH: v_int(0, 2); break;
    case J_GIFT: v_int(0, 1); break;
    case J_TURTLE_BEAN: v_int(0, j ? a : 5), v_int(1, 1); break;
    case J_EROSION: {
      int n = 0;
      for (int i = 0; i < R.ncards_alloc; i++) n += (R.cards[i].flags & CF_USED) != 0;
      int d = R.starting_deck - n;
      v_int(0, 4), v_int(1, d > 0 ? 4 * d : 0), v_int(2, R.starting_deck);
      break;
    }
    case J_RESERVED_PARKING: v_int(0, 1), v_num(1, p), v_int(2, 2); break;
    case J_MAIL: v_int(0, 5), v_str(1, rank_names[R.mail_rank]); break;
    case J_TO_THE_MOON: v_int(0, 1); break;
    case J_HALLUCINATION: v_num(0, p), v_int(1, 2); break;
    case J_FORTUNE_TELLER: v_int(0, 1), v_int(1, R.tarots_used); break;
    case J_JUGGLER: v_int(0, 1); break;
    case J_DRUNKARD: v_int(0, 1); break;
    case J_STONE: v_int(0, 25), v_int(1, 25 * count_used(E_STONE, 0)); break;
    case J_GOLDEN: v_int(0, 4); break;
    case J_LUCKY_CAT: v_num(0, 0.25), v_num(1, x); break;
    case J_BASEBALL: v_num(0, 1.5); break;
    case J_BULL: v_int(0, 2), v_int(1, 2 * (R.money > 0 ? R.money : 0)); break;
    case J_DIET_COLA: v_str(0, "Double Tag"); break;
    case J_TRADING: v_int(0, 3); break;
    case J_FLASH: v_int(0, 2), v_int(1, a); break;
    case J_POPCORN: v_int(0, j ? a : 20), v_int(1, 4); break;
    case J_TROUSERS: v_int(0, 2), v_str(1, hand_name(H_TWO_PAIR)), v_int(2, a); break;
    case J_ANCIENT: v_num(0, 1.5), v_str(1, suit_names[R.ancient_suit]); break;
    case J_RAMEN: v_num(0, j ? x : 2), v_num(1, 0.01); break;
    case J_WALKIE_TALKIE: v_int(0, 10), v_int(1, 4); break;
    case J_SELZER: v_int(0, j ? a : 10); break;
    case J_CASTLE: v_int(0, 3), v_str(1, suit_names[R.castle_suit]), v_int(2, a); break;
    case J_SMILEY: v_int(0, 5); break;
    case J_CAMPFIRE: v_num(0, 0.25), v_num(1, x); break;
    case J_TICKET: v_int(0, 4); break;
    case J_ACROBAT: v_int(0, 3); break;
    case J_SOCK_AND_BUSKIN: v_int(0, 2); break;
    case J_SWASHBUCKLER: {
      int s = 0;
      for (int i = 0; i < R.njokers; i++)
        if (&R.jokers[i] != j) s += joker_sell_value(&R.jokers[i]);
      v_int(0, s);
      break;
    }
    case J_TROUBADOUR: v_int(0, 2), v_int(1, 1); break;
    case J_CERTIFICATE: v_int(0, 1); break;
    case J_THROWBACK: v_num(0, 0.25), v_num(1, 1 + R.skips * 0.25); break;
    case J_HANGING_CHAD: v_int(0, 2); break;
    case J_ROUGH_GEM: v_int(0, 1); break;
    case J_BLOODSTONE: v_num(0, p), v_int(1, 2), v_num(2, 1.5); break;
    case J_ARROWHEAD: v_int(0, 50); break;
    case J_ONYX_AGATE: v_int(0, 7); break;
    case J_GLASS: v_num(0, 0.75), v_num(1, x); break;
    case J_FLOWER_POT: v_int(0, 3); break;
    case J_WEE: v_int(0, a), v_int(1, 8); break;
    case J_MERRY_ANDY: v_int(0, 3), v_int(1, -1); break;
    case J_IDOL: v_num(0, 2), v_str(1, rank_names[R.idol_rank]), v_str(2, suit_names_plural[R.idol_suit]); break;
    case J_SEEING_DOUBLE: v_int(0, 2); break;
    case J_MATADOR: v_int(0, 8); break;
    case J_HIT_THE_ROAD: v_num(0, 0.5), v_num(1, x); break;
    case J_DUO: v_num(0, 2), v_str(1, hand_name(H_PAIR)); break;
    case J_TRIO: v_num(0, 3), v_str(1, hand_name(H_THREE_KIND)); break;
    case J_FAMILY: v_num(0, 4), v_str(1, hand_name(H_FOUR_KIND)); break;
    case J_ORDER: v_num(0, 3), v_str(1, hand_name(H_STRAIGHT)); break;
    case J_TRIBE: v_num(0, 2), v_str(1, hand_name(H_FLUSH)); break;
    case J_STUNTMAN: v_int(0, 250), v_int(1, 2); break;
    case J_INVISIBLE: v_int(0, 2), v_int(1, a); break;
    case J_SATELLITE: {
      int n = 0;
      for (int k = 0; k < 12; k++) n += (R.planets_used_mask >> k) & 1;
      v_int(0, 1), v_int(1, n);
      break;
    }
    case J_SHOOT_THE_MOON: v_int(0, 13); break;
    case J_DRIVERS_LICENSE: v_int(0, 3), v_int(1, count_used(-1, 0)); break;
    case J_ASTRONOMER: break;
    case J_BOOTSTRAPS: v_int(0, 2), v_int(1, 5), v_int(2, 2 * (R.money / 5 > 0 ? R.money / 5 : 0)); break;
    case J_CAINO: v_int(0, 1), v_num(1, x); break;
    case J_TRIBOULET: v_int(0, 2); break;
    case J_YORICK: v_int(0, 1), v_int(1, 23), v_int(2, j ? j->b : 23), v_num(3, x); break;
    case J_PERKEO: v_int(0, 1); break;
    case J_OOPS: case J_PAREIDOLIA: case J_SPLASH: case J_SIXTH_SENSE: case J_SUPERPOSITION: case J_SHORTCUT:
    case J_MIDAS_MASK: case J_LUCHADOR: case J_MR_BONES: case J_SMEARED: case J_RING_MASTER: case J_BLUEPRINT:
    case J_BRAINSTORM: case J_CARTOMANCER: case J_BURNT: case J_CHICOT: case J_DNA: case J_MARBLE: case J_MIME:
    case J_RAISED_FIST: case J_MISPRINT: case J_SUPERNOVA: case J_FOUR_FINGERS:
      break;
  }
}

static const uint8_t planet_hand[12] = {H_PAIR, H_THREE_KIND, H_FULL_HOUSE, H_FOUR_KIND, H_FLUSH, H_STRAIGHT,
                                        H_TWO_PAIR, H_STRAIGHT_FLUSH, H_HIGH_CARD, H_FIVE_KIND, H_FLUSH_HOUSE,
                                        H_FLUSH_FIVE};
static const uint8_t lchips5[12] = {10, 8, 7, 8, 6, 5, 3, 6, 4, 4, 3, 2}, lmult[12] = {3, 4, 3, 4, 3, 2, 2, 3, 2, 1, 1, 1};

static void cons_vars(int id) {
  static const char *const enh[] = {"Lucky Card", "Mult Card", "Bonus Card", "Wild Card", "Steel Card", "Glass Card",
                                    "Gold Card", "Stone Card"};
  switch (id) {
    case C_MAGICIAN: v_int(0, 2), v_str(1, enh[0]); break;
    case C_EMPRESS: v_int(0, 2), v_str(1, enh[1]); break;
    case C_HEIROPHANT: v_int(0, 2), v_str(1, enh[2]); break;
    case C_LOVERS: v_int(0, 1), v_str(1, enh[3]); break;
    case C_CHARIOT: v_int(0, 1), v_str(1, enh[4]); break;
    case C_JUSTICE: v_int(0, 1), v_str(1, enh[5]); break;
    case C_DEVIL: v_int(0, 1), v_str(1, enh[6]); break;
    case C_TOWER: v_int(0, 1), v_str(1, enh[7]); break;
    case C_HIGH_PRIESTESS: case C_EMPEROR: v_int(0, 2); break;
    case C_HERMIT: v_int(0, 20); break;
    case C_WHEEL_OF_FORTUNE: v_num(0, probs_normal()), v_int(1, 4); break;
    case C_STRENGTH: case C_HANGED_MAN: case C_DEATH: v_int(0, 2); break;
    case C_TEMPERANCE: {
      int m = 0;
      for (int i = 0; i < R.njokers; i++) m += joker_sell_value(&R.jokers[i]);
      v_int(0, 50), v_int(1, m > 50 ? 50 : m);
      break;
    }
    case C_STAR: v_int(0, 3), v_str(1, "Diamonds"); break;
    case C_MOON: v_int(0, 3), v_str(1, "Clubs"); break;
    case C_SUN: v_int(0, 3), v_str(1, "Hearts"); break;
    case C_WORLD: v_int(0, 3), v_str(1, "Spades"); break;
    case C_FAMILIAR: v_int(0, 3); break;
    case C_GRIM: v_int(0, 2); break;
    case C_INCANTATION: v_int(0, 4); break;
    case C_ECTOPLASM: v_int(0, R.ecto_minus + 1); break;
    case C_IMMOLATE: v_int(0, 5), v_int(1, 20); break;
    case C_CRYPTID: v_int(0, 2); break;
    default:
      if (id >= C_MERCURY && id <= C_ERIS) {
        int h = planet_hand[id - C_MERCURY];
        v_int(0, R.hands[h].level), v_str(1, hand_name(h)), v_int(2, lmult[h]), v_int(3, lchips5[h] * 5);
      }
  }
}

static void other_vars(int kind, int id, const void *extra) {
  (void)extra;
  if (kind == TK_VOUCHER) {
    static const int16_t v[32] = {0, 25, 2, 2, 0, 0, 1, 1, 2, 2, 50, 0, 0, 1, 10, 1,
                                  0, 50, 4, 2, 0, 0, 1, 1, 4, 4, 100, 0, 0, 1, 10, 1};
    if (id == V_OBSERVATORY) v_num(0, 1.5);
    else v_int(0, v[id]);
  } else if (kind == TK_TAG) {
    const int orb = extra ? *(const int *)extra : -1;
    switch (id) {
      case TAG_INVESTMENT: v_int(0, 25); break;
      case TAG_HANDY: v_int(0, 1), v_int(1, R.hands_played); break;
      case TAG_GARBAGE: v_int(0, 1), v_int(1, R.unused_discards); break;
      case TAG_JUGGLE: v_int(0, 3); break;
      case TAG_TOP_UP: v_int(0, 2); break;
      case TAG_SKIP: v_int(0, 5), v_int(1, (R.skips + 1) * 5); break;
      case TAG_ORBITAL: v_str(0, orb >= 0 ? hand_name(orb) : "[poker hand]"), v_int(1, 3); break;
      case TAG_ECONOMY: v_int(0, 40); break;
    }
  } else if (kind == TK_BLIND) {
    if (id == BL_OX) v_str(0, hand_name(R.most_played));
  } else if (kind == TK_PACK) {
    static const uint8_t n[5][3] = {{3, 5, 5}, {3, 5, 5}, {3, 5, 5}, {2, 4, 4}, {2, 4, 4}};
    v_int(0, id % 3 == 2 ? 2 : 1), v_int(1, n[id / 3][id % 3]);
  } else if (kind == TK_ENH) {
    switch (id) {
      case E_MULT: v_int(0, 4); break;
      case E_GLASS: v_int(0, 2), v_num(1, probs_normal()), v_int(2, 4); break;
      case E_STEEL: v_num(0, 1.5); break;
      case E_STONE: v_int(0, 50); break;
      case E_GOLD: v_int(0, 3); break;
      case E_LUCKY: v_num(0, probs_normal()), v_int(1, 20), v_int(2, 5), v_int(3, 20), v_int(4, 15); break;
    }
  } else if (kind == TK_EDITION) {
    static const int e[5] = {0, 50, 10, 0, 1};
    if (id == ED_POLY) v_num(0, 1.5);
    else v_int(0, e[id]);
  }
}

/* text index of an item's name/description */
static int text_index(int kind, int id) {
  switch (kind) {
    case TK_JOKER: return TXT_JOKER + id;
    case TK_CONS: return TXT_CONS + id;
    case TK_VOUCHER: return TXT_VOUCH + id;
    case TK_TAG: return TXT_TAG + id;
    case TK_BLIND: return TXT_BLIND + id;
    case TK_ENH: return TXT_EXTRA + id - 1;
    case TK_EDITION: return TXT_EXTRA + 8 + id - 1;
    case TK_SEAL: return TXT_EXTRA + 12 + id - 1;
    case TK_PACK: return TXT_EXTRA + 16 + id;
  }
  return 0;
}

static const char *tname(int kind, int id) { return text_get(text_index(kind, id), 0); }

void desc_text(char *out, int maxlen, int kind, int id, const void *extra) {
  for (int k = 0; k < 6; k++) vars[k][0] = 0;
  if (kind == TK_JOKER) joker_vars(id, (const joker_t *)extra);
  else if (kind == TK_CONS) cons_vars(id);
  else other_vars(kind, id, extra);
  const char *s = text_get(text_index(kind, id), 1);
  int n = 0;
  if (kind == TK_JOKER && id == J_MISPRINT) {
    /* the game shows a random number flickering here */
    char *o = str_cat(out, TC_MULT "+");
    o = fmt_int(o, (long)((g_time / 90) * 7 % 24));
    str_cat(o, TC_RESET " Mult");
    return;
  }
  if (kind == TK_BLIND && id == BL_WHEEL) {
    fmt_short(vars[5], probs_normal());
    for (const char *p = vars[5]; *p && n < maxlen - 1; p++) out[n++] = *p;
  }
  for (; *s && n < maxlen - 1; s++) {
    unsigned char c = (unsigned char)*s;
    if (c >= 0x11 && c <= 0x16) {
      for (const char *p = vars[c - 0x11]; *p && n < maxlen - 1; p++) out[n++] = *p;
    } else
      out[n++] = (char)c;
  }
  out[n] = 0;
}

/* ------------------------------------------------------------ info box */
static const char *const rarity_name[5] = {"", "Common", "Uncommon", "Rare", "Legendary"};
static const C rarity_col[5] = {0, HEXC(0x009DFF), HEXC(0x4BC292), HEXC(0xFE5F55), HEXC(0xB26CBB)};

/* the coloured pill under an info box (rarity, set, edition) */
#define BADGE_H 11
static void badge(int cx, int y, const char *label, C col, int w) {
  panel(cx - w / 2, y, w, BADGE_H, 5, col, 1);
  g_text_box(label, cx - w / 2, y, w, BADGE_H, F_LABEL, C_WHITE);
}

/* box: name, description on white, badges. Anchor: the item's rectangle
 * (x, y top, w width). The box goes below it, or above when there is no room. */
void draw_tooltip_for(int kind, int id, int ed, const void *extra, int ax, int ay, int aw) {
  static char d[200], d2[120];
  const char *name = tname(kind, id);
  char namebuf[40];
  d[0] = 0, d2[0] = 0;
  if (kind == TK_CARD) {
    /* playing card: "+11 chips" and its enhancement / seal */
    const vcard_t *c = (const vcard_t *)extra;
    char *o = namebuf;
    if (c->enh == E_STONE) o = str_cat(o, "Stone Card");
    else {
      o = str_cat(o, rank_names[c->rank]);
      o = str_cat(o, " of ");
      o = str_cat(o, suit_names_plural[c->suit & 3]);
    }
    name = namebuf;
    o = d;
    if (c->flags & CF_DEBUFF) o = str_cat(o, TC_MULT "Debuffed" TC_RESET "\nScores no chips\nand all abilities\nare disabled");
    else {
      if (c->enh != E_STONE) {
        o = str_cat(o, TC_CHIPS "+");
        o = fmt_int(o, card_chip_nominal(c->rank));
        o = str_cat(o, TC_RESET " chips");
      }
      int extra_chips = c->perma + (c->enh == E_BONUS ? 30 : 0);
      if (extra_chips) {
        o = str_cat(o, d[0] ? "\n" TC_CHIPS "+" : TC_CHIPS "+");
        o = fmt_int(o, extra_chips);
        o = str_cat(o, TC_RESET " extra chips");
      }
      if (c->enh && c->enh != E_BONUS) {
        char t[120];
        desc_text(t, sizeof t, TK_ENH, c->enh, 0);
        if (d[0]) o = str_cat(o, "\n");
        o = str_cat(o, t);
      }
      if (c->seal) desc_text(d2, sizeof d2, TK_SEAL, c->seal, 0);
      else if (c->ed) desc_text(d2, sizeof d2, TK_EDITION, c->ed, 0);
    }
  } else {
    desc_text(d, sizeof d, kind, id, extra);
    if (ed) desc_text(d2, sizeof d2, TK_EDITION, ed, 0);
  }
  /* size: the name (medium), the texts (small) on white, the badges */
  int lines = d[0] ? g_lines(d) : 0, lines2 = d2[0] ? g_lines(d2) : 0;
  int w = lines ? g_textw(d, F_TEXT) : 0;
  if (lines2 && g_textw(d2, F_TEXT) > w) w = g_textw(d2, F_TEXT);
  w += 10; /* white box padding */
  int nf = F_NAME;
  if (g_textw(name, T_MED) + 10 > w) w = g_textw(name, T_MED) + 10;
  if (w < 64) w = 64;
  w += 6;  /* outer frame */
  const char *bl[3];
  C bc[3];
  int nb = 0;
  /* Blueprint and Brainstorm say whether they can copy their target */
  if (kind == TK_JOKER && (id == J_BLUEPRINT || id == J_BRAINSTORM) && extra) {
    const joker_t *jj = (const joker_t *)extra;
    int me = (int)(jj - R.jokers);
    if (me >= 0 && me < R.njokers) {
      int t = id == J_BRAINSTORM ? 0 : me + 1;
      int ok = t < R.njokers && t != me && joker_info[R.jokers[t].id].bp;
      bl[nb] = ok ? "compatible" : "incompatible", bc[nb++] = ok ? C_GREEN : C_RED;
    }
  }
  if (kind == TK_JOKER) bl[nb] = rarity_name[joker_info[id].rarity], bc[nb++] = rarity_col[joker_info[id].rarity];
  if (kind == TK_CONS) {
    int set = id <= C_WORLD ? 0 : id <= C_ERIS ? 1 : 2;
    bl[nb] = set == 0 ? "Tarot" : set == 1 ? "Planet" : "Spectral";
    bc[nb++] = set == 0 ? C_TAROT : set == 1 ? C_PLANET : C_SPECTRAL;
  }
  if (kind == TK_VOUCHER) bl[nb] = "Voucher", bc[nb++] = HEXC(0xFD682B);
  if (ed && nb < 3) bl[nb] = tname(TK_EDITION, ed), bc[nb++] = ed == ED_NEG ? HEXC(0x2A2A2A) : C_EDITION;
  int bw = 0;
  for (int i = 0; i < nb; i++)
    if (g_textw(bl[i], F_LABEL) + 14 > bw) bw = g_textw(bl[i], F_LABEL) + 14;
  if (bw < 48) bw = 48;
  /* heights: frame 3, name 15, white boxes (lines x 10 + 5), badges 13 each */
  int boxh = lines ? lines * 10 + 5 : 0, boxh2 = lines2 ? lines2 * 10 + 5 : 0;
  int h = 3 + 15 + boxh + (lines2 ? boxh2 + 3 : 0) + (nb ? 3 + nb * (BADGE_H + 2) : 0) + 3;
  int x = ax + aw / 2 - w / 2;
  if (x < SIDE_W + 1) x = SIDE_W + 1;
  if (x + w > 318) x = 318 - w;
  if (x < 2) x = 2;
  int y = ay - h - 4;
  if (y < 1 && ay > 30) {
    /* no room above a card in the middle of the screen: beside it, so its own
       buttons and the panels under it stay visible; never over the sidebar */
    int bx = ax + aw + 4;
    if (bx + w > 318) bx = ax - w - 4;
    if (bx >= SIDE_W + 1) x = bx, y = ay + CH / 2 - h / 2;
    if (y < 1) y = 1; /* else as high as it goes, over the card's top */
  }
  if (x < 2 || y < 1) x = x < 2 ? 2 : x, y = ay + CH + 6;
  if (y + h > 238) y = 238 - h;
  /* grey box with a dark edge, the name, then the texts on white */
  g_rrect(x - 1, y - 1, w + 2, h + 3, 6, HEXC(0x1D2628));
  g_rrect(x, y, w, h, 5, HEXC(0x4F6367));
  g_text_box(name, x, y + 3, w, 15, nf, C_WHITE);
  int yy = y + 3 + 15;
  if (lines) {
    g_rrect(x + 3, yy, w - 6, boxh, 4, C_WHITE);
    g_text(d, x + w / 2, yy + 3, F_TEXT | T_CENTER, C_TEXT_DARK);
    yy += boxh;
  }
  if (lines2) {
    yy += 3;
    g_rrect(x + 3, yy, w - 6, boxh2, 4, C_WHITE);
    g_text(d2, x + w / 2, yy + 3, F_TEXT | T_CENTER, C_TEXT_DARK);
    yy += boxh2;
  }
  if (nb) yy += 3;
  for (int i = 0; i < nb; i++, yy += BADGE_H + 2) badge(x + w / 2, yy, bl[i], bc[i], bw);
}

/* re-flows a text to a width (colour codes kept, lines joined) */
void wrap_text(char *out, int maxlen, const char *in, int width, int flags) {
  char word[48];
  int n = 0, line_w = 0, space_w = g_textw(" x", flags) - g_textw("x", flags);
  const char *p = in;
  while (*p && n < maxlen - 1) {
    while (*p == ' ' || *p == '\n') p++;
    if (!*p) break;
    int k = 0;
    while (*p && *p != ' ' && *p != '\n' && k < 47) word[k++] = *p++;
    word[k] = 0;
    int ww = g_textw(word, flags);
    if (line_w > 0 && line_w + space_w + ww > width) {
      out[n++] = '\n';
      line_w = 0;
    } else if (line_w > 0) {
      out[n++] = ' ';
      line_w += space_w;
    }
    for (int i = 0; i < k && n < maxlen - 1; i++) out[n++] = word[i];
    line_w += ww;
  }
  out[n] = 0;
}
