/* The calculator: EADK display, keyboard, time and files.
 *
 * Keys, Minecraft's on the calculator's keyboard: the arrows look around;
 * ln, sin, cos and tan are W, A, S and D; pi jumps; OK uses and places, Back
 * mines and hits; 1-9 pick the hotbar slot; var opens the inventory; square
 * root sneaks, x squared sprints; Toolbox pauses; Home saves and quits. */
#include <eadk.h>
#include "../../common/epsilon_app.h"
#include "../../common/epsilon_files.h"
#include "nb.h"

#define KEY(k) ((uint64_t)1 << (k))

uint32_t plat_keys(void) {
  uint64_t k = eadk_keyboard_scan();
  uint32_t r = 0;
  if (k & KEY(eadk_key_left)) r |= K_LEFT;
  if (k & KEY(eadk_key_right)) r |= K_RIGHT;
  if (k & KEY(eadk_key_up)) r |= K_UP;
  if (k & KEY(eadk_key_down)) r |= K_DOWN;
  if (k & KEY(eadk_key_ln)) r |= K_FWD;
  if (k & KEY(eadk_key_cosine)) r |= K_BACKW;
  if (k & KEY(eadk_key_sine)) r |= K_STRAFE_L;
  if (k & KEY(eadk_key_tangent)) r |= K_STRAFE_R;
  if (k & KEY(eadk_key_pi)) r |= K_JUMP;
  if (k & (KEY(eadk_key_ok) | KEY(eadk_key_exe))) r |= K_USE;
  if (k & (KEY(eadk_key_back) | KEY(eadk_key_backspace))) r |= K_ATTACK;
  if (k & KEY(eadk_key_var)) r |= K_INV;
  if (k & (KEY(eadk_key_home) | KEY(eadk_key_on_off))) r |= K_HOME;
  if (k & KEY(eadk_key_sqrt)) r |= K_SNEAK;
  if (k & KEY(eadk_key_square)) r |= K_SPRINT;
  if (k & KEY(eadk_key_toolbox)) r |= K_PAUSE;
  if (k & KEY(eadk_key_xnt)) r |= K_DROP;
  static const uint8_t digits[9] = {eadk_key_one, eadk_key_two, eadk_key_three, eadk_key_four, eadk_key_five,
                                    eadk_key_six, eadk_key_seven, eadk_key_eight, eadk_key_nine};
  for (int i = 0; i < 9; i++)
    if (k & KEY(digits[i])) r |= K_SLOT1 << i;
  if (k) r |= K_ANY;
  return r;
}

uint32_t plat_millis(void) { return (uint32_t)eadk_timing_millis(); }
void plat_sleep(uint32_t ms) { eadk_timing_msleep(ms); }
void plat_push(int x, int y, int w, int h, const uint16_t *px) {
  eadk_display_push_rect((eadk_rect_t){(uint16_t)x, (uint16_t)y, (uint16_t)w, (uint16_t)h}, px);
}
bool plat_save(const char *name, const void *data, uint32_t len) { return ef_write(name, data, len); }
const uint8_t *plat_load(const char *name, uint32_t *len) { return ef_read(name, len); }
void plat_begin(void) { np_app_begin(); }
int plat_end(void) { return np_app_end(); }
