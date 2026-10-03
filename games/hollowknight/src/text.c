/* Text: the game's sheets' entries (TEXT, in the pack's own code page) in its fonts (FONT: glyphs of 4-bit alpha);
 * the dialogue box (DialogueManager: Box Open, its Text's DialogueBox and Dialogue Page Control) and the prompt
 * markers over what the Knight can act on (PromptMarker). Text is a third bigger than the game's (tools/text.py),
 * the boxes with it. */
#include <math.h>
#include "game.h"

#define DT 0.02f
#define HUD_PX (FOCAL / (-1.342f - CAM_Z))   /* (the HUD's pixels a unit) */

const uint8_t *text_get(int id) {
  const uint8_t *s = section(SEC_TEXT);
  if ((uint32_t)id >= rd32(s)) return (const uint8_t *)"";
  return s + 4 + 4 * rd32(s) + rd32(s + 4 + 4 * id);
}

const uint8_t *font_style(int style) {
  const uint8_t *f = section(SEC_FONT);
  return f + rd32(f + 4 + 4 * style);
}

static float adv_of(const uint8_t *st, uint8_t c) {
  const Glyph *g = font_glyph(st, c, 0);
  return g ? g->adv * (1.0f / 64) : 0;
}

float text_width(int style, const uint8_t *s, int n) {
  const uint8_t *st = font_style(style);
  float w = 0;
  for (int i = 0; i < n && s[i]; i++) w += adv_of(st, s[i]);
  return w;
}

/* a line from s[i] in width (word wrapped as TextMeshPro does): its end; *next where the next line starts */
static int line_end(const uint8_t *st, const uint8_t *s, int i, float width, int *next) {
  float w = 0;
  int space = -1;
  for (int j = i;; j++) {
    uint8_t c = s[j];
    if (!c || c == TEXT_PAGE) return *next = j, j;
    if (c == TEXT_BR) return *next = j + 1, j;
    float a = adv_of(st, c);
    if (c != ' ' && w + a > width && j > i) {
      if (space > i) return *next = space + 1, space;
      return *next = j, j;
    }
    if (c == ' ') space = j;
    w += a;
  }
}

/* a text in a box (HUD units: its top at y, w wide), word wrapped: each line from x (align 0), centered on it (1) or
 * ending at it (2) -> how far down it goes (HUD units) */
float text_box(int text, int style, float x, float y, float w, int align, float a) {
  if (text < 0) return 0;
  const uint8_t *s = text_get(text), *st = font_style(style);
  float px = HUD_PX, wp = w * px;
  float py = VIEW_H / 2 - y * px + font_asc(st);
  uint32_t col = (uint32_t)(a * 255 + 0.5f) << 24 | 0xFFFFFF;
  int lines = 0;
  for (int i = 0; s[i];) {
    int next, e = line_end(st, s, i, wp, &next);
    if (e > i && s[i] != TEXT_PAGE) {
      float lw = text_width(style, s + i, e - i);
      float lx = VIEW_W / 2 + x * px - (align == 1 ? lw / 2 : align == 2 ? lw : 0);
      if (a > 0) gfx_text(style, lx, py, s + i, e - i, col, 0);
    }
    lines++;
    py += font_line(st);
    if (next <= i) break;
    i = next;
  }
  return lines * font_line(st) / px;
}

/* ---------------------------------------------------------------- the dialogue box */
#define BOX_X 0.0f
#define BOX_Y 4.51f          /* (DialogueBox's place: the box scales about it) */
#define TEXT_X (-0.1031f)    /* (Text: its rect's center, its size) */
#define TEXT_Y 4.49f
#define TEXT_W 17.9475f
#define TEXT_H 4.1109f
#define REVEAL_SPEED 65.0f   /* (DialogueBox.revealSpeed; sped up: 200) */
#define MAX_PAGES 24
#define KEYS_CONTINUE (K_JUMP | K_ATTACK | K_SPELL | K_FOCUS | K_DASH | K_OK | K_BACK | K_PAUSE | K_MAP | K_INV)

