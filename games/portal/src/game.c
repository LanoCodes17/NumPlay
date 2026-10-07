/* The test chambers: level loading and the whole simulation.
 *
 * This follows Portal Returns routine by routine so that the player, the
 * cube and the energy pellet move, collide and go through portals exactly
 * like on the TI-84 Plus CE, frame for frame (about 47 frames a second).
 * Units: x in 2 pixel steps, y in pixels. The level is 20x15 tiles of 16px. */
#include "portal.h"
#include "assets.h"

/* tiles */
enum {
  T_EMPTY = 1, T_SOLID = 2, T_SPIKE = 0x0F, T_DOOR = 0x10, T_FIZZ = 0x14, T_RECV_OFF = 0x15, T_RECV_ON = 0x16,
  T_GLASS = 0x17, T_DROP = 0x18, T_BLOODY = 0x1F
};

/* ------------------------------------------------------------ state */
static u8 map[300];
static u8 last_row, last_col; /* where the last tile lookup landed */

/* player */
static u8 px, py, up, fall, spl, spr_, jumpv, faceleft, walking, anim_sub, anim_frame, friction;
static const u8 *pspr;
static u8 startx, starty;
static u8 dead, dead_t; /* death flag and animation counter */
static u8 fire_latch, grab_latch;

/* portals: orange and blue, y then x; 0xFF = none */
static u8 o_y = 0xFF, o_x, b_y = 0xFF, b_x;
static u8 o_horiz, b_horiz;   /* drawn with the horizontal sprite */
static u8 o_dir, b_dir;       /* 0 floor, 1 right wall, 2 ceiling, 3 left wall */
static u8 next_orange;        /* 0xFF: the next shot is orange */
static u8 shot_dir;           /* direction of the portal being placed / exit */

/* traversal of a portal */
static u8 depth;              /* how deep the player is in the entry portal */
static u8 off;                /* offset along the portal */
static u8 en_y, en_x, ex_y, ex_x, en_dir;  /* entry and exit portal of this check */
static u8 swapped;

/* cube */
static u8 has_cube, holding, cx, cy, c_up, c_fall, c_l, c_r, c_air, c_fric, drop_x, drop_y, cube_timer, c_off;

/* buttons: x, y, pressed, door col, door row, counter, door frame */
typedef struct {
  u8 flag, x, y, on, dcol, drow, t, frame;
} button_t;
static button_t buttons[9];
static u8 nbuttons;
static u8 btn_pos[9][2];

/* fields: kind (1 fizzler, 0 electric), x, y, vertical */
typedef struct {
  u8 kind, x, y, vert, pad;
} field_t;
static field_t fields[28];
static u8 nfields;

/* energy pellet: direction, launch point, door state and position, pellet position */
typedef struct {
  u8 dir, x0, y0, on, dcol, drow, t, frame, x, y;
} pellet_t;
static pellet_t pel;
static u8 has_pellet, pellet_live, pellet_vanish, pel_dir0;

/* level */
static int cur_pack, cur_level;
static u8 fly_ok;

/* ------------------------------------------------------------ packs */
const pack_t PACKS[2] = {{PACK_MAIN, PACK_MAIN_SIZE}, {PACK_PRELUDE, PACK_PRELUDE_SIZE}};

int pack_levels(int pack) { return PACKS[pack].data[0]; }

/* Levels end with 0xAF: the original finds level n by skipping n-1 of
 * them, one byte then up to the next 0xAF. */
const u8 *level_ptr(int pack, int n) {
  const u8 *p = PACKS[pack].data + 1, *end = PACKS[pack].data + PACKS[pack].size;
  while (--n > 0) {
    p++;
    while (p < end && *p != 0xAF) p++;
    p++;
  }
  return p;
}

/* ------------------------------------------------------------ tiles */
/* Tile under a point; outside the screen counts as solid. */
static u8 tile_at(int x2, int y) {
  x2 &= 0xFF, y &= 0xFF;
  if (y == 0xFF || y >= 0xF0 || x2 == 0xFF || x2 >= 0xA0) return T_SOLID;
  last_row = y >> 4, last_col = x2 >> 3;
  return map[last_row * 20 + last_col];
}
static bool empty_tile(u8 t) { return t == T_EMPTY || t == T_FIZZ; }

static void set_tile(int col, int row, u8 t) {
  map[row * 20 + col] = t;
  tile_draw(t, col, row);
}
/* set the tile under a point again (redraws it) */
static void redraw_at(int x2, int y) {
  u8 t = tile_at(x2, y);
  if (!((x2 & 0xFF) >= 0xA0 || (y & 0xFF) >= 0xF0)) set_tile((x2 & 0xFF) >> 3, (y & 0xFF) >> 4, t);
}

static u8 sra(u8 v) { return (u8)((s8)v >> 1); }
static bool in_box(int x, int y, int x0, int y0, int x1, int y1) { return x >= x0 && x < x1 && y >= y0 && y < y1; }

/* ------------------------------------------------------------ drawing helpers */
/* Sprites by their address in the original program: the table keeps the
 * original order and sizes, so offsets between them work the same. */
#define SP(a) (S_E1C7 + ((a) - 0xE1C7))
static void draw_with(const u8 *s, int x2, int y, int slot, u16 c) {
  u16 keep = pal[slot];
  pal[slot] = c;
  spr(s, x2, y);
  pal[slot] = keep;
}

static void draw_portals(void) {
  /* orange as is, blue with the draw slot set to blue */
  if (o_y != 0xFF) spr(o_horiz ? SP(0xE1D9) : SP(0xE1C7), o_x, o_y);
  if (b_y != 0xFF) draw_with(b_horiz ? SP(0xE1D9) : SP(0xE1C7), b_x, b_y, C_DRAW, pal[C_BLUE]);
}

static void draw_hud(void) {
  if (!next_orange) draw_with(SP(0xE1FF), 0x9A, 0xE4, C_DRAW, pal[C_BLUE]);
  else spr(SP(0xE1FF), 0x9A, 0xE4);
}

static const u8 *cube_spr(void) { return SP(0xE74B); }

static void draw_player_at(int x, int y) { /* C067: with the cube, and the portals on top */
  if (has_cube && !(holding && depth)) {
    if (holding) {
      cx = px + 1 + !faceleft;
      cy = py + 4;
    }
    spr(pspr, x, y);
    spr(cube_spr(), cx, cy);
  } else spr(pspr, x, y);
  draw_portals();
}
static void draw_player(void) { draw_player_at(px, py); }

static const u8 *stand_frame(void) { return pspr = faceleft ? SP(0xE273) : SP(0xE23F); }

static void run_anim(void) {
  if (++anim_sub != 2) return;
  anim_sub = 0;
  if (++anim_frame == 3) anim_frame = 0;
  pspr = (faceleft ? SP(0xE30F) : SP(0xE273)) + 52 * (anim_frame + 1);
}

/* ------------------------------------------------------------ portals: traversal */
/* The player's state against the four kinds of entry portal, set by
 * check_portals(): 0 clear, 0xFE touching, 0xFF inside. */
static u8 st_floor, st_rwall, st_ceil, st_lwall;
static u8 exit_dir;

static void teleport(u8 x, u8 y) { /* C002 */
  px = x, py = y;
  depth = 0, st_floor = 0;
  draw_player();
}

/* What shows at the exit portal while the player is partly inside the
 * entry portal; at full depth the player moves there and keeps the
 * speed (BB5A). */
