/* "My levels" list and the level editor, laid out like Geometry Dash's
 * editor: grid view on top, build/edit/delete modes and an object palette
 * in the bottom panel. */
#include "app.h"
#include "ui.h"
#include <math.h>
#include <string.h>
#include "../../common/np_text.h"

enum { MODE_BUILD, MODE_EDIT, MODE_DELETE };
enum { UNDO_ADD, UNDO_REMOVE, UNDO_XF };
typedef struct { uint8_t op; uint16_t index; LObj obj; } Undo;
#define UNDO_N 32
#define PAGE_ITEMS 12
#define PAGES ((EDITOR_TYPES + PAGE_ITEMS - 1) / PAGE_ITEMS)

static Level edit_level;
static CustomMeta meta;
static unsigned count;
static Undo undo[UNDO_N];
static int undo_len;
static SceneOpts opts;
static bool settings_open;
static int settings_sel, clear_armed;
static const char *const slot_names[CUSTOM_SLOTS] = {T("MY LEVEL 1"), T("MY LEVEL 2"), T("MY LEVEL 3")};
static const char *const theme_names[THEME_COUNT] = {T("BLUE"), T("PINK"), T("GREEN"), T("RED"), T("OCEAN"), T("PURPLE"), T("VIOLET"),
                                                              T("LIME"), T("PLUM")};

static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

static void fill_level(Level *L, int slot, const CustomMeta *m, unsigned n) {
  memset(L, 0, sizeof(*L));
  const LevelDef *d = &level_defs[m->theme % THEME_COUNT];
  L->name = slot_names[slot];
  unsigned end = n ? level_objs[n - 1].x + 300u : 0;
  if (end < 900) end = 900;
  if (end > 64000) end = 64000;
  L->end_x = (uint16_t)end;
  L->wall_x = (uint16_t)end;
  memcpy(L->colors[CH_BG], d->bg, 3);
  memcpy(L->colors[CH_G1], d->g1, 3);
  memset(L->colors[CH_LINE], 255, 3);
  memset(L->colors[CH_OBJ], 255, 3);
  L->bpm = 120;
  L->start_mode = m->flags & 1 ? MODE_SHIP : MODE_CUBE;
  level_load_flat(L, level_objs, n);
}

bool editor_load_slot(int slot, Level *L) {
  if (slot < 0 || slot >= CUSTOM_SLOTS) return false;
  if (!app.testing) {
    CustomMeta m = {0};
    count = 0;
    if (save_read_custom(slot, level_objs, CUSTOM_MAX, &m)) count = m.count;
    else m = custom_meta[slot];
    meta = m;
  }
  fill_level(L, slot, &meta, count);
  return level_valid(L) || count == 0;
}

static bool save_level(void) {
  meta.count = (uint16_t)count;
  fill_level(&edit_level, app.slot, &meta, count);
  meta.end_x = edit_level.end_x;
  meta.exists = true;
  bool ok = save_write_custom(app.slot, level_objs, count, &meta);
  if (ok) {
    custom_meta[app.slot] = meta;
    app.dirty = false;
    /* edits reset the progress of the level */
  }
  return ok;
}

unsigned editor_object_count(void) { return count; }

static bool keys_open; /* the key guide (see below) */
static bool keys_tick(void);

void editor_flush(void) {
  if (app.dirty) save_level();
}

void editor_open(int slot) {
  if (!app.testing) {
    editor_load_slot(slot, &edit_level);
    app.cur_x = 7;
    app.cur_y = 0;
    app.edit_cam_x = 0;
    app.edit_cam_y = 0;
    app.brush = 0;
    app.rotate = 0;
    app.edit_mode = MODE_BUILD;
    app.edit_page = 0;
    undo_len = 0;
    app.dirty = false;
  }
  app.testing = false;
  settings_open = false;
  keys_open = !(progress.options & OPT_KEYS_SEEN); /* the first time: which key does what */
  fx_reset();
  fill_level(&edit_level, slot, &meta, count);
}

/* ------------------------------------------------------------ editing */

