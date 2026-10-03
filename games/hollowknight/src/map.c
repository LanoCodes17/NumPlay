/* The map: the Knight's Map Control FSM (the map key held on the ground: the map out, walking with it; tapped twice:
 * the inventory's), the HUD's Quick Map (its backdrop, the area's name, or no map) and Game_Map, the area's part shown:
 * its rooms as Cornifer drew them, rough until the quill maps them, their pins, the arrows to the areas next to them,
 * the compass (Wayward Compass: where the Knight is), the shade's mark. The rooms visited, mapped, where a cocoon was
 * broken: PlayerData's (scenesVisited, scenesMapped, scenesEncounteredCocoon). */
#include <math.h>
#include "game.h"

#define DT 0.02f
#define BUTTON_DOWN_TIME 0.1f   /* (Button Down Time) */
#define DOUBLE_TIME 0.25f       /* (Check Double's wait; Close Doub Check's open time) */
#define WALK_SPEED 4.0f
#define HUD_PX (FOCAL / (-1.342f - CAM_Z))

typedef struct { uint8_t first, n; int16_t have; float qx, qy; int16_t title; uint32_t zones; } MapArea;
typedef struct { uint8_t area, room; int16_t sprite, full; float x, y, w, h; uint32_t rgb; uint8_t rough; } MapRoom;
typedef struct { uint8_t room, kind; int16_t sprite; float x, y; int16_t f1, f2; } MapPin;
typedef struct { uint8_t room; int16_t sprite; float x, y, ang; int16_t text; float tx, top; uint8_t align; int16_t visited; } MapNext;
typedef struct { int8_t area, room; int16_t text; float x, top; uint32_t rgb; uint8_t align, world_only; } MapName;
static const MapArea areas[] = MAP_AREAS;
static const MapRoom rooms[] = MAP_ROOMS;
static const MapPin pins[] = MAP_PINS;
static const MapNext nexts[] = MAP_NEXT;
static const MapName names[] = MAP_NAMES;
#define NAREAS (int)(sizeof areas / sizeof areas[0])
#define NMROOMS (int)(sizeof rooms / sizeof rooms[0])

enum {
  MC_INACTIVE, MC_BUTTON_DOWN, MC_CHECK_DOUBLE, MC_OPEN, MC_IDLE, MC_TURN, MC_WALK, MC_CLOSE, MC_CLOSE_DOUBLE_FAIL
};
static struct {
  uint8_t st;
  float t, open_time;
  bool in_control, walk_right, walking;
  /* the quick map: shown (and its area), the backdrop's, the name's, the no map message's fades */
  bool shown;
  int8_t area;         /* (-1: no map of it) */
  float bg_a, name_a, nomap_a, t_open;
  Anim compass;
} mc;

static bool held(void) { return g_hero.keys & K_MAP; }
static bool pressed(void) { return (g_hero.keys & K_MAP) && !(g_hero.prev_keys & K_MAP); }

/* ---------------------------------------------------------------- PlayerData's lists */
static bool bit(const uint8_t *b, int i) { return i >= 0 && i < 40 && (b[i >> 3] >> (i & 7) & 1); }
static void set_bit(uint8_t *b, int i) {
  if (i >= 0 && i < 40) b[i >> 3] |= (uint8_t)(1 << (i & 7));
}

void map_room_entered(void) { set_bit(g_pd.rooms_visited, g_room.id); }   /* (SceneManager: scenesVisited) */
void map_cocoon_broken(void) { set_bit(g_pd.rooms_cocoon, g_room.id); }   /* (AddToCocoonList) */

static bool have_area(int a) { return areas[a].have < 0 || pd_flag(areas[a].have); }

/* UpdateGameMap: with the quill, the rooms visited whose area's map the Knight has, mapped -> any new */
bool map_update(void) {
  if (!pd_flag(PDF_HAS_QUILL)) return false;
  bool any = false;
  for (int i = 0; i < NMROOMS; i++) {
    int r = rooms[i].room;
    if (r == 255 || !bit(g_pd.rooms_visited, r) || bit(g_pd.rooms_mapped, r) || !have_area(rooms[i].area)) continue;
    set_bit(g_pd.rooms_mapped, r);
    any = true;
  }
  return any;
}

