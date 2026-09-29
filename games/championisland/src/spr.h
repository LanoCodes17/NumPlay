#ifndef CI_SPR_H
#define CI_SPR_H
#include "ci.h"


/* LZMA decoding (one stream at a time) */
void z_open(uint32_t off, uint32_t clen, uint32_t rawlen);
uint32_t z_read(uint32_t n, const uint8_t **at);
bool z_get(uint8_t *dst, uint32_t n);

/* Run-length rows of a sprite, decoded on demand, or NULL:
 * u16 w, u16 h, u16 row offsets[h] (from the start), then each row:
 * nruns, then (skip, len, len palette indices) per run. */
const uint8_t *spr_get(uint16_t sprite);
const uint8_t *spr_peek(uint16_t sprite);    /* only if cached */
const uint8_t *spr_stream(uint16_t sprite);   /* 14-byte stream record or NULL */
void spr_setup(uint8_t *mem, uint32_t size);   /* where the cache lives (game.c's arena) */
void spr_reset(void);
uint8_t *z_scratch(uint32_t *size);             /* the decoder's ring when it is idle */
void spr_tick(void);
uint32_t spr_cache_used(void);
uint32_t spr_capacity(void);
#endif
