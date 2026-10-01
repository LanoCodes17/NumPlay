/* Saving: each world's record (its name, seed, the time, the weather, the player, chests and
 * furnaces) next to the records of its changed blocks (edits.c), and the options. A world
 * saves every 45 seconds of play, as Minecraft does, and when you quit; block changes reach
 * storage as they fill the journal. Worlds live in slots: the records of slot 3 are
 * "nb3.nbw" and "nb3r..." (its regions). */
#include <stddef.h>
#include "nb.h"
#pragma GCC optimize("Os")   /* (not where the time goes: small) */
#include "edits.h"

#define OPTIONS_REC "numblocks.cfg"

Options opt = {2, 0, 100, 1, 1, 0, 0, 0};
int64_t world_seed;

/* the record's head: version 1 had no name (it was "New World") */
typedef struct {
  char magic[4];
  uint16_t player_size, tiles_size;
  int64_t seed;
  uint32_t time;
  Weather weather;
} Head1;
typedef struct {
  Head1 h;
  char name[WORLD_NAME + 1];
  uint32_t played;
} Head2;   /* version 2 had no cheats or game rules */
typedef struct {
  Head2 h2;
  uint8_t cheats, rules, pad[2];
} Head;

extern uint32_t game_time;
void *tiles_data(uint32_t *len);

uint8_t game_rules = GR_DEFAULT;
bool world_cheats, lan_open, lan_cheats;
static int slot = 1;                   /* the world being played */
static char cur_name[WORLD_NAME + 1];  /* its name */
static uint32_t cur_played;

static void prefix_of(int s, char *out) { out[0] = 'n', out[1] = 'b', out[2] = (char)('0' + s), out[3] = 0; }
static void record_of(int s, char *out) {
  prefix_of(s, out);
  out[3] = '.', out[4] = 'n', out[5] = 'b', out[6] = 'w', out[7] = 0;
}
static void copy_name(char *d, const char *s) {
  int i = 0;
  for (; s[i] && i < WORLD_NAME; i++) d[i] = s[i];
  d[i] = 0;
}

/* a slot's record, read as the latest head (a version 1 record named "New World"); NULL: none */
static const uint8_t *read_head(int s, Head *h, uint32_t *len, uint32_t *head_len) {
  char rec[8];
  record_of(s, rec);
  const uint8_t *d = plat_load(rec, len);
  if (!d || *len < sizeof(Head1)) return NULL;
  memset(h, 0, sizeof *h);
  if (!memcmp(d, "NBW1", 4)) {
    memcpy(&h->h2.h, d, sizeof(Head1));
    copy_name(h->h2.name, "New World");
    *head_len = sizeof(Head1);
  } else if (!memcmp(d, "NBW2", 4) && *len >= sizeof(Head2)) {
    memcpy(&h->h2, d, sizeof(Head2));
    *head_len = sizeof(Head2);
  } else if (!memcmp(d, "NBW3", 4) && *len >= sizeof(Head)) {
    memcpy(h, d, sizeof(Head));
    *head_len = sizeof(Head);
  } else
    return NULL;
  h->h2.name[WORLD_NAME] = 0;
  if (h->h2.h.player_size != sizeof pl || *len < *head_len + sizeof pl) return NULL;
  if (*head_len < sizeof(Head)) {
    /* (older worlds: cheats as a new world of their mode would have them, the rules as they start) */
    h->cheats = d[*head_len + offsetof(Player, mode)] == 1;
    h->rules = GR_DEFAULT;
  }
  return d;
}

bool world_info(int s, WorldInfo *w) {
  Head h;
  uint32_t len, hl;
  const uint8_t *d = read_head(s, &h, &len, &hl);
  if (!d) return false;
  Player p;
  memcpy(&p, d + hl, sizeof p);
  copy_name(w->name, h.h2.name);
  w->mode = p.mode;
  w->cheats = h.cheats;
  w->seed = h.h2.h.seed;
  w->played = h.h2.played;
  return true;
}

int world_free_slot(void) {
  WorldInfo w;
  for (int s = 1; s <= MAX_WORLDS; s++)
    if (!world_info(s, &w)) return s;
  return 0;
}

static bool name_taken(const char *n) {
  WorldInfo w;
  for (int s = 1; s <= MAX_WORLDS; s++)
    if (world_info(s, &w) && !strcmp(w.name, n)) return true;
  return false;
}

void world_unique_name(const char *base, char *out) {
  char b[WORLD_NAME + 1];
  copy_name(b, base[0] ? base : "New World");
  copy_name(out, b);
  for (int k = 2; name_taken(out) && k < 100; k++) {
    /* "New World (2)": the number kept whole, the name cut to fit */
    char sfx[8] = {' ', '(', 0};
    int n = 2;
    if (k >= 10) sfx[n++] = (char)('0' + k / 10);
    sfx[n++] = (char)('0' + k % 10), sfx[n++] = ')', sfx[n] = 0;
    int bl = (int)strlen(b), room = WORLD_NAME - n;
    if (bl > room) bl = room;
    memcpy(out, b, (size_t)bl);
    memcpy(out + bl, sfx, (size_t)n + 1);
  }
}

