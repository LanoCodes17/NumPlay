#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("Os")   /* not drawn every frame: smaller over faster */
#endif
/* The menus, like the game's own: the title (OuiTitleScreen), the main menu
 * (OuiMainMenu), the chapter select (OuiChapterSelect, OuiChapterPanel),
 * options with the key bindings, the key sheet, the credits, the chapter's end
 * (AreaComplete) and the pause menu (Level.Pause, TextMenu). Words come from
 * the dialog (UISTR) and are drawn straight into the strips: nothing is kept. */
#include "cutscene.h"
#include "text.h"

enum { S_NONE, S_SHEET, S_TITLE, S_MAIN, S_CHAPTERS, S_PANEL, S_OPTIONS, S_KEYS, S_CREDITS, S_COMPLETE, S_PAUSE, S_CONFIRM };
static struct {
  uint8_t screen, back;      /* back: where Back goes from the options (S_PAUSE or S_MAIN) */
  int8_t sel, area, mode, cp, binding, confirm;
  uint8_t hold, last_dir;    /* the arrows' repeat */
  float t, wig, repeat;
} M;
uint8_t g_menu;              /* the screen on (S_NONE while playing) */

/* ---------------------------------------------------------------- drawing: pictures, then words */
static int pass;             /* 0: pictures and boxes (commands), 1: words (into the strip) */
static uint16_t *strip_;
static int sy0_, sy1_;
static void say(const char *s, float x, float y, float jx, float jy, float sc, uint16_t col, uint8_t a) {
  if (pass && a) text_into(strip_, sy0_, sy1_, s, x, y, jx, jy, sc, col, a);
}
static void pic(uint16_t tex, float x, float y, float jx, float jy, float sc, uint16_t tint, uint8_t a) {
  Tex t;
  if (pass || !a || !tex_get(tex, &t)) return;
  gfx_tex_ex(tex, x / 6, y / 6, t.fw * t.scale * jx, t.fh * t.scale * jy, sc, sc, 0, tint, a, 0);
}
static void box(float x, float y, float w, float h, uint16_t col, uint8_t a) {
  if (!pass) gfx_rect(x / 6, y / 6, w / 6, h / 6, col, a);
}
static uint16_t highlight(void) {   /* TextMenu.HighlightColor: A and B in turn */
  return fmodf(g_level.raw_time_active + M.t, 0.2f) < 0.1f ? rgb(0x84FF54) : rgb(0xFCFF59);
}
#define WHITE 0xFFFF
#define GRAY rgb(0x808080)
#define SLATE rgb(0x2F4F4F)

/* the overworld behind the menus: a night sky and HiresSnow (50 flakes blown left) */
static void backdrop(void) {
  if (pass) return;
  static const uint32_t sky[6] = {0x0b0d24, 0x121633, 0x1a1e45, 0x262759, 0x35306a, 0x45376f};
  for (int i = 0; i < 6; i++) gfx_rect(0, i * 30, VIEW_W, 30, rgb(sky[i]), 255);
  Tex t;
  if (!tex_get(T__snow, &t)) return;
  float time = M.t + 100;
  for (int i = 0; i < 24; i++) {
    uint32_t h = (uint32_t)i * 2654435761u;
    float n = (h >> 8 & 255) / 255.f;
    n = n * n * n * n;
    float sc = 0.05f + n * 0.75f, speed = sc * (2500 + (h >> 16 & 255) * 10);
    float x = 2176 - fmodf(time * speed + (h & 0xFFFF), 2176) - 128, y = (h >> 4 & 1023) + sinf(time + i) * 100;
    gfx_tex_ex(T__snow, x / 6, fmodf(y, 1080) / 6, t.fw * t.scale / 2.f, t.fh * t.scale / 2.f, sc, sc, 0, WHITE,
               (uint8_t)(255 * (1 - n * 0.8f) * 0.6f), GF_ADD);
  }
}

/* ---------------------------------------------------------------- input: confirm, cancel, the arrows with repeat */
static bool confirm(void) { return btn_pressed(&g_in.confirm); }
static bool cancel(void) { return btn_pressed(&g_in.back); }
static int dir_pressed(int axis) {   /* -1, 0, 1: the arrows, repeating while held (MenuUp/MenuDown) */
  int d = axis ? g_in.move_y : g_in.move_x;
  uint8_t code = (uint8_t)(d ? (axis ? 3 : 1) + (d > 0) * 4 : 0);
  if (!d) return 0;
  if (M.last_dir != code) {
    M.last_dir = code, M.repeat = 0.4f;
    return d;
  }
  if ((M.repeat -= RAW_DT) <= 0) {
    M.repeat = 0.1f;
    return d;
  }
  return 0;
}
static void inputs_end(void) {
  if (!g_in.move_x && !g_in.move_y) M.last_dir = 0;
}
static void go(int screen, int sel) {
  M.screen = (uint8_t)screen, M.sel = (int8_t)sel, M.t = 0, M.wig = 0;
  g_menu = (uint8_t)screen;
  btn_consume(&g_in.confirm);
  btn_consume(&g_in.back);
}

