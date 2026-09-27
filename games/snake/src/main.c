/* Snake: the snake of Google's search page game, for the NumWorks calculator,
 * with the options of Tatone26's Snake from All the Apps (speed, board size,
 * walls, wrapping) and a few of Google's (more apples, portals).
 *
 * The snake is a chain of capsules through the centres of its cells, so it
 * glides from cell to cell and turns with round corners. Each frame only the
 * cells around the head, the tail and the popping apples are drawn again: one
 * cell at a time, composed in a small buffer, then pushed. Menus are drawn in
 * bands of 20 rows. */
#include <eadk.h>
#include <stdbool.h>
#include <stdint.h>
#include "../../common/epsilon_app.h"
#include "../../common/epsilon_files.h"

#ifdef __ELF__ /* app name and API level, for the calculator's installer */
const char eadk_app_name[] __attribute__((section(".rodata.eadk_app_name"))) = "Snake";
const uint32_t eadk_api_level __attribute__((section(".rodata.eadk_api_level"))) = 0;
#endif

typedef uint16_t color;
#define RGB(c) (color)((((c) >> 8) & 0xF800) | (((c) >> 5) & 0x07E0) | (((c) >> 3) & 0x1F))
#define WHITE 0xFFFF
/* the keys used, in 32 bits: 4, 6, 2 and EXE come down from the high word */
#define KEY(k) (1u << ((k) - ((k) >= 32) * 24))
static uint32_t keys(void) {
  uint64_t s = eadk_keyboard_scan();
  return ((uint32_t)s & 0x800001FFu) | ((uint32_t)(s >> 32) & 0x00100850u) << 8;
}

/* Google Snake's colours */
#define C_BAR RGB(0x4A752C)   /* the band on top, and the panels */
#define C_EDGE RGB(0x578A34)  /* around the field, and the walls */
#define C_LIGHT RGB(0xAAD751) /* the checkerboard */
#define C_DARK RGB(0xA2D149)
#define C_SNAKE RGB(0x4E7CF6)
#define C_APPLE RGB(0xE7471D)
#define C_GOLD RGB(0xFFC53D)
#define C_PALE RGB(0xCFE6B0) /* small text on green */
#define HDR 26               /* the band on top */

/* ------------------------------------------------------------------ drawing */
#define BUFPX (320 * 20)
static color buf[BUFPX];
static int rx, ry, rw, rh;     /* the part of the screen in buf */
static int kx0, ky0, kx1, ky1; /* where drawing lands, inside it */

static int imin(int a, int b) { return a < b ? a : b; }
static int imax(int a, int b) { return a > b ? a : b; }

static void clip(int x, int y, int w, int h) {
  kx0 = imax(x, rx), ky0 = imax(y, ry), kx1 = imin(x + w, rx + rw), ky1 = imin(y + h, ry + rh);
}
static void unclip(void) { clip(0, 0, 320, 240); }

/* f over b; a from 0 (all b) to 32 (all f) */
static color mix(color f, color b, int a) {
  uint32_t x = (f | (uint32_t)f << 16) & 0x07E0F81F, y = (b | (uint32_t)b << 16) & 0x07E0F81F;
  y = (y + ((x - y) * (uint32_t)a >> 5)) & 0x07E0F81F;
  return (color)(y | y >> 16);
}

static void fill(int x, int y, int w, int h, color c) {
  for (int j = imax(y, ky0), j1 = imin(y + h, ky1); j < j1; j++)
    for (int i = imax(x, kx0), i1 = imin(x + w, kx1); i < i1; i++) buf[(j - ry) * rw + i - rx] = c;
}

/* A capsule from a to b, its radius going from ra to rb, with soft edges.
   Everything round is one: discs, the snake, letters, leaves. */
static void blob(float ax, float ay, float bx, float by, float ra, float rb, color c) {
  float dx = bx - ax, dy = by - ay, l = dx * dx + dy * dy, inv = l > 0 ? 1 / l : 0, r = (ra > rb ? ra : rb) + 2;
  int x0 = imax(kx0, (int)((ax < bx ? ax : bx) - r)), x1 = imin(kx1, (int)((ax > bx ? ax : bx) + r));
  int y0 = imax(ky0, (int)((ay < by ? ay : by) - r)), y1 = imin(ky1, (int)((ay > by ? ay : by) + r));
  for (int y = y0; y < y1; y++) {
    float qy = y + 0.5f - ay;
    color *p = buf + (y - ry) * rw - rx;
    for (int x = x0; x < x1; x++) {
      float qx = x + 0.5f - ax, t = (qx * dx + qy * dy) * inv;
      t = t < 0 ? 0 : t > 1 ? 1 : t;
      float ex = qx - t * dx, ey = qy - t * dy, e = ra + (rb - ra) * t + 0.5f, d = ex * ex + ey * ey;
      if (d >= e * e) continue;
      float a = e - __builtin_sqrtf(d);
      p[x] = a >= 1 ? c : mix(c, p[x], (int)(a * 32));
    }
  }
}
static void disc(float x, float y, float r, color c) { blob(x, y, x, y, r, r, c); }
static void box(int x, int y, int w, int h, int r, color c) {
  fill(x + r, y, w - 2 * r, h, c);
  fill(x, y + r, w, h - 2 * r, c);
  for (int i = 0; i < 4; i++) disc(x + r + (i & 1) * (w - 2 * r), y + r + (i >> 1) * (h - 2 * r), r, c);
}
/* darkens everything under the band on top, behind a panel */
static void dim(void) {
  clip(0, HDR, 320, 240);
  for (int j = ky0; j < ky1; j++)
    for (color *p = buf + (j - ry) * rw, *e = p + rw; p < e; p++) *p = mix(*p, 0, 14);
  unclip();
}

