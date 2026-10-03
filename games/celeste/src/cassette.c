#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("Os")   /* not drawn every frame: smaller over faster */
#endif
/* Cassettes: the Cassette (B-side tape) with its message, CassetteBlock and
 * CassetteBlockManager (blocks in time with the music, here with its clock).
 * Line by line from the game's code. */
#include "cutscene.h"
#include "text.h"

/* ---------------------------------------------------------------- the memory every chapter has */
typedef struct {
  /* Cassette.CollectRoutine (one at a time) */
  Co co;
  float p, alpha, timer, wait;
  uint8_t phase;
  V2 cam_was, cam_to, from, to;
  Ent *cassette, *msg;
  bool wait_key;
  Step zoom;
  char text[120];
  TextDraw td;
  /* Player.StartCassetteFly */
  CassetteFly fly;
} CasRam;
#define CR (*(CasRam *)(void *)g_chram[14])
uint32_t cassette_ram(void) { return sizeof(CasRam); }
CassetteFly *cassette_fly(void) { return &CR.fly; }

/* ---------------------------------------------------------------- CassetteBlockManager */
typedef struct {
  int current, beat, lead_beats, max_beat, offset;
  float beat_timer;
  uint8_t started;   /* the song's instance exists (its first update only makes it) */
} Cbm;
static const EntClass CBM;
static const EntClass BLOCK;
static Ent *first_of(const EntClass *c) {
  for (int i = 0; i < g_nents; i++)
    if (g_ents[i].cls == c && g_ents[i].dead != 1) return &g_ents[i];
  return NULL;
}
#define FOR_BLOCKS(b) \
  for (Ent *b = g_ents; b < g_ents + g_nents; b++) \
    if (b->cls == &BLOCK && b->dead != 1)

typedef struct {
  uint8_t index, activated, leader_flag, ncx, ncy;
  int8_t block_height;
  uint16_t leader, side;   /* g_ents indices */
  int16_t lox, loy;        /* the group's origin, from the block */
  float wsx, wsy;          /* wigglerScaler (the leader's) */
  Wiggler wig;             /* the leader's */
  uint8_t cells[40];       /* per 8x8 cell: the tile of the sheet (tx | ty << 2), 12 for none */
} Block;
#define NO_TILE 12

static void block_set_activated_silently(Ent *e, bool on);
static void block_will_toggle(Ent *e);
static bool cassette_should_manage(void) { return g_session.mode != M_A || !g_session.cassette; }

static void cbm_set_active(int index) {
  FOR_BLOCKS(b) ST(b, Block)->activated = ST(b, Block)->index == index;
}
static void cbm_set_will_activate(int index) {
  FOR_BLOCKS(b) if (ST(b, Block)->index == index || ST(b, Block)->activated) block_will_toggle(b);
}
static void cbm_update(Ent *e) {
  Cbm *m = ST(e, Cbm);
  if (!m->started) {   /* the cassette song's instance is made: nothing advances this update */
    m->started = 1;
    return;
  }
  m->beat_timer += DT;   /* AdvanceMusic(DeltaTime * tempoMult): the tempo is 1 in every map */
  if (!(m->beat_timer >= 1 / 6.f)) return;
  m->beat_timer -= 1 / 6.f;
  m->beat = (m->beat + 1) % 256;
  if (m->beat % 8 == 0) {
    m->current = (m->current + 1) % m->max_beat;
    cbm_set_active(m->current);
  } else if ((m->beat + 1) % 8 == 0)
    cbm_set_will_activate((m->current + 1) % m->max_beat);
  if (m->lead_beats > 0 && --m->lead_beats == 0) m->beat = 0;
}
static const EntClass CBM = {.name = "cassetteBlockManager", .size = sizeof(Cbm), .update = cbm_update};