static void exit_side(void) {
  const u8 *saved = pspr, *h;
  u8 t, x, y, k = depth >> 1;
  int de;
  switch (exit_dir) {
  case 0: /* floor portal: comes out upward */
    t = ex_y - depth;
    h = SP(0xE233), de = faceleft ? 0 : 0x6C;
    if (!depth) {
      spr(SP(0xE233), ex_x + off, (u8)(t - 2));
      pspr = saved;
      return;
    }
    if (k == 1) h = SP(0xE4E1);
    else if (k == 2) h = SP(0xE4CB);
    else if (k == 3) h = SP(0xE4AB);
    else if (k == 4) h = SP(0xE481);
    else if (k == 5) {
      u8 e = spr_ == 1 ? 0 : spr_, l = spl == 1 ? 0 : spl;
      up = fall + l + e + up;
      spr_ = spl = fall = 0;
      stand_frame();
      teleport(ex_x + off, t);
      return;
    }
    pspr = h + de;
    x = ex_x + off;
    spr(SP(0xE233), x, (u8)(t - 2));
    draw_player_at(x, t);
    pspr = saved;
    return;
  case 1: /* right wall: comes out to the left */
    t = ex_x + 1;
    h = SP(0xE227), de = faceleft ? 0 : 0xC0;
    if (!depth) {
      spr(SP(0xE227), (u8)(t - 2), ex_y + off * 2);
      pspr = saved;
      return;
    }
    if (k == 1) h = SP(0xE3EB), de = 0;
    else if (k == 2) h = SP(0xE5A3);
    else if (k == 3) h = SP(0xE583);
    else if (k == 4) h = SP(0xE559);
    else if (k == 5) {
      stand_frame();
      teleport((u8)(t - k - 1), ex_y + off * 2);
      spr(SP(0xE233), px, py);
      u8 a = fall + up + spl + spr_;
      spl = a >= 0x1E ? 0x1E : a;
      spr_ = up = fall = 0;
      return;
    }
    pspr = h + de;
    x = t - (k + 1), y = ex_y + off * 2;
    spr(SP(0xE227), (u8)(x - 1), y);
    if (fall && off) spr(SP(0xE233), (u8)(ex_x - 5), (u8)(y - 2));
    if (off != 3) spr(SP(0xE233), (u8)(ex_x - 5), ex_y + 10 + off * 2);
    draw_player_at(x, y);
    pspr = saved;
    return;
  case 2: /* ceiling: comes out downward */
    t = ex_y + 2;
    h = SP(0xE233);
    if (k == 1) h = SP(0xE3DF);
    else if (k == 2) h = SP(0xE441);
    else if (k == 3) h = SP(0xE421);
    else if (k == 4) h = faceleft ? SP(0xE3F7) : SP(0xE457);
    else if (k == 5) {
      stand_frame();
      teleport(ex_x + off, t);
      spr(SP(0xE233), px, (u8)(py - 2));
      fall = up + fall + spl + spr_;
      spr_ = up = spl = 0;
      return;
    }
    pspr = h;
    x = ex_x + off;
    spr(SP(0xE233), x, (u8)(t + depth));
    if (fall && off) spr(SP(0xE227), (u8)(ex_x + off - 1), ex_y + 2);
    if (off != 3) spr(SP(0xE227), ex_x + off + 5, ex_y + 2);
    draw_player_at(x, t);
    pspr = saved;
    return;
  default: /* left wall: comes out to the right */
    t = ex_x + 1;
    h = SP(0xE227), de = faceleft ? 0 : 0xC0;
    if (!depth) {
      spr(SP(0xE227), t, ex_y + off * 2);
      pspr = saved;
      return;
    }
    if (k == 1) h = SP(0xE3EB), de = 0;
    else if (k == 2) h = SP(0xE603);
    else if (k == 3) h = SP(0xE5E3);
    else if (k == 4) h = SP(0xE5B9);
    else if (k == 5) {
      stand_frame();
      teleport(t, ex_y + off * 2);
      spr(SP(0xE233), px, py);
      if (en_dir == 3) spr(SP(0xE227), en_x + 1, en_y + off * 2);
      u8 a = up + fall + spl + spr_;
      spr_ = a >= 0x1E ? 0x1E : a;
      spl = up = fall = 0;
      return;
    }
    pspr = h + de;
    y = ex_y + off * 2;
    spr(SP(0xE227), t + k, y);
    if (fall && off) spr(SP(0xE233), ex_x + 1, (u8)(y - 2));
    if (off != 3) spr(SP(0xE233), ex_x + 1, ex_y + 10 + off * 2);
    draw_player_at(t, y);
    pspr = saved;
    return;
  }
}

/* One entry portal (at en_x, en_y facing `dir`). Returns true when the
 * player is inside it (the original then skips the other portal). */
static bool enter(u8 dir) {
  u8 d = en_x, e = en_y, a, k;
  const u8 *h;
  int de;
  switch (dir) {
  case 0: /* floor portal */
    if (px < d || (a = px - d) >= 4) return false;
    off = a;
    a = (u8)(py + 10);
    if (a < e || (a = a - e) >= 12) return false;
    depth = a;
    st_floor = 0xFE;
    exit_side();
    if (!depth) {
      stand_frame();
      return false;
    }
    st_floor = 0xFF;
    h = SP(0xE233), de = faceleft ? 0 : 0x6C, k = depth >> 1;
    if (k == 1) h = SP(0xE481);
    else if (k == 2) h = SP(0xE4AB);
    else if (k == 3) h = SP(0xE4CB);
    else if (k == 4) h = SP(0xE4E1);
    else if (k == 5) st_floor = 0xFE, h = stand_frame();
    pspr = h + de;
    spr(SP(0xE233), px, (u8)(py - 2));
    return true;
  case 1: /* right wall portal */
    if (py < e || (a = py - e) >= 8) return false;
    off = a >> 1;
    a = (u8)(px + 6 - d);
    if (a & 0x80) return false;
    a = (u8)(a * 2 - 2);
    if (a >= 12) return false;
    depth = a;
    st_rwall = 0xFE;
    spr(SP(0xE227), (u8)(px - 1), py);
    exit_side();
    if (!depth) {
      stand_frame();
      return false;
    }
    st_rwall = 0xFF;
    h = SP(0xE227), de = faceleft ? 0 : 0xC0, k = depth >> 1;
    if (k == 1) h = SP(0xE559);
    else if (k == 2) h = SP(0xE583);
    else if (k == 3) h = SP(0xE5A3);
    else if (k == 4) h = SP(0xE3EB), de = 0;
    else if (k == 5) st_rwall = 0xFE, h = stand_frame();
    pspr = h + de;
    return true;
  case 2: /* ceiling portal */
    if (px < d || (a = px - d) >= 4) return false;
    off = a;
    a = (u8)(py - 2);
    if (e < a || (a = e - a) >= 12) return false;
    depth = a;
    spr(SP(0xE233), en_x + off, en_y + 2);
    exit_side();
    st_ceil = 0xFE;
    if (!depth) {
      stand_frame();
      return false;
    }
    st_ceil = 0xFF;
    h = SP(0xE233), k = depth >> 1;
    if (k == 1) h = faceleft ? SP(0xE3F7) : SP(0xE457);
    else if (k == 2) h = SP(0xE421);
    else if (k == 3) h = SP(0xE441);
    else if (k == 4) h = SP(0xE3DF);
    else if (k == 5) st_ceil = 0xFE, h = stand_frame();
    pspr = h;
    spr(SP(0xE233), en_x + off, (u8)(en_y + 12 - depth));
    return true;
  default: /* left wall portal */
    if (py < e || (a = py - e) >= 8) return false;
    off = a >> 1;
    a = (u8)(d - (u8)(px - 1));
    if (a & 0x80) return false;
    a = (u8)(a * 2);
    if (a >= 12) return false;
    depth = a;
    st_lwall = 0xFE;
    exit_side();
    st_lwall = 0xFE;
    if (!depth) {
      stand_frame();
      return false;
    }
    st_lwall = 0xFF;
    h = SP(0xE227), de = faceleft ? 0 : 0xC0, k = depth >> 1;
    if (k == 1) h = SP(0xE5B9);
    else if (k == 2) h = SP(0xE5E3);
    else if (k == 3) h = SP(0xE603);
    else if (k == 4) h = SP(0xE3EB), de = 0;
    else if (k == 5) st_lwall = 0xFE, h = stand_frame();
    pspr = h + de;
    spr(SP(0xE227), (u8)(en_x + 6 - k), en_y + off * 2);
    return true;
  }
}

/* B7F3: the player against both portals, blue first (orange first while
 * inside the orange one). */
static void check_portals(void) {
  depth = off = 0;
  st_floor = st_ceil = st_rwall = st_lwall = 0;
  if (!swapped) {
    if (b_y == 0xFF || o_y == 0xFF) return;
    ex_y = o_y, ex_x = o_x, en_y = b_y, en_x = b_x;
    exit_dir = o_dir, en_dir = b_dir;
    if (enter(b_dir)) return;
  }
  ex_y = b_y, ex_x = b_x, en_y = o_y, en_x = o_x;
  exit_dir = b_dir, swapped = 0xFF, en_dir = o_dir;
  if (enter(o_dir)) return;
  swapped = 0;
}

/* draw the player at the entry opening instead of its logical place */
static void draw_at_ceiling(void) { /* B41C */
  u8 y = py;
  py = en_y + 2;
  draw_player();
  py = y;
}
static void draw_at_lwall(void) { /* B76D */
  u8 x = px;
  px = en_x + 1;
  draw_player();
  px = x;
}

/* ------------------------------------------------------------ player movement */
static void level_exit(void);
static bool left_game; /* set when the level ends (next level or quit) */

static bool hop(void) { /* B66A: at the lip of a side portal */
  if (off == 0 || off == 4) {
    up = 4;
    return true;
  }
  return false;
}

/* 2 pixels down (B44B) */
static void move_down(void) {
  if (st_ceil == 0xFF) { /* B435 */
    py += 2;
    check_portals();
    if (depth) draw_at_ceiling();
    else draw_player();
    return;
  }
  if (st_ceil != 0xFE) {
    if (st_floor == 0xFE) goto erase_legs;
    if (st_floor == 0xFF) goto erase_top;
    if (st_rwall != 0xFE) {
      if (st_rwall == 0xFF && off != 3) goto side;
      if (st_lwall != 0xFE && st_lwall == 0xFF && off != 3) goto side;
    }
  }
  /* B49F */
  stand_frame();
  if (!empty_tile(tile_at(px, py + 10))) return;
  if (!empty_tile(tile_at(px + 4, py + 10))) return;
erase_legs:
  spr(SP(0xE221), px, py + 6);
  spr(SP(0xE221), px + 4, py + 6);
erase_top:
  spr(SP(0xE233), px, py);
step:
  py += 2;
  check_portals();
  if (st_lwall == 0xFF) draw_at_lwall();
  else draw_player();
  return;
side: /* B529 */
  spr(SP(0xE233), st_lwall ? en_x + 1 : (u8)(en_x - 5), en_y + off * 2);
  goto step;
}

