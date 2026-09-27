#ifndef FLAME_H
#define FLAME_H
#include "gfx.h"

/* earned: chips x mult on display; required: the blind's score */
void flame_update(int dt_ms, double earned, double required, int allowed, int calm); /* calm: die down quickly */
int flame_on(void);
/* the smoke grid of a flame w x h, once a frame (FLAME_GR x FLAME_GC values) */
#define FLAME_GC 29 /* up to 56 pixels wide */
#define FLAME_GR 20 /* up to 62% of 60 pixels high */
void flame_grid(int k, int w, int h, uint16_t *g);
void flame_rows(int k, int x, int y, int w, int h, int top, const uint16_t *g, C *strip, int y0, int ya, int yb, int cx0,
                int cx1);
#endif