static Ent *cbm_new(void) {
  Ent *e = ent_new(&CBM, 0, 0);
  if (!e) return NULL;
  Cbm *m = ST(e, Cbm);
  e->tags = TAG_GLOBAL;
  /* Awake: the areas with a cassette song lead in 16 beats; the others follow the level's music */
  bool level_music = g_session.area == 0 || g_session.area == 8 || g_session.area >= 10;
  m->lead_beats = level_music ? 0 : 16;
  m->offset = level_music ? 5 : 0;
  m->max_beat = g_level.cassette_beats;
  return e;
}

/* the end of LoadLevel, and TransitionListener.OnOutBegin */
void cassette_level_start(bool transition) {
  Ent *e = first_of(&CBM);
  if (!e) return;
  Cbm *m = ST(e, Cbm);
  if (transition && !g_level.has_cassette_blocks) {
    ent_remove(e);
    return;
  }
  if (!g_level.has_cassette_blocks || !cassette_should_manage()) return;
  /* OnLevelStart */
  m->max_beat = g_level.cassette_beats;
  m->current = m->beat % 8 >= 5 ? m->max_beat - 2 : m->max_beat - 1;
  FOR_BLOCKS(b) if (b->room == g_level.room_slot) block_set_activated_silently(b, ST(b, Block)->index == m->current);
}

/* ---------------------------------------------------------------- CassetteBlock */
static const uint32_t COLORS[4] = {0x49aaf0, 0xf049be, 0xfcdc3a, 0x38e04e};
static uint16_t block_color(const Block *b) { return rgb(COLORS[b->index < 4 ? b->index : 0]); }
static uint16_t block_disabled_color(const Block *b) {   /* 667da5 times the color */
  uint32_t c = COLORS[b->index < 4 ? b->index : 0];
  int r = (int)(c >> 16) * 0x66 / 255, g = (int)(c >> 8 & 255) * 0x7d / 255, bl = (int)(c & 255) * 0xa5 / 255;
  return rgb((uint32_t)(r << 16 | g << 8 | bl));
}
/* a cassette block's color for what it carries: enabled or disabled (none: white) */
uint16_t cassette_tint(const Ent *blk, bool on) {
  if (!blk || !blk->cls || strcmp(blk->cls->name, "cassetteBlock")) return 0xFFFF;
  return on ? block_color(ST(blk, Block)) : block_disabled_color(ST(blk, Block));
}
static Ent *block_leader(const Ent *e) { return &g_ents[ST(e, Block)->leader]; }

static void block_shift(Ent *e, int amount) {   /* ShiftSize */
  plat_move_v(e, (float)amount);
  ST(e, Block)->block_height = (int8_t)(ST(e, Block)->block_height - amount);
}

/* TryActorWiggleUp */
static bool block_wiggle_up(Ent *e, Ent *actor) {
  Block *b = ST(e, Block);
  FOR_BLOCKS(o) if (o != e && ST(o, Block)->leader == b->leader && collide_ent_at(o, o->x, o->y + 4, actor)) return false;
  bool was = e->collidable;
  e->collidable = 1;
  for (int i = 1; i <= 4; i++)
    if (!collide_solid(actor, actor->x, actor->y - i)) {
      actor->y -= (float)i;
      e->collidable = was;
      return true;
    }
  e->collidable = was;
  return false;
}
static bool block_blocked(Ent *e) {   /* BlockedCheck */
  for (Ent *a = g_ents; a < g_ents + g_nents; a++)
    if (a->cls && a->dead != 1 && (a->kind & KIND_HOLDABLE) && (a->kind & KIND_ACTOR) && collide_ent_at(e, e->x, e->y, a) &&
        !block_wiggle_up(e, a))
      return true;
  Ent *p = level_player_ent();
  return p && collide_ent_at(e, e->x, e->y, p) && !block_wiggle_up(e, p);
}

