/* Plays NumBlocks on a computer with scripted keys and saves screenshots.
 *
 *   play [--frames N] [--ms-per-frame M] [--keys "10-40:fwd,50:use,..."] [--shots 30,60] [--out DIR]
 *
 * Keys: left right up down fwd back sleft sright jump use attack inv sneak sprint pause 1..9. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include "../src/nb.h"

extern uint32_t host_keys, host_time;
void host_shot(const char *path);
void game_init(void);
bool game_frame(void);

typedef struct { int a, b; uint32_t k; } Hold;
static Hold holds[512];
static int nholds;

static uint32_t key_bit(const char *s) {
  static const struct { const char *n; uint32_t k; } t[] = {
    {"left", K_LEFT}, {"right", K_RIGHT}, {"up", K_UP}, {"down", K_DOWN}, {"fwd", K_FWD}, {"back", K_BACKW},
    {"sleft", K_STRAFE_L}, {"sright", K_STRAFE_R}, {"jump", K_JUMP}, {"use", K_USE}, {"attack", K_ATTACK},
    {"inv", K_INV}, {"sneak", K_SNEAK}, {"sprint", K_SPRINT}, {"pause", K_PAUSE}, {"home", K_HOME}};
  for (unsigned i = 0; i < sizeof t / sizeof t[0]; i++)
    if (!strcmp(s, t[i].n)) return t[i].k;
  if (s[0] >= '1' && s[0] <= '9' && !s[1]) return K_SLOT1 << (s[0] - '1');
  return 0;
}

int main(int argc, char **argv) {
  const char *out = "build/play", *shots = "";
  int frames = 60, mspf = 50;
  for (int i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "--frames") && i + 1 < argc) frames = atoi(argv[++i]);
    else if (!strcmp(argv[i], "--ms-per-frame") && i + 1 < argc) mspf = atoi(argv[++i]);
    else if (!strcmp(argv[i], "--out") && i + 1 < argc) out = argv[++i];
    else if (!strcmp(argv[i], "--shots") && i + 1 < argc) shots = argv[++i];
    else if (!strcmp(argv[i], "--keys") && i + 1 < argc) {
      char *s = strdup(argv[++i]);
      for (char *t = strtok(s, ","); t && nholds < 512; t = strtok(NULL, ",")) {
        int a, b;
        char k[16];
        if (sscanf(t, "%d-%d:%15s", &a, &b, k) == 3) holds[nholds++] = (Hold){a, b, key_bit(k)};
        else if (sscanf(t, "%d:%15s", &a, k) == 2) holds[nholds++] = (Hold){a, a, key_bit(k)};
      }
    }
  }
  mkdir(out, 0755);
  clock_t c0 = clock();
  game_init();
  double init_ms = (clock() - c0) * 1000.0 / CLOCKS_PER_SEC;
  c0 = clock();
  for (int f = 0; f < frames; f++) {
    host_keys = 0;
    for (int i = 0; i < nholds; i++)
      if (f >= holds[i].a && f <= holds[i].b) host_keys |= holds[i].k;
    host_time += mspf;
    if (!game_frame()) break;
    for (const char *s = shots; *s;) {
      if (atoi(s) == f) {
        char p[512];
        snprintf(p, sizeof p, "%s/shot_%d.ppm", out, f);
        host_shot(p);
      }
      const char *c = strchr(s, ',');
      if (!c) break;
      s = c + 1;
    }
  }
  extern unsigned long st_steps, st_texels, st_jumps, st_pixels;
  printf("per frame: %.0f traces, %.0f steps\n", (double)st_pixels / frames, (double)st_steps / frames);
  extern unsigned long st_dis, st_k0, st_fpfail, st_fpok, st_sky;
  printf("between per frame: %.0f disagree, %.0f kind0, %.0f face fail, %.0f face ok, %.0f sky\n", (double)st_dis / frames, (double)st_k0 / frames, (double)st_fpfail / frames, (double)st_fpok / frames, (double)st_sky / frames);
  printf("per pixel: %.1f steps, %.2f texels, %.2f jumps\n", (double)st_steps / st_pixels, (double)st_texels / st_pixels, (double)st_jumps / st_pixels);
  printf("init %.1f ms, %.2f ms a frame (host)\n", init_ms, (clock() - c0) * 1000.0 / CLOCKS_PER_SEC / frames);
  return 0;
}
