#ifndef CI_FONT_H
#define CI_FONT_H
#include <stdint.h>

#define FONT_HEIGHT 11
#define FONT_LINE 12

void font_scale(int k);      /* 1: the font's 10 px, 2 and 3 for big texts */
int font_width(const char *s, const char *end);
void font_measure(const char *s, int lw, int lh, int *w, int *h);
int font_lines(const char *s, int lw);
/* draws the rows [by, by + rows) of text whose first line's top-left anchor is (ox, oy) */
void font_draw(const char *s, uint8_t align, int lw, int lh, int ox, int oy, uint16_t *band, int by, int rows, int stride,
               uint16_t color, unsigned ga);
#endif
