/* The level: a chapter's rooms, the camera, transitions between rooms,
 * deaths and respawns (Celeste's Level, LevelLoader and Session). */
#ifndef LEVEL_H
#define LEVEL_H
#include "celeste.h"
#include "ent.h"

#define MARGIN 2          /* tiles kept around each room (tools/pack.py) */
/* room blob header (tools/pack.py room_blob): u16 offsets of the parts, then where the background tiles
 * go in RAM and how far what follows the tiles moves there; everything before RB_ENTS stays in RAM */
enum { RB_FG, RB_BG, RB_EXTRAS, RB_DFG, RB_DBG, RB_SPIN, RB_NEED, RB_ENTS, RB_TRIGS, RB_END, RB_BG_RAM, RB_SHIFT,
       RB_HEADER };
#define SPIN_X0 128

/* chapter record (tools/pack.py): u16 name, u16 nrooms, s16 x0, s16 y0, u16 tw, u16 th, u8 nbg, u8 nfg,
 * u8 area, u8 mode, u8 inventory, u8 ncheckpoints, u8 nberries, u8 detected strawberries,
 * u16 berries, u16 checkpoints (offsets in the record), u16 chapter files, u16 0, u32 room offsets[nrooms],
 * u32 styleground offsets[nbg + nfg].
 * berry: u16 room, u16 entity id, u8 checkpoint, u8 flags (BF_); checkpoint: u16 room, u8 inventory
 * (255: the mode's), u8 flags (CPF_) */
#define CH_AREA 14
#define CH_MODE 15
#define CH_INVENTORY 16
#define CH_NCHECKPOINTS 17
#define CH_NBERRIES 18
#define CH_DETECTED 19
#define CH_BERRIES 20
#define CH_CHECKPOINTS 22
#define CH_FILES 24         /* u16: the chapter files (src/chN.c) whose entities it has, bit N */
#define CH_ROOMS 28
enum { BF_GOLDEN = 1, BF_MOON = 2, BF_WINGED = 4 };
enum { CPF_DREAMING = 1, CPF_COLD = 2, CPF_FEELINGDOWN = 4, CPF_BADELINE = 8 };
enum { INV_PROLOGUE, INV_DEFAULT, INV_OLDSITE, INV_CH6END, INV_THESUMMIT, INV_CORE, INV_FAREWELL };

/* a layer of tiles: rows of runs, each a byte (type index << bits | count - 1) */
typedef struct {
  const uint8_t *types, *data;
  const uint16_t *rows;
  uint8_t bits;
} TileLayer;

typedef struct {
  int index;              /* in the chapter, -1: none */
  const uint8_t *info;    /* the chapter's room record (flash) */
  int x, y, w, h;         /* bounds, pixels */
  int tx, ty, tw, th;     /* tile grid with its margin: tile coordinates */
  TileLayer layer[2];     /* fg, bg */
  uint8_t *decals_fg, *decals_bg;
  int nfg, nbg;
  uint8_t *obj;           /* objtiles (tiles that blocks take): u16 x, y, tile */
  int nobj;
  uint8_t *scen[2];       /* scenery tiles drawn over fg / bg: u16 x, y, tile */
  int nscen[2];
  const uint16_t *spin_first;   /* spinners not attached to solids: first index of each tile row */
  const uint16_t *spin;         /* cell x + SPIN_X0 | x % 8 << 10 | y % 8 << 13 */
  int spin_row0, spin_rows;     /* their rows (room tile rows; some are outside the room) */
  const uint8_t *spin_mask;     /* a nibble each: quarters inside solid tiles */
  const uint8_t *spin_dust;     /* dust rooms (DustStaticSpinner): u16 each, the DustGraphic's nodes (pack.py) */
  int nspin;
  uint8_t *spin_gone;     /* destroyed ones */
  const uint8_t *need;    /* the textures loaded with it: u16 n, ids */
  uint8_t *mem;           /* in the room pool */
  uint32_t used;
} Room;

