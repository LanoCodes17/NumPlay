/* The game's own scripts (PlayMaker FSMs) of its characters and set pieces, run as they are (tools/vm.py compiles
 * them): states entered on events, their actions run on entering and each step, FINISHED once all are done. Events an
 * FSM sends itself switch its state once the action that sent them is over; those from others at once. The objects
 * they use (VM objects) are drawn here; the Knight, the dialogue box, the area title are the game's own. */
#pragma GCC optimize("Os")   /* (its code small: not where a frame's time goes) */
#include <math.h>
#include "game.h"
#ifdef HOST
#include <stdio.h>
#include <stdlib.h>
#endif

#define DT 0.02f
#define MAX_VM_OBJS 32   /* (tools/vm.py: MAX_OBJS, MAX_FSMS, MAX_VARS) */
#define MAX_VM_FSMS 24
#define MAX_VM_VARS 256
#define MAX_MOVERS 4
#define NONE 0xFFFF
#define OWNER 0xFF0F
enum { O_HERO = 0xFF00, O_HERO_LIGHT, O_DIALOGUE_MANAGER, O_DIALOGUE_TEXT, O_AREA_TITLE, O_CAMERA_PARENT, O_MAIN_CAMERA,
       O_GAME_MANAGER, O_HUD_BLANKER, O_HORNET = 0xFF0C, O_WHITE_BLANKER, O_CHARM_TUTE };
#define O_GATE 0xFD00   /* (+ k: a battle gate, obj.c's) */
#define O_ENT 0xF000    /* (+ i: a room record (a camera lock area), the game's) */
enum { OF_ACTIVE = 1, OF_RENDERER = 2, OF_ANIMATOR = 4, OF_TRIGGER = 8, OF_COLLIDER = 16,
       OF_GONE = 32, OF_INSIDE = 64, OF_WAS_INSIDE = 128,
       OF_COND_OFF = 256,                                       /* (not there: its condition) */
       OF_SPELL_HIT = 512, OF_SPELL_IN = 1024, OF_SPELL_WAS = 2048 };   /* (a spell in its trigger) */
#define OF_ANIM_OFF 32   /* (its record's: off once its clip is over, DeactivateAfter2dtkAnimation) */
#define OF_WAVE 64       /* (its record's: WaveEffectControl, grows and fades as it is on: bx its speed, by its scale) */
#define OF_FADE 128      /* (its record's: SimpleSpriteFade, fades as it is on, then off: bx its time, by the alpha to) */
#define OF_STARTED 4     /* (its own: its animator started, the first time it was on) */
enum { M_ENTER, M_UPDATE, M_FIXED };

typedef struct {
  float x, y, z, sx, sy, bx, by, bhx, bhy;
  uint16_t parent, name, sprite, clip, map;
  uint8_t nmap, flags;
  uint16_t cond, layer;
  int16_t order;
  uint16_t blend;   /* (its material's: BL_*) */
  uint8_t col0, ncol;   /* (the colliders it has: on as it is) */
  uint8_t r, g, b, a;   /* (its renderer's color) */
  uint16_t cond2;        /* (a second condition, as cond) */
} ObjRec;

typedef struct {
  float x, y, sx, sy;   /* its place and scale now */
  Anim anim;
  uint16_t flags;
  uint8_t alpha;   /* (its material's color's alpha: the scripts') */
} Obj;
typedef struct {
  int16_t obj;          /* (-1: none) */
  uint8_t ease, tween, loop;
  uint8_t nc, ccol[2];  /* (flung: its body's contacts) */
  float vx, vy, g;      /* (one the scripts set moving: velocity, gravity scale) */
  union {
    /* (iTweenMoveBy: by, how much of it so far (eased), time so far, its time, ease; 1 going, 2 over; its loop: none,
     * again, back and forth. It moves by steps, with what else moves it: Translate. (tween 3: flung, below)) */
    struct {
      float tdx, tdy, te, tt, ttime;
    };
    /* (flung (FlingObject): a body, as Box2D moves it; its box, bounce and friction as the action has them) */
    struct {
      const uint8_t *box;
      float cnx[2], cny[2];
    };
  };
} Mover;

typedef struct {
  const uint8_t *def;
  uint16_t owner, name, var0;
  uint8_t state, next, nvars, epoch;
  bool on, finished;
  uint64_t done;        /* its state's actions that are over */
  float t;              /* time in its state */
  bool started, pending;   /* (started once; to start, the next frame: Start) */
} Fsm;

static struct {
  const ObjRec *rec;
  const uint8_t *map;   /* (clip maps: string, clip) */
  int nobjs, nfsms;
  Obj objs[MAX_VM_OBJS];
  Fsm fsms[MAX_VM_FSMS];
  uint32_t vars[MAX_VM_VARS];
  Mover movers[MAX_MOVERS];
  const uint8_t *persist;   /* (what the save keeps: FSM, slot, bit) */
  int npersist;
  Fsm *executing;       /* (the FSM whose action runs: its events to itself wait) */
  int depth;
  /* the area title's variables as scripts set them (title.c starts it from them) */
  bool title_npc, title_visited, title_right, title_start;
  float blank_alpha;
  bool blank_on;
  int title;
  uint32_t prev_keys;
} vm;

/* ---------------------------------------------------------------- the data (VMDEF: definitions, string tables) */
static const uint8_t *vmdef(void) { return section(SEC_VMDEF); }
static uint32_t nstrings(void) { return rd32(vmdef() + 4); }
static const uint8_t *str_tables(void) {
  /* (the section's end: per string, its title, prompt, Knight's clip, PlayerData bool) */
  return vmdef() + section_size(SEC_VMDEF) - (nstrings() * 7 + (nstrings() & 1));
}
static int str_title(uint16_t s) { return s < nstrings() ? (int)str_tables()[s] : 255; }
static int str_prompt(uint16_t s) {
  if (s >= nstrings()) return -1;
  const uint8_t *t = str_tables() + nstrings() + (nstrings() & 1);
  uint16_t v = rd16(t + 2 * s);
  return v == 0xFFFF ? -1 : v;
}
static int str_knight_clip(uint16_t s) {
  if (s >= nstrings()) return -1;
  const uint8_t *t = str_tables() + nstrings() + (nstrings() & 1) + 2 * nstrings();
  uint16_t v = rd16(t + 2 * s);
  return v == 0xFFFF ? -1 : v;
}
static uint16_t str_pd(uint16_t s) {
  if (s >= nstrings()) return 0xFFFF;
  const uint8_t *t = str_tables() + nstrings() + (nstrings() & 1) + 4 * nstrings();
  return rd16(t + 2 * s);
}
static const uint8_t *def_at(int i) {
  const uint8_t *d = vmdef();
  return d + rd32(d + 8 + 4 * i);
}

/* a definition: nstates, nvars, start, nglobal, nconsts (u16), consts, global transitions, state offsets, states */
static int d_nconsts(const uint8_t *d) { return rd16(d + 4); }
static const uint8_t *d_global(const uint8_t *d) { return d + 6 + 4 * d_nconsts(d); }
static const uint8_t *d_state(const uint8_t *d, int s) {
  int at = 6 + 4 * d_nconsts(d) + 2 * d[3];
  at += at & 1;
  return d + rd16(d + at + 2 * s);
}

/* ---------------------------------------------------------------- values */
static uint32_t val(const Fsm *f, uint16_t s) {
  if (s == NONE) return 0;
  if (s < f->nvars) return vm.vars[f->var0 + s];
  return rd32(f->def + 6 + 4 * (s - f->nvars));
}
static bool has(uint16_t s) { return s != NONE; }
static float fval(const Fsm *f, uint16_t s) {
  uint32_t u = val(f, s);
  float x;
  memcpy(&x, &u, 4);
  return x;
}
static int ival(const Fsm *f, uint16_t s) { return (int32_t)val(f, s); }
static int oval(const Fsm *f, uint16_t s) {
  uint32_t o = val(f, s);
  if (s == NONE) return NONE;
  return o == OWNER ? f->owner : (int)(o & 0xFFFF);
}
static void set_var(Fsm *f, uint8_t v, uint32_t x) {
  if (v != 255 && v < f->nvars) vm.vars[f->var0 + v] = x;
}
static void set_f(Fsm *f, uint8_t v, float x) {
  uint32_t u;
  memcpy(&u, &x, 4);
  set_var(f, v, u);
}

/* operands as they come */
typedef struct {
  const uint8_t *p;
} Rd;
static uint16_t rv(Rd *r) {
  uint16_t s = rd16(r->p);
  r->p += 2;
  return s;
}
static uint8_t rb(Rd *r) { return *r->p++; }
static float rdf(const uint8_t *p) {
  float x;
  memcpy(&x, p, 4);
  return x;
}

