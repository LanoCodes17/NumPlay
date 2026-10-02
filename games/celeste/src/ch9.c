#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("Os")   /* not drawn every frame: smaller over faster */
#endif
/* Core (chapter 9): the core mode and what follows it (the switch, fire balls,
 * wall boosters, bounce blocks, fire barriers and ice blocks, rising and
 * sandwich lava), the heart gem door and Core's triggers.
 * Ported from the game's classes (CoreModeToggle.cs, FireBall.cs, LavaRect.cs...). */
#include "badeline.h"

/* ---------------------------------------------------------------- helpers (Calc) */
static float dist2(V2 a, V2 b) { return (a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y); }
static uint16_t tex_named(const char *p) { return res_tex_by_name(p); }
static int tex_family(const char *prefix, uint16_t *out, int n) {
  char p[64];
  int k = 0;
  for (int i = 0; i < n; i++) {
    out[i] = tex_named(path2(p, prefix, NULL, i));
    if (out[i] != 0xFFFF) k = i + 1;
  }
  return k;
}
static void draw_centered(uint16_t tex, float x, float y, uint16_t tint, uint8_t alpha) {
  Tex t;
  if (tex == 0xFFFF || !tex_get(tex, &t)) return;
  float k = t.scale > 1 ? t.scale : 1;   /* stored smaller, drawn scaled up */
  gfx_tex_ex(tex, x, y, t.fw * k / 2.f, t.fh * k / 2.f, 1, 1, 0, tint, alpha, 0);
}
/* Image.DrawOutline(Color.Black): the four neighbours in black */
static void draw_outlined(uint16_t tex, float x, float y, float ox, float oy, uint8_t flags, uint16_t tint, uint8_t alpha) {
  static const int8_t O[4][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}};
  for (int k = 0; k < 4; k++) gfx_tex_ex(tex, x + O[k][0], y + O[k][1], ox, oy, 1, 1, 0, 0, alpha, flags | GF_SILHOUETTE);
  gfx_tex_ex(tex, x, y, ox, oy, 1, 1, 0, tint, alpha, flags);
}
static uint32_t lerp_rgb(uint32_t a, uint32_t b, float t) {
  t = clampf(t, 0, 1);
  int r = (int)(((a >> 16) & 255) + (((int)((b >> 16) & 255) - (int)((a >> 16) & 255)) * t));
  int g = (int)(((a >> 8) & 255) + (((int)((b >> 8) & 255) - (int)((a >> 8) & 255)) * t));
  int bl = (int)((a & 255) + (((int)(b & 255) - (int)(a & 255)) * t));
  return (uint32_t)(r << 16 | g << 8 | bl);
}
static float hash01(uint32_t seed, uint32_t i) {
  uint32_t h = seed * 0x9E3779B1u ^ (i + 0x7F4A7C15u) * 0x85EBCA6Bu;
  h ^= h >> 15, h *= 0x2C1B3C6Du, h ^= h >> 12, h *= 0x297A2D39u, h ^= h >> 15;
  return (h >> 8) / 16777216.f;
}
static uint8_t ent_ref(const Ent *e) { return (uint8_t)(e - g_ents); }
static Ent *ent_at(uint8_t i) { return &g_ents[i]; }
/* the player's hurtbox (PlayerCollider checks) */
static void player_hurtbox(const Ent *pe, float *l, float *t, float *r, float *b) {
  float w = pe->cw, h = pe->ch, x = pe->cx, y = pe->cy;
  if (h == 11) h = 9;
  else if (h == 6) h = 4;
  else if (h == 8 && w == 8) w = 6, h = 6, x = -3, y = -9;
  *l = pe->x + x, *t = pe->y + y, *r = *l + w, *b = *t + h;
}
static bool hurt_rect(const Player *p, float l, float t, float r, float b) {
  float pl, pt, pr, pb;
  player_hurtbox(p->ent, &pl, &pt, &pr, &pb);
  return pr > l && pb > t && pl < r && pt < b;
}
static bool hurt_circle(const Player *p, float cx, float cy, float rad) {
  float pl, pt, pr, pb;
  player_hurtbox(p->ent, &pl, &pt, &pr, &pb);
  float nx = clampf(cx, pl, pr), ny = clampf(cy, pt, pb);
  return (nx - cx) * (nx - cx) + (ny - cy) * (ny - cy) < rad * rad;
}

/* ---------------------------------------------------------------- compact sprites */
typedef struct { uint8_t bank, anim, frame, playing; int8_t last; uint8_t pad[3]; float timer; } CSpr;
static Sprite cs_raw(const CSpr *c) {
  Sprite s;
  memset(&s, 0, sizeof s);
  s.bank = c->bank, s.anim = c->anim, s.frame = c->frame, s.playing = c->playing;
  s.timer = c->timer, s.rate = 1, s.last_goto = c->last, s.sx = s.sy = 1, s.tint = 0xFFFF, s.alpha = 255, s.visible = 1;
  return s;
}
static void cs_back(CSpr *c, const Sprite *s) {
  c->bank = s->bank, c->anim = s->anim, c->frame = s->frame, c->playing = s->playing, c->timer = s->timer, c->last = s->last_goto;
}
static void cs_init(CSpr *c, int bank) {
  Sprite s;
  spr_init(&s, bank);
  cs_back(c, &s);
}
static void cs_play(CSpr *c, int anim, bool restart) {
  Sprite s = cs_raw(c);
  spr_play(&s, anim, restart);
  cs_back(c, &s);
}
/* Play(id, restart: false, randomizeFrame: true) */
static void cs_play_random(CSpr *c, int anim) {
  if (c->anim == anim) return;
  cs_play(c, anim, false);
  Sprite s = cs_raw(c);
  int n = spr_frames(&s);
  float delay = n ? spr_anim_duration(c->bank, anim) / n : 0;
  c->timer = rndf() * delay;
  c->frame = (uint8_t)(n ? rndi(n) : 0);
}
static int cs_update(CSpr *c) {   /* returns the animation that passed its last frame */
  if (!c->playing || c->anim == 255) return -1;
  Sprite s = cs_raw(c);
  int n = spr_frames(&s);
  float delay = n ? spr_anim_duration(c->bank, c->anim) / n : 0;
  int ended = (c->frame >= n - 1 && delay > 0 && c->timer + DT >= delay) ? c->anim : -1;
  spr_update(&s);
  cs_back(c, &s);
  return ended;
}
static uint16_t cs_tex(const CSpr *c) {
  Sprite s = cs_raw(c);
  uint16_t t = spr_tex(&s);
  if (t != 0xFFFF && !res_cached(t)) {   /* an animation not loaded yet: all its frames in one pass (as spr_draw) */
    uint16_t ids[32], gotos[8];
    int ng, a = c->anim != 255 ? c->anim : c->last;
    if (a >= 0) res_load(ids, bank_anim_textures(c->bank, a, ids, 32, gotos, &ng));
  }
  return t;
}
static void cs_origin(const CSpr *c, uint16_t t, float *ox, float *oy) {
  Sprite s;
  spr_init(&s, c->bank);
  Tex tx;
  *ox = s.ox, *oy = s.oy;
  if (s.justify && tex_get(t, &tx)) *ox = tx.fw * tx.scale * s.jx, *oy = tx.fh * tx.scale * s.jy;
}
static void cs_draw(const CSpr *c, float x, float y, float sx, float sy, uint16_t tint, uint8_t alpha, uint8_t flags) {
  uint16_t t = cs_tex(c);
  if (t == 0xFFFF) return;
  float ox, oy;
  cs_origin(c, t, &ox, &oy);
  gfx_tex_ex(t, x, y, ox, oy, sx, sy, 0, tint, alpha, flags);
}
/* Sprite.DrawOutline(Color.Black) then the sprite */
static void cs_draw_outlined(const CSpr *c, float x, float y, uint8_t flags) {
  uint16_t t = cs_tex(c);
  if (t == 0xFFFF) return;
  float ox, oy;
  cs_origin(c, t, &ox, &oy);
  draw_outlined(t, x, y, ox, oy, flags, 0xFFFF, 255);
}

/* nodes of the room's entities, a pool per room slot (freed when its last user goes) */
#define NODES 160
enum { OFF_nodes = 0, END_nodes = OFF_nodes + (int)sizeof(int16_t[2][NODES][2]) };
#define nodes (*(int16_t(*)[2][NODES][2])(void *)(g_chram[9] + OFF_nodes))
static uint8_t nnodes[2], node_users[2];
static int nodes_add(const EData *d, bool with_position, int *count) {
  int slot = g_level.room_slot, at = nnodes[slot], n = 0;
  if (with_position && nnodes[slot] < NODES) {
    nodes[slot][nnodes[slot]][0] = (int16_t)d->x, nodes[slot][nnodes[slot]][1] = (int16_t)d->y;
    nnodes[slot]++, n++;
  }
  for (int i = 0; i < d->nnodes && nnodes[slot] < NODES; i++) {
    V2 v = ed_node(d, i);
    nodes[slot][nnodes[slot]][0] = (int16_t)v.x, nodes[slot][nnodes[slot]][1] = (int16_t)v.y;
    nnodes[slot]++, n++;
  }
  *count = n;
  return at;
}
static void nodes_release(int slot) {
  if (node_users[slot] && --node_users[slot] == 0) nnodes[slot] = 0;
}
static V2 node_at(int slot, int i) { return v2(nodes[slot][i][0], nodes[slot][i][1]); }

/* ---------------------------------------------------------------- the core mode (Level.CoreMode) */
static void core_listeners(int mode);
static void core_set(int mode) {
  if (g_level.core_mode == mode) return;
  g_level.core_mode = mode;
  level_set_flag("cold", mode == 2);
  core_listeners(mode);   /* CoreModeListener.OnChange */
}

/* ---------------------------------------------------------------- CoreModeToggle */
typedef struct { CSpr spr; float cooldown; uint8_t ice, only_fire, only_ice, persistent, play_sounds, pad[3]; } Toggle;
static bool toggle_usable(Toggle *t) {
  if (!t->only_fire || t->ice) return t->only_ice ? !t->ice : true;
  return false;
}
static void toggle_sprite(Toggle *t, bool animate) {
  if (animate) {
    if (toggle_usable(t)) cs_play(&t->spr, t->ice ? A_coreFlipSwitch_ice : A_coreFlipSwitch_hot, false);
    else cs_play(&t->spr, t->ice ? A_coreFlipSwitch_iceOff : A_coreFlipSwitch_hotOff, false);
  } else if (toggle_usable(t))
    cs_play(&t->spr, t->ice ? A_coreFlipSwitch_iceLoop : A_coreFlipSwitch_hotLoop, false);
  else
    cs_play(&t->spr, t->ice ? A_coreFlipSwitch_iceOffLoop : A_coreFlipSwitch_hotOffLoop, false);
  t->play_sounds = 0;
}
static void toggle_on_mode(Ent *e, int mode) {
  Toggle *t = ST(e, Toggle);
  t->ice = mode == 2;
  toggle_sprite(t, true);
}
static void toggle_on_player(Ent *e, Player *p) {
  (void)p;
  Toggle *t = ST(e, Toggle);
  if (!toggle_usable(t) || t->cooldown > 0) return;
  t->play_sounds = 1;
  core_set(g_level.core_mode == 2 ? 1 : 2);
  if (t->persistent) g_session.core_mode = (uint8_t)g_level.core_mode;
  level_flash_a(0xFFFF, 0.15f, true);
  level_freeze(0.05f);
  ST(e, Toggle)->cooldown = 1;
}
static void toggle_update(Ent *e) {
  Toggle *t = ST(e, Toggle);
  cs_update(&t->spr);
  if (t->cooldown > 0) t->cooldown -= DT;
}
static void toggle_render(Ent *e) { cs_draw(&ST(e, Toggle)->spr, e->x, e->y, 1, 1, 0xFFFF, 255, 0); }
static void toggle_awake(Ent *e) {   /* Added */
  Toggle *t = ST(e, Toggle);
  t->ice = g_level.core_mode == 2;
  toggle_sprite(t, false);
}
static const EntClass TOGGLE = {.name = "coreModeToggle", .size = sizeof(Toggle), .update = toggle_update, .render = toggle_render,
                                .awake = toggle_awake, .on_player = toggle_on_player, .kind = KIND_PCOLLIDE};
