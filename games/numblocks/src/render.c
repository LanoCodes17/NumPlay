/* The 3D view: one ray per pixel of a 160 x 120 picture (shown at 2x), walked
 * through the block cache a block at a time until it meets something.
 *
 * Like Minecraft 1.8 with Fast graphics and smooth lighting off: each face is
 * lit by the light of the block in front of it, times its side's shade (top
 * 1.0, bottom 0.5, north/south 0.8, east/west 0.6); grass and leaves take
 * their biome's colour; linear fog fades to the sky's horizon colour at the
 * edge of the view. Leaves, glass, plants and water let the ray go on through
 * their see-through texels. The sky has its gradient, the sun or the moon, and
 * Minecraft's flat clouds at y = 128. */
#include <math.h>
#include "nb.h"

#define SR 4                       /* picture rows per strip (8 screen rows) */
static uint16_t strip[SCREEN_W * SR * 2];

/* per frame */
static float ox, oy, oz;           /* camera, cache coordinates */
static float fwx, fwy, fwz, rgx, rgz, upx, upy, upz;
static float fog0, fog1;           /* fog start and end (blocks) */
static int fog_r, fog_g, fog_b, sky_r, sky_g, sky_b;   /* 0..255 */
static int shade[4][16];           /* face shade x light level -> 0..256 */
static float sun_x, sun_y, sun_z;  /* towards the sun */
static float cloud_off;
static bool night;
static int under_water;

static const float TAN_V = 0.70020754f;   /* 70 degree vertical field of view */
#define TAN_H (TAN_V * (float)RW / (float)RH)
#define MAX_T 19.0f

static inline int r5(uint16_t c) { return (c >> 11) << 3 | (c >> 13); }
static inline int g6(uint16_t c) { return ((c >> 5) & 63) << 2 | ((c >> 9) & 3); }
static inline int b5(uint16_t c) { return (c & 31) << 3 | ((c >> 2) & 7); }
static inline uint16_t pack(int r, int g, int b) {
  if (r > 255) r = 255;
  if (g > 255) g = 255;
  if (b > 255) b = 255;
  return (uint16_t)((r & 0xF8) << 8 | (g & 0xFC) << 3 | b >> 3);
}

#ifdef HOST
unsigned long st_steps, st_texels, st_jumps, st_pixels;
#define ST(x) (x)
#else
#define ST(x) ((void)0)
#endif

/* ---------------------------------------------------------------- texels */
static inline int texel(int tex, int u, int v) {
  uint8_t p = tex_px[tex][(v * 16 + u) >> 1];
  return (u & 1) ? p >> 4 : p & 15;
}

static uint16_t tint_of(int b, int col) {
  int biome = vbiome[col];
  switch (blk_flags[b] & BF_TINT) {
    case 1: return biome_grass[biome];
    case 2: return biome_foliage[biome];
    case 3: return 0x630E;   /* spruce 0x619961 */
    case 4: return 0x8434;   /* birch 0x80A755 */
    case 5: return biome_water[biome];
    case 6: return 0x2384;   /* lily pad 0x208030 */
  }
  return 0xFFFF;
}

/* the colour of a texel (RGB565), or -1 if it is see-through */
static inline int texel565(int b, int tex, int u, int v, int col) {
  ST(st_texels++);
  int t = texel(tex, u, v);
  int fl = tex_flags[tex];
  if (!t && (fl & 0x20)) return -1;
  uint16_t c = tex_pal[tex][t];
  if (t >= (fl & 0x1F)) {
    uint16_t k = tint_of(b, col);
    c = pack(r5(c) * r5(k) >> 8, g6(c) * g6(k) >> 8, b5(c) * b5(k) >> 8);
  }
  return c;
}
static inline int texel_rgb(int b, int tex, int u, int v, int col, int *r, int *g, int *bl) {
  int c = texel565(b, tex, u, v, col);
  if (c < 0) return -1;
  *r = r5((uint16_t)c);
  *g = g6((uint16_t)c);
  *bl = b5((uint16_t)c);
  return 0;
}

