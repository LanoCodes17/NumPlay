/* Deleting a game's save files from Epsilon's file system.
 *
 * The file system is a FileSystem object in RAM: a magic word, the record
 * buffer, the magic word again, then private members. Records are packed:
 * a 16-bit size, the NUL-terminated name, the content; a zero size ends the
 * list. Removing a record slides the following ones down, exactly like
 * Epsilon's own destroyRecord. Epsilon also caches the position of the last
 * record it looked up (a name checksum and a pointer, stored after the
 * buffer); that cache is located and verified, then cleared, so Epsilon does
 * not use a stale position. If it cannot be verified, only a record that is
 * last in the list (nothing moves) is removed. */
#include <string.h>
#include "np.h"
#include "sys.h"

#if PLATFORM_DEVICE && !NP_SIMULATOR
#define FS_MAGIC 0xEE0BDDBAu

typedef struct {
  uint8_t *buf;
  uint32_t size;
  uint32_t *after;  /* first word after the footer */
} fs_t;

static bool open_fs(fs_t *fs) {
  const np_userland_t *h = np_userland();
  if (!h) return false;
  uint32_t ram = h->storage_ram, size = h->storage_size;
  if (ram < 0x24000000 || ram % 4 || size < 1024 || size > 0x10000 || ram + size + 8 > 0x24040000) return false;
  uint32_t *magic = (uint32_t *)ram;
  uint32_t footer;
  memcpy(&footer, (uint8_t *)ram + 4 + size, 4);
  if (*magic != FS_MAGIC || footer != FS_MAGIC) return false;
  fs->buf = (uint8_t *)ram + 4;
  fs->size = size;
  fs->after = (uint32_t *)(ram + 4 + size + 4);
  return true;
}

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }

/* End of the records (offset of the terminating zero size), or -1 if corrupt. */
static int records_end(const fs_t *fs) {
  uint32_t p = 0;
  while (p + 2 <= fs->size) {
    uint16_t n = rd16(fs->buf + p);
    if (!n) return (int)p;
    if (n < 4 || p + n > fs->size) return -1;
    p += n;
  }
  return -1;
}

static int find(const fs_t *fs, const char *name, uint16_t *size) {
  uint32_t p = 0;
  size_t len = strlen(name) + 1;
  while (p + 2 <= fs->size) {
    uint16_t n = rd16(fs->buf + p);
    if (n < 4 || p + n > fs->size) return -1;
    if (n >= 2 + len && !memcmp(fs->buf + p + 2, name, len)) {
      *size = n;
      return (int)p;
    }
    p += n;
  }
  return -1;
}

static bool is_record_start(const fs_t *fs, uint32_t off, int end) {
  uint32_t p = 0;
  while ((int)p < end) {
    if (p == off) return true;
    p += rd16(fs->buf + p);
  }
  return false;
}

/* Epsilon's Record checksum: CRC of the CRCs of the base name and extension. */
static uint32_t name_crc(const char *full) {
  const char *dot = strchr(full, '.');
  if (!dot) return 0;
  uint32_t parts[2] = {np_crc32_bytes(full, (uint32_t)(dot - full)), np_crc32_bytes(dot + 1, (uint32_t)strlen(dot + 1))};
  return np_crc32_words(parts, 2);
}

/* Finds Epsilon's (accessible size, cached record checksum, cached pointer)
 * triple after the buffer. Exactly one candidate must pass every check. */
static uint32_t *find_cache(const fs_t *fs, int end) {
  uint32_t *found = 0;
  for (int i = 1; i < 96; i++) {
    uint32_t *w = fs->after + i;
    uint32_t accessible = w[0], crc = w[1], ptr = w[2];
    if (accessible > fs->size || (int)accessible < end + 2 || accessible < fs->size / 2) continue;
    if (ptr == 0) {
      if (crc != 0) continue;
    } else {
      uint32_t base = (uint32_t)(uintptr_t)fs->buf;
      if (ptr < base || ptr >= base + (uint32_t)end || !is_record_start(fs, ptr - base, end)) continue;
      if (crc == 0 || crc != name_crc((const char *)(uintptr_t)(ptr + 2))) continue;
    }
    if (found) return 0;
    found = w;
  }
  return found;
}

bool np_storage_delete(const char *name) {
  fs_t fs;
  if (!open_fs(&fs)) return false;
  int end = records_end(&fs);
  if (end < 0) return false;
  uint16_t size;
  int at = find(&fs, name, &size);
  if (at < 0) return true;
  uint32_t *cache = find_cache(&fs, end);
  bool last = at + size == end;
  if (!cache && !last) return false;
  /* slide what follows (and the terminating zero) down */
  memmove(fs.buf + at, fs.buf + at + size, (size_t)(end + 2 - (at + size)));
  memset(fs.buf + end + 2 - size, 0, size);
  if (cache) cache[1] = cache[2] = 0;
  return true;
}

uint32_t np_storage_record_size(const char *name) {
  fs_t fs;
  uint16_t size;
  if (!open_fs(&fs) || find(&fs, name, &size) < 0) return 0;
  return size;
}

#else
bool np_storage_delete(const char *name) {
  (void)name;
  return true;
}
uint32_t np_storage_record_size(const char *name) {
  (void)name;
  return 0;
}
#endif