static int brush_type(void) { return editor_types[clampi(app.brush, 0, EDITOR_TYPES - 1)]; }

static void cell_offset(int type, int rot, int *ox, int *oy) {
  int dy = objdefs[type].editor_dy;
  switch (rot & 3) {
    case 1: *ox = dy; *oy = 0; break;
    case 2: *ox = 0; *oy = -dy; break;
    case 3: *ox = -dy; *oy = 0; break;
    default: *ox = 0; *oy = dy; break;
  }
}

static unsigned lower_bound(int x) {
  unsigned a = 0, b = count;
  while (a < b) {
    unsigned m = (a + b) / 2;
    if (level_objs[m].x < x) a = m + 1; else b = m;
  }
  return a;
}

static int find_in_cell(int col, int row, int from_end) {
  int x0 = col * 30, y0 = row * 30;
  unsigned i = lower_bound(x0);
  int found = -1;
  for (; i < count && level_objs[i].x < x0 + 30; i++)
    if (level_objs[i].y >= y0 && level_objs[i].y < y0 + 30) {
      found = (int)i;
      if (!from_end) break;
    }
  return found;
}

static void push_undo(int op, unsigned index, LObj o) {
  if (undo_len == UNDO_N) { memmove(undo, undo + 1, sizeof(Undo) * (UNDO_N - 1)); undo_len--; }
  undo[undo_len++] = (Undo){(uint8_t)op, (uint16_t)index, o};
}

static void remove_at(unsigned i) {
  memmove(level_objs + i, level_objs + i + 1, (count - i - 1) * sizeof(LObj));
  count--;
}
static unsigned insert_obj(LObj o) {
  unsigned i = count;
  while (i && level_objs[i - 1].x > o.x) { level_objs[i] = level_objs[i - 1]; i--; }
  level_objs[i] = o;
  count++;
  return i;
}

static void edited(void) {
  app.dirty = true;
  fill_level(&edit_level, app.slot, &meta, count);
  LevelStat *s = &progress.lv[LEVEL_COUNT + app.slot];
  s->normal = s->practice = 0;
}

static void place(void) {
  int type = brush_type(), ox, oy;
  cell_offset(type, app.rotate, &ox, &oy);
  LObj o = {(uint16_t)(app.cur_x * 30 + 15 + ox), (int16_t)(app.cur_y * 30 + 15 + oy), (uint8_t)type, (uint8_t)(app.rotate & 3)};
  /* the same object in the same cell is replaced */
  int x0 = app.cur_x * 30, y0 = app.cur_y * 30;
  for (unsigned i = lower_bound(x0); i < count && level_objs[i].x < x0 + 30; i++)
    if (level_objs[i].type == type && level_objs[i].y >= y0 && level_objs[i].y < y0 + 30) {
      push_undo(UNDO_REMOVE, i, level_objs[i]);
      remove_at(i);
      break;
    }
  if (count >= CUSTOM_MAX) { app_notice(T("OBJECT LIMIT REACHED")); return; }
  unsigned i = insert_obj(o);
  push_undo(UNDO_ADD, i, o);
  edited();
}

static void erase(void) {
  int i = find_in_cell(app.cur_x, app.cur_y, 1);
  if (i < 0) return;
  push_undo(UNDO_REMOVE, (unsigned)i, level_objs[i]);
  remove_at((unsigned)i);
  edited();
}

static void rotate_obj(void) {
  int i = find_in_cell(app.cur_x, app.cur_y, 1);
  if (i < 0) return;
  LObj *o = &level_objs[i];
  push_undo(UNDO_XF, (unsigned)i, *o);
  int ox, oy, nx, ny, r = o->xf & 3;
  cell_offset(o->type, r, &ox, &oy);
  cell_offset(o->type, r + 1, &nx, &ny);
  o->x = (uint16_t)(o->x - ox + nx);
  o->y = (int16_t)(o->y - oy + ny);
  o->xf = (uint8_t)((o->xf & ~3) | ((r + 1) & 3));
  /* keep the list sorted after the horizontal shift */
  LObj moved = *o;
  remove_at((unsigned)i);
  insert_obj(moved);
  undo_len = 0;   /* index-based history is no longer valid */
  edited();
}

