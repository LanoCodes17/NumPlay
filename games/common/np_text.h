/* Text in other languages: NumPlay-French.nwa, NumPlay-Chinese.nwa and the
 * games on their own in those languages are built from the same sources.
 *
 * T("...") marks a text players read. In English it is the text itself; for a
 * language build, tools/lang.py copies the sources and puts each text's
 * translation (from games/<game>/lang/<code>.txt) in its place, so every
 * file carries only its own language. Sources and translations are UTF-8.
 *
 * Drawing them, each game with its own font:
 * - np_utf8() reads one letter (a code point) at a time;
 * - accented Latin letters (French) are the game's own letter with an accent
 *   drawn over it (or a cedilla under it): np_latin() names both, and
 *   np_accent() draws the accent at the game's scale;
 * - other letters (Chinese) come from a 12-pixel font (Fusion Pixel, SIL Open
 *   Font License): np_xglyph() gives a letter's 1-bit rows, from a table that
 *   tools/lang.py makes with only the letters the build uses; the launcher
 *   holds it for all its games (launcher/src/np_xfont.h).
 *
 * Only NumPlay itself is built in other languages. NP_TEXT_EXTRA is 1 there
 * (tools/lang.py sets it): code that handles letters beyond ASCII sits behind
 * it, so the English apps carry none of it. */
#ifndef NP_TEXT_H
#define NP_TEXT_H
#include <stdbool.h>
#include <stdint.h>

#define T(s) (s)
#define NP_TEXT_EXTRA 0

/* The next code point of a UTF-8 string, *s moved past it (0 at the end). */
static inline uint32_t np_utf8(const char **s) {
  const uint8_t *p = (const uint8_t *)*s;
  uint32_t c = p[0];
  if (c < 0x80) {
    if (c) (*s)++;
    return c;
  }
  int n = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : c >= 0xC0 ? 1 : 0;
  c &= 0x3F >> n;
  for (int i = 1; i <= n; i++) {
    if ((p[i] & 0xC0) != 0x80) {
      (*s) += i;
      return '?';
    }
    c = c << 6 | (p[i] & 0x3F);
  }
  (*s) += n + 1;
  return n ? c : '?';
}

/* The cells a text takes in a fixed-width font (games drawing with the firmware's font, 7 or 10 pixels a
 * letter): a letter each, two for a Chinese one (launcher/src/compat.c draws them in two cells). */
static inline int np_text_cells(const char *s) {
  int n = 0;
  while (*s) {
    uint32_t c = NP_TEXT_EXTRA ? np_utf8(&s) : (uint8_t)*s++;
    n += c >= 0x2E80 ? 2 : 1;
  }
  return n;
}

/* Accents of np_latin() */
enum { NP_ACC_NONE, NP_ACC_ACUTE, NP_ACC_GRAVE, NP_ACC_CIRC, NP_ACC_DIAER, NP_ACC_CEDIL, NP_ACC_TILDE, NP_ACC_RING };

/* A Latin letter with an accent: returns the accent, *base the plain letter
 * (0 when cp is no such letter). Ligatures (œ, æ, ß) and a few signs give
 * *second too, a letter to draw after the first. Guillemets, typographic
 * quotes and others give plain ASCII stand-ins. */
static inline int np_latin(uint32_t cp, char *base, char *second) {
  static const char letters[] = "AAAAAAACEEEEIIIIDNOOOOOxOUUUUYTsaaaaaaaceeeeiiiidnooooo/ouuuuyty";
  static const uint8_t accents[] = {
      2, 1, 3, 6, 4, 7, 0, 5, 2, 1, 3, 4, 2, 1, 3, 4, 0, 6, 2, 1, 3, 6, 4, 0, 0, 2, 1, 3, 4, 1, 0, 0,
      2, 1, 3, 6, 4, 7, 0, 5, 2, 1, 3, 4, 2, 1, 3, 4, 0, 6, 2, 1, 3, 6, 4, 0, 0, 2, 1, 3, 4, 1, 0, 4};
  *second = 0;
  if (cp >= 0xC0 && cp <= 0xFF) {
    *base = letters[cp - 0xC0];
    if (cp == 0xC6 || cp == 0xE6) *second = cp == 0xC6 ? 'E' : 'e';
    if (cp == 0xDF) *second = 's';
    return accents[cp - 0xC0];
  }
  *base = 0;
  switch (cp) {
    case 0x152: *base = 'O', *second = 'E'; break;
    case 0x153: *base = 'o', *second = 'e'; break;
    case 0x178: *base = 'Y'; return NP_ACC_DIAER;
    case 0xAB: *base = '<', *second = '<'; break; /* « */
    case 0xBB: *base = '>', *second = '>'; break; /* » */
    case 0xB0: *base = 'o'; break;                /* ° */
    case 0xA0: case 0x202F: *base = ' '; break;   /* (no-break spaces) */
    case 0x2018: case 0x2019: *base = '\''; break;
    case 0x201C: case 0x201D: *base = '"'; break;
    case 0x2026: *base = '.', *second = '.'; break;
    case 0x2013: case 0x2014: *base = '-'; break;
  }
  return NP_ACC_NONE;
}