static void block_visual(Ent *e) {   /* UpdateVisualState */
  Block *b = ST(e, Block);
  if (!e->collidable) e->depth = 8990;
  else {
    Ent *p = level_player_ent();
    e->depth = p && e_top(p) >= e_bottom(e) - 1 ? 10 : -10;
  }
  for (Ent *m = g_ents; m < g_ents + g_nents; m++)
    if (m->cls && ent_platform(m) == e) m->depth = e->depth + 1;
  Ent *side = &g_ents[b->side];
  if (side->cls) side->depth = e->depth + 5, side->visible = b->block_height > 0;
}

static void block_set_activated_silently(Ent *e, bool on) {
  Block *b = ST(e, Block);
  b->activated = on;
  e->collidable = on;
  block_visual(e);
  if (on) plat_static_movers_enable(e, true);
  else {
    block_shift(e, 2);
    plat_static_movers_enable(e, false);
  }
}
static void block_will_toggle(Ent *e) {
  block_shift(e, e->collidable ? 1 : -1);
  block_visual(e);
}
void cassette_blocks_finish(void) {   /* CassetteBlockManager.StopBlocks */
  FOR_BLOCKS(b) ST(b, Block)->activated = 0;
}

static void block_update(Ent *e) {
  Block *b = ST(e, Block);
  plat_update(e);
  if (b->leader_flag) wiggler_update(&b->wig);
  if (b->leader_flag && b->activated && !e->collidable) {
    bool blocked = false;
    FOR_BLOCKS(o) if (ST(o, Block)->leader == b->leader && block_blocked(o)) {
      blocked = true;
      break;
    }
    if (!blocked) {
      FOR_BLOCKS(o) if (ST(o, Block)->leader == b->leader) {
        o->collidable = 1;
        plat_static_movers_enable(o, true);
        block_shift(o, -1);
      }
      wiggler_restart(&b->wig);
    }
  } else if (!b->activated && e->collidable) {
    block_shift(e, 1);
    e->collidable = 0;
    plat_static_movers_enable(e, false);
  }
  block_visual(e);
}

/* the group's wiggle (the leader's): scale around the group's origin */
static V2 block_scale(const Ent *e) {
  const Block *l = ST(block_leader(e), Block);
  return v2(1 + l->wig.value * 0.05f * l->wsx, 1 + l->wig.value * 0.15f * l->wsy);
}
/* for the spikes on a block (Spikes.SetOrigins): its group's scale, and its origin */
bool cassette_block_scale(const Ent *e, V2 *origin, V2 *scale) {
  if (!e || e->cls != &BLOCK) return false;
  *origin = v2(e->x + ST(e, Block)->lox, e->y + ST(e, Block)->loy);
  *scale = block_scale(e);
  return true;
}

static uint16_t block_tex(const Ent *e) {
  return e->collidable ? T_objects_cassetteblock_solid : (uint16_t)(T_objects_cassetteblock_pressed00 + ST(e, Block)->index % 4);
}
static void block_strip(uint16_t *strip, int y0, int y1, void *ctx) {
  Ent *e = ctx;
  Block *b = ST(e, Block);
  Tex t;
  if (!tex_get(block_tex(e), &t)) return;
  V2 s = block_scale(e);
  float x = floorf(e->x + 0.5f) - g_camx, y = floorf(e->y + 0.5f) - g_camy;
  blit_cells(strip, y0, y1, &t, x, y, b->ncx, b->ncy, b->cells, x + b->lox, y + b->loy, s.x, s.y, block_color(b), 255);
}
static void block_render(Ent *e) {
  Block *b = ST(e, Block);
  Tex t;
  if (!tex_get(block_tex(e), &t)) return;   /* (loaded before the strips are drawn) */
  V2 s = block_scale(e);
  float y = floorf(e->y + 0.5f) - g_camy, gy = y + b->loy;
  float top = gy + (y - gy) * s.y, bot = gy + (y + b->ncy * 8 - gy) * s.y;
  gfx_custom(block_strip, e, (int)floorf(top) - 1, (int)ceilf(bot) + 1);
}
static void side_render(Ent *e) {   /* BoxSide */
  Ent *blk = &g_ents[ST(e, uint16_t)[0]];
  if (blk->cls != &BLOCK) return;
  Block *b = ST(blk, Block);
  gfx_rect(blk->x, e_bottom(blk) - 8, blk->cw, (float)(8 + b->block_height), block_disabled_color(b), 255);
}
static const EntClass SIDE = {.name = "boxSide", .size = sizeof(uint16_t) * 2, .render = side_render};

