/* NumDance: a rhythm game in the style of Friday Night Funkin' and Dance
 * Dance Revolution. Arrows scroll to a row of gray targets; press the
 * matching key just as each one lands.
 *
 * The calculator has no speaker, so the music is something you see: the
 * stage pulses on every beat and every chart sits on the beat grid. A song is
 * a list of sections (verse, chorus, break...) that becomes notes bar by bar
 * from a seed, so a whole song takes a few bytes and its choruses come back
 * like in a real song.
 *
 * Only what changes is redrawn. The screen is cut into sixteen columns of 20
 * pixels with a bit per row that says "repaint me"; marked parts are composed
 * in a small buffer and pushed, which keeps the game at 60 frames a second.
 * Keys are read every millisecond between frames, so presses are timed to
 * the millisecond, not to the frame. */
#include <eadk.h>
#include <stdbool.h>
#include <stdint.h>
#include "../../common/epsilon_app.h"
#include "../../common/epsilon_files.h"

#ifdef __ELF__ /* app name and API level, for the calculator's installer */
const char eadk_app_name[] __attribute__((section(".rodata.eadk_app_name"))) = "NumDance";
const uint32_t eadk_api_level __attribute__((section(".rodata.eadk_api_level"))) = 0;
#endif

#define W 320
#define H 240
typedef uint16_t color;
#define RGB(c) (color)((((c) >> 8) & 0xF800) | (((c) >> 5) & 0x07E0) | (((c) >> 3) & 0x1F))
#define WHITE 0xFFFF
#define INK RGB(0x0B0712) /* outlines */
#define KEY(k) (1ull << (k))

static int mini(int a, int b) { return a < b ? a : b; }
static int maxi(int a, int b) { return a > b ? a : b; }
static int clamp(int v, int a, int b) { return v < a ? a : v > b ? b : v; }
static int iabs(int v) { return v < 0 ? -v : v; }
static void zero(void *p, int n) {
  for (volatile uint8_t *q = p; n--;) *q++ = 0; /* volatile: no call to memset */
}

/* f over b; a from 0 (all b) to 32 (all f) */
static color mix(color f, color b, int a) {
  uint32_t x = (f | (uint32_t)f << 16) & 0x07E0F81F, y = (b | (uint32_t)b << 16) & 0x07E0F81F;
  y = (y + ((x - y) * (uint32_t)a >> 5)) & 0x07E0F81F;
  return (color)(y | y >> 16);
}

/* ------------------------------------------------------------------ drawing */
#define BUFPX (W * 24)
static color buf[BUFPX] __attribute__((aligned(4)));
static int bx, by, bw, bh; /* the part of the screen buf holds */

/* n pixels of c, two at a time */
typedef uint32_t __attribute__((may_alias)) pair_t;
static void fill(color *p, int n, color c) {
  if (n <= 0) return;
  if ((uintptr_t)p & 2) *p++ = c, n--;
  pair_t *q = (pair_t *)(void *)p, v = c | (uint32_t)c << 16;
  for (; n > 1; n -= 2) *q++ = v;
  if (n) *(color *)q = c;
}

static void rect(int x, int y, int w, int h, color c, int a) {
  int x0 = maxi(x, bx), x1 = mini(x + w, bx + bw), y0 = maxi(y, by), y1 = mini(y + h, by + bh);
  if (x0 >= x1 || a <= 0) return;
  for (int j = y0; j < y1; j++) {
    color *p = buf + (j - by) * bw + x0 - bx, *e = p + x1 - x0;
    if (a >= 32) fill(p, x1 - x0, c);
    else
      for (; p < e; p++) *p = mix(c, *p, a);
  }
}

/* a disc, or a ring t pixels thick, with soft edges */
static void disc(int cx, int cy, int r, int t, color c, int a) {
  int x0 = maxi(cx - r - 1, bx), x1 = mini(cx + r + 2, bx + bw), y0 = maxi(cy - r - 1, by), y1 = mini(cy + r + 2, by + bh);
  int ri = t ? r - t : 0, ir = (16 << 16) / maxi(r, 1), iri = (16 << 16) / maxi(ri, 1);
  for (int y = y0; y < y1; y++)
    for (int x = x0; x < x1; x++) {
      int d = (x - cx) * (x - cx) + (y - cy) * (y - cy), e = r * r - d + r, k;
      if (e <= 0) continue;
      k = e >= 2 * r ? 32 : e * ir >> 16; /* about a pixel of soft edge */
      if (ri > 0) {
        e = d - ri * ri + ri;
        if (e <= 0) continue;
        if (e < 2 * ri) k = mini(k, e * iri >> 16);
      }
      color *p = buf + (y - by) * bw + x - bx;
      *p = mix(c, *p, k * a >> 5);
    }
}

/* ------------------------------------------------------------------ arrows */
/* One arrow shape (pointing up) drawn at four sizes when the app starts:
   0 empty, 1-15 the soft outer edge, 16 outline, 17 inner rim, 18-49 body
   from the tip to the tail. Palettes give it colours; the other directions
   read the same pixels turned. */
static const uint8_t SZ[4] = {32, 36, 38, 40};
static uint16_t soff[4];
static uint8_t spr[32 * 32 + 36 * 36 + 38 * 38 + 40 * 40];
enum { P_NOTE, P_PRESS = 4, P_CONF = 8, P_STRUM = 12, P_BEAT, P_MISS, NPAL };
static color pal[NPAL][50];
static const color lanec[4] = {RGB(0xC24B99), RGB(0x00FFFF), RGB(0x12FA05), RGB(0xF9393F)};

static void make_arrows(void) {
  static const int8_t vx[7] = {0, 88, 37, 37, -37, -37, -88}, vy[7] = {-88, 2, 2, 86, 86, 2, 2};
  int o = 0;
  for (int s = 0; s < 4; s++) {
    int n = SZ[s];
    float h = n * .5f, ol = n * .075f;
    soff[s] = (uint16_t)o;
    for (int j = 0; j < n; j++)
      for (int i = 0; i < n; i++) {
        float x = (i + .5f) / h - 1, y = (j + .5f) / h - 1, dd = 9;
        bool in = false;
        for (int k = 0, l = 6; k < 7; l = k++) { /* distance to the outline, and inside or not */
          float ax = vx[k] * .01f, ay = vy[k] * .01f, ex = vx[l] * .01f - ax, ey = vy[l] * .01f - ay;
          float wx = x - ax, wy = y - ay, u = (wx * ex + wy * ey) / (ex * ex + ey * ey);
          u = u < 0 ? 0 : u > 1 ? 1 : u;
          float qx = wx - ex * u, qy = wy - ey * u;
          if (qx * qx + qy * qy < dd) dd = qx * qx + qy * qy;
          if ((ay > y) != (ay + ey > y) && x < ax + ex * (y - ay) / ey) in = !in;
        }
        float d = __builtin_sqrtf(dd) * h * (in ? -1 : 1) - n * .07f; /* in pixels, corners rounded */
        int q = d > .5f ? 0 : d > -.5f ? 1 + (int)((.5f - d) * 14.9f) : d > -ol ? 16 : d > -ol - 1.4f ? 17 : 18 + clamp((int)((y + 1) * 16), 0, 31);
        spr[o++] = (uint8_t)q;
      }
  }
}

static void mkpal(int i, color out, color rim, color hi, color lo) {
  pal[i][16] = out, pal[i][17] = rim;
  for (int k = 0; k < 32; k++) pal[i][18 + k] = mix(lo, hi, k);
}

static void make_palettes(void) {
  color gray = RGB(0x87A3AD);
  for (int l = 0; l < 4; l++) {
    color c = lanec[l], pc = mix(c, gray, 12);
    mkpal(P_NOTE + l, mix(0, c, 26), mix(WHITE, c, 15), mix(WHITE, c, 6), mix(0, c, 10));
    mkpal(P_PRESS + l, RGB(0x1B2127), mix(WHITE, pc, 8), pc, mix(0, pc, 12));
    mkpal(P_CONF + l, mix(0, c, 14), WHITE, mix(WHITE, c, 22), mix(WHITE, c, 6));
  }
  mkpal(P_STRUM, RGB(0x1C242B), RGB(0xC6D6DC), RGB(0xA0B6BF), RGB(0x6B838D));
  mkpal(P_BEAT, RGB(0x26313A), RGB(0xEEF5F8), RGB(0xC2D4DB), RGB(0x8AA2AC));
  mkpal(P_MISS, INK, RGB(0x5A5A70), RGB(0x46465A), RGB(0x2A2A38));
}

static void arrow(int dir, int cx, int cy, int s, const color *pl, int a) {
  int n = SZ[s], x0 = cx - n / 2, y0 = cy - n / 2, sx, sy, base;
  switch (dir) {
    case 0: sx = n, sy = -1, base = n - 1; break;
    case 1: sx = 1, sy = -n, base = n * (n - 1); break;
    case 2: sx = 1, sy = n, base = 0; break;
    default: sx = -n, sy = 1, base = n * (n - 1);
  }
  int xa = maxi(x0, bx), xb = mini(x0 + n, bx + bw), ya = maxi(y0, by), yb = mini(y0 + n, by + bh);
  for (int y = ya; y < yb; y++) {
    color *p = buf + (y - by) * bw + xa - bx;
    const uint8_t *q = spr + soff[s] + base + (y - y0) * sy + (xa - x0) * sx;
    for (int x = xa; x < xb; x++, p++, q += sx) {
      int v = *q;
      if (v) *p = v < 16 ? mix(pl[16], *p, v * a >> 4) : a >= 32 ? pl[v] : mix(pl[v], *p, a);
    }
  }
}

