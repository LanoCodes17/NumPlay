/* Breakout: Atari's 1976 brick breaker, and an Arcade+ mode in the spirit of
 * Arkanoid (hand-made rounds, silver and gold bricks, power-up capsules).
 *
 * Nothing is drawn straight to the screen: whatever moves or changes marks
 * the rectangle it covers, and each marked rectangle is composed in a small
 * buffer (background, walls, bricks, paddle, balls, particles, text) and
 * pushed once. A frame costs a few thousand pixels and nothing flickers. */
#include <eadk.h>
#include <stdbool.h>
#include <stdint.h>
#include "../../common/epsilon_app.h"
#include "../../common/epsilon_files.h"

#ifdef __ELF__ /* app name and API level, for the calculator's installer */
const char eadk_app_name[] __attribute__((section(".rodata.eadk_app_name"))) = "Breakout";
const uint32_t eadk_api_level __attribute__((section(".rodata.eadk_api_level"))) = 0;
#endif

#define W 320
#define H 240
typedef uint16_t color;
#define RGB(c) (color)((((c) >> 8) & 0xF800) | (((c) >> 5) & 0x07E0) | (((c) >> 3) & 0x1F))
#define WHITE 0xFFFF
#define GRAY RGB(0x8E8E8E)
#define BLUE RGB(0x4A6CE8)
#define DIM RGB(0xB0B0B0)
#define KEY(k) (1ull << (k))

/* the playfield */
#define FX0 10 /* inside of the side walls */
#define FX1 310
#define TOP 28 /* the top wall, 8 pixels thick */
#define FY0 36
#define COLS 15
#define ROWS 12
#define CW 20 /* a brick's cell */
#define CH 8
#define BY0 52 /* the first row of bricks */
#define PY 222 /* the top of the paddle */
#define BS 5   /* the ball's size */
#define ONE 256 /* positions and speeds are in 256ths of a pixel */

static int imin(int a, int b) { return a < b ? a : b; }
static int imax(int a, int b) { return a > b ? a : b; }
static int iabs(int v) { return v < 0 ? -v : v; }

static uint32_t seed = 0x2545F491;
static uint32_t rnd(void) {
  seed ^= seed << 13;
  seed ^= seed >> 17;
  seed ^= seed << 5;
  return seed;
}
static int rndi(int n) { return (int)(rnd() % (uint32_t)n); }

/* f over b; a from 0 (all b) to 32 (all f) */
static color mix(color f, color b, int a) {
  uint32_t x = (f | (uint32_t)f << 16) & 0x07E0F81F, y = (b | (uint32_t)b << 16) & 0x07E0F81F;
  y = (y + ((x - y) * (uint32_t)a >> 5)) & 0x07E0F81F;
  return (color)(y | y >> 16);
}

/* ------------------------------------------------------------------ bricks */
enum {
  K_NONE, K_RED, K_ORANGE, K_GREEN, K_YELLOW, /* the classic wall */
  K_WHITE, K_ORANGE2, K_CYAN, K_GREEN2, K_RED2, K_BLUE, K_PINK, K_YELLOW2, K_SILVER, K_GOLD, K_RAINBOW
};
static const color kcol[15] = {
  0, RGB(0xC84848), RGB(0xC66C3A), RGB(0x48A048), RGB(0xA2A22A),
  RGB(0xF4F4F4), RGB(0xFF8A1C), RGB(0x30D8F0), RGB(0x3CD040), RGB(0xF03C3C), RGB(0x3868F8), RGB(0xF058D0),
  RGB(0xF8E040), RGB(0xA8B0BC), RGB(0xD8A030)};
/* a round's "rainbow" bricks take their colour from their row */
static const uint8_t rainbow[ROWS] = {K_WHITE, K_ORANGE2, K_RED2, K_YELLOW2, K_BLUE, K_PINK,
                                      K_GREEN2, K_CYAN, K_ORANGE2, K_RED2, K_YELLOW2, K_BLUE};

/* Arcade+ rounds, all symmetric: a row is its 8 left cells (the 8th is the
   middle one), 2 bits each, picking one of the round's 3 kinds. Q() reads
   them as digits after a leading 1. */
#define QD(n, p) ((n) / (p) % 10)
#define Q(n) (uint16_t)(QD(n, 10000000) | QD(n, 1000000) << 2 | QD(n, 100000) << 4 | QD(n, 10000) << 6 | \
                         QD(n, 1000) << 8 | QD(n, 100) << 10 | QD(n, 10) << 12 | QD(n, 1) << 14)
#define PAL(a, b, c) (uint16_t)((a) | (b) << 4 | (c) << 8)
typedef struct {
  uint16_t pal, row[ROWS];
} round_t;
static const round_t rounds[] = {
  {PAL(K_RAINBOW, K_SILVER, 0), /* welcome */
   {0, Q(122222222), Q(111111111), Q(111111111), Q(111111111), Q(111111111), Q(111111111)}},
  {PAL(K_RAINBOW, K_GOLD, 0), /* pyramid */
   {Q(100000001), Q(100000011), Q(100000111), Q(100001111), Q(100011111), Q(100111111), Q(101111111),
    Q(111111111), 0, Q(120002000)}},
  {PAL(K_CYAN, K_PINK, K_SILVER), /* checkers */
   {0, Q(133333333), Q(112121212), Q(121212121), Q(112121212), Q(121212121), Q(112121212), Q(121212121)}},
  {PAL(K_GREEN2, K_SILVER, 0), /* invader */
   {0, Q(100001000), Q(100000100), Q(100001111), Q(100011011), Q(100111111), Q(100101111), Q(100101000),
    Q(100000110), 0, Q(102200220)}},
  {PAL(K_RAINBOW, K_SILVER, K_GOLD), /* gates */
   {0, Q(111111111), Q(111111111), 0, Q(133333302), 0, Q(111111111), 0, Q(130333333)}},
  {PAL(K_RAINBOW, K_SILVER, K_GOLD), /* diamond */
   {Q(130000002), Q(100000021), Q(100000211), Q(100002111), Q(100021111), Q(100002111), Q(100000211),
    Q(100000021), Q(130000002)}},
  {PAL(K_BLUE, K_SILVER, K_GOLD), /* pillars */
   {Q(122222222), Q(110101010), Q(110101010), Q(110101010), Q(110101010), Q(110101010), Q(110101010),
    Q(110101010), Q(130003000)}},
  {PAL(K_CYAN, K_PINK, K_YELLOW2), /* chevrons */
   {0, Q(112312312), Q(123123123), Q(131231231), Q(112312312), Q(123123123), Q(131231231), Q(112312312),
    Q(123123123)}},
  {PAL(K_RED2, K_SILVER, K_GOLD), /* fortress */
   {Q(111111111), 0, Q(100333333), Q(100311111), Q(100312221), Q(100312221), Q(100311111), Q(100333000), 0,
    Q(111000111)}},
  {PAL(K_BLUE, K_ORANGE2, K_SILVER), /* target */
   {0, Q(111111111), Q(112222222), Q(112111111), Q(112133333), Q(112131111), Q(112133333), Q(112111111),
    Q(112222222), Q(111111111)}},
  {PAL(K_RED2, K_PINK, K_GOLD), /* heart */
   {Q(130000000), Q(100022200), Q(100211110), Q(100111111), Q(100111111), Q(100011111), Q(100001111),
    Q(100000111), Q(100000011), Q(100000001), Q(130000000)}},
  {PAL(K_RAINBOW, K_SILVER, K_GOLD), /* the last one */
   {Q(122222222), Q(111111111), Q(131111111), Q(122222222), Q(111111111), Q(111133111), Q(111111111),
    Q(122222222), 0, Q(130303030)}},
};
#define NROUNDS (int)(sizeof rounds / sizeof rounds[0])

/* a brick: its kind (4 bits), hits left (2 bits), and a flash when hit (2 bits) */
static uint8_t brick[ROWS][COLS];

