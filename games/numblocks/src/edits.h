/* The player's changes to the world (the generator makes everything else). */
#ifndef NB_EDITS_H
#define NB_EDITS_H
#include <stdbool.h>
#include <stdint.h>

#define EDITS_MAX 2048
void edits_clear(void);
bool edits_put(int x, int y, int z, int b);      /* false: the log is full */
void edits_apply(int cx, int cz, int y0, int h, uint8_t *slab);   /* onto a chunk from gen_slab */
int edits_top(int x, int z, int top);            /* the column's sky top with the edits (1 + y) */
int edits_count(void);
#endif