/* ---------------------------------------------------------------- Map Control */
static struct {
  bool on;
  float t, a, quill_a;
  Anim quill;
} mm;   /* (the map's update message: below) */
static void quick_open(void);
static void quick_close(void);

void map_reset(void) {
  memset(&mc, 0, sizeof mc);
  mc.compass.sprite = -1;
  memset(&mm, 0, sizeof mm);
  mm.quill.sprite = -1;
}

static bool can_quick_map(void) { return hero_can_quick_map(); }

static void regain(void) {
  /* Regain Control (not at a bench) */
  g_pd.disable_pause = false;
  if (!pd_flag(PDF_AT_BENCH)) {
    hero_regain_control();
    hero_start_anim_control();
  }
  mc.in_control = false;
  mc.st = MC_INACTIVE, mc.open_time = 0;
}

static void open_map(void) {
  /* Open Map: the Knight's control taken, Map Open */
  mc.open_time = 0;
  g_pd.disable_pause = true;
  quick_open();
  mc.in_control = true;
  hero_relinquish_control();
  hero_stop_anim_control();
  anim_play_from_frame(&g_hero.anim, CLIP_KNIGHT_MAP_OPEN, 0);
  mc.st = MC_OPEN;
}

static void close_map(void) {
  /* Close Map: Map Away */
  quick_close();
  g_hero.body.vx = 0;
  mc.walking = false;
  anim_play_from_frame(&g_hero.anim, CLIP_KNIGHT_MAP_AWAY, 0);
  mc.st = MC_CLOSE;
}

static void idle(void) {
  g_hero.body.vx = 0;
  mc.walking = false;
  anim_play(&g_hero.anim, CLIP_KNIGHT_MAP_IDLE);
  mc.st = MC_IDLE;
}

static void open_inventory_map(void) {
  /* (Double!: OPEN INVENTORY MAP) */
  inv_open_map();
}

/* (HERO DAMAGED, LEAVING SCENE, FSM CANCEL: Cancel All) */
void map_cancel(void) {
  if (mc.st == MC_INACTIVE) return;
  quick_close();
  bool in_control = mc.in_control;
  mc.st = MC_INACTIVE, mc.open_time = 0, mc.walking = false, mc.in_control = false;
  if (in_control) {
    g_pd.disable_pause = false;
    hero_start_anim_control();
  }
}

