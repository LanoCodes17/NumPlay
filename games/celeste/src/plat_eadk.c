/* The calculator: EADK display, keyboard, time and files. */
#include <eadk.h>
#include "../../common/epsilon_app.h"
#include "../../common/epsilon_files.h"
#include "celeste.h"

#define KEY(k) ((uint64_t)1 << (k))

/* key bindings: one EADK key per game action (settings can change them) */
uint8_t g_bind[4];   /* jump, dash, grab, talk */
void plat_default_binds(void) {
  g_bind[0] = eadk_key_ok, g_bind[1] = eadk_key_back, g_bind[2] = eadk_key_toolbox, g_bind[3] = eadk_key_back;
}
static uint64_t scan, scan_prev;
/* the key that went down this frame (an EADK key), -1 if none */
int plat_key_pressed(void) {
  uint64_t d = scan & ~scan_prev;
  return d ? __builtin_ctzll(d) : -1;
}

uint32_t plat_keys(void) {
  uint64_t k = eadk_keyboard_scan();
  scan_prev = scan, scan = k;
  uint32_t r = 0;
  if (k & KEY(eadk_key_left)) r |= K_LEFT;
  if (k & KEY(eadk_key_right)) r |= K_RIGHT;
  if (k & KEY(eadk_key_up)) r |= K_UP;
  if (k & KEY(eadk_key_down)) r |= K_DOWN;
  if (k & KEY(g_bind[0])) r |= K_JUMP;
  if (k & KEY(g_bind[1])) r |= K_DASH;
  if (k & KEY(g_bind[2])) r |= K_GRAB;
  if (k & KEY(g_bind[3])) r |= K_TALK;
  if (k & (KEY(eadk_key_ok) | KEY(eadk_key_exe))) r |= K_OK;
  if (k & KEY(eadk_key_back)) r |= K_BACK;   /* (menus: cancel) */
  if (k & KEY(eadk_key_backspace)) r |= K_PAUSE;
  if (k & KEY(eadk_key_var)) r |= K_JOURNAL;   /* (Input.MenuJournal: only the cheat code reads it) */
  if (k & (KEY(eadk_key_home) | KEY(eadk_key_on_off))) r |= K_HOME;
  if (k) r |= K_ANY;
  return r;
}

uint32_t plat_millis(void) { return (uint32_t)eadk_timing_millis(); }
void plat_sleep(uint32_t ms) { eadk_timing_msleep(ms); }
void plat_push(int x, int y, int w, int h, const uint16_t *px) {
  eadk_display_push_rect((eadk_rect_t){(uint16_t)x, (uint16_t)y, (uint16_t)w, (uint16_t)h}, px);
}
void plat_fill(int x, int y, int w, int h, uint16_t c) {
  if (w > 0 && h > 0) eadk_display_push_rect_uniform((eadk_rect_t){(uint16_t)x, (uint16_t)y, (uint16_t)w, (uint16_t)h}, c);
}
bool plat_save(const char *name, const void *data, uint32_t len) { return ef_write(name, data, len); }
const uint8_t *plat_load(const char *name, uint32_t *len) { return ef_read(name, len); }
void plat_begin(void) { np_app_begin(); }
int plat_end(void) { return np_app_end(); }