static bool same_at(const Ent *e, float x, float y) {   /* CheckForSame */
  FOR_BLOCKS(o) if (ST(o, Block)->index == ST(e, Block)->index && collide_rect(o, x, y, x + 8, y + 8)) return true;
  return false;
}
static void find_in_group(Ent *leader, Ent *blk) {   /* FindInGroup */
  uint16_t li = (uint16_t)(leader - g_ents);
  FOR_BLOCKS(o) {
    if (o == leader || o == blk || ST(o, Block)->leader == li || ST(o, Block)->index != ST(leader, Block)->index) continue;
    if (collide_rect(o, blk->x - 1, blk->y, blk->x + blk->cw + 1, blk->y + blk->ch) ||
        collide_rect(o, blk->x, blk->y - 1, blk->x + blk->cw, blk->y + blk->ch + 1)) {
      ST(o, Block)->leader = li;
      find_in_group(leader, o);
    }
  }
}

static void block_awake(Ent *e) {
  Block *b = ST(e, Block);
  plat_static_movers_attach(e);
  uint16_t me = (uint16_t)(e - g_ents);
  Ent *side = ent_new(&SIDE, e->x, e->y);
  if (side) {
    ST(side, uint16_t)[0] = me;
    b->side = (uint16_t)(side - g_ents);
    side->room = e->room;
  }
  if (b->leader == 0xFFFF) {   /* no group yet: this one leads it */
    b->leader_flag = 1;
    b->leader = me;
    find_in_group(e, e);
    float l = 1e9f, r = -1e9f, t = 1e9f, bo = -1e9f;
    FOR_BLOCKS(o) if (ST(o, Block)->leader == me) {
      l = fminf(l, o->x), r = fmaxf(r, o->x + o->cw), t = fminf(t, o->y), bo = fmaxf(bo, o->y + o->ch);
    }
    float gx = (float)(int)(l + (r - l) / 2), gy = (float)(int)bo;
    b->wsx = clamped_map(r - l, 32, 96, 1, 0.2f), b->wsy = clamped_map(bo - t, 32, 96, 1, 0.2f);
    wiggler_init(&b->wig, 0.3f, 3);
    FOR_BLOCKS(o) if (ST(o, Block)->leader == me) {
      ST(o, Block)->lox = (int16_t)(gx - o->x), ST(o, Block)->loy = (int16_t)(gy - o->y);
    }
  }
  /* the spikes on it: its colors, seen while disabled, scaled with the group */
  for (Ent *m = g_ents; m < g_ents + g_nents; m++)
    if (m->cls && ent_platform(m) == e && (m->kind & KIND_SPIKES))
      spikes_set_cassette(m, v2(e->x + b->lox, e->y + b->loy));
  /* the sheet's tiles: corners, edges, inner corners */
  for (int cy = 0; cy < b->ncy; cy++)
    for (int cx = 0; cx < b->ncx; cx++) {
      float x = e->x + cx * 8, y = e->y + cy * 8;
      bool l = same_at(e, x - 8, y), r = same_at(e, x + 8, y), u = same_at(e, x, y - 8), d = same_at(e, x, y + 8);
      int tx = -1, ty = 0;
      if (l && r && u && d) {
        if (!same_at(e, x + 8, y - 8)) tx = 3, ty = 0;
        else if (!same_at(e, x - 8, y - 8)) tx = 3, ty = 1;
        else if (!same_at(e, x + 8, y + 8)) tx = 3, ty = 2;
        else if (!same_at(e, x - 8, y + 8)) tx = 3, ty = 3;
        else tx = 1, ty = 1;
      } else if (l && r && !u && d) tx = 1, ty = 0;
      else if (l && r && u && !d) tx = 1, ty = 2;
      else if (l && !r && u && d) tx = 2, ty = 1;
      else if (!l && r && u && d) tx = 0, ty = 1;
      else if (l && !r && !u && d) tx = 2, ty = 0;
      else if (!l && r && !u && d) tx = 0, ty = 0;
      else if (l && !r && u && !d) tx = 2, ty = 2;
      else if (!l && r && u && !d) tx = 0, ty = 2;
      int i = cy * b->ncx + cx, v = tx < 0 ? NO_TILE : tx | ty << 2;
      b->cells[i >> 1] = (uint8_t)((b->cells[i >> 1] & ~(15 << ((i & 1) * 4))) | v << ((i & 1) * 4));
    }
  block_visual(e);
}

