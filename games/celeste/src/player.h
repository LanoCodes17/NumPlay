/* Madeline: a port of Player.cs (NoelFB/Celeste, MIT). */
#ifndef PLAYER_H
#define PLAYER_H
#include "ent.h"

enum {
  ST_NORMAL, ST_CLIMB, ST_DASH, ST_SWIM, ST_BOOST, ST_REDDASH, ST_HITSQUASH, ST_LAUNCH, ST_PICKUP, ST_DREAMDASH,
  ST_SUMMITLAUNCH, ST_DUMMY, ST_INTROWALK, ST_INTROJUMP, ST_INTRORESPAWN, ST_INTROWAKEUP, ST_BIRDDASHTUTORIAL,
  ST_FROZEN, ST_REFLECTIONFALL, ST_STARFLY, ST_TEMPLEFALL, ST_CASSETTEFLY, ST_ATTRACT, ST_INTROMOONJUMP, ST_FLINGBIRD,
  ST_INTROTHINKFORABIT, ST_COUNT
};
enum { INTRO_TRANSITION, INTRO_RESPAWN, INTRO_WALKINRIGHT, INTRO_WALKINLEFT, INTRO_JUMP, INTRO_WAKEUP, INTRO_FALL,
       INTRO_TEMPLEMIRRORVOID, INTRO_NONE, INTRO_THINKFORABIT };

#define HAIR_NODES 10

struct Player {
  Ent *ent;
  V2 speed;
  int facing;               /* 1 right, -1 left */
  Sprite spr;
  int8_t spr_dy;            /* Sprite.Y */
  int hair_count, start_hair_count;
  V2 hair[HAIR_NODES];
  uint16_t hair_color;      /* RGB565 */
  float hair_r, hair_g, hair_b;   /* Hair.Color, for Color.Lerp */
  V2 hair_step;
  float hair_step_facing, hair_step_approach, hair_step_ysine, hair_wave;
  bool hair_simulate, hair_outline;
  float hair_alpha;
  int dashes, last_dashes, max_dashes;
  float stamina;
  int state, prev_state;
  bool state_locked;
  /* the state's coroutine: a step and a wait timer */
  int co_step;
  float co_wait;
  bool co_active;
  float co_t;
  bool on_ground, was_on_ground, on_safe_ground;
  int move_x;
  bool flash, was_ducking, ducking;
  float idle_timer;
  float jump_grace_timer;
  bool auto_jump;
  float auto_jump_timer;
  float var_jump_speed, var_jump_timer;
  int force_move_x;
  float force_move_x_timer;
  int hop_wait_x;
  float hop_wait_x_speed;
  V2 last_aim;
  float dash_cooldown_timer, dash_refill_cooldown_timer;
  V2 dash_dir;
  float wall_slide_timer;
  int wall_slide_dir;
  float climb_no_move_timer;
  V2 carry_offset;
  V2 dead_offset;
  float intro_ease;
  float wall_speed_retention_timer, wall_speed_retained;
  int wall_boost_dir;
  float wall_boost_timer;
  float max_fall;
  float dash_attack_timer;
  bool was_tired;
  float highest_air_y;
  bool dash_started_on_ground, fast_jump;
  int last_climb_move;
  float no_wind_timer;
  float dream_dash_can_end_timer;
  Ent *climb_hop_solid;
  V2 climb_hop_solid_pos;
  float play_footstep_on_land;
  float min_hold_timer;
  bool called_dash_events;
  bool launched;
  float launched_timer;
  float dash_trail_timer;
  bool was_dash_b;
  V2 before_dash_speed;
  bool started_dashing;
  float hair_flash_timer;
  V2 wind_dir;
  float wind_timeout, wind_hair_timer;
  bool dead, just_respawned;
  int intro;
  bool dummy_moving, dummy_gravity, dummy_friction, dummy_maxspeed, dummy_auto_animate;
  float hit_squash_no_move_timer;
  bool dream_jump;
  Ent *dream_block;
  Ent *holding;
  V2 swap_cancel;
  int golden_room;
  uint8_t bird_trails;      /* BirdDashTutorial */
  bool bird_climbing;
  float bird_trail_t;          /* PlayerDeadBody.HasGolden: the golden's room, -1 none */
  float strawb_reset_timer;
  int strawb_index;
  bool strawberries_blocked;
  V2 prev_pos;
  int16_t respawn_tween_on;
  float respawn_t;
  V2 respawn_from;
  bool force_camera_update;
  V2 cam_anchor, cam_anchor_lerp;
  bool cam_anchor_ignore_x, cam_anchor_ignore_y;
  /* the dead body */
  bool body;
  V2 body_bounce;
  float body_t, body_scale;
  int body_step;
  float death_effect;
  uint16_t death_color;
  bool death_finished;
  float launch_approach_x;
  bool has_launch_approach;
  float star_fly_timer, star_fly_speed_lerp;
  bool star_fly_transforming;
  V2 star_fly_last_dir;
  V2 boost_target;
  bool boost_red;
  Ent *current_booster, *last_booster;
  V2 attract_to;
  float summit_target_x, summit_particle_timer;
  Sprite sweat;
  bool no_backpack;
  bool inventory_dreamdash, inventory_norefills;
  int inventory_dashes;
  uint8_t triggers_inside[16];
  int ntriggers_inside;
};
extern Player g_player;

