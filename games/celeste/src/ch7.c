#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("Os")   /* not drawn every frame: smaller over faster */
#endif
/* The Summit (chapter 7): its checkpoints (with their confetti), the summit
 * gems and the gem manager, the clouds, the ascent from one part of the
 * mountain to the next (AscendManager, CS07_Ascend, HeightDisplay), the
 * summit (CS07_Ending), the credits that follow it (CS07_Credits), and Granny
 * at the end of the C-side. Line by line from the game's code. */
#include "badeline.h"

static V2 shake_vec(void) { return v2((float)(rndi(3) - 1), (float)(rndi(3) - 1)); }   /* Calc.Random.ShakeVector */
static void draw_centered(uint16_t tex, float x, float y, uint16_t tint, uint8_t alpha, float sx, float sy, float rot) {
  Tex t;
  if (tex == 0xFFFF || !tex_get(tex, &t)) return;
  gfx_tex_ex(tex, x, y, t.fw * t.scale / 2.f, t.fh * t.scale / 2.f, sx, sy, rot, tint, alpha, 0);
}
static Room *room(void) { return g_level.room; }
static void split_particles(Player *p) {   /* Player.CreateSplitParticles */
  particles_emit(PL_MID, &P_Player_P_Split, 16, player_center(p), v2(6, 6), P_Player_P_Split.direction);
}

/* ---------------------------------------------------------------- SummitCheckpoint and its confetti */
#define CONFETTI 30
typedef struct { V2 pos, speed; float timer, percent, approach; uint8_t color, duration; } Confetto;
typedef struct { Confetto p[CONFETTI]; } Confetti;
static const uint32_t CONFETTI_COLORS[3] = {0xFE2074, 0x205EFE, 0xCEFE20};
static void confetti_update(Ent *e) {
  Confetti *c = ST(e, Confetti);
  bool live = false;
  for (int i = 0; i < CONFETTI; i++) {
    Confetto *p = &c->p[i];
    p->pos = v2add(p->pos, v2mul(p->speed, DT));
    p->speed.x = approach(p->speed.x, 0, 80 * DT);
    p->speed.y = approach(p->speed.y, 20, 500 * DT);
    p->timer += DT;
    p->percent += DT / p->duration;
    if (p->speed.y > 0) p->approach = approach(p->approach, 5, DT * 16);
    live |= p->percent < 1;
  }
  if (!live) ent_remove(e);   /* (every one gone: alpha 0) */
}
static void confetti_render(Ent *e) {   /* particles/confetti (4 x 1) as a line, turned */
  Confetti *c = ST(e, Confetti);
  for (int i = 0; i < CONFETTI; i++) {
    Confetto *p = &c->p[i];
    float ang, alpha = clamped_map(p->percent, 0.9f, 1, 1, 0);
    V2 at = p->pos;
    if (p->speed.y < 0) ang = atan2f(p->speed.y, p->speed.x);
    else {
      ang = sinf(p->timer * 4);
      at = v2add(at, angle_vec(PI_F / 2 + ang, p->approach));
    }
    if (alpha <= 0) continue;
    V2 d = angle_vec(ang, 1.5f);
    gfx_line(at.x - d.x, at.y + 1 - d.y, at.x + d.x, at.y + 1 + d.y, 0, a8(alpha * 0.5f));
    gfx_line(at.x - d.x, at.y - d.y, at.x + d.x, at.y + d.y, rgb(CONFETTI_COLORS[p->color]), a8(alpha));
  }
}
static const EntClass CONFETTI_CLS = {.name = "confettiRenderer", .size = sizeof(Confetti), .update = confetti_update, .render = confetti_render};
static void confetti_new(V2 at) {
  Ent *e = ent_new(&CONFETTI_CLS, at.x, at.y);
  if (!e) return;
  e->depth = -10010;
  e->collidable = 0;
  Confetti *c = ST(e, Confetti);
  for (int i = 0; i < CONFETTI; i++) {
    Confetto *p = &c->p[i];
    p->pos = v2add(at, v2((float)(rndi(6) - 3), (float)(rndi(6) - 3)));
    p->color = (uint8_t)rndi(3);
    p->timer = rndf();
    p->duration = (uint8_t)(2 + rndi(2));
    p->speed = angle_vec(-PI_F / 2 + rnd_rangef(-0.5f, 0.5f), (float)(140 + rndi(80)));
  }
}

typedef struct { V2 respawn; uint8_t number, active; } SCheck;
static void scheck_flag(char *f, int n) {
  path2(f, "summit_checkpoint_", NULL, -1);
  char *o = f + strlen(f);
  if (n >= 10) *o++ = (char)('0' + n / 10);
  *o++ = (char)('0' + n % 10), *o = 0;
}
static void scheck_activate(Ent *e, bool hit) {
  SCheck *c = ST(e, SCheck);
  char f[24];
  c->active = 1;
  scheck_flag(f, c->number);
  level_set_flag(f, true);
  g_session.rx = (int32_t)c->respawn.x, g_session.ry = (int32_t)c->respawn.y, g_session.has_respawn = 1;
  if (!hit) return;
  g_session.dashes_at_level_start = g_session.dashes;   /* UpdateLevelStartDashes, HitCheckpoint */
  g_session.hit_checkpoint = 1;
  confetti_new(v2(e->x, e->y));   /* (the displacement burst is not drawn) */
}
static void scheck_awake(Ent *e) {
  Player *p = level_player();
  if (!ST(e, SCheck)->active && p && collide_ent_at(e, e->x, e->y, p->ent)) scheck_activate(e, false);
}
static void scheck_update(Ent *e) {
  Player *p = level_player();
  if (!ST(e, SCheck)->active && p && !p->dead && collide_ent_at(e, e->x, e->y, p->ent) && p->on_ground && p->speed.y >= 0)
    scheck_activate(e, true);
}
static void scheck_render(Ent *e) {
  SCheck *c = ST(e, SCheck);
  uint16_t base = c->active ? T_scenery_summitcheckpoints_base02
                  : fmodf(g_level.time_active, 0.5f) > 0.25f ? T_scenery_summitcheckpoints_base00 : T_scenery_summitcheckpoints_base01;
  uint16_t num = c->active ? T_scenery_summitcheckpoints_number00 : T_scenery_summitcheckpoints_numberbg00;
  Tex t;
  if (tex_get(base, &t)) gfx_tex(base, e->x - (t.fw / 2 + 1), e->y - t.fh / 2, 0, 0xFFFF, 255);
  gfx_tex((uint16_t)(num + c->number / 10 % 10), e->x - 4, e->y + 1, 0, 0xFFFF, 255);   /* justified (1, 0) and (0, 0) */
  gfx_tex((uint16_t)(num + c->number % 10), e->x, e->y + 1, 0, 0xFFFF, 255);
}
static const EntClass SCHECK = {.name = "summitcheckpoint", .size = sizeof(SCheck), .update = scheck_update, .render = scheck_render,
                                .awake = scheck_awake};
static void new_scheck(const EData *d) {
  Ent *e = ent_new(&SCHECK, d->x, d->y);
  if (!e) return;
  ent_box(e, 32, 32, -16, -8);
  e->depth = 8999;
  SCheck *c = ST(e, SCheck);
  c->number = (uint8_t)EA(d, summitcheckpoint, number);
  char f[24];
  scheck_flag(f, c->number);
  c->active = level_get_flag(f);
  c->respawn = level_closest_spawn(v2(d->x, d->y));
}

/* ---------------------------------------------------------------- SummitGem */
static const uint32_t GEM_COLORS[6] = {0x9EE9FF, 0x54BAFF, 0x90FF2D, 0xFFD300, 0xFF609D, 0xC5E1BA};
static uint16_t gem_tex(int gem, const char *what) {   /* collectables/summitgems/N/gem00 (14 frames) or .../bg */
  char p[40] = "collectables/summitgems/0/";
  p[24] = (char)('0' + gem);
  return res_tex_by_name(path2(p, p, what, -1));
}
typedef struct { float alpha; } BgFlash;
static void bgflash_update(Ent *e) {
  BgFlash *b = ST(e, BgFlash);
  if ((b->alpha = approach(b->alpha, 0, DT * 0.5f)) <= 0) ent_remove(e);
}
static void bgflash_render(Ent *e) { gfx_rect(g_level.cam.x - 10, g_level.cam.y - 10, 340, 200, 0, a8(ST(e, BgFlash)->alpha)); }
static const EntClass BGFLASH = {.name = "summitGemBgFlash", .size = sizeof(BgFlash), .update = bgflash_update, .render = bgflash_render};
typedef struct {
  Wiggler scale, move;
  V2 dir;
  float t, bounce_delay;
  uint32_t gid;
  uint16_t frame0;
  uint8_t gem, have, smash;
} Gem;
static void gem_on_player(Ent *e, Player *p) {
  Gem *g = ST(e, Gem);
  if (player_dash_attacking(p)) {
    g->smash = 1;   /* Add(new Coroutine(SmashRoutine(player, level))) */
    e->visible = 0, e->collidable = 0;
    p->stamina = 110;
    level_set_do_not_load(g->gid);
    g_session.summit_gems |= (uint8_t)(1 << g->gem);
    g_save.summit_gems |= (uint8_t)(1 << g->gem);   /* SaveData.RegisterSummitGem */
    level_shake(0.3f);
    level_freeze(0.1f);
    float ang = atan2f(p->speed.y, p->speed.x);
    particles_emit_c(PL_FG, &P_SummitGem_P_Shatter, 5, v2(e->x, e->y), v2(4, 4), GEM_COLORS[g->gem], ang - PI_F / 2);
    particles_emit_c(PL_FG, &P_SummitGem_P_Shatter, 5, v2(e->x, e->y), v2(4, 4), GEM_COLORS[g->gem], ang + PI_F / 2);
    for (int i = 0; i < 10; i++) absorb_orb_new(v2(e->x, e->y), p->ent);   /* (SlashFx is not drawn) */
    level_flash(0xFFFF, true);
    Ent *f = ent_new(&BGFLASH, 0, 0);
    if (f) f->depth = 10100, f->tags = TAG_PERSISTENT, f->collidable = 0, ST(f, BgFlash)->alpha = 1;
    g_time_rate = 0.5f;
    return;
  }
  player_point_bounce(p, e_center(e));
  wiggler_restart(&g->move);
  wiggler_restart(&g->scale);
  g->dir = v2sub(e_center(e), player_center(p));
  g->dir = v2len(g->dir) > 0 ? v2norm(g->dir) : v2(0, 1);
  if (g->bounce_delay <= 0) g->bounce_delay = 0.1f;
}
static void gem_update(Ent *e) {
  Gem *g = ST(e, Gem);
  if (g->smash) {   /* SmashRoutine: the time rate back to 1 */
    if (g_time_rate < 1 && (g_time_rate += RAW_DT * 0.5f) < 1) return;
    g_time_rate = 1;
    ent_remove(e);
    return;
  }
  g->t += DT;
  wiggler_update(&g->scale);
  wiggler_update(&g->move);
  g->bounce_delay -= DT;
}
static void gem_render(Ent *e) {
  Gem *g = ST(e, Gem);
  uint16_t t = (uint16_t)(g->frame0 + (int)(g->t / 0.08f) % 14);
  V2 off = v2mul(g->dir, g->move.value * -8);
  float s = 1 + g->scale.value * 0.3f;
  draw_centered(t, e->x + off.x, e->y + off.y, 0xFFFF, g->have ? 128 : 255, s, s, 0);
}
static const EntClass GEM = {.name = "summitgem", .size = sizeof(Gem), .update = gem_update, .render = gem_render,
                             .on_player = gem_on_player, .kind = KIND_PCOLLIDE};
