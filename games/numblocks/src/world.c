/* The world around the player: the block cache (see nb.h), filled from the
 * generator a chunk at a time with the player's edits on top, and its light.
 *
 * The cache moves in steps of 8 blocks (4 vertically) once the player comes
 * near an edge: what it still covers is moved, the rest is generated. */
#include "nb.h"
#include "edits.h"

uint8_t vc[VCY * VCZ * VCX];
uint8_t vlight[VCY * VCZ * VCX / 2];
uint8_t vbiome[VCZ * VCX];
uint8_t vtop[VCZ * VCX];
uint8_t vmac[MCY * MCZ * MCX];      /* per column: 1 + the highest block that stops sky light, 0 if none */
int vc_x0, vc_y0, vc_z0;
static bool vc_valid;

/* one chunk of the generator's output; also the edit log's merge buffer */
static uint32_t slab32[16 * 16 * VCY / 4];
#define slab ((uint8_t *)slab32)

static inline int floordiv(int a, int b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }
static inline int ifloor(float v) { int i = (int)v; return v < (float)i ? i - 1 : i; }

/* light stops at opaque blocks; leaves and water let some through (1.8: opacity 1 and 3) */
static inline int opacity(int b) {
  if (blk_flags[b] & BF_OPAQUE) return 15;
  int m = blk_model[b];
  if (m == M_LEAVES || b == B_COBWEB) return 1;
  if (m == M_LIQUID) return 3;
  return 0;
}

/* ---------------------------------------------------------------- light */
static inline void light_put(int i, int v) {
  uint8_t *p = &vlight[i >> 1];
  *p = (i & 1) ? (uint8_t)((*p & 0x0F) | v << 4) : (uint8_t)((*p & 0xF0) | v);
}

/* Sky light in the box [x0,x1) x [y0,y1) x [z0,z1) of the cache: 15 straight
 * under the open sky, then spread sideways and down, one level less a block
 * (more through leaves and water), as Minecraft's light does. */
static void light_box(int x0, int y0, int z0, int x1, int y1, int z1) {
  if (x0 < 0) x0 = 0;
  if (z0 < 0) z0 = 0;
  if (y0 < 0) y0 = 0;
  if (x1 > VCX) x1 = VCX;
  if (z1 > VCZ) z1 = VCZ;
  if (y1 > VCY) y1 = VCY;
  for (int z = z0; z < z1; z++)
    for (int x = x0; x < x1; x++) {
      int top = vtop[z * VCX + x] - vc_y0;   /* local y of the first sky-lit cell */
      int lvl = 15;
      for (int y = VCY - 1; y >= y0; y--) {
        int i = VC_I(x, y, z), b = vc[i];
        if (y < top) lvl = 0;
        else if (b != B_AIR) {
          lvl -= opacity(b);
          if (lvl < 0) lvl = 0;
        }
        if (y < y1) light_put(i, lvl);
      }
    }
  /* spread: sweeps in each direction, twice */
  for (int round = 0; round < 2; round++) {
    for (int y = y1 - 1; y >= y0; y--)
      for (int z = z0; z < z1; z++)
        for (int x = x0; x < x1; x++) {
          int i = VC_I(x, y, z), b = vc[i], op = opacity(b);
          if (op >= 15) continue;
          int best = light_at(i), dec = op ? op : 1;
          int n[6] = {x > 0 ? i - 1 : -1, x < VCX - 1 ? i + 1 : -1, z > 0 ? i - VCX : -1, z < VCZ - 1 ? i + VCX : -1,
                      y < VCY - 1 ? i + VCX * VCZ : -1, y > 0 ? i - VCX * VCZ : -1};
          for (int k = 0; k < 6; k++)
            if (n[k] >= 0) {
              int v = light_at(n[k]) - dec;
              if (v > best) best = v;
            }
          if (best != light_at(i)) light_put(i, best);
        }
    for (int y = y0; y < y1; y++)
      for (int z = z1 - 1; z >= z0; z--)
        for (int x = x1 - 1; x >= x0; x--) {
          int i = VC_I(x, y, z), b = vc[i], op = opacity(b);
          if (op >= 15) continue;
          int best = light_at(i), dec = op ? op : 1;
          int n[6] = {x > 0 ? i - 1 : -1, x < VCX - 1 ? i + 1 : -1, z > 0 ? i - VCX : -1, z < VCZ - 1 ? i + VCX : -1,
                      y < VCY - 1 ? i + VCX * VCZ : -1, y > 0 ? i - VCX * VCZ : -1};
          for (int k = 0; k < 6; k++)
            if (n[k] >= 0) {
              int v = light_at(n[k]) - dec;
              if (v > best) best = v;
            }
          if (best != light_at(i)) light_put(i, best);
        }
  }
}

