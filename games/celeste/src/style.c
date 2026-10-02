/* Stylegrounds: the chapter's parallax backgrounds and effects (Celeste's
 * Backdrop, Parallax, Snow, ...), drawn in screen space behind and in front of
 * the level. */
#include "level.h"

#define MAX_SG 64
typedef struct {
  float x, y;          /* Position (moves with speed) */
  float fade;          /* fadeIn */
  bool visible;
} SgState;
static SgState sg[MAX_SG];
static int nbg, nfg;
float g_snow_alpha;   /* Snow.Alpha (the Prologue's ending fades the snow out) */
static float snow_tx, snow_ty;   /* the snow's time: modulo 320 s (when the flakes' x repeat) and 2 pi s (their wave) */

/* record: u8 effect, u8 flags, u16 tex, f32 x, y, scrollx, scrolly, speedx, speedy, alpha, u32 color,
 * u16 flag, notflag, tag, fadex, fadey, only */
static const uint8_t *rec(int i) {
  const uint8_t *c = g_level.ch;
  int nrooms = rd16(c + 2);
  return c + rd32(c + CH_ROOMS + 4 * nrooms + 4 * i);
}
enum { SF_LOOPX = 1, SF_LOOPY = 2, SF_FLIPX = 4, SF_FLIPY = 8, SF_INSTANTIN = 16, SF_INSTANTOUT = 32, SF_FADEIN = 64, SF_ADD = 128 };

static bool is_visible(int i) {
  const uint8_t *r = rec(i);
  const uint8_t *info = g_level.room->info;
  uint32_t lo = rd32(info + 28), hi = rd32(info + 32);
  bool in_room = i < 32 ? (lo >> i) & 1 : (hi >> (i - 32)) & 1;
  if (!in_room) return false;
  const char *flag = str(rd16(r + 36)), *notflag = str(rd16(r + 38));
  if (*flag && !level_get_flag(flag)) return false;
  if (*notflag && level_get_flag(notflag)) return false;
  return true;
}

void style_init(void) {
  const uint8_t *c = g_level.ch;
  g_snow_alpha = 1;
  nbg = c[12], nfg = c[13];
  for (int i = 0; i < nbg + nfg && i < MAX_SG; i++) {
    const uint8_t *r = rec(i);
    sg[i].x = rdf(r + 4), sg[i].y = rdf(r + 8);
    sg[i].visible = is_visible(i);
    sg[i].fade = sg[i].visible ? 1 : 0;
  }
}

void style_update(void) {
  snow_tx = fmodf(snow_tx + DT, 320), snow_ty = fmodf(snow_ty + DT, 2 * PI_F);
  for (int i = 0; i < nbg + nfg && i < MAX_SG; i++) {
    const uint8_t *r = rec(i);
    sg[i].visible = is_visible(i);
    sg[i].x += rdf(r + 20) * DT + g_level.wind.x * DT * 0;
    sg[i].y += rdf(r + 24) * DT;
    if (r[0] == SG_snowFg || r[0] == SG_snowBg) sg[i].fade = approach(sg[i].fade, sg[i].visible ? 1 : 0, DT * 2);   /* visibleFade */
    else if (r[1] & SF_FADEIN) sg[i].fade = approach(sg[i].fade, sg[i].visible ? 1 : 0, DT);
    else sg[i].fade = sg[i].visible ? 1 : 0;
  }
}

/* Snow: 60 flakes blown left, a pixel each. A flake's start, speed, color and wave come from its number, where it is
 * now from the time (Snow.Update's steps, worked out): the flakes need no memory */