static void new_gem(const EData *d) {
  uint32_t gid = level_entity_hash(d);
  Ent *e = ent_new(&GEM, d->x, d->y);
  if (!e) return;
  ent_box(e, 12, 12, -6, -6);
  Gem *g = ST(e, Gem);
  g->gid = gid;
  g->gem = (uint8_t)((int)EA(d, summitgem, gem) % 6);
  g->frame0 = gem_tex(g->gem, "gem00");
  g->have = g_save.summit_gems >> g->gem & 1;
  wiggler_init(&g->scale, 0.5f, 4);
  wiggler_init(&g->move, 0.8f, 2);
  g->move.start_zero = true;
}

/* ---------------------------------------------------------------- SummitGemManager */
typedef struct { V2 shake; float t, y, scale, bloom; uint16_t frame0, bg; uint8_t spin, gone; } MGem;
static void mgem_update(Ent *e) {
  MGem *g = ST(e, MGem);
  g->t += DT;
  if (g->spin && g->t >= 14 * 0.05f) g->spin = 0, g->t = 0;   /* "spin" goes back to "idle" */
}
static void mgem_render(Ent *e) {
  MGem *g = ST(e, MGem);
  draw_centered(g->bg, e->x, e->y, 0xFFFF, 255, 1, 1, 0);
  if (g->gone) return;
  draw_centered((uint16_t)(g->frame0 + (g->spin ? (int)(g->t / 0.05f) % 14 : 0)), e->x + g->shake.x, e->y + g->y + g->shake.y, 0xFFFF, 255,
                g->scale, g->scale, 0);
  bloom_add(e->x, e->y + g->y, g->bloom, 20);
}
static const EntClass MGEM = {.name = "summitGemManagerGem", .size = sizeof(MGem), .update = mgem_update, .render = mgem_render};
typedef struct { Co co; Ent *gems[6]; Ent *heart; V2 from; float p; uint8_t n, index, broken; } GemMgr;
static void gemmgr_update(Ent *e) {
  GemMgr *m = ST(e, GemMgr);
  Co *c = &m->co;
  Player *p;
  MGem *g;
  CO_BEGIN(c);
  if (g_session.heart) {
    for (int i = 0; i < m->n; i++) ST(m->gems[i], MGem)->gone = 1;
    CO_YIELD(c);
    return;
  }
  for (;;) {
    p = level_player();
    if (p && v2len(v2sub(v2(p->ent->x, p->ent->y), v2(e->x, e->y))) < 64) break;
    CO_YIELD(c);
  }
  CO_WAIT(c, 0.5f);
  for (m->index = 0; m->index < m->n; m->index++) {
    {
      bool has = (g_session.summit_gems >> m->index & 1) ||
                 (!(g_session.old_stats.flags & MS_HEART) && (g_save.summit_gems >> m->index & 1));
      if (!has) continue;
    }
    g = ST(m->gems[m->index], MGem);
    g->spin = 1, g->t = 0;
    while (ST(m->gems[m->index], MGem)->spin) {
      g = ST(m->gems[m->index], MGem);
      g->bloom = approach(g->bloom, 1, DT * 3);
      if (g->bloom > 0.5f) g->shake = shake_vec();
      g->y -= DT * 8;
      g->scale = 1 + g->bloom * 0.1f;
      CO_YIELD(c);
    }
    CO_WAIT(c, 0.2f);
    level_shake(0.3f);
    {
      Ent *ge = m->gems[m->index];
      for (int i = 0; i < 20; i++)
        particles_emit_c(PL_FG, &P_SummitGem_P_Shatter, 1, v2add(v2(ge->x, ge->y), v2((float)(rndi(16) - 8), (float)(rndi(16) - 8))),
                         v2(0, 0), GEM_COLORS[m->index], rndf() * 2 * PI_F);
      ST(ge, MGem)->gone = 1;
    }
    m->broken++;
    CO_WAIT(c, 0.25f);
  }
  if (m->broken < 6) {
    CO_YIELD(c);
    return;
  }
  for (int i = 0; i < g_nents; i++)   /* the HeartGem (another file's blackGem) */
    if (g_ents[i].cls && g_ents[i].dead != 1 && !strcmp(g_ents[i].cls->name, "blackGem")) m->heart = &g_ents[i];
  if (!m->heart) {
    CO_YIELD(c);
    return;
  }
  CO_WAIT(c, 0.1f);
  m->from = v2(m->heart->x, m->heart->y);
  for (m->p = 0; m->p < 1; m->p += DT) {
    if (!m->heart->cls || m->heart->dead == 1) break;
    {
      V2 at = v2lerp(m->from, v2(e->x, e->y - 16), ease_cube_out(m->p));
      m->heart->x = at.x, m->heart->y = at.y;
    }
    CO_YIELD(c);
  }
  CO_END(c);
}
static const EntClass GEMMGR = {.name = "summitGemManager", .size = sizeof(GemMgr), .update = gemmgr_update};
static void new_gemmgr(const EData *d) {
  Ent *e = ent_new(&GEMMGR, d->x, d->y);
  if (!e) return;
  e->depth = -10010;
  e->visible = e->collidable = 0;
  GemMgr *m = ST(e, GemMgr);
  for (int i = 0; i < d->nnodes && i < 6; i++) {
    V2 at = ed_node(d, i);
    Ent *ge = ent_new(&MGEM, at.x, at.y);
    if (!ge) break;
    ge->depth = -10010;
    ge->collidable = 0;
    MGem *g = ST(ge, MGem);
    g->frame0 = gem_tex(i, "gem00"), g->bg = gem_tex(i, "bg");
    g->scale = 1;
    m->gems[m->n++] = ge;
  }
}

/* ---------------------------------------------------------------- SummitCloud */
typedef struct { SineWave sine; float diff; uint8_t tex, flip; } SCloud;
static void scloud_update(Ent *e) { sine_update(&ST(e, SCloud)->sine); }
static void scloud_render(Ent *e) {
  SCloud *c = ST(e, SCloud);
  V2 mid = v2add(g_level.cam, v2(160, 90));
  V2 at = v2add(v2(e->x, e->y), v2mul(v2sub(v2(e->x + 64, e->y + 32), mid), 0.1f + c->diff));
  draw_centered(c->tex ? T_scenery_summitclouds_cloud01 : T_scenery_summitclouds_cloud00, at.x, at.y + c->sine.value * 8, 0xFFFF, 255,
                c->flip ? -1 : 1, 1, 0);
}
static const EntClass SCLOUD = {.name = "summitcloud", .size = sizeof(SCloud), .update = scloud_update, .render = scloud_render};
static void new_scloud(const EData *d) {
  Ent *e = ent_new(&SCLOUD, d->x, d->y);
  if (!e) return;
  e->depth = -10550;
  e->collidable = 0;
  SCloud *c = ST(e, SCloud);
  c->diff = rnd_rangef(0.1f, 0.2f);
  c->tex = (uint8_t)rndi(2);   /* GetAtlasSubtextures("scenery/summitclouds/cloud"): 00, 01 */
  c->flip = (uint8_t)rndi(2);
  c->sine.freq = rnd_rangef(0.05f, 0.15f);
  sine_randomize(&c->sine);
}

/* ---------------------------------------------------------------- SpeedRing */
typedef struct { V2 normal; float lerp; } Ring;
static void ring_update(Ent *e) {
  Ring *r = ST(e, Ring);
  r->lerp += 3 * DT;
  e->x += r->normal.x * 10 * DT, e->y += r->normal.y * 10 * DT;
  if (r->lerp >= 1) ent_remove(e);
}
static V2 ring_at(const Ring *r, float a, float max) {
  V2 v = angle_vec(a, 1);
  return v2mul(v, lerpf(max, max * 0.5f, fabsf(v.x * r->normal.x + v.y * r->normal.y)));
}
static void ring_render(Ent *e) {   /* DrawRing: 16 lines, white * Lerp(0.6, 0, lerp) */
  Ring *r = ST(e, Ring);
  float max = lerpf(4, 14, r->lerp);
  uint8_t a = a8(lerpf(0.6f, 0, r->lerp));
  V2 prev = ring_at(r, 0, max);
  for (int i = 1; i <= 8; i++) {
    V2 v = ring_at(r, i * PI_F / 8, max);
    gfx_line(e->x + prev.x, e->y + prev.y, e->x + v.x, e->y + v.y, 0xFFFF, a);
    gfx_line(e->x - prev.x, e->y - prev.y, e->x - v.x, e->y - v.y, 0xFFFF, a);
    prev = v;
  }
}
static const EntClass RING = {.name = "speedRing", .size = sizeof(Ring), .update = ring_update, .render = ring_render};
static void ring_new(V2 at, float angle) {
  Ent *e = ent_new(&RING, at.x, at.y);
  if (e) e->collidable = 0, ST(e, Ring)->normal = angle_vec(angle, 1);
}

/* ---------------------------------------------------------------- HeightDisplay */
typedef struct {
  Co co;
  float approach, ease, pulse, cam_p;
  int16_t height;
  int8_t index;
  uint8_t room, easing_cam, cam_up, has_text;
} Height;
static void height_update(Ent *e) {
  Height *h = ST(e, Height);
  Player *p = level_player();
  Room *rm = room();
  if (h->index >= 0 && h->ease > 0) {
    float d = h->height - h->approach;
    if (d > 100) h->approach += 1000 * DT;
    else if (d > 25) h->approach += 200 * DT;
    else if (d > 5) h->approach += 50 * DT;
    else if (d > 0) h->approach += 10 * DT;
    else h->approach = h->height;
  }
  if (!h->easing_cam) g_level.cam.y = (float)(rm->y + rm->h - 180 + 64);
  if (h->cam_up) {   /* CameraUp */
    if (h->cam_p < 1) {
      g_level.cam.y = rm->y + rm->h - 180 + 64 * (1 - ease_cube_out(h->cam_p));
      h->cam_p += DT * 1.5f;
    } else
      h->cam_up = 0;
  }
  Co *c = &h->co;
  CO_BEGIN(c);
  while (!p || rm->index == h->room) {
    CO_YIELD(c);
    p = level_player();
    rm = room();
  }
  h->easing_cam = 0;   /* (StepAudioProgression: no music) */
  CO_WAIT(c, 0.1f);
  h->easing_cam = 1, h->cam_up = 1, h->cam_p = 0;
  while ((h->ease += DT / 0.15f) < 1) CO_YIELD(c);
  while (h->approach < h->height && !actor_on_ground(level_player_ent(), 1)) CO_YIELD(c);
  h->approach = h->height;
  h->pulse = 1;
  while ((h->pulse -= DT * 4) > 0) CO_YIELD(c);
  h->pulse = 0;
  CO_WAIT(c, 1);
  while ((h->ease -= DT / 0.15f) > 0) CO_YIELD(c);
  ent_remove(e);
  CO_END(c);
}
static void height_render(Ent *e) {   /* "{X} M": the number, then the rest */
  Height *h = ST(e, Height);
  if (g_level.paused || h->index < 0 || h->ease <= 0 || !h->has_text) return;
  char right[24], num[8], key[16] = "CH7_HEIGHT_0";
  key[11] = (char)('0' + h->index);
  int nr = dialog_clean(key, right, sizeof right);   /* ("{X} M": the number's place dropped) */
  num[0] = 0;
  {
    char tmp[8], *o = num;
    int v = (int)h->approach, k = 0;
    do tmp[k++] = (char)('0' + v % 10), v /= 10;
    while (v);
    while (k) *o++ = tmp[--k];
    *o = 0;
  }
  char full[8];
  int hv = h->height, k = 0;
  do full[k++] = '0', hv /= 10;
  while (hv);
  float k_ = 1.2f + h->pulse * 0.2f, e_ = ease_sine_inout(h->ease);
  float wn = font_measure(full, k, FONT_S), w = wn + font_measure(right, nr, FONT_S), lh = font_line_height(FONT_S);
  float bw = w * k_ + 64 / 6.f, bh = (lh * k_ + 32 / 6.f) * e_;
  gfx_hud(true);
  gfx_rect(160 - bw / 2, 90 - bh / 2, bw, bh, 0, 255);
  uint8_t a = a8(e_);
  float x = 160 - w / 2;
  font_draw(right, nr, x + wn, 90 - lh / 2, FONT_S, 0xFFFF, a);
  font_draw_justified(num, x + wn / 2, 90, 0.5f, 0.5f, FONT_S, 0xFFFF, a);
  gfx_hud(false);
}
static const EntClass HEIGHT = {.name = "heightDisplay", .size = sizeof(Height), .update = height_update, .render = height_render};
static void height_new(int index) {
  Ent *e = ent_new(&HEIGHT, 0, 0);
  if (!e) return;
  e->tags = TAG_HUD | TAG_PERSISTENT;
  e->collidable = 0;
  Height *h = ST(e, Height);
  h->index = (int8_t)index;
  h->easing_cam = 1;
  h->room = (uint8_t)room()->index;
  char key[16] = "CH7_HEIGHT_0";
  key[11] = (char)('0' + (index < 0 ? 0 : index));
  if (index >= 0 && dialog_has(key)) {
    h->has_text = 1;
    h->height = (int16_t)((index + 1) * 500);
    h->approach = (float)(index * 500);
  }
}

