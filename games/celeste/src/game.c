#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("Os")   /* not drawn every frame: smaller over faster */
#endif
/* The game: the level, the death wipe, and the frame loop's steps. */
#include "level.h"
#include "player.h"
#include "fx.h"
#include "wipe.h"

uint32_t g_frame;
float g_dt = RAW_DT, g_time_rate = 1;
void game_play(int chapter, int checkpoint);
bool g_running = true;

bool g_in_level;   /* a level is on (the menus can be over it: pause) */

/* start chapter `chapter` (in data.bin) at a checkpoint (-1: its start), or carry on the saved session */
void game_play(int chapter, int checkpoint) {
  bool resume = chapter < 0;
  if (!resume) session_start(chapter, checkpoint);
  g_save.has_session = 1;
  g_in_level = true;
  g_menu = 0;
  hud_reset();
  level_start(resume ? INTRO_RESPAWN : session_intro(true));
}

/* AreaComplete after the Summit's A-side: the session goes on in credits-summit, the credits on (ch7.c) */
void game_credits(void) {
  extern uint8_t g_credits_next;
  int r = chapter_find_room(g_session.chapter, "credits-summit");
  if (r < 0) return;
  g_session.level = (uint8_t)r, g_session.has_respawn = 0;
  g_credits_next = 1;
  g_save.has_session = 1;
  g_in_level = true;
  g_menu = 0;
  hud_reset();
  level_start(INTRO_NONE);
}

void game_init(void) {
  res_init();
  rnd_seed(&g_rnd, 0);
  save_load();
#ifdef PERF_ROOM   /* speed tests (tools/emu.py): straight into a room */
  session_start(PERF_CHAPTER, -1);
  g_session.level = (uint8_t)chapter_find_room(PERF_CHAPTER, PERF_ROOM);
  g_in_level = true;
  level_start(INTRO_NONE);
#ifdef PERF_X   /* and at a point of it */
  g_player.ent->x = g_level.room->x + PERF_X, g_player.ent->y = g_level.room->y + PERF_Y;
  g_level.cam = player_camera_target(&g_player);
#endif
  return;
#endif
  menu_open_title();   /* the key sheet first, the first time */
}

/* LevelExit: the save written, then the chapter's end, the map or the main menu, or the chapter again */
void game_level_exit(int mode) {
  int area = g_session.area;
  switch (mode) {
    case LEXIT_RESTART:   /* Session.Restart: from where it began */
      game_play(g_session.chapter, g_session.start_checkpoint);
      return;
    case LEXIT_SAVEQUIT: g_save.has_session = 1; break;
    default: g_save.has_session = 0; break;
  }
  g_save.last_area = (uint8_t)area, g_save.last_mode = g_session.mode;
  save_write();
  g_in_level = false;
  wipe_start(WIPE_FADE, true, NULL);
  if (mode == LEXIT_SAVEQUIT) menu_open_main();
  else menu_open_overworld(mode == LEXIT_COMPLETED && !g_level.in_credits, mode == LEXIT_COMPLETED && g_should_advance);
}

/* Home: the run is kept (Save & Quit) */
void game_save(void) {
  if (g_in_level) g_save.has_session = g_session.in_area;   /* (from the menus, the run Save & Quit kept stays) */
  save_write();
}

void game_frame(void) {
  g_dt = RAW_DT * g_time_rate;
  input_update(plat_keys());
  if (g_in.keys & K_HOME) {
    g_running = false;
    return;
  }
  if (g_menu) menu_update();
  if (g_in_level) level_update(), hud_update();
  else wipe_update();
  g_frame++;
}

void game_draw(void) {
  gfx_begin();
  gfx_clear(0);
  if (g_in_level) {
    /* Level.Render: the point that stays put is (focus * target - (160, 90)) / (target - 1) */
    float zt = g_level.zoom_target;
    V2 fp = zt != 1 ? v2((g_level.zoom_focus.x * zt - 160) / (zt - 1), (g_level.zoom_focus.y * zt - 90) / (zt - 1)) : v2(0, 0);
    gfx_zoom(g_level.zoom, fp.x, fp.y);
    gfx_camera(g_level.cam.x + g_level.shake_vec.x, g_level.cam.y + g_level.shake_vec.y);
    style_render(false);
    ents_render_between(1 << 30, -(1 << 30));
    style_render(true);
    if (g_level.flash > 0) {
      gfx_screen(true);
      gfx_rect(0, 0, VIEW_W, VIEW_H, g_level.flash_color, (uint8_t)(g_level.flash * g_level.flash_alpha));
      gfx_screen(false);
      Ent *p = level_player_ent();
      if (g_level.flash_draw_player && p && p->visible) p->cls->render(p);
    }
    ents_render_hud();
  } else
    gfx_zoom(1, 0, 0);
  menu_render();
  wipe_render();
  {   /* the room's code in the top bar (Options: for bug reports): the map's area and side, then the room */
    char code[24] = "";
    if (g_in_level && (g_save.options & OPT_ROOM)) {
      code[0] = (char)('0' + g_session.area), code[1] = (char)('A' + g_session.mode), code[2] = ' ';
      strncpy(code + 3, level_room_name(), sizeof code - 4);
    }
    gfx_top_label(code);
  }
  gfx_end();
}