static void new_toggle(const EData *d) {
  Ent *e = ent_new(&TOGGLE, d->x, d->y);
  if (!e) return;
  ent_box(e, 16, 24, -8, -12);
  e->depth = 2000;
  Toggle *t = ST(e, Toggle);
  t->only_fire = EAB(d, coreModeToggle, onlyFire);
  t->only_ice = EAB(d, coreModeToggle, onlyIce);
  t->persistent = EAB(d, coreModeToggle, persistent);
  cs_init(&t->spr, SB_coreFlipSwitch);
}

/* ---------------------------------------------------------------- FireBall */
typedef struct {
  CSpr spr;
  Wiggler hit;
  V2 hit_dir;
  float speed, speed_mult, percent, mult;
  uint8_t node0, nnode, slot, index, amount, ice, broken, not_core;
} Fire;
static V2 fire_pos(Fire *f, float percent) {   /* GetPercentPosition */
  if (percent <= 0) return node_at(f->slot, f->node0);
  if (percent >= 1) return node_at(f->slot, f->node0 + f->nnode - 1);
  float total = 0;
  for (int i = 1; i < f->nnode; i++) total += v2len(v2sub(node_at(f->slot, f->node0 + i), node_at(f->slot, f->node0 + i - 1)));
  float at = total * percent, len = 0;
  int i = 0;
  for (; i < f->nnode - 1; i++) {
    float seg = v2len(v2sub(node_at(f->slot, f->node0 + i + 1), node_at(f->slot, f->node0 + i)));
    if (len + seg > at) break;
    len += seg;
  }
  if (i >= f->nnode - 1) i = f->nnode - 2;
  float seg = v2len(v2sub(node_at(f->slot, f->node0 + i + 1), node_at(f->slot, f->node0 + i)));
  float t = clamped_map(percent, len / total, (len + seg) / total, 0, 1);
  return v2lerp(node_at(f->slot, f->node0 + i), node_at(f->slot, f->node0 + i + 1), t);
}
static void fire_kill(Ent *e, Player *p) {
  Fire *f = ST(e, Fire);
  V2 dir = safe_norm(v2sub(player_center(p), v2(e->x, e->y)), 1);
  player_die(p, dir, false);
  if (p->dead) {
    f->hit_dir = dir;
    wiggler_restart(&f->hit);
  }
}
/* two PlayerColliders: the Circle(6) and Hitbox(16, 6, -8, -3); this entity's box covers both */
static void fire_on_player(Ent *e, Player *p) {
  Fire *f = ST(e, Fire);
  if (hurt_circle(p, e->x, e->y, 6)) {   /* OnPlayer */
    if (!f->ice && !f->broken) fire_kill(e, p);
    else if (f->ice && !f->broken && e_bottom(p->ent) > e->y + 4) fire_kill(e, p);
    if (p->dead) return;
  }
  if (e->collidable && hurt_rect(p, e->x - 8, e->y - 3, e->x + 8, e->y + 3)) {   /* OnBounce */
    if (f->ice && !f->broken && e_bottom(p->ent) <= e->y + 4 && p->speed.y >= 0) {
      cs_play(&f->spr, A_fireball_shatter, false);
      f->broken = 1;
      e->collidable = 0;
      player_bounce(p, (float)(int)(e->y - 2));
      particles_emit(PL_MID, &P_FireBall_P_IceBreak, 18, v2(e->x, e->y), v2(6, 6), 0);
    }
  }
}
static void fire_on_mode(Ent *e, int mode) {
  Fire *f = ST(e, Fire);
  f->ice = mode == 2;
  if (!f->broken) cs_play_random(&f->spr, f->ice ? A_fireball_ice : A_fireball_hot);
}
static void fire_update(Ent *e) {
  if (g_level.transitioning) return;
  Fire *f = ST(e, Fire);
  cs_update(&f->spr);
  wiggler_update(&f->hit);
  f->speed_mult = approach(f->speed_mult, f->ice ? 0.5f : 1, 2 * DT);
  f->percent += f->speed * f->speed_mult * DT;
  if (f->percent >= 1) {
    f->percent = fmodf(f->percent, 1);
    if (f->broken && !v2eq(node_at(f->slot, f->node0 + f->nnode - 1), node_at(f->slot, f->node0))) {
      f->broken = 0;
      e->collidable = 1;
      cs_play_random(&f->spr, f->ice ? A_fireball_ice : A_fireball_hot);
    }
  }
  V2 p = fire_pos(f, f->percent);
  e->x = p.x, e->y = p.y;
  if (!f->broken && level_on_interval(f->ice ? 0.08f : 0.05f))
    particles_emit(PL_BG, f->ice ? &P_FireBall_P_IceTrail : &P_FireBall_P_FireTrail, 1, p, v2(4, 4), 0);
}
static void fire_render(Ent *e) {
  Fire *f = ST(e, Fire);
  float x = e->x + f->hit_dir.x * f->hit.value * 8, y = e->y + f->hit_dir.y * f->hit.value * 8;
  if (!f->broken) cs_draw_outlined(&f->spr, x, y, 0);
  else cs_draw(&f->spr, x, y, 1, 1, 0xFFFF, 255, 0);
}
static void fire_awake(Ent *e) {   /* Added */
  Fire *f = ST(e, Fire);
  f->ice = g_level.core_mode == 2 || f->not_core;
  f->speed_mult = f->ice ? 0 : 1;
  cs_play_random(&f->spr, f->ice ? A_fireball_ice : A_fireball_hot);
}
static void fire_removed(Ent *e) { nodes_release(ST(e, Fire)->slot); }
static const EntClass FIRE = {.name = "fireBall", .size = sizeof(Fire), .update = fire_update, .render = fire_render, .awake = fire_awake,
                              .removed = fire_removed, .on_player = fire_on_player, .kind = KIND_PCOLLIDE};
static void new_fireball(const EData *d) {
  int n;
  int at = nodes_add(d, true, &n);
  if (n < 2) return;
  int amount = (int)EA(d, fireBall, amount);
  if (amount < 1) amount = 1;
  float offset = EA(d, fireBall, offset), mult = EA(d, fireBall, speed);
  float total = 0;
  int slot = g_level.room_slot;
  for (int i = 1; i < n; i++) total += v2len(v2sub(node_at(slot, at + i), node_at(slot, at + i - 1)));
  for (int k = 0; k < amount; k++) {   /* Added: the others of the group */
    Ent *e = ent_new(&FIRE, d->x, d->y);
    if (!e) return;
    ent_box(e, 16, 12, -8, -6);
    e->tags = TAG_TRANSITION_UPDATE;
    Fire *f = ST(e, Fire);
    f->node0 = (uint8_t)at, f->nnode = (uint8_t)n, f->slot = (uint8_t)slot;
    f->index = (uint8_t)k, f->amount = (uint8_t)amount;
    f->mult = mult;
    f->speed = 60 / total * mult;
    f->percent = k == 0 ? 0 : (float)k / amount;
    f->percent += 1.f / amount * offset;
    f->percent = fmodf(f->percent, 1);
    V2 p = fire_pos(f, f->percent);
    e->x = p.x, e->y = p.y;
    cs_init(&f->spr, SB_fireball);
    wiggler_init(&f->hit, 1.2f, 2);
    f->hit.start_zero = true;
    node_users[slot]++;
  }
}

/* ---------------------------------------------------------------- WallBooster */
#define WB_TILES 40
typedef struct {
  float t;                 /* the tiles' animation clock: every frame they show moves together */
  int8_t facing;
  uint8_t ice, ntiles, pad;
  uint8_t tile[WB_TILES];  /* ice: bit 7 = "iceShine", the rest the frame */
} WallB;
static uint16_t wb_tex[2][3][8];   /* [ice][top, mid, bottom][frame] */
static void wb_on_mode(Ent *e, int mode) {
  WallB *w = ST(e, WallB);
  w->ice = mode == 2;
  if (w->ice) e->kind |= KIND_CLIMBBLOCKER;   /* climbBlocker.Blocking */
  else e->kind &= (uint16_t)~KIND_CLIMBBLOCKER;
  w->t = 0;
  memset(w->tile, 0, sizeof w->tile);   /* tiles.Play(ice / hot) */
}
static void wb_update(Ent *e) {
  if (g_level.transitioning) return;
  WallB *w = ST(e, WallB);
  float delay = w->ice ? 0.08f : 0.035f;
  w->t += DT;
  if (w->t < delay) return;
  w->t -= delay;
  for (int i = 0; i < w->ntiles; i++) {
    uint8_t *k = &w->tile[i];
    if (!w->ice) {
      *k = (uint8_t)((*k + 1) % 8);   /* "hot": frames 0-7, looping */
      continue;
    }
    int shine = *k >> 7, f = (*k & 127) + 1;
    if (f >= (shine ? 5 : 10)) {   /* ice: frame 0 ten times, then ice x5 or iceShine (frames 0-4) */
      shine = shine ? 0 : rndi(6) == 5;
      f = 0;
    }
    *k = (uint8_t)(shine << 7 | f);
  }
}
static void wb_render(Ent *e) {
  WallB *w = ST(e, WallB);
  for (int i = 0; i < w->ntiles; i++) {
    int part = i == 0 ? 0 : (i * 8 + 16 > e->ch) ? 2 : 1;
    int f = w->ice ? ((w->tile[i] >> 7) ? (w->tile[i] & 127) : 0) : w->tile[i];
    uint16_t t = wb_tex[w->ice][part][f];
    gfx_tex(t, e->x + (w->facing > 0 ? 4 : 0), e->y + i * 8, w->facing > 0 ? GF_FLIPX : 0, 0xFFFF, 255);
  }
}
static void wb_awake(Ent *e) { wb_on_mode(e, g_level.core_mode == 2 ? 2 : 0); }
static const EntClass WALLBOOSTER = {.name = "wallBooster", .size = sizeof(WallB), .update = wb_update, .render = wb_render,
                                     .awake = wb_awake};
static void new_wallbooster(const EData *d) {
  float h = EA(d, wallBooster, height);
  bool left = EAB(d, wallBooster, left);
  Ent *e = ent_new(&WALLBOOSTER, d->x, d->y);
  if (!e) return;
  e->tags = TAG_TRANSITION_UPDATE;
  e->depth = 1999;
  if (left) ent_box(e, 2, h, 0, 0);
  else ent_box(e, 2, h, 6, 0);
  WallB *w = ST(e, WallB);
  w->facing = left ? -1 : 1;
  w->ntiles = (uint8_t)fminf(WB_TILES, ceilf(h / 8));
  if (!wb_tex[0][0][0]) {
    static const char *P[2][3] = {{"objects/wallBooster/fireTop", "objects/wallBooster/fireMid", "objects/wallBooster/fireBottom"},
                                  {"objects/wallBooster/iceTop", "objects/wallBooster/iceMid", "objects/wallBooster/iceBottom"}};
    for (int k = 0; k < 2; k++)
      for (int p = 0; p < 3; p++) tex_family(P[k][p], wb_tex[k][p], k ? 5 : 8);
  }
}

