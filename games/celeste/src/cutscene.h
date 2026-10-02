/* Cutscenes: Monocle's coroutines in C, CutsceneEntity, the level's zoom and
 * camera moves, and the player's walks (Player.DummyWalkTo...). */
#ifndef CUTSCENE_H
#define CUTSCENE_H
#include "entities.h"
#include "text.h"
#include "wipe.h"

/* ---------------------------------------------------------------- coroutines
 * A coroutine keeps its place and its wait in a Co in the entity's state; its
 * body runs between CO_BEGIN and CO_END, once per update:
 *   CO_YIELD   yield return null
 *   CO_WAIT(t) yield return t   (the wait runs down first, then one more update)
 *   CO_NEST    yield return a nested coroutine: one update to start it, `call`
 *              (true while it runs) each update after, one update when it ends
 * Locals do not survive a yield: keep them in the state. */
typedef struct { int co; float wait; } Co;
#define CO_BEGIN(c) CO_BEGIN_DT(c, DT)
#define CO_BEGIN_DT(c, dt)   \
  if ((c)->wait > 0) {       \
    (c)->wait -= (dt);       \
    return;                  \
  }                          \
  switch ((c)->co) {         \
    case 0:
#define CO_YIELD_(c, n) \
  do {                  \
    (c)->co = (n);      \
    return;             \
    case (n):;          \
  } while (0)
#define CO_YIELD(c) CO_YIELD_(c, __COUNTER__ + 1)
#define CO_WAIT(c, t)  \
  do {                 \
    (c)->wait = (t);   \
    CO_YIELD(c);       \
  } while (0)
#define CO_NEST(c, call)          \
  do {                            \
    CO_YIELD(c);                  \
    while (call) CO_YIELD(c);     \
    CO_YIELD(c);                  \
  } while (0)
#define CO_END(c) \
  default:;       \
  }               \
  (c)->co = -1
#define CO_DONE(c) ((c)->co < 0)

/* yield return Textbox.Say(key, events...): `tb` (an Ent * in the state) holds the textbox */
#define CO_SAY(c, tb, key, events, ctx)          \
  do {                                           \
    CO_YIELD(c);                                 \
    (tb) = textbox_say(key, events, ctx);        \
    while (textbox_opened(tb)) CO_YIELD(c);      \
    CO_YIELD(c);                                 \
  } while (0)

/* ---------------------------------------------------------------- CutsceneEntity */
typedef struct {
  Co co;
  bool skipped, running, fade_in_on_skip, ending_after;
  void (*on_end)(Ent *e, bool skipped);   /* OnEnd(level), WasSkipped */
} Cutscene;
/* Level.StartCutscene: the cutscene's state must begin with a Cutscene */
void cutscene_start(Ent *e, void (*on_end)(Ent *e, bool skipped), bool fade_in_on_skip, bool ending_after);
void cutscene_end(Ent *e);                /* EndCutscene(level): OnEnd, the level's cutscene over, removed */
void level_skip_cutscene(void);           /* from the pause menu */
bool level_can_skip_cutscene(void);

/* ---------------------------------------------------------------- zoom and camera (nested steps) */
typedef struct { float p, from_zoom; V2 from, to; float zoom, duration; } Step;
void zoom_snap(V2 focus, float zoom);
bool zoom_to(Step *s, V2 focus, float zoom, float duration);       /* Level.ZoomTo */
bool zoom_across(Step *s, V2 focus, float zoom, float duration);   /* Level.ZoomAcross */
bool zoom_back(Step *s, float duration);                           /* Level.ZoomBack */
void zoom_reset(void);                                              /* Level.ResetZoom */
bool camera_to(Step *s, V2 target, float duration, float (*ease)(float));   /* CutsceneEntity.CameraTo */
/* a step begins with p = 0: reset it before each nested use */
static inline void step_reset(Step *s) { memset(s, 0, sizeof *s); s->p = -1; }
/* ---------------------------------------------------------------- nested coroutines
 * `yield return Routine()`: a nested routine is a bool function called once an update (from CO_NEST): it
 * returns true at each of its yields and false on the update it ends. Its place and wait are in a Co. */
#define NB_BEGIN(c)        \
  if ((c)->wait > 0) {     \
    (c)->wait -= DT;       \
    return true;           \
  }                        \
  switch ((c)->co) {       \
    case 0:
#define NB_YIELD_(c, n) \
  do {                  \
    (c)->co = (n);      \
    return true;        \
    case (n):;          \
  } while (0)
#define NB_YIELD(c) NB_YIELD_(c, __COUNTER__ + 1)
#define NB_WAIT(c, t)  \
  do {                 \
    (c)->wait = (t);   \
    NB_YIELD(c);       \
  } while (0)
#define NB_NEST(c, call)          \
  do {                            \
    NB_YIELD(c);                  \
    while (call) NB_YIELD(c);     \
    NB_YIELD(c);                  \
  } while (0)
#define NB_END(c) \
  default:;       \
  }               \
  (c)->co = 0;    \
  (c)->wait = 0;  \
  return false
/* yield return Textbox.Say(...) inside a nested routine */
#define NB_SAY(c, tb, key, events, ctx)          \
  do {                                           \
    NB_YIELD(c);                                 \
    (tb) = textbox_say(key, events, ctx);        \
    while (textbox_opened(tb)) NB_YIELD(c);      \
    NB_YIELD(c);                                 \
  } while (0)

/* BreathingMinigame(winnable) (src/breath.c): done once Completed (or gone); not winnable, it pauses for the
 * cutscene before the feather pops */
Ent *breath_new(bool winnable);
bool breath_done(Ent *e);
bool breath_pausing(Ent *e);
void breath_resume(Ent *e);
void breath_remove(Ent *e);
#endif