/* ---------------------------------------------------------------- the sky */
static uint16_t sky(float dx, float dy, float dz, float len) {
  float e = dy / len;   /* elevation, -1..1 */
  /* horizon (fog colour) to the sky's colour above; below the horizon, fog */
  float k = e <= 0 ? 0 : e > 0.4f ? 1 : e / 0.4f;
  int r = fog_r + (int)((sky_r - fog_r) * k), g = fog_g + (int)((sky_g - fog_g) * k), b = fog_b + (int)((sky_b - fog_b) * k);
  if (under_water) return pack(r / 4, g / 3, b / 2 + 40);
  /* the sun (or the moon): a square 1/3 of the way to the horizon, as in 1.8 */
  float sd = (dx * sun_x + dy * sun_y + dz * sun_z) / len;
  if (sd > 0.94f) {
    float s = sd > 0.985f ? 1.0f : (sd - 0.94f) / 0.045f;
    if (night) r += (int)(190 * s), g += (int)(200 * s), b += (int)(210 * s);
    else r += (int)(255 * s), g += (int)(250 * s), b += (int)(200 * s);
  }
  /* clouds: Minecraft's 12-block cells at y = 128.33 */
  if (dy > 0.01f) {
    float t = (128.33f - (oy + vc_y0)) / dy;
    float cx = (ox + vc_x0) + dx * t + cloud_off, cz = (oz + vc_z0) + dz * t;
    float dist = t * len;
    if (dist < 200) {
      int ix = (int)floorf(cx / 12) & 255, iz = (int)floorf(cz / 12) & 255;
      extern const uint8_t clouds[256 * 256 / 8];
      if (clouds[(iz * 256 + ix) >> 3] & (1 << (ix & 7))) {
        int w = night ? 60 : 255;
        float a = 0.8f * (1 - dist / 200);
        r += (int)((w - r) * a);
        g += (int)((w - g) * a);
        b += (int)((w - b) * a);
      }
    }
  }
  return pack(r, g, b);
}

/* ---------------------------------------------------------------- tracing */
/* boxes of the shaped models, in sixteenths: x0 y0 z0 x1 y1 z1 */
static const int8_t BOX_SLAB[6] = {0, 0, 0, 16, 8, 16}, BOX_LAYER[6] = {0, 0, 0, 16, 2, 16},
                    BOX_TORCH[6] = {7, 0, 7, 9, 10, 9}, BOX_CACTUS[6] = {1, 0, 1, 15, 16, 15},
                    BOX_FLAT[6] = {0, 0, 0, 16, 1, 16}, BOX_LIQUID[6] = {0, 0, 0, 16, 14, 16},
                    BOX_POST[6] = {6, 0, 6, 10, 16, 10}, BOX_PANE[6] = {7, 0, 7, 9, 16, 9},
                    BOX_DOOR[6] = {0, 0, 0, 16, 16, 3}, BOX_LADDER[6] = {0, 0, 15, 16, 16, 16};

static const int8_t *model_box(int m) {
  switch (m) {
    case M_SLAB: return BOX_SLAB;
    case M_LAYER: return BOX_LAYER;
    case M_TORCH: return BOX_TORCH;
    case M_CACTUS: return BOX_CACTUS;
    case M_FLAT: return BOX_FLAT;
    case M_LIQUID: return BOX_LIQUID;
    case M_FENCE: return BOX_POST;
    case M_PANE: return BOX_PANE;
    case M_DOOR: return BOX_DOOR;
    case M_LADDER: case M_VINE: return BOX_LADDER;
  }
  return NULL;
}

typedef struct {
  float t;
  int face;        /* 0 -Y, 1 +Y, 2 -Z, 3 +Z, 4 -X, 5 +X */
  int u, v;
} Hit;

