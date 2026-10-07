#ifndef GFX_H
#define GFX_H
#include <stdint.h>
#include "../../common/np_text.h"

typedef uint16_t C;
#define RGB(r, g, b) ((C)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3)))
#define HEXC(h) RGB(((h) >> 16) & 255, ((h) >> 8) & 255, (h) & 255)

/* text flags. Sizes, after Balatro's hierarchy (all m6x11):
   T_LARGE  11 pixel capitals: values (chips, mult, score, money), titles
   T_MED     9 pixel capitals: names and headings (blind, poker hand, item)
   T_SMALL   7 pixel capitals: labels, buttons, descriptions */
enum {
  T_SMALL = 0,
  T_LARGE = 1,
  T_X2 = 2,      /* doubled pixels */
  T_SHADOW = 4,  /* dark shadow one pixel below */
  T_CENTER = 8,  /* x is the centre */
  T_RIGHT = 16,  /* x is the right edge */
  T_OUTLINE = 32, /* dark outline all around */
  T_TIGHT = 64,   /* condensed: no gap between letters, narrow spaces */
  T_MED = 128,
  T_COND = 256,   /* condensed: large digits, or the whole medium size, one pixel narrower */
  T_ROT = 512     /* turned a quarter left, reading upwards; (x, y) is the bottom left */
};
/* NumPlay's language builds: XROWS rows more above a text, for accents over capitals; in the
   Chinese one (a translated text tells), taller lines for its 12-pixel letters */
#define XROWS (NP_TEXT_EXTRA ? 2 : 0)
#define TXT_CJK (NP_TEXT_EXTRA && (unsigned char)T("Mult")[0] >= 0xE0)
#define LINE_H (TXT_CJK ? 12 : 10) /* lines of the small size */
/* inline colour codes in strings */
#define TC_WHITE "\x01"
#define TC_CHIPS "\x02"
#define TC_MULT "\x03"
#define TC_ATTN "\x04"
#define TC_MONEY "\x05"
#define TC_GREEN "\x06"
#define TC_GREY "\x07"
#define TC_TAROT "\x08"
#define TC_PLANET "\x09"
#define TC_SPECTRAL "\x0B"
#define TC_DARK "\x0C"
#define TC_XMULT "\x0E" /* white on a red badge until the next code */
#define TC_EDITION "\x0F"
#define TC_RESET "\x10" /* back to the item's colour */
/* suit symbols, small size only */
#define SUIT_SPADE "\x7f"
#define SUIT_HEART "\x80"
#define SUIT_CLUB "\x81"
#define SUIT_DIAMOND "\x82"

/* sprite effects */
enum {
  FX_NONE = 0,
  FX_FOIL = 1,
  FX_HOLO = 2,
  FX_POLY = 3,
  FX_NEG = 4,
  FX_EDMASK = 7,
  FX_DIM = 8,      /* darkened (debuffed, unaffordable) */
  FX_FLIPX = 16,   /* mirrored */
  FX_WHITE = 32,   /* flat white (flash) */
  FX_SHADOW = 64,  /* flat translucent black (card shadow) */
  FX_GREY = 128,   /* desaturated */
};

void g_init(void);
void g_begin(void);
void g_end(void); /* draws the list on screen */
void g_clip(int x, int y, int w, int h);
void g_noclip(void);
void g_rect(int x, int y, int w, int h, C c);
void g_rrect(int x, int y, int w, int h, int r, C c);
void g_shade(int x, int y, int w, int h, int r, int alpha); /* darken, alpha 0..32 */
void g_sprite(int id, int x, int y, int fx, int scale); /* scale: 256 = 1 */
void g_sprite_wh(int id, int x, int y, int w, int h, int fx);
void g_rsquare(int cx, int cy, int hs, int angle, C c); /* angle: 64 = a quarter turn */
void g_flame(int k, int x, int y, int w, int h, int top); /* 0 chips, 1 mult; thins out just under top */
void g_text(const char *s, int x, int y, int flags, C c);
int g_textw(const char *s, int flags);
int g_cap(int flags);   /* height of the capitals */
int g_pitch(int flags); /* distance between lines */
int g_lines(const char *s);
void g_text_box(const char *s, int x, int y, int w, int h, int flags, C c); /* centred in the box */
void g_line_h(int x, int y, int w, C c);
void g_bg(int swirl, C solid);
void *g_scratch(int *size); /* the strip buffer, free outside g_end */
extern uint32_t g_time; /* ms, for animated effects */

/* t 0..32 */
static inline __attribute__((always_inline)) C mix565(C a, C b, int t) {
  uint32_t A = (a | (uint32_t)a << 16) & 0x07E0F81F, B = (b | (uint32_t)b << 16) & 0x07E0F81F;
  uint32_t M = ((A * (uint32_t)(32 - t) + B * (uint32_t)t) >> 5) & 0x07E0F81F;
  return (C)(M | M >> 16);
}
/* darker by t/32, the fast way (no multiply per channel pair of the source) */
static inline __attribute__((always_inline)) C shade565(C a, int t) {
  uint32_t A = (a | (uint32_t)a << 16) & 0x07E0F81F;
  uint32_t M = ((A * (uint32_t)(32 - t)) >> 5) & 0x07E0F81F;
  return (C)(M | M >> 16);
}
void g_bg_cover(int x); /* the background is hidden left of x this frame */
C darken(C a, int t);      /* t 0..32 of black */
C lighten(C a, int t);
#endif
