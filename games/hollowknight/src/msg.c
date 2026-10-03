/* The message as the Knight takes an item (UI Msg Get Item: its Msg Control FSM): a dark backdrop, the item's icon and
 * name fade up, then how to use it (Tap or Press, the key, two lines), then the stop marker; a key sends it away.
 * Places in HUD units from the screen's center (its prefab, made at the origin, drawn by the HUD's camera). */
#include <math.h>
#include "game.h"

#define DT 0.02f
#define HUD_PX (FOCAL / (-1.342f - CAM_Z))

static const int16_t table[][6] = MSG_TABLE;   /* (name, prefix, Tap or Press, the two lines, the key's name) */
static const int16_t icons[] = {SPRITE_MSG_ICON_FIREBALL, SPRITE_MSG_ICON_DASH};

enum { MS_OFF, MS_TOP_UP, MS_BOT_UP, MS_STOP_UP, MS_DETECT, MS_DOWN };
/* (color_fader: what fades together; each its delay going up) */
enum { F_BG, F_ICON, F_NAME, F_PREFIX, F_PRESS, F_MSG1, F_MSG2, F_BUTTON, F_STOP, F_FLEUR, NF };
static const float up_delay[NF] = {0, 0.5f, 0.5f, 0.5f, 0, 0, 0.5f, 0, 0, 0};

static struct {
  uint8_t st, item;
  float t;
  float a[NF];      /* each part's alpha */
  float t_up[NF];   /* (its UP's time: -1 none) */
  bool down;
  uint32_t prev_keys;
} m;

void msg_show(int item) {
  memset(&m, 0, sizeof m);
  m.item = (uint8_t)item;
  for (int i = 0; i < NF; i++) m.t_up[i] = -1;
  /* Top Up: the backdrop, the icon, the name and its prefix (and the game saved) */
  m.t_up[F_BG] = m.t_up[F_ICON] = m.t_up[F_NAME] = m.t_up[F_PREFIX] = 0;
  save_game();
  m.st = MS_TOP_UP, m.t = 0;
  m.prev_keys = g_hero.keys;
}

bool msg_shown(void) { return m.st != MS_OFF; }

/* the HUD Blanker White (Blanker Control): FADE IN to white, FADE OUT from it, linearly over its Fade Time */
static struct {
  float alpha, from, to, t, time;
  bool on;
} wb = {0, 0, 0, 0, 1, false};

void white_blanker_time(float t) { wb.time = t; }

void white_blanker_fade(bool in) {
  wb.from = in ? 0 : 1, wb.to = in ? 1 : 0, wb.t = 0, wb.on = true;
  wb.alpha = wb.from;
}

void white_blanker_reset(void) { wb.on = false, wb.alpha = 0, wb.time = 1; }

static void white_blanker_tick(void) {
  if (!wb.on) return;
  wb.t += DT;
  float k = wb.time > 0 ? wb.t / wb.time : 1;
  wb.alpha = wb.from + (wb.to - wb.from) * (k > 1 ? 1 : k);
  if (k >= 1 && wb.to == 0) wb.on = false;
}

void msg_tick(void) {
  white_blanker_tick();
  if (m.st == MS_OFF) return;
  uint32_t pressed = g_hero.keys & ~m.prev_keys;
  m.prev_keys = g_hero.keys;
  m.t += DT;
  for (int i = 0; i < NF; i++) {
    if (m.down) {
      /* DOWN: to nothing in 1 s (the stop marker 0.15 s) */
      m.a[i] -= DT / (i == F_STOP ? 0.15f : 1);
      if (m.a[i] < 0) m.a[i] = 0;
    } else if (m.t_up[i] >= 0) {
      m.t_up[i] += DT;
      float k = (m.t_up[i] - up_delay[i]) / 1;
      if (k > 0) m.a[i] = k > 1 ? 1 : k;
    }
  }
  switch (m.st) {
    case MS_TOP_UP:
      if (m.t >= 2.5f) {
        /* the fleur (all at once), Bot Up: how to use it */
        m.a[F_FLEUR] = 1;
        m.t_up[F_PRESS] = m.t_up[F_MSG1] = m.t_up[F_MSG2] = m.t_up[F_BUTTON] = 0;
        m.st = MS_BOT_UP, m.t = 0;
      }
      break;
    case MS_BOT_UP:
      if (m.t >= 3) m.a[F_STOP] = 1, m.st = MS_STOP_UP, m.t = 0;
      break;
    case MS_STOP_UP:
      if (m.t >= 0.25f) m.st = MS_DETECT;
      break;
    case MS_DETECT:
      if (pressed & (K_SPELL | K_JUMP | K_ATTACK)) m.down = true, m.st = MS_DOWN, m.t = 0;
      break;
    case MS_DOWN:
      if (m.t >= 1) {
        m.st = MS_OFF;
        vm_broadcast(VMEV_GET_ITEM_MSG_END);
      }
      break;
  }
}

