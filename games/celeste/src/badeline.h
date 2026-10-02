/* The story's shared pieces (src/ch6.c), for chapters 5 to 9: BadelineDummy
 * with its BadelineAutoAnimator, the NPC base class (its sprite, light,
 * talker and MoveTo), and coroutines nested in coroutines. */
#ifndef BADELINE_H
#define BADELINE_H
#include "npc.h"
#include "talk.h"

/* routines an entity runs for others (FloatTo, MoveTo): started now, they step from the next update; a
 * coroutine waits on one with CO_AWAIT (it goes on the update after the one the routine ends on) */
typedef struct { uint32_t start, done; uint8_t step; } Job;
static inline void job_start(Job *j) { j->start = g_frame, j->step = 1; }
static inline bool job_due(const Job *j) { return j->step && j->start != g_frame; }
static inline void job_end(Job *j) { j->step = 0, j->done = g_frame; }
static inline bool job_busy(const Job *j) { return j->step || j->done == g_frame; }
#define CO_AWAIT(c, job)               \
  do {                                 \
    CO_YIELD(c);                       \
    while (job_busy(job)) CO_YIELD(c); \
  } while (0)
#define NB_AWAIT(c, job)               \
  do {                                 \
    NB_YIELD(c);                       \
    while (job_busy(job)) NB_YIELD(c); \
  } while (0)

/* nested steps of the level and the player, as one statement */
#define CO_WALK(c, w, walk)                       \
  do {                                            \
    (w) = (walk);                                 \
    CO_NEST(c, player_walk(&g_player, &(w)));     \
  } while (0)
#define NB_WALK(c, w, walk)                       \
  do {                                            \
    (w) = (walk);                                 \
    NB_NEST(c, player_walk(&g_player, &(w)));     \
  } while (0)
#define CO_ZOOM_TO(c, st, focus, zoom, d) \
  do {                                    \
    step_reset(st);                       \
    CO_NEST(c, zoom_to(st, focus, zoom, d)); \
  } while (0)
#define NB_ZOOM_TO(c, st, focus, zoom, d) \
  do {                                    \
    step_reset(st);                       \
    NB_NEST(c, zoom_to(st, focus, zoom, d)); \
  } while (0)
#define CO_ZOOM_BACK(c, st, d)            \
  do {                                    \
    step_reset(st);                       \
    CO_NEST(c, zoom_back(st, d));         \
  } while (0)
static inline Walk walk_back(int x) { return (Walk){(float)x, 1, WALK_EXACT, WALK_BACKWARDS, 0, 0}; }
static inline Walk walk_mult(int x, float mult) { return (Walk){(float)x, mult, WALK_EXACT, 0, 0, 0}; }

/* CutsceneEntity: a new one of that class (its state begins with a Cutscene), started (Level.StartCutscene) */
Ent *scene_new(const EntClass *cls, void (*on_end)(Ent *e, bool skipped), bool fade_in_on_skip, bool ending_after);
void player_dummy(Player *p);    /* StateMachine.State = Dummy; Locked = true */
void player_free(Player *p);     /* Locked = false; State = Normal */
/* a textbox event's own coroutine: reset when another event starts */
Co *ev_co(Co *c, int8_t *last, int index);
/* Level.LoadLevel of another room at the end of the frame (its default spawn; the player comes in with the respawn
 * intro, which player_intro_none() undoes on the room's first update: IntroTypes.None) */
void level_goto(const char *room);
void player_intro_none(Player *p);
/* OshiroSprite's update: it turns to the side with the portrait's "side" animations (its wiggler: Pop) */
void oshiro_sprite_update(Sprite *s, Wiggler *w);
/* FadeWipe(wipeIn, done) with its duration; ScreenWipe.Wait(): CO_NEST(c, wipe_busy()) */
void fade_wipe(bool in, float duration, WipeDone done);
static inline bool wipe_busy(void) { return g_wipe.active && g_wipe.percent < 1; }

/* ---------------------------------------------------------------- PlayerHair (4 nodes) */
typedef struct {
  V2 n[4];
  float sx;                   /* drawn with: the sprite's |Scale.X|, its color and alpha */
  uint16_t color;
  uint8_t alpha, pad;
} Hair;
void hair_follow(Hair *h, const Sprite *s, V2 render_pos, int facing);                        /* PlayerHair.AfterUpdate */
void hair_draw(Hair *h, const Sprite *s, int facing, uint16_t color, float alpha);            /* PlayerHair.Render */

