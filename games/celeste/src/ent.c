/* Entities: the pool, depth order, collisions, and Monocle's Actor, Solid,
 * JumpThru movement (pixel by pixel, pushing and carrying riders). */
#include "ent.h"
#include "level.h"
const EntMore g_no_more;

Ent g_ents[MAX_ENTS];
int g_nents;
static uint16_t next_order;
uint16_t g_new_eid = 0xFFFF;
static uint8_t sorted[MAX_ENTS];   /* by depth: rendering */
static uint8_t uord[MAX_ENTS];     /* by insertion: updating (Monocle's EntityList) */
static int nsorted;
static bool unsorted = true;
Ent *g_solidtiles;

uint8_t g_ent_arena[ENT_ARENA] __attribute__((aligned(8)));
/* states hold pointers: aligned for them (8 bytes on a 64-bit computer) */
#define ARENA_ALIGN ((uint32_t)sizeof(void *) > 4 ? 8u : 4u)
static inline uint32_t arena_size(const EntClass *c) { return (c->size + ARENA_ALIGN - 1) & ~(ARENA_ALIGN - 1); }
static uint32_t arena_top;   /* bytes used */

Ent *ent_new(const EntClass *cls, float x, float y) {
  uint32_t size = arena_size(cls);
  if (arena_top + size > ENT_ARENA) return NULL;
  for (int i = 0; i < MAX_ENTS; i++) {
    Ent *e = &g_ents[i];
    if (e->cls) continue;
    memset(e, 0, sizeof *e);
    e->cls = cls;
    e->kind = cls->kind;
    e->x = x, e->y = y;
    e->active = e->visible = e->collidable = 1;
    e->allow_pushing = 1;
    e->order = next_order++;
    e->eid = g_new_eid;
    e->room = (uint8_t)g_level.room_slot;
    e->dead = 2;   /* added: joins the update list next frame (Scene.Add) */
    e->data = (uint16_t)(arena_top / 4);
    memset(g_ent_arena + arena_top, 0, size);
    arena_top += size;
    if (i >= g_nents) g_nents = i + 1;
    unsorted = true;
    return e;
  }
  return NULL;
}

/* slides the states of live entities down over freed ones (end of frame only) */
static void arena_compact(void) {
  uint32_t top = 0;
  /* in arena order: entities sorted by data offset */
  for (;;) {
    Ent *next = NULL;
    for (int i = 0; i < g_nents; i++) {
      Ent *e = &g_ents[i];
      if (e->cls && e->cls->size && e->data * 4u >= top && (!next || e->data < next->data)) next = e;
    }
    if (!next) break;
    uint32_t size = arena_size(next->cls);
    if (next->data * 4u != top) memmove(g_ent_arena + top, g_ent_arena + next->data * 4u, size);
    next->data = (uint16_t)(top / 4);
    top += size;
  }
  arena_top = top;
}

void ent_remove(Ent *e) {
  if (!e || !e->cls || e->dead == 1) return;
  e->dead = 1;
  e->collidable = 0;
}

void ent_box(Ent *e, float w, float h, float x, float y) {
  e->ctype = COL_BOX;
  e->cw = w, e->ch = h, e->cx = x, e->cy = y;
}
void ent_circle(Ent *e, float r, float x, float y) {
  e->ctype = COL_CIRCLE;
  e->cw = r, e->cx = x, e->cy = y, e->ch = 0;
}

static void sort_ents(void) {
  nsorted = 0;
  for (int i = 0; i < g_nents; i++)
    if (g_ents[i].cls && g_ents[i].dead != 2) sorted[nsorted++] = (uint8_t)i;
  /* depth descending (back first), then insertion order */
  for (int i = 1; i < nsorted; i++) {
    uint8_t k = sorted[i];
    int j = i;
    while (j > 0) {
      Ent *a = &g_ents[sorted[j - 1]], *b = &g_ents[k];
      if (a->depth > b->depth || (a->depth == b->depth && (int16_t)(a->order - b->order) < 0)) break;
      sorted[j] = sorted[j - 1];
      j--;
    }
    sorted[j] = k;
  }
  memcpy(uord, sorted, (size_t)nsorted);
  for (int i = 1; i < nsorted; i++) {
    uint8_t k = uord[i];
    int j = i;
    while (j > 0 && (int16_t)(g_ents[uord[j - 1]].order - g_ents[k].order) > 0) uord[j] = uord[j - 1], j--;
    uord[j] = k;
  }
  unsorted = false;
}

