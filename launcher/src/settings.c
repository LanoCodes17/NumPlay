/* Settings: the list of installed games, and uninstalling one of them behind
 * a clear warning and a hold-to-confirm button. */
#include <string.h>
#include "sys.h"
#include "ui.h"

#define BG_TOP 0x3B4252
#define BG_BOTTOM 0x171A21
#define DANGER 0xE5484D
#define DANGER_TOP 0x8C1D23
#define DANGER_BOTTOM 0x2A080B
#define OK_GREEN 0x2FB36B
#define ROW_H 34
#define ROWS_Y 50
#define HOLD_MS 1400

static char *fmt_kb(char *out, uint32_t bytes) {
  uint32_t kb = (bytes + 1023) / 1024;
  char t[8];
  int n = 0;
  do t[n++] = (char)('0' + kb % 10);
  while (kb /= 10);
  char *o = out;
  while (n) *o++ = t[--n];
  strcpy(o, " KB");
  return out;
}

/* ---------------------------------------------------------------- list */
typedef struct {
  int n, sel, top;
  int8_t games[16];
  float cursor;       /* animated selection */
} list_t;

static void draw_thumb(int game, int x, int y, int w, int h) {
  const np_game_t *g = &np_games[game];
  const uint8_t *px = ui_shot(game, 0);
  if (px) gfx_image(px, g->shots[0].palette, g->shots[0].ncolors, SHOT_W, SHOT_H, x, y, w, h, 5, 0);
  else gfx_rrect(x, y, w, h, 5, gfx_rgb(g->top), 256);
}

static void list_scene(void *ctx) {
  list_t *l = ctx;
  gfx_vgrad(0, 0, SCREEN_W, SCREEN_H, BG_TOP, BG_BOTTOM);
  if (gfx_y0 < 44) {
    gfx_icon(&np_icon_back, 14, 17, 0xFFFF, 200);
    gfx_text(&np_font_title, 36, 30, "Settings", 0xFFFF, 256);
    uint32_t total = 0;
    for (int i = 0; i < l->n; i++) total += np_game_size(l->games[i]);
    char kb[16];
    if (l->n) {
      gfx_text_right(&np_font_small, SCREEN_W - 14, 20, "INSTALLED GAMES", 0xFFFF, 150);
      gfx_text_right(&np_font_body, SCREEN_W - 14, 34, fmt_kb(kb, total), 0xFFFF, 230);
    }
  }
  if (!l->n) {
    gfx_icon(&np_icon_check, 145, 92, 0xFFFF, 150);
    gfx_text_center(&np_font_body, 160, 146, "No games left to uninstall", 0xFFFF, 220);
    gfx_text_center(&np_font_small, 160, 164, "Reinstall NumPlay to get them back", 0xFFFF, 150);
    return;
  }
  /* selection highlight slides between rows */
  float cy = ROWS_Y + (l->cursor - l->top) * (ROW_H + 4);
  gfx_rrect(10, (int)cy, SCREEN_W - 20, ROW_H, 10, 0xFFFF, 46);
  for (int r = 0; r < 5 && l->top + r < l->n; r++) {
    int i = l->top + r, game = l->games[i];
    int y = ROWS_Y + r * (ROW_H + 4);
    if (y >= gfx_y1 || y + ROW_H <= gfx_y0) continue;
    const np_game_t *g = &np_games[game];
    draw_thumb(game, 16, y + 4, 35, 26);
    gfx_text(&np_font_body, 60, y + 15, g->title, 0xFFFF, 256);
    char kb[16];
    gfx_text(&np_font_small, 60, y + 28, fmt_kb(kb, np_game_size(game)), 0xFFFF, 150);
    if (i == l->sel) {
      gfx_rrect(SCREEN_W - 104, y + 7, 86, 20, 10, gfx_rgb(DANGER), 256);
      gfx_icon(&np_icon_trash, SCREEN_W - 96, y + 9, 0xFFFF, 256);
      gfx_text(&np_font_small, SCREEN_W - 78, y + 21, "Uninstall", 0xFFFF, 256);
    }
  }
}

/* ---------------------------------------------------------------- dialogs */
typedef struct {
  int game;
  int choice;         /* 0 cancel, 1 uninstall */
  float hold;         /* 0..1 while OK is held on "Uninstall" */
  bool hinted;
  int stage;          /* 0 confirm, 1 working, 2 done, 3 error */
  int error;
  float progress;
  uint32_t freed;
} dialog_t;

