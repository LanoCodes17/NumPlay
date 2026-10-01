/* A stand-in generator (rolling hills, trees, ores) used until the port of
 * Minecraft 1.8.8's generator (gen.c) is in. Same interface as gen.c. */
#include <math.h>
#include <stdlib.h>
#include "nb.h"

static int64_t wseed;

static uint32_t hash3(int x, int y, int z) {
  uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u + (uint32_t)z * 2147483647u + (uint32_t)wseed;
  h = (h ^ (h >> 13)) * 1274126177u;
  return h ^ (h >> 16);
}
static float vnoise(float x, float z) {
  int ix = (int)floorf(x), iz = (int)floorf(z);
  float fx = x - ix, fz = z - iz;
  fx = fx * fx * (3 - 2 * fx);
  fz = fz * fz * (3 - 2 * fz);
  float a = (hash3(ix, 0, iz) & 1023) / 1023.0f, b = (hash3(ix + 1, 0, iz) & 1023) / 1023.0f;
  float c = (hash3(ix, 0, iz + 1) & 1023) / 1023.0f, d = (hash3(ix + 1, 0, iz + 1) & 1023) / 1023.0f;
  return a + (b - a) * fx + (c - a) * fz + (a - b - c + d) * fx * fz;
}
static int height(int x, int z) {
  float h = 64 + vnoise(x / 32.0f, z / 32.0f) * 16 + vnoise(x / 8.0f, z / 8.0f) * 4 - 6;
  return (int)h;
}

void gen_init(int64_t seed) { wseed = seed; }
int gen_biome(int x, int z) { return vnoise(x / 96.0f + 50, z / 96.0f) > 0.6f ? 4 : 1; }
int gen_top(int x, int z) {
  int h = height(x, z);
  return h < 62 ? 62 : h;
}
void gen_spawn(int *x, int *y, int *z) {
  *x = 8;
  *z = 8;
  *y = gen_top(8, 8) + 1;
}

static bool tree_at(int x, int z) { return (hash3(x, 7, z) % 61) == 0 && height(x, z) >= 63; }

void gen_slab(int cx, int cz, int y0, int h, uint8_t *out) {
  memset(out, B_AIR, (size_t)h * 256);
  for (int z = 0; z < 16; z++)
    for (int x = 0; x < 16; x++) {
      int wx = cx * 16 + x, wz = cz * 16 + z, top = height(wx, wz);
      for (int y = y0; y < y0 + h; y++) {
        int b = B_AIR;
        if (y == 0) b = B_BEDROCK;
        else if (y < top - 3) {
          uint32_t r = hash3(wx, y, wz);
          b = (r % 97) == 0 ? B_COAL_ORE : (r % 151) == 0 ? B_IRON_ORE : (r % 33) == 0 ? B_GRAVEL : B_STONE;
          if (y < 16 && (r % 401) == 0) b = B_DIAMOND_ORE;
        } else if (y < top) b = top < 64 ? B_SAND : B_DIRT;
        else if (y == top) b = top < 63 ? B_SAND : B_GRASS;
        else if (y < 63) b = B_WATER;
        else if (y == top + 1 && top >= 63 && (hash3(wx, y, wz) % 7) == 0) b = (hash3(wx, 1, wz) % 9) ? B_TALL_GRASS : B_POPPY;
        out[(y - y0) * 256 + z * 16 + x] = (uint8_t)b;
      }
    }
  /* trees, from this chunk and its neighbours */
  for (int tz = cz * 16 - 2; tz < cz * 16 + 18; tz++)
    for (int tx = cx * 16 - 2; tx < cx * 16 + 18; tx++) {
      if (!tree_at(tx, tz)) continue;
      int base = height(tx, tz) + 1;
      for (int dy = 0; dy < 7; dy++)
        for (int dz = -2; dz <= 2; dz++)
          for (int dx = -2; dx <= 2; dx++) {
            int wx = tx + dx, wz = tz + dz, y = base + dy, lx = wx - cx * 16, lz = wz - cz * 16;
            if (lx < 0 || lx > 15 || lz < 0 || lz > 15 || y < y0 || y >= y0 + h) continue;
            int b = B_AIR;
            if (dx == 0 && dz == 0 && dy < 5) b = B_LOG_OAK;
            else if (dy >= 3 && dy <= 4 && !(abs(dx) == 2 && abs(dz) == 2)) b = B_LEAVES_OAK;
            else if (dy >= 5 && abs(dx) <= 1 && abs(dz) <= 1 && !(dy == 6 && abs(dx) + abs(dz) == 2)) b = B_LEAVES_OAK;
            if (b != B_AIR) out[(y - y0) * 256 + lz * 16 + lx] = (uint8_t)b;
          }
    }
}
float gen_temp_noise(int x, int z) { return 0; }