/* ---------------------------------------------------------------- chapters: what the save has */
int chapter_count(void) { return rd16(section(SEC_CHAPTERS)); }
static uint16_t area_icon(int a) {
  static const uint16_t ids[10] = {T__areas_intro, T__areas_city, T__areas_oldsite, T__areas_resort, T__areas_cliffside,
                                   T__areas_temple, T__areas_reflection, T__areas_Summit, T__areas_intro, T__areas_core};
  return ids[a];
}
static const uint32_t TITLE_BASE[10] = {0x383838, 0x6c7c81, 0x247F35, 0xb93c27, 0xFF7F83, 0x8314bc, 0x359FE0, 0xFFD819, 0x383838, 0x761008};
static const uint32_t TITLE_ACCENT[10] = {0x50AFAE, 0x2f344b, 0xE4EF69, 0xffdd42, 0x6D54B7, 0xdf72f9, 0x3C5CBC, 0x197DB7, 0x50AFAE, 0xE0201D};
static bool interlude(int a) { return a == 0 || a == 8; }
static int max_area(void) {   /* the last chapter there is, up to the ones unlocked */
  int m = 0;
  for (int a = 0; a <= 9; a++)
    if (chapter_index(a, M_A) >= 0 && a <= g_save.unlocked_areas) m = a;
  return m;
}
static bool has_mode(int a, int mode) {
  if (chapter_index(a, mode) < 0 || interlude(a)) return mode == M_A && chapter_index(a, M_A) >= 0;
  if (mode == M_B) return (g_save.cassettes >> a & 1) != 0;
  if (mode == M_C) return save_unlocked_modes() >= 3;
  return true;
}
static int popcount64(uint64_t v) {
  int n = 0;
  for (; v; v &= v - 1) n++;
  return n;
}
static bool session_here(void) {
  return g_save.has_session && g_session.in_area && g_session.area == M.area && g_session.mode == M.mode;
}
/* the panel's start points: Continue (a session here), Start, then the checkpoints reached */
static int start_count(int *ncp) {
  int ch = chapter_index(M.area, M.mode);
  *ncp = 0;
  if (ch < 0) return 0;
  const uint8_t *c = chapter_at(ch);
  int n = c[CH_NCHECKPOINTS], k = 0;
  for (int i = 0; i < n; i++)
    if (g_save.modes[M.area][M.mode].checkpoints >> i & 1) k++;
  *ncp = k;
  return (session_here() ? 1 : 0) + 1 + k;
}
static int nth_checkpoint(int k) {   /* the k-th checkpoint reached */
  for (int i = 0; i < 8; i++)
    if (g_save.modes[M.area][M.mode].checkpoints >> i & 1 && k-- == 0) return i;
  return 0;
}
static void checkpoint_name(int i, char *out) {   /* Dialog checkpoint_{area}{h}_{i} (Core's are 8's) */
  static const char *const ids[10] = {"0", "1", "2", "3", "4", "5", "6", "7", "8", "8"};
  char k[24] = "checkpoint_";
  strcat(k, ids[M.area]);
  if (M.mode == M_B) strcat(k, M.area >= 7 ? "H" : "h");
  int n = (int)strlen(k);
  k[n] = '_', k[n + 1] = (char)('0' + i), k[n + 2] = 0;
  strcpy(out, ui_str(k));
}

/* ---------------------------------------------------------------- game flow */
static void play_chapter(int start) {   /* the panel's choice: Continue, Start or a checkpoint */
  int ncp;
  int n = start_count(&ncp);
  (void)n;
  g_menu = M.screen = S_NONE;
  if (session_here() && start == 0) {
    game_play(-1, -1);
    return;
  }
  int k = start - (session_here() ? 1 : 0);
  game_play(chapter_index(M.area, M.mode), k == 0 ? -1 : nth_checkpoint(k - 1));
}
void menu_open_overworld(bool completed) {   /* the chapter select: after a chapter, its end first */
  M.area = (int8_t)(g_save.last_area <= 9 ? g_save.last_area : 1);
  M.mode = (int8_t)g_save.last_mode;
  if (M.area > max_area()) M.area = (int8_t)max_area();
  go(completed ? S_COMPLETE : S_CHAPTERS, 0);
}
void menu_open_title(void) { go(g_save.key_sheet_seen ? S_TITLE : S_SHEET, 0); }
void menu_open_main(void) { go(S_MAIN, 0); }