typedef struct {
  int chapter;            /* index into data.bin's chapters */
  const uint8_t *ch;      /* chapter record */
  int nrooms;
  Room rooms[2];
  int room_slot;          /* the current room's slot */
  Room *room;
  /* camera */
  V2 cam;
  V2 cam_offset;          /* Level.CameraOffset (triggers) */
  int cam_lock;           /* CameraLockModes: 0 none, 1 BoostSequence, 2 FinalBoss, 3 FinalBossNoY */
  Ent *cam_locker;        /* the CameraLocker's entity, its MaxXOffset and MaxYOffset */
  float cam_lock_dx, cam_lock_dy;
  float cam_upward_max_y;
  V2 shake_vec, shake_dir;
  float flash;            /* Level.Flash: the screen in flash_color */
  uint16_t flash_color;
  uint8_t flash_alpha;    /* the color's own alpha (White * 0.5) */
  bool do_flash, flash_draw_player, no_retry;   /* no_retry: !Level.CanRetry */
  bool has_cassette_blocks;   /* Level.HasCassetteBlocks, CassetteBlockBeats (the room's) */
  uint8_t cassette_beats;
  float shake_timer;
  /* time */
  float time_active, raw_time_active, prev_time_active;
  float freeze;           /* Celeste.Freeze */
  /* transitions */
  bool transitioning;
  V2 tr_dir, tr_cam_from, tr_cam_to, tr_player_to;
  float tr_at;
  float tr_duration;
  /* respawn */
  V2 respawn;
  V2 start_position;      /* the room's checkpoint (LoadLevel's startPosition), if has_start_position */
  bool has_start_position;
  uint8_t last_intro;     /* LastIntroType: the INTRO_ the room was loaded with */
  bool dead_reload;
  bool in_cutscene;
  bool skipping_cutscene;
  Ent *cutscene;          /* the CutsceneEntity running (its OnEnd on skip) */
  uint8_t skip_step;
  float zoom, zoom_target; /* Level.Zoom, ZoomTarget */
  V2 zoom_focus;          /* ZoomFocusPoint, screen pixels */
  bool paused;
  bool frozen;            /* Level.Frozen: only FrozenUpdate entities */
  bool formation;         /* FormationBackdrop.Display */
  float formation_alpha, formation_fade;
  bool can_retry;
  bool in_space;
  int core_mode;
  V2 wind;
  int wind_pattern;
  float wind_sine_timer, wind_sine;   /* WindSineTimer, WindSine */
  float lighting, lighting_target;
  float bloom;
  bool timer_started, timer_stopped, completed, in_credits;
  bool pause_lock;        /* PauseLock */
  float ending_delay;     /* EndingCutsceneDelay: the area completes when it runs out */
  float ending_text_y;
} Level;
extern Level g_level;

/* ---------------------------------------------------------------- Session and SaveData (save.c) */
#define AREAS 11            /* prologue ... core, farewell */
enum { M_A, M_B, M_C };
enum { MS_COMPLETED = 1, MS_SINGLERUN = 2, MS_FULLCLEAR = 4, MS_HEART = 8 };
/* color grades (Session.ColorGrade) */
enum { CG_NONE, CG_FEELINGDOWN, CG_OLDSITE, CG_REFLECTION, CG_PANICATTACK, CG_TEMPLEVOID, CG_HOT, CG_COLD, CG_GOLDEN,
       CG_CREDITS, CG_COUNT };
/* AreaModeStats; times in frames (1/60 s, Celeste's fixed step) */
typedef struct {
  uint64_t berries;          /* Strawberries, by index in the chapter's list (CH_BERRIES) */
  uint32_t deaths, time, best_time, best_fc_time;
  uint16_t best_dashes, best_deaths;
  uint8_t flags;             /* MS_ */
  uint8_t checkpoints;       /* Checkpoints reached, by index */
  uint8_t pad[2];
} ModeStats;

/* Session: one go at a chapter side */
typedef struct {
  uint8_t chapter;           /* in data.bin */
  uint8_t area, mode;
  uint8_t level;             /* the room (Session.Level) */
  int8_t start_checkpoint;   /* StartCheckpoint, -1: the start */
  uint8_t inv_dashes, dreamdash, backpack, no_refills;   /* Inventory */
  uint8_t has_respawn;
  uint8_t started_from_beginning, first_level, in_area, cassette, heart, dreaming;
  uint8_t core_mode;         /* CoreModes: 0 none, 1 hot, 2 cold */
  uint8_t grabbed_golden, hit_checkpoint, beat_best_time, unlocked_cside, summit_gems;
  uint8_t color_grade;
  uint8_t nflags, ndnl, nkeys;
  int32_t rx, ry;            /* RespawnPoint */
  uint32_t time;
  uint32_t deaths, dashes, dashes_at_level_start, deaths_in_current_level;
  float lighting_alpha_add, bloom_base_add, dark_room_alpha;
  uint64_t berries;          /* Strawberries */
  uint32_t flags[32];        /* Flags, hashed */
  uint32_t dnl[32];          /* DoNotLoad: room << 16 | entity id */
  uint32_t keys[4];          /* Keys */
  uint8_t visited[32];       /* LevelFlags: a bit per room */
  struct { uint32_t key; int32_t value; } counters[3];
  ModeStats old_stats;       /* OldStats: the side's stats before this go */
  /* since 1.6.1 (a 1.6.0 save has zeros here) */
  uint8_t furthest_seen;     /* FurthestSeenLevel: the room + 1, 0 if none */
  uint8_t pad_[3];
  uint32_t dnl_more[16];     /* DoNotLoad past dnl's 32 */
  uint8_t torches_lit[56];   /* the Torches lit (the game's "torch_" flags): a bit each, from the room's RR_TORCHES * 2 */
} Session;

