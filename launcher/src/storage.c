/* Deleting a game's save files from Epsilon's file system, with the careful
 * record handling shared with the games (games/common/epsilon_files.h). */
#include "np.h"
#include "../../games/common/epsilon_files.h"

bool np_storage_delete(const char *name) { return ef_remove(name); }

uint32_t np_storage_record_size(const char *name) {
  uint32_t len = 0;
  return ef_read(name, &len) ? len : 0;
}
