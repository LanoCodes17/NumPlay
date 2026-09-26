/* Files and app lifetime for the Rust games, on the calculator.
 *
 * The file functions wrap games/common/epsilon_files.h, which finds Epsilon's
 * file system through the userland header of the running software, keeps the
 * record list terminated and clears Epsilon's record cache when records move.
 * (They replace the earlier helpers, which could write through a null header
 * address after a cold boot and could corrupt the records that follow an
 * erased one.)
 *
 * np_app_run() runs the game with Home held back by Epsilon, so that leaving
 * cleans up first (see games/common/epsilon_app.h); np_app_leave() leaves
 * from anywhere, for Home or a panic. */
#include "storage.h"
#include "../../../../common/epsilon_files.h"
#include "../../../../common/jump.h"

bool extapp_fileExists(const char *filename) {
  uint32_t len;
  return ef_read(filename, &len) != NULL;
}

const char *extapp_fileRead(const char *filename, uint32_t *len) {
  return (const char *)ef_read(filename, len);
}

bool extapp_fileWrite(const char *filename, const char *content, uint32_t len) {
  return ef_write(filename, content, len);
}

bool extapp_fileErase(const char *filename) { return ef_remove(filename); }

static np_jump_t leave;
static bool running;

int np_app_run(void (*game)(void)) {
  np_app_begin();
  if (!np_save_jump(leave)) {
    running = true;
    game();
  }
  running = false;
  return np_app_end();
}

void np_app_leave(void) {
  if (running) np_jump(leave);
}
