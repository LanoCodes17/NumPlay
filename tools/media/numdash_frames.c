/* Renders NumDash gameplay for the README: plays a level with one of the
 * recorded inputs from games/numdash/tests/replays and saves raw RGB565
 * frames that tools/record.py's helpers turn into a GIF.
 * usage: numdash_frames level replay.txt out_dir first_tick last_tick step */
#include "fx.h"
#include "scene.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint16_t fb[GFX_W * GFX_H];

int main(int argc, char **argv) {
  if (argc < 7) return 2;
  int lvl = atoi(argv[1]), first = atoi(argv[4]), last = atoi(argv[5]), step = atoi(argv[6]);
  static Level L;
  static Game g;
  if (!level_load_builtin(&L, (unsigned)lvl)) return 1;
  static int ticks[20000], vals[20000];
  int n = 0, t, v;
  FILE *f = fopen(argv[2], "r");
  while (f && fscanf(f, "input=%d,%d\n", &t, &v) == 2 && n < 20000) ticks[n] = t, vals[n++] = v;
  if (f) fclose(f);
  game_start(&g, &L, false);
  fx_reset();
  int pos = 0, frame = 0;
  bool held = false;
  for (int i = 1; i <= last; i++) {
    while (pos < n && ticks[pos] <= i) held = vals[pos++];
    game_step(&g, held);
    if (i % 6 == 0) fx_update(&g, 6.0f / 240, rgb(125, 255, 0), rgb(0, 255, 255), false);
    if (g.dead) {
      fprintf(stderr, "dead at tick %d\n", i);
      return 1;
    }
    if (i >= first && (i - first) % step == 0) {
      SceneOpts o = {0};
      o.p1 = rgb(125, 255, 0);
      o.p2 = rgb(0, 255, 255);
      o.show_percent = true;
      o.show_bar = true;
      o.attempt = 1;
      o.time = i / 240.f;
      scene_prepare(&g, &o);
      for (int s = 0; s < STRIPS; s++) {
        gfx_begin_strip(s);
        scene_draw();
        memcpy(fb + s * STRIP_H * GFX_W, gfx_strip, sizeof(gfx_strip));
      }
      char path[512];
      snprintf(path, sizeof path, "%s/%06d_%06d.raw", argv[3], frame++, (i - first) * 1000 / 240);
      FILE *o2 = fopen(path, "wb");
      fwrite(fb, sizeof fb, 1, o2);
      fclose(o2);
    }
  }
  return 0;
}

/* no file system here */
uint8_t *platform_storage(size_t *size) {
  (void)size;
  return NULL;
}
void platform_storage_commit(void) {}