void ents_awake_new(void) {
  bool any = false;
  for (int i = 0; i < g_nents; i++)
    if (g_ents[i].cls && g_ents[i].dead == 2) g_ents[i].dead = 0, any = true;
  if (!any) return;
  unsorted = true;
  for (int i = 0; i < g_nents; i++) {
    Ent *e = &g_ents[i];
    if (!e->cls || e->awoken) continue;
    e->awoken = 1;
    /* Solid.Awake / JumpThru.Awake: the static movers riding it are its own (not the level's tiles': AllowStaticMovers) */
    if ((e->kind & (KIND_SOLID | KIND_JUMPTHRU)) && e != g_solidtiles && e->dead != 1) plat_static_movers_attach(e);
    if (e->cls->awake) e->cls->awake(e);
  }
}

void ents_flush_removed(void) {
  for (int i = 0; i < g_nents; i++) {
    Ent *e = &g_ents[i];
    if (e->cls && e->dead == 1) {
      if (e->cls->removed) e->cls->removed(e);
      /* static movers lose their platform, platforms their movers */
      for (int j = 0; j < g_nents; j++)
        if (g_ents[j].cls && ent_platform(&g_ents[j]) == e) ent_set_platform(&g_ents[j], NULL);
      e->cls = NULL;
      unsorted = true;
    }
  }
  while (g_nents > 0 && !g_ents[g_nents - 1].cls) g_nents--;
  arena_compact();
}

/* Level.Update: paused, frozen or between rooms, only the entities tagged for it update */
void ents_update(void) {
  if (unsorted) sort_ents();
  uint8_t need = g_level.paused ? TAG_PAUSE_UPDATE : g_level.frozen ? TAG_FROZEN_UPDATE
                 : g_level.transitioning ? TAG_TRANSITION_UPDATE : 0;
  for (int i = 0; i < nsorted; i++) {
    Ent *e = &g_ents[uord[i]];
    if (!e->cls || e->dead || !e->active) continue;
    if (need && !(e->tags & need)) continue;
    if (e->cls->update) e->cls->update(e);
  }
}

void ents_mark_unsorted(void) { unsorted = true; }

void ents_render_between(int hi, int lo) {
  if (unsorted) sort_ents();
  for (int i = 0; i < nsorted; i++) {
    Ent *e = &g_ents[sorted[i]];
    if (!e->cls || e->dead == 1 || !e->visible || !e->cls->render || (e->tags & TAG_HUD)) continue;
    if (e->depth > hi || e->depth <= lo) continue;
    e->cls->render(e);
  }
}
/* HudRenderer: the interface's entities, over everything */
void ents_render_hud(void) {
  if (unsorted) sort_ents();
  for (int i = 0; i < nsorted; i++) {
    Ent *e = &g_ents[sorted[i]];
    if (e->cls && e->dead != 1 && e->visible && e->cls->render && (e->tags & TAG_HUD)) e->cls->render(e);
  }
}

void ents_clear(bool keep_persistent) {
  for (int i = 0; i < g_nents; i++) {
    Ent *e = &g_ents[i];
    if (!e->cls) continue;
    if (keep_persistent && (e->tags & (TAG_PERSISTENT | TAG_GLOBAL))) continue;
    if (e->cls->removed) e->cls->removed(e);
    e->cls = NULL;
  }
  while (g_nents > 0 && !g_ents[g_nents - 1].cls) g_nents--;
  unsorted = true;
  arena_compact();
}

void ents_remove_room(int room) {
  for (int i = 0; i < g_nents; i++) {
    Ent *e = &g_ents[i];
    if (e->cls && e->room == room && !(e->tags & (TAG_PERSISTENT | TAG_GLOBAL))) {
      if (e->cls->removed) e->cls->removed(e);
      e->cls = NULL;
    }
  }
  while (g_nents > 0 && !g_ents[g_nents - 1].cls) g_nents--;
  unsorted = true;
  arena_compact();
}

/* ---------------------------------------------------------------- collision */
static bool rect_circle(float l, float t, float r, float b, float cx, float cy, float rad) {
  if (cx >= l && cx <= r && cy >= t && cy <= b) return true;
  float nx = clampf(cx, l, r), ny = clampf(cy, t, b);
  float dx = cx - nx, dy = cy - ny;
  return dx * dx + dy * dy < rad * rad;
}

