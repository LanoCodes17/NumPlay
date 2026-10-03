/* Hollow Knight for the NumWorks calculator.
 *
 * Inspired by Hollow Knight, not affiliated with Team Cherry.
 * Made by Mason Chen as part of NumPlay. */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "data.h"

#define SCREEN_W 320
#define SCREEN_H 240
/* the game keeps a 16:10 view (ForceCameraAspect): bars above and below */
#define VIEW_W 320
#define VIEW_H 200
#define VIEW_Y 20

/* the camera: perspective, 24 degrees high at 16:9, 38.1 units in front of the gameplay plane */
#define CAM_Z (-38.1f)
#define FOCAL 421.94f        /* (VIEW_H / 2) / tan(fov / 2), fov = 24 * (16/9) / 1.6 */

/* ---------------------------------------------------------------- platform */
enum {
  K_LEFT = 1, K_RIGHT = 2, K_UP = 4, K_DOWN = 8,
  K_JUMP = 16, K_ATTACK = 32, K_FOCUS = 64, K_SPELL = 128, K_DASH = 256,
  K_OK = 512, K_BACK = 1024, K_PAUSE = 2048, K_MAP = 4096, K_INV = 8192, K_HOME = 16384, K_ANY = 32768,
};
uint32_t plat_keys(void);
uint32_t plat_millis(void);
void plat_sleep(uint32_t ms);
void plat_push(int x, int y, int w, int h, const uint16_t *px);
void plat_fill(int x, int y, int w, int h, uint16_t c);
bool plat_save(const char *name, const void *data, uint32_t len);
const uint8_t *plat_load(const char *name, uint32_t *len);

/* ---------------------------------------------------------------- data.bin */
extern const uint8_t *hk_bin;
const uint8_t *section(int id);
uint32_t section_size(int id);
static inline uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static inline uint32_t rd32(const uint8_t *p) { return (uint32_t)(p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24); }
void lz_decode(const uint8_t *src, uint32_t comp, uint8_t *dst, uint32_t raw);
/* decoders' working memory (LZMA probabilities or the tile coder's), one at a time */
extern uint16_t g_scratch[4096];
float f16(uint16_t h);
const char *str_at(int id);   /* the shared string table (SEC_STR) */

/* ---------------------------------------------------------------- textures (tex.c) */
typedef struct {
  uint16_t w, h;
  uint8_t tw, th, fmt, flags;   /* flags: TEX_SMOOTH (drawn interpolated: the blurred background) */
  uint32_t pal_off, map_off, blk_first;
} TexRec;
#define TEX_SMOOTH 1
enum { FMT_PAL4 = 0, FMT_ALPHA2 = 1, FMT_SOFT = 2, FMT_SOFTA = 3 };
#define TILE 16
#define TILE_SHIFT(fmt) ((fmt) == FMT_ALPHA2 ? 5 : 4)   /* a tile's width, log 2 */
#define TEX_NONE 0xFFFF
const TexRec *tex_rec(uint16_t t);
/* the tile's texels (128 bytes: 16x16 of 4 bits, or 32x16 of 2 bits for FMT_ALPHA2), NULL for an empty one; decodes
 * on demand */
const uint8_t *tex_tile(uint16_t t, int tx, int ty);
/* the same, as a cache slot (-1: an empty tile): it holds until the cache runs out of room in a frame */
int tex_slot(uint16_t t, int tx, int ty);
const uint8_t *tex_slot_px(int s);
extern bool g_tex_overload;
/* a tile row's codes: 0 empty, 1 a tile, 3 an opaque tile (2 bits each) */
const uint8_t *tex_row_codes(uint16_t t, int ty);
void tex_frame(void);   /* a new frame: tiles used from now on are this frame's */
extern uint32_t g_tex_decodes, g_tex_misses, g_tex_calls;