/* ---------------------------------------------------------------- the empty regions */
static void mac_box(int x0, int y0, int z0, int x1, int y1, int z1) {
  for (int my = y0 >> 2; my <= (y1 - 1) >> 2 && my < MCY; my++)
    for (int mz = z0 >> 2; mz <= (z1 - 1) >> 2 && mz < MCZ; mz++)
      for (int mx = x0 >> 2; mx <= (x1 - 1) >> 2 && mx < MCX; mx++) {
        uint8_t any = 0;
        for (int y = my * 4; y < my * 4 + 4 && !any; y++)
          for (int z = mz * 4; z < mz * 4 + 4 && !any; z++) {
            const uint8_t *r = &vc[VC_I(mx * 4, y, z)];
            any = (uint8_t)(r[0] | r[1] | r[2] | r[3]);
          }
        vmac[(my * MCZ + mz) * MCX + mx] = any != 0;
      }
}

/* ---------------------------------------------------------------- filling */
/* the chunk (cx, cz) for the cache cells not already filled: x, z in the cache
 * box [nx0, nx1) x [nz0, nz1) (world), every y of the cache */
static void fill_chunk(int cx, int cz, const bool *keep, int ox, int oy, int oz) {
  gen_slab(cx, cz, vc_y0, VCY, slab);
  edits_apply(cx, cz, vc_y0, VCY, slab);
  for (int z = 0; z < 16; z++) {
    int lz = cz * 16 + z - vc_z0;
    if (lz < 0 || lz >= VCZ) continue;
    for (int x = 0; x < 16; x++) {
      int lx = cx * 16 + x - vc_x0;
      if (lx < 0 || lx >= VCX) continue;
      int col = lz * VCX + lx;
      bool old_col = keep && keep[col];
      for (int y = 0; y < VCY; y++) {
        /* cells the old cache had are kept (their content is the same) */
        if (old_col && y + vc_y0 - oy >= 0 && y + vc_y0 - oy < VCY) continue;
        vc[VC_I(lx, y, lz)] = slab[(y * 16 + z) * 16 + x];
      }
      if (!old_col) {
        vbiome[col] = (uint8_t)gen_biome(cx * 16 + x, cz * 16 + z);
        int t = edits_top(cx * 16 + x, cz * 16 + z, gen_top(cx * 16 + x, cz * 16 + z) + 1);
        vtop[col] = (uint8_t)(t < 0 ? 0 : t > 255 ? 255 : t);
      }
    }
  }
  (void)ox;
  (void)oz;
}

static bool kept[VCZ * VCX];   /* columns the old cache covered (moved, not generated) */

