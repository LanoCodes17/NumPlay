#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("Os")   /* not drawn every frame: smaller over faster */
#endif
/* CutsceneEntity's part in the level (starting, ending and skipping a
 * cutscene), the zoom and camera moves cutscenes use. */
#include "cutscene.h"

/* ---------------------------------------------------------------- CutsceneEntity */
void cutscene_start(Ent *e, void (*on_end)(Ent *e, bool skipped), bool fade_in_on_skip, bool ending_after) {
  Cutscene *c = ST(e, Cutscene);
  c->running = true;
  c->on_end = on_end;
  c->fade_in_on_skip = fade_in_on_skip;
  c->ending_after = ending_after;
  g_level.in_cutscene = true;   /* Level.StartCutscene */
  g_level.cutscene = e;
}

void cutscene_end(Ent *e) {
  Cutscene *c = ST(e, Cutscene);
  c->running = false;
  if (c->on_end) c->on_end(e, c->skipped);
  if (!g_level.skipping_cutscene) g_level.in_cutscene = false;   /* Level.EndCutscene */
  if (g_level.cutscene == e) g_level.cutscene = NULL;
  ent_remove(e);
}

bool level_can_skip_cutscene(void) { return g_level.in_cutscene && g_level.cutscene && !g_level.skipping_cutscene; }

/* SkipCutsceneRoutine: a fade out, the cutscene's end, a fade in */
static void skip_faded_in(void) {
  g_level.skipping_cutscene = false;
  g_level.in_cutscene = false;
}
static void skip_faded_out(void) {
  Ent *e = g_level.cutscene;
  bool fade_in = true;
  if (e && e->cls) {
    Cutscene *c = ST(e, Cutscene);
    c->skipped = true;
    fade_in = c->fade_in_on_skip;
    cutscene_end(e);
  }
  zoom_reset();
  if (fade_in) {
    wipe_start(WIPE_FADE, true, skip_faded_in);
    g_wipe.duration = 0.25f;
  } else
    skip_faded_in();
}
void level_skip_cutscene(void) {
  if (!level_can_skip_cutscene()) return;
  g_level.skipping_cutscene = true;
  /* the textboxes go */
  for (int i = 0; i < g_nents; i++)
    if (g_ents[i].cls && g_ents[i].cls->name && !strcmp(g_ents[i].cls->name, "textbox")) ent_remove(&g_ents[i]);
  wipe_start(WIPE_FADE, false, skip_faded_out);
  g_wipe.duration = 0.25f;
}

/* ---------------------------------------------------------------- zoom */
void zoom_snap(V2 focus, float zoom) {
  g_level.zoom_focus = focus;
  g_level.zoom_target = g_level.zoom = zoom;
}
void zoom_reset(void) {
  g_level.zoom = g_level.zoom_target = 1;
  g_level.zoom_focus = v2(160, 90);
}
bool zoom_to(Step *s, V2 focus, float zoom, float duration) {
  if (s->p < 0) {
    g_level.zoom_focus = focus;
    g_level.zoom_target = zoom;
    s->from_zoom = g_level.zoom;
    s->p = 0;
  } else
    s->p += DT / duration;
  if (s->p < 1) {
    g_level.zoom = lerpf(s->from_zoom, g_level.zoom_target, ease_sine_inout(clampf(s->p, 0, 1)));
    return true;
  }
  g_level.zoom = g_level.zoom_target;
  return false;
}
bool zoom_across(Step *s, V2 focus, float zoom, float duration) {
  if (s->p < 0) {
    s->from_zoom = g_level.zoom;
    s->from = g_level.zoom_focus;
    s->p = 0;
  } else
    s->p += DT / duration;
  if (s->p < 1) {
    float k = ease_sine_inout(clampf(s->p, 0, 1));
    g_level.zoom = g_level.zoom_target = lerpf(s->from_zoom, zoom, k);
    g_level.zoom_focus = v2lerp(s->from, focus, k);
    return true;
  }
  g_level.zoom = g_level.zoom_target;
  g_level.zoom_focus = focus;
  return false;
}
bool zoom_back(Step *s, float duration) {
  if (s->p < 0) {
    s->from_zoom = g_level.zoom;
    s->p = 0;
  } else
    s->p += DT / duration;
  if (s->p < 1) {
    g_level.zoom = lerpf(s->from_zoom, 1, ease_sine_inout(clampf(s->p, 0, 1)));
    return true;
  }
  zoom_reset();
  return false;
}

/* ---------------------------------------------------------------- the camera */
bool camera_to(Step *s, V2 target, float duration, float (*ease)(float)) {
  if (!ease) ease = ease_cube_inout;
  if (s->p < 0) {
    s->from = g_level.cam;
    s->p = 0;
  } else
    s->p += DT / duration;
  if (s->p < 1) {
    g_level.cam = v2add(s->from, v2mul(v2sub(target, s->from), ease(s->p)));
    return true;
  }
  g_level.cam = target;
  return false;
}
