#ifndef UTIL_H
#define UTIL_H
#include <stdint.h>

char *fmt_int(char *o, long v);            /* plain integer */
char *fmt_commas(char *o, double v);       /* Balatro's number_format */
char *fmt_short(char *o, double v);        /* Lua's tostring for small values: 1.5, 2, 0.25 */
char *str_cat(char *o, const char *s);
int str_len(const char *s);
double dfloor(double v);
#endif
