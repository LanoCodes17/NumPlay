/* Solitaire: Klondike, the way Windows Solitaire plays it, for the NumWorks
 * calculator. Rewritten in C for NumPlay from Tatone26's Solitaire in All the
 * Apps, which is in the public domain.
 *
 * The table is drawn only where something changed: each changed rectangle is
 * composed in a small buffer, strip by strip, and pushed once, so nothing
 * flickers. Cards, pips and court figures come from a few tiny bitmaps. */
#include <eadk.h>
#include <stdbool.h>
#include <stdint.h>
#include "../../common/epsilon_app.h"
#include "../../common/epsilon_files.h"
#include "../../common/jump.h"

#ifdef __ELF__ /* app name and API level, for the calculator's installer */
const char eadk_app_name[] __attribute__((section(".rodata.eadk_app_name"))) = "Solitaire";
const uint32_t eadk_api_level __attribute__((section(".rodata.eadk_api_level"))) = 0;
#endif

typedef uint16_t color;
#define RGB(c) (color)((((c) >> 8) & 0xF800) | (((c) >> 5) & 0x07E0) | (((c) >> 3) & 0x1F))
#define WHITE 0xFFFF
#define BLACK 0
#define FELT RGB(0x008000)
#define SLOT RGB(0x003800)
#define RED RGB(0xD00000)
#define FACE RGB(0xC0C0C0)
#define LIGHT RGB(0xDFDFDF)
#define SHADOW RGB(0x808080)
#define NAVY RGB(0x000080)
#define GOLD RGB(0xFFD800)
#define KEY(k) (1ull << (k))

/* ------------------------------------------------------------------ art */
/* art begin */
/* suits: spades, hearts, clubs, diamonds (left half and middle column of each row) */
static const uint8_t pip7[4][7] = {{0x01,0x03,0x07,0x0F,0x06,0x01,0x03}, {0x06,0x0F,0x0F,0x0F,0x07,0x03,0x01}, {0x03,0x03,0x06,0x0F,0x06,0x01,0x03}, {0x01,0x03,0x07,0x0F,0x07,0x03,0x01}};
static const uint8_t pip13[4][15] = {
  {0x01,0x03,0x07,0x0F,0x1F,0x3F,0x7F,0x7F,0x7F,0x3D,0x19,0x01,0x03,0x0F,0x00},
  {0x00,0x38,0x7C,0x7E,0x7F,0x7F,0x7F,0x7F,0x3F,0x1F,0x0F,0x07,0x03,0x01,0x00},
  {0x00,0x03,0x07,0x07,0x07,0x33,0x79,0x7F,0x7F,0x79,0x31,0x01,0x03,0x0F,0x00},
  {0x01,0x03,0x07,0x07,0x0F,0x1F,0x3F,0x7F,0x3F,0x1F,0x0F,0x07,0x07,0x03,0x01}};
/* court figures, top half, 22x16 at 3 bits a pixel (see court()) */
static const uint8_t figs[3][133] = {
  {0x00,0x00,0x20,0x49,0x92,0x04,0x00,0x00,0x00,0x00,0x10,0x49,0x92,0xA4,0x00,0x40,0x00,0x00,0x88,0x24,0x49,0x92,0x94,0x84,0x01,0x00,0x20,0x24,0x49,0x92,0x64,0x48,0x06,0x00,0x00,0xE4,0x6D,0xDB,0x3E,0x48,0x02,0x00,0x00,0xF0,0x37,0x6D,0x7A,0x00,0x00,0x00,0x00,0xC0,0xDF,0xB6,0xED,0x01,0x00,0x00,0x00,0x00,0x7F,0xDB,0xB6,0x05,0x00,0x00,0x00,0x00,0xFC,0xAD,0xA4,0x16,0x00,0x00,0x00,0x00,0x80,0xBF,0x6D,0x0B,0x00,0x01,0x00,0x00,0x00,0x90,0xB6,0x0D,0x80,0x30,0x00,0x00,0x00,0xD9,0x48,0x72,0x0B,0xC2,0x00,0x00,0x80,0x6C,0x1B,0xB9,0x6D,0x09,0x03,0x00,0x40,0x36,0x69,0xDB,0xA4,0x2D,0x0C,0x00,0x20,0x9B,0x24,0xCD,0x9E,0xA4,0x85,0x01,0x80,0x6C,0x93,0x36,0x7B,0x93,0x16,0x06},
  {0x00,0x00,0x00,0x41,0x12,0x04,0x00,0x00,0x00,0x00,0x00,0x84,0x49,0x18,0x00,0x00,0x00,0x00,0x00,0x1E,0x92,0x64,0x0E,0x00,0x00,0x00,0x00,0x7F,0x92,0x24,0xF9,0x01,0x00,0x00,0x00,0xFC,0x6D,0xDB,0xB6,0x3F,0x00,0x00,0x00,0xFE,0x37,0x6D,0xD3,0xFF,0x00,0x00,0x00,0xF8,0xDF,0xB6,0x6D,0xFB,0x1F,0x00,0x00,0xE0,0x7F,0x5B,0xB5,0xED,0x7F,0x00,0x00,0x80,0xFF,0x6F,0xDB,0xF6,0xFF,0x41,0x00,0x00,0xF0,0x3F,0x6D,0x1B,0xFC,0xA0,0x08,0x00,0x40,0x42,0xB6,0x2D,0x99,0x00,0x04,0x00,0x20,0x92,0x48,0x92,0x94,0x14,0x04,0x00,0x10,0x49,0x93,0x48,0x69,0x92,0x12,0x00,0x88,0xA4,0x6D,0x92,0xB4,0x4D,0x52,0x00,0x44,0xD2,0x66,0x7B,0xB3,0xBD,0x49,0x0A,0x10,0x69,0x9B,0xED,0xCD,0xF6,0x36,0x29},
  {0x00,0x00,0x00,0x41,0x10,0x04,0x00,0x00,0x00,0x00,0x00,0x84,0x61,0x18,0x00,0x00,0x00,0x00,0x00,0x10,0x92,0x64,0x00,0x00,0x00,0x00,0x00,0x40,0x28,0x8E,0x01,0x00,0x00,0x00,0x00,0x20,0x49,0x92,0x24,0x00,0x00,0x00,0x00,0x80,0xBC,0x6D,0xDB,0x07,0x00,0x00,0x00,0x00,0xDE,0xB4,0xE9,0x11,0x00,0x00,0x00,0x00,0x78,0xDB,0xB6,0x3D,0x02,0x00,0x00,0x00,0xFC,0x6D,0xD3,0xF6,0x47,0x00,0x00,0x00,0xF0,0xBF,0x92,0xFA,0x1F,0x01,0x00,0x00,0xC0,0xFF,0xFF,0xFF,0xFF,0x24,0x00,0x00,0x20,0xDB,0xFF,0xFF,0xDF,0xC2,0x00,0x00,0x90,0x6D,0xFC,0xFF,0x73,0x5B,0x02,0x00,0xC8,0xA6,0x8D,0x24,0xB9,0x69,0x0B,0x00,0x64,0x93,0xB4,0xD9,0xDE,0x24,0x6D,0x01,0x90,0x6D,0xDA,0x66,0x7B,0x9B,0xB6,0x05}};
/* art end */