/* ---------------------------------------------------------------- the key sheet (as NumBlocks has it) */
static const char *key_name(int k) {
  static const char *const names[53] = {
      [4] = "OK", [5] = "Back", [6] = "Home", [12] = "shift", [13] = "alpha", [14] = "x,n,t", [15] = "var", [16] = "toolbox",
      [17] = "<x", [18] = "e^x", [19] = "ln", [20] = "log", [21] = "i", [22] = ",", [23] = "x^y", [24] = "sin",
      [25] = "cos", [26] = "tan", [27] = "pi", [28] = "sqrt", [29] = "x^2", [30] = "7", [31] = "8", [32] = "9",
      [33] = "(", [34] = ")", [36] = "4", [37] = "5", [38] = "6", [39] = "x", [40] = "/", [42] = "1", [43] = "2",
      [44] = "3", [45] = "+", [46] = "-", [48] = "0", [49] = ".", [50] = "x10^x", [51] = "ans", [52] = "EXE"};
  return k < 53 && names[k] ? names[k] : "?";
}
static const char *const ACTION_KEYS[4] = {"KEY_CONFIG_JUMP", "KEY_CONFIG_DASH", "KEY_CONFIG_GRAB", "KEY_CONFIG_TALK"};
static const char *action_on(int k) {
  for (int a = 0; a < 4; a++)
    if (g_bind[a] == k) return ui_str(ACTION_KEYS[a]);
  return NULL;
}
static void key_cap(float x, float y, float w, int k, const char *what) {   /* a key: bright with what it does */
  box(x, y, w, 174, what ? rgb(0xA0A0A0) : rgb(0x505050), 255);
  box(x + 6, y + 6, w - 12, 162, what ? rgb(0x282828) : rgb(0x181818), 255);
  say(key_name(k), x + w / 2, y + (what ? 52 : 87), 0.5f, 0.5f, 0.8f, what ? WHITE : rgb(0x707070), 255);
  if (what) say(what, x + w / 2, y + 124, 0.5f, 0.5f, 0.8f, rgb(0xFFFFA0), 255);
}
static void sheet_draw(void) {
  say("Controls", 960, 40, 0.5f, 0.5f, 1, WHITE, 255);
  /* the arrows' pad */
  if (!pass) {
    gfx_circle(40, 52, 26, rgb(0xA0A0A0), 255, 24);
    gfx_circle(40, 52, 24, rgb(0x282828), 255, 24);
  }
  say("Move", 240, 312, 0.5f, 0.5f, 0.8f, rgb(0xFFFFA0), 255);
  key_cap(780, 160, 360, 6, "Save, quit");
  key_cap(1416, 80, 480, 4, action_on(4));
  key_cap(1416, 272, 480, 5, ui_str("KEY_CONFIG_PAUSE"));
  for (int r = 0; r < 3; r++)
    for (int c = 0; c < 6; c++) {
      int k = 12 + r * 6 + c;
      key_cap(c < 2 ? 30 + c * 222 : 474 + (c - 2) * 360, 480 + r * 192, c < 2 ? 204 : 342, k, action_on(k));
    }
  say("Menus: OK or EXE confirm, Back cancels.", 960, 1050, 0.5f, 0.5f, 0.8f, WHITE, 255);
}
static void sheet_update(void) {
  if (confirm() || cancel()) {
    if (!g_save.key_sheet_seen) g_save.key_sheet_seen = 1, save_write();
    go(M.back == S_OPTIONS ? S_OPTIONS : S_TITLE, M.back == S_OPTIONS ? 1 : 0);
  }
}

/* ---------------------------------------------------------------- title and main menu */
static void title_draw(void) {
  backdrop();
  float a = fminf(1, M.t * 2);
  pic(T__title, 960, 400, 0.5f, 0.5f, 1, WHITE, (uint8_t)(a * 255));
  if (fmodf(M.t, 1) < 0.75f) pic(T__textboxbutton, 1824, 984, 0.5f, 0.5f, 1, WHITE, (uint8_t)(a * 255));
}
static void title_update(void) {
  if (M.t > 0.3f && (confirm() || cancel())) go(S_MAIN, 0);
}

static void main_draw(void) {
  static const char *const words[4] = {"MENU_BEGIN", "MENU_OPTIONS", "MENU_CREDITS", "MENU_EXIT"};
  static const uint16_t icons[4] = {T__menu_start, T__menu_options, T__menu_credits, T__menu_exit};
  backdrop();
  for (int i = 0; i < 4; i++) {
    bool on = M.sel == i;
    float x = 300 + (on ? 24 : 0), y = i == 0 ? 360 : 560 + (i - 1) * 140;
    float sc = i == 0 ? 1 : 0.6f;
    pic(icons[i], x + (i ? 50 : 100), y, 0.5f, 0.5f, sc, WHITE, 255);
    say(ui_str(words[i]), x + (i ? 120 : 230), y, 0, 0.5f, i == 0 ? 2 : 1, on ? highlight() : WHITE, 255);
  }
}
static void main_update(void) {
  int d = dir_pressed(1);
  if (d) M.sel = (int8_t)((M.sel + d + 4) % 4), M.wig = 0;
  if (cancel()) go(S_TITLE, 0);
  else if (confirm()) switch (M.sel) {
      case 0:
        M.area = (int8_t)(g_save.last_area <= max_area() ? g_save.last_area : max_area());
        M.mode = (int8_t)g_save.last_mode;
        go(S_CHAPTERS, 0);
        if (g_save.has_session && g_session.in_area) go(S_PANEL, 0);   /* (the run Home saved: Continue first) */
        break;
      case 1: M.back = S_MAIN, go(S_OPTIONS, 0); break;
      case 2: go(S_CREDITS, 0); break;
      case 3: g_running = false; break;
    }
}

