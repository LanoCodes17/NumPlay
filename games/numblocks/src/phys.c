/* Collisions with blocks, for the player and every entity (Entity.moveEntity:
 * the box is moved along y, then x, then z, as far as block boxes let it). */
#include "nb.h"

static inline int ifloor(float v) { int i = (int)v; return v < (float)i ? i - 1 : i; }

/* the collision box of the block at (x, y, z), in world coordinates; false if none */
bool block_box(int x, int y, int z, float *b) {
  int s = world_get(x, y, z);
  if (!(blk_flags[s] & BF_SOLID)) return false;
  float y1 = 1;
  int m = blk_model[s];
  if (m == M_SLAB) y1 = 0.5f;
  else if (m == M_LAYER) y1 = 0.125f;
  else if (m == M_FENCE) y1 = 1.5f;
  b[0] = (float)x;
  b[1] = (float)y;
  b[2] = (float)z;
  b[3] = x + 1.0f;
  b[4] = y + y1;
  b[5] = z + 1.0f;
  if (m == M_CACTUS) b[0] += 1 / 16.0f, b[2] += 1 / 16.0f, b[3] -= 1 / 16.0f, b[5] -= 1 / 16.0f;
  return true;
}

/* how far the box `a` can move along `axis` by `d` before touching block boxes */
float phys_clip(const float *a, int axis, float d) {
  int x0 = ifloor(a[0] - (axis == 0 && d < 0 ? -d : 0)) - 1, x1 = ifloor(a[3] + (axis == 0 && d > 0 ? d : 0)) + 1;
  int y0 = ifloor(a[1] - (axis == 1 && d < 0 ? -d : 0)) - 1, y1 = ifloor(a[4] + (axis == 1 && d > 0 ? d : 0)) + 1;
  int z0 = ifloor(a[2] - (axis == 2 && d < 0 ? -d : 0)) - 1, z1 = ifloor(a[5] + (axis == 2 && d > 0 ? d : 0)) + 1;
  for (int y = y0; y <= y1; y++)
    for (int z = z0; z <= z1; z++)
      for (int x = x0; x <= x1; x++) {
        float b[6];
        if (!block_box(x, y, z, b)) continue;
        /* overlapping on the other two axes? */
        bool ov = true;
        for (int k = 0; k < 3; k++)
          if (k != axis && (a[k + 3] <= b[k] || a[k] >= b[k + 3])) ov = false;
        if (!ov) continue;
        if (d > 0 && a[axis + 3] <= b[axis]) {
          float m = b[axis] - a[axis + 3];
          if (m < d) d = m;
        } else if (d < 0 && a[axis] >= b[axis + 3]) {
          float m = b[axis + 3] - a[axis];
          if (m > d) d = m;
        }
      }
  return d;
}

/* moves a box (w wide, h high, feet at p) by v; stops on blocks, zeroes the
 * velocity along blocked axes; true if it landed on something */
bool phys_move(float *p, float *v, float w, float h) {
  float a[6] = {p[0] - w / 2, p[1], p[2] - w / 2, p[0] + w / 2, p[1] + h, p[2] + w / 2};
  float dy = phys_clip(a, 1, v[1]);
  a[1] += dy, a[4] += dy;
  float dx = phys_clip(a, 0, v[0]);
  a[0] += dx, a[3] += dx;
  float dz = phys_clip(a, 2, v[2]);
  a[2] += dz, a[5] += dz;
  bool ground = dy != v[1] && v[1] < 0;
  if (dy != v[1]) v[1] = 0;
  if (dx != v[0]) v[0] = 0;
  if (dz != v[2]) v[2] = 0;
  p[0] = (a[0] + a[3]) / 2, p[1] = a[1], p[2] = (a[2] + a[5]) / 2;
  return ground;
}