/* ASCII 32..122 (space to z), columns of 7 bits, top bit first */
static const uint8_t font[91][5] = {
  {0x00,0x00,0x00,0x00,0x00},{0x00,0x00,0x5F,0x00,0x00},{0x00,0x07,0x00,0x07,0x00},{0x14,0x7F,0x14,0x7F,0x14},
  {0x24,0x2A,0x7F,0x2A,0x12},{0x23,0x13,0x08,0x64,0x62},{0x36,0x49,0x55,0x22,0x50},{0x00,0x05,0x03,0x00,0x00},
  {0x00,0x1C,0x22,0x41,0x00},{0x00,0x41,0x22,0x1C,0x00},{0x14,0x08,0x3E,0x08,0x14},{0x08,0x08,0x3E,0x08,0x08},
  {0x00,0x50,0x30,0x00,0x00},{0x08,0x08,0x08,0x08,0x08},{0x00,0x60,0x60,0x00,0x00},{0x20,0x10,0x08,0x04,0x02},
  {0x3E,0x41,0x41,0x41,0x3E},{0x00,0x42,0x7F,0x40,0x00},{0x42,0x61,0x51,0x49,0x46},{0x21,0x41,0x45,0x4B,0x31},
  {0x18,0x14,0x12,0x7F,0x10},{0x27,0x45,0x45,0x45,0x39},{0x3C,0x4A,0x49,0x49,0x30},{0x01,0x71,0x09,0x05,0x03},
  {0x36,0x49,0x49,0x49,0x36},{0x06,0x49,0x49,0x29,0x1E},{0x00,0x36,0x36,0x00,0x00},{0x00,0x56,0x36,0x00,0x00},
  {0x08,0x14,0x22,0x41,0x00},{0x14,0x14,0x14,0x14,0x14},{0x00,0x41,0x22,0x14,0x08},{0x02,0x01,0x51,0x09,0x06},
  {0x32,0x49,0x79,0x41,0x3E},{0x7E,0x11,0x11,0x11,0x7E},{0x7F,0x49,0x49,0x49,0x36},{0x3E,0x41,0x41,0x41,0x22},
  {0x7F,0x41,0x41,0x22,0x1C},{0x7F,0x49,0x49,0x49,0x41},{0x7F,0x09,0x09,0x09,0x01},{0x3E,0x41,0x49,0x49,0x7A},
  {0x7F,0x08,0x08,0x08,0x7F},{0x00,0x41,0x7F,0x41,0x00},{0x20,0x40,0x41,0x3F,0x01},{0x7F,0x08,0x14,0x22,0x41},
  {0x7F,0x40,0x40,0x40,0x40},{0x7F,0x02,0x0C,0x02,0x7F},{0x7F,0x04,0x08,0x10,0x7F},{0x3E,0x41,0x41,0x41,0x3E},
  {0x7F,0x09,0x09,0x09,0x06},{0x3E,0x41,0x51,0x21,0x5E},{0x7F,0x09,0x19,0x29,0x46},{0x46,0x49,0x49,0x49,0x31},
  {0x01,0x01,0x7F,0x01,0x01},{0x3F,0x40,0x40,0x40,0x3F},{0x1F,0x20,0x40,0x20,0x1F},{0x3F,0x40,0x38,0x40,0x3F},
  {0x63,0x14,0x08,0x14,0x63},{0x07,0x08,0x70,0x08,0x07},{0x61,0x51,0x49,0x45,0x43},{0x00,0x7F,0x41,0x41,0x00},
  {0x02,0x04,0x08,0x10,0x20},{0x00,0x41,0x41,0x7F,0x00},{0x04,0x02,0x01,0x02,0x04},{0x40,0x40,0x40,0x40,0x40},
  {0x00,0x01,0x02,0x04,0x00},{0x20,0x54,0x54,0x54,0x78},{0x7F,0x48,0x44,0x44,0x38},{0x38,0x44,0x44,0x44,0x20},
  {0x38,0x44,0x44,0x48,0x7F},{0x38,0x54,0x54,0x54,0x18},{0x08,0x7E,0x09,0x01,0x02},{0x0C,0x52,0x52,0x52,0x3E},
  {0x7F,0x08,0x04,0x04,0x78},{0x00,0x44,0x7D,0x40,0x00},{0x20,0x40,0x44,0x3D,0x00},{0x7F,0x10,0x28,0x44,0x00},
  {0x00,0x41,0x7F,0x40,0x00},{0x7C,0x04,0x18,0x04,0x78},{0x7C,0x08,0x04,0x04,0x78},{0x38,0x44,0x44,0x44,0x38},
  {0x7C,0x14,0x14,0x14,0x08},{0x08,0x14,0x14,0x18,0x7C},{0x7C,0x08,0x04,0x04,0x08},{0x48,0x54,0x54,0x54,0x20},
  {0x04,0x3F,0x44,0x40,0x20},{0x3C,0x40,0x40,0x20,0x7C},{0x1C,0x20,0x40,0x20,0x1C},{0x3C,0x40,0x30,0x40,0x3C},
  {0x44,0x28,0x10,0x28,0x44},{0x0C,0x50,0x50,0x50,0x3C},{0x44,0x64,0x54,0x4C,0x44},
};

/* ------------------------------------------------------------------ drawing */
/* Everything is composed in buf, one region of the screen at a time: the
   region's pixels, row after row (rw wide). Drawing outside it is clipped. */
#define BUFPX (320 * 24)
static color buf[BUFPX];
static int rx, ry, rw, rh;

static int min(int a, int b) { return a < b ? a : b; }
static int max(int a, int b) { return a > b ? a : b; }

static void fill(int x, int y, int w, int h, color c) {
  int x0 = max(x, rx), x1 = min(x + w, rx + rw), y0 = max(y, ry), y1 = min(y + h, ry + rh);
  if (x0 >= x1) return;
  for (int j = y0; j < y1; j++)
    for (color *p = buf + (j - ry) * rw + (x0 - rx), *e = p + (x1 - x0); p < e; p++) *p = c;
}
static void px(int x, int y, color c) {
  if ((unsigned)(x - rx) < (unsigned)rw && (unsigned)(y - ry) < (unsigned)rh) buf[(y - ry) * rw + x - rx] = c;
}
static void frame(int x, int y, int w, int h, int t, color c) {
  fill(x, y, w, t, c), fill(x, y + h - t, w, t, c), fill(x, y, t, h, c), fill(x + w - t, y, t, h, c);
}
/* the top and left edges in one colour, the bottom and right in another */
static void bevel(int x, int y, int w, int h, color tl, color br) {
  fill(x, y, w, 1, tl), fill(x, y, 1, h, tl), fill(x, y + h - 1, w, 1, br), fill(x + w - 1, y, 1, h, br);
}
/* a card's shape: rounded corners, an outline o, filled with f */
static void rrect(int x, int y, int w, int h, color o, color f) {
  fill(x + 2, y, w - 4, h, o), fill(x + 1, y + 1, w - 2, h - 2, o), fill(x, y + 2, w, h - 4, o);
  fill(x + 2, y + 1, w - 4, h - 2, f), fill(x + 1, y + 2, w - 2, h - 4, f);
}
/* a ring between radii r0 and r1 (a disc when r0 is 0) */
static void ring(int cx, int cy, int r0, int r1, color c) {
  for (int dy = -r1; dy <= r1; dy++)
    for (int dx = -r1; dx <= r1; dx++) {
      int d = dx * dx + dy * dy;
      if (d >= r0 * r0 && d <= r1 * r1 + r1) px(cx + dx, cy + dy, c);
    }
}
/* a left-right symmetric bitmap: each row holds the left half and the middle
   column, first column in the top bit; upside down when flip */
static void sym(const uint8_t *rows, int n, int half, int x, int y, color c, int flip) {
  for (int j = 0; j < n; j++) {
    int r = rows[flip ? n - 1 - j : j];
    for (int i = 0; i < half; i++)
      if (r >> (half - 1 - i) & 1) px(x + i, y + j, c), px(x + 2 * half - 2 - i, y + j, c);
  }
}
/* a character of the 5x7 font, k times bigger, turned upside down when flip */
static void glyph(int ch, int x, int y, int k, color c, int flip) {
  const uint8_t *g = font[(unsigned)(ch - 32) < 91 ? ch - 32 : '?' - 32];
  for (int i = 0; i < 5; i++)
    for (int j = 0; j < 7; j++)
      if (g[i] >> j & 1) fill(x + (flip ? 4 - i : i) * k, y + (flip ? 6 - j : j) * k, k, k, c);
}
static int slen(const char *s) {
  int n = 0;
  while (s[n]) n++;
  return n;
}
static void text(const char *s, int x, int y, int k, color c) {
  for (; *s; s++, x += 6 * k) glyph(*s, x, y, k, c, 0);
}
/* the firmware's fonts, straight to the screen: 7x14 or 10x18 cells */
static void str(const char *s, int x, int y, int large, color fg, color bg) {
  eadk_display_draw_string(s, (eadk_point_t){(uint16_t)x, (uint16_t)y}, large, fg, bg);
}

static char *cat(char *o, const char *s) {
  while (*s) *o++ = *s++;
  *o = 0;
  return o;
}
static char *num(char *o, int32_t v) {
  char t[11];
  int n = 0;
  uint32_t u = v < 0 ? 0u - (uint32_t)v : (uint32_t)v;
  if (v < 0) *o++ = '-';
  do t[n++] = (char)('0' + u % 10); while (u /= 10);
  while (n) *o++ = t[--n];
  *o = 0;
  return o;
}
/* Vegas dollars, written like Windows: -$52 */
static char *money(char *o, int32_t v) {
  char *s = o;
  o = num(cat(o, "$"), v);
  if (v < 0) s[0] = '-', s[1] = '$';
  return o;
}

/* ------------------------------------------------------------------ cards */
enum { STOCK, WASTE, F0, T0 = 6, NP = 13 }; /* the piles */

/* everything kept between visits (solitaire.sav): options, statistics and
   the game in progress */
static struct {
  uint8_t magic, version, draw, scoring; /* scoring: 0 Standard, 1 Vegas, 2 none */
  uint8_t timed, keep, live, started;    /* live: a game to continue */
  uint8_t gdraw, gscoring, gtimed, passes; /* the game's own options; recycled decks */
  uint8_t wfan, back, pad[2];            /* cards spread on the waste; the card back */
  uint16_t played, won, streak, best_streak, best_time, moves;
  int32_t best_score, bank, score; /* bank: all Vegas games added up */
  uint32_t seed, ms;
  uint8_t n[NP], cards[52], pad2[3];
} V;
#define SAVE_NAME "solitaire.sav"