/* 2 pixels up (B2FB) */
static void move_up(void) {
  if (st_rwall == 0xFE) goto normal;
  if (st_rwall == 0xFF) {
    if (hop()) return;
    goto side;
  }
  if (st_lwall == 0xFE) goto normal;
  if (st_lwall == 0xFF) {
    if (hop()) return;
    goto side;
  }
  if (st_floor == 0xFE) goto erase;
  if (st_floor) goto step;
  if (st_ceil == 0xFE) goto erase;
  if (st_ceil) { /* B40C */
    py -= 2;
    check_portals();
    if (!depth) draw_player();
    else draw_at_ceiling();
    return;
  }
normal: /* B351 */
  stand_frame();
  if (!empty_tile(tile_at(px + 4, py - 2)) || !empty_tile(tile_at(px, py - 2))) {
    up = 0;
    return;
  }
erase:
  spr(SP(0xE233), px, py + 8);
step:
  py -= 2;
  check_portals();
draw: /* B3A9 */
  if (!st_floor && depth == 2) draw_at_ceiling();
  else draw_player();
  return;
side: /* B3BD */
  if (st_lwall == 0xFE) goto draw;
  spr(SP(0xE233), st_lwall ? en_x + 1 : (u8)(en_x - 5), en_y + off * 2 + 8);
  py -= 2;
  check_portals();
  if (st_lwall == 0xFF) draw_at_lwall();
  else if (st_rwall == 0xFF) draw_player();
  else goto draw;
}

/* 2 pixels right (B554) */
static void move_right(void) {
  if (px == 0x9B) {
    level_exit();
    return;
  }
  faceleft = 0;
  if (st_floor == 0xFE) goto check;
  if (st_floor == 0xFF) {
    if (off == 3) goto moved;
    if (off < 2) goto inc;
  }
  if (st_ceil) return;
  if (st_lwall == 0xFE) goto erase;
  if (st_lwall) { /* B785 */
    px++;
    check_portals();
    if (!depth) draw_player();
    else draw_at_lwall();
    return;
  }
  if (st_rwall == 0xFE) goto erase;
  if (st_rwall) goto erase1;
check: /* B5A7 */
  if (!empty_tile(tile_at(px + 5, py + 9)) || !empty_tile(tile_at(px + 5, py))) {
    spr_ = 0;
    return;
  }
  if (walking) run_anim();
erase:
  spr(SP(0xE221), px, py + 6);
  spr(SP(0xE221), px, py);
erase1:
  spr(SP(0xE221), px, py);
inc:
  px++;
moved:
  check_portals();
  if (st_ceil == 0xFF) { /* B63A */
    spr(SP(0xE227), (u8)(px - 1), en_y + 2);
    spr(SP(0xE227), px + 5, en_y + 2);
    draw_at_ceiling();
  } else draw_player();
}

/* 2 pixels left (B67C) */
static void move_left(void) {
  faceleft = 0xFF;
  if (st_floor == 0xFE) goto check;
  if (st_floor == 0xFF) {
    if (off) goto dec;
    return;
  }
  if (st_ceil) return;
  if (st_rwall == 0xFF) goto dec;
  if (st_lwall == 0xFE) goto erase;
  if (st_lwall) { /* B75C */
    px--;
    check_portals();
    if (!depth) draw_player();
    else draw_at_lwall();
    return;
  }
check: /* B6BF */
  if (!px) return;
  if (!empty_tile(tile_at(px - 1, py + 9)) || !empty_tile(tile_at(px - 1, py))) {
    spl = 0;
    return;
  }
  if (walking) run_anim();
erase:
  spr(SP(0xE221), px + 3, py);
  spr(SP(0xE221), px + 4, py + 6);
dec: /* B731 */
  px--;
  check_portals();
  if (st_ceil == 0xFF) {
    spr(SP(0xE227), (u8)(px - 1), en_y + 2);
    spr(SP(0xE227), px + 5, en_y + 2);
    draw_at_ceiling();
  } else if (!st_lwall) draw_player();
  else if (depth == 2) draw_at_lwall();
  else draw_player();
}

/* ------------------------------------------------------------ portal gun */
static u8 fire_up_flag, fire_right_flag; /* V53D1, V53D2 */

static bool pass(u8 t) { return t == T_EMPTY || t == T_GLASS; } /* beams go through */

static void dot(u8 d, u8 e) { spr(next_orange ? SP(0xE1F3) : SP(0xE1EB), d, e); }
static void undot(u8 d, u8 e) { spr(tile_at(d, e) == T_GLASS ? SP(0xE1FB) : SP(0xE1F7), d, e); }

/* Which tiles take a portal on each face (a = tile - 1, like the original). */
static bool bad_floor(u8 a) { /* LD72D, C435, C56A */
  if (a == 0 || a == 1 || a == 3 || a == 4 || a == 5 || a == 7 || a == 9 || a == 13) return true;
  if (a == 0x1B || a == 0x1D) return false;
  return a >= 14;
}
static bool bad_ceiling(u8 a) { /* LD753, C10D, C4DE */
  if (a == 0 || a == 1 || a == 2 || a == 4 || a == 5 || a == 6 || a == 13) return true;
  if (a == 0x1B || a == 0x1C) return false;
  return a >= 14;
}
static bool bad_rface(u8 a) { /* LD7BF, C249: the wall's right face */
  if (a < 5 || a == 8 || a == 9 || a == 10 || a == 12) return true;
  if (a == 0x1B || a == 0x1C || a == 0x1D) return false;
  return a >= 14;
}
static bool bad_lface(u8 a) { /* LD7E2, C302: the wall's left face */
  if (a < 4 || a == 5 || a == 6 || a == 7 || a == 11 || a == 12) return true;
  if (a == 0x1B || a == 0x1C || a == 0x1D) return false;
  return a >= 14;
}

static bool aborted; /* the shot was cancelled while placing */

/* LD664: keep the new portal (at d, e) off the other portal: returns true
 * after moving it one step, and cancels the shot if it can't move. */
static bool adjust(u8 *d, u8 *e) {
  u8 oy, ox, oh;
  if (!next_orange) oy = o_y, ox = o_x, oh = o_horiz;
  else oy = b_y, ox = b_x, oh = b_horiz;
  if (oy == 0xFF) return false;
  if (oh) { /* the other one is horizontal: [ox, ox+8) x [oy, oy+1) */
    if (in_box(*d, (u8)(*e - 1), ox, oy, ox + 8, oy + 1)) {
      (*d)++;
      u8 t = tile_at(*d + 8, *e);
      if (fire_up_flag ? bad_ceiling(t - 1) : bad_floor(t - 1)) return aborted = true, false;
      return true;
    }
    if (in_box(*d + 7, (u8)(*e - 1), ox, oy, ox + 8, oy + 1)) {
      (*d)--;
      u8 t = tile_at(*d, *e);
      if (fire_up_flag ? bad_ceiling(t - 1) : bad_floor(t - 1)) return aborted = true, false;
      return true;
    }
    return false;
  }
  /* vertical: [ox, ox+1) x [oy, oy+16) */
  u8 h = fire_right_flag ? *d - 1 : *d + 1;
  if (in_box(h, (u8)(*e + 16), ox, oy, ox + 1, oy + 16)) {
    *e -= 2;
    u8 t = tile_at(*d + 1, *e);
    if (fire_right_flag ? bad_lface(t - 1) : bad_rface(t - 1)) return aborted = true, false;
    return true;
  }
  if (in_box(h, *e, ox, oy, ox + 1, oy + 16)) {
    *e += 2;
    u8 t = tile_at(*d, *e + 16);
    if (fire_right_flag ? bad_lface(t - 1) : bad_rface(t - 1)) return aborted = true, false;
    return true;
  }
  return false;
}
static bool adjust_all(u8 *d, u8 *e) {
  aborted = false;
  while (adjust(d, e)) {}
  return !aborted;
}

static void erase_portal(bool orange) { /* C6AD / C6DD: back to the wall's edge color */
  if (orange) {
    if (o_y != 0xFF) draw_with(o_horiz ? SP(0xE1D9) : SP(0xE1C7), o_x, o_y, C_DRAW, pal[C_EDGE]);
  } else if (b_y != 0xFF) draw_with(b_horiz ? SP(0xE1D9) : SP(0xE1C7), b_x, b_y, C_DRAW, pal[C_EDGE]);
}

/* C517: put the portal of the current color at (d, e) */
static void place(u8 d, u8 e, u8 horiz) {
  erase_portal(next_orange);
  if (next_orange) o_horiz = horiz;
  else b_horiz = horiz;
  next_orange = ~next_orange;
  if (!next_orange) {
    o_y = e, o_x = d, o_dir = shot_dir;
  } else {
    b_y = e, b_x = d, b_dir = shot_dir;
  }
  draw_portals();
  hint(HINT_SWAP);
}