static void undo_last(void) {
  if (!undo_len) { app_notice(T("NOTHING TO UNDO")); return; }
  Undo u = undo[--undo_len];
  if (u.op == UNDO_ADD) {
    if (u.index < count) remove_at(u.index);
  } else if (u.op == UNDO_REMOVE) {
    if (count < CUSTOM_MAX) insert_obj(u.obj);
  } else if (u.index < count) {
    level_objs[u.index] = u.obj;
  }
  edited();
}

static void pick(void) {
  int i = find_in_cell(app.cur_x, app.cur_y, 1);
  if (i < 0) return;
  for (int k = 0; k < EDITOR_TYPES; k++)
    if (editor_types[k] == level_objs[i].type) { app.brush = k; app.edit_page = k / PAGE_ITEMS; }
  app.rotate = level_objs[i].xf & 3;
  app.edit_mode = MODE_BUILD;
}

static void settings_tick(void) {
  if (app_hit(K_BACK | K_PROPS)) { settings_open = false; return; }
  if (app_hit(K_UP)) settings_sel = (settings_sel + 2) % 3;
  if (app_hit(K_DOWN)) settings_sel = (settings_sel + 1) % 3;
  if (settings_sel != 2) clear_armed = 0;
  int d = app_hit(K_RIGHT) ? 1 : app_hit(K_LEFT) ? -1 : 0;
  if (settings_sel == 0 && d) { meta.theme = (uint8_t)((meta.theme + THEME_COUNT + d) % THEME_COUNT); edited(); }
  if (settings_sel == 1 && (d || app_accept())) { meta.flags ^= 1; edited(); }
  if (settings_sel == 2 && app_accept()) {
    if (clear_armed) {
      count = 0;
      undo_len = 0;
      clear_armed = 0;
      edited();
      app_notice(T("LEVEL CLEARED"));
    } else {
      clear_armed = 1;
    }
  }
}

void editor_tick(void) {
  if (app.fading_out) return;
  if (keys_tick()) return;
  if (settings_open) { settings_tick(); return; }
  uint32_t h = app.hit;
  if (h & K_BACK) {
    if (app.dirty && !save_level()) app_notice(T("SAVE FAILED - STORAGE FULL"));
    app_go(SCR_CREATOR);
    return;
  }
  if (h & K_EXE) {
    if (app.dirty && !save_level()) app_notice(T("NOT SAVED - STORAGE FULL"));
    app.testing = true;
    app.level = LEVEL_COUNT + app.slot;
    app.practice = false;
    app_go(SCR_PLAY);
    return;
  }
  if (h & K_LEFT) app.cur_x--;
  if (h & K_RIGHT) app.cur_x++;
  if (h & K_UP) app.cur_y++;
  if (h & K_DOWN) app.cur_y--;
  app.cur_x = clampi(app.cur_x, 0, 2100);
  app.cur_y = clampi(app.cur_y, 0, 39);
  if (h & (K_PLUS | K_TOOL)) { app.brush = (app.brush + 1) % EDITOR_TYPES; app.edit_mode = MODE_BUILD; }
  if (h & K_MINUS) { app.brush = (app.brush + EDITOR_TYPES - 1) % EDITOR_TYPES; app.edit_mode = MODE_BUILD; }
  app.edit_page = app.brush / PAGE_ITEMS;
  if (h & K_SHIFT) app.rotate = (app.rotate + 1) & 3;
  if (h & K_CHECK) app.edit_mode = (app.edit_mode + 1) % 3;
  if (h & (K_OK)) {
    if (app.edit_mode == MODE_BUILD) place();
    else if (app.edit_mode == MODE_EDIT) rotate_obj();
    else erase();
  }
  if (h & K_ERASE) erase();
  if (h & K_UNDO) undo_last();
  if (h & K_COPY) pick();
  if (h & K_SAVE) app_notice(save_level() ? T("LEVEL SAVED") : T("SAVE FAILED - STORAGE FULL"));
  if (h & K_PROPS) { settings_open = true; settings_sel = 0; clear_armed = 0; }
}