/* A card is rank * 4 + suit (suits: spades, hearts, clubs, diamonds, so bit 0
   is red), plus UP when it lies face up. */
#define UP 0x80
#define RANK(c) (((c) & 63) >> 2)
#define SUIT(c) ((c) & 3)
#define CW 40
#define CH 54
#define COLX(i) (5 + (i) * 45)
#define TOPY 2
#define TABY 60
#define STY 230 /* the status bar */
#define FAN 15  /* the waste's spread, drawing 2 or 3 */

/* where the pips of 2..10 go: groups of pips on a 3x13 grid, and which groups
   each rank uses. A group: column (bits 6-7), row (0-3), mirrored across the
   middle column (bit 4) and the middle row (bit 5). */
static const uint8_t pip_group[8] = {0x60, 0x46, 0x30, 0x16, 0x43, 0x49, 0x34, 0x62};
static const uint8_t pip_ranks[9] = {0x01, 0x03, 0x04, 0x06, 0x0C, 0x1C, 0x3C, 0x46, 0xC4};

static void pip(int s, int col, int row, int x, int y, color c) {
  sym(pip7[s], 7, 4, x + 9 + col * 8, y + 10 + row * 9 / 4, c, row > 6);
}

/* the index in a corner: the rank in bold and a small suit; flip draws the
   one in the bottom right corner, upside down */
static void corner(int c, int x, int y, color ink, int flip) {
  int r = RANK(c), u = 3;
  const char *s = r == 9 ? "10" : &"A23456789?JQK"[r];
  for (int n = r == 9 ? 2 : 1, i = 0; i < n; i++) {
    int w = s[i] == '1' ? 4 : 6, gx = s[i] == '1' ? u - 1 : u; /* the 1 of 10 is narrow */
    for (int b = 0; b < 2; b++) glyph(s[i], x + (flip ? CW - gx - 6 : gx) + b, y + (flip ? CH - 9 : 2), 1, ink, flip);
    u += w + (n == 2 && !i);
  }
  u++;
  sym(pip7[SUIT(c)], 7, 4, x + (flip ? CW - u - 7 : u), y + (flip ? CH - 9 : 2), ink, flip);
}

/* a court card: a framed figure, the bottom half the top one turned round */
static void court(int c, int x, int y, color ink, color inv) {
  const uint8_t *f = figs[RANK(c) - 10];
  color pal[8] = {WHITE, BLACK, RED, RGB(0x2040C0), RGB(0xF0C020), RGB(0xF8D8B0), ink ^ inv, RGB(0x8C5014)};
  frame(x + 8, y + 10, 24, 34, 1, ink);
  for (int b = 0; b < 22 * 16; b++) {
    int v = (f[b * 3 >> 3] | f[(b * 3 >> 3) + 1] << 8) >> (b * 3 & 7) & 7, i = b % 22, j = b / 22;
    if (v) px(x + 9 + i, y + 11 + j, pal[v] ^ inv), px(x + 30 - i, y + 42 - j, pal[v] ^ inv);
  }
}

/* the backs of the Deck dialog, inside a white border: the classic blue
   lattice, a red one, a basket weave and diamonds */
static const color backs[4][2] = {{RGB(0x1830A8), RGB(0x7890F0)}, {RGB(0xA01010), RGB(0xF07070)},
                                  {RGB(0x005850), RGB(0x50C8B0)}, {RGB(0x704000), RGB(0xF0C040)}};
/* How much of a covered back shows. Squeezed under 6 px, the border shrinks
   so the pattern still shows; under 3 px even the edge takes its colour. */
static int sq = 6;
static void back(int x, int y) {
  int s = V.back & 3, t = sq < 6;
  rrect(x, y, CW, CH, sq < 3 ? backs[s][0] : BLACK, WHITE);
  int x0 = max(x + 3 - t, rx), x1 = min(x + CW - 3 + t, rx + rw), y0 = max(y + 3 - 2 * t, ry), y1 = min(y + CH - 3, ry + rh);
  for (int j = y0; j < y1; j++)
    for (int i = x0; i < x1; i++) {
      int a = i - x, b = j - y;
      buf[(j - ry) * rw + i - rx] = backs[s][s < 2   ? ((a + b) & 3) == 1 || ((a - b) & 3) == 1
                                             : s == 2 ? (((a >> 2) + (b >> 2)) & 1 ? a : b) & 1
                                                      : ((a + b) & 7) < 2 || ((a - b) & 7) < 2];
    }
}

/* a card at (x, y); inv shows it selected (colours inverted, as in Windows) */
static void card(int c, int x, int y, color inv) {
  if (x >= rx + rw || x + CW <= rx || y >= ry + rh || y + CH <= ry) return;
  if (!(c & UP)) {
    back(x, y);
    return;
  }
  int s = SUIT(c), r = RANK(c);
  color ink = (s & 1 ? RED : BLACK) ^ inv;
  rrect(x, y, CW, CH, BLACK ^ inv, WHITE ^ inv);
  corner(c, x, y, ink, 0);
  corner(c, x, y, ink, 1);
  if (!r) {
    sym(pip13[s], 15, 7, x + 14, y + 20, ink, 0);
  } else if (r < 10) {
    for (int g = 0, m = pip_ranks[r - 1]; g < 8; g++) {
      if (!(m >> g & 1)) continue;
      int b = pip_group[g], col = b >> 6, row = b & 15;
      pip(s, col, row, x, y, ink);
      if (b & 0x10) pip(s, 2 - col, row, x, y, ink);
      if (b & 0x20) pip(s, col, 12 - row, x, y, ink);
      if ((b & 0x30) == 0x30) pip(s, 2 - col, 12 - row, x, y, ink);
    }
  } else {
    court(c, x, y, ink, inv);
  }
}
/* an empty place, etched in the felt */
static void slot(int x, int y) {
  rrect(x, y, CW, CH, RGB(0x00A800), FELT);
  rrect(x, y, CW - 1, CH - 1, SLOT, FELT);
}

/* ------------------------------------------------------------------ state */
static uint8_t pl[NP][52], pn[NP]; /* the piles, bottom card first */
static uint8_t fd[7], od[7], ou[7]; /* per column: face-down cards, their spread, the face-up spread */

static uint32_t now, seed = 0x9E3779B9;
static uint32_t rnd(void) {
  seed ^= seed << 13;
  seed ^= seed >> 17;
  seed ^= seed << 5;
  return seed;
}

/* the waste shows the last cards drawn, spread (none left in the spread: the
   top card alone) */
static int fan(void) { return min(min(max(V.wfan, 1), V.gdraw), pn[WASTE]); }

/* each column's spread, squeezed until it fits above the status bar */
static void relayout(void) {
  for (int i = 0; i < 7; i++) {
    int n = pn[T0 + i], d = 0, a = 6, b = 13;
    while (d < n && !(pl[T0 + i][d] & UP)) d++;
    /* face up to 9 px (the index still shows whole), face down to 3, face
       up to 8, then whatever it takes */
    while (d * a + max(n - d - 1, 0) * b > STY - 2 - TABY - CH) {
      if (b > 9 || (a < 4 && b > 8)) b--;
      else if (a > 1) a--;
      else if (b > 1) b--;
      else break;
    }
    fd[i] = (uint8_t)d, od[i] = (uint8_t)a, ou[i] = (uint8_t)b;
  }
}

/* where card k of pile p lies (k may be the free place above the top) */
static void pos(int p, int k, int *x, int *y) {
  *y = TOPY;
  if (p == STOCK) {
    *x = COLX(0);
  } else if (p == WASTE) {
    *x = COLX(1) + max(k - (pn[WASTE] - fan()), 0) * FAN;
  } else if (p < T0) {
    *x = COLX(p + 1);
  } else {
    int i = p - T0, d = fd[i];
    *x = COLX(i);
    *y = TABY + (k < d ? k * od[i] : d * od[i] + (k - d) * ou[i]);
  }
}

/* how many cards from the top can be picked up together */
static int run(int p) {
  int n = pn[p], k = 1;
  if (!n || !(pl[p][n - 1] & UP)) return 0;
  if (p < T0) return 1;
  for (; k < n; k++) {
    int a = pl[p][n - k - 1], b = pl[p][n - k];
    if (!(a & UP) || RANK(a) != RANK(b) + 1 || !((a ^ b) & 1)) break;
  }
  return k;
}