/* ------------------------------------------------------------------ text */
/* ASCII 32..95, columns of 7 bits, top bit first; lower case is drawn upper */
static const uint8_t font[64][5] = {
  {0x00,0x00,0x00,0x00,0x00},{0x00,0x00,0x5F,0x00,0x00},{0x00,0x07,0x00,0x07,0x00},{0x14,0x7F,0x14,0x7F,0x14},
  {0x24,0x2A,0x7F,0x2A,0x12},{0x23,0x13,0x08,0x64,0x62},{0x36,0x49,0x55,0x22,0x50},{0x00,0x05,0x03,0x00,0x00},
  {0x00,0x1C,0x22,0x41,0x00},{0x00,0x41,0x22,0x1C,0x00},{0x14,0x08,0x3E,0x08,0x14},{0x08,0x08,0x3E,0x08,0x08},
  {0x00,0x50,0x30,0x00,0x00},{0x08,0x08,0x08,0x08,0x08},{0x00,0x60,0x60,0x00,0x00},{0x20,0x10,0x08,0x04,0x02},
  {0x3E,0x41,0x41,0x41,0x3E},{0x00,0x42,0x7F,0x40,0x00},{0x42,0x61,0x51,0x49,0x46},{0x21,0x41,0x45,0x4B,0x31},
  {0x18,0x14,0x12,0x7F,0x10},{0x27,0x45,0x45,0x45,0x39},{0x3C,0x4A,0x49,0x49,0x30},{0x01,0x71,0x09,0x05,0x03},
  {0x36,0x49,0x49,0x49,0x36},{0x06,0x49,0x49,0x29,0x1E},{0x00,0x36,0x36,0x00,0x00},{0x00,0x56,0x36,0x00,0x00},
  {0x08,0x14,0x22,0x41,0x00},{0x14,0x14,0x14,0x14,0x14},{0x00,0x41,0x22,0x14,0x08},{0x02,0x01,0x51,0x09,0x06},
  {0x32,0x49,0x79,0x41,0x3E},{0x7E,0x11,0x11,0x11,0x7E},{0x7F,0x49,0x49,0x49,0x36},{0x3E,0x41,0x41,0x41,0x22},
  {0x7F,0x41,0x41,0x22,0x1C},{0x7F,0x49,0x49,0x49,0x41},{0x7F,0x09,0x09,0x09,0x01},{0x3E,0x41,0x49,0x49,0x7A},
  {0x7F,0x08,0x08,0x08,0x7F},{0x00,0x41,0x7F,0x41,0x00},{0x20,0x40,0x41,0x3F,0x01},{0x7F,0x08,0x14,0x22,0x41},
  {0x7F,0x40,0x40,0x40,0x40},{0x7F,0x02,0x0C,0x02,0x7F},{0x7F,0x04,0x08,0x10,0x7F},{0x3E,0x41,0x41,0x41,0x3E},
  {0x7F,0x09,0x09,0x09,0x06},{0x3E,0x41,0x51,0x21,0x5E},{0x7F,0x09,0x19,0x29,0x46},{0x46,0x49,0x49,0x49,0x31},
  {0x01,0x01,0x7F,0x01,0x01},{0x3F,0x40,0x40,0x40,0x3F},{0x1F,0x20,0x40,0x20,0x1F},{0x3F,0x40,0x38,0x40,0x3F},
  {0x63,0x14,0x08,0x14,0x63},{0x07,0x08,0x70,0x08,0x07},{0x61,0x51,0x49,0x45,0x43},{0x00,0x7F,0x41,0x41,0x00},
  {0x02,0x04,0x08,0x10,0x20},{0x00,0x41,0x41,0x7F,0x00},{0x04,0x02,0x01,0x02,0x04},{0x40,0x40,0x40,0x40,0x40},
};

static int slen(const char *s) {
  int n = 0;
  while (s[n]) n++;
  return n;
}
static int ga = 32, gsl, gp; /* text alpha, slant (pixels per dot row) and letter spacing */
static int tw(const char *s, int k) { return slen(s) ? slen(s) * (6 * k + gp) - k - gp : 0; }

/* each dot a k-square grown by g pixels; c at the top to c2 (if not 0) at the bottom */
static void glyphs(const char *s, int x, int y, int k, int g, color c, color c2) {
  if (y - g >= by + bh || y + 7 * k + g <= by) return;
  for (; *s; s++, x += 6 * k + gp) {
    unsigned ch = (uint8_t)*s;
    if (ch >= 'a' && ch <= 'z') ch -= 32;
    ch -= 32;
    if (ch > 63) ch = '?' - 32;
    if (x - g >= bx + bw || x + 5 * k + g + 6 * gsl <= bx) continue;
    for (int i = 0; i < 5; i++)
      for (int j = 0, b = font[ch][i]; b; j++, b >>= 1)
        if (b & 1) rect(x + i * k - g + (6 - j) * gsl, y + j * k - g, k + 2 * g, k + 2 * g, c2 ? mix(c2, c, j * 5) : c, ga);
  }
}
/* text with a dark outline g pixels wide */
static void text(const char *s, int x, int y, int k, int g, color c, color c2) {
  if (g) glyphs(s, x, y, k, g, INK, INK);
  glyphs(s, x, y, k, 0, c, c2);
}
static void ctext(const char *s, int cx, int y, int k, int g, color c, color c2) {
  text(s, cx - tw(s, k) / 2, y, k, g, c, c2);
}
static void small(const char *s, int cx, int y, color c) { ctext(s, cx, y, 1, 1, c, 0); }
static void heading(const char *s, int y) { ctext(s, 160, y, 3, 2, WHITE, RGB(0xFFE070)); }

/* a menu choice: the chosen one bright, between two arrows */
static void item(const char *t, int cx, int y, bool on) {
  ctext(t, cx, y, 2, on ? 2 : 1, on ? WHITE : RGB(0x8A82A8), on ? RGB(0xFFE070) : RGB(0x6A6288));
  if (on) {
    int d = tw(t, 2) / 2 + 20;
    arrow(3, cx - d, y + 6, 0, pal[P_NOTE + 3], 32);
    arrow(0, cx + d, y + 6, 0, pal[P_NOTE], 32);
  }
}

static char *num(char *o, uint32_t v) {
  char t[10];
  int n = 0;
  do t[n++] = (char)('0' + v % 10); while (v /= 10);
  while (n) *o++ = t[--n];
  *o = 0;
  return o;
}
static char *cat(char *o, const char *s) {
  while (*s) *o++ = *s++;
  *o = 0;
  return o;
}
static char *signed_ms(char *o, int v) {
  *o++ = v < 0 ? '-' : '+';
  return cat(num(o, (uint32_t)iabs(v)), " ms");
}
/* v/100 with two decimals */
static char *pct(char *o, int v) {
  o = num(o, (uint32_t)v / 100);
  *o++ = '.';
  *o++ = (char)('0' + v / 10 % 10);
  *o++ = (char)('0' + v % 10);
  return cat(o, "%");
}

/* ------------------------------------------------------------------ repainting */
enum { S_TITLE, S_SONGS, S_OPTIONS, S_HELP, S_CALIB, S_PLAY, S_RESULTS };
static int screen, sel, menu_beat;
static uint16_t dirt[H]; /* a bit per column of 20 pixels that must be repainted, for each row */
static int dirty_px;

static void mark(int x, int y, int w, int h) {
  int x0 = maxi(x, 0), x1 = mini(x + w, W), y0 = maxi(y, 0), y1 = mini(y + h, H);
  if (x0 >= x1 || y0 >= y1) return;
  unsigned m = (2u << (x1 - 1) / 20) - (1u << x0 / 20);
  for (int r = y0; r < y1; r++)
    for (unsigned n = m & ~dirt[r]; n; n &= n - 1) dirty_px += 20;
  for (int r = y0; r < y1; r++) dirt[r] |= (uint16_t)m;
}
static void mark_all(void) { mark(0, 0, W, H); }

static void scene(void);

static void paint(int x, int y, int w, int h) {
  int rows = BUFPX / w;
  for (int y0 = y; y0 < y + h; y0 += rows) {
    bx = x, by = y0, bw = w, bh = mini(rows, y + h - y0);
    scene();
    eadk_display_push_rect((eadk_rect_t){(uint16_t)x, (uint16_t)y0, (uint16_t)w, (uint16_t)bh}, buf);
  }
}

/* rows that need the same columns go together, as few rectangles as possible */
static void flush(void) {
  for (int y = 0; y < H;) {
    int m = dirt[y], y1 = y + 1;
    while (y1 < H && dirt[y1] == m) y1++;
    for (int b = 0; b < 16;) {
      if (!(m >> b & 1)) {
        b++;
        continue;
      }
      int e = b;
      while (e < 16 && (m >> e & 1)) e++;
      paint(b * 20, y, (e - b) * 20, y1 - y);
      b = e;
    }
    y = y1;
  }
  for (int y = 0; y < H; y++) dirt[y] = 0;
  dirty_px = 0;
}

/* things with a fixed place that change now and then: repainted when their
   key or their rectangle changes */
typedef struct {
  int16_t x, y, w, h;
  int32_t key;
  uint8_t wait; /* frames the stage has been kept waiting */
} watch_t;
#define NW 26 /* 0-11 the play screen or menus, 12-19 and 24-25 the stage, 20-23 the rest */
static watch_t wt[NW];
static void watch(int i, int x, int y, int w, int h, int32_t key) {
  watch_t *o = &wt[i];
  if (o->key == key && o->x == x && o->y == y && o->w == w && o->h == h) return;
  /* when a lot changes, the stage can wait a frame or two, never longer */
  if ((i >= 24 || (i >= 12 && i < 20)) && dirty_px > 22000 && screen == S_PLAY && o->wait++ < 2) return;
  mark(o->x, o->y, o->w, o->h);
  mark(x, y, w, h);
  *o = (watch_t){(int16_t)x, (int16_t)y, (int16_t)w, (int16_t)h, key, 0};
}