enum { PC_IDLE, PC_PAGE_END, PC_STOP_PAUSE, PC_CONV_END, PC_SFX, PC_END };
static struct {
  /* Box Open: the box's scale (iTweenScaleTo) and its FadeGroup (0.5 s up, 0.3 s down, to 0.6) */
  bool up;
  float scale, scale_from, scale_to, scale_t, scale_time;
  float alpha, fade_t;
  Anim fleur_top, fleur_bot;
  bool fleurs_on;
  /* DialogueBox */
  const uint8_t *text;
  uint16_t page_start[MAX_PAGES + 1];
  uint8_t npages, page;
  int visible;          /* maxVisibleCharacters */
  float reveal, speed;
  bool typing, fast;
  /* Dialogue Page Control, the arrow and the stop marker (Arrow Anim) */
  uint8_t pc;
  Anim arrow, stop;
  bool arrow_on, stop_on;
  bool finished;        /* CONVO_FINISH, since dialogue_start */
  uint32_t prev_keys;
  /* Box Open Dream: the dream box (up, or going down a while), the text centred (SetTextMeshProAlignment) */
  Anim dream;
  bool dream_on, centre;
  float dream_off;
} dl;

static float ease_sine(float t, bool out) { return out ? sinf(t * 1.5707964f) : 1 - cosf(t * 1.5707964f); }

static void box_scale_to(float to, float time) {
  dl.scale_from = dl.scale, dl.scale_to = to, dl.scale_t = 0, dl.scale_time = time;
}

void dialogue_box_up(void) {
  /* (Box Up) */
  dl.up = true;
  hud_slide(true);
  box_scale_to(1, 0.5f);
  dl.fade_t = 0;
  anim_play_from_frame(&dl.fleur_top, CLIP_DIALOGUE_FLEUR_TOP_UP, 0);
  anim_play_from_frame(&dl.fleur_bot, CLIP_DIALOGUE_FLEUR_BOT_UP, 0);
  dl.fleurs_on = true;
}

void dialogue_box_down(void) {
  if (!dl.up) return;
  dl.up = false;
  hud_slide(false);
  box_scale_to(0.75f, 0.3f);
  dl.fade_t = 0;
  anim_play_from_frame(&dl.fleur_top, CLIP_DIALOGUE_FLEUR_TOP_DOWN, 0);
  anim_play_from_frame(&dl.fleur_bot, CLIP_DIALOGUE_FLEUR_BOT_DOWN, 0);
}

void dialogue_dream_box(bool up) {
  if (up) {
    /* Box Up: Dream Up, the HUD out */
    anim_play_from_frame(&dl.dream, CLIP_DIALOGUE_DREAM_UP, 0);
    hud_slide(true);
    dl.dream_on = true, dl.dream_off = 0;
  } else if (dl.dream_on) {
    /* Box Down: Dream Down, the HUD in; a quarter second, then gone (Stop Audio) */
    anim_play_from_frame(&dl.dream, CLIP_DIALOGUE_DREAM_DOWN, 0);
    hud_slide(false);
    dl.dream_off = 0.25f;
  }
}

void dialogue_centre(bool on) { dl.centre = on; }

static int lines_per_page(const uint8_t *st) {
  float h = TEXT_H * HUD_PX * TEXT_K;
  return 1 + (int)((h - font_asc(st) - font_desc(st)) / font_line(st));
}

/* SetConversation: its pages (TextMeshPro's page mode: as many lines as the rect holds, and <page>) */
static void set_conversation(const uint8_t *s) {
  const uint8_t *st = font_style(STYLE_DIALOGUE);
  float width = TEXT_W * HUD_PX * TEXT_K;
  int per = lines_per_page(st);
  dl.text = s, dl.npages = 0;
  int i = 0;
  while (s[i] && dl.npages < MAX_PAGES) {
    dl.page_start[dl.npages++] = (uint16_t)i;
    for (int l = 0; l < per && s[i] && s[i] != TEXT_PAGE; l++) line_end(st, s, i, width, &i);
    if (s[i] == TEXT_PAGE) i++;
  }
  if (!dl.npages) dl.page_start[dl.npages++] = 0;
  dl.page_start[dl.npages] = (uint16_t)i;
}