void map_tick(void) {
  bool down = pressed(), on = held();
  bool ended = g_hero.anim.events & ANIM_DONE;
  /* (Open time: counted idle and with the map out; from naught as it is opened) */
  if (mc.st == MC_INACTIVE || (mc.st >= MC_OPEN && mc.st <= MC_WALK)) mc.open_time = fminf(3, mc.open_time + DT);
  /* (leaving the ground: Quick Cancel) */
  if (mc.st >= MC_OPEN && mc.st <= MC_WALK && !g_hero.cs.on_ground) {
    quick_close();
    regain();
    return;
  }
  switch (mc.st) {
    case MC_INACTIVE:
      if (!down) break;
      /* Button Down Check */
      if (g_pd.disable_pause || !pd_flag(PDF_HAS_MAP) || pd_flag(PDF_AT_BENCH) || !can_quick_map()) break;
      mc.st = MC_BUTTON_DOWN, mc.t = 0;
      break;
    case MC_BUTTON_DOWN:
      if (down) {
        open_inventory_map();
        mc.st = MC_INACTIVE, mc.open_time = 0;
        break;
      }
      if ((mc.t += DT) < BUTTON_DOWN_TIME) break;
      /* Button Still Down?, Can QuickMap?, Has Map? */
      if (on && !g_pd.disable_pause && can_quick_map() && pd_flag(PDF_HAS_MAP)) open_map();
      else if (on) mc.st = MC_INACTIVE, mc.open_time = 0;
      else mc.st = MC_CHECK_DOUBLE, mc.t = 0;
      break;
    case MC_CHECK_DOUBLE:
      if (down && !g_pd.disable_pause && can_quick_map()) {
        open_inventory_map();
        mc.st = MC_INACTIVE, mc.open_time = 0;
      } else if ((mc.t += DT) >= DOUBLE_TIME)
        mc.st = MC_INACTIVE, mc.open_time = 0;
      break;
    case MC_OPEN:
      if (!on) close_map();
      else if (ended || !g_hero.anim.playing) idle();
      break;
    case MC_IDLE:
      if (!on) {
        close_map();
        break;
      }
      if (g_hero.keys & (K_LEFT | K_RIGHT)) {
        /* Turn R?, Turn L?: turned already, walking; else Map Turn first */
        mc.walk_right = (g_hero.keys & K_RIGHT) != 0;
        g_hero.body.vx = 0;
        if (g_hero.cs.facing_right == mc.walk_right) {
          mc.st = MC_WALK;
          anim_play(&g_hero.anim, CLIP_KNIGHT_MAP_WALK);
        } else {
          anim_play_from_frame(&g_hero.anim, CLIP_KNIGHT_MAP_TURN, 0);
          mc.st = MC_TURN;
        }
      }
      break;
    case MC_TURN:
      if (!on) close_map();
      else if (ended || !g_hero.anim.playing) {
        mc.st = MC_WALK;
        anim_play(&g_hero.anim, CLIP_KNIGHT_MAP_WALK);
      }
      break;
    case MC_WALK:
      if (!on) {
        close_map();
        break;
      }
      if (!(g_hero.keys & (mc.walk_right ? K_RIGHT : K_LEFT))) {
        idle();
        break;
      }
      /* (MAPWALK START; FaceRight, FaceLeft; its speed each frame) */
      mc.walking = true;
      hero_face(mc.walk_right);
      g_hero.body.vx = mc.walk_right ? WALK_SPEED : -WALK_SPEED;
      break;
    case MC_CLOSE:
      if (down) {
        /* Close Doub Check: soon after it was opened, the inventory's map; else the Map Away goes on */
        if (mc.open_time <= DOUBLE_TIME) {
          anim_play(&g_hero.anim, CLIP_KNIGHT_IDLE);
          open_inventory_map();
          mc.in_control = false;
          g_pd.disable_pause = false;
          hero_regain_control();
          hero_start_anim_control();
          mc.st = MC_INACTIVE, mc.open_time = 0;
          break;
        }
        mc.st = MC_CLOSE_DOUBLE_FAIL;
      }
      if (ended || !g_hero.anim.playing) regain();
      break;
    case MC_CLOSE_DOUBLE_FAIL:
      if (ended || !g_hero.anim.playing) regain();
      break;
  }
}

/* ---------------------------------------------------------------- the quick map */
static void quick_open(void) {
  /* Open: the backdrop up; 0.2 s, then the area's map and name (no map: its message) */
  mc.shown = true, mc.t_open = 0, mc.area = -2;
  anim_play(&mc.compass, CLIP_MAPUI_IDLE);
}

static void quick_close(void) {
  /* Close: the map gone at once; the name, the backdrop, the message fade */
  mc.shown = false;
}

static int area_of_zone(void) {
  for (int a = 0; a < NAREAS; a++)
    if (areas[a].zones >> g_pd.map_zone & 1) return a;
  return -1;
}

static void map_msg_tick(void);

void map_hud_tick(void) {
  map_msg_tick();
  /* (FadeGroups: the backdrop in 0.4 s, out 0.25; the name in 0.4, out 0.3; the message in 0.3, out 0.25) */
  if (mc.shown) {
    mc.t_open += DT;
    mc.bg_a = fminf(1, mc.bg_a + DT / 0.4f);
    if (mc.t_open >= 0.2f && mc.area == -2) {
      int a = area_of_zone();
      mc.area = (int8_t)(a >= 0 && have_area(a) ? a : -1);
    }
    if (mc.area >= 0) mc.name_a = fminf(1, mc.name_a + DT / 0.4f);
    else if (mc.area == -1) mc.nomap_a = fminf(1, mc.nomap_a + DT / 0.3f);
    mc.compass.events = 0;
    anim_play(&mc.compass, mc.walking ? CLIP_MAPUI_WALK : CLIP_MAPUI_IDLE);
    anim_update(&mc.compass, DT);
  } else {
    mc.bg_a = fmaxf(0, mc.bg_a - DT / 0.25f);
    mc.name_a = fmaxf(0, mc.name_a - DT / 0.3f);
    mc.nomap_a = fmaxf(0, mc.nomap_a - DT / 0.25f);
  }
}