bool tiles_solid_rect(float l, float t, float r, float b) {
  /* Grid.Collide(Rectangle): the rect is in whole pixels */
  int x0 = (int)floorf(l), y0 = (int)floorf(t), x1 = (int)floorf(r), y1 = (int)floorf(b);
  int w = x1 - x0, h = y1 - y0;
  if (w <= 0 || h <= 0) return false;
  return level_solid_cells(x0 >> 3, y0 >> 3, (x0 + w - 1) >> 3, (y0 + h - 1) >> 3);
}

/* the collider of e at (x, y) vs other's, both as Monocle colliders */
bool collide_ent_at(const Ent *e, float x, float y, const Ent *o) {
  if (e->ctype == COL_NONE || o->ctype == COL_NONE) return false;
  /* ColliderLists: the entity's own test against the other's box */
  if (o->ctype == COL_LIST && e->ctype == COL_BOX)
    return MORE(o->cls)->collide_rect(o, x + e->cx, y + e->cy, x + e->cx + e->cw, y + e->cy + e->ch);
  if (e->ctype == COL_LIST && o->ctype == COL_BOX) {
    float dx = e->x - x, dy = e->y - y;
    return MORE(e->cls)->collide_rect(e, o->x + o->cx + dx, o->y + o->cy + dy, o->x + o->cx + o->cw + dx, o->y + o->cy + o->ch + dy);
  }
  float l = x + e->cx, t = y + e->cy;
  if (e->ctype == COL_BOX) {
    float r = l + e->cw, b = t + e->ch;
    switch (o->ctype) {
      case COL_BOX: return r > o->x + o->cx && b > o->y + o->cy && l < o->x + o->cx + o->cw && t < o->y + o->cy + o->ch;
      case COL_CIRCLE: return rect_circle(l, t, r, b, o->x + o->cx, o->y + o->cy, o->cw);
      case COL_GRID: return o->cls == NULL ? false : (o == g_solidtiles ? tiles_solid_rect((int)l, (int)t, (int)l + (int)e->cw, (int)t + (int)e->ch) : level_grid_collide(o, l, t, r, b));
    }
  } else if (e->ctype == COL_CIRCLE) {
    float cx = x + e->cx, cy = y + e->cy;
    switch (o->ctype) {
      case COL_BOX: return rect_circle(o->x + o->cx, o->y + o->cy, o->x + o->cx + o->cw, o->y + o->cy + o->ch, cx, cy, e->cw);
      case COL_CIRCLE: {
        float dx = cx - (o->x + o->cx), dy = cy - (o->y + o->cy), rr = e->cw + o->cw;
        return dx * dx + dy * dy < rr * rr;
      }
    }
  }
  return false;
}

bool collide_rect(const Ent *e, float l, float t, float r, float b) {
  if (e->ctype == COL_BOX)
    return e->x + e->cx + e->cw > l && e->y + e->cy + e->ch > t && e->x + e->cx < r && e->y + e->cy < b;
  if (e->ctype == COL_CIRCLE) return rect_circle(l, t, r, b, e->x + e->cx, e->y + e->cy, e->cw);
  if (e == g_solidtiles) return tiles_solid_rect(l, t, r, b);
  if (e->ctype == COL_GRID) return level_grid_collide(e, l, t, r, b);
  if (e->ctype == COL_LIST) return MORE(e->cls)->collide_rect(e, l, t, r, b);
  return false;
}

static inline bool live(const Ent *o, const Ent *self) { return o->cls && o != self && o->collidable && o->dead != 1; }

Ent *collide_first_solid(const Ent *e, float x, float y) {
  for (int i = 0; i < g_nents; i++) {
    Ent *o = &g_ents[i];
    if ((o->kind & KIND_SOLID) && live(o, e) && collide_ent_at(e, x, y, o)) return o;
  }
  return NULL;
}
bool collide_solid(const Ent *e, float x, float y) { return collide_first_solid(e, x, y) != NULL; }

Ent *collide_first_kind(const Ent *e, float x, float y, uint16_t kind) {
  for (int i = 0; i < g_nents; i++) {
    Ent *o = &g_ents[i];
    if ((o->kind & kind) && live(o, e) && collide_ent_at(e, x, y, o)) return o;
  }
  return NULL;
}