/* ---------------------------------------------------------------- rooms (room.c) */
typedef struct {
  int16_t ax, ay;      /* the texture's top-left corner (1/64 unit) */
  int16_t z;           /* depth (1/128 unit) */
  uint16_t tex;
  uint8_t tint, flags;
  uint16_t a, b;       /* world units a texel along u and v (half floats, signed) */
  int16_t rot;         /* the u axis' angle (1/65536 turn) */
} Inst;
enum { BL_ALPHA = 0, BL_ADD, BL_SCREEN, BL_LINEARLIGHT, BL_OVERLAY, BL_MULTIPLY };
#define F_BLEND 7
#define F_LIT 8
#define F_ROT 16
#define F_SOLID 32
#define F_DYN 64
#define F_GRASS 128
typedef struct {
  uint16_t version, nsec, ntint, axis;
  float w, h, blur_z, saturation;
  float ambient[4];
  float hero_light[4];
  float lo, hi;        /* the camera's range along the axis the room is split on */
  uint16_t ninst;
  uint16_t nseg, ncol, gw, gh;   /* the ground (phys.c) */
  uint16_t nent;                 /* game objects (game.h: Ent) */
  uint32_t ground, ents;         /* (in SEC_RBLOB) */
  uint8_t lut[3][64];
} RoomHdr;
typedef struct {
  uint32_t off, len;   /* its instances, in draw order (read in place) */
  uint16_t count, pad;
  float start, end;    /* camera positions from which its instances can show */
} SectorRec;
#define MAX_LOADED 16
typedef struct {
  int id;
  RoomHdr *h;
  const uint8_t *tints;     /* RGBA */
  const SectorRec *secs;
  int nnear;
  uint8_t near[MAX_LOADED];   /* the sectors near the camera */
} Room;
extern Room g_room;
bool room_load(int id);
void room_near(float cx, float cy);   /* picks the sectors near the camera */
/* their instances, merged back to front: room_first, then room_next until it says no more; with each, its sorting
 * layer and order */
void room_first(void);
bool room_next(Inst *out, uint32_t *group);
#define SORT_KEY(layer, order) ((uint32_t)(layer) << 16 | (uint32_t)((order) + 32768))
const char *room_name(int id);

/* ---------------------------------------------------------------- sprites and animations (anim.c) */
typedef struct {
  uint16_t tex, pad;
  float lx, ty;   /* the texture's top-left corner, from the sprite's origin (units) */
  float tu, tv;   /* units a texel */
} SpriteRec;
const SpriteRec *sprite_rec(int id);
enum { ANIM_DONE = 1, ANIM_TRIGGER = 2 };
typedef struct {
  int16_t clip, frame, sprite;
  float time, fps;   /* time in frames */
  bool playing, paused;
  uint8_t events;    /* ANIM_*: what happened since the owner last cleared them */
} Anim;
void anim_play(Anim *a, int clip);   /* (a clip already playing goes on) */
void anim_play_from(Anim *a, int clip, float start);
void anim_play_from_frame(Anim *a, int clip, int frame);
bool anim_is_playing(const Anim *a, int clip);
void anim_update(Anim *a, float dt);
float clip_duration(int clip);
int clip_frames_count(int clip);
uint16_t to_f16(float f);
void sprite_inst(int sprite, float x, float y, float z, float sx, float sy, uint8_t tint, Inst *out);

/* ---------------------------------------------------------------- the ground (phys.c) */
/* a collider's flags (tools/coll.py) */
enum { CF_TERRAIN = 1, CF_STEEP = 2, CF_NONSLIDER = 4, CF_NOHARDLAND = 8, CF_ROOF = 16, CF_SOLID = 32 };
typedef struct {
  float dist, x, y, nx, ny;   /* where, and the ground's normal there */
  int col;
} PhysHit;
/* a ray from (x, y) along (dx, dy) (a unit vector) for dist: the nearest ground (of colliders with a flag in mask) */
bool phys_ray(float x, float y, float dx, float dy, float dist, uint8_t mask, PhysHit *hit);
uint8_t phys_col_flags(int col);
bool phys_tile_solid(int x, int y);

enum { EV_ENTER, EV_STAY, EV_EXIT };
typedef struct {
  uint8_t kind, col;
  float nx, ny;   /* the contact's normal, towards the body */
} BodyEvent;
#define MAX_CONTACTS 8
#define MAX_EVENTS 16
/* a rigid body with a box collider, as Unity moves it (Rigidbody2D, Box2D) */
typedef struct {
  float x, y, vx, vy;     /* its position (the transform's) and velocity */
  float ox, oy, hx, hy;   /* the box: its center from the position, half its size */
  float gravity_scale;
  uint8_t mask;           /* the colliders that stop it (CF_*) */
  int ncontacts;
  uint8_t ccol[MAX_CONTACTS];
  float cnx[MAX_CONTACTS], cny[MAX_CONTACTS];
  int nevents;            /* the last step's: OnCollisionEnter2D, Stay, Exit */
  BodyEvent events[MAX_EVENTS];
} Body;
void body_step(Body *b, float dt);

/* ---------------------------------------------------------------- drawing (gfx.c) */
extern float g_cam_x, g_cam_y;   /* the view's center (world units) */
void gfx_frame(void);            /* draws the room at the camera into the screen */
/* a sprite of the Knight, an enemy, an effect... for the next frame, among the room's by its sorting layer and order
 * (SORT_KEY), then depth */
bool gfx_actor(const Inst *in, uint32_t group);
extern uint32_t g_gfx_items, g_gfx_pixels;
