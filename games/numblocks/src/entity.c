/* Entities: dropped items (EntityItem) here, mobs and arrows in mob.c.
 *
 * An item falls (0.04 a tick, drag 0.98), slides (0.6 x 0.98 on the ground),
 * bounces a little, joins the same items near it, can be picked up after 10
 * ticks (40 when the player threw it) and disappears after 5 minutes. */
#include <math.h>
#include "nb.h"

Entity ents[N_ENT];

/* ---------------------------------------------------------------- particles (EntityDiggingFX, EntityRainFX) */
Particle parts[N_PART];

/* breaking a block: its bits, coloured like its texture, fly out and fall (EffectRenderer.addBlockDestroyEffects) */
void particles_break(int x, int y, int z, int b) {
  int tex = blk_tex[b][2];
  for (int k = 0; k < 12; k++) {
    Particle *p = &parts[rnd(N_PART)];
    int u = rnd(16), v = rnd(16);
    uint8_t q = tex_px[tex][(v * 16 + u) >> 1];
    int i = (u & 1) ? q >> 4 : q & 15;
    if (!i && (tex_flags[tex] & 0x20)) continue;
    p->c = tex_pal[tex][i];
    if (i >= (tex_flags[tex] & 0x1F)) p->c = 0x5CA9;   /* (tinted: a grass green) */
    p->x = x + 0.2f + rndf() * 0.6f, p->y = y + 0.2f + rndf() * 0.6f, p->z = z + 0.2f + rndf() * 0.6f;
    p->vx = (p->x - x - 0.5f) * 0.3f + (rndf() - 0.5f) * 0.1f;
    p->vy = (p->y - y - 0.5f) * 0.3f + 0.1f + rndf() * 0.1f;
    p->vz = (p->z - z - 0.5f) * 0.3f + (rndf() - 0.5f) * 0.1f;
    p->age = 0;
    p->life = (uint8_t)(4 / (rndf() * 0.9f + 0.1f));
  }
}

/* EntityRenderer.addRainParticles: drops splash on whatever the rain falls on
 * within 10 blocks, up to 100 a tick in a full downpour (half with Fast
 * graphics); here only as many as there are free particles */
static void rain_splashes(void) {
  float f = opt.fancy ? rain_str : rain_str / 2;
  int n = (int)(100 * f * f), px = (int)floorf(pl.x), py = (int)floorf(pl.y), pz = (int)floorf(pl.z);
  for (int i = 0; i < N_PART && n > 0; i++) {
    Particle *p = &parts[i];
    if (p->age < (p->life & ~RAIN_DROP)) continue;
    /* a free one: look for where it lands (a few tries, as most of the 100 would) */
    for (int k = 0; k < 4 && n > 0; k++, n--) {
      int x = px + rnd(10) - rnd(10), z = pz + rnd(10) - rnd(10), y = world_rain_top(x, z);
      if (y > py + 10 || y < py - 10 || !world_loaded(x, y - 1, z)) continue;
      int lx = x - vc_x0, lz = z - vc_z0, below = world_get(x, y - 1, z);
      if (biome_rain[vbiome[lz * VCX + lx]] != 1 || temp_at(x, y, z) < 0.15f || below == B_AIR || is_lava(below)) continue;
      float b[6], top = (float)y;
      if (block_box(x, y - 1, z, b)) top = b[4];
      p->x = x + rndf(), p->y = top + 0.1f, p->z = z + rndf();
      p->vx = (rndf() - 0.5f) * 0.03f, p->vz = (rndf() - 0.5f) * 0.03f, p->vy = rndf() * 0.2f + 0.1f;
      p->c = 0x1A59;   /* (24, 72, 204), the splash sprites' blue */
      p->age = 0;
      p->life = (uint8_t)(RAIN_DROP | (int)(8 / (rndf() * 0.8f + 0.2f)));
      break;
    }
  }
}

static void particles_tick(void) {
  if (rain_str > 0) rain_splashes();
  for (int i = 0; i < N_PART; i++) {
    Particle *p = &parts[i];
    bool drop = p->life & RAIN_DROP;
    if (p->age >= (p->life & ~RAIN_DROP)) continue;
    p->age++;
    p->vy -= drop ? 0.06f : 0.04f;
    float np[3] = {p->x, p->y, p->z}, v[3] = {p->vx, p->vy, p->vz};
    bool ground = phys_move(np, v, 0.1f, 0.1f);
    p->x = np[0], p->y = np[1], p->z = np[2];
    p->vx = v[0] * 0.98f, p->vy = v[1] * 0.98f, p->vz = v[2] * 0.98f;
    if (ground) {
      p->vx *= 0.7f, p->vz *= 0.7f;
      if (drop && rnd(2)) p->age = p->life & ~RAIN_DROP;   /* half the drops are gone when they land */
    }
  }
}