/* the ray against a box (sixteenths) inside the cell at (cx, cy, cz), local ray origin p, dir d */
static bool hit_box(const int8_t *bx, float px, float py, float pz, float dx, float dy, float dz, Hit *h) {
  float t0 = -1e9f, t1 = 1e9f;
  int f0 = 0;
  float lo[3] = {bx[0] / 16.0f, bx[1] / 16.0f, bx[2] / 16.0f}, hi[3] = {bx[3] / 16.0f, bx[4] / 16.0f, bx[5] / 16.0f};
  float p[3] = {px, py, pz}, d[3] = {dx, dy, dz};
  static const int axis_face[3][2] = {{4, 5}, {0, 1}, {2, 3}};   /* entering through min / max side */
  for (int a = 0; a < 3; a++) {
    if (d[a] == 0) {
      if (p[a] < lo[a] || p[a] > hi[a]) return false;
      continue;
    }
    float ta = (lo[a] - p[a]) / d[a], tb = (hi[a] - p[a]) / d[a];
    int fa = axis_face[a][0], fb = axis_face[a][1];
    if (ta > tb) {
      float t = ta;
      ta = tb;
      tb = t;
      int f = fa;
      fa = fb;
      fb = f;
    }
    if (ta > t0) t0 = ta, f0 = fa;
    if (tb < t1) t1 = tb;
  }
  if (t0 > t1 || t1 < 0) return false;
  if (t0 < 0) t0 = 0;
  float hx = px + dx * t0, hy = py + dy * t0, hz = pz + dz * t0;
  h->t = t0;
  h->face = f0;
  int ux, vy;
  switch (f0) {
    case 0: case 1: ux = (int)(hx * 16); vy = (int)(hz * 16); break;
    case 2: ux = 15 - (int)(hx * 16); vy = 15 - (int)(hy * 16); break;
    case 3: ux = (int)(hx * 16); vy = 15 - (int)(hy * 16); break;
    case 4: ux = (int)(hz * 16); vy = 15 - (int)(hy * 16); break;
    default: ux = 15 - (int)(hz * 16); vy = 15 - (int)(hy * 16); break;
  }
  h->u = ux < 0 ? 0 : ux > 15 ? 15 : ux;
  h->v = vy < 0 ? 0 : vy > 15 ? 15 : vy;
  return true;
}

/* the two crossed planes of a plant, local ray origin p and direction d, between t0 and t1 */
static bool hit_cross(int tex, float px, float py, float pz, float dx, float dy, float dz, float t0, float t1, Hit *h) {
  float best = 1e9f;
  for (int k = 0; k < 2; k++) {
    /* plane x = z (k 0) or x = 1 - z (k 1) */
    float den = k ? dx + dz : dx - dz;
    if (fabsf(den) < 1e-6f) continue;
    float t = k ? (1 - px - pz) / den : (pz - px) / den;
    if (t < t0 || t > t1 || t >= best) continue;
    float hx = px + dx * t, hy = py + dy * t;
    if (hx < 0 || hx >= 1 || hy < 0 || hy >= 1) continue;
    int u = (int)(hx * 16), v = 15 - (int)(hy * 16);
    if (v < 0 || v > 15) continue;
    if (!texel(tex, u, v) && (tex_flags[tex] & 0x20)) continue;
    best = t;
    h->t = t;
    h->face = 3;
    h->u = u;
    h->v = v;
  }
  return best < 1e9f;
}

/* shade a face texel: face shade, light, fog. Colours are shaded in RGB565
 * directly, red and blue in one multiply, green in another (s: 0..32) */
static float fog_k;   /* 1 / (fog1 - fog0) */
static inline uint16_t shade565(uint16_t c, int s) {
  uint32_t rb = ((uint32_t)(c & 0xF81F) * (uint32_t)s >> 5) & 0xF81F, g = ((uint32_t)(c & 0x07E0) * (uint32_t)s >> 5) & 0x07E0;
  return (uint16_t)(rb | g);
}
static inline uint16_t mix565(uint16_t a, uint16_t b, int k) {   /* a towards b by k/32 */
  uint32_t A = (a | (uint32_t)a << 16) & 0x07E0F81F, B = (b | (uint32_t)b << 16) & 0x07E0F81F;
  uint32_t m = (A + (((B - A) * (uint32_t)k) >> 5)) & 0x07E0F81F;
  return (uint16_t)(m | m >> 16);
}
static uint16_t fog565;
static inline uint16_t lit565(uint16_t c, int face, int light, float dist) {
  static const uint8_t side[6] = {3, 0, 1, 1, 2, 2};   /* 0.5, 1.0, 0.8, 0.6 */
  c = shade565(c, shade[side[face]][light]);
  if (dist > fog0) {
    int k = dist >= fog1 ? 32 : (int)((dist - fog0) * fog_k * 32);
    c = mix565(c, fog565, k);
  }
  return c;
}
static inline uint16_t lit(int r, int g, int b, int face, int light, float dist) {
  return lit565(pack(r, g, b), face, light, dist);
}

