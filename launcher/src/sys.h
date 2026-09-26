/* What the launcher needs from the calculator's software beyond EADK: the
 * userland header (where the file system and the app area are), and a few
 * system calls. Everything here is validated before use and is a no-op in the
 * simulator. */
#ifndef NP_SYS_H
#define NP_SYS_H
#include "np.h"

typedef struct {
  uint32_t magic;             /* 0xDEC0EDFE */
  char version[8];            /* "25.2.2" */
  uint32_t storage_ram, storage_size;
  uint32_t apps_flash_start, apps_flash_end;
  uint32_t apps_ram_start, apps_ram_end;
  uint32_t name_flash_start, name_flash_end;
  uint32_t footer;            /* 0xDEC0EDFE */
} np_userland_t;

/* The header of the running software, or NULL if it cannot be identified. */
const np_userland_t *np_userland(void);
int np_software_major(void);  /* 0 if unknown */

uint32_t np_crc32_bytes(const void *data, uint32_t len);
uint32_t np_crc32_words(const uint32_t *data, uint32_t words);
int np_battery_level(void);   /* 0 empty .. 3+ full, -1 unknown */
bool np_battery_charging(void);
bool np_flash_erase_sector(int sector);
/* While locked, the Home key cannot interrupt the app (Epsilon's circuit
 * breaker), so a critical section runs to its end. */
void np_interrupts_lock(void);
void np_interrupts_unlock(void);
bool np_flash_write(void *dst, const void *src, uint32_t len);
#endif
