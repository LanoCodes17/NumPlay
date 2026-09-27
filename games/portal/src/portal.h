/* Portal Returns for the NumWorks calculator.
 *
 * A port of the TI-84 Plus CE game by MateoConLechuga: same test chambers,
 * tiles, sprites and physics. Positions keep the original's units: x in
 * 2 pixel steps (0 to 159), y in pixels, so every movement and collision
 * lands on the same pixels as on the TI. */
#ifndef PORTAL_H
#define PORTAL_H
#include <eadk.h>
#include <stdbool.h>
#include <stdint.h>

typedef uint8_t u8;
typedef int8_t s8;
typedef uint16_t u16;
typedef uint32_t u32;

/* ------------------------------------------------------------ gfx.c */
void assets_init(void); /* unpacks the art and levels */
/* palette slots (the original's color table) */
enum {
  C_TEXT, C_PLAYER, C_BG, C_EDGE, C_FIZZ, C_T5, C_ORANGE, C_T7,
  C_T8, C_T9, C_GLASS, C_FIELD, C_BLUE, C_DRAW, C_ORANGE2, C_ELEC
};
extern u16 pal[16];
extern u16 text_fg, text_bg, screen_bg; /* text colors, screen fill */
void spr(const u8 *s, int x2, int y);       /* 4 bpp sprite, x in 2 px units */
void spr1(const u8 *s, int x2, int y);      /* 1 bpp picture, text colors */
void tile_draw(int t, int col, int row);
void fill(int x, int y, int w, int h, u16 c);
void cls(void);                             /* whole screen in the text background */
int text(const char *s, int x2, int y);     /* returns the x after the text */
void glyph(char c, int x2, int y);
int number(int v, int x2, int y);
void sign_clip(int x0, int x1);             /* keep drawing off the hint sign (x0 == x1: off) */
const u8 *tile_sprite(int t);

/* ------------------------------------------------------------ input */
bool key_down(int k);   /* from the last scan */
void scan_keys(void);   /* reads the keyboard, quits on Home */
int wait_key(void);     /* next key press (event), quits on Home */
void flush_keys(void);  /* forget pending key presses */

/* ------------------------------------------------------------ game.c */
typedef struct {
  const u8 *data;
  int size;
} pack_t;
extern const pack_t PACKS[2];
int pack_levels(int pack);
const u8 *level_ptr(int pack, int n);
/* play from level `n` of `pack`; returns when the player leaves */
void play(int pack, int n);

/* ------------------------------------------------------------ main.c */
typedef struct {
  u8 magic, version;
  u8 unlocked[2];   /* highest level reachable in each pack */
  u8 level[2];      /* level shown in the selector */
  u8 done[2][6];    /* completed levels, 1 bit each */
  u8 scheme;        /* color scheme 0..3 */
  u8 hints;         /* hints already shown, 1 bit each */
  u8 pack;          /* last pack played */
  u8 flags;         /* bit 0: game finished (flying unlocked), bit 1: spikes hint seen */
  u8 sum;
} save_t;
extern save_t save;
void save_write(void);
void apply_scheme(void);
void quit_now(void);
enum { HINT_MOVE, HINT_FIRE, HINT_SWAP, HINT_CUBE, HINT_BUTTON, HINT_FIZZLER, HINT_FIELD, HINT_PELLET, HINT_SPIKES };
void hint(int h);                 /* queue a hint (shown once per save) */
bool hint_tick(void (*restore)(void)); /* true while a hint covers the top */
void hint_hide(void);             /* before the pause menu */
void hint_show(void);             /* after the chamber is redrawn */
void hints_clear(void);
int top_tile(int col);            /* the chamber's tile in row 0 */
int pause_menu(void);             /* 0 go on, 1 quit, 2 restart */
void level_started(int pack, int n);
void level_done(int pack, int n);

#endif
