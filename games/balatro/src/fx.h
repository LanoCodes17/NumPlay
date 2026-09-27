#ifndef FX_H
#define FX_H
#include "gfx.h"

void fx_frame(uint32_t ms);
/* a pixel of card art with an edition; (sx, sy) in the sprite (sw x sh);
   seed varies the sheen from card to card */
C fx_edition(C c, int ed, int sx, int sy, int sw, int sh, float seed);
#endif