/* ------------------------------------------------------------------ saved */
#define NSONG 8 /* 7 songs, then Endless */
#define SAVE_NAME "numdance.sav"
static struct {
  uint8_t magic, version, speed, up, nofail, song, diff;
  int8_t offset;          /* ms taken off every key press */
  uint8_t grade[NSONG][4]; /* 1-6 D..S+, 8: full combo */
  uint32_t best[NSONG][4];
} S;
static bool save_due;

static void load(void) {
  uint32_t n = 0;
  const uint8_t *d = ef_read(SAVE_NAME, &n);
  if (d && n == sizeof S && d[0] == 'D' && d[1] == 1) {
    for (uint32_t i = 0; i < n; i++) ((uint8_t *)&S)[i] = d[i];
    if (S.speed > 6) S.speed = 2;
    S.up &= 1, S.nofail &= 1;
    if (S.song >= NSONG) S.song = 0;
    S.diff &= 3;
    S.offset = (int8_t)clamp(S.offset, -120, 120);
    for (int i = 0; i < NSONG; i++)
      for (int j = 0; j < 4; j++) {
        if ((S.grade[i][j] & 7) > 6 || S.grade[i][j] > 15) S.grade[i][j] = 0;
        if (S.best[i][j] > 99999999) S.best[i][j] = 0;
      }
  } else {
    zero(&S, sizeof S);
    S.speed = 2, S.diff = 1;
  }
}
static void save(void) {
  S.magic = 'D', S.version = 1;
  ef_write(SAVE_NAME, &S, sizeof S);
  save_due = false;
}

/* ------------------------------------------------------------------ songs */
enum { STREAM, STAIRS, TRILL, SHAPES, JACKS };            /* what the lanes do */
enum { INTRO, VERSE, BUILD, CHORUS, BREAK, DROP, OUTRO }; /* song sections */
#define STY(fam, hold, jump, sync) ((fam) | (hold) << 3 | (jump) << 5 | (sync) << 7)
#define SC(t, n) ((t) << 4 | ((n) - 1))
typedef struct {
  char name[12];
  uint8_t bpm, seed, style, sec[9];
  color c;
} song_t;
static const song_t songs[NSONG] = {
  {"Warm Up", 96, 3, STY(STAIRS, 1, 0, 0),
   {SC(INTRO, 2), SC(VERSE, 4), SC(CHORUS, 4), SC(VERSE, 4), SC(CHORUS, 4), SC(BREAK, 2), SC(CHORUS, 4), SC(OUTRO, 2)},
   RGB(0x2F9BFF)},
  {"Slow Jam", 84, 5, STY(STREAM, 3, 1, 0),
   {SC(INTRO, 2), SC(VERSE, 4), SC(CHORUS, 4), SC(BREAK, 2), SC(VERSE, 4), SC(CHORUS, 4), SC(OUTRO, 2)},
   RGB(0xFF8A2A)},
  {"Bubble Pop", 112, 21, STY(TRILL, 1, 1, 1),
   {SC(INTRO, 2), SC(VERSE, 4), SC(BUILD, 4), SC(CHORUS, 8), SC(BREAK, 2), SC(VERSE, 4), SC(BUILD, 4), SC(CHORUS, 8), SC(OUTRO, 2)},
   RGB(0xFF6FC8)},
  {"Neon Nights", 124, 7, STY(SHAPES, 1, 2, 1),
   {SC(INTRO, 2), SC(VERSE, 8), SC(BUILD, 4), SC(CHORUS, 8), SC(VERSE, 8), SC(CHORUS, 8), SC(OUTRO, 2)},
   RGB(0xB03CFF)},
  {"Starlight", 140, 13, STY(JACKS, 2, 2, 1),
   {SC(INTRO, 2), SC(VERSE, 8), SC(CHORUS, 8), SC(BREAK, 4), SC(CHORUS, 8), SC(DROP, 8), SC(OUTRO, 2)},
   RGB(0x4F6BFF)},
  {"Hyperdrive", 160, 9, STY(STREAM, 0, 1, 0),
   {SC(INTRO, 4), SC(VERSE, 8), SC(BUILD, 4), SC(DROP, 8), SC(BREAK, 2), SC(VERSE, 8), SC(BUILD, 4), SC(DROP, 8), SC(OUTRO, 2)},
   RGB(0x00C89A)},
  {"Boss Rush", 180, 17, STY(STAIRS, 1, 3, 1),
   {SC(INTRO, 4), SC(VERSE, 8), SC(BUILD, 4), SC(DROP, 16), SC(BREAK, 2), SC(CHORUS, 8), SC(DROP, 8), SC(OUTRO, 2)},
   RGB(0xFF3344)},
  {"Endless", 100, 0, 0, {0}, RGB(0xB8B8C8)}, /* its sections are in gen_bar */
};
static const char *const diff_name[4] = {"Easy", "Normal", "Hard", "Expert"};
static const color diff_c[4] = {RGB(0x12FA05), RGB(0xFFD400), RGB(0xF9393F), RGB(0xC24B99)};

/* rhythms for a bar, a bit per 16th: each level adds to the ones below */
static const uint16_t tpl[5][4] = {
  {0x1111, 0x0101, 0x0111, 0x1101}, /* quarters, halves */
  {0x0404, 0x4040, 0x1040, 0x4400}, /* some eighths */
  {0x5555, 0x4444, 0x4848, 0x2424}, /* eighths, off-beats */
  {0x8080, 0x0808, 0x0202, 0x8888}, /* sixteenth pick-ups */
  {0x7777, 0x2222, 0x0A0A, 0x6666}, /* bursts */
};
static const uint8_t shapes[4][4] = {{0, 1, 2, 3}, {3, 2, 1, 0}, {0, 2, 1, 3}, {1, 3, 0, 2}};
/* what a thumb can do: time between any two notes, and two in one lane */
static const int16_t min_gap[4] = {290, 160, 125, 85}, jack_gap[4] = {600, 380, 250, 170};

typedef struct {
  int32_t t;    /* ms of song time */
  uint16_t len; /* a hold's length, or 0 */
  uint8_t lane, st;
} note_t;
enum { N_WAIT, N_HOLD, N_DONE, N_MISS, N_DROP };
#define NR 128
static note_t notes[NR];
static int nh, nt; /* the ring's next free slot and oldest note */

typedef struct {
  int32_t t16; /* 1/16 ms */
  uint16_t beat16;
  uint8_t type;
} bar_t;
static bar_t bars[8];
static int nbars;

static struct {
  const song_t *s; /* 0: Endless */
  int d, sec, bis, type, nb, occ[7], count, bpm;
  int32_t t16, last_t, busy[4], hold_end, jump_t;
  int last, dir, mir, fam, ta, tb, shape, idx, lastp, lastn, quick;
  bool done, counting;
} G;
static uint32_t rs;
static uint32_t crand(void) {
  rs ^= rs << 13;
  rs ^= rs >> 17;
  rs ^= rs << 5;
  return rs;
}

static void gen_start(int song, int d) {
  zero(&G, sizeof G);
  G.s = song < NSONG - 1 ? &songs[song] : 0;
  G.d = d, G.bpm = songs[song].bpm;
  G.last_t = G.hold_end = G.jump_t = -99999;
  for (int l = 0; l < 4; l++) G.busy[l] = -99999;
  nbars = nh = nt = 0;
}

static void add_note(int32_t t, int lane, int len) {
  G.count++;
  if (G.counting) return;
  notes[nh] = (note_t){t, (uint16_t)len, (uint8_t)lane, N_WAIT};
  nh = (nh + 1) & (NR - 1);
}

/* the next lane of this bar's figure, or -1 if all are busy */
static int pick(int32_t t) {
  int l;
  switch (G.fam) {
    case STREAM: l = (G.last + 1 + (int)(crand() % 3)) & 3; break;
    case STAIRS:
      l = G.last + G.dir;
      if (l < 0 || l > 3) G.dir = -G.dir, l = G.last + G.dir;
      break;
    case TRILL: l = G.idx & 1 ? G.tb : G.ta; break;
    case SHAPES: l = shapes[G.shape][G.idx & 3]; break;
    default: l = G.idx & 1 ? G.last : (G.last + 1 + (int)(crand() % 3)) & 3; /* jacks: each twice */
  }
  G.idx++;
  for (int k = 0; k < 4; k++, l = (l + 1) & 3) {
    int p = G.mir ? 3 - l : l;
    if (t >= G.busy[p]) {
      G.last = l;
      return p;
    }
  }
  return -1;
}