Ent *collide_first_jumpthru_outside(const Ent *e, float x, float y) {
  for (int i = 0; i < g_nents; i++) {
    Ent *o = &g_ents[i];
    if ((o->kind & KIND_JUMPTHRU) && live(o, e) && collide_ent_at(e, x, y, o) && !collide_ent_at(e, e->x, e->y, o)) return o;
  }
  return NULL;
}

bool collide_jumpthru(const Ent *e, float x, float y) { return collide_first_kind(e, x, y, KIND_JUMPTHRU) != NULL; }

bool rect_solid(float l, float t, float r, float b) {
  for (int i = 0; i < g_nents; i++) {
    Ent *o = &g_ents[i];
    if ((o->kind & KIND_SOLID) && o->cls && o->collidable && o->dead != 1 && collide_rect(o, l, t, r, b)) return true;
  }
  return false;
}
bool point_solid(float x, float y) { return rect_solid(x, y, x + 1, y + 1); }

/* ---------------------------------------------------------------- Actor */
bool actor_is_riding_jumpthru(Ent *e, Ent *j) {
  if (MORE(e->cls)->is_riding_jumpthru) return MORE(e->cls)->is_riding_jumpthru(e, j);
  if (e->ignore_jumpthrus) return false;
  return collide_ent_at(e, e->x, e->y + 1, j) && !collide_ent_at(e, e->x, e->y, j);
}
bool actor_is_riding_solid(Ent *e, Ent *s) {
  if (MORE(e->cls)->is_riding_solid) return MORE(e->cls)->is_riding_solid(e, s);
  return collide_ent_at(e, e->x, e->y + 1, s);
}

bool actor_on_ground(const Ent *e, int down) {
  if (collide_solid(e, e->x, e->y + down)) return true;
  if (!e->ignore_jumpthrus) return collide_first_jumpthru_outside(e, e->x, e->y + down) != NULL;
  return false;
}
bool actor_on_ground_at(Ent *e, float x, float y, int down) {
  float ox = e->x, oy = e->y;
  e->x = x, e->y = y;
  bool r = actor_on_ground(e, down);
  e->x = ox, e->y = oy;
  return r;
}

void actor_set_lift(Ent *e, V2 v) {
  e->lift = v;
  if ((v.x != 0 || v.y != 0) && (e->kind & KIND_ACTOR)) {
    ActorExt *x = ST(e, ActorExt);
    x->last_lift = v;
    x->lift_timer = 0.16f;
  }
}
V2 actor_lift(const Ent *e) {
  if (e->lift.x == 0 && e->lift.y == 0 && (e->kind & KIND_ACTOR)) return ST(e, ActorExt)->last_lift;
  return e->lift;
}
void actor_update_lift(Ent *e) {
  e->lift = v2(0, 0);
  ActorExt *x = ST(e, ActorExt);
  if (x->lift_timer > 0) {
    x->lift_timer -= DT;
    if (x->lift_timer <= 0) x->last_lift = v2(0, 0);
  }
}

bool actor_move_h_exact(Ent *e, int move, CollideFn cb, Ent *pusher) {
  float tx = e->x + move;
  int s = signi(move), moved = 0;
  while (move) {
    Ent *hit = collide_first_solid(e, e->x + s, e->y);
    if (hit) {
      e->remx = 0;
      if (cb) {
        Collision c = {v2((float)s, 0), v2((float)moved, 0), v2(tx, e->y), hit, pusher};
        cb(e, &c);
      }
      return true;
    }
    moved += s;
    move -= s;
    e->x += s;
  }
  return false;
}

bool actor_move_v_exact(Ent *e, int move, CollideFn cb, Ent *pusher) {
  float ty = e->y + move;
  int s = signi(move), moved = 0;
  while (move) {
    Ent *hit = collide_first_solid(e, e->x, e->y + s);
    if (!hit && move > 0 && !e->ignore_jumpthrus) hit = collide_first_jumpthru_outside(e, e->x, e->y + s);
    if (hit) {
      e->remy = 0;
      if (cb) {
        Collision c = {v2(0, (float)s), v2(0, (float)moved), v2(e->x, ty), hit, pusher};
        cb(e, &c);
      }
      return true;
    }
    moved += s;
    move -= s;
    e->y += s;
  }
  return false;
}

