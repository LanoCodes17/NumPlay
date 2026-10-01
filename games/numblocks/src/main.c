/* NumBlocks: the game loop. Game ticks run 20 times a second, as in
 * Minecraft; frames are drawn as fast as the calculator can, with the camera
 * placed between the last two ticks so it moves smoothly. */
#include <math.h>
#include "nb.h"

#ifndef HOST
const char eadk_app_name[] __attribute__((section(".rodata.eadk_app_name"))) = "NumBlocks";
const uint32_t eadk_api_level __attribute__((section(".rodata.eadk_api_level"))) = 0;
#endif

uint32_t perf_frames __attribute__((used));   /* read by tools/emu.py */
uint32_t game_time = 1000;                    /* Minecraft's time of day: 0 sunrise, 6000 noon */
bool game_running = true;

static uint32_t last, acc, held;
static float px, py, pz;

void game_init(void) {
  world_new(0);
  player_spawn();
  world_follow(pl.x, pl.y, pl.z);
  last = plat_millis();
  px = pl.x, py = pl.y, pz = pl.z;
}

/* one frame: input, the ticks due, the picture; false once the player quits */
bool game_frame(void) {
  uint32_t now = plat_millis();
  uint32_t dt = now - last;
  last = now;
  if (dt > 250) dt = 250;
  uint32_t k = plat_keys();
  if (k & K_HOME) return false;
  /* looking around: arrows, 150 degrees a second sideways, 100 up and down */
  float turn = dt / 1000.0f;
  if (k & K_LEFT) pl.yaw -= 150 * turn;
  if (k & K_RIGHT) pl.yaw += 150 * turn;
  if (k & K_UP) pl.pitch -= 100 * turn;
  if (k & K_DOWN) pl.pitch += 100 * turn;
  if (pl.pitch > 90) pl.pitch = 90;
  if (pl.pitch < -90) pl.pitch = -90;
  pl.sprinting = (k & K_SPRINT) && (k & K_FWD) && !pl.sneaking;
  acc += dt;
  uint32_t pressed = k & ~held;
  held = k;
  while (acc >= 50) {
    px = pl.x, py = pl.y, pz = pl.z;
    player_tick(k, pressed);
    pressed = 0;
    game_time++;
    acc -= 50;
  }
  world_follow(pl.x, pl.y, pl.z);
  float t = acc / 50.0f;
  Camera c = {px + (pl.x - px) * t, py + (pl.y - py) * t + (pl.sneaking ? 1.54f : 1.62f), pz + (pl.z - pz) * t, pl.yaw,
              pl.pitch};
  render_frame(&c, game_time);
  perf_frames++;
  return true;
}

#ifndef HOST
int main(void) {
  plat_begin();
  game_init();
  while (game_frame()) {}
  return plat_end();
}
#endif
