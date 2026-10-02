#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("Os")   /* not drawn every frame: smaller over faster */
#endif
/* Dust bunnies (DustGraphic, drawn with DustEdges' colored outline) and the
 * spinners that move: Dust/Blade RotateSpinner and TrackSpinner. A room can
 * have hundreds of dust bunnies (DustStaticSpinner, spinner.c's records): what
 * the game keeps per bunny comes here from its place (the random picks) and
 * from the time (rotations, blinks), the nodes from pack.py (the tiles). */
#include "entities.h"

static const uint16_t BASE[3] = {T_danger_dustcreature_base00, T_danger_dustcreature_base01, T_danger_dustcreature_base02};
static const uint16_t OVERLAY[3] = {T_danger_dustcreature_overlay00, T_danger_dustcreature_overlay01,
                                    T_danger_dustcreature_overlay02};
static const uint16_t EYES[2][3] = {{T_danger_dustcreature_eyes00, T_danger_dustcreature_eyes01, T_danger_dustcreature_eyes02},
                                    {T_danger_dustcreature_templeeyes00, T_danger_dustcreature_templeeyes01,
                                     T_danger_dustcreature_templeeyes02}};

static uint32_t mix(uint32_t h) {
  h ^= h >> 16;
  h *= 0x7feb352du;
  h ^= h >> 15;
  h *= 0x846ca68bu;
  return h ^ (h >> 16);
}
static float hf(uint32_t h, int k) { return (float)(mix(h + (uint32_t)k * 0x9E3779B9u) >> 8) / 16777216.f; }

/* the dust that killed Madeline (OnHitPlayer): it shakes, its eyes go dead */
static struct { float x, y, timer; V2 shake; } hit;
void dust_hit(float x, float y) {
  hit.x = x, hit.y = y, hit.timer = 0.6f;
}

/* DustStyles: area 5's are blue, everyone else's (the resort's) red */
static bool temple_style(void) { return g_session.area == 5; }
static uint16_t edge_cols[8];
static uint32_t edge_epoch = 0xFFFFFFFF;
static float edge_ease;
/* FxDust: the outline's colors, from noise (two random fields 2.5 pixels a cell, eased one into the other each second) */
static uint16_t edge_color(int x, int y, void *ctx) {
  (void)ctx;
  int cx = (int)floorf((x + g_camx) / 2.5f), cy = (int)floorf((y + g_camy) / 2.5f);
  uint32_t e = edge_epoch;
  uint32_t ox0 = mix(e) & 127, oy0 = mix(e * 3 + 1) & 63, ox1 = mix(e + 1) & 127, oy1 = mix(e * 3 + 4) & 63;
  float a = (float)(mix((uint32_t)(cx + ox0) * 73856093u ^ (uint32_t)(cy + oy0) * 19349663u ^ e) & 255) / 255.f;
  float b = (float)(mix((uint32_t)(cx + ox1) * 73856093u ^ (uint32_t)(cy + oy1) * 19349663u ^ (e + 1)) & 255) / 255.f;
  float n = a + (b - a) * edge_ease;
  return edge_cols[(int)(n * 7.99f)];
}
static void edge_prepare(void) {
  float t = g_level.time_active;
  uint32_t e = (uint32_t)t;
  edge_ease = t - (float)e;
  if (e == edge_epoch && edge_cols[0]) return;
  edge_epoch = e;
  static const uint32_t red[3] = {0xf25a10, 0xff0000, 0xf21067}, blue[3] = {0x245ebb, 0x17a0ff, 0x17a0ff};
  const uint32_t *c = temple_style() ? blue : red;
  for (int i = 0; i < 8; i++) {   /* the three colors, one into the next */
    float n = i / 7.f * 2;
    int k = n >= 1 ? 1 : 0;
    float f = n - k;
    uint32_t p = c[k], q = c[k + 1];
    int r = (int)((p >> 16) + (((int)(q >> 16) - (int)(p >> 16)) * f));
    int g = (int)((p >> 8 & 255) + (((int)(q >> 8 & 255) - (int)(p >> 8 & 255)) * f));
    int bl = (int)((p & 255) + (((int)(q & 255) - (int)(p & 255)) * f));
    edge_cols[i] = rgb((uint32_t)(r << 16 | g << 8 | bl));
  }
}

/* one DustGraphic: at (x, y), its picks from seed; nodes: enabled (4 bits), expanded in x (4), in y (4);
 * eyes: their direction unless follow; static: DustStaticSpinner's (autoControlEyes) */
