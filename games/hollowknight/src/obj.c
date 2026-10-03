/* The room's objects that the Knight acts on (tools/ents.py: ENT_OBJ and the records after each): what the nail hits,
 * breakables and the debris they fling. */
#include <math.h>
#include "game.h"

#define DT 0.02f
#define MAX_OBJS 64
#define MAX_PIECES 24

typedef struct {
  uint8_t kind, state;
  uint16_t ent;   /* its record */
} Obj;
static Obj objs[MAX_OBJS];
static int nobjs;
static uint32_t swing_hit[MAX_OBJS / 32];   /* (LimitSendEvents: an object is hit once a swing) */

enum { BR_WHOLE, BR_BROKEN };

/* ---------------------------------------------------------------- Unity's Random (an xorshift here) */
static uint32_t rng = 0x2545F491u;
float rand_range(float lo, float hi) {
  rng ^= rng << 13, rng ^= rng >> 17, rng ^= rng << 5;
  return lo + (hi - lo) * (float)(rng >> 8) * (1.0f / 16777216.0f);
}

static const Ent *ent_at(int i) {
  int n;
  return &room_ents(&n)[i];
}

/* ---------------------------------------------------------------- debris (Rigidbody2D, ObjectBounce, SpinSelf) */
typedef struct {
  int16_t sprite;
  uint8_t layer, steps;
  uint16_t order;
  bool on, resting, spin;
  float x, y, z, vx, vy, ang, w;   /* (w: degrees a second) */
  float gravity, bounce, spin_factor, mirror, speed;
} Piece;
static Piece pieces[MAX_PIECES];

static void piece_fling(const Ent *p, float ox, float oy, float angle_offset, float amin, float amax, float smin, float smax,
                        float mult) {
  Piece *q = NULL;
  for (int i = 0; i < MAX_PIECES && !q; i++)
    if (!pieces[i].on) q = &pieces[i];
  if (!q) return;   /* (all in use: this one is not seen) */
  q->on = true, q->resting = false, q->steps = 0;
  q->sprite = (int16_t)p->s0, q->layer = p->group, q->order = p->a;
  q->x = ox + p->x0, q->y = oy + p->y0, q->z = p->y1;
  q->spin = p->flags & 1;
  /* (Break turns it by the angle offset; SpinSelf.Start, after, at random) */
  q->ang = q->spin ? rand_range(0, 360) : p->x1 + angle_offset;
  q->gravity = p->p0, q->bounce = p->p1, q->spin_factor = p->p2, q->mirror = p->p3;
  float a = rand_range(amin, amax) * (float)M_PI / 180, s = rand_range(smin, smax) * mult;
  q->vx = cosf(a) * s, q->vy = sinf(a) * s;
  q->w = 0, q->speed = s;
}

static void pieces_tick(void) {
  for (int i = 0; i < MAX_PIECES; i++) {
    Piece *q = &pieces[i];
    if (!q->on || q->resting) continue;
    /* SpinSelf: one push of torque on its second step (a polygon of about a unit: this many degrees a second) */
    if (q->spin && q->steps == 1) q->w = q->vx * q->spin_factor * 60;
    if (q->steps < 255) q->steps++;
    q->vy -= 60 * q->gravity * DT;
    q->w *= 1 - 0.05f * DT;
    float dx = q->vx * DT, dy = q->vy * DT, len = sqrtf(dx * dx + dy * dy);
    PhysHit hit;
    if (len > 0 && phys_ray(q->x, q->y, dx / len, dy / len, len + 0.05f, CF_TERRAIN, &hit)) {
      q->x = hit.x + hit.nx * 0.05f, q->y = hit.y + hit.ny * 0.05f;
      float speed = sqrtf(q->vx * q->vx + q->vy * q->vy);
      if (q->bounce >= 0 && speed > 1) {
        /* ObjectBounce: off the normal, at its speed times the bounce factor */
        float d = q->vx * hit.nx + q->vy * hit.ny;
        float rx = q->vx - 2 * d * hit.nx, ry = q->vy - 2 * d * hit.ny, k = speed * q->bounce * rand_range(0.8f, 1.2f) / speed;
        q->vx = rx * k, q->vy = ry * k;
        q->w *= 0.5f;
        if (hit.ny > 0.5f && fabsf(q->vy) < 1.5f) q->resting = true;
      } else if (hit.ny > 0.5f)
        q->resting = true;
      else
        q->vx = 0;
    } else
      q->x += dx, q->y += dy;
    q->ang += q->w * DT;
    if (q->y < -20) q->on = false;
  }
}