static void place_ceiling(u8 d, u8 e) { /* C4C7 */
  e++;
  fire_up_flag = 0xFF;
  if (!adjust_all(&d, &e)) return;
  e--, d++;
  shot_dir = 2;
  for (;;) {
    u8 t = tile_at(d + 6, e);
    d--;
    if (!bad_ceiling(t - 1)) break;
  }
  e++;
  aborted = false;
  adjust(&d, &e);
  if (aborted) return;
  e--;
  place(d, e, 0xFF);
}

static void place_floor(u8 d, u8 e) { /* C557 */
  fire_up_flag = 0;
  if (!adjust_all(&d, &e)) return;
  d++;
  shot_dir = 0;
  for (;;) {
    u8 t = tile_at(d + 6, e);
    d--;
    if (!bad_floor(t - 1)) break;
  }
  aborted = false;
  adjust(&d, &e);
  if (aborted) return;
  e--;
  place(d, e, 0xFF);
}

static void place_rface(u8 d, u8 e) { /* C267: a wall to the left */
  fire_right_flag = 0;
  if (!adjust_all(&d, &e)) return;
  e++;
  shot_dir = 3;
  if (tile_at(d + 2, e) == T_FIZZ) return;
  for (;;) {
    u8 t = tile_at(d, e + 14);
    e--;
    if (!bad_rface(t - 1)) break;
  }
  aborted = false;
  adjust(&d, &e);
  if (aborted) return;
  d++;
  place(d, e, 0);
}

static void place_lface(u8 d, u8 e) { /* C323: a wall to the right */
  fire_right_flag = 0xFF;
  if (!adjust_all(&d, &e)) return;
  e++;
  shot_dir = 1;
  for (;;) {
    u8 t = tile_at(d, e + 14);
    e--;
    if (!bad_lface(t - 1)) break;
  }
  aborted = false;
  adjust(&d, &e);
  if (aborted) return;
  d--;
  if (tile_at(d, e) == T_FIZZ) return;
  place(d, e, 0);
}

/* the checks after a beam stops: a = blocking tile - 1 */
static void hit_ceiling(u8 a, u8 d, u8 e) { /* C10D */
  if (bad_ceiling(a)) return;
  place_ceiling(d, e);
}
static void hit_rface(u8 a, u8 d, u8 e) { /* C249 */
  if (bad_rface(a)) return;
  place_rface(d, e);
}
static void hit_lface(u8 a, u8 d, u8 e) { /* C302 */
  if (bad_lface(a)) return;
  place_lface(d, e);
}
static void hit_floor(u8 a, u8 d, u8 e) { /* C435 */
  if (bad_floor(a)) return;
  place_floor(d, e);
}

/* B2DD: one shot per key press */
static bool fire_ok(void) {
  if (fire_latch == 1) return false;
  fire_latch = 1;
  return true;
}

/* Beams: drawn dot by dot along the path, then erased the same way. */
static void fire(int key) {
  u8 d, e, sd, se, t = 0, t2;
  int pass2;
  if (!fire_ok()) return;
  switch (key) {
  case 8: /* up (C0CD) */
    sd = px, se = py - 1;
    for (int k = 0; k < 2; k++) {
      d = sd, e = se;
      do {
        t = tile_at(d, e);
        e++;
        if (t != T_GLASS) (k ? undot : dot)(d, e);
        e -= 3;
      } while (pass(t));
    }
    hit_ceiling(t - 1, d, e + 1);
    return;
  case 2: /* down (C3F3) */
    sd = px, se = py + 10;
    for (int k = 0; k < 2; k++) {
      d = sd, e = se;
      do {
        t = tile_at(d, e);
        e -= 2;
        if (t != T_GLASS) (k ? undot : dot)(d, e);
        e += 3;
      } while (pass(t));
    }
    hit_floor(t - 1, d, e);
    return;
  case 4: /* left (C20C) */
    sd = px - 1, se = py;
    for (int k = 0; k < 2; k++) {
      d = sd, e = se;
      do {
        t = tile_at(d, e);
        d++;
        if (t != T_GLASS) (k ? undot : dot)(d, e);
        d -= 2;
      } while (pass(t));
    }
    hit_rface(t - 1, d, e);
    return;
  case 6: /* right (C2C4) */
    sd = px + 4, se = py;
    for (int k = 0; k < 2; k++) {
      d = sd, e = se;
      do {
        t = tile_at(d, e);
        d--;
        if (t != T_GLASS) (k ? undot : dot)(d, e);
        d += 2;
      } while (pass(t));
    }
    hit_lface(t - 1, d, e);
    return;
  case 7: /* up-left (C12C) */
    sd = px - 1, se = py;
    for (int k = 0; k < 2; k++) {
      d = sd, e = se, pass2 = 0;
      for (;;) {
        t = tile_at(d, e);
        if (t != T_GLASS) (k ? undot : dot)(d + 1, e);
        e -= 2;
        if (!pass(t)) break;
        t2 = tile_at(d, e);
        d--;
        if (!pass(t2)) {
          t = t2, pass2 = 1;
          break;
        }
      }
    }
    if (pass2) hit_ceiling(t - 1, d + 1, e);
    else hit_rface(t - 1, d - 1, e + 2);
    return;
  case 9: /* up-right (C197) */
    sd = px + 4, se = py + 1;
    for (int k = 0; k < 2; k++) {
      d = sd, e = se, pass2 = 0;
      for (;;) {
        t = tile_at(d, e);
        if (t != T_GLASS) (k ? undot : dot)(d - 1, e + 1);
        e -= 2;
        if (!pass(t)) break;
        t2 = tile_at(d, e);
        d++;
        if (!pass(t2)) {
          t = t2, pass2 = 1;
          break;
        }
      }
    }
    if (pass2) hit_ceiling(t - 1, d - 1, e - 1);
    else hit_lface(t - 1, d + 1, e + 1);
    return;
  case 1: /* down-left (C37F) */
    sd = px, se = py + 4;
    for (int k = 0; k < 2; k++) {
      d = sd, e = se, pass2 = 0;
      for (;;) {
        t = tile_at(d, e);
        if (t != T_GLASS) (k ? undot : dot)(d + 1, e);
        e += 2;
        if (!pass(t)) break;
        t2 = tile_at(d, e);
        d--;
        if (!pass(t2)) {
          t = t2, pass2 = 1;
          break;
        }
      }
    }
    if (pass2) hit_floor(t - 1, d + 1, e + 1);
    else hit_rface(t - 1, d - 1, e);
    return;
  case 3: /* down-right (C45A) */
    sd = px + 4, se = py + 4;
    for (int k = 0; k < 2; k++) {
      d = sd, e = se, pass2 = 0;
      for (;;) {
        t = tile_at(d, e);
        if (t != T_GLASS) (k ? undot : dot)(d - 1, e);
        e += 2;
        if (!pass(t)) break;
        t2 = tile_at(d, e);
        d++;
        if (!pass(t2)) {
          t = t2, pass2 = 1;
          break;
        }
      }
    }
    if (pass2) hit_floor(t - 1, d, e + 1);
    else hit_lface(t - 1, d + 1, e + 2);
    return;
  }
}

/* ------------------------------------------------------------ input */
/* The keys as the original reads them: number keys by keypad group, and
 * a shot only fires when a single number key of the group is down. */
static u8 k_arrows;  /* bit 0 down, 1 left, 2 right, 3 up */
static u8 grp5, grp4, grp3; /* 3 6 9 / 2 5 8 / 1 4 7 as bits 1, 2, 3 */
static bool k_grab, k_fly;

static bool number_key(void) {
  if (grp5 == 2) return fire(3), true;
  if (grp5 == 4) return fire(6), true;
  if (grp5 == 8) return fire(9), true;
  if (grp4 == 2) return fire(2), true;
  if (grp4 == 4) {
    if (fire_ok()) next_orange = ~next_orange; /* 5: switch color */
    return true;
  }
  if (grp4 == 8) return fire(8), true;
  if (grp3 == 2) return fire(1), true;
  if (grp3 == 4) return fire(4), true;
  if (grp3 == 8) return fire(7), true;
  return false;
}

/* ------------------------------------------------------------ frame steps */
static void jump(void) { /* B0B2 */
  if (fall) return;
  if (st_rwall == 0xFF) {
    if (hop()) return;
    up = jumpv;
    return;
  }
  if (st_lwall == 0xFF) {
    if (hop()) return;
    up = jumpv;
    return;
  }
  if (st_floor) return;
  if (empty_tile(tile_at(px, py + 10)) && empty_tile(tile_at(px + 4, py + 10))) return;
  up = jumpv;
}

static void press_right(void) { /* B122 */
  if (spl) spl--;
  else if (spr_ < 1) spr_++;
}
static void press_left(void) { /* B10F */
  if (spr_) spr_--;
  else if (spl < 1) spl++;
}

static int steps(u8 v, u8 two, u8 three) { return v < two ? 1 : v < three ? 2 : 3; }

static void vertical(void) { /* B13A */
  int b;
  if (up >= 1 && up <= 4) b = 0;
  else if (!up) b = steps(fall, 0x0D, 0x17);
  else b = steps(up, 0x0D, 0x17);
  do {
    if (depth || st_floor || !number_key()) fire_latch = 0;
    if (!b) {
      up--;
      return;
    }
    if (!up) {
      if (fall) move_down();
    } else if (b == 1) up--;
    if (up) move_up();
    if (left_game) return;
  } while (--b);
}