typedef struct {
  float x, y;
  uint32_t seed;
  uint16_t nodes;
  bool statik;
  V2 eye;
  bool eye_set;
} Dust;

static uint32_t *mask;   /* the strip's dust (DustEdges draws one outline around all of it) */
#define MASK_BYTES (17 * 40)   /* the rows of a strip and one each side, 320 bits each */
uint32_t dust_ram(void) { return MASK_BYTES; }
/* the textures' colors (each has its own palette): center, bases, overlays, eyes (red), dead eyes */
enum { L_CENTER, L_BASE, L_OVERLAY = L_BASE + 3, L_EYES = L_OVERLAY + 3, L_DEAD = L_EYES + 3, L_COUNT };
typedef struct { uint16_t c[L_COUNT][16], a[L_COUNT][16]; Tex t[L_COUNT]; bool ok[L_COUNT]; } Luts;
static void luts_make(Luts *l) {
  memset(l, 0, sizeof *l);
  uint16_t ids[L_COUNT] = {T_danger_dustcreature_center00, BASE[0], BASE[1], BASE[2], OVERLAY[0], OVERLAY[1], OVERLAY[2],
                           EYES[temple_style()][0], EYES[temple_style()][1], EYES[temple_style()][2],
                           T_danger_dustcreature_deadEyes};
  uint16_t eye = temple_style() ? rgb(0x245ebb) : rgb(0xff0000);
  for (int i = 0; i < L_COUNT; i++)
    if ((l->ok[i] = tex_get(ids[i], &l->t[i])) && l->t[i].fmt == TF_RAW4)
      pal_lut(l->t[i].pal, i >= L_EYES ? eye : 0xFFFF, l->c[i], l->a[i]);
    else
      l->ok[i] = false;
}
static void dust_draw(uint16_t *strip, int sy0, int sy1, const Dust *d, int pass, const Luts *lt) {   /* pass 0: body, 1: eyes */
  uint32_t h = mix(d->seed);
  float t = g_level.time_active;
  bool killer = hit.timer > 0 && d->x == hit.x && d->y == hit.y;
  float px = d->x, py = d->y;
  int en = d->nodes & 15;
  /* AddDustNodesIfInCamera: Position moved a pixel towards the nodes */
  if (en & 5) px -= 1;
  if (en & 10) px += 1;
  if (en & 3) py -= 1;
  if (en & 12) py += 1;
  if (killer) px += hit.shake.x, py += hit.shake.y;
  float sx = floorf(px + 0.5f) - g_camx, sy = floorf(py + 0.5f) - g_camy;   /* RenderPosition on screen */
  if (sy + 20 < sy0 - 1 || sy - 20 > sy1 + 1) return;
  if (!lt->ok[L_CENTER]) return;
  if (pass) goto eyes;
  Layer l[9];
  int n = 0;
  float tr = hf(h, 1) + t * 0.6f;   /* the center turns with timer */
  const Tex *tc = &lt->t[L_CENTER];
  l[n++] = (Layer){tc, sx, sy, tc->fw / 2.f, tc->fh / 2.f, cosf(tr), sinf(tr), 1, lt->c[L_CENTER], lt->a[L_CENTER]};
  int reach = 13;
  static const int8_t nx[4] = {-1, 1, -1, 1}, ny[4] = {-1, -1, 1, 1};
  for (int i = 3; i >= 0; i--) {
    if (!(en >> i & 1)) continue;
    int bi = (int)(hf(h, 10 + i) * 3), oi = (int)(hf(h, 20 + i) * 3);
    if (!lt->ok[L_BASE + bi] || !lt->ok[L_OVERLAY + oi]) continue;
    const Tex *tb = &lt->t[L_BASE + bi], *to = &lt->t[L_OVERLAY + oi];
    float r = hf(h, 30 + i) * PI_F * 2 + t * 0.5f;
    float vx = d->nodes >> (4 + i) & 1 ? 5.f : 1.f, vy = d->nodes >> (8 + i) & 1 ? 5.f : 1.f;
    if (vx > 1 || vy > 1) reach = 15;
    float ax = sx + nx[i] * 0.70710677f * vx, ay = sy + ny[i] * 0.70710677f * vy;
    float c = cosf(r), s = sinf(r);
    l[n++] = (Layer){to, ax, ay, to->fw / 2.f, to->fh / 2.f, c, -s, 1, lt->c[L_OVERLAY + oi], lt->a[L_OVERLAY + oi]};
    l[n++] = (Layer){tb, ax, ay, tb->fw / 2.f, tb->fh / 2.f, c, s, 1, lt->c[L_BASE + bi], lt->a[L_BASE + bi]};
  }
  blit_layers(strip, sy0, sy1, l, n, (int)sx - reach, (int)sy - reach, (int)sx + reach + 1, (int)sy + reach + 1, edge_color,
              NULL, mask);
  return;
eyes:;
  /* the eyes (Eyeballs) */
  int count = (en & 1) + (en >> 1 & 1) + (en >> 2 & 1) + (en >> 3 & 1);
  bool exist = !d->statik || hf(h, 2) < 0.5f;
  if (!exist) return;
  V2 dir = d->eye;
  if (!d->eye_set) {
    bool by_rotation = count < 4;
    V2 range = v2(0, 0);
    if (by_rotation) {
      for (int i = 0; i < 4; i++)
        if (en >> i & 1) range = v2add(range, v2(nx[i] * 0.70710677f, ny[i] * 0.70710677f));
      float len = v2len(range);
      if (len > 0) range = v2mul(range, 1 / len);
      dir = range;
    } else
      dir = angle_vec(hf(h, 3) * PI_F * 2, 1);
    Player *p = level_player();
    if (hf(h, 4) < 0.3f && p) {   /* eyesFollowPlayer */
      V2 to_p = v2sub(v2(p->ent->x, p->ent->y), v2(d->x, d->y));
      float l2 = v2len(to_p);
      if (l2 > 0) {
        to_p = v2mul(to_p, 1 / l2);
        if (by_rotation) {
          float a = atan2f(range.y, range.x), b = atan2f(to_p.y, to_p.x), diff = b - a;
          while (diff > PI_F) diff -= PI_F * 2;
          while (diff < -PI_F) diff += PI_F * 2;
          diff = clampf(diff, -PI_F / 4, PI_F / 4);
          dir = angle_vec(a + diff, 1);
        } else
          dir = to_p;
      }
    }
  }
  /* BlinkRoutine: 2 to 3.5 s open, the left one shut first, both for 0.25 s */
  float period = 2.27f + hf(h, 5) * 1.5f + 0.035f, ph = fmodf(t + hf(h, 6) * period, period);
  bool left = !(ph > period - 0.27f), right = !(ph > period - 0.25f);
  if (killer) left = right = true;
  int ei = killer ? L_DEAD : L_EYES + (int)(mix(h + 7) % 3);
  if (!lt->ok[ei]) return;
  const Tex *te = &lt->t[ei];
  V2 perp = v2(-dir.y, dir.x);
  float pl = v2len(perp);
  if (pl > 0) perp = v2mul(perp, 1 / pl);
  for (int k = -1; k <= 1; k += 2) {   /* DrawCentered */
    if (!(k < 0 ? left : right)) continue;
    float ex = sx + dir.x * 5 - k * perp.x * 3, ey = sy + dir.y * 5 - k * perp.y * 3;
    blit_raw4(strip, sy0, sy1, te, (int)floorf(ex - te->fw / 2.f + 0.5f), (int)floorf(ey - te->fh / 2.f + 0.5f), lt->c[ei],
              lt->a[ei]);
  }
}