/* ------------------------------------------------------------------ the game */
#define MAXN (25 * 17)
static const uint8_t BW[3] = {15, 20, 25}, BH[3] = {10, 13, 17}, CS[3] = {20, 15, 12};
static const uint8_t STEP_MS[3] = {190, 135, 90};
static const int8_t DX[4] = {1, 0, -1, 0}, DY[4] = {0, 1, 0, -1}; /* right, down, left, up */

/* everything kept between visits (snake.sav) */
static struct {
  uint8_t magic, version;
  uint8_t opt[6];     /* speed, size, apples, walls, wrap, portals */
  uint16_t best[9];   /* per size and speed */
} V;
static const uint8_t OPT_N[6] = {3, 3, 3, 2, 2, 2}, OPT_DEF[6] = {1, 1, 0, 0, 0, 0};
#define SAVE_NAME "snake.sav"
#define O_SPEED V.opt[0]
#define O_SIZE V.opt[1]
#define O_APPLES V.opt[2]
#define O_WALLS V.opt[3]
#define O_WRAP V.opt[4]
#define O_PORTAL V.opt[5]

static int bw, bh, cs, fx, fy; /* board in cells, cell size, where the field starts */
static uint16_t body[MAXN];    /* the snake's cells (x | y << 8), a ring from tl to hd */
static uint8_t ndir[MAXN];     /* the direction each was entered in */
static uint8_t occ[MAXN];      /* per cell */
#define O_BODY 1
#define O_WALL 2
static int hd, tl, len, grow, score, nq, hop, otail, acc, wpop, gate[3][2]; /* gate: each pair's portals last used */
static uint8_t q[2]; /* turns asked for, not made yet */
static bool tmove;   /* the tail left otail in the last step */
static uint32_t now, wborn, chew_t, seed;

typedef struct {
  uint16_t p;
  uint8_t kind; /* 0 an apple, else the pair of portals it belongs to */
  uint8_t eat;  /* being eaten: steps left before it goes */
  bool live;
  uint32_t born;
} apple_t;
static apple_t ap[8];

static uint32_t rnd(void) {
  seed ^= seed << 13;
  seed ^= seed >> 17;
  seed ^= seed << 5;
  return seed;
}
static int iabs(int v) { return v < 0 ? -v : v; }
static int at(int p) { return (p >> 8) * bw + (p & 255); }
static int step_ms(void) { return STEP_MS[O_SPEED]; }
static uint16_t *best(void) { return &V.best[O_SIZE * 3 + O_SPEED]; }

static bool far(int p, int h) { return iabs((p & 255) - (h & 255)) + iabs((p >> 8) - (h >> 8)) > 2; }
/* a cell for an apple or a wall: free, and (first try) not right by the head,
   or the portal it is about to come out of */
static bool vacant(int p, int pass) {
  if (occ[at(p)]) return false;
  for (int i = 0; i < 8; i++)
    if (ap[i].live && ap[i].p == p) return false;
  return pass || (far(p, body[hd]) && (hop < 0 || far(p, hop)));
}
/* the n-th vacant cell, or (1 << 20) less the number of them */
static int nth(int pass, int n) {
  for (int y = 0; y < bh; y++)
    for (int x = 0; x < bw; x++)
      if (vacant(x | y << 8, pass) && !n--) return x | y << 8;
  return n;
}
static int pick(void) {
  for (int pass = 0; pass < 2; pass++) {
    int n = (1 << 20) - nth(pass, 1 << 20);
    if (n) return nth(pass, (int)(rnd() % (uint32_t)n));
  }
  return -1;
}
static bool spawn(int kind) {
  int p = pick();
  for (int i = 0; i < 8 && p >= 0; i++)
    if (!ap[i].live) {
      ap[i] = (apple_t){(uint16_t)p, (uint8_t)kind, 0, true, now};
      return true;
    }
  return false;
}

