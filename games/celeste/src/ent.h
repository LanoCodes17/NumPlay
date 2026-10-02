/* Entities, as in Monocle: a position, a collider, a depth; actors and
 * solids that move a whole pixel at a time with sub-pixel remainders. */
#ifndef ENT_H
#define ENT_H
#include "celeste.h"

typedef struct Ent Ent;
typedef struct Player Player;

enum {   /* kinds (Monocle's Tracker types) */
  KIND_SOLID = 1, KIND_JUMPTHRU = 2, KIND_ACTOR = 4, KIND_TRIGGER = 8, KIND_PLAYER = 16,
  KIND_PCOLLIDE = 32,      /* has a PlayerCollider */
  KIND_STATICMOVER = 64,   /* sticks to a platform */
  KIND_SPIKES = 128, KIND_WATER = 256, KIND_DREAMBLOCK = 512, KIND_HOLDABLE = 1024, KIND_SPINNER = 2048,
  KIND_TALK = 4096, KIND_BLOCKFIELD = 8192, KIND_KILLBOX = 16384, KIND_CLIMBBLOCKER = 32768
};
enum { COL_NONE, COL_BOX, COL_CIRCLE, COL_GRID, COL_LIST };
enum { TAG_PERSISTENT = 1, TAG_GLOBAL = 2, TAG_HUD = 4, TAG_TRANSITION_UPDATE = 8, TAG_FROZEN_UPDATE = 16, TAG_PAUSE_UPDATE = 32 };
enum { DASH_NORMAL, DASH_REBOUND, DASH_BOUNCE, DASH_IGNORE };

/* depths (Celeste.Depths) */
enum {
  D_BGTERRAIN = 10000, D_BGMIRRORS = 11500, D_BGDECALS = 9000, D_BGPARTICLES = 8000, D_SOLIDSBELOW = 5000,
  D_BELOW = 2000, D_NPCS = 1000, D_THEOCRYSTAL = 100, D_PLAYER = 0, D_DUST = -50, D_PICKUPS = -100,
  D_PARTICLES = -8000, D_ABOVE = -8500, D_SOLIDS = -9000, D_FGTERRAIN = -10000, D_FGDECALS = -10500,
  D_DREAMBLOCKS = -11000, D_CRYSTALSPINNERS = -11500, D_PLAYERDREAMDASHING = -12000, D_ENEMY = -12500,
  D_FAKEWALLS = -13000, D_FGPARTICLES = -50000, D_TOP = -1000000, D_FORMATIONSEQUENCES = -2000000
};

typedef struct { V2 dir, moved, target; Ent *hit, *pusher; } Collision;
typedef void (*CollideFn)(Ent *self, Collision *c);

typedef struct {
  const char *name;
  uint16_t size;          /* bytes of state (ST) */
  void (*update)(Ent *e);
  void (*render)(Ent *e);
  void (*awake)(Ent *e);
  void (*removed)(Ent *e);
  /* platforms */
  int (*on_dash_collide)(Ent *e, Player *p, V2 dir);   /* DASH_* */
  void (*on_collide)(Ent *e, V2 dir);
  void (*on_shake)(Ent *e, V2 amount);
  void (*on_staticmover_trigger)(Ent *e, Ent *mover);
  /* PlayerCollider */
  void (*on_player)(Ent *e, Player *p);
  /* StaticMover */
  void (*sm_move)(Ent *e, V2 amount);
  void (*sm_shake)(Ent *e, V2 amount);
  void (*sm_enable)(Ent *e, bool on);
  void (*sm_destroy)(Ent *e);
  bool (*sm_riding)(Ent *e, Ent *platform);           /* SolidChecker / JumpThruChecker */
  void (*sm_attach)(Ent *e, Ent *platform);
  /* actors */
  void (*on_squish)(Ent *e, Collision *c);
  bool (*is_riding_solid)(Ent *e, Ent *s);
  bool (*is_riding_jumpthru)(Ent *e, Ent *j);
  /* triggers */
  void (*on_enter)(Ent *e, Player *p);
  void (*on_stay)(Ent *e, Player *p);
  void (*on_leave)(Ent *e, Player *p);
  /* Follower: the leader got it / let it go; DashListener */
  void (*on_gain_leader)(Ent *e);
  void (*on_lose_leader)(Ent *e);
  void (*on_dash)(Ent *e, V2 dir);
  /* custom collision for entities made of many parts (spinners, clutter): e vs a rect */
  bool (*collide_rect)(const Ent *e, float l, float t, float r, float b);
  /* tracker-like membership */
  uint16_t kind;
} EntClass;