/* ---------------------------------------------------------------- PlayerData */
static bool pd_bool(uint16_t s) {
  uint16_t c = str_pd(s);
  if (c == 0xFFF0) return g_pd.disable_pause;
  if (c == 0xFFF1) return g_pd.has_spell;
  if (c == 0xFFF2) return g_pd.can_dash;
  if (c == 0xFFFD) return true;
  if (c >= PDF_COUNT) return false;
  return pd_flag(c);
}
static void pd_set_bool(uint16_t s, bool on) {
  uint16_t c = str_pd(s);
  if (c == 0xFFF0) g_pd.disable_pause = on;
  else if (c == 0xFFF1) g_pd.has_spell = on;
  else if (c == 0xFFF2) g_pd.can_dash = on;
  else if (c == PDF_HAS_DASH) pd_set_flag(c, on), g_pd.has_dash = on;   /* (kept twice) */
  else if (c < PDF_COUNT) pd_set_flag(c, on);
}
/* (tools/vm.py: PD_INTS) */
static int pd_int(int i) {
  switch (i) {
    case 0: return g_pd.mp;
    case 1: return g_pd.health;
    case 2: return g_pd.max_health;
    case 3: return g_pd.geo;
    case 4: return g_pd.fireball_level;
    case 7: return g_pd.shaman;
    case 8: return g_pd.elderbug;
    case 10: return g_pd.nail_damage;
    case 11: return g_pd.hornet_greenpath;
    case 12: return g_pd.quirrel_egg_temple;
    case 13: return g_pd.charms_owned;
    case 14: case 15: case 16: case 17: return g_pd.trinkets[i - 14];
    case 18: return g_pd.rancid_eggs;
    case 19: return g_pd.ore;
    default: return 0;
  }
}
static void pd_set_int(int i, int v) {
  switch (i) {
    case 0: g_pd.mp = (int16_t)v; break;
    case 1: g_pd.health = (int8_t)v; break;
    case 3: g_pd.geo = v; break;
    case 4: g_pd.fireball_level = (uint8_t)v; break;
    case 7: g_pd.shaman = (uint8_t)v; break;
    case 8: g_pd.elderbug = (uint8_t)v; break;
    case 11: g_pd.hornet_greenpath = (uint8_t)v; break;
    case 12: g_pd.quirrel_egg_temple = (uint8_t)v; break;
    case 13: g_pd.charms_owned = (uint8_t)v; break;
    case 14: case 15: case 16: case 17: g_pd.trinkets[i - 14] = (uint8_t)v; break;
    case 18: g_pd.rancid_eggs = (uint8_t)v; break;
    case 19: g_pd.ore = (uint8_t)v; break;
  }
}

/* ---------------------------------------------------------------- objects */
static bool obj_ok(int o) { return o >= 0 && o < vm.nobjs; }

static bool obj_active(int o) {
  for (int k = 0; obj_ok(o) && k < 16; k++) {
    if (!(vm.objs[o].flags & OF_ACTIVE) || (vm.objs[o].flags & OF_GONE)) return false;
    o = vm.rec[o].parent == NONE ? -1 : vm.rec[o].parent;
  }
  return true;
}

static void obj_pos(int o, float *x, float *y) {
  if (o == O_MAIN_CAMERA) {
    *x = g_cam_x, *y = g_cam_y;
  } else if (o == O_HERO) {
    *x = g_hero.body.x, *y = g_hero.body.y;
  } else if (obj_ok(o)) {
    *x = vm.objs[o].x, *y = vm.objs[o].y;
  } else
    *x = *y = 0;
}

static void obj_move(int o, float x, float y) {
  if (o == O_HERO) {
    g_hero.body.x = x, g_hero.body.y = y;
    return;
  }
  if (!obj_ok(o)) return;
  /* (its children with it) */
  float dx = x - vm.objs[o].x, dy = y - vm.objs[o].y;
  vm.objs[o].x = x, vm.objs[o].y = y;
  for (int c = 0; c < vm.nobjs; c++)
    if (vm.rec[c].parent == o) obj_move(c, vm.objs[c].x + dx, vm.objs[c].y + dy);
}

static float obj_scale_x(int o) {
  if (o == O_HERO) return g_hero.cs.facing_right ? -1.0f : 1.0f;
  return obj_ok(o) ? vm.objs[o].sx : 1;
}

static void obj_set_scale_x(int o, float sx) {
  if (o == O_HERO) {
    g_hero.cs.facing_right = sx < 0;
    return;
  }
  if (obj_ok(o)) vm.objs[o].sx = sx;
}

static int find_child(int o, uint16_t name) {
  for (int c = 0; c < vm.nobjs; c++)
    if (vm.rec[c].parent == o && vm.rec[c].name == name) return c;
  return NONE;
}

static int obj_clip(int o, uint16_t name) {
  if (o == O_HERO) return str_knight_clip(name);
  if (!obj_ok(o)) return -1;
  const uint8_t *m = vm.map + 4 * vm.rec[o].map;
  for (int i = 0; i < vm.rec[o].nmap; i++)
    if (rd16(m + 4 * i) == name) return rd16(m + 4 * i + 2);
  return -1;
}

static Anim *obj_anim(int o) {
  if (o == O_HERO) return &g_hero.anim;
  return obj_ok(o) ? &vm.objs[o].anim : NULL;
}

static Mover *mover(int o, bool make) {
  for (int i = 0; i < MAX_MOVERS; i++)
    if (vm.movers[i].obj == o) return &vm.movers[i];
  if (!make) return NULL;
  for (int i = 0; i < MAX_MOVERS; i++)
    if (vm.movers[i].obj < 0) {
      memset(&vm.movers[i], 0, sizeof vm.movers[i]);
      vm.movers[i].obj = (int16_t)o;
      return &vm.movers[i];
    }
  return NULL;
}

/* (an animator plays its first clip the first time its object is on) */
static void anims_start(void) {
  for (int i = 0; i < vm.nobjs; i++)
    if (!(vm.objs[i].flags & OF_STARTED) && vm.rec[i].clip != NONE && obj_active(i)) {
      vm.objs[i].flags |= OF_STARTED;
      anim_play_from_frame(&vm.objs[i].anim, vm.rec[i].clip, 0);
    }
}

/* (the colliders objects have: on as they are) */
static void obj_colliders(void) {
  for (int i = 0; i < vm.nobjs; i++)
    if (vm.rec[i].ncol && !(vm.objs[i].flags & OF_COND_OFF))
      for (int c = vm.rec[i].col0; c < vm.rec[i].col0 + vm.rec[i].ncol; c++) phys_collider_enable(c, obj_active(i));
}

static void fsm_start(Fsm *f);
static void obj_set_active(int o, bool on) {
  if (o == O_AREA_TITLE) {
    if (on) vm.title_start = true;
    return;
  }
  if (!obj_ok(o)) return;
  bool was = obj_active(o);
  if (on) vm.objs[o].flags |= OF_ACTIVE;
  else vm.objs[o].flags &= (uint16_t)~OF_ACTIVE;
  if (!was && on) anims_start();
  /* (its wave, its fade, from the start; a wave's speed (anim.fps) its first) */
  if (!was && on && (vm.rec[o].flags & (OF_WAVE | OF_FADE))) vm.objs[o].anim.time = 0, vm.objs[o].anim.fps = vm.rec[o].bx;
  /* (an FSM starts again as its object comes on: RestartOnEnable; the first time, on its Start, the next frame) */
  if (!was && on)
    for (int i = 0; i < vm.nfsms; i++) {
      Fsm *f = &vm.fsms[i];
      if (!f->on && obj_active(f->owner)) {
        if (f->started) fsm_start(f);
        else f->pending = true;
      }
    }
  if (was && !on)
    for (int i = 0; i < vm.nfsms; i++)
      if (vm.fsms[i].on && !obj_active(vm.fsms[i].owner)) vm.fsms[i].on = false;
  if (was != on) obj_colliders();
}

static bool hero_box_in(int o) {
  const ObjRec *r = &vm.rec[o];
  const Body *b = &g_hero.body;
  float cx = vm.objs[o].x + r->bx, cy = vm.objs[o].y + r->by;
  float x0 = b->x + b->ox - b->hx, x1 = b->x + b->ox + b->hx, y0 = b->y + b->oy - b->hy, y1 = b->y + b->oy + b->hy;
  return x1 > cx - r->bhx && x0 < cx + r->bhx && y1 > cy - r->bhy && y0 < cy + r->bhy;
}

/* ---------------------------------------------------------------- events */
static void enter_state(Fsm *f, int s);

/* the state an event takes it to (global transitions first), or -1 */
static int transition(const Fsm *f, int ev) {
  const uint8_t *g = d_global(f->def);
  for (int i = 0; i < f->def[3]; i++)
    if (g[2 * i] == ev) return g[2 * i + 1];
  const uint8_t *st = d_state(f->def, f->state);
  for (int i = 0; i < st[0]; i++)
    if (st[2 + 2 * i] == ev) return st[3 + 2 * i];
  return -1;
}