static void new_game(void) {
  bw = BW[O_SIZE], bh = BH[O_SIZE], cs = CS[O_SIZE];
  fx = (320 - bw * cs) / 2, fy = HDR + (240 - HDR - bh * cs) / 2;
  for (int i = 0; i < MAXN; i++) occ[i] = 0;
  for (int i = 0; i < 8; i++) ap[i].live = false;
  int row = bh / 2;
  for (int i = 0; i < 4; i++) body[i] = (uint16_t)((1 + i) | row << 8), ndir[i] = 0, occ[row * bw + 1 + i] = O_BODY;
  for (int i = 0; i < 6; i++) gate[i / 2][i & 1] = -1;
  tl = 0, hd = 3, len = 4, grow = score = nq = 0, hop = wpop = -1, tmove = false, acc = step_ms();
  chew_t = now - 1000;
  /* the first apple straight ahead, like Google's */
  int n = O_APPLES * 2 + 1, k = O_PORTAL;
  ap[0] = (apple_t){(uint16_t)((bw - 5) | row << 8), (uint8_t)k, 0, true, 0};
  if (k) {
    spawn(1);
    for (k = 2; k <= (n + 1) / 2; k++) spawn(k), spawn(k);
  } else {
    while (--n) spawn(0);
  }
  for (int i = 0; i < 8; i++) ap[i].born = 0; /* no popping in the menus */
}

/* One cell forward; false if the snake crashes. */
static bool step(void) {
  int h = body[hd], d = ndir[hd], np;
  if (hop >= 0) { /* out of the other portal, the same way */
    np = hop, hop = -1;
  } else {
    if (nq) d = q[0], q[0] = q[1], nq--;
    int x = (h & 255) + DX[d], y = (h >> 8) + DY[d];
    if (x < 0 || y < 0 || x >= bw || y >= bh) {
      if (!O_WRAP) return false;
      x = (x + bw) % bw, y = (y + bh) % bh;
    }
    np = x | y << 8;
  }
  int c = at(np);
  bool moving = !grow;
  if ((occ[c] & O_WALL) || ((occ[c] & O_BODY) && !(moving && np == body[tl]))) return false;
  for (int i = 0; i < 8; i++)
    if (ap[i].eat && !--ap[i].eat) ap[i].live = false, chew_t = now;
  if (moving) {
    otail = body[tl];
    occ[at(otail)] &= (uint8_t)~O_BODY;
    tl = (tl + 1) % MAXN, len--;
  } else {
    grow--;
  }
  tmove = moving;
  hd = (hd + 1) % MAXN, len++;
  body[hd] = (uint16_t)np, ndir[hd] = (uint8_t)d, occ[c] |= O_BODY;
  for (int i = 0; i < 8; i++) {
    apple_t *a = &ap[i];
    if (!a->live || a->eat || a->p != np) continue;
    a->eat = 1, score++, grow++;
    if (a->kind) {
      for (int j = 0; j < 8; j++)
        if (j != i && ap[j].live && !ap[j].eat && ap[j].kind == a->kind) hop = gate[a->kind - 1][1] = ap[j].p, ap[j].eat = 2;
      gate[a->kind - 1][0] = np;
      if (spawn(a->kind) && !spawn(a->kind)) /* no room for two: a plain apple */
        for (int j = 0; j < 8; j++)
          if (ap[j].live && !ap[j].eat && ap[j].kind == a->kind) ap[j].kind = 0;
    } else {
      spawn(0);
    }
    if (O_WALLS && !(score & 1)) { /* Tatone26's walls: one more every two apples */
      int w = pick();
      if (w >= 0) occ[at(w)] |= O_WALL, wpop = w, wborn = now;
    }
    break;
  }
  return true;
}

/* ------------------------------------------------------------------ the board */
enum { S_TITLE, S_SET, S_READY, S_PLAY, S_PAUSE, S_QUIT, S_DIE, S_OVER };
static int state, sel;
static bool dead, newbest, won;
static float prog, mouth; /* how far into the step; how open the mouth is */
static color scol;        /* the snake's colour (it flashes when it dies) */
static int qx0, qy0, qx1, qy1; /* the cells the drawn part of the screen touches */

static int fl(int v) { return (v + cs * 16) / cs - 16; } /* floor(v / cs) */
static bool near(int p) {
  int x = p & 255, y = p >> 8;
  return x >= qx0 && x <= qx1 && y >= qy0 && y <= qy1;
}
static float ux(int p) { return fx + (p & 255) * cs + cs * 0.5f; }
static float uy(int p) { return fy + (p >> 8) * cs + cs * 0.5f; }
static void cellclip(int p) { clip(fx + (p & 255) * cs, fy + (p >> 8) * cs, cs, cs); }
static void fieldclip(void) { clip(fx, fy, bw * cs, bh * cs); }

static void ground(void) {
  for (int j = 0; j < rh; j++) {
    int y = ry + j, cy = fl(y - fy), cx = fl(rx - fx), e = fx + cx * cs + cs;
    bool in = y >= HDR && cy >= 0 && cy < bh;
    color *p = buf + j * rw;
    for (int i = 0; i < rw; i++) {
      if (rx + i >= e) cx++, e += cs;
      p[i] = y < HDR ? C_BAR : in && cx >= 0 && cx < bw ? ((cx + cy) & 1 ? C_DARK : C_LIGHT) : C_EDGE;
    }
  }
}