/* An entity: about 64 bytes here, plus its type's own state (EntClass.size bytes)
 * in a shared arena, reached with ST(e, Type). */
struct Ent {
  const EntClass *cls;
  float x, y;
  float remx, remy;      /* movementCounter */
  V2 lift;               /* platforms: LiftSpeed this frame; actors: current lift speed */
  int32_t depth;
  float cx, cy, cw, ch;  /* collider: box left, top, width, height; circle: cx, cy, radius in cw */
  uint16_t kind;
  uint16_t eid;          /* id in the map (EntityID with the room) */
  uint16_t order;        /* insertion order, for stable depth sorting */
  uint16_t data;         /* state: offset / 4 in the arena */
  uint16_t active : 1, visible : 1, collidable : 1, triggered : 1, safe : 1, treat_naive : 1, ignore_jumpthrus : 1,
      allow_pushing : 1, shaking : 1, awoken : 1, dead : 2, ctype : 3;
  uint8_t tags, room;
  int8_t shakex, shakey; /* Platform.Shake */
  uint16_t plat;         /* static movers: the platform carrying them (g_ents index + 1, 0: none) */
};
#define MAX_ENTS 256
_Static_assert(sizeof(struct Ent) == (sizeof(void *) == 4 ? 64 : 72), "Ent: 64 bytes on the calculator");
#define ENT_ARENA (9 * 1024)
extern uint8_t g_ent_arena[];
/* the state of e, a T (its class's size must be sizeof(T)) */
#define ST(e, T) ((T *)(void *)(g_ent_arena + (uint32_t)(e)->data * 4))
/* actors keep their lift speed grace and squish callback here, at the start of their state */
typedef struct { V2 last_lift; float lift_timer; } ActorExt;
extern Ent g_ents[MAX_ENTS];
static inline Ent *ent_platform(const Ent *e) { return e->plat ? &g_ents[e->plat - 1] : NULL; }
static inline void ent_set_platform(Ent *e, const Ent *p) { e->plat = p ? (uint16_t)(p - g_ents + 1) : 0; }
extern int g_nents;

Ent *ent_new(const EntClass *cls, float x, float y);
extern uint16_t g_new_eid;   /* the id in the map new entities get (EntityID), while a room loads */
void ent_remove(Ent *e);
void ent_box(Ent *e, float w, float h, float x, float y);
void ent_circle(Ent *e, float r, float x, float y);
void ents_update(void);
void ents_render_between(int depth_hi, int depth_lo);
void ents_render_hud(void);
void ents_flush_removed(void);
void ents_awake_new(void);
void ents_clear(bool keep_persistent);
void ents_remove_room(int room);
void ents_mark_unsorted(void);

/* the collider's bounds (a circle's: its center, radius cw) */
static inline float e_left(const Ent *e) { return e->x + e->cx - (e->ctype == COL_CIRCLE ? e->cw : 0); }
static inline float e_top(const Ent *e) { return e->y + e->cy - (e->ctype == COL_CIRCLE ? e->cw : 0); }
static inline float e_right(const Ent *e) { return e->x + e->cx + e->cw; }
static inline float e_bottom(const Ent *e) { return e->y + e->cy + (e->ctype == COL_CIRCLE ? e->cw : e->ch); }
static inline float e_cxm(const Ent *e) { return e->x + e->cx + (e->ctype == COL_CIRCLE ? 0 : e->cw / 2); }
static inline float e_cym(const Ent *e) { return e->y + e->cy + (e->ctype == COL_CIRCLE ? 0 : e->ch / 2); }
static inline V2 e_center(const Ent *e) { return v2(e_cxm(e), e_cym(e)); }