static void run_switches(Fsm *f) {
  while (f->next != 255 && vm.depth < 48) {
    int s = f->next;
    f->next = 255;
    vm.depth++;
    enter_state(f, s);
    vm.depth--;
  }
}

static void fsm_event(Fsm *f, int ev) {
  if (ev == 255) return;
  if (!f->on) {
    /* (one not started yet starts as it hears one) */
    if (!f->pending || !obj_active(f->owner)) return;
    fsm_start(f);
  }
  int s = transition(f, ev);
  if (s < 0) return;
  f->next = (uint8_t)s;
  if (vm.executing != f) run_switches(f);   /* (from another: at once) */
}

static void game_event(int ev);
void vm_broadcast(int ev) {
  for (int i = 0; i < vm.nfsms; i++) fsm_event(&vm.fsms[i], ev);
  game_event(ev);
}

static void obj_event(int o, int ev, uint16_t fsm_name) {
  if (o >= O_GATE && o < O_GATE + 0x100) {
    if (ev >= VMEV_BG_CLOSE && ev <= VMEV_BG_DESTROY) gate_event_at(o - O_GATE, ev - VMEV_BG_CLOSE);
    return;
  }
  if (o == O_HORNET) {
    if (ev == VMEV_WAKE) enemies_hornet_wake();
    return;
  }
  if (o == O_WHITE_BLANKER) {
    if (ev == VMEV_FADE_IN || ev == VMEV_FADE_OUT) white_blanker_fade(ev == VMEV_FADE_IN);
    return;
  }
  if (o == O_CHARM_TUTE) {
    if (ev == VMEV_CLOSE) charm_tute_close();
    return;
  }
  if (o == O_HERO) {
    if (ev == VMEV_FSM_CANCEL) spell_cancel();   /* (its FSMs' FSM CANCEL: a spell, a focus, stopped) */
    return;
  }
  if (o == O_DIALOGUE_MANAGER) {
    if (ev == VMEV_BOX_UP) dialogue_box_up();
    else if (ev == VMEV_BOX_DOWN) dialogue_box_down();
    else if (ev == VMEV_BOX_UP_DREAM) dialogue_dream_box(true);
    else if (ev == VMEV_BOX_DOWN_DREAM) dialogue_dream_box(false);
    return;
  }
  for (int i = 0; i < vm.nfsms; i++)
    if (vm.fsms[i].owner == o && (fsm_name == NONE || vm.fsms[i].name == fsm_name)) fsm_event(&vm.fsms[i], ev);
}

/* events the game's own objects hear */
static void game_event(int ev) {
  if (ev >= VMEV_BG_CLOSE && ev <= VMEV_BG_DESTROY) gates_event(ev - VMEV_BG_CLOSE);
  else if (ev == VMEV_NPC_TITLE_DOWN) title_npc_down();
  else if (ev == VMEV_NPC_CONVO_START) title_npc_convo_start();
}

static void send(Fsm *f, Rd *r, int ev) {
  /* an event target: kind (0 itself, 1 an object's FSMs, 2 all, 3 an object's FSM by name), object, FSM name */
  int kind = rb(r);
  uint16_t go = rv(r), fname = rv(r);
  switch (kind) {
    case 0: fsm_event(f, ev); break;
    case 1: obj_event(oval(f, go), ev, NONE); break;
    case 2: vm_broadcast(ev); break;
    case 3: obj_event(oval(f, go), ev, fname); break;
  }
}

/* ---------------------------------------------------------------- the actions */
static bool hero_can_talk(void) {
  const Hero *h = &g_hero;
  return h->accepting_input && h->state != HS_NO_INPUT && !h->control_relinquished && h->cs.on_ground &&
         !h->cs.attacking && !h->cs.dashing;
}

static uint32_t pressed_keys(void) { return g_hero.keys & ~vm.prev_keys; }