/* the notes of the next bar */
static void gen_bar(void) {
  const song_t *s = G.s;
  int seed = s ? s->seed : 99 + G.sec / 5;
  if (G.done) return;
  if (!G.bis) { /* a new section */
    int sc;
    if (!s) { /* Endless: the same loop of sections, faster each time */
      static const uint8_t loop[5] = {SC(VERSE, 4), SC(BUILD, 4), SC(CHORUS, 8), SC(BREAK, 2), SC(DROP, 8)};
      sc = loop[G.sec % 5];
      G.bpm = mini(100 + G.sec / 5 * 8, 196);
    } else if (G.sec >= 9 || !(sc = s->sec[G.sec])) {
      G.done = true;
      return;
    }
    G.type = sc >> 4, G.nb = (sc & 15) + 1;
    G.mir = G.occ[G.type]++ & 1; /* a section's second time is mirrored */
    rs = (uint32_t)seed * 0x9E3779B1u + (uint32_t)G.type * 7919u + 1;
    crand();
    G.last = (int)(crand() & 3), G.dir = crand() & 1 ? 1 : -1;
  }
  int d = G.d, type = G.type, bis = G.bis, style = s ? s->style : STY(STREAM, 1, 2, 1);
  int32_t bar16 = 3840000 / G.bpm, step16 = bar16 / 16; /* 4 beats of 60000/bpm ms */
  if (!G.counting) bars[nbars & 7] = (bar_t){G.t16, (uint16_t)(bar16 / 4), (uint8_t)type};
  nbars++;
  /* the bars of a section go A B A C, and the same seed makes the same bar */
  int v = bis & 1 ? 1 + (bis >> 1 & 1) : 0;
  rs = (uint32_t)seed * 0x9E3779B1u ^ (uint32_t)(type << 24 | v << 16 | (bis >> 2) << 8 | 0x55);
  crand();
  uint32_t r = crand();
  int tier = d + (type == INTRO || type == OUTRO ? -1 : type == CHORUS || type == DROP ? 1 : type == BUILD ? bis * 2 >= G.nb : 0);
  bool fast = step16 >> 4 < min_gap[d]; /* sixteenths too fast to run */
  tier = clamp(tier + (s ? 0 : G.sec / 10), 0, fast && d < 3 ? 3 : 4);
  fast &= d == 3;
  uint16_t m = 0;
  for (int k = 0; k <= tier; k++) {
    int i = (int)(r >> (k * 2) & 3);
    if (!(style >> 7) && (k == 1 || k == 2)) i &= 1; /* straight songs keep to the beat */
    m |= tpl[k][i];
  }
  for (int k = 0, run = 0; k < 16; k++) { /* no more than 5 sixteenths in a row */
    run = m >> k & 1 ? run + 1 : 0;
    if (run > 5) m &= (uint16_t)~(1 << k), run = 0;
    /* too fast on Expert: runs become pairs, a pick-up into the next step */
    if (fast && (k & 1) && (m >> k & 3) == 3) m &= (uint16_t)~(1 << (k - 1));
  }
  bool brk = type == BREAK;
  if (brk) m = d >= 2 ? 0x0101 : 1;
  G.fam = type == BUILD ? STAIRS : crand() % 3 ? (style & 7) : (int)(crand() % 5);
  G.ta = (int)(crand() & 3), G.tb = (G.ta + 1 + (int)(crand() % 3)) & 3, G.shape = (int)(crand() & 3), G.idx = 0;
  int holdp = (style >> 3 & 3) + (brk ? 8 : 0);
  int jumpp = type == CHORUS || type == DROP ? (style >> 5 & 3) + 1 + (d == 3) : type == VERSE ? (style >> 5 & 3) / 2 : 0;
  int steps[17], ns = 0, prev = -8;
  for (int k = 0; k < 16; k++)
    if (m >> k & 1) steps[ns++] = k;
  steps[ns] = 16;
  for (int i = 0; i < ns; i++) {
    int st = steps[i], gap = steps[i + 1] - st, l, len = 0;
    int32_t t = (G.t16 + st * step16) >> 4;
    bool quick = t - G.last_t < min_gap[d];
    if (quick) { /* Expert: a quick note on the other thumb, one at a time, with room after it */
      l = 3 - G.lastp;
      if (d < 3 || t - G.last_t < 75 || (gap * step16 >> 4) < min_gap[d] || G.quick || G.lastn > 1 || t < G.hold_end || t < G.busy[l]) continue;
      G.last = 3 - G.last;
    } else {
      l = pick(t);
      if (l < 0) continue;
      if (gap >= 4 && t >= G.hold_end && (int)(crand() & 7) < holdp) { /* one hold at a time */
        int n = gap - 2;
        if (d >= 2 && !brk && !(crand() & 3)) n = 6 + (int)(crand() & 3) * 2; /* long: other notes come meanwhile */
        len = n * step16 >> 4;
      }
    }
    add_note(t, l, len);
    G.busy[l] = t + len + jack_gap[d];
    G.quick = quick, G.lastp = l, G.lastn = 1;
    /* a jump: two arrows on a beat, with room around it, at most every other beat */
    if (!quick && d && !(st & 3) && (d > 1 || !st) && gap >= 2 && st - prev >= 2 && t >= G.hold_end && t - G.jump_t >= bar16 >> 5 &&
        (int)(crand() & 7) < jumpp) {
      int l2 = pick(t);
      if (l2 >= 0) add_note(t, l2, len), G.busy[l2] = t + len + jack_gap[d], G.lastn = 2, G.jump_t = t;
    }
    G.hold_end = maxi(G.hold_end, t + len);
    G.last_t = t, prev = st;
  }
  G.t16 += bar16;
  if (++G.bis == G.nb) G.bis = 0, G.sec++;
}

/* ms since the last beat at song time t; *n: that beat's number */
static int beat_at(int32_t t, int *n, int *type) {
  int i = nbars - 1;
  while (i > 0 && i > nbars - 8 && (bars[i & 7].t16 >> 4) > t) i--;
  if (i < 0) return *n = 0, *type = 0, 0;
  const bar_t *b = &bars[i & 7];
  int32_t d = t * 16 - b->t16, k = d >= 0 ? d / b->beat16 : -1 - (-d - 1) / b->beat16;
  *n = i * 4 + (int)k, *type = b->type;
  return (int)((d - k * b->beat16) >> 4);
}

/* ------------------------------------------------------------------ keys */
static uint32_t now;
static uint64_t held, hit, press, rep_ok; /* rep_ok: keys pressed on this screen, which may repeat */
static uint32_t rep_at;
static bool quit, tq; /* tq: "Quit game?" over the title */
static int tsel;
static uint8_t lanes; /* lanes held */
typedef struct {
  uint32_t t;
  uint8_t lane, down;
} ev_t;
static ev_t evq[32];
static int nev;
/* left, down, up, right: the arrows, 4 5 8 6, and 1 2 3 + */
static const uint8_t lane_key[12] = {0, 36, 42, 2, 37, 43, 1, 31, 44, 3, 38, 45};

/* between frames too, so presses get the right time */
static void poll(void) {
  uint64_t k = eadk_keyboard_scan();
  uint32_t ms = (uint32_t)eadk_timing_millis();
  hit |= k & ~held;
  held = k;
  if (k & (KEY(eadk_key_home) | KEY(eadk_key_on_off))) quit = true;
  int ls = 0;
  for (int i = 0; i < 12; i++)
    if (k >> lane_key[i] & 1) ls |= 1 << (i / 3);
  for (int l = 0; l < 4; l++)
    if (((ls ^ lanes) >> l & 1) && nev < 32) evq[nev++] = (ev_t){ms, (uint8_t)l, (uint8_t)(ls >> l & 1)};
  lanes = (uint8_t)ls;
}

static void input(void) {
  poll();
  press = hit, hit = 0;
  rep_ok = (rep_ok | press) & held;
  const uint64_t rep = (KEY(eadk_key_left) | KEY(eadk_key_right) | KEY(eadk_key_up) | KEY(eadk_key_down)) & rep_ok;
  if (press & rep) rep_at = now + 380;
  else if ((held & rep) && (int32_t)(now - rep_at) >= 0) press |= held & rep, rep_at = now + 90;
}
static void go(int s, int i) {
  screen = s, sel = i, rep_ok = 0; /* a key still held from the last screen does not repeat */
  mark_all();
}

static bool pressed(int k) { return press >> k & 1; }
static bool ok_key(void) { return pressed(eadk_key_ok) || pressed(eadk_key_exe); }
static int ud(void) { return pressed(eadk_key_down) - pressed(eadk_key_up); }
static int lr(void) { return pressed(eadk_key_right) - pressed(eadk_key_left); }

/* ------------------------------------------------------------------ the stage */
static color bgrow[H], lanerow[H];

static void make_stage(void) {
  for (int y = 0; y < H; y++) {
    color c = y < 212 ? mix(RGB(0x331552), RGB(0x0A0514), y * 32 / 212) : y == 212 ? RGB(0x6E48A8) : mix(RGB(0x0C0714), RGB(0x2A1A40), (y - 213) * 32 / 27);
    bgrow[y] = c;
    lanerow[y] = mix(RGB(0x05030A), c, 21);
  }
}

/* a speaker cone in one pass: rim, surround, cone, dust cap and its shine */
static void cone(int cx, int cy, int r, int glow) {
  int R = r + 3, x0 = maxi(cx - R, bx), x1 = mini(cx + R + 1, bx + bw), y0 = maxi(cy - R, by), y1 = mini(cy + R + 1, by + bh);
  int cap = (r / 3 + 1) * (r / 3 + 1), in = (r - 1) * (r - 1), out = (r + 1) * (r + 1), fold = r * r * 4 / 9;
  int grad = (26 << 16) / (in - cap), hl = r / 7 + 1;
  color sur = mix(RGB(0x6E62A0), RGB(0x2B2440), glow), body = mix(RGB(0x544A80), RGB(0x2A2340), glow);
  color capc = mix(RGB(0xA89EDA), RGB(0x585080), glow);
  for (int y = y0; y < y1; y++)
    for (int x = x0; x < x1; x++) {
      int dx = x - cx, dy = y - cy, d = dx * dx + dy * dy;
      if (d > R * R) continue;
      color c = d > out ? RGB(0x07050B) : d > in ? sur : d > cap ? mix(RGB(0x100C18), body, (d - cap) * grad >> 16) : capc;
      if (iabs(d - fold) < 2 * r) c = mix(0, c, 12);
      if ((dx + r / 9) * (dx + r / 9) + (dy + r / 9) * (dy + r / 9) <= hl * hl) c = mix(WHITE, c, 18);
      buf[(y - by) * bw + x - bx] = c;
    }
}