static int page_last(void) {   /* (the page's end: its last character's index + 1) */
  int e = dl.page_start[dl.page + 1];
  while (e > dl.page_start[dl.page] && (dl.text[e - 1] == TEXT_PAGE)) e--;
  return e;
}

static void show_page(int p) {
  if (p >= dl.npages) {
    dl.pc = PC_STOP_PAUSE;   /* (CONVERSATION_END) */
    return;
  }
  dl.page = (uint8_t)p;
  dl.visible = dl.page_start[p];
  dl.typing = true, dl.fast = false, dl.speed = REVEAL_SPEED, dl.reveal = 1;   /* (the first at once) */
}

void dialogue_start(int text) {
  /* StartConversation */
  set_conversation(text_get(text));
  dl.finished = false;
  dl.pc = PC_IDLE;
  dl.prev_keys = g_hero.keys;
  show_page(0);
}

bool dialogue_finished(void) { return dl.finished; }
bool dialogue_box_shown(void) { return dl.up || dl.alpha > 0; }

static void end_conversation(void) {
  /* End Conversation: the markers down, the text hidden, CONVO_FINISH */
  if (dl.stop_on) anim_play_from_frame(&dl.stop, CLIP_DIALOGUE_STOP_DOWN, 0);
  if (dl.arrow_on) anim_play_from_frame(&dl.arrow, CLIP_DIALOGUE_ARROW_DOWN, 0);
  dl.text = NULL;
  dl.finished = true;
  dl.pc = PC_IDLE;
  vm_broadcast(VMEV_CONVO_FINISH);
}

void dialogue_cancel(void) {
  /* (CONVO CANCEL, LEAVING SCENE) */
  if (dl.text) end_conversation();
}

void dialogue_tick(void) {
  uint32_t keys = g_hero.keys, pressed = keys & ~dl.prev_keys;
  dl.prev_keys = keys;
  bool cont = (pressed & KEYS_CONTINUE) != 0;   /* (ListenForPromptContinue) */
  /* the box */
  if (dl.scale_t < dl.scale_time) {
    dl.scale_t += DT;
    float t = dl.scale_t >= dl.scale_time ? 1 : dl.scale_t / dl.scale_time;
    dl.scale = dl.scale_from + (dl.scale_to - dl.scale_from) * ease_sine(t, dl.scale_to > dl.scale_from);
  }
  dl.fade_t += DT;
  if (dl.up) dl.alpha = fminf(1, dl.fade_t / 0.5f);
  else if (dl.alpha > 0) dl.alpha = fmaxf(0, 1 - dl.fade_t / 0.3f);
  if (dl.fleurs_on) {
    dl.fleur_top.events = dl.fleur_bot.events = 0;
    anim_update(&dl.fleur_top, DT);
    anim_update(&dl.fleur_bot, DT);
    if (!dl.up && !dl.fleur_top.playing && !dl.fleur_bot.playing) dl.fleurs_on = false;
  }
  if (dl.dream_on) {
    dl.dream.events = 0;
    anim_update(&dl.dream, DT);
    if (dl.dream_off > 0 && (dl.dream_off -= DT) <= 0) dl.dream_on = false;
  }
  if (dl.arrow_on) anim_update(&dl.arrow, DT);
  if (dl.stop_on) anim_update(&dl.stop, DT);
  if (!dl.text) return;
  /* DialogueBox's typewriter */
  if (dl.typing) {
    int last = page_last();
    dl.reveal += DT * dl.speed;
    while (dl.reveal >= 1 && dl.visible < last) dl.visible++, dl.reveal -= 1;
    if (dl.visible >= last) {
      dl.typing = false;
      if (dl.page + 1 >= dl.npages) dl.pc = PC_STOP_PAUSE;   /* CONVERSATION_END */
      else {
        /* PAGE_END: the arrow up */
        dl.pc = PC_PAGE_END;
        dl.arrow_on = true;
        anim_play_from_frame(&dl.arrow, CLIP_DIALOGUE_ARROW_UP, 0);
        return;
      }
    }
  }
  switch (dl.pc) {
    case PC_IDLE:
      if (cont && dl.typing && !dl.fast) dl.fast = true, dl.speed = 200;   /* (SpeedupTypewriter) */
      break;
    case PC_PAGE_END:
      if (cont) {
        /* Show Next Page */
        anim_play_from_frame(&dl.arrow, CLIP_DIALOGUE_ARROW_DOWN, 0);
        dl.pc = PC_IDLE;
        show_page(dl.page + 1);
      }
      break;
    case PC_STOP_PAUSE:
      /* (Stop Pause: 0 s) then the stop marker up */
      dl.stop_on = true, dl.arrow_on = false;
      anim_play_from_frame(&dl.stop, CLIP_DIALOGUE_STOP_UP, 0);
      dl.pc = PC_CONV_END;
      break;
    case PC_CONV_END:
      if (cont) dl.pc = PC_SFX;   /* (the text cleared, the next frame) */
      break;
    case PC_SFX:
      dl.visible = dl.page_start[dl.page];
      dl.pc = PC_END;
      break;
    case PC_END:
      end_conversation();
      break;
  }
}