/* (one action: on entering its state, each step after, or each fixed step) -> whether it is over */
static bool act(Fsm *f, const uint8_t *a, int mode) {
  Rd r = {a + 2};
  int op = a[0];
  bool enter = mode == M_ENTER;
  switch (op) {
    case VMOP_WAIT: {
      float t = fval(f, rv(&r));
      int ev = rb(&r);
      if (f->t >= t && (mode == M_UPDATE || t <= 0)) {
        fsm_event(f, ev);
        return true;
      }
      return false;
    }
    case VMOP_NEXTFRAMEEVENT:
      if (enter) return false;
      if (mode != M_UPDATE) return false;
      fsm_event(f, rb(&r));
      return true;
    case VMOP_SENDEVENT:
    case VMOP_SENDEVENTBYNAME: {
      Rd t = r;
      r.p += 5;
      int ev = rb(&r);
      send(f, &t, ev);
      return true;
    }
    case VMOP_SETBOOLVALUE: {
      uint8_t v = rb(&r);
      set_var(f, v, val(f, rv(&r)) != 0);
      return !rb(&r);
    }
    case VMOP_SETFLOATVALUE:
    case VMOP_SETINTVALUE:
    case VMOP_SETSTRINGVALUE:
    case VMOP_SETGAMEOBJECT: {
      uint8_t v = rb(&r);
      uint16_t s = rv(&r);
      uint32_t x = val(f, s);
      if (op == VMOP_SETGAMEOBJECT && x == OWNER) x = f->owner;
      set_var(f, v, x);
      return !rb(&r);
    }
    case VMOP_GETOWNER: set_var(f, rb(&r), f->owner); return true;
    case VMOP_GETHERO: set_var(f, rb(&r), O_HERO); return true;
    case VMOP_FINDCHILD: {
      int o = oval(f, rv(&r));
      uint16_t name = (uint16_t)val(f, rv(&r));
      set_var(f, rb(&r), find_child(o, name));
      return true;
    }
    case VMOP_GETPARENT: {
      int o = oval(f, rv(&r));
      set_var(f, rb(&r), obj_ok(o) ? vm.rec[o].parent : NONE);
      return true;
    }
    case VMOP_FLOATADD:
    case VMOP_FLOATSUBTRACT:
    case VMOP_FLOATMULTIPLY: {
      uint8_t v = rb(&r);
      float x = fval(f, v), y = fval(f, rv(&r));
      bool every = rb(&r);
      set_f(f, v, op == VMOP_FLOATADD ? x + y : op == VMOP_FLOATSUBTRACT ? x - y : x * y);
      return !every;
    }
    case VMOP_FLOATCOMPARE: {
      float a1 = fval(f, rv(&r)), a2 = fval(f, rv(&r)), tol = fval(f, rv(&r));
      int eq = rb(&r), lt = rb(&r), gt = rb(&r);
      bool every = rb(&r);
      if (fabsf(a1 - a2) <= tol) fsm_event(f, eq);
      else if (a1 < a2) fsm_event(f, lt);
      else fsm_event(f, gt);
      return !every;
    }
    case VMOP_FLOATTESTTOBOOL: {
      float a1 = fval(f, rv(&r)), a2 = fval(f, rv(&r)), tol = fval(f, rv(&r));
      uint8_t eq = rb(&r), lt = rb(&r), gt = rb(&r);
      bool every = rb(&r);
      bool e = fabsf(a1 - a2) <= tol;
      set_var(f, eq, e), set_var(f, lt, !e && a1 < a2), set_var(f, gt, !e && a1 > a2);
      return !every;
    }
    case VMOP_FLOATINRANGE: {
      float x = fval(f, rv(&r)), lo = fval(f, rv(&r)), hi = fval(f, rv(&r));
      uint8_t v = rb(&r);
      int te = rb(&r), fe = rb(&r);
      bool every = rb(&r);
      bool in = x >= lo && x <= hi;
      set_var(f, v, in);
      fsm_event(f, in ? te : fe);
      return !every;
    }
    case VMOP_INTCOMPARE: {
      int a1 = ival(f, rv(&r)), a2 = ival(f, rv(&r));
      int eq = rb(&r), lt = rb(&r), gt = rb(&r);
      bool every = rb(&r);
      fsm_event(f, a1 == a2 ? eq : a1 < a2 ? lt : gt);
      return !every;
    }
    case VMOP_INTTESTTOBOOL:
    case VMOP_INTCOMPARETOBOOL: {
      int a1 = ival(f, rv(&r)), a2 = ival(f, rv(&r));
      uint8_t eq = rb(&r), lt = rb(&r), gt = rb(&r);
      bool every = rb(&r);
      set_var(f, eq, a1 == a2), set_var(f, lt, a1 < a2), set_var(f, gt, a1 > a2);
      return !every;
    }
    case VMOP_INTADD: {
      uint8_t v = rb(&r);
      set_var(f, v, (uint32_t)(ival(f, v) + ival(f, rv(&r))));
      return !rb(&r);
    }
    case VMOP_INTSWITCH: {
      int x = ival(f, rv(&r));
      int n = rb(&r);
      const uint8_t *cmp = r.p;
      r.p += 2 * n;
      int ne = rb(&r);
      for (int i = 0; i < n && i < ne; i++)
        if (ival(f, rd16(cmp + 2 * i)) == x) {
          fsm_event(f, r.p[i]);
          break;
        }
      r.p += ne;
      return !rb(&r);
    }
    case VMOP_BOOLTEST: {
      bool b = val(f, rv(&r)) != 0;
      int te = rb(&r), fe = rb(&r);
      bool every = rb(&r);
      fsm_event(f, b ? te : fe);
      return !every;
    }
    case VMOP_BOOLALLTRUE:
    case VMOP_BOOLANYTRUE: {
      int n = rb(&r);
      bool all = true, any = false;
      for (int i = 0; i < n; i++) {
        bool b = val(f, rv(&r)) != 0;
        all &= b, any |= b;
      }
      int ev = rb(&r);
      uint8_t v = rb(&r);
      bool every = rb(&r);
      bool res = op == VMOP_BOOLALLTRUE ? (n > 0 && all) : any;
      set_var(f, v, res);
      if (res) fsm_event(f, ev);
      return !every;
    }
    case VMOP_BOOLTESTMULTI: {
      int n = rb(&r);
      const uint8_t *vars = r.p;
      r.p += 2 * n;
      int m = rb(&r);
      bool all = n == m;
      for (int i = 0; i < n && i < m; i++) all &= (val(f, rd16(vars + 2 * i)) != 0) == (val(f, rd16(r.p + 2 * i)) != 0);
      r.p += 2 * m;
      int te = rb(&r), fe = rb(&r);
      uint8_t v = rb(&r);
      bool every = rb(&r);
      set_var(f, v, all);
      fsm_event(f, all ? te : fe);
      return !every;
    }
    case VMOP_STRINGCOMPARE: {
      uint32_t a1 = val(f, rv(&r)), a2 = val(f, rv(&r));
      int eq = rb(&r), ne = rb(&r);
      uint8_t v = rb(&r);
      bool every = rb(&r);
      set_var(f, v, a1 == a2);
      fsm_event(f, a1 == a2 ? eq : ne);
      return !every;
    }
    case VMOP_GETPLAYERDATABOOL: {
      bool b = pd_bool((uint16_t)val(f, rv(&r)));
      set_var(f, rb(&r), b);
      return true;
    }
    case VMOP_SETPLAYERDATABOOL: {
      uint16_t s = (uint16_t)val(f, rv(&r));
      pd_set_bool(s, val(f, rv(&r)) != 0);
      return true;
    }
    case VMOP_PLAYERDATABOOLTEST: {
      bool b = pd_bool((uint16_t)val(f, rv(&r)));
      int te = rb(&r), fe = rb(&r);
      fsm_event(f, b ? te : fe);
      return true;
    }
    case VMOP_PLAYERDATABOOLTRUEANDFALSE: {
      bool t = pd_bool((uint16_t)val(f, rv(&r))), fl = pd_bool((uint16_t)val(f, rv(&r)));
      int te = rb(&r), fe = rb(&r);
      fsm_event(f, t && !fl ? te : fe);
      return true;
    }
    case VMOP_GETPLAYERDATAINT: {
      int i = rb(&r);
      set_var(f, rb(&r), (uint32_t)pd_int(i));
      return true;
    }
    case VMOP_SETPLAYERDATAINT: {
      int i = rb(&r);
      pd_set_int(i, ival(f, rv(&r)));
      return true;
    }
    case VMOP_INCREMENTPLAYERDATAINT: {
      int i = rb(&r);
      pd_set_int(i, pd_int(i) + 1);
      return true;
    }
    case VMOP_FLINGOBJECT: {
      int o = oval(f, rv(&r));
      float smin = fval(f, rv(&r)), smax = fval(f, rv(&r)), amin = fval(f, rv(&r)), amax = fval(f, rv(&r));
      Mover *m = obj_ok(o) ? mover(o, true) : NULL;
      if (m) {
        float s = rand_range(smin, smax), a = rand_range(amin, amax) * (float)M_PI / 180;
        m->vx = s * cosf(a), m->vy = s * sinf(a);
        m->box = r.p, m->nc = 0, m->tween = 3;
      }
      return true;
    }
    case VMOP_GETSPEED2D: {
      int o = oval(f, rv(&r));
      uint8_t st = rb(&r);
      bool every = rb(&r);
      Mover *m = mover(o, false);
      set_f(f, st, o == O_HERO ? sqrtf(g_hero.body.vx * g_hero.body.vx + g_hero.body.vy * g_hero.body.vy)
                   : m ? sqrtf(m->vx * m->vx + m->vy * m->vy) : 0);
      return !every;
    }
    case VMOP_SENDRANDOMEVENT: {
      int n = rb(&r);
      if (n) fsm_event(f, r.p[(int)rand_range(0, (float)n - 0.001f)]);
      return true;
    }
    case VMOP_ACTIVATEGAMEOBJECT: {
      int o = oval(f, rv(&r));
      bool on = val(f, rv(&r)) != 0;
      obj_set_active(o, on);
      return true;
    }
    case VMOP_SETSPRITERENDERER:
    case VMOP_SETMESHRENDERER: {
      int o = oval(f, rv(&r));
      bool on = val(f, rv(&r)) != 0;
      if (o == O_HERO) g_hero.hidden = !on;
      else if (obj_ok(o)) vm.objs[o].flags = (uint16_t)(on ? vm.objs[o].flags | OF_RENDERER : vm.objs[o].flags & ~OF_RENDERER);
      return true;
    }
    case VMOP_SETCOLLIDER: {
      int o = oval(f, rv(&r));
      bool on = val(f, rv(&r)) != 0;
      if (obj_ok(o)) vm.objs[o].flags = (uint16_t)(on ? vm.objs[o].flags | OF_COLLIDER : vm.objs[o].flags & ~OF_COLLIDER);
      return true;
    }
    case VMOP_DESTROYOBJECT: {
      int o = oval(f, rv(&r));
      if (o >= O_ENT && o < O_ENT + MAX_ENTS) ent_set_enabled(o - O_ENT, false);   /* (a camera lock area: gone) */
      if (obj_ok(o)) {
        vm.objs[o].flags |= OF_GONE;
        obj_set_active(o, false);
      }
      return true;
    }
    case VMOP_DESTROYSELF:
      vm.objs[f->owner].flags |= OF_GONE;
      obj_set_active(f->owner, false);
      return true;
    case VMOP_DESTROYALLCHILDREN: {
      int o = oval(f, rv(&r));
      for (int c = 0; c < vm.nobjs; c++)
        if (obj_ok(o) && vm.rec[c].parent == o) vm.objs[c].flags |= OF_GONE;
      return true;
    }
    case VMOP_GETPOSITION: {
      int o = oval(f, rv(&r));
      uint8_t v = rb(&r), vx = rb(&r), vy = rb(&r);
      rb(&r);
      rb(&r);
      bool every = rb(&r);
      float x, y;
      obj_pos(o, &x, &y);
      if (v != 255) set_f(f, v, x), set_f(f, (uint8_t)(v + 1), y);
      set_f(f, vx, x), set_f(f, vy, y);
      return !every;
    }
    case VMOP_SETPOSITION: {
      int o = oval(f, rv(&r));
      uint16_t vec = rv(&r), sx = rv(&r), sy = rv(&r);
      rv(&r);
      rb(&r);
      bool every = rb(&r);
      float x, y;
      obj_pos(o, &x, &y);
      if (has(vec)) x = fval(f, vec), y = fval(f, (uint16_t)(vec + 1));
      if (has(sx)) x = fval(f, sx);
      if (has(sy)) y = fval(f, sy);
      obj_move(o, x, y);
      return !every;
    }
    case VMOP_GETSCALE: {
      int o = oval(f, rv(&r));
      set_f(f, rb(&r), obj_scale_x(o));
      set_f(f, rb(&r), obj_ok(o) ? vm.objs[o].sy : 1);
      return true;
    }
    case VMOP_SETSCALE: {
      int o = oval(f, rv(&r));
      uint16_t sx = rv(&r), sy = rv(&r);
      bool every = rb(&r);
      if (has(sx)) obj_set_scale_x(o, fval(f, sx));
      if (has(sy) && obj_ok(o)) vm.objs[o].sy = fval(f, sy);
      return !every;
    }
    case VMOP_SETVELOCITY2D: {
      int o = oval(f, rv(&r));
      uint16_t vx = rv(&r), vy = rv(&r);
      bool every = rb(&r);
      if (o == O_HERO) {
        if (has(vx)) g_hero.body.vx = fval(f, vx);
        if (has(vy)) g_hero.body.vy = fval(f, vy);
      } else if (obj_ok(o)) {
        Mover *m = mover(o, true);
        if (m && has(vx)) m->vx = fval(f, vx);
        if (m && has(vy)) m->vy = fval(f, vy);
      }
      return !every;
    }
    case VMOP_SETGRAVITY2DSCALE: {
      int o = oval(f, rv(&r));
      float g = fval(f, rv(&r));
      if (o == O_HERO) g_hero.body.gravity_scale = g;
      else if (obj_ok(o)) {
        Mover *m = mover(o, true);
        if (m) m->g = g;
      }
      return true;
    }
    case VMOP_CHECKTARGETDIRECTION: {
      int o = oval(f, rv(&r)), t = oval(f, rv(&r));
      int above = rb(&r), below = rb(&r), right = rb(&r), left = rb(&r);
      bool every = rb(&r);
      float x0, y0, x1, y1;
      obj_pos(o, &x0, &y0);
      obj_pos(t, &x1, &y1);
      if (x1 > x0) fsm_event(f, right);
      else fsm_event(f, left);
      if (y1 > y0) fsm_event(f, above);
      else fsm_event(f, below);
      return !every;
    }
    case VMOP_FACEOBJECT: {
      int a1 = oval(f, rv(&r)), b1 = oval(f, rv(&r));
      bool faces_right = rb(&r), play = rb(&r);
      uint16_t clip = (uint16_t)val(f, rv(&r));
      bool reset = rb(&r), every = rb(&r);
      (void)reset;
      float xa, ya, xb, yb;
      obj_pos(a1, &xa, &ya);
      obj_pos(b1, &xb, &yb);
      float sx = obj_scale_x(a1);
      /* (turned to face it: its x scale flipped, its turn clip played) */
      bool want_right = xb > xa;
      bool now_right = faces_right ? sx > 0 : sx < 0;
      if (want_right != now_right) {
        obj_set_scale_x(a1, -sx);
        int c = obj_clip(a1, clip);
        Anim *an = obj_anim(a1);
        if (play && c >= 0 && an) anim_play_from_frame(an, c, 0);
      }
      return !every;
    }
    case VMOP_TK2DPLAYANIMATION:
    case VMOP_TK2DPLAYANIMATIONWITHEVENTS: {
      int o = oval(f, rv(&r));
      uint16_t name = (uint16_t)val(f, rv(&r));
      Anim *an = obj_anim(o);
      int c = obj_clip(o, name);
      if (op == VMOP_TK2DPLAYANIMATION) {
        if (enter && an && c >= 0) anim_play(an, c);
        return true;
      }
      rb(&r);
      int done_ev = rb(&r);
      if (enter) {
        if (an && c >= 0) anim_play_from_frame(an, c, 0);
        return false;
      }
      if (mode == M_UPDATE && (!an || !an->playing || (an->events & ANIM_DONE))) {
        fsm_event(f, done_ev);
        return true;
      }
      return false;
    }
    case VMOP_TK2DWATCHANIMATIONEVENTS: {
      int o = oval(f, rv(&r));
      rb(&r);
      int done_ev = rb(&r);
      Anim *an = obj_anim(o);
      if (mode == M_UPDATE && (!an || !an->playing || (an->events & ANIM_DONE))) {
        fsm_event(f, done_ev);
        return true;
      }
      return false;
    }
    case VMOP_TK2DPLAYFRAME: {
      int o = oval(f, rv(&r));
      int fr = ival(f, rv(&r));
      Anim *an = obj_anim(o);
      if (an && an->clip >= 0) anim_play_from_frame(an, an->clip, fr);
      return true;
    }
    case VMOP_TK2DSPRITEGETID: {
      int o = oval(f, rv(&r));
      uint8_t v = rb(&r);
      bool every = rb(&r);
      Anim *an = obj_anim(o);
      set_var(f, v, an ? (uint32_t)an->frame : 0);
      return !every;
    }
    case VMOP_TRIGGER2DEVENT: {
      /* (the Knight, or a spell, against its owner's trigger: 0 entering, 1 in it, 2 leaving) */
      int kind = rb(&r), ev = rb(&r), tag = rb(&r);
      if (mode != M_FIXED || !obj_ok(f->owner) || !(vm.objs[f->owner].flags & OF_COLLIDER)) return false;
      uint16_t fl = vm.objs[f->owner].flags;
      bool in = fl & (tag ? OF_SPELL_IN : OF_INSIDE), was = fl & (tag ? OF_SPELL_WAS : OF_WAS_INSIDE);
      if ((kind == 0 && in && !was) || (kind == 1 && in) || (kind == 2 && !in && was)) fsm_event(f, ev);
      return false;
    }
    case VMOP_LISTENFORUP:
    case VMOP_LISTENFORDOWN:
    case VMOP_LISTENFORLEFT:
    case VMOP_LISTENFORRIGHT:
    case VMOP_LISTENFORATTACK:
    case VMOP_LISTENFORJUMP:
    case VMOP_LISTENFORCAST:
    case VMOP_LISTENFORINVENTORY: {
      int ev = rb(&r);
      if (mode != M_UPDATE) return false;
      uint32_t k = op == VMOP_LISTENFORUP ? K_UP : op == VMOP_LISTENFORDOWN ? K_DOWN : op == VMOP_LISTENFORLEFT ? K_LEFT
                 : op == VMOP_LISTENFORRIGHT ? K_RIGHT : op == VMOP_LISTENFORATTACK ? K_ATTACK : op == VMOP_LISTENFORJUMP ? K_JUMP
                 : op == VMOP_LISTENFORINVENTORY ? K_INV : K_SPELL;
      if (pressed_keys() & k) fsm_event(f, ev);
      return false;
    }
    case VMOP_CHECKTRACKTRIGGERCOUNT: {
      int count = ival(f, rv(&r)), test = rb(&r), ev = rb(&r);
      bool every = rb(&r);
      if (mode == M_UPDATE) return false;
      int n = obj_ok(f->owner) && (vm.objs[f->owner].flags & OF_INSIDE) ? 1 : 0;
      bool ok = test == 0 ? n == count : test == 1 ? n < count : test == 2 ? n > count : test == 3 ? n <= count : n >= count;
      if (ok) fsm_event(f, ev);
      return enter && (!every || ok);
    }
    case VMOP_SHOWPROMPTMARKER: {
      uint16_t label = (uint16_t)val(f, rv(&r));
      int at = oval(f, rv(&r));
      uint8_t v = rb(&r);
      float x, y;
      obj_pos(at, &x, &y);
      int prev = v != 255 ? (int)val(f, v) - 0xFE00 : -1;
      int h = prompt_show(prev, str_prompt(label), x, y);
      set_var(f, v, (uint32_t)(h + 0xFE00));
      return true;
    }
    case VMOP_HIDEPROMPTMARKER: {
      uint32_t h = val(f, rv(&r));
      if (h >= 0xFE00 && h < 0xFF00) prompt_hide((int)h - 0xFE00);
      return true;
    }
    case VMOP_SETFSMBOOL:
    case VMOP_SETFSMSTRING: {
      int o = oval(f, rv(&r));
      uint16_t fname = (uint16_t)val(f, rv(&r));
      uint16_t var = (uint16_t)val(f, rv(&r));
      uint32_t x = val(f, rv(&r));
      uint8_t slot = rb(&r);
      if (o == O_AREA_TITLE) {
        /* (Area Title's variables: NPC Title, Visited, Display Right, Area Event) */
        int t = str_title(var);
        (void)t;
        if (op == VMOP_SETFSMSTRING) vm.title = str_title((uint16_t)x);
        else if (var == VMSTR_NPC_TITLE) vm.title_npc = x;
        else if (var == VMSTR_VISITED) vm.title_visited = x;
        else if (var == VMSTR_DISPLAY_RIGHT) vm.title_right = x;
      } else if (obj_ok(o) && slot != 255)
        /* (another FSM's variable: its FSMs of that name) */
        for (int i = 0; i < vm.nfsms; i++)
          if (vm.fsms[i].owner == o && vm.fsms[i].name == fname) set_var(&vm.fsms[i], slot, x);
      return true;
    }
    case VMOP_STARTCONVERSATION:
      dialogue_start(rv(&r));
      return true;
    case VMOP_NOTICEICON:
      notice_icon(rb(&r));
      return true;
    case VMOP_NOTICETEXT:
      notice_show((int)val(f, rv(&r)));
      return true;
    case VMOP_CHARMNOTICE:
      charm_notice((int)val(f, rv(&r)));
      return true;
    case VMOP_CHARMTUTE:
      charm_tute();
      return true;
    case VMOP_STARTCONVERSATIONOF: {
      /* (the conversation its key and sheet name now) */
      uint16_t key = (uint16_t)val(f, rv(&r)), sheet = (uint16_t)val(f, rv(&r));
      int n = rb(&r);
      for (int k = 0; k < n; k++) {
        uint16_t kk = rv(&r), ss = rv(&r), t = rv(&r);
        if (kk == key && ss == sheet) {
          dialogue_start(t);
          break;
        }
      }
      return true;
    }
    case VMOP_HEROCALL: {
      int m = rb(&r);
      uint8_t store = rb(&r);
      float a0 = fval(f, rv(&r));
      Hero *h = &g_hero;
      switch (m) {
        case 0: hero_relinquish_control(); break;
        case 1: hero_regain_control(); break;
        case 2: hero_stop_anim_control(); break;
        case 3: hero_start_anim_control(); break;
        case 4: hero_face(false); break;
        case 5: hero_face(true); break;
        case 6: set_var(f, store, hero_can_talk()); break;
        case 7: h->prevent_cast = 0.3f; break;
        case 8: h->cs.on_ground = true; break;
        case 9: hero_add_mp_charge((int)a0); break;
        case 10: {
          /* FindGroundPoint: below a point, the Knight's place standing there */
          float y = hero_ground_y(h->body.x, a0);
          set_f(f, store, h->body.x), set_f(f, (uint8_t)(store + 1), y);
          break;
        }
        case 11: save_set_respawn("Death Respawn Marker", true); break;
        case 15: save_game(); break;
        case 13: hero_relinquish_control_not_velocity(); break;
        case 16: hero_gravity(a0 != 0); break;
        case 19: set_var(f, store, hero_can_talk()); break;   /* (CanInspect: as CanTalk) */
      }
      return true;
    }
    case VMOP_SHAKE:
      cam_shake(rb(&r));
      return true;
    case VMOP_SETRUMBLE: {
      int k = rb(&r);
      cam_rumble(val(f, rv(&r)) ? k : RUMBLE_OFF);
      return true;
    }
    case VMOP_EASEFLOAT: {
      float from = fval(f, rv(&r)), to = fval(f, rv(&r));
      uint8_t v = rb(&r);
      float t = fval(f, rv(&r));
      int ev = rb(&r);
      float k = t > 0 ? f->t / t : 1;
      if (k > 1) k = 1;
      set_f(f, v, from + (to - from) * k);
      if (k >= 1 && mode == M_UPDATE) {
        fsm_event(f, ev);
        return true;
      }
      return false;
    }
    case VMOP_CAMERAZOOM:
      return true;   /* (the camera's moves towards the scene and back: the view here keeps its distance) */
    case VMOP_COLLISION2DEVENT: {
      /* (a falling object reaching the ground) */
      int ev = rb(&r);
      Mover *m = mover(f->owner, false);
      if (mode == M_FIXED && m && m->vy == 0 && m->g > 0) {
        fsm_event(f, ev);
        return true;
      }
      return false;
    }
    case VMOP_GETITEMMSG:
      msg_show(rb(&r));
      return true;
    case VMOP_BLANKER: {
      float a = fval(f, rv(&r));
      bool every = rb(&r);
      vm.blank_alpha = a;
      blanker_set(a, vm.blank_on);
      return !every;
    }
    case VMOP_BLANKERON:
      vm.blank_on = val(f, rv(&r)) != 0;
      blanker_set(vm.blank_alpha, vm.blank_on);
      return true;
    case VMOP_FINDGAMEOBJECT: {
      uint16_t name = (uint16_t)val(f, rv(&r));
      int found = NONE;
      for (int i = 0; i < vm.nobjs && found == NONE; i++)
        if (vm.rec[i].name == name && obj_active(i)) found = i;
      if (found == NONE) {
        /* (a battle gate, a camera lock area: the game's) */
        int g = gate_find(name), c = g < 0 ? camlock_find(name) : -1;
        if (g >= 0) found = O_GATE + g;
        else if (c >= 0) found = O_ENT + c;
      }
      set_var(f, rb(&r), (uint32_t)found);
      return true;
    }
    case VMOP_CREATEOBJECT: {
      /* (a prefab made beforehand: on, where it is made) */
      int o = oval(f, rv(&r));
      uint16_t x = rv(&r), y = rv(&r);
      uint8_t store = rb(&r);
      uint16_t at = rv(&r);
      if (obj_ok(o)) {
        if (has(at)) {
          /* (at its spawn point, by an offset) */
          float px, py;
          obj_pos(oval(f, at), &px, &py);
          obj_move(o, px + (has(x) ? fval(f, x) : 0), py + (has(y) ? fval(f, y) : 0));
        } else if (has(x) && has(y))
          obj_move(o, fval(f, x), fval(f, y));
        vm.objs[o].flags &= (uint16_t)~OF_GONE;
        obj_set_active(o, true);
      }
      set_var(f, store, (uint32_t)o);
      return true;
    }
    case VMOP_ACTIVATEALLCHILDREN: {
      int o = oval(f, rv(&r));
      bool on = val(f, rv(&r)) != 0;
      for (int c = 0; c < vm.nobjs; c++)
        if (obj_ok(o) && vm.rec[c].parent == o) obj_set_active(c, on);
      return true;
    }
    case VMOP_ITWEENMOVEBY: {
      /* (by a vector, in a time, eased; over once it is there) */
      int o = oval(f, rv(&r));
      uint16_t vec = rv(&r);
      float t = fval(f, rv(&r));
      int ease = rb(&r), loop = rb(&r), ev = rb(&r);
      Mover *m = mover(o, enter);
      if (enter) {
        if (!m) return true;
        m->nc = 0;
        m->tdx = has(vec) ? fval(f, vec) : 0, m->tdy = has(vec) ? fval(f, (uint16_t)(vec + 1)) : 0;
        m->te = 0, m->tt = 0, m->ttime = t, m->ease = (uint8_t)ease, m->tween = 1, m->loop = (uint8_t)loop;
        return false;
      }
      if (mode == M_UPDATE && (!m || m->tween != 1)) {
        if (m) m->tween = 0;
        fsm_event(f, ev);
        return true;
      }
      return false;
    }
    case VMOP_SETFSMFLOAT: {
      int o = oval(f, rv(&r));
      uint16_t fname = (uint16_t)val(f, rv(&r));
      rv(&r);
      uint32_t x = val(f, rv(&r));
      uint8_t slot = rb(&r);
      float v;
      memcpy(&v, &x, 4);
      if (o == O_WHITE_BLANKER) white_blanker_time(v);   /* (its Fade Time) */
      else if (obj_ok(o) && slot != 255)
        for (int i = 0; i < vm.nfsms; i++)
          if (vm.fsms[i].owner == o && vm.fsms[i].name == fname) set_var(&vm.fsms[i], slot, x);
      return true;
    }
    case VMOP_TEXTALIGN:
      dialogue_centre(rb(&r) != 0);
      return true;
    case VMOP_OBJALPHA: {
      int o = oval(f, rv(&r));
      float a = fval(f, rv(&r));
      bool every = rb(&r);
      if (obj_ok(o)) vm.objs[o].alpha = (uint8_t)(a <= 0 ? 0 : a >= 1 ? 255 : a * 255 + 0.5f);
      return !every;
    }
    case VMOP_GETOBJALPHA: {
      int o = oval(f, rv(&r));
      set_f(f, rb(&r), obj_ok(o) ? vm.objs[o].alpha / 255.0f : 1);
      return true;
    }
    case VMOP_BUILDSTRING: {
      /* (the string its parts make now: one of those it can) */
      int n = rb(&r);
      uint16_t parts[4];
      for (int k = 0; k < n; k++) {
        uint16_t sl = rv(&r);
        if (k < 4) parts[k] = (uint16_t)val(f, sl);
      }
      int rows = rb(&r);
      uint32_t found = 0;
      for (int j = 0; j < rows; j++) {
        uint16_t res = rv(&r);
        bool match = true;
        for (int k = 0; k < n; k++)
          if (rv(&r) != parts[k < 4 ? k : 3]) match = false;
        if (match) found = res;
      }
      set_var(f, rb(&r), found);
      return true;
    }
    default:
      return true;
  }
}