typedef struct {
  uint32_t magic;
  uint16_t version, size;
  ModeStats modes[AREAS][3];
  uint16_t cassettes;        /* AreaStats.Cassette, a bit per area */
  uint8_t unlocked_areas, last_area, last_mode, revealed_ch9, summit_gems, key_sheet_seen;
  uint8_t has_session, assists, options, flags;   /* options: 1 no screen shake, 2 no flashes, 12 speedrun clock;
                                                     flags (SaveData.Flags): 1 MetTheo, 2 TheoKnowsName */
  uint32_t total_deaths, total_golden, total_jumps, total_wall_jumps, total_dashes, time;
  uint8_t bind[4];           /* jump, dash, grab, talk: EADK keys */
  Session session;           /* CurrentSession, and the one being played */
  /* since 1.6.1 */
  uint8_t cheat_mode;        /* CheatMode */
  uint8_t binds_set;         /* 1: bind holds 1.6.1's keys or the player's own (1.6.0's defaults are replaced) */
  uint8_t pad_[2];
  uint32_t checksum;
} SaveData;
extern SaveData g_save;
#define g_session (g_save.session)

void save_load(void);
bool save_write(void);                 /* false: storage full */
ModeStats *save_mode(void);            /* the current side's */
bool save_check_berry(int index);      /* SaveData.CheckStrawberry */
void save_add_berry(int index, bool golden);
void save_add_death(void);
void save_add_time(uint32_t frames);
bool save_set_checkpoint(int index);
void save_register_heart(void);
int save_unlocked_modes(void);
int save_total_berries(void);         /* SaveData.TotalStrawberries */
bool save_flag(int bit);               /* SaveData.HasFlag: 0 MetTheo, 1 TheoKnowsName */
void save_set_flag(int bit);
void save_register_cassette(void);
void save_register_completion(void);
void session_start(int chapter, int checkpoint);   /* new Session(area, checkpoint) */
void session_restart(int room);                    /* Session.Restart(into room), -1 for its start */
int session_intro(bool just_started);              /* INTRO_ for LevelLoader */
int level_start_room(void);
void level_register_complete(void);
void level_flash(uint16_t color, bool draw_player_over);
void level_flash_a(uint16_t color, float alpha, bool draw_player_over);   /* Level.Flash(color * alpha) */
void level_camera_locker(Ent *e, int mode, float max_dx, float max_dy);   /* new CameraLocker(mode, dx, dy), added */
void level_load_room(int index, int intro);   /* LoadLevel(intro) into that room, at the frame's end */
/* the same, at the spawn nearest a point of the room (fx, fy: fractions of its width and height) */
void level_load_room_near(int index, int intro, float fx, float fy);
void level_complete_area(bool spotlight_wipe, bool skip_wipe);
enum { LEXIT_COMPLETED, LEXIT_SAVEQUIT, LEXIT_RESTART, LEXIT_GIVEUP, LEXIT_GOLDEN_RESTART };
void game_level_exit(int mode);   /* game.c: LevelExit */
void level_extra_textures(const uint16_t *ids, int n);   /* kept loaded with the rooms' (n = 0: none) */
int chapter_find_room(int chapter, const char *name);   /* -1 if none */
const char *chapter_room_name(int chapter, int i);      /* NULL past its last room */
bool level_visited(const char *room);                    /* Session.GetLevelFlag */
static inline float level_visual_wind(void) { return g_level.wind.x + g_level.wind_sine; }
int session_berry_index(int room, int eid);        /* in the chapter's list, -1 if none */
int chapter_index(int area, int mode);             /* in data.bin, -1 if not there */
const uint8_t *chapter_at(int c);                  /* its record */
int chapter_count(void);
void level_save_and_quit(void);                    /* LevelExit.Mode.SaveAndQuit */
void level_give_up(bool restart);                  /* GiveUp: the chapter again from its start, or the map */
/* the menus (menu.c) */
extern uint8_t g_menu;                             /* a menu is on (0: none) */
void menu_update(void);
void menu_render(void);
void menu_pause(void);
void menu_open_title(void);
void menu_open_overworld(bool completed, bool advance);
extern bool g_should_advance;   /* Session.ShouldAdvance, as the chapter was completed */
void menu_open_main(void);
void hud_update(void);   /* TotalStrawberriesDisplay */
void hud_reset(void);
extern bool g_in_level;
void game_play(int chapter, int checkpoint);   /* game.c: a chapter from a checkpoint (-1: start), or the saved session (chapter -1) */