/* ---------------------------------------------------------------- chapter select and panel */
static void banner(float x, float y, int a, float alpha) {   /* the title: areaselect/title and accent, chapter and name */
  uint8_t al = (uint8_t)(alpha * 255);
  pic(T__areaselect_title, x, y, 0, 0, 1, rgb(TITLE_BASE[a]), al);
  pic(T__areaselect_accent, x, y, 0, 0, 1, rgb(TITLE_ACCENT[a]), al);
  char name[16] = "AREA_";
  name[5] = (char)('0' + a), name[6] = 0;
  uint16_t text = a == 7 ? 0 : WHITE;
  if (interlude(a)) say(ui_str(name), x + 816, y + 86, 1, 0.5f, 1, text, (uint8_t)(al * 0.8f));
  else {
    char chapter[24];
    const char *c = ui_str("AREA_CHAPTER");
    int n = 0;
    for (; *c && n < 20; c++) {   /* "Chapter {x}" */
      if (*c == '{') {
        while (*c && *c != '}') c++;
        chapter[n++] = (char)('0' + (a == 9 ? 8 : a));
        continue;
      }
      chapter[n++] = *c;
    }
    chapter[n] = 0;
    say(chapter, x + 816, y + 84, 1, 1, 0.6f, rgb(TITLE_ACCENT[a]), (uint8_t)(al * 0.8f));
    say(ui_str(name), x + 816, y + 66, 1, 0, 1, text, (uint8_t)(al * 0.8f));
  }
}
static void chapters_draw(void) {
  backdrop();
  int top = max_area();
  for (int a = 0; a <= top; a++) {   /* the icons across the top, the chosen one bigger and lower */
    if (chapter_index(a, M_A) < 0) continue;
    bool on = a == M.area;
    float x = 960 + (a - M.area) * 220, y = on ? 360 : 200;
    pic(area_icon(a), x, y, 0.5f, 0.5f, on ? 1.25f + sinf(M.wig * 20) * 0.04f * fmaxf(0, 1 - M.wig * 3) : 0.75f, WHITE, 255);
  }
  banner(552, 620, M.area, 1);
  ModeStats *ms = &g_save.modes[M.area][M_A];
  if (!interlude(M.area) && (ms->berries || (ms->flags & MS_COMPLETED))) {
    char b[16];
    int ch = chapter_index(M.area, M_A), total = ch >= 0 ? chapter_at(ch)[CH_DETECTED] : 0;
    int got = popcount64(ms->berries);
    pic(T__collectables_strawberry, 840, 880, 0.5f, 0.5f, 0.6f, WHITE, 255);
    b[0] = 0;
    char *p = b;
    p += uitoa(got, p);
    *p++ = '/';
    uitoa(total, p);
    say(b, 890, 880, 0, 0.5f, 1, WHITE, 255);
  }
}
static void chapters_update(void) {
  int d = dir_pressed(0);
  if (d) {
    int a = M.area + d;
    while (a >= 0 && a <= max_area() && chapter_index(a, M_A) < 0) a += d;
    if (a >= 0 && a <= max_area()) M.area = (int8_t)a, M.wig = 0;
  }
  if (cancel()) go(S_MAIN, 0);
  else if (confirm()) {
    M.mode = M_A;
    go(S_PANEL, session_here() ? 0 : 0);
  }
}