/* what the last trace met: 1 a face of a whole block (cell hx, hy, hz, face hf), 2 the sky, 0 anything else */
static int hit_kind, hit_x, hit_y, hit_z, hit_f;
static uint16_t trace(float dx, float dy, float dz) {
  hit_kind = 0;
  ST(st_pixels++);
  float len = sqrtf(dx * dx + dy * dy + dz * dz);
  int x = (int)floorf(ox), y = (int)floorf(oy), z = (int)floorf(oz);
  int sx = dx > 0 ? 1 : -1, sy = dy > 0 ? 1 : -1, sz = dz > 0 ? 1 : -1;
  /* signed inverses: the ray reaches plane x = X at t = (X - ox) * ivx */
  float ivx = dx != 0 ? 1 / dx : 1e9f, ivy = dy != 0 ? 1 / dy : 1e9f, ivz = dz != 0 ? 1 / dz : 1e9f;
  float idx = fabsf(ivx), idy = fabsf(ivy), idz = fabsf(ivz);
  int bx = dx > 0, by = dy > 0, bz = dz > 0;
  float tx = (x + bx - ox) * ivx, ty = (y + by - oy) * ivy, tz = (z + bz - oz) * ivz;
  float tmax = MAX_T / len, t = 0;
  int face = -1;
  /* a see-through layer in front (water): its colour and how much of it */
  int wr = 0, wg = 0, wb = 0, wa = 0;
  if ((unsigned)x >= VCX || (unsigned)y >= VCY || (unsigned)z >= VCZ) return hit_kind = 2, sky(dx, dy, dz, len);
  int i = VC_I(x, y, z), prev = i;
  int inside = vc[i];   /* the block the camera is in (water...) */
  int fresh = 1;        /* just entered a 4 x 4 x 4 region */
  int ex0 = sx > 0 ? 0 : 3, ey0 = sy > 0 ? 0 : 3, ez0 = sz > 0 ? 0 : 3;
  for (;;) {
    ST(st_steps++);
    int b = vc[i];
    if (b != B_AIR && !(b == inside && blk_model[b] == M_LIQUID)) {
      int m = blk_model[b];
      int col = z * VCX + x;
      float lx = ox + dx * t - x, ly = oy + dy * t - y, lz = oz + dz * t - z;
      if (m == M_CUBE || m == M_LEAVES || m == M_GLASS) {
        /* the face the ray came in through */
        float hx = lx, hy = ly, hz = lz;
        int u, v, f = face < 0 ? 1 : face;
        switch (f) {
          case 0: case 1: u = (int)(hx * 16); v = (int)(hz * 16); break;
          case 2: u = 15 - (int)(hx * 16); v = 15 - (int)(hy * 16); break;
          case 3: u = (int)(hx * 16); v = 15 - (int)(hy * 16); break;
          case 4: u = (int)(hz * 16); v = 15 - (int)(hy * 16); break;
          default: u = 15 - (int)(hz * 16); v = 15 - (int)(hy * 16); break;
        }
        u &= 15;
        v &= 15;
        int tc = texel565(b, blk_tex[b][f], u, v, col);
        if (tc >= 0) {
          uint16_t c = lit565((uint16_t)tc, f, light_at(prev), t * len);
          if (wa) c = pack((r5(c) * (256 - wa) + wr * wa) >> 8, (g6(c) * (256 - wa) + wg * wa) >> 8,
                           (b5(c) * (256 - wa) + wb * wa) >> 8);
          else if (face >= 0) hit_kind = 1, hit_x = x, hit_y = y, hit_z = z, hit_f = f;
          return c;
        }
      } else if (m == M_CROSS) {
        Hit h;
        float t1 = fminf(fminf(tx, ty), tz) - t;
        if (hit_cross(blk_tex[b][3], lx, ly, lz, dx, dy, dz, 0, t1, &h)) {
          int r, g, bb;
          texel_rgb(b, blk_tex[b][3], h.u, h.v, col, &r, &g, &bb);
          uint16_t c = lit(r, g, bb, 1, light_at(i), (t + h.t) * len);
          if (wa) c = pack((r5(c) * (256 - wa) + wr * wa) >> 8, (g6(c) * (256 - wa) + wg * wa) >> 8,
                           (b5(c) * (256 - wa) + wb * wa) >> 8);
          return c;
        }
      } else {
        const int8_t *bx = model_box(m);
        Hit h;
        if (bx && hit_box(bx, lx, ly, lz, dx, dy, dz, &h)) {
          int r, g, bb;
          int tex = blk_tex[b][h.face];
          if (texel_rgb(b, tex, h.u, h.v, col, &r, &g, &bb) == 0) {
            uint16_t c = lit(r, g, bb, h.face, light_at(m == M_LIQUID || m == M_TORCH ? i : prev), (t + h.t) * len);
            if (m == M_LIQUID && !wa) {
              /* water: see through it (lava is opaque) */
              if (b == B_WATER || b == B_FLOWING_WATER) {
                wr = r5(c), wg = g6(c), wb = b5(c), wa = 150;
                inside = b;
                goto next;
              }
            }
            if (wa) c = pack((r5(c) * (256 - wa) + wr * wa) >> 8, (g6(c) * (256 - wa) + wg * wa) >> 8,
                             (b5(c) * (256 - wa) + wb * wa) >> 8);
            return c;
          }
        }
      }
    }
  next:
    prev = i;
    if (b == B_AIR && fresh && !vmac[MC_I(x, y, z)]) {
      /* an empty 4 x 4 x 4 region: jump to where the ray leaves it */
      ST(st_jumps++);
      float ex = ((x & ~3) + (bx ? 4 : 0) - ox) * ivx, ey = ((y & ~3) + (by ? 4 : 0) - oy) * ivy,
            ez = ((z & ~3) + (bz ? 4 : 0) - oz) * ivz;
      float te;
      int ax;
      if (ex < ey && ex < ez) te = ex, ax = 0;
      else if (ey < ez) te = ey, ax = 1;
      else te = ez, ax = 2;
      if (te > tmax) break;
      t = te;
      /* the cell just past the boundary: on the crossing axis it is the next one */
      float hx = ox + dx * te, hy = oy + dy * te, hz = oz + dz * te;
      x = ax == 0 ? (x & ~3) + (bx ? 4 : -1) : (int)floorf(hx);
      y = ax == 1 ? (y & ~3) + (by ? 4 : -1) : (int)floorf(hy);
      z = ax == 2 ? (z & ~3) + (bz ? 4 : -1) : (int)floorf(hz);
      if ((unsigned)x >= VCX || (unsigned)y >= VCY || (unsigned)z >= VCZ) break;
      face = ax == 0 ? (sx > 0 ? 4 : 5) : ax == 1 ? (sy > 0 ? 0 : 1) : (sz > 0 ? 2 : 3);
      tx = (x + bx - ox) * ivx;
      ty = (y + by - oy) * ivy;
      tz = (z + bz - oz) * ivz;
      i = VC_I(x, y, z);
      /* the air cell in front of the boundary lights what is hit there */
      int bk = ax == 0 ? sx : ax == 1 ? sy * VCX * VCZ : sz * VCX;
      int px = x - (ax == 0 ? sx : 0), py = y - (ax == 1 ? sy : 0), pz = z - (ax == 2 ? sz : 0);
      prev = (unsigned)px < VCX && (unsigned)py < VCY && (unsigned)pz < VCZ ? i - bk : i;
      fresh = 1;
      continue;
    }
    /* the next cell */
    if (tx < ty) {
      if (tx < tz) {
        t = tx;
        tx += idx;
        x += sx;
        if ((unsigned)x >= VCX) break;
        i += sx;
        face = sx > 0 ? 4 : 5;
        fresh = (x & 3) == ex0;
      } else {
        t = tz;
        tz += idz;
        z += sz;
        if ((unsigned)z >= VCZ) break;
        i += sz * VCX;
        face = sz > 0 ? 2 : 3;
        fresh = (z & 3) == ez0;
      }
    } else if (ty < tz) {
      t = ty;
      ty += idy;
      y += sy;
      if ((unsigned)y >= VCY) break;
      i += sy * VCX * VCZ;
      face = sy > 0 ? 0 : 1;
      fresh = (y & 3) == ey0;
    } else {
      t = tz;
      tz += idz;
      z += sz;
      if ((unsigned)z >= VCZ) break;
      i += sz * VCX;
      face = sz > 0 ? 2 : 3;
      fresh = (z & 3) == ez0;
    }
    if (t > tmax) break;
  }
  uint16_t c = sky(dx, dy, dz, len);
  if (!wa) hit_kind = 2;
  if (wa) c = pack((r5(c) * (256 - wa) + wr * wa) >> 8, (g6(c) * (256 - wa) + wg * wa) >> 8, (b5(c) * (256 - wa) + wb * wa) >> 8);
  return c;
}

