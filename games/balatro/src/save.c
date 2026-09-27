/* The save: one record, balatro.sav. A header, the settings and lifetime
 * stats, then the run (only the used parts of its arrays), and a checksum.
 * Anything that does not check out is ignored. */
#include <stddef.h>
#include <string.h>
#include "../../common/epsilon_app.h"
#include "../../common/epsilon_files.h"
#include "game.h"
#include "save.h"

static const char NAME[] = "balatro.sav";
#define MAGIC 0xBA
#define VERSION 1
settings_t S = {1, 0, 0, 0, 0, 0};

#include "gfx.h"
static uint8_t *buf;
static int bufsize;
static int pos, len, reading, bad;

static void blk(void *p, int n) {
  if (bad || n < 0) return;
  if (pos + n > bufsize || (reading && pos + n > len)) {
    bad = 1;
    return;
  }
  if (reading) memcpy(p, buf + pos, (size_t)n);
  else memcpy(buf + pos, p, (size_t)n);
  pos += n;
}
#define F(x) blk(&R.x, (int)sizeof R.x)
#define A(x, n, max)            \
  do {                          \
    if ((n) > (max)) bad = 1;   \
    blk(R.x, (int)sizeof R.x[0] * (n)); \
  } while (0)

static void sync_run(void) {
  blk(&R, (int)offsetof(run_t, cards));
  F(ncards_alloc);
  A(cards, R.ncards_alloc, MAXCARDS);
  F(ndeck);
  A(deck, R.ndeck, MAXCARDS);
  F(nhand);
  A(hand, R.nhand, MAXHAND);
  A(sel, R.nhand, MAXHAND);
  F(nplay);
  A(play, R.nplay, 8);
  F(ndiscard);
  A(discard, R.ndiscard, MAXCARDS);
  F(njokers);
  A(jokers, R.njokers, MAXJ);
  F(ncons);
  A(cons, R.ncons, MAXCONS);
  F(ntags);
  A(tags, R.ntags, MAXTAGS);
  A(tag_orbital, R.ntags, MAXTAGS);
  F(nshop);
  A(shop, R.nshop, MAXSHOP);
  F(nshop_v);
  A(shop_v, R.nshop_v, 3);
  F(nshop_p);
  A(shop_p, R.nshop_p, 2);
  F(pack_kind), F(pack_size), F(pack_picks), F(pack_from_tag);
  F(npack);
  A(pack, R.npack, MAXPACK);
  blk(&R.cash_blind, (int)(offsetof(run_t, end_marker) - offsetof(run_t, cash_blind)));
}

static uint32_t sum(const uint8_t *p, int n) {
  uint32_t h = 2166136261u;
  for (int i = 0; i < n; i++) h = (h ^ p[i]) * 16777619u;
  return h;
}

void save_write(int with_run) {
  buf = g_scratch(&bufsize);
  pos = 0, reading = 0, bad = 0;
  uint8_t head[3] = {MAGIC, VERSION, (uint8_t)(with_run ? 1 : 0)};
  blk(head, 3);
  blk(&S, (int)sizeof S);
  if (with_run) sync_run();
  if (bad) return;
  uint32_t h = sum(buf, pos);
  blk(&h, 4);
  if (bad) return;
  ef_write(NAME, buf, (uint32_t)pos);
}

/* returns 1 if a run was restored into R */
int save_read(void) {
  uint32_t n = 0;
  const uint8_t *p = ef_read(NAME, &n);
  buf = g_scratch(&bufsize);
  if (!p || n < 3 + sizeof S + 4 || n > (uint32_t)bufsize) return 0;
  memcpy(buf, p, n);
  len = (int)n;
  uint32_t h;
  memcpy(&h, buf + n - 4, 4);
  if (buf[0] != MAGIC || buf[1] != VERSION || sum(buf, (int)n - 4) != h) return 0;
  pos = 3, reading = 1, bad = 0;
  settings_t s2;
  blk(&s2, (int)sizeof s2);
  if (bad) return 0;
  if (s2.speed > 3) s2.speed = 1;
  S = s2;
  if (!buf[2]) return 0;
  memset(&R, 0, sizeof R);
  sync_run();
  if (bad || pos != len - 4 || R.phase > PH_WIN || R.njokers > MAXJ || R.ncons > MAXCONS) {
    memset(&R, 0, sizeof R);
    return 0;
  }
  /* indices must point at used cards */
  for (int i = 0; i < R.ndeck; i++)
    if (R.deck[i] >= R.ncards_alloc) bad = 1;
  for (int i = 0; i < R.nhand; i++)
    if (R.hand[i] >= R.ncards_alloc) bad = 1;
  for (int i = 0; i < R.nplay; i++)
    if (R.play[i] >= R.ncards_alloc) bad = 1;
  for (int i = 0; i < R.ndiscard; i++)
    if (R.discard[i] >= R.ncards_alloc) bad = 1;
  if (bad || R.ncards_alloc > MAXCARDS) {
    memset(&R, 0, sizeof R);
    return 0;
  }
  return 1;
}
