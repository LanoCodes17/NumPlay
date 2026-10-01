/* NumBlocks: Minecraft 1.8.8 for the NumWorks calculator.
 *
 * The world around the player lives in a block cache (vc) of VCX x VCY x VCZ
 * blocks that follows the player: whatever leaves it is generated again from
 * the seed when it comes back, with the player's changes (the edit log, edits.c)
 * applied on top. Light is kept per block too: sky light and block light.
 *
 * Minecraft's axes: +X east, +Y up, +Z south. Yaw 0 faces south (+Z) and
 * grows clockwise seen from above (90 faces west); pitch > 0 looks down. */
#ifndef NB_H
#define NB_H
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "blocks.h"
#include "data.h"

/* ---------------------------------------------------------------- the platform (plat_eadk.c / plat_host.c) */
enum {
  K_LEFT = 1 << 0, K_RIGHT = 1 << 1, K_UP = 1 << 2, K_DOWN = 1 << 3,      /* arrows: look */
  K_FWD = 1 << 4, K_BACKW = 1 << 5, K_STRAFE_L = 1 << 6, K_STRAFE_R = 1 << 7,   /* ln, cos, sin, tan */
  K_JUMP = 1 << 8, K_USE = 1 << 9, K_ATTACK = 1 << 10, K_INV = 1 << 11,   /* pi, OK, Back, var */
  K_HOME = 1 << 12, K_SNEAK = 1 << 13, K_PAUSE = 1 << 14, K_DROP = 1 << 15,
  K_SLOT1 = 1 << 16,   /* K_SLOT1 << n: digit n + 1 */
  K_SPRINT = 1 << 25,
  K_EXE = 1 << 26,       /* EXE alone (OK and EXE both give K_USE): right click in screens */
  K_SHIFT = 1 << 27,     /* shift: shift click in screens */
  K_BACK = 1 << 28,      /* Back alone: closes screens */
  K_OK = 1 << 29,        /* OK alone */
  K_ZERO = 1 << 30,      /* 0 (typing a seed) */
  K_MINUS = 1u << 31     /* - (typing a seed) */
};
uint32_t plat_keys(void);
uint32_t plat_millis(void);
void plat_sleep(uint32_t ms);
void plat_push(int x, int y, int w, int h, const uint16_t *px);
bool plat_save(const char *name, const void *data, uint32_t len);
const uint8_t *plat_load(const char *name, uint32_t *len);   /* unaligned, valid until storage changes */
void plat_remove_prefix(const char *prefix);
uint32_t plat_storage_free(void);
void plat_begin(void);
int plat_end(void);

#define SCREEN_W 320
#define SCREEN_H 240

/* ---------------------------------------------------------------- liquids */
static inline bool is_water(int b) {
  return b == B_WATER || b == B_FLOWING_WATER || (b >= B_FLOWING_WATER_2 && b <= B_FALLING_WATER);
}
static inline bool is_door_lower(int b) {
  return b == B_DOOR_OAK_LOWER || (b >= B_DOOR_OAK_LOWER_S && b <= B_DOOR_OAK_LOWER_N_OPEN);
}
static inline bool is_lava(int b) { return b == B_LAVA || b == B_FLOWING_LAVA || (b >= B_FLOWING_LAVA_4 && b <= B_FALLING_LAVA); }

/* ---------------------------------------------------------------- the generator (gen.c) */
void gen_init(int64_t seed);
void gen_slab(int cx, int cz, int y0, int h, uint8_t *out);   /* out[(y - y0) * 256 + z * 16 + x] */
int gen_biome(int x, int z);
int gen_top(int x, int z);
void gen_spawn(int *x, int *y, int *z);

/* ---------------------------------------------------------------- the world (world.c) */
#define VCX 40
#define VCZ 40
#define VCY 24
#define WORLD_H 128
extern uint8_t vc[VCY * VCZ * VCX];      /* blocks: index (y * VCZ + z) * VCX + x */
extern uint8_t vl[VCY * VCZ * VCX];      /* light: sky light (low 4 bits), block light (high 4 bits) */
extern uint8_t vbiome[VCZ * VCX];        /* biome of each column */
extern int vc_x0, vc_y0, vc_z0;          /* world position of vc[0] */
#define VC_I(x, y, z) (((y) * VCZ + (z)) * VCX + (x))
/* 4 x 4 x 4 regions of the cache: non-zero if any block in it is not air (rays skip empty ones) */
#define MCX (VCX / 4)
#define MCY (VCY / 4)
#define MCZ (VCZ / 4)
extern uint8_t vmac[MCY * MCZ * MCX];
#define MC_I(x, y, z) ((((y) >> 2) * MCZ + ((z) >> 2)) * MCX + ((x) >> 2))
static inline int light_at(int i) { return vl[i] & 15; }          /* sky light */
static inline int block_light_at(int i) { return vl[i] >> 4; }   /* torches, lava, glowstone... */