/* a pixel between samples that all met faces of the same plane (same side,
 * same coordinate): the plane gives the point, and the block there is drawn
 * if it really shows that face; otherwise (-1) the pixel is traced */
static int face_pixel(float dx, float dy, float dz, int plane, int f) {
  float t;
  switch (f >> 1) {
    case 0: t = (plane - oy) / dy; break;
    case 1: t = (plane - oz) / dz; break;
    default: t = (plane - ox) / dx; break;
  }
  if (!(t > 0)) return -1;
  float hx = ox + dx * t, hy = oy + dy * t, hz = oz + dz * t;
  int x = (int)hx, y = (int)hy, z = (int)hz;
  /* the block behind the plane: the plane is its min side for faces 0 2 4 */
  switch (f) {
    case 0: y = plane; break;
    case 1: y = plane - 1; break;
    case 2: z = plane; break;
    case 3: z = plane - 1; break;
    case 4: x = plane; break;
    default: x = plane - 1; break;
  }
  if ((unsigned)x >= VCX || (unsigned)y >= VCY || (unsigned)z >= VCZ || hx < 0 || hy < 0 || hz < 0) return -1;
  int b = vc[VC_I(x, y, z)];
  int m = blk_model[b];
  if (m != M_CUBE && m != M_LEAVES && m != M_GLASS) return -1;
  static const int8_t nx[6] = {0, 0, 0, 0, -1, 1}, ny[6] = {-1, 1, 0, 0, 0, 0}, nz[6] = {0, 0, -1, 1, 0, 0};
  int px = x + nx[f], py = y + ny[f], pz = z + nz[f];
  int light = 15;
  if ((unsigned)px < VCX && (unsigned)py < VCY && (unsigned)pz < VCZ) {
    int fi = VC_I(px, py, pz);
    if (vc[fi] != B_AIR) return -1;   /* hidden, or something stands in front: trace */
    light = light_at(fi);
  }
  float lx = hx - x, ly = hy - y, lz = hz - z;
  int u, v;
  switch (f) {
    case 0: case 1: u = (int)(lx * 16); v = (int)(lz * 16); break;
    case 2: u = 15 - (int)(lx * 16); v = 15 - (int)(ly * 16); break;
    case 3: u = (int)(lx * 16); v = 15 - (int)(ly * 16); break;
    case 4: u = (int)(lz * 16); v = 15 - (int)(ly * 16); break;
    default: u = 15 - (int)(lz * 16); v = 15 - (int)(ly * 16); break;
  }
  if ((unsigned)u > 15 || (unsigned)v > 15) return -1;
  int tc = texel565(b, blk_tex[b][f], u, v, z * VCX + x);
  if (tc < 0) return -1;
  float d2 = t * t * (dx * dx + dy * dy + dz * dz);
  return lit565((uint16_t)tc, f, light, d2 > fog0 * fog0 ? sqrtf(d2) : 0);
}