Entity *ent_new(int type, float x, float y, float z) {
  Entity *e = NULL;
  for (int i = 0; i < N_ENT; i++)
    if (ents[i].type == E_NONE) {
      e = &ents[i];
      break;
    }
  if (!e) {
    /* full: the oldest item makes room */
    int best = -1;
    for (int i = 0; i < N_ENT; i++)
      if (ents[i].type == E_ITEM && (best < 0 || ents[i].age > ents[best].age)) best = i;
    if (best < 0) return NULL;
    e = &ents[best];
  }
  memset(e, 0, sizeof *e);
  e->type = (uint8_t)type;
  e->x = e->px = x;
  e->y = e->py = y;
  e->z = e->pz = z;
  return e;
}

/* Block.spawnAsEntity / EntityPlayer.dropItem */
void ent_drop(int id, int count, int dmg, float x, float y, float z, bool thrown) {
  if (!id || count <= 0) return;
  Entity *e = ent_new(E_ITEM, x, y, z);
  if (!e) return;
  e->item.id = (uint16_t)id;
  e->item.aux = (uint16_t)(item_dur(id) ? dmg : count);
  if (thrown) {
    /* EntityPlayer.dropItem: 0.3 forwards, a little up, a little at random */
    float yaw = pl.yaw * 0.017453292f, pitch = pl.pitch * 0.017453292f;
    e->vx = -sinf(yaw) * cosf(pitch) * 0.3f;
    e->vz = cosf(yaw) * cosf(pitch) * 0.3f;
    e->vy = -sinf(pitch) * 0.3f + 0.1f;
    float a = rndf() * 6.2831853f, f = 0.02f * rndf();
    e->vx += cosf(a) * f;
    e->vy += (rndf() - rndf()) * 0.1f;
    e->vz += sinf(a) * f;
    e->delay = 40;
  } else {
    e->vx = rndf() * 0.2f - 0.1f;
    e->vy = 0.2f;
    e->vz = rndf() * 0.2f - 0.1f;
    e->delay = 10;
  }
  e->yaw = rndf() * 360;
}

static void item_tick(Entity *e) {
  float p[3] = {e->x, e->y, e->z}, v[3] = {e->vx, e->vy - 0.04f, e->vz};
  /* stuck in a block: pushed up out of it */
  float b[6];
  if (block_box((int)floorf(e->x), (int)floorf(e->y + 0.125f), (int)floorf(e->z), b) && e->y + 0.125f < b[4])
    v[1] = 0.1f;
  bool ground = phys_move(p, v, 0.25f, 0.25f);
  e->x = p[0], e->y = p[1], e->z = p[2];
  float f = ground ? 0.6f * 0.98f : 0.98f;
  e->vx = v[0] * f;
  e->vy = v[1] * 0.98f;
  e->vz = v[2] * f;
  if (ground) e->vy *= -0.5f;
  e->on_ground = ground;
  if (e->delay > 0) e->delay--;
  if (e->age >= 6000) {
    e->type = E_NONE;
    return;
  }
  /* EntityItem.searchForOtherItemsNearby: the same items within half a block join */
  if ((e->age & 7) == 0 && !item_dur(e->item.id)) {
    for (int i = 0; i < N_ENT; i++) {
      Entity *o = &ents[i];
      if (o == e || o->type != E_ITEM || o->item.id != e->item.id) continue;
      if (fabsf(o->x - e->x) > 0.75f || fabsf(o->y - e->y) > 0.5f || fabsf(o->z - e->z) > 0.75f) continue;
      int room = item_max(e->item.id) - e->item.aux;
      if (room <= 0) break;
      int k = o->item.aux < room ? o->item.aux : room;
      e->item.aux = (uint16_t)(e->item.aux + k);
      o->item.aux = (uint16_t)(o->item.aux - k);
      if (!o->item.aux) o->type = E_NONE;
      if (o->delay > e->delay) e->delay = o->delay;
    }
  }
  /* picked up: the player's box grown by 1, 0.5, 1 touches it */
  if (!e->delay && !pl.dead && fabsf(e->x - pl.x) < 0.3f + 0.125f + 1 && e->y + 0.25f > pl.y - 0.5f &&
      e->y < pl.y + 1.8f + 0.5f && fabsf(e->z - pl.z) < 0.3f + 0.125f + 1) {
    int left = inv_add(pl.inv, 36, e->item.id, item_count(&e->item), item_dur(e->item.id) ? e->item.aux : 0);
    if (!left) e->type = E_NONE;
    else if (!item_dur(e->item.id)) e->item.aux = (uint16_t)left;
  }
}

void ents_tick(void) {
  particles_tick();
  for (int i = 0; i < N_ENT; i++) {
    Entity *e = &ents[i];
    if (e->type == E_NONE) continue;
    e->px = e->x, e->py = e->y, e->pz = e->z;
    e->age++;
    /* far outside the loaded blocks: gone */
    if (!world_loaded((int)floorf(e->x), vc_y0, (int)floorf(e->z))) {
      e->type = E_NONE;
      continue;
    }
    if (e->type == E_ITEM) item_tick(e);
    else mob_tick(e);
    if (e->y < -64) e->type = E_NONE;
  }
  static int spawn_wait;
  if (++spawn_wait >= 20) {
    spawn_wait = 0;
    mobs_spawn();
  }
}
