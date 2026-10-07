#include "sys.h"

#if PLATFORM_DEVICE && !NP_SIMULATOR
#define USERLAND_MAGIC 0xDEC0EDFEu

/* System calls, numbered as in Epsilon's svcall.h (stable since 2022). The
 * kernel returns the result in r0 and may clobber r1-r3. */
#define SVC2(n, a, b)                                                                        \
  ({                                                                                         \
    register uint32_t r0 __asm__("r0") = (uint32_t)(a);                                      \
    register uint32_t r1 __asm__("r1") = (uint32_t)(b);                                      \
    __asm__ volatile("svc %[imm]" : "+r"(r0), "+r"(r1) : [imm] "I"(n) : "r2", "r3", "r12", "memory"); \
    r0;                                                                                      \
  })
#define SVC4(n, a, b, c, d)                                                                  \
  ({                                                                                         \
    register uint32_t r0 __asm__("r0") = (uint32_t)(a);                                      \
    register uint32_t r1 __asm__("r1") = (uint32_t)(b);                                      \
    register uint32_t r2 __asm__("r2") = (uint32_t)(c);                                      \
    register uint32_t r3 __asm__("r3") = (uint32_t)(d);                                      \
    __asm__ volatile("svc %[imm]" : "+r"(r0), "+r"(r1), "+r"(r2), "+r"(r3) : [imm] "I"(n) : "r12", "memory"); \
    r0;                                                                                      \
  })

/* 1 for Epsilon's header, 2 for Upsilon's (no device name: the footer two words sooner, then
 * Omega's magic; and none of the system calls for Home, checksums or flash), 0 if neither */
static int header_kind(uint32_t address) {
  /* the userland header sits at the start of the userland, after the kernel
   * (and an optional extra data sector) of either firmware slot */
  if (address != 0x90010000 && address != 0x90020000 && address != 0x90410000 && address != 0x90420000) return 0;
  const np_userland_t *h = (const np_userland_t *)address;
  const volatile uint32_t *w = (const volatile uint32_t *)address;
  if (h->magic != USERLAND_MAGIC) return 0;
  int kind = h->footer == USERLAND_MAGIC ? 1 : w[9] == USERLAND_MAGIC && w[10] == 0xEFBEADDEu ? 2 : 0;
  /* this app must run from the app area it describes */
  uint32_t self = (uint32_t)(uintptr_t)&header_kind;
  bool inside = h->apps_flash_start < h->apps_flash_end && self >= h->apps_flash_start && self < h->apps_flash_end &&
                h->apps_flash_start % 0x10000 == 0;
  return inside ? kind : 0;
}

static bool upsilon;

const np_userland_t *np_userland(void) {
  static const np_userland_t *cached;
  static bool done;
  if (done) return cached;
  done = true;
  /* Only external flash is probed: it exists on every model (RAM addresses
   * differ between models, and reading a missing one faults). */
  static const uint32_t candidates[] = {0x90010000, 0x90020000, 0x90410000, 0x90420000};
  for (unsigned i = 0; i < 4; i++) {
    int kind = header_kind(candidates[i]);
    if (!kind) continue;
    if (cached) {  /* two matches: refuse to guess */
      cached = 0;
      upsilon = false;
      return 0;
    }
    cached = (const np_userland_t *)candidates[i];
    upsilon = kind == 2;
  }
  return cached;
}

bool np_upsilon(void) { return np_userland() && upsilon; }
bool np_slow_model(void) {
  const np_userland_t *h = np_userland();
  return h && h->storage_ram < 0x24000000u;
}

/* The whole session runs with Home held back (Epsilon's circuit breaker),
 * and ends with the app's RAM cleared: see games/common/epsilon_app.h. */
extern char _data_section_start_ram[], _heap_end[];
static bool session_locked;

void np_session_begin(void) {
  if (!np_userland() || np_upsilon()) return;
  SVC2(10, 0, 0);
  session_locked = true;
}

int np_session_end(void) {
  bool unlock = session_locked;
  for (volatile uint32_t *p = (volatile uint32_t *)(void *)_data_section_start_ram; p < (volatile uint32_t *)(void *)_heap_end; p++)
    *p = 0;
  if (unlock) SVC2(13, 0, 0);
  return 0;
}

uint32_t np_crc32_bytes(const void *data, uint32_t len) { return len ? SVC2(15, data, len) : 0; }
uint32_t np_crc32_words(const uint32_t *data, uint32_t words) { return SVC2(16, data, words); }
int np_battery_level(void) { return (int)(SVC2(4, 0, 0) & 0xFF); }
bool np_battery_charging(void) { return (SVC2(3, 0, 0) & 0xFF) != 0; }
bool np_flash_erase_sector(int sector) { return (SVC2(30, sector, 1) & 0xFF) != 0; }
bool np_flash_write(void *dst, const void *src, uint32_t len) { return (SVC4(32, dst, src, len, 1) & 0xFF) != 0; }
void np_interrupts_lock(void) {
  if (!np_upsilon()) SVC2(10, 0, 0);
}
void np_interrupts_unlock(void) {
  if (!np_upsilon()) SVC2(13, 0, 0);
}

#else /* simulator */

const np_userland_t *np_userland(void) { return 0; }
uint32_t np_crc32_bytes(const void *data, uint32_t len) {
  (void)data, (void)len;
  return 0;
}
uint32_t np_crc32_words(const uint32_t *data, uint32_t words) {
  (void)data, (void)words;
  return 0;
}
int np_battery_level(void) { return -1; }
bool np_battery_charging(void) { return false; }
bool np_flash_erase_sector(int sector) {
  (void)sector;
  return false;
}
bool np_flash_write(void *dst, const void *src, uint32_t len) {
  (void)dst, (void)src, (void)len;
  return false;
}
void np_interrupts_lock(void) {}
void np_interrupts_unlock(void) {}
void np_session_begin(void) {}
int np_session_end(void) { return 0; }
bool np_upsilon(void) { return false; }
bool np_slow_model(void) { return false; }
#endif

int np_software_major(void) {
  const np_userland_t *h = np_userland();
  if (!h) return 0;
  int major = 0, i = 0;
  while (i < 3 && h->version[i] >= '0' && h->version[i] <= '9') major = major * 10 + (h->version[i++] - '0');
  return i && h->version[i] == '.' ? major : 0;
}