static const char *error_text(int e) {
  switch (e) {
    case NP_UNINSTALL_UNSUPPORTED:
      return "Your calculator's software doesn't allow apps to uninstall parts of themselves. Update it to Epsilon 21 or "
             "newer at my.numworks.com, then try again.";
    case NP_UNINSTALL_BATTERY:
      return "The battery is too low to safely erase flash memory. Plug in your calculator, then try again.";
    default:
      return "Something went wrong while erasing. To be safe, reinstall NumPlay from my.numworks.com/apps.";
  }
}

static void dialog_scene(void *ctx) {
  dialog_t *d = ctx;
  const np_game_t *g = &np_games[d->game];
  char line[64];
  if (d->stage == 2) {
    gfx_vgrad(0, 0, SCREEN_W, SCREEN_H, 0x1F5A3C, 0x0B1A12);
    gfx_circle(160, 74, 34, gfx_rgb(OK_GREEN), 256);
    gfx_icon(&np_icon_check, 145, 59, 0xFFFF, 256);
    strcpy(line, g->title);
    strcat(line, " was uninstalled");
    gfx_text_center(&np_font_title, 160, 142, line, 0xFFFF, 256);
    char kb[16];
    strcpy(line, fmt_kb(kb, d->freed));
    strcat(line, " of flash memory freed");
    gfx_text_center(&np_font_body, 160, 166, line, 0xFFFF, 200);
    ui_button(110, 192, 100, 28, "OK", true, OK_GREEN, 256);
    return;
  }
  gfx_vgrad(0, 0, SCREEN_W, SCREEN_H, DANGER_TOP, DANGER_BOTTOM);
  gfx_icon(&np_icon_warning, 137, 14, 0xFFFF, 256);
  if (d->stage == 3) {
    gfx_text_center(&np_font_title, 160, 84, "Couldn't uninstall", 0xFFFF, 256);
    gfx_paragraph(&np_font_body, 160, 112, 280, 17, error_text(d->error), 0xFFFF, 220);
    ui_button(110, 196, 100, 28, "OK", true, DANGER, 256);
    return;
  }
  if (d->stage == 1) {
    strcpy(line, "Uninstalling ");
    strcat(line, g->title);
    strcat(line, "...");
    gfx_text_center(&np_font_title, 160, 96, line, 0xFFFF, 256);
    gfx_rrect(40, 118, 240, 10, 5, 0xFFFF, 60);
    int w = (int)(240 * d->progress);
    if (w >= 10) gfx_rrect(40, 118, w, 10, 5, 0xFFFF, 256);
    gfx_text_center(&np_font_body, 160, 152, "Don't turn off your calculator.", 0xFFFF, 200);
    return;
  }
  strcpy(line, "Uninstall ");
  strcat(line, g->title);
  strcat(line, "?");
  gfx_text_center(&np_font_title, 160, 80, line, 0xFFFF, 256);
  strcpy(line, "This deletes ");
  strcat(line, g->title);
  int y = gfx_paragraph(&np_font_body, 160, 102, 290, 16, line, 0xFFFF, 240);
  y = gfx_paragraph(&np_font_body, 160, y, 290, 16, "and all of its saved progress. You can't undo this.", 0xFFFF, 240);
  gfx_paragraph(&np_font_small, 160, y + 4, 290, 13,
                "To play it again, you'll have to reinstall all of NumPlay from my.numworks.com/apps, and "
                "reinstalling may reset your progress in every game.",
                0xFFFF, 170);
  ui_button(38, 198, 110, 28, "Cancel", d->choice == 0, 0x555D70, 256);
  /* the uninstall button fills up while OK is held */
  int bx = 172, by = 198, bw = 110, bh = 28;
  if (d->choice == 1) {
    gfx_rrect(bx, by + 2, bw, bh, bh / 2, 0, 64);
    gfx_rrect(bx, by, bw, bh, bh / 2, gfx_rgb(DANGER), 256);
    int fill = (int)(bw * d->hold);
    if (fill > 0) {
      gfx_rrect(bx, by, NP_MAX(fill, bh), bh, bh / 2, 0xFFFF, 110);
    }
    gfx_text_center(&np_font_body, bx + bw / 2, by + 18, d->hinted ? "Hold OK" : "Uninstall", 0xFFFF, 256);
  } else {
    gfx_rrect(bx, by, bw, bh, bh / 2, 0xFFFF, 40);
    gfx_text_center(&np_font_body, bx + bw / 2, by + 18, "Uninstall", 0xFFFF, 200);
  }
}