static void panel_draw(void) {
  backdrop();
  float x = 900, y = 160;
  box(x - 24, y - 24, 1000, 880, rgb(0xE8E4DC), 255);   /* the card */
  box(x - 24, y - 24, 1000, 20, rgb(0xC8C4BC), 255);
  banner(x - 60, y, M.area, 1);
  pic(area_icon(M.area), x + 860, y + 86, 0.5f, 0.5f, 0.8f, WHITE, 255);
  /* the sides: A, B, C */
  static const char *const modes[3] = {"OVERWORLD_NORMAL", "OVERWORLD_REMIX", "OVERWORLD_REMIX2"};
  float mx = x + 40;
  for (int m = 0; m < 3; m++) {
    if (!has_mode(M.area, m)) continue;
    const char *w = interlude(M.area) ? ui_str("FILE_BEGIN") : ui_str(modes[m]);
    say(w, mx, y + 280, 0, 0.5f, 0.8f, m == M.mode ? rgb(0x101010) : rgb(0x808080), 255);
    if (m == M.mode) box(mx, y + 314, text_measure(w) * 0.8f, 8, rgb(TITLE_BASE[M.area]), 255);
    mx += text_measure(w) * 0.8f + 60;
  }
  /* where to start */
  int ncp, n = start_count(&ncp);
  bool cont = session_here();
  for (int i = 0; i < n; i++) {
    char name[48];
    if (cont && i == 0) strcpy(name, ui_str("FILE_CONTINUE"));
    else if (i == (cont ? 1 : 0)) strcpy(name, ui_str("OVERWORLD_START"));
    else checkpoint_name(nth_checkpoint(i - (cont ? 2 : 1)), name);
    say(name, x + 60, y + 400 + i * 72, 0, 0.5f, 0.9f, i == M.sel ? highlight() : rgb(0x303030), 255);
  }
  /* the stats: strawberries, deaths, the heart, the cassette */
  ModeStats *ms = &g_save.modes[M.area][M.mode];
  if (!interlude(M.area)) {
    char b[24], *p = b;
    int ch = chapter_index(M.area, M.mode), total = ch >= 0 ? chapter_at(ch)[CH_DETECTED] : 0;
    p += uitoa(popcount64(ms->berries), p);
    *p++ = '/';
    uitoa(total, p);
    pic(T__collectables_strawberry, x + 80, y + 790, 0.5f, 0.5f, 0.6f, WHITE, 255);
    say(b, x + 130, y + 790, 0, 0.5f, 0.8f, rgb(0x303030), 255);
    p = b;
    uitoa((int)ms->deaths, p);
    pic(T__collectables_skullBlue, x + 420, y + 790, 0.5f, 0.5f, 0.6f, WHITE, 255);
    say(b, x + 460, y + 790, 0, 0.5f, 0.8f, rgb(0x303030), 255);
    if (ms->flags & MS_HEART) {
      static const uint16_t hearts[3] = {SB_gui_heartgem0, SB_gui_heartgem1, SB_gui_heartgem2};
      Sprite s;
      spr_init(&s, hearts[M.mode]);
      s.sx = s.sy = 0.4f;
      if (!pass) gfx_hud(true), spr_draw(&s, (x + 760) / 6, (y + 790) / 6);
    }
    if (M.mode == M_A && (g_save.cassettes >> M.area & 1)) pic(T__collectables_cassette, x + 880, y + 790, 0.5f, 0.5f, 0.25f, WHITE, 255);
  }
}
static void panel_update(void) {
  int ncp, n = start_count(&ncp);
  int d = dir_pressed(1);
  if (d && n) M.sel = (int8_t)((M.sel + d + n) % n);
  d = dir_pressed(0);
  if (d) {
    int m = M.mode + d;
    while (m >= 0 && m < 3 && !has_mode(M.area, m)) m += d;
    if (m >= 0 && m < 3) M.mode = (int8_t)m, M.sel = 0;
  }
  if (cancel()) go(S_CHAPTERS, 0);
  else if (confirm() && n) play_chapter(M.sel);
}

/* ---------------------------------------------------------------- options and keys (TextMenu) */
#define OPT_SHAKE 1
#define OPT_FLASH 2
static const char *on_off(bool on) { return ui_str(on ? "OPTIONS_ON" : "OPTIONS_OFF"); }
static void menu_item(const char *w, const char *value, int i, float y, bool disabled) {
  uint16_t col = disabled ? SLATE : i == M.sel ? highlight() : WHITE;
  if (value) {
    say(w, 560, y, 0, 0.5f, 1, col, 255);
    say(value, 1360, y, 0.5f, 0.5f, 0.8f, col, 255);
  } else
    say(w, 960, y, 0.5f, 0.5f, 1, col, 255);
}
static void options_draw(void) {
  if (M.back == S_MAIN) backdrop();
  else box(0, 0, 1920, 1080, 0, 178);
  say(ui_str("OPTIONS_TITLE"), 960, 200, 0.5f, 0.5f, 2, GRAY, 255);
  static const char *const clocks[3] = {"OPTIONS_OFF", "OPTIONS_SPEEDRUN_CHAPTER", "OPTIONS_SPEEDRUN_FILE"};
  menu_item(ui_str("OPTIONS_KEYCONFIG"), NULL, 0, 380, false);
  menu_item("Key Sheet", NULL, 1, 448, false);
  menu_item(ui_str("OPTIONS_DISABLE_SHAKE"), on_off(!(g_save.options & OPT_SHAKE)), 2, 560, false);
  menu_item(ui_str("OPTIONS_DISABLE_FLASH"), on_off(g_save.options & OPT_FLASH), 3, 628, false);
  menu_item(ui_str("OPTIONS_SPEEDRUN"), ui_str(clocks[g_save.options >> 2 & 3]), 4, 696, false);
}
static void options_update(void) {
  int d = dir_pressed(1);
  if (d) M.sel = (int8_t)((M.sel + d + 5) % 5);
  int h = dir_pressed(0);
  bool ok = confirm();
  if (M.sel == 2 && (h || ok)) g_save.options ^= OPT_SHAKE;
  if (M.sel == 3 && (h || ok)) g_save.options ^= OPT_FLASH;
  if (M.sel == 4 && (h || ok)) {
    int c = ((g_save.options >> 2 & 3) + (h < 0 ? 2 : 1)) % 3;
    g_save.options = (uint8_t)((g_save.options & ~12) | c << 2);
  }
  if (ok && M.sel == 0) go(S_KEYS, 0), M.binding = -1;
  else if (ok && M.sel == 1) {
    uint8_t b = M.back;
    go(S_SHEET, 0);
    M.back = S_OPTIONS;
    (void)b;
  } else if (cancel()) {
    save_write();
    if (M.back == S_PAUSE) go(S_PAUSE, 0);
    else go(S_MAIN, 1);
  }
}
static bool bindable(int k) { return k == 4 || (k >= 12 && k <= 51 && k != 35 && k != 41 && k != 47); }
static void keys_draw(void) {
  if (M.back == S_MAIN) backdrop();
  else box(0, 0, 1920, 1080, 0, 178);
  say(ui_str("KEY_CONFIG_TITLE"), 960, 200, 0.5f, 0.5f, 2, GRAY, 255);
  say(ui_str("KEY_CONFIG_GAMEPLAY"), 960, 330, 0.5f, 0.5f, 0.6f, GRAY, 255);
  for (int a = 0; a < 4; a++) menu_item(ui_str(ACTION_KEYS[a]), M.binding == a ? "..." : key_name(g_bind[a]), a, 400 + a * 68, false);
  menu_item(ui_str("KEY_CONFIG_RESET"), NULL, 4, 700, false);
  if (M.binding >= 0) {
    char w[48];
    strcpy(w, ui_str("KEY_CONFIG_CHANGING"));
    strcat(w, " ");
    strcat(w, ui_str(ACTION_KEYS[M.binding]));
    say(w, 960, 860, 0.5f, 0.5f, 0.8f, WHITE, 255);
  }
}
static void keys_update(void) {
  if (M.binding >= 0) {   /* the next key pressed */
    int k = plat_key_pressed();
    if (k < 0) return;
    if (bindable(k)) g_bind[M.binding] = (uint8_t)k, memcpy(g_save.bind, g_bind, 4), save_write();
    M.binding = -1;
    btn_consume(&g_in.confirm);
    return;
  }
  int d = dir_pressed(1);
  if (d) M.sel = (int8_t)((M.sel + d + 5) % 5);
  if (confirm()) {
    if (M.sel < 4) M.binding = M.sel, plat_key_pressed();
    else plat_default_binds(), memcpy(g_save.bind, g_bind, 4), save_write();
  } else if (cancel())
    go(S_OPTIONS, 0);
}