static const EntClass BLOCK = {.name = "cassetteBlock", .size = sizeof(Block), .update = block_update, .render = block_render,
                               .awake = block_awake, .kind = KIND_SOLID};

static bool block_new(const EData *d) {
  int w = (int)EA(d, cassetteBlock, width), h = (int)EA(d, cassetteBlock, height);
  int index = (int)EA(d, cassetteBlock, index);
  if ((w / 8) * (h / 8) > 80) return true;   /* (none so big in the maps) */
  Ent *e = ent_new(&BLOCK, d->x, d->y);
  if (!e) return true;
  Block *b = ST(e, Block);
  ent_box(e, (float)w, (float)h, 0, 0);
  e->collidable = 0;
  b->index = (uint8_t)index;
  b->ncx = (uint8_t)(w / 8), b->ncy = (uint8_t)(h / 8);
  b->block_height = 2;
  b->leader = 0xFFFF;
  b->side = 0;
  /* Level.LoadLevel */
  g_level.has_cassette_blocks = true;
  if (index + 1 > g_level.cassette_beats) g_level.cassette_beats = (uint8_t)(index + 1);
  if (!first_of(&CBM) && cassette_should_manage()) cbm_new();
  return true;
}

/* ---------------------------------------------------------------- Cassette */
typedef struct {
  Sprite spr;
  SineWave hover;
  Wiggler wig;
  int16_t n0x, n0y, n1x, n1y;
  uint8_t ghost, collected, collecting, nodes;
} Cas;

/* UnlockedBSide: the message, in the interface */
static void msg_update(Ent *e) {
  (void)e;
  CR.timer += DT;
}
static void msg_render(Ent *e) {
  (void)e;
  float num = ease_cube_out(CR.alpha);
  uint8_t a = (uint8_t)(num * 255);
  float k = 64 * (1 - num);
  gfx_hud(true);
  gfx_rect(0, 0, VIEW_W, VIEW_H, 0, (uint8_t)(num * 0.8f * 255));
  Tex t;
  if (tex_get(T__collectables_cassette, &t))
    gfx_tex_ex(T__collectables_cassette, 160, (604 - k + 32) / 6, t.fw * t.scale / 2.f, (float)t.fh * t.scale, 1, 1, 0,
               0xFFFF, a, 0);
  CR.td = (TextDraw){CR.text, 960, 604 + k, 0.5f, 0, 1, 0xFFFF, a};
  text_draw(&CR.td);
  if (CR.wait_key && tex_get(T__textboxbutton, &t))
    gfx_tex_ex(T__textboxbutton, 1824 / 6.f, (984 + (fmodf(CR.timer, 1) < 0.25f ? 6 : 0)) / 6.f, t.fw * t.scale / 2.f,
               t.fh * t.scale / 2.f, 1, 1, 0, 0xFFFF, 255, 0);
  gfx_hud(false);
}
static const EntClass MSG = {.name = "unlockedBSide", .update = msg_update, .render = msg_render};