/* ---------------------------------------------------------------- the moving spinners */
typedef struct {
  V2 center, start, end, eye, eye_target, outwards;
  float percent, length, pause, angle;
  Sprite spr;
  uint8_t rotate, clockwise, moving, dust, fall, up, speed, trail, started, nodes, color;
} Mover;
static const EntClass MOVER;

/* the dust layer (DustEdges, depth -48): every dust bunny on the screen */
typedef struct { uint16_t *strip; int sy0, sy1, pass; const Luts *lt; } StripCtx;
static void one_static(float x, float y, uint16_t bits, void *ctx) {
  StripCtx *c = ctx;
  Dust d = {x, y, (uint32_t)((int)x * 31 + (int)y * 977), bits, true, {0, 0}, false};
  dust_draw(c->strip, c->sy0, c->sy1, &d, c->pass, c->lt);
}
static void layer_strip(uint16_t *strip, int sy0, int sy1, void *ctx) {
  (void)ctx;
  Luts lt;
  luts_make(&lt);
  StripCtx c = {strip, sy0, sy1, 0, &lt};
  uint32_t need = (uint32_t)(sy1 - sy0 + 2) * 40;
  mask = need <= MASK_BYTES && g_chram[13] ? (uint32_t *)(void *)g_chram[13] : NULL;
  if (mask) memset(mask, 0, need);
  /* the bodies, their outline (DustEdges), then the eyes in front (Eyeballs, depth -51) */
  for (c.pass = 0; c.pass < 2; c.pass++) {
    for (int s = 0; s < 2; s++) {
      const Room *rm = &g_level.rooms[s];
      if (rm->index >= 0 && rm->spin_dust)
        spinners_each(rm, g_camx - 20.f, sy0 - 20.f + g_camy, g_camx + VIEW_W + 20.f, sy1 + 20.f + g_camy, one_static, &c);
    }
    for (int i = 0; i < g_nents; i++) {
      Ent *e = &g_ents[i];
      if (e->cls != &MOVER || e->dead == 1 || !ST(e, Mover)->dust || !e->visible) continue;
      Mover *m = ST(e, Mover);
      Dust d = {e->x, e->y, (uint32_t)(e->eid * 7919u + 13u), m->nodes, false, m->eye, true};
      dust_draw(strip, sy0, sy1, &d, c.pass, &lt);
    }
    if (!c.pass && mask) blit_edges(strip, sy0, sy1, mask, edge_color, NULL);
  }
}
static void layer_update(Ent *e) {
  (void)e;
  if (hit.timer > 0) {
    hit.timer -= DT;
    if (hit.timer <= 0) hit.shake = v2(0, 0);
    else if (level_on_interval(0.05f)) hit.shake = v2((float)(rndi(3) - 1), (float)(rndi(3) - 1));
  }
}
static void layer_render(Ent *e) {
  (void)e;
  if (!g_level.rooms[0].spin_dust && !g_level.rooms[1].spin_dust && g_session.area != 3 && g_session.area != 7) return;
  /* the textures, loaded before the strips are drawn */
  Tex t;
  tex_get(T_danger_dustcreature_center00, &t);
  for (int i = 0; i < 3; i++) tex_get(BASE[i], &t), tex_get(OVERLAY[i], &t), tex_get(EYES[temple_style()][i], &t);
  tex_get(T_danger_dustcreature_deadEyes, &t);
  edge_prepare();
  gfx_custom(layer_strip, NULL, 0, VIEW_H);
}
static const EntClass LAYER = {.name = "dustEdges", .update = layer_update, .render = layer_render};
void dust_entities(void) {
  Ent *e = ent_new(&LAYER, 0, 0);
  if (!e) return;
  e->depth = -48;
  e->tags = TAG_GLOBAL | TAG_TRANSITION_UPDATE;
  e->collidable = 0;
  hit.timer = 0;
}