static void horizontal(void) { /* B21A */
  int b;
  if (spr_) b = steps(spr_, 0x0D, 0x14);
  else if (spl) b = steps(spl, 0x0D, 0x14);
  else return;
  do {
    if (!depth && !st_floor) number_key();
    if (spl) move_left();
    if (spr_) move_right();
    if (left_game) return;
  } while (--b);
}

/* ------------------------------------------------------------ cube */
static void cube_erase(void) { spr(SP(0xE755), cx, cy); }
static void cube_draw(void) { spr(cube_spr(), cx, cy); }

static void cube_reset(void) { /* DA81: back to the dropper */
  holding = cube_timer = 0;
  c_r = c_l = c_up = c_fall = 0;
  redraw_at(cx, cy);
  redraw_at(cx + 1, cy);
  redraw_at(cx + 1, cy + 2);
  redraw_at(cx, cy + 2);
  cx = drop_x * 8 + 3;
  cy = drop_y * 16 + 14;
}

/* LC99D: a tile the cube may enter; a fizzler grid takes the cube away */
enum { C_OK, C_BLOCK, C_FIZZLED };
static int cube_tile(u8 d, u8 y) {
  u8 t = tile_at(d, y);
  if (t == T_FIZZ) {
    cube_reset();
    return C_FIZZLED;
  }
  return t == T_EMPTY ? C_OK : C_BLOCK;
}

static void cube_left(void) { /* CCC2 */
  int r = cube_tile(cx - 1, cy);
  if (r == C_OK) r = cube_tile(cx - 1, cy + 4);
  if (r != C_OK) {
    c_l = 1;
    return;
  }
  cube_erase();
  cx--;
  cube_draw();
}
static void cube_right(void) { /* CCFA */
  int r = cube_tile(cx + 2, cy);
  if (r == C_OK) r = cube_tile(cx + 2, cy + 4);
  if (r != C_OK) {
    c_r = 0;
    return;
  }
  cube_erase();
  cx++;
  cube_draw();
}
static void cube_down(void) { /* CD33 */
  if (cube_tile(cx + 1, cy + 4) != C_OK || cube_tile(cx, cy + 4) != C_OK) return;
  cube_erase();
  cy += 2;
  cube_draw();
}
static void cube_upward(void) { /* CD60 */
  if (cube_tile(cx + 1, cy - 2) != C_OK || cube_tile(cx + 1, cy - 2) != C_OK) return;
  cube_erase();
  cy -= 2;
  cube_draw();
}

/* CDB0: the cube through the portals */
static bool cube_enter(u8 dir) {
  u8 d = en_x, e = en_y, a;
  switch (dir) {
  case 0:
    if (cx < d || (a = cx - d) >= 7) return false;
    c_off = a;
    if ((u8)(cy + 4) != e) return false;
    break;
  case 1:
    if (!c_r) return false;
    if (cy < e || (a = cy - e) >= 13) return false;
    c_off = a >> 1;
    if ((u8)(cx + 2) != d) return false;
    break;
  case 2:
    if (cx < d || (a = cx - d) >= 5) return false;
    c_off = a;
    if (e != (u8)(cy - 2)) return false;
    break;
  default:
    if (!c_l) return false;
    if (cy < e || (a = cy - e) >= 13) return false;
    c_off = a >> 1;
    if (d != (u8)(cx - 1)) return false;
    break;
  }
  u8 nx, ny;
  switch (exit_dir) {
  case 0:
    if (!c_fall) c_up = 8;
    c_up = c_fall + c_up + sra(c_l) + sra(c_r);
    c_r = c_l = c_fall = 0;
    ny = ex_y - 4, nx = ex_x + c_off;
    break;
  case 1:
    if (!c_fall) c_l = 2;
    c_l = c_fall + c_up + c_l + c_r;
    c_r = c_up = c_fall = 0;
    nx = ex_x - 3, ny = ex_y + c_off * 2;
    break;
  case 2:
    c_fall = c_fall + c_up + sra(c_l) + sra(c_r);
    c_r = c_up = c_l = 0;
    ny = ex_y + 4, nx = ex_x + c_off;
    break;
  default:
    c_r = c_fall + c_up + c_l + c_r;
    c_l = c_up = c_fall = 0;
    nx = ex_x + 2, ny = ex_y + c_off * 2;
    break;
  }
  cube_erase();
  cx = nx, cy = ny;
  cube_draw();
  return true;
}
static void cube_portals(void) {
  if (b_y == 0xFF || o_y == 0xFF) return;
  ex_y = o_y, ex_x = o_x, en_y = b_y, en_x = b_x, exit_dir = o_dir;
  if (cube_enter(b_dir)) return;
  ex_y = b_y, ex_x = b_x, en_y = o_y, en_x = o_x, exit_dir = b_dir;
  cube_enter(o_dir);
}

static void cube_gravity(void) { /* CFEC */
  if (c_up >= 4) goto ground;
  c_air = 0xFF;
  if (!empty_tile(tile_at(cx, cy + 4)) || !empty_tile(tile_at(cx + 1, cy + 4))) goto ground;
  c_air = 0;
  if (c_fall != 0x3C) c_fall++;
  goto fric;
ground:
  c_fall = 0;
  if (c_l) c_l--;
  if (c_r) c_r--;
fric:
  if (!c_air) {
    if (c_fric != 2) c_fric++;
    else {
      if (c_l) c_l--;
      if (c_r) c_r--;
      c_fric = 0;
    }
  }
}

static void cube_vertical(void) { /* D0A8 */
  int b;
  if (c_up >= 1 && c_up <= 4) b = 0;
  else if (!c_up) b = steps(c_fall, 0x0D, 0x1B);
  else b = steps(c_up, 0x0D, 0x17);
  do {
    if (!b) {
      c_up--;
      return;
    }
    if (!c_up) {
      if (c_fall) cube_down();
    } else if (b == 1) c_up--;
    if (c_up) cube_upward();
  } while (--b);
}

static void cube_horizontal(void) { /* D12F */
  int b;
  if (c_r) b = steps(c_r, 0x0D, 0x14);
  else if (c_l) b = steps(c_l, 0x0D, 0x14);
  else return;
  do {
    if (c_l) cube_left();
    if (c_r) cube_right();
  } while (--b);
}

/* 2nd / OK when released: pick up or drop (AF86) */
static void grab(void) {
  if (grab_latch || !has_cube) return;
  grab_latch = has_cube;
  if (holding) { /* B060: drop, thrown forward */
    cy -= 4;
    c_l = spl < 2 && faceleft ? 0x0A : spl;
    c_r = spr_ < 2 && !faceleft ? 0x0A : spr_;
    c_up = up, c_fall = fall;
    holding = 0;
    return;
  }
  u8 x0 = px < 6 ? 0 : px - 6;
  if (!in_box(cx, cy, x0, (u8)(py - 8), px + 9, py + 20)) return;
  holding = 0xFF;
  cube_erase();
  redraw_at(cx, cy);
  cx = px + 1 + !faceleft;
  cy = py + 4;
  u8 t = tile_at(cx, cy + 1);
  if (t == T_EMPTY || t == T_FIZZ) return;
  for (int i = 0; i < 3; i++) {
    cx--;
    t = tile_at(cx + 1, cy);
    if (t == T_EMPTY || t == T_FIZZ) return;
  }
}

/* ------------------------------------------------------------ doors, buttons, fields */
/* A door opens or closes one frame out of four (C935 / C95B). */
static void door_open(u8 dcol, u8 drow, u8 *t, u8 *frame) {
  if (dcol == 0xFF) return;
  if (++*t != 4) return;
  *t = 0;
  if (*frame == 4) return;
  u8 a = (*frame)++ + 0x10;
  set_tile(dcol, drow, a == 0x13 ? T_EMPTY : a);
}
static void door_close(u8 dcol, u8 drow, u8 *t, u8 *frame) {
  if (dcol == 0xFF) return;
  if (++*t != 4) return;
  *t = 0;
  if (!*frame) return;
  u8 a = (*frame)-- + 0x0F;
  set_tile(dcol, drow, a);
}

static void buttons_step(void) { /* C872 */
  for (int i = 0; i < nbuttons; i++) {
    button_t *b = &buttons[i];
    u8 a;
    bool down = false;
    if (px + 4 >= b->x && (a = px + 4 - b->x) < 11) {
      a = (u8)(py + 10);
      if (a >= b->y && (u8)(a - b->y) < 12) down = true;
    }
    if (!down && has_cube && cx >= b->x && (u8)(cx - b->x) < 7) {
      a = (u8)(cy + 2);
      if (a >= b->y && (u8)(a - b->y) < 12) down = true;
    }
    if (down && b->on) {
      door_open(b->dcol, b->drow, &b->t, &b->frame);
      continue;
    }
    b->on = down;
    spr(down ? SP(0xE6FB) : SP(0xE6D9), b->x, b->y);
    if (has_cube && !depth) cube_draw();
    door_close(b->dcol, b->drow, &b->t, &b->frame);
  }
}