/* ---------------------------------------------------------------- CS07_Ascend */
typedef struct {
  Cutscene cs;
  Ent *badeline, *tb;
  V2 origin, center;
  float dist, timer;
  int8_t index;
  uint8_t spinning, spin_on, dark;
  char key[24];
} Ascend7;
static void ascend_spin(Ascend7 *s) {   /* SpinCharacters, an update of it */
  Player *p = &g_player;
  s->dist = approach(s->dist, s->spinning ? 1 : 0, DT * 4);
  int num = (int)(s->timer / (PI_F * 2) * 14 + 10);
  float si = sinf(s->timer), co = cosf(s->timer), r = ease_cube_out(s->dist) * 32;
  sprite_set_frame(&p->spr, num);
  if (s->badeline) bd_set_frame(s->badeline, num + 7);
  p->ent->x = s->center.x - si * r, p->ent->y = s->center.y - co * s->dist * 8;
  if (s->badeline) s->badeline->x = s->center.x + si * r, s->badeline->y = s->center.y + co * s->dist * 8;
  s->timer -= DT * 2;
  if (s->timer <= 0) s->timer += PI_F * 2;
  if (!s->spinning && s->dist <= 0) s->spin_on = 0;
}
static void ascend7_update(Ent *e) {
  Ascend7 *s = ST(e, Ascend7);
  Player *p = level_player();
  Co *c = &s->cs.co;
  if (s->spin_on == 1) ascend_spin(s);
  else if (s->spin_on == 2) s->spin_on = 1;
  CO_BEGIN(c);
  while (!level_player()) CO_YIELD(c);
  p = level_player();
  s->origin = v2(p->ent->x, p->ent->y);
  split_particles(p);   /* (the displacement burst is not drawn) */
  p->dashes = 1;
  p->facing = 1;
  s->badeline = bd_new(s->origin);
  if (s->badeline) BD(s->badeline)->auto_enabled = 0;
  s->spinning = 1;
  s->center = s->origin, s->timer = PI_F / 2, s->dist = 0;   /* SpinCharacters */
  spr_play(&p->spr, A_player_spin, false);
  if (s->badeline) spr_play(&BD(s->badeline)->spr, A_badeline_spin, false), BD(s->badeline)->spr.sx = 1;
  s->spin_on = 2;
  CO_SAY(c, s->tb, s->key, NULL, NULL);
  s->spinning = 0;
  CO_WAIT(c, 0.25f);
  if (s->badeline) ent_remove(s->badeline), s->badeline = NULL;
  p->dashes = 2;
  split_particles(p);
  cutscene_end(e);
  return;
  CO_END(c);
}
static void ascend7_end(Ent *e, bool skipped) {
  Ascend7 *s = ST(e, Ascend7);
  (void)skipped;
  if (s->badeline) ent_remove(s->badeline);
  Player *p = level_player();
  if (p && s->origin.x != 0) {
    p->dashes = 2;
    p->ent->x = s->origin.x, p->ent->y = s->origin.y;
  }
  if (!s->dark) height_new(s->index);
}
static const EntClass ASCEND7 = {.name = "CS07_Ascend", .size = sizeof(Ascend7), .update = ascend7_update};

/* ---------------------------------------------------------------- AscendManager (SummitBackgroundManager) */
#define STREAKS 80
typedef struct { float y; uint16_t speed, xic; } Streak;   /* xic: x | texture << 10 | color << 12 */
enum { OFF_streaks = 0, END_streaks = OFF_streaks + (int)sizeof(Streak[STREAKS]) };
#define streaks (*(Streak(*)[STREAKS])(void *)(g_chram[7] + OFF_streaks))
typedef struct {
  Co co;
  Ent *cs, *fader, *streaks_e, *clouds_e;
  V2 from;
  float fade, p, scroll;
  int8_t index;
  uint8_t intro, dark, out_top;
  char cutscene[24];
} AscendM;
static const EntClass ASCENDM;
/* Streaks: 80 slices falling past, as bars of their color (white and e69ecb, or dark blues) */
static const uint8_t SLICE_W[3] = {9, 3, 3};
static float ascend_fade(Ent *m) { return m && m->cls == &ASCENDM ? ST(m, AscendM)->fade : 1; }
typedef struct { Ent *manager; float alpha; } Streaks;
static void streaks_update(Ent *e) {
  (void)e;
  for (int i = 0; i < STREAKS; i++) streaks[i].y += streaks[i].speed * DT;
}
static void streaks_strip(uint16_t *strip, int sy0, int sy1, void *ctx) {
  Ent *e = ctx;
  Streaks *s = ST(e, Streaks);
  AscendM *m = s->manager && s->manager->cls == &ASCENDM ? ST(s->manager, AscendM) : NULL;
  float a = ease_sine_inout(ascend_fade(s->manager) * s->alpha);
  int al = (int)(a * 256);
  uint16_t cols[2];
  cols[0] = m && m->dark ? rgb(0x041B44) : 0xFFFF;
  cols[1] = m && m->dark ? rgb(0x011230) : rgb(0xE69ECB);
  if (al <= 0) return;
  cols[0] = scale565(cols[0], al), cols[1] = scale565(cols[1], al);
  for (int i = 0; i < STREAKS; i++) {
    Streak *k = &streaks[i];
    float t = clamped_map(k->speed, 600, 2000, 0, 1), big = 1 + 3 * t;
    int kx = k->xic & 1023, idx = k->xic >> 10 & 3, col = k->xic >> 12 & 1;
    float sx = (1 - 0.75f * t) * big, sy = (1 + t) * big;
    float y = -128 + fmodf(fmodf(k->y, 436) + 436, 436);
    int w = (int)(SLICE_W[idx] * sx + 0.5f), h = (int)(110 * sy);
    int x0 = (int)(kx - w / 2.f), y0 = (int)(y - h / 2.f), y1 = y0 + h;
    if (y0 < sy0) y0 = sy0;
    if (y1 > sy1) y1 = sy1;
    for (int yy = y0; yy < y1; yy++)
      for (int xx = x0 < 0 ? 0 : x0; xx < x0 + w && xx < VIEW_W; xx++) {
        uint16_t *d = strip + (yy - sy0) * VIEW_W + xx;
        *d = blend565(*d, cols[col], al);
      }
  }
  for (int yy = sy0; yy < sy1; yy++)   /* the bars at the sides */
    for (int xx = 0; xx < VIEW_W; xx++)
      if (xx < 16 || xx >= VIEW_W - 16) {
        uint16_t *d = strip + (yy - sy0) * VIEW_W + xx;
        *d = blend565(*d, cols[0], al);
      }
}
static void streaks_render(Ent *e) { gfx_custom(streaks_strip, e, 0, VIEW_H); }
static const EntClass STREAKS_CLS = {.name = "ascendStreaks", .size = sizeof(Streaks), .update = streaks_update, .render = streaks_render};
/* Clouds: ten launch clouds falling past */
typedef struct { Ent *manager; float alpha; struct { float x, y, speed; uint8_t idx; } p[10]; } Clouds;
static void clouds_update(Ent *e) {
  Clouds *c = ST(e, Clouds);
  for (int i = 0; i < 10; i++) c->p[i].y += c->p[i].speed * DT;
}
static void clouds_render(Ent *e) {
  Clouds *c = ST(e, Clouds);
  AscendM *m = c->manager && c->manager->cls == &ASCENDM ? ST(c->manager, AscendM) : NULL;
  float a = ascend_fade(c->manager) * c->alpha;
  uint16_t col = rgb(m && m->dark ? 0x082644 : 0xB64A86);
  for (int i = 0; i < 10; i++) {
    float y = -360 + fmodf(fmodf(c->p[i].y, 900) + 900, 900);
    draw_centered(c->p[i].idx ? T_scenery_launch_cloud01 : T_scenery_launch_cloud00, g_level.cam.x + c->p[i].x, g_level.cam.y + y, col,
                  a8(a), 1, 1, 0);
  }
}
static const EntClass CLOUDS_CLS = {.name = "ascendClouds", .size = sizeof(Clouds), .update = clouds_update, .render = clouds_render};
typedef struct { Ent *manager; float fade; uint8_t dark; } AFader;
static void afader_render(Ent *e) {
  AFader *f = ST(e, AFader);
  if (f->fade > 0) gfx_rect(g_level.cam.x - 10, g_level.cam.y - 10, 340, 200, f->dark ? 0 : 0xFFFF, a8(f->fade));
}
static const EntClass AFADER = {.name = "ascendFader", .size = sizeof(AFader), .render = afader_render};