/* can the top n cards of f go on t? */
static int legal(int f, int n, int t) {
  if (f == t || t < F0 || (f >= F0 && f < T0 && t < T0) || n < 1 || n > run(f)) return 0;
  int c = pl[f][pn[f] - n], top = pn[t] ? pl[t][pn[t] - 1] : -1;
  if (t < T0) return n == 1 && (top < 0 ? RANK(c) == 0 : SUIT(top) == SUIT(c) && RANK(top) + 1 == RANK(c));
  return top < 0 ? RANK(c) == 12 : RANK(top) == RANK(c) + 1 && ((top ^ c) & 1);
}

/* ------------------------------------------------------------------ screen */
static np_jump_t leave; /* Home and On/Off leave from anywhere through it */
static uint64_t held, pend, rep; /* keys down, presses not handled yet, arrows that repeat */
static uint32_t repeat_at, last_frame;
static int layer;   /* what the screen shows: 0 the table, 1 the title, 2 one widget */
static int ticking; /* the game's clock runs (not in dialogs) */
static void scene(void);

/* draws the scene over a rectangle of the screen and pushes it */
static void paint(int x, int y, int w, int h) {
  if (x < 0) w += x, x = 0;
  if (y < 0) h += y, y = 0;
  w = min(w, 320 - x), h = min(h, 240 - y);
  if (w <= 0 || h <= 0) return;
  for (int y0 = y, sh = BUFPX / w; y0 < y + h; y0 += sh) {
    rx = x, ry = y0, rw = w, rh = min(sh, y + h - y0);
    scene();
    eadk_display_push_rect((eadk_rect_t){(uint16_t)rx, (uint16_t)ry, (uint16_t)rw, (uint16_t)rh}, buf);
  }
}

/* rectangles to repaint at the next frame, merged when they overlap */
static int16_t dr[12][4];
static int ndr;
static void dirty(int x, int y, int w, int h) {
  int x1 = x + w, y1 = y + h;
  for (int i = 0; i < ndr; i++) {
    int16_t *d = dr[i];
    if (x < d[2] && d[0] < x1 && y < d[3] && d[1] < y1) {
      d[0] = (int16_t)min(d[0], x), d[1] = (int16_t)min(d[1], y), d[2] = (int16_t)max(d[2], x1), d[3] = (int16_t)max(d[3], y1);
      return;
    }
  }
  if (ndr == 12) {
    int16_t *d = dr[11];
    d[0] = (int16_t)min(d[0], x), d[1] = (int16_t)min(d[1], y), d[2] = (int16_t)max(d[2], x1), d[3] = (int16_t)max(d[3], y1);
    return;
  }
  dr[ndr][0] = (int16_t)x, dr[ndr][1] = (int16_t)y, dr[ndr][2] = (int16_t)x1, dr[ndr][3] = (int16_t)y1;
  ndr++;
}
static void dirty_pile(int p) {
  if (p < T0) dirty(COLX(p == STOCK ? 0 : p == WASTE ? 1 : p + 1) - 2, 0, CW + 4 + (p == WASTE) * 2 * FAN, TABY - 2);
  else dirty(COLX(p - T0) - 2, TABY - 2, CW + 4, STY - TABY + 2);
}
static void dirty_status(void) { dirty(0, STY, 320, 10); }

/* keys: presses since the last look, arrows repeating while held */
static void poll(void) {
  uint64_t k = eadk_keyboard_scan(), e = k & ~held;
  const uint64_t arrows = KEY(eadk_key_left) | KEY(eadk_key_right) | KEY(eadk_key_up) | KEY(eadk_key_down);
  if (e & (KEY(eadk_key_home) | KEY(eadk_key_on_off))) np_jump(leave);
  rep &= k; /* only arrows pressed here repeat, not one still held from the launcher */
  if (e & arrows) rep |= e & arrows, repeat_at = now + 320;
  else if (rep && (int32_t)(now - repeat_at) >= 0) e |= rep, repeat_at = now + 80;
  held = k;
  pend |= e;
}

static void tick(uint32_t dt);
/* one frame: repaint what changed, at most 60 times a second. Timed from the
   refresh, so a long repaint does not make the next frame miss one. */
static void next_frame(void) {
  uint32_t t = (uint32_t)eadk_timing_millis();
  if (t - last_frame < 14) eadk_timing_msleep(14 - (t - last_frame));
  eadk_display_wait_for_vblank();
  last_frame = (uint32_t)eadk_timing_millis();
  for (int i = 0; i < ndr; i++) paint(dr[i][0], dr[i][1], dr[i][2] - dr[i][0], dr[i][3] - dr[i][1]);
  ndr = 0;
  t = (uint32_t)eadk_timing_millis();
  if (ticking) tick((uint32_t)min((int)(t - now), 100)); /* animations count too */
  now = t;
  poll();
}
static uint64_t take(void) {
  uint64_t e = pend;
  pend = 0;
  return e;
}
#define OK_KEYS (KEY(eadk_key_ok) | KEY(eadk_key_exe))

/* ------------------------------------------------------------------ widgets */
/* Windows 95 style: grey faces, bevelled edges, a navy title bar, and navy
   for the focus too. Widgets are painted one at a time (layer 2); their text
   is the firmware's font, drawn over them afterwards. */
enum { W_WIN, W_BTN, W_RADIO, W_CHECK, W_GROUP, W_ITEM, W_BACK };
#define F_ON 1 /* a radio button or a box that is on */
#define F_FOCUS 2
#define F_OFF 4 /* greyed out */
static int wt, wx, wy, ww, wh, wf;
static const char *wl;

static void button(int x, int y, int w, int h, int focus) {
  if (focus) frame(x, y, w, h, 1, BLACK), x++, y++, w -= 2, h -= 2;
  fill(x, y, w, h, focus ? NAVY : FACE);
  bevel(x, y, w, h, WHITE, BLACK);
  bevel(x + 1, y + 1, w - 2, h - 2, LIGHT, SHADOW);
}
/* a navy title bar and n of its buttons: close, maximize, minimize */
static void caption(int x, int y, int w, int n) {
  fill(x, y, w, 20, NAVY);
  for (int i = 0; i < n; i++) {
    int bx = x + w - 18 - i * 16 - (i > 0) * 2;
    button(bx, y + 3, 16, 14, 0);
    if (!i) glyph('x', bx + 5, y + 6, 1, BLACK, 0);
    else if (i == 1) frame(bx + 3, y + 5, 9, 9, 1, BLACK), fill(bx + 3, y + 6, 9, 1, BLACK);
    else fill(bx + 4, y + 12, 6, 2, BLACK);
  }
}
static void widget_draw(void) {
  int x = wx, y = wy, f = wf & F_FOCUS;
  fill(x, y, ww, wh, wt == W_ITEM && f ? NAVY : FACE);
  if (wt == W_WIN) {
    bevel(x, y, ww, wh, LIGHT, BLACK);
    bevel(x + 1, y + 1, ww - 2, wh - 2, WHITE, SHADOW);
    caption(x + 3, y + 3, ww - 6, 1);
    if (wf) { /* the question mark */
      ring(x + 30, y + 46, 0, 14, RGB(0x1040D0));
      ring(x + 30, y + 46, 13, 14, BLACK);
      glyph('?', x + 25, y + 39, 2, WHITE, 0);
    }
  } else if (wt == W_BTN) {
    button(x, y, ww, wh, f);
  } else if (wt == W_GROUP) {
    bevel(x, y + 7, ww - 1, wh - 8, SHADOW, WHITE);
    bevel(x + 1, y + 8, ww - 1, wh - 8, WHITE, SHADOW);
  } else if (wt == W_BACK) { /* a card back to pick, style in the high bits */
    int s = V.back;
    if (f) frame(x, y, ww, wh, 2, NAVY);
    V.back = (uint8_t)(wf >> 4), back(x + 4, y + 4), V.back = (uint8_t)s;
  } else if (wt != W_ITEM) {
    if (wt == W_RADIO) {
      ring(x + 6, y + 8, 0, 6, SHADOW);
      ring(x + 6, y + 8, 0, 5, WHITE);
      if (wf & F_ON) ring(x + 6, y + 8, 0, 2, BLACK);
    } else {
      bevel(x, y + 2, 13, 13, SHADOW, WHITE);
      bevel(x + 1, y + 3, 11, 11, BLACK, LIGHT);
      fill(x + 2, y + 4, 9, 9, wf & F_OFF ? FACE : WHITE);
      for (int i = 0; (wf & F_ON) && i < 7; i++) fill(x + 3 + i, y + 5 + (i <= 2 ? i + 2 : 6 - i), 1, 3, wf & F_OFF ? SHADOW : BLACK);
    }
    if (f) fill(x + 16, y, slen(wl) * 7 + 4, 16, NAVY);
  }
}
/* paints a widget, then its label */
static void widget(int t, int x, int y, int w, int h, const char *label, int f) {
  int l = layer;
  wt = t, wx = x, wy = y, ww = w, wh = h, wl = label, wf = f, layer = 2;
  paint(x, y, w, h);
  layer = l;
  if (!label) return;
  if (t == W_BTN) x += (w - slen(label) * 7) / 2 - 18, y += (h - 14) / 2 - 1;
  else if (t == W_GROUP) x -= 11, y--;
  else if (t == W_ITEM) y++;
  str(label, x + 18, y + 1, 0, f & F_OFF ? SHADOW : f & F_FOCUS ? WHITE : BLACK, f & F_FOCUS ? NAVY : FACE);
}
static void win(int x, int y, int w, int h, const char *title, int icon) {
  widget(W_WIN, x, y, w, h, 0, icon);
  str(title, x + 7, y + 6, 0, WHITE, NAVY);
}