/* iTween's eases (EaseType: quad, cubic, quart, quint each in, out, in-out; sine, expo, circ; linear) */
static float ease(int type, float k) {
  int g = type / 3, w = type % 3;
  if (type >= 21) return k;
  switch (g) {
    case 0: return w == 0 ? k * k : w == 1 ? 1 - (1 - k) * (1 - k) : k < 0.5f ? 2 * k * k : 1 - 2 * (1 - k) * (1 - k);
    case 1: return w == 0 ? k * k * k : w == 1 ? 1 - (1 - k) * (1 - k) * (1 - k)
                 : k < 0.5f ? 4 * k * k * k : 1 - 4 * (1 - k) * (1 - k) * (1 - k);
    case 2: return w == 0 ? k * k * k * k : w == 1 ? 1 - powf(1 - k, 4) : k < 0.5f ? 8 * powf(k, 4) : 1 - 8 * powf(1 - k, 4);
    case 3: return w == 0 ? powf(k, 5) : w == 1 ? 1 - powf(1 - k, 5) : k < 0.5f ? 16 * powf(k, 5) : 1 - 16 * powf(1 - k, 5);
    case 4: return w == 0 ? 1 - cosf(k * (float)M_PI / 2) : w == 1 ? sinf(k * (float)M_PI / 2) : (1 - cosf(k * (float)M_PI)) / 2;
    case 5:
      if (k <= 0 || k >= 1) return k;
      return w == 0 ? powf(2, 10 * (k - 1)) : w == 1 ? 1 - powf(2, -10 * k)
           : k < 0.5f ? powf(2, 20 * k - 10) / 2 : 1 - powf(2, -20 * k + 10) / 2;
    default:
      return w == 0 ? 1 - sqrtf(1 - k * k) : w == 1 ? sqrtf(1 - (k - 1) * (k - 1))
           : k < 0.5f ? (1 - sqrtf(1 - 4 * k * k)) / 2 : (1 + sqrtf(1 - (2 * k - 2) * (2 * k - 2))) / 2;
  }
}