/* the map's colors: a tint each (the menus', the item message's: never up with it), afresh each frame */
static uint32_t tint_used[4];
static int tint_n;
static uint8_t map_tint(uint32_t rgb, uint8_t a) {
  static const uint8_t slots[] = {29, 30, 31, 25};
  uint32_t *used = tint_used;
  int n = tint_n;
  uint32_t key = rgb << 8 | a;
  for (int i = 0; i < n; i++)
    if (used[i] == key) return gfx_dyn_tint(slots[i], (uint8_t)(rgb >> 16), (uint8_t)(rgb >> 8), (uint8_t)rgb, a);
  int i = n < 4 ? tint_n++ : 3;
  used[i] = key;
  return gfx_dyn_tint(slots[i], (uint8_t)(rgb >> 16), (uint8_t)(rgb >> 8), (uint8_t)rgb, a);
}

static void hud_sprite(int sprite, float x, float y, float k, float deg, uint8_t tint) {
  if (sprite < 0) return;
  Inst in;
  if (deg != 0) sprite_inst_rot(sprite, x, y, 0, k, k, deg, tint, &in);
  else sprite_inst(sprite, x, y, 0, k, k, tint, &in);
  gfx_hud(&in, 0);
}

/* a text: its lines centred on x, from its left or to its right (align 0 left, 1 centred, 2 right), its top at top */
static void hud_text(int text, int style, float x, float top, int align, uint32_t rgb, float a) {
  uint8_t al = (uint8_t)(a * 255 + 0.5f);
  if (text < 0 || !al) return;
  const uint8_t *s = text_get(text), *st = font_style(style);
  float py = VIEW_H / 2 - top * HUD_PX + font_asc(st);
  for (int i = 0;;) {
    int e = i;
    while (s[e] && s[e] != TEXT_BR) e++;
    float w = text_width(style, s + i, e - i);
    gfx_text(style, VIEW_W / 2 + x * HUD_PX - (align == 1 ? w / 2 : align == 2 ? w : 0), py, s + i, e - i,
             (uint32_t)al << 24 | rgb, 0);
    if (!s[e]) break;
    i = e + 1, py += font_line(st);
  }
}

/* a room of the map shown: rough with its area's map; else once mapped */
static bool room_shown(int i) {
  const MapRoom *r = &rooms[i];
  bool mapped = r->room != 255 && bit(g_pd.rooms_mapped, r->room);
  return r->rough || (mapped && pd_flag(PDF_HAS_QUILL));
}

static bool pin_shown(const MapPin *p) {
  if (p->kind == 1) return pd_flag(PDF_HAS_PIN_COCOON) && rooms[p->room].room != 255 && bit(g_pd.rooms_cocoon, rooms[p->room].room);
  return pd_flag(p->f1) && (p->f2 < 0 || pd_flag(p->f2));
}

/* the area's map at (ox, oy), k HUD units a map unit (its sprites made at the quick map's), the next areas' arrows or
 * not */
static void draw_area(int a, float ox, float oy, float k, bool quick) {
  float sk = k / MAP_QUICK_K;
  uint8_t white = gfx_dyn_tint(16, 255, 255, 255, 255);
  for (int i = areas[a].first; i < areas[a].first + areas[a].n; i++) {
    const MapRoom *r = &rooms[i];
    if (!room_shown(i)) continue;
    bool mapped = r->room != 255 && bit(g_pd.rooms_mapped, r->room);
    hud_sprite(mapped && r->full >= 0 ? r->full : r->sprite, ox + r->x * k, oy + r->y * k, sk, 0, map_tint(r->rgb, 255));
  }
  /* (the pins: the HUD camera's layer 30, as Map Key Pref has it) */
  for (unsigned i = 0; i < sizeof pins / sizeof pins[0]; i++) {
    const MapPin *p = &pins[i];
    if (rooms[p->room].area != a || !room_shown(p->room) || !pin_shown(p)) continue;
    hud_sprite(p->sprite, ox + p->x * k, oy + p->y * k, sk, 0, white);
  }
  for (unsigned i = 0; i < sizeof names / sizeof names[0]; i++) {
    const MapName *n = &names[i];
    if (n->area != a || (n->room >= 0 && !room_shown(n->room)) || (quick && n->world_only)) continue;
    hud_text(n->text, STYLE_MSG, ox + n->x * k, oy + n->top * k, n->align, n->rgb, 1);
  }
  if (!quick) return;
  for (unsigned i = 0; i < sizeof nexts / sizeof nexts[0]; i++) {
    const MapNext *n = &nexts[i];
    if (rooms[n->room].area != a || !room_shown(n->room) || (n->visited >= 0 && !pd_flag(n->visited))) continue;
    hud_sprite(n->sprite, ox + n->x * k, oy + n->y * k, sk, n->ang, white);
    hud_text(n->text, STYLE_MSG, ox + n->tx * k, oy + n->top * k, n->align, 0xFFFFFF, 1);
  }
}