/* ---------------------------------------------------------------- BadelineDummy */
typedef struct {
  Sprite spr;                 /* PlayerSprite(Badeline): spr.sx is Sprite.Scale.X */
  Hair hair;                  /* PlayerHair, BadelineOldsite.HairColor */
  V2 spr_pos, float_normal;   /* Sprite.Position (Wave), floatNormal */
  V2 target, perp;            /* FloatTo's */
  SineWave wave;
  Wiggler pop;                /* the autoanimator's */
  Job job;                    /* FloatTo / WalkTo */
  float float_speed, float_accel, floatness, light_alpha, hair_alpha, speed, walk_speed;
  int8_t hair_facing, last_anim, turn_at_end;
  uint8_t auto_enabled, syncing, walking, face, fade_light, quick_end, no_light;
} BDummy;
Ent *bd_new(V2 at);                       /* new BadelineDummy(position), added */
void bd_appear(Ent *e);                   /* Appear(level, silent) */
void bd_vanish(Ent *e);                   /* Vanish(): particles, removed */
/* FloatTo(target, turnAtEndTo (0: none), faceDirection, fadeLight, quickEnd) and WalkTo(x, speed): CO_AWAIT on ->job */
void bd_float_to(Ent *e, V2 target, int turn_at_end, bool face, bool fade_light, bool quick_end);
void bd_walk_to(Ent *e, float x, float speed);
void bd_set_frame(Ent *e, int frame);     /* Sprite.SetAnimationFrame */
#define BD(e) ST(e, BDummy)
void sprite_set_frame(Sprite *s, int frame);

/* ---------------------------------------------------------------- NPC */
enum { NPC_THEO05_ENTRANCE = 1, NPC_THEO05_MIRROR, NPC_THEO06_PLATEAU, NPC_GRANNY06, NPC_THEO06_ENDING, NPC_GRANNY06_ENDING,
       NPC_GRANNY07X, NPC_GRANNY08, NPC_THEO08, NPC_GRANNY09_IN, NPC_GRANNY09_OUT, NPC_OSHIRO };
typedef struct {
  Sprite spr;
  Talk talk;
  Job mv;                     /* MoveTo */
  V2 mv_target;
  float mv_speed, mv_alpha, light_alpha, maxspeed, speed_y;
  uint32_t light_color;
  int8_t light_dy, mv_turn;
  uint8_t light_r0, light_r1, has_light, has_talk, move_y, gravity, mv_fade, mv_remove, idle_anim, move_anim;
  uint8_t which;              /* the chapter file's own class */
  uint16_t haha;              /* its Hahaha: g_ents index + 1, 0 none */
  float f[4];                 /* the class's own */
  int32_t i[4];
  void (*think)(Ent *e);      /* the class's Update, before NPC.Update */
  void (*on_talk)(Ent *e);    /* TalkComponent's OnTalk */
  void (*paint)(Ent *e);      /* its own Render, instead of NPC's */
} Npc;
#define NPC_(e) ST(e, Npc)
Ent *npc_new(float x, float y, int bank, int anim, void (*think)(Ent *e));
void npc_light(Ent *e, int dy, uint32_t color, int r0, int r1);   /* VertexLight(0, dy) */
void npc_talker(Ent *e, int x, int y, int w, int h, V2 draw_at, void (*on_talk)(Ent *e));
void npc_haha(Ent *e);                    /* its Hahaha at (8, -4), laughing with the sprite's "laugh" */
/* MoveTo(target, fadeIn, turnAtEndTo (0: none), removeAtEnd): CO_AWAIT on ->mv */
void npc_move_to(Ent *e, V2 target, bool fade_in, int turn_at_end, bool remove_at_end);
Ent *npc_find(int which);                 /* the first NPC of that class */

/* ---------------------------------------------------------------- the textbox's portrait */
/* Textbox.PortraitName / PortraitAnimation: the shown portrait's bank and its idle animation; false if
 * no textbox shows one (text.c's, weak here until it has one) */
bool textbox_portrait(int *bank, int *idle_anim);
#endif