static bool ascend_fade_to(AscendM *m, float target, float duration) {   /* FadeTo */
  m->fade = approach(m->fade, target, DT / duration);
  return m->fade != target;   /* (FadeSnapTo: the snow, the bloom and the parallax are not changed) */
}
static void ascendm_flag(const char *pre, int index, bool on) {
  char f[16];
  path2(f, pre, NULL, -1);
  char *o = f + strlen(f);
  if (index < 0) *o++ = '-', index = -index;
  *o++ = (char)('0' + index % 10), *o = 0;
  level_set_flag(f, on);
}
static void ascendm_update(Ent *e) {
  AscendM *m = ST(e, AscendM);
  Player *p = level_player();
  m->scroll += DT * 240;
  Co *c = &m->co;
  CO_BEGIN(c);
  while (!p || p->ent->y > e->y) {
    CO_YIELD(c);
    p = level_player();
  }
  if (m->index == 9) CO_WAIT(c, 1.6f);
  m->streaks_e = ent_new(&STREAKS_CLS, 0, 0);
  if (m->streaks_e) {
    m->streaks_e->depth = 20, m->streaks_e->collidable = 0;
    ST(m->streaks_e, Streaks)->manager = e, ST(m->streaks_e, Streaks)->alpha = 1;
    for (int i = 0; i < STREAKS; i++) {
      Streak *k = &streaks[i];
      float x = 160 + rnd_rangef(24, 144) * (rndi(2) ? 1 : -1);
      k->y = rndf() * 436;
      k->speed = (uint16_t)(clamped_map(fabsf(x - 160), 0, 160, 0.25f, 1) * rnd_rangef(600, 2000));
      k->xic = (uint16_t)((int)x | rndi(3) << 10 | rndi(2) << 12);
    }
  }
  if (!m->dark) {
    m->clouds_e = ent_new(&CLOUDS_CLS, 0, 0);
    if (m->clouds_e) {
      m->clouds_e->depth = -1000000, m->clouds_e->collidable = 0;
      Clouds *cl = ST(m->clouds_e, Clouds);
      cl->manager = e, cl->alpha = 1;
      for (int i = 0; i < 10; i++) cl->p[i].x = rndf() * 320, cl->p[i].y = rndf() * 900, cl->p[i].speed = (float)(400 + rndi(400)), cl->p[i].idx = (uint8_t)rndi(2);
    }
  }
  ascendm_flag("beginswap_", m->index, true);
  p = level_player();
  spr_play(&p->spr, A_player_launch, false);
  p->speed = v2(0, 0);
  player_set_state(p, ST_DUMMY);
  p->dummy_gravity = false;
  p->dummy_auto_animate = false;
  if (m->intro) {
    m->fade = 1;
    g_level.cam = v2sub(player_center(p), v2(160, 90));
    CO_WAIT(c, 2.3f);
  } else {
    while (ascend_fade_to(m, 1, m->dark ? 2 : 0.8f)) CO_YIELD(c);
    if (m->cutscene[0]) {
      CO_WAIT(c, 0.25f);
      m->cs = scene_new(&ASCEND7, ascend7_end, true, false);
      if (m->cs) {
        Ascend7 *s = ST(m->cs, Ascend7);
        s->index = m->index, s->dark = m->dark;
        memcpy(s->key, m->cutscene, sizeof s->key);
      }
      CO_YIELD(c);
      while (m->cs && m->cs->cls == &ASCEND7 && m->cs->dead != 1) CO_YIELD(c);
    } else
      CO_WAIT(c, 0.5f);
  }
  g_level.can_retry = false;
  p = level_player();
  spr_play(&p->spr, A_player_launch, false);
  CO_WAIT(c, 0.25f);
  m->from = v2(level_player_ent()->x, level_player_ent()->y);
  for (m->p = 0; m->p < 1; m->p += DT / 1) {
    {
      Ent *pe = level_player_ent();
      V2 at = v2add(v2lerp(m->from, v2add(m->from, v2(0, 60)), ease_cube_inout(m->p)), shake_vec());
      pe->x = at.x, pe->y = at.y;
    }
    CO_YIELD(c);
  }
  m->fader = ent_new(&AFADER, 0, 0);
  if (m->fader) m->fader->depth = -1000010, m->fader->collidable = 0, ST(m->fader, AFader)->manager = e, ST(m->fader, AFader)->dark = m->dark;
  m->from = v2(level_player_ent()->x, level_player_ent()->y);
  for (m->p = 0; m->p < 1; m->p += DT / 0.5f) {
    {
      Ent *pe = level_player_ent();
      float y = pe->y;
      V2 at = v2lerp(m->from, v2add(m->from, v2(0, -160)), ease_sine_in(m->p));
      pe->x = at.x, pe->y = at.y;
      if (m->p == 0 || floorf(pe->y / 16) != floorf(y / 16)) ring_new(player_center(&g_player), -PI_F / 2);   /* Calc.OnInterval */
      if (m->fader) ST(m->fader, AFader)->fade = m->p >= 0.5f ? (m->p - 0.5f) * 2 : 0;
    }
    CO_YIELD(c);
  }
  g_level.can_retry = true;
  m->out_top = 1;
  {
    Player *pl = &g_player;
    pl->ent->y = (float)room()->y;
    pl->summit_target_x = pl->ent->x;   /* SummitLaunch */
    player_set_state(pl, ST_SUMMITLAUNCH);
    pl->dummy_gravity = true;
    pl->dummy_auto_animate = true;
  }
  ascendm_flag("bgswap_", m->index, true);
  g_level.tr_duration = 0.05f;   /* NextTransitionDuration */
  if (m->intro) height_new(-1);
  CO_END(c);
}
static void ascendm_render(Ent *e) {
  AscendM *m = ST(e, AscendM);
  if (m->fade > 0) gfx_rect(g_level.cam.x - 10, g_level.cam.y - 10, 340, 200, m->dark ? 0 : rgb(0x75A0AB), a8(m->fade));
}
static void ascendm_removed(Ent *e) {
  AscendM *m = ST(e, AscendM);
  m->fade = 0;
  ascendm_flag("bgswap_", m->index, false);
  ascendm_flag("beginswap_", m->index, false);
  if (!m->out_top) return;
  /* the next part's wipe in (ScreenWipe.WipeColor white for the light ones: the wipes here are black) */
  static const uint8_t NEXT[6] = {WIPE_ANGLED, WIPE_DREAM, WIPE_KEYDOOR, WIPE_WIND, WIPE_DROP, WIPE_MOUNTAIN};
  if (m->intro) wipe_start(WIPE_MOUNTAIN, true, NULL);
  else if (m->index >= 0 && m->index < 6) wipe_start(NEXT[m->index], true, NULL);
  else if (m->index >= 9) wipe_start(WIPE_STARFIELD, true, NULL);
}
static const EntClass ASCENDM = {.name = "SummitBackgroundManager", .size = sizeof(AscendM), .update = ascendm_update,
                                 .render = ascendm_render, .removed = ascendm_removed};
static void new_ascendm(const EData *d) {
  Ent *e = ent_new(&ASCENDM, d->x, d->y);
  if (!e) return;
  e->tags = TAG_TRANSITION_UPDATE;
  e->depth = 8900;
  e->collidable = 0;
  AscendM *m = ST(e, AscendM);
  m->index = (int8_t)EA(d, SummitBackgroundManager, index);
  m->intro = EAB(d, SummitBackgroundManager, intro_launch);
  m->dark = EAB(d, SummitBackgroundManager, dark);
  strncpy(m->cutscene, EAS(d, SummitBackgroundManager, cutscene), sizeof m->cutscene - 1);
}

/* ---------------------------------------------------------------- CS07_Ending (the summit) */
static void credits_begin(void);
typedef struct {
  Cutscene cs;
  Ent *badeline, *tb;
  Walk walk;
  Step cam, cam2;
  V2 target, cam2_to;
  Co ev;
  int8_t ev_i;
  uint8_t cam_on, cam2_on;
} Ending7;
static bool ending7_nb(Ending7 *s, Co *c, int i) {
  Player *p = &g_player;
  switch (i) {
    case 0:   /* WaitABit */
      NB_BEGIN(c);
      NB_WAIT(c, 3);
      NB_END(c);
    case 1:   /* SitDown */
      NB_BEGIN(c);
      NB_WAIT(c, 0.5f);
      p->dummy_auto_animate = true;
      s->walk = walk_to(p->ent->x + 16);
      s->walk.mult = 0.25f;
      NB_NEST(c, player_walk(p, &s->walk));
      NB_WAIT(c, 0.1f);
      p->dummy_auto_animate = false;
      spr_play(&p->spr, A_player_sitDown, false);
      NB_WAIT(c, 1);
      NB_END(c);
    default:   /* BadelineApproaches */
      NB_BEGIN(c);
      NB_WAIT(c, 0.5f);
      BD(s->badeline)->spr.sx = -1;
      NB_WAIT(c, 1);
      BD(s->badeline)->spr.sx = 1;
      NB_WAIT(c, 1);
      step_reset(&s->cam2);
      s->cam2_on = 2;   /* CameraTo(camera + (88, 0), 6): a coroutine of its own */
      s->cam2_to = v2add(g_level.cam, v2(88, 0));
      BD(s->badeline)->float_speed = 40;
      bd_float_to(s->badeline, v2(p->ent->x - 10, p->ent->y - 4), 0, true, false, false);
      NB_AWAIT(c, &BD(s->badeline)->job);
      NB_WAIT(c, 0.5f);
      NB_END(c);
  }
}
static bool ending7_ev(void *ctx, int i) {
  Ending7 *s = ST((Ent *)ctx, Ending7);
  return !ending7_nb(s, ev_co(&s->ev, &s->ev_i, i), i);
}
static void ending7_update(Ent *e) {
  Ending7 *s = ST(e, Ending7);
  Player *p = &g_player;
  Co *c = &s->cs.co;
  CO_BEGIN(c);
  player_set_state(p, ST_DUMMY);
  CO_WALK(c, s->walk, walk_to(s->target.x));
  CO_WAIT(c, 0.25f);
  step_reset(&s->cam);
  s->cam_on = 2;   /* CameraTo(target + (-160, -130), 3) */
  p->facing = 1;
  CO_WAIT(c, 1);
  spr_play(&p->spr, A_player_idle, false);
  p->dummy_auto_animate = false;
  p->dashes = p->inventory_dashes = 1;
  g_session.inv_dashes = 1;
  s->badeline = bd_new(player_center(p));
  split_particles(p);
  if (!s->badeline) {
    cutscene_end(e);
    return;
  }
  BD(s->badeline)->spr.sx = 1;
  bd_float_to(s->badeline, v2add(s->target, v2(-10, -30)), 1, false, false, false);
  CO_AWAIT(c, &BD(s->badeline)->job);
  CO_WAIT(c, 0.5f);
  s->ev_i = -1;
  CO_SAY(c, s->tb, "CH7_ENDING", ending7_ev, e);
  CO_WAIT(c, 1);
  cutscene_end(e);
  return;
  CO_END(c);
}
static void ending7_tick(Ent *e) {
  Ending7 *s = ST(e, Ending7);
  ending7_update(e);
  if (e->dead == 1) return;
  if (s->cam_on == 2) s->cam_on = 1;
  else if (s->cam_on && !camera_to(&s->cam, v2add(s->target, v2(-160, -130)), 3, ease_cube_inout)) s->cam_on = 0;
  if (s->cam2_on == 2) s->cam2_on = 1;
  else if (s->cam2_on && !camera_to(&s->cam2, s->cam2_to, 6, ease_cube_inout)) s->cam2_on = 0;
}
static void ending7_wiped(void) {
  if (g_session.mode == M_A) credits_begin();   /* AreaComplete, then the credits (in this chapter's credits rooms) */
  else level_complete_area(false, true);
}
static void ending7_end(Ent *e, bool skipped) {   /* Level.CompleteArea(spotlightWipe: false), Duration 2, EndTimer 1 */
  (void)e, (void)skipped;
  g_level.pause_lock = true;
  if (g_level.skipping_cutscene) {
    ending7_wiped();
    return;
  }
  wipe_start(WIPE_FADE, false, ending7_wiped);
  g_wipe.duration = 2, g_wipe.end_timer = 1;
}
static const EntClass ENDING7 = {.name = "CS07_Ending", .size = sizeof(Ending7), .update = ending7_tick};
typedef struct { V2 target; uint8_t triggered; } Summit;
static void summit_enter(Ent *e, Player *p) {
  Summit *s = ST(e, Summit);
  (void)p;
  if (s->triggered) return;
  s->triggered = 1;
  Ent *cs = scene_new(&ENDING7, ending7_end, false, true);
  if (!cs) return;
  level_register_complete();   /* OnBegin: RegisterAreaComplete */
  ST(cs, Ending7)->target = s->target;
}
static const EntClass SUMMITTRIG = {.name = "eventTrigger", .size = sizeof(Summit), .kind = KIND_TRIGGER,
                                    .more = &(const EntMore){.on_enter = summit_enter}};