static void draw_buttons(void) { /* C843 */
  for (int i = 0; i < nbuttons; i++) spr(buttons[i].on ? SP(0xE6FB) : SP(0xE6D9), buttons[i].x, buttons[i].y);
}

static void erase_openings(void) { /* DB0C: an empty tile over each portal's mouth */
  const u8 *s = tile_sprite(T_EMPTY);
  for (int k = 0; k < 2; k++) {
    u8 y = k ? o_y : b_y, x = k ? o_x : b_x, dir = k ? o_dir : b_dir;
    if (y == 0xFF) continue;
    if (dir == 0) spr(s, x, (u8)(y - 16));
    else if (dir == 1) spr(s, (u8)(x - 8), y);
    else if (dir == 2) spr(s, x, y + 2);
    else spr(s, x + 1, y);
  }
}

static void clear_portals(void) {
  erase_portal(false);
  erase_portal(true);
  o_y = b_y = 0xFF;
}

static void fields_step(void) { /* C764 */
  for (int i = 0; i < nfields; i++) {
    field_t *f = &fields[i];
    u8 a;
    bool touch = false;
    if (!f->vert) {
      if (px + 4 >= f->x && (u8)(px + 4 - f->x) < 11) {
        a = (u8)(py + 9);
        touch = a >= f->y && (u8)(a - f->y) < 12;
      }
    } else {
      a = (u8)(py - 1);
      if (a >= f->y && (u8)(a - f->y) < 16) {
        a = (u8)(px - 1);
        touch = f->x >= a && (u8)(f->x - a) < 6;
      }
    }
    if (touch && !st_floor && !st_ceil) {
      if (!f->kind) dead = 0xFF;
      else {
        erase_openings();
        clear_portals();
      }
    }
    f->pad = f->vert;
    draw_with(f->vert ? SP(0xE72F) : SP(0xE71D), f->x, f->y, C_FIELD, f->kind ? pal[C_FIZZ] : pal[C_ELEC]);
  }
}

/* ------------------------------------------------------------ energy pellet */
static void pellet_erase(void) { spr(SP(0xE755), pel.x, pel.y); }
static void pellet_draw(void) { spr(SP(0xE741), pel.x, pel.y); }

/* LC988: true if the pellet bounces; receivers light up (or go out) */
static bool pellet_blocked(u8 d, u8 y) {
  u8 t = tile_at(d, y), col = (d & 0xFF) >> 3;
  if (t == T_RECV_OFF) {
    set_tile(col, last_row, T_RECV_ON);
    pel.on = 0xFF;
    if (pellet_vanish) {
      pellet_live = 0;
      pellet_erase();
    }
    return true;
  }
  if (t == T_RECV_ON) {
    set_tile(col, last_row, T_RECV_OFF);
    pel.on = 0;
    return true;
  }
  if (t == T_FIZZ) return false;
  return t != T_EMPTY;
}

static bool pellet_enter(u8 dir) { /* CB9D.. */
  u8 d = en_x, e = en_y, a, k;
  switch (dir) {
  case 0:
    if (pel.dir != 0) return false;
    if (pel.x < d || (a = pel.x - d) >= 7) return false;
    k = a;
    if ((u8)(pel.y + 4) != e) return false;
    break;
  case 3:
    if (pel.dir != 3) return false;
    if (pel.y < e || (a = pel.y - e) >= 14) return false;
    k = a >> 1;
    if (d != (u8)(pel.x - 1)) return false;
    break;
  case 2:
    if (pel.dir != 2) return false;
    if (pel.x < d || (a = pel.x - d) >= 7) return false;
    k = a;
    if (e != (u8)(pel.y - 2)) return false;
    break;
  default:
    if (pel.dir != 1) return false;
    if (pel.y < e || (a = pel.y - e) >= 14) return false;
    k = a >> 1;
    if ((u8)(pel.x + 3) != d) return false;
    break;
  }
  /* CC20: a pellet coming through while the player is in a portal kills */
  if (st_lwall || st_rwall || st_floor || st_ceil) dead = 0xFF;
  u8 nx, ny;
  if (exit_dir == 0) pel.dir = 2, ny = ex_y - 4, nx = ex_x + k;
  else if (exit_dir == 1) pel.dir = 3, nx = ex_x - 2, ny = ex_y + k * 2;
  else if (exit_dir == 2) pel.dir = 0, ny = ex_y + 2, nx = ex_x + k;
  else pel.dir = 1, nx = ex_x + 2, ny = ex_y + k * 2;
  pellet_erase();
  pel.y = ny, pel.x = nx;
  pellet_draw();
  return false;
}

static void pellet_moved(void) { /* CB31 */
  pellet_draw();
  if (b_y == 0xFF || o_y == 0xFF) return;
  ex_y = o_y, ex_x = o_x, en_y = b_y, en_x = b_x, exit_dir = o_dir;
  pellet_enter(b_dir);
  ex_y = b_y, ex_x = b_x, en_y = o_y, en_x = o_x, exit_dir = b_dir;
  pellet_enter(o_dir);
}

static void pellet_step(void) { /* C9E7 */
  if (!has_pellet) return;
  if (pellet_live && in_box(pel.x, pel.y, (u8)(px - 1), (u8)(py - 2), px + 5, py + 11)) dead = 0xFF;
  if (pel.dcol != 0xFF) {
    if (!pel.on) door_close(pel.dcol, pel.drow, &pel.t, &pel.frame);
    if (pel.on) door_open(pel.dcol, pel.drow, &pel.t, &pel.frame);
  }
  if (!pellet_live) return;
  switch (pel.dir) {
  case 0: /* down */
    if (pellet_blocked(pel.x, pel.y + 5) || pellet_blocked(pel.x + 1, pel.y + 5)) {
      pel.dir = 2;
      return;
    }
    pellet_erase();
    pel.y += 2;
    break;
  case 1: /* right */
    if (pellet_blocked(pel.x + 2, pel.y) || pellet_blocked(pel.x + 2, pel.y + 3)) {
      pel.dir = 3;
      return;
    }
    pellet_erase();
    pel.x++;
    break;
  case 2: /* up */
    if (pellet_blocked(pel.x, pel.y - 2) || pellet_blocked(pel.x + 1, pel.y - 2)) {
      pel.dir = 0;
      return;
    }
    pellet_erase();
    pel.y -= 2;
    break;
  default: /* left */
    if (pellet_blocked(pel.x - 1, pel.y) || pellet_blocked(pel.x - 1, pel.y + 3)) {
      pel.dir = 1;
      return;
    }
    pellet_erase();
    pel.x--;
    break;
  }
  pellet_moved();
}

/* ------------------------------------------------------------ hazards and death */
static void hazards(void) { /* C01B: doors crush, a fizzler grid takes the cube */
  u8 t = tile_at(px, py);
  if (t >= T_DOOR && t < T_FIZZ) {
    dead = 0xFF;
    return;
  }
  t = tile_at(px + 4, py + 9);
  if (t == T_FIZZ) {
    if (holding) cube_reset();
    return;
  }
  if (t >= T_DOOR && t < T_FIZZ) dead = 0xFF;
}

static u8 ctr_d, ctr_e;
static void particle(int dx, int dy) { /* DCC2 */
  u8 d = ctr_d + dx, e = ctr_e + dy;
  if (!empty_tile(tile_at(d, e)) || !empty_tile(tile_at(d, e + 1))) return;
  spr(dead_t < 10 ? SP(0xE1EF) : SP(0xE1F7), d, e);
}
static const s8 BURST[][2] = {
  /* DB8A */ {0, 0}, {0, -2}, {0, 2}, {-1, 0}, {1, 0},
  /* DBA5 */ {0, -4}, {0, 4}, {-2, 0}, {2, 0}, {1, -2}, {1, 2}, {-1, -2}, {-1, 2},
  /* DC03 */ {-1, 4}, {-1, -4}, {1, -4}, {1, 4}, {0, 6}, {0, -6}, {-3, 0}, {3, 0},
  /* DC4F */ {-2, -2}, {-2, 2}, {2, -2}, {2, 2},
  /* DC79 */ {-4, 0}, {4, 0}, {0, -8}, {0, 8}, {2, 4}, {-2, 4}, {2, -4}, {-2, -4},
};
static void death_anim(void) { /* D978 */
  static const u8 upto[5] = {5, 13, 21, 25, 33};
  if (dead_t >= 20) return;
  ctr_e = py + 4, ctr_d = px + 2;
  int stage = (dead_t % 10) >> 1;
  /* DBA5 starts with the DB8A pattern again: burst sizes are cumulative */
  for (int i = 0; i < upto[stage]; i++) particle(BURST[i][0], BURST[i][1]);
}

static void pellet_reset(void) {
  pellet_erase();
  has_pellet = 0xFF;
  pel.x = pel.x0, pel.y = pel.y0, pel.dir = pel_dir0;
  if (pel.dcol != 0xFF) set_tile(pel.dcol, pel.drow, T_DOOR);
}