static void box_sprite(int sprite, float x, float y, float sx, float sy, uint8_t tint) {
  /* (placed about the box's place at its scale and the text's; sized at its scale) */
  float k = dl.scale * TEXT_K;
  Inst in;
  sprite_inst(sprite, BOX_X + (x - BOX_X) * k, BOX_Y + (y - BOX_Y) * k, 0, sx * dl.scale, sy * dl.scale, tint, &in);
  gfx_hud(&in, 0);
}
#define MARKER_K (1.3f * TEXT_K)   /* (the arrow's and the stop's scale) */

void dialogue_draw(void) {
  if (dl.alpha > 0 || dl.fleurs_on) {
    /* the backboard (0.6 of its color's alpha), the fleurs */
    uint8_t a = (uint8_t)(dl.alpha * 0.6f * 0.616f * 255);
    if (a) box_sprite(SPRITE_DIALOGUE_BACKBOARD, 0, 4.51f - 0.09f, 1, 1.2527f / 2.325f, gfx_dyn_tint(18, 255, 255, 255, a));
    uint8_t white = gfx_dyn_tint(19, 255, 255, 255, 255);
    if (dl.fleurs_on) {
      box_sprite(dl.fleur_top.sprite, 0, 4.51f + 2.85f, TEXT_K, TEXT_K, white);
      box_sprite(dl.fleur_bot.sprite, 0, 4.51f - 2.73f, TEXT_K, TEXT_K, white);
    }
  }
  uint8_t white = gfx_dyn_tint(20, 255, 255, 255, 255);
  if (dl.dream_on && dl.dream.sprite >= 0) {
    /* (Box Dream: its place and scale in the box's, with the text's) */
    Inst in;
    sprite_inst(dl.dream.sprite, BOX_X + (0.1f - BOX_X) * TEXT_K, BOX_Y + (4.46f - BOX_Y) * TEXT_K, 0, 1.0745f * TEXT_K,
                1.0745f * TEXT_K, white, &in);
    gfx_hud(&in, 0);
  }
  if (dl.arrow_on && dl.arrow.sprite >= 0) box_sprite(dl.arrow.sprite, 0.0069f, 1.695f, MARKER_K, MARKER_K, white);
  if (dl.stop_on && dl.stop.sprite >= 0) box_sprite(dl.stop.sprite, -0.0231f, 1.695f, MARKER_K, MARKER_K, white);
  if (!dl.text) return;
  /* the page's lines, top left in the rect, as far as the typewriter shows */
  const uint8_t *st = font_style(STYLE_DIALOGUE);
  float w = TEXT_W * HUD_PX * TEXT_K, h = TEXT_H * HUD_PX * TEXT_K;
  float left = VIEW_W / 2 + TEXT_X * HUD_PX - w / 2;
  float y = VIEW_H / 2 - (BOX_Y + (TEXT_Y - BOX_Y) * TEXT_K) * HUD_PX - h / 2 + font_asc(st);
  int i = dl.page_start[dl.page], last = page_last(), per = lines_per_page(st);
  for (int l = 0; l < per && i < last; l++) {
    int next, e = line_end(st, dl.text, i, w, &next);
    int n = (e < dl.visible ? e : dl.visible) - i;
    /* (centred: the whole line's place, as it is typed) */
    float x = dl.centre ? left + (w - text_width(STYLE_DIALOGUE, dl.text + i, e - i)) / 2 : left;
    if (n > 0) gfx_text(STYLE_DIALOGUE, x, y, dl.text + i, n, 0xFFFFFFFFu, 0);
    if (dl.visible <= e) break;
    i = next, y += font_line(st);
  }
}