bool save_world(void) {
  bool ok = edits_flush();
  uint32_t tl;
  void *t = tiles_data(&tl);
  Head h;
  memset(&h, 0, sizeof h);
  h.h2.h = (Head1){{'N', 'B', 'W', '3'}, (uint16_t)sizeof pl, (uint16_t)tl, world_seed, game_time, weather};
  copy_name(h.h2.name, cur_name);
  h.h2.played = cur_played;
  h.cheats = world_cheats, h.rules = game_rules;
  /* one record: the head, the player, the tiles (written from a buffer made in the merge scratch) */
  uint32_t n = sizeof h + sizeof pl + tl;
  uint8_t *b = (uint8_t *)edits_scratch(n);
  if (!b) return false;
  memcpy(b, &h, sizeof h);
  memcpy(b + sizeof h, &pl, sizeof pl);
  memcpy(b + sizeof h + sizeof pl, t, tl);
  char rec[8];
  record_of(slot, rec);
  ok = plat_save(rec, b, n) && ok;
  edits_forget();
  return ok;
}

/* the world becoming the one played: the latest in the list */
static void played(int s) {
  opt.last_world = (uint8_t)s;
  cur_played = ++opt.plays;
  save_options();
}

bool load_world(int s) {
  Head h;
  uint32_t len, hl, tl;
  const uint8_t *d = read_head(s, &h, &len, &hl);
  void *t = tiles_data(&tl);
  if (!d || h.h2.h.tiles_size != tl || len != hl + sizeof pl + tl) return false;
  slot = s;
  copy_name(cur_name, h.h2.name);
  cur_played = h.h2.played;
  world_seed = h.h2.h.seed;
  game_time = h.h2.h.time;
  world_cheats = h.cheats, lan_open = lan_cheats = false, game_rules = h.rules;
  /* World.calculateInitialWeather */
  weather = h.h2.h.weather;
  rain_str = weather.raining ? 1 : 0;
  thunder_str = weather.thundering ? 1 : 0;
  memcpy(&pl, d + hl, sizeof pl);
  memcpy(t, d + hl + sizeof pl, tl);
  memset(ents, 0, sizeof ents);
  char pre[4];
  prefix_of(s, pre);
  world_new(world_seed, pre);
  if (s != opt.last_world || h.h2.played != opt.plays) played(s);
  return true;
}

void new_world(int s, int64_t seed, int mode, bool cheats, const char *name) {
  char pre[4];
  prefix_of(s, pre);
  plat_remove_prefix(pre);
  slot = s;
  copy_name(cur_name, name);
  uint32_t tl;
  void *t = tiles_data(&tl);   /* (first: the length is only known after the call) */
  memset(t, 0, tl);
  memset(ents, 0, sizeof ents);
  world_seed = seed;
  game_time = 0;
  world_cheats = cheats, lan_open = lan_cheats = false, game_rules = GR_DEFAULT;
  memset(&weather, 0, sizeof weather);
  rain_str = thunder_str = 0;
  world_new(seed, pre);
  player_spawn();
  pl.mode = (uint8_t)mode;
  played(s);
}

bool rename_world(int s, const char *name) {
  Head h;
  uint32_t len, hl;
  const uint8_t *d = read_head(s, &h, &len, &hl);
  if (!d) return false;
  /* the record again with the new head (made in the merge scratch, as saving does) */
  uint32_t body = len - hl, n = sizeof h + body;
  uint8_t *b = (uint8_t *)edits_scratch(n);
  if (!b) return false;
  memcpy(b + sizeof h, d + hl, body);   /* (first: the scratch may be where nothing else is) */
  memcpy(h.h2.h.magic, "NBW3", 4);
  copy_name(h.h2.name, name);
  memcpy(b, &h, sizeof h);
  char rec[8];
  record_of(s, rec);
  bool ok = plat_save(rec, b, n);
  edits_forget();
  if (s == slot) copy_name(cur_name, name);
  return ok;
}

void delete_world(int s) {
  char pre[4];
  prefix_of(s, pre);
  plat_remove_prefix(pre);
  if (opt.last_world == s) opt.last_world = 0, save_options();
}

void load_options(void) {
  uint32_t len = 0;
  const uint8_t *d = plat_load(OPTIONS_REC, &len);
  /* (an older, shorter record: what it has; the rest stays as it was) */
  if (d && len <= sizeof opt) memcpy(&opt, d, len);
}
void save_options(void) { plat_save(OPTIONS_REC, &opt, sizeof opt); }