void editor_frame(float dt) {
  /* camera keeps the cursor inside the view */
  float cx = app.cur_x * 30 + 15, cy = app.cur_y * 30 + 15;
  float want_x = app.edit_cam_x, want_y = app.edit_cam_y;
  if (cx - want_x < 90) want_x = cx - 90;
  if (cx - want_x > VIEW_W - 90) want_x = cx - (VIEW_W - 90);
  if (want_x < -60) want_x = -60;
  /* the bottom panel hides the lowest 90 units of the view */
  if (cy - want_y > 250) want_y = cy - 250;
  if (cy - want_y < 15) want_y = cy - 15;
  if (want_y < 0) want_y = 0;
  float k = fminf(1, dt * 14);
  app.edit_cam_x += (want_x - app.edit_cam_x) * k;
  app.edit_cam_y += (want_y - app.edit_cam_y) * k;
  Game *g = &app.g;
  memset(g, 0, sizeof(*g));
  g->L = &edit_level;
  g->cam_x = app.edit_cam_x;
  g->cam_y = app.edit_cam_y;
  g->ground_x = g->bg_x = app.edit_cam_x;
  g->dead = true;
  for (int c = 0; c < CH_COUNT; c++) memcpy(g->ch[c].cur, edit_level.colors[c], 3);
  memset(&opts, 0, sizeof(opts));
  opts.p1 = app_p1();
  opts.p2 = app_p2();
  opts.editor = true;
  opts.hide_player = true;
  opts.time = app.t;
  scene_prepare(g, &opts);
}

/* ------------------------------------------------------------ key guide */

/* The calculator's keys (its screen left out), and the ones the editor uses
 * labelled on both sides: the key's name, what it does, and a line to it.
 * Shown the first time the editor opens, and from My Levels anytime. */

typedef struct {
  int16_t x, y;     /* the key's centre on the drawing */
  const char *name, *what;
} KeyLabel;

/* sorted from top to bottom on each side, so the lines do not cross */
static const KeyLabel keys_left[] = {
    {159, 57, T("HOME"), T("QUIT")},           {126, 66, T("ARROWS"), T("MOVE")},
    {121, 95, "SHIFT", T("ROTATE")},           {136, 95, "ALPHA", T("UNDO")},
    {151, 95, "X,N,T", T("COPY BLOCK")},       {136, 108, "LN", T("SETTINGS")},
    {123, 178, "0", T("CHANGE MODE")},
};
static const KeyLabel keys_right[] = {
    {181, 62, "OK", T("USE THE MODE")},        {198, 62, T("BACK"), T("SAVE, LEAVE")},
    {166, 95, "VAR", T("SAVE")},               {181, 95, T("TOOLBOX"), T("NEXT BLOCK")},
    {196, 95, T("BACKSPACE"), T("DELETE")},    {177, 164, "+", T("NEXT BLOCK")},
    {195, 164, "-", T("PREV BLOCK")},          {195, 178, "EXE", T("PLAYTEST")},
};

static void thin_line(int x0, int y0, int x1, int y1, color_t c, unsigned a) {
  int dx = x1 > x0 ? x1 - x0 : x0 - x1, dy = y1 > y0 ? y0 - y1 : y1 - y0;
  int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, err = dx + dy;
  for (;;) {
    gfx_blend(x0, y0, 1, 1, c, a);
    if (x0 == x1 && y0 == y1) break;
    int e2 = 2 * err;
    if (e2 >= dy) err += dy, x0 += sx;
    if (e2 <= dx) err += dx, y0 += sy;
  }
}

static bool key_used(int x, int y) {
  if (x >= 111 && x < 142 && y >= 52 && y < 83) return true; /* the arrows */
  for (unsigned i = 0; i < sizeof keys_left / sizeof *keys_left; i++)
    if (keys_left[i].x >= x && keys_left[i].x < x + 16 && keys_left[i].y >= y && keys_left[i].y < y + 11) return true;
  for (unsigned i = 0; i < sizeof keys_right / sizeof *keys_right; i++)
    if (keys_right[i].x >= x && keys_right[i].x < x + 16 && keys_right[i].y >= y && keys_right[i].y < y + 11) return true;
  return false;
}

