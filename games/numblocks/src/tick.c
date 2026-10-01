/* The world's own changes: liquids flowing (BlockDynamicLiquid) and random
 * block ticks (WorldServer.updateBlocks: 3 random blocks a 16 x 16 x 16
 * section each tick): crops and saplings grow, grass spreads and dies,
 * leaves far from a log decay, sugar cane and cactus grow. */
#include <stdlib.h>
#include "nb.h"

static const int8_t NX[6] = {0, 0, 0, 0, -1, 1}, NY[6] = {-1, 1, 0, 0, 0, 0}, NZ[6] = {0, 0, -1, 1, 0, 0};
extern uint32_t game_time;

/* ---------------------------------------------------------------- liquids */
typedef struct {
  int16_t x, z;
  uint8_t y, pad;
  uint16_t due;   /* the tick it runs (low 16 bits of game_time) */
} Pending;
#define N_PENDING 160
static Pending pend[N_PENDING];
static int npend;

void fluid_schedule(int x, int y, int z) {
  int b = world_get(x, y, z);
  if (blk_model[b] != M_LIQUID) return;
  for (int i = 0; i < npend; i++)
    if (pend[i].x == x && pend[i].y == y && pend[i].z == z) return;
  if (npend >= N_PENDING) return;
  /* water moves every 5 ticks, lava every 30 */
  pend[npend++] = (Pending){(int16_t)x, (int16_t)z, (uint8_t)y, 0, (uint16_t)(game_time + (is_lava(b) ? 30 : 5))};
}

static int level_of(int b) { return blk_meta[b]; }   /* 0 source, 1-7 flowing, 8 falling */
static int liquid(int water, int level) {
  if (water) {
    if (level == 0) return B_WATER;
    if (level >= 8) return B_FALLING_WATER;
    return level == 1 ? B_FLOWING_WATER : B_FLOWING_WATER_2 + level - 2;
  }
  if (level == 0) return B_LAVA;
  if (level >= 8) return B_FALLING_LAVA;
  return level <= 2 ? B_FLOWING_LAVA : level <= 4 ? B_FLOWING_LAVA_4 : B_FLOWING_LAVA_6;
}
static bool same(int water, int b) { return water ? is_water(b) : is_lava(b); }

/* can the liquid flow into this block (BlockDynamicLiquid.canFlowInto)? */
static bool open_to(int water, int b) {
  if (same(water, b)) return false;
  if (water && is_lava(b)) return false;   /* water never flows into lava: the lava turns to stone */
  if (!water && is_water(b)) return true;
  if (b == B_AIR) return true;
  if (blk_flags[b] & BF_SOLID) return false;
  /* plants, torches, snow... are washed away (doors, ladders and signs stop it) */
  int m = blk_model[b];
  return m == M_CROSS || m == M_TORCH || m == M_FLAT || m == M_LAYER || m == M_VINE;
}

static void set(int x, int y, int z, int b) {
  world_set(x, y, z, b);
  neighbours_changed(x, y, z);
}

/* lava touching water (BlockLiquid.checkForMixing): obsidian from a source, cobblestone from flowing */
static bool mix(int x, int y, int z, int b) {
  if (!is_lava(b)) return false;
  for (int f = 1; f < 6; f++)
    if (is_water(world_get(x + NX[f], y + NY[f], z + NZ[f]))) {
      int lv = level_of(b);
      if (lv == 0) set(x, y, z, B_OBSIDIAN);
      else if (lv <= 4) set(x, y, z, B_COBBLESTONE);
      else continue;
      return true;
    }
  return false;
}

static void flow_into(int water, int x, int y, int z, int level) {
  int b = world_get(x, y, z);
  if (!open_to(water, b)) return;
  if (!water && is_water(b)) {
    set(x, y, z, B_STONE);   /* lava flowing onto water */
    return;
  }
  if (b != B_AIR && !(water ? is_lava(b) : is_water(b))) break_block_at(x, y, z, true);
  set(x, y, z, liquid(water, level));
}

/* how far (up to 4 for water, 2 for lava) a way down is going this way, 1000 if none */
static int slope_dist(int water, int x, int y, int z, int dist, int from, int max) {
  int best = 1000;
  for (int f = 2; f < 6; f++) {
    if (f == (from ^ 1)) continue;
    int nx = x + NX[f], nz = z + NZ[f];
    int b = world_get(nx, y, nz);
    if ((blk_flags[b] & BF_SOLID) || (same(water, b) && level_of(b) == 0)) continue;
    if (open_to(water, world_get(nx, y - 1, nz)) || same(water, world_get(nx, y - 1, nz))) return dist;
    if (dist < max) {
      int d = slope_dist(water, nx, y, nz, dist + 1, f, max);
      if (d < best) best = d;
    }
  }
  return best;
}