/* the samples of one row of the coarse grid (every other pixel, and one past the right edge) */
typedef struct {
  uint16_t c;
  uint8_t kind, f;
  int8_t plane;   /* the face's plane: its coordinate along the face's axis */
} Sample;
static Sample rows[3][RW / 2 + 1];

static void sample_row(Sample *s, int py) {
  float sv = (1 - 2 * (py + 0.5f) / RH) * TAN_V;
  float bx = fwx + upx * sv, by = fwy + upy * sv, bz = fwz + upz * sv;
  for (int k = 0; k <= RW / 2; k++) {
    float su = (2 * (2 * k + 0.5f) / RW - 1) * TAN_H;
    s[k].c = trace(bx + rgx * su, by, bz + rgz * su);
    s[k].kind = (uint8_t)hit_kind;
    s[k].f = (uint8_t)hit_f;
    int f = hit_f;
    s[k].plane = (int8_t)((f >> 1) == 0 ? hit_y + (f & 1) : (f >> 1) == 1 ? hit_z + (f & 1) : hit_x + (f & 1));
  }
}

static inline bool same(const Sample *a, const Sample *b) {
  return a->kind == b->kind && (a->kind == 2 || (a->kind == 1 && a->f == b->f && a->plane == b->plane));
}

/* the pixel (px, py) knowing the samples around it agree (a, b) or not */
static uint16_t between(int px, int py, const Sample *a, const Sample *b, const Sample *c, const Sample *d) {
  float sv = (1 - 2 * (py + 0.5f) / RH) * TAN_V, su = (2 * (px + 0.5f) / RW - 1) * TAN_H;
  float dx = fwx + upx * sv + rgx * su, dy = fwy + upy * sv, dz = fwz + upz * sv + rgz * su;
  bool agree = same(a, b) && (!c || (same(a, c) && same(a, d)));
  if (agree && a->kind == 1) {
    int r = face_pixel(dx, dy, dz, a->plane, a->f);
    if (r >= 0) return (uint16_t)r;
  }
  if (agree && a->kind == 2) return sky(dx, dy, dz, sqrtf(dx * dx + dy * dy + dz * dz));
  return trace(dx, dy, dz);
}