static void key(int x, int y, int w, int h, color_t c) {
  color_t face = key_used(x, y) ? rgb(255, 214, 90) : c;
  gfx_round_rect(x, y, w, h, 3, rgb(90, 90, 96), 256);
  gfx_round_rect(x + 1, y + 1, w - 2, h - 2, 2, face, 256);
}

static void keys_draw(void) {
  gfx_blend(0, 0, GFX_W, GFX_H, rgb(10, 14, 40), 235);
  ui_title(FONT_BIG, 160, 22, T("EDITOR KEYS"));
  /* the calculator, without its screen */
  gfx_round_rect(108, 42, 104, 152, 10, rgb(60, 62, 70), 256);
  gfx_round_rect(110, 44, 100, 148, 9, rgb(236, 236, 238), 256);
  key(122, 52, 9, 9, 0xffff);   /* up */
  key(122, 72, 9, 9, 0xffff);   /* down */
  key(111, 62, 10, 9, 0xffff);  /* left */
  key(132, 62, 10, 9, 0xffff);  /* right */
  gfx_round_rect(150, 53, 19, 9, 4, rgb(255, 176, 40), 256);  /* home */
  gfx_round_rect(150, 68, 19, 9, 4, rgb(50, 50, 56), 256);    /* on/off */
  gfx_round_rect(175, 56, 13, 13, 6, rgb(90, 90, 96), 256);   /* OK */
  gfx_round_rect(176, 57, 11, 11, 5, rgb(255, 214, 90), 256);
  gfx_round_rect(192, 56, 13, 13, 6, rgb(90, 90, 96), 256);   /* back */
  gfx_round_rect(193, 57, 11, 11, 5, rgb(255, 214, 90), 256);
  for (int r = 0; r < 3; r++)
    for (int c = 0; c < 6; c++) key(114 + c * 15, 90 + r * 13, 14, 11, 0xffff);
  for (int r = 0; r < 4; r++)
    for (int c = 0; c < 5; c++) key(114 + c * 18, 131 + r * 14, 17, 12, 0xffff);
  /* the labels: the key's name, what it does, and a line to the key */
  color_t line = rgb(255, 150, 0);
  unsigned nl = sizeof keys_left / sizeof *keys_left, nr = sizeof keys_right / sizeof *keys_right;
  for (unsigned i = 0; i < nl; i++) {
    const KeyLabel *k = &keys_left[i];
    int y = 48 + (int)i * 23;
    if (NP_TEXT_EXTRA && (uint8_t)k->what[0] >= 0xE0) { /* Chinese, taller: the key and what it does on one line */
      int ww = gfx_text_width(FONT_SMALL, k->what);
      gfx_text_right(FONT_SMALL, 100 - ww - 2, y + 11, k->name, GOLD_TOP, GOLD_BOTTOM, 256);
      gfx_text_right(FONT_SMALL, 100, y + 11, k->what, 0xffff, 0xffff, 220);
    } else {
      gfx_text_right(FONT_SMALL, 100, y + 8, k->name, GOLD_TOP, GOLD_BOTTOM, 256);
      gfx_text_right(FONT_SMALL, 100, y + 18, k->what, 0xffff, 0xffff, 220);
    }
    thin_line(102, y + 4, k->x, k->y, line, 256);
    gfx_round_rect(k->x - 1, k->y - 1, 3, 3, 1, line, 256);
  }
  for (unsigned i = 0; i < nr; i++) {
    const KeyLabel *k = &keys_right[i];
    int y = 44 + (int)i * 20;
    if (NP_TEXT_EXTRA && (uint8_t)k->what[0] >= 0xE0) {
      gfx_text(FONT_SMALL, 220, y + 11, k->name, GOLD_TOP, GOLD_BOTTOM, 256);
      gfx_text(FONT_SMALL, 220 + gfx_text_width(FONT_SMALL, k->name) + 2, y + 11, k->what, 0xffff, 0xffff, 220);
    } else {
      gfx_text(FONT_SMALL, 220, y + 8, k->name, GOLD_TOP, GOLD_BOTTOM, 256);
      gfx_text(FONT_SMALL, 220, y + 18, k->what, 0xffff, 0xffff, 220);
    }
    thin_line(218, y + 4, k->x, k->y, line, 256);
    gfx_round_rect(k->x - 1, k->y - 1, 3, 3, 1, line, 256);
  }
  gfx_text_center(FONT_SMALL, 160, 232, T("OK: GOT IT"), 0xffff, 0xffff, 230);
}

