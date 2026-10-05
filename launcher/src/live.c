/* The backgrounds are those of NumVisuals (games/numvisuals), with the same
 * formulas. Time is the millisecond counter. */
#include "live.h"

#define W SCREEN_W
#define H SCREEN_H
#define RGB1(c) (color_t)((((c) >> 8) & 0xF800) | (((c) >> 5) & 0x07E0) | (((c) >> 3) & 0x1F))

enum { AURORA, SUNSET, PLASMA, PASTEL, LAVA, OCEAN };

static int16_t *sn;     /* sine, -256..256 */
static color_t *pal;    /* 256 colours */
static int16_t *scr;    /* scratch tables, W * 6 values */
static bool ready;
static int mode = PLASMA;
static int now;
static int plasma_k, plasma_speed;

#define S(a) sn[(a) & 255]
#define SCRATCH_VALUES (W * 6)

static int clamp(int v, int a, int b) { return v < a ? a : v > b ? b : v; }
static int iabs(int v) { return v < 0 ? -v : v; }

/* f over b; a from 0 (all b) to 32 (all f) */
static color_t mixf(color_t f, color_t b, int a) {
  uint32_t x = (f | (uint32_t)f << 16) & 0x07E0F81F, y = (b | (uint32_t)b << 16) & 0x07E0F81F;
  y = (y + ((x - y) * (uint32_t)a >> 5)) & 0x07E0F81F;
  return (color_t)(y | y >> 16);
}

typedef struct {
  const char *name;
  uint8_t n;
  uint32_t col[6];
} bg_t;
static const bg_t bgs[LIVE_COUNT] = {
    {"Aurora", 5, {0x02030C, 0x071A33, 0x0E6070, 0x33E8A0, 0xD2FFEA}},
    {"Sunset Drive", 4, {0x14002E, 0x55106E, 0xC72C79, 0xFF8A5B}},
    {"Plasma", 6, {0x1B0B3A, 0x6B1FA8, 0xFF3E8A, 0xFFC857, 0x2EC4B6, 0x1B0B3A}},
    {"Pastel", 6, {0xFFB3C7, 0xB9A2FF, 0x8FD8FF, 0x9EEDB6, 0xFFE08A, 0xFFB3C7}},
    {"Lava Lamp", 5, {0x12061F, 0x4A0F3F, 0xB8233B, 0xFF7A2E, 0xFFE7A6}},
    {"Ocean", 4, {0x0E1B45, 0x5A3C8C, 0xE9867A, 0xFFD6A0}},
};

/* a gradient through the colours of the current background */
static void palette(void) {
  if (!pal || mode >= LIVE_COUNT) return;
  const uint32_t *c = bgs[mode].col;
  int n = bgs[mode].n;
  for (int i = 0; i < 256; i++) {
    int p = i * (n - 1), k = p >> 8, f = p & 255;
    uint32_t o = 0;
    for (int s = 0; s < 24; s += 8)
      o |= (uint32_t)((((c[k] >> s) & 255) * (256 - f) + ((c[k + 1] >> s) & 255) * f) >> 8) << s;
    pal[i] = RGB1(o);
  }
  if (mode == PLASMA) plasma_k = 10, plasma_speed = 16;
  if (mode == PASTEL) plasma_k = 6, plasma_speed = 8;
}

void live_init(void) {
  ready = false;
  sn = np_alloc(256 * sizeof(int16_t));
  pal = np_alloc(256 * sizeof(color_t));
  scr = np_alloc(SCRATCH_VALUES * sizeof(int16_t));
  if (!sn || !pal || !scr) return;
  for (int i = 0; i < 128; i++) { /* Bhaskara's sine */
    float x = i * (3.14159265f / 128), q = x * (3.14159265f - x);
    sn[i] = (int16_t)(16 * q / (49.348022f - 4 * q) * 256 + 0.5f);
    sn[i + 128] = (int16_t)-sn[i];
  }
  palette();
  ready = true;
}

bool live_ready(void) { return ready; }
bool live_active(void) { return ready && mode < LIVE_COUNT; }
int live_mode(void) { return mode; }
void live_set(int m) {
  mode = (m >= 0 && m <= LIVE_COUNT) ? m : PLASMA;
  palette();
}
void live_next(void) { live_set((mode + 1) % (LIVE_COUNT + 1)); }
const char *live_name(int m) { return m >= 0 && m < LIVE_COUNT ? bgs[m].name : "None"; }

