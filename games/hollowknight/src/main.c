/* Hollow Knight for the NumWorks calculator.
 *
 * Inspired by Hollow Knight, not affiliated with Team Cherry.
 * Made by Mason Chen as part of NumPlay. */
#include "game.h"

#ifndef HOST
const char eadk_app_name[] __attribute__((section(".rodata.eadk_app_name"))) = "Hollow Knight";
const uint32_t eadk_api_level __attribute__((section(".rodata.eadk_api_level"))) = 0;
extern const uint8_t hk_data[];

void plat_begin(void);
int plat_end(void);

uint32_t perf_frames __attribute__((used));   /* read by tools/emu.py */
uint32_t perf_updates __attribute__((used));

#define TICK_MS 20      /* the game's step (Unity's fixed step) */
#define MAX_TICKS 4     /* steps caught up before a frame is drawn */

#ifdef PERF_ROOM
/* timing: the camera panning across a room */
#ifndef PERF_X
#define PERF_X 30
#endif
#ifndef PERF_Y
#define PERF_Y 40
#endif
#ifndef PERF_DX
#define PERF_DX 0.15f
#endif
int main(void) {
  plat_begin();
  hk_bin = hk_data;
  plat_fill(0, 0, SCREEN_W, SCREEN_H, 0);
  room_load(PERF_ROOM);
  g_cam_x = PERF_X, g_cam_y = PERF_Y;
  for (;;) {
    if (plat_keys() & K_HOME) break;
    gfx_frame();
    perf_frames++, perf_updates++;
    g_cam_x += PERF_DX;
    if (g_cam_x > g_room.h->w - 14.6f) g_cam_x = 14.6f;
  }
  return plat_end();
}
#else
int main(void) {
  plat_begin();
  hk_bin = hk_data;
  plat_fill(0, 0, SCREEN_W, SCREEN_H, 0);
  menu_start();
  uint32_t last = plat_millis(), acc = 0;
  for (;;) {
    uint32_t keys = plat_keys();
    if ((keys & K_HOME) || menu_quit()) break;
    uint32_t now = plat_millis();
    acc += now - last, last = now;
    int ticks = 0;
    while (acc >= TICK_MS && ticks < MAX_TICKS) {
      if (!menu_tick(keys)) game_tick(keys);
      acc -= TICK_MS, ticks++, perf_updates++;
    }
    if (ticks == MAX_TICKS) acc = 0;   /* (too slow to catch up: the game slows down instead) */
    if (!ticks) {
      plat_sleep(TICK_MS - acc);
      continue;
    }
    /* (the game, or the title screen and the save profiles alone; the pause menu over the game) */
    g_gfx_no_room = !menu_in_game();
    if (menu_in_game()) game_draw_layers();
    menu_draw();
    gfx_frame();
    perf_frames++;
  }
  return plat_end();
}
#endif
#endif