/* A dialog box: text lines, then buttons (in a column when vert). Returns the
   button chosen, or -1 for Back. box keeps where it was. */
static int16_t box[4];
static int dialog(const char *title, const char *body, const char *const *btn, int nb, int vert, int icon) {
  int lines = 0, tw = 0, bw = 76, ix = icon ? 48 : 0;
  for (const char *s = body; s && *s; lines++) {
    int n = 0;
    while (s[n] && s[n] != '\n') n++;
    tw = max(tw, n * 7);
    s += n + (s[n] == '\n');
  }
  for (int i = 0; i < nb; i++) bw = max(bw, slen(btn[i]) * 7 + 28);
  int bb = vert ? bw : nb * bw + (nb - 1) * 10, w = max(max(tw + ix, bb) + 36, slen(title) * 7 + 44);
  int th = max(lines * 16, icon ? 36 : 0), h = 34 + th + (th ? 12 : 0) + (vert ? nb * 30 - 6 : 24) + 12;
  int x = (320 - w) / 2, y = (240 - h) / 2, by = y + 34 + th + (th ? 12 : 0), f = 0;
  box[0] = (int16_t)x, box[1] = (int16_t)y, box[2] = (int16_t)w, box[3] = (int16_t)h;
  ndr = 0, ticking = 0; /* nothing of the table shows through, and the clock stops */
  win(x, y, w, h, title, icon);
  char ln[48];
  for (int i = 0, n; body && *body; i++, body += n + (body[n] == '\n')) {
    for (n = 0; body[n] && body[n] != '\n' && n < 47; n++) ln[n] = body[n];
    ln[n] = 0;
    str(ln, x + 18 + ix, y + 34 + (th - lines * 16) / 2 + i * 16, 0, BLACK, FACE);
  }
  for (uint64_t e = 0;; e = take()) {
    int d = (e & KEY(vert ? eadk_key_down : eadk_key_right) ? 1 : 0) - (e & KEY(vert ? eadk_key_up : eadk_key_left) ? 1 : 0);
    for (int i = 0; i < nb; i++)
      if (!e || (d && (i == f || i == (f + d + nb) % nb)))
        widget(W_BTN, vert ? x + (w - bw) / 2 : x + (w - bb) / 2 + i * (bw + 10), vert ? by + i * 30 : by, bw, 24, btn[i],
               (i == (f + d + nb) % nb) * F_FOCUS);
    f = (f + d + nb) % nb;
    if (e & OK_KEYS) return f;
    if (e & KEY(eadk_key_back)) return -1;
    next_frame();
  }
}

/* ------------------------------------------------------------------ the table */
static int cp = T0 + 3, cd = 1; /* the cursor: pile, and cards picked with Up in a column */
static int sp = -1, sn;         /* the cards picked up: pile and count */
static int hint_p = -1;         /* the pile a hint points to */
static int nocur;               /* hides the cursor: dealing, winning */
static uint32_t hint_until, msg_until;
static const char *msg;

/* cards in flight: they are hidden in their pile until they land */
typedef struct {
  uint8_t c;
  int8_t t; /* frames flown; waits while negative */
  int16_t x0, y0, x1, y1;
} fly_t;
#define NFLY 28
static fly_t fl[NFLY];
static int nfl, dur = 9;
static uint64_t hid;

static void fly_pos(const fly_t *f, int *x, int *y) {
  int t = f->t * (2 * dur - f->t), d = dur * dur; /* easing out */
  *x = f->x0 + (f->x1 - f->x0) * t / d, *y = f->y0 + (f->y1 - f->y0) * t / d;
}
static int shown(int c) { return !(hid >> (c & 63) & 1); }

/* the cursor's rectangle: the cards it points at */
static void cur_rect(int *x, int *y, int *h) {
  int n = pn[cp], y1;
  pos(cp, max(n - (cp >= T0 ? cd : 1), 0), x, y);
  pos(cp, max(n - 1, 0), x, &y1);
  *h = y1 - *y + CH;
}
static void dirty_cursor(void) {
  int x, y, h;
  cur_rect(&x, &y, &h);
  dirty(x - 2, y - 2, CW + 4, h + 4);
}
static void set_cursor(int p, int d) {
  dirty_cursor();
  cp = p, cd = p >= T0 ? max(min(d, run(p)), 1) : 1;
  dirty_cursor();
}
static void say(const char *s) {
  msg = s, msg_until = now + 1800;
  dirty_status();
}

/* with Keep score, Vegas shows the bank, which already holds a finished game */
static int32_t shown_score(void) { return V.gscoring == 1 && V.keep ? V.bank + (V.live ? V.score : 0) : V.score; }

static void status_bar(void) {
  char s[64], *o = s;
  fill(0, STY, 320, 10, FACE);
  fill(0, STY, 320, 1, WHITE);
  o = num(cat(o, "Moves: "), V.moves);
  if (V.gscoring < 2) o = (V.gscoring ? money : num)(cat(o, "   Score: "), shown_score());
  if (V.gtimed) o = num(cat(o, "   Time: "), (int32_t)(V.ms / 1000));
  text(s, 316 - (int)(o - s) * 6, STY + 2, 1, BLACK);
  if (msg) fill(0, STY + 1, slen(msg) * 6 + 8, 9, FACE), text(msg, 4, STY + 2, 1, BLACK);
}

static int can_recycle(void) { return V.gscoring != 1 || V.passes < V.gdraw - 1; }

static void game_scene(void) {
  fill(rx, ry, rw, rh, FELT);
  if (ry < TABY) {
    int k = pn[STOCK] - 1, x, y;
    while (k >= 0 && !shown(pl[STOCK][k])) k--;
    if (k >= 0) { /* the deck, a little thicker when it holds more */
      for (int i = min(k / 8, 2); i; i--) fill(COLX(0) + CW - 1 + i, TOPY + 1 + i, 1, CH - 2, BLACK), fill(COLX(0) + 1 + i, TOPY + CH - 1 + i, CW - 2, 1, BLACK);
      card(pl[STOCK][k], COLX(0), TOPY, 0);
    } else { /* the empty stock: O to deal again, X when that is over */
      slot(COLX(0), TOPY);
      if (can_recycle()) {
        ring(COLX(0) + 19, TOPY + 26, 8, 11, RGB(0x30E030));
      } else {
        for (int i = -8; i <= 8; i++) fill(COLX(0) + 18 + i, TOPY + 25 + i, 3, 3, RED), fill(COLX(0) + 18 + i, TOPY + 25 - i, 3, 3, RED);
      }
    }
    for (k = max(pn[WASTE] - fan() - 1, 0); k < pn[WASTE]; k++)
      if (shown(pl[WASTE][k])) pos(WASTE, k, &x, &y), card(pl[WASTE][k], x, y, sp == WASTE && k == pn[WASTE] - 1 ? 0xFFFF : 0);
    for (int p = F0; p < T0; p++) {
      k = pn[p] - 1;
      while (k >= 0 && !shown(pl[p][k])) k--;
      if (k >= 0) card(pl[p][k], COLX(p + 1), TOPY, sp == p && k == pn[p] - 1 ? 0xFFFF : 0);
      else slot(COLX(p + 1), TOPY);
    }
  }
  for (int p = T0; p < NP; p++) {
    int x, y;
    if (!pn[p] || !shown(pl[p][0])) slot(COLX(p - T0), TABY);
    for (int k = 0; k < pn[p]; k++, sq = 6)
      if (shown(pl[p][k])) sq = k + 1 < pn[p] ? od[p - T0] : 6, pos(p, k, &x, &y), card(pl[p][k], x, y, p == sp && k >= pn[p] - sn ? 0xFFFF : 0);
  }
  if (hint_p >= 0 && (now / 200 & 1)) {
    int x, y;
    pos(hint_p, max(pn[hint_p] - 1, 0), &x, &y);
    frame(x - 2, y - 2, CW + 4, CH + 4, 2, RGB(0x40E8FF));
  }
  if (!nocur) {
    int x, y, h;
    cur_rect(&x, &y, &h);
    frame(x - 2, y - 2, CW + 4, h + 4, 2, GOLD);
  }
  for (int i = 0; i < nfl; i++) {
    int x, y;
    if (fl[i].t >= 0) fly_pos(&fl[i], &x, &y), card(fl[i].c, x, y, 0);
  }
  if (ry + rh > STY) status_bar();
}

