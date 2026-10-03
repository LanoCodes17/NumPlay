/* GameManager.SaveGame and LoadGame: PlayerData in a file of the calculator's, one a slot. The file: a header with a
 * checksum, PlayerData's saved part, then the objects' states by name (tools/ents.py: PHASH), so that a save keeps
 * meaning the same objects whatever the data's version. A file that does not check out is never loaded. */
#pragma GCC optimize("Os")   /* (its code small: not where a frame's time goes) */
#include "game.h"

#define SAVE_MAGIC 0x56534B48u   /* "HKSV" */
#define SAVE_VERSION 2   /* (1: PlayerData up to PD_SAVED_V1, loaded with the rest cleared) */

typedef struct {
  uint32_t magic, version, size, crc;   /* (size, crc: of what follows the header) */
} SaveHead;

static int cur_slot;

static uint32_t crc32(const uint8_t *p, uint32_t n) {
  uint32_t c = 0xFFFFFFFFu;
  while (n--) {
    c ^= *p++;
    for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1)));
  }
  return ~c;
}

static void slot_name(int slot, char *out) {
  memcpy(out, "hk1.sav", 8);
  out[2] = (char)('1' + slot);
}

void save_select(int slot) { cur_slot = slot < 0 || slot >= SAVE_SLOTS ? 0 : slot; }

static const uint8_t *phash(uint32_t *n) {
  const uint8_t *s = section(SEC_PHASH);
  *n = rd32(s);
  return s + 4;
}

bool save_game(void) {
  /* (built in the decoders' scratch memory: no decoding goes on meanwhile) */
  uint8_t *buf = (uint8_t *)g_scratch;
  uint32_t cap = sizeof g_scratch;
  SaveHead h = {SAVE_MAGIC, SAVE_VERSION, 0, 0};
  uint32_t at = sizeof h;
  memcpy(buf + at, &g_pd, PD_SAVED);
  at += PD_SAVED;
  uint32_t nbits, count = 0;
  const uint8_t *names = phash(&nbits);
  uint32_t count_at = at;
  at += 4;
  for (uint32_t i = 0; i < nbits && i < MAX_PERSIST; i++) {
    if (!(g_pd.persist[i >> 3] >> (i & 7) & 1)) continue;
    if (at + 4 > cap) return false;
    memcpy(buf + at, names + 4 * i, 4);
    at += 4, count++;
  }
  memcpy(buf + count_at, &count, 4);
  h.size = at - sizeof h;
  h.crc = crc32(buf + sizeof h, h.size);
  memcpy(buf, &h, sizeof h);
  char name[8];
  slot_name(cur_slot, name);
  return plat_save(name, buf, at);
}

/* a slot's file, checked: its PlayerData part (*pd_size long: its version's), its states' names */
static const uint8_t *checked(int slot, uint32_t *count, uint32_t *pd_size) {
  char name[8];
  slot_name(slot, name);
  uint32_t len;
  const uint8_t *p = plat_load(name, &len);
  SaveHead h;
  if (!p || len < sizeof h) return NULL;
  memcpy(&h, p, sizeof h);
  uint32_t pd = h.version == SAVE_VERSION ? PD_SAVED : h.version == 1 ? PD_SAVED_V1 : 0;
  if (h.magic != SAVE_MAGIC || !pd || len < sizeof h + pd + 4 || h.size != len - sizeof h ||
      h.crc != crc32(p + sizeof h, h.size))
    return NULL;
  memcpy(count, p + sizeof h + pd, 4);
  if (sizeof h + pd + 4 + 4ull * *count != len) return NULL;
  *pd_size = pd;
  return p + sizeof h;
}

int save_stats(int slot, SaveStats *st) {
  char name[8];
  slot_name(slot, name);
  uint32_t len, count, pd;
  if (!plat_load(name, &len)) return 0;
  const uint8_t *p = checked(slot, &count, &pd);
  if (!p) return -1;
  /* (its PlayerData's fields, read where they are) */
  memcpy(&st->max_health, p + offsetof(PlayerData, max_health), sizeof st->max_health);
  memcpy(&st->mp_reserve_max, p + offsetof(PlayerData, mp_reserve_max), sizeof st->mp_reserve_max);
  memcpy(&st->geo, p + offsetof(PlayerData, geo), sizeof st->geo);
  memcpy(&st->play_time, p + offsetof(PlayerData, play_time), sizeof st->play_time);
  st->zone = p[offsetof(PlayerData, map_zone)];
  return 1;
}

bool save_clear(int slot) {
  char name[8];
  slot_name(slot, name);
  return plat_remove(name);
}

bool save_exists(int slot) {
  uint32_t count, pd;
  return checked(slot, &count, &pd) != NULL;
}

bool save_load(int slot) {
  uint32_t count, pd;
  const uint8_t *p = checked(slot, &count, &pd);
  if (!p) return false;
  save_select(slot);
  memset(&g_pd, 0, sizeof g_pd);
  memcpy(&g_pd, p, pd);
  /* (counts kept within their arrays whatever the file says) */
  for (int c = 0; c < 4; c++)
    if (g_pd.markers_placed[c] > 6) g_pd.markers_placed[c] = 6;
  if (g_pd.map_key_pref > 2) g_pd.map_key_pref = 0;
  /* (names kept within their bounds whatever the file says) */
  g_pd.respawn_scene[SCENE_NAME - 1] = g_pd.respawn_marker[SCENE_NAME - 1] = g_pd.shade_scene[SCENE_NAME - 1] = 0;
  uint32_t nbits;
  const uint8_t *names = phash(&nbits);
  const uint8_t *q = p + pd + 4;
  for (uint32_t k = 0; k < count; k++, q += 4) {
    uint32_t want = rd32(q);
    for (uint32_t i = 0; i < nbits && i < MAX_PERSIST; i++)
      if (rd32(names + 4 * i) == want) {
        g_pd.persist[i >> 3] |= (uint8_t)(1 << (i & 7));
        break;
      }
  }
  return true;
}

void save_set_respawn_kind(const char *marker, bool facing_right, int type) {
  /* (SetPlayerDataString respawnMarkerName, respawnScene; respawnType 1: a bench, 0: face down) */
  strncpy(g_pd.respawn_marker, marker, SCENE_NAME - 1);
  strncpy(g_pd.respawn_scene, room_name(g_room.id), SCENE_NAME - 1);
  g_pd.respawn_marker[SCENE_NAME - 1] = g_pd.respawn_scene[SCENE_NAME - 1] = 0;
  g_pd.respawn_type = (uint8_t)type;
  g_pd.respawn_facing_right = facing_right;
}

void save_set_respawn(const char *marker, bool facing_right) { save_set_respawn_kind(marker, facing_right, 1); }
