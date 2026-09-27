/* Plays thousands of matches on the host with random key presses and checks
 * that the rules never reach an impossible state or get stuck.
 *   cc -O2 -I<eadk include dir> -I../src autoplay.c ../src/game.c ../src/scene.c ../src/anim.c ../src/assets.c ../src/font.c -o autoplay */
#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "../src/game.h"
#include "../src/gfx.h"
#include "../src/scene.h"
#include "../src/story.h"

uint8_t gfx_fb[GFX_W * GFX_H];
static uint64_t clock_ms, steps;
static jmp_buf done;
static int died, won, doubled, stage_max;

/* the calculator's services */
uint32_t _eadk_timing_millis_low(void) { return (uint32_t)clock_ms; }
uint32_t _eadk_timing_millis_high(void) { return (uint32_t)(clock_ms >> 32); }
uint32_t eadk_random(void) { return rand(); }

/* drawing does nothing here */
void gfx_init(void) {}
void gfx_clear(uint8_t c) {}
void gfx_plate(int i) {}
int gfx_cached(uint32_t key) { return 0; }
void gfx_cache_store(uint32_t key) {}
void gfx_cache_clear(void) {}
void gfx_sprite(int i, int x, int y) {}
void gfx_sprite_dim(int i, int x, int y, int d) {}
void gfx_fill(int x, int y, int w, int h, uint8_t c) {}
void gfx_zoom(int x, int y) {}
void gfx_darken(int x, int y, int w, int h, int d) {}
void gfx_brackets(int x, int y, int w, int h, uint8_t c) {}
int gfx_text_w(const font_t *f, const char *s) { return 0; }
void gfx_text(const font_t *f, const char *s, int x, int y, uint8_t c) {}
void gfx_text_c(const font_t *f, const char *s, int x, int y, uint8_t c) {}
void gfx_text_shadow(const font_t *f, const char *s, int x, int y, uint8_t c) {}
int gfx_lines(const char *s) { return 1; }
void gfx_light(int a, int b, int c) {}
void gfx_shake(int x, int y) {}
void gfx_present(void) {}
void gfx_present_rect(int x, int y, int w, int h) {}

static void check(void) {
  static uint32_t sig, same;
  uint32_t now = G.phase | G.stage << 4 | G.nshell << 8 | G.load << 12 | (uint32_t)G.hp[0] << 20 | (uint32_t)G.hp[1] << 24;
  for (int g = 0; g < 8; g++) now += (G.items[0][g] + 2) * (g + 1) * 131;
  if (now != sig) sig = now, same = 0;
  if (++same > 200000 || ++steps > 20000000) {
    printf("stuck: phase %d stage %d load %d shells %d hp %d/%d cursor %d view %d items %d %d %d %d %d %d %d %d | %d %d %d %d %d %d %d %d sawed %d dcuffed %d wire %d %d\n", G.phase, G.stage, G.load, G.nshell,
           G.hp[0], G.hp[1], T.cursor, T.view, G.items[0][0], G.items[0][1], G.items[0][2], G.items[0][3], G.items[0][4], G.items[0][5], G.items[0][6], G.items[0][7],
           G.items[1][0], G.items[1][1], G.items[1][2], G.items[1][3], G.items[1][4], G.items[1][5], G.items[1][6], G.items[1][7], G.sawed, G.dcuffed, G.wire[0], G.wire[1]);
    exit(1);
  }
  int bad = G.nshell > 8 || G.stage > 2 || G.hp[0] < 0 || G.hp[1] < 0 || G.hp[0] > G.maxhp || G.hp[1] > G.maxhp;
  for (int s = 0; s < 2; s++)
    for (int g = 0; g < 8; g++) bad |= G.items[s][g] < -1 || G.items[s][g] >= IT_COUNT;
  if (bad) {
    printf("bad state: phase %d stage %d hp %d/%d max %d shells %d\n", G.phase, G.stage, G.hp[0], G.hp[1], G.maxhp,
           G.nshell);
    exit(1);
  }
  if (G.stage > stage_max) stage_max = G.stage;
}

int key_wait(int ms) {
  clock_ms += 50;
  check();
  static const int keys[] = {KEY_LEFT, KEY_RIGHT, KEY_UP, KEY_DOWN, KEY_OK, KEY_OK, KEY_OK, KEY_EXE};
  if (rand() % 4 == 0 && ms >= 0) return -1;
  return keys[rand() % 8];
}
void pause_ms(int ms) {
  clock_ms += ms;
  check();
}
int pause_skip(int ms) {
  pause_ms(ms);
  return 0;
}
void app_pause_menu(void) {}
void save_match(void) {}
void save_profile(void) {}

int story_intro(void) { return MODE_STORY; }
void story_enter(int mode) {}
static int revives, retries, retry_stage_ok = 1;
void story_revive(void) { revives++; }
void story_retry(void) { retries++; }
int story_death(void) {
  died++;
  /* "retry" in heaven now and then: the final round again, never round 1 */
  if (G.mode == MODE_STORY && retries < 3 * died / 4) {
    if (G.stage != 2) retry_stage_ok = 0;
    return 1;
  }
  longjmp(done, 1);
}
int story_double_or_nothing(void) {
  doubled++;
  return rand() % 3 != 0;
}
void story_ending(void) {
  won++;
  longjmp(done, 1);
}
char *story_money(char *o, uint64_t v) { return o; }

int main(int argc, char **argv) {
  int n = argc > 1 ? atoi(argv[1]) : 2000;
  srand(1);
  for (int i = 0; i < n; i++) {
    G.rng = 1 + i * 7919;
    game_new(i & 1 ? MODE_DON : MODE_STORY);
    G.name[0] = 'A';
    steps = 0;
    if (!setjmp(done)) game_run();
  }
  printf("%d matches: %d won, %d deaths (%d retried in heaven, final round each time: %s), %d revivals, %d double-or-nothing offers, deepest stage %d\n",
         n, won, died, retries, retry_stage_ok ? "yes" : "NO", revives, doubled, stage_max);
  return 0;
}