/* while the guide is open it takes the keys; closing it the first time
   remembers it was seen */
static bool keys_tick(void) {
  if (!keys_open) return false;
  if (app_hit(K_OK | K_EXE | K_BACK)) {
    keys_open = false;
    if (!(progress.options & OPT_KEYS_SEEN)) {
      progress.options |= OPT_KEYS_SEEN;
      app_save_progress();
    }
  }
  return true;
}

/* ------------------------------------------------------------ drawing */

static void draw_type_icon(int type, int cx, int cy, int box, int rot, unsigned alpha) {
  const ObjDef *d = &objdefs[type];
  int w = 0, h = 0;
  for (int k = 0; k < d->nparts; k++) {
    const Sprite *s = &sprites[OBJ_PART(d, k)->sprite];
    if (OBJ_PART(d, k)->ctype >= CT_GLOW) continue;
    if (s->w > w) w = s->w;
    if (s->h > h) h = s->h;
  }
  float sc = box / (float)(w > h ? w : h);
  if (sc > 1) sc = 1;
  for (int k = d->nparts - 1; k >= 0; k--) {
    const ObjPart *pt = OBJ_PART(d, k);
    if (pt->ctype >= CT_GLOW) continue;
    color_t tint = pt->ctype == CT_BLACK ? 0 : pt->ctype == CT_P1ADD ? app_p1() : pt->ctype == CT_P2ADD ? app_p2() : 0xffff;
    int mode = pt->ctype == CT_P1ADD || pt->ctype == CT_P2ADD ? BLEND_ADD : BLEND_NORMAL;
    float ox = pt->dx4 / 4.f * (2.f / 3) * sc, oy = -pt->dy4 / 4.f * (2.f / 3) * sc;
    gfx_sprite_ex(pt->sprite, (int)((cx + ox) * 16), (int)((cy + oy) * 16), rot * 90 * 16, (int)(sc * 256), 0, tint, alpha, mode);
  }
}

static void draw_cursor(void) {
  const Game *g = &app.g;
  float sx = wx_to_sx(g, app.cur_x * 30.0f), sy = wy_to_sy(g, app.cur_y * 30.0f + 30);
  int x = (int)floorf(sx + .5f), y = (int)floorf(sy + .5f);
  if (app.edit_mode == MODE_BUILD) {
    int ox, oy, t = brush_type();
    cell_offset(t, app.rotate, &ox, &oy);
    draw_type_icon(t, x + 10 + ox * 2 / 3, y + 10 - oy * 2 / 3, 40, app.rotate, 150);
  }
  color_t c = app.edit_mode == MODE_BUILD ? 0xffff : app.edit_mode == MODE_EDIT ? rgb(0, 255, 0) : rgb(255, 60, 60);
  unsigned a = (unsigned)(170 + 80 * sinf(app.t * 6));
  gfx_blend(x, y, 21, 2, c, a);
  gfx_blend(x, y + 19, 21, 2, c, a);
  gfx_blend(x, y + 2, 2, 17, c, a);
  gfx_blend(x + 19, y + 2, 2, 17, c, a);
}