/* ------------------------------------------------------------------ the game */
enum { CLASSIC, ARCADE };
enum { S_TITLE, S_SET, S_READY, S_PLAY, S_LOST, S_CLEAR, S_PAUSE, S_QUIT, S_OVER };
enum { P_NONE, P_ENLARGE, P_CATCH, P_LASER };
/* capsules: Slow, Catch, Laser, Enlarge, Disruption (three balls), Player (a
   life) and Break (a way out to the next round), in Arkanoid's colours */
static const char cap_letter[7] = {16, 11, 14, 13, 12, 15, 10}; /* in mini[] */
static const color cap_col[7] = {RGB(0xF08C20), RGB(0x38C848), RGB(0xE83838), RGB(0x3C6CF0), RGB(0x30C8E8),
                                 RGB(0x9098A8), RGB(0xE858C8)};
static const uint8_t cap_odds[7] = {20, 18, 18, 20, 14, 4, 6};

typedef struct {
  int32_t x, y, vx, vy;
  int16_t off;          /* where it sits on the paddle when caught, in pixels */
  int16_t tx[3], ty[3]; /* the trail */
  uint8_t on, stuck;
} ball_t;
typedef struct {
  int16_t x, y; /* in 16ths of a pixel */
  int8_t vx, vy;
  color c;
  uint8_t life, max;
} part_t;
typedef struct {
  int16_t x, y;
  uint8_t on, type;
} thing_t; /* a capsule or a laser shot */

#define NBALL 3
#define NPART 48
#define NSHOT 6
static ball_t ball[NBALL];
static part_t part[NPART];
static thing_t cap, shot[NSHOT];
static int mode, state, resume_state, st_t, tk, sel, tsel, round_no, start_round, bricks_left, rows_shown;
static int32_t score;
static int lives, wall, hits, power, laser_cd, catch_t, warp;
static bool demo, got_orange, got_red, shrunk, won, fresh_best, full;
static int px, pv, spd, spd0; /* the paddle's centre and speed, the balls' speed (in 256ths) */
static int pw, pw_to;         /* the paddle's width, and the width it is going to */
static int demo_aim;          /* where the self-playing paddle aims */
/* the shake: time left, strength, and the offsets of the bricks (bx, by) and of
   everything else (sx, sy), which only moves in a big shake */
static int shake_t, shake_a, bx, by, sx, sy;
static bool shake_all;

/* ------------------------------------------------------------------ save */
static struct {
  uint8_t magic, version, reached, diff, shake, trail, pad[2];
  uint32_t best[2];
} sv, saved;
#define SAVE_NAME "breakout.sav"

static void save(void) {
  const uint8_t *a = (const uint8_t *)&sv, *b = (const uint8_t *)&saved;
  unsigned i = 0;
  while (i < sizeof sv && a[i] == b[i]) i++;
  if (i < sizeof sv && ef_write(SAVE_NAME, &sv, sizeof sv)) saved = sv;
}
static void load(void) {
  uint32_t n = 0;
  const uint8_t *d = ef_read(SAVE_NAME, &n);
  sv.magic = 'B', sv.version = 1, sv.reached = 1, sv.diff = 1, sv.shake = 1, sv.trail = 1;
  if (d && n == sizeof sv && d[0] == 'B' && d[1] == 1 && d[2] >= 1 && d[2] <= NROUNDS && d[3] < 3 && d[4] < 2 &&
      d[5] < 2) {
    for (uint32_t i = 0; i < n; i++) ((uint8_t *)&sv)[i] = d[i];
    for (int m = 0; m < 2; m++)
      if (sv.best[m] > 999999) sv.best[m] = 0;
    saved = sv;
  }
  start_round = sv.reached;
}

/* ------------------------------------------------------------------ dirty rectangles */
typedef struct {
  int16_t x0, y0, x1, y1;
} rect_t;
#define NDIRTY 40
static rect_t dirty[NDIRTY];
static int ndirty;

/* marks an area to redraw, merged with another when that wastes little
   (overlapping areas are simply drawn twice) */
static void mark(int x0, int y0, int x1, int y1) {
  x0 = imax(x0, 0), y0 = imax(y0, 0), x1 = imin(x1, W), y1 = imin(y1, H);
  if (x0 >= x1 || y0 >= y1) return;
  for (int i = 0; i < ndirty;) {
    rect_t *d = &dirty[i];
    int u0 = imin(x0, d->x0), v0 = imin(y0, d->y0), u1 = imax(x1, d->x1), v1 = imax(y1, d->y1);
    if ((u1 - u0) * (v1 - v0) <= (x1 - x0) * (y1 - y0) + (d->x1 - d->x0) * (d->y1 - d->y0) + 256) {
      x0 = u0, y0 = v0, x1 = u1, y1 = v1;
      *d = dirty[--ndirty];
      i = 0;
    } else {
      i++;
    }
  }
  if (ndirty == NDIRTY) { /* too many: the last one takes this one in */
    rect_t *d = &dirty[NDIRTY - 1];
    x0 = imin(x0, d->x0), y0 = imin(y0, d->y0), x1 = imax(x1, d->x1), y1 = imax(y1, d->y1);
    ndirty--;
  }
  dirty[ndirty++] = (rect_t){(int16_t)x0, (int16_t)y0, (int16_t)x1, (int16_t)y1};
}
/* something in the playfield, which shakes */
static void mark_at(int x, int y, int w, int h) { mark(x + sx, y + sy, x + sx + w, y + sy + h); }
static void mark_cell(int r, int c) { /* however it shakes, and its shadow */
  int x = FX0 + c * CW, y = BY0 + r * CH;
  mark(x - 4, y - 4, x + CW + 7, y + CH + 7);
}

/* ------------------------------------------------------------------ drawing */
#define BUFN (W * 16)
static color buf[BUFN];
static int rx0, ry0, rx1, ry1, rw; /* the area buf holds */
static int cx0, cx1;                /* and the columns drawing may touch */

static void rect(int x, int y, int w, int h, color c, int a) {
  int x0 = imax(x, cx0), x1 = imin(x + w, cx1), y0 = imax(y, ry0), y1 = imin(y + h, ry1);
  for (int j = y0; j < y1; j++)
    for (color *p = buf + (j - ry0) * rw + x0 - rx0, *e = p + x1 - x0; p < e; p++) *p = a >= 32 ? c : mix(c, *p, a);
}
static __attribute__((noinline)) void fill(int x, int y, int w, int h, color c) { rect(x, y, w, h, c, 32); }
static bool seen(int x, int y, int w, int h) { return x < rx1 && x + w > rx0 && y < ry1 && y + h > ry0; }

