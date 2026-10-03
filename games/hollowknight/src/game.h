/* The game: the Knight, what the player has, the camera, the scene. */
#pragma once
#include "hk.h"

/* ---------------------------------------------------------------- PlayerData (what a save keeps) */
#define MAX_PERSIST 1024
typedef struct {
  uint8_t persist[MAX_PERSIST / 8];   /* the objects' states (PersistentBoolItem) */
  int8_t health, max_health, health_blue;
  int16_t mp, mp_reserve, max_mp, mp_reserve_max;
  int32_t geo;
  int8_t nail_damage;
  bool can_dash, has_spell, has_dash;
  uint8_t fireball_level;
  bool disable_pause;
} PlayerData;
extern PlayerData g_pd;

/* ---------------------------------------------------------------- the room's game objects (tools/ents.py) */
enum { ENT_CAMLOCK = 1, ENT_GATE, ENT_HAZARD_MARKER, ENT_RESPAWN, ENT_HAZARD_TRIGGER, ENT_MASK, ENT_DAMAGE, ENT_SHAPE,
       ENT_BOX, ENT_OBJ, ENT_PIECE };   /* (shape, box, piece: more of the record before) */
enum { OK_BREAKABLE = 1, OK_ENEMY };     /* objects (ENT_OBJ's flags) */
enum { HB_BOUNCE = 1, HB_RECOIL = 2 };    /* a hit box (ENT_BOX's flags): a down slash bounces off it, a slash recoils */
enum { HAZ_NONE, HAZ_NORMAL, HAZ_SPIKES, HAZ_ACID, HAZ_LAVA, HAZ_PIT };   /* DamageHero.hazardType */
enum { MK_SECRET = 1, MK_REMASK = 2, MK_SIMPLE = 4 };   /* masks: the unmasker, remasker and inverse FSMs */
enum { CL_PREVENT_UP = 1, CL_PREVENT_DOWN = 2, CL_MAX_PRIORITY = 4 };
enum { G_DOOR = 1, G_ENTER_RIGHT = 2, G_ENTER_LEFT = 4, G_DONT_WALK_OUT = 8, G_NON_HAZARD = 16 };
#define FACING_RIGHT 1
typedef struct {
  uint8_t type, flags;
  uint8_t group, group2;   /* its render groups */
  uint16_t a;              /* gate: the room it leads to; hazard trigger: its marker */
  uint16_t persist;        /* its bit in the save (NO_PERSIST: none) */
  float x0, y0, x1, y1;    /* its trigger (or its place) */
  float p0, p1, p2, p3;    /* camera lock: x min, y min, x max, y max (-1: none); gate: entry delay; mask: fade time,
                              pause, then the trigger kind (simple) or the alphas of Idle and Fade Out (remasker) */
  uint16_t s0, s1;         /* names (str_at): a gate's own and its entry point's */
} Ent;
#define NO_PERSIST 0xFFFF
void world_enter(void);                     /* the room's objects, as the room starts */
void world_tick(void);                      /* 1/50 s */
void world_trigger(int ent, int kind);      /* the Knight entering, staying in, leaving an object's trigger (EV_*) */
void world_hero_in_position(void);          /* the Knight done entering the room (WaitForHeroInPosition) */
void world_send_hit(int ent);               /* HIT to an object's FSM (from another's) */
void group_fade(int group, float alpha, float time);   /* iTweenFadeTo (linear) */
bool persist_get(int bit);   /* an object's state in the save (PersistentBoolItem) */
void persist_set(int bit);

/* the objects the Knight acts on (obj.c) */
void obj_enter(void);
void obj_tick(void);
void obj_draw(void);
void obj_swing_start(void);                                  /* a slash's hit shape on: what it hits, once */
int obj_nail(const float *pts, int npts, float direction);   /* -> HB_* of what it touches */
float rand_range(float lo, float hi);   /* Random.Range (floats) */

/* enemies, their corpses, geo (enemy.c) */
void enemies_enter(void);
void enemies_fixed(void);    /* FixedUpdate and their physics */
void enemies_update(void);   /* Update */
void enemies_draw(void);
void enemies_swing_start(void);
int enemies_nail(const float *pts, int npts, float direction, int damage);
int enemies_touch_hero(float x0, float y0, float x1, float y1, int *side);   /* -> its damage, 0: none */
int cardinal(float degrees);            /* DirectionUtils.GetCardinalDirection */
const Ent *room_ents(int *n);
#define MAX_ENTS 320

/* ---------------------------------------------------------------- the game */
typedef struct {
  bool paused;
  float time;
  float time_scale;           /* (Time.timeScale: freezes when the Knight is hit) */
  float step_acc;
  float hazard_x, hazard_y;   /* where a hazard sends the Knight back to */
  bool hazard_facing_right;
  /* GameManager's coroutines */
  uint8_t freeze_phase;
  bool freeze_hero;
  float freeze_t, freeze_from, freeze_down, freeze_wait, freeze_up, freeze_target;
  uint8_t hazard_phase;
  float hazard_t;
  /* the camera's fade to black */
  float fade, fade_from, fade_to, fade_t, fade_time, fade_delay;
} Game;
void game_freeze_moment(void);         /* the Knight hit: FreezeMoment with HeroController's DAMAGE_FREEZE_* */
void game_freeze(float down, float wait, float up, float target, bool hero);   /* GameManager.FreezeMoment */
#define FREEZE_MOMENT_1() game_freeze(0.04f, 0.03f, 0.04f, 0, false)              /* FreezeMoment(1): a kill */
void game_player_dead_from_hazard(void);
void game_fade(float to, float time, float delay);
extern Game g_game;