bool actor_move_h(Ent *e, float amount, CollideFn cb, Ent *pusher) {
  e->remx += amount;
  int n = roundi(e->remx);
  if (n) {
    e->remx -= n;
    return actor_move_h_exact(e, n, cb, pusher);
  }
  return false;
}
bool actor_move_v(Ent *e, float amount, CollideFn cb, Ent *pusher) {
  e->remy += amount;
  int n = roundi(e->remy);
  if (n) {
    e->remy -= n;
    return actor_move_v_exact(e, n, cb, pusher);
  }
  return false;
}
void actor_move_to_x(Ent *e, float x, CollideFn cb) { actor_move_h(e, x - e->x - e->remx, cb, NULL); }
void actor_move_to_y(Ent *e, float y, CollideFn cb) { actor_move_v(e, y - e->y - e->remy, cb, NULL); }
void actor_move_towards_x(Ent *e, float x, float amount, CollideFn cb) { actor_move_to_x(e, approach(e->x + e->remx, x, amount), cb); }
void actor_move_towards_y(Ent *e, float y, float amount, CollideFn cb) { actor_move_to_y(e, approach(e->y + e->remy, y, amount), cb); }

void actor_naive_move(Ent *e, V2 a) {
  e->remx += a.x, e->remy += a.y;
  int nx = roundi(e->remx), ny = roundi(e->remy);
  e->x += nx, e->y += ny;
  e->remx -= nx, e->remy -= ny;
}

bool actor_try_squish_wiggle(Ent *e, Collision *c) {
  if (c->pusher) c->pusher->collidable = 1;
  for (int i = 0; i <= 3; i++)
    for (int j = 0; j <= 3; j++) {
      if (!i && !j) continue;
      for (int sx = 1; sx >= -1; sx -= 2)
        for (int sy = 1; sy >= -1; sy -= 2) {
          float x = e->x + i * sx, y = e->y + j * sy;
          if (!collide_solid(e, x, y)) {
            e->x = x, e->y = y;
            if (c->pusher) c->pusher->collidable = 0;
            return true;
          }
        }
    }
  if (c->pusher) c->pusher->collidable = 0;
  return false;
}

static void actor_squish(Ent *e, Collision *c) {
  if (MORE(e->cls)->on_squish) MORE(e->cls)->on_squish(e, c);
  else if (!actor_try_squish_wiggle(e, c)) ent_remove(e);
}

/* ---------------------------------------------------------------- Platform */
static Ent *riders[32];
static int nriders;

static void get_riders(Ent *p) {
  nriders = 0;
  for (int i = 0; i < g_nents; i++) {
    Ent *a = &g_ents[i];
    if (!(a->kind & KIND_ACTOR) || !a->cls || a->dead == 1) continue;
    if ((p->kind & KIND_SOLID) ? actor_is_riding_solid(a, p) : actor_is_riding_jumpthru(a, p))
      if (nriders < 32) riders[nriders++] = a;
  }
}
static bool is_rider(Ent *a, Ent **list, int n) {
  for (int i = 0; i < n; i++)
    if (list[i] == a) return true;
  return false;
}

void plat_static_movers_move(Ent *p, V2 amount) {
  for (int i = 0; i < g_nents; i++) {
    Ent *m = &g_ents[i];
    if (m->cls && ent_platform(m) == p && m->dead != 1) {
      if (MORE(m->cls)->sm_move) MORE(m->cls)->sm_move(m, amount);
      else m->x += amount.x, m->y += amount.y;
    }
  }
}
void plat_static_movers_shake(Ent *p, V2 amount) {
  for (int i = 0; i < g_nents; i++) {
    Ent *m = &g_ents[i];
    if (m->cls && ent_platform(m) == p && MORE(m->cls)->sm_shake) MORE(m->cls)->sm_shake(m, amount);
  }
}
void plat_static_movers_enable(Ent *p, bool on) {
  for (int i = 0; i < g_nents; i++) {
    Ent *m = &g_ents[i];
    if (m->cls && ent_platform(m) == p) {
      if (MORE(m->cls)->sm_enable) MORE(m->cls)->sm_enable(m, on);
      else m->collidable = m->visible = m->active = on;
    }
  }
}
void plat_static_movers_destroy(Ent *p) {
  for (int i = 0; i < g_nents; i++) {
    Ent *m = &g_ents[i];
    if (m->cls && ent_platform(m) == p) {
      if (MORE(m->cls)->sm_destroy) MORE(m->cls)->sm_destroy(m);
      else ent_remove(m);
      ent_set_platform(m, NULL);
    }
  }
}
void plat_static_movers_trigger(Ent *p) {
  if (MORE(p->cls)->on_staticmover_trigger) MORE(p->cls)->on_staticmover_trigger(p, NULL);
}

