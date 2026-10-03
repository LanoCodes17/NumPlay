/* The room's game objects: their state, run each tick; the render groups they fade. */
#include "game.h"

#define DT 0.02f

/* ---------------------------------------------------------------- render groups: iTweenFadeTo */
static struct {
  float from, to, t, time;
  bool on;
} fades[MAX_GROUPS];

void group_fade(int g, float alpha, float time) {
  if (g <= 0 || g >= MAX_GROUPS) return;
  fades[g].from = g_group_alpha[g] / 255.0f, fades[g].to = alpha, fades[g].t = 0, fades[g].time = time, fades[g].on = true;
  if (time <= 0) {
    g_group_alpha[g] = (uint8_t)(alpha * 255 + 0.5f);
    fades[g].on = false;
  }
}

static void fades_tick(void) {
  for (int g = 1; g < MAX_GROUPS; g++) {
    if (!fades[g].on) continue;
    fades[g].t += DT;
    float k = fades[g].t >= fades[g].time ? 1 : fades[g].t / fades[g].time;
    float a = fades[g].from + (fades[g].to - fades[g].from) * k;
    g_group_alpha[g] = (uint8_t)(a * 255 + 0.5f);
    if (k >= 1) fades[g].on = false;
  }
}

/* ---------------------------------------------------------------- the objects' state */
typedef struct {
  uint8_t state;
  float timer;
} EntState;
static EntState es[MAX_ENTS];
static bool hero_in_position;

static bool persist_get(int i) { return i != NO_PERSIST && (g_pd.persist[i >> 3] >> (i & 7) & 1); }
static void persist_set(int i) {
  if (i != NO_PERSIST) g_pd.persist[i >> 3] |= (uint8_t)(1 << (i & 7));
}

/* masks' states (their FSMs') */
enum { M_PAUSE, M_IDLE, M_IDLE_STAY, M_FADE_OUT, M_FADE_IN, M_DONE };

static void mask_idle(const Ent *e, EntState *s) {
  if (e->flags == MK_SECRET) {
    if (persist_get(e->persist)) {
      group_fade(e->group, 0, 0.1f);   /* (Activate) */
      s->state = M_DONE;
    } else
      s->state = M_IDLE_STAY;
    return;
  }
  if (e->flags == MK_REMASK && e->p2 >= 0) {
    /* (Idle fades the owner at once; an inverse mask child the other way) */
    group_fade(e->group, e->p2, 0.01f);
    group_fade(e->group2, 1 - e->p2, 0.01f);
  }
  s->state = M_IDLE;
}

static void mask_tick(const Ent *e, EntState *s) {
  if (s->state == M_PAUSE && hero_in_position) {
    s->timer += DT;
    if (s->timer >= e->p1) mask_idle(e, s);
  }
}

static void mask_trigger(const Ent *e, EntState *s, int kind) {
  float t = e->p0;
  switch (e->flags) {
    case MK_SECRET:
      if (s->state == M_IDLE_STAY && kind == EV_STAY) {
        group_fade(e->group, 0, t);
        persist_set(e->persist);
        s->state = M_DONE;
      }
      break;
    case MK_REMASK: {
      float out = e->p3 < 0 ? 0 : e->p3;   /* (Fade Out's alpha; Fade In's the other) */
      if ((s->state == M_IDLE || s->state == M_FADE_IN) && kind == EV_STAY) {
        group_fade(e->group, out, t);
        group_fade(e->group2, 1 - out, t);
        persist_set(e->persist);
        s->state = M_FADE_OUT;
      } else if (s->state == M_FADE_OUT && kind == EV_EXIT) {
        group_fade(e->group, 1 - out, t);
        group_fade(e->group2, out, t);
        s->state = M_FADE_IN;
      }
      break;
    }
    case MK_SIMPLE:
      if (s->state == M_IDLE && ((e->p2 == 0 && kind == EV_ENTER) || (e->p2 == 1 && kind == EV_STAY))) {
        group_fade(e->group, 0, t);
        s->state = M_DONE;
      }
      break;
  }
}

/* ---------------------------------------------------------------- the room */
void world_enter(void) {
  memset(es, 0, sizeof es);
  memset(fades, 0, sizeof fades);
  memset(g_group_alpha, 255, sizeof g_group_alpha);
  hero_in_position = false;
  int n;
  const Ent *e = room_ents(&n);
  for (int i = 0; i < n && i < MAX_ENTS; i++) {
    if (e[i].type != ENT_MASK) continue;
    if (e[i].flags == MK_SIMPLE) es[i].state = M_IDLE;
    else es[i].state = M_PAUSE;
    if (e[i].group2) group_fade(e[i].group2, 0, 0);   /* (Pause: the inverse mask hidden) */
  }
}

void world_hero_in_position(void) { hero_in_position = true; }

void world_trigger(int i, int kind) {
  int n;
  const Ent *e = &room_ents(&n)[i];
  if (e->type == ENT_MASK) mask_trigger(e, &es[i], kind);
}

void world_tick(void) {
  int n;
  const Ent *e = room_ents(&n);
  for (int i = 0; i < n && i < MAX_ENTS; i++)
    if (e[i].type == ENT_MASK) mask_tick(&e[i], &es[i]);
  fades_tick();
}
