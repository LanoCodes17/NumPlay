/* Rooms: a scene's header in RAM, its instances in sectors along its long axis, read in place near the camera. */
#include "hk.h"

#define HDR_RAM 2048
#define ENTRY 44   /* name (32), header offset, packed size, size */
static uint32_t hdr_ram[HDR_RAM / 4];
Room g_room = {.id = -1};

static const uint8_t *room_entry(int id) { return section(SEC_ROOMS) + 4 + ENTRY * id; }
const char *room_name(int id) { return (const char *)room_entry(id); }

/* (tools/pack.py: RVAR) a room loads with other scenes: by a PlayerData bool, its first one (the room's own entry) or
 * the alternative (an entry of its own); each one's groups hidden in the other */
static int room_variant(int id, uint16_t *lo, uint16_t *hi) {
  const uint8_t *v = section(SEC_RVAR);
  uint32_t n = rd32(v);
  *lo = *hi = 0;
  for (uint32_t i = 0; i < n; i++) {
    const uint8_t *r = v + 4 + 16 * i;
    if ((r[0] | r[1] << 8) != id) continue;
    bool first = room_flag(r[4] | r[5] << 8) == (r[6] != 0);
    const uint8_t *h = r + (first ? 12 : 8);   /* (the other's groups) */
    *lo = (uint16_t)(h[0] | h[1] << 8), *hi = (uint16_t)(h[2] | h[3] << 8);
    return first ? id : (r[2] | r[3] << 8);
  }
  return id;
}

bool room_load(int id) {
  if (id < 0 || id >= NUM_ROOMS) return false;
  uint16_t lo, hi;
  const uint8_t *e = room_entry(room_variant(id, &lo, &hi));
  uint32_t off = rd32(e + 32), comp = rd32(e + 36), raw = rd32(e + 40);
  if (raw > sizeof hdr_ram) return false;
  lz_decode(section(SEC_RBLOB) + off, comp, (uint8_t *)hdr_ram, raw);
  RoomHdr *h = (RoomHdr *)hdr_ram;
  g_room.id = id;
  g_room.h = h;
  g_room.hide_lo = lo, g_room.hide_hi = hi;
  g_room.tints = (const uint8_t *)hdr_ram + sizeof(RoomHdr);
  g_room.secs = (const SectorRec *)((const uint8_t *)hdr_ram + ((sizeof(RoomHdr) + 4u * h->ntint + 3) & ~3u));
  g_room.nnear = 0;
  return true;
}

void room_near(float cx, float cy) {
  const RoomHdr *h = g_room.h;
  float p = h->axis ? cy : cx;
  int n = 0;
  for (int i = 0; i < h->nsec && n < MAX_LOADED; i++)
    if (g_room.secs[i].start <= p + 2 && g_room.secs[i].end >= p - 2) g_room.near[n++] = (uint8_t)i;
  g_room.nnear = n;
}

/* ---------------------------------------------------------------- reading the sectors (tools/pack.py: sector_stream) */
typedef struct {
  const uint8_t *p;
  uint16_t left, rank;
  uint32_t group;   /* its sorting layer and order (room.h: SORT_KEY) */
  Inst in;          /* the one read last */
} Stream;
static Stream streams[MAX_LOADED];
static int nstreams;

static inline uint32_t uvar(const uint8_t **pp) {
  const uint8_t *p = *pp;
  uint32_t v = 0;
  int s = 0;
  uint8_t c;
  do {
    c = *p++;
    v |= (uint32_t)(c & 127) << s;
    s += 7;
  } while (c & 128);
  *pp = p;
  return v;
}
static inline int32_t svar(const uint8_t **pp) {
  uint32_t u = uvar(pp);
  return (int32_t)(u >> 1) ^ -(int32_t)(u & 1);
}

/* the stream's next instance (into s->in), false at its end */
static bool advance(Stream *s) {
  if (!s->left) return false;
  s->left--;
  const uint8_t *p = s->p;
  Inst *in = &s->in;
  uint8_t flags = *p++, aux = *p++;
  in->flags = flags;
  s->rank = (uint16_t)(s->rank + ((aux & 64) ? 1 : uvar(&p) + 2));
  if (aux & 128) {
    uint8_t layer = *p++;
    s->group = SORT_KEY(layer, svar(&p));
  }
  if (!(aux & 2)) in->tex = (uint16_t)(in->tex + svar(&p));
  in->tint = (aux & 1) ? *p++ : 0;
  in->ax = (int16_t)(in->ax + svar(&p));
  in->ay = (int16_t)(in->ay + svar(&p));
  if (!(aux & 4)) in->z = (int16_t)(in->z + svar(&p));
  if (!(aux & 32)) in->a = rd16(p), p += 2;
  switch (aux >> 3 & 3) {
    case 0: in->b = rd16(p), p += 2; break;
    case 1: in->b = in->a; break;
    case 2: in->b = in->a ^ 0x8000; break;
    default: break;
  }
  if (flags & F_ROT) in->rot = (int16_t)rd16(p), p += 2;
  else in->rot = 0;
  in->group = (flags & F_DYN) ? (*p++ & (MAX_GROUPS - 1)) : 0;
  s->p = p;
  return true;
}

void room_first(void) {
  const uint8_t *blob = section(SEC_RBLOB);
  nstreams = 0;
  for (int j = 0; j < g_room.nnear; j++) {
    const SectorRec *r = &g_room.secs[g_room.near[j]];
    Stream *s = &streams[nstreams];
    memset(s, 0, sizeof *s);
    s->p = blob + r->off, s->left = r->count, s->rank = 0xFFFF;
    if (advance(s)) nstreams++;
  }
}

bool room_next(Inst *out, uint32_t *group) {
  if (!nstreams) return false;
  int best = 0;
  for (int j = 1; j < nstreams; j++)
    if (streams[j].rank < streams[best].rank) best = j;
  Stream *s = &streams[best];
  *out = s->in, *group = s->group;
  if (!advance(s)) streams[best] = streams[--nstreams];
  return true;
}