/* ---------------------------------------------------------------- LavaRect: drawn a strip at a time */
enum { LAVA_ALL, LAVA_TOP, LAVA_BOTTOM };
typedef struct {
  float ox, oy;            /* its Position, relative to the entity */
  float w, h, timer, fade, spikey, small, big, curve, mult;
  uint32_t surface, edge, center;
  uint16_t seed;
  uint8_t step, only;
} Lava;
static void lava_init(Lava *l, float w, float h, int step) {
  memset(l, 0, sizeof *l);
  l->w = w, l->h = h, l->step = (uint8_t)step;
  l->fade = 16, l->small = 1, l->big = 4, l->curve = 12, l->mult = 1;
  l->surface = 0xFFFFFF, l->edge = 0xD3D3D3, l->center = 0xA9A9A9;
  l->timer = rndf() * 100;
  l->seed = (uint16_t)rndi(65536);
}
static float lava_sin(float v) { return (1 + sinf(v)) / 2; }
static float lava_wave(const Lava *l, int step, float length) {   /* LavaRect.Wave */
  int num = step * l->step;
  float k = l->only != LAVA_ALL ? 1 : clamped_map((float)num, 0, length * 0.1f, 0, 1) * clamped_map((float)num, length * 0.9f, length, 1, 0);
  float v = lava_sin(num * 0.25f + l->timer * 4) * l->small;
  v += lava_sin(num * 0.05f + l->timer * 0.5f) * l->big;
  if (step % 2 == 0) v += l->spikey;
  if (l->only != LAVA_ALL) v += (1 - yoyo(num / length)) * l->curve;
  return v * k;
}
static void lava_update(Lava *l) { l->timer += l->mult * DT; }
static int lava_nbubbles(const Lava *l) { return (int)(l->w * l->h * 0.005f); }

/* drawing happens a strip at a time after every render call: each frame the waves are sampled once
 * (at the mesh's vertices, every SurfaceStep pixels, in 1/4 pixels) and the strips interpolate them */
#define LAVA_SAMPLES 2048
enum { OFF_lava_samples = ((END_nodes + 7) & ~7), END_lava_samples = OFF_lava_samples + (int)sizeof(int8_t[LAVA_SAMPLES]) };
#define lava_samples (*(int8_t(*)[LAVA_SAMPLES])(void *)(g_chram[9] + OFF_lava_samples))
static int nlava_samples;
#define MAX_LAVA_DRAWS 12
typedef struct { Ent *e; Lava *l; int16_t edge[4], nedge[4]; } LavaCtx;   /* top, right, bottom, left */
enum { OFF_lava_draws = ((END_lava_samples + 7) & ~7), END_lava_draws = OFF_lava_draws + (int)sizeof(LavaCtx[MAX_LAVA_DRAWS]) };
#define lava_draws (*(LavaCtx(*)[MAX_LAVA_DRAWS])(void *)(g_chram[9] + OFF_lava_draws))
static int nlava_draws;
static uint32_t lava_frame;
static uint16_t lava_bubble_tex[4];
static float lava_sample(const LavaCtx *c, int edge, float s) {   /* the wave at s along an edge */
  const Lava *l = c->l;
  int n = c->nedge[edge];
  if (n < 2) return 0;
  float k = s / l->step;
  int i = (int)floorf(k);
  float f = k - i;
  if (i < 0) i = 0, f = 0;
  if (i >= n - 1) i = n - 2, f = 1;
  const int8_t *w = lava_samples + c->edge[edge];
  return (w[i] * (1 - f) + w[i + 1] * f) * 0.25f;
}
static bool lava_sample_edge(LavaCtx *c, int edge, float length) {
  int n = (int)(length / c->l->step) + 2;
  if (nlava_samples + n > LAVA_SAMPLES) return false;
  c->edge[edge] = (int16_t)nlava_samples, c->nedge[edge] = (int16_t)n;
  for (int i = 0; i < n; i++) {
    float v = lava_wave(c->l, i, length) * 4;
    lava_samples[nlava_samples++] = (int8_t)clampf(roundf(v), -127, 127);
  }
  return true;
}
/* bubbles rise from the bottom and start again when they reach the surface; their x, speed and
 * alpha never change, so they come from the seed, and their height from LavaRect.timer */
static float lava_bubble_speed(const Lava *l, int i) { return (float)(4 + (int)(hash01(l->seed, 4u * i + 1) * 8)); }
static void lava_bubble(const LavaCtx *c, int i, float *x, float *y, float *alpha, int *cycle) {
  const Lava *l = c->l;
  float bx = 1 + hash01(l->seed, 4u * i) * (l->w - 2);
  float y0 = hash01(l->seed, 4u * i + 2) * l->h;
  float top = 2 - lava_sample(c, 0, (float)((int)(bx / l->step) * l->step));
  float span = l->h - 1 - top;
  if (span < 1) span = 1;
  float travel = l->h - 1 - y0 + lava_bubble_speed(l, i) * l->timer;
  *cycle = (int)floorf(travel / span);
  *y = l->h - 1 - fmodf(travel, span);
  *x = bx;
  *alpha = 0.4f + hash01(l->seed, 4u * i + 3) * 0.4f;
}
static void lava_strip(uint16_t *strip, int y0, int y1, void *ctx) {
  const LavaCtx *c = ctx;
  const Lava *l = c->l;
  float bx = c->e->x + l->ox, by = c->e->y + l->oy;
  int vx0 = (int)floorf(bx) - g_camx, vy0 = (int)floorf(by) - g_camy;
  float fade_x = fminf(l->fade, l->w / 2), fade_y = fminf(l->fade, l->h / 2);
  uint16_t cs = rgb(l->surface), cc = rgb(l->center);
  uint32_t ce = l->edge, cm = l->center;
  int xa = vx0 < 0 ? 0 : vx0, xb = vx0 + (int)l->w > VIEW_W ? VIEW_W : vx0 + (int)l->w;
  float up = l->only == LAVA_TOP ? 64 : 0, down = l->only == LAVA_BOTTOM ? 64 : 0;
  for (int vy = y0; vy < y1; vy++) {
    float ly = vy - vy0 + 0.5f;
    if (ly < -up || ly >= l->h + down) continue;
    uint16_t *row = strip + (vy - y0) * VIEW_W;
    float wl = 0, wr = 0;
    if (l->only == LAVA_ALL) wl = lava_sample(c, 3, l->h - ly), wr = lava_sample(c, 1, ly);
    for (int vx = xa; vx < xb; vx++) {
      float lx = vx - vx0 + 0.5f, d, fd = fade_y;
      if (l->only == LAVA_TOP) d = ly + lava_sample(c, 0, lx);
      else if (l->only == LAVA_BOTTOM) d = l->h + lava_sample(c, 2, l->w - lx) - ly;
      else {
        float dt = ly + lava_sample(c, 0, lx), db = l->h + lava_sample(c, 2, l->w - lx) - ly, dl = lx + wl, dr = l->w + wr - lx;
        d = dt;
        if (db < d) d = db;
        if (dl < d) d = dl, fd = fade_x;
        if (dr < d) d = dr, fd = fade_x;
      }
      if (d < 0) continue;
      if (d < 1) row[vx] = cs;
      else if (d < fd) row[vx] = rgb(lerp_rgb(ce, cm, (d - 1) / (fd - 1)));
      else row[vx] = cc;
    }
  }
  /* the bubbles (particles/bubble in SurfaceColor * alpha), and where they popped on the surface */
  Tex bt;
  bool have = tex_get(T_particles_bubble, &bt);
  int n = lava_nbubbles(l);
  for (int i = 0; i < n; i++) {
    float x, y, a;
    int cycle;
    lava_bubble(c, i, &x, &y, &a, &cycle);
    int px = (int)floorf(bx + x + 0.5f) - g_camx - 2, py = (int)floorf(by + y + 0.5f) - g_camy - 2;
    if (have && py + 4 > y0 && py < y1 && px + 4 > 0 && px < VIEW_W) blit_tex(strip, y0, y1, &bt, px, py, 0, cs, a8(a));
    if (l->only == LAVA_BOTTOM || cycle <= 0 || l->mult <= 0 || hash01(l->seed ^ 0x55u, (uint32_t)(i * 977 + cycle)) >= 0.75f) continue;
    /* Frame += DT * 6 since it reached the top: 4 frames of danger/lava/bubble_a */
    float since = (l->h - 1 - y) / lava_bubble_speed(l, i) / l->mult;
    int f = (int)(since * 6);
    if (f < 0 || f >= 4 || lava_bubble_tex[f] == 0xFFFF) continue;
    Tex st;
    if (!tex_get(lava_bubble_tex[f], &st)) continue;
    int num = (int)(x / l->step);
    float sy = 1 - lava_sample(c, 0, (float)(num * l->step));
    int sx_ = (int)floorf(bx + num * l->step - st.fw * 0.5f + 0.5f) - g_camx;   /* DrawJustified(0.5, 1) */
    int sy_ = (int)floorf(by + sy - st.fh + 0.5f) - g_camy;
    blit_tex(strip, y0, y1, &st, sx_ + st.ox, sy_ + st.oy, 0, cs, 255);
  }
}
static void lava_render(Ent *e, Lava *l) {
  if (lava_frame != g_frame) lava_frame = g_frame, nlava_draws = 0, nlava_samples = 0;
  if (nlava_draws >= MAX_LAVA_DRAWS) return;
  float top = e->y + l->oy - (l->only == LAVA_TOP ? 64 : 0), bottom = e->y + l->oy + l->h + (l->only == LAVA_BOTTOM ? 64 : 0);
  int y0 = (int)floorf(top) - g_camy, y1 = (int)ceilf(bottom) - g_camy + 1;
  if (y1 <= 0 || y0 >= VIEW_H || e->x + l->ox + l->w < g_camx || e->x + l->ox > g_camx + VIEW_W) return;
  LavaCtx *c = &lava_draws[nlava_draws];
  memset(c, 0, sizeof *c);
  c->e = e, c->l = l;
  if (!lava_sample_edge(c, 0, l->w)) return;   /* the top: the bubbles need it in every mode */
  if (l->only != LAVA_TOP && !lava_sample_edge(c, 2, l->w)) return;
  if (l->only == LAVA_ALL && (!lava_sample_edge(c, 1, l->h) || !lava_sample_edge(c, 3, l->h))) return;
  nlava_draws++;
  if (lava_bubble_tex[0] == 0) tex_family("danger/lava/bubble_a", lava_bubble_tex, 4);
  Tex bt;   /* nothing loads while strips are drawn: the bubbles are loaded (and kept) now */
  for (int i = 0; i < 4; i++) tex_get(lava_bubble_tex[i], &bt);
  tex_get(T_particles_bubble, &bt);
  gfx_custom(lava_strip, c, y0 < 0 ? 0 : y0, y1 > VIEW_H ? VIEW_H : y1);
}