void world_new(int64_t seed, const char *name);   /* name: the save's record prefix */
void world_follow(float x, float y, float z);   /* keeps the cache around (x, y, z) */
int world_get(int x, int y, int z);            /* B_AIR outside the cache, B_BEDROCK below 0 */
void world_set(int x, int y, int z, int b);     /* a player's change: kept in the edit log */
bool world_loaded(int x, int y, int z);
bool world_pending(void);                       /* chunks still being made after a move */

/* ---------------------------------------------------------------- items (inv.c) */
typedef struct { uint16_t id, aux; } Stack;   /* aux: how many; for tools and armour, their damage (one) */
int item_max(int id);
int item_dur(int id);
int item_kind(int id);
int item_count(const Stack *s);
const char *item_label(int id);
int item_icon(int id);   /* its sprite, -1 if none */
int item_fuel(int id);
int smelt_of(int id);
int inv_add(Stack *inv, int n, int id, int count, int dmg);   /* what does not fit */
void stack_take(Stack *s, int k);
bool stack_wear(Stack *s, int k);   /* false: it broke */
int craft_find(const Stack *grid, int w);   /* a recipe, or -1 */
bool can_harvest(int b, int held);
float dig_speed(int b, int held);
int block_drops(int b, int held, Stack *out);   /* up to 2 */
int rnd(int n);
float rndf(void);

/* ---------------------------------------------------------------- collisions (phys.c) */
bool block_box(int x, int y, int z, float *b);
int block_boxes(int b, int x, int y, int z, int8_t (*o)[6]);   /* its shape, up to 5 boxes; 0: a whole cube */
float phys_clip(const float *box, int axis, float d);
bool phys_move(float *p, float *v, float w, float h);

/* ---------------------------------------------------------------- the player (player.c) */
typedef struct {
  float x, y, z;          /* feet */
  float vx, vy, vz;       /* blocks a tick */
  float yaw, pitch;       /* degrees */
  bool on_ground, in_water, sneaking, sprinting, flying, dead;
  int hit_x, hit_y, hit_z, hit_face;   /* the block looked at (hit_face -1: none) */
  float breaking;         /* 0..1 of the block being mined */
  int slot;               /* the hotbar slot held */
  Stack inv[36];          /* 0-8 the hotbar, then the three rows */
  Stack armor[4];         /* boots, leggings, chestplate, helmet */
  Stack craft[4];         /* the inventory's 2 x 2 crafting grid */
  Stack cursor;           /* held by the cursor in a screen */
  float health, sat, exhaustion, fall, last_damage;
  int food, food_timer, air, invuln, hurt_time, fire, using_ticks;
  int xp_level, xp_total;
  int sleep_timer;        /* ticks asleep in a bed (0: awake) */
  float walked, bob, prev_walked, prev_bob;   /* view bobbing (EntityPlayer.cameraYaw) */
  float xp;               /* 0..1 of the way to the next level */
  int spawn_x, spawn_y, spawn_z;
  uint8_t mode;           /* 0 survival, 1 creative */
} Player;
extern Player pl;
void player_spawn(void);
void player_tick(uint32_t keys, uint32_t pressed);   /* 20 a second */
static inline Stack *held(void) { return &pl.inv[pl.slot]; }
void player_hurt(float amount, int kind);   /* kind: DMG_* */
enum { DMG_GENERIC, DMG_FALL, DMG_DROWN, DMG_LAVA, DMG_FIRE, DMG_STARVE, DMG_WALL, DMG_VOID, DMG_MOB, DMG_ARROW,
       DMG_EXPLOSION, DMG_CACTUS };
void player_add_xp(int n);
void player_swing(void);   /* the arm swings (gui.c) */
void hand_tick(void);
void gui_message(const char *s);   /* a line at the bottom left, as Minecraft's chat shows */

