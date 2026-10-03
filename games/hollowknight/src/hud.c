/* The HUD (Hud Canvas, drawn by its own camera): the masks, the soul orb, the geo count. Places in HUD units from the
 * screen's center. */
#include <math.h>
#include "game.h"

#define DT 0.02f
#define MAX_MASKS 11

/* (Hud Canvas, then Health, Soul Orb, Geo Counter: their places) */
#define CANVAS_X (-8.71f)
#define CANVAS_Y 6.6f
#define HEALTH_X (CANVAS_X + 8.19f)
#define HEALTH_Y (CANVAS_Y - 7.51f)
#define ORB_X (CANVAS_X - 2.99f)
#define ORB_Y (CANVAS_Y - 0.9f)
#define FILL_X (ORB_X + 1.56f)
#define FILL_Y (ORB_Y - 0.68f)
#define LIQUID_BOTTOM_Y (-0.59f)   /* (Soul Orb Control: Liquid Bottom Y, Liquid Y Per MP) */
#define LIQUID_Y_PER_MP 0.0171f
#define FOCUS_MP 33

static struct {
  Anim mask[MAX_MASKS];
  int8_t shown_health;   /* what the masks show */
  Anim frame, liquid, coin;
  float liquid_grey;     /* (Liquid Control: grey while it cannot heal) */
  bool started;
} hd;

void hud_reset(void) {
  memset(&hd, 0, sizeof hd);
  hd.shown_health = -1;
  anim_play(&hd.frame, CLIP_HUD_HUD_FRAME_IDLE);
  anim_play(&hd.liquid, CLIP_LIQUID_IDLE);
  anim_play(&hd.coin, CLIP_HUD_COIN_IDLE);
}

void hud_tick(void) {
  if (!hd.started) hud_reset(), hd.started = true;
  int health = g_pd.health, max = g_pd.max_health > MAX_MASKS ? MAX_MASKS : g_pd.max_health;
  if (hd.shown_health < 0) {
    for (int i = 0; i < max; i++) anim_play(&hd.mask[i], i < health ? CLIP_HUD_HEALTH_IDLE : CLIP_HUD_HEALTH_EMPTY);
    hd.shown_health = (int8_t)health;
  }
  /* (health_display: a mask lost breaks, one got back refills) */
  while (hd.shown_health > health) anim_play_from_frame(&hd.mask[--hd.shown_health], CLIP_HUD_HEALTH_BREAK, 0);
  while (hd.shown_health < health && hd.shown_health < max) anim_play_from_frame(&hd.mask[hd.shown_health++], CLIP_HUD_HEALTH_REFILL, 0);
  for (int i = 0; i < max; i++) {
    Anim *a = &hd.mask[i];
    a->events = 0;
    anim_update(a, DT);
    if (a->events & ANIM_DONE) anim_play(a, a->clip == CLIP_HUD_HEALTH_BREAK ? CLIP_HUD_HEALTH_EMPTY : CLIP_HUD_HEALTH_IDLE);
  }
  anim_update(&hd.frame, DT);
  anim_update(&hd.liquid, DT);
  anim_update(&hd.coin, DT);
  /* EaseColor over 0.2 s, to grey or white */
  float to = g_pd.mp < FOCUS_MP ? 1 : 0;
  if (hd.liquid_grey < to) hd.liquid_grey = fminf(to, hd.liquid_grey + DT / 0.2f);
  else if (hd.liquid_grey > to) hd.liquid_grey = fmaxf(to, hd.liquid_grey - DT / 0.2f);
}

static void hud_sprite(int sprite, float x, float y, float sx, uint8_t tint, int clip) {
  Inst in;
  sprite_inst(sprite, x, y, 0, sx, 1, tint, &in);
  gfx_hud(&in, clip);
}

void hud_draw(void) {
  if (!hd.started) return;
  uint8_t white = gfx_dyn_tint(8, 255, 255, 255, 255);
  /* the soul orb: its liquid (in the orb's circle), the frame over it */
  if (g_pd.mp > 1) {
    float y = FILL_Y + LIQUID_BOTTOM_Y + g_pd.mp * LIQUID_Y_PER_MP;
    uint8_t g = (uint8_t)(255 - (255 - 110) * hd.liquid_grey);
    gfx_hud_clip(1, FILL_X - 2.28f, FILL_Y + 1.32f, 0.97f);
    hud_sprite(hd.liquid.sprite, FILL_X - 2.23f, y, 1, gfx_dyn_tint(9, g, g, g, 255), 1);
  }
  hud_sprite(hd.frame.sprite, ORB_X + 0.12f, ORB_Y + 0.92f, 1, white, 0);
  /* the masks */
  int max = g_pd.max_health > MAX_MASKS ? MAX_MASKS : g_pd.max_health;
  for (int i = 0; i < max; i++) hud_sprite(hd.mask[i].sprite, HEALTH_X - 10.32f + 0.94f * i, HEALTH_Y + 7.7f, 1, white, 0);
  /* geo: the coin, the count (TrajanPro, its left at the text's place) */
  hud_sprite(hd.coin.sprite, HEALTH_X - 10.33f, HEALTH_Y + 6.61f, 1, white, 0);
  char buf[12];
  int n = 0;
  int32_t v = g_pd.geo < 0 ? 0 : g_pd.geo;
  do buf[n++] = (char)('0' + v % 10), v /= 10;
  while (v && n < 11);
  static const float adv[10] = DIGIT_ADV;
  float x = HEALTH_X - 9.76f, y = HEALTH_Y + 6.45f + 0.23f;   /* (anchored middle left: the baseline a little below) */
  while (n--) {
    int d = buf[n] - '0';
    hud_sprite(SPRITE_DIGIT0 + d, x, y, 1, white, 0);
    x += adv[d];
  }
}
