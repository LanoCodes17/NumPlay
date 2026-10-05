/* Which language the player chose in NumPlay's Settings (the launcher keeps it
 * in the file "numplay.set" of Epsilon's file system, bit 2 of the third
 * content byte). A game gives the file system buffer it already uses for its
 * saves; nothing is written. Header only, no other dependencies. */
#ifndef NP_LANG_H
#define NP_LANG_H
#include <stdbool.h>
#include <stdint.h>

__attribute__((unused)) static bool np_lang_french(const uint8_t *buf, uint32_t size) {
  static const char name[] = "numplay.set";
  if (!buf) return false;
  for (uint32_t p = 0; p + 4 <= size;) {
    uint32_t n = (uint32_t)buf[p] | (uint32_t)buf[p + 1] << 8;
    if (n < 4 || p + n > size) return false; /* the zero size ends the list */
    uint32_t i = 0;
    while (i < sizeof name && p + 2 + i < p + n && buf[p + 2 + i] == (uint8_t)name[i]) i++;
    if (i == sizeof name) {
      const uint8_t *c = buf + p + 2 + sizeof name;
      return p + n - (p + 2 + sizeof name) == 4 && c[0] == 0x4E && c[1] == 1 && (c[2] & 4);
    }
    p += n;
  }
  return false;
}
#endif
