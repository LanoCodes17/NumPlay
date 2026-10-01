/* What is drawn over the 3D view: the crosshair, the hotbar with its icons,
 * and the block being mined. Drawn strip by strip, just before each strip
 * goes to the screen (render.c), so nothing flickers. */
#include "nb.h"

static int clip_y0, clip_y1;   /* the strip: screen rows [clip_y0, clip_y1) */
static uint16_t *clip_buf;

/* a packed sprite at (x, y), its palette index 0 see-through */
static void sprite(int id, int x, int y) {
  int w = spr_w[id], h = spr_h[id], stride = (w + 1) / 2;
  const uint8_t *px = spr_px + spr_off[id];
  const uint16_t *pal = spr_pal[id];
  for (int r = 0; r < h; r++) {
    int sy = y + r;
    if (sy < clip_y0 || sy >= clip_y1) continue;
    uint16_t *d = clip_buf + (sy - clip_y0) * SCREEN_W;
    const uint8_t *s = px + r * stride;
    for (int c = 0; c < w; c++) {
      int sx = x + c;
      if ((unsigned)sx >= SCREEN_W) continue;
      int i = (c & 1) ? s[c >> 1] >> 4 : s[c >> 1] & 15;
      if (i) d[sx] = pal[i];
    }
  }
}

/* Minecraft draws its crosshair inverting what is behind it */
static void crosshair(void) {
  for (int k = -4; k <= 4; k++) {
    int pts[2][2] = {{SCREEN_W / 2 + k, SCREEN_H / 2}, {SCREEN_W / 2, SCREEN_H / 2 + k}};
    for (int p = 0; p < 2; p++) {
      if (p == 1 && k == 0) continue;
      int x = pts[p][0], y = pts[p][1];
      if (y < clip_y0 || y >= clip_y1) continue;
      uint16_t *d = clip_buf + (y - clip_y0) * SCREEN_W + x;
      *d = (uint16_t)~*d;
    }
  }
}

void hud_strip(uint16_t *buf, int y0, int rows) {
  clip_buf = buf;
  clip_y0 = y0;
  clip_y1 = y0 + rows;
  crosshair();
  /* the hotbar, 182 x 22, centred at the bottom */
  int hx = (SCREEN_W - 182) / 2, hy = SCREEN_H - 22;
  if (clip_y1 > hy - 1) {
    sprite(SP_HOTBAR, hx, hy);
    sprite(SP_HOTBAR_SEL, hx - 1 + pl.slot * 20, hy - 1);
    for (int i = 0; i < 9; i++) {
      int b = pl.hotbar[i];
      if (b > 0 && b < B_COUNT && blk_icon[b] != 0xFFFF) sprite(blk_icon[b], hx + 3 + i * 20, hy + 3);
    }
  }
}