static void panel(void) {
  const int top = 178;
  gfx_blend(0, top, GFX_W, GFX_H - top, 0, 190);
  gfx_fill(0, top, GFX_W, 1, rgb(90, 90, 90));
  static const char *const modes[3] = {T("BUILD"), T("EDIT"), T("DELETE")};
  for (int i = 0; i < 3; i++) ui_text_button(30, top + 12 + i * 20, 50, 17, modes[i], app.edit_mode == i ? BTN_GREEN : BTN_GRAY, 1);
  int page = app.edit_page;
  for (int k = 0; k < PAGE_ITEMS; k++) {
    int idx = page * PAGE_ITEMS + k;
    if (idx >= EDITOR_TYPES) break;
    int bx = 70 + (k % 6) * 32, by = top + 5 + (k / 6) * 29;
    bool on = idx == app.brush;
    gfx_round_rect(bx, by, 28, 26, 4, 0, 256);
    gfx_round_rect(bx + 1, by + 1, 26, 24, 3, on ? rgb(90, 210, 60) : rgb(150, 150, 150), 256);
    gfx_round_rect(bx + 2, by + 2, 24, 22, 3, on ? rgb(60, 160, 40) : rgb(105, 105, 105), 256);
    draw_type_icon(editor_types[idx], bx + 14, by + 13, 20, 0, 256);
  }
  ui_nav_dots(286, top + 12, PAGES, page);
  char buf[16];
  int n = gfx_format_uint(buf, count);
  buf[n++] = '/';
  gfx_format_uint(buf + n, CUSTOM_MAX);
  gfx_text_center(FONT_SMALL, 286, top + 34, buf, 0xffff, 0xffff, 220);
  static const char *const rots[4] = {T("ROT 0"), T("ROT 90"), T("ROT 180"), T("ROT 270")};
  gfx_text_center(FONT_SMALL, 286, top + 50, rots[app.rotate], 0xffff, 0xffff, 220);
}

static void settings_draw(void) {
  ui_dim(110);
  ui_window_brown(40, 44, 240, 150);
  ui_title(FONT_BIG, 160, 70, T("LEVEL SETTINGS"));
  const LevelDef *d = &level_defs[meta.theme % THEME_COUNT];
  const char *labels[3] = {T("COLORS"), T("START MODE"), T("CLEAR LEVEL")};
  for (int i = 0; i < 3; i++) {
    int y = 98 + i * 28;
    color_t t = settings_sel == i ? GOLD_TOP : 0xffff, b = settings_sel == i ? GOLD_BOTTOM : 0xffff;
    gfx_text(FONT_SMALL, 60, y + 4, labels[i], t, b, 256);
  }
  gfx_round_rect(176, 90, 18, 16, 3, 0, 256);
  gfx_fill(177, 91, 16, 14, rgb(d->bg[0], d->bg[1], d->bg[2]));
  gfx_text(FONT_SMALL, 200, 102, theme_names[meta.theme % THEME_COUNT], 0xffff, 0xffff, 256);
  gfx_text(FONT_SMALL, 200, 130, meta.flags & 1 ? T("SHIP") : T("CUBE"), 0xffff, 0xffff, 256);
  ui_text_button(214, 154, 90, 20, clear_armed ? T("SURE?") : T("CLEAR"), clear_armed ? BTN_PINK : BTN_GRAY, settings_sel == 2 ? 1.08f : 1);
  gfx_text_center(FONT_SMALL, 160, 186, T("LEFT RIGHT: CHANGE    BACK: CLOSE"), rgb(255, 230, 190), rgb(255, 230, 190), 220);
}

void editor_draw(void) {
  scene_draw();
  draw_cursor();
  panel();
  const int y0 = 12 - NP_TEXT_EXTRA, y1 = 24 + 2 * NP_TEXT_EXTRA; /* (further apart for Chinese letters) */
  gfx_text(FONT_SMALL, 6, y0, slot_names[app.slot], 0xffff, 0xffff, 230);
  if (app.dirty) gfx_text(FONT_SMALL, 6, y1, T("UNSAVED"), rgb(255, 220, 120), rgb(255, 180, 60), 230);
  gfx_text_right(FONT_SMALL, 314, y0, T("EXE: PLAYTEST"), 0xffff, 0xffff, 200);
  gfx_text_right(FONT_SMALL, 314, y1, T("LN: SETTINGS"), 0xffff, 0xffff, 200);
  if (settings_open) settings_draw();
  if (keys_open) keys_draw();
}