/* what the beat does to the stage, a frame apart so it doesn't all repaint at once */
static int beat_n, beat_ph, beat_type, spk, bop, rbeat;
static uint8_t lamp[4];
static void pulses(void) {
  spk = beat_ph >= 16 && beat_ph < 116 ? 2 : 0;
  bop = beat_ph >= 33 && beat_ph < 133 ? 2 : 0;
  rbeat = beat_ph < 90;
  for (int l = 0; l < 4; l++) /* the beat's lamp flashes; in a chorus the others stay on */
    lamp[l] = (uint8_t)((beat_n & 3) == l ? (beat_ph < 150 ? 2 : beat_ph < 300) : beat_type == CHORUS || beat_type == DROP);
}

/* LED bars on the floor: they jump on the beat and fall */
static int vu(int i) {
  int top = 3 + (int)(((uint32_t)(beat_n * 12 + i) * 2654435761u >> 28) % 5);
  return clamp(top - beat_ph / 60, 1, 7);
}

/* lamps sit in the middle of 40-pixel columns: a flash repaints just those */
static int lamp_x(int l) { return 20 + 40 * l + (l >> 1) * 160; }

/* the stage: a back wall, a floor with LED bars, a speaker each side, four lamps */
static void stage(bool lanes_on) {
  int a = lanes_on ? clamp(80 - bx, 0, bw) : bw, b = lanes_on ? clamp(240 - bx, 0, bw) : bw;
  for (int j = 0; j < bh; j++) {
    color *p = buf + j * bw, c = bgrow[by + j];
    fill(p, a, c);
    fill(p + a, b - a, lanerow[by + j]);
    fill(p + b, bw - b, c);
  }
  for (int s = 0; s < 2; s++) {
    int cx = s ? 280 : 40;
    if (bx >= cx + 32 || bx + bw <= cx - 32) continue;
    rect(cx - 30, 102, 60, 111, RGB(0x2E2742), 32);
    rect(cx - 28, 104, 56, 107, RGB(0x17131F), 32);
    rect(cx - 30, 102, 60, 1, RGB(0x4A3F66), 32);
    cone(cx, 132, 12 + spk / 2, spk * 12);
    cone(cx, 180, 20 + spk, spk * 12);
    for (int i = 0; i < 6; i++)
      for (int k = 0, n = vu(s * 6 + i); k < n; k++)
        rect(cx - 27 + i * 9, 234 - k * 3, 7, 2, k > 5 ? RGB(0xFF3B4A) : k > 3 ? RGB(0xFFD23A) : RGB(0x3BE37A), 32);
  }
  /* lamps on a bar: the one of this beat is lit, all of them in a chorus */
  rect(0, 0, 80, 5, RGB(0x2A2A36), 32);
  rect(240, 0, 80, 5, RGB(0x2A2A36), 32);
  for (int l = 0; l < 4; l++) {
    int lx = lamp_x(l), lv = lamp[l];
    if (bx >= lx + 22 || bx + bw <= lx - 22) continue;
    if (lv) /* the beam */
      for (int y = maxi(by, 12); y < mini(by + bh, 100); y++) {
        int hw = 2 + (y - 12) / 5;
        rect(lx - hw, y, 2 * hw + 1, 1, lanec[l], (100 - y) * lv / 22);
      }
    disc(lx, 9, 7, 0, RGB(0x15141C), 32);
    disc(lx, 10, 4, 0, lv ? mix(WHITE, lanec[l], lv == 2 ? 16 : 4) : mix(0, lanec[l], 22), 32);
  }
  if (lanes_on)
    for (int k = 0; k < 5; k++) rect(80 + 40 * k - (k == 4), 0, 1, H, WHITE, k % 4 ? 2 : 4);
}

static void stage_watches(void) {
  pulses();
  for (int s = 0; s < 2; s++) {
    int cx = s ? 280 : 40;
    watch(16 + s * 2, cx - 26, 154, 52, 52, spk);
    watch(17 + s * 2, cx - 17, 115, 34, 34, spk);
    int key = 0;
    for (int i = 0; i < 6; i++) key = key << 3 | vu(s * 6 + i);
    watch(24 + s, cx - 27, 214, 54, 22, key);
  }
  for (int l = 0; l < 4; l++) watch(12 + l, lamp_x(l) - 20, 0, 40, 100, lamp[l]);
}

/* ------------------------------------------------------------------ playing */
#define WIN 140 /* later than this, a note is missed */
static const char *const judge_name[4] = {"Perfect", "Great", "Good", "Miss"};
static const color judge_c[4][2] = {{RGB(0xFFF4A8), RGB(0xFFA800)}, {RGB(0xC6FFB0), RGB(0x2BD35A)}, {RGB(0x9FE8FF), RGB(0x2E7BFF)}, {RGB(0xFFB0B8), RGB(0xE0203A)}};

static struct {
  int32_t t0; /* millis at song time 0 */
  int32_t t, pt, end;
  int song, d, score, combo, maxc, health, judged, acc, cnt[4], held_mask;
  uint32_t jt, ht[4], ct;
  int jk, late, hk[4];
  int pxms, ry, sgn, ahead; /* 1/256 pixel per ms; the targets' row; +1 when arrows go down */
  bool failed, paused, confirm;
  int menu;
  uint32_t resume;
} P;
static int info_notes, info_len;

static int pxms(void) { return (2 + S.speed) * 31 / 2; } /* 1/256 pixel per ms */
static int note_y(int32_t dt) { return P.ry - P.sgn * (int)(dt * P.pxms >> 8); }
static int lane_x(int l) { return 100 + 40 * l; }
static bool gone(int y) { return P.sgn > 0 ? y > H + 24 : y < -24; }
static bool not_yet(int y) { return P.sgn > 0 ? y < -24 : y > H + 24; }

/* what a note covers now: its arrow and the end of its trail */
static void note_mark(const note_t *n, int32_t t) {
  int x = lane_x(n->lane);
  if (n->st == N_WAIT || n->st == N_MISS) mark(x - 20, note_y(n->t - t) - 20, 40, 40);
  if (n->len) mark(x - 8, note_y(n->t + n->len - t) - 8, 16, 16);
}
/* all of it, when it changes look */
static void note_mark_all(const note_t *n) {
  int x = lane_x(n->lane), y0 = n->st == N_HOLD ? P.ry : note_y(n->t - P.t), y1 = note_y(n->t + n->len - P.t);
  mark(x - 20, mini(y0, y1) - 20, 40, iabs(y1 - y0) + 40);
}

static void start_song(void) {
  int song = S.song, d = S.diff;
  zero(&P, sizeof P);
  P.song = song, P.d = d, P.health = 500;
  gen_start(song, d);
  gen_bar(); /* the first bar gives the beat of the count-in */
  P.pxms = pxms();
  P.sgn = S.up ? -1 : 1;
  P.ry = S.up ? 40 : 200;
  P.ahead = 250 * 256 / P.pxms;
  int beat = 60000 / G.bpm, lead = maxi(4, (P.ahead + 300) / beat + 1) * beat;
  P.t = P.pt = -lead;
  P.t0 = (int32_t)now + lead;
  for (int l = 0; l < 4; l++) P.ht[l] = now - 9999;
  P.jt = P.ct = now - 9999;
  nev = 0, rep_ok = 0;
  screen = S_PLAY;
  for (int i = 0; i < NW; i++) wt[i].key = -1;
  mark_all();
}

static void judge_show(int k, int late) {
  P.jk = k, P.late = late, P.jt = now;
  if (k < 3) P.combo++, P.ct = now;
  else P.combo = 0;
  P.maxc = maxi(P.maxc, P.combo);
}

static void hit_note(note_t *n, int d) {
  static const int16_t pts[3] = {350, 200, 100}, hp[3] = {20, 14, 5}, acc[3] = {100, 75, 40};
  int ad = iabs(d), k = ad <= 45 ? 0 : ad <= 90 ? 1 : 2;
  P.cnt[k]++, P.judged++, P.acc += acc[k], P.score += pts[k];
  P.health = mini(P.health + hp[k], 1000);
  judge_show(k, k ? (d > 0 ? 2 : 1) : 0);
  P.ht[n->lane] = now, P.hk[n->lane] = k;
  note_mark_all(n);
  n->st = n->len ? N_HOLD : N_DONE;
}

static void miss(void) {
  P.cnt[3]++, P.judged++;
  P.health -= P.d ? 40 : 25; /* a dozen misses in a row from the start fail a song */
  judge_show(3, 0);
}

static void press_lane(int l, int32_t tp) {
  for (int i = nt; i != nh; i = (i + 1) & (NR - 1)) {
    note_t *n = &notes[i];
    if (n->lane != l || n->st != N_WAIT) continue;
    int d = (int)(tp - n->t);
    if (d < -WIN) return; /* too early for anything: a free tap */
    if (d <= WIN) {
      hit_note(n, d);
      return;
    }
  }
}

static void end_hold(note_t *n, int32_t tr) {
  note_mark_all(n);
  if (tr >= n->t + n->len - 100) { /* held to the end */
    n->st = N_DONE;
    P.score += 150, P.health = mini(P.health + 10, 1000);
    P.ht[n->lane] = now;
  } else { /* let go: the rest of the trail goes gray */
    n->len = (uint16_t)(n->t + n->len - tr), n->t = tr, n->st = N_DROP;
    P.cnt[3]++, P.judged++, P.health -= 25;
    judge_show(3, 0);
    note_mark_all(n);
  }
}

static void release_lane(int l, int32_t tr) {
  for (int i = nt; i != nh; i = (i + 1) & (NR - 1))
    if (notes[i].lane == l && notes[i].st == N_HOLD) end_hold(&notes[i], tr);
}

static void results(void);
/* "Song - Difficulty" */
static void song_line(char *s) { cat(cat(cat(s, songs[P.song].name), " - "), diff_name[P.d]); }