/* ASCII 32..90 (lowercase is drawn as capitals), columns of 7 bits, top bit first */
static const uint8_t font[59][5] = {
  {0x00,0x00,0x00,0x00,0x00},{0x00,0x00,0x5F,0x00,0x00},{0x00,0x07,0x00,0x07,0x00},{0x14,0x7F,0x14,0x7F,0x14},
  {0x24,0x2A,0x7F,0x2A,0x12},{0x23,0x13,0x08,0x64,0x62},{0x36,0x49,0x55,0x22,0x50},{0x00,0x05,0x03,0x00,0x00},
  {0x00,0x1C,0x22,0x41,0x00},{0x00,0x41,0x22,0x1C,0x00},{0x14,0x08,0x3E,0x08,0x14},{0x08,0x08,0x3E,0x08,0x08},
  {0x00,0x50,0x30,0x00,0x00},{0x08,0x08,0x08,0x08,0x08},{0x00,0x60,0x60,0x00,0x00},{0x20,0x10,0x08,0x04,0x02},
  {0x3E,0x51,0x49,0x45,0x3E},{0x00,0x42,0x7F,0x40,0x00},{0x42,0x61,0x51,0x49,0x46},{0x21,0x41,0x45,0x4B,0x31},
  {0x18,0x14,0x12,0x7F,0x10},{0x27,0x45,0x45,0x45,0x39},{0x3C,0x4A,0x49,0x49,0x30},{0x01,0x71,0x09,0x05,0x03},
  {0x36,0x49,0x49,0x49,0x36},{0x06,0x49,0x49,0x29,0x1E},{0x00,0x36,0x36,0x00,0x00},{0x00,0x56,0x36,0x00,0x00},
  {0x08,0x14,0x22,0x41,0x00},{0x14,0x14,0x14,0x14,0x14},{0x00,0x41,0x22,0x14,0x08},{0x02,0x01,0x51,0x09,0x06},
  {0x32,0x49,0x79,0x41,0x3E},{0x7E,0x11,0x11,0x11,0x7E},{0x7F,0x49,0x49,0x49,0x36},{0x3E,0x41,0x41,0x41,0x22},
  {0x7F,0x41,0x41,0x22,0x1C},{0x7F,0x49,0x49,0x49,0x41},{0x7F,0x09,0x09,0x09,0x01},{0x3E,0x41,0x49,0x49,0x7A},
  {0x7F,0x08,0x08,0x08,0x7F},{0x00,0x41,0x7F,0x41,0x00},{0x20,0x40,0x41,0x3F,0x01},{0x7F,0x08,0x14,0x22,0x41},
  {0x7F,0x40,0x40,0x40,0x40},{0x7F,0x02,0x0C,0x02,0x7F},{0x7F,0x04,0x08,0x10,0x7F},{0x3E,0x41,0x41,0x41,0x3E},
  {0x7F,0x09,0x09,0x09,0x06},{0x3E,0x41,0x51,0x21,0x5E},{0x7F,0x09,0x19,0x29,0x46},{0x46,0x49,0x49,0x49,0x31},
  {0x01,0x01,0x7F,0x01,0x01},{0x3F,0x40,0x40,0x40,0x3F},{0x1F,0x20,0x40,0x20,0x1F},{0x3F,0x40,0x38,0x40,0x3F},
  {0x63,0x14,0x08,0x14,0x63},{0x07,0x08,0x70,0x08,0x07},{0x61,0x51,0x49,0x45,0x43},
};
/* 3x5 blocky figures, like the arcade's score, then the capsule letters
   B C D E L P S: five rows of three bits (an octal digit each) */
static const uint16_t mini[17] = {075557, 026227, 071747, 071717, 055711, 074717, 074757, 071111, 075757,
                                  075717, 065656, 074447, 065556, 074647, 044447, 065644, 034216};

static int glyph_of(char ch) {
  if (ch >= 'a' && ch <= 'z') ch = (char)(ch - 32);
  return ch < 32 || ch > 'Z' ? '?' - 32 : ch - 32;
}
static int slen(const char *s) {
  int n = 0;
  while (s[n]) n++;
  return n;
}
static int tw(const char *s, int k) { return slen(s) * 6 * k - k; }
/* text from x, with a dark shadow so it reads over anything */
static void text(const char *s, int x, int y, int k, color c) {
  if (!seen(x, y, tw(s, k) + k, 8 * k)) return;
  for (int pass = 0; pass < 2; pass++)
    for (int n = 0; s[n]; n++) {
      const uint8_t *g = font[glyph_of(s[n])];
      int o = pass ? 0 : (k + 1) / 2;
      for (int i = 0; i < 5; i++)
        for (int j = 0, b = g[i]; b; j++, b >>= 1)
          if (b & 1) rect(x + n * 6 * k + i * k + o, y + j * k + o, k, k, pass ? c : 0, pass ? 32 : 20);
    }
}
static void ctext(const char *s, int y, int k, color c) { text(s, 160 - tw(s, k) / 2, y, k, c); } /* centred */

static void mini_glyph(int g, int x, int y, int kx, int ky, color c) {
  for (int j = 0; j < 5; j++)
    for (int i = 0; i < 3; i++)
      if (mini[g] >> (14 - j * 3 - i) & 1) fill(x + i * kx, y + j * ky, kx, ky, c);
}
static char *cat(char *o, const char *t) {
  while ((*o = *t++)) o++;
  return o;
}
static char *itoa_(char *o, int32_t v) {
  char t[12];
  int n = 0;
  do t[n++] = (char)('0' + v % 10); while (v /= 10);
  while (n) *o++ = t[--n];
  *o = 0;
  return o;
}
/* a number in blocky figures along the top, at least `pad` of them, from x
   (or ending at x when right-aligned) */
static void figures(int32_t v, int pad, int x, color c, bool right) {
  char s[12];
  int n = (int)(itoa_(s, v) - s), z = imax(pad - n, 0), all = n + z;
  if (right) x -= all * 16 - 4;
  for (int i = 0; i < all; i++) mini_glyph(i < z ? 0 : s[i - z] - '0', x + i * 16, 4, 4, 4, c);
}

/* ------------------------------------------------------------------ the scene */
static color bg_lo, bg_mid, bg_hi; /* Arcade+ backgrounds, a colour per round */
static bool fancy(void) { return mode == ARCADE && !demo; }

static void background(void) {
  if (!fancy()) {
    for (color *p = buf, *e = buf + rw * (ry1 - ry0); p < e; p++) *p = 0;
    return;
  }
  /* bevelled tiles, turned into diamonds every other round: a row of one
     tile, repeated */
  for (int y = ry0; y < ry1; y++) {
    color *p = buf + (y - ry0) * rw, t[16];
    int v = (y - FY0) & 15;
    for (int u = 0; u < 16; u++) {
      int d = iabs(u - 8) + iabs(v - 8);
      t[u] = round_no & 1 ? (d == 8 ? bg_hi : d == 7 || d < 2 ? bg_lo : bg_mid)
                          : !u || !v ? bg_hi : u == 15 || v == 15 ? bg_lo : bg_mid;
    }
    for (int x = rx0; x < rx1; x++) *p++ = y >= FY0 && x >= FX0 && x < FX1 ? t[(x - FX0) & 15] : 0;
  }
}

static color pipe_shade(int i, int n) { /* a metal pipe, lit from the top left */
  int t = i * 2 - n / 2;
  return mix(RGB(0xECF0FA), RGB(0x2A3244), imin(iabs(t) * 32 / n + 2, 32));
}
static void walls(void) {
  int ox = sx, oy = sy;
  if (!fancy()) {
    fill(ox, oy + TOP, W, 8, GRAY);
    for (int e = 0; e <= FX1; e += FX1) /* both sides, with the arcade's blue strip where the paddle plays */
      fill(ox + e, oy + TOP, FX0, H - TOP, GRAY), fill(ox + e, oy + PY - 8, FX0, 22, BLUE);
    return;
  }
  for (int i = 0; i < 8; i++) fill(ox, oy + TOP + i, W, 1, pipe_shade(i, 8));
  for (int x = 40; x < W; x += 80) rect(ox + x, oy + TOP, 3, 8, 0, 14);
  for (int e = 0; e <= FX1; e += FX1) {
    for (int i = 0; i < FX0; i++) fill(ox + e + i, oy + FY0, 1, H - FY0, pipe_shade(i, FX0));
    for (int y = FY0 + 36; y < H; y += 56) /* joints, with a lamp */
      rect(ox + e, oy + y, FX0, 3, 0, 14), fill(ox + e + 3, oy + y + 7, 4, 10, RGB(0x2A60D0));
  }
  if (warp) { /* the way out, blinking */
    fill(ox + FX1, oy + PY - 10, FX0, 22, 0);
    for (int j = 0; j < 3; j++)
      rect(ox + FX1 + 2, oy + PY - 7 + j * 6, 6, 3, RGB(0xF058D0), ((tk >> 3) + j) % 3 ? 10 : 32);
  }
}