/* ---------------------------------------------------------------- the Knight (hero.c) */
enum { HS_INACTIVE, HS_IDLE, HS_RUNNING, HS_AIRBORNE, HS_WALL_SLIDING, HS_HARD_LANDING, HS_DASH_LANDING, HS_NO_INPUT,
       HS_PREVIOUS, HS_GROUNDED };   /* ActorStates (grounded and previous only as SetState's arguments) */
enum { TS_WAITING_TO_TRANSITION, TS_EXITING_SCENE, TS_WAITING_TO_ENTER_LEVEL, TS_ENTERING_SCENE, TS_DROPPING_DOWN };
enum { GATE_TOP, GATE_RIGHT, GATE_LEFT, GATE_BOTTOM, GATE_DOOR, GATE_UNKNOWN };
enum { SIDE_TOP, SIDE_LEFT, SIDE_RIGHT, SIDE_BOTTOM };
enum { ATK_NORMAL, ATK_UP, ATK_DOWN };
enum { SLASH_NORMAL, SLASH_ALT, SLASH_UP, SLASH_DOWN };
enum { DAMAGE_FULL, DAMAGE_HAZARD_ONLY, DAMAGE_NONE };
#define RECOIL_DURATION 0.2f

typedef struct {   /* HeroControllerStates */
  bool facing_right, on_ground, jumping, dashing, falling, attacking, up_attacking, down_attacking, alt_attack;
  bool looking_up, looking_down, looking_up_anim, looking_down_anim, bouncing, recoiling_left, recoiling_right;
  bool recoiling, recoil_frozen, dead, hazard_death, hazard_respawning, will_hard_land, casting, cast_recoiling;
  bool prevent_dash, dash_cooldown, in_walk_zone, touching_wall, touching_non_slider, was_on_ground, transitioning;
  bool invulnerable, focusing;
} CState;

typedef struct {
  Body body;
  uint32_t keys, prev_keys;
  uint8_t state, prev_state, anim_state, transition_state, gate_position, damage_mode;
  float move_input, vertical_input;
  CState cs;
  int jump_steps, jumped_steps, jump_queue_steps, dash_queue_steps, attack_queue_steps, recoil_steps;
  int landing_buffer_steps, ledge_buffer_steps, head_bump_steps;
  bool jump_queuing, dash_queuing, attack_queuing, recoil_large, hard_landed, air_dashed;
  float dash_timer, dash_cooldown_timer, attack_time, attack_duration, attack_cooldown, alt_attack_time;
  float time_since_level, bounce_timer, recoil_timer, recoil_vx, recoil_vy, fall_timer, hard_landing_timer;
  float dash_landing_timer, hard_land_fail_safe_timer, floating_buffer_timer, look_delay_timer, prev_gravity;
  float transition_vx, transition_vy;
  bool accepting_input, control_relinquished, doing_hazard_respawn, landed_event;
  bool touching_wall_l, touching_wall_r;
  bool hidden, hit_buffered;
  int8_t buffered_side, buffered_damage, buffered_hazard;
  float invuln_freeze, invuln_time, pulse_t, recoil_timer2, respawn_timer;
  bool invuln_routine, pulsing, pulse_reverse, respawning;
  int8_t thunk_dir;
  bool thunk_hit;
  float thunk_timer;
  /* HeroAnimationController */
  Anim anim;
  bool anim_control, play_landing, play_run_to_idle, play_dash_to_idle, complete_delegate, set_entry_anim;
  bool was_facing_right, was_attacking;
} Hero;
extern Hero g_hero;

void hero_init(float x, float y, bool facing_right);
void hero_fixed(uint32_t keys);   /* a 1/50 s step: FixedUpdate, the physics and its collisions */
void hero_update(void);           /* then Update and the animations */
void hero_draw(void);
bool hero_touching_ground(void);
void hero_recoil_left(void);
void hero_recoil_right(void);
void hero_recoil_down(void);
void hero_bounce(void);
void hero_finished_entering_scene(void);
void hero_take_damage(int side, int damage, int hazard);   /* (side: where the damage comes from, SIDE_LEFT/RIGHT) */
void hero_recoil_unfreeze(void);       /* the end of StartRecoil, after the freeze */
void hero_hazard_respawn(void);        /* HeroController.HazardRespawn */
void hero_check_damage(void);          /* HeroBox: the hazards and enemies touching it */
void hero_late_update(void);           /* (HeroBox.LateUpdate: a buffered hit) */
void hero_soul_gain(void);             /* a nail's hit on an enemy */
void hero_add_geo(int amount);

/* the nail's slashes (NailSlash), children of the Knight */
void slash_start(int kind);
void slash_cancel(void);
void slash_tick(void);
void slash_draw(void);

/* effects */
void fx_dash_burst(float x, float y, bool facing_right, bool on_ground);

void game_new(void);
bool game_enter(int room, float x, float y, bool facing_right);
void game_tick(uint32_t keys);   /* 1/50 s */
void game_draw(void);

/* ---------------------------------------------------------------- the camera (camera.c) */
void cam_init(void);
void cam_tick(void);
void cam_snap_to_hero(void);   /* (after a respawn) */
enum { SHAKE_ENEMY_KILL = 1, SHAKE_AVERAGE, SHAKE_BIG };   /* the CameraShake FSM's events */
void cam_shake(int kind);
void cam_freeze(void);         /* FreezeInPlace (both) */
void cam_lock(int ent);      /* CameraController.LockToArea, the hero entering the area's trigger */
void cam_release(int ent);   /* ReleaseLock */