/* ---------------------------------------------------------------- entities (entity.c) */
enum { E_NONE, E_ITEM, E_ZOMBIE, E_SKELETON, E_CREEPER, E_SPIDER, E_PIG, E_COW, E_SHEEP, E_CHICKEN, E_ARROW, E_TNT };
typedef struct {
  uint8_t type, on_ground, hurt, state;   /* hurt: ticks of red; state 255: dying (timer counts) */
  int16_t age, health, timer, delay;   /* delay: an item's pickup delay, a mob's attack wait, a creeper's fuse */
  float x, y, z, vx, vy, vz, yaw, pitch;
  float px, py, pz;       /* last tick's position */
  float limb, limb_amt;   /* the walk's swing */
  float gx, gz;           /* where it is going */
  int16_t panic, fire;
  int16_t love, growth;   /* animals: in love (ticks), a baby's growing up (negative: ticks left) */
  uint8_t invuln, sheared;
  Stack item;
} Entity;
#define N_ENT 24
extern Entity ents[N_ENT];
Entity *ent_new(int type, float x, float y, float z);
void ent_drop(int id, int count, int dmg, float x, float y, float z, bool thrown);
void ents_tick(void);
void mob_tick(Entity *e);
void mobs_spawn(void);
void explode(float x, float y, float z, float power);
void throw_item(int id, float speed, bool from_player);   /* arrows, snowballs, eggs */
bool mob_attack(const Entity *e);   /* the player hits this mob (with the held item) */
bool mob_use(Entity *e);            /* the player uses the held item on it (shears, bucket) */
Entity *entity_looked_at(float reach, float block_t);   /* the mob under the crosshair, nearer than block_t */
extern float tick_frac;

void player_respawn(void);

/* ---------------------------------------------------------------- liquids and growing (tick.c) */
void fluid_schedule(int x, int y, int z);   /* a liquid there may move */
void world_tick(void);                      /* 20 a second: liquids, random block ticks */
void neighbours_changed(int x, int y, int z);
void break_block_at(int x, int y, int z, bool drops);

/* ---------------------------------------------------------------- screens (gui.c) */
enum { GUI_NONE, GUI_INVENTORY, GUI_CRAFTING, GUI_FURNACE, GUI_CHEST, GUI_CREATIVE,
       GUI_PAUSE, GUI_DEATH, GUI_OPTIONS, GUI_TITLE, GUI_WORLDS, GUI_CREATE, GUI_CONFIRM, GUI_LOADING, GUI_CONTROLS };
extern int gui;            /* the screen open */
void gui_open(int screen, int x, int y, int z);
void gui_close(void);
void gui_input(uint32_t keys, uint32_t pressed);
void gui_tick(void);       /* 20 a second: furnaces */
void tiles_removed(int x, int y, int z);   /* a chest or furnace broken: its items fall out */
void gui_menu(int screen);  /* opens a menu screen */
enum { ACT_NONE, ACT_PLAY, ACT_NEW, ACT_QUIT_APP, ACT_SAVE_QUIT, ACT_RESPAWN, ACT_TITLE };
extern int menu_choice;    /* what a menu asks main.c to do (ACT_*), 0 if nothing */
extern int create_mode;
extern char seed_text[21];

/* ---------------------------------------------------------------- saves and options (save.c) */
typedef struct {
  uint8_t difficulty;   /* 0 peaceful, 1 easy, 2 normal, 3 hard */
  uint8_t fancy;        /* graphics: 0 fast (solid leaves), 1 fancy */
  uint8_t look;         /* look speed, % */
  uint8_t clouds, bobbing;
} Options;
extern Options opt;
extern int64_t world_seed;
bool save_world(void);
bool load_world(void);
bool world_exists(void);
int world_mode(void);
void new_world(int64_t seed, int mode);
void delete_world(void);
void load_options(void);
void save_options(void);

/* ---------------------------------------------------------------- drawing (render.c, hud.c) */
#define RW 160
#define RH 120
typedef struct { float x, y, z, yaw, pitch; } Camera;
void render_frame(const Camera *c, uint32_t time_of_day);
/* the HUD and screens draw over each strip of the screen before it is sent */
void hud_strip(uint16_t *buf, int y0, int rows);
#endif