static void recenter(int nx0, int ny0, int nz0) {
  edits_forget();
  int dx = nx0 - vc_x0, dy = ny0 - vc_y0, dz = nz0 - vc_z0;
  bool any = vc_valid && dx > -VCX && dx < VCX && dz > -VCZ && dz < VCZ && dy > -VCY && dy < VCY;
  /* move what stays: in an order that never overwrites a cell before it is read */
  if (any) {
    int ys = dy >= 0 ? 0 : VCY - 1, ye = dy >= 0 ? VCY : -1, yi = dy >= 0 ? 1 : -1;
    int zs = dz >= 0 ? 0 : VCZ - 1, ze = dz >= 0 ? VCZ : -1, zi = dz >= 0 ? 1 : -1;
    int xs = dx >= 0 ? 0 : VCX - 1, xe = dx >= 0 ? VCX : -1, xi = dx >= 0 ? 1 : -1;
    for (int y = ys; y != ye; y += yi)
      for (int z = zs; z != ze; z += zi)
        for (int x = xs; x != xe; x += xi) {
          int sx = x + dx, sy = y + dy, sz = z + dz;
          if (sx < 0 || sx >= VCX || sy < 0 || sy >= VCY || sz < 0 || sz >= VCZ) continue;
          vc[VC_I(x, y, z)] = vc[VC_I(sx, sy, sz)];
        }
    for (int z = zs; z != ze; z += zi)
      for (int x = xs; x != xe; x += xi) {
        int sx = x + dx, sz = z + dz;
        bool in = sx >= 0 && sx < VCX && sz >= 0 && sz < VCZ;
        kept[z * VCX + x] = in;
        if (in) {
          vbiome[z * VCX + x] = vbiome[sz * VCX + sx];
          vtop[z * VCX + x] = vtop[sz * VCX + sx];
        }
      }
  } else memset(kept, 0, sizeof kept);
  int oy = vc_y0;
  vc_x0 = nx0;
  vc_y0 = ny0;
  vc_z0 = nz0;
  for (int cz = floordiv(nz0, 16); cz <= floordiv(nz0 + VCZ - 1, 16); cz++)
    for (int cx = floordiv(nx0, 16); cx <= floordiv(nx0 + VCX - 1, 16); cx++) {
      /* skip chunks the old cache covered entirely */
      bool need = !any || dy != 0;
      for (int z = 0; z < 16 && !need; z++)
        for (int x = 0; x < 16 && !need; x++) {
          int lx = cx * 16 + x - vc_x0, lz = cz * 16 + z - vc_z0;
          if (lx >= 0 && lx < VCX && lz >= 0 && lz < VCZ && !kept[lz * VCX + lx]) need = true;
        }
      if (need) fill_chunk(cx, cz, any ? kept : NULL, 0, any ? oy : vc_y0 + 100000, 0);
    }
  vc_valid = true;
  light_box(0, 0, 0, VCX, VCY, VCZ);
  mac_box(0, 0, 0, VCX, VCY, VCZ);
}

void world_new(int64_t seed, const char *name) {
  gen_init(seed);
  edits_setup(name, slab32, sizeof slab32 / 4);
  edits_clear();
  vc_valid = false;
}

void world_follow(float x, float y, float z) {
  int px = ifloor(x), py = ifloor(y), pz = ifloor(z);
  int lx = px - vc_x0, ly = py - vc_y0, lz = pz - vc_z0;
  if (vc_valid && lx >= 12 && lx < VCX - 12 && lz >= 12 && lz < VCZ - 12 && ly >= 10 && ly < VCY - 10) return;
  int nx0 = (px - VCX / 2) & ~7, nz0 = (pz - VCZ / 2) & ~7;
  int ny0 = (py - VCY / 2 + 2) & ~3;
  if (ny0 < 0) ny0 = 0;
  if (ny0 > WORLD_H - VCY) ny0 = WORLD_H - VCY;
  if (vc_valid && nx0 == vc_x0 && ny0 == vc_y0 && nz0 == vc_z0) return;
  recenter(nx0, ny0, nz0);
}

bool world_loaded(int x, int y, int z) {
  return (unsigned)(x - vc_x0) < VCX && (unsigned)(y - vc_y0) < VCY && (unsigned)(z - vc_z0) < VCZ;
}

int world_get(int x, int y, int z) {
  if (y < 0) return B_BEDROCK;
  if (!world_loaded(x, y, z)) return B_AIR;
  return vc[VC_I(x - vc_x0, y - vc_y0, z - vc_z0)];
}

void world_set(int x, int y, int z, int b) {
  if (!world_loaded(x, y, z) || y < 0 || y >= WORLD_H) return;
  int lx = x - vc_x0, ly = y - vc_y0, lz = z - vc_z0;
  vc[VC_I(lx, ly, lz)] = (uint8_t)b;
  edits_put(x, y, z, b);
  /* the column's sky top, then the light around */
  int col = lz * VCX + lx, t = vtop[col];
  if (opacity(b) >= 15 && y + 1 > t) vtop[col] = (uint8_t)(y + 1);
  else if (y + 1 == t && opacity(b) < 15) {
    int ny = y;
    while (ny > vc_y0 && opacity(vc[VC_I(lx, ny - vc_y0 - 1, lz)]) < 15) ny--;
    vtop[col] = (uint8_t)(ny > vc_y0 ? ny : 0);
  }
  light_box(lx - 15, ly - 15, lz - 15, lx + 16, VCY, lz + 16);
  mac_box(lx, ly, lz, lx + 1, ly + 1, lz + 1);
}