/* ---------------------------------------------------------------- per frame */
static void frame_aurora(void) {
  int16_t *ca = scr, *cb = scr + W, *cc = scr + 2 * W;
  int t = now;
  for (int x = 0; x < W; x++) {
    ca[x] = (int16_t)(104 + (S(x + t / 29) * 22 + S(x * 2 - t / 41 + S(x / 2 + t / 67) / 6) * 12) / 256);
    cb[x] = (int16_t)(175 + S(x * 3 + t / 13) / 9 + S(x * 9 - t / 9) / 12 + S(x / 2 - t / 23) / 4);
    cc[x] = (int16_t)(220 + S(x * 2 + 50) / 26 + S(x * 7) / 50);
  }
}

static void frame_plasma(void) {
  int16_t *ca = scr, *cb = scr + W;
  int t = now * plasma_speed / 16, k = plasma_k;
  for (int x = 0; x < W; x++) ca[x] = (int16_t)(S(x * k / 8 + t / 7) + S(x * k / 13 - t / 11));
  for (int y = 0; y < H; y++) cb[y] = (int16_t)(S(y * k / 8 - t / 9) + S(y * k / 17 + t / 13));
}

static void frame_lava(void) {
  int16_t *bx = scr, *by = scr + 6;
  int t = now;
  for (int i = 0; i < 6; i++) {
    bx[i] = (int16_t)(160 + S(t / (41 + i * 7) + i * 43) * (110 - i * 9) / 256);
    by[i] = (int16_t)(120 + S(t / (53 + i * 11) + i * 97 + 64) * (100 - i * 6) / 256);
  }
}

static void frame_ocean(void) {
  static const uint8_t base[6] = {121, 130, 142, 158, 180, 209}, amp[6] = {2, 3, 4, 6, 9, 13};
  int t = now;
  for (int k = 0; k < 6; k++)
    for (int x = 0; x < W; x++)
      scr[k * W + x] = (int16_t)(base[k] + (S(x * (8 - k) / 4 + t / (70 + k * 25) + k * 50) * 2 +
                                            S(x * (11 - k) / 3 - t / (110 + k * 30) + k * 90)) * amp[k] / 768);
}

void live_frame(uint32_t now_ms) {
  if (!live_active()) return;
  now = (int)now_ms;
  switch (mode) {
    case AURORA: frame_aurora(); break;
    case PLASMA:
    case PASTEL: frame_plasma(); break;
    case LAVA: frame_lava(); break;
    case OCEAN: frame_ocean(); break;
    default: break;
  }
}

/* ---------------------------------------------------------------- per strip */
static void strip_aurora(int top, int sh) {
  int16_t *ca = scr, *cb = scr + W, *cc = scr + 2 * W;
  for (int j = 0; j < sh; j++) {
    int y = top + j, sky = 8 + y * 52 / H;
    color_t *p = gfx_buf + j * W;
    for (int x = 0; x < W; x++) {
      if (y >= cc[x]) {
        p[x] = RGB1(0x010308);
        continue;
      }
      int d = ca[x] - y, i = d >= 0 ? cb[x] - d * 3 / 2 : cb[x] * 2 / 3 + d * 3;
      if (i < sky) {
        uint32_t h = (uint32_t)x * 73856093u ^ (uint32_t)y * 19349663u;
        i = (h * 0x5BD1E995u) >> 22 ? sky : 190;
      }
      p[x] = pal[i > 255 ? 255 : i];
    }
  }
}