static void draw_brick(int r, int c, int x, int y) {
  uint8_t v = brick[r][c];
  int k = v & 15, f = v >> 6;
  color col = kcol[k];
  if (!fancy()) {
    fill(x, y + 1, CW - 2, CH - 2, col);
    return;
  }
  fill(x, y, CW - 1, CH - 1, col);
  rect(x, y, CW - 1, 1, WHITE, k >= K_SILVER ? 22 : 14);
  rect(x, y + 1, 1, CH - 2, WHITE, 8);
  rect(x + 1, y + CH - 2, CW - 2, 1, 0, 12);
  rect(x + CW - 2, y + 1, 1, CH - 2, 0, 10);
  if (k >= K_SILVER) rect(x + 3, y + 2, 3, 2, WHITE, 24); /* a shine */
  if (f) rect(x + (3 - f) * 6, y, 6, CH - 1, WHITE, 24); /* a glint runs across when hit */
}
static void bricks(void) {
  int ox = bx + FX0, oy = by + BY0;
  int r0 = imax((ry0 - oy - CH - 3) / CH, 0), r1 = imin((ry1 - oy) / CH + 1, rows_shown);
  int c0 = imax((rx0 - ox - CW - 3) / CW, 0), c1 = imin((rx1 - ox) / CW + 1, COLS);
  if (ry1 <= oy - 4) return;
  cx0 = imax(rx0, FX0), cx1 = imin(rx1, FX1); /* shaken bricks slide under the walls */
  if (fancy()) /* shadows first, so the bricks cover the ones that fall on bricks */
    for (int r = r0; r < r1; r++)
      for (int c = c0; c < c1; c++)
        if (brick[r][c]) rect(ox + c * CW + 3, oy + r * CH + 3, CW - 1, CH - 1, 0, 15);
  for (int r = r0; r < r1; r++)
    for (int c = c0; c < c1; c++)
      if (brick[r][c]) draw_brick(r, c, ox + c * CW, oy + r * CH);
  cx0 = rx0, cx1 = rx1;
}

static bool paddle_shown(void) { return state != S_LOST && (state != S_OVER || won); }
static void paddle(void) {
  if (!paddle_shown()) return;
  int x = ((px + ONE / 2) >> 8) - pw / 2 + sx, y = PY + sy;
  if (!seen(x, y - 3, pw, 10)) return;
  if (!fancy()) {
    fill(x, y, pw, 4, BLUE);
    return;
  }
  /* the Vaus: a silver capsule with red ends, lit from above */
  static const uint8_t inset[7] = {2, 1, 0, 0, 0, 1, 2}, lit[7] = {20, 31, 25, 18, 12, 7, 3};
  for (int j = 0; j < 7; j++) {
    int i = inset[j];
    color red = mix(RGB(0xFF6060), RGB(0x400808), lit[j]);
    fill(x + i, y + j, pw - 2 * i, 1, mix(WHITE, RGB(0x303848), lit[j]));
    fill(x + i, y + j, 7 - i, 1, red);
    fill(x + pw - 7, y + j, 7 - i, 1, red);
  }
  rect(x + 7, y + 1, 1, 5, 0, 12);
  rect(x + pw - 8, y + 1, 1, 5, 0, 12);
  rect(x + pw / 2 - 3, y + 2, 6, 3, RGB(0x40A0F8), power == P_CATCH ? 32 : 12);
  if (power == P_LASER) { /* two cannons */
    fill(x + 2, y - 3, 3, 3, RGB(0x505868));
    fill(x + pw - 5, y - 3, 3, 3, RGB(0x505868));
  }
}

/* in the arcade, colour came from strips of film on the screen: the ball
   takes the colour of the rows it flies through */
static color ball_col(int y) {
  if (fancy()) return RGB(0xE8F0FF);
  if (y + 2 >= PY - 8) return BLUE;
  int r = (y + 2 - BY0) / CH;
  if (!demo && y + 2 >= BY0 + CH && r <= 8) return kcol[(r + 1) / 2];
  return WHITE;
}
static void balls(void) {
  for (int i = 0; i < NBALL; i++) {
    ball_t *b = &ball[i];
    if (!b->on) continue;
    if (sv.trail && !b->stuck)
      for (int t = 2; t >= 0; t--) {
        int s = BS - 1 - t / 2;
        rect(b->tx[t] + sx + (BS - s) / 2, b->ty[t] + sy + (BS - s) / 2, s, s, ball_col(b->ty[t]), 16 - t * 5);
      }
    int x = (b->x >> 8) + sx, y = (b->y >> 8) + sy;
    color c = ball_col(b->y >> 8);
    if (!fancy()) {
      fill(x, y, BS, BS, c);
    } else {
      fill(x + 1, y, BS - 2, BS, c);
      fill(x, y + 1, BS, BS - 2, c);
      fill(x + BS - 2, y + BS - 2, 1, 1, RGB(0x8090B0));
      fill(x + 1, y + 1, 1, 1, WHITE);
    }
  }
}

static void things(void) {
  if (cap.on) { /* a capsule: a little lit pill with its letter, and a shine going round */
    static const uint8_t inset[8] = {2, 1, 0, 0, 0, 0, 1, 2}, lit[8] = {14, 8, 0, 0, 4, 9, 14, 18};
    int x = cap.x + sx, y = cap.y + sy, sh = (st_t >> 1) & 31;
    color c = cap_col[cap.type];
    for (int j = 0; j < 8; j++)
      fill(x + inset[j], y + j, 16 - 2 * inset[j], 1, j < 2 ? mix(WHITE, c, lit[j]) : mix(0, c, lit[j]));
    if (sh < 14) rect(x + 1 + sh, y + 1, 2, 6, WHITE, 12);
    mini_glyph(cap_letter[cap.type], x + 7, y + 2, 1, 1, mix(0, c, 20));
    mini_glyph(cap_letter[cap.type], x + 6, y + 1, 1, 1, WHITE);
  }
  for (int i = 0; i < NSHOT; i++)
    if (shot[i].on) {
      fill(shot[i].x + sx, shot[i].y + sy, 2, 7, RGB(0xFF5030));
      fill(shot[i].x + sx, shot[i].y + sy, 2, 3, RGB(0xFFF0A0));
    }
  for (int i = 0; i < NPART; i++) {
    part_t *p = &part[i];
    if (p->life) rect((p->x >> 4) + sx, (p->y >> 4) + sy, 2, 2, p->c, 8 + 24 * p->life / p->max);
  }
}

static void hud(void) {
  if (ry0 >= TOP) return;
  int32_t best = (int32_t)sv.best[mode] > score ? (int32_t)sv.best[mode] : score;
  if (mode == CLASSIC) { /* score, ball, best: like the arcade's two scores and ball; the
                            title shows the last game's score, as arcades do */
    figures(score, 3, 18, GRAY, false);
    if (!demo) figures(4 - imax(lives, 1), 1, 154, GRAY, false);
    figures(best, 3, 302, mix(GRAY, 0, 18), true);
    return;
  }
  char s[12] = "HI ";
  figures(score, 1, 18, WHITE, false);
  int x = 160 - (round_no >= 9 ? 31 : 23);
  text("ROUND", x, 17, 1, RGB(0xF05050));
  figures(round_no + 1, 1, x + 34, RGB(0xF05050), false);
  itoa_(s + 3, best);
  text(s, 302 - tw(s, 1), 4, 1, RGB(0xA0A0A0));
  for (int i = 0; i < imin(lives - 1, 6); i++) { /* spare lives, as little paddles */
    int x = 294 - i * 13;
    fill(x, 16, 10, 4, RGB(0xB8C0CC));
    fill(x, 16, 2, 4, RGB(0xE03030));
    fill(x + 8, 16, 2, 4, RGB(0xE03030));
  }
}

/* ------------------------------------------------------------------ overlays */
static const char *const diff_names[3] = {"EASY", "NORMAL", "HARD"};

/* where each screen's text is, so a change redraws only that */
static void ui_area(int s) {
  if (s <= S_SET) mark(20, 100, 300, 214);
  else if (s < S_PAUSE) mark(40, 146, 280, 196); /* the banner */
  else full = true;                               /* a menu over the dimmed game */
}

static void panel(int x, int y, int w, int h) {
  fill(x, y, w, h, GRAY);
  fill(x + 3, y + 3, w - 6, h - 6, 0);
}
static void item(const char *s, int cy, bool on) {
  if (on) {
    fill(100, cy - 4, 120, 22, BLUE);
  }
  ctext(s, cy, 2, on ? WHITE : DIM);
}