static void reset_state(void) { /* ACC8 */
  anim_frame = up = fall = spr_ = spl = 0;
  c_up = c_fall = c_r = c_l = 0;
  swapped = faceleft = anim_sub = holding = cube_timer = 0;
  pel.on = pel.t = pel.frame = 0;
  dead = dead_t = 0;
  next_orange = 0;
  o_y = b_y = 0xFF;
  pellet_live = 0xFF;
  walking = 0xFF;
  pspr = SP(0xE23F);
}

static void respawn(void) { /* D9E2 */
  erase_openings();
  px = startx, py = starty;
  dead = dead_t = 0;
  clear_portals();
  pellet_live = 0xFF;
  if (has_pellet) pellet_reset();
  if (has_cube) cube_reset();
  for (int i = 0; i < 300; i++)
    if (map[i] == T_RECV_ON) set_tile(i % 20, i / 20, T_RECV_OFF);
  reset_state();
}

/* ------------------------------------------------------------ level loading */
/* LD3AC: 300 tiles, run-length coded, with objects mixed in. Returns the
 * data after the map. */
static const u8 *parse_map(const u8 *p) {
  int n = 0;
  has_pellet = 0, nfields = 0, nbuttons = 0, has_cube = 0;
  pel.dcol = 0xFF;
  while (n < 300) {
    u8 b = *p;
    int col = n % 20, row = n / 20;
    if (b >= 0x9B) { /* a run */
      u8 t = 0xFF - b, cnt = p[1];
      p += 2;
      while (cnt--) {
        col = n % 20, row = n / 20;
        if (t == 0x14 || (t >= 0x21 && t <= 0x23)) goto field_run;
        map[n++] = t;
        continue;
      field_run:
        if (nfields < 28) {
          field_t *f = &fields[nfields++];
          f->kind = t == 0x14 || t == 0x21;
          f->x = col * 8 + (t == 0x21 || t == 0x23 ? 4 : 0);
          f->y = row * 16 + (t == 0x14 || t == 0x22 ? 8 : 0);
          f->vert = t == 0x21 ? 1 : t == 0x23 ? 0xFF : 0;
          f->pad = 0;
        }
        map[n++] = t == 0x14 || t == 0x21 ? T_FIZZ : T_EMPTY;
      }
      continue;
    }
    p++;
    if (b >= 0x37 && b <= 0x39) map[n++] = b - 0x1B;
    else if (b == 0x24) { /* door opened by the pellet */
      pel.dcol = col, pel.drow = row, pel.on = 0;
      map[n++] = T_DOOR;
    } else if (b == 0x1B) { /* start */
      px = startx = col * 8;
      py = starty = row * 16 + 6;
      map[n++] = T_EMPTY;
    } else if (b == 0x1C) { /* cube dropper */
      if (!has_cube) has_cube = 0xFF, drop_x = col, drop_y = row;
      map[n++] = T_DROP;
    } else if (b >= 0x18 && b <= 0x1A) { /* pellet launcher */
      if (!has_pellet) {
        has_pellet = 0xFF;
        u8 x = col * 8, y = row * 16;
        if (b == 0x18) x += 3, y += 16;
        else if (b == 0x19) x -= 2, y += 6;
        else x += 8, y += 6;
        pel.x0 = pel.x = x, pel.y0 = pel.y = y;
        pel.on = pel.t = pel.frame = 0;
      }
      pel_dir0 = pel.dir = b == 0x18 ? 0 : b == 0x1A ? 1 : 3;
      map[n++] = b;
    } else if (b == 0x14 || (b >= 0x21 && b <= 0x23)) {
      if (nfields < 28) {
        field_t *f = &fields[nfields++];
        f->kind = b == 0x14 || b == 0x21;
        f->x = col * 8 + (b == 0x21 || b == 0x23 ? 4 : 0);
        f->y = row * 16 + (b == 0x14 || b == 0x22 ? 8 : 0);
        f->vert = b == 0x21 ? 1 : b == 0x23 ? 0xFF : 0;
        f->pad = 0;
      }
      map[n++] = b == 0x14 || b == 0x21 ? T_FIZZ : T_EMPTY;
    } else if (b >= 0x2A) { /* button; 0x34 and 0x36 hold two */
      int count = b == 0x34 || b == 0x36 ? 2 : 1;
      for (int k = 0; k < count; k++)
        if (nbuttons < 9) btn_pos[nbuttons][0] = col * 8, btn_pos[nbuttons][1] = row * 16 - 2, nbuttons++;
      map[n++] = b & 1 ? 4 : 2;
    } else if (b >= 0x25) map[n++] = T_DOOR;
    else map[n++] = b;
  }
  return p;
}

/* AB52..ACC4: the level after the text: map, buttons, cube, pellet */
static void load_level(const u8 *p) {
  p = parse_map(p);
  for (int i = 0; i < nbuttons; i++) {
    button_t *b = &buttons[i];
    b->flag = 0, b->x = btn_pos[i][0], b->y = btn_pos[i][1], b->on = 0;
    b->dcol = p[0], b->drow = p[1], b->t = b->frame = 0;
    p += 2;
  }
  if (has_cube) cx = drop_x * 8 + 3, cy = drop_y * 16 + 14;
  pellet_vanish = 0;
  if (has_pellet && *p != 0xAF) pellet_vanish = 0xFF, p++;
}

/* ------------------------------------------------------------ the frame */
/* ADEA: landing on spikes: they turn red and the player dies. The original
 * then carries on with a value left over from drawing the tile: twice
 * the last pixel's color index. */
static u8 spike(void) {
  set_tile(last_col, last_row, T_BLOODY);
  return (tile_sprite(T_BLOODY)[2 + 127] & 15) << 1;
}

static void ground(void) { /* AD88..AE9F */
  u8 t;
  if (up >= 4) goto grounded;
  walking = 0xFF;
  if (st_rwall == 0xFF || st_lwall == 0xFF) {
    if (off != 3) goto airborne;
    goto grounded;
  }
  if (st_floor == 0xFE) {
    walking = 0;
    if (!fall) fall = 3;
    goto friction_air;
  }
  if (st_floor == 0xFF) {
    walking = 0;
    goto friction_air;
  }
  t = tile_at(px, py + 10);
  if (t == T_SPIKE || t == T_BLOODY) t = dead = spike();
  if (t != T_FIZZ && t != T_EMPTY) goto grounded;
  t = tile_at(px + 4, py + 10);
  if (t != T_FIZZ) {
    if (t == T_SPIKE || t == T_BLOODY) t = dead = spike();
    if (t != T_EMPTY) goto grounded;
  }
airborne:
  walking = 0;
  if (fall != 0x3C) fall++;
  goto friction_air;
grounded:
  fall = 0;
  if (spl) spl--;
  if (spr_) spr_--;
friction_air:
  if (walking) return;
  if (friction != 2) friction++;
  else {
    if (spl) spl--;
    if (spr_) spr_--;
    friction = 0;
  }
}

static void redraw_level(void);

static void frame(void) {
  if (cube_timer != 0x14 && ++cube_timer == 0x14 && has_cube) set_tile(drop_x, drop_y, T_DROP);
  ground();
  hazards();
  pellet_step();
  buttons_step();
  fields_step();
  draw_hud();
  check_portals();
  if (k_fly) {
    if (fly_ok) up = 0x0C; /* after the ending: fly */
  } else if (k_grab) grab_latch = 0;
  else grab();
  if (dead) {
    dead_t++;
    if (dead_t == 0x1D) {
      respawn();
      return;
    }
    death_anim();
    holding = 0;
  } else {
    if (k_arrows & 8) jump();
    if (k_arrows & 4) press_right();
    if (k_arrows & 2) press_left();
    vertical();
    if (left_game) return;
    horizontal();
    if (left_game) return;
  }
  if (has_cube && !holding) {
    cube_portals();
    cube_gravity();
    cube_vertical();
    cube_horizontal();
  }
  if (dead || depth || st_ceil == 0xFF) return;
  draw_player();
}

/* ------------------------------------------------------------ screens */
static void floor_row(void) { /* D8B3: a floor under the text */
  for (int c = 0; c < 20; c++) tile_draw(3, c, 14);
}
static void clear_top(void) { fill(0, 0, 320, 224, screen_bg); }

/* Typed out like the original (about 22 ms a letter; Alpha, OK or EXE
 * speed it up). Returns false if Back was pressed. */
static bool type_lines(const u8 **pp, int n) {
  const u8 *p = *pp;
  bool fast = false;
  for (int i = 0; i < n; i++) {
    int x = 6, y = 0x12 + 0x12 * i;
    while (*p) {
#if NP_TEXT_EXTRA /* (a letter can be a few bytes) */
      const char *q = (const char *)p;
      x = letter(&q, x, y, true);
      p = (const u8 *)q;
#else
      glyph(*p++, x, y);
      x += 5;
#endif
      if (!fast) {
        uint64_t until = eadk_timing_millis() + 22;
        while (eadk_timing_millis() < until) {
          scan_keys();
          if (key_down(eadk_key_alpha) || key_down(eadk_key_ok) || key_down(eadk_key_exe)) fast = true;
          if (key_down(eadk_key_back)) return false;
          eadk_timing_msleep(1);
        }
      }
    }
    p++;
  }
  *pp = p;
  flush_keys();
  return true;
}