static void play_update(void) {
  P.pt = P.t;
  P.t = (int32_t)(now - (uint32_t)P.t0);
  int32_t tj = P.t - S.offset;
  while (!G.done && (G.t16 >> 4) < P.t + P.ahead + 400 && ((nh - nt) & (NR - 1)) < NR - 40) gen_bar();
  if (G.done && !P.end) P.end = G.t16 >> 4;
  for (int i = 0; i < nev; i++) {
    int32_t tp = (int32_t)(evq[i].t - (uint32_t)P.t0) - S.offset;
    if (evq[i].down) press_lane(evq[i].lane, tp);
    else release_lane(evq[i].lane, tp);
  }
  nev = 0;
  bool left = false;
  P.held_mask = 0;
  for (int i = nt; i != nh; i = (i + 1) & (NR - 1)) {
    note_t *n = &notes[i];
    if (n->st == N_WAIT && tj - n->t > WIN) {
      note_mark_all(n);
      n->st = N_MISS;
      miss();
    } else if (n->st == N_HOLD) {
      if (tj >= n->t + n->len) end_hold(n, tj);
      else if (!(lanes >> n->lane & 1)) end_hold(n, tj);
      else P.held_mask |= 1 << n->lane;
    }
    left |= n->st <= N_HOLD;
    note_mark(n, P.pt);
    note_mark(n, P.t);
  }
  while (nt != nh) { /* forget what is done and off the screen */
    note_t *n = &notes[nt];
    if (n->st == N_DONE || (n->st >= N_MISS && gone(note_y(n->t + n->len - P.t)))) nt = (nt + 1) & (NR - 1);
    else break;
  }
  /* beat lines */
  for (int i = maxi(0, nbars - 8); i < nbars; i++)
    for (int k = i ? 0 : -8; k < 4; k++) {
      int32_t tb = (bars[i & 7].t16 + k * bars[i & 7].beat16) >> 4;
      mark(80, note_y(tb - P.t), 160, 1);
      mark(80, note_y(tb - P.pt), 160, 1);
    }
  if (P.health <= 0) {
    P.health = 0;
    if (!S.nofail || !G.s) P.failed = true;
  }
  if (P.failed || (P.end && !left && tj > P.end + 500)) results();
}

/* the parts of the play screen that change in place */
static void play_watches(void) {
  for (int l = 0; l < 4; l++) {
    int age = (int)(now - P.ht[l]), x = lane_x(l);
    int key = age < 160 || (P.held_mask >> l & 1) ? (age < 50 ? 3 : 2) | 8 : lanes >> l & 1 ? 16 : 1 + rbeat;
    watch(l, x - 20, P.ry - 20, 40, 40, key);
    watch(4 + l, x - 30, P.ry - 30, 60, 60, age < 170 && !P.hk[l] ? age / 34 : -1);
  }
  int ja = (int)(now - P.jt), jy = S.up ? 118 : 96;
  char s[12];
  int w = tw(judge_name[P.jk], 3) + 8;
  watch(8, 160 - w / 2, jy - 14, w, 42, ja < 500 ? P.jk | P.late << 2 | (ja < 50) << 4 : -1);
  num(s, (uint32_t)P.combo);
  w = maxi(tw(s, 4), 30) + 8;
  watch(9, 160 - w / 2, jy + 24, w, 46, P.combo >= 4 ? P.combo << 4 | (now - P.ct < 80) : -1);
  int hy = S.up ? 222 : 4, split = 62 + 196 * (1000 - P.health) / 1000;
  watch(23, split - 25, hy - 8, 50, 27, split | bop << 10 | (P.health < 200) << 12 | (P.health > 800) << 13); /* and the bar */
  watch(11, 60, S.up ? 206 : 26, 200, 11, P.score * 7 + P.cnt[3] * 131 + P.acc * 3 + P.judged);
  int n, ty, ph = beat_at(P.t, &n, &ty);
  watch(20, 60, 90, 200, 40, P.t < 0 && n >= -3 && ph < 300 ? n * 4 + (ph < 50) : -1);
}

static void draw_notes(void) {
  for (int pass = 0; pass < 2; pass++)
    for (int i = nt; i != nh; i = (i + 1) & (NR - 1)) {
      const note_t *n = &notes[i];
      int x = lane_x(n->lane), y = note_y(n->t - P.t);
      if (not_yet(y)) break;
      if (n->st == N_DONE || x + 20 <= bx || x - 20 >= bx + bw) continue;
      if (!pass && n->len) { /* the trail */
        int ya = n->st == N_HOLD ? P.ry : y, yb = note_y(n->t + n->len - P.t), y0 = mini(ya, yb), y1 = maxi(ya, yb);
        color c = n->st >= N_MISS ? RGB(0x34344A) : lanec[n->lane]; /* gray once missed */
        rect(x - 6, y0, 12, y1 - y0, INK, 32);
        rect(x - 4, y0, 8, y1 - y0, c, 32);
        rect(x - 2, y0, 2, y1 - y0, mix(WHITE, c, 8), 32);
        disc(x, yb, 6, 0, INK, 32);
        disc(x, yb, 4, 0, c, 32);
      }
      if (pass && (n->st == N_WAIT || n->st == N_MISS))
        arrow(n->lane, x, y, 1, pal[n->st == N_MISS ? P_MISS : P_NOTE + n->lane], n->st == N_MISS ? 18 : 32);
    }
}


static void icon(int cx, int cy, int r, color c, bool sad) {
  disc(cx, cy, r + 2, 0, INK, 32);
  disc(cx, cy, r, 0, c, 32);
  rect(cx - r / 2 - 1, cy - r / 3, 3, 4, INK, 32);
  rect(cx + r / 2 - 1, cy - r / 3, 3, 4, INK, 32);
  int m = cy + r / 3 + 1, e = sad ? 2 : -2; /* the mouth's corners go up or down */
  rect(cx - 3, m, 7, 2, INK, 32);
  rect(cx - 5, m + e, 2, 2, INK, 32);
  rect(cx + 4, m + e, 2, 2, INK, 32);
}

static void hud(void) {
  char s[48], *o;
  /* the health bar, with the two singers' faces where the colours meet */
  int hy = S.up ? 222 : 4, split = 62 + 196 * (1000 - P.health) / 1000;
  if (by < hy + 20 && by + bh > hy - 6) {
    rect(60, hy, 200, 10, INK, 32);
    rect(62, hy + 2, split - 62, 6, RGB(0xFF2A3D), 32);
    rect(split, hy + 2, 258 - split, 6, RGB(0x57F23B), 32);
    rect(62, hy + 2, 196, 2, WHITE, 6);
    icon(split - 11, hy + 5, 9 + bop, RGB(0xB164E8), P.health > 800);
    icon(split + 11, hy + 5, 9 + bop, RGB(0x39B8E6), P.health < 200);
  }
  int sy = S.up ? 207 : 27;
  if (by < sy + 10 && by + bh > sy - 2) {
    o = cat(s, "Score ");
    o = num(o, (uint32_t)P.score);
    o = cat(o, "  Misses ");
    o = num(o, (uint32_t)P.cnt[3]);
    o = cat(o, "  ");
    if (P.judged) pct(o, P.acc * 100 / P.judged);
    else cat(o, "--%");
    small(s, 160, sy, WHITE);
  }
}

static void scene_play(void) {
  stage(true);
  /* beat lines, brighter at the start of a bar */
  for (int i = maxi(0, nbars - 8); i < nbars; i++)
    for (int k = i ? 0 : -8; k < 4; k++) {
      int y = note_y(((bars[i & 7].t16 + k * bars[i & 7].beat16) >> 4) - P.t);
      rect(81, y, 158, 1, WHITE, k ? 3 : 6);
    }
  /* the targets */
  for (int l = 0; l < 4; l++) {
    int age = (int)(now - P.ht[l]), x = lane_x(l);
    if (age < 160 || (P.held_mask >> l & 1)) arrow(l, x, P.ry, age < 50 ? 3 : 2, pal[P_CONF + l], 32);
    else if (lanes >> l & 1) arrow(l, x, P.ry, 0, pal[P_PRESS + l], 32);
    else arrow(l, x, P.ry, 1 + rbeat, pal[rbeat ? P_BEAT : P_STRUM], 32);
  }
  /* the judgment and the combo */
  int ja = (int)(now - P.jt), jy = S.up ? 118 : 96;
  if (ja < 500) {
    int y = jy + (ja < 50) * 3;
    ctext(judge_name[P.jk], 160, y, 3, 2, judge_c[P.jk][0], judge_c[P.jk][1]);
    if (P.late) small(P.late == 2 ? "Late" : "Early", 160, y - 11, P.late == 2 ? RGB(0xFFB070) : RGB(0x80C8FF));
  }
  if (P.combo >= 4) {
    char s[12];
    num(s, (uint32_t)P.combo);
    int pop = now - P.ct < 80;
    ctext(s, 160, jy + 30 - pop * 2, 4, 2, WHITE, RGB(0xC8C8DC));
    small("Combo", 160, jy + 60, RGB(0xD8D8E8));
  }
  draw_notes();
  /* a splash on a perfect hit */
  for (int l = 0; l < 4; l++) {
    int age = (int)(now - P.ht[l]);
    if (age >= 170 || P.hk[l]) continue;
    age = age / 34 * 34; /* in five steps */
    int r = 16 + age / 14, a = 30 - age * 30 / 170, x = lane_x(l);
    disc(x, P.ry, r, 3 - age / 60, mix(WHITE, lanec[l], 12), a);
    for (int k = 0; k < 4; k++) disc(x + (k & 1 ? r : -r) * 3 / 4, P.ry + (k & 2 ? r : -r) * 3 / 4, 2, 0, WHITE, a);
  }
  hud();
  /* ready, set, go! on the beats before the song */
  int n, ty, ph = beat_at(P.t, &n, &ty);
  if (P.t < 0 && n >= -3 && ph < 300) {
    static const char *const cd[3] = {"Ready?", "Set", "Go!"};
    ctext(cd[n + 3], 160, 96 + (ph < 50) * 3, 4, 2, WHITE, RGB(0xFFE070));
  }
  if (P.resume) {
    char s[2] = {(char)('1' + (P.resume - now) / 400), 0};
    rect(0, 0, W, H, 0, 10);
    rect(122, 78, 76, 72, INK, 26);
    ctext(s, 160, 90, 7, 3, WHITE, RGB(0xFFE070));
  }
  if (P.paused) {
    static const char *const items[3] = {"Resume", "Restart", "Quit game"};
    rect(0, 0, W, H, 0, 20);
    rect(70, 52, 180, 136, INK, 28);
    rect(70, 52, 180, 2, lanec[P.song & 3], 32);
    ctext(P.confirm ? "Quit game?" : "Paused", 160, 64, 3, 2, WHITE, RGB(0xC8C8DC));
    char s[32];
    song_line(s);
    small(s, 160, 92, RGB(0x9A94B8));
    for (int i = 0; i < (P.confirm ? 2 : 3); i++) {
      item(P.confirm ? (i ? "No" : "Yes") : items[i], 160, 112 + i * 24, i == P.menu);
    }
  }
}