static void logo(void) {
  /* BREAKOUT in bold letters built of bricks, in the four colours of the wall */
  static const char name[] = "BREAKOUT";
  int x0 = 23, y0 = 46;
  if (!seen(x0, y0, 275, 42)) return;
  for (int n = 0; n < 8; n++) {
    const uint8_t *g = font[name[n] - 32];
    for (int i = 0; i < 6; i++) {
      int b = (i < 5 ? g[i] : 0) | (i ? g[i - 1] : 0);
      for (int j = 0; j < 7; j++)
        if (b >> j & 1) fill(x0 + (n * 7 + i) * 5, y0 + j * 6, 5, 5, kcol[1 + j * 4 / 7]);
    }
  }
  for (int j = 0; j < 7; j++) /* the mortar between bricks */
    for (int x = x0 + (j & 1) * 5 + 9; x < x0 + 275; x += 10) fill(x, y0 + j * 6, 1, 5, 0);
}

static void overlay(void) {
  char s[24];
  if (state == S_TITLE || state == S_SET) {
    logo();
    if (state == S_TITLE) {
      static const char *const items[3] = {"CLASSIC", "ARCADE+", "SETTINGS"};
      for (int i = 0; i < 3; i++) item(items[i], 108 + i * 28, tsel == i);
      char *o = s;
      if (tsel == 1) /* the round to start from, among those reached */
        o = cat(itoa_(cat(s, sv.reached > 1 ? "< ROUND " : "ROUND "), start_round), sv.reached > 1 ? " >   " : "   ");
      itoa_(cat(o, "BEST "), (int32_t)sv.best[tsel & 1]);
      if (tsel < 2) ctext(s, 198, 1, DIM);
    } else {
      static const char *const names[3] = {"DIFFICULTY", "SCREEN SHAKE", "BALL TRAIL"};
      for (int i = 0; i < 3; i++) {
        color c = sel == i ? WHITE : DIM;
        if (sel == i) fill(24, 104 + i * 28, 272, 22, BLUE);
        text(names[i], 32, 108 + i * 28, 2, c);
        const char *v = i == 0 ? diff_names[sv.diff] : (i == 1 ? sv.shake : sv.trail) ? "ON" : "OFF";
        text(v, 288 - tw(v, 2), 108 + i * 28, 2, c);
      }
      ctext("LEFT/RIGHT: CHANGE   BACK: DONE", 198, 1, DIM);
    }
    return;
  }
  if (state == S_CLEAR) ctext(mode == CLASSIC ? "WALL CLEARED" : warp ? "WARP!" : "ROUND CLEAR", 152, 2, WHITE);
  if (state == S_READY && !demo) {
    itoa_(cat(s, mode == CLASSIC ? (wall == 2 ? "WALL 2  BALL " : "BALL ") : "ROUND "),
          mode == CLASSIC ? 4 - lives : round_no + 1);
    ctext(s, 152, 2, WHITE);
    if (st_t > 20) ctext("OK: LAUNCH", 176, 1, DIM);
  }
  if (state == S_PAUSE) {
    static const char *const items[3] = {"RESUME", "RESTART", "QUIT GAME"};
    panel(84, 62, 152, 116);
    ctext("PAUSED", 74, 2, kcol[K_YELLOW]);
    for (int i = 0; i < 3; i++) item(items[i], 104 + i * 24, sel == i);
  } else if (state == S_QUIT) {
    panel(84, 74, 152, 92);
    ctext("QUIT GAME?", 88, 2, WHITE);
    for (int i = 0; i < 2; i++) {
      int x = 102 + i * 62;
      if (sel == i) fill(x, 124, 54, 24, i ? RGB(0xC84848) : BLUE);
      text(i ? "YES" : "NO", x + 16 - i * 6, 129, 2, sel == i ? WHITE : DIM);
    }
  } else if (state == S_OVER) {
    panel(50, 50, 220, 140);
    ctext(won ? (mode == CLASSIC ? "YOU WIN!" : "ALL CLEAR!") : "GAME OVER", 64, 3, won ? RGB(0xA2E22A) : RGB(0xE05050));
    itoa_(cat(s, "SCORE "), score);
    ctext(s, 104, 2, WHITE);
    itoa_(cat(s, "BEST "), (int32_t)sv.best[mode]);
    if (fresh_best) cat(s, st_t & 16 ? "NEW BEST!" : "");
    ctext(s, 128, 2, fresh_best ? kcol[K_YELLOW2] : DIM);
    ctext("OK: PLAY AGAIN   BACK: MENU", 166, 1, DIM);
  }
}

/* one area of the screen, in layers */
static void render(void) {
  background();
  walls();
  bricks();
  things();
  paddle();
  balls();
  hud();
  if (state >= S_PAUSE) /* dimmed under a menu */
    for (color *p = buf, *e = buf + rw * (ry1 - ry0); p < e; p++) *p = (color)((*p >> 1 & 0x7BEF) + (*p >> 2 & 0x39E7));
  overlay();
}

static void flush(void) {
  if (full) ndirty = 0, mark(0, 0, W, H), full = false;
  for (int i = 0; i < ndirty; i++) {
    rect_t r = dirty[i];
    int w = r.x1 - r.x0, band = BUFN / w;
    for (int y = r.y0; y < r.y1; y += band) {
      cx0 = rx0 = r.x0, cx1 = rx1 = r.x1, ry0 = y, ry1 = imin(y + band, r.y1), rw = w;
      render();
      eadk_display_push_rect((eadk_rect_t){(uint16_t)rx0, (uint16_t)ry0, (uint16_t)w, (uint16_t)(ry1 - ry0)}, buf);
    }
  }
  ndirty = 0;
}

/* everything that moves, where it is now (called before and after moving) */
static int paddle_key, paddle_mx;
static void mark_moving(void) {
  for (int i = 0; i < NBALL; i++) {
    ball_t *b = &ball[i];
    if (!b->on) continue;
    int x0 = b->x >> 8, y0 = b->y >> 8, x1 = x0 + BS, y1 = y0 + BS;
    if (sv.trail)
      for (int t = 0; t < 3; t++)
        x0 = imin(x0, b->tx[t]), y0 = imin(y0, b->ty[t]), x1 = imax(x1, b->tx[t] + BS), y1 = imax(y1, b->ty[t] + BS);
    mark_at(x0, y0, x1 - x0, y1 - y0);
  }
  if (cap.on) mark_at(cap.x, cap.y, 16, 8);
  for (int i = 0; i < NSHOT; i++)
    if (shot[i].on) mark_at(shot[i].x, shot[i].y, 2, 7);
  for (int i = 0; i < NPART; i++)
    if (part[i].life) mark_at(part[i].x >> 4, part[i].y >> 4, 2, 2);
  /* the paddle, only when it changed: where it was drawn, and where it is */
  int x = (px + ONE / 2) >> 8, key = x | pw << 9 | power << 16 | paddle_shown() << 18 | (sx + 8) << 20 | (sy + 8) << 25;
  if (key != paddle_key) {
    x -= pw / 2;
    mark(paddle_mx, PY - 3 - 4, paddle_mx + (paddle_key >> 9 & 127) + 8, PY + 7 + 4);
    mark_at(x, PY - 3, pw, 10);
    paddle_key = key, paddle_mx = x + sx - 4;
  }
}

/* ------------------------------------------------------------------ effects */
/* n sparks flying out of an area; f: how hard, in 16ths of a pixel a tick */
static void burst(int x, int y, int w, int h, color c, int n, int f) {
  static int next;
  while (n--) {
    part_t *p = &part[next];
    next = (next + 1) % NPART;
    if (p->life) mark_at(p->x >> 4, p->y >> 4, 2, 2);
    p->x = (int16_t)((x + rndi(w)) * 16), p->y = (int16_t)((y + rndi(h)) * 16);
    p->vx = (int8_t)(rndi(2 * f + 1) - f), p->vy = (int8_t)(rndi(2 * f + 1) - f * 13 / 10);
    p->c = c;
    p->max = p->life = (uint8_t)(16 + rndi(20));
  }
}
static void shake(int a, int t) {
  if (!sv.shake) return;
  shake_a = imax(shake_a, a), shake_t = imax(shake_t, t), shake_all |= a > 2;
}