static void strip_sunset(int top, int sh) {
  int t = now;
  for (int j = 0; j < sh; j++) {
    int y = top + j;
    color_t *p = gfx_buf + j * W;
    if (y < 150) {
      color_t sky = pal[y * 255 / 150];
      int dy = y - 102, r2 = 3600 - dy * dy;
      bool cut = dy > 4 && dy % 11 < (dy - 4) / 9 + 1;
      color_t sun = mixf(RGB1(0xFF3F81), RGB1(0xFFE45E), clamp((y - 42) * 32 / 108, 0, 32));
      for (int x = 0; x < W; x++) {
        int dx = x - 160, d2 = dx * dx + dy * dy;
        p[x] = dx * dx < r2 && !cut ? sun : d2 < 8100 ? mixf(RGB1(0xFF4F9A), sky, (8100 - d2) / 700) : sky;
      }
    } else {
      int d = y - 146, z0 = 98304 / d, z1 = 98304 / (d + 1), off = t / 2, a = clamp(4 + d / 3, 0, 28);
      bool row = ((z0 + off) >> 8) != ((z1 + off) >> 8) || d == 4;
      color_t base = mixf(RGB1(0x0A0018), RGB1(0x2A0A4A), d * 32 / 94), line = mixf(RGB1(0xFF3DDB), base, d == 4 ? 32 : a);
      int q = (-160 * z0) >> 15;
      for (int x = 0; x < W; x++) {
        int nq = ((x - 159) * z0) >> 15;
        p[x] = row || nq != q ? line : base;
        q = nq;
      }
    }
  }
}

static void strip_plasma(int top, int sh) {
  int16_t *ca = scr, *cb = scr + W;
  int t = now * plasma_speed / 16, k = plasma_k;
  for (int j = 0; j < sh; j++) {
    int y = top + j, r = cb[y];
    color_t *p = gfx_buf + j * W;
    for (int x = 0; x < W; x++) p[x] = pal[(uint8_t)(((ca[x] + r + S((x + y) * k / 16 + t / 5)) >> 3) + (t >> 4))];
  }
}

static void strip_lava(int top, int sh) {
  int16_t *bx = scr, *by = scr + 6;
  for (int j = 0; j < sh; j++) {
    int y = top + j, dy2[6];
    color_t *p = gfx_buf + j * W;
    for (int i = 0; i < 6; i++) dy2[i] = (y - by[i]) * (y - by[i]) + 16;
    for (int x = 0; x < W; x += 2) {
      int f = 0;
      for (int i = 0; i < 6; i++) {
        int dx = x - bx[i], r = 26 + i * 3;
        f += (r * r << 7) / (dx * dx + dy2[i]);
      }
      p[x] = p[x + 1] = pal[f < 128 ? f * 5 / 8 : 150 + (f - 128 > 315 ? 105 : (f - 128) / 3)];
    }
  }
}

static void strip_ocean(int top, int sh) {
  static const uint32_t sea_rgb[6] = {0x7FA9CF, 0x5A8CBE, 0x3F72A8, 0x2B5B91, 0x1C4677, 0x11335E};
  color_t sea[6];
  for (int i = 0; i < 6; i++) sea[i] = RGB1(sea_rgb[i]);
  int t = now;
  for (int j = 0; j < sh; j++) {
    int y = top + j;
    color_t *p = gfx_buf + j * W;
    for (int x = 0; x < W; x++) {
      int k = 5;
      while (k >= 0 && y < scr[k * W + x]) k--;
      color_t c;
      if (k < 0) {
        int dx = x - 236, dy = y - 92, d2 = dx * dx + dy * dy;
        c = d2 < 225 ? RGB1(0xFFF1C9) : pal[clamp(y * 2, 0, 255)];
        if (d2 >= 225 && d2 < 2500) c = mixf(RGB1(0xFFD9A0), c, (2500 - d2) / 160);
      } else {
        int e = y - scr[k * W + x];
        c = e < 2 ? mixf(0xFFFF, sea[k], 12 - e * 5) : sea[k];
        if (iabs(x - 236) < 4 + (y - 120) / 4 && S(y * 29 + x * 3 + t / 6) > 170) c = mixf(RGB1(0xFFE2B0), c, 14);
      }
      p[x] = c;
    }
  }
}

void live_strip(void) {
  if (!live_active()) return;
  int top = gfx_y0, sh = gfx_y1 - gfx_y0;
  switch (mode) {
    case AURORA: strip_aurora(top, sh); break;
    case SUNSET: strip_sunset(top, sh); break;
    case PLASMA:
    case PASTEL: strip_plasma(top, sh); break;
    case LAVA: strip_lava(top, sh); break;
    case OCEAN: strip_ocean(top, sh); break;
    default: break;
  }
  /* three quarters of the brightness, so that the white text and the cards stay readable */
  color_t *p = gfx_buf, *e = gfx_buf + sh * W;
  for (; p < e; p++) *p = (color_t)(((*p & 0xF7DE) >> 1) + ((*p & 0xE79C) >> 2));
}