static bool msg_ease_in(void) {   /* EaseIn: true while it runs */
  if (CR.phase == 0) {
    if ((CR.alpha += DT / 0.5f) < 1) return true;
    CR.alpha = 1;
    CR.wait = 1.5f;   /* yield return 1.5f */
    CR.phase = 1;
    return true;
  }
  if (CR.wait > 0) {
    CR.wait -= DT;
    return true;
  }
  CR.wait_key = true;
  CR.phase = 0;
  return false;
}
static bool msg_ease_out(void) {
  CR.wait_key = false;
  if ((CR.alpha -= DT / 0.5f) > 0) return true;
  CR.alpha = 0;
  if (CR.msg) ent_remove(CR.msg);
  CR.msg = NULL;
  return false;
}

WEAK void level_sandwich_lava_leave(void) {}

static void cas_routine(Ent *e) {   /* CollectRoutine */
  Cas *c = ST(e, Cas);
  Co *co = &CR.co;
  CO_BEGIN(co);
  c->collecting = 1;
  g_level.pause_lock = true;
  g_level.frozen = true;
  e->tags = TAG_FROZEN_UPDATE;
  g_session.cassette = 1;
  {
    V2 sp = level_closest_spawn(v2(c->n1x, c->n1y));
    g_session.rx = (int32_t)sp.x, g_session.ry = (int32_t)sp.y;
  }
  g_session.dashes_at_level_start = g_session.dashes;
  save_register_cassette();
  cassette_blocks_finish();
  e->depth = -1000000;
  level_shake(0.3f);
  level_flash(0xFFFF, false);
  {
    const Room *rm = g_level.room;
    CR.cam_was = g_level.cam;
    V2 v = v2(e->x - 160, e->y - 90);
    CR.cam_to = v2(clampf(v.x, rm->x - 64, rm->x + rm->w + 64 - 320), clampf(v.y, rm->y - 32, rm->y + rm->h + 32 - 180));
    g_level.cam = CR.cam_to;
    V2 f = v2sub(v2(e->x, e->y), g_level.cam);
    zoom_snap(v2(clampf(f.x, 60, 260), clampf(f.y, 60, 120)), 2);
  }
  spr_play(&c->spr, A_cassette_spin, true);
  c->spr.rate = 2;
  for (CR.p = 0; CR.p < 1.5f; CR.p += DT) {
    c->spr.rate += DT * 4;
    CO_YIELD(co);
  }
  c->spr.rate = 0;
  c->spr.frame = 0;
  wiggler_restart(&c->wig);
  CO_WAIT(co, 0.25f);
  CR.from = v2(e->x, e->y);
  CR.to = v2(e->x, g_level.cam.y - 16);
  for (CR.p = 0; CR.p < 1; CR.p += DT / 0.4f) {
    c->spr.sx = lerpf(1, 0.1f, CR.p);
    c->spr.sy = lerpf(1, 3, CR.p);
    float k = ease_cube_in(CR.p);
    e->x = lerpf(CR.from.x, CR.to.x, k), e->y = lerpf(CR.from.y, CR.to.y, k);
    CO_YIELD(co);
  }
  e->visible = 0;
  CR.alpha = 0;
  CR.timer = 0;
  CR.text[0] = 0;
  dialog_clean("UI_REMIX_UNLOCKED", CR.text, sizeof CR.text);
  text_auto_newline(CR.text, sizeof CR.text, 900);
  CR.msg = ent_new(&MSG, 0, 0);
  if (CR.msg) CR.msg->tags = TAG_HUD | TAG_PAUSE_UPDATE, CR.msg->depth = -10000;
  CO_NEST(co, msg_ease_in());
  while (!btn_pressed(&g_in.confirm)) CO_YIELD(co);
  CO_NEST(co, msg_ease_out());
  step_reset(&CR.zoom);
  for (CR.p = 0; CR.p < 1; CR.p += DT / 0.25f) {
    zoom_back(&CR.zoom, 0.2f);   /* (its own coroutine, alongside) */
    g_level.cam = v2add(CR.cam_to, v2mul(v2sub(CR.cam_was, CR.cam_to), ease_sine_inout(CR.p)));
    CO_YIELD(co);
  }
  if (!g_player.dead && c->nodes >= 2) player_start_cassette_fly(&g_player, v2(c->n1x, c->n1y), v2(c->n0x, c->n0y));
  level_sandwich_lava_leave();
  g_level.frozen = false;
  CO_WAIT(co, 0.25f);
  {
    Ent *m = first_of(&CBM);   /* Finish */
    if (m) ent_remove(m);
  }
  g_level.pause_lock = false;
  zoom_reset();
  ent_remove(e);
  CO_END(co);
}

