#ifndef UI_H
#define UI_H
#include <stdint.h>
#include "game.h"
#include "gfx.h"

/* palette (G.C in globals.lua) */
#define C_BLACK HEXC(0x374244)
#define C_L_BLACK HEXC(0x4F6367)
#define C_GREY HEXC(0x5F7377)
#define C_RED HEXC(0xFE5F55)
#define C_BLUE HEXC(0x009DFF)
#define C_GREEN HEXC(0x4BC292)
#define C_ORANGE HEXC(0xFDA200)
#define C_GOLD HEXC(0xEAC058)
#define C_MONEY HEXC(0xF3B958)
#define C_ATTN HEXC(0xFF9A00)
#define C_PURPLE HEXC(0x8867A5)
#define C_WHITE 0xFFFF
#define C_TEXT_DARK HEXC(0x4F6367)
#define C_PANEL HEXC(0x3A4B4E)
#define C_DARK HEXC(0x1E2B2D)
#define C_TAROT HEXC(0xA782D1)
#define C_PLANET HEXC(0x13AFCE)
#define C_SPECTRAL HEXC(0x4584FA)
#define C_EDITION HEXC(0x4CA893)
#define C_INACTIVE HEXC(0x666666)
#define C_FOCUS HEXC(0xFF9A00)

/* layout */
#define SIDE_W 104
#define CW 35
#define CH 47
#define JOKER_X 108
#define JOKER_W 143
#define CONS_X 256
#define CONS_W 61
#define ROW_Y 4
#define PLAY_Y 72
#define HAND_Y 146
#define HAND_X 108
#define HAND_W 171
#define DECK_X 283
#define DECK_Y 176
#define AREA_X (SIDE_W + 3)     /* the play area, right of the sidebar */
#define AREA_W (317 - AREA_X)

/* type roles (see gfx.h): one size per role on every screen */
#define F_VALUE (T_LARGE | T_SHADOW)  /* numbers: chips, mult, score, money, counters */
#define F_TITLE (T_LARGE | T_SHADOW)  /* panel titles */
#define F_NAME (T_MED | T_SHADOW)     /* names: blind, poker hand, item, pack */
#define F_LABEL (T_SMALL | T_SHADOW)  /* labels and button texts on colour */
#define F_TEXT T_SMALL                /* descriptions on white */

/* visual state */
typedef struct {
  float x, y;
  int16_t tx, ty;
  uint8_t area, rank, suit, enh, ed, seal, flags, raise;
  uint16_t perma;
  uint8_t juice, fade, shatter, down;
} vcard_t;

typedef struct {
  uint16_t uid;
  uint8_t id, ed, flags, juice, dying, sold;
  float x, y;
} vitem_t;

typedef struct {
  vcard_t cards[MAXCARDS];
  uint8_t hand[MAXHAND], nhand;
  uint8_t play[8], nplay;
  vitem_t jokers[MAXJ + 4];
  uint8_t njokers;
  vitem_t cons[MAXCONS + 2];
  uint8_t ncons;
  int32_t money, money_target;
  int16_t hands, discards;
  double score, score_target, chips, mult;
  int16_t hand_name, hand_level;
  uint8_t flame, blind_flash;
  int16_t deck_count;
} vis_t;
extern vis_t V;

/* focus */
extern int focus_zone, focus_idx;
extern int focus_shake; /* x offset of the focused element when an action is refused */

/* drawing helpers (draw.c) */
void panel(int x, int y, int w, int h, int r, C c, int emboss);
void button(int x, int y, int w, int h, C c, const char *label, int flags, int focused, int enabled);
void keycap(int x, int y, const char *label);
void draw_card(const vcard_t *c, int x, int y, int scale, int focused);
void draw_card_back(int x, int y, int scale);
void draw_joker_sprite(int id, int ed, int x, int y, int scale, int flags);
void draw_cons_sprite(int id, int ed, int x, int y, int scale, int flags);
void draw_item(const sitem_t *it, int x, int y, int scale, int flags);
void draw_tooltip_for(int kind, int id, int ed, const void *extra, int ax, int ay, int aw);
void draw_price(int x, int y, int cost, int affordable);
void draw_blind_chip(int b, int x, int y, int scale);
int item_sprite(const sitem_t *it);
void desc_text(char *out, int maxlen, int kind, int id, const void *extra);
void fmt_money(char *o, int v);
void label_dollars(const char *label, int n, int cx, int y, int maxw);
int num_fit(char *t, double v, int maxw, int big); /* formats v, returns the largest size that fits */
int text_fit(const char *t, int maxw, int big);    /* largest of big, medium, small that fits */
void wrap_text(char *out, int maxlen, const char *in, int width, int flags);
extern const C popup_col[];
enum { TK_JOKER, TK_CONS, TK_VOUCHER, TK_PACK, TK_TAG, TK_BLIND, TK_CARD, TK_EDITION, TK_SEAL, TK_ENH };

/* sidebar and common parts (ui.c) */
void draw_sidebar(void);
C readable(C c, int base); /* a blind's colour, darkened when white text would not read on it */
void draw_areas(int show_hand, int show_play);
int ui_busy(void);
void ui_sync_all(int instant);
void ui_update(int dt);
float speed_factor(void);
extern int ui_hold_ms; /* how long OK or EXE has been held */
#endif
