/* NumPlay: one app for all the games. */
#include "np.h"

#if PLATFORM_DEVICE && !NP_SIMULATOR
const char eadk_app_name[] __attribute__((section(".rodata.eadk_app_name"))) = "NumPlay";
const uint32_t eadk_api_level __attribute__((section(".rodata.eadk_api_level"))) = 0;
#endif

int main(void) {
  np_finish_pending_uninstalls();
  int selected = np_game_count ? 0 : -1;
  for (;;) {
    int r = np_home(&selected);
    if (r == -2) break;
    if (r == -1) {
      np_settings();
      continue;
    }
    np_game_run(r);
    np_wait_release();
  }
  np_wait_release();
  return 0;
}
