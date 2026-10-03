/* The room's ground (tools/coll.py: segments, read in place) and what the game asks of Unity's 2D physics: rays, and
 * bodies (a box) that fall, slide along the ground and say which colliders they touch. */
#include <math.h>
#include "hk.h"
#ifdef HOST
#include <stdio.h>
#include <stdlib.h>
#endif

#define UNIT (1.0f / 128)
#define CELL 8.0f
#define SKIN 0.01f   /* how far a body rests from the ground (Box2D's polygon radius) */

typedef struct {
  int16_t x0, y0, x1, y1;   /* (1/128 unit) */
} Seg;

static const uint8_t *g_cols, *g_tiles, *g_seg_col;
static const Seg *g_segs;
static const uint16_t *g_cell_first, *g_cell_list;
static int g_tw, g_th, g_row_bytes;
static int g_room_ground = -1;
static uint8_t g_col_off[256 / 8];   /* colliders switched off (a broken wall's) */

void phys_collider_enable(int col, bool on) {
  if (col <= 0 || col >= 256) return;
  if (on) g_col_off[col >> 3] &= (uint8_t)~(1 << (col & 7));
  else g_col_off[col >> 3] |= (uint8_t)(1 << (col & 7));
}

void phys_colliders_reset(void) { memset(g_col_off, 0, sizeof g_col_off); }

static void ground_init(void) {
  if (g_room_ground == g_room.id) return;
  const RoomHdr *h = g_room.h;
  const uint8_t *p = section(SEC_RBLOB) + h->ground;
  g_cols = p;
  p += (h->ncol + 1u) & ~1u;
  g_tw = rd16(p), g_th = rd16(p + 2);
  g_row_bytes = (g_tw + 7) / 8;
  g_tiles = p + 4;
  p += (4u + (uint32_t)g_th * g_row_bytes + 1) & ~1u;
  g_segs = (const Seg *)(const void *)p;
  p += sizeof(Seg) * h->nseg;
  g_seg_col = p;
  p += (h->nseg + 1u) & ~1u;
  g_cell_first = (const uint16_t *)(const void *)p;
  g_cell_list = g_cell_first + h->gw * h->gh + 1;
  g_room_ground = g_room.id;
}

uint8_t phys_col_flags(int col) {
  ground_init();
  return g_cols[col];
}

bool phys_tile_solid(int x, int y) {
  ground_init();
  if ((unsigned)x >= (unsigned)g_tw || (unsigned)y >= (unsigned)g_th) return false;
  return g_tiles[y * g_row_bytes + (x >> 3)] >> (x & 7) & 1;
}

static inline int cell_x(float x) {
  int c = (int)floorf(x / CELL) + 1, w = g_room.h->gw;
  return c < 0 ? 0 : c >= w ? w - 1 : c;
}
static inline int cell_y(float y) {
  int c = (int)floorf(y / CELL) + 1, h = g_room.h->gh;
  return c < 0 ? 0 : c >= h ? h - 1 : c;
}

/* the ground's segments near a box: the room's (maybe twice) and the tilemap's faces between solid and open tiles */
typedef struct {
  float ax, ay, bx, by;
  uint8_t col;
} Cand;
#define MAX_CAND 192

static int gather(float x0, float y0, float x1, float y1, uint8_t mask, Cand *out) {
  ground_init();
  int n = 0;
  int gw = g_room.h->gw;
  for (int cy = cell_y(y0), cy1 = cell_y(y1); cy <= cy1; cy++)
    for (int cx = cell_x(x0), cx1 = cell_x(x1); cx <= cx1; cx++)
      for (int k = g_cell_first[cy * gw + cx], e = g_cell_first[cy * gw + cx + 1]; k < e; k++) {
        int i = g_cell_list[k];
        uint8_t col = g_seg_col[i];
        if (!(g_cols[col] & mask) || (g_col_off[col >> 3] >> (col & 7) & 1) || n == MAX_CAND) continue;
        const Seg *s = &g_segs[i];
        float ax = s->x0 * UNIT, ay = s->y0 * UNIT, bx = s->x1 * UNIT, by = s->y1 * UNIT;
        if ((ax < x0 && bx < x0) || (ax > x1 && bx > x1) || (ay < y0 && by < y0) || (ay > y1 && by > y1)) continue;
        out[n++] = (Cand){ax, ay, bx, by, col};
      }
  if (!(g_cols[0] & mask)) return n;
  int tx0 = (int)floorf(x0) - 1, tx1 = (int)floorf(x1) + 1, ty0 = (int)floorf(y0) - 1, ty1 = (int)floorf(y1) + 1;
  if (tx0 < 0) tx0 = 0;
  if (ty0 < 0) ty0 = 0;
  if (tx1 >= g_tw) tx1 = g_tw - 1;
  if (ty1 >= g_th) ty1 = g_th - 1;
  for (int ty = ty0; ty <= ty1; ty++)
    for (int tx = tx0; tx <= tx1; tx++) {
      if (!phys_tile_solid(tx, ty) || n > MAX_CAND - 4) continue;
      float X = (float)tx, Y = (float)ty;
      if (!phys_tile_solid(tx - 1, ty)) out[n++] = (Cand){X, Y, X, Y + 1, 0};
      if (!phys_tile_solid(tx + 1, ty)) out[n++] = (Cand){X + 1, Y, X + 1, Y + 1, 0};
      if (!phys_tile_solid(tx, ty - 1)) out[n++] = (Cand){X, Y, X + 1, Y, 0};
      if (!phys_tile_solid(tx, ty + 1)) out[n++] = (Cand){X, Y + 1, X + 1, Y + 1, 0};
    }
  return n;
}