/* collision of e's collider moved to (x, y) */
bool collide_ent_at(const Ent *e, float x, float y, const Ent *other);
bool collide_rect(const Ent *e, float l, float t, float r, float b);   /* e's collider vs a rect */
Ent *collide_first_solid(const Ent *e, float x, float y);
bool collide_solid(const Ent *e, float x, float y);
Ent *collide_first_jumpthru_outside(const Ent *e, float x, float y);   /* CollideFirstOutside<JumpThru> */
bool collide_jumpthru(const Ent *e, float x, float y);                 /* CollideCheck<JumpThru> */
Ent *collide_first_kind(const Ent *e, float x, float y, uint16_t kind);
bool rect_solid(float l, float t, float r, float b);                     /* Scene.CollideCheck<Solid>(rect) */
bool point_solid(float x, float y);

/* Actor */
bool actor_move_h(Ent *e, float amount, CollideFn cb, Ent *pusher);
bool actor_move_v(Ent *e, float amount, CollideFn cb, Ent *pusher);
bool actor_move_h_exact(Ent *e, int amount, CollideFn cb, Ent *pusher);
bool actor_move_v_exact(Ent *e, int amount, CollideFn cb, Ent *pusher);
void actor_move_to_x(Ent *e, float x, CollideFn cb);
void actor_move_to_y(Ent *e, float y, CollideFn cb);
void actor_move_towards_x(Ent *e, float x, float amount, CollideFn cb);
void actor_move_towards_y(Ent *e, float y, float amount, CollideFn cb);
void actor_naive_move(Ent *e, V2 amount);
bool actor_on_ground(const Ent *e, int down);
bool actor_on_ground_at(Ent *e, float x, float y, int down);
bool actor_try_squish_wiggle(Ent *e, Collision *c);
void actor_set_lift(Ent *e, V2 v);
V2 actor_lift(const Ent *e);
void actor_update_lift(Ent *e);
bool actor_is_riding_solid(Ent *e, Ent *s);
bool actor_is_riding_jumpthru(Ent *e, Ent *j);

/* Platform (Solid / JumpThru) */
void plat_move_h(Ent *e, float amount);
void plat_move_v(Ent *e, float amount);
void plat_move_h_lift(Ent *e, float amount, float lift);
void plat_move_v_lift(Ent *e, float amount, float lift);
void plat_move_h_exact(Ent *e, int amount);
void plat_move_v_exact(Ent *e, int amount);
void plat_move_to(Ent *e, float x, float y);
void plat_move_to_x(Ent *e, float x);
void plat_move_to_y(Ent *e, float y);
void plat_move_towards_x(Ent *e, float x, float amount);
void plat_move_towards_y(Ent *e, float y, float amount);
bool plat_move_v_collide_solids(Ent *e, float amount, bool thru_dash_blocks);
bool plat_move_h_collide_solids(Ent *e, float amount, bool thru_dash_blocks);
void plat_update(Ent *e);                 /* lift speed reset and shaking */
void plat_start_shaking(Ent *e, float time);
void plat_stop_shaking(Ent *e);
void plat_static_movers_attach(Ent *e);
void plat_static_movers_move(Ent *e, V2 amount);
void plat_static_movers_shake(Ent *e, V2 amount);
void plat_static_movers_enable(Ent *e, bool on);
void plat_static_movers_destroy(Ent *e);
void plat_static_movers_trigger(Ent *e);
bool solid_has_player_rider(Ent *e);
bool solid_has_player_on_top(Ent *e);
bool solid_has_player_climbing(Ent *e);
bool solid_has_rider(Ent *e);
bool jumpthru_has_player_rider(Ent *e);
#define e_shake(e) v2((e)->shakex, (e)->shakey)
static inline V2 plat_exact(const Ent *e) { return v2(e->x + e->remx, e->y + e->remy); }

/* the level's tiles act as one solid (SolidTiles) */
extern Ent *g_solidtiles;
bool tiles_solid_rect(float l, float t, float r, float b);
#endif