/* ---------------------------------------------------------------- FireBarrier and IceBlock */
typedef struct { Lava lava; uint8_t solid, ice, pad[2]; } Barrier;
static const EntClass FIREBARRIER, ICEBLOCK;
static void inner_solid_update(Ent *e) { plat_update(e); }
static const EntClass INNER_SOLID = {.name = "solid", .update = inner_solid_update, .kind = KIND_SOLID};
static void barrier_on_player(Ent *e, Player *p) { player_die(p, safe_norm(v2sub(player_center(p), e_center(e)), 1), false); }
static void barrier_on_mode(Ent *e, int mode) {
  Barrier *b = ST(e, Barrier);
  bool on = b->ice ? mode == 2 : mode == 1;
  e->collidable = on;
  Ent *s = ent_at(b->solid);
  if (s->cls == &INNER_SOLID) s->collidable = on;
  if (on) return;
  V2 c = e_center(e);
  for (int i = 0; i < e->cw; i += 4)
    for (int j = 0; j < e->ch; j += 4) {
      V2 v = v2(e->x + i + 2 + rnd_rangef(-2, 2), e->y + j + 2 + rnd_rangef(-2, 2));
      particles_emit1(PL_MID, b->ice ? &P_IceBlock_P_Deactivate : &P_FireBarrier_P_Deactivate, v, vangle(v2sub(v, c)));
    }
}
static void barrier_update(Ent *e) {
  if (g_level.transitioning && !ST(e, Barrier)->ice) return;
  lava_update(&ST(e, Barrier)->lava);
}
static void barrier_render(Ent *e) {
  if (e->collidable) lava_render(e, &ST(e, Barrier)->lava);
}
static void barrier_awake(Ent *e) {   /* Added: the solid inside, on in its mode */
  Barrier *b = ST(e, Barrier);
  Ent *s = ent_new(&INNER_SOLID, e->x + 2, e->y + 3);
  bool on = b->ice ? g_level.core_mode == 2 : g_level.core_mode == 1;
  e->collidable = on;
  if (!s) return;
  ent_box(s, e->cw - 4, e->ch - 5, 0, 0);
  s->visible = 0;
  s->collidable = on;
  s->dead = 0;   /* in the scene now, as Added does */
  ST(e, Barrier)->solid = ent_ref(s);
}
static void barrier_removed(Ent *e) {
  Ent *s = ent_at(ST(e, Barrier)->solid);
  if (s->cls == &INNER_SOLID) ent_remove(s);
}
static const EntClass FIREBARRIER = {.name = "fireBarrier", .size = sizeof(Barrier), .update = barrier_update, .render = barrier_render,
                                     .awake = barrier_awake, .removed = barrier_removed, .on_player = barrier_on_player,
                                     .kind = KIND_PCOLLIDE};
static const EntClass ICEBLOCK = {.name = "iceBlock", .size = sizeof(Barrier), .update = barrier_update, .render = barrier_render,
                                  .awake = barrier_awake, .removed = barrier_removed, .on_player = barrier_on_player,
                                  .kind = KIND_PCOLLIDE};
static void new_barrier(const EData *d, bool ice) {
  float w = ice ? EA(d, iceBlock, width) : EA(d, fireBarrier, width), h = ice ? EA(d, iceBlock, height) : EA(d, fireBarrier, height);
  Ent *e = ent_new(ice ? &ICEBLOCK : &FIREBARRIER, d->x, d->y);
  if (!e) return;
  ent_box(e, w, h, 0, 0);
  e->depth = -8500;
  if (!ice) e->tags = TAG_TRANSITION_UPDATE;
  Barrier *b = ST(e, Barrier);
  b->ice = ice;
  b->solid = ent_ref(e);
  if (ice) {
    lava_init(&b->lava, w, h, 2);
    b->lava.mult = 0;
    b->lava.surface = 0xA6FFF4, b->lava.edge = 0x6CD6EB, b->lava.center = 0x4CA8D6;
    b->lava.small = 1, b->lava.big = 1, b->lava.curve = 1, b->lava.spikey = 3;
  } else {
    lava_init(&b->lava, w, h, 4);
    b->lava.surface = 0xFF8933, b->lava.edge = 0xF25E29, b->lava.center = 0xD01C01;   /* RisingLava.Hot */
    b->lava.small = 2, b->lava.big = 1, b->lava.curve = 1;
  }
}

/* ---------------------------------------------------------------- RisingLava and SandwichLava */
static const uint32_t HOT[3] = {0xFF8933, 0xF25E29, 0xD01C01}, COLD[3] = {0x33FFE7, 0x4CA2EB, 0x0151D0};
typedef struct {
  Lava bottom, top;
  float lerp, delay, start_x, trans_y, alarm;
  uint8_t intro, ice, waiting, sandwich, leaving, persistent, in_transition, pad;
} Rising;
static bool sandwich_handoff;   /* the next room's sandwich lava gave its place to the one already here */
static void sandwich_leave(Ent *e) {   /* Leave() */
  Rising *r = ST(e, Rising);
  e->tags |= TAG_TRANSITION_UPDATE;
  r->leaving = 1;
  e->collidable = 0;
  r->alarm = 2;
}
static void rising_colors(Rising *r, Lava *l) {
  l->surface = lerp_rgb(HOT[0], COLD[0], r->lerp);
  l->edge = lerp_rgb(HOT[1], COLD[1], r->lerp);
  l->center = lerp_rgb(HOT[2], COLD[2], r->lerp);
  l->spikey = r->lerp * 5;
  l->mult = (1 - r->lerp) * 2;
  l->fade = r->ice ? 128 : 32;
}
static void rising_on_mode(Ent *e, int mode) { ST(e, Rising)->ice = mode == 2; }
/* SandwichLava's collider is two boxes: the player is checked against each here */
static void rising_on_player(Ent *e, Player *p) {
  Rising *r = ST(e, Rising);
  if (r->sandwich) {
    if (r->waiting) return;
    if (!hurt_rect(p, e->x, e->y, e->x + 340, e->y + 120) && !hurt_rect(p, e->x, e->y - 280, e->x + 340, e->y - 160)) return;
  }
  player_die(p, v2(0, -1), false);
}
static float sandwich_center_y(void) { return (float)(g_level.room->y + g_level.room->h) - 10; }
static void rising_update(Ent *e) {
  Rising *r = ST(e, Rising);
  Player *p = level_player();
  if (r->sandwich) {
    if (g_level.transitioning) {
      if (!r->in_transition) {   /* TransitionListener.OnOutBegin: leave unless the next room has one too */
        r->in_transition = 1;
        r->trans_y = e->y;
        if (r->persistent && !sandwich_handoff) sandwich_leave(e);
        sandwich_handoff = false;
      }
      e->x = g_level.cam.x;   /* OnOut */
      if (!r->leaving) e->y = lerpf(r->trans_y, sandwich_center_y(), g_level.tr_at);
      if (g_level.tr_at > 0.95f && r->leaving) ent_remove(e);
      return;
    }
    r->in_transition = 0;
    e->x = g_level.cam.x;
    r->delay -= DT;
    if (r->alarm > 0 && (r->alarm -= DT) <= 0) {
      ent_remove(e);
      return;
    }
    e->visible = 1;
    if (r->waiting) {
      e->y = approach(e->y, sandwich_center_y(), 128 * DT);
      if (p && p->ent->x >= r->start_x && !p->just_respawned && p->state != ST_DUMMY) r->waiting = 0;
    } else if (!r->leaving && r->delay <= 0)
      e->y += (r->ice ? 20 : -20) * DT;
    r->top.oy = approach(r->top.oy, -160 - r->top.h + (r->leaving ? -512 : 0), (r->leaving ? 256 : 64) * DT);
    r->bottom.oy = approach(r->bottom.oy, r->leaving ? 512 : 0, (r->leaving ? 256 : 64) * DT);
  } else {
    r->delay -= DT;
    e->x = g_level.cam.x;
    e->visible = 1;
    float num = 1;
    if (r->waiting) {
      if (!r->intro && p && p->just_respawned) e->y = approach(e->y, p->ent->y + 32, 32 * DT);
      if ((!r->ice || !r->intro) && (!p || !p->just_respawned)) r->waiting = 0;
    } else {
      float bottom = g_level.cam.y + 180 - 12;
      if (e->y > bottom + 96) e->y = bottom + 96;
      num = e->y > bottom ? clamped_map(e->y - bottom, 0, 96, 1, 2) : clamped_map(bottom - e->y, 0, 32, 1, 0.5f);
      if (r->delay <= 0) e->y += -30 * num * DT;
    }
  }
  r->lerp = approach(r->lerp, r->ice ? 1 : 0, DT * 4);
  rising_colors(r, &r->bottom);
  lava_update(&r->bottom);
  if (r->sandwich) {
    rising_colors(r, &r->top);
    lava_update(&r->top);
  }
}
static void rising_render(Ent *e) {
  Rising *r = ST(e, Rising);
  lava_render(e, &r->bottom);
  if (r->sandwich) lava_render(e, &r->top);
}
static void rising_awake(Ent *e) {
  Rising *r = ST(e, Rising);
  Player *p = level_player();
  if (r->intro) r->waiting = 1;
  else if (p && p->just_respawned) r->waiting = 1;
  if (r->intro) e->visible = 1;
}
static void sandwich_awake(Ent *e) {
  Rising *r = ST(e, Rising);
  Player *p = level_player();
  if (p && (p->just_respawned || p->ent->x < r->start_x)) r->waiting = 1;
  /* a sandwich lava from the last room stays: this one only moves its start */
  for (int i = 0; i < g_nents; i++) {
    Ent *o = &g_ents[i];
    if (o == e || !o->cls || o->dead == 1 || o->cls != e->cls) continue;
    Rising *q = ST(o, Rising);
    if (!r->persistent && !q->leaving) {
      q->start_x = r->start_x;
      q->waiting = 1;
      sandwich_handoff = true;
      ent_remove(e);
      return;
    }
  }
  r->persistent = 1;
  e->tags |= TAG_PERSISTENT | TAG_TRANSITION_UPDATE;   /* its TransitionListener runs during transitions */
  if (!p || p->intro != INTRO_RESPAWN) r->top.oy -= 60, r->bottom.oy += 60;
  else e->visible = 1;
}
static const EntClass SANDWICH = {.name = "sandwichLava", .size = sizeof(Rising), .update = rising_update, .render = rising_render,
                                  .awake = sandwich_awake, .on_player = rising_on_player, .kind = KIND_PCOLLIDE};
static const EntClass RISING2 = {.name = "risingLava", .size = sizeof(Rising), .update = rising_update, .render = rising_render,
                                 .awake = rising_awake, .on_player = rising_on_player, .kind = KIND_PCOLLIDE};
static void new_rising(const EData *d, bool sandwich) {
  Room *rm = g_level.room;
  if (!rm) return;
  Ent *e = ent_new(sandwich ? &SANDWICH : &RISING2, 0, 0);
  if (!e) return;
  e->depth = -1000000;
  e->visible = 0;
  Rising *r = ST(e, Rising);
  lava_init(&r->bottom, 400, 200, 4);
  r->bottom.ox = -40, r->bottom.only = LAVA_TOP, r->bottom.small = 2;
  /* Added: below the room, as the room being loaded sees it */
  float rx = d->rx, ry = d->ry;
  int rh = 0;
  for (int s = 0; s < 2; s++)
    if (g_level.rooms[s].index == d->room) rh = g_level.rooms[s].h;
  if (!rh) rh = rm->h;
  e->x = rx - 10;
  r->ice = g_session.core_mode == 2;
  if (sandwich) {
    ent_box(e, 340, 400, 0, -280);   /* both boxes (the hit test picks them apart) */
    r->sandwich = 1;
    r->start_x = d->x;
    lava_init(&r->top, 400, 200, 4);
    r->top.ox = -40, r->top.oy = -360, r->top.only = LAVA_BOTTOM, r->top.small = 2;
    r->top.big = r->bottom.big = 2;
    r->top.curve = r->bottom.curve = 4;
    e->y = ry + rh - 10;
  } else {
    ent_box(e, 340, 120, 0, 0);
    r->intro = EAB(d, risingLava, intro);
    e->y = ry + rh + 16;
  }
}