/* ---------------------------------------------------------------- rays */
bool phys_ray(float x, float y, float dx, float dy, float dist, uint8_t mask, PhysHit *hit) {
  float ex = x + dx * dist, ey = y + dy * dist;
  Cand cand[MAX_CAND];
  int n = gather(fminf(x, ex), fminf(y, ey), fmaxf(x, ex), fmaxf(y, ey), mask, cand);
  float best = dist + 1;
  int bcol = -1;
  float bnx = 0, bny = 0;
  for (int i = 0; i < n; i++) {
    const Cand *s = &cand[i];
    float sx = s->bx - s->ax, sy = s->by - s->ay;
    float den = dx * sy - dy * sx;
    if (fabsf(den) < 1e-9f) continue;
    float qx = s->ax - x, qy = s->ay - y;
    float t = (qx * sy - qy * sx) / den;     /* along the ray */
    float u = (qx * dy - qy * dx) / den;     /* along the segment */
    if (t < 0 || t > dist || u < 0 || u > 1 || t >= best) continue;
    best = t, bcol = s->col;
    float l = sqrtf(sx * sx + sy * sy);
    bnx = -sy / l, bny = sx / l;
    if (bnx * dx + bny * dy > 0) bnx = -bnx, bny = -bny;   /* facing the ray */
  }
  if (bcol < 0) return false;
  if (hit) hit->dist = best, hit->x = x + dx * best, hit->y = y + dy * best, hit->nx = bnx, hit->ny = bny, hit->col = bcol;
  return true;
}

/* ---------------------------------------------------------------- a box against a segment (separating axes) */
/* the box (center cx, cy, half sizes hx, hy) moving by (dx, dy) and the segment: the first time (0..1) they touch and the
 * normal there (towards the box); false if they do not. Already overlapping: time 0 and the shortest way out (depth). */
static bool box_seg(float cx, float cy, float hx, float hy, float dx, float dy, const Cand *s, float *toi, float *nx,
                    float *ny, float *depth) {
  float ax = s->ax, ay = s->ay, bx = s->bx, by = s->by;
  float sx = bx - ax, sy = by - ay, l = sqrtf(sx * sx + sy * sy);
  if (l < 1e-6f) return false;
  float axes[3][2] = {{1, 0}, {0, 1}, {-sy / l, sx / l}};
  float t0 = -1e30f, t1 = 1e30f, tnx = 0, tny = 0, pen = 1e30f, pnx = 0, pny = 0;
  bool inside = true;   /* (overlapping on every axis now, not just touching) */
  for (int i = 0; i < 3; i++) {
    float ux = axes[i][0], uy = axes[i][1];
    float r = hx * fabsf(ux) + hy * fabsf(uy);
    float c = cx * ux + cy * uy, pa = ax * ux + ay * uy, pb = bx * ux + by * uy;
    float lo = (pa < pb ? pa : pb) - r, hi = (pa < pb ? pb : pa) + r;   /* where the box center overlaps */
    float v = dx * ux + dy * uy;
    /* overlap now: how deep, and which way out */
    if (c > lo + 1e-5f && c < hi - 1e-5f) {
      float out_lo = c - lo, out_hi = hi - c;
      if (out_lo < pen) pen = out_lo, pnx = -ux, pny = -uy;   /* (out past lo: the negative way) */
      if (out_hi < pen) pen = out_hi, pnx = ux, pny = uy;
    } else
      inside = false;
    if (fabsf(v) < 1e-12f) {
      if (c <= lo || c >= hi) return false;   /* never meet on this axis */
      continue;
    }
    float ta = (lo - c) / v, tb = (hi - c) / v;
    float enter = ta < tb ? ta : tb, leave = ta < tb ? tb : ta;
    if (enter > t0) {
      t0 = enter;
      /* the normal opposes the motion on this axis */
      tnx = v > 0 ? -ux : ux, tny = v > 0 ? -uy : uy;
    }
    if (leave < t1) t1 = leave;
    if (t0 > t1) return false;
  }
  if (t1 < 0 || t0 > 1) return false;
  if (t0 < 0) {
    /* overlapping already; or just touching, and not moving into it */
    if (!inside) return false;
    *toi = 0, *nx = pnx, *ny = pny, *depth = pen;
    return true;
  }
  *toi = t0, *nx = tnx, *ny = tny, *depth = 0;
  return true;
}