/* ------------------------------------------------------------------ rules */
static int isqrt(int v) { return (int)__builtin_sqrtf((float)v); }
static void set_speed(int s) {
  spd = s;
  for (int i = 0; i < NBALL; i++) {
    ball_t *b = &ball[i];
    int l = isqrt(b->vx * b->vx + b->vy * b->vy);
    if (l) b->vx = b->vx * s / l, b->vy = b->vy * s / l;
  }
}
static int diff_k(void) { return sv.diff == 0 ? 82 : sv.diff == 2 ? 118 : 100; } /* percent */
static void classic_speed(void) { /* faster after 4 and 12 hits, and at the orange and red rows */
  set_speed(spd0 * (100 + 17 * ((hits >= 4) + (hits >= 12) + got_orange + got_red)) / 100);
}

static int base_width(void) { return (mode == ARCADE ? 40 : 38) + (sv.diff == 0 ? 8 : sv.diff == 2 ? -6 : 0); }

static void load_wall(void) {
  bricks_left = 0;
  for (int r = 0; r < ROWS; r++)
    for (int c = 0; c < COLS; c++) {
      int k = 0;
      if (demo) {
      } else if (mode == CLASSIC) {
        if (r >= 1 && r <= 8) k = (r + 1) / 2;
      } else {
        const round_t *rd = &rounds[round_no];
        int v = rd->row[r] >> 2 * (c < 8 ? c : 14 - c) & 3;
        if (v) k = rd->pal >> 4 * (v - 1) & 15;
        if (k == K_RAINBOW) k = rainbow[r];
      }
      int hp = k == K_SILVER ? 2 + (round_no >= 6) : 1;
      brick[r][c] = (uint8_t)(k ? k | hp << 4 : 0);
      bricks_left += k && k != K_GOLD;
    }
  rows_shown = 0;
  if (mode == ARCADE) { /* this round's background colour */
    static const uint32_t hue[4] = {0x14286C, 0x0C4A3A, 0x4A1848, 0x3A2A10};
    color b = RGB(hue[round_no & 3]);
    bg_mid = mix(b, 0, 16), bg_hi = mix(b, 0, 22), bg_lo = mix(b, 0, 8);
  }
}

static void ball_tick(ball_t *b);
static void serve(void) { /* a new ball, on the paddle */
  for (int i = 0; i < NBALL; i++) ball[i].on = 0;
  ball_t *b = &ball[0];
  b->on = b->stuck = 1;
  b->off = (int16_t)(demo ? 0 : rndi(9) - 4);
  b->vx = 154, b->vy = -ONE;
  pw = pw_to = base_width(), demo_aim = 0;
  power = P_NONE;
  if (warp) warp = 0, mark_at(FX1, PY - 10, FX0, 22); /* the gate closes */
  cap.on = 0;
  for (int i = 0; i < NSHOT; i++) shot[i].on = 0;
  if (mode == CLASSIC) {
    hits = 0, got_orange = got_red = shrunk = false;
    spd0 = 589 * diff_k() / 100; /* 2.3 pixels a tick */
    classic_speed();
  } else {
    spd0 = (538 + 20 * imin(round_no, 10)) * diff_k() / 100;
    set_speed(spd0);
  }
  ball_tick(b);
}

static void set_state(int s) {
  ui_area(state);
  state = s, st_t = 0, sel = 0;
  ui_area(state);
}

static void start_round_(void) {
  load_wall();
  serve();
  set_state(S_READY);
  if (mode == ARCADE && round_no + 1 > sv.reached) sv.reached = (uint8_t)(round_no + 1), save();
}

static void new_game(int m) {
  mode = m, demo = false;
  score = 0, lives = 3, wall = 1, fresh_best = false, won = false;
  round_no = m == ARCADE ? start_round - 1 : 0;
  for (int i = 0; i < NPART; i++) part[i].life = 0;
  px = (FX0 + FX1) / 2 * ONE;
  start_round_();
  full = true;
}

static void title(void) {
  mode = CLASSIC, demo = true;
  load_wall();
  serve();
  set_state(S_TITLE);
  full = true;
}

/* keeps the best score, for a finished or abandoned game */
static void keep_best(void) {
  if (!demo && (uint32_t)score > sv.best[mode]) sv.best[mode] = (uint32_t)score, fresh_best = true;
  save();
}

static void add_score(int n) {
  score += n;
  if (score > 999999) score = 999999;
  mark(0, 0, W, TOP);
}

#ifndef CAP_CHANCE
#define CAP_CHANCE 22 /* percent of bricks that drop a capsule */
#endif
static void drop_capsule(int x, int y) {
  if (cap.on || rndi(100) >= CAP_CHANCE) return;
  int r = rndi(100), t = 0;
  while (r >= cap_odds[t]) r -= cap_odds[t++];
  if (t == 4 && (ball[1].on || ball[2].on)) t = 3;
  cap = (thing_t){(int16_t)(x + 2), (int16_t)y, 1, (uint8_t)t};
}

static void hit_brick(int r, int c) {
  uint8_t v = brick[r][c];
  int k = v & 15, hp = v >> 4 & 3;
  mark_cell(r, c);
  if (k == K_GOLD || hp > 1) { /* it holds, and shines */
    brick[r][c] = (uint8_t)(k | (k == K_GOLD ? hp : hp - 1) << 4 | 3 << 6);
    shake(1, 3);
    return;
  }
  brick[r][c] = 0;
  bricks_left--;
  int x = FX0 + c * CW, y = BY0 + r * CH;
  burst(x, y, CW - 2, CH - 2, kcol[k], 9, 22);
  shake(1 + (k >= K_SILVER), 5);
  if (demo) return;
  if (mode == CLASSIC) {
    add_score(9 - 2 * k);
    hits++;
    if (k == K_ORANGE) got_orange = true;
    if (k == K_RED) got_red = true;
    classic_speed();
  } else {
    add_score(k == K_SILVER ? 50 * (round_no + 1) : 50 + 10 * (k - K_WHITE));
    if (k != K_SILVER) drop_capsule(x, y);
  }
}

/* the brick under the ball, if any: hit it */
static bool hit_bricks(const ball_t *b) {
  int x0 = (b->x >> 8) - FX0, y0 = (b->y >> 8) - BY0;
  if (y0 + BS <= 0 || y0 >= ROWS * CH) return false;
  for (int r = imax(y0, 0) / CH; r <= imin(y0 + BS - 1, ROWS * CH - 1) / CH; r++)
    for (int c = imax(x0, 0) / CW; c <= imin(x0 + BS - 1, COLS * CW - 1) / CW; c++)
      if (brick[r][c]) {
        hit_brick(r, c);
        return true;
      }
  return false;
}

/* a: the angle from straight up, in 256ths of a radian (a sine good to 1 %) */
static void aim(ball_t *b, int a) {
  int a2 = a * a / 256, s = a * (256 - a2 * (256 - a2 / 20) / 1536) / 256;
  b->vx = spd * s / 256, b->vy = -spd * isqrt(65536 - s * s) / 256;
}
static int untouched; /* ticks since a ball last met the paddle */
/* Long without the paddle, a bounce turns the ball about 6 degrees, so it
   can't loop forever between the walls and gold bricks. */
static void nudge(ball_t *b) {
  if (untouched < 600) return;
  untouched = 420;
  for (int k = 0, t = rndi(2) ? 25 : -25; k < 2; k++, t = -t) {
    int vx = (b->vx * 255 - b->vy * t) / 256, vy = (b->vy * 255 + b->vx * t) / 256;
    if (iabs(vx) * 256 <= spd * 228 && iabs(vy) * 256 >= spd * 60) {
      b->vx = vx, b->vy = vy;
      return;
    }
  }
}
static void launch(ball_t *b) {
  untouched = 0;
  b->stuck = 0;
  int o = b->off * 256 / (pw / 2);
  aim(b, (o < 0 ? -90 : 90) + o / 2);
}

static void lose_life(void) {
  shake(4, 14);
  burst((px >> 8) - pw / 2, PY, pw, 5, fancy() ? RGB(0xC0C8D8) : BLUE, 16, 35);
  set_state(S_LOST);
}