/* RotateSpinner, TrackSpinner */
static const float PAUSE[3] = {0.3f, 0.2f, 0.6f}, MOVE[3] = {0.9f, 0.4f, 0.3f};
static void track_pos(Ent *e) {   /* UpdatePosition */
  Mover *m = ST(e, Mover);
  float k = ease_sine_inout(m->percent);
  e->x = m->start.x + (m->end.x - m->start.x) * k, e->y = m->start.y + (m->end.y - m->start.y) * k;
}
static float rotate_angle(const Mover *m) { return lerpf(4.712389f, -PI_F / 2, m->percent); }

static void track_start(Ent *e) {   /* OnTrackStart */
  Mover *m = ST(e, Mover);
  if (!m->dust) spr_play(&m->spr, A_templeBlade_spin, true), m->started = 1, m->trail = 1;
}
static void track_end(Ent *e) {   /* OnTrackEnd */
  Mover *m = ST(e, Mover);
  if (!m->dust) {
    m->trail = 0;
    return;
  }
  if (m->outwards.x != 0 || m->outwards.y != 0) {
    float a = atan2f(m->outwards.y, m->outwards.x), b = m->up ? m->angle + PI_F : m->angle, diff = b - a;
    while (diff > PI_F) diff -= PI_F * 2;
    while (diff < -PI_F) diff += PI_F * 2;
    m->eye_target = angle_vec(a + diff * 0.3f, 1);
  } else
    m->eye_target = angle_vec(m->up ? m->angle + PI_F : m->angle, 1);
}

