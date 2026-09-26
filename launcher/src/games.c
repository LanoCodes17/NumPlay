/* Installed games: each one is a block of flash between its head and tail
 * markers (see tools/gen_games.py). Uninstalling wipes the block, so a game
 * counts as installed only while both markers are intact. */
#include <string.h>
#include "np.h"
#include "sys.h"

#define HEAD_MAGIC 0x3147504Eu /* "NPG1" */
#define TAIL_MAGIC 0x444E4550u /* "PEND" */

#if NP_SIMULATOR
bool np_sim_removed[16];
#endif

bool np_game_installed(int i) {
  const np_game_t *g = &np_games[i];
#if NP_SIMULATOR
  (void)g;
  return !np_sim_removed[i];
#else
  /* volatile: this flash changes under the compiler's feet when uninstalling */
  const volatile uint32_t *head = g->begin, *tail = g->end - 1;
  return head[0] == HEAD_MAGIC && tail[0] == TAIL_MAGIC;
#endif
}

uint32_t np_game_size(int i) {
  const np_game_t *g = &np_games[i];
#if NP_SIMULATOR
  uint32_t shots = 0;
  for (int k = 0; k < g->nshots; k++) shots += g->shots[k].zsize + 2 * g->shots[k].ncolors;
  return g->est_size + shots;
#else
  return (uint32_t)((const uint8_t *)g->end - (const uint8_t *)g->begin);
#endif
}

void np_game_run(int i) {
  const np_game_t *g = &np_games[i];
  /* fresh RAM, as if the game had just been launched from the home screen */
  if (g->data_size) memcpy(g->data, g->data_init, g->data_size);
  if (g->bss_size) memset(g->bss, 0, g->bss_size);
  g->main();
}
