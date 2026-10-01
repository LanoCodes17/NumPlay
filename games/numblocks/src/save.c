/* Saving: the world's record (its seed, the time, the player, chests and
 * furnaces) next to the records of its changed blocks (edits.c), and the
 * options. A world saves every 45 seconds of play, as Minecraft does, and
 * when you quit; block changes reach storage as they fill the journal. */
#include "nb.h"
#include "edits.h"

#define WORLD "nb1"
#define WORLD_REC "nb1.nbw"
#define OPTIONS_REC "numblocks.cfg"

Options opt = {2, 0, 100, 1, 1};
int64_t world_seed;

typedef struct {
  char magic[4];
  uint16_t player_size, tiles_size;
  int64_t seed;
  uint32_t time;
} Head;

extern uint32_t game_time;
void *tiles_data(uint32_t *len);

static uint8_t buf_head[sizeof(Head)];

bool save_world(void) {
  bool ok = edits_flush();
  uint32_t tl;
  void *t = tiles_data(&tl);
  Head h = {{'N', 'B', 'W', '1'}, (uint16_t)sizeof pl, (uint16_t)tl, world_seed, game_time};
  /* one record: the head, the player, the tiles (written from a buffer made in the merge scratch) */
  uint32_t n = sizeof h + sizeof pl + tl;
  uint8_t *b = (uint8_t *)edits_scratch(n);
  if (!b) return false;
  memcpy(b, &h, sizeof h);
  memcpy(b + sizeof h, &pl, sizeof pl);
  memcpy(b + sizeof h + sizeof pl, t, tl);
  ok = plat_save(WORLD_REC, b, n) && ok;
  edits_forget();
  (void)buf_head;
  return ok;
}

bool world_exists(void) {
  uint32_t len = 0;
  const uint8_t *d = plat_load(WORLD_REC, &len);
  return d && len >= sizeof(Head) && !memcmp(d, "NBW1", 4);
}

/* the saved world's game mode, or -1 */
int world_mode(void) {
  uint32_t len = 0;
  const uint8_t *d = plat_load(WORLD_REC, &len);
  if (!d || len < sizeof(Head) + sizeof pl || memcmp(d, "NBW1", 4)) return -1;
  Player p;
  memcpy(&p, d + sizeof(Head), sizeof p);
  return p.mode;
}

bool load_world(void) {
  uint32_t len = 0, tl;
  const uint8_t *d = plat_load(WORLD_REC, &len);
  void *t = tiles_data(&tl);
  Head h;
  if (!d || len < sizeof h) return false;
  memcpy(&h, d, sizeof h);
  if (memcmp(h.magic, "NBW1", 4) || h.player_size != sizeof pl || h.tiles_size != tl || len != sizeof h + sizeof pl + tl)
    return false;
  world_seed = h.seed;
  game_time = h.time;
  memcpy(&pl, d + sizeof h, sizeof pl);
  memcpy(t, d + sizeof h + sizeof pl, tl);
  memset(ents, 0, sizeof ents);
  world_new(world_seed, WORLD);
  return true;
}

void new_world(int64_t seed, int mode) {
  plat_remove_prefix(WORLD);
  uint32_t tl;
  memset(tiles_data(&tl), 0, tl);
  memset(ents, 0, sizeof ents);
  world_seed = seed;
  game_time = 0;
  world_new(seed, WORLD);
  player_spawn();
  pl.mode = (uint8_t)mode;
}

void delete_world(void) { plat_remove_prefix(WORLD); }

void load_options(void) {
  uint32_t len = 0;
  const uint8_t *d = plat_load(OPTIONS_REC, &len);
  if (d && len == sizeof opt) memcpy(&opt, d, sizeof opt);
}
void save_options(void) { plat_save(OPTIONS_REC, &opt, sizeof opt); }