/* ActivateAllChildren of an object the game names (Hornet's Hornet Saver) */
void vm_activate_children(uint16_t name, bool on) {
  for (int o = 0; o < vm.nobjs; o++)
    if (vm.rec[o].name == name && !(vm.objs[o].flags & OF_COND_OFF))
      for (int c = 0; c < vm.nobjs; c++)
        if (vm.rec[c].parent == o) obj_set_active(c, on);
}

/* (a spell's box: the triggers it is in, this step) */
void vm_spell(float x0, float y0, float x1, float y1) {
  for (int i = 0; i < vm.nobjs; i++) {
    const ObjRec *r = &vm.rec[i];
    float cx = vm.objs[i].x + r->bx, cy = vm.objs[i].y + r->by;
    if ((r->flags & OF_TRIGGER) && x1 > cx - r->bhx && x0 < cx + r->bhx && y1 > cy - r->bhy && y0 < cy + r->bhy)
      vm.objs[i].flags |= OF_SPELL_HIT;
  }
}

/* ---------------------------------------------------------------- states */
static void enter_state(Fsm *f, int s) {
  const uint8_t *st = d_state(f->def, s);
  f->state = (uint8_t)s, f->epoch++, f->t = 0, f->done = 0, f->finished = false;
  uint8_t epoch = f->epoch;
  const uint8_t *a = st + 2 + 2 * st[0];
  Fsm *prev = vm.executing;
  vm.executing = f;
  for (int i = 0; i < st[1]; i++, a += 2 + a[1]) {
    if (act(f, a, M_ENTER)) f->done |= (uint64_t)1 << i;
    if (f->epoch != epoch || f->next != 255) break;
  }
  vm.executing = prev;
  if (f->epoch == epoch && f->next == 255 && f->done == (st[1] >= 64 ? ~0ull : ((uint64_t)1 << st[1]) - 1)) {
    f->finished = true;
    f->next = 255;
    int t = transition(f, VMEV_FINISHED);
    if (t >= 0) f->next = (uint8_t)t;
  }
}