/* ---------------------------------------------------------------- credits */
static void credits_draw(void) {
  backdrop();
  pic(T__title, 960, 380, 0.5f, 0.5f, 0.8f, WHITE, 255);
  say("Inspired by Celeste, not affiliated with Maddy Makes Games", 960, 620, 0.5f, 0.5f, 0.8f, WHITE, 255);
  say("Made by Mason Chen as part of NumPlay", 960, 720, 0.5f, 0.5f, 0.8f, WHITE, 255);
}
static void credits_update(void) {
  if (confirm() || cancel()) go(S_MAIN, 2);
}

/* ---------------------------------------------------------------- the chapter's end (AreaComplete) */
static void complete_draw(void) {
  int a = M.area;
  box(0, 0, 1920, 1080, rgb(TITLE_BASE[a]), 255);
  box(0, 760, 1920, 320, rgb(TITLE_ACCENT[a]), 255);
  pic(area_icon(a), 960, 300, 0.5f, 0.5f, 1.5f, WHITE, 255);
  ModeStats *ms = &g_save.modes[a][M.mode];
  const char *head = M.mode == M_B ? "AREACOMPLETE_BSIDE" : M.mode == M_C ? "AREACOMPLETE_CSIDE"
                     : (ms->flags & MS_FULLCLEAR) ? "AREACOMPLETE_NORMAL_FULLCLEAR" : "AREACOMPLETE_NORMAL";
  say(ui_str(head), 960, 560, 0.5f, 0.5f, 2, a == 7 ? 0 : WHITE, 255);
  char b[32], *p = b;
  int ch = chapter_index(a, M.mode);
  p += uitoa(popcount64(ms->berries), p);
  *p++ = '/';
  uitoa(ch >= 0 ? chapter_at(ch)[CH_DETECTED] : 0, p);
  if (!interlude(a)) {
    pic(T__collectables_strawberry, 640, 900, 0.5f, 0.5f, 0.8f, WHITE, 255);
    say(b, 700, 900, 0, 0.5f, 1, WHITE, 255);
    p = b;
    uitoa((int)g_session.deaths, p);
    pic(T__collectables_skullBlue, 1060, 900, 0.5f, 0.5f, 0.8f, WHITE, 255);
    say(b, 1110, 900, 0, 0.5f, 1, WHITE, 255);
  }
  time_text(g_session.time, b);
  say(b, 960, 1000, 0.5f, 0.5f, 0.8f, WHITE, 255);
  if (M.t > 1 && fmodf(M.t, 1) < 0.75f) pic(T__textboxbutton, 1824, 984, 0.5f, 0.5f, 1, WHITE, 255);
}
static void complete_update(void) {
  if (M.t > 1 && (confirm() || cancel())) {
    M.area = (int8_t)(g_save.unlocked_areas <= max_area() ? g_save.unlocked_areas : max_area());
    M.mode = M_A;
    go(S_CHAPTERS, 0);
  }
}