/* ------------------------------------------------------------------ moves */
/* flies every card in flight to its place; skip lets a key land them all */
static void animate(int skip) {
  while (nfl) {
    int j = 0, x, y;
    for (int i = 0; i < nfl; i++) {
      fly_t *f = &fl[i];
      if (f->t >= 0) fly_pos(f, &x, &y), dirty(x, y, CW, CH);
      if (++f->t >= dur || (skip && pend)) {
        hid &= ~(1ull << (f->c & 63));
        dirty(f->x1, f->y1, CW, CH);
        continue;
      }
      if (f->t >= 0) fly_pos(f, &x, &y), dirty(x, y, CW, CH);
      fl[j++] = *f;
    }
    nfl = j;
    if (skip && pend) pend = 0;
    next_frame();
  }
}
static void launch(int c, int x0, int y0, int x1, int y1, int t) {
  if (nfl == NFLY) return;
  fl[nfl++] = (fly_t){(uint8_t)c, (int8_t)t, (int16_t)x0, (int16_t)y0, (int16_t)x1, (int16_t)y1};
  hid |= 1ull << (c & 63);
}

/* Moving cards: begin() notes where they are and moves them in the state,
   fly() sends them from there to their new places. turn: one at a time,
   turned over (the stock and the waste); else as a block. */
static int16_t mx[NFLY], my[NFLY];
static int mn, mt, mturn;
static void begin(int f, int t, int n, int turn) {
  for (int k = 0; k < n && k < NFLY; k++) {
    int x, y;
    pos(f, turn ? pn[f] - 1 - k : pn[f] - n + k, &x, &y);
    mx[k] = (int16_t)x, my[k] = (int16_t)y;
  }
  dirty_pile(f), dirty_pile(t);
  for (int k = 0; k < n; k++) {
    int c = pl[f][turn ? pn[f] - 1 - k : pn[f] - n + k];
    pl[t][pn[t] + k] = (uint8_t)(turn ? (c & 63) | (t == WASTE ? UP : 0) : c);
  }
  pn[t] = (uint8_t)(pn[t] + n), pn[f] = (uint8_t)(pn[f] - n);
  mn = n, mt = t, mturn = turn;
}
static void fly(int frames, int stagger) {
  relayout();
  dur = frames;
  for (int k = 0; k < mn && k < NFLY; k++) {
    int i = pn[mt] - mn + k, x, y;
    pos(mt, i, &x, &y);
    launch(pl[mt][i], mx[k], my[k], x, y, -k * stagger);
  }
  animate(0);
  dirty_pile(mt);
}

/* the undo history: each move, and what it changed */
typedef struct {
  uint8_t f, t, n, fl; /* fl: 1 turned one at a time, 2 turned a card up after; the waste's spread << 4 */
  int16_t ds;          /* the score it gave */
} undo_t;
static undo_t U[256];
static uint8_t ut;
static int nu;
static void push_undo(int f, int t, int n, int flags, int ds) {
  U[ut++] = (undo_t){(uint8_t)f, (uint8_t)t, (uint8_t)n, (uint8_t)(flags | V.wfan << 4), (int16_t)ds};
  nu = min(nu + 1, 256);
}

static int add_score(int d) { /* what actually changed: Standard stops at 0 */
  int32_t s = V.score + d;
  if (!V.gscoring && s < 0) s = 0;
  d = (int)(s - V.score);
  V.score = s;
  dirty_status();
  return d;
}
/* the clock, while a game is on; Standard loses 2 points every 10 seconds */
static void tick(uint32_t dt) {
  if (!V.live || !V.started) return;
  uint32_t t0 = V.ms;
  V.ms += dt;
  if (V.ms / 10000 != t0 / 10000 && !V.gscoring && V.gtimed) add_score(-2);
  if (V.ms / 1000 != t0 / 1000 && V.gtimed) dirty_status();
}
static void counted(void) {
  if (!V.started) V.started = 1, V.played++;
  V.moves++;
  hint_p = -1;
  dirty_status();
}
static void deselect(void) {
  if (sp >= 0) dirty_pile(sp);
  sp = -1;
}

static void move(int f, int n, int t, int frames) {
  push_undo(f, t, n, 0, 0);
  undo_t *u = &U[(uint8_t)(ut - 1)];
  begin(f, t, n, 0);
  if (f == WASTE && V.wfan > 1) V.wfan--;
  u->ds = (int16_t)add_score(!V.gscoring ? (t < T0 ? 10 : f == WASTE ? 5 : f < T0 ? -15 : 0) :
                             V.gscoring == 1 ? (t < T0 ? 5 : f >= F0 && f < T0 ? -5 : 0) : 0);
  if (f >= T0 && pn[f] && !(pl[f][pn[f] - 1] & UP)) { /* turn the card underneath */
    pl[f][pn[f] - 1] |= UP;
    u->fl |= 2;
    if (!V.gscoring) u->ds = (int16_t)(u->ds + add_score(5));
  }
  counted();
  fly(frames, 0);
}

static void draw_cards(void) {
  if (sp == WASTE) deselect();
  if (pn[STOCK]) {
    int n = min(V.gdraw, pn[STOCK]);
    push_undo(STOCK, WASTE, n, 1, 0);
    begin(STOCK, WASTE, n, 1);
    V.wfan = (uint8_t)n;
  } else if (!pn[WASTE]) {
    return;
  } else if (!can_recycle()) {
    say("No more passes");
    return;
  } else { /* the waste goes back to the stock */
    int d = V.gscoring ? 0 : V.gdraw == 1 ? -100 : V.passes >= 2 ? -20 : 0;
    push_undo(WASTE, STOCK, pn[WASTE], 1, 0);
    begin(WASTE, STOCK, pn[WASTE], 1);
    U[(uint8_t)(ut - 1)].ds = (int16_t)add_score(d);
    V.passes++, V.wfan = 0;
    counted();
    fly(8, 0);
    return;
  }
  counted();
  fly(8, 3);
}

static void undo(void) {
  if (!nu) {
    say("Nothing to undo");
    return;
  }
  deselect();
  nu--;
  undo_t u = U[--ut];
  if (u.fl & 2) pl[u.f][pn[u.f] - 1] &= (uint8_t)~UP;
  begin(u.t, u.f, u.n, u.fl & 1);
  V.wfan = u.fl >> 4;
  add_score(-u.ds);
  if (u.f == WASTE && u.t == STOCK) V.passes--;
  if (V.moves) V.moves--;
  hint_p = -1;
  fly(8, 0);
}

/* the lowest card that can go to a foundation, or 0 */
static int to_foundation(int frames) {
  int best = 99, bf = 0, bt = 0;
  for (int f = WASTE; f < NP; f++) {
    if (f >= F0 && f < T0) continue;
    for (int t = F0; t < T0; t++)
      if (legal(f, 1, t) && RANK(pl[f][pn[f] - 1]) < best) best = RANK(pl[f][pn[f] - 1]), bf = f, bt = t;
  }
  if (best == 99) return 0;
  move(bf, 1, bt, frames);
  return 1;
}

static void hint(void) {
  int f, t, n = 1;
  for (f = WASTE; f < NP; f++)
    for (t = F0; t < T0; t++)
      if ((f < F0 || f >= T0) && legal(f, 1, t)) goto found;
  for (f = T0; f < NP; f++) { /* a run that turns a card up or empties its column */
    n = run(f);
    int base = pn[f] - n;
    if (n && (base ? !(pl[f][base - 1] & UP) : 1))
      for (t = T0; t < NP; t++)
        if (legal(f, n, t) && (base || pn[t])) goto found;
  }
  for (f = WASTE, n = 1, t = T0; t < NP; t++)
    if (legal(WASTE, 1, t)) goto found;
  if (pn[STOCK] || (pn[WASTE] && can_recycle())) {
    set_cursor(STOCK, 1);
    say("Draw a card");
  } else {
    say("No more moves");
  }
  return;
found:
  deselect();
  set_cursor(f, n);
  hint_p = t, hint_until = now + 1600;
  say("Hint");
}

/* OK: pick up, put down, or draw */
static void act(void) {
  if (cp == STOCK) {
    draw_cards();
    return;
  }
  int n = cp >= T0 ? cd : 1;
  if (sp < 0) {
    if (n <= run(cp)) sp = cp, sn = n, dirty_pile(cp);
    return;
  }
  if (cp == sp) { /* a second OK sends the card up, like a double click */
    int f = sp;
    deselect();
    for (int t = F0; t < T0 && sn == 1; t++)
      if (legal(f, 1, t)) {
        move(f, 1, t, 9);
        return;
      }
    return;
  }
  for (n = 1; n <= run(sp) && !legal(sp, n, cp); n++) {} /* only one count can fit */
  if (n > run(sp)) {
    say("Not there");
    return;
  }
  int f = sp;
  deselect();
  move(f, n, cp, 9);
}