static color checker(int p) { return ((p & 255) + (p >> 8)) & 1 ? C_DARK : C_LIGHT; }

/* an apple of s pixels centred on (x, y); a portal has a ring in its pair's
   colour (and only the ring once it is in use: kind | 8) */
static const uint32_t ring[3] = {0x9A5CF6, 0xFF8A1E, 0x1EB8E8};
static void apple(float x, float y, float s, int kind, color bg) {
  if (kind) { /* a ring, and a smaller apple in it */
    disc(x, y, s * 0.5f, RGB(ring[(kind & 3) - 1]));
    disc(x, y, s * 0.35f, bg);
    if (kind & 8) return;
    s *= 0.72f;
  }
  blob(x + s * 0.02f, y - s * 0.2f, x + s * 0.08f, y - s * 0.42f, s * 0.05f, s * 0.04f, RGB(0x6D4C2F));
  disc(x, y + s * 0.05f, s * 0.37f, C_APPLE);
  disc(x - s * 0.14f, y - s * 0.05f, s * 0.09f, RGB(0xF58A67));
  blob(x + s * 0.1f, y - s * 0.34f, x + s * 0.33f, y - s * 0.44f, s * 0.09f, s * 0.035f, RGB(0x4E9F2D));
}

static float ease(uint32_t born, int ms) { /* 0 to 1 with a little overshoot */
  int a = (int)(now - born);
  if (a >= ms) return 1;
  float t = (float)a / ms - 1;
  return 1 + t * t * (2.70158f * t + 1.70158f);
}

static void items(void) {
  for (int y = imax(qy0, 0); y <= imin(qy1, bh - 1); y++)
    for (int x = imax(qx0, 0); x <= imin(qx1, bw - 1); x++)
      if (occ[y * bw + x] & O_WALL) { /* a block of hedge */
        int p = x | y << 8, m = (int)(cs * (1 - (p == wpop ? ease(wborn, 300) : 1)) / 2) + 1, w = cs - 2 * m;
        int bx = fx + x * cs + m, by = fy + y * cs + m;
        box(bx, by, w, w, cs / 5, RGB(0x3D6624));
        box(bx, by, w, w - cs / 7 - 1, cs / 5, C_EDGE);
      }
  for (int i = 0; i < 6; i++) { /* portals the snake is going through: a dark hole under it */
    int g = gate[i / 2][i & 1];
    if (g >= 0 && near(g) && (occ[at(g)] & O_BODY)) disc(ux(g), uy(g), cs * 0.36f, mix(RGB(ring[i / 2]), 0, 16));
  }
  for (int i = 0; i < 8; i++) {
    apple_t *a = &ap[i];
    if (a->live && near(a->p)) apple(ux(a->p), uy(a->p), cs * ease(a->born, 260), a->kind, checker(a->p));
  }
}

/* the rings of portals the snake is going through, over it: it goes into
   one and comes out of the other */
static void rings(void) {
  for (int i = 0; i < 6; i++) {
    int g = gate[i / 2][i & 1];
    if (g < 0 || !near(g) || !(occ[at(g)] & O_BODY)) continue;
    float c = 1, sn = 0, r = cs * 0.43f;
    for (int k = 0; k < 16; k++) { /* 16 dots around, turning by 22.5 degrees */
      disc(ux(g) + c * r, uy(g) + sn * r, cs * 0.085f, RGB(ring[i / 2]));
      float c2 = c * 0.92388f - sn * 0.38268f;
      sn = sn * 0.92388f + c * 0.38268f, c = c2;
    }
  }
}

/* The head at (x, y), looking along (ax, ay), r its radius: an open mouth,
   and eyes that look where it goes. */
static void face(float x, float y, float ax, float ay, float r) {
  if (mouth > 0.05f) {
    float mx = x + ax * r * 0.5f, my = y + ay * r * 0.5f, w = r * 0.45f * mouth, m = r * 0.42f * mouth;
    blob(mx - ay * w, my + ax * w, mx + ay * w, my - ax * w, m, m, RGB(0x7A1F1A));
  }
  for (int s = -1; s <= 1; s += 2) {
    float ex = x - ax * r * 0.1f - ay * r * 0.5f * s, ey = y - ay * r * 0.1f + ax * r * 0.5f * s, k = r * 0.22f;
    disc(ex, ey, r * 0.47f, WHITE);
    if (dead) {
      blob(ex - k, ey - k, ex + k, ey + k, r * 0.07f, r * 0.07f, 0);
      blob(ex - k, ey + k, ex + k, ey - k, r * 0.07f, r * 0.07f, 0);
    } else {
      disc(ex + ax * r * 0.2f, ey + ay * r * 0.2f, r * 0.25f, RGB(0x1B1B1B));
    }
  }
}

static float rad(float s) { return cs * (s >= 1.5f ? 0.41f : 0.25f + 0.107f * s); } /* thinner near the tail */

