/* Host build of the game logic for tests: the screen is a memory buffer and
 * the chamber runs one frame per call (see ../src/game.c, PORTAL_TEST).
 *   host PACK LEVEL KEYS...   keys per frame as hex, prints the state and
 *   writes the final screen to out.raw */
#include <eadk.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint16_t fb[240][320];
void eadk_display_push_rect(eadk_rect_t r, const eadk_color_t *p) {
  for (int y = 0; y < r.height; y++)
    for (int x = 0; x < r.width; x++)
      if (r.y + y < 240 && r.x + x < 320) fb[r.y + y][r.x + x] = p[y * r.width + x];
}
void eadk_display_push_rect_uniform(eadk_rect_t r, eadk_color_t c) {
  for (int y = 0; y < r.height; y++)
    for (int x = 0; x < r.width; x++)
      if (r.y + y < 240 && r.x + x < 320) fb[r.y + y][r.x + x] = c;
}
void eadk_display_pull_rect(eadk_rect_t r, eadk_color_t *p) {
  for (int y = 0; y < r.height; y++)
    for (int x = 0; x < r.width; x++) p[y * r.width + x] = fb[r.y + y][r.x + x];
}
void eadk_display_draw_string(const char *t, eadk_point_t p, bool l, eadk_color_t a, eadk_color_t b) {}
static uint32_t ms;
uint32_t _eadk_timing_millis_low(void) { return ms; }
uint32_t _eadk_timing_millis_high(void) { return 0; }
void eadk_timing_msleep(uint32_t m) { ms += m; }
void eadk_timing_usleep(uint32_t u) { ms += u / 1000; }
void _eadk_keyboard_scan_do_scan(void) {}
uint32_t _eadk_keyboard_scan_low(void) { return 0; }
uint32_t _eadk_keyboard_scan_high(void) { return 0; }
eadk_event_t eadk_event_get(int32_t *t) { exit(3); }
uint32_t eadk_random(void) { return 4; }

void test_load(int pack, int n);
int test_frame(uint32_t keys);
void test_state(uint8_t *o);
void apply_scheme(void);
/* random search for inputs that finish a chamber */
static uint32_t rng = 1;
static uint32_t rnd(void) { rng ^= rng << 13, rng ^= rng >> 17, rng ^= rng << 5; return rng; }
static int search(int pack, int level, long trials, int maxf) {
  static const uint32_t combos[] = {4, 4, 4, 12, 12, 8, 2, 2, 10, 0, 0, 16};
  static uint32_t seq[4000];
  for (long t = 0; t < trials; t++) {
    test_load(pack, level);
    int n = 0;
    while (n < maxf) {
      uint32_t k = combos[rnd() % 12];
      if (rnd() % 5 == 0) k |= 1u << (8 + rnd() % 9);
      int d = 1 + rnd() % 24;
      for (int i = 0; i < d && n < maxf; i++) {
        seq[n++] = k;
        if (test_frame(k)) {
          printf("%ld %d:", t, n);
          for (int j = 0; j < n; j++) printf(" %x", seq[j]);
          printf("\n");
          return 0;
        }
        k &= 0xFF; /* digits are taps */
      }
    }
  }
  return 1;
}

void assets_init(void);
int main(int argc, char **argv) {
  assets_init();
  apply_scheme();
  if (!strcmp(argv[1], "search")) {
    rng = atoi(argv[6]);
    return search(atoi(argv[2]), atoi(argv[3]), atol(argv[4]), atoi(argv[5]));
  }
  test_load(atoi(argv[1]), atoi(argv[2]));
  FILE *f = fopen(getenv("FRAMES") ? getenv("FRAMES") : "frames.raw", "wb");
  for (int i = 3; i < argc; i++) {
    uint8_t s[20];
    test_state(s);
    printf("%d", i - 3);
    for (int k = 0; k < 20; k++) printf(" %d", s[k]);
    printf("\n");
    if (test_frame((uint32_t)strtoul(argv[i], 0, 16))) break;
    fwrite(fb, sizeof fb, 1, f);
  }
  fclose(f);
  return 0;
}