/* ---------------------------------------------------------------- BounceBlock */
enum { BB_WAITING, BB_WINDUP, BB_BOUNCING, BB_END, BB_BROKEN };
typedef struct {
  V2 bounce_dir, start, bounce_lift, debris_dir;
  float move_speed, windup_start, windup, respawn, bounce_end, flash, alarm;
  CSpr hot_center, cold_center;
  uint16_t seed;
  uint8_t state, ice, ice_next, reformed;
} Bounce;
static uint16_t bb_tex[2], bb_rubble[2][4];
/* the debris of broken blocks (BreakDebris, RespawnDebris): a pool drawn by one entity */
#define MAX_DEBRIS 96
/* vx, vy: its speed in 1/8 px/s, or (RespawnDebris) where it goes, in pixels; dur in 1/100 s */
typedef struct { float x, y; int16_t vx, vy; float percent; uint8_t dur, tex, respawn, used; } BDebris;
enum { OFF_debris = ((END_lava_draws + 7) & ~7), END_debris = OFF_debris + (int)sizeof(BDebris[MAX_DEBRIS]) };
#define debris (*(BDebris(*)[MAX_DEBRIS])(void *)(g_chram[9] + OFF_debris))
static void debris_add(V2 at, V2 v, float duration, bool ice, bool respawn) {
  for (int i = 0; i < MAX_DEBRIS; i++)
    if (!debris[i].used) {
      BDebris *b = &debris[i];
      b->x = at.x, b->y = at.y;
      b->vx = (int16_t)roundf(respawn ? v.x : v.x * 8), b->vy = (int16_t)roundf(respawn ? v.y : v.y * 8);
      b->percent = 0, b->dur = (uint8_t)clampf(duration * 100 + 0.5f, 1, 255);
      b->tex = (uint8_t)((ice ? 4 : 0) + rndi(4));
      b->respawn = respawn, b->used = 1;
      return;
    }
}
static void debrismgr_update(Ent *e) {
  (void)e;
  for (int i = 0; i < MAX_DEBRIS; i++) {
    BDebris *b = &debris[i];
    if (!b->used) continue;
    if (b->respawn) {   /* RespawnDebris: from x, y to vx, vy */
      if (b->percent > 1) {
        b->used = 0;
        continue;
      }
      b->percent += DT / (b->dur * 0.01f);
    } else {
      if (b->percent >= 1) {
        b->used = 0;
        continue;
      }
      float vx = b->vx / 8.f, vy = b->vy / 8.f;
      b->x += vx * DT, b->y += vy * DT;
      b->vx = (int16_t)roundf(approach(vx, 0, 180 * DT) * 8);
      b->vy = (int16_t)clampf(roundf((vy + 200 * DT) * 8), -32767, 32767);
      b->percent += DT / (b->dur * 0.01f);
    }
  }
}
static void debrismgr_render(Ent *e) {
  (void)e;
  for (int i = 0; i < MAX_DEBRIS; i++) {
    BDebris *b = &debris[i];
    if (!b->used) continue;
    uint16_t t = bb_rubble[b->tex >> 2][b->tex & 3];
    Tex tx;
    if (!tex_get(t, &tx)) continue;
    float x = b->x, y = b->y, a = 1 - b->percent;
    if (b->respawn) {
      float k = ease_cube_in(fminf(b->percent, 1));
      x = b->x + (b->vx - b->x) * k, y = b->y + (b->vy - b->y) * k, a = b->percent;
    }
    draw_outlined(t, x, y, tx.fw * tx.scale / 2.f, tx.fh * tx.scale / 2.f, 0, 0xFFFF, a8(a));
  }
}
static const EntClass DEBRISMGR = {.name = "bounceBlockDebris", .update = debrismgr_update, .render = debrismgr_render};
static void debris_manager(void) {
  for (int i = 0; i < g_nents; i++)
    if (g_ents[i].cls == &DEBRISMGR && g_ents[i].dead != 1) return;
  memset(debris, 0, sizeof debris);
  Ent *m = ent_new(&DEBRISMGR, 0, 0);
  if (m) m->tags = TAG_GLOBAL, m->collidable = 0, m->depth = -9000;
}

static Player *bb_windup_player(Ent *e) {   /* WindUpPlayerCheck */
  Ent *pe = level_player_ent();
  Player *p = level_player();
  if (!pe || !p) return NULL;
  if (collide_ent_at(e, e->x, e->y - 1, pe) && p->speed.y >= 0) return p;
  if (collide_ent_at(e, e->x + 1, e->y, pe) && p->state == ST_CLIMB && p->facing == -1) return p;
  if (collide_ent_at(e, e->x - 1, e->y, pe) && p->state == ST_CLIMB && p->facing == 1) return p;
  return NULL;
}
static void bb_move_to(Ent *e, V2 to, V2 lift) {
  plat_move_h_lift(e, to.x - (e->x + e->remx), lift.x);
  plat_move_v_lift(e, to.y - (e->y + e->remy), lift.y);
}
static void bb_break(Ent *e) {
  Bounce *b = ST(e, Bounce);
  b->state = BB_BROKEN;
  e->collidable = 0;
  plat_static_movers_enable(e, false);
  b->respawn = 1.6f;
  V2 dir = b->ice ? v2(0, 1) : b->debris_dir, c = e_center(e);
  for (int i = 0; i < e->cw; i += 8)
    for (int j = 0; j < e->ch; j += 8) {
      V2 at = v2(e->x + i + 4, e->y + j + 4);
      if (b->ice) dir = safe_norm(v2sub(at, c), 1);
      V2 d = angle_vec(vangle(dir) + rnd_rangef(-0.1f, 0.1f), 1);
      float sp = b->ice ? (float)(20 + rndi(20)) : (float)(120 + rndi(80));
      debris_add(at, v2mul(d, sp), (float)(2 + rndi(1)), b->ice, false);
    }
  float a = vangle(b->debris_dir);
  for (int k = 0; k < e->cw; k += 4)
    for (int l = 0; l < e->ch; l += 4) {
      V2 v = v2(e->x + 2 + k + rnd_rangef(-1, 1), e->y + 2 + l + rnd_rangef(-1, 1));
      particles_emit1(PL_MID, b->ice ? &P_BounceBlock_P_IceBreak : &P_BounceBlock_P_FireBreak, v, b->ice ? vangle(v2sub(v, c)) : a);
    }
}
static void bb_mode_check(Bounce *b) {
  if (b->ice_next != b->ice) b->ice = b->ice_next;   /* ToggleSprite: drawn from ice */
}
static void bb_update(Ent *e) {
  Bounce *b = ST(e, Bounce);
  plat_update(e);
  cs_update(&b->hot_center);
  cs_update(&b->cold_center);
  if (b->alarm > 0 && (b->alarm -= DT) <= 0) {   /* the reform alarm */
    b->reformed = 1;
    b->flash = 0.6f;
    plat_static_movers_enable(e, true);
    for (int i = 0; i < e->cw; i += 4) {
      particles_emit1(PL_MID, &P_BounceBlock_P_Reform, v2(e->x + 2 + i + rndi(2) - 1, e->y), -PI_F / 2);
      particles_emit1(PL_MID, &P_BounceBlock_P_Reform, v2(e->x + 2 + i + rndi(2) - 1, e_bottom(e) - 1), PI_F / 2);
    }
    for (int j = 0; j < e->ch; j += 4) {
      particles_emit1(PL_MID, &P_BounceBlock_P_Reform, v2(e->x, e->y + 2 + j + rndi(2) - 1), PI_F);
      particles_emit1(PL_MID, &P_BounceBlock_P_Reform, v2(e_right(e) - 1, e->y + 2 + j + rndi(2) - 1), 0);
    }
  }
  b->flash = approach(b->flash, 0, DT * 8);
  V2 ex = plat_exact(e);
  switch (b->state) {
    case BB_WAITING: {
      bb_mode_check(b);
      b->move_speed = approach(b->move_speed, 100, 400 * DT);
      V2 to = approach_v(ex, b->start, b->move_speed * DT);
      V2 lift = safe_norm(v2sub(to, ex), b->move_speed);
      lift.x *= 0.75f;
      bb_move_to(e, to, lift);
      b = ST(e, Bounce);
      b->windup = approach(b->windup, 0, DT);
      Player *p = bb_windup_player(e);
      if (p) {
        b->move_speed = 80;
        b->windup_start = 0;
        b->bounce_dir = b->ice ? v2(0, -1) : safe_norm(v2sub(player_center(p), e_center(e)), 1);
        b->state = BB_WINDUP;
        if (b->ice) plat_start_shaking(e, 0.2f);
      }
      break;
    }
    case BB_WINDUP: {
      Player *p = bb_windup_player(e);
      if (p) b->bounce_dir = b->ice ? v2(0, -1) : safe_norm(v2sub(player_center(p), e_center(e)), 1);
      if (b->windup_start > 0) {
        b->windup_start -= DT;
        b->windup = approach(b->windup, 0, DT);
        break;
      }
      b->move_speed = approach(b->move_speed, b->ice ? 35 : 40, 600 * DT);
      float k = b->ice ? 0.333f : 1;
      V2 target = v2sub(b->start, v2mul(b->bounce_dir, b->ice ? 16 : 10));
      V2 to = approach_v(ex, target, b->move_speed * k * DT);
      V2 lift = safe_norm(v2sub(to, ex), b->move_speed * k);
      lift.x *= 0.75f;
      bb_move_to(e, to, lift);
      b = ST(e, Bounce);
      ex = plat_exact(e);
      b->windup = clamped_map(v2len(v2sub(ex, target)), 16, 2, 0, 1);
      if (b->ice && dist2(ex, target) <= 12) plat_start_shaking(e, 0.1f);
      else if (!b->ice && b->windup >= 0.5f) plat_start_shaking(e, 0.1f);
      if (dist2(ex, target) <= 2) {
        if (b->ice) bb_break(e);
        else b->state = BB_BOUNCING;
        ST(e, Bounce)->move_speed = 0;
      }
      break;
    }
    case BB_BOUNCING: {
      b->move_speed = approach(b->move_speed, 140, 800 * DT);
      V2 target = v2add(b->start, v2mul(b->bounce_dir, 24));
      V2 to = approach_v(ex, target, b->move_speed * DT);
      b->bounce_lift = safe_norm(v2sub(to, ex), fminf(b->move_speed * 3, 200));
      b->bounce_lift.x *= 0.75f;
      bb_move_to(e, to, b->bounce_lift);
      b = ST(e, Bounce);
      b->windup = 1;
      if (v2eq(plat_exact(e), target) || (!b->ice && !bb_windup_player(e))) {
        b->debris_dir = safe_norm(v2sub(target, b->start), 1);
        b->state = BB_END;
        b->move_speed = 0;
        b->bounce_end = 0.05f;
        Player *p = bb_windup_player(e);   /* ShakeOffPlayer */
        if (p) {
          player_set_state(p, ST_NORMAL);
          p->speed = b->bounce_lift;
          p->jump_grace_timer = 0.1f;   /* StartJumpGraceTime */
        }
      }
      break;
    }
    case BB_END:
      b->bounce_end -= DT;
      if (b->bounce_end <= 0) bb_break(e);
      break;
    case BB_BROKEN: {
      if (e->depth != 8990) e->depth = 8990, ents_mark_unsorted();
      b->reformed = 0;
      if (b->respawn > 0) {
        b->respawn -= DT;
        break;
      }
      float px = e->x, py = e->y;
      e->x = b->start.x, e->y = b->start.y;
      bool blocked = false;
      e->collidable = 1;
      for (int i = 0; i < g_nents && !blocked; i++) {
        Ent *o = &g_ents[i];
        if (o != e && o->cls && o->dead != 1 && o->collidable && (o->kind & (KIND_ACTOR | KIND_SOLID)) && collide_ent_at(e, e->x, e->y, o))
          blocked = true;
      }
      e->collidable = 0;
      if (blocked) {
        e->x = px, e->y = py;
        break;
      }
      bb_mode_check(b);
      for (int i = 0; i < e->cw; i += 8)
        for (int j = 0; j < e->ch; j += 8) {
          V2 at = v2(e->x + i + 4, e->y + j + 4);
          debris_add(v2add(at, safe_norm(v2sub(at, e_center(e)), 12)), at, 0.35f, b->ice, true);
        }
      b->alarm = 0.35f;
      e->depth = -9000;
      ents_mark_unsorted();
      plat_static_movers_move(e, v2(e->x - px, e->y - py));
      e->collidable = 1;
      b->state = BB_WAITING;
      break;
    }
  }
}
static void bb_render(Ent *e) {
  Bounce *b = ST(e, Bounce);
  float x = e->x + e->shakex, y = e->y + e->shakey;
  if (b->state != BB_BROKEN && b->reformed) {
    int nx = 8, ny = 8;   /* the 64x64 sources: 8 cells */
    for (int i = 0; i < e->cw; i += 8)
      for (int j = 0; j < e->ch; j += 8) {
        int ci = i == 0 ? 0 : i >= e->cw - 8 ? nx - 1 : 1 + (int)(hash01(b->seed + b->ice * 7919u, (uint32_t)(i * 64 + j) * 2) * (nx - 2));
        int cj = j == 0 ? 0 : j >= e->ch - 8 ? ny - 1 : 1 + (int)(hash01(b->seed + b->ice * 7919u, (uint32_t)(i * 64 + j) * 2 + 1) * (ny - 2));
        gfx_tex_part(bb_tex[b->ice], x + i, y + j, ci * 8, cj * 8, 8, 8, 0, 0xFFFF, 255);
      }
    cs_draw(b->ice ? &b->cold_center : &b->hot_center, x + e->cw / 2, y + e->ch / 2, 1, 1, 0xFFFF, 255, 0);
  }
  if (b->flash > 0) {
    float k = ease_cube_out(b->flash), n = k * 2;
    gfx_rect(e->x - n, e->y - n, e->cw + n * 2, e->ch + n * 2, 0xFFFF, a8(k));
  }
}
static void bb_on_mode(Ent *e, int mode) { ST(e, Bounce)->ice_next = mode == 2; }
static void bb_awake(Ent *e) {
  Bounce *b = ST(e, Bounce);
  b->ice_next = b->ice = g_level.core_mode == 2;
}
static const EntClass BOUNCE = {.name = "bounceBlock", .size = sizeof(Bounce), .update = bb_update, .render = bb_render, .awake = bb_awake,
                                .kind = KIND_SOLID};