/* The snake from cell a to cell b (entered going d), from u0 to u1 of the
   way. Across an edge (wrapping) or a portal, a and b are not side by side:
   each gets its own half, cut at its cell's border. */
static void piece(int a, int b, int d, float u0, float u1, float r0, float r1) {
  float sx = DX[d] * cs, sy = DY[d] * cs, ax = ux(a), ay = uy(a);
  bool side = (a & 255) + DX[d] == (b & 255) && (a >> 8) + DY[d] == (b >> 8);
  if (!side) cellclip(a);
  blob(ax + sx * u0, ay + sy * u0, ax + sx * u1, ay + sy * u1, r0, r1, scol);
  if (side) return;
  ax = ux(b) - sx, ay = uy(b) - sy;
  cellclip(b);
  blob(ax + sx * u0, ay + sy * u0, ax + sx * u1, ay + sy * u1, r0, r1, scol);
  fieldclip();
}

static void snake(void) {
  fieldclip();
  float p = prog, s = tmove ? 1 - p : 0;
  int k = tl;
  if (tmove && (near(otail) || near(body[tl]))) piece(otail, body[tl], ndir[tl], p, 1, rad(0), rad(s));
  for (int i = 0; i < len - 1; i++) {
    int k1 = (k + 1) % MAXN;
    float u = i == len - 2 ? p : 1;
    if (near(body[k]) || near(body[k1])) piece(body[k], body[k1], ndir[k1], 0, u, rad(s + i), rad(s + i + u));
    k = k1;
  }
  int h = body[hd], b = body[(hd + MAXN - 1) % MAXN], d = ndir[hd], d0 = ndir[(hd + MAXN - 1) % MAXN];
  if (near(h) || near(b)) {
    /* the head turns over the first third of a step */
    float w = p * 3 > 1 ? 1 : p * 3, ax = DX[d0] + (DX[d] - DX[d0]) * w, ay = DY[d0] + (DY[d] - DY[d0]) * w;
    float n = 1 / __builtin_sqrtf(ax * ax + ay * ay), r = cs * 0.41f;
    bool side = (b & 255) + DX[d] == (h & 255) && (b >> 8) + DY[d] == (h >> 8);
    if (!side) cellclip(h);
    face(ux(h) - DX[d] * cs * (1 - p), uy(h) - DY[d] * cs * (1 - p), ax * n, ay * n, r);
    if (!side) {
      cellclip(b);
      face(ux(b) + DX[d] * cs * p, uy(b) + DY[d] * cs * p, ax * n, ay * n, r);
    }
  }
  unclip();
}

/* ------------------------------------------------------------------ text and panels */
static int pass, ty0, ty1; /* 0: shapes into buf; 1: the text whose last row is in ty0..ty1 */
static int slen(const char *s) {
  int n = 0;
  while (s[n]) n++;
  return n;
}
static void lab(const char *s, int x, int y, int large, color fg, color bg) {
  int b = y + (large ? 17 : 13);
  if (pass && b >= ty0 && b < ty1) eadk_display_draw_string(s, (eadk_point_t){(uint16_t)x, (uint16_t)y}, large, fg, bg);
}
static void clab(const char *s, int cx, int y, int large, color fg, color bg) {
  lab(s, cx - slen(s) * (large ? 10 : 7) / 2, y, large, fg, bg);
}
static void button(const char *s, int x, int y, int w, int h, bool on) {
  color c = on ? C_SNAKE : C_EDGE;
  if (!pass) box(x, y, w, h, h / 2, c);
  clab(s, x + w / 2, y + (h - 18) / 2, 1, WHITE, c);
}
/* n buttons from names[0], the first at (x, y), the others dx, dy further */
static void buttons(const char *const *names, int n, int x, int y, int w, int dx, int dy) {
  for (int i = 0; i < n; i++) button(names[i], x + i * dx, y + i * dy, w, 32, sel == i);
}
static const char *const BUTTONS[] = {"Play", "Settings", "Resume", "Restart", "Quit game", "No", "Yes", "Play", "Menu"};
static void panel(int x, int y, int w, int h) {
  if (pass) return;
  dim();
  box(x, y + 3, w, h, 14, RGB(0x2A4A18));
  box(x, y, w, h, 14, C_BAR);
}
static char *itoa(char *o, int v) {
  char t[8];
  int n = 0;
  do t[n++] = (char)('0' + v % 10); while (v /= 10);
  while (n) *o++ = t[--n];
  *o = 0;
  return o;
}

static void trophy(float x, float y, float k, color bg) {
  for (int s = -1; s <= 1; s += 2) {
    disc(x + 6 * k * s, y - 4 * k, 3 * k, C_GOLD);
    disc(x + 6 * k * s, y - 4 * k, 1.4f * k, bg);
  }
  clip(0, (int)(y - 7 * k), 320, 240);
  blob(x, y - 7 * k, x, y, 7 * k, 2 * k, C_GOLD);
  unclip();
  fill((int)(x - k), (int)(y), (int)(2 * k + 1), (int)(4 * k), C_GOLD);
  box((int)(x - 5 * k), (int)(y + 3 * k), (int)(10 * k), (int)(3 * k), (int)k, C_GOLD);
}

