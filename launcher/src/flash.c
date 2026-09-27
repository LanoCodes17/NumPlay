/* Uninstalling a game for real: its block of flash is wiped, which frees the
 * space it used, and its save files are deleted.
 *
 * External flash is erased in 64 KiB sectors. Sectors that lie entirely
 * inside the game's block are erased; the ends of the block that share a
 * sector with something else are overwritten with zeros instead (programming
 * only clears bits, so nothing outside the block is touched and nothing
 * needs to be buffered or rewritten). The first word is cleared first, so an
 * interrupted uninstall still leaves the game marked as removed, and the
 * launcher finishes the job the next time it starts. */
#include <string.h>
#include "np.h"
#include "sys.h"

#define SECTOR 0x10000u
#define FLASH_BASE 0x90000000u

#if NP_SIMULATOR
extern bool np_sim_removed[];
#endif

bool np_uninstall_supported(void) {
#if NP_SIMULATOR
  return true;
#else
  /* The flash system calls have kept their numbers and arguments since
   * Epsilon 20; older or unknown software is left alone. */
  return np_userland() && np_software_major() >= 21;
#endif
}

bool np_battery_ok(void) {
#if NP_SIMULATOR
  return true;
#else
  return np_battery_charging() || np_battery_level() >= 2;
#endif
}

#if !NP_SIMULATOR
/* Epsilon's Ion::Device::Flash::SectorAtAddress: 8 x 4K, 1 x 32K, then 64K. */
static int sector_index(uint32_t address) { return 8 + (int)((address - FLASH_BASE) >> 16); }

static bool zero_range(uint32_t a, uint32_t b) {
  static const uint32_t chunk = 256;
  uint8_t zeros[256];  /* the source must be in RAM, not in the flash being written */
  memset(zeros, 0, sizeof zeros);
  while (a < b) {
    uint32_t n = NP_MIN(chunk - (a % chunk), b - a);
    bool clean = true;
    for (uint32_t i = 0; i < n; i++)
      if (((const volatile uint8_t *)a)[i]) clean = false;
    if (!clean && !np_flash_write((void *)a, zeros, n)) return false;
    a += n;
  }
  return true;
}

static bool erased(uint32_t a, uint32_t b) {
  for (const volatile uint32_t *p = (const volatile uint32_t *)a; (uint32_t)p < b; p++)
    if (*p != 0xFFFFFFFFu) return false;
  return true;
}

static bool zeroed(uint32_t a, uint32_t b) {
  for (const volatile uint8_t *p = (const volatile uint8_t *)a; (uint32_t)p < b; p++)
    if (*p) return false;
  return true;
}

/* The block must be inside the app area, outside the code running now. */
static bool range_ok(uint32_t a, uint32_t b) {
  const np_userland_t *h = np_userland();
  uint32_t self = (uint32_t)(uintptr_t)&range_ok;
  return h && a < b && a % 4 == 0 && b % 4 == 0 && a >= h->apps_flash_start && b <= h->apps_flash_end &&
         !(self >= a && self < b) && b - a < 0x400000;
}

static int wipe(uint32_t a, uint32_t b, void (*progress)(int, int)) {
  uint32_t s0 = (a + SECTOR - 1) & ~(SECTOR - 1), s1 = b & ~(SECTOR - 1);
  if (s1 < s0) s0 = s1 = b;  /* the block sits inside one sector */
  int total = 2 + (int)((s1 - s0) / SECTOR), done = 0;
  if (progress) progress(done, total);
  /* 1. the head marker, so the game is gone even if we stop here */
  if (!zero_range(a, a + 4)) return NP_UNINSTALL_FAILED;
  if (!zero_range(a + 4, s0)) return NP_UNINSTALL_FAILED;
  if (progress) progress(++done, total);
  for (uint32_t s = s0; s < s1; s += SECTOR) {
    if (!erased(s, s + SECTOR) && !np_flash_erase_sector(sector_index(s))) return NP_UNINSTALL_FAILED;
    if (progress) progress(++done, total);
  }
  if (!zero_range(s1, b)) return NP_UNINSTALL_FAILED;
  if (progress) progress(++done, total);
  bool ok = zeroed(a, s0) && erased(s0, s1) && zeroed(s1, b);
  return ok ? NP_UNINSTALL_OK : NP_UNINSTALL_FAILED;
}
#endif

int np_uninstall(int i, void (*progress)(int, int)) {
  const np_game_t *g = &np_games[i];
  if (!np_uninstall_supported()) return NP_UNINSTALL_UNSUPPORTED;
  if (!np_battery_ok()) return NP_UNINSTALL_BATTERY;
  np_interrupts_lock();
  for (const char *const *r = g->records; r && *r; r++) np_storage_delete(*r);
  np_interrupts_unlock();
#if NP_SIMULATOR
  for (int k = 0; k <= 4; k++) {
    if (progress) progress(k, 4);
    np_sleep(180);
  }
  np_sim_removed[i] = true;
  return NP_UNINSTALL_OK;
#else
  uint32_t a = (uint32_t)(uintptr_t)g->begin, b = (uint32_t)(uintptr_t)g->end;
  if (!range_ok(a, b)) return NP_UNINSTALL_FAILED;
  np_interrupts_lock();  /* Home must not stop us halfway */
  int r = wipe(a, b, progress);
  np_interrupts_unlock();
  return r;
#endif
}

void np_finish_pending_uninstalls(void) {
#if !NP_SIMULATOR
  if (!np_uninstall_supported() || !np_battery_ok()) return;
  for (int i = 0; i < np_game_count; i++) {
    if (np_game_installed(i)) continue;
    const np_game_t *g = &np_games[i];
    uint32_t a = (uint32_t)(uintptr_t)g->begin, b = (uint32_t)(uintptr_t)g->end;
    if (!range_ok(a, b)) continue;
    uint32_t s0 = (a + SECTOR - 1) & ~(SECTOR - 1), s1 = b & ~(SECTOR - 1);
    if (s1 < s0) s0 = s1 = b;
    /* Only finish what an uninstall started: it clears the first word before
     * anything else (then, when the block starts a sector, erases it). Any
     * other value is not ours to wipe. */
    uint32_t head = *(const volatile uint32_t *)a;
    if (!(head == 0 || (a == s0 && head == 0xFFFFFFFFu))) continue;
    if (zeroed(a, s0) && erased(s0, s1) && zeroed(s1, b)) continue;
    np_interrupts_lock();
    wipe(a, b, 0);
    np_interrupts_unlock();
  }
#endif
}