static void new_bounce(const EData *d) {
  Ent *e = ent_new(&BOUNCE, d->x, d->y);
  if (!e) return;
  ent_box(e, EA(d, bounceBlock, width), EA(d, bounceBlock, height), 0, 0);
  e->depth = -9000;
  Bounce *b = ST(e, Bounce);
  b->start = v2(d->x, d->y);
  b->reformed = 1;
  b->seed = (uint16_t)rndi(65536);
  cs_init(&b->hot_center, SB_bumpBlockCenterFire);
  cs_init(&b->cold_center, SB_bumpBlockCenterIce);
  if (!bb_tex[0]) {
    bb_tex[0] = tex_named("objects/BumpBlockNew/fire00");
    bb_tex[1] = tex_named("objects/BumpBlockNew/ice00");
    tex_family("objects/BumpBlockNew/fire_rubble", bb_rubble[0], 4);
    tex_family("objects/BumpBlockNew/ice_rubble", bb_rubble[1], 4);
  }
  debris_manager();
}

/* ---------------------------------------------------------------- HeartGemDoor */
typedef struct {
  float open_dist, open_pct, counter, offset, heart_alpha, co_wait, p, from[2], to[2];
  V2 mist;
  uint16_t seed;
  uint8_t requires, opened, start_hidden, co, top, bot, pad[2];
} HDoor;
static uint16_t hd_icon[7];
/* HeartGemDoor.P_Shimmer (tools/gen_particles.py does not output it) */
static const PType P_SHIMMER = {0xFFBAFFFF, 0xFF5ABCE2, PC_BLINK, PF_LATE, PR_NONE, 0, 1.0f, 0.0f, 2.0f, 5.0f, 1.0f, 1.4f, 2.0f, 0.0f,
                                1.0471976f, 0.0f, 0.0f, 0, {0.0f, 0.0f}, 0.0f, {0}, 0, 0};
static int hearts_total(void) {   /* SaveData.TotalHeartGems */
  int n = 0;
  for (int a = 0; a < AREAS; a++)
    for (int m = 0; m < 3; m++)
      if (g_save.modes[a][m].flags & MS_HEART) n++;
  return n;
}
static const EntClass DOORSOLID;
static void doorsolid_update(Ent *e) { plat_update(e); }
static const EntClass DOORSOLID = {.name = "heartGemDoorSolid", .update = doorsolid_update, .kind = KIND_SOLID};
static void hd_white_line(Ent *e);
static void hd_update(Ent *e) {
  HDoor *h = ST(e, HDoor);
  Ent *top = ent_at(h->top), *bot = ent_at(h->bot);
  bool ok = top->cls == &DOORSOLID && bot->cls == &DOORSOLID;
  Player *p = level_player();
  /* Routine() */
  if (ok && h->co) {
    if (h->co_wait > 0) h->co_wait -= DT;
    else
      switch (h->co) {
        case 1:   /* startHidden: wait for the player */
          if (!h->start_hidden) {
            h->co = 10;
            goto counting;
          }
          if (!p || fabsf(p->ent->x - e_cxm(e)) >= 100) break;
          e->visible = 1;
          h->heart_alpha = 0;
          h->to[0] = top->y, h->to[1] = bot->y;
          top->y -= 240, bot->y -= 240;
          h->from[0] = top->y, h->from[1] = bot->y;
          h->p = 0;
          h->co = 2;
          /* fall through */
        case 2:
          if (h->p < 1) {
            float k = ease_cube_in(h->p);
            plat_move_to_y(top, h->from[0] + (h->to[0] - h->from[0]) * k);
            plat_move_to_y(bot, h->from[1] + (h->to[1] - h->from[1]) * k);
            for (int i = 0; i < g_nents; i++) {
              Ent *o = &g_ents[i];
              if (o->cls && o->dead != 1 && level_is_dash_block(o) && collide_ent_at(o, o->x, o->y, bot)) {
                level_shake(0.5f);
                level_freeze(0.1f);
                level_break_dash_block(o, v2(0, 1));
                if (p && fabsf(p->ent->x - e_cxm(e)) < 40) player_point_bounce(p, v2(p->ent->x + 8, p->ent->y));
              }
            }
            h->p += DT * 1.2f;
            break;
          }
          level_shake(0.5f);
          level_freeze(0.1f);
          top->y = h->to[0], bot->y = h->to[1];
          h->co = 3;
          /* fall through */
        case 3:
          if (h->heart_alpha < 1) {
            h->heart_alpha = approach(h->heart_alpha, 1, DT * 2);
            break;
          }
          h->co_wait = 0.6f;
          h->co = 10;
          break;
        case 10:
        counting:
          if (!h->opened && h->counter < h->requires) {
            if (p && fabsf(p->ent->x - e_cxm(e)) < 80 && p->ent->x < e->x) {
              int hearts = g_save.cheat_mode ? h->requires : hearts_total();   /* HeartGems */
              if (hearts < h->requires) level_set_flag("granny_door", true);
              int was = (int)h->counter, target = hearts < h->requires ? hearts : h->requires;
              h->counter = approach(h->counter, (float)target, DT * h->requires * 0.8f);
              if (was != (int)h->counter) h->co_wait = 0.1f;
            } else
              h->counter = approach(h->counter, 0, DT * h->requires * 4);
            break;
          }
          h->co_wait = 0.5f;
          h->co = 11;
          break;
        case 11: {
          hd_white_line(e);
          level_shake(0.3f);
          level_flash_a(0xFFFF, 0.5f, false);
          h->opened = 1;
          char flag[40];
          level_set_flag(path2(flag, "opened_heartgem_door_", NULL, h->requires), true);
          h->offset = 0;
          h->co_wait = 0.6f;
          h->co = 12;
          break;
        }
        case 12:
          h->from[0] = top->y, h->to[0] = top->y - h->open_dist;
          h->from[1] = bot->y, h->to[1] = bot->y + h->open_dist;
          h->p = 0;
          h->co = 13;
          /* fall through */
        case 13:
          if (h->p < 1) {
            level_shake(0.3f);
            h->open_pct = ease_cube_in(h->p);
            plat_move_to_y(top, lerpf(h->from[0], h->to[0], h->open_pct));
            plat_move_to_y(bot, lerpf(h->from[1], h->to[1], h->open_pct));
            if (h->p >= 0.4f && level_on_interval(0.1f))
              for (int i = 4; i < e->cw; i += 4) {
                particles_emit(PL_BG, &P_SHIMMER, 1, v2(e_left(top) + i + 1, e_bottom(top) - 2), v2(2, 2), -PI_F / 2);
                particles_emit(PL_BG, &P_SHIMMER, 1, v2(e_left(bot) + i + 1, e_top(bot) + 2), v2(2, 2), PI_F / 2);
              }
            h->p += DT;
            break;
          }
          plat_move_to_y(top, h->to[0]);
          plat_move_to_y(bot, h->to[1]);
          h->open_pct = 1;
          h->co = 0;
          break;
      }
  }
  h = ST(e, HDoor);
  if (!h->opened) {
    h->offset += 12 * DT;
    h->mist.x -= 4 * DT;
    h->mist.y -= 24 * DT;
  }
}
static float fmodp(float x, float m) { return fmodf(fmodf(x, m) + m, m); }
/* DrawInterior: the color, two mists, the falling sparkles (a pixel each) */
static void hd_interior(Ent *e, float bx, float by, int w, int h) {
  HDoor *d = ST(e, HDoor);
  gfx_rect(bx, by, (float)w, (float)h, rgb(0x18668F), 255);
  /* DrawMist twice, in White * 0.6 (the mist is stored at half size: at full size it took most of the texture cache) */
  for (int pass = 0; pass < 2; pass++) {
    V2 m = pass ? v2(d->mist.y * 1.5f, d->mist.x * 1.5f) : d->mist;
    for (int i = 0; i < w; i += 160)
      for (int j = 0; j < h; j += 160) {
        int sw = 160 < w - i ? 160 : w - i, sh = 160 < h - j ? 160 : h - j;
        gfx_tex_part(T_objects_heartdoor_mist, bx + i, by + j, (int)fmodp(m.x, 160), (int)fmodp(m.y, 160), sw, sh, 0, 0xFFFF, 153);
      }
  }
  V2 cam = d->opened ? v2(0, 0) : g_level.cam;
  Room *rm = g_level.room;
  for (int i = 0; i < 50; i++) {
    float speed = (float)(4 + (int)(hash01(d->seed, 3u * i + 2) * 8));
    float px = hash01(d->seed, 3u * i) * e->cw, py = hash01(d->seed, 3u * i + 1) * rm->h + speed * g_level.time_active;
    if (d->opened) py = hash01(d->seed, 3u * i + 1) * rm->h;
    float x = fmodp(px + cam.x * 0.2f, (float)w), y = fmodp(py + cam.y * 0.2f, (float)h);
    gfx_pixel(bx + x, by + y, 0xFFFF, a8(0.2f + hash01(d->seed ^ 77u, (uint32_t)i) * 0.4f));
  }
}
static void hd_edges(Ent *e, float bx, float by, int w, int h, uint8_t alpha) {
  HDoor *d = ST(e, HDoor);
  int num = (int)fmodf(d->offset, 8);
  uint16_t edge = T_objects_heartdoor_edge, top = T_objects_heartdoor_top;
  /* DrawJustified((0.5, 0)) of 8-wide parts, the left one flipped */
  if (num > 0) {
    gfx_tex_part(edge, bx + 4 - 4, by, 0, 8 - num, 7, num, GF_FLIPX, 0xFFFF, alpha);
    gfx_tex_part(edge, bx + w - 4 - 3.5f, by, 0, 8 - num, 7, num, 0, 0xFFFF, alpha);
  }
  for (int i = num; i < h; i += 8) {
    int hh = 8 < h - i ? 8 : h - i;
    gfx_tex_part(edge, bx, by + i, 0, 0, 8, hh, GF_FLIPX, 0xFFFF, alpha);
    gfx_tex_part(edge, bx + w - 8, by + i, 0, 0, 8, hh, 0, 0xFFFF, alpha);
  }
  for (int j = 0; j < w; j += 8) {
    gfx_tex(top, bx + j, by, 0, 0xFFFF, alpha);
    gfx_tex(top, bx + j, by + h - 8, GF_FLIPY, 0xFFFF, alpha);
  }
}
static void hd_render(Ent *e) {
  HDoor *d = ST(e, HDoor);
  Ent *top = ent_at(d->top), *bot = ent_at(d->bot);
  if (top->cls != &DOORSOLID || bot->cls != &DOORSOLID) return;
  uint8_t alpha = d->opened ? 64 : 255;
  int w = (int)e->cw;
  if (!d->opened) {
    hd_interior(e, (float)(int)top->x, (float)(int)top->y, w, (int)(top->ch + bot->ch));
    hd_edges(e, (float)(int)top->x, (float)(int)top->y, w, (int)(top->ch + bot->ch), alpha);
  } else {
    hd_interior(e, (float)(int)top->x, (float)(int)top->y, w, (int)top->ch);
    hd_edges(e, (float)(int)top->x, (float)(int)top->y, w, (int)top->ch, alpha);
    hd_interior(e, (float)(int)bot->x, (float)(int)bot->y, w, (int)bot->ch);
    hd_edges(e, (float)(int)bot->x, (float)(int)bot->y, w, (int)bot->ch, alpha);
  }
  if (d->heart_alpha <= 0) return;
  float num = 12;
  int per = (int)((e->cw - 8) / num), rows = (int)ceilf((float)d->requires / per);
  uint8_t a = (uint8_t)(alpha * d->heart_alpha);
  for (int i = 0; i < rows; i++) {
    int n = (i + 1) * per < d->requires ? per : d->requires - i * per;
    V2 at = v2add(v2(e->x + e->cw * 0.5f, e->y), v2mul(v2(-n / 2.f + 0.5f, -rows / 2.f + i + 0.5f), num));
    if (d->opened) at.y += (i < rows / 2 ? -1 : 1) * (d->open_pct * d->open_dist + 8);
    for (int j = 0; j < n; j++) {
      int k = i * per + j;
      float f = ease_cube_in(clamped_map(d->counter, (float)k, k + 1.f, 0, 1));
      draw_centered(hd_icon[(int)(f * 6)], at.x + j * num, at.y, 0xFFFF, a);
    }
  }
}
/* WhiteLine: a line across the screen that thins out, then sparkles along it */
typedef struct { float fade; int16_t size, pad; } WhiteLine;
static void wl_update(Ent *e) {
  WhiteLine *w = ST(e, WhiteLine);
  w->fade = approach(w->fade, 0, DT);
  if (w->fade > 0) return;
  ent_remove(e);
  for (float x = (float)(int)g_level.cam.x; x < g_level.cam.x + 320; x += 1)
    if (x < e->x || x >= e->x + w->size) particles_emit1(PL_MID, &P_HeartGemDoor_P_Slice, v2(x, e->y), 0);
}
static void wl_render(Ent *e) {
  WhiteLine *w = ST(e, WhiteLine);
  float n = fmaxf(1, 4 * w->fade);
  gfx_rect(g_level.cam.x - 10, e->y - n / 2, 340, n, 0xFFFF, 255);
}
static const EntClass WHITELINE = {.name = "heartGemDoorWhiteLine", .size = sizeof(WhiteLine), .update = wl_update, .render = wl_render};
static void hd_white_line(Ent *e) {
  Ent *w = ent_new(&WHITELINE, e->x, e->y);
  if (!w) return;
  w->depth = -1000000;
  w->collidable = 0;
  ST(w, WhiteLine)->fade = 1;
  ST(w, WhiteLine)->size = (int16_t)e->cw;
}
static void hd_awake(Ent *e) {
  HDoor *h = ST(e, HDoor);
  Ent *bot = ent_at(h->bot);
  if (bot->cls != &DOORSOLID) return;
  if (h->opened) {
    for (int i = 0; i < g_nents; i++)
      if (g_ents[i].cls && level_is_dash_block(&g_ents[i]) && collide_ent_at(&g_ents[i], g_ents[i].x, g_ents[i].y, bot)) ent_remove(&g_ents[i]);
  } else if (h->start_hidden) {
    Ent *pe = level_player_ent();
    if (pe && pe->x > e->x) {
      h->start_hidden = 0;
      for (int i = 0; i < g_nents; i++)
        if (g_ents[i].cls && level_is_dash_block(&g_ents[i]) && collide_ent_at(&g_ents[i], g_ents[i].x, g_ents[i].y, bot))
          ent_remove(&g_ents[i]);
    } else
      e->visible = 0;
  }
}
static const EntClass HDOOR = {.name = "heartGemDoor", .size = sizeof(HDoor), .update = hd_update, .render = hd_render, .awake = hd_awake};
static void new_hdoor(const EData *d) {
  Room *rm = NULL;
  for (int s = 0; s < 2; s++)
    if (g_level.rooms[s].index == d->room) rm = &g_level.rooms[s];
  if (!rm) rm = g_level.room;
  float w = EA(d, heartGemDoor, width);
  Ent *e = ent_new(&HDOOR, d->x, d->y);
  if (!e) return;
  ent_box(e, w, 0, 0, 0);
  e->collidable = 0;
  HDoor *h = ST(e, HDoor);
  h->requires = (uint8_t)EA(d, heartGemDoor, requires);
  h->open_dist = d->nnodes ? fabsf(ed_node(d, 0).y - d->y) : 32;
  h->start_hidden = EAB(d, heartGemDoor, startHidden);
  h->heart_alpha = 1;
  h->seed = (uint16_t)rndi(65536);
  h->co = 1;
  if (!hd_icon[0]) tex_family("objects/heartdoor/icon", hd_icon, 7);
  /* Added: the two solids above and below */
  Ent *top = ent_new(&DOORSOLID, d->x, rm->y - 32);
  Ent *bot = ent_new(&DOORSOLID, d->x, d->y);
  h = ST(e, HDoor);
  if (!top || !bot) return;
  ent_box(top, w, d->y - rm->y + 32, 0, 0);
  ent_box(bot, w, rm->y + rm->h - d->y + 32, 0, 0);
  top->safe = bot->safe = 1;
  top->visible = bot->visible = 0;
  h->top = ent_ref(top), h->bot = ent_ref(bot);
  char flag[40];
  if (level_get_flag(path2(flag, "opened_heartgem_door_", NULL, h->requires))) {
    h->opened = 1;
    h->open_pct = 1;
    h->counter = h->requires;
    top->y -= h->open_dist;
    bot->y += h->open_dist;
    h->co = 0;
  }
}