static void ball_tick(ball_t *b) {
  for (int t = 2; t > 0; t--) b->tx[t] = b->tx[t - 1], b->ty[t] = b->ty[t - 1];
  b->tx[0] = (int16_t)(b->x >> 8), b->ty[0] = (int16_t)(b->y >> 8);
  if (b->stuck) {
    int x = (px >> 8) + b->off - BS / 2;
    x = x < FX0 ? FX0 : x > FX1 - BS ? FX1 - BS : x;
    b->x = x * ONE, b->y = (PY - BS) * ONE;
    for (int t = 0; t < 3; t++) b->tx[t] = (int16_t)x, b->ty[t] = PY - BS;
    return;
  }
  int n = imax(iabs(b->vx), iabs(b->vy)) / ONE + 1; /* steps of a pixel at most */
  for (int i = 0; i < n; i++) {
    int dx = b->vx / n, dy = b->vy / n;
    b->x += dx;
    if (b->x < FX0 * ONE) b->x = FX0 * ONE, b->vx = iabs(b->vx), nudge(b);
    else if (b->x > (FX1 - BS) * ONE) b->x = (FX1 - BS) * ONE, b->vx = -iabs(b->vx), nudge(b);
    else if (hit_bricks(b)) b->x -= dx, b->vx = -b->vx, nudge(b);
    b->y += dy;
    if (b->y < FY0 * ONE) {
      b->y = FY0 * ONE, b->vy = iabs(b->vy), nudge(b);
      if (mode == CLASSIC && !demo && !shrunk) { /* broke through: the paddle shrinks to half */
        shrunk = true, pw_to = base_width() / 2;
        shake(2, 8);
      }
    } else if (hit_bricks(b)) {
      b->y -= dy, b->vy = -b->vy, nudge(b);
    }
    int half = pw * ONE / 2, bot = b->y + BS * ONE, mid = b->x + BS * ONE / 2 - px;
    if (b->vy > 0 && bot >= PY * ONE && bot - dy <= (PY + 1) * ONE && mid > -half - BS * ONE / 2 &&
        mid < half + BS * ONE / 2) { /* the paddle: where it lands decides where it goes */
      b->y = (PY - BS) * ONE, untouched = 0;
      if (mode == ARCADE && !demo && spd < spd0 * 17 / 10) spd = spd * 65 / 64;
      if (power == P_CATCH) {
        b->stuck = 1, b->off = (int16_t)(mid / ONE), catch_t = 0;
      } else { /* up to 1.1 radians off vertical, and never too steep */
        int a = mid * 282 / (half + BS * ONE / 2);
        if (a > -56 && a < 56) a = b->vx < 0 ? -56 : 56;
        aim(b, a);
      }
      demo_aim = rndi(pw * 4 / 5 + 1) - pw * 2 / 5;
      return;
    }
    if (b->y > H * ONE) {
      b->on = 0;
      return;
    }
  }
}

static void clear_round(void) {
  for (int i = 0; i < NBALL; i++) ball[i].on = 0;
  for (int i = 0; i < NSHOT; i++) shot[i].on = 0;
  cap.on = 0;
  set_state(S_CLEAR);
}

static void catch_capsule(int t) {
  add_score(1000);
  burst(cap.x, PY - 2, 16, 4, cap_col[t], 8, 19);
  if (t == 0) set_speed(spd0); /* slow */
  if (t == 4) { /* three balls */
    ball_t *b = &ball[0];
    for (int i = 0; i < NBALL; i++)
      if (ball[i].on) b = &ball[i];
    b->stuck = 0;
    int a = b->vx * 256 / spd;
    for (int i = 0; i < NBALL; i++)
      if (!ball[i].on) {
        ball[i] = *b;
        a += 128;
        int t = a > 282 ? a - 564 : a;
        if (t > -56 && t < 56) t = t < 0 ? -56 : 56; /* never steeper than the paddle sends it */
        aim(&ball[i], t);
      }
  }
  if (t == 5 && lives < 9) lives++, mark(0, 0, W, TOP);
  if (t == 6) warp = 1, mark_at(FX1, PY - 10, FX0, 22);
  if (t == 1 || t == 2 || t == 3) {
    power = t == 1 ? P_CATCH : t == 2 ? P_LASER : P_ENLARGE;
    pw_to = t == 3 ? base_width() * 8 / 5 : base_width();
  } else if (t != 5) {
    power = P_NONE, pw_to = base_width();
  }
  if (power != P_CATCH)
    for (int i = 0; i < NBALL; i++)
      if (ball[i].stuck && ball[i].on && state == S_PLAY) launch(&ball[i]);
}

/* one 60th of a second of play; d is the paddle's direction, fire is OK */
static void play_tick(int d, bool fire) {
  /* the paddle: a tap nudges it, holding speeds it up */
  if (!d) pv = 0;
  else if (pv * d <= 0) pv = d * 282;
  else if ((pv += d * 115) * d > 7 * ONE) pv = d * 7 * ONE;
  px += pv;
  if (pw != pw_to) pw += pw < pw_to ? 1 : -1;
  int lo = (FX0 * 2 + pw) * ONE / 2, hi = (FX1 * 2 - pw) * ONE / 2;
  if (warp && d > 0 && px >= hi - ONE / 2 && state == S_PLAY) { /* out through the gate */
    add_score(10000);
    clear_round();
    return;
  }
  if (px < lo) px = lo, pv = 0;
  if (px > hi) px = hi, pv = 0;

  if (demo ? ball[0].stuck : state == S_READY) {
    ball_tick(&ball[0]);
    if (rows_shown < ROWS && !(st_t & 1)) { /* the wall builds up, row by row */
      mark_at(FX0 - 3, BY0 + rows_shown * CH - 3, COLS * CW + 6, CH + 7);
      rows_shown++;
    }
    if (fire) {
      launch(&ball[0]);
      if (rows_shown < ROWS) rows_shown = ROWS, full = true;
      if (!demo) set_state(S_PLAY);
    }
    return;
  }
  if (!demo && state != S_PLAY) return;
  int alive = 0;
  for (int i = 0; i < NBALL; i++)
    if (ball[i].on) {
      ball_tick(&ball[i]);
      alive += ball[i].on;
    }
  if (!alive) {
    if (demo) serve();
    else lose_life();
    return;
  }
  /* catch: OK lets go, or it goes by itself */
  if (power == P_CATCH)
    for (int i = 0; i < NBALL; i++)
      if (ball[i].on && ball[i].stuck && (fire || ++catch_t > 150)) launch(&ball[i]);
  if (power == P_LASER && fire && laser_cd <= 0) {
    int x = (px >> 8) - pw / 2;
    for (int k = 0, i = 0; k < 2 && i < NSHOT; i++)
      if (!shot[i].on) shot[i] = (thing_t){(int16_t)(k++ ? x + pw - 5 : x + 3), PY - 9, 1, 0};
    laser_cd = 12;
  }
  laser_cd--;
  for (int i = 0; i < NSHOT; i++) {
    thing_t *s = &shot[i];
    if (!s->on) continue;
    s->y -= 6;
    if (s->y < FY0) {
      s->on = 0;
      continue;
    }
    int r = (s->y - BY0) / CH, c = (s->x - FX0) / CW;
    if (s->y >= BY0 && r < ROWS && brick[r][c]) hit_brick(r, c), s->on = 0;
  }
  if (cap.on && (cap.y += (st_t & 1) + 1) > H) cap.on = 0;
  int pl = (px >> 8) - pw / 2;
  if (cap.on && cap.y + 8 >= PY && cap.y <= PY + 6 && cap.x + 16 > pl && cap.x < pl + pw) {
    cap.on = 0;
    catch_capsule(cap.type);
  }
  if (!bricks_left && !demo) {
    shake(3, 10);
    clear_round();
  }
}

/* the title screen's paddle plays by itself, after the lowest ball */
static int demo_dir(void) {
  ball_t *b = &ball[0];
  for (int i = 1; i < NBALL; i++)
    if (ball[i].on && (!b->on || ball[i].y > b->y)) b = &ball[i];
  int dd = (b->x >> 8) + BS / 2 - demo_aim - (px >> 8);
  return dd > 3 ? 1 : dd < -3 ? -1 : 0;
}