static void play_frame(void) {
  if (P.paused) {
    int n = P.confirm ? 2 : 3, m = clamp(P.menu + ud(), 0, n - 1);
    if (m != P.menu) P.menu = m, mark_all();
    if (pressed(eadk_key_back)) {
      if (P.confirm) P.confirm = false, P.menu = 2;
      else P.paused = false, P.resume = now + 1199;
      mark_all();
    } else if (ok_key()) {
      if (P.confirm) {
        if (!P.menu) quit = true; /* back to NumPlay (or the calculator) */
        else P.confirm = false, P.menu = 2;
      } else if (P.menu == 0) P.paused = false, P.resume = now + 1199;
      else if (P.menu == 1) start_song();
      else P.confirm = true, P.menu = 1;
      mark_all();
    }
    nev = 0;
    return;
  }
  if (P.resume) {
    nev = 0;
    if ((int32_t)(now - P.resume) >= 0) P.resume = 0, P.t0 = (int32_t)now - P.t, P.pt = P.t, mark_all();
    else watch(21, 100, 80, 120, 70, (int)(P.resume - now) / 400);
    return;
  }
  if (pressed(eadk_key_back)) {
    P.paused = true, P.menu = 0, P.confirm = false, rep_ok = 0;
    mark_all();
    return;
  }
  play_update();
  if (screen != S_PLAY) return;
  beat_ph = beat_at(P.t, &beat_n, &beat_type);
  pulses();
  play_watches();
  stage_watches();
}

/* ------------------------------------------------------------------ results */
static int grade, grade_bars, new_best;
static void results(void) {
  int acc = P.judged ? P.acc * 100 / P.judged : 0, ty;
  beat_at(P.t, &grade_bars, &ty);
  grade_bars /= 4;
  bool fc = !P.cnt[3] && !P.failed;
  static const int16_t need[5] = {7000, 8000, 9000, 9500, 9900}; /* accuracy for C, B, A, S, S+ */
  grade = 1;
  while (grade < 6 && acc >= need[grade - 1] && (grade < 5 || fc)) grade++;
  if (P.failed && G.s) grade = 0;
  new_best = 0;
  if (!P.failed || !G.s) {
    uint8_t *g = &S.grade[P.song][P.d], ng = (uint8_t)(maxi(grade, *g & 7) | (*g & 8) | (fc ? 8 : 0));
    if ((uint32_t)P.score > S.best[P.song][P.d]) S.best[P.song][P.d] = (uint32_t)P.score, new_best = 1;
    if (ng != *g || new_best) *g = ng, save_due = true;
  }
  if (save_due) save();
  go(S_RESULTS, 0);
}

static const char *const grade_name[7] = {"F", "D", "C", "B", "A", "S", "S+"};
static const color grade_c[7] = {RGB(0xFF4050), RGB(0xB0A0C0), RGB(0xC080FF), RGB(0x40B8FF), RGB(0x40E070), RGB(0xFFD040), RGB(0xFFF0A0)};

static void scene_results(void) {
  char s[40], *o;
  stage(false);
  rect(0, 0, W, H, 0, 20);
  song_line(s);
  ctext(s, 160, 8, 2, 2, WHITE, RGB(0xC8C8DC));
  /* the grade on a badge */
  color gc = grade_c[grade];
  disc(80, 94, 52, 0, INK, 24);
  disc(80, 94, 52, 4, gc, 32);
  if (P.failed && G.s) ctext("Failed", 80, 87, 2, 2, RGB(0xFFB0B8), RGB(0xE0203A));
  else ctext(grade_name[grade], 80, 66, 8, 3, gc, mix(0, gc, 9));
  if (!P.cnt[3] && !P.failed) small("Full combo!", 80, 156, RGB(0x9FE8FF));
  if (!G.s) {
    o = cat(s, "Bars: ");
    num(o, (uint32_t)grade_bars);
    small(s, 80, 172, WHITE);
  }
  static const char *const lbl[6] = {"Perfect", "Great", "Good", "Miss", "Max combo", "Accuracy"};
  rect(160, 34, 150, 100, INK, 18);
  for (int i = 0; i < 6; i++) {
    int y = 40 + i * 16;
    text(lbl[i], 168, y, 1, 1, i < 4 ? judge_c[i][0] : WHITE, i < 4 ? judge_c[i][1] : 0);
    if (i == 5) pct(s, P.judged ? P.acc * 100 / P.judged : 0);
    else num(s, (uint32_t)(i < 4 ? P.cnt[i] : P.maxc));
    text(s, 302 - tw(s, 1), y, 1, 1, WHITE, 0);
  }
  small("Score", 235, 142, RGB(0xC8C8DC));
  num(s, (uint32_t)P.score);
  ctext(s, 235, 154, 3, 2, WHITE, RGB(0xFFE070));
  if (new_best && now % 600 < 400) small("New best!", 235, 182, RGB(0xFFE070));
  static const char *const items[2] = {"Retry", "Songs"};
  for (int i = 0; i < 2; i++) item(items[i], 100 + i * 120, 212, sel == i);
}

/* ------------------------------------------------------------------ menus */
static int lpos; /* the song list's scroll, 1/16 of an item */

static void song_info(void) {
  if (S.song == NSONG - 1) return;
  gen_start(S.song, S.diff);
  G.counting = true;
  while (!G.done) gen_bar();
  info_notes = G.count, info_len = (G.t16 >> 4) / 1000;
}

static void scene_title(void) {
  stage(false);
  /* the name hops, letter after letter, on every beat */
  static const char *const words[2] = {"Num", "Dance"};
  for (int w = 0, i = 0; w < 2; w++) {
    const char *s = words[w];
    int x = 160 - tw(s, 6) / 2, y = 14 + w * 50;
    for (int k = 0; s[k]; k++, i++) {
      int p = beat_ph - i * 30, hop = p >= 0 && p < 160 ? (p < 60 ? p / 8 : (160 - p) * 7 / 100) : 0;
      char c[2] = {s[k], 0};
      color lc = lanec[i & 3];
      text(c, x + k * 36, y - hop, 6, 3, mix(WHITE, lc, 10), mix(0, lc, 6));
    }
  }
  for (int l = 0; l < 4; l++) {
    bool on = (menu_beat & 3) == l && beat_ph < 250;
    arrow(l, 100 + 40 * l, 134, on ? 3 : 1, pal[on ? P_CONF + l : P_STRUM], 32);
  }
  static const char *const items[3] = {"Play", "Options", "How to play"};
  for (int i = 0; i < 3; i++) item(items[i], 160, 166 + i * 24, sel == i);
  if (tq) {
    rect(0, 0, W, H, 0, 12);
    rect(60, 140, 200, 94, INK, 32); /* over the menu */
    rect(60, 140, 200, 2, lanec[0], 32);
    ctext("Quit game?", 160, 150, 3, 2, WHITE, RGB(0xC8C8DC));
    for (int i = 0; i < 2; i++) item(i ? "No" : "Yes", 160, 184 + i * 24, tsel == i);
  }
}

static void scene_songs(void) {
  int song = S.song;
  color c = songs[song].c;
  for (int j = 0; j < bh; j++) {
    int y = by + j;
    color *p = buf + j * bw;
    for (int i = 0; i < bw; i++) {
      int x = bx + i;
      p[i] = mix(c, RGB(0x0C0614), (((x + y * 2) >> 4) & 1 ? 11 : 9) - y * 7 / H);
    }
  }
  for (int i = 0; i < NSONG; i++) {
    int off = i * 16 - lpos, y = 128 + off * 34 / 16, x = 20 + iabs(off) / 2;
    if (y < 56 || y > H) continue;
    bool on = i == song;
    const char *name = songs[i].name;
    text(name, x, y, 3, 2, on ? WHITE : mix(c, RGB(0x9890B0), 6), on ? RGB(0xFFE070) : mix(c, RGB(0x686080), 6));
    int g = S.grade[i][S.diff] & 7;
    if (g && i < NSONG - 1) text(grade_name[g], x + tw(name, 3) + 10, y + 4, 2, 1, grade_c[g], 0);
  }
  /* the score box */
  char s[40], *o;
  rect(176, 0, 144, 60, 0, 18);
  o = cat(s, "< ");
  o = cat(o, diff_name[S.diff]);
  cat(o, " >");
  ctext(s, 248, 8, 2, 1, diff_c[S.diff], mix(0, diff_c[S.diff], 10));
  o = cat(s, "Best ");
  o = num(o, S.best[song][S.diff]);
  if (S.grade[song][S.diff] & 8) cat(o, " FC");
  small(s, 248, 28, WHITE);
  if (song < NSONG - 1) {
    o = num(s, songs[song].bpm);
    o = cat(o, " BPM  ");
    o = num(o, (uint32_t)info_len / 60);
    *o++ = ':';
    *o++ = (char)('0' + info_len % 60 / 10);
    *o++ = (char)('0' + info_len % 10);
    o = cat(o, "  ");
    o = num(o, (uint32_t)info_notes);
    cat(o, " notes");
  } else cat(s, "Faster and faster");
  small(s, 248, 42, RGB(0xC8C0E0));
  rect(0, 226, W, 14, 0, 18);
  small("OK: play   Left/Right: difficulty   Back: menu", 160, 229, RGB(0xC8C0E0));
}