/* ---------------------------------------------------------------- the pause menu (Level.Pause) */
enum { P_RESUME, P_SKIP, P_RETRY, P_OPTIONS, P_SAVEQUIT, P_RESTART, P_RETURN, P_COUNT };
static int pause_items(int8_t *items) {
  int n = 0;
  items[n++] = P_RESUME;
  if (g_level.in_cutscene && !g_level.skipping_cutscene) items[n++] = P_SKIP;
  if (!g_level.in_cutscene && !g_level.skipping_cutscene) items[n++] = P_RETRY;
  items[n++] = P_OPTIONS;
  items[n++] = P_SAVEQUIT;
  items[n++] = P_RESTART;
  if (g_save.modes[0][0].flags & MS_COMPLETED) items[n++] = P_RETURN;
  return n;
}
static bool pause_disabled(int item) {
  if (item == P_RETRY) return g_level.no_retry || g_level.frozen || g_level.completed || g_player.dead;
  if (item == P_SAVEQUIT) return g_player.state == ST_REFLECTIONFALL;
  return false;
}
static void pause_draw(void) {
  static const char *const words[P_COUNT] = {"MENU_PAUSE_RESUME", "MENU_PAUSE_SKIP_CUTSCENE", "MENU_PAUSE_RETRY",
                                             "MENU_PAUSE_OPTIONS", "MENU_PAUSE_SAVEQUIT", "MENU_PAUSE_RESTARTAREA",
                                             "MENU_PAUSE_RETURN"};
  box(0, 0, 1920, 1080, 0, 178);   /* HudRenderer.BackgroundFade */
  int8_t items[P_COUNT];
  int n = pause_items(items);
  float h = 128 + n * 68 + 40, y = 540 - h / 2 + 64;
  say(ui_str("MENU_PAUSE_TITLE"), 960, y, 0.5f, 0.5f, 2, GRAY, 255);
  y += 64 + 34;
  for (int i = 0; i < n; i++) {
    if (items[i] == P_RESTART) y += 40;   /* SubHeader("") */
    menu_item(ui_str(words[items[i]]), NULL, i, y + 34, pause_disabled(items[i]));
    y += 68;
  }
}
static void confirm_draw(void) {
  box(0, 0, 1920, 1080, 0, 178);
  bool restart = M.confirm == 0;
  say(ui_str(restart ? "MENU_RESTART_TITLE" : "MENU_RETURN_TITLE"), 960, 360, 0.5f, 0.5f, 2, GRAY, 255);
  menu_item(ui_str(restart ? "MENU_RESTART_CONTINUE" : "MENU_RETURN_CONTINUE"), NULL, 0, 520, false);
  menu_item(ui_str(restart ? "MENU_RESTART_CANCEL" : "MENU_RETURN_CANCEL"), NULL, 1, 588, false);
}
void menu_pause(void) {   /* Level.Pause */
  g_level.paused = true;
  go(S_PAUSE, 0);
}
static void unpause(void) {
  g_level.paused = false;
  g_menu = M.screen = S_NONE;
  level_freeze(0.15f);   /* Engine.FreezeTimer */
  btn_consume(&g_in.jump);
  btn_consume(&g_in.dash);
}
static void pause_update(void) {
  int8_t items[P_COUNT];
  int n = pause_items(items);
  int d = dir_pressed(1);
  if (d)
    for (int k = 0; k < n; k++) {   /* (disabled ones are skipped) */
      M.sel = (int8_t)((M.sel + d + n) % n);
      if (!pause_disabled(items[M.sel])) break;
    }
  if (cancel() || btn_pressed(&g_in.pause)) {
    unpause();
    return;
  }
  if (!confirm() || pause_disabled(items[M.sel])) return;
  switch (items[M.sel]) {
    case P_RESUME: unpause(); break;
    case P_SKIP:
      unpause();
      level_skip_cutscene();
      break;
    case P_RETRY:
      unpause();
      g_time_rate = 1;
      g_level.in_cutscene = g_level.skipping_cutscene = false;
      player_die_ex(&g_player, v2(0, 0), true, true);
      break;
    case P_OPTIONS: M.back = S_PAUSE, go(S_OPTIONS, 0); break;
    case P_SAVEQUIT:   /* the session kept, a death counted: on to the map */
      g_level.paused = false;
      g_session.deaths++, g_session.deaths_in_current_level++;
      save_add_death();
      g_menu = M.screen = S_NONE;
      level_save_and_quit();
      break;
    case P_RESTART: M.confirm = 0, go(S_CONFIRM, 1); break;
    case P_RETURN: M.confirm = 1, go(S_CONFIRM, 1); break;
  }
}
static void confirm_update(void) {
  int d = dir_pressed(1);
  if (d) M.sel = (int8_t)((M.sel + d + 2) % 2);
  if (cancel() || (confirm() && M.sel == 1)) {
    go(S_PAUSE, 0);
    return;
  }
  if (!confirm()) return;
  g_level.paused = false;
  g_menu = M.screen = S_NONE;
  level_give_up(M.confirm == 0);   /* GiveUp: the chapter again, or the map */
}