/* ------------------------------------------------------------------ games */
static void deal(uint32_t s) {
  uint8_t d[52];
  for (int i = 0; i < 52; i++) d[i] = (uint8_t)i;
  V.seed = s;
  s |= 1;
  for (int i = 51; i > 0; i--) {
    s ^= s << 13, s ^= s >> 17, s ^= s << 5;
    int j = (int)(s % (uint32_t)(i + 1));
    uint8_t c = d[i];
    d[i] = d[j], d[j] = c;
  }
  for (int p = 0; p < NP; p++) pn[p] = 0;
  int k = 0;
  for (int r = 0; r < 7; r++)
    for (int c = r; c < 7; c++) pl[T0 + c][pn[T0 + c]++] = (uint8_t)(d[k++] | (c == r ? UP : 0));
  while (k < 52) pl[STOCK][pn[STOCK]++] = d[k++];
  V.live = 1, V.started = 0, V.moves = 0, V.ms = 0, V.passes = 0, V.wfan = 0;
  V.gdraw = V.draw, V.gscoring = V.scoring, V.gtimed = V.timed;
  V.score = V.gscoring == 1 ? -52 : 0;
  nu = 0, sp = -1, hint_p = -1, cd = 1;
  relayout();
}
/* the cards fly from the stock to the columns, row by row */
static void deal_fly(void) {
  for (int r = 0, k = 0; r < 7; r++)
    for (int c = r; c < 7; c++, k++) {
      int x, y;
      pos(T0 + c, r, &x, &y);
      launch(pl[T0 + c][r], COLX(0), TOPY, x, y, -k * 2);
    }
  dirty(0, 0, 320, 240);
  dur = 10;
  animate(1);
}

static void save(void) {
  V.magic = 'S', V.version = 1;
  for (int p = 0, k = 0; p < NP; p++) {
    V.n[p] = pn[p];
    for (int i = 0; i < pn[p] && k < 52; i++) V.cards[k++] = pl[p][i];
  }
  ef_write(SAVE_NAME, &V, sizeof V);
}

/* reads the save, keeping only what makes sense */
static void load(void) {
  uint32_t n = 0;
  const uint8_t *d = ef_read(SAVE_NAME, &n);
  V.draw = 3, V.timed = 1;
  if (!d || n != sizeof V || d[0] != 'S' || d[1] != 1) return;
  for (uint32_t i = 0; i < n; i++) ((uint8_t *)&V)[i] = d[i];
  if (V.draw < 1 || V.draw > 3) V.draw = 3;
  V.scoring %= 3, V.timed &= 1, V.keep &= 1;
  uint64_t seen = 0;
  int k = 0, ok = V.live == 1 && V.gdraw >= 1 && V.gdraw <= 3 && V.gscoring < 3 && V.gtimed < 2;
  for (int p = 0; p < NP && ok; p++) {
    if (V.n[p] > 52 - k) ok = 0;
    for (int i = 0; ok && i < V.n[p]; i++, k++) {
      int c = V.cards[k] & 63, up = p == WASTE || (p >= F0 && p < T0) || (p >= T0 && (V.cards[k] & UP || i == V.n[p] - 1 || (i && pl[p][i - 1] & UP)));
      if (c >= 52 || seen >> c & 1) ok = 0;
      if (p >= F0 && p < T0 && (RANK(c) != i || (i && SUIT(c) != SUIT(pl[p][0])))) ok = 0;
      seen |= 1ull << c;
      pl[p][i] = (uint8_t)(c | (up ? UP : 0));
    }
    pn[p] = V.n[p];
  }
  if (!ok || k != 52) {
    V.live = 0;
    for (int p = 0; p < NP; p++) pn[p] = 0;
  }
  V.started &= 1;
}

static void abandon(void) {
  if (V.live && V.started) {
    V.streak = 0;
    if (V.gscoring == 1) V.bank += V.score;
  }
  V.live = 0;
}

/* ------------------------------------------------------------------ the win */
/* one card straight onto the screen, over whatever is there */
static void stamp(int c, int x, int y) {
  rx = max(x, 0), ry = max(y, 0), rw = min(x + CW, 320) - rx, rh = min(y + CH, 240) - ry;
  if (rw <= 0 || rh <= 0) return;
  eadk_rect_t r = {(uint16_t)rx, (uint16_t)ry, (uint16_t)rw, (uint16_t)rh};
  eadk_display_pull_rect(r, buf);
  if (c < 0) fill(x, y, CW, CH, FELT), slot(x, y);
  else card(c, x, y, 0);
  eadk_display_push_rect(r, buf);
}

/* The famous ending: the cards leave the foundations one by one, kings
   first, bounce along the bottom of the screen and leave a trail. */
static void bounce(void) {
  ndr = 0;
  take();
  for (int r = 12; r >= 0; r--)
    for (int p = F0; p < T0; p++) {
      if (!pn[p]) continue;
      int c = pl[p][--pn[p]], x = COLX(p + 1) * 16, y = TOPY * 16;
      int vx = (int)(rnd() % 40 + 20) * (rnd() & 1 ? 1 : -1), vy = -(int)(rnd() % 90);
      stamp(pn[p] ? pl[p][pn[p] - 1] : -1, COLX(p + 1), TOPY);
      while (x > -CW * 16 && x < 320 * 16) {
        for (int s = 0; s < 5; s++) {
          x += vx, vy += 9, y += vy;
          if (y > (240 - CH) * 16) y = (240 - CH) * 16, vy = -vy * 13 / 16;
          stamp(c, x >> 4, y >> 4);
        }
        next_frame();
        if (take()) return;
      }
    }
}

static int won(void) { return pn[F0] + pn[F0 + 1] + pn[F0 + 2] + pn[F0 + 3] == 52; }

/* after every move: finish by itself once every card is face up */
static void settle(void) {
  if (!pn[STOCK] && !pn[WASTE] && !won()) {
    for (int i = 0; i < 7; i++)
      if (fd[i]) return;
    deselect();
    while (to_foundation(6)) {}
  }
}

static void finish(void) {
  uint32_t secs = V.ms / 1000;
  V.live = 0, V.won++, V.streak++;
  if (V.streak > V.best_streak) V.best_streak = V.streak;
  if (!V.gscoring && V.gtimed && secs >= 30) V.score += (int32_t)(700000 / secs); /* Windows' bonus */
  if (V.gscoring == 1) V.bank += V.score;
  if (!V.best_time || secs < V.best_time) V.best_time = (uint16_t)min((int)secs, 65535);
  if (!V.gscoring && V.score > V.best_score) V.best_score = V.score;
  save();
}

/* ------------------------------------------------------------------ play */
static const char *const yes_no[] = {"Yes", "No"}, *const ok_btn[] = {"OK"};

static void new_deal(uint32_t s) {
  abandon();
  deal(s);
  layer = 0;
  set_cursor(T0 + 3, 1);
  deal_fly();
}

