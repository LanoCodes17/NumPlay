#ifndef BG_H
#define BG_H
#include <stdint.h>
#include "gfx.h"

#define BG_CELL 4
void bg_mode(int title);
void bg_colours(uint32_t col, uint32_t special, uint32_t tertiary, float contrast, float spin, int instant);
void bg_reset(void);
void bg_update(float dt, int rows);
void bg_row(C *dst, int y, int x0, int x1);
#endif