static void cas_update(Ent *e) {
  Cas *c = ST(e, Cas);
  spr_update(&c->spr);
  wiggler_update(&c->wig);
  if (!c->collecting || c->wig.active) c->spr.sx = c->spr.sy = 1 + c->wig.value * 0.25f;
  sine_update(&c->hover);
  if (c->collected && CR.cassette == e) cas_routine(e);
  if (e->dead) return;
  if (!c->collecting && level_on_interval(0.1f))
    particles_emit(PL_MID, &P_Cassette_P_Shine, 1, v2(e->x, e->y), v2(12, 10), 0);
}
static void cas_on_player(Ent *e, Player *p) {
  Cas *c = ST(e, Cas);
  if (c->collected) return;
  player_refill_stamina(p);
  c->collected = 1;
  level_freeze(0.1f);
  memset(&CR.co, 0, sizeof CR.co);
  CR.cassette = e;
}
static void cas_render(Ent *e) {
  Cas *c = ST(e, Cas);
  float y = c->hover.value * 2;
  spr_draw(&c->spr, e->x, e->y + y);
  bloom_add(e->x, e->y + y, 0.25f, 16);
  light_add(e->x, e->y + y, 0xFFFFFF, 0.4f, 32, 64);
}
static const EntClass CASSETTE = {.name = "cassette", .size = sizeof(Cas), .update = cas_update, .render = cas_render,
                                  .on_player = cas_on_player, .kind = KIND_PCOLLIDE};

static bool cas_new(const EData *d) {
  if (g_session.cassette) return true;
  Ent *e = ent_new(&CASSETTE, d->x, d->y);
  if (!e) return true;
  Cas *c = ST(e, Cas);
  ent_box(e, 16, 16, -8, -8);
  c->nodes = (uint8_t)d->nnodes;
  if (d->nnodes >= 2) {
    V2 a = ed_node(d, 0), b = ed_node(d, 1);
    c->n0x = (int16_t)a.x, c->n0y = (int16_t)a.y, c->n1x = (int16_t)b.x, c->n1y = (int16_t)b.y;
  }
  /* Added */
  c->ghost = (g_save.cassettes >> g_session.area & 1) != 0;
  spr_init(&c->spr, c->ghost ? SB_cassetteGhost : SB_cassette);
  spr_play(&c->spr, c->ghost ? A_cassetteGhost_idle : A_cassette_idle, true);
  if (c->ghost) c->spr.alpha = 204;
  wiggler_init(&c->wig, 0.25f, 4);
  c->hover.freq = 0.5f;
  sine_set(&c->hover, 0);
  return true;
}

bool cassette_create(const EData *d) {
  switch (d->type) {
    case ET_cassette: return cas_new(d);
    case ET_cassetteBlock: return block_new(d);
  }
  return false;
}