void plat_static_movers_attach(Ent *p) {
  uint8_t was = p->collidable;
  if (p->kind & KIND_SOLID) p->collidable = 1;   /* Solid.Awake: collidable while it looks */
  for (int i = 0; i < g_nents; i++) {
    Ent *m = &g_ents[i];
    if (!m->cls || !(m->kind & KIND_STATICMOVER) || m->plat || m->dead == 1) continue;
    if (MORE(m->cls)->sm_riding && MORE(m->cls)->sm_riding(m, p)) {
      ent_set_platform(m, p);
      if (MORE(m->cls)->sm_attach) MORE(m->cls)->sm_attach(m, p);
    }
  }
  p->collidable = was;
}

#define MAX_SHAKES 24
typedef struct { Ent *e; float timer; } Shake;
static Shake shakes[MAX_SHAKES];
static int nshakes;

void plat_update(Ent *e) {
  e->lift = v2(0, 0);
  if (e->shaking) {
    if (level_on_interval(0.04f)) {
      V2 was = e_shake(e);
      e->shakex = (int8_t)(rndi(3) - 1), e->shakey = (int8_t)(rndi(3) - 1);   /* Calc.Random.ShakeVector */
      V2 d = v2sub(e_shake(e), was);
      if (MORE(e->cls)->on_shake) MORE(e->cls)->on_shake(e, d);
      else plat_static_movers_shake(e, d);
    }
    for (int i = 0; i < nshakes; i++)
      if (shakes[i].e == e && shakes[i].timer > 0) {
        shakes[i].timer -= DT;
        if (shakes[i].timer <= 0) {
          shakes[i] = shakes[--nshakes];
          plat_stop_shaking(e);
        }
        break;
      }
  }
}
void plat_start_shaking(Ent *e, float t) {
  e->shaking = 1;
  /* Platform.ShakeTimer: kept for the few shaking at once (those that stopped or went are dropped) */
  for (int i = 0; i < nshakes; i++)
    if (!shakes[i].e->cls || !shakes[i].e->shaking) shakes[i--] = shakes[--nshakes];
  for (int i = 0; i < nshakes; i++)
    if (shakes[i].e == e) {
      shakes[i].timer = t;
      return;
    }
  if (nshakes < MAX_SHAKES) shakes[nshakes++] = (Shake){e, t};
}
void plat_stop_shaking(Ent *e) {
  for (int i = 0; i < nshakes; i++)
    if (shakes[i].e == e) shakes[i--] = shakes[--nshakes];
  e->shaking = 0;
  if (e->shakex || e->shakey) {
    V2 d = v2(-e->shakex, -(float)e->shakey);
    if (MORE(e->cls)->on_shake) MORE(e->cls)->on_shake(e, d);
    else plat_static_movers_shake(e, d);
    e->shakex = e->shakey = 0;
  }
}

static void solid_move_h_exact(Ent *s, int move) {
  get_riders(s);
  Ent *rl[32];
  int nr = nriders;
  memcpy(rl, riders, sizeof(Ent *) * nr);
  float right = e_right(s), left = e_left(s);
  Ent *p = level_player_ent();
  if (p && g_in.move_x == signi(move) && signf(level_player_speed_x()) == signi(move) && !is_rider(p, rl, nr) &&
      collide_ent_at(s, s->x + move, s->y - 1, p))
    actor_move_v(p, 1, NULL, NULL);
  (void)right, (void)left;
  s->x += move;
  plat_static_movers_move(s, v2((float)move, 0));
  if (s->collidable) {
    for (int i = 0; i < g_nents; i++) {
      Ent *a = &g_ents[i];
      if (!(a->kind & KIND_ACTOR) || !a->cls || a->dead == 1 || !a->allow_pushing) continue;
      uint8_t was = a->collidable;
      a->collidable = 1;
      if (!a->treat_naive && collide_ent_at(s, s->x, s->y, a)) {
        int mv = move > 0 ? (int)(e_right(s) - e_left(a)) : (int)(e_left(s) - e_right(a));
        s->collidable = 0;
        actor_move_h_exact(a, mv, actor_squish, s);
        actor_set_lift(a, s->lift);
        s->collidable = 1;
      } else if (is_rider(a, rl, nr)) {
        s->collidable = 0;
        if (a->treat_naive) actor_naive_move(a, v2((float)move, 0));
        else actor_move_h_exact(a, move, NULL, NULL);
        actor_set_lift(a, s->lift);
        s->collidable = 1;
      }
      a->collidable = was;
    }
  }
}

