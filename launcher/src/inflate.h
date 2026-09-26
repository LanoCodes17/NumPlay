#ifndef NP_INFLATE_H
#define NP_INFLATE_H
#include <stdbool.h>
#include <stdint.h>

typedef struct np_inflate_tables np_inflate_tables_t;
uint32_t np_inflate_tables_size(void);
/* Returns the number of bytes written, or -1 if the data is invalid. */
int np_inflate(uint8_t *out, uint32_t cap, const uint8_t *in, uint32_t len, np_inflate_tables_t *tables);
#endif