static void fsm_start(Fsm *f) {
  /* (its variables as they start, its first state) */
  const uint8_t *init = NULL;
  (void)init;
  f->on = true, f->next = 255, f->started = true, f->pending = false;
  vm.depth = 0;
  enter_state(f, f->def[2]);
  run_switches(f);
}

static void fsm_step(Fsm *f, int mode) {
  if (!f->on || f->finished) return;
  const uint8_t *st = d_state(f->def, f->state);
  if (mode == M_UPDATE) f->t += DT;
  uint8_t epoch = f->epoch;
  const uint8_t *a = st + 2 + 2 * st[0];
  Fsm *prev = vm.executing;
  vm.executing = f;
  for (int i = 0; i < st[1]; i++, a += 2 + a[1]) {
    if (f->done >> i & 1) continue;
    if (act(f, a, mode)) f->done |= (uint64_t)1 << i;
    if (f->epoch != epoch) break;
  }
  vm.executing = prev;
  if (f->epoch == epoch && f->next == 255 && f->done == (st[1] >= 64 ? ~0ull : ((uint64_t)1 << st[1]) - 1)) {
    f->finished = true;
    int t = transition(f, VMEV_FINISHED);
    if (t >= 0) f->next = (uint8_t)t;
  }
  vm.depth = 0;
  run_switches(f);
}

/* ---------------------------------------------------------------- the room */
static const uint8_t *room_vm(void) {
  const uint8_t *s = section(SEC_VM);
  uint32_t off = rd32(s + 4 * g_room.id);
  return off ? s + off : NULL;
}

void vm_enter(void) {
  memset(&vm, 0, sizeof vm);
  vm.title = -1;
  for (int k = 0; k < MAX_MOVERS; k++) vm.movers[k].obj = -1;
  const uint8_t *b = room_vm();
  if (!b) return;
  int nobjs = rd16(b), nfsms = rd16(b + 2), nmap = rd16(b + 4);
  if (nobjs > MAX_VM_OBJS || nfsms > MAX_VM_FSMS) return;
  vm.nobjs = nobjs;
  vm.rec = (const ObjRec *)(const void *)(b + 8);
  vm.map = b + 8 + sizeof(ObjRec) * nobjs;
  for (int i = 0; i < nobjs; i++) {
    const ObjRec *r = &vm.rec[i];
    Obj *o = &vm.objs[i];
    o->x = r->x, o->y = r->y, o->sx = r->sx, o->sy = r->sy;
    o->flags = r->flags & (OF_ACTIVE | OF_RENDERER | OF_COLLIDER);
    /* (DeactivateIfPlayerdataTrue, False; or a scene loaded with the room's by a bool, not now) */
    if ((r->cond != NONE && pd_flag(r->cond & 0x7FFF) == ((r->cond & 0x8000) != 0)) ||
        (r->cond2 != NONE && pd_flag(r->cond2 & 0x7FFF) == ((r->cond2 & 0x8000) != 0)))
      o->flags = OF_COND_OFF;
    o->anim.clip = -1, o->anim.sprite = -1;
    if (r->flags & OF_WAVE) o->anim.fps = r->bx;
    o->alpha = 255;
  }
  const uint8_t *p = vm.map + 4 * nmap;
  int nvars = 0;
  for (int i = 0; i < nfsms; i++) {
    Fsm *f = &vm.fsms[i];
    int di = rd16(p), n = rd16(p + 6);
    if (nvars + n > MAX_VM_VARS) return;
    f->def = def_at(di), f->owner = rd16(p + 2), f->name = rd16(p + 4), f->var0 = (uint16_t)nvars, f->nvars = f->def[1];
    for (int k = 0; k < n; k++) vm.vars[nvars + k] = rd32(p + 8 + 4 * k);
    nvars += n;
    p += 8 + 4 * n;
    f->next = 255;
  }
  vm.nfsms = nfsms;
  /* (PersistentBoolItem's Start: its FSM's Activated as the save has it) */
  vm.persist = p, vm.npersist = rd16(b + 6);
  for (int i = 0; i < vm.npersist; i++, p += 4)
    if (p[0] < nfsms && persist_get(rd16(p + 2))) set_var(&vm.fsms[p[0]], p[1], 1);
  vm.prev_keys = g_hero.keys;
  /* (what it holds within the condition's: off too) */
  for (int i = 0; i < nobjs; i++)
    for (int p = vm.rec[i].parent, k = 0; p != NONE && p < nobjs && k < 16; p = vm.rec[p].parent, k++)
      if (vm.objs[p].flags & OF_COND_OFF) vm.objs[i].flags = OF_COND_OFF;
  obj_colliders();
  anims_start();
  for (int i = 0; i < nfsms; i++)
    if (obj_active(vm.fsms[i].owner)) fsm_start(&vm.fsms[i]);
}

