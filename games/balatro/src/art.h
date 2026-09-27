#ifndef ART_H
#define ART_H
#include <stdint.h>
#include "assets.h"

#define ART_ARENA (50 * 1024)

void art_flush(void);
void art_begin_frame(void);
void art_use(int id, int top, int bottom);  /* the frame uses this sprite from row top to bottom */
const uint8_t *art_get(int id);             /* decodes if needed */
const uint8_t *art_get_at(int id, int y);   /* same, may free sprites only used above row y */
const uint8_t *art_peek(int id);            /* only if already decoded */
#endif