/* SNAKE in the snake's own tube: points x << 4 | y on a 4 x 8 grid, 0xF0 lifts
   the pen, 0xF1 goes to the next letter */
static const uint8_t LOGO[] = {
  0x41, 0x30, 0x10, 0x01, 0x03, 0x14, 0x34, 0x45, 0x47, 0x38, 0x18, 0x07, 0xF1,
  0x08, 0x00, 0x48, 0x40, 0xF1,
  0x08, 0x02, 0x20, 0x42, 0x48, 0xF0, 0x05, 0x45, 0xF1,
  0x00, 0x08, 0xF0, 0x40, 0x04, 0x48, 0xF1,
  0x40, 0x00, 0x08, 0x48, 0xF0, 0x04, 0x34,
};
static void logo(int x, int y) {
  for (int k = 0; k < 2; k++) { /* a shadow, then the snake */
    float px = 0, py = 0, u = 4.5f;
    int lx = x, ly = y + 3 - 3 * k;
    bool pen = false;
    for (unsigned i = 0; i < sizeof LOGO; i++) {
      int c = LOGO[i];
      if (c >= 0xF0) {
        pen = false, lx += c & 1 ? 30 : 0;
        continue;
      }
      float nx = lx + (c >> 4) * u, ny = ly + (c & 15) * u;
      if (pen) blob(px, py, nx, ny, 4.6f, 4.6f, k ? C_SNAKE : RGB(0x2A4A18));
      px = nx, py = ny, pen = true;
    }
  }
  disc(x + 19, y + 4.5f, 6.5f, C_SNAKE); /* the head, on the S */
  mouth = 0, dead = false;
  face(x + 20, y + 4.5f, 1, 0, 6.5f);
}

static const char *const OPT_NAME[6] = {"Speed", "Size", "Apples", "Walls", "Wrap", "Portals"};
static const char *const OPT_VAL[] = {"Slow", "Normal", "Fast", "Small", "Medium", "Large", "1", "3", "5", "Off", "On"};

/* the band on top, and the panel of the moment */
static void ui(void) {
  char t[8];
  if (!pass) apple(15, 13, 24, 0, C_BAR), trophy(84, 13, 1, C_BAR);
  itoa(t, score);
  lab(t, 30, 4, 1, WHITE, C_BAR);
  itoa(t, *best());
  lab(t, 98, 4, 1, WHITE, C_BAR);
  switch (state) {
    case S_READY: /* the arrow keys, until the first move */
      if (pass) break;
      box(116, 104, 88, 58, 10, C_BAR);
      for (int d = 0; d < 4; d++) {
        int x = 160 + DX[d] * 26, y = d == 3 ? 119 : 146;
        box(x - 11, y - 11, 22, 22, 5, RGB(0xE4EEDA));
        for (int t = 0, a; (a = 5 - t) >= 0; t++) /* a triangle, one line at a time */
          fill(x + DX[d] * (t - 2) - a * !DX[d], y + DY[d] * (t - 2) - a * !DY[d], DX[d] ? 1 : 2 * a + 1, DX[d] ? 2 * a + 1 : 1, C_BAR);
      }
      break;
    case S_TITLE:
      panel(46, 42, 228, 176);
      if (!pass) {
        logo(70, 56);
        apple(238, 72, 34, 0, C_BAR);
      }
      buttons(BUTTONS, 2, 85, 114, 150, 0, 42);
      clab("Based on Tatone26's version", 160, 196, 0, C_PALE, C_BAR);
      break;
    case S_SET:
      panel(28, 34, 264, 200);
      clab("Settings", 160, 44, 1, WHITE, C_BAR);
      for (int i = 0; i < 6; i++) {
        int y = 70 + i * 26, on = sel == i;
        color c = on ? C_SNAKE : C_BAR;
        if (on && !pass) box(40, y, 240, 24, 12, C_SNAKE);
        lab(OPT_NAME[i], 54, y + 3, 1, WHITE, c);
        clab(OPT_VAL[(i < 3 ? i * 3 : 9) + V.opt[i]], 226, y + 3, 1, WHITE, c);
        if (on) lab("<", 174, y + 3, 1, WHITE, c), lab(">", 268, y + 3, 1, WHITE, c);
      }
      break;
    case S_PAUSE:
      panel(80, 54, 160, 160);
      clab("Paused", 160, 66, 1, WHITE, C_BAR);
      buttons(BUTTONS + 2, 3, 95, 94, 130, 0, 40);
      break;
    case S_QUIT:
      panel(80, 76, 160, 100);
      clab("Quit game?", 160, 92, 1, WHITE, C_BAR);
      buttons(BUTTONS + 5, 2, 94, 127, 62, 70, 0);
      break;
    case S_OVER:
      panel(64, 54, 192, 140);
      if (!pass) apple(104, 88, 36, 0, C_BAR), trophy(190, 88, 1.6f, C_BAR);
      itoa(t, score);
      lab(t, 126, 79, 1, WHITE, C_BAR);
      itoa(t, *best());
      lab(t, 212, 79, 1, WHITE, C_BAR);
      if (won || newbest) clab(won ? "You win!" : "New best!", 160, 114, 1, C_GOLD, C_BAR);
      buttons(BUTTONS + 7, 2, 78, 147, 80, 84, 0);
      break;
  }
}

