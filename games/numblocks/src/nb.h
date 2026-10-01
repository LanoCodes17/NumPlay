/* NumBlocks: Minecraft 1.8.8 for the NumWorks calculator.
 *
 * The world around the player lives in a block cache (vc) of VCX x VCY x VCZ
 * blocks that follows the player: whatever leaves it is generated again from
 * the seed when it comes back, with the player's changes (the edit log, edits.c)
 * applied on top. Light is kept per block too (sky light, 4 bits).
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
  K_ANY = 1u << 31
};
uint32_t plat_keys(void);
uint32_t plat_millis(void);
void plat_sleep(uint32_t ms);
void plat_push(int x, int y, int w, int h, const uint16_t *px);
bool plat_save(const char *name, const void *data, uint32_t len);
const uint8_t *plat_load(const char *name, uint32_t *len);
void plat_begin(void);
int plat_end(void);

#define SCREEN_W 320
#define SCREEN_H 240

/* ---------------------------------------------------------------- the generator (gen.c) */
void gen_init(int64_t seed);
void gen_slab(int cx, int cz, int y0, int h, uint8_t *out);   /* out[(y - y0) * 256 + z * 16 + x] */
int gen_biome(int x, int z);
int gen_top(int x, int z);
void gen_spawn(int *x, int *y, int *z);

/* ---------------------------------------------------------------- the world (world.c) */
#define VCX 40
#define VCZ 40
#define VCY 32
#define WORLD_H 128
extern uint8_t vc[VCY * VCZ * VCX];      /* blocks: index (y * VCZ + z) * VCX + x */
extern uint8_t vlight[VCY * VCZ * VCX / 2];   /* sky light, 4 bits */
extern uint8_t vbiome[VCZ * VCX];        /* biome of each column */
extern int vc_x0, vc_y0, vc_z0;          /* world position of vc[0] */
#define VC_I(x, y, z) (((y) * VCZ + (z)) * VCX + (x))
/* 4 x 4 x 4 regions of the cache: non-zero if any block in it is not air (rays skip empty ones) */
#define MCX (VCX / 4)
#define MCY (VCY / 4)
#define MCZ (VCZ / 4)
extern uint8_t vmac[MCY * MCZ * MCX];
#define MC_I(x, y, z) ((((y) >> 2) * MCZ + ((z) >> 2)) * MCX + ((x) >> 2))
static inline int light_at(int i) { return (vlight[i >> 1] >> ((i & 1) * 4)) & 15; }

void world_new(int64_t seed);
void world_follow(float x, float y, float z);   /* keeps the cache around (x, y, z) */
int world_get(int x, int y, int z);            /* B_AIR outside the cache, B_BEDROCK below 0 */
void world_set(int x, int y, int z, int b);     /* a player's change: kept in the edit log */
bool world_loaded(int x, int y, int z);

/* ---------------------------------------------------------------- the player (player.c) */
typedef struct {
  float x, y, z;          /* feet */
  float vx, vy, vz;       /* blocks a tick */
  float yaw, pitch;       /* degrees */
  bool on_ground, in_water, sneaking, sprinting;
  int hit_x, hit_y, hit_z, hit_face;   /* the block looked at (hit_face -1: none) */
  float breaking;         /* 0..1 of the block being mined */
  int hotbar[9], slot;
} Player;
extern Player pl;
void player_spawn(void);
void player_tick(uint32_t keys, uint32_t pressed);   /* 20 a second */

/* ---------------------------------------------------------------- drawing (render.c, hud.c) */
#define RW 160
#define RH 120
typedef struct { float x, y, z, yaw, pitch; } Camera;
void render_frame(const Camera *c, uint32_t time_of_day);
/* the HUD and screens draw over each strip of the screen before it is sent */
void hud_strip(uint16_t *buf, int y0, int rows);
#endif
