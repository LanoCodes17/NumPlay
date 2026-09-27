#include "util.h"

int str_len(const char *s) {
  int n = 0;
  while (s[n]) n++;
  return n;
}

char *str_cat(char *o, const char *s) {
  while (*s) *o++ = *s++;
  *o = 0;
  return o;
}

char *fmt_int(char *o, long v) {
  char t[24];
  int n = 0;
  if (v < 0) *o++ = '-', v = -v;
  do t[n++] = (char)('0' + v % 10); while (v /= 10);
  while (n) *o++ = t[--n];
  *o = 0;
  return o;
}

double dfloor(double v) {
  if (v >= 9.2e18 || v <= -9.2e18) return v;
  long long i = (long long)v;
  if ((double)i > v) i--;
  return (double)i;
}

/* integer with thousands separators */
static char *commas_ll(char *o, unsigned long long v) {
  char t[32];
  int n = 0, k = 0;
  do {
    if (k && k % 3 == 0) t[n++] = ',';
    t[n++] = (char)('0' + v % 10);
    k++;
  } while (v /= 10);
  while (n) *o++ = t[--n];
  *o = 0;
  return o;
}

char *fmt_commas(char *o, double v) {
  if (v != v || v > 1.7e308) return str_cat(o, "naneinf");
  if (v < 0) {
    *o++ = '-';
    v = -v;
  }
  if (v >= 1e11) {
    int e = 0;
    double m = v;
    while (m >= 10) m /= 10, e++;
    long r = (long)(m * 1000 + 0.5);
    if (r >= 10000) r /= 10, e++;
    o = fmt_int(o, r / 1000);
    *o++ = '.';
    *o++ = (char)('0' + r / 100 % 10);
    *o++ = (char)('0' + r / 10 % 10);
    *o++ = (char)('0' + r % 10);
    *o++ = 'e';
    return fmt_int(o, e);
  }
  if (v == dfloor(v)) return commas_ll(o, (unsigned long long)v);
  int dec = v >= 100 ? 0 : v >= 10 ? 1 : 2;
  double scale = dec == 0 ? 1 : dec == 1 ? 10 : 100;
  unsigned long long r = (unsigned long long)(v * scale + 0.5);
  o = commas_ll(o, (unsigned long long)(r / (unsigned long long)scale));
  if (dec) {
    *o++ = '.';
    if (dec == 2) *o++ = (char)('0' + r / 10 % 10);
    *o++ = (char)('0' + r % 10);
    *o = 0;
  }
  return o;
}

char *fmt_short(char *o, double v) {
  if (v < 0) *o++ = '-', v = -v;
  if (!(v < 1e6)) return fmt_commas(o, v); /* big values: 1,234,567 or 1.234e30 */
  long r = (long)(v * 100 + 0.5);
  o = fmt_int(o, r / 100);
  int f = (int)(r % 100);
  if (f) {
    *o++ = '.';
    *o++ = (char)('0' + f / 10);
    if (f % 10) *o++ = (char)('0' + f % 10);
    *o = 0;
  }
  return o;
}