static void pieces_draw(void) {
  for (int i = 0; i < MAX_PIECES; i++) {
    const Piece *q = &pieces[i];
    if (!q->on) continue;
    Inst in;
    sprite_inst_rot(q->sprite, q->x, q->y, q->z, q->mirror, 1, q->ang, 0, &in);
    gfx_actor(&in, SORT_KEY(q->layer, (int)q->order - 32768));
  }
}

/* ---------------------------------------------------------------- Breakable */
/* SetStaticPartsActivation (broken) */
static void breakable_set_broken(const Obj *o, const Ent *e) {
  group_fade(e->group, 0, 0);              /* (the whole renderer and parts off) */
  if (e->group2) group_fade(e->group2, 1, 0);   /* (the remnants on) */
  for (int c = e->a; c < e->a + e->s0; c++) phys_collider_enable(c, false);
  if (e->y1 > 0) world_send_hit((int)e->y1 - 1);   /* (its hitEventReciever) */
  (void)o;
}

static void breakable_enter(Obj *o, const Ent *e) {
  if (e->group2) group_fade(e->group2, 0, 0);   /* (Start: the remnants off) */
  if (persist_get(e->persist)) {
    o->state = BR_BROKEN;
    breakable_set_broken(o, e);
  }
}

/* Breakable.Hit: which way the debris flies, then Break */
static void breakable_hit(Obj *o, const Ent *e, float direction) {
  if (o->state == BR_BROKEN) return;
  int dir = cardinal(direction);
  float angle_offset = e->p2, amin, amax, mult = 1;
  switch (dir) {
    case 2: angle_offset = -angle_offset, amin = 120, amax = 160; break;
    case 0: amin = 30, amax = 70; break;
    case 1: angle_offset = 0, amin = 70, amax = 110, mult = 1.5f; break;
    default: angle_offset = 0, amin = 160, amax = 380; break;
  }
  o->state = BR_BROKEN;
  breakable_set_broken(o, e);
  persist_set(e->persist);
  const Ent *p = e + 1 + e->s1;
  for (int i = 0; i < (int)e->p3; i++, p++)
    if (p->type == ENT_PIECE) piece_fling(p, e->x0, e->y0, angle_offset, amin, amax, e->p0, e->p1, mult);
  cam_shake(SHAKE_ENEMY_KILL);
}

/* ---------------------------------------------------------------- the room */
void obj_enter(void) {
  nobjs = 0;
  phys_colliders_reset();
  enemies_enter();
  memset(pieces, 0, sizeof pieces);
  memset(swing_hit, 0, sizeof swing_hit);
  int n;
  const Ent *es = room_ents(&n);
  for (int i = 0; i < n && nobjs < MAX_OBJS; i++) {
    if (es[i].type != ENT_OBJ) continue;
    Obj *o = &objs[nobjs++];
    o->kind = es[i].flags, o->state = 0, o->ent = (uint16_t)i;
    if (o->kind == OK_BREAKABLE) breakable_enter(o, &es[i]);
  }
}

void obj_tick(void) {
  pieces_tick();
  enemies_update();
}

void obj_draw(void) {
  pieces_draw();
  enemies_draw();
}

/* ---------------------------------------------------------------- the nail */
void obj_swing_start(void) {
  memset(swing_hit, 0, sizeof swing_hit);
  enemies_swing_start();
}

/* the slash's shape this step (world, convex): what it touches is hit (once a swing), the direction (degrees) the
 * blow goes; -> what the Knight does (HB_*: bounce, recoil) */
int obj_nail(const float *pts, int npts, float direction) {
  int out = enemies_nail(pts, npts, direction, g_pd.nail_damage);
  for (int k = 0; k < nobjs; k++) {
    Obj *o = &objs[k];
    const Ent *e = ent_at(o->ent);
    bool touched = false;
    for (int j = 0; j < e->s1; j++) {
      const Ent *b = e + 1 + j;
      if (b->type != ENT_BOX || !box_meets_shape(b->x0, b->y0, b->x1, b->y1, pts, npts)) continue;
      touched = true;
      out |= b->flags;
    }
    if (!touched || (swing_hit[k >> 5] >> (k & 31) & 1)) continue;
    swing_hit[k >> 5] |= 1u << (k & 31);
    if (o->kind == OK_BREAKABLE) breakable_hit(o, e, direction);
  }
  return out;
}

/* DirectionUtils.GetCardinalDirection: 0 right, 1 up, 2 left, 3 down */
int cardinal(float degrees) {
  int d = (int)lrintf(degrees / 90.0f) % 4;
  return d < 0 ? d + 4 : d;
}
