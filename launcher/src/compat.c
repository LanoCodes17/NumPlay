/* Text drawn by the calculator's software, for the games that use it (the
 * build renames their eadk_display_draw_string to this).
 *
 * On Epsilon, the firmware draws it. Upsilon's .nwa apps may not reach the
 * firmware's text function the way Epsilon's do, so there NumPlay draws it
 * itself, in cells of the firmware's sizes (7 x 14, or 10 x 18 large) so that
 * the games' layouts stay as they are, with NumPlay's own font. So it does too
 * in a language build for a text with letters the firmware has no font for
 * (Chinese: two cells each, from the 12-pixel font of games/common/np_text.h). */
#include "np.h"
#include "font.h"
#include "sys.h"
#include "../../games/common/np_text.h"

static color_t blend(color_t bg, color_t fg, int a16) {
  if (a16 >= 15) return fg;
  if (a16 <= 0) return bg;
  uint32_t B = (bg | (uint32_t)bg << 16) & 0x07E0F81F, F = (fg | (uint32_t)fg << 16) & 0x07E0F81F;
  uint32_t M = ((B * (uint32_t)(16 - a16) + F * (uint32_t)a16) >> 4) & 0x07E0F81F;
  return (color_t)(M | M >> 16);
}

static void draw_cell(const np_font_t *f, int cw, int ch, int x, int y, char c, color_t fg, color_t bg) {
  color_t cell[10 * 18];
  for (int i = 0; i < cw * ch; i++) cell[i] = bg;
  unsigned k = (uint8_t)c;
  if (k >= f->first && k < (unsigned)f->first + f->count) {
    const np_glyph_t *g = &f->glyphs[k - f->first];
    int pen = (cw - g->advance) / 2, base = (ch + f->ascent - f->descent) / 2;
    const uint8_t *d = f->data + g->offset;
    for (int j = 0; j < g->h; j++)
      for (int i = 0; i < g->w; i++) {
        int n = j * g->w + i, a = (d[n >> 1] >> ((n & 1) * 4)) & 15;
        int px = pen + g->x + i, py = base + g->y + j;
        if (a && px >= 0 && px < cw && py >= 0 && py < ch) cell[py * cw + px] = blend(bg, fg, a);
      }
  }
  if (x + cw > 320 || y + ch > 240) return;
  eadk_display_push_rect((eadk_rect_t){(uint16_t)x, (uint16_t)y, (uint16_t)cw, (uint16_t)ch}, cell);
}

typedef struct {
  color_t *cell;
  int cw, ch;
  color_t fg;
} cell_plot_t;
static void cell_plot(int x, int y, void *ctx) {
  cell_plot_t *p = ctx;
  if (x >= 0 && x < p->cw && y >= 0 && y < p->ch) p->cell[y * p->cw + x] = p->fg;
}

/* Chinese letters, two cells each: n of them from (x, y). Their 12-pixel letters go 14 pixels
 * apart, centred in the cells (in the large font's 20-pixel pairs, a letter a pair looks spaced). */
#define WIDE_STEP 14
static void draw_wide(const uint32_t *cps, int n, int cw, int ch, int x, int y, color_t fg, color_t bg) {
  color_t cell[20 * 18];
  int start = (2 * cw - WIDE_STEP) * n / 2 + (WIDE_STEP - 12) / 2;
  for (int k = 0; k < n; k++, x += 2 * cw) {
    for (int i = 0; i < 2 * cw * ch; i++) cell[i] = bg;
    cell_plot_t p = {cell, 2 * cw, ch, fg};
    for (int i = 0; i < n; i++) {
      int pen = start + WIDE_STEP * i - 2 * cw * k;
      if (pen > -12 && pen < 2 * cw) np_xdraw(cps[i], pen, (ch - 12) / 2, 1, cell_plot, &p);
    }
    if (x + 2 * cw > 320 || y + ch > 240) return;
    eadk_display_push_rect((eadk_rect_t){(uint16_t)x, (uint16_t)y, (uint16_t)(2 * cw), (uint16_t)ch}, cell);
  }
}

static bool has_wide(const char *s) {
  while (*s)
    if (np_utf8(&s) >= 0x2E80) return true;
  return false;
}

void np_display_draw_string(const char *text, eadk_point_t point, bool large_font, eadk_color_t text_color,
                            eadk_color_t background_color) {
  if (!np_upsilon() && !(NP_TEXT_EXTRA && has_wide(text))) { /* (always, in the simulator) */
    eadk_display_draw_string(text, point, large_font, text_color, background_color);
    return;
  }
  const np_font_t *f = large_font ? &np_font_body : &np_font_small;
  int cw = large_font ? 10 : 7, ch = large_font ? 18 : 14, x = point.x, y = point.y;
  for (const char *s = text; *s;) {
    if (*s == '\n') {
      x = point.x, y += ch, s++;
      continue;
    }
    if (!NP_TEXT_EXTRA) {
      if ((uint8_t)*s >= 0x80 && ((uint8_t)*s & 0xC0) == 0x80) { /* the rest of a UTF-8 letter */
        s++;
        continue;
      }
      draw_cell(f, cw, ch, x, y, (uint8_t)*s >= 0x80 ? '?' : *s, text_color, background_color);
      s++, x += cw;
      continue;
    }
    const char *at = s;
    uint32_t cp = np_utf8(&s);
    if (cp >= 0x2E80 && np_xglyph(cp)) { /* the run of them */
      uint32_t run[16];
      int n = 0;
      for (s = at; *s && n < 16;) {
        const char *next = s;
        uint32_t c = np_utf8(&next);
        if (c < 0x2E80 || !np_xglyph(c)) break;
        run[n++] = c, s = next;
      }
      draw_wide(run, n, cw, ch, x, y, text_color, background_color);
      x += 2 * cw * n;
      continue;
    }
    char base = (char)cp, second = 0;
    if (cp >= 0x80 && (np_latin(cp, &base, &second), !base)) base = '?';
    draw_cell(f, cw, ch, x, y, base, text_color, background_color);
    x += cw;
    if (second) draw_cell(f, cw, ch, x, y, second, text_color, background_color), x += cw;
  }
}