/* ---------------------------------------------------------------- the core mode's listeners */
static void core_listeners(int mode) {
  for (int i = 0; i < g_nents; i++) {
    Ent *e = &g_ents[i];
    if (!e->cls || e->dead == 1) continue;
    if (e->cls == &TOGGLE) toggle_on_mode(e, mode);
    else if (e->cls == &FIRE) {
      if (!ST(e, Fire)->not_core) fire_on_mode(e, mode);
    } else if (e->cls == &WALLBOOSTER)
      wb_on_mode(e, mode);
    else if (e->cls == &FIREBARRIER || e->cls == &ICEBLOCK)
      barrier_on_mode(e, mode);
    else if (e->cls == &RISING2 || e->cls == &SANDWICH)
      rising_on_mode(e, mode);
    else if (e->cls == &BOUNCE)
      bb_on_mode(e, mode);
  }
}

/* ---------------------------------------------------------------- triggers */
typedef struct { V2 target, lerp; uint8_t mode_x, mode_y, xonly, yonly; } AdvTrig;
static float pos_lerp(Ent *e, Player *p, int mode) {   /* Trigger.GetPositionLerp */
  float px = e_cxm(p->ent), py = e_cym(p->ent);
  float l = e_left(e), r = e_right(e), t = e_top(e), b = e_bottom(e), cx = e_cxm(e), cy = e_cym(e);
#define CMAP(v, a, bb) clampf(((v) - (a)) / ((bb) - (a)), 0, 1)
  switch (mode) {
    case 1: return CMAP(px, l, r);
    case 2: return CMAP(px, r, l);
    case 3: return CMAP(py, t, b);
    case 4: return CMAP(py, b, t);
    case 5: return fminf(CMAP(px, l, cx), CMAP(px, r, cx));
    case 6: return fminf(CMAP(py, t, cy), CMAP(py, b, cy));
    default: return 1;
  }
#undef CMAP
}
static int pos_mode(const char *m) {
  static const char *MODES[] = {"NoEffect", "LeftToRight", "RightToLeft", "TopToBottom", "BottomToTop", "HorizontalCenter", "VerticalCenter"};
  for (int i = 0; i < 7; i++)
    if (!strcmp(m, MODES[i])) return i;
  return 0;
}
static void adv_stay(Ent *e, Player *p) {
  AdvTrig *t = ST(e, AdvTrig);
  p->cam_anchor = t->target;
  p->cam_anchor_lerp = v2(clampf(t->lerp.x * pos_lerp(e, p, t->mode_x), 0, 1), clampf(t->lerp.y * pos_lerp(e, p, t->mode_y), 0, 1));
  p->cam_anchor_ignore_x = t->yonly;
  p->cam_anchor_ignore_y = t->xonly;
}
static void adv_leave(Ent *e, Player *p) {
  for (int i = 0; i < g_nents; i++) {
    Ent *o = &g_ents[i];
    if (o == e || !o->cls || o->dead == 1 || !o->triggered) continue;
    if (!strcmp(o->cls->name, "cameraTargetTrigger") || !strcmp(o->cls->name, "cameraAdvanceTargetTrigger")) return;
  }
  p->cam_anchor_lerp = v2(0, 0);
}
static const EntClass ADVTRIG = {.name = "cameraAdvanceTargetTrigger", .size = sizeof(AdvTrig), .kind = KIND_TRIGGER,
                                 .more = &(const EntMore){.on_stay = adv_stay, .on_leave = adv_leave}};
typedef struct { uint8_t state; } NoRefill;
static void norefill_enter(Ent *e, Player *p) {
  g_session.no_refills = ST(e, NoRefill)->state;   /* Session.Inventory.NoRefills */
  p->inventory_norefills = ST(e, NoRefill)->state;
}
static const EntClass NOREFILL = {.name = "noRefillTrigger", .size = sizeof(NoRefill), .kind = KIND_TRIGGER,
                                  .more = &(const EntMore){.on_enter = norefill_enter}};