static void solid_move_v_exact(Ent *s, int move) {
  get_riders(s);
  Ent *rl[32];
  int nr = nriders;
  memcpy(rl, riders, sizeof(Ent *) * nr);
  s->y += move;
  plat_static_movers_move(s, v2(0, (float)move));
  if (s->collidable) {
    for (int i = 0; i < g_nents; i++) {
      Ent *a = &g_ents[i];
      if (!(a->kind & KIND_ACTOR) || !a->cls || a->dead == 1 || !a->allow_pushing) continue;
      uint8_t was = a->collidable;
      a->collidable = 1;
      if (!a->treat_naive && collide_ent_at(s, s->x, s->y, a)) {
        int mv = move > 0 ? (int)(e_bottom(s) - e_top(a)) : (int)(e_top(s) - e_bottom(a));
        s->collidable = 0;
        actor_move_v_exact(a, mv, actor_squish, s);
        actor_set_lift(a, s->lift);
        s->collidable = 1;
      } else if (is_rider(a, rl, nr)) {
        s->collidable = 0;
        if (a->treat_naive) actor_naive_move(a, v2(0, (float)move));
        else actor_move_v_exact(a, move, NULL, NULL);
        actor_set_lift(a, s->lift);
        s->collidable = 1;
      }
      a->collidable = was;
    }
  }
}

static void jumpthru_move_h_exact(Ent *j, int move) {
  if (j->collidable) {
    for (int i = 0; i < g_nents; i++) {
      Ent *a = &g_ents[i];
      if (!(a->kind & KIND_ACTOR) || !a->cls || a->dead == 1) continue;
      if (actor_is_riding_jumpthru(a, j)) {
        if (a->treat_naive) actor_naive_move(a, v2((float)move, 0));
        else actor_move_h_exact(a, move, NULL, NULL);
      }
    }
  }
  j->x += move;
  plat_static_movers_move(j, v2((float)move, 0));
}

static void jumpthru_move_v_exact(Ent *j, int move) {
  if (j->collidable) {
    for (int i = 0; i < g_nents; i++) {
      Ent *a = &g_ents[i];
      if (!(a->kind & KIND_ACTOR) || !a->cls || a->dead == 1) continue;
      if (move < 0) {
        if (actor_is_riding_jumpthru(a, j)) {
          j->collidable = 0;
          if (a->treat_naive) actor_naive_move(a, v2(0, (float)move));
          else actor_move_v_exact(a, move, NULL, NULL);
          actor_set_lift(a, j->lift);
          j->collidable = 1;
        } else if (!a->treat_naive && collide_ent_at(j, j->x, j->y + move, a) && !collide_ent_at(j, j->x, j->y, a)) {
          j->collidable = 0;
          actor_move_v_exact(a, (int)(e_top(j) + move - e_bottom(a)), NULL, NULL);
          actor_set_lift(a, j->lift);
          j->collidable = 1;
        }
      } else if (actor_is_riding_jumpthru(a, j)) {
        j->collidable = 0;
        if (a->treat_naive) actor_naive_move(a, v2(0, (float)move));
        else actor_move_v_exact(a, move, NULL, NULL);
        actor_set_lift(a, j->lift);
        j->collidable = 1;
      }
    }
  }
  j->y += move;
  plat_static_movers_move(j, v2(0, (float)move));
}

void plat_move_h_exact(Ent *e, int move) {
  if (e->kind & KIND_SOLID) solid_move_h_exact(e, move);
  else if (e->kind & KIND_JUMPTHRU) jumpthru_move_h_exact(e, move);
  else e->x += move;
}
void plat_move_v_exact(Ent *e, int move) {
  if (e->kind & KIND_SOLID) solid_move_v_exact(e, move);
  else if (e->kind & KIND_JUMPTHRU) jumpthru_move_v_exact(e, move);
  else e->y += move;
}