static void fluid_update(int x, int y, int z) {
  int b = world_get(x, y, z);
  if (blk_model[b] != M_LIQUID) return;
  int water = is_water(b);
  if (mix(x, y, z, b)) return;
  int level = level_of(b), decay = water ? 1 : 2;
  if (level > 0) {
    /* the level it should have: from the lowest neighbour, or falling from above */
    int lowest = -1, sources = 0;
    for (int f = 2; f < 6; f++) {
      int n = world_get(x + NX[f], y, z + NZ[f]);
      if (!same(water, n)) continue;
      int l = level_of(n);
      if (l == 0) sources++;
      if (l >= 8) l = 0;
      if (lowest < 0 || l < lowest) lowest = l;
    }
    int nl = lowest < 0 ? -1 : lowest + decay;
    if (nl >= 8) nl = -1;
    int above = world_get(x, y + 1, z);
    if (same(water, above)) nl = 8;   /* falling */
    /* two sources beside it and something under: a new source (infinite water) */
    if (water && sources >= 2) {
      int under = world_get(x, y - 1, z);
      if ((blk_flags[under] & BF_SOLID) || (is_water(under) && level_of(under) == 0)) nl = 0;
    }
    if (nl != level) {
      if (nl < 0) {
        set(x, y, z, B_AIR);
        return;
      }
      level = nl;
      set(x, y, z, liquid(water, level));
    }
  }
  /* down first, then sideways towards the nearest way down */
  int under = world_get(x, y - 1, z);
  if (y > 0 && open_to(water, under)) {
    if (!water && is_water(under)) set(x, y - 1, z, B_STONE);
    else flow_into(water, x, y - 1, z, 8);
    return;
  }
  if (level == 0 || (!same(water, under) && !open_to(water, under))) {
    int next = level >= 8 ? decay : level + decay;
    if (next >= 8) return;
    int dist[4], best = 1000, max = water ? 4 : 2;
    for (int f = 2; f < 6; f++) {
      int nx = x + NX[f], nz = z + NZ[f];
      int n = world_get(nx, y, nz);
      dist[f - 2] = 1000;
      if ((blk_flags[n] & BF_SOLID) || (same(water, n) && level_of(n) == 0)) continue;
      dist[f - 2] = open_to(water, world_get(nx, y - 1, nz)) || same(water, world_get(nx, y - 1, nz))
                        ? 0
                        : slope_dist(water, nx, y, nz, 1, f, max);
      if (dist[f - 2] < best) best = dist[f - 2];
    }
    for (int f = 2; f < 6; f++)
      if (dist[f - 2] == best || best == 1000) flow_into(water, x + NX[f], y, z + NZ[f], next);
  }
}

static void fluids_tick(void) {
  uint16_t now = (uint16_t)game_time;
  int done = 0;
  for (int i = 0; i < npend && done < 24;) {
    if ((int16_t)(now - pend[i].due) < 0) {
      i++;
      continue;
    }
    Pending p = pend[i];
    pend[i] = pend[--npend];
    if (world_loaded(p.x, p.y, p.z)) fluid_update(p.x, p.y, p.z);
    done++;
  }
}

/* ---------------------------------------------------------------- growing */
static bool soil(int b) { return b == B_GRASS || b == B_DIRT || b == B_PODZOL || b == B_COARSE_DIRT; }

/* WorldGenTrees: a small tree of this wood (oak, birch, spruce and the others alike), if there is room */
static void grow_tree(int x, int y, int z, int sapling) {
  int v = sapling - B_SAPLING_OAK;
  static const uint16_t logs[6] = {B_LOG_OAK, B_LOG_SPRUCE, B_LOG_BIRCH, B_LOG_JUNGLE, B_LOG_ACACIA, B_LOG_DARK_OAK};
  static const uint16_t leaves[6] = {B_LEAVES_OAK, B_LEAVES_SPRUCE, B_LEAVES_BIRCH, B_LEAVES_JUNGLE, B_LEAVES_ACACIA,
                                     B_LEAVES_DARK_OAK};
  int h = 4 + rnd(3) + (v == 2 ? 1 : 0) + (v == 1 ? 2 : 0);
  for (int dy = 1; dy <= h + 1; dy++)
    for (int dz = -1; dz <= 1; dz++)
      for (int dx = -1; dx <= 1; dx++) {
        int b = world_get(x + dx, y + dy, z + dz);
        if (b != B_AIR && blk_model[b] != M_LEAVES && blk_model[b] != M_CROSS) return;
      }
  if (y + h + 2 >= WORLD_H) return;
  if (v == 1) {
    /* spruce (WorldGenTaiga2): rings of leaves narrowing to the top */
    int r = 0;
    for (int dy = h; dy >= 2; dy--) {
      for (int dz = -r; dz <= r; dz++)
        for (int dx = -r; dx <= r; dx++)
          if ((abs(dx) != r || abs(dz) != r || r == 0) && world_get(x + dx, y + dy, z + dz) == B_AIR)
            world_set(x + dx, y + dy, z + dz, leaves[v]);
      r = r >= 2 ? 1 : r + 1;
    }
    world_set(x, y + h + 1, z, leaves[v]);
  } else {
    /* oak and the others: four layers of leaves, the corners left out at random */
    for (int dy = h - 3; dy <= h; dy++) {
      int r = dy >= h - 1 ? 1 : 2;
      for (int dz = -r; dz <= r; dz++)
        for (int dx = -r; dx <= r; dx++) {
          if (abs(dx) == r && abs(dz) == r && (dy == h || rnd(2) == 0)) continue;
          if (world_get(x + dx, y + dy, z + dz) == B_AIR) world_set(x + dx, y + dy, z + dz, leaves[v]);
        }
    }
  }
  for (int dy = 0; dy < h; dy++) world_set(x, y + dy, z, logs[v]);
  world_set(x, y - 1, z, B_DIRT);
}

