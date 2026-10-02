/* Building entities from a room's records (tools/pack.py write_entity). */
#ifndef ENTITIES_H
#define ENTITIES_H
#include "level.h"
#include "player.h"
#include "fx.h"

typedef struct {
  int type, nnodes, id;
  float x, y;            /* world position (room offset added) */
  const uint8_t *attrs, *nodes;
  int room;              /* the room's index in the chapter */
  float rx, ry;          /* the room's offset */
  bool trig;             /* a trigger: its EntityID is apart from the entities' (ID + 10000000) */
} EData;

float ea_num(const EData *d, int off, char kind);
static inline bool ea_bool(const EData *d, int off, char kind) { return ea_num(d, off, kind) != 0; }
const char *ea_str(const EData *d, int off);
V2 ed_node(const EData *d, int i);
#define EA(d, ent, attr) ea_num(d, EA_##ent##_##attr, EK_##ent##_##attr)
#define EAB(d, ent, attr) (EA(d, ent, attr) != 0)
#define EAS(d, ent, attr) ea_str(d, EA_##ent##_##attr)

/* per-type state: ST(e, T) (ent.h); the class's .size must be sizeof(T) */

/* the area's look (AreaData): jumpthru, spikes, crumble blocks */
const char *area_jumpthru(void);
const char *area_crumble(void);
uint32_t level_entity_hash(const EData *d);   /* its EntityID (DoNotLoad) */

/* factories: entities.c and the per-chapter files */
bool ent_create(const EData *d);
/* each chapter's own entities and triggers (src/chN.c): true if it made it */
bool ents_ch0(const EData *d);
bool ents_ch1(const EData *d);
bool ents_ch8(const EData *d);
bool trigs_ch1(const EData *d);
bool trigs_ch8(const EData *d);
bool ents_ch2(const EData *d);
bool ents_ch3(const EData *d);
bool ents_ch4(const EData *d);
bool ents_ch5(const EData *d);
bool ents_ch6(const EData *d);
bool ents_ch7(const EData *d);
bool ents_ch9(const EData *d);
bool trigs_ch2(const EData *d);
bool trigs_ch3(const EData *d);
bool trigs_ch4(const EData *d);
bool trigs_ch5(const EData *d);
bool trigs_ch6(const EData *d);
bool trigs_ch7(const EData *d);
bool trigs_ch9(const EData *d);
bool trig_create(const EData *d);
Ent *level_holdable_check(void);          /* a holdable the player can grab now, picked up (Holdable.Pickup) */
void holdable_carry(Ent *h, V2 at);       /* Holdable.Carry */
void holdable_release(Ent *h, V2 force);  /* Holdable.Release */
bool theo_left_behind(const Player *p);
V2 level_closest_spawn(V2 at);
void level_dash_listeners(V2 dir);
void level_booster_boosted(Ent *booster, V2 dir);

void level_debris(float x, float y, char type, V2 from);
void level_break_dash_block(Ent *e, V2 dir);
/* the player's followers (Leader, Follower: berry.c): strawberries, keys, seeds trail behind it.
 * A follower calls leader_tick(e) where its components update (Follower.Update); the class's
 * on_gain_leader / on_lose_leader are Follower.OnGainLeader / OnLoseLeader */
bool leader_gain(Ent *f, float follow_delay, bool persistent);   /* GainFollower (FollowDelay, PersistentFollow) */
void leader_lose(Ent *f);                                         /* LoseFollower */
void leader_transfer(void);                                       /* TransferFollowers */
int leader_count(void);
Ent *leader_follower(int i);
int leader_index(const Ent *f);                                   /* FollowIndex: -1 without a leader */
bool leader_has(const Ent *f);
bool leader_has_id(uint32_t id);
float leader_delay(const Ent *f);                                 /* DelayTimer */
void leader_tick(const Ent *f);
void leader_set_move(const Ent *f, bool move);                    /* MoveTowardsLeader */
void leader_update(V2 at);
void leader_reset(void);
bool berry_create(const EData *d);
bool heart_create(const EData *d);                                /* heart.c: HeartGem */
bool cassette_create(const EData *d);                             /* cassette.c: Cassette, CassetteBlock */
bool dust_create(const EData *d);                                 /* dust.c: Rotate/TrackSpinner */
void dust_entities(void);                                         /* the dust layer (DustEdges) */
void dust_hit(float x, float y);                                  /* the dust bunny there killed Madeline */
void spinners_each(const Room *rm, float x0, float y0, float x1, float y1,
                   void (*fn)(float x, float y, uint16_t nodes, void *ctx), void *ctx);
void cassette_level_start(bool transition);                       /* the end of LoadLevel; OnOutBegin */
bool cassette_block_scale(const Ent *block, V2 *origin, V2 *scale);
void cassette_blocks_finish(void);
void spikes_set_cassette(Ent *spikes, uint16_t on, uint16_t off, V2 origin);
typedef struct { V2 a, b, c; float lerp; } CassetteFly;           /* Player.cassetteFlyCurve, cassetteFlyLerp */
CassetteFly *cassette_fly(void);
Ent *absorb_orb_new(V2 at, Ent *into);                            /* AbsorbOrb: into the player when NULL */
void absorb_orbs_remove(void);
void level_remove_camera_offset_triggers(void);
bool berry_trigger(const EData *d);
void berries_collect_all(void);
bool level_gold_collect_check(const Ent *player);
int ent_attr_size(int type);
int trig_attr_size(int type);

/* Monocle's Wiggler and SineWave components */
typedef struct {
  float counter, sine_counter, increment, sine_add, value;
  bool active, start_zero;
} Wiggler;
void wiggler_init(Wiggler *w, float duration, float freq);    /* Wiggler.Create */
void wiggler_restart(Wiggler *w);                              /* Start() */
void wiggler_start(Wiggler *w, float duration, float freq);   /* Start(duration, frequency) */
void wiggler_update(Wiggler *w);
typedef struct { float counter, freq, value, value_over_two, two_value; } SineWave;
void sine_set(SineWave *s, float counter);                     /* the Counter setter */
void sine_randomize(SineWave *s);
void sine_update(SineWave *s);
/* Monocle's Tween: percent goes 0 to 1 over the duration */
typedef struct { float time_left, duration, percent; bool active; } Tween;
void tween_start(Tween *t, float duration);
bool tween_update(Tween *t);   /* true on the frame it ends */
#endif
