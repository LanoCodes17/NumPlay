/* The HUD (Hud Canvas, drawn by its own camera): the masks, the soul orb, the geo count. Places in HUD units from the
 * screen's center. */
#include <math.h>
#include "game.h"

#define DT 0.02f
#define MAX_MASKS 11
#define MAX_BLUE 8

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
  /* lifeblood (Blue Health: blue_health_display): each mask's state, its place's number (after the masks) */
  Anim blue[MAX_BLUE];
  uint8_t blue_st[MAX_BLUE];
  int8_t shown_blue;
  Anim frame, liquid, coin, burst;
  bool burst_on;
  float liquid_grey;     /* (Liquid Control: grey while it cannot heal) */
  bool started;
  /* Slide Out: the canvas shrinks about its corner (iTweenScaleTo, 0.15 s), then goes off the screen */
  bool out, off;
  float scale, scale_from, scale_t;
} hd;

#define SCALE_TIME 0.15f
void hud_slide(bool out) {
  if (!hd.started || out == hd.out) return;
  hd.out = out;
  if (!out) hd.off = false;   /* (Come In: back at its place) */
  hd.scale_from = hd.scale, hd.scale_t = 0;
}

/* the soul orb's HUD_frame (Load Animation): cracked while the soul is limited; Limiter Burst (Animate) */
void hud_soul_limiter(bool up) {
  anim_play_from_frame(&hd.frame, up ? CLIP_HUD_HUD_FRAME_CRACKAPPEAR : CLIP_HUD_HUD_FRAME_IDLE, 0);
  anim_play_from_frame(&hd.burst, CLIP_HUD_SOUL_BURST, 0);
  hd.burst_on = true;
}

enum { BL_NONE, BL_APPEAR, BL_IDLE, BL_BREAK };

void hud_reset(void) {
  memset(&hd, 0, sizeof hd);
  hd.shown_health = -1, hd.shown_blue = -1;
  hd.scale = 1, hd.scale_t = SCALE_TIME;
  anim_play(&hd.frame, g_pd.soul_limited ? CLIP_HUD_HUD_FRAME_CRACKED : CLIP_HUD_HUD_FRAME_IDLE);
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
  /* lifeblood: one more appears after the rest; one lost breaks (all at once, fast, as the Knight is healed full) */
  int blue = g_pd.health_blue > MAX_BLUE ? MAX_BLUE : g_pd.health_blue;
  if (hd.shown_blue < 0) {
    for (int i = 0; i < blue; i++) anim_play(&hd.blue[i], CLIP_HUD_BLUE_IDLE), hd.blue_st[i] = BL_IDLE;
    hd.shown_blue = (int8_t)blue;
  }
  while (hd.shown_blue < blue) {
    int i = hd.shown_blue++;
    anim_play_from_frame(&hd.blue[i], CLIP_HUD_BLUE_APPEAR, 0), hd.blue_st[i] = BL_APPEAR;
  }
  bool full = blue == 0 && g_pd.health == g_pd.max_health;
  while (hd.shown_blue > blue) {
    int i = --hd.shown_blue;
    anim_play_from_frame(&hd.blue[i], full ? CLIP_HUD_BLUE_BREAK_FAST : CLIP_HUD_BLUE_BREAK, 0), hd.blue_st[i] = BL_BREAK;
  }
  for (int i = 0; i < MAX_BLUE; i++) {
    Anim *a = &hd.blue[i];
    if (hd.blue_st[i] == BL_NONE) continue;
    a->events = 0;
    anim_update(a, DT);
    if (!(a->events & ANIM_DONE)) continue;
    if (hd.blue_st[i] == BL_BREAK) hd.blue_st[i] = BL_NONE;
    else if (hd.blue_st[i] == BL_APPEAR) {
      /* (Idle, from a frame at random) */
      anim_play_from_frame(a, CLIP_HUD_BLUE_IDLE, (int)rand_range(0, 26.999f));
      hd.blue_st[i] = BL_IDLE;
    }
  }
  hd.frame.events = 0;
  anim_update(&hd.frame, DT);
  if ((hd.frame.events & ANIM_DONE) && hd.frame.clip == CLIP_HUD_HUD_FRAME_CRACKAPPEAR) anim_play(&hd.frame, CLIP_HUD_HUD_FRAME_CRACKED);
  if (hd.burst_on) {
    hd.burst.events = 0;
    anim_update(&hd.burst, DT);
    if (hd.burst.events & ANIM_DONE) hd.burst_on = false;
  }
  anim_update(&hd.liquid, DT);
  anim_update(&hd.coin, DT);
  if (hd.scale_t < SCALE_TIME) {
    hd.scale_t += DT;
    float t = hd.scale_t >= SCALE_TIME ? 1 : hd.scale_t / SCALE_TIME, to = hd.out ? 0.75f : 1;
    hd.scale = hd.scale_from + (to - hd.scale_from) * (hd.out ? 1 - cosf(t * 1.5707964f) : sinf(t * 1.5707964f));
    if (hd.out && hd.scale_t >= SCALE_TIME) hd.off = true;   /* (Out, after 0.15 s) */
  }
  /* EaseColor over 0.2 s, to grey or white */
  float to = g_pd.mp < FOCUS_MP ? 1 : 0;
  if (hd.liquid_grey < to) hd.liquid_grey = fminf(to, hd.liquid_grey + DT / 0.2f);
  else if (hd.liquid_grey > to) hd.liquid_grey = fmaxf(to, hd.liquid_grey - DT / 0.2f);
}