typedef struct { float camx; uint8_t fg, a, row[60]; } SnowDraw;   /* row: each flake's on the screen (this frame) */
static SnowDraw snow_draws[2];
static uint32_t flake_hash(int k, int fg) { return (uint32_t)(k + 1 + fg * 60) * 2654435761u; }
static float flake_speed(uint32_t h, int fg) { return (float)((fg ? 120 : 40) + (int)(h >> 8) % (fg ? 180 : 60)); }
static void snow_strip(uint16_t *strip, int y0, int y1, void *ctx) {
  const SnowDraw *s = ctx;
  static const uint16_t COLORS[2][2] = {{0x3186, 0x198F}, {0xFFFF, 0x64BD}};   /* Background-, ForegroundColors */
  int a = s->a + (s->a >> 7);
  for (int k = 0; k < 60; k++) {
    int py = s->row[k];
    if (py < y0 || py >= y1) continue;
    uint32_t h = flake_hash(k, s->fg);
    float x = (float)(h >> 22) * (320 / 1024.f) - flake_speed(h, s->fg) * snow_tx - s->camx;
    int px = (int)(x - floorf(x / 320) * 320);
    uint16_t *d = strip + (py - y0) * VIEW_W + (px < VIEW_W ? px : VIEW_W - 1), c = COLORS[s->fg][h >> 31];
    *d = a >= 256 ? c : blend565(*d, scale565(c, a), a);
  }
}
static void snow(int i, bool fg) {
  float a = sg[i].fade * g_snow_alpha;
  if (a <= 0) return;
  SnowDraw *s = &snow_draws[fg];
  s->camx = g_level.cam.x, s->fg = fg, s->a = a8(a);
  float half = snow_ty / 2, sh = sinf(half);
  for (int k = 0; k < 60; k++) {   /* y: the wave's sum, cos(w) - cos(w + t) = 2 sin(w + t / 2) sin(t / 2) */
    uint32_t h = flake_hash(k, fg), g = h * 2246822519u;
    float y = (float)(g >> 22) * (180 / 1024.f) + flake_speed(h, fg) * 0.4f * sinf((g & 1023) * (2 * PI_F / 1024) + half) * sh -
              g_level.cam.y;
    int py = (int)(y - floorf(y / 180) * 180);
    s->row[k] = (uint8_t)(py < VIEW_H ? py : VIEW_H - 1);
  }
  gfx_custom(snow_strip, s, 0, VIEW_H);
}

static void parallax(int i) {
  const uint8_t *r = rec(i);
  uint16_t tex = rd16(r + 2);
  Tex t;
  if (tex == 0xFFFF || !tex_get(tex, &t)) return;
  float camx = floorf(g_level.cam.x), camy = floorf(g_level.cam.y);
  float px = floorf(sg[i].x - camx * rdf(r + 12)), py = floorf(sg[i].y - camy * rdf(r + 16));
  float alpha = sg[i].fade * rdf(r + 28);
  uint32_t col = rd32(r + 32);
  uint16_t tint = col == 0xFFFFFF ? 0xFFFF : rgb(col);
  if (alpha <= 1 / 255.f) return;
  uint8_t flags = r[1];
  int fw = t.fw * t.scale, fh = t.fh * t.scale;
  if (flags & SF_LOOPX) {
    while (px < 0) px += fw;
    while (px > 0) px -= fw;
  }
  if (flags & SF_LOOPY) {
    while (py < 0) py += fh;
    while (py > 0) py -= fh;
  }
  uint8_t gf = (flags & SF_FLIPX ? GF_FLIPX : 0) | (flags & SF_FLIPY ? GF_FLIPY : 0) | (flags & SF_ADD ? GF_ADD | GF_ADDALPHA : 0);
  uint8_t a = (uint8_t)(alpha * 255);
  for (float x = px; x < 320; x += fw) {
    for (float y = py; y < 180; y += fh) {
      gfx_tex(tex, x, y, gf, tint, a);
      if (!(flags & SF_LOOPY)) break;
    }
    if (!(flags & SF_LOOPX)) break;
  }
}

void style_render(bool fg) {
  gfx_screen(true);
  int from = fg ? nbg : 0, to = fg ? nbg + nfg : nbg;
  for (int i = from; i < to && i < MAX_SG; i++) {
    const uint8_t *r = rec(i);
    if (sg[i].fade <= 0) continue;
    switch (r[0]) {
      case SG_parallax: parallax(i); break;
      case SG_snowFg:
      case SG_snowBg: snow(i, r[0] == SG_snowFg); break;
      default: break;
    }
  }
  gfx_screen(false);
}
