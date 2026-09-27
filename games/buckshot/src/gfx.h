/* Buckshot's picture engine: a 320x240 frame buffer of palette indices,
 * pictures streamed into it from compressed art, text, and a palette that
 * fades, flashes and tints as it is pushed to the screen. */
#ifndef BR_GFX_H
#define BR_GFX_H
#include <stdbool.h>
#include <stdint.h>
#include "assets.h"
#include "font.h"

#define GFX_W 320
#define GFX_H 240

extern uint8_t gfx_fb[GFX_W * GFX_H];

void gfx_decode(const uint8_t *src, uint8_t *fb, int x, int y, int w, int h, int opaque, const uint8_t *map, int scale);

void gfx_init(void);
void gfx_clear(uint8_t c);
void gfx_plate(int img);                     /* a whole picture */
int gfx_cached(uint32_t key);                /* the background kept under `key` back in the frame buffer: 1 if there was one */
void gfx_cache_store(uint32_t key);          /* keep the frame buffer as the background for `key` */
void gfx_cache_clear(void);
void gfx_sprite(int img, int dx, int dy);    /* a cut-out, moved by (dx, dy) */
void gfx_sprite_dim(int img, int dx, int dy, int dim); /* drawn darker (dim 0..3) */
void gfx_fill(int x, int y, int w, int h, uint8_t c);
void gfx_darken(int x, int y, int w, int h, int dim);
void gfx_brackets(int x, int y, int w, int h, uint8_t c);
int gfx_text_w(const font_t *f, const char *s);
void gfx_text(const font_t *f, const char *s, int x, int y, uint8_t c);
void gfx_text_c(const font_t *f, const char *s, int cx, int y, uint8_t c); /* centered, multi-line */
void gfx_text_shadow(const font_t *f, const char *s, int cx, int y, uint8_t c);
int gfx_lines(const char *s);

/* Palette effects: light 0 (black) .. 256 (normal); flash 0..256 toward white;
 * red 0..256 toward a blood tint. Takes effect on the next present. */
void gfx_light(int light, int flash, int red);
void gfx_shake(int dx, int dy);
void gfx_present(void);
void gfx_present_rect(int x, int y, int w, int h);
#endif