void level_start(int intro);                  /* the session's chapter, at its level */
void level_update(void);
void level_reload(void);                      /* after death: back to the respawn point */
bool level_on_interval(float interval);
bool level_on_raw_interval(float interval);
void level_shake(float t);
void level_dir_shake(V2 dir, float t);
void level_freeze(float t);
Ent *level_player_ent(void);
Player *level_player(void);
float level_player_speed_x(void);
bool level_player_climbing(void);
int level_player_facing(void);
bool level_solid_cells(int cx0, int cy0, int cx1, int cy1);
bool level_grid_collide(const Ent *grid, float l, float t, float r, float b);
char level_tile_type(int layer, int cx, int cy);
bool level_is_dash_block(Ent *e);
void level_break_dash_block(Ent *e, V2 dir);
void level_enforce_bounds(Player *p);
V2 level_spawn_near(V2 at);
const char *level_room_name(void);
const char *level_room_name_of(int index);
bool level_in_bounds(V2 p, float pad);
void level_set_flag(const char *flag, bool on);
bool level_get_flag(const char *flag);
uint32_t level_entity_id(int room, int eid);
bool level_do_not_load(uint32_t id);
void level_set_do_not_load(uint32_t id);

/* tiles (tiles.c) */
typedef uint8_t TileQ;   /* a block's tile: its quad, ty * 8 + tx in the tile sheet (255: none) */
int autotile(int layer, char (*get)(int x, int y, void *ctx), void *ctx, int x, int y, uint8_t *quad, int variant);
void tiles_render_layer(int layer, int depth);
void tiles_invalidate(void);
int terrain_of(int layer, char c);
uint16_t terrain_tex(int t);
/* a box of tiles of one type, as Celeste's GenerateBox (falling blocks...) */
void tiles_box(char type, int w, int h, TileQ *out);
void tiles_overlay(char type, int x, int y, int w, int h, TileQ *out);   /* blended into the room (dash blocks) */
void tiles_draw(const TileQ *q, char type, int w, int h, float x, float y, uint16_t tint, uint8_t alpha);
TileQ *blocks_new_tiles(int n);     /* n tiles for an entity of the room being made (NULL: none left), until it goes */
void blocks_free_tiles(int slot);   /* the room in that slot is gone: its blocks' tiles (entities.c) */

/* stylegrounds (style.c) */
extern float g_snow_alpha;   /* the Snow stylegrounds' Alpha */
void hires_snow(float time, float alpha);   /* HiresSnow (menu.c), in screen coordinates */
void style_init(void);
void style_update(void);
void style_render(bool fg);

void level_tiles_entities(void);
#define RR_TORCHES 39   /* in a room record (Room.info): its torches' first bit in Session.torches, halved */

/* lights (light.c): call while rendering */
void light_add(float x, float y, uint32_t color, float alpha, float start_fade, float end_fade);
void bloom_add(float x, float y, float alpha, float radius);
void lights_begin(void);

/* crystal and dust spinners that do not move (spinner.c) */
void spinners_entities(void);
bool spinners_hit_rect(float l, float t, float r, float b);   /* the hitboxes of spinners overlap a rect */
void spinners_destroy_near(float x, float y, float r);        /* Reflection's falls and the boss */

/* decals */
void decals_render(bool fg);

/* entities (entities.c) */
void entities_load(Room *r, const uint8_t *blob, int room_slot);
void entities_triggers(Room *r, const uint8_t *blob);
#endif