/* ---------------------------------------------------------------- NPC07X_Granny_Ending */
typedef struct {
  Cutscene cs;
  Ent *granny, *tb;
  Walk walk;
  Step step;
  Co ev;
  int8_t ev_i;
} Granny7;
static bool granny7_ev(void *ctx, int i) {   /* StartLaughing, StopLaughing (then a yield) */
  Granny7 *s = ST((Ent *)ctx, Granny7);
  Co *c = ev_co(&s->ev, &s->ev_i, i);
  if (!c->co) {
    spr_play(&NPC_(s->granny)->spr, i == 0 ? A_granny_laugh : A_granny_idle, false);
    c->co = 1;
    return false;
  }
  return true;
}
static void granny7_update(Ent *e) {
  Granny7 *s = ST(e, Granny7);
  Player *p = &g_player;
  Npc *n = NPC_(s->granny);
  Co *c = &s->cs.co;
  CO_BEGIN(c);
  player_set_state(p, ST_DUMMY);
  p->force_camera_update = true;
  while (!actor_on_ground(p->ent, 1)) CO_YIELD(c);
  CO_WALK(c, s->walk, walk_exact((int)s->granny->x - 16));
  p->facing = 1;
  if (n->i[0] < 2) {
    CO_WAIT(c, 0.5f);
    CO_ZOOM_TO(c, &s->step, v2(s->granny->x - g_level.cam.x, s->granny->y - g_level.cam.y - 32), 2, 0.5f);
    s->ev_i = -1;
    CO_SAY(c, s->tb, n->i[0] == 0 ? "CH7_CSIDE_OLDLADY" : "CH7_CSIDE_OLDLADY_B", granny7_ev, e);
    if (n->i[0] == 1) n->talk.enabled = false;
  }
  CO_ZOOM_BACK(c, &s->step, 0.5f);
  cutscene_end(e);
  return;
  CO_END(c);
}
static void granny7_end(Ent *e, bool skipped) {   /* EndTalking */
  Granny7 *s = ST(e, Granny7);
  (void)skipped;
  player_set_state(&g_player, ST_NORMAL);
  g_player.force_camera_update = false;
  NPC_(s->granny)->i[0]++;
  spr_play(&NPC_(s->granny)->spr, A_granny_idle, false);
}
static const EntClass GRANNY7 = {.name = "npc07xGrannyTalk", .size = sizeof(Granny7), .update = granny7_update};
static void granny7_talk(Ent *e) {
  Ent *cs = scene_new(&GRANNY7, granny7_end, true, false);
  if (cs) ST(cs, Granny7)->granny = e;
}
static void new_granny7(const EData *d) {
  Ent *e = npc_new(d->x, d->y, SB_granny, A_granny_idle, NULL);
  if (!e) return;
  Npc *n = NPC_(e);
  n->which = NPC_GRANNY07X;
  n->spr.sx = -1;
  n->move_anim = A_granny_walk, n->maxspeed = 40;
  npc_talker(e, -20, -8, 40, 8, v2(0, -24), granny7_talk);
  npc_haha(e);
}

/* ---------------------------------------------------------------- CS07_Credits */
/* After the summit, scenes of the climb play with Madeline walking left and Badeline along, while the credits
 * roll. The game's own staff credits are not shown: in their place, what this is and who made it. */
static const char *const CREDITS_TEXT[6] = {"Inspired by Celeste,", "not affiliated with", "Maddy Makes Games", "",
                                            "Made by Mason Chen", "as part of NumPlay"};
static float linear(float t) { return t; }
/* the player's animations Badeline's sprite has too (ChaserState.Animation, Sprite.Has) */
static const uint8_t BL_ANIMS[][2] = {
    {A_player_idle, A_badeline_idle}, {A_player_idleA, A_badeline_idleA}, {A_player_idleB, A_badeline_idleB},
    {A_player_idleC, A_badeline_idleC}, {A_player_lookUp, A_badeline_lookUp}, {A_player_walk, A_badeline_walk},
    {A_player_push, A_badeline_push}, {A_player_runSlow, A_badeline_runSlow}, {A_player_runFast, A_badeline_runFast},
    {A_player_runStumble, A_badeline_runStumble}, {A_player_dash, A_badeline_dash}, {A_player_slide, A_badeline_slide},
    {A_player_jumpSlow, A_badeline_jumpSlow}, {A_player_jumpFast, A_badeline_jumpFast}, {A_player_fallSlow, A_badeline_fallSlow},
    {A_player_fallFast, A_badeline_fallFast}, {A_player_tired, A_badeline_tired}, {A_player_wallslide, A_badeline_wallslide},
    {A_player_climbup, A_badeline_climbup}, {A_player_duck, A_badeline_duck}, {A_player_edge, A_badeline_edge},
    {A_player_sleep, A_badeline_sleep}, {A_player_faint, A_badeline_faint}, {A_player_fainted, A_badeline_fainted},
    {A_player_flip, A_badeline_flip}, {A_player_skid, A_badeline_skid}, {A_player_dangling, A_badeline_dangling},
    {A_player_spin, A_badeline_spin}, {A_player_hug, A_badeline_hug}};
#define CHASE 32
typedef struct { float x, y; uint32_t frame; uint8_t anim, left, ground, pad; } Chase;
enum { EVT_NONE, EVT_WAITJUMPDASH, EVT_WAITJUMPDOUBLEDASH, EVT_CLIMBDOWN, EVT_WAIT, EVT_BADELINEOFFSET, EVT_OSHIRO };
typedef struct { Ent *e; float angle, dist, p; uint8_t remove_at_end, on; } Around;
typedef struct {
  Co co, ev;
  Step cam;
  Walk walk;
  V2 bl_from, cam_target, osh_target, from, to, dusty_off, dusty_start, override;
  float fade, bg_fade, fade_target, walk_offset, bl_approach, p, high_player, high_baddy, scroll, bottom_timer, dusty_t;
  float cam_dur, cam_delay, other_vy;
  Ent *bl, *other, *oshiro, *bird, *dust[3];
  Around around[3];
  float (*cam_ease)(float);
  Chase chase[CHASE];
  int chase_n, chase_head;
  uint8_t active, room_seen, event, auto_walk, auto_cam, bl_float, bl_walk, fading, cam_on, bl_appr, dusty_on, osh_moving;
  uint8_t ev_on, wait_par, dashing, high, broom, post, need_setup;
} Credits;
enum { OFF_credits = ((END_streaks + 7) & ~7), END_credits = OFF_credits + (int)sizeof(Credits) };
#define cr (*(Credits *)(void *)(g_chram[7] + OFF_credits))
static bool have_ch7(void) { return g_level.ch && (rd16(g_level.ch + CH_FILES) >> 7 & 1); }

static void credits_begin(void) {   /* AreaComplete: the session goes on in credits-summit, with CS07_Credits */
  memset(&cr, 0, sizeof cr);
  cr.active = 1;
  cr.fade = 1;
  g_session.has_respawn = 0;
  level_goto("credits-summit");
}
/* the Fill backdrop: black over the background */
static void fill_render(Ent *e) {
  (void)e;
  if (cr.bg_fade > 0) gfx_rect(g_level.cam.x - 10, g_level.cam.y - 10, 340, 200, 0, a8(cr.bg_fade));
}
static const EntClass FILL = {.name = "creditsFill", .render = fill_render};

/* Player.GetChasePosition: what Madeline did timeAgo seconds ago */
static bool chase_get(float time_ago, Chase *out) {
  bool older = false;
  for (int k = 0; k < cr.chase_n; k++) {
    Chase *s = &cr.chase[(cr.chase_head + k) % CHASE];
    float ago = (g_frame - s->frame) * DT;
    if (ago <= time_ago) {
      if (older || time_ago - ago < 0.02f) {
        *out = *s;
        return true;
      }
      return false;
    }
    older = true;
  }
  return false;
}
static void chase_record(Player *p) {   /* Player.ChaserStates (each update) */
  int i = (cr.chase_head + cr.chase_n) % CHASE;
  if (cr.chase_n < CHASE) cr.chase_n++;
  else cr.chase_head = (cr.chase_head + 1) % CHASE;
  Chase *s = &cr.chase[i];
  s->x = p->ent->x, s->y = p->ent->y, s->frame = g_frame;
  s->anim = p->spr.anim, s->left = p->facing < 0, s->ground = p->on_ground;
}
static int bl_anim(int player_anim) {
  for (unsigned i = 0; i < sizeof BL_ANIMS / 2; i++)
    if (BL_ANIMS[i][0] == player_anim) return BL_ANIMS[i][1];
  return -1;
}
static Ent *trigger_at(int event) {   /* a CreditsTrigger of that event */
  for (int i = 0; i < g_nents; i++) {
    Ent *o = &g_ents[i];
    if (o->cls && o->dead != 1 && !strcmp(o->cls->name, "creditsTrigger") && *(uint8_t *)ST(o, uint8_t) == event) return o;
  }
  return NULL;
}
static void player_jump_dir(Player *p, int dir) {   /* PlayerJump */
  p->facing = dir;
  p->dummy_friction = false;
  p->dummy_auto_animate = true;
  p->speed.x = dir * 120.f;
  player_jump(p, true, true);
  p->auto_jump = true;
  p->auto_jump_timer = 2;
}
static void player_dash_toward(Player *p, V2 dir) {   /* OverrideDashDirection; StateMachine.State = StartDash() */
  cr.override = dir;
  cr.dashing = 1;
  p->dashes = p->dashes - 1 < 0 ? 0 : p->dashes - 1;
  player_set_state(p, ST_DASH);
}
static bool wall_at(Player *p, float dx, float dy) { return collide_solid(p->ent, p->ent->x + dx, p->ent->y + dy); }

/* the dust bunnies of the resort scene (DustGraphic, drawn simply: base, center, eyes) and their boxes */
typedef struct { float t, rot; uint8_t box; } Dusty;
static void dusty_render(Ent *e) {
  Dusty *d = ST(e, Dusty);
  uint16_t box = (uint16_t)(T_decals_3_resort_brokenbox_a + d->box);
  Tex t;
  if (tex_get(box, &t)) gfx_tex_ex(box, e->x, e->y - 4, t.fw / 2.f, (float)t.fh, 1, 1, 0, 0xFFFF, 255, 0);
  draw_centered(T_danger_dustcreature_base00, e->x, e->y, 0xFFFF, 255, 1, 1, d->rot);
  draw_centered(T_danger_dustcreature_center00, e->x, e->y, 0xFFFF, 255, 1, 1, 0);
  V2 dir = angle_vec(d->t, 1), perp = v2(-dir.y, dir.x);
  for (int s = -1; s <= 1; s += 2)
    draw_centered(T_danger_dustcreature_eyes00, e->x + dir.x * 5 + perp.x * 3 * s, e->y + dir.y * 5 + perp.y * 3 * s, rgb(0xFF0000), 255, 1, 1, 0);
}
static void dusty_update(Ent *e) {
  Dusty *d = ST(e, Dusty);
  d->t += DT * 0.6f;
  d->rot += DT;
}
static const EntClass DUSTY = {.name = "creditsDusty", .size = sizeof(Dusty), .update = dusty_update, .render = dusty_render};
static void broom_render(Ent *e) {
  Tex t;
  if (tex_get(T_characters_oshiro_broom, &t)) gfx_tex(T_characters_oshiro_broom, e->x - 48, e->y - 48, 0, 0xFFFF, 255);   /* origin: the sprite's */
}
static const EntClass BROOM = {.name = "oshiroBroom", .render = broom_render};