/* ---------------------------------------------------------------- CoreMessage */
typedef struct { float alpha; uint8_t line; } CoreMsg;
/* Dialog.Clean("app_ending").Split('\n')[line] (empty lines left out) */
static int core_line(int line, char *out, int cap) {
  char all[200];
  int n = dialog_clean("app_ending", all, sizeof all), k = 0, at = 0;
  for (int i = 0; i < n; i++) {
    if (all[i] == '\n') {
      if (i > 0 && all[i - 1] != '\n') k++;
      continue;
    }
    if (k == line && at < cap - 1) out[at++] = all[i];
  }
  out[at] = 0;
  return at;
}
static void coremsg_update(Ent *e) {
  Player *p = level_player();
  if (p) ST(e, CoreMsg)->alpha = ease_cube_inout(clamped_map(fabsf(e->x - p->ent->x), 0, 128, 1, 0));
}
static void coremsg_render(Ent *e) {
  CoreMsg *m = ST(e, CoreMsg);
  char text[64];
  if (m->alpha <= 0 || !core_line(m->line, text, sizeof text)) return;
  float x = e->x - g_level.cam.x, y = e->y - g_level.cam.y;
  gfx_hud(true);
  font_draw_justified(text, x + (x - 160) * 0.2f, y + (y - 90) * 0.2f, 0.5f, 0.5f, FONT_S, 0xFFFF, (uint8_t)(m->alpha * 255));
  gfx_hud(false);
}
static const EntClass COREMSG = {.name = "coreMessage", .size = sizeof(CoreMsg), .update = coremsg_update, .render = coremsg_render};

/* ---------------------------------------------------------------- NPC09_Granny_Inside and _Outside */
/* Session.GetCounter / IncrementCounter */
static int32_t *session_counter(const char *name) {
  uint32_t h = 2166136261u;
  for (const char *s = name; *s; s++) h = (h ^ (uint8_t)*s) * 16777619u;
  h |= 1;
  for (int i = 0; i < 3; i++)
    if (g_session.counters[i].key == h) return &g_session.counters[i].value;
  for (int i = 0; i < 3; i++)
    if (!g_session.counters[i].key) {
      g_session.counters[i].key = h, g_session.counters[i].value = 0;
      return &g_session.counters[i].value;
    }
  return &g_session.counters[2].value;
}
enum { G_CONV, G_TALKING, G_LEAVING };   /* the grannies' Npc.i[] */
static bool granny_door_talk(void) { return level_get_flag("granny_door") && !level_get_flag("granny_door_done"); }
static bool granny_talker_on(Ent *e) {
  int conv = NPC_(e)->i[G_CONV];
  return conv <= 0 || conv >= 4 ? granny_door_talk() : true;
}
typedef struct {
  Cutscene cs;
  Ent *granny, *tb;
  Walk walk;
  Step step;
  V2 zoom_at;
  Co ev;
  int8_t ev_i;
  uint8_t door;
} GTalk;
static bool gtalk_nb(GTalk *s, Co *c, int i) {
  Npc *n = NPC_(s->granny);
  if (n->which == NPC_GRANNY09_OUT) {
    if (i == 0) {   /* MoveRight */
      NB_BEGIN(c);
      npc_move_to(s->granny, v2(s->granny->x + 8, s->granny->y), false, 0, false);
      NB_AWAIT(c, &n->mv);
      NB_END(c);
    }
    NB_BEGIN(c);   /* ExitRight */
    n->i[G_LEAVING] = 1;
    npc_move_to(s->granny, v2(g_level.room->x + g_level.room->w + 16, s->granny->y), false, 0, false);
    NB_YIELD(c);
    NB_END(c);
  }
  NB_BEGIN(c);   /* StartLaughing, StopLaughing */
  spr_play(&n->spr, i == 0 ? A_granny_laugh : A_granny_idle, false);
  NB_YIELD(c);
  NB_END(c);
}
static bool gtalk_ev(void *ctx, int i) {
  GTalk *s = ST((Ent *)ctx, GTalk);
  return !gtalk_nb(s, ev_co(&s->ev, &s->ev_i, i), i);
}
static const char *const GRANNY_TALKS[4] = {"APP_OLDLADY_B", "APP_OLDLADY_C", "APP_OLDLADY_D", "APP_OLDLADY_E"};
static void gtalk_update(Ent *e) {
  GTalk *s = ST(e, GTalk);
  Player *p = &g_player;
  Npc *n = NPC_(s->granny);
  Co *c = &s->cs.co;
  CO_BEGIN(c);
  player_set_state(p, ST_DUMMY);
  s->ev_i = -1;
  if (n->which == NPC_GRANNY09_OUT) {   /* NPC09_Granny_Outside.TalkRoutine */
    while (!actor_on_ground(p->ent, 1)) CO_YIELD(c);
    n->spr.sx = -1;
    CO_WALK(c, s->walk, walk_exact((int)s->granny->x - 16));
    CO_WAIT(c, 0.5f);
    CO_ZOOM_TO(c, &s->step, v2(200, 110), 2, 0.5f);
    CO_SAY(c, s->tb, "APP_OLDLADY_A", gtalk_ev, e);
    CO_ZOOM_BACK(c, &s->step, 0.5f);
    n->spr.sx = 1;
    if (!n->i[G_LEAVING]) {   /* yield return ExitRight() */
      CO_YIELD(c);
      n->i[G_LEAVING] = 1;
      npc_move_to(s->granny, v2(g_level.room->x + g_level.room->w + 16, s->granny->y), false, 0, false);
      CO_YIELD(c);
      CO_YIELD(c);
    }
    while (s->granny->x < g_level.room->x + g_level.room->w + 8) CO_YIELD(c);
  } else {   /* NPC09_Granny_Inside.TalkRoutine */
    p->dashes = 1;
    p->force_camera_update = true;
    while (!actor_on_ground(p->ent, 1)) CO_YIELD(c);
    CO_WALK(c, s->walk, walk_exact((int)s->granny->x - 16));
    p->facing = 1;
    p->force_camera_update = false;
    s->zoom_at = v2(s->granny->x - 8 - g_level.cam.x, 110);
    s->door = granny_door_talk();
    if (s->door) {
      n->spr.sx = -1;
      CO_ZOOM_TO(c, &s->step, s->zoom_at, 2, 0.5f);
      CO_SAY(c, s->tb, "APP_OLDLADY_LOCKED", NULL, NULL);
    } else if (n->i[G_CONV] < 4) {
      if (n->i[G_CONV] == 0) {
        CO_WAIT(c, 0.5f);
        n->spr.sx = -1;
        CO_WAIT(c, 0.25f);
      } else
        n->spr.sx = -1;
      CO_ZOOM_TO(c, &s->step, s->zoom_at, 2, 0.5f);
      CO_SAY(c, s->tb, GRANNY_TALKS[n->i[G_CONV]], gtalk_ev, e);
    }
    n->talk.enabled = granny_talker_on(s->granny);
    CO_ZOOM_BACK(c, &s->step, 0.5f);
  }
  cutscene_end(e);
  return;
  CO_END(c);
}
static void gtalk_end(Ent *e, bool skipped) {   /* EndTalking */
  GTalk *s = ST(e, GTalk);
  Npc *n = NPC_(s->granny);
  (void)skipped;
  player_set_state(&g_player, ST_NORMAL);
  if (n->which == NPC_GRANNY09_OUT) {
    level_set_flag("granny_outside", true);
    ent_remove(s->granny);
    return;
  }
  g_player.force_camera_update = false;
  if (granny_door_talk()) level_set_flag("granny_door_done", true);
  else {
    ++*session_counter("granny");
    n->i[G_CONV]++;
  }
  spr_play(&n->spr, A_granny_idle, false);
  n->i[G_TALKING] = 0;
}
static const EntClass GTALK = {.name = "grannyTalk", .size = sizeof(GTalk), .update = gtalk_update};
static void granny9_talk(Ent *e) {   /* OnTalk: Level.StartCutscene(EndTalking) and TalkRoutine */
  Ent *cs = scene_new(&GTALK, gtalk_end, true, false);
  if (!cs) return;
  ST(cs, GTalk)->granny = e;
  NPC_(e)->i[G_TALKING] = 1;
}
static void granny9_think(Ent *e) {
  Npc *n = NPC_(e);
  Player *p = level_player();
  if (n->which == NPC_GRANNY09_OUT) {
    if (!n->i[G_TALKING] && p && p->ent->x > e->x - 48) granny9_talk(e);
    return;
  }
  if (!n->i[G_TALKING] && n->i[G_CONV] == 0 && p && fabsf(p->ent->x - e->x) < 48) granny9_talk(e);
  n->talk.enabled = granny_talker_on(e);
}
static bool new_granny9(const EData *d) {
  const char *who = EAS(d, npc, npc);
  bool out = !strcmp(who, "granny_09_outside");
  if (!out && strcmp(who, "granny_09_inside")) return false;
  if (out && level_get_flag("granny_outside")) return true;
  Ent *e = npc_new(d->x, d->y, SB_granny, A_granny_idle, granny9_think);
  if (!e) return true;
  Npc *n = NPC_(e);
  n->which = out ? NPC_GRANNY09_OUT : NPC_GRANNY09_IN;
  n->move_anim = A_granny_walk, n->maxspeed = 40;
  if (out) n->idle_anim = A_granny_idle;
  else {
    npc_talker(e, -20, -8, 40, 8, v2(0, -24), granny9_talk);
    n->talk.enabled = false;
    n->i[G_CONV] = *session_counter("granny");
  }
  npc_haha(e);
  return true;
}

/* ---------------------------------------------------------------- the factories */
bool ents_ch9(const EData *d) {
  switch (d->type) {
    case ET_coreModeToggle: new_toggle(d); return true;
    case ET_fireBall: new_fireball(d); return true;
    case ET_wallBooster: new_wallbooster(d); return true;
    case ET_bounceBlock: new_bounce(d); return true;
    case ET_fireBarrier: new_barrier(d, false); return true;
    case ET_iceBlock: new_barrier(d, true); return true;
    case ET_risingLava: new_rising(d, false); return true;
    case ET_sandwichLava: new_rising(d, true); return true;
    case ET_heartGemDoor: new_hdoor(d); return true;
    case ET_coreMessage: {
      Ent *e = ent_new(&COREMSG, d->x, d->y);
      if (e) e->tags = TAG_HUD, e->collidable = 0, ST(e, CoreMsg)->line = (uint8_t)EA(d, coreMessage, line);
      return true;
    }
    case ET_npc: return new_granny9(d);
  }
  return false;
}

bool trigs_ch9(const EData *d) {
  switch (d->type) {
    case TT_cameraAdvanceTargetTrigger: {
      Ent *e = ent_new(&ADVTRIG, d->x, d->y);
      if (!e) return true;
      ent_box(e, EA(d, cameraAdvanceTargetTrigger, width), EA(d, cameraAdvanceTargetTrigger, height), 0, 0);
      e->visible = 0;
      AdvTrig *t = ST(e, AdvTrig);
      t->target = v2sub(ed_node(d, 0), v2(160, 90));
      t->lerp = v2(EA(d, cameraAdvanceTargetTrigger, lerpStrengthX), EA(d, cameraAdvanceTargetTrigger, lerpStrengthY));
      t->mode_x = (uint8_t)pos_mode(EAS(d, cameraAdvanceTargetTrigger, positionModeX));
      t->mode_y = (uint8_t)pos_mode(EAS(d, cameraAdvanceTargetTrigger, positionModeY));
      t->xonly = EAB(d, cameraAdvanceTargetTrigger, xOnly);
      t->yonly = EAB(d, cameraAdvanceTargetTrigger, yOnly);
      return true;
    }
    case TT_noRefillTrigger: {
      Ent *e = ent_new(&NOREFILL, d->x, d->y);
      if (!e) return true;
      ent_box(e, EA(d, noRefillTrigger, width), EA(d, noRefillTrigger, height), 0, 0);
      e->visible = 0;
      ST(e, NoRefill)->state = EAB(d, noRefillTrigger, state);
      return true;
    }
  }
  return false;
}

/* this chapter's tables: in the memory it is given while it is played (res_chapter_ram) */
uint32_t ch9_ram(void) { return END_debris; }