/* ---------------------------------------------------------------- bodies */
#define GRAVITY (-60.0f)

static void body_box(const Body *b, float *cx, float *cy) { *cx = b->x + b->ox, *cy = b->y + b->oy; }

/* the colliders the box touches now, with a normal each (towards the box) */
static int touching(const Body *b, uint8_t mask, uint8_t *cols, float *nxs, float *nys, int max) {
  float cx, cy;
  body_box(b, &cx, &cy);
  float hx = b->hx + SKIN * 2, hy = b->hy + SKIN * 2;
  Cand cand[MAX_CAND];
  int nc = gather(cx - hx, cy - hy, cx + hx, cy + hy, mask, cand), n = 0;
  for (int k = 0; k < nc; k++) {
    const Cand *s = &cand[k];
    float toi, nx, ny, depth;
    if (!box_seg(cx, cy, hx, hy, 0, 0, s, &toi, &nx, &ny, &depth)) continue;
    int i = 0;
    while (i < n && cols[i] != s->col) i++;
    if (i < n) {
      /* (the most upward normal of its contacts: standing beats touching a side) */
      if (ny > nys[i]) nxs[i] = nx, nys[i] = ny;
      continue;
    }
    if (n == max) break;
    cols[n] = s->col, nxs[n] = nx, nys[n] = ny, n++;
  }
  return n;
}