/* DoEvent's events, nested in WaitForPlayer */
static bool credits_event(int which) {
  Player *p = &g_player;
  Co *c = &cr.ev;
  NB_BEGIN(c);
  if (which == EVT_WAIT) {
    if (cr.bl) spr_play(&BD(cr.bl)->spr, A_badeline_idle, false);
    cr.bl_walk = 0;
  }
  cr.auto_walk = 0;
  p->dummy_friction = true;
  NB_WAIT(c, 0.1f);
  if (which == EVT_WAITJUMPDASH) {
    player_jump_dir(p, -1);
    NB_WAIT(c, 0.2f);
    player_dash_toward(p, v2(-1, -1));   /* (not normalized: as the game) */
    NB_WAIT(c, 0.6f);
    cr.dashing = 0;
    player_set_state(p, ST_DUMMY);
    cr.auto_walk = 1;
  } else if (which == EVT_WAITJUMPDOUBLEDASH) {
    p->facing = 1;
    NB_WAIT(c, 0.25f);
    if (cr.bl) {   /* BadelineCombine */
      cr.bl_float = 0;
      cr.from = v2(cr.bl->x, cr.bl->y);
      for (cr.p = 0; cr.p < 1; cr.p += DT / 0.25f) {
        {
          V2 at = v2lerp(cr.from, v2(p->ent->x, p->ent->y), ease_cube_in(cr.p));
          cr.bl->x = at.x, cr.bl->y = at.y;
        }
        NB_YIELD(c);
      }
      cr.bl->visible = 0;
    }
    p->dashes = 2;
    NB_WAIT(c, 0.5f);
    p->facing = -1;
    NB_WAIT(c, 0.7f);
    player_jump_dir(p, -1);
    NB_WAIT(c, 0.4f);
    player_dash_toward(p, v2(-1, -1));   /* (not normalized: as the game) */
    NB_WAIT(c, 0.6f);
    player_dash_toward(p, v2(-1, 0));
    NB_WAIT(c, 0.6f);
    cr.dashing = 0;
    player_set_state(p, ST_DUMMY);
    cr.auto_walk = 1;
    while (!actor_on_ground(p->ent, 1)) NB_YIELD(c);
    cr.auto_walk = 0;
    p->dummy_friction = true;
    p->dashes = 2;
    NB_WAIT(c, 0.5f);
    p->facing = 1;
    NB_WAIT(c, 1);
    if (cr.bl) cr.bl->x = p->ent->x, cr.bl->y = p->ent->y, cr.bl->visible = 1;
    cr.bl_float = 1;
    p->dashes = 1;
    NB_WAIT(c, 0.8f);
    p->facing = -1;
    cr.auto_walk = 1;
    p->dummy_friction = false;
  } else if (which == EVT_CLIMBDOWN) {
    player_jump_dir(p, -1);
    NB_WAIT(c, 0.4f);
    while (!wall_at(p, -1, 0)) NB_YIELD(c);
    p->dummy_auto_animate = false;
    spr_play(&p->spr, A_player_wallslide, false);
    while (wall_at(p, -1, 32)) {
      if (level_on_interval(0.01f)) dust_burst(v2add(player_center(p), v2(-5, 4)), -PI_F / 2, 1);   /* CreateWallSlideParticles */
      p->speed.y = fminf(p->speed.y, 40);
      NB_YIELD(c);
    }
    player_jump_dir(p, 1);
    NB_WAIT(c, 0.4f);
    while (!wall_at(p, 1, 0)) NB_YIELD(c);
    p->dummy_auto_animate = false;
    spr_play(&p->spr, A_player_wallslide, false);
    while (!wall_at(p, 0, 32)) {
      if (level_on_interval(0.01f)) dust_burst(v2add(player_center(p), v2(5, 4)), -PI_F / 2, 1);
      p->speed.y = fminf(p->speed.y, 40);
      NB_YIELD(c);
    }
    player_jump_dir(p, -1);
    NB_WAIT(c, 0.4f);
    cr.auto_walk = 1;
  } else if (which == EVT_WAIT) {
    p->dummy_auto_animate = false;
    p->speed = v2(0, 0);
    NB_WAIT(c, 0.5f);
    spr_play(&p->spr, A_player_lookUp, false);
    NB_WAIT(c, 2);
    for (int i = 0; i < g_nents; i++)
      if (g_ents[i].cls && g_ents[i].dead != 1 && !strcmp(g_ents[i].cls->name, "bird")) ST(&g_ents[i], Bird)->auto_fly = 1;
    NB_WAIT(c, 0.1f);
    spr_play(&p->spr, A_player_idle, false);
    NB_WAIT(c, 1);
    cr.auto_walk = 1;
    p->dummy_friction = false;
    p->dummy_auto_animate = true;
    cr.bl_walk = 1;
    cr.bl_approach = 0;
    if (cr.bl) {
      cr.bl_from = v2(cr.bl->x, cr.bl->y);
      spr_play(&BD(cr.bl)->spr, A_badeline_walk, false);
    }
    while (cr.bl_approach < 1) {
      cr.bl_approach += DT * 4;
      NB_YIELD(c);
    }
  }
  NB_END(c);
}
/* WaitForPlayer: until Madeline is past the room's middle, with the triggers' events on the way */
static bool credits_wait_player(void) {
  Player *p = level_player();
  if (cr.ev_on) {
    if (credits_event(cr.event)) return true;
    cr.ev_on = 0;
    cr.event = 0;
    return true;   /* Event = null; yield return null */
  }
  if (!p || p->ent->x <= room()->x + 160) return false;
  if (cr.event == EVT_WAITJUMPDASH || cr.event == EVT_WAITJUMPDOUBLEDASH || cr.event == EVT_CLIMBDOWN || cr.event == EVT_WAIT) {
    cr.ev_on = 1;
    memset(&cr.ev, 0, sizeof cr.ev);
    return true;   /* yield return DoEvent(Event): it begins next update */
  }
  cr.event = 0;
  return true;
}
/* BadelineAround: a Badeline circling Madeline */
static void around_start(int i, V2 start, V2 around, Ent *bl) {
  Around *a = &cr.around[i];
  a->remove_at_end = bl == NULL;
  a->e = bl ? bl : bd_new(start);
  if (!a->e) return;
  spr_play(&BD(a->e)->spr, A_badeline_fallSlow, false);
  a->angle = atan2f(start.y - around.y, start.x - around.x);
  a->dist = v2len(v2sub(around, start));
  a->p = 0;
  a->on = 2;
  cr.to = around;
}
static void around_step(Around *a) {
  if (a->on == 2) {
    a->on = 1;
    return;
  }
  if (a->p < 1) {
    float num = a->p * 2, yo = a->p <= 0.5f ? a->p * 2 : 1 - (a->p - 0.5f) * 2;
    V2 at = v2add(cr.to, angle_vec(a->angle - num * PI_F * 2, a->dist + yo * 16 + sinf(a->p * PI_F * 2 * 4) * 5));
    a->e->x = at.x, a->e->y = at.y;
    BD(a->e)->spr.sx = (float)sgn(cr.to.x - a->e->x);
    if (!a->remove_at_end) g_player.facing = sgn(a->e->x - g_player.ent->x) ? sgn(a->e->x - g_player.ent->x) : g_player.facing;
    if (level_on_interval(0.1f)) {
      BDummy *b = BD(a->e);
      uint16_t t = spr_tex(&b->spr);
      if (t != 0xFFFF) trail_add(a->e->x, a->e->y, t, b->spr.ox, b->spr.oy, b->spr.sx, b->spr.sy, 0xAC3232, 1);   /* Player.NormalHairColor */
    }
    a->p += DT / 3;
    return;
  }
  if (a->remove_at_end) bd_vanish(a->e);
  else spr_play(&BD(a->e)->spr, A_badeline_laugh, false);
  a->on = 0;
}
/* SetupLevel */
static bool credits_setup(void) {
  Player *p = level_player();
  if (!p) return true;
  player_intro_none(p);
  cr.bl = bd_new(v2(p->ent->x + 16, p->ent->y - 16));
  if (cr.bl) BD(cr.bl)->floatness = 4;
  cr.bl_float = 1, cr.bl_walk = 0, cr.bl_approach = 0;
  g_session.inv_dashes = 1;
  p->dashes = p->inventory_dashes = 1;
  player_set_state(p, ST_DUMMY);
  p->dummy_friction = false;
  p->dummy_maxspeed = false;
  p->facing = -1;
  cr.auto_walk = 1, cr.auto_cam = 1;
  g_level.cam_offset = v2(70, -24);
  g_level.cam = player_camera_target(p);
  cr.chase_n = 0;
  return false;
}
static void credits_next(const char *room_name) {   /* NextLevel (its two yields follow) */
  for (int i = 0; i < 3; i++) cr.around[i].on = 0, cr.dust[i] = NULL;
  cr.bl = cr.other = cr.oshiro = cr.bird = NULL;
  cr.cam_on = cr.bl_appr = cr.dusty_on = cr.osh_moving = cr.fading = cr.high = cr.wait_par = cr.broom = 0;
  level_goto(room_name);
}
static void credits_fade(float target) { cr.fade_target = target, cr.fading = 1; }
static void credits_camera(V2 target, float duration, float (*ease)(float)) {
  step_reset(&cr.cam);
  cr.cam_target = target, cr.cam_dur = duration, cr.cam_ease = ease;
  cr.cam_on = 2;
}
static void credits_done(void) {   /* OnEnd: on to the Epilogue */
  cr.active = 0;
  level_complete_area(false, true);
}
static void credits_routine(void) {
  Player *p = level_player();
  Co *c = &cr.co;
  CO_BEGIN(c);
  g_level.in_credits = true;
  g_level.completed = true;
  CO_YIELD(c);
  g_wipe.active = false;
  CO_WAIT(c, 0.5f);
  CO_WAIT(c, 3);
  cr.bg_fade = 0;
  credits_fade(0);
  CO_NEST(c, credits_setup());
  CO_NEST(c, credits_wait_player());
  credits_fade(1);
  while (cr.fading) CO_YIELD(c);
  CO_WAIT(c, 1);
  cr.bg_fade = 0.1f;
  /* credits-dashes */
  credits_next("credits-dashes");
  CO_YIELD(c);
  CO_YIELD(c);
  CO_NEST(c, credits_setup());
  credits_fade(0);
  CO_NEST(c, credits_wait_player());
  credits_fade(1);
  while (cr.fading) CO_YIELD(c);
  CO_WAIT(c, 1);
  cr.bg_fade = 0.2f;
  /* credits-walking */
  credits_next("credits-walking");
  CO_YIELD(c);
  CO_YIELD(c);
  CO_NEST(c, credits_setup());
  credits_fade(0);
  CO_WAIT(c, 5.8f);
  cr.bl_float = 0;
  CO_WAIT(c, 0.5f);
  if (cr.bl) BD(cr.bl)->spr.sx = 1;
  CO_WAIT(c, 0.5f);
  cr.auto_walk = 0;
  p->speed = v2(0, 0);
  p->facing = 1;
  CO_WAIT(c, 1.5f);
  if (cr.bl) BD(cr.bl)->spr.sx = -1;
  CO_WAIT(c, 1);
  cr.bl_walk = 1;
  if (cr.bl) cr.bl_from = v2(cr.bl->x, cr.bl->y);
  cr.bl_appr = 1;   /* BadelineApproachWalking */
  CO_WAIT(c, 0.7f);
  cr.auto_walk = 1;
  p->facing = -1;
  CO_NEST(c, credits_wait_player());
  credits_fade(1);
  while (cr.fading) CO_YIELD(c);
  CO_WAIT(c, 1);
  cr.bg_fade = 0.3f;
  /* credits-tree */
  credits_next("credits-tree");
  CO_YIELD(c);
  CO_YIELD(c);
  CO_NEST(c, credits_setup());
  cr.auto_cam = 0;   /* (the Petals foreground is not drawn) */
  cr.to = v2add(g_level.cam, v2(-220, 32));
  g_level.cam.x -= 100;
  cr.bl_approach = 1, cr.bl_float = 0, cr.bl_walk = 1;
  if (cr.bl) BD(cr.bl)->floatness = 0;
  credits_fade(0);
  credits_camera(cr.to, 12, linear);
  CO_WAIT(c, 3.5f);
  if (cr.bl) spr_play(&BD(cr.bl)->spr, A_badeline_idle, false);
  cr.bl_walk = 0;
  CO_WAIT(c, 0.25f);
  cr.auto_walk = 0;
  spr_play(&p->spr, A_player_idle, false);
  p->speed = v2(0, 0);
  p->dummy_auto_animate = false;
  p->facing = 1;
  CO_WAIT(c, 0.5f);
  spr_play(&p->spr, A_player_sitDown, false);
  CO_WAIT(c, 4);
  if (cr.bl) spr_play(&BD(cr.bl)->spr, A_badeline_laugh, false);
  CO_WAIT(c, 1.75f);
  credits_fade(1);
  while (cr.fading) CO_YIELD(c);
  CO_WAIT(c, 1);
  cr.bg_fade = 0.4f;
  /* credits-clouds: Madeline and a Badeline that is a Player too, on the clouds */
  credits_next("credits-clouds");
  CO_YIELD(c);
  CO_YIELD(c);
  CO_NEST(c, credits_setup());
  cr.auto_walk = 0;
  p->speed = v2(0, 0);
  cr.auto_cam = 0;
  p->force_camera_update = false;
  if (cr.bl) cr.bl->visible = 0;
  {
    Ent *t = trigger_at(EVT_BADELINEOFFSET);
    if (t) {
      cr.other = bd_new(v2(t->x, t->y));
      if (cr.other) {
        BDummy *b = BD(cr.other);
        b->floatness = 0, b->spr.sx = -1;
        spr_play(&b->spr, A_badeline_idle, false);
        ent_box(cr.other, 8, 11, -4, -11);
      }
      cr.other_vy = 0;
    }
  }
  credits_fade(0);
  g_level.cam.y -= 100;
  credits_camera(v2add(g_level.cam, v2(0, 160)), 12, linear);
  for (cr.p = 0; cr.p < 10; cr.p += DT) {
    p = level_player();
    if (((cr.p > 3 && cr.p < 6) || cr.p > 9) && p->speed.y < 0 && actor_on_ground(p->ent, 4)) cr.high_player = 0.25f;
    if (cr.other && cr.p > 5 && cr.p < 8 && cr.other_vy < 0 && actor_on_ground(cr.other, 4)) cr.high_baddy = 0.25f;
    if (cr.high_player > 0) cr.high_player -= DT, p->speed.y = -200;
    if (cr.high_baddy > 0) cr.high_baddy -= DT, cr.other_vy = -200;
    CO_YIELD(c);
  }
  credits_fade(1);
  while (cr.fading) CO_YIELD(c);
  CO_WAIT(c, 1);
  cr.bg_fade = 0.5f;
  /* credits-resort: Oshiro sweeps the dust bunnies away */
  credits_next("credits-resort");
  CO_YIELD(c);
  CO_YIELD(c);
  CO_NEST(c, credits_setup());
  credits_fade(0);
  cr.bl_approach = 1, cr.bl_float = 0, cr.bl_walk = 1;
  if (cr.bl) BD(cr.bl)->floatness = 0;
  {
    Ent *t = trigger_at(EVT_OSHIRO);
    V2 at = t ? v2(t->x, t->y + 4) : v2(room()->x + 52.f, room()->y + 148.f);
    cr.oshiro = npc_new(at.x, at.y, SB_oshiro, A_oshiro_sweeping, NULL);
    if (cr.oshiro) {
      Npc *n = NPC_(cr.oshiro);
      n->move_anim = n->idle_anim = A_oshiro_sweeping;
      n->maxspeed = 10;
      n->which = NPC_OSHIRO;
      cr.oshiro->depth = -60;
    }
    cr.dusty_start = cr.dusty_off = v2add(at, v2(220, -24));   /* DustyRoutine */
    for (int i = 0; i < 3; i++) {
      cr.dust[i] = ent_new(&DUSTY, cr.dusty_off.x + i * 24, cr.dusty_off.y);
      if (cr.dust[i]) cr.dust[i]->depth = -50, cr.dust[i]->collidable = 0, ST(cr.dust[i], Dusty)->box = (uint8_t)i, ST(cr.dust[i], Dusty)->t = rndf();
    }
    cr.dusty_t = 0, cr.dusty_on = 1;
  }
  CO_WAIT(c, 4.8f);
  if (cr.oshiro) {
    cr.osh_target = v2add(v2(cr.oshiro->x, cr.oshiro->y), v2(116, 0));
    npc_move_to(cr.oshiro, cr.osh_target, false, 0, false);   /* (a coroutine of its own) */
  }
  CO_WAIT(c, 2);
  cr.auto_cam = 0;
  step_reset(&cr.cam);
  CO_NEST(c, camera_to(&cr.cam, v2((float)room()->x + 64, (float)room()->y), 2, NULL));
  CO_WAIT(c, 5);
  if (cr.oshiro) {
    cr.bird = bird_new(v2add(v2(cr.oshiro->x, cr.oshiro->y), v2(280, -160)), BIRD_NONE);
    if (cr.bird) {
      cr.bird->depth = 10010;
      Bird *b = ST(cr.bird, Bird);
      b->facing = -1;
      spr_play(&b->spr, A_bird_fall, false);
      cr.from = v2(cr.bird->x, cr.bird->y);
      cr.to = v2add(cr.osh_target, v2(50, -12));
    }
  }
  for (cr.p = 0; cr.bird && cr.p < 1; cr.p += DT * 0.5f) {
    {
      V2 at = v2lerp(cr.from, cr.to, ease_quad_out(cr.p));
      cr.bird->x = at.x, cr.bird->y = at.y;
      if (cr.p > 0.5f) {
        spr_play(&ST(cr.bird, Bird)->spr, A_bird_fly, false);
        if (cr.bird->depth != -1000000) cr.bird->depth = -1000000, ents_mark_unsorted();
      }
    }
    CO_YIELD(c);
  }
  if (cr.bird) {
    cr.bird->x = cr.to.x, cr.bird->y = cr.to.y;
    spr_play(&ST(cr.bird, Bird)->spr, A_bird_idle, false);
  }
  if (cr.oshiro) {
    NPC_(cr.oshiro)->mv.step = 0;   /* oshiroRoutine.RemoveSelf() */
    spr_play(&NPC_(cr.oshiro)->spr, A_oshiro_putBroomAway, false);
    cr.broom = 1;
  }
  CO_WAIT(c, 0.5f);
  if (cr.bird) spr_play(&ST(cr.bird, Bird)->spr, A_bird_croak, false);
  CO_WAIT(c, 0.6f);
  if (cr.oshiro) {
    Npc *n = NPC_(cr.oshiro);
    n->maxspeed = 40, n->move_anim = A_oshiro_move, n->idle_anim = A_oshiro_idle;
    npc_move_to(cr.oshiro, v2add(cr.osh_target, v2(14, 0)), false, 0, false);
    CO_AWAIT(c, &NPC_(cr.oshiro)->mv);
  }
  CO_WAIT(c, 2);
  if (cr.bird) ST(cr.bird, Bird)->fly_now = 1;   /* Add(new Coroutine(bird.StartleAndFlyAway())) */
  CO_WAIT(c, 0.75f);
  if (cr.bird && cr.bird->cls) cr.bird->depth = 10010, ents_mark_unsorted();
  if (cr.oshiro) NPC_(cr.oshiro)->spr.sx = -1;
  credits_fade(1);
  while (cr.fading) CO_YIELD(c);
  CO_WAIT(c, 1);
  cr.bg_fade = 0.6f;
  /* credits-wallslide */
  credits_next("credits-wallslide");
  CO_YIELD(c);
  CO_YIELD(c);
  CO_NEST(c, credits_setup());
  cr.bl_float = 0;
  if (cr.bl) {
    BDummy *b = BD(cr.bl);
    b->floatness = 0;
    spr_play(&b->spr, A_badeline_idle, false);
    b->spr.sx = 1;
    Ent *t = trigger_at(EVT_BADELINEOFFSET);
    if (t) cr.bl->x = t->x + 8, cr.bl->y = t->y + 16;
  }
  credits_fade(0);
  cr.wait_par = 1;   /* Add(new Coroutine(WaitForPlayer())) */
  while (cr.bl && level_player_ent()->x > cr.bl->x - 16) CO_YIELD(c);
  if (cr.bl) BD(cr.bl)->spr.sx = -1;
  CO_WAIT(c, 0.1f);
  cr.bl_walk = 1;
  if (cr.bl) cr.bl_from = v2(cr.bl->x, cr.bl->y), spr_play(&BD(cr.bl)->spr, A_badeline_walk, false);
  cr.bl_approach = 0;
  while (cr.bl_approach != 1) {
    cr.bl_approach = approach(cr.bl_approach, 1, DT * 4);
    CO_YIELD(c);
  }
  while (level_player_ent()->x > room()->x + 160) CO_YIELD(c);
  credits_fade(1);
  while (cr.fading) CO_YIELD(c);
  CO_WAIT(c, 1);
  cr.bg_fade = 0.7f;
  /* credits-payphone: Badeline circles Madeline */
  credits_next("credits-payphone");
  CO_YIELD(c);
  CO_YIELD(c);
  CO_NEST(c, credits_setup());
  p->speed = v2(0, 0);
  p->facing = -1;
  cr.auto_walk = 0;
  if (cr.bl) {
    BDummy *b = BD(cr.bl);
    spr_play(&b->spr, A_badeline_idle, false);
    b->floatness = 0;
    cr.bl->y = p->ent->y;
    b->spr.sx = 1;
  }
  cr.bl_float = 0;
  cr.auto_cam = 0;
  g_level.cam.x += 100;
  credits_camera(v2add(g_level.cam, v2(-200, 0)), 14, linear);
  credits_fade(0);
  CO_WAIT(c, 1.5f);
  if (cr.bl) BD(cr.bl)->spr.sx = -1;
  CO_WAIT(c, 0.5f);
  if (cr.bl) bd_float_to(cr.bl, v2(cr.bl->x + 16, cr.bl->y - 12), -1, false, false, false);
  CO_WAIT(c, 0.5f);
  p->facing = 1;
  CO_WAIT(c, 1.5f);
  if (cr.bl) {
    cr.from = v2(cr.bl->x, cr.bl->y);
    around_start(0, cr.from, player_center(p), cr.bl);
  }
  CO_WAIT(c, 0.5f);
  around_start(1, cr.from, cr.to, NULL);
  CO_WAIT(c, 0.5f);
  around_start(2, cr.from, cr.to, NULL);
  CO_WAIT(c, 3);
  if (cr.bl) spr_play(&BD(cr.bl)->spr, A_badeline_laugh, false);
  CO_WAIT(c, 0.5f);
  p->facing = -1;
  CO_WAIT(c, 0.5f);
  p->dummy_auto_animate = false;
  spr_play(&p->spr, A_player_sitDown, false);
  CO_WAIT(c, 3);
  credits_fade(1);
  while (cr.fading) CO_YIELD(c);
  CO_WAIT(c, 1);
  cr.bg_fade = 0.8f;
  /* credits-city */
  credits_next("credits-city");
  CO_YIELD(c);
  CO_YIELD(c);
  CO_NEST(c, credits_setup());
  for (int i = 0; i < g_nents; i++)
    if (g_ents[i].cls && g_ents[i].dead != 1 && !strcmp(g_ents[i].cls->name, "bird")) ST(&g_ents[i], Bird)->facing = 1;
  cr.bl_approach = 1, cr.bl_float = 0, cr.bl_walk = 1;
  if (cr.bl) BD(cr.bl)->floatness = 0;
  credits_fade(0);
  CO_NEST(c, credits_wait_player());
  credits_fade(1);
  while (cr.fading) CO_YIELD(c);
  CO_WAIT(c, 1);
  cr.bg_fade = 0;
  /* credits-prologue */
  credits_next("credits-prologue");
  CO_YIELD(c);
  CO_YIELD(c);
  CO_NEST(c, credits_setup());
  cr.bl_approach = 1, cr.bl_float = 0, cr.bl_walk = 1;
  if (cr.bl) BD(cr.bl)->floatness = 0;
  credits_fade(0);
  CO_NEST(c, credits_wait_player());
  credits_fade(1);
  while (cr.fading) CO_YIELD(c);
  while (cr.bottom_timer < 2) CO_YIELD(c);
  fade_wipe(false, 0.5f, credits_done);   /* its end: EndCutscene */
  CO_END(c);
}
/* CS07_Credits.Update (before the player's), then its coroutines */
static const EntClass CREDITSPOST;
static void credits_update(Ent *e) {
  Player *p = level_player();
  if (!cr.active) {
    ent_remove(e);
    return;
  }
  if (cr.need_setup && p && !credits_setup()) cr.need_setup = 0;
  if (!cr.post && p) {   /* (made after the player: it updates after it) */
    Ent *pe = ent_new(&CREDITSPOST, 0, 0);
    if (pe) pe->visible = pe->collidable = 0;
    cr.post = 1;
  }
  if (p && p->ent->cls) {
    if (cr.dashing) p->force_move_x = sgn(cr.override.x), p->force_move_x_timer = DT * 2;   /* Input.MoveX = its x */
    if (cr.dashing && p->state == ST_DASH && p->co_step == 2 && !v2eq(p->dash_dir, cr.override)) {
      p->speed = v2mul(cr.override, 240);   /* the dash goes the overridden way */
      p->dash_dir = cr.override;
      if (cr.override.x) p->facing = sgn(cr.override.x);
    }
    if (cr.auto_walk) {
      if (p->on_ground) {
        p->speed.x = -44.8f;
        bool wall = wall_at(p, -20, 0), gap = !wall_at(p, -8, 1) && !wall_at(p, -8, 32);
        if (wall || gap) {
          player_jump(p, true, true);
          p->auto_jump = true;
          p->auto_jump_timer = wall ? 0.6f : 2;
        }
      } else
        p->speed.x = -64;
    }
    if (cr.bl && cr.bl_float) {
      V2 to = v2(p->ent->x + 16, p->ent->y - 16), at = v2(cr.bl->x, cr.bl->y);
      at = v2add(at, v2mul(v2sub(to, at), 1 - powf(0.01f, DT)));
      cr.bl->x = at.x, cr.bl->y = at.y;
      BD(cr.bl)->spr.sx = -1;
    }
    if (cr.bl && cr.bl_walk) {
      Chase st;
      bool got = chase_get(0.35f + sinf(cr.walk_offset) * 0.1f, &st);
      if (got && st.ground) cr.walk_offset += DT;
      if (got) {
        BDummy *b = BD(cr.bl);
        if (cr.bl_approach >= 1) {
          cr.bl->x = st.x, cr.bl->y = st.y;
          int a = bl_anim(st.anim);
          if (a >= 0) spr_play(&b->spr, a, false);
          b->spr.sx = st.left ? -1.f : 1.f;
        } else {
          V2 at = v2lerp(cr.bl_from, v2(st.x, st.y), cr.bl_approach);
          cr.bl->x = at.x, cr.bl->y = at.y;
        }
      }
    }
    if (fabsf(p->speed.x) > 90) p->speed.x = approach(p->speed.x, 90 * sgn(p->speed.x), 1000 * DT);
  }
  /* the credits roll (Credits.Update): the lines come up the column and stay */
  cr.scroll += 100 * 0.6f * DT / 6;
  if (cr.scroll >= 180) cr.bottom_timer += DT;
  /* the coroutines: Routine, then the ones added beside it */
  credits_routine();
  if (!cr.active) return;
  if (cr.fading) {   /* FadeTo */
    cr.fade = approach(cr.fade, cr.fade_target, DT * 0.5f);
    if (cr.fade == cr.fade_target) cr.fading = 0;
  }
  if (cr.cam_on == 2) cr.cam_on = 1;
  else if (cr.cam_on && !camera_to(&cr.cam, cr.cam_target, cr.cam_dur, cr.cam_ease)) cr.cam_on = 0;
  if (cr.bl_appr && cr.bl) {   /* BadelineApproachWalking */
    BDummy *b = BD(cr.bl);
    if (cr.bl_approach < 1) {
      b->floatness = approach(b->floatness, 0, DT * 8);
      cr.bl_approach = approach(cr.bl_approach, 1, DT * 0.6f);
    } else
      cr.bl_appr = 0;
  }
  if (cr.wait_par && !credits_wait_player()) cr.wait_par = 0;
  for (int i = 0; i < 3; i++)
    if (cr.around[i].on && cr.around[i].e && cr.around[i].e->cls) around_step(&cr.around[i]);
  if (cr.dusty_on) {   /* DustyRoutine */
    cr.dusty_t += DT;
    if (cr.dusty_t > 3.8f) {
      for (int j = 0; j < 3; j++)
        if (cr.dust[j]) cr.dust[j]->x = cr.dusty_off.x + j * 24, cr.dust[j]->y = cr.dusty_off.y + sinf((cr.dusty_t - 3.8f) * 4 + j * 0.8f) * 4;
      if (cr.dusty_off.x < room()->x + 120) cr.dusty_off.y = approach(cr.dusty_off.y, cr.dusty_start.y + 16, DT * 16);
      cr.dusty_off.x -= 26 * DT;
    }
  }
  if (cr.broom && cr.oshiro && NPC_(cr.oshiro)->spr.anim == A_oshiro_putBroomAway && NPC_(cr.oshiro)->spr.frame == 10) {
    Ent *b = ent_new(&BROOM, cr.oshiro->x, cr.oshiro->y);   /* the broom stays behind */
    if (b) b->depth = cr.oshiro->depth + 1, b->collidable = 0;
    cr.broom = 0;
  }
  if (cr.other) {   /* the other Player (Badeline): in its dummy state, falling, landing */
    cr.other_vy = approach(cr.other_vy, 160, 900 * DT);
    if (!actor_move_v(cr.other, cr.other_vy * DT, NULL, NULL) && cr.other_vy > 0 && actor_on_ground(cr.other, 1)) cr.other_vy = 0;
    else if (actor_on_ground(cr.other, 1) && cr.other_vy > 0) cr.other_vy = 0;
  }
}
/* PostUpdate: the camera follows Madeline; and her ChaserStates */
static void credits_post(Ent *e) {
  Player *p = level_player();
  (void)e;
  if (!cr.active || !p || !p->ent->cls) return;
  chase_record(p);
  if (!cr.auto_cam) return;
  V2 t = player_camera_target(p);
  if (!p->on_ground) t.y = (g_level.cam.y * 2 + t.y) / 3;
  g_level.cam = v2add(g_level.cam, v2mul(v2sub(t, g_level.cam), 1 - powf(0.01f, DT)));
  g_level.cam.x = (float)(int)t.x;
}
static void credits_render(Ent *e) {
  (void)e;
  if (g_level.paused) return;
  gfx_hud(true);
  /* creditsgradient (Gui): black, from nothing at 145 to 0.9 at 225, at 0.6 */
  for (int x = 145; x < 225; x += 8) gfx_rect((float)x, -2, 8, 184, 0, a8(0.6f * 0.9f * (x - 145 + 4) / 80.f));
  gfx_rect(225, -2, 97, 184, 0, a8(0.6f * 0.9f));
  if (cr.fade > 0) gfx_rect(-2, -2, 324, 184, 0, a8(ease_cube_inout(cr.fade)));
  float lh = font_line_height(FONT_S), y = 180 + 8 - cr.scroll;
  if (y < 90 - lh * 3) y = 90 - lh * 3;
  for (int i = 0; i < 6; i++)
    if (CREDITS_TEXT[i][0]) font_draw_justified(CREDITS_TEXT[i], 303, y + i * lh, 1, 0, FONT_S, 0xFFFF, 255);
  gfx_hud(false);
}
static const EntClass CREDITS = {.name = "CS07_Credits", .update = credits_update, .render = credits_render};
static const EntClass CREDITSPOST = {.name = "CS07_CreditsPostUpdate", .update = credits_post};
/* a room of the credits loads: the cutscene, its backdrop fill and its post update are in it */
static void credits_room(void) {
  if (!have_ch7() || !cr.active || !g_level.room) return;
  for (int i = 0; i < g_nents; i++)   /* (one per load) */
    if (g_ents[i].cls == &CREDITS) return;
  /* what was made in the room before is gone; the same room again (a death): set up again */
  for (int i = 0; i < 3; i++) cr.around[i].on = 0, cr.dust[i] = NULL;
  cr.bl = cr.other = cr.oshiro = cr.bird = NULL;
  cr.need_setup = cr.room_seen == (uint8_t)(g_level.room->index + 1);
  cr.room_seen = (uint8_t)(g_level.room->index + 1);
  Ent *e = ent_new(&CREDITS, 0, 0);
  if (e) e->tags = TAG_HUD, e->collidable = 0, e->depth = -2000000;
  e = ent_new(&FILL, 0, 0);
  if (e) e->depth = 2000000, e->collidable = 0;
  cr.post = 0;
}
/* CreditsTrigger: its event to the credits */
static void ctrig_enter(Ent *e, Player *p) {
  (void)p;
  if (have_ch7() && cr.active) cr.event = *ST(e, uint8_t);
}
static const EntClass CTRIG = {.name = "creditsTrigger", .size = 4, .kind = KIND_TRIGGER,
                               .more = &(const EntMore){.on_enter = ctrig_enter}};