/* the compass (the Knight's room's place on the map, as far across it as he is across the room) */
static bool compass_at(float ox, float oy, float k, float *x, float *y) {
  for (int i = 0; i < NMROOMS; i++) {
    const MapRoom *r = &rooms[i];
    if (r->room != g_room.id || r->w <= 0 || !g_room.h || g_room.h->w <= 0 || g_room.h->h <= 0) continue;
    *x = ox + (r->x - r->w / 2 + g_hero.body.x / g_room.h->w * r->w) * k;
    *y = oy + (r->y - r->h / 2 + g_hero.body.y / g_room.h->h * r->h) * k;
    return true;
  }
  return false;
}

/* the shade's mark: the middle of the room it is in (PlayerData's shadeMapPos) */
static bool shade_at(float ox, float oy, float k, float *x, float *y) {
  if (!g_pd.soul_limited) return false;
  for (int i = 0; i < NMROOMS; i++) {
    const MapRoom *r = &rooms[i];
    if (r->room == 255 || strcmp(room_name(r->room), g_pd.shade_scene)) continue;
    *x = ox + r->x * k, *y = oy + r->y * k;
    return true;
  }
  return false;
}

static void draw_marks(float ox, float oy, float k) {
  static const struct { int16_t sprite; float dx, dy; uint8_t a; } shade[2] = MAP_SHADE;
  static const int16_t markers[4] = MAP_MARKERS;
  float sk = k / MAP_QUICK_K, x, y;
  if (shade_at(ox, oy, k, &x, &y))
    for (int i = 0; i < 2; i++)
      hud_sprite(shade[i].sprite, x + shade[i].dx * k, y + shade[i].dy * k, sk, 0, map_tint(0xFFFFFF, shade[i].a));
  uint8_t white = gfx_dyn_tint(16, 255, 255, 255, 255);
  for (int c = 0; c < 4; c++)
    for (int m = 0; m < g_pd.markers_placed[c] && m < 6; m++)
      hud_sprite(markers[c], ox + g_pd.markers[c][m][0] * 0.01f * k, oy + g_pd.markers[c][m][1] * 0.01f * k, sk, 0, white);
  if (charm_on(2) && mc.compass.sprite >= 0 && compass_at(ox, oy, k, &x, &y))
    hud_sprite(mc.compass.sprite, x, y, MAP_COMPASS_K * k, 0, white);
}

static void map_msg_draw(void);

