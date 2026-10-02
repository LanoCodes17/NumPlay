#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("Os")   /* not drawn every frame: smaller over faster */
#endif
/* BreathingMinigame: holding up breathes the feather higher, letting go lets it fall; kept in the box long
 * enough, it is calm. Golden Ridge's gondola (winnable, with its words) and Reflection's dream (it cannot be
 * won: the feather gets cut). The glow behind the feather (MagicGlow, blurred) is not drawn. */
#include "cutscene.h"
#include "level.h"
#include "player.h"
#include "text.h"
#include "data.h"

#define DOTS 50
typedef struct { int16_t x, y; uint8_t scale, speed, sin, pad; } Dot;   /* interface units x8; scale /255 * 0.8 */
typedef struct {
  Co co, box_co;
  SineWave sine, wave;
  float feather, speed, stab, bounds, center, mult, inside, trail, losing_t, bg, box, falpha, palpha, pspeed, timer;
  float active, text_a, p, sy, pop_p, half_p;   /* sy: the feather's top (interface units) */
  uint8_t winnable, completed, pausing, box_on, losing, box_started, popped, slicing, text;
  char words[96];
  TextDraw td;
  Dot dots[DOTS];
} Breath;
static const EntClass BREATH;

static void dot_reset(Dot *d, bool top, bool bottom) {   /* Particle.Reset */
  float n = rndf();
  n *= n * n * n;
  d->x = (int16_t)(rndf() * 1920 * 8), d->y = (int16_t)((top ? -128 : bottom ? 1208 : rndf() * 1080) * 8);
  d->scale = (uint8_t)((0.05f + n * 0.75f) / 0.8f * 255), d->speed = (uint8_t)((2 + rndf() * 6) * 32), d->sin = (uint8_t)rndi(256);
}
static bool box_in(Breath *b) {   /* FadeBoxIn */
  Co *c = &b->box_co;
  NB_BEGIN(c);
  NB_WAIT(c, b->winnable ? 5 : 2);
  while (fabsf(b->feather) > 300) NB_YIELD(c);
  b->box_on = 1;
  b->sine.freq = 0.12f;
  sine_set(&b->sine, 0);
  while (b->box < 1) {
    b->box += DT;
    NB_YIELD(c);
  }
  b->box = 1;
  NB_END(c);
}
static bool confirm(void) { return btn_pressed(&g_in.confirm) || btn_pressed(&g_in.jump); }   /* MenuConfirm */