/* Draws a part of the screen, band by band; text comes right after the band
   that holds its last row. */
static void area(int x, int y, int w, int h) {
  int band = BUFPX / w;
  for (int y0 = y; y0 < y + h; y0 += band) {
    rx = x, ry = y0, rw = w, rh = imin(band, y + h - y0);
    qx0 = fl(rx - fx), qx1 = fl(rx + rw - 1 - fx), qy0 = fl(ry - fy), qy1 = fl(ry + rh - 1 - fy);
    unclip();
    ground();
    items();
    snake();
    rings();
    pass = 0;
    ui();
    eadk_display_push_rect((eadk_rect_t){(uint16_t)rx, (uint16_t)ry, (uint16_t)rw, (uint16_t)rh}, buf);
    if (w == 320) pass = 1, ty0 = ry, ty1 = ry + rh, ui();
  }
}

/* In play: only the cells that change, and those that changed last time
   (so they are left in their final state). */
static uint16_t dcur[24], dprev[24];
static int ncur, nprev;
static void mark(int p) {
  for (int i = 0; i < ncur; i++)
    if (dcur[i] == p) return;
  if (ncur < 24) dcur[ncur++] = (uint16_t)p;
}
static void cells(void) {
  ncur = 0;
  for (int i = 0; i < 3 && i < len; i++) mark(body[(hd + MAXN - i) % MAXN]), mark(body[(tl + i) % MAXN]);
  if (tmove) mark(otail);
  for (int i = 0; i < 8; i++)
    if (ap[i].live && (ap[i].eat || (int)(now - ap[i].born) < 300)) mark(ap[i].p);
  if (wpop >= 0 && (int)(now - wborn) < 350) mark(wpop);
  int n = ncur;
  for (int i = 0; i < nprev; i++) mark(dprev[i]);
  for (int i = 0; i < ncur; i++) {
    int p = dcur[i];
    area(fx + (p & 255) * cs, fy + (p >> 8) * cs, cs, cs);
  }
  for (nprev = 0; nprev < n; nprev++) dprev[nprev] = dcur[nprev]; /* only this frame's own */
}

static void save(void) {
  V.magic = 'S', V.version = 1;
  ef_write(SAVE_NAME, &V, sizeof V);
}

/* the arrow keys, or 6 2 4 8, as directions */
static const uint32_t DIR_KEYS[4] = {KEY(eadk_key_right) | KEY(eadk_key_six), KEY(eadk_key_down) | KEY(eadk_key_two),
                                     KEY(eadk_key_left) | KEY(eadk_key_four), KEY(eadk_key_up) | KEY(eadk_key_eight)};