static void random_tick(int x, int y, int z) {
  int b = world_get(x, y, z);
  int i = VC_I(x - vc_x0, y - vc_y0, z - vc_z0);
  int above = world_get(x, y + 1, z);
  int light = i + VCX * VCZ < VCY * VCZ * VCX && y + 1 - vc_y0 < VCY ? light_at(i + VCX * VCZ) : 15;
  int blight = y + 1 - vc_y0 < VCY ? block_light_at(i + VCX * VCZ) : 0;
  int lit = light > blight ? light : blight;
  if (b == B_GRASS) {
    /* BlockGrass: dies under something dark, else spreads to dirt nearby */
    if (lit < 4 && (blk_flags[above] & BF_OPAQUE)) world_set(x, y, z, B_DIRT);
    else if (lit >= 9)
      for (int k = 0; k < 4; k++) {
        int gx = x + rnd(3) - 1, gy = y + rnd(5) - 3, gz = z + rnd(3) - 1;
        if (world_get(gx, gy, gz) == B_DIRT && !(blk_flags[world_get(gx, gy + 1, gz)] & BF_OPAQUE)) world_set(gx, gy, gz, B_GRASS);
      }
    return;
  }
  if (b >= B_WHEAT_0 && b < B_WHEAT_7) {
    /* BlockCrops: in the light, now and then, faster on wet farmland */
    if (lit >= 9 && rnd(world_get(x, y - 1, z) == B_FARMLAND_WET ? 7 : 13) == 0) world_set(x, y, z, b + 1);
    return;
  }
  if ((b >= B_CARROTS_0 && b < B_CARROTS_3) || (b >= B_POTATOES_0 && b < B_POTATOES_3)) {
    /* four looks for eight ages: half as likely to show a change */
    if (lit >= 9 && rnd(world_get(x, y - 1, z) == B_FARMLAND_WET ? 14 : 26) == 0) world_set(x, y, z, b + 1);
    return;
  }
  if (b == B_FARMLAND || b == B_FARMLAND_WET) {
    /* wet with water within 4 blocks */
    bool wet = false;
    for (int dz = -4; dz <= 4 && !wet; dz++)
      for (int dx = -4; dx <= 4 && !wet; dx++)
        for (int dy = 0; dy <= 1 && !wet; dy++) wet = is_water(world_get(x + dx, y + dy, z + dz));
    if (wet != (b == B_FARMLAND_WET)) world_set(x, y, z, wet ? B_FARMLAND_WET : B_FARMLAND);
    else if (!wet && !(above >= B_WHEAT_0 && above <= B_WHEAT_7) && rnd(4) == 0) world_set(x, y, z, B_DIRT);
    return;
  }
  if (b >= B_SAPLING_OAK && b <= B_SAPLING_DARK_OAK) {
    /* BlockSapling: 1 in 7 in the light, two stages */
    if (lit >= 9 && rnd(14) == 0 && soil(world_get(x, y - 1, z))) grow_tree(x, y, z, b);
    return;
  }
  if (b == B_SUGAR_CANE || b == B_CACTUS) {
    /* up to three high, one block every 16 ticks of its */
    if (above == B_AIR && rnd(16) == 0) {
      int h = 1;
      while (h < 3 && world_get(x, y - h, z) == b) h++;
      if (h < 3) {
        world_set(x, y + 1, z, b);
        neighbours_changed(x, y + 1, z);
      }
    }
    return;
  }
  if (blk_model[b] == M_LEAVES) {
    /* BlockLeaves: no log within 4 blocks: it decays */
    for (int dy = -4; dy <= 4; dy++)
      for (int dz = -4; dz <= 4; dz++)
        for (int dx = -4; dx <= 4; dx++) {
          int n = world_get(x + dx, y + dy, z + dz);
          if ((n >= B_LOG_OAK && n <= B_LOG_JUNGLE_BARK) || (n >= B_LOG_ACACIA && n <= B_LOG_DARK_OAK_BARK)) return;
          if (!world_loaded(x + dx, y + dy, z + dz)) return;
        }
    break_block_at(x, y, z, true);
    return;
  }
  if (b == B_SNOW_LAYER && blight > 11) world_set(x, y, z, B_AIR);
  if (b == B_ICE && blight > 11) world_set(x, y, z, B_WATER);
}

void world_tick(void) {
  fluids_tick();
  /* about 3 a section: the cache is VCX x VCY x VCZ blocks */
  int n = VCX * VCY * VCZ * 3 / 4096;
  for (int k = 0; k < n; k++) {
    int x = vc_x0 + rnd(VCX), y = vc_y0 + rnd(VCY), z = vc_z0 + rnd(VCZ);
    random_tick(x, y, z);
  }
}