void body_step(Body *b, float dt) {
  if (b->gravity_scale) b->vy += GRAVITY * b->gravity_scale * dt;
  /* resting contacts take away the velocity into them (the solver), then the box moves and slides */
  for (int i = 0; i < b->ncontacts; i++) {
    float nx = b->cnx[i], ny = b->cny[i], vn = b->vx * nx + b->vy * ny;
    if (vn >= 0) continue;
    b->vx -= vn * nx, b->vy -= vn * ny;
    if (b->friction > 0) {
      /* (Coulomb: the tangent velocity less up to the friction times the normal impulse) */
      float vt = b->vx * -ny + b->vy * nx, f = -vn * b->friction;
      float d = vt > f ? f : vt < -f ? -f : vt;
      b->vx -= d * -ny, b->vy -= d * nx;
    }
  }
  float left = 1;
  for (int it = 0; it < 4 && left > 0; it++) {
    float dx = b->vx * dt * left, dy = b->vy * dt * left;
    float cx, cy;
    body_box(b, &cx, &cy);
    float hx = b->hx + SKIN, hy = b->hy + SKIN;
    float bx0 = fminf(cx, cx + dx) - hx, bx1 = fmaxf(cx, cx + dx) + hx, by0 = fminf(cy, cy + dy) - hy, by1 = fmaxf(cy, cy + dy) + hy;
    float best = 2, bnx = 0, bny = 0, deep = 0, dnx = 0, dny = 0;
    Cand cand[MAX_CAND];
    int nc = gather(bx0, by0, bx1, by1, b->mask, cand);
    for (int k = 0; k < nc; k++) {
      const Cand *s = &cand[k];
      float toi, nx, ny, depth;
      if (!box_seg(cx, cy, hx, hy, dx, dy, s, &toi, &nx, &ny, &depth)) continue;
      if (depth > 0) {
#ifdef HOST
        if (getenv("PHYSDBG")) fprintf(stderr, "  deep seg %.3f,%.3f-%.3f,%.3f col %d depth %.3f n %.2f,%.2f box c %.3f,%.3f h %.3f,%.3f\n", s->ax, s->ay, s->bx, s->by, s->col, depth, nx, ny, cx, cy, hx, hy);
#endif
        if (depth > deep) deep = depth, dnx = nx, dny = ny;
        continue;
      }
      if (nx * dx + ny * dy >= 0) continue;   /* moving away from it */
      if (toi < best) best = toi, bnx = nx, bny = ny;
    }
#ifdef HOST
    if (getenv("PHYSDBG")) fprintf(stderr, "it %d d %.4f,%.4f best %.3f n %.2f,%.2f deep %.4f dn %.2f,%.2f nc %d\n", it, dx, dy, best, bnx, bny, deep, dnx, dny, nc);
#endif
    if (deep > 0) {
      /* inside the ground (it moved, or the body was put there): out the shortest way */
      b->x += dnx * deep, b->y += dny * deep;
      float vn = b->vx * dnx + b->vy * dny;
      if (vn < 0) b->vx -= vn * dnx, b->vy -= vn * dny;
      continue;
    }
    if (best > 1) {
      b->x += dx, b->y += dy;
      break;
    }
    b->x += dx * best, b->y += dy * best;
    float vn = b->vx * bnx + b->vy * bny;
    if (vn < 0) b->vx -= vn * bnx, b->vy -= vn * bny;
    left *= 1 - best;
  }
  /* what it touches now: enter, stay, exit */
  uint8_t cols[MAX_CONTACTS];
  float nxs[MAX_CONTACTS], nys[MAX_CONTACTS];
  int n = touching(b, b->mask, cols, nxs, nys, MAX_CONTACTS);
  b->nevents = 0;
  for (int i = 0; i < b->ncontacts; i++) {
    int j = 0;
    while (j < n && cols[j] != b->ccol[i]) j++;
    if (j == n && b->events && b->nevents < MAX_EVENTS) b->events[b->nevents++] = (BodyEvent){EV_EXIT, b->ccol[i], b->cnx[i], b->cny[i]};
  }
  for (int j = 0; j < n; j++) {
    int i = 0;
    while (i < b->ncontacts && b->ccol[i] != cols[j]) i++;
    if (b->events && b->nevents < MAX_EVENTS) b->events[b->nevents++] = (BodyEvent){i == b->ncontacts ? EV_ENTER : EV_STAY, cols[j], nxs[j], nys[j]};
  }
  b->ncontacts = n;
  for (int j = 0; j < n; j++) b->ccol[j] = cols[j], b->cnx[j] = nxs[j], b->cny[j] = nys[j];
}

/* a box (x0, y0, x1, y1) and a convex polygon of n points: do they overlap (separating axes)? */
bool box_meets_shape(float x0, float y0, float x1, float y1, const float *pts, int n) {
  if (n < 3) return true;
  for (int i = 0; i < n; i++) {
    float ax = pts[2 * i], ay = pts[2 * i + 1], bx = pts[2 * ((i + 1) % n)], by = pts[2 * ((i + 1) % n) + 1];
    float nx = -(by - ay), ny = bx - ax;
    if (nx == 0 && ny == 0) continue;
    float pmin = 1e30f, pmax = -1e30f;
    for (int j = 0; j < n; j++) {
      float d = pts[2 * j] * nx + pts[2 * j + 1] * ny;
      if (d < pmin) pmin = d;
      if (d > pmax) pmax = d;
    }
    float c[4] = {x0 * nx + y0 * ny, x1 * nx + y0 * ny, x0 * nx + y1 * ny, x1 * nx + y1 * ny};
    float bmin = c[0], bmax = c[0];
    for (int k = 1; k < 4; k++) bmin = c[k] < bmin ? c[k] : bmin, bmax = c[k] > bmax ? c[k] : bmax;
    if (bmax < pmin || bmin > pmax) return false;
  }
  /* (and the box's own axes) */
  float px0 = 1e30f, px1 = -1e30f, py0 = 1e30f, py1 = -1e30f;
  for (int j = 0; j < n; j++) {
    px0 = fminf(px0, pts[2 * j]), px1 = fmaxf(px1, pts[2 * j]), py0 = fminf(py0, pts[2 * j + 1]), py1 = fmaxf(py1, pts[2 * j + 1]);
  }
  return !(px1 < x0 || px0 > x1 || py1 < y0 || py0 > y1);
}