int main(void) {
  np_app_begin();
  now = eadk_timing_millis();
  seed = eadk_random() ^ now ^ 0x9E3779B9u;
  if (!seed) seed = 1;
  uint32_t n = 0;
  const uint8_t *d = ef_read(SAVE_NAME, &n);
  if (d && n == sizeof V && d[0] == 'S' && d[1] == 1) {
    for (uint32_t i = 0; i < n; i++) ((uint8_t *)&V)[i] = d[i];
    for (int i = 0; i < 9; i++)
      if (V.best[i] > MAXN) V.best[i] = 0;
  }
  for (int i = 0; i < 6; i++)
    if (!d || V.opt[i] >= OPT_N[i]) V.opt[i] = OPT_DEF[i];
  new_game();
  state = S_TITLE, scol = C_SNAKE;
  bool full = true, hdr = false, dirty = false;
  int resume = S_PLAY, flash = -1;
  uint32_t last = now, die_t = 0, rep_t = 0;
  uint32_t held = keys();
  for (;;) {
    now = eadk_timing_millis();
    int dt = (int)(now - last);
    dt = dt < 0 ? 0 : dt > 100 ? 100 : dt;
    last = now;
    uint32_t k = keys(), edge = k & ~held, hit = edge, arrows = DIR_KEYS[0] | DIR_KEYS[1] | DIR_KEYS[2] | DIR_KEYS[3];
    held = k;
    if (k & (KEY(eadk_key_home) | KEY(eadk_key_on_off))) break;
    int dk = -1; /* a direction, repeated while held (menus) */
    if (hit & arrows) rep_t = now + 350;
    else if ((k & arrows) && (int)(now - rep_t) >= 0) hit |= k & arrows, rep_t = now + 90;
    for (int i = 3; i >= 0; i--)
      if (hit & DIR_KEYS[i]) dk = i;
    bool ok = hit & (KEY(eadk_key_ok) | KEY(eadk_key_exe)), back = hit & KEY(eadk_key_back), redraw = true;
    switch (state) {
      case S_TITLE:
        if (dk == 1 || dk == 3) sel ^= 1;
        else if (ok) state = sel ? S_SET : S_READY, sel = 0;
        else if (back) resume = S_TITLE, state = S_QUIT, sel = 0; /* "Quit game?" from the title */
        else redraw = false;
        break;
      case S_SET:
        if (dk == 1 || dk == 3) {
          sel = (sel + (dk == 1 ? 1 : 5)) % 6;
        } else if (dk == 0 || dk == 2) {
          V.opt[sel] = (uint8_t)((V.opt[sel] + (dk ? OPT_N[sel] - 1 : 1)) % OPT_N[sel]);
          new_game(), dirty = true;
        } else if (ok || back) {
          state = S_TITLE, sel = 1;
          if (dirty) save(), dirty = false;
        } else {
          redraw = false;
        }
        break;
      case S_READY:
      case S_PLAY:
        redraw = false;
        if (back) {
          resume = state, state = S_PAUSE, sel = 0, redraw = true;
          break;
        }
        for (int i = 0; i < 4; i++) { /* up to two turns ahead, never straight back */
          int l = nq ? q[nq - 1] : ndir[hd];
          if ((edge & DIR_KEYS[i]) && nq < 2 && i != l && i != (l ^ 2)) q[nq++] = (uint8_t)i;
        }
        if (state == S_READY) {
          if (!nq && !(edge & (arrows | KEY(eadk_key_ok) | KEY(eadk_key_exe)))) break;
          state = S_PLAY, full = true;
        }
        int ate = score;
        bool crash = false, left = false;
        for (acc += dt; acc >= step_ms() && !crash; acc -= step_ms()) crash = !step();
        hdr |= score != ate;
        for (int i = 0; i < 8; i++) left |= ap[i].live && !ap[i].eat;
        if (crash || !left) { /* crashed, or no room left for an apple: the board is full */
          state = S_DIE, die_t = now, acc = step_ms(), dead = crash, won = !crash, newbest = score > *best();
          if (newbest) *best() = (uint16_t)score, save();
        }
        break;
      case S_PAUSE:
        if (dk == 1 || dk == 3) sel = (sel + (dk == 1 ? 1 : 2)) % 3;
        else if (back || (ok && !sel)) state = S_READY; /* it waits for a key: no surprise start */
        else if (ok && sel == 1) new_game(), state = S_READY;
        else if (ok) state = S_QUIT, sel = 0;
        else redraw = false;
        break;
      case S_QUIT:
        if (dk == 0 || dk == 2) sel ^= 1;
        else if (ok && sel) goto quit;
        else if (ok || back) state = resume == S_TITLE ? S_TITLE : S_PAUSE, sel = resume == S_TITLE ? 0 : 2;
        else redraw = false;
        break;
      case S_DIE:
        redraw = (int)(now - die_t) > 1100;
        if (redraw) state = S_OVER, sel = 0, scol = C_SNAKE;
        break;
      case S_OVER:
        if (dk == 0 || dk == 2) sel ^= 1;
        else if (ok || back) state = ok && !sel ? S_READY : S_TITLE, sel = 0, new_game();
        else redraw = false;
        break;
    }
    if (state != S_DIE && state != S_OVER) dead = won = false;
    full |= redraw;
    prog = (float)acc / step_ms();
    if (prog > 1) prog = 1;
    /* the mouth opens on an apple just ahead, and shuts after a bite */
    int h = body[hd], ahead = ((h & 255) + DX[ndir[hd]]) | ((h >> 8) + DY[ndir[hd]]) << 8;
    mouth = 1 - (int)(now - chew_t) / 160.0f;
    for (int i = 0; i < 8; i++)
      if (ap[i].live && (ap[i].p == h || (ap[i].p == ahead && !ap[i].eat && prog > mouth))) mouth = ap[i].p == h ? 1 : prog;
    if (dead) mouth = 0;
    eadk_display_wait_for_vblank();
    if (full) {
      area(0, 0, 320, 240);
      full = hdr = false, nprev = 0, flash = -1;
    } else if (state == S_DIE) {
      int f = (int)(now - die_t) / 150;
      if (f != flash && f < 7) { /* the snake flashes */
        flash = f, scol = f & 1 ? RGB(0xC9D8FF) : C_SNAKE;
        area(0, HDR, 320, 240 - HDR);
      }
    } else if (state == S_PLAY || state == S_READY) {
      if (hdr) area(0, 0, 320, HDR), hdr = false;
      cells();
    }
    uint32_t spent = (uint32_t)eadk_timing_millis() - now;
    if (spent < 16) eadk_timing_msleep(16 - spent);
  }
quit:
  if (dirty) save();
  return np_app_end();
}
