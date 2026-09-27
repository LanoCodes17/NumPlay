/* Save files in Epsilon's file system, with the careful record handling shared
 * with the games (games/common/epsilon_files.h): deleting a game's saves, and
 * NumPlay's own settings. */
#include "np.h"
#include "../../games/common/epsilon_files.h"

bool np_storage_delete(const char *name) { return ef_remove(name); }

uint32_t np_storage_record_size(const char *name) {
  uint32_t len = 0;
  return ef_read(name, &len) ? len : 0;
}

bool np_reset_game(int game) {
  bool ok = true;
  for (const char *const *r = np_games[game].progress; r && *r; r++) ok &= np_storage_delete(*r);
  return ok;
}

#define CONFIG_NAME "numplay.set"
#define CONFIG_MAGIC 0x4E /* 'N' */

void np_config_load(np_config_t *c) {
  c->disguise = false;
  c->secret = NP_SECRET_XNT;
  c->hint = true;
  uint32_t len = 0;
  const uint8_t *d = ef_read(CONFIG_NAME, &len);
  if (!d || len != 4 || d[0] != CONFIG_MAGIC || d[1] != 1) return;
  c->disguise = d[2] & 1;
  c->hint = !(d[2] & 2); /* a flag for "no hint": older files keep the hint on */
  if (d[3] < NP_SECRET_COUNT) c->secret = d[3];
}

bool np_config_save(const np_config_t *c) {
  uint8_t d[4] = {CONFIG_MAGIC, 1, (uint8_t)(c->disguise | (c->hint ? 0 : 2)), c->secret};
  return ef_write(CONFIG_NAME, d, sizeof d);
}