void plat_move_h(Ent *e, float amount) {
  e->lift.x = amount / DT;
  e->remx += amount;
  int n = roundi(e->remx);
  if (n) {
    e->remx -= n;
    plat_move_h_exact(e, n);
  }
}
void plat_move_v(Ent *e, float amount) {
  e->lift.y = amount / DT;
  e->remy += amount;
  int n = roundi(e->remy);
  if (n) {
    e->remy -= n;
    plat_move_v_exact(e, n);
  }
}
void plat_move_h_lift(Ent *e, float amount, float lift) {
  e->lift.x = lift;
  e->remx += amount;
  int n = roundi(e->remx);
  if (n) {
    e->remx -= n;
    plat_move_h_exact(e, n);
  }
}
void plat_move_v_lift(Ent *e, float amount, float lift) {
  e->lift.y = lift;
  e->remy += amount;
  int n = roundi(e->remy);
  if (n) {
    e->remy -= n;
    plat_move_v_exact(e, n);
  }
}
void plat_move_to_x(Ent *e, float x) { plat_move_h(e, x - (e->x + e->remx)); }
void plat_move_to_y(Ent *e, float y) { plat_move_v(e, y - (e->y + e->remy)); }
void plat_move_to(Ent *e, float x, float y) {
  plat_move_h(e, x - (e->x + e->remx));
  plat_move_v(e, y - (e->y + e->remy));
}
void plat_move_towards_x(Ent *e, float x, float amount) { plat_move_to_x(e, approach(e->x + e->remx, x, amount)); }
void plat_move_towards_y(Ent *e, float y, float amount) { plat_move_to_y(e, approach(e->y + e->remy, y, amount)); }

/* MoveVExactCollideSolids / MoveHExactCollideSolids: step by step, breaking dash blocks on the way */
static bool exact_collide_solids(Ent *e, int move, bool vertical, bool thru_dash_blocks) {
  float was = vertical ? e->y : e->x;
  int s = signi(move), moved = 0;
  Ent *hit = NULL;
  while (move) {
    float dx = vertical ? 0 : (float)s, dy = vertical ? (float)s : 0;
    if (thru_dash_blocks)
      for (int i = 0; i < g_nents; i++) {
        Ent *o = &g_ents[i];
        if (o->cls && o->dead != 1 && o->collidable && level_is_dash_block(o) && collide_ent_at(e, e->x + dx, e->y + dy, o)) {
          level_break_dash_block(o, v2(dx, dy));
          level_shake(0.2f);
        }
      }
    hit = collide_first_solid(e, e->x + dx, e->y + dy);
    if (hit) break;
    if (vertical && move > 0) {
      hit = collide_first_jumpthru_outside(e, e->x, e->y + dy);
      if (hit) break;
    }
    moved += s;
    move -= s;
    if (vertical) e->y += s;
    else e->x += s;
  }
  if (vertical) e->y = was;
  else e->x = was;
  if (vertical) plat_move_v_exact(e, moved);
  else plat_move_h_exact(e, moved);
  return hit != NULL;
}
bool plat_move_v_collide_solids(Ent *e, float amount, bool thru_dash_blocks) {
  e->lift.y = amount / DT;
  e->remy += amount;
  int n = roundi(e->remy);
  if (!n) return false;
  e->remy -= n;
  return exact_collide_solids(e, n, true, thru_dash_blocks);
}
bool plat_move_h_collide_solids(Ent *e, float amount, bool thru_dash_blocks) {
  e->lift.x = amount / DT;
  e->remx += amount;
  int n = roundi(e->remx);
  if (!n) return false;
  e->remx -= n;
  return exact_collide_solids(e, n, false, thru_dash_blocks);
}

bool solid_has_player_rider(Ent *e) {
  Ent *p = level_player_ent();
  return p && actor_is_riding_solid(p, e);
}
bool solid_has_player_on_top(Ent *e) {
  Ent *p = level_player_ent();
  return p && collide_ent_at(e, e->x, e->y - 1, p);
}
bool solid_has_player_climbing(Ent *e) {
  Ent *p = level_player_ent();
  if (!p || !level_player_climbing()) return false;
  int f = level_player_facing();
  return f < 0 ? collide_ent_at(e, e->x + 1, e->y, p) : collide_ent_at(e, e->x - 1, e->y, p);
}
bool solid_has_rider(Ent *e) {
  for (int i = 0; i < g_nents; i++) {
    Ent *a = &g_ents[i];
    if ((a->kind & KIND_ACTOR) && a->cls && a->dead != 1 && actor_is_riding_solid(a, e)) return true;
  }
  return false;
}
bool jumpthru_has_player_rider(Ent *e) {
  Ent *p = level_player_ent();
  return p && actor_is_riding_jumpthru(p, e);
}