/* ---------------------------------------------------------------- the factories */
bool ents_ch7(const EData *d) {
  credits_room();
  switch (d->type) {
    case ET_summitcheckpoint: new_scheck(d); return true;
    case ET_summitgem: new_gem(d); return true;
    case ET_summitGemManager: new_gemmgr(d); return true;
    case ET_summitcloud: new_scloud(d); return true;
    case ET_SummitBackgroundManager: new_ascendm(d); return true;
    case ET_npc:
      if (strcmp(EAS(d, npc, npc), "granny_07x")) return false;
      new_granny7(d);
      return true;
  }
  return false;
}
bool trigs_ch7(const EData *d) {
  if (d->type == TT_creditsTrigger) {
    static const char *const EVENTS[6] = {"WaitJumpDash", "WaitJumpDoubleDash", "ClimbDown", "Wait", "BadelineOffset", "Oshiro"};
    const char *ev = EAS(d, creditsTrigger, event);
    uint8_t which = EVT_NONE;
    for (int i = 0; i < 6; i++)
      if (!strcmp(ev, EVENTS[i])) which = (uint8_t)(i + 1);
    Ent *e = ent_new(&CTRIG, d->x, d->y);
    if (e) ent_box(e, EA(d, creditsTrigger, width), EA(d, creditsTrigger, height), 0, 0), e->visible = 0, *ST(e, uint8_t) = which;
    return true;
  }
  if (d->type == TT_eventTrigger && !strcmp(EAS(d, eventTrigger, event), "ch7_summit")) {
    Ent *e = ent_new(&SUMMITTRIG, d->x, d->y);
    float w = EA(d, eventTrigger, width), h = EA(d, eventTrigger, height);
    if (e) ent_box(e, w, h, 0, 0), e->visible = 0, ST(e, Summit)->target = v2(d->x + w / 2, d->y + h);
    return true;
  }
  return false;
}
uint32_t ch7_ram(void) { return END_credits; }