/* a text: its lines centered on x (align 0) or ending at x (1); the first's top or middle at y -> its width (pixels) */
static float line(int text, int style, float x, float y, int align, bool top, float a) {
  uint32_t al = (uint32_t)(a * 255 + 0.5f);
  if (text < 0) return 0;
  const uint8_t *s = text_get(text), *st = font_style(style);
  float py = VIEW_H / 2 - y * HUD_PX + (top ? font_asc(st) : (font_asc(st) - font_desc(st)) / 2), wmax = 0;
  for (int i = 0;;) {
    int e = i;
    while (s[e] && s[e] != TEXT_BR) e++;
    float w = text_width(style, s + i, e - i);
    if (w > wmax) wmax = w;
    if (al) gfx_text(style, VIEW_W / 2 + x * HUD_PX - (align == 0 ? w / 2 : align == 1 ? w : 0), py, s + i, e - i, al << 24 | 0xFFFFFF, 0);
    if (!s[e]) break;
    i = e + 1, py += font_line(st);
  }
  return wmax;
}

static void sprite(int sprite, float x, float y, float a, int slot) {
  uint8_t al = (uint8_t)(a * 255 + 0.5f);
  if (!al) return;
  Inst in;
  sprite_inst(sprite, x, y, 0, 1, 1, gfx_dyn_tint(slot, 255, 255, 255, al), &in);
  gfx_hud(&in, 0);
}

/* the HUD Blanker: black over the screen (under the message), its color's alpha as the scripts set it */
static float blank_alpha;
static bool blank_on;
void blanker_set(float alpha, bool on) { blank_alpha = alpha, blank_on = on; }

void msg_draw(void) {
  if (wb.on && wb.alpha > 0)
    gfx_hud_fill(-15, -9, 15, 9, gfx_dyn_tint(8, 255, 255, 255, (uint8_t)(wb.alpha * 255 + 0.5f)));
  if (blank_on && blank_alpha > 0)
    gfx_hud_fill(-15, -9, 15, 9, gfx_dyn_tint(27, 0, 0, 0, (uint8_t)(blank_alpha * 255 + 0.5f)));
  if (m.st == MS_OFF) return;
  const int16_t *t = table[m.item];
  /* (BG: black, at 0.766 of its color's alpha) */
  sprite(SPRITE_MSG_BG, 0, 0, m.a[F_BG] * 0.766f, 22);
  sprite(icons[m.item], 0, 4.73f, m.a[F_ICON], 23);
  sprite(SPRITE_MSG_FLEUR, 0, -1.48f, m.a[F_FLEUR], 24);
  sprite(SPRITE_MSG_STOP, 0, -6.84f, m.a[F_STOP], 26);
  line(t[0], STYLE_MSG_NAME, 0, -0.23f, 0, false, m.a[F_NAME]);
  line(t[1], STYLE_MSG, 0, 0.84f, 0, false, m.a[F_PREFIX]);
  line(t[2], STYLE_MSG, -8.5f + 11.1377f * 1.4407f / 2, -2.93f, 1, false, m.a[F_PRESS]);
  line(t[3], STYLE_MSG, 0, -4.64f + 1.0253f * 1.4407f / 2, 0, true, m.a[F_MSG1]);
  line(t[4], STYLE_MSG, 0, -6.15f + 1.7591f * 1.4407f / 2, 0, true, m.a[F_MSG2]);
  /* the key (the calculator's, by name: its key sprite as wide as the name) */
  float w = line(t[5], STYLE_PROMPT, 0, -2.93f, 0, false, 0) / HUD_PX + 0.5f, kx = 0.53f - 0.475f + w / 2;
  uint8_t al = (uint8_t)(m.a[F_BUTTON] * 255 + 0.5f);
  if (al) {
    Inst in;
    sprite_inst(SPRITE_MSG_KEY, kx, -2.93f, 0, w / 0.95f, 1, gfx_dyn_tint(25, 255, 255, 255, al), &in);
    gfx_hud(&in, 0);
    line(t[5], STYLE_PROMPT, kx, -2.93f, 0, false, m.a[F_BUTTON]);
  }
}
