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
  nbg = c[12], nfg = c[13];
  for (int i = 0; i < nbg + nfg && i < MAX_SG; i++) {
    const uint8_t *r = rec(i);
    sg[i].x = rdf(r + 4), sg[i].y = rdf(r + 8);
    sg[i].visible = is_visible(i);
    sg[i].fade = sg[i].visible ? 1 : 0;
  }
}

void style_update(void) {
  for (int i = 0; i < nbg + nfg && i < MAX_SG; i++) {
    const uint8_t *r = rec(i);
    sg[i].visible = is_visible(i);
    sg[i].x += rdf(r + 20) * DT + g_level.wind.x * DT * 0;
    sg[i].y += rdf(r + 24) * DT;
    if (r[1] & SF_FADEIN) sg[i].fade = approach(sg[i].fade, sg[i].visible ? 1 : 0, DT);
    else sg[i].fade = sg[i].visible ? 1 : 0;
  }
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
  uint8_t gf = (flags & SF_FLIPX ? GF_FLIPX : 0) | (flags & SF_FLIPY ? GF_FLIPY : 0) | (flags & SF_ADD ? GF_ADD : 0);
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
      default: break;
    }
  }
  gfx_screen(false);
}
