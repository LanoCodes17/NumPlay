/* The calculator: EADK display, keyboard, time and files.
 *
 * Keys, Minecraft's on the calculator's keyboard: the left thumb looks around
 * with the arrows, the right one walks with the keys under OK and Back, laid
 * out like W, A, S and D: comma, pi, square root and x squared; backspace
 * sprints. Under the arrows, shift jumps and alpha sneaks. OK uses and places,
 * Back mines and hits; 1-9 pick the hotbar slot; var opens the inventory;
 * x,n,t drops; Toolbox pauses; Home saves and quits. */
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
  if (k & KEY(eadk_key_comma)) r |= K_FWD;
  if (k & KEY(eadk_key_sqrt)) r |= K_BACKW;
  if (k & KEY(eadk_key_pi)) r |= K_STRAFE_L;
  if (k & KEY(eadk_key_square)) r |= K_STRAFE_R;
  if (k & KEY(eadk_key_shift)) r |= K_JUMP;
  if (k & (KEY(eadk_key_ok) | KEY(eadk_key_exe))) r |= K_USE;
  if (k & KEY(eadk_key_ok)) r |= K_OK;
  if (k & KEY(eadk_key_exe)) r |= K_EXE;
  if (k & KEY(eadk_key_shift)) r |= K_SHIFT;
  if (k & KEY(eadk_key_back)) r |= K_BACK;
  if (k & KEY(eadk_key_zero)) r |= K_ZERO;
  if (k & KEY(eadk_key_minus)) r |= K_MINUS;
  if (k & KEY(eadk_key_back)) r |= K_ATTACK;
  if (k & KEY(eadk_key_var)) r |= K_INV;
  if (k & (KEY(eadk_key_home) | KEY(eadk_key_on_off))) r |= K_HOME;
  if (k & KEY(eadk_key_alpha)) r |= K_SNEAK;
  if (k & KEY(eadk_key_backspace)) r |= K_SPRINT;
  if (k & KEY(eadk_key_toolbox)) r |= K_PAUSE;
  if (k & KEY(eadk_key_xnt)) r |= K_DROP;
  static const uint8_t digits[9] = {eadk_key_one, eadk_key_two, eadk_key_three, eadk_key_four, eadk_key_five,
                                    eadk_key_six, eadk_key_seven, eadk_key_eight, eadk_key_nine};
  for (int i = 0; i < 9; i++)
    if (k & KEY(digits[i])) r |= K_SLOT1 << i;
  return r;
}

uint32_t plat_millis(void) { return (uint32_t)eadk_timing_millis(); }
void plat_sleep(uint32_t ms) { eadk_timing_msleep(ms); }
void plat_push(int x, int y, int w, int h, const uint16_t *px) {
  eadk_display_push_rect((eadk_rect_t){(uint16_t)x, (uint16_t)y, (uint16_t)w, (uint16_t)h}, px);
}
bool plat_save(const char *name, const void *data, uint32_t len) { return ef_write(name, data, len); }
const uint8_t *plat_load(const char *name, uint32_t *len) { return ef_read(name, len); }

/* removes every record whose name starts with `prefix` */
void plat_remove_prefix(const char *prefix) {
  for (bool again = true; again;) {
    again = false;
    ef_fs_t fs;
    if (!ef_open(&fs)) return;
    int end = ef_end(&fs);
    if (end < 0) return;
    for (uint32_t p = 0; (int)p < end; p += ef_rd16(fs.buf + p)) {
      const char *n = (const char *)fs.buf + p + 2;
      uint32_t i = 0;
      while (prefix[i] && n[i] == prefix[i]) i++;
      if (!prefix[i]) {
        char name[40];
        uint32_t k = 0;
        while (n[k] && k < sizeof name - 1) name[k] = n[k], k++;
        name[k] = 0;
        if (!ef_remove(name)) return;
        again = true;   /* records moved: start over */
        break;
      }
    }
  }
}

/* bytes left in the calculator's storage */
uint32_t plat_storage_free(void) {
  ef_fs_t fs;
  if (!ef_open(&fs)) return 0;
  int end = ef_end(&fs);
  return end < 0 ? 0 : fs.size - (uint32_t)end - 2;
}
void plat_begin(void) { np_app_begin(); }
int plat_end(void) { return np_app_end(); }
