/* The player: Minecraft 1.8.8's movement (EntityLiving.moveEntityWithHeading,
 * Entity.moveEntity), the block looked at, mining and placing.
 *
 * Everything runs in game ticks, 20 a second, with the game's numbers:
 * gravity 0.08 and drag 0.98 a tick, a jump of 0.42, ground friction
 * 0.6 x 0.91, walking at 0.1 (x 1.3 sprinting, x 0.3 sneaking), 0.02 in the
 * air, a 0.6 x 1.8 box that steps up 0.6, eyes at 1.62. */
#include <math.h>
#include "nb.h"

Player pl;

#define W 0.3f   /* half width */
#define H 1.8f

static inline int ifloor(float v) { int i = (int)v; return v < (float)i ? i - 1 : i; }

/* the collision box of the block at (x, y, z), in world coordinates; false if none */
static bool block_box(int x, int y, int z, float *b) {
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
static float clip(const float *a, int axis, float d) {
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

static void box_of(float x, float y, float z, float *a) {
  a[0] = x - W, a[1] = y, a[2] = z - W, a[3] = x + W, a[4] = y + H, a[5] = z + W;
}

/* Entity.moveEntity: y first, then x and z; step up 0.6 if that goes further */
static void move(float dx, float dy, float dz) {
  float a[6];
  box_of(pl.x, pl.y, pl.z, a);
  float ody = dy, odx = dx, odz = dz;
  /* sneaking on the ground: no walking off edges */
  if (pl.on_ground && pl.sneaking) {
    float t[6];
    for (; dx != 0; dx = fabsf(dx) < 0.05f ? 0 : dx - (dx > 0 ? 0.05f : -0.05f)) {
      memcpy(t, a, sizeof t);
      t[0] += dx, t[3] += dx, t[1] -= 1, t[4] = t[1] + 1;
      if (clip(t, 1, -0.01f) > -0.01f) break;   /* something under */
    }
    for (; dz != 0; dz = fabsf(dz) < 0.05f ? 0 : dz - (dz > 0 ? 0.05f : -0.05f)) {
      memcpy(t, a, sizeof t);
      t[2] += dz, t[5] += dz, t[1] -= 1, t[4] = t[1] + 1;
      if (clip(t, 1, -0.01f) > -0.01f) break;
    }
    odx = dx, odz = dz;
  }
  float mdy = clip(a, 1, dy);
  a[1] += mdy, a[4] += mdy;
  float mdx = clip(a, 0, dx);
  a[0] += mdx, a[3] += mdx;
  float mdz = clip(a, 2, dz);
  a[2] += mdz, a[5] += mdz;
  bool grounded = ody != mdy && ody < 0;
  if ((grounded || pl.on_ground) && (odx != mdx || odz != mdz)) {
    /* try again 0.6 higher */
    float s[6];
    box_of(pl.x, pl.y, pl.z, s);
    float up = clip(s, 1, 0.6f);
    s[1] += up, s[4] += up;
    float sx = clip(s, 0, odx);
    s[0] += sx, s[3] += sx;
    float sz = clip(s, 2, odz);
    s[2] += sz, s[5] += sz;
    float down = clip(s, 1, -up);
    s[1] += down, s[4] += down;
    if (sx * sx + sz * sz > mdx * mdx + mdz * mdz) {
      memcpy(a, s, sizeof s);
      mdx = sx, mdz = sz, mdy = up + down;
    }
  }
  pl.x = (a[0] + a[3]) / 2;
  pl.y = a[1];
  pl.z = (a[2] + a[5]) / 2;
  pl.on_ground = ody != mdy && ody < 0;
  if (ody != mdy) pl.vy = 0;
  if (odx != mdx) pl.vx = 0;
  if (odz != mdz) pl.vz = 0;
}

/* Entity.moveFlying */
static void move_flying(float strafe, float fwd, float f) {
  float d = strafe * strafe + fwd * fwd;
  if (d < 1e-4f) return;
  d = sqrtf(d);
  if (d < 1) d = 1;
  d = f / d;
  strafe *= d;
  fwd *= d;
  float y = pl.yaw * 0.017453292f, s = sinf(y), c = cosf(y);
  pl.vx += strafe * c - fwd * s;
  pl.vz += fwd * c + strafe * s;
}

static bool in_liquid(void) {
  int b = world_get(ifloor(pl.x), ifloor(pl.y + 0.4f), ifloor(pl.z));
  return blk_model[b] == M_LIQUID;
}

/* ---------------------------------------------------------------- looking */
/* the first block the eyes' ray meets within 4.5 blocks (1.8's survival reach) */
static void pick(void) {
  float ex = pl.x, ey = pl.y + (pl.sneaking ? 1.54f : 1.62f), ez = pl.z;
  float yaw = pl.yaw * 0.017453292f, pitch = pl.pitch * 0.017453292f;
  float dx = -sinf(yaw) * cosf(pitch), dy = -sinf(pitch), dz = cosf(yaw) * cosf(pitch);
  int x = ifloor(ex), y = ifloor(ey), z = ifloor(ez);
  int sx = dx > 0 ? 1 : -1, sy = dy > 0 ? 1 : -1, sz = dz > 0 ? 1 : -1;
  float idx = dx != 0 ? fabsf(1 / dx) : 1e9f, idy = dy != 0 ? fabsf(1 / dy) : 1e9f, idz = dz != 0 ? fabsf(1 / dz) : 1e9f;
  float tx = dx > 0 ? (x + 1 - ex) * idx : (ex - x) * idx, ty = dy > 0 ? (y + 1 - ey) * idy : (ey - y) * idy,
        tz = dz > 0 ? (z + 1 - ez) * idz : (ez - z) * idz;
  int face = -1;
  float t = 0;
  pl.hit_face = -1;
  while (t <= 4.5f) {
    int b = world_get(x, y, z);
    if (b != B_AIR && blk_model[b] != M_LIQUID) {
      pl.hit_x = x, pl.hit_y = y, pl.hit_z = z, pl.hit_face = face < 0 ? 1 : face;
      return;
    }
    if (tx < ty && tx < tz) t = tx, tx += idx, x += sx, face = sx > 0 ? 4 : 5;
    else if (ty < tz) t = ty, ty += idy, y += sy, face = sy > 0 ? 0 : 1;
    else t = tz, tz += idz, z += sz, face = sz > 0 ? 2 : 3;
  }
}

/* ---------------------------------------------------------------- a tick */
void player_spawn(void) {
  int x, y, z;
  gen_spawn(&x, &y, &z);
  memset(&pl, 0, sizeof pl);
  pl.x = x + 0.5f;
  pl.y = (float)y;
  pl.z = z + 0.5f;
  static const int start[9] = {B_DIRT, B_COBBLESTONE, B_PLANKS_OAK, B_LOG_OAK, B_GLASS, B_TORCH, B_STONE_BRICKS, B_SAND,
                               B_CRAFTING_TABLE};
  memcpy(pl.hotbar, start, sizeof start);
}

static int break_cooldown;

void player_tick(uint32_t keys, uint32_t pressed) {
  float fwd = (keys & K_FWD ? 1.0f : 0) - (keys & K_BACKW ? 1.0f : 0);
  float strafe = (keys & K_STRAFE_L ? 1.0f : 0) - (keys & K_STRAFE_R ? 1.0f : 0);
  pl.sneaking = (keys & K_SNEAK) != 0;
  fwd *= 0.98f;
  strafe *= 0.98f;
  if (pl.sneaking) fwd *= 0.3f, strafe *= 0.3f;
  pl.in_water = in_liquid();
  if (keys & K_JUMP) {
    if (pl.in_water) pl.vy += 0.04f;
    else if (pl.on_ground) {
      pl.vy = 0.42f;
      if (pl.sprinting) {
        float y = pl.yaw * 0.017453292f;
        pl.vx -= sinf(y) * 0.2f;
        pl.vz += cosf(y) * 0.2f;
      }
    }
  }
  if (pl.in_water) {
    move_flying(strafe, fwd, 0.02f);
    move(pl.vx, pl.vy, pl.vz);
    pl.vx *= 0.8f, pl.vy *= 0.8f, pl.vz *= 0.8f;
    pl.vy -= 0.02f;
  } else {
    float fr = pl.on_ground ? 0.6f * 0.91f : 0.91f;
    float speed = 0.1f * (pl.sprinting ? 1.3f : 1.0f);
    float f = pl.on_ground ? speed * (0.16277136f / (fr * fr * fr)) : (pl.sprinting ? 0.026f : 0.02f);
    move_flying(strafe, fwd, f);
    move(pl.vx, pl.vy, pl.vz);
    pl.vy -= 0.08f;
    pl.vy *= 0.98f;
    pl.vx *= fr;
    pl.vz *= fr;
  }
  if (pl.y < -64) pl.y = 100, pl.vy = 0;
  pick();
  /* mining (Back held) and placing (OK) */
  if (break_cooldown) break_cooldown--;
  if ((keys & K_ATTACK) && pl.hit_face >= 0 && !break_cooldown) {
    int b = world_get(pl.hit_x, pl.hit_y, pl.hit_z);
    int hard = blk_hard[b];
    if (hard == 255) pl.breaking = 0;
    else {
      /* by hand: 1 / hardness / 100 a tick if the block needs a tool, else / 30 */
      float per = hard ? 20.0f / hard / (blk_tool[b] == T_PICKAXE ? 100.0f : 30.0f) : 1.0f;
      pl.breaking += per;
      if (pl.breaking >= 1) {
        world_set(pl.hit_x, pl.hit_y, pl.hit_z, B_AIR);
        pl.breaking = 0;
        break_cooldown = 5;
      }
    }
  } else pl.breaking = 0;
  if ((pressed & K_USE) && pl.hit_face >= 0) {
    static const int nx[6] = {0, 0, 0, 0, -1, 1}, ny[6] = {-1, 1, 0, 0, 0, 0}, nz[6] = {0, 0, -1, 1, 0, 0};
    int x = pl.hit_x + nx[pl.hit_face], y = pl.hit_y + ny[pl.hit_face], z = pl.hit_z + nz[pl.hit_face];
    int b = pl.hotbar[pl.slot];
    float a[6];
    box_of(pl.x, pl.y, pl.z, a);
    bool inside = x + 1 > a[0] && x < a[3] && y + 1 > a[1] && y < a[4] && z + 1 > a[2] && z < a[5];
    int here = world_get(x, y, z);
    if ((here == B_AIR || blk_model[here] == M_LIQUID) && (!inside || !(blk_flags[b] & BF_SOLID))) world_set(x, y, z, b);
  }
  for (int n = 0; n < 9; n++)
    if (pressed & (K_SLOT1 << n)) pl.slot = n;
}