static const char *const opt_name[5] = {"Scroll speed", "Direction", "Offset", "Calibrate offset", "No-fail"};
static void scene_options(void) {
  stage(false);
  rect(0, 0, W, H, 0, 16);
  heading("Options", 12);
  for (int i = 0; i < 5; i++) {
    char s[24] = "", *o = s;
    bool on = sel == i;
    int y = 58 + i * 28;
    color c = on ? WHITE : RGB(0x9A94B8);
    if (on) rect(12, y - 6, 296, 26, WHITE, 5);
    text(opt_name[i], 20, y, 2, on, c, 0);
    if (on && i != 3) o = cat(o, "< ");
    if (i == 0) o = num(o, 1 + S.speed / 2), *o++ = '.', *o++ = S.speed & 1 ? '5' : '0', o = cat(o, "x");
    else if (i == 1) o = cat(o, S.up ? "Up" : "Down");
    else if (i == 2) o = signed_ms(o, S.offset);
    else if (i == 4) o = cat(o, S.nofail ? "On" : "Off");
    if (on && i != 3) cat(o, " >");
    text(s, 300 - tw(s, 2), y, 2, on, on ? RGB(0xFFE070) : c, 0);
  }
  small("Offset: + if you tend to press late", 160, 206, RGB(0x9A94B8));
  small("Up/Down: choose   Left/Right: change   Back: done", 160, 222, RGB(0x9A94B8));
}

static void scene_help(void) {
  stage(false);
  rect(0, 0, W, H, 0, 18);
  heading("How to play", 10);
  static const char *const keys[4] = {"Left 4 1", "Down 5 2", "Up 8 3", "Right 6 +"};
  for (int l = 0; l < 4; l++) {
    arrow(l, 70 + 60 * l, 62, 1, pal[P_NOTE + l], 32);
    small(keys[l], 70 + 60 * l, 86, WHITE);
  }
  static const char *const lines[6] = {"Press an arrow's key just as it", "reaches the gray target. Keep holding",
                                       "the key along a trail. Misses drain", "the health bar; empty, the song fails.",
                                       "Back: pause   Home: quit", "The beat shows on the stage lights."};
  for (int i = 0; i < 6; i++) small(lines[i], 160, 110 + i * 14 + (i > 3) * 6, i > 3 ? RGB(0xB8B0D0) : WHITE);
  static const uint16_t jx[4] = {49, 143, 219, 289};
  for (int k = 0; k < 4; k++) ctext(judge_name[k], jx[k], 212, 2, 1, judge_c[k][0], judge_c[k][1]);
}

/* calibration: an arrow every half second, press as it lands */
static int cal_n, cal_sum, cal_last;
static uint32_t cal_t0;
static void scene_calib(void) {
  char s[16];
  stage(false);
  rect(0, 0, W, H, 0, 20);
  rect(120, 0, 80, H, 0, 14);
  int t = (int)(now - cal_t0), ry = S.up ? 40 : 200, sgn = S.up ? -1 : 1, px = pxms(), beat = t % 500 < 90;
  arrow(2, 160, ry, 1 + beat, pal[beat ? P_BEAT : P_STRUM], 32);
  for (int k = t / 500 - 1; k <= t / 500 + 8; k++) {
    int y = ry - sgn * ((k * 500 - t) * px >> 8);
    if (k >= 2) arrow(2, 160, y, 1, pal[P_NOTE + 2], 32);
  }
  ctext("Calibrate", 60, 10, 2, 1, WHITE, RGB(0xFFE070));
  if (cal_n) {
    small("Last tap", 60, 96, WHITE);
    signed_ms(s, cal_last);
    ctext(s, 60, 110, 2, 1, cal_last < 0 ? RGB(0x80C8FF) : RGB(0xFFB070), 0);
  }
  small(cal_n < 8 ? "Taps" : "Offset", 260, 96, WHITE);
  if (cal_n < 8) s[0] = (char)('0' + cal_n), cat(s + 1, " of 8");
  else signed_ms(s, cal_sum / 8);
  ctext(s, 260, 110, 2, 1, RGB(0xFFE070), 0);
  small(cal_n < 8 ? "Press any arrow as each one lands" : "OK: keep it   Back: cancel", 160, 226, WHITE);
}

static void scene(void) {
  switch (screen) {
    case S_TITLE: scene_title(); break;
    case S_SONGS: scene_songs(); break;
    case S_OPTIONS: scene_options(); break;
    case S_HELP: scene_help(); break;
    case S_CALIB: scene_calib(); break;
    case S_PLAY: scene_play(); break;
    default: scene_results();
  }
}

static void menu_frame(void) {
  int u = ud(), v = lr();
  bool ok = ok_key(), back = pressed(eadk_key_back);
  switch (screen) {
    case S_TITLE:
      if (tq) {
        if (u) tsel = clamp(tsel + u, 0, 1), mark_all();
        if (back || (ok && tsel)) tq = false, mark_all();
        else if (ok) quit = true;
        break;
      }
      if (back) {
        tq = true, tsel = 1, mark_all();
        break;
      }
      if (u) sel = clamp(sel + u, 0, 2), mark_all();
      if (ok) {
        if (sel == 0) song_info(), go(S_SONGS, 0), lpos = S.song * 16;
        else go(sel == 1 ? S_OPTIONS : S_HELP, 0);
      }
      break;
    case S_SONGS:
      if (u) S.song = (uint8_t)((S.song + NSONG + u) % NSONG), song_info(), save_due = true;
      if (v) S.diff = (uint8_t)((S.diff + 4 + v) & 3), song_info(), save_due = true, mark_all();
      if (lpos != S.song * 16) {
        int d = S.song * 16 - lpos;
        lpos += d / 3 ? d / 3 : d > 0 ? 1 : -1;
        mark_all();
      }
      if (ok) start_song();
      else if (back) go(S_TITLE, 0);
      break;
    case S_OPTIONS:
      if (u) sel = clamp(sel + u, 0, 4), mark_all();
      if (v) {
        if (sel == 0) S.speed = (uint8_t)clamp(S.speed + v, 0, 6);
        if (sel == 1) S.up ^= 1;
        if (sel == 2) S.offset = (int8_t)clamp(S.offset + v * 5, -120, 120);
        if (sel == 4) S.nofail ^= 1;
        save_due = true, mark_all();
      }
      if (ok && sel == 3) cal_n = cal_sum = 0, cal_t0 = now, nev = 0, go(S_CALIB, 0);
      else if (back) {
        if (save_due) save();
        go(S_TITLE, 1);
      }
      break;
    case S_HELP:
      if (ok || back) go(S_TITLE, 2);
      break;
    case S_CALIB:
      for (int i = 0; i < nev; i++)
        if (evq[i].down && cal_n < 8) {
          int t = (int)(evq[i].t - cal_t0), e = t - (t + 250) / 500 * 500;
          if ((t + 250) / 500 >= 2) cal_last = e, cal_sum += e, cal_n++;
        }
      nev = 0;
      if (ok && cal_n >= 8) S.offset = (int8_t)clamp(cal_sum / 8, -120, 120), save_due = true, go(S_OPTIONS, 2);
      else if (back) go(S_OPTIONS, 3);
      mark_all(); /* it all moves */
      break;
    case S_RESULTS:
      if (v) sel = clamp(sel + v, 0, 1), mark_all();
      if (ok && !sel) start_song();
      else if (ok || back) song_info(), go(S_SONGS, 0), lpos = S.song * 16;
      else if (new_best) watch(22, 190, 180, 90, 12, now % 600 < 400);
      break;
  }
  nev = 0;
  if (screen == S_TITLE) { /* the title's letters and targets move to a 120 BPM beat */
    beat_ph = (int)(now % 500), menu_beat = (int)(now / 500), beat_n = menu_beat, beat_type = 0;
    watch(0, 40, 0, 240, 120, beat_ph < 400 ? beat_ph / 16 : 99);
    for (int l = 0; l < 4; l++) watch(1 + l, 80 + 40 * l, 114, 40, 40, (menu_beat & 3) == l && beat_ph < 250);
    stage_watches();
  }
}

int main(void) {
  np_app_begin();
  make_arrows();
  make_palettes();
  make_stage();
  load();
  now = (uint32_t)eadk_timing_millis();
  held = eadk_keyboard_scan();
  poll();
  nev = 0, hit = 0;
  for (int i = 0; i < NW; i++) wt[i].key = -1;
  go(S_TITLE, 0);
  while (!quit) {
    now = (uint32_t)eadk_timing_millis();
    input();
    if (quit) break;
    if (screen == S_PLAY) play_frame();
    else menu_frame();
    flush();
    /* keep reading the keys until it is nearly time for the next frame */
    do {
      eadk_timing_msleep(1);
      poll();
    } while ((uint32_t)eadk_timing_millis() - now < 12);
    eadk_display_wait_for_vblank();
    while ((uint32_t)eadk_timing_millis() - now < 16) eadk_timing_msleep(1), poll(); /* where vblank does not wait */
  }
  if (save_due) save();
  return np_app_end();
}