/* ------------------------------------------------------------ my levels */

void creator_tick(void) {
  if (keys_tick()) return;
  if (app_hit(K_BACK)) { app_go(SCR_MENU); return; }
  /* row -1: the Editor keys button above the levels */
  if (app_hit(K_UP)) app.row = app.row < 0 ? CUSTOM_SLOTS - 1 : app.row - 1;
  if (app_hit(K_DOWN)) app.row = app.row == CUSTOM_SLOTS - 1 ? -1 : app.row + 1;
  if (app.row < 0) {
    if (app_accept()) keys_open = true;
    return;
  }
  if (app_hit(K_LEFT | K_RIGHT)) app.sel ^= 1;
  if (app_accept()) {
    app.slot = app.row;
    if (app.sel == 0) {
      app.testing = false;
      app_go(SCR_EDITOR);
    } else if (!custom_meta[app.row].exists || !custom_meta[app.row].count) {
      app_notice(T("THIS LEVEL IS EMPTY"));
    } else {
      app.level = LEVEL_COUNT + app.row;
      app.practice = false;
      app.testing = false;
      app_go(SCR_PLAY);
    }
  }
}

void creator_draw(void) {
  ui_gradient_bg(rgb(0, 102, 255));
  gfx_sprite(SPR_ARROW_BACK, 22, 26, 0, 0xffff, 256, BLEND_NORMAL);
  /* list frame: lime sides and the header bar */
  gfx_round_rect(26, 46, 268, 172, 4, 0, 256);
  gfx_vgrad(28, 48, 6, 168, rgb(190, 242, 72), rgb(80, 150, 30));
  gfx_vgrad(286, 48, 6, 168, rgb(190, 242, 72), rgb(80, 150, 30));
  ui_top_bar(40, false);
  ui_title(FONT_BIG, 160, 38, T("MY LEVELS"));
  for (int i = 0; i < CUSTOM_SLOTS; i++) {
    int y = 52 + i * 54;
    color_t c = i & 1 ? rgb(161, 88, 44) : rgb(194, 114, 62);
    gfx_fill(34, y, 252, 54, c);
    gfx_fill(34, y + 53, 252, 1, rgb(120, 60, 30));
    const CustomMeta *m = &custom_meta[i];
    gfx_text(FONT_BIG, 44, y + 24, slot_names[i], 0xffff, 0xffff, 256);
    char buf[24];
    int n = gfx_format_uint(buf, m->exists ? m->count : 0);
    memcpy(buf + n, T(" OBJECTS"), strlen(T(" OBJECTS")) + 1);
    gfx_text(FONT_SMALL, 46, y + 42, buf, rgb(255, 230, 190), rgb(255, 230, 190), 256);
    const LevelStat *s = &progress.lv[LEVEL_COUNT + i];
    if (m->exists && m->count) {
      n = gfx_format_uint(buf, s->normal);
      memcpy(buf + n, "%", 2);
      gfx_text(FONT_SMALL, 140, y + 42, buf, 0xffff, 0xffff, 220);
    }
    bool row = app.row == i;
    ui_text_button(216, y + 27, 52, 22, T("EDIT"), BTN_GREEN, row && app.sel == 0 ? 1.12f : 1);
    ui_text_button(266, y + 27, 42, 22, T("PLAY"), BTN_CYAN, row && app.sel == 1 ? 1.12f : 1);
  }
  gfx_round_rect(26, 212, 268, 8, 3, 0, 256);
  gfx_vgrad(28, 213, 264, 5, rgb(190, 242, 72), rgb(80, 150, 30));
  gfx_text_center(FONT_SMALL, 160, 234, T("UP DOWN: LEVEL    LEFT RIGHT: EDIT / PLAY"), 0xffff, 0xffff, 200);
  ui_text_button(262, 26, 70, 20, T("KEYS"), app.row < 0 ? BTN_GREEN : BTN_GRAY, app.row < 0 ? 1.12f : 1);
  if (keys_open) keys_draw();
}