/* the game, until the player leaves it */
static void play(void) {
  static const char *const pause_items[] = {"Resume", "Restart", "New deal", "Quit game"};
  layer = 0, sp = -1, hint_p = -1, msg = 0;
  relayout();
  set_cursor(cp, cd);
  dirty(0, 0, 320, 240);
  uint32_t last = now;
  for (;;) {
    next_frame();
    uint64_t e = take();
    uint32_t dt = min((int)(now - last), 100);
    last = now;
    if (V.started) { /* the clock; Standard loses 2 points every 10 seconds */
      uint32_t t0 = V.ms;
      V.ms += dt;
      if (V.ms / 10000 != t0 / 10000 && !V.gscoring && V.gtimed) add_score(-2);
      if (V.ms / 1000 != t0 / 1000 && V.gtimed) dirty_status();
    }
    if (msg && (int32_t)(now - msg_until) > 0) msg = 0, dirty_status();
    if (hint_p >= 0) {
      if ((int32_t)(now - hint_until) > 0) dirty_pile(hint_p), hint_p = -1;
      else if (now / 200 != (now - dt) / 200) dirty_pile(hint_p);
    }
    int lr = (e & KEY(eadk_key_right) ? 1 : 0) - (e & KEY(eadk_key_left) ? 1 : 0);
    if (lr) {
      if (cp >= T0) set_cursor(T0 + (cp - T0 + lr + 7) % 7, 1);
      else set_cursor((cp + lr + 6) % 6, 1); /* stock, waste, foundations */
    }
    if (e & KEY(eadk_key_up)) {
      if (cp >= T0 && sp < 0 && cd < run(cp)) set_cursor(cp, cd + 1);
      else if (cp >= T0) set_cursor(cp == T0 ? STOCK : cp < T0 + 3 ? WASTE : cp - 7, 1);
    }
    if (e & KEY(eadk_key_down)) {
      if (cp >= T0) set_cursor(cp, cd - 1);
      else set_cursor(cp == STOCK ? T0 : cp == WASTE ? T0 + 1 : cp + 7, 1);
    }
    if (e & KEY(eadk_key_ok)) act();
    if (e & KEY(eadk_key_exe)) draw_cards();
    if (e & KEY(eadk_key_toolbox)) {
      deselect();
      if (!to_foundation(7)) say("Nothing to play");
      while (to_foundation(7)) {}
    }
    if (e & KEY(eadk_key_backspace)) undo();
    if (e & KEY(eadk_key_shift)) hint();
    if (e & KEY(eadk_key_back)) {
      if (sp >= 0) {
        deselect();
      } else {
        for (;;) {
          int r = dialog("Paused", 0, pause_items, 4, 1, 0);
          if (r == 3) {
            if (!dialog("Solitaire", "Quit game?", yes_no, 2, 0, 1)) np_jump(leave); /* saves, then leaves */
            paint(0, 0, 320, 240);
            continue;
          }
          dirty(0, 0, 320, 240);
          if (r == 1 || r == 2) new_deal(r == 1 ? V.seed : rnd());
          break;
        }
        last = now;
      }
    }
    if (cp >= T0 && cd > max(run(cp), 1)) set_cursor(cp, cd); /* the column changed */
    settle();
    if (won() && V.live) {
      char s[96], *o = s;
      finish();
      o = num(cat(o, "You win!\nTime: "), (int32_t)(V.ms / 1000));
      o = num(cat(o, " seconds\nMoves: "), V.moves);
      if (V.gscoring < 2) o = (V.gscoring ? money : num)(cat(o, "\nScore: "), shown_score());
      cat(o, "\n\nDeal again?");
      next_frame();
      bounce();
      if (dialog("Solitaire", s, yes_no, 2, 0, 0)) return;
      new_deal(rnd());
      last = now;
    }
  }
}

/* ------------------------------------------------------------------ menus */
static const char *const menu_items[] = {"Continue", "New game", "Options", "Statistics", "How to play"};
static int menu_first, menu_at;
static int menu_y(int i) { return 131 - (5 - menu_first) * 15 + (i - menu_first) * 30; }

static void title_scene(void) {
  static const uint8_t hand[5] = {0, (12 * 4 + 1) | UP, (11 * 4 + 2) | UP, (10 * 4 + 3) | UP, UP};
  fill(rx, ry, rw, rh, FELT);
  text("Solitaire", 57, 14, 4, RGB(0x004000));
  text("Solitaire", 54, 11, 4, WHITE);
  for (int i = 0; i < 5; i++) card(hand[i], 14 + i * 26, 96 + (i - 2) * (i - 2) * 3, 0);
  for (int i = menu_first; i < 5; i++) button(184, menu_y(i), 124, 24, i == menu_at);
  char m[24], *o = num(cat(m, "Draw "), V.draw);
  cat(o, V.scoring ? V.scoring == 1 ? ", Vegas" : "" : ", Standard");
  text("Based on Tatone26's version", 4, 229, 1, RGB(0x9CD89C));
  text(m, 316 - slen(m) * 6, 229, 1, RGB(0x9CD89C));
}
static void scene(void) {
  if (layer == 2) widget_draw();
  else if (layer == 1) title_scene();
  else game_scene();
}
static void title_labels(void) {
  for (int i = menu_first; i < 5; i++)
    str(menu_items[i], 184 + (124 - slen(menu_items[i]) * 7) / 2, menu_y(i) + 5, 0, i == menu_at ? WHITE : BLACK,
        i == menu_at ? NAVY : FACE);
}

static void options(void) {
  static const char *const lbl[8] = {"One (Easy)", "Two (Normal)", "Three (Hard)", "Timed game", "Standard", "Vegas", "None", "Keep score"};
  int x = 22, y = 28, f = 0;
  ndr = 0;
  win(x, y, 276, 184, "Options", 0);
  widget(W_GROUP, x + 10, y + 32, 124, 80, "Draw", 0);
  widget(W_GROUP, x + 142, y + 32, 124, 80, "Scoring", 0);
  for (uint64_t e = 0;; e = take()) {
    int o = f;
    if (e & KEY(eadk_key_up)) f = f == 8 ? 3 : f % 4 ? f - 1 : f;
    if (e & KEY(eadk_key_down)) f = f == 8 ? 8 : f % 4 == 3 ? 8 : f + 1;
    if ((e & (KEY(eadk_key_left) | KEY(eadk_key_right))) && f < 8) f ^= 4;
    if (e & OK_KEYS) {
      if (f == 8) break;
      if (f == 3) V.timed ^= 1;
      else if (f == 7) V.keep ^= 1;
      else if (f < 3) V.draw = (uint8_t)(f + 1);
      else V.scoring = (uint8_t)(f - 4);
    }
    if (e & KEY(eadk_key_back)) break;
    for (int i = 0; i < 9; i++) {
      if (e && i != f && i != o && !(e & OK_KEYS)) continue;
      if (i == 8) {
        widget(W_BTN, x + 103, y + 150, 70, 24, "OK", f == 8);
        continue;
      }
      int on = i == 3 ? V.timed : i == 7 ? V.keep : i < 3 ? V.draw == i + 1 : V.scoring == i - 4;
      widget(i % 4 == 3 ? W_CHECK : W_RADIO, x + 20 + (i / 4) * 132, y + 50 + (i % 4) * 20 + (i % 4 == 3) * 10, 110, 16,
             lbl[i], on | (i == f) << 1);
    }
    next_frame();
  }
  save();
}

static void stats(void) {
  char s[200], *o = s;
  o = num(cat(o, "Games played: "), V.played);
  o = num(cat(o, "\nGames won: "), V.won);
  if (V.played) o = cat(num(cat(o, " ("), V.won * 100 / V.played), "%)");
  o = num(cat(o, "\nStreak: "), V.streak);
  o = num(cat(o, "   Best: "), V.best_streak);
  o = cat(o, "\nBest time: ");
  if (V.best_time) {
    o = num(o, V.best_time / 60);
    *o++ = ':', *o++ = (char)('0' + V.best_time % 60 / 10), *o++ = (char)('0' + V.best_time % 10), *o = 0;
  } else {
    o = cat(o, "-");
  }
  o = num(cat(o, "\nBest score: "), V.best_score);
  money(cat(o, "\nVegas bank: "), V.bank);
  dialog("Statistics", s, ok_btn, 1, 0, 0);
}

static void help(void) {
  dialog("How to play",
         "Build four piles, ace to king, one\n"
         "suit each. In the columns, go down\n"
         "and alternate red and black.\n"
         "OK         Pick up, put down\n"
         "OK twice   Send to a foundation\n"
         "Up, Down   Pick more or fewer\n"
         "EXE        Draw from the deck\n"
         "Toolbox    Play all you can up\n"
         "Backspace  Undo      Shift  Hint",
         ok_btn, 1, 0, 0);
}

int main(void) {
  np_app_begin();
  if (np_save_jump(leave)) {
    save();
    return np_app_end();
  }
  now = last_frame = (uint32_t)eadk_timing_millis();
  seed ^= eadk_random() ^ now;
  load();
  held = eadk_keyboard_scan();
  for (;;) {
    menu_first = V.live ? 0 : 1;
    menu_at = max(menu_at, menu_first);
    if (menu_at > 4) menu_at = menu_first;
    layer = 1;
    paint(0, 0, 320, 240);
    title_labels();
    for (uint64_t e = 0;; e = take()) {
      int d = (e & KEY(eadk_key_down) ? 1 : 0) - (e & KEY(eadk_key_up) ? 1 : 0);
      if (d) {
        int o = menu_at;
        menu_at = menu_first + (menu_at - menu_first + d + 5 - menu_first) % (5 - menu_first);
        for (int i = 0; i < 2; i++) {
          int k = i ? menu_at : o;
          widget(W_BTN, 184, menu_y(k), 124, 24, menu_items[k], k == menu_at ? F_FOCUS : 0);
        }
      }
      if (e & OK_KEYS) break;
      if ((e & KEY(eadk_key_back)) && !dialog("Solitaire", "Quit game?", yes_no, 2, 0, 1)) np_jump(leave);
      if (e & KEY(eadk_key_back)) layer = 1, paint(0, 0, 320, 240), title_labels();
      next_frame();
    }
    if (menu_at == 0) play();
    else if (menu_at == 1) new_deal(rnd()), play();
    else if (menu_at == 2) options();
    else if (menu_at == 3) stats();
    else help();
  }
}