/* ---------------------------------------------------------------- the frame */
static void setup_light(float sun) {
  /* Minecraft's light table (gamma "Moody"): (1 - f) / (3f + 1), f = 1 - level / 15,
   * sky light scaled by the sun, then brightened to at least 0.03 */
  static const float face_shade[4] = {1.0f, 0.8f, 0.6f, 0.5f};
  for (int l = 0; l < 16; l++) {
    float f = 1 - l / 15.0f;
    float bri = (1 - f) / (f * 3 + 1);
    bri = bri * (sun * 0.95f + 0.05f);
    bri = bri * 0.96f + 0.03f;
    for (int s = 0; s < 4; s++) shade[s][l] = (int)(bri * face_shade[s] * 32 + 0.5f);
  }
}

void render_frame(const Camera *c, uint32_t tod) {
  float yaw = c->yaw * 0.017453292f, pitch = c->pitch * 0.017453292f;
  float cy = cosf(yaw), sy = sinf(yaw), cp = cosf(pitch), sp = sinf(pitch);
  fwx = -sy * cp;
  fwy = -sp;
  fwz = cy * cp;
  rgx = -cy;
  rgz = -sy;
  /* up = right x forward */
  upx = 0 * fwz - rgz * fwy;
  upy = rgz * fwx - rgx * fwz;
  upz = rgx * fwy - 0 * fwx;
  ox = c->x - vc_x0;
  oy = c->y - vc_y0;
  oz = c->z - vc_z0;
  /* the time of day: 0 sunrise, 6000 noon, 18000 midnight (Minecraft's ticks) */
  float ang = (tod % 24000) / 24000.0f - 0.25f;
  if (ang < 0) ang += 1;
  ang = ang + ((1 - (cosf(ang * 3.14159265f) + 1) / 2) - ang) / 3;
  float day = cosf(ang * 6.2831853f) * 2 + 0.5f;
  if (day < 0) day = 0;
  if (day > 1) day = 1;
  float sunb = 1 - (cosf(ang * 6.2831853f) * 2 + 0.2f);
  sunb = sunb < 0 ? 0 : sunb > 1 ? 1 : sunb;
  sunb = (1 - sunb) * 0.8f + 0.2f;
  setup_light(sunb);
  night = day < 0.5f;
  /* the sun's direction: it rises in the east (+X) */
  float sa = ang * 6.2831853f;
  sun_x = -sinf(sa);
  sun_y = cosf(sa);
  sun_z = 0;
  if (night) sun_x = -sun_x, sun_y = -sun_y;
  /* the sky's colour from the biome's temperature (1.8: HSB), and the fog's */
  int col = (int)(oz) * VCX + (int)(ox);
  if (col < 0 || col >= VCX * VCZ) col = 0;
  float temp = biome_temp[vbiome[col]] / 50.0f / 3.0f;
  if (temp < -1) temp = -1;
  if (temp > 1) temp = 1;
  float h = 0.62222224f - temp * 0.05f, s = 0.5f + temp * 0.1f;
  /* HSB to RGB, brightness 1 */
  float hh = (h - floorf(h)) * 6, ff = hh - floorf(hh);
  float p = 1 - s, q = 1 - s * ff, tt = 1 - s * (1 - ff);
  float R, G, B;
  switch ((int)hh) {
    case 0: R = 1, G = tt, B = p; break;
    case 1: R = q, G = 1, B = p; break;
    case 2: R = p, G = 1, B = tt; break;
    case 3: R = p, G = q, B = 1; break;
    case 4: R = tt, G = p, B = 1; break;
    default: R = 1, G = p, B = q; break;
  }
  sky_r = (int)(R * day * 255);
  sky_g = (int)(G * day * 255);
  sky_b = (int)(B * day * 255);
  float fr = 0.7529412f * (day * 0.94f + 0.06f), fg = 0.84705883f * (day * 0.94f + 0.06f), fb = 1.0f * (day * 0.91f + 0.09f);
  /* 1.8 mixes the fog towards the sky at short view distances */
  fr += (R * day - fr) * 0.26f;
  fg += (G * day - fg) * 0.26f;
  fb += (B * day - fb) * 0.26f;
  fog_r = (int)(fr * 255);
  fog_g = (int)(fg * 255);
  fog_b = (int)(fb * 255);
  fog1 = MAX_T;
  fog0 = MAX_T * 0.75f;
  int ci = (unsigned)(int)floorf(ox) < VCX && (unsigned)(int)floorf(oy) < VCY && (unsigned)(int)floorf(oz) < VCZ
               ? vc[VC_I((int)floorf(ox), (int)floorf(oy), (int)floorf(oz))]
               : B_AIR;
  under_water = ci == B_WATER || ci == B_FLOWING_WATER;
  if (under_water) {
    fog_r = 10, fog_g = 30, fog_b = 110;
    fog0 = 0;
    fog1 = 9;
  }
  fog_k = 1 / (fog1 - fog0);
  fog565 = pack(fog_r, fog_g, fog_b);
  cloud_off = (tod % 24000) * 0.03f;

  /* every other pixel of every other row is traced; the others come from
   * their neighbours when those met the same face (or the sky) */
  Sample *top = rows[0], *mid = rows[1], *bot = rows[2];
  sample_row(top, 0);
  for (int py = 0; py < RH; py += SR) {
    for (int r = 0; r < SR; r += 2) {
      int y = py + r;
      sample_row(bot, y + 2);
      uint16_t *row0 = strip + (r * 2) * SCREEN_W, *row1 = strip + (r * 2 + 2) * SCREEN_W;
      for (int k = 0; k < RW / 2; k++) {
        int x = 2 * k;
        uint16_t c00 = top[k].c;
        uint16_t c10 = between(x + 1, y, &top[k], &top[k + 1], NULL, NULL);
        uint16_t c01 = between(x, y + 1, &top[k], &bot[k], NULL, NULL);
        uint16_t c11 = between(x + 1, y + 1, &top[k], &top[k + 1], &bot[k], &bot[k + 1]);
        row0[x * 2] = row0[x * 2 + 1] = c00;
        row0[x * 2 + 2] = row0[x * 2 + 3] = c10;
        row1[x * 2] = row1[x * 2 + 1] = c01;
        row1[x * 2 + 2] = row1[x * 2 + 3] = c11;
      }
      memcpy(row0 + SCREEN_W, row0, SCREEN_W * 2);
      memcpy(row1 + SCREEN_W, row1, SCREEN_W * 2);
      Sample *t = top;
      top = bot;
      bot = t;
    }
    hud_strip(strip, py * 2, SR * 2);
    plat_push(0, py * 2, SCREEN_W, SR * 2, strip);
  }
  (void)mid;
}