static void hud_sprite_s(int sprite, float x, float y, float sx, float sy, uint8_t tint, int clip) {
  /* (on the canvas: about its corner, at its scale) */
  float k = hd.scale;
  Inst in;
  sprite_inst(sprite, CANVAS_X + (x - CANVAS_X) * k, CANVAS_Y + (y - CANVAS_Y) * k, 0, sx * k, sy * k, tint, &in);
  gfx_hud(&in, clip);
}

static void hud_sprite(int sprite, float x, float y, float sx, uint8_t tint, int clip) { hud_sprite_s(sprite, x, y, sx, 1, tint, clip); }

void hud_draw(void) {
  if (!hd.started || hd.off) return;
  float k = hd.scale;
  uint8_t white = gfx_dyn_tint(16, 255, 255, 255, 255);
  /* the soul orb: its liquid (in the orb's circle), the frame over it */
  if (g_pd.mp > 1 && !hd.out) {   /* (the liquid's renderer off while the HUD is out) */
    float y = FILL_Y + LIQUID_BOTTOM_Y + g_pd.mp * LIQUID_Y_PER_MP;
    uint8_t g = (uint8_t)(255 - (255 - 110) * hd.liquid_grey);
    gfx_hud_clip(1, CANVAS_X + (FILL_X - 2.28f - CANVAS_X) * k, CANVAS_Y + (FILL_Y + 1.32f - CANVAS_Y) * k, 0.97f * k);
    hud_sprite(hd.liquid.sprite, FILL_X - 2.23f, y, 1, gfx_dyn_tint(17, g, g, g, 255), 1);
  }
  hud_sprite(hd.frame.sprite, ORB_X + 0.12f, ORB_Y + 0.92f, 1, white, 0);
  if (hd.burst_on) hud_sprite(hd.burst.sprite, ORB_X - 0.715f, ORB_Y + 0.4318f, 1, white, 0);
  /* the masks */
  int max = g_pd.max_health > MAX_MASKS ? MAX_MASKS : g_pd.max_health;
  for (int i = 0; i < max; i++) hud_sprite(hd.mask[i].sprite, HEALTH_X - 10.32f + 0.94f * i, HEALTH_Y + 7.7f, 1, white, 0);
  for (int i = 0; i < MAX_BLUE; i++)
    if (hd.blue_st[i] != BL_NONE)
      hud_sprite_s(hd.blue[i].sprite, HEALTH_X - 10.32f + 0.94f * (max + i), HEALTH_Y + 7.68f, 0.75f / 0.7135f, 0.75f / 0.7135f,
                   white, 0);   /* (its frames at the masks' 0.7135) */
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