void vm_tick(void) {
  if (!vm.nfsms) return;
  /* (FSMs whose objects came on: their Start) */
  for (int i = 0; i < vm.nfsms; i++)
    if (vm.fsms[i].pending && !vm.fsms[i].on && obj_active(vm.fsms[i].owner)) fsm_start(&vm.fsms[i]);
  /* the Knight in the objects' triggers */
  for (int i = 0; i < vm.nobjs; i++) {
    Obj *o = &vm.objs[i];
    o->flags = (uint16_t)((o->flags & ~(OF_WAS_INSIDE | OF_SPELL_WAS)) | ((o->flags & OF_INSIDE) ? OF_WAS_INSIDE : 0) |
                          ((o->flags & OF_SPELL_IN) ? OF_SPELL_WAS : 0));
    bool on = (vm.rec[i].flags & OF_TRIGGER) && (o->flags & OF_COLLIDER) && obj_active(i);
    bool in = on && hero_box_in(i), spell = on && (o->flags & OF_SPELL_HIT);
    o->flags = (uint16_t)((in ? o->flags | OF_INSIDE : o->flags & ~OF_INSIDE) & ~OF_SPELL_HIT);
    o->flags = (uint16_t)(spell ? o->flags | OF_SPELL_IN : o->flags & ~OF_SPELL_IN);
    if (vm.rec[i].flags & OF_WAVE) {
      /* (WaveEffectControl: its speed down by a twentieth a step, to a half at least; its timer at that speed; past 1,
       * off) */
      if (obj_active(i)) {
        o->anim.fps = o->anim.fps * 0.95f < 0.5f ? 0.5f : o->anim.fps * 0.95f;
        if ((o->anim.time += DT * o->anim.fps) > 1) obj_set_active(i, false);
      }
      continue;
    }
    if (vm.rec[i].flags & OF_FADE) {
      /* (SimpleSpriteFade: over its time; then off) */
      if (obj_active(i) && (o->anim.time += DT / vm.rec[i].bx) >= 1) obj_set_active(i, false);
      continue;
    }
    o->anim.events = 0;
    anim_update(&o->anim, DT);
    if ((vm.rec[i].flags & OF_ANIM_OFF) && (o->anim.events & ANIM_DONE)) obj_set_active(i, false);
  }
  /* (those set moving: falling to the ground) */
  for (int k = 0; k < MAX_MOVERS; k++) {
    Mover *m = &vm.movers[k];
    if (m->obj < 0) continue;
    Obj *o = &vm.objs[m->obj];
    if (m->tween == 3) {
      /* (flung: its body; ObjectBounce as it hits, from the speed it had) */
      Body b;
      memset(&b, 0, sizeof b);
      b.x = o->x, b.y = o->y, b.vx = m->vx, b.vy = m->vy;
      b.ox = rdf(m->box), b.oy = rdf(m->box + 4), b.hx = rdf(m->box + 8), b.hy = rdf(m->box + 12);
      b.gravity_scale = m->g, b.friction = rdf(m->box + 20), b.mask = CF_TERRAIN;
      b.ncontacts = m->nc;
      for (int c = 0; c < m->nc; c++) b.ccol[c] = m->ccol[c], b.cnx[c] = m->cnx[c], b.cny[c] = m->cny[c];
      float pvx = b.vx, pvy = b.vy;
      int had = b.ncontacts;
      body_step(&b, DT);
      if (b.ncontacts > had) body_bounce(&b, pvx, pvy, had, rdf(m->box + 16));
      m->vx = b.vx, m->vy = b.vy;
      m->nc = (uint8_t)(b.ncontacts < 2 ? b.ncontacts : 2);
      for (int c = 0; c < m->nc; c++) m->ccol[c] = b.ccol[c], m->cnx[c] = b.cnx[c], m->cny[c] = b.cny[c];
      obj_move(m->obj, b.x, b.y);
      continue;
    }
    float nx = o->x + m->vx * DT, ny = o->y;
    if (m->g > 0) {
      m->vy -= 60 * m->g * DT;
      ny = o->y + m->vy * DT;
      PhysHit hit;
      if (m->vy < 0 && phys_ray(o->x, o->y, 0, -1, o->y - ny + 0.76f, CF_TERRAIN, &hit) && hit.dist <= o->y - ny + 0.76f)
        ny = o->y - hit.dist + 0.76f, m->vy = 0;
    }
    obj_move(m->obj, nx, ny);
  }
  for (int i = 0; i < vm.nfsms; i++) fsm_step(&vm.fsms[i], M_FIXED);
  /* (tweens: iTween's Update) */
  for (int k = 0; k < MAX_MOVERS; k++) {
    Mover *m = &vm.movers[k];
    if (m->obj < 0 || m->tween != 1) continue;
    m->tt += DT;
    float q = m->ttime > 0 ? m->tt / m->ttime : 1;
    if (m->loop && m->ttime > 0) {
      /* (looping: never over; back and forth, the way back eased as the way there) */
      float span = m->loop == 2 ? 2 * m->ttime : m->ttime;
      while (m->tt >= span) m->tt -= span;
      q = m->tt / m->ttime;
      if (q > 1) q = 2 - q;
    } else if (q >= 1) q = 1, m->tween = 2;
    float e = ease(m->ease, q), x, y;
    obj_pos(m->obj, &x, &y);
    obj_move(m->obj, x + m->tdx * (e - m->te), y + m->tdy * (e - m->te));
    m->te = e;
  }
  for (int i = 0; i < vm.nfsms; i++) fsm_step(&vm.fsms[i], M_UPDATE);
  vm.prev_keys = g_hero.keys;
  /* (SaveState: Activated into the save) */
  for (int i = 0; i < vm.npersist; i++) {
    const uint8_t *p = vm.persist + 4 * i;
    const Fsm *f = &vm.fsms[p[0]];
    if (p[1] < f->nvars && vm.vars[f->var0 + p[1]] && !persist_get(rd16(p + 2))) persist_set(rd16(p + 2));
  }
  if (vm.title_start) {
    vm.title_start = false;
    if (vm.title >= 0)
      title_show(vm.title, (vm.title_npc ? TF_NPC : 0) | (vm.title_visited ? TF_VISITED : 0) | (vm.title_right ? TF_RIGHT : 0));
    vm.title_npc = vm.title_visited = vm.title_right = false;
  }
}

/* (a colour for an object this frame: the same colour, the same slot; the room's slots no one else uses, graded with
 * it: below the HUD's) */
static const uint8_t tint_slots[] = {8, 9, 12, 13, 14};
static uint32_t tint_used[sizeof tint_slots];
static uint8_t obj_tint(int *n, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
  uint32_t c = (uint32_t)r | (uint32_t)g << 8 | (uint32_t)b << 16 | (uint32_t)a << 24;
  int k = 0;
  while (k < *n && tint_used[k] != c) k++;
  if (k == *n) {
    if (*n < (int)sizeof tint_slots) (*n)++;
    else k = *n - 1;   /* (more than there are: the last one's) */
    tint_used[k] = c;
  }
  return gfx_dyn_tint(tint_slots[k], r, g, b, a);
}

void vm_draw(void) {
  int ntint = 0;
  for (int i = 0; i < vm.nobjs; i++) {
    const Obj *o = &vm.objs[i];
    const ObjRec *r = &vm.rec[i];
    if (!(o->flags & OF_RENDERER) || !obj_active(i)) continue;
    int sprite = (r->flags & OF_ANIMATOR) ? o->anim.sprite : (r->sprite == NONE ? -1 : r->sprite);
    if (sprite < 0) continue;
    Inst in;
    /* (its color: its renderer's, its alpha the scripts' too; a wave's, a fade's as they go) */
    float a = r->a / 255.0f * o->alpha / 255.0f, t = o->anim.time, sx = o->sx, sy = o->sy;
    if (r->flags & OF_WAVE) a *= 1 - t, sx = sy = (1 + 4 * t) * r->by;
    else if (r->flags & OF_FADE) a = a + (r->by - a) * (t > 1 ? 1 : t);
    uint8_t al = (uint8_t)(a <= 0 ? 0 : a >= 1 ? 255 : a * 255 + 0.5f), tint = 0;
    if (al != 255 || (r->r & r->g & r->b) != 255) tint = obj_tint(&ntint, r->r, r->g, r->b, al);
    if (!al) continue;
    sprite_inst(sprite, o->x, o->y, r->z, sx, sy, tint, &in);
    in.flags = (uint8_t)((in.flags & ~F_BLEND) | r->blend);
    gfx_actor(&in, SORT_KEY(r->layer, r->order));
  }
}

#ifdef HOST
void vm_debug(void) {
  printf("   vm");
  for (int i = 0; i < vm.nfsms; i++) {
    const Fsm *f = &vm.fsms[i];
    if (f->on) printf(" %d:%d", i, f->state);
    else printf(" %d:-", i);
  }
  printf("\n");
  if (getenv("VMOBJ"))
    for (int i = 0; i < vm.nobjs; i++)
      printf("   obj %d name %d flags %d active %d cols %d+%d at %.2f,%.2f clip %d sprite %d/%d alpha %d scale %.2f,%.2f\n", i,
             vm.rec[i].name, vm.objs[i].flags, obj_active(i), vm.rec[i].col0, vm.rec[i].ncol, vm.objs[i].x, vm.objs[i].y,
             vm.objs[i].anim.clip, vm.objs[i].anim.sprite, vm.rec[i].sprite, vm.objs[i].alpha, vm.objs[i].sx, vm.objs[i].sy);
  if (getenv("VMOBJ"))
    for (int i = 0; i < vm.nobjs; i++)
      if (vm.rec[i].flags & OF_TRIGGER)
        printf("   trigger %d at %.2f,%.2f half %.2f,%.2f in %d\n", i, vm.objs[i].x + vm.rec[i].bx, vm.objs[i].y + vm.rec[i].by,
               vm.rec[i].bhx, vm.rec[i].bhy, (vm.objs[i].flags & OF_INSIDE) != 0);
}
#endif