static void breath_update(Ent *e) {
  Breath *b = ST(e, Breath);
  Co *c = &b->co;
  b->timer += DT;
  b->trail = approach(b->trail, b->speed, DT * 200 * 8);
  sine_update(&b->wave);
  if (b->box_on) sine_update(&b->sine);
  for (int i = 0; i < DOTS; i++) {
    Dot *d = &b->dots[i];
    float sp = d->scale / 255.f * 0.8f * d->speed / 32.f;
    d->y = (int16_t)clampf(d->y + sp * b->pspeed * DT * 8, -32768, 32767);
    if (b->pspeed > -400) d->x = (int16_t)(d->x + (b->pspeed + 400) * sinf(d->sin / 256.f * 2 * PI_F + b->timer) * 0.8f * DT);
    if (d->y < -128 * 8 || d->y > 1208 * 8) dot_reset(d, b->pspeed >= 0, b->pspeed < 0);
  }
  if (b->box_started == 1 && !box_in(b)) b->box_started = 2;
  CO_BEGIN(c);
  b->inside = 1;
  for (b->p = 0; b->p < 1; b->p += DT) {
    CO_YIELD(c);
    b->bg = fminf(b->p, 1) * 0.65f;
  }
  for (b->text = 1; b->text <= (b->winnable ? 5 : 0); b->text++) {   /* ShowText(n), FadeGameIn after the first */
    if (b->text_a != 0)
      for (b->p = 0; b->p < 1; b->p += DT * 4) {
        CO_YIELD(c);
        b->text_a = 1 - b->p;
      }
    b->text_a = 0;
    {
      char key[24] = "CH4_GONDOLA_FEATHER_0";
      key[20] = (char)('0' + b->text);
      dialog_clean(key, b->words, sizeof b->words);
    }
    CO_WAIT(c, 0.1f);
    for (b->p = 0; b->p < 1; b->p += DT * 4) {
      CO_YIELD(c);
      b->text_a = b->p;
    }
    b->text_a = 1;
    while (!confirm()) CO_YIELD(c);
    for (b->p = 0; b->p < 1; b->p += DT * 4) {
      CO_YIELD(c);
      b->text_a = 1 - b->p;
    }
    b->text_a = 0;
    if (b->text == 1)
      while (b->falpha < 1) {
        b->falpha += DT;
        CO_YIELD(c);
      }
  }
  b->text = 0;
  while (b->falpha < 1) {   /* FadeGameIn */
    b->falpha += DT;
    CO_YIELD(c);
  }
  b->falpha = 1;
  b->box_started = 1;
  b->active = 450;
  while (b->stab < 30) {
    float num = b->stab / 30;
    bool up = g_in.jump.check || g_in.dash.check || g_in.aim_y < 0;
    if (b->losing) {
      if (fmodf(b->losing_t * 10, 1) > 0.5f) up = !up;   /* Calc.BetweenInterval(losingTimer * 10, 0.5) */
      b->active = 450 - ease_cube_in(b->losing_t) * 200;
    }
    if (up) {
      if (b->feather > -b->active) b->speed -= 280 * DT;
      b->pspeed -= 2800 * DT;
    } else {
      if (b->feather < b->active) b->speed += 280 * DT;
      b->pspeed += 2800 * DT;
    }
    b->speed = clampf(b->speed, -200, 200);
    if (b->feather > b->active && b->mult == 0 && b->speed > 0) b->speed = 0;
    if (b->feather < b->active && b->mult == 0 && b->speed < 0) b->speed = 0;
    b->pspeed = clampf(b->pspeed, -1600, 120);
    b->mult = approach(b->mult, ((b->feather < -b->active && b->speed < 0) || (b->feather > b->active && b->speed > 0)) ? 0 : 1, DT * 4);
    b->bounds = approach(b->bounds, 160 - 60 * num, DT * 16);
    b->feather += b->speed * b->mult * DT;
    if (b->box_on) {
      b->center = -b->sine.value * 300 * lerpf(1, 0, ease_cube_in(num));
      if (b->feather > b->center - b->bounds && b->feather < b->center + b->bounds) {
        b->inside += DT;
        if (b->inside > 0.2f) b->stab += DT;
      } else {
        if (b->inside > 0.2f) b->stab = fmaxf(0, b->stab - 0.5f);
        if (b->stab > 0) b->stab -= 0.5f * DT;
        b->inside = 0;
      }
    }
    b->bg = approach(b->bg, 0.65f + fminf(1, num / 0.8f) * 0.35f, DT);
    b->sy = 540 + b->feather - 128;
    b->palpha = approach(b->palpha, 1, DT);
    if (!b->winnable && b->stab > 12) b->losing = 1;
    if (b->losing && (b->losing_t += DT / 5) > 1) break;
    CO_YIELD(c);
  }
  if (!b->winnable) {
    for (b->pausing = 1; b->pausing;) {   /* the cutscene goes on, then lets it pop */
      b->sy += (540 - b->sy) * (1 - powf(0.01f, DT));
      b->box -= DT * 10;
      b->palpha = b->box;
      CO_YIELD(c);
    }
    b->losing = 0, b->losing_t = 0;
    b->popped = 1;   /* PopFeather: still, centered */
    CO_WAIT(c, 0.25f);
    b->slicing = 1;
    for (b->pop_p = 0; b->pop_p < 1; b->pop_p += DT * 8) CO_YIELD(c);
    b->slicing = 0;
    level_shake(0.3f);
    level_flash(0xFFFF, false);
    b->popped = 2;   /* the halves */
    for (b->half_p = 0; b->half_p < 1; b->half_p += DT) {
      b->falpha = 1 - b->half_p;
      CO_YIELD(c);
    }
  } else {
    b->bg = 1;
    while (b->box > 0) {
      CO_YIELD(c);
      b->box -= DT;
      b->palpha = b->box;
    }
    b->palpha = 0;
    CO_WAIT(c, 2);
    while (b->falpha > 0) {
      CO_YIELD(c);
      b->falpha -= DT;
    }
    CO_WAIT(c, 1);
  }
  for (b->completed = 1; b->bg > 0; b->bg -= DT * (b->winnable ? 1 : 10)) CO_YIELD(c);
  ent_remove(e);
  return;
  CO_END(c);
}