/* PlayerSprite's frame metadata (SEC_PMETA): hair offset, bangs frame, has hair, carry offset */
void hair_meta(uint16_t tex, int *hx, int *hy, int *frame, bool *has, int *carry);
void player_spawn(V2 at, int intro);
V2 player_spawn_near(V2 at);
void player_start_cassette_fly(Player *p, V2 target, V2 control);
V2 player_camera_target(Player *p);
bool player_transition_to(Player *p, V2 target);
void player_on_transition(Player *p);
void player_before_up_transition(Player *p);
void player_before_down_transition(Player *p);
void player_on_bounds_h(Player *p);
void player_on_bounds_v(Player *p);
void player_die_ex(Player *p, V2 dir, bool even_if_invincible, bool register_death);   /* Player.Die */
static inline void player_die(Player *p, V2 dir, bool even_if_invincible) { player_die_ex(p, dir, even_if_invincible, true); }
int berry_golden_room(void);
/* cutscene walks (Player.DummyWalkTo, DummyWalkToExact, DummyRunTo) as nested steps: true while walking;
 * a Walk starts with phase 0 */
enum { WALK_TO, WALK_EXACT, RUN_TO };
enum { WALK_BACKWARDS = 1, WALK_INTO_WALLS = 2, WALK_CANCEL_ON_FALL = 4, RUN_FAST = 8 };
typedef struct { float x, mult; uint8_t mode, flags, phase; int8_t last; } Walk;
static inline Walk walk_to(float x) { return (Walk){x, 1, WALK_TO, 0, 0, 0}; }
static inline Walk walk_exact(int x) { return (Walk){(float)x, 1, WALK_EXACT, 0, 0, 0}; }
bool player_walk(Player *p, Walk *w);   /* the room of a golden strawberry (not winged) following the player, -1 */
void leader_lose_all(void);
void player_after_update(Player *p);
void player_set_state(Player *p, int st);
bool player_refill_dash(Player *p);
bool player_use_refill(Player *p, bool two);
void player_refill_stamina(Player *p);
void player_bounce(Player *p, float from_y);
void player_super_bounce(Player *p, float from_y);
void player_side_bounce(Player *p, int dir, float from_x, float from_y);
void player_rebound(Player *p, int dir);
void player_point_bounce(Player *p, V2 from);
bool player_dash_attacking(const Player *p);
bool player_in_control(const Player *p);
V2 player_explode_launch(Player *p, V2 from, bool snap_up);
bool player_start_star_fly(Player *p);
void player_boost(Player *p, Ent *booster, bool red);
V2 player_center(const Player *p);
bool player_bounce_check(const Player *p, float y);
bool player_ducking(const Player *p);
void player_set_ducking(Player *p, bool d);
int player_max_dashes(const Player *p);
void player_jump(Player *p, bool particles, bool sfx);
void player_start_attract(Player *p, V2 to);
#endif