void dialogue_reset(void) {
  memset(&dl, 0, sizeof dl);
  dl.scale = 0.75f;
  dl.arrow.sprite = dl.stop.sprite = dl.dream.sprite = -1;
}

/* ---------------------------------------------------------------- prompt markers */
#define MAX_PROMPTS 4
#define PROMPT_FADE_IN 0.4f
#define PROMPT_FADE_OUT 0.233f
typedef struct {
  bool used, visible;
  int16_t label;   /* its text */
  float x, y, t;
  Anim anim;
} Prompt;
static Prompt prompts[MAX_PROMPTS];

int prompt_show(int handle, int label, float x, float y) {
  /* ShowPromptMarker: the stored one again, or a new one */
  Prompt *p = handle >= 0 && handle < MAX_PROMPTS && prompts[handle].used ? &prompts[handle] : NULL;
  if (!p) {
    for (int i = 0; i < MAX_PROMPTS && !p; i++)
      if (!prompts[i].used) p = &prompts[i], handle = i;
    if (!p) return -1;
    memset(p, 0, sizeof *p);
    p->used = true;
  }
  p->label = (int16_t)label, p->x = x, p->y = y;
  anim_play_from_frame(&p->anim, CLIP_PROMPT_UP, 0);
  p->visible = true, p->t = 0;
  return handle;
}

void prompt_hide(int handle) {
  if (handle < 0 || handle >= MAX_PROMPTS || !prompts[handle].used || !prompts[handle].visible) return;
  Prompt *p = &prompts[handle];
  anim_play_from_frame(&p->anim, CLIP_PROMPT_DOWN, 0);
  p->visible = false, p->t = 0;   /* (recycled after the fade) */
}

void prompts_reset(void) { memset(prompts, 0, sizeof prompts); }

void prompts_tick(void) {
  for (int i = 0; i < MAX_PROMPTS; i++) {
    Prompt *p = &prompts[i];
    if (!p->used) continue;
    p->t += DT;
    anim_update(&p->anim, DT);
    if (!p->visible && p->t >= PROMPT_FADE_OUT) p->used = false;
  }
}

void prompts_draw(void) {
  for (int i = 0; i < MAX_PROMPTS; i++) {
    Prompt *p = &prompts[i];
    if (!p->used) continue;
    float a = p->visible ? fminf(1, p->t / PROMPT_FADE_IN) : fmaxf(0, 1 - p->t / PROMPT_FADE_OUT);
    Inst in;
    if (p->anim.sprite >= 0) {
      sprite_inst(p->anim.sprite, p->x, p->y, 0, 1, 1, 0, &in);   /* (the room's tint 0: white) */
      gfx_actor(&in, PROMPT_SORT);
    }
    /* the shadow (black, faded with the label) */
    sprite_inst(SPRITE_PROMPT_SHADOW, p->x - 0.14f, p->y + 0.42f, 0.01f, 1, 0.58f / 0.8f, gfx_dyn_tint(7, 0, 0, 0, (uint8_t)(a * 255)), &in);
    gfx_actor(&in, PROMPT_SORT);
    /* the label, centered on (0, 0.249) */
    const uint8_t *s = text_get(p->label);
    int n = (int)strlen((const char *)s);
    const uint8_t *st = font_style(STYLE_PROMPT);
    float k = FOCAL / (0 - CAM_Z);
    float cx = VIEW_W / 2 + (p->x - g_cam_x) * k, cy = VIEW_H / 2 - (p->y + 0.249f - g_cam_y) * k;
    gfx_text(STYLE_PROMPT, cx - text_width(STYLE_PROMPT, s, n) / 2, cy + font_asc(st) / 2, s, n,
             (uint32_t)(a * 255) << 24 | 0xFFFFFF, 1);
  }
}