static void mover_update(Ent *e) {
  Mover *m = ST(e, Mover);
  if (!m->dust) spr_update(&m->spr);
  if (m->rotate) {
    if (m->moving) {
      if (m->clockwise) m->percent += 1 - DT / 1.8f;
      else m->percent += DT / 1.8f;
      m->percent = fmodf(m->percent, 1);
      V2 v = angle_vec(rotate_angle(m), m->length);
      e->x = m->center.x + v.x, e->y = m->center.y + v.y;
    }
    if (m->fall) {
      m->center.y += 160 * DT;
      if (e->y > g_level.room->y + g_level.room->h + 32) {
        ent_remove(e);
        return;
      }
    }
    if (m->dust) {
      if (m->moving) {
        m->eye = angle_vec(rotate_angle(m) + PI_F / 2 * (m->clockwise ? 1 : -1), 1);
        if (level_on_interval(0.02f)) particles_emit(PL_BG, &P_DustStaticSpinner_P_Move, 1, v2(e->x, e->y), v2(4, 4), 0);
      }
    } else {
      if (level_on_interval(0.04f)) particles_emit(PL_BG, &P_BladeTrackSpinner_P_Trail, 2, v2(e->x, e->y), v2(3, 3), 0);
      if (level_on_interval(1)) spr_play(&m->spr, A_templeBlade_spin, false);
    }
    return;
  }
  /* TrackSpinner */
  if (m->dust && (m->eye.x != m->eye_target.x || m->eye.y != m->eye_target.y)) {   /* DustGraphic: EyeDirection approaches */
    V2 d = v2sub(m->eye_target, m->eye);
    float l = v2len(d), k = 12 * DT;
    m->eye = l <= k ? m->eye_target : v2add(m->eye, v2mul(d, k / l));
  }
  if (m->moving) {
    if (m->pause > 0) {
      m->pause -= DT;
      if (m->pause <= 0) track_start(e);
    } else {
      m->percent = approach(m->percent, m->up ? 1.f : 0.f, DT / MOVE[m->speed]);
      track_pos(e);
      if ((m->up && m->percent == 1) || (!m->up && m->percent == 0)) {
        m->up = !m->up;
        m->pause = PAUSE[m->speed];
        track_end(e);
      }
    }
  }
  if (m->dust) {
    if (m->moving && m->pause < 0 && level_on_interval(0.02f))
      particles_emit(PL_BG, &P_DustStaticSpinner_P_Move, 1, v2(e->x, e->y), v2(4, 4), 0);
  } else if (m->trail && level_on_interval(0.04f))
    particles_emit(PL_BG, &P_BladeTrackSpinner_P_Trail, 2, v2(e->x, e->y), v2(3, 3), 0);
}

static void mover_on_player(Ent *e, Player *p) {
  Mover *m = ST(e, Mover);
  V2 d = v2sub(v2(p->ent->x, p->ent->y), v2(e->x, e->y));
  float l = v2len(d);
  bool was = p->dead;
  player_die(p, l > 0 ? v2mul(d, 1 / l) : v2(0, 0), false);
  if (!was && p->dead) {
    m->moving = 0;
    if (m->dust) dust_hit(e->x, e->y);
  }
}
static void mover_render(Ent *e) {
  Mover *m = ST(e, Mover);
  if (!m->dust) spr_draw(&m->spr, e->x, e->y);
}
static bool mover_collide(const Ent *e, float l, float t, float r, float b) {   /* Circle(6), Hitbox(16, 4, -8, -3) */
  float nx = clampf(e->x, l, r), ny = clampf(e->y, t, b), dx = e->x - nx, dy = e->y - ny;
  if (dx * dx + dy * dy < 36) return true;
  return !ST(e, Mover)->rotate && r > e->x - 8 && l < e->x + 8 && b > e->y - 3 && t < e->y + 1;
}
/* StaticMover: a RotateSpinner rides what its center is in */
static bool mover_riding(Ent *e, Ent *p) {
  Mover *m = ST(e, Mover);
  return m->rotate && p->collidable && collide_rect(p, m->center.x, m->center.y, m->center.x + 1, m->center.y + 1);
}
static void mover_sm_move(Ent *e, V2 v) {
  Mover *m = ST(e, Mover);
  m->center = v2add(m->center, v);
  e->x += v.x, e->y += v.y;
}
static void mover_sm_destroy(Ent *e) { ST(e, Mover)->fall = 1; }