/* ---------------------------------------------------------------- the level's HUD: TotalStrawberriesDisplay, the speedrun clock */
static struct { float lerp, update, wait, y; int amount; bool started; } H;
int save_total_berries(void) {   /* SaveData.TotalStrawberries */
  int n = 0;
  for (int a = 0; a < AREAS; a++)
    for (int m = 0; m < 3; m++) n += popcount64(g_save.modes[a][m].berries);
  return n;
}
void hud_update(void) {
  int total = save_total_berries();
  if (!H.started) H.started = true, H.amount = total, H.y = 96;
  if (total > H.amount && H.update <= 0) H.update = 0.4f;
  if (total > H.amount || H.update > 0 || H.wait > 0 || g_level.paused) H.lerp = approach(H.lerp, 1, 1.2f * RAW_DT);
  else H.lerp = approach(H.lerp, 0, 2 * RAW_DT);
  if (H.wait > 0) H.wait -= RAW_DT;
  if (H.update > 0 && H.lerp == 1) {
    H.update -= RAW_DT;
    if (H.update <= 0) {
      if (H.amount < total) H.amount++;
      H.wait = 2;
      if (H.amount < total) H.update = 0.3f;
    }
  }
  int clock = g_save.options >> 2 & 3;
  H.y = approach(H.y, 96.f + (clock == 1 ? 58 : clock == 2 ? 78 : 0), DT * 800);
}
void hud_reset(void) { H.started = false, H.lerp = 0; }
static void hud_draw(void) {
  int clock = g_save.options >> 2 & 3;
  if (clock) {   /* SpeedrunTimerDisplay */
    char b[16];
    pic(T__strawberryCountBG, -96, 12, 0, 0, 1, WHITE, 255);
    time_text(clock == 2 ? g_save.time + g_session.time : g_session.time, b);
    say(b, 32, 44, 0, 0.5f, 1, g_level.completed ? rgb(0x00FF00) : WHITE, 255);
  }
  if (H.lerp <= 0) return;
  Tex bg;
  float w = tex_get(T__strawberryCountBG, &bg) ? bg.fw * 6.f : 288;
  float x = roundf(-w + (32 + w) * ease_cube_out(H.lerp)), y = roundf(H.y);
  pic(T__strawberryCountBG, x - 96, y + 12 - 19, 0, 0, 1, WHITE, 255);
  pic(T__collectables_strawberry, x + 30, y, 0.5f, 0.5f, 1, WHITE, 255);
  Tex tx;
  float xw = tex_get(T__x, &tx) ? tx.fw * 6.f : 24;
  pic(T__x, x + 62 + xw / 2, y + 2, 0.5f, 0.5f, 1, WHITE, 255);
  char n[8];
  uitoa(H.amount, n);
  say(n, x + 62 + xw + 2 + text_measure(n) / 2, y, 0.5f, 0.5f, 1, WHITE, 255);
}

/* ---------------------------------------------------------------- the frame */
void menu_update(void) {
  M.t += RAW_DT, M.wig += RAW_DT;
  switch (M.screen) {
    case S_SHEET: sheet_update(); break;
    case S_TITLE: title_update(); break;
    case S_MAIN: main_update(); break;
    case S_CHAPTERS: chapters_update(); break;
    case S_PANEL: panel_update(); break;
    case S_OPTIONS: options_update(); break;
    case S_KEYS: keys_update(); break;
    case S_CREDITS: credits_update(); break;
    case S_COMPLETE: complete_update(); break;
    case S_PAUSE: pause_update(); break;
    case S_CONFIRM: confirm_update(); break;
  }
  inputs_end();
}
static void draw_screen(void) {
  switch (M.screen) {
    case S_SHEET: sheet_draw(); break;
    case S_TITLE: title_draw(); break;
    case S_MAIN: main_draw(); break;
    case S_CHAPTERS: chapters_draw(); break;
    case S_PANEL: panel_draw(); break;
    case S_OPTIONS: options_draw(); break;
    case S_KEYS: keys_draw(); break;
    case S_CREDITS: credits_draw(); break;
    case S_COMPLETE: complete_draw(); break;
    case S_PAUSE: pause_draw(); break;
    case S_CONFIRM: confirm_draw(); break;
  }
}
static void words_strip(uint16_t *strip, int y0, int y1, void *ctx) {
  (void)ctx;
  pass = 1, strip_ = strip, sy0_ = y0, sy1_ = y1;
  if (g_in_level && !g_level.in_credits) hud_draw();
  draw_screen();
  pass = 0;
}
/* drawn over the level (its HUD, the pause) or alone (the overworld): pictures, then a layer of words */
void menu_render(void) {
  if (!g_menu) M.screen = S_NONE;   /* (a level started elsewhere closes them) */
  bool hud = g_in_level && !g_level.in_credits && (H.lerp > 0 || (g_save.options & 12));
  if (M.screen == S_NONE && !hud) return;
  gfx_hud(true);
  pass = 0;
  if (hud) hud_draw();
  draw_screen();
  gfx_hud(true);
  gfx_custom(words_strip, NULL, 0, VIEW_H);
  gfx_hud(false);
}