static dialog_t *active_dialog;
static void on_progress(int done, int total) {
  active_dialog->progress = total ? (float)done / total : 1;
  ui_frame(dialog_scene, active_dialog);
}

static void wait_ok(dialog_t *d) {
  ui_keys_t k = {np_keys(), 0, 0};
  ui_frame(dialog_scene, d);
  for (;;) {
    uint32_t p = ui_poll(&k);
    if (p & (K_OK | K_BACK | K_HOME)) return;
    np_sleep(16);
  }
}

/* Returns true if the game was uninstalled. */
static bool uninstall_flow(int game) {
  dialog_t d = {game, 0, 0, false, 0, 0, 0, 0};
  active_dialog = &d;
  if (!np_uninstall_supported() || !np_battery_ok()) {
    d.stage = 3;
    d.error = !np_uninstall_supported() ? NP_UNINSTALL_UNSUPPORTED : NP_UNINSTALL_BATTERY;
    wait_ok(&d);
    return false;
  }
  ui_keys_t k = {np_keys(), 0, 0};
  uint32_t last = np_millis();
  bool dirty = true;
  for (;;) {
    uint32_t now = np_millis(), dt = now - last;
    last = now;
    uint32_t p = ui_poll(&k);
    if (p & (K_BACK | K_HOME)) return false;
    if (p & K_LEFT) d.choice = 0, d.hold = 0, dirty = true;
    if (p & K_RIGHT) d.choice = 1, dirty = true;
    if (p & K_OK) {
      if (d.choice == 0) return false;
      d.hinted = true;
      dirty = true;
    }
    if (d.choice == 1 && (k.held & K_OK)) {
      d.hold += dt / (float)HOLD_MS;
      dirty = true;
      if (d.hold >= 1) break;
    } else if (d.hold > 0) {
      d.hold = NP_MAX(d.hold - dt / 300.f, 0.f);
      dirty = true;
    }
    if (dirty) {
      ui_frame(dialog_scene, &d);
      dirty = false;
    } else {
      np_sleep(16);
    }
  }
  d.stage = 1;
  d.freed = np_game_size(game);
  ui_frame(dialog_scene, &d);
  int r = np_uninstall(game, on_progress);
  np_wait_release();
  if (r == NP_UNINSTALL_OK) {
    d.stage = 2;
  } else {
    d.stage = 3;
    d.error = r;
  }
  wait_ok(&d);
  return r == NP_UNINSTALL_OK;
}

void np_settings(void) {
  ui_init();
  list_t l = {0};
  for (int i = 0; i < np_game_count; i++)
    if (np_game_installed(i)) l.games[l.n++] = (int8_t)i;
  ui_keys_t k = {np_keys(), 0, 0};
  uint32_t last = np_millis();
  bool dirty = true;
  for (;;) {
    uint32_t now = np_millis();
    float dt = NP_MIN((now - last) / 1000.f, 0.05f);
    last = now;
    uint32_t p = ui_poll(&k);
    if (p & (K_BACK | K_HOME)) return;
    if (l.n) {
      if (p & K_UP) l.sel = NP_MAX(l.sel - 1, 0);
      if (p & K_DOWN) l.sel = NP_MIN(l.sel + 1, l.n - 1);
      if (l.sel < l.top) l.top = l.sel;
      if (l.sel > l.top + 4) l.top = l.sel - 4;
      if (p & (K_OK | K_BACKSPACE)) {
        if (uninstall_flow(l.games[l.sel])) {
          l.n = 0;
          for (int i = 0; i < np_game_count; i++)
            if (np_game_installed(i)) l.games[l.n++] = (int8_t)i;
          l.sel = NP_MIN(l.sel, NP_MAX(l.n - 1, 0));
          l.top = NP_MIN(l.top, l.sel);
          l.cursor = l.sel;
        }
        k.held = np_keys();
        last = np_millis();
        dirty = true;
      }
      if (p) dirty = true;
    }
    float c = ui_approach(l.cursor, (float)l.sel, dt, 0.05f);
    if (c != l.cursor) dirty = true;
    l.cursor = c;
    if (dirty) {
      ui_frame(list_scene, &l);
      dirty = false;
    } else {
      np_sleep(16);
    }
  }
}