/* the particles: OVR's snow (a soft dot), stretched while they rush up */
static void dots_strip(uint16_t *strip, int y0, int y1, void *ctx) {
  const Breath *b = ctx;
  float k = b->pspeed < 0 ? fmaxf(1, -b->pspeed * 0.004f) : 1, kx = b->pspeed < 0 ? fminf(1, 1 / (-b->pspeed * 0.004f)) : 1;
  int a = (int)(0.5f * b->palpha * 256);
  if (a <= 0) return;
  for (int i = 0; i < DOTS; i++) {
    const Dot *d = &b->dots[i];
    float r = d->scale / 255.f * 0.8f * 127 / 6, cx = d->x / 48.f, cy = d->y / 48.f, rx = fmaxf(r * kx, 0.5f), ry = fmaxf(r * k, 0.5f);
    int ya = (int)floorf(cy - ry), yb = (int)ceilf(cy + ry);
    if (yb < y0 || ya >= y1) continue;
    for (int y = ya < y0 ? y0 : ya; y <= yb && y < y1; y++)
      for (int x = (int)floorf(cx - rx); x <= (int)ceilf(cx + rx); x++) {
        if (x < 0 || x >= VIEW_W) continue;
        float dx = (x + 0.5f - cx) / rx, dy = (y + 0.5f - cy) / ry, q = dx * dx + dy * dy;
        if (q >= 1) continue;
        int al = (int)(a * (1 - q));
        uint16_t *p = strip + (y - y0) * VIEW_W + x;
        *p = blend565(*p, scale565(0xFFFF, al), al);
      }
  }
}
#define BORDER_W 207   /* Gui feather/border's width, box's size, feather's height (interface pixels) */
#define BOX_W 300
#define FEATHER_H 256
static void breath_render(Ent *e) {
  Breath *b = ST(e, Breath);
  const float k = 1 / 6.f;
  gfx_hud(true);
  gfx_rect(-2, -2, 324, 184, 0, a8(b->bg));
  if (!g_level.frozen && !g_level.paused && !g_level.skipping_cutscene && !g_player.dead) {
    Tex t;
    float cy = 540 + b->center;
    if (b->box > 0 && tex_get(T__feather_box, &t))   /* the box, white at a quarter */
      gfx_tex_ex(T__feather_box, 160, cy * k, t.fw * t.scale / 2.f, t.fh * t.scale / 2.f, (BORDER_W * 2 - 32.f) / BOX_W,
                 (b->bounds * 2 - 32) / BOX_W, 0, 0xFFFF, a8(b->box * 0.25f), 0);
    if (b->box > 0 && tex_get(T__feather_border, &t)) {   /* its corners: brighter while the feather is in */
      uint8_t a = a8(b->box * (b->inside > 0.2f ? 1 : 0.6f));
      float w = t.fw * t.scale, h = t.fh * t.scale;
      gfx_tex_ex(T__feather_border, (960 - BORDER_W) * k, (cy - b->bounds) * k, 0, 0, 1, 1, 0, 0xFFFF, a, 0);
      gfx_tex_ex(T__feather_border, (960 + BORDER_W) * k, (cy + b->bounds) * k, w, h, 1, 1, 0, 0xFFFF, a, GF_FLIPX | GF_FLIPY);
    }
    gfx_custom(dots_strip, b, 0, VIEW_H);
    uint8_t fa = a8(b->falpha);
    float fx = 960, fy = b->sy;
    if (fa && !b->popped) {   /* the feather (GuiSpriteBank "feather": hover, or flutter while out of the box) */
      float rot = b->wave.value * 0.25f + 0.1f;
      if (b->losing) {
        V2 d = safe_norm(v2((float)(rndi(3) - 1), (float)(rndi(3) - 1)), 1);
        fx += d.x * b->losing_t * 10, fy += d.y * b->losing_t * 10;
        rot += (rndi(3) - 1) * b->losing_t * 0.1f;
      }
      int f = (b->inside > 0 || !b->box_on) ? 0 : (int)(b->timer * 10) % 4;
      uint16_t ft = (uint16_t)(T__feather_feather0 + f);
      if (tex_get(ft, &t)) gfx_tex_ex(ft, fx * k, fy * k, t.fw * t.scale / 2.f, 0, 1, 1, rot, 0xFFFF, fa, 0);
    }
    fy += FEATHER_H / 2.f;   /* (popped: centered) */
    if (fa && b->popped == 1 && tex_get(T__feather_feather0, &t))
      gfx_tex_ex(T__feather_feather0, fx * k, fy * k, t.fw * t.scale / 2.f, t.fh * t.scale / 2.f, 1, 1, 0, 0xFFFF, fa, 0);
    if (b->slicing && tex_get(T__feather_slice, &t)) {
      V2 at = v2add(v2(fx, fy), v2lerp(v2(128, -128), v2(-128, 128), b->pop_p));
      float y = yoyo(b->pop_p);
      gfx_tex_ex(T__feather_slice, at.x * k, at.y * k, t.fw * t.scale / 2.f, t.fh * t.scale / 2.f, (0.25f + y * 0.75f) * 8,
                 (0.5f + (1 - y) * 0.5f) * 8, atan2f(112 - 165, 140 - 96), 0xFFFF, 255, 0);
    }
    if (fa && b->popped == 2)
      for (int i = 0; i < 2; i++) {
        uint16_t h = i ? T__feather_feather_half1 : T__feather_feather_half0;
        V2 at = v2add(v2(fx, fy), v2mul(v2(i ? 128 : -128, i ? 32 : -32), b->half_p));
        if (tex_get(h, &t)) gfx_tex_ex(h, at.x * k, at.y * k, t.fw * t.scale / 2.f, t.fh * t.scale / 2.f, 1, 1, 0, 0xFFFF, fa, 0);
      }
    if (b->text && b->text_a > 0) {   /* the words, and the button once they are in */
      b->td = (TextDraw){b->words, 960, 920, 0.5f, 0.5f, 1, 0xFFFF, a8(b->text_a)};
      text_draw(&b->td);
      if (b->text_a >= 1 && tex_get(T__textboxbutton, &t))
        gfx_tex_ex(T__textboxbutton, ((1920 + text_measure(b->words)) / 2 + 40) * k, (920 + 32 - 16 + (fmodf(b->timer, 1) < 0.25f ? 6 : 0)) * k,
                   t.fw * t.scale / 2.f, t.fh * t.scale / 2.f, 1, 1, 0, 0xFFFF, 255, 0);
    }
  }
  gfx_hud(false);
}
static const EntClass BREATH = {.name = "breathingMinigame", .size = sizeof(Breath), .update = breath_update, .render = breath_render};

Ent *breath_new(bool winnable) {
  Ent *e = ent_new(&BREATH, 0, 0);
  if (!e) return NULL;
  e->tags = TAG_HUD, e->depth = 100, e->collidable = 0;
  Breath *b = ST(e, Breath);
  b->winnable = winnable;
  b->bounds = 160, b->mult = 1, b->pspeed = 120;
  b->wave.freq = 0.25f;
  sine_set(&b->wave, 0);
  b->sy = 540 - 128;
  for (int i = 0; i < DOTS; i++) dot_reset(&b->dots[i], false, false);
  return e;
}
static Breath *live(Ent *e) { return e && e->cls == &BREATH && e->dead != 1 ? ST(e, Breath) : NULL; }
bool breath_done(Ent *e) { return !live(e) || live(e)->completed; }
bool breath_pausing(Ent *e) { return live(e) && live(e)->pausing; }
void breath_resume(Ent *e) {
  if (live(e)) live(e)->pausing = 0;
}
void breath_remove(Ent *e) {
  if (live(e)) ent_remove(e);
}