void map_draw(void) {
  tint_n = 0;
  map_msg_draw();
  if (mc.bg_a <= 0 && mc.name_a <= 0 && mc.nomap_a <= 0) return;
  float bga = MAP_BG_A * mc.bg_a;
  if (bga > 0) gfx_hud_fill(-15, -9, 15, 9, gfx_dyn_tint(27, 0, 0, 0, (uint8_t)(bga * 255 + 0.5f)));
  if (mc.shown && mc.area >= 0) {
    const MapArea *ar = &areas[mc.area];
    draw_area(mc.area, ar->qx, ar->qy, MAP_QUICK_K, true);
    draw_marks(ar->qx, ar->qy, MAP_QUICK_K);
  }
  /* the area's name on its backboard; no map: its symbol and line */
  static const struct { float x, top; } title = MAP_TITLE;
  static const struct { int16_t sprite; float x, y, kx, ky; uint8_t a; } back = MAP_TITLE_BACK;
  if (mc.name_a > 0 && mc.area >= 0) {
    Inst in;
    sprite_inst(back.sprite, back.x, back.y, 0, back.kx, back.ky, map_tint(0xFFFFFF, (uint8_t)(back.a * mc.name_a)), &in);
    gfx_hud(&in, 0);
    hud_text(areas[mc.area].title, STYLE_DIALOGUE, title.x, title.top, 1, 0xFFFFFF, mc.name_a);
  }
  static const struct { int16_t sprite; float x, y; } nomap = MAP_NO_MAP;
  static const struct { int16_t text; float x, top; } nomsg = MAP_NO_MAP_MSG;
  if (mc.nomap_a > 0) {
    hud_sprite(nomap.sprite, nomap.x, nomap.y, 1, 0, map_tint(0xFFFFFF, (uint8_t)(255 * mc.nomap_a)));
    hud_text(nomsg.text, STYLE_DIALOGUE, nomsg.x, nomsg.top, 1, 0xFFFFFF, mc.nomap_a);
  }
}

bool map_shown(void) { return mc.st != MC_INACTIVE && mc.st != MC_BUTTON_DOWN && mc.st != MC_CHECK_DOUBLE; }

/* the quick map from a bench (Bench Control's Map Idle: OPEN QUICK MAP; Close Map: CLOSE QUICK MAP) */
void map_quick(bool open) {
  if (open) quick_open();
  else quick_close();
}

/* ---------------------------------------------------------------- the map's update message */
/* (Map Msg: up (0.3 s: its backboard, its line, its quill writing), 3 s, Map Complete, then down: the backboard and
 * the line in 0.2 s, the quill in 1; gone after 1 s) */
void map_msg_show(void) {
  memset(&mm, 0, sizeof mm);
  mm.on = true;
  anim_play_from_frame(&mm.quill, CLIP_JOURNALMSG_MAP_WRITING, 0);
}

static void map_msg_tick(void) {
  if (!mm.on) return;
  mm.t += DT;
  mm.quill.events = 0;
  anim_update(&mm.quill, DT);
  if (mm.t < 3.3f) {
    mm.a = mm.quill_a = fminf(1, mm.t / 0.3f);
    return;
  }
  if (mm.quill.clip != CLIP_JOURNALMSG_MAP_COMPLETE) anim_play_from_frame(&mm.quill, CLIP_JOURNALMSG_MAP_COMPLETE, 0);
  float d = mm.t - 3.3f;
  mm.a = fmaxf(0, 1 - d / 0.2f), mm.quill_a = fmaxf(0, 1 - d);
  if (d >= 1) mm.on = false;
}

static void map_msg_draw(void) {
  static const struct { float x, y, qx, qy, tx, ty; int16_t text, sprite; float bx, by, kx, ky; uint8_t a; } m = MAP_MSG;
  if (!mm.on) return;
  const float k = MAP_MSG_K;
  Inst in;
  if (mm.a > 0) {
    sprite_inst(m.sprite, m.x + m.bx, m.y + m.by, 0, m.kx, m.ky, map_tint(0xFFFFFF, (uint8_t)(m.a * mm.a)), &in);
    gfx_hud(&in, 0);
    /* (its line: left aligned, its middle on its place) */
    const uint8_t *st = font_style(STYLE_MSG);
    const uint8_t *s = text_get(m.text);
    int n = (int)strlen((const char *)s);
    float py = VIEW_H / 2 - (m.y + m.ty) * HUD_PX + (font_asc(st) - font_desc(st)) / 2;
    gfx_text(STYLE_MSG, VIEW_W / 2 + (m.x + m.tx) * HUD_PX, py, s, n, (uint32_t)(mm.a * 255 + 0.5f) << 24 | 0xFFFFFF, 0);
  }
  if (mm.quill_a > 0 && mm.quill.sprite >= 0) {
    sprite_inst(mm.quill.sprite, m.x + m.qx, m.y + m.qy, 0, k, k, map_tint(0xFFFFFE, (uint8_t)(255 * mm.quill_a)), &in);
    gfx_hud(&in, 0);
  }
  (void)k;
}