static void mover_awake(Ent *e) {
  Mover *m = ST(e, Mover);
  if (m->rotate) return;
  track_start(e);
  if (!m->dust) return;
  /* DustTrackSpinner.Establish: against a wall the whole way, the nodes on its side go */
  V2 dir = v2sub(m->end, m->start);
  float len = v2len(dir);
  if (len <= 0) return;
  dir = v2mul(dir, 1 / len);
  V2 side = v2(-dir.y, dir.x);
  bool on = rect_solid(e->x + side.x * 4 - 2, e->y + side.y * 4 - 2, e->x + side.x * 4 + 2, e->y + side.y * 4 + 2);
  if (!on) {
    side = v2(-side.x, -side.y);
    on = rect_solid(e->x + side.x * 4 - 2, e->y + side.y * 4 - 2, e->x + side.x * 4 + 2, e->y + side.y * 4 + 2);
  }
  if (!on) return;
  for (int i = 8; i < len && on; i += 8) {
    float x = e->x + side.x * 4 + dir.x * i, y = e->y + side.y * 4 + dir.y * i;
    on = rect_solid(x - 2, y - 2, x + 2, y + 2);
  }
  if (!on) return;
  if (side.x < 0) m->nodes &= ~5;        /* LeftNodes */
  else if (side.x > 0) m->nodes &= ~10;  /* RightNodes */
  else if (side.y < 0) m->nodes &= ~3;   /* TopNodes */
  else if (side.y > 0) m->nodes &= ~12;  /* BottomNodes */
  m->outwards = v2(-side.x, -side.y);
  float a = atan2f(m->outwards.y, m->outwards.x), b = m->up ? m->angle + PI_F : m->angle, diff = b - a;
  while (diff > PI_F) diff -= PI_F * 2;
  while (diff < -PI_F) diff += PI_F * 2;
  m->eye = m->eye_target = angle_vec(a + diff * 0.3f, 1);
}

static const EntClass MOVER = {.name = "movingSpinner", .size = sizeof(Mover), .update = mover_update,
                               .render = mover_render, .on_player = mover_on_player, .awake = mover_awake,
                               .collide_rect = mover_collide, .sm_riding = mover_riding, .sm_move = mover_sm_move,
                               .sm_destroy = mover_sm_destroy, .kind = KIND_PCOLLIDE | KIND_STATICMOVER};

static bool dust_room_of(const EData *d) {
  int a = g_session.area;
  return a == 3 || (a == 7 && !strncmp(level_room_name_of(d->room), "d-", 2));
}

static bool mover_new(const EData *d, bool rotate) {
  Ent *e = ent_new(&MOVER, d->x, d->y);
  if (!e) return true;
  Mover *m = ST(e, Mover);
  e->depth = -50;
  e->ctype = COL_LIST;
  e->cx = -8, e->cy = -6, e->cw = 16, e->ch = 12;
  m->rotate = rotate;
  m->dust = dust_room_of(d);
  m->moving = 1;
  m->nodes = 15;
  if (!m->dust) {
    spr_init(&m->spr, SB_templeBlade);
    spr_play(&m->spr, A_templeBlade_idle, true);
  }
  if (rotate) {
    m->center = ed_node(d, 0);
    m->clockwise = EAB(d, rotateSpinner, clockwise);
    float a = atan2f(e->y - m->center.y, e->x - m->center.x);   /* Calc.Angle(center, Position), wrapped */
    while (a > PI_F) a -= PI_F * 2;
    while (a <= -PI_F) a += PI_F * 2;
    m->percent = (a - -PI_F / 2) / (4.712389f - -PI_F / 2);   /* Calc.Percent */
    m->length = v2len(v2sub(v2(e->x, e->y), m->center));
    V2 v = angle_vec(rotate_angle(m), m->length);
    e->x = m->center.x + v.x, e->y = m->center.y + v.y;
  } else {
    m->start = v2(d->x, d->y);
    m->end = ed_node(d, 0);
    const char *sp = EAS(d, trackSpinner, speed);
    m->speed = !strcmp(sp, "Slow") ? 0 : !strcmp(sp, "Fast") ? 2 : 1;
    m->angle = atan2f(m->start.y - m->end.y, m->start.x - m->end.x);
    m->percent = EAB(d, trackSpinner, startCenter) ? 0.5f : 0;
    m->up = 1;
    track_pos(e);
    V2 dir = v2sub(m->end, m->start);
    float l = v2len(dir);
    m->eye = m->eye_target = l > 0 ? v2mul(dir, 1 / l) : v2(1, 0);
  }
  return true;
}

bool dust_create(const EData *d) {
  switch (d->type) {
    case ET_rotateSpinner: return mover_new(d, true);
    case ET_trackSpinner: return mover_new(d, false);
  }
  return false;
}