static void tick(int d, bool fire) {
  st_t++, tk++, untouched++;
  if (shake_t) {
    shake_t--;
    int a = imin(shake_a, shake_t / 2 + 1);
    bx = shake_t ? rndi(2 * a + 1) - a : 0, by = shake_t ? rndi(2 * a + 1) - a : 0;
    sx = shake_all ? bx : 0, sy = shake_all ? by : 0;
    if (!shake_t) shake_a = 0, shake_all = false;
  }
  for (int r = 0; r < ROWS; r++) /* glints fade */
    for (int c = 0; c < COLS; c++)
      if (brick[r][c] >> 6 && !(st_t & 1)) brick[r][c] = (uint8_t)(brick[r][c] - 64), mark_cell(r, c);
  for (int i = 0; i < NPART; i++) {
    part_t *p = &part[i];
    if (!p->life) continue;
    p->x += p->vx, p->y += p->vy, p->vy += 2, p->vx -= p->vx / 16;
    p->life--;
  }
  if (demo) d = demo_dir(), fire = !(st_t & 63);
#ifdef AUTOPLAY /* a test build that plays by itself */
  if (state == S_READY || state == S_PLAY) d = demo_dir(), fire = fire || !(st_t & 31);
#endif
  if (state == S_READY && st_t == 21) ui_area(S_READY); /* the hint shows up */
  if (warp && !(tk & 7)) mark_at(FX1, PY - 10, FX0, 22); /* the gate's lights */
  if (demo || state == S_READY || state == S_PLAY) play_tick(d, fire);
  bool over = false;
  if (state == S_LOST && st_t > 70) {
    if (--lives > 0) serve(), set_state(S_READY), mark(0, 0, W, TOP);
    else over = true;
  }
  if (state == S_CLEAR && st_t > 100) {
    if (mode == CLASSIC ? wall == 2 : round_no + 1 == NROUNDS) won = over = true;
    else wall++, round_no += mode == ARCADE, start_round_(), full = true;
  }
  if (over) keep_best(), set_state(S_OVER);
}

/* n ticks of play, and the areas they change */
static void frame(int n, int d, bool fire) {
  mark_moving();
  int psx = sx, psy = sy, pbx = bx, pby = by;
  while (n-- > 0) {
    tick(d, fire);
    fire = false;
  }
  mark_moving();
  if (sx != psx || sy != psy) { /* the walls shake */
    mark(0, TOP - 4, W, FY0 + 4);
    mark(0, FY0, FX0 + 4, H);
    mark(FX1 - 4, FY0, W, H);
  }
  if (bx != pbx || by != pby) { /* the bricks shake */
    int r0 = ROWS, r1 = 0;
    for (int r = 0; r < ROWS; r++)
      for (int c = 0; c < COLS; c++)
        if (brick[r][c]) r0 = imin(r0, r), r1 = r + 1;
    if (r1) mark(FX0, BY0 + r0 * CH - 4, FX1, BY0 + r1 * CH + 8);
  }
}

/* ------------------------------------------------------------------ menus */
static bool leave, ask_title; /* Quit game: back to NumPlay (or the calculator) */
static void menu_input(uint64_t hit) {
  bool ok = hit & (KEY(eadk_key_ok) | KEY(eadk_key_exe)), back = hit & KEY(eadk_key_back);
  int ud = (hit & KEY(eadk_key_down) ? 1 : 0) - (hit & KEY(eadk_key_up) ? 1 : 0);
  int lr = (hit & KEY(eadk_key_right) ? 1 : 0) - (hit & KEY(eadk_key_left) ? 1 : 0);
  int before = sel + tsel * 4 + start_round * 16;
  switch (state) {
    case S_TITLE:
      tsel = (tsel + ud + 3) % 3;
      if (tsel == 1 && lr) start_round = (start_round + lr + sv.reached - 1) % sv.reached + 1;
      if (back) {
        ask_title = true, sel = 0;
        set_state(S_QUIT);
      } else if (ok) {
        if (tsel == 2) set_state(S_SET);
        else new_game(tsel);
      }
      break;
    case S_SET:
      sel = (sel + ud + 3) % 3;
      if (lr || ok) {
        if (sel == 0) sv.diff = (uint8_t)((sv.diff + (lr ? lr : 1) + 3) % 3);
        else if (sel == 1) sv.shake ^= 1;
        else sv.trail ^= 1;
        ui_area(S_SET);
      }
      if (back) {
        save();
        set_state(S_TITLE);
      }
      break;
    case S_READY:
    case S_PLAY:
      if (back) resume_state = state, set_state(S_PAUSE);
      break;
    case S_PAUSE:
      sel = (sel + ud + 3) % 3;
      if (back || (ok && sel == 0)) {
        state = resume_state, st_t = 30, full = true;
      } else if (ok && sel == 1) {
        keep_best();
        new_game(mode);
      } else if (ok) {
        set_state(S_QUIT);
      }
      break;
    case S_QUIT:
      if (lr) sel ^= 1;
      if ((back || (ok && !sel)) && ask_title) ask_title = false, title();
      else if (back || (ok && !sel)) set_state(S_PAUSE), sel = 2;
      else if (ok) {
        keep_best();
        leave = true;
      }
      break;
    case S_OVER:
      if (ok) new_game(mode);
      else if (back) title();
      break;
  }
  if (before != sel + tsel * 4 + start_round * 16) ui_area(state);
}

int main(void) {
  np_app_begin();
  load();
  seed ^= (uint32_t)eadk_timing_millis() * 2654435761u;
  title();
  uint64_t ignore = eadk_keyboard_scan(), held = 0;
  const uint64_t rep = KEY(eadk_key_left) | KEY(eadk_key_right) | KEY(eadk_key_up) | KEY(eadk_key_down);
  uint32_t t0 = (uint32_t)eadk_timing_millis(), ticks = 0, frames = 0, repeat_at = 0;
  for (;;) {
    uint32_t now = (uint32_t)eadk_timing_millis();
    uint64_t k = eadk_keyboard_scan();
    ignore &= k; /* keys held from before start count once released */
    k &= ~ignore;
    if (k & (KEY(eadk_key_home) | KEY(eadk_key_on_off))) break;
    uint64_t hit = k & ~held;
    held = k;
    bool in_menu = state == S_TITLE || state == S_SET || state >= S_PAUSE;
    if (hit & rep) repeat_at = now + 350;
    else if (in_menu && (k & rep) && (int32_t)(now - repeat_at) >= 0) hit |= k & rep, repeat_at = now + 90;
    int s = state;
    menu_input(hit);
    if (leave) break;
    bool fire = s == state && (s == S_READY || s == S_PLAY) && (hit & (KEY(eadk_key_ok) | KEY(eadk_key_exe) | KEY(eadk_key_up)));
    int d = (k & (KEY(eadk_key_right) | KEY(eadk_key_six)) ? 1 : 0) - (k & (KEY(eadk_key_left) | KEY(eadk_key_four)) ? 1 : 0);
    /* the game runs 60 ticks a second, whatever the frame rate */
    uint32_t due = (now - t0) * 3 / 50;
    static bool fire_due; /* a press waits for a tick to use it */
    if ((int32_t)(due - ticks) > 4) ticks = due - 4;
    if (state < S_PAUSE) {
      /* the ticks owed, or one ahead: every frame moves, and none twice as much */
      int n = (int32_t)(due - ticks) > 0 ? (int)(due - ticks) : (int32_t)(ticks - due) < 1;
      fire_due |= fire;
      frame(n, d, fire_due);
      if (n) fire_due = false;
      ticks += (uint32_t)n;
    } else {
      ticks = due, fire_due = false;
      st_t++;
      if (state == S_OVER && !(st_t & 15) && fresh_best) mark(60, 124, 260, 148);
    }
    eadk_display_wait_for_vblank();
    flush();
    /* at most 60 frames a second */
    frames++;
    uint32_t next = t0 + frames * 50 / 3, after = (uint32_t)eadk_timing_millis();
    if ((int32_t)(next - after) > 0) eadk_timing_msleep(next - after);
    else if (after - next > 100) frames = (after - t0) * 3 / 50;
  }
  keep_best();
  return np_app_end();
}