/* waits for OK / EXE (true) or Back (false) */
static bool wait_continue(void) {
  for (;;) {
    int k = wait_key();
    if (k == eadk_key_ok || k == eadk_key_exe) return true;
    if (k == eadk_key_back) return false;
  }
}

static void scroll_in(void) { /* DDC0 */
#ifdef PERF
  uint32_t s0 = (uint32_t)eadk_timing_millis();
#endif
  for (int c = 0; c < 20; c++) {
    uint64_t t0 = eadk_timing_millis();
    /* The original moves the screen 16 px left and draws column c at the
     * right edge. What is left of the new columns is the blank screen and
     * the floor row, which a 16 px shift does not change, so drawing the
     * new columns in their shifted places gives the same picture without
     * reading the screen back. */
    for (int j = 0; j <= c; j++)
      for (int r = 0; r < 15; r++) tile_draw(map[r * 20 + j], 19 - c + j, r);
#ifdef PERF
    number((uint32_t)eadk_timing_millis() - t0, 1, 30);
#endif
    while (eadk_timing_millis() - t0 < 30) eadk_timing_msleep(1); /* 30 ms a column, as on the TI */
    scan_keys(); /* Home still quits */
  }
#ifdef PERF
  number((uint32_t)eadk_timing_millis() - s0, 1, 40);
#endif
}

static void redraw_level(void) { /* after the pause menu */
  for (int i = 0; i < 300; i++) tile_draw(map[i], i % 20, i / 20);
  draw_buttons();
  for (int i = 0; i < nfields; i++)
    draw_with(fields[i].vert ? SP(0xE72F) : SP(0xE71D), fields[i].x, fields[i].y, C_FIELD, fields[i].kind ? pal[C_FIZZ] : pal[C_ELEC]);
  if (has_cube && !holding) cube_draw();
  if (has_pellet && pellet_live) pellet_draw();
  draw_hud();
  draw_player();
}

static const char *const ENDING[] = {
  T("HELLO?"), T("ARE YOU STILL THERE?"), T("WHERE DID YOU GO?"), "", T("I GUESS YOU MADE IT THROUGH."),
  T("FINE, IF YOU WANT TO LEAVE,"), T("I GUESS I GET ALL THIS"), T("CAKE TO MYSELF."), "", T("GOODBYE."),
  T("PRESS SHIFT TO FLY")};

static void ending(void) { /* D876 */
  clear_top();
  floor_row();
  static u8 buf[400];
  u8 *o = buf;
  int n = 0;
  for (unsigned i = 0; i < sizeof ENDING / sizeof *ENDING; i++, n++) {
    for (const char *c = ENDING[i]; *c; c++) *o++ = *c;
    *o++ = 0;
  }
  const u8 *p = buf;
  type_lines(&p, n);
  wait_continue();
}

/* ------------------------------------------------------------ playing */
static u8 k_back;

void read_keys(void) {
  scan_keys();
  k_arrows = (key_down(eadk_key_down) ? 1 : 0) | (key_down(eadk_key_left) ? 2 : 0) | (key_down(eadk_key_right) ? 4 : 0) |
             (key_down(eadk_key_up) ? 8 : 0);
  grp5 = (key_down(eadk_key_three) ? 2 : 0) | (key_down(eadk_key_six) ? 4 : 0) | (key_down(eadk_key_nine) ? 8 : 0);
  grp4 = (key_down(eadk_key_two) ? 2 : 0) | (key_down(eadk_key_five) ? 4 : 0) | (key_down(eadk_key_eight) ? 8 : 0);
  grp3 = (key_down(eadk_key_one) ? 2 : 0) | (key_down(eadk_key_four) ? 4 : 0) | (key_down(eadk_key_seven) ? 8 : 0);
  k_grab = key_down(eadk_key_ok) || key_down(eadk_key_exe);
  k_fly = key_down(eadk_key_shift);
  static bool back_was = true; /* Back opens the menu when pressed, not while held */
  bool b = key_down(eadk_key_back);
  k_back = b && !back_was;
  back_was = b;
#ifdef CHEAT /* testing: 0 jumps next to the exit */
  if (key_down(eadk_key_zero)) left_game = true;
#endif
}

static void level_exit(void) { left_game = true; }

int top_tile(int col) { return map[col]; }

/* ------------------------------------------------------------ hints */
static void restore_top(void) { /* what the sign covered */
  for (int i = 0; i < 20; i++) tile_draw(map[i], i, 0);
  draw_buttons();
  if (has_cube && !holding && cy < 16) cube_draw();
  if (has_pellet && pellet_live && pel.y < 16) pellet_draw();
  draw_portals();
  if (!dead && !depth) draw_player();
}

static void level_hints(void) {
  hint(HINT_MOVE), hint(HINT_FIRE); /* the first chamber played */
  if (has_cube) hint(HINT_CUBE);
  if (nbuttons) hint(HINT_BUTTON);
  bool fizz = false, zap = false, spikes = false;
  for (int i = 0; i < nfields; i++) fields[i].kind ? (fizz = true) : (zap = true);
  for (int i = 0; i < 300; i++) spikes |= map[i] == T_SPIKE;
  if (fizz) hint(HINT_FIZZLER);
  if (zap) hint(HINT_FIELD);
  if (spikes) hint(HINT_SPIKES);
  if (has_pellet) hint(HINT_PELLET);
}

/* Plays from level n; returns when the player goes back to the title. */
void play(int pack, int n) {
  cur_pack = pack;
  fly_ok = pack == 0 && (save.flags & 1);
  for (;;) {
    cur_level = n;
    level_started(pack, n);
    const u8 *p = level_ptr(pack, n);
    jumpv = p[1];
    int lines = p[2];
    p += 3;
    clear_top();
    floor_row();
    if (lines) {
      if (!type_lines(&p, lines)) return;
      if (!wait_continue()) return;
      clear_top();
    }
    load_level(p);
    scroll_in();
    reset_state();
    left_game = false;
    level_hints();
    /* the original's pace: about 21 ms a frame, a bit more with many fields */
    uint32_t period = 2110 + 10 * nfields;
    uint64_t next = eadk_timing_millis() * 100; /* 64 bits: never wraps */
    bool restart = false;
    while (!left_game) {
      uint64_t now;
      while ((now = eadk_timing_millis() * 100) < next) {
        scan_keys(); /* Home still quits */
        eadk_timing_msleep(1);
      }
      next += period;
      if (now > next + 10000) next = now; /* after a pause, don't rush */
      read_keys();
      if (k_back) {
        hint_hide();
        int r = pause_menu();
        if (r == 1) return;       /* quit to the title */
        if (r == 2) {             /* restart the level */
          restart = true;
          break;
        }
        redraw_level();
        hint_show();
        next = eadk_timing_millis() * 100;
        continue;
      }
#ifdef PERF
      uint32_t t0 = (uint32_t)eadk_timing_millis();
#endif
      frame();
      if (!left_game) hint_tick(restore_top);
#ifdef PERF
      static uint32_t worst;
      uint32_t dt = (uint32_t)eadk_timing_millis() - t0;
      if (dt > worst) worst = dt;
      number(worst, 1, 1);
      number(dt, 1, 11);
#endif
    }
    hints_clear();
    if (restart) continue;
    /* level complete */
    level_done(pack, n);
    if (n >= pack_levels(pack)) {
      if (pack == 0) {
        ending();
        fly_ok = true;
      }
      return;
    }
    n++;
  }
}

#ifdef PORTAL_TEST
/* For test/host.c: run chambers frame by frame against recorded inputs. */
void test_load(int pack, int n) {
  save.scheme = 1;
  apply_scheme();
  cur_pack = pack, cur_level = n;
  const u8 *p = level_ptr(pack, n);
  jumpv = p[1];
  int lines = p[2];
  p += 3;
  while (lines--) {
    while (*p) p++;
    p++;
  }
  load_level(p);
  for (int i = 0; i < 300; i++) tile_draw(map[i], i % 20, i / 20);
  reset_state();
  left_game = false;
}
/* keys: bits 0-3 arrows (down left right up), 4 grab, 5 fly, 8.. digits 1-9 */
int test_frame(u32 keys) {
  k_arrows = keys & 15;
  k_grab = keys >> 4 & 1;
  k_fly = keys >> 5 & 1;
  if (keys >> 6 & 1) px = 0x9A, spr_ = 1;
  u8 d[10];
  for (int i = 1; i <= 9; i++) d[i] = keys >> (7 + i) & 1;
  grp5 = d[3] << 1 | d[6] << 2 | d[9] << 3;
  grp4 = d[2] << 1 | d[5] << 2 | d[8] << 3;
  grp3 = d[1] << 1 | d[4] << 2 | d[7] << 3;
  frame();
  return left_game;
}
void test_state(u8 *o) {
  u8 v[] = {px, py, up, fall, spl, spr_, dead, dead_t, o_y, o_x, b_y, b_x, cx, cy, holding, depth, pel.x, pel.y, next_orange, walking};
  for (unsigned i = 0; i < sizeof v; i++) o[i] = v[i];
}
#endif