/* Draws an accent 5 x 2 font pixels big, its top-left at (x, y), each font
 * pixel a scale x scale square, with plot(x, y, ctx) per screen pixel. The
 * game knows its letters: the accent goes centered over a letter (or under
 * it, for a cedilla), in the rows its font leaves free above lowercase
 * letters, or on the first rows of a capital, as small capitals do. */
static inline void np_accent(int acc, int x, int y, int scale, void (*plot)(int x, int y, void *ctx), void *ctx) {
  /* bits 0-4 the upper row, 5-9 the lower one, left to right */
  static const uint16_t marks[] = {0, 0x088, 0x082, 0x144, 0x140, 0x0C4, 0x154, 0x14E};
  if (acc <= NP_ACC_NONE || acc > NP_ACC_RING) return;
  for (int j = 0; j < 2; j++)
    for (int i = 0; i < 5; i++)
      if (marks[acc] >> (j * 5 + i) & 1)
        for (int a = 0; a < scale; a++)
          for (int b = 0; b < scale; b++) plot(x + i * scale + b, y + j * scale + a, ctx);
}

/* ---- the 12-pixel font */
typedef struct {
  uint16_t cp;        /* (Chinese and its punctuation are all below 0x10000) */
  uint8_t adv, w, h;  /* advance; the bitmap's size */
  int8_t x, y;        /* the bitmap's top-left from the pen, y down from the top of a 12-pixel line */
  uint16_t off;       /* its first bit in rows: w x h bits, row by row, the first pixel first */
} np_xglyph_t;
typedef struct {
  uint32_t count;
  const np_xglyph_t *glyphs;  /* sorted by code point */
  const uint8_t *rows;        /* bits, lowest first in each byte */
} np_xfont_t;

extern const np_xfont_t np_xfont; /* the launcher's (launcher/src/np_xfont.h) */

/* The glyph of cp in the 12-pixel font, or NULL. */
static inline const np_xglyph_t *np_xglyph(uint32_t cp) {
  uint32_t lo = 0, hi = np_xfont.count;
  while (lo < hi) {
    uint32_t m = (lo + hi) / 2;
    if (np_xfont.glyphs[m].cp < cp) lo = m + 1;
    else hi = m;
  }
  return lo < np_xfont.count && np_xfont.glyphs[lo].cp == cp ? &np_xfont.glyphs[lo] : 0;
}

/* Draws cp from the 12-pixel font with its line's top at (x, top), each font
 * pixel a scale x scale square; returns the advance (0 if the font has none). */
static inline int np_xdraw(uint32_t cp, int x, int top, int scale, void (*plot)(int x, int y, void *ctx), void *ctx) {
  const np_xglyph_t *g = np_xglyph(cp);
  if (!g) return 0;
  uint32_t bit = (uint32_t)g->off * 8;
  for (int j = 0; j < g->h; j++)
    for (int i = 0; i < g->w; i++, bit++)
      if (np_xfont.rows[bit >> 3] >> (bit & 7) & 1)
        for (int a = 0; a < scale; a++)
          for (int b = 0; b < scale; b++) plot(x + (g->x + i) * scale + b, top + (g->y + j) * scale + a, ctx);
  return g->adv * scale;
}
static inline int np_xadvance(uint32_t cp, int scale) {
  const np_xglyph_t *g = np_xglyph(cp);
  return g ? g->adv * scale : 0;
}
#endif
