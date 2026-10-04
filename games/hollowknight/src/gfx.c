/* Drawing a room: its sprites front to back, a strip of rows at a time. Each pixel keeps the light gathered so far
 * and how much still gets through from behind, so what is hidden is never fetched nor drawn. What is behind the
 * blur plane is drawn small beforehand (as the game's background camera does) and shows through at the end.
 * Then the strip is dithered to the screen's 16 bits. */
#include <math.h>
#include <stdint.h>
#include "hk.h"
#ifdef HOST
#include <stdio.h>
#include <stdlib.h>
#endif

#define STRIP_H 4
#ifndef MAX_ITEMS
#define MAX_ITEMS 512
#endif
#ifndef NPAL
#define NPAL 104
#endif
#define NPX (STRIP_H * VIEW_W)

float g_cam_x, g_cam_y;
uint32_t g_gfx_items, g_gfx_pixels, g_gfx_dropped;
#ifdef HOST
uint32_t g_peak_pals, g_peak_arena, g_peak_soft;
#endif

/* the strip: light gathered (x 128, room above for lights that add): red << 16 | blue, green; and the light still
 * getting through (255 .. 0) */
static uint32_t arb[NPX];
static uint16_t ag[NPX];
static uint8_t trans[NPX] __attribute__((aligned(4)));
static uint32_t cov[STRIP_H][VIEW_W / 32];   /* pixels with no light left from behind */
static bool strip_ready;   /* the strip buffers are clear (finish_strip leaves them so) */

/* an instance in front of the blur plane, seen this frame */
typedef struct {
  Inst in;
  uint16_t x0, x1;   /* on screen, clipped */
  uint8_t y0, y1;
  uint8_t pal;
  int8_t cty;        /* the tile row whose slots the arena holds */
  uint8_t clip;      /* (HUD: 1 + its clip circle) */
  uint16_t ar;       /* its tiles (a row of them, or all if turned), in the arena */
} Item;

/* the HUD's clip circles */
typedef struct {
  float cx, cy, r;   /* screen pixels */
  float x0, y0, x1, y1;
  bool box;          /* (else a circle) */
} HudClip;
#define MAX_HUD_CLIPS 4
static HudClip hud_clips[MAX_HUD_CLIPS];
#ifndef ARENA
#define ARENA 2048
#endif
#define UNRES_SLOT 0xFFFE
#define EMPTY_SLOT 0xFFFF
static uint16_t arena[ARENA];
static int arena_top;
static Item items[MAX_ITEMS];
static int nitems;

/* ---------------------------------------------------------------- palettes: a texture's colors for a tint, graded */
typedef struct {
  uint32_t key;
  uint16_t frame;
  uint8_t solid;    /* the tint is opaque: a tile of opaque texels hides what is behind */
  uint32_t c[16];   /* premultiplied 0xAARRGGBB; [0] transparent */
} Pal;
static Pal pals[NPAL];
static uint16_t pal_frame = 1;
static int pal_hand;

static void soft_reset(void);
static uint8_t lut3[3][256];   /* the room's curves */
static float room_sat;
static int lut_room = -1;

static void make_luts(void) {
  RoomHdr *h = g_room.h;
  for (int c = 0; c < 3; c++)
    for (int i = 0; i < 256; i++) {
      float x = i * 63.0f / 255.0f;
      int a = (int)x;
      if (a > 62) a = 62;
      float t = x - a;
      lut3[c][i] = (uint8_t)(h->lut[c][a] + (h->lut[c][a + 1] - h->lut[c][a]) * t + 0.5f);
    }
  room_sat = h->saturation;
  lut_room = g_room.id;
  soft_reset();
  for (int i = 0; i < NPAL; i++) pals[i].key = 0xFFFFFFFF;
}

static bool grade_off;   /* (the HUD's tints: the room's grading not for them) */
#define HUD_TINT 240      /* (dynamic tints from slot 16 on are the HUD's: 16 the HUD's white, 17 its soul, 18-20 the
                             dialogue's, 21 the area title's, 22-26 the item message's, 27 and 28 the blankers, 29-31
                             the menus'; below, the room's: 0 the Knight's, 1 his light, 2-6 and 10, 11, 15 the
                             enemies', 7 the prompts', 8, 9, 12-14 the scripts' objects') */

static uint32_t grade(float r, float g, float b, float a) {
  /* r g b a: 0..1 straight */
  int ri = (int)(r * 255 + 0.5f), gi = (int)(g * 255 + 0.5f), bi = (int)(b * 255 + 0.5f);
  if (ri > 255) ri = 255;
  if (gi > 255) gi = 255;
  if (bi > 255) bi = 255;
  float R = ri, G = gi, B = bi;
  if (!grade_off) {
    R = lut3[0][ri], G = lut3[1][gi], B = lut3[2][bi];
    float L = 0.2126f * R + 0.7152f * G + 0.0722f * B;
    R = L + (R - L) * room_sat, G = L + (G - L) * room_sat, B = L + (B - L) * room_sat;
  }
  R = R < 0 ? 0 : R > 255 ? 255 : R;
  G = G < 0 ? 0 : G > 255 ? 255 : G;
  B = B < 0 ? 0 : B > 255 ? 255 : B;
  int A = (int)(a * 255 + 0.5f);
  if (A > 255) A = 255;
  return (uint32_t)A << 24 | (uint32_t)(R * a + 0.5f) << 16 | (uint32_t)(G * a + 0.5f) << 8 | (uint32_t)(B * a + 0.5f);
}

uint8_t g_group_alpha[MAX_GROUPS];
uint8_t g_screen_fade;   /* how black the screen is (the camera's fade) */

/* tints: the room's, then colors the game sets (gfx_dyn_tint): a color, and a flash color and amount (SpriteFlash) */
#define DYN_TINT 224
static uint8_t dyn_tints[256 - DYN_TINT][8];
static inline const uint8_t *tint_rgba(int tint) { return tint >= DYN_TINT ? dyn_tints[tint - DYN_TINT] : g_room.tints + 4 * tint; }
static void pals_forget_tint(int tint);

uint8_t gfx_dyn_flash(int slot, uint8_t r, uint8_t g, uint8_t b, uint8_t a, uint8_t fr, uint8_t fg, uint8_t fb, uint8_t amount) {
  uint8_t *t = dyn_tints[slot & 31];
  uint8_t v[8] = {r, g, b, a, fr, fg, fb, amount};
  if (memcmp(t, v, 8)) {
    /* (the colors made with its old values are stale) */
    memcpy(t, v, 8);
    pals_forget_tint(DYN_TINT + (slot & 31));
  }
  return (uint8_t)(DYN_TINT + (slot & 31));
}

uint8_t gfx_dyn_tint(int slot, uint8_t r, uint8_t g, uint8_t b, uint8_t a) { return gfx_dyn_flash(slot, r, g, b, a, 0, 0, 0, 0); }

/* the instance's alpha: its tint's, times its render group's */
static inline uint32_t inst_alpha(const Inst *in) {
  uint32_t a = tint_rgba(in->tint)[3];
  return in->group ? (a * g_group_alpha[in->group & (MAX_GROUPS - 1)] + 127) / 255 : a;
}

/* a palette as tools/pack.py codes it (pal_code): per color, Rice codes of its steps from the one before (green, then
 * red and blue less half the green: SEC_PAL's first bytes say how many low bits each has), then a bit: opaque, else 8
 * bits of alpha */
typedef struct {
  const uint8_t *p;
  uint32_t buf;
  int n;
} Bits;
static uint32_t bits_get(Bits *b, int k) {
  while (b->n < k) b->buf |= (uint32_t)*b->p++ << b->n, b->n += 8;
  uint32_t v = b->buf & ((1u << k) - 1);
  b->buf >>= k, b->n -= k;
  return v;
}
static int bits_step(Bits *b, int k) {
  uint32_t q = 0;
  while (bits_get(b, 1)) q++;
  uint32_t u = q << k | bits_get(b, k);
  return (int)(u >> 1) ^ -(int)(u & 1);
}

static int pal_get(uint16_t tex, uint8_t tint, uint8_t flags, uint32_t alpha) {
  /* (tex << 17: solid colors' TEX_NONE comes out as 0xFFFE....; black textures' keys below those) */
  uint32_t key = (uint32_t)tex << 17 | alpha << 9 | (uint32_t)tint << 1 | ((flags & F_LIT) ? 1 : 0);
  if (tex != TEX_NONE) {
    const TexRec *r = tex_rec(tex);
    if (r->fmt >= FMT_SOFT) return 0;   /* (no colors: soft_get) */
    if (r->fmt == FMT_ALPHA2) key = 0xFFFC0000u | alpha;   /* black: only the alpha matters */
  }
  for (int i = 0; i < NPAL; i++)
    if (pals[i].key == key) {
      pals[i].frame = pal_frame;
      return i;
    }
  int s = -1;
  for (int n = 0; n < NPAL; n++) {
    int i = (pal_hand + n) % NPAL;
    if (pals[i].frame != pal_frame) {
      s = i;
      break;
    }
  }
  if (s < 0) s = pal_hand;
  pal_hand = (s + 1) % NPAL;
  Pal *p = &pals[s];
  p->key = key, p->frame = pal_frame;
  const uint8_t *t = tint_rgba(tint);
  grade_off = tint >= HUD_TINT;
  p->solid = alpha == 255;
  float tr = t[0] / 255.0f, tg = t[1] / 255.0f, tb = t[2] / 255.0f, ta = alpha / 255.0f;
  if (flags & F_LIT) {
    const float *am = g_room.h->ambient;
    tr *= am[0], tg *= am[1], tb *= am[2];
  }
  p->c[0] = 0;
  if (tex == TEX_NONE) {
    p->c[1] = grade(tr, tg, tb, ta);
    return s;
  }
  const TexRec *r = tex_rec(tex);
  if (r->fmt >= FMT_SOFT) return s;
  if (r->fmt == FMT_ALPHA2) {   /* (drawn black whatever the tint) */
    for (int i = 1; i < 4; i++) p->c[i] = (uint32_t)(ta * i / 3.0f * 255 + 0.5f) << 24;
  } else {
    /* (a flash: towards its color by its amount, the alpha kept) */
    float fk = tint >= DYN_TINT ? t[7] / 255.0f : 0, fr = fk ? t[4] / 255.0f * fk : 0, fg = fk ? t[5] / 255.0f * fk : 0,
          fb = fk ? t[6] / 255.0f * fk : 0;
    const uint8_t *ks = section(SEC_PAL);
    Bits bits = {ks + 2 * r->pal, 0, 0};
    int g = 0, ro = 0, bo = 0;
    for (int i = 0; i < 15; i++) {
      /* (red 5 bits, green 6, blue 5) */
      g += bits_step(&bits, ks[0]), ro += bits_step(&bits, ks[1]), bo += bits_step(&bits, ks[2]);
      int a = bits_get(&bits, 1) ? 255 : (int)bits_get(&bits, 8);
      p->c[i + 1] = grade((float)(ro + (g >> 1)) / 31 * tr * (1 - fk) + fr, (float)g / 63 * tg * (1 - fk) + fg,
                          (float)(bo + (g >> 1)) / 31 * tb * (1 - fk) + fb, a / 255.0f * ta);
    }
  }
  return s;
}


/* ---------------------------------------------------------------- smooth textures: a few texels, graded for a tint (those of
 * one color are alpha only, read in place) */
#ifndef SOFT_POOL
#define SOFT_POOL 768
#endif
#ifndef NSOFT
#define NSOFT 16
#endif
static uint32_t soft_pool[SOFT_POOL];
typedef struct {
  uint32_t key;
  uint16_t frame, off, n;
} SoftEnt;
static SoftEnt soft_ent[NSOFT];
static int nsoft, soft_top;

static void pals_forget_tint(int tint) {
  for (int i = 0; i < NPAL; i++)
    if ((pals[i].key >> 1 & 255) == (uint32_t)tint && pals[i].key < 0xFFFC0000u) pals[i].key = 0xFFFFFFFFu;
  for (int i = 0; i < nsoft; i++)
    if ((soft_ent[i].key >> 1 & 255) == (uint32_t)tint) soft_ent[i].key = 0xFFFFFFFFu;
}

static void soft_reset(void) { nsoft = 0, soft_top = 0; }

/* (FMT_SOFTA: its data is its color, then its alpha texels) */
static uint32_t softa_color(const TexRec *r) { return rd32(section(SEC_SOFT) + r->off); }

uint32_t g_soft_misses;
static const uint32_t *soft_get(uint16_t tex, uint8_t tint, uint8_t flags, uint32_t alpha) {
  const TexRec *rr = tex_rec(tex);
  if (rr->fmt == FMT_SOFTA) return (const uint32_t *)(const void *)(section(SEC_SOFT) + rr->off + 4);   /* (alpha, in place) */
  uint32_t key = (uint32_t)tex << 17 | alpha << 9 | (uint32_t)tint << 1 | ((flags & F_LIT) ? 1 : 0);
  for (int i = 0; i < nsoft; i++)
    if (soft_ent[i].key == key) {
      soft_ent[i].frame = pal_frame;
      return soft_pool + soft_ent[i].off;
    }
  const TexRec *r = rr;
  int n = r->w * r->h;   /* (in words) */
  g_soft_misses++;
  if (n > SOFT_POOL) return NULL;
  if (soft_top + n > SOFT_POOL || nsoft == NSOFT) {
    /* keep this frame's, packed */
    int k = 0, top = 0;
    for (int i = 0; i < nsoft; i++) {
      if (soft_ent[i].frame != pal_frame) continue;
      memmove(soft_pool + top, soft_pool + soft_ent[i].off, soft_ent[i].n * 4u);
      soft_ent[k] = soft_ent[i];
      soft_ent[k].off = (uint16_t)top;
      top += soft_ent[k].n, k++;
    }
    nsoft = k, soft_top = top;
    if (soft_top + n > SOFT_POOL || nsoft == NSOFT) return NULL;
  }
  uint32_t *px = soft_pool + soft_top;
  const uint8_t *src = section(SEC_SOFT) + r->off;
  lz_decode(src + 4, rd32(src), (uint8_t *)px, (uint32_t)n * 4);
  const uint8_t *t = tint_rgba(tint);
  float tr = t[0] / 255.0f, tg = t[1] / 255.0f, tb = t[2] / 255.0f, ta = alpha / 255.0f;
  if (flags & F_LIT) {
    const float *am = g_room.h->ambient;
    tr *= am[0], tg *= am[1], tb *= am[2];
  }
  grade_off = tint >= HUD_TINT;
  for (int i = 0; i < n; i++) {
    const uint8_t *c = (const uint8_t *)&px[i];   /* premultiplied r g b a bytes */
    int a = c[3];
    if (!a) {
      px[i] = 0;
      continue;
    }
    float ia = 1.0f / a;
    px[i] = grade(c[0] * ia * tr, c[1] * ia * tg, c[2] * ia * tb, a / 255.0f * ta);
  }
  soft_ent[nsoft++] = (SoftEnt){key, pal_frame, (uint16_t)soft_top, (uint16_t)n};
  soft_top += n;
  return px;
}

/* ---------------------------------------------------------------- an item's mapping from screen to texels */
typedef struct {
  float ox, oy;          /* screen position of texel (0, 0) */
  float iux, iuy, ivx, ivy;   /* u = iux*(x-ox) + iuy*(y-oy), v = ivx*(x-ox) + ivy*(y-oy) */
  float pu, pv;          /* (axis aligned) screen pixels a texel along u and v, signed */
  bool rot;
} Map;

static float inst_k(const Inst *in) { return FOCAL / (in->z * (1.0f / 128) - CAM_Z); }

/* the instance's u and v steps on screen (pixels a texel) */
static void inst_axes(const Inst *in, float k, float *ux, float *uy, float *vx, float *vy) {
  float a = f16(in->a) * k, b = f16(in->b) * k;
  if (in->flags & F_ROT) {
    float t = in->rot * (6.2831853f / 65536.0f), c = cosf(t), s = sinf(t);
    /* world u = a (c, s), v = b (-s, c); screen y is down */
    *ux = a * c, *uy = -a * s, *vx = -b * s, *vy = -b * c;
  } else
    *ux = a, *uy = 0, *vx = 0, *vy = -b;
}

static void inst_map(const Inst *in, Map *m) {
  float k = inst_k(in);
  m->ox = VIEW_W / 2 + (in->ax * (1.0f / 64) - g_cam_x) * k;
  m->oy = VIEW_H / 2 - (in->ay * (1.0f / 64) - g_cam_y) * k;
  float ax, ay, bx, by;
  inst_axes(in, k, &ax, &ay, &bx, &by);
  if (in->flags & F_ROT) {
    /* screen = o + u (ax, ay) + v (bx, by): the inverse */
    float det = ax * by - bx * ay;
    if (det == 0) det = 1e-6f;
    m->iux = by / det, m->iuy = -bx / det, m->ivx = -ay / det, m->ivy = ax / det;
    m->pu = ax, m->pv = by;
    m->rot = true;
  } else {
    m->pu = ax, m->pv = by;
    m->iux = 1 / ax, m->iuy = 0, m->ivx = 0, m->ivy = 1 / by;
    m->rot = false;
  }
}

__attribute__((noinline)) static bool item_box(const Inst *in, Item *it) {
  float k = inst_k(in);
  if (!(k > 0) || k > 400) return false;
  float bx0, by0, bx1, by1;
  if (in->flags & F_SOLID) {
    float ox = VIEW_W / 2 + (in->ax * (1.0f / 64) - g_cam_x) * k, oy = VIEW_H / 2 - (in->ay * (1.0f / 64) - g_cam_y) * k;
    bx0 = ox, by0 = oy, bx1 = ox + f16(in->a) * k, by1 = oy - f16(in->b) * k;
  } else {
    const TexRec *r = tex_rec(in->tex);
    Map m;
    inst_map(in, &m);
    if (in->flags & F_ROT) {
      float ux, uy, vx, vy;
      inst_axes(in, k, &ux, &uy, &vx, &vy);
      ux *= r->w, uy *= r->w, vx *= r->h, vy *= r->h;
      float xs[4] = {m.ox, m.ox + ux, m.ox + vx, m.ox + ux + vx}, ys[4] = {m.oy, m.oy + uy, m.oy + vy, m.oy + uy + vy};
      bx0 = bx1 = xs[0], by0 = by1 = ys[0];
      for (int i = 1; i < 4; i++) {
        if (xs[i] < bx0) bx0 = xs[i];
        if (xs[i] > bx1) bx1 = xs[i];
        if (ys[i] < by0) by0 = ys[i];
        if (ys[i] > by1) by1 = ys[i];
      }
    } else {
      float ex = m.ox + m.pu * r->w, ey = m.oy + m.pv * r->h;
      bx0 = m.ox < ex ? m.ox : ex, bx1 = m.ox < ex ? ex : m.ox;
      by0 = m.oy < ey ? m.oy : ey, by1 = m.oy < ey ? ey : m.oy;
    }
  }
  if (bx1 <= 0 || by1 <= 0 || bx0 >= VIEW_W || by0 >= VIEW_H) return false;
  int a = (int)(bx0 + 0.5f), b = (int)(by0 + 0.5f), c = (int)(bx1 + 0.5f), d = (int)(by1 + 0.5f);
  if (a < 0) a = 0;
  if (b < 0) b = 0;
  if (c > VIEW_W) c = VIEW_W;
  if (d > VIEW_H) d = VIEW_H;
  if (a >= c || b >= d) return false;
  it->x0 = (uint16_t)a, it->y0 = (uint8_t)b, it->x1 = (uint16_t)c, it->y1 = (uint8_t)d;
  return true;
}

/* ---------------------------------------------------------------- back to front blending (the small background) */
static inline uint32_t blend_over(uint32_t d, uint32_t s) {
  uint32_t ia = 256 - (s >> 24);
  uint32_t rb = ((d & 0xFF00FF) * ia >> 8) & 0xFF00FF, g = ((d & 0xFF00) * ia >> 8) & 0xFF00;
  return (s & 0xFFFFFF) + rb + g;
}
static inline uint32_t blend_mode(uint32_t d, uint32_t s, int mode) {
  int dr = d >> 16 & 255, dg = d >> 8 & 255, db = d & 255;
  int sr = s >> 16 & 255, sg = s >> 8 & 255, sb = s & 255, sa = s >> 24;
  int r, g, b;
  switch (mode) {
    case BL_ADD: r = dr + sr, g = dg + sg, b = db + sb; break;
    case BL_SCREEN: r = dr + sr - ((dr * sr * 257 + 257) >> 16), g = dg + sg - ((dg * sg * 257 + 257) >> 16), b = db + sb - ((db * sb * 257 + 257) >> 16); break;
    case BL_LINEARLIGHT: r = dr + 2 * sr - sa, g = dg + 2 * sg - sa, b = db + 2 * sb - sa; break;
    case BL_MULTIPLY: r = dr * (256 - sa + sr) >> 8, g = dg * (256 - sa + sg) >> 8, b = db * (256 - sa + sb) >> 8; break;
    default: return blend_over(d, s);
  }
  r = r < 0 ? 0 : r > 255 ? 255 : r;
  g = g < 0 ? 0 : g > 255 ? 255 : g;
  b = b < 0 ? 0 : b > 255 ? 255 : b;
  return (uint32_t)r << 16 | (uint32_t)g << 8 | (uint32_t)b;
}

/* ---------------------------------------------------------------- front to back: a pixel takes a source's light */
static inline void cover(int row, int x) { cov[row][x >> 5] |= 1u << (x & 31); }
/* [a, b) */
static void cover_run(int row, int a, int b) {
  uint32_t *cv = cov[row];
  while (a < b) {
    int w = a >> 5, n = 32 - (a & 31);
    if (n > b - a) n = b - a;
    cv[w] |= (n == 32 ? 0xFFFFFFFFu : ((1u << n) - 1)) << (a & 31);
    a += n;
  }
}
static inline int code_of(const uint8_t *codes, int tx) { return codes[tx >> 2] >> ((tx & 3) * 2) & 3; }

/* s: premultiplied 0xAARRGGBB; p: the pixel's index in the strip */
#ifdef HOST
uint32_t g_kind_px[6];
#define COUNT_PX(k, n) (g_kind_px[k] += (uint32_t)(n))
#else
#define COUNT_PX(k, n) ((void)0)
#endif
/* (a strip's rows are whole words of cov: pixel p's bit is bit p of it all) */
#define COVER_P(p) (((uint32_t *)(void *)cov)[(p) >> 5] |= 1u << ((p) & 31))
static inline void take_over(int row, int x, int p, uint32_t s) {
  (void)row, (void)x;
  uint32_t t = trans[p], tt = (t + 1) >> 1;
  arb[p] += (s & 0xFF00FF) * tt;
  ag[p] = (uint16_t)(ag[p] + ((s >> 8) & 255) * tt);
  if (s >= 0xFF000000u) {   /* (opaque: nothing left from behind) */
    trans[p] = 0, COVER_P(p);
    return;
  }
  t = (t * (255 - (s >> 24)) * 257) >> 16;
  if (t < 2) t = 0, COVER_P(p);
  trans[p] = (uint8_t)t;
}

#define ACC_TOP (65535 - 255 * 128 - 1024)   /* lights that add stop here: what is behind, and the dither, still fit */
static void take_mode(int row, int x, int p, uint32_t s, int mode) {
  int32_t t = trans[p], tt = (t + 1) >> 1;
  int32_t sr = s >> 16 & 255, sg = s >> 8 & 255, sb = s & 255, sa = s >> 24;
  int32_t r = arb[p] >> 16, g = ag[p], b = arb[p] & 0xFFFF;
  switch (mode) {
    case BL_ADD: r += sr * tt, g += sg * tt, b += sb * tt; break;
    case BL_SCREEN: {
      r += sr * tt, g += sg * tt, b += sb * tt;
      int32_t m = sr > sg ? sr : sg;
      m = m > sb ? m : sb;
      t = (t * (255 - m) * 257) >> 16;
      break;
    }
    case BL_LINEARLIGHT: r += (2 * sr - sa) * tt, g += (2 * sg - sa) * tt, b += (2 * sb - sa) * tt; break;
    case BL_MULTIPLY: t = (t * (255 - sa + (sr + sg + sb) / 3) * 257) >> 16; break;
    default:
      r += sr * tt, g += sg * tt, b += sb * tt;
      t = (t * (255 - sa) * 257) >> 16;
      break;
  }
  r = r < 0 ? 0 : r > ACC_TOP ? ACC_TOP : r;
  g = g < 0 ? 0 : g > ACC_TOP ? ACC_TOP : g;
  b = b < 0 ? 0 : b > ACC_TOP ? ACC_TOP : b;
  arb[p] = (uint32_t)r << 16 | (uint32_t)b;
  ag[p] = (uint16_t)g;
  if (t < 2) t = 0, COVER_P(p);
  trans[p] = (uint8_t)t;
  (void)row, (void)x;
}

#define TAKE(row, x, p, s) (mode ? take_mode(row, x, p, s, mode) : take_over(row, x, p, s))

/* the first pixel in [x, b) with light left (b if none), and the first after it with none */
static inline int open_from(const uint32_t *cv, int x, int b) {
  while (x < b) {
    uint32_t w = ~cv[x >> 5] >> (x & 31);
    if (w) {
      x += __builtin_ctz(w);
      return x < b ? x : b;
    }
    x = (x | 31) + 1;
  }
  return b;
}
static inline int closed_from(const uint32_t *cv, int x, int b) {
  while (x < b) {
    uint32_t w = cv[x >> 5] >> (x & 31);
    if (w) {
      x += __builtin_ctz(w);
      return x < b ? x : b;
    }
    x = (x | 31) + 1;
  }
  return b;
}

/* the pixels i in [0, n) where 0 <= u0 + du i < U and 0 <= v0 + dv i < V (16.16): [*i0, *i1) */
static void clip_lin(int32_t u0, int32_t du, int32_t U, int n, int *i0, int *i1) {
  int a = *i0, b = *i1;
  if (du == 0) {
    if (u0 < 0 || u0 >= U) a = b;
  } else if (du > 0) {
    if (u0 < 0) {
      int k = (-u0 + du - 1) / du;
      if (k > a) a = k;
    }
    int k = u0 >= U ? 0 : (U - 1 - u0) / du + 1;   /* first i with u >= U */
    if (k < b) b = k;
  } else {
    if (u0 >= U) {
      int k = (u0 - U) / -du + 1;
      if (k > a) a = k;
    }
    int k = u0 < 0 ? 0 : u0 / -du + 1;   /* first i with u < 0 */
    if (k < b) b = k;
  }
  (void)n;
  *i0 = a, *i1 = b > a ? b : a;
}

static inline uint32_t rgb_of565(uint32_t c) { return (c >> 11) << 19 | (c >> 5 & 63) << 10 | (c & 31) << 3; }

static inline uint32_t lerp4(uint32_t a, uint32_t b, uint32_t w) {   /* w: 0..256 */
  uint32_t iw = 256 - w;
  uint32_t rb = ((a & 0xFF00FF) * iw + (b & 0xFF00FF) * w) >> 8 & 0xFF00FF;
  uint32_t ag = (((a >> 8) & 0xFF00FF) * iw + ((b >> 8) & 0xFF00FF) * w) & 0xFF00FF00;
  return rb | ag;
}

typedef struct {
  uint16_t tex, tw;
  const TexRec *r;
  uint16_t *slots;   /* tw * th, or NULL */
  const uint32_t *pal;
} BiTex;

static inline const uint8_t *bi_tile(const BiTex *b, int tx, int ty) {
  if (b->slots && !g_tex_overload) {
    uint16_t *e = b->slots + ty * b->tw + tx;
    if (*e == UNRES_SLOT) {
      int q = tex_slot(b->tex, tx, ty);
      *e = q < 0 ? EMPTY_SLOT : (uint16_t)q;
    }
    return *e == EMPTY_SLOT ? NULL : tex_slot_px(*e);
  }
  return tex_tile(b->tex, tx, ty);
}

static inline uint32_t bi_texel(const BiTex *b, int u, int v) {
  if ((unsigned)u >= b->r->w || (unsigned)v >= b->r->h) return 0;
  const uint8_t *t = bi_tile(b, u >> 4, v >> 4);
  if (!t) return 0;
  int ur = u & 15, vr = v & 15;
  return b->pal[t[vr * 8 + (ur >> 1)] >> ((ur & 1) * 4) & 15];
}

/* the four texels around (u, v) (16.16), mixed */
static inline uint32_t bi_sample(const BiTex *b, int32_t u, int32_t v) {
  int c = u >> 16, rr = v >> 16;
  uint32_t fu = (uint32_t)(u >> 8) & 255, fv = (uint32_t)(v >> 8) & 255;
  uint32_t t00, t10, t01, t11;
  if ((c & 15) != 15 && (rr & 15) != 15 && (unsigned)c < (unsigned)(b->r->w - 1) && (unsigned)rr < (unsigned)(b->r->h - 1)) {
    const uint8_t *t = bi_tile(b, c >> 4, rr >> 4);
    if (!t) return 0;
    int ur = c & 15, vr = rr & 15;
    const uint8_t *row = t + vr * 8;
    int i0 = row[ur >> 1] >> ((ur & 1) * 4) & 15, i1 = row[(ur + 1) >> 1] >> (((ur + 1) & 1) * 4) & 15;
    row += 8;
    int i2 = row[ur >> 1] >> ((ur & 1) * 4) & 15, i3 = row[(ur + 1) >> 1] >> (((ur + 1) & 1) * 4) & 15;
    if (i0 == i1 && i0 == i2 && i0 == i3) return b->pal[i0];   /* (the same four: as mixed; clear ones, 0) */
    t00 = b->pal[i0], t10 = b->pal[i1], t01 = b->pal[i2], t11 = b->pal[i3];
  } else {
    t00 = bi_texel(b, c, rr), t10 = bi_texel(b, c + 1, rr), t01 = bi_texel(b, c, rr + 1), t11 = bi_texel(b, c + 1, rr + 1);
  }
  return lerp4(lerp4(t00, t10, fu), lerp4(t01, t11, fu), fv);
}

/* ---------------------------------------------------------------- one item in a strip, on the pixels with light left */
typedef struct {
  int idx, mode, row, sy0;
  const Inst *in;
  const TexRec *r;
  const Map *m;
  const uint32_t *pal;
  Item *it;
} Ctx;

/* a run [a, b) of row y (all pixels with light left), by kind */
static void run_solid(const Ctx *c, int y, int a, int b) {
  int mode = c->mode;
  uint32_t s = c->pal[1];
  for (int x = a; x < b; x++) TAKE(c->row, x, c->row * VIEW_W + x, s);
}

static void run_axis(const Ctx *c, int y, int a, int b, uint16_t *tp, int ty, int vr) {
  const Map *m = c->m;
  const TexRec *r = c->r;
  int mode = c->mode, row = c->row, W = r->w;
  bool a2 = r->fmt == FMT_ALPHA2, solid = pals[c->it->pal].solid;
  const uint8_t *codes = tex_row_codes(c->in->tex, ty);
  int sh = TILE_SHIFT(r->fmt), um = (1 << sh) - 1;
  int32_t du = (int32_t)(65536.0f / m->pu);
  int32_t u = (int32_t)((a + 0.5f - m->ox) * m->iux * 65536.0f);
  int x = a, p = row * VIEW_W + a;
  while (x < b) {
    int ui = u >> 16;
    if ((unsigned)ui >= (unsigned)W) {
      x++, p++, u += du;
      continue;
    }
    int tx = ui >> sh;
    int32_t edge = du > 0 ? ((tx + 1) << (16 + sh)) : ((tx << (16 + sh)) - 1);
    int n = (int)((edge - u) / du) + 1;
    if (n > b - x) n = b - x;
    if (n < 1) n = 1;
    uint16_t sl = tp[tx];
    if (sl == UNRES_SLOT) {
      int q = tex_slot(c->in->tex, tx, ty);
      sl = tp[tx] = q < 0 ? EMPTY_SLOT : (uint16_t)q;
    }
    if (sl == EMPTY_SLOT) {
      x += n, p += n, u += du * n;
      continue;
    }
    const uint8_t *t = tex_slot_px(sl);
    int e = x + n;
    if (solid && !mode && code_of(codes, tx) == 3) {
      /* a tile of opaque texels over everything behind it: nothing is left from behind */
      int x0 = x;
      if (a2) {
        uint32_t s = c->pal[3];
        for (; x < e; x++, p++) {
          uint32_t tt = (trans[p] + 1u) >> 1;
          arb[p] += (s & 0xFF00FF) * tt, ag[p] = (uint16_t)(ag[p] + ((s >> 8) & 255) * tt), trans[p] = 0;
        }
        u += du * (e - x0);
      } else {
        const uint8_t *rw = t + vr * 8;
        for (; x < e; x++, p++, u += du) {
          int ur = (u >> 16) & 15;
          uint32_t s = c->pal[rw[ur >> 1] >> ((ur & 1) << 2) & 15], tt = (trans[p] + 1u) >> 1;
          arb[p] += (s & 0xFF00FF) * tt, ag[p] = (uint16_t)(ag[p] + ((s >> 8) & 255) * tt), trans[p] = 0;
        }
      }
      cover_run(row, x0, e);
      continue;
    }
    if (a2) {
      const uint8_t *rw = t + vr * 8;
      for (; x < e; x++, p++, u += du) {
        int ur = (u >> 16) & um, ci = rw[ur >> 2] >> ((ur & 3) * 2) & 3;
        if (ci) TAKE(row, x, p, c->pal[ci]);
      }
    } else if (!mode) {
      const uint8_t *rw = t + vr * 8;
      for (; x < e; x++, p++, u += du) {
        int ur = (u >> 16) & 15, ci = rw[ur >> 1] >> ((ur & 1) << 2) & 15;
        if (ci) take_over(row, x, p, c->pal[ci]);
      }
    } else {
      const uint8_t *rw = t + vr * 8;
      for (; x < e; x++, p++, u += du) {
        int ur = (u >> 16) & 15, ci = rw[ur >> 1] >> ((ur & 1) << 2) & 15;
        if (ci) take_mode(row, x, p, c->pal[ci], mode);
      }
    }
  }
}

static inline const uint8_t *turned_tile(uint16_t *rtp, uint16_t tex, int tw, int tx, int ty) {
  if (rtp && !g_tex_overload) {
    uint16_t *e = rtp + ty * tw + tx;
    if (*e == UNRES_SLOT) {
      int q = tex_slot(tex, tx, ty);
      *e = q < 0 ? EMPTY_SLOT : (uint16_t)q;
    }
    return *e == EMPTY_SLOT ? NULL : tex_slot_px(*e);
  }
  return tex_tile(tex, tx, ty);
}

typedef struct { uint16_t *rtp; uint16_t tex, tw; } TurnSrc;
/* the tile a key names (out of the loop below) */
__attribute__((noinline)) static const uint8_t *turned_key(const TurnSrc *s, int key) {
  return turned_tile(s->rtp, s->tex, s->tw, key & 255, key >> 8);
}
static void run_turned(const Ctx *c, int y, int a, int b, int *last_key, const uint8_t **last_t) {
  const Map *m = c->m;
  const TexRec *r = c->r;
  int mode = c->mode, row = c->row;
  int sh = TILE_SHIFT(r->fmt), um = (1 << sh) - 1, tw = r->tw;
  uint16_t tex = c->in->tex;
  const uint32_t *pal = c->pal;
  uint16_t *rtp = c->it->ar != 0xFFFF ? arena + c->it->ar : NULL;
  int32_t dux = (int32_t)(m->iux * 65536), dvx = (int32_t)(m->ivx * 65536);
  int32_t W16 = (int32_t)r->w << 16, H16 = (int32_t)r->h << 16;
  float fx = a + 0.5f - m->ox, fy = y + 0.5f - m->oy;
  int32_t u = (int32_t)((m->iux * fx + m->iuy * fy) * 65536), v = (int32_t)((m->ivx * fx + m->ivy * fy) * 65536);
  int i0 = 0, i1 = b - a;
  clip_lin(u, dux, W16, b - a, &i0, &i1);
  clip_lin(v, dvx, H16, b - a, &i0, &i1);
  u += dux * i0, v += dvx * i0;
  int lk = *last_key;
  const uint8_t *lt = *last_t;
  TurnSrc src = {rtp, tex, (uint16_t)tw};
  (void)sh, (void)um;
  /* (few values live in the loop: the pixel's index, its texel's place, the tile) */
#define TURNED(SH, TEXEL, PUT)                                                     \
  for (int p = row * VIEW_W + a + i0, pe = p + i1 - i0; p < pe; p++, u += dux, v += dvx) { \
    int ui = u >> 16, vi = v >> 16, tkey = (vi >> 4) << 8 | (ui >> SH);          \
    if (tkey != lk) lk = tkey, lt = turned_key(&src, tkey);                    \
    if (!lt) continue;                                                         \
    int ur = ui & ((1 << SH) - 1), vr = vi & 15, ci = TEXEL;                   \
    if (ci) PUT;                                                               \
  }
  if (r->fmt == FMT_ALPHA2) {
    if (!mode) TURNED(5, lt[vr * 8 + (ur >> 2)] >> ((ur & 3) * 2) & 3, take_over(0, 0, p, pal[ci]))
    else TURNED(5, lt[vr * 8 + (ur >> 2)] >> ((ur & 3) * 2) & 3, take_mode(0, 0, p, pal[ci], mode))
  } else {
    if (!mode) TURNED(4, lt[vr * 8 + (ur >> 1)] >> ((ur & 1) * 4) & 15, take_over(0, 0, p, pal[ci]))
    else TURNED(4, lt[vr * 8 + (ur >> 1)] >> ((ur & 1) * 4) & 15, take_mode(0, 0, p, pal[ci], mode))
  }
#undef TURNED
  *last_key = lk, *last_t = lt;
}

/* smooth textures: a few texels, interpolated */
static void run_soft(const Ctx *c, int y, int a, int b, const uint32_t *tx, uint32_t crb, uint32_t cg, uint32_t ta) {
  const Map *m = c->m;
  const TexRec *r = c->r;
  int mode = c->mode, row = c->row, W = r->w, H = r->h;
  bool mono = r->fmt == FMT_SOFTA;
  const uint8_t *al = (const uint8_t *)tx;
  float fx = a + 0.5f - m->ox, fy = y + 0.5f - m->oy;
  int32_t u0 = (int32_t)((m->iux * fx + m->iuy * fy - 0.5f) * 65536), v0 = (int32_t)((m->ivx * fx + m->ivy * fy - 0.5f) * 65536);
  int32_t dux = (int32_t)(m->iux * 65536), dvx = (int32_t)(m->ivx * 65536);
  /* where all four texels are inside (the edges of these textures are clear) */
  int i0 = 0, i1 = b - a;
  clip_lin(u0, dux, (W - 1) << 16, b - a, &i0, &i1);
  clip_lin(v0, dvx, (H - 1) << 16, b - a, &i0, &i1);
  if (i0 >= i1) return;
  u0 += dux * i0, v0 += dvx * i0;
  int x = a + i0, e = a + i1, p = row * VIEW_W + a + i0;
  if (mono && dvx == 0) {
    /* upright, one color: the row's two texel rows mixed once per texel column */
    int rr = v0 >> 16;
    uint32_t fv = (uint32_t)(v0 >> 8) & 255;
    const uint8_t *r0 = al + rr * W, *r1 = r0 + W;
    int lc = -1;
    uint32_t c0 = 0, c1 = 0;
    for (; x < e; x++, p++, u0 += dux) {
      int cx = u0 >> 16;
      if (cx != lc) {
        lc = cx;
        c0 = (r0[cx] * (256 - fv) + r1[cx] * fv) * ta >> 8, c1 = (r0[cx + 1] * (256 - fv) + r1[cx + 1] * fv) * ta >> 8;
      }
      uint32_t fu = (uint32_t)(u0 >> 8) & 255, aa = (c0 * (256 - fu) + c1 * fu) >> 16;
      if (!aa) continue;
      uint32_t s = aa << 24 | ((crb * aa >> 8) & 0xFF00FF) | ((cg * aa >> 8) & 0xFF00);
      TAKE(row, x, p, s);
    }
    return;
  }
  for (; x < e; x++, p++, u0 += dux, v0 += dvx) {
    int cx = u0 >> 16, rr = v0 >> 16;
    uint32_t fu = (uint32_t)(u0 >> 8) & 255, fv = (uint32_t)(v0 >> 8) & 255, s;
    if (mono) {
      const uint8_t *p0 = al + rr * W + cx, *p1 = p0 + W;
      uint32_t top = p0[0] * (256 - fu) + p0[1] * fu, bot = p1[0] * (256 - fu) + p1[1] * fu;
      uint32_t aa = ((top * (256 - fv) + bot * fv) >> 16) * ta >> 8;
      if (!aa) continue;
      s = aa << 24 | ((crb * aa >> 8) & 0xFF00FF) | ((cg * aa >> 8) & 0xFF00);
    } else {
      const uint32_t *p0 = tx + rr * W + cx, *p1 = p0 + W;
      s = lerp4(lerp4(p0[0], p0[1], fu), lerp4(p1[0], p1[1], fu), fv);
      if (!(s >> 24)) continue;
    }
    TAKE(row, x, p, s);
  }
}

/* the blurred background's own textures, if drawn in front of the blur plane: interpolated tiles */
static void run_bilinear(const Ctx *c, int y, int a, int b, BiTex *bt) {
  const Map *m = c->m;
  int mode = c->mode, row = c->row;
  float fx = a + 0.5f - m->ox, fy = y + 0.5f - m->oy;
  int32_t u = (int32_t)((m->iux * fx + m->iuy * fy - 0.5f) * 65536) + 65536, v = (int32_t)((m->ivx * fx + m->ivy * fy - 0.5f) * 65536) + 65536;
  int32_t dux = (int32_t)(m->iux * 65536), dvx = (int32_t)(m->ivx * 65536);
  int i0 = 0, i1 = b - a;
  clip_lin(u, dux, (int32_t)(c->r->w + 1) << 16, b - a, &i0, &i1);
  clip_lin(v, dvx, (int32_t)(c->r->h + 1) << 16, b - a, &i0, &i1);
  u += dux * i0 - 65536, v += dvx * i0 - 65536;
  for (int x = a + i0, e = a + i1, p = row * VIEW_W + a + i0; x < e; x++, p++, u += dux, v += dvx) {
    uint32_t s = bi_sample(bt, u, v);
    if (s >> 24) TAKE(row, x, p, s);
  }
}

__attribute__((noinline)) static void draw_item(int idx, Item *it, int sy0, int sy1) {
  const Inst *in = &it->in;
  int ya = it->y0 > sy0 ? it->y0 : sy0, yb = it->y1 < sy1 ? it->y1 : sy1;
  if (ya >= yb) return;
  Ctx c = {idx, in->flags & F_BLEND, 0, sy0, in, NULL, NULL, pals[it->pal].c, it};
  Map m;
  int kind = 0;   /* 0 solid, 1 axis, 2 turned, 3 soft, 4 bilinear */
  const uint32_t *soft = NULL;
  uint32_t crb = 0, cg = 0, ta = 256;
  BiTex bt;
  if (!(in->flags & F_SOLID)) {
    c.r = tex_rec(in->tex);
    inst_map(in, &m);
    c.m = &m;
    if (c.r->fmt >= FMT_SOFT) {
      kind = 3;
      soft = soft_get(in->tex, in->tint, in->flags, inst_alpha(in));
      if (!soft) return;
      if (c.r->fmt == FMT_SOFTA) {
        const uint8_t *t = tint_rgba(in->tint);
        float tr = t[0] / 255.0f, tg = t[1] / 255.0f, tb = t[2] / 255.0f;
        if (in->flags & F_LIT) tr *= g_room.h->ambient[0], tg *= g_room.h->ambient[1], tb *= g_room.h->ambient[2];
        uint32_t base = softa_color(c.r);
        grade_off = in->tint >= HUD_TINT;
        uint32_t col = grade((base & 255) / 255.0f * tr, (base >> 8 & 255) / 255.0f * tg, (base >> 16 & 255) / 255.0f * tb, 1);
        crb = col & 0xFF00FF, cg = col & 0xFF00, ta = inst_alpha(in) + 1u;
      }
    } else if ((c.r->flags & TEX_SMOOTH) && c.r->fmt == FMT_PAL4) {
      kind = 4;
      bt = (BiTex){in->tex, c.r->tw, c.r, NULL, c.pal};
      if (it->ar != 0xFFFF && (in->flags & F_ROT)) bt.slots = arena + it->ar;
    } else
      kind = m.rot ? 2 : 1;
  }
#ifdef HOST
  if (getenv("PROBE")) {
    int px, py;
    sscanf(getenv("PROBE"), "%d,%d", &px, &py);
    if (py >= ya && py < yb && px >= it->x0 && px < it->x1)
      fprintf(stderr, "item %d kind %d tex %u fmt %d blend %d z %.2f trans-before %u\n", idx, kind, in->tex,
              c.r ? c.r->fmt : -1, c.mode, in->z / 128.0, trans[(py - sy0) * VIEW_W + px]);
  }
#endif
  uint16_t local[64], *tp = NULL;
  if (kind == 1) {
    tp = it->ar != 0xFFFF ? arena + it->ar : local;
    if (tp == local) it->cty = -1;
  }
  int last_key = -1;
  const uint8_t *last_t = NULL;
  for (int y = ya; y < yb; y++) {
    c.row = y - sy0;
    const uint32_t *cv = cov[c.row];
    int x = open_from(cv, it->x0, it->x1);
    if (x >= it->x1) continue;
    int ty = 0, vr = 0;
    if (kind == 1) {
      int v = (int)((y + 0.5f - m.oy) * m.ivy);
      if (v < 0 || v >= c.r->h) continue;
      ty = v / TILE, vr = v & (TILE - 1);
      if (ty != it->cty || g_tex_overload) {
        it->cty = (int16_t)ty;
        for (int i = 0; i < c.r->tw; i++) tp[i] = UNRES_SLOT;
      }
    }
    int xe = it->x1;
    if (it->clip) {
      /* (a circle or a box: this row's span of it) */
      const HudClip *k = &hud_clips[it->clip - 1];
      int ca, cb;
      if (k->box) {
        if (y + 0.5f < k->y0 || y + 0.5f >= k->y1) continue;
        ca = (int)(k->x0 + 0.5f), cb = (int)(k->x1 + 0.5f);
      } else {
        float dy = y + 0.5f - k->cy, w2 = k->r * k->r - dy * dy;
        if (w2 <= 0) continue;
        float w = sqrtf(w2);
        ca = (int)(k->cx - w + 0.5f), cb = (int)(k->cx + w + 0.5f);
      }
      if (cb < xe) xe = cb;
      if (ca > x) x = open_from(cv, ca, it->x1);
    }
    while (x < xe) {
      int e = closed_from(cv, x, xe);
      COUNT_PX(kind, e - x);
#ifdef HOST
      if (kind == 3) COUNT_PX(5, (e - x) * (c.r->fmt == FMT_SOFTA ? 1 : 0) * (m.rot ? 1 : 0));
#endif
      switch (kind) {
        case 0: run_solid(&c, y, x, e); break;
        case 1: run_axis(&c, y, x, e, tp, ty, vr); break;
        case 2: run_turned(&c, y, x, e, &last_key, &last_t); break;
        case 3: run_soft(&c, y, x, e, soft, crb, cg, ta); break;
        default: run_bilinear(&c, y, x, e, &bt); break;
      }
      x = open_from(cv, e, xe);
    }
  }
}

/* ---------------------------------------------------------------- the background: what is behind the blur plane,
 * drawn small, blurred and stretched behind the rest (as LightBlurredBackground does) */
#define BG_S 4
#define BG_W (VIEW_W / BG_S)
#define BG_H (VIEW_H / BG_S)
static uint16_t bgbuf[BG_W * BG_H];   /* RGB565 */
static bool bg_on;

static inline void bg_put(uint16_t *d, uint32_t s, int mode) {
  uint32_t dd = rgb_of565(*d);
  dd = mode ? blend_mode(dd, s, mode) : blend_over(dd, s);
  *d = (uint16_t)((dd >> 8 & 0xF800) | (dd >> 5 & 0x07E0) | (dd >> 3 & 0x001F));
}

/* an item behind the blur plane, into the small background (back to front), sampled at the centers of its pixels */
static void draw_bg_item(Item *it) {
  const Inst *in = &it->in;
  int mode = in->flags & F_BLEND;
  const uint32_t *pal = pals[it->pal].c;
  int bx0 = it->x0 / BG_S, by0 = it->y0 / BG_S, bx1 = (it->x1 + BG_S - 1) / BG_S, by1 = (it->y1 + BG_S - 1) / BG_S;
  if (in->flags & F_SOLID) {
    /* the pixels whose centers are in the box */
    int a = (it->x0 + BG_S / 2) / BG_S, b = (it->x1 + BG_S / 2) / BG_S, c = (it->y0 + BG_S / 2) / BG_S, d = (it->y1 + BG_S / 2) / BG_S;
    for (int by = c; by < d; by++)
      for (int bx = a; bx < b; bx++) bg_put(bgbuf + by * BG_W + bx, pal[1], mode);
    return;
  }
  const TexRec *r = tex_rec(in->tex);
  Map m;
  inst_map(in, &m);
  const uint32_t *soft = NULL;
  uint32_t crb = 0, cg = 0, ta = 256;
  if (r->fmt >= FMT_SOFT) {
    soft = soft_get(in->tex, in->tint, in->flags, inst_alpha(in));
    if (!soft) return;
    if (r->fmt == FMT_SOFTA) {
      const uint8_t *t = tint_rgba(in->tint);
      float tr = t[0] / 255.0f, tg = t[1] / 255.0f, tb = t[2] / 255.0f;
      if (in->flags & F_LIT) tr *= g_room.h->ambient[0], tg *= g_room.h->ambient[1], tb *= g_room.h->ambient[2];
      uint32_t base = softa_color(r);
      grade_off = in->tint >= HUD_TINT;
      uint32_t c = grade((base & 255) / 255.0f * tr, (base >> 8 & 255) / 255.0f * tg, (base >> 16 & 255) / 255.0f * tb, 1);
      crb = c & 0xFF00FF, cg = c & 0xFF00, ta = inst_alpha(in) + 1u;
    }
  }
  BiTex bt = {in->tex, r->tw, r, NULL, pal};
  if (it->ar != 0xFFFF && (in->flags & F_ROT)) bt.slots = arena + it->ar;
  int W = r->w, H = r->h;
  /* texel coordinates (16.16) less half a texel: where bilinear samples are centered */
  int32_t du = (int32_t)(m.iux * BG_S * 65536), dv = (int32_t)(m.ivx * BG_S * 65536);
  for (int by = by0; by < by1; by++) {
    uint16_t *d16 = bgbuf + by * BG_W;
    float fx = bx0 * BG_S + BG_S * 0.5f - m.ox, fy = by * BG_S + BG_S * 0.5f - m.oy;
    int32_t u = (int32_t)((m.iux * fx + m.iuy * fy - 0.5f) * 65536), v = (int32_t)((m.ivx * fx + m.ivy * fy - 0.5f) * 65536);
    int i0 = 0, i1 = bx1 - bx0;
    if (r->fmt == FMT_ALPHA2) {
      /* nearest */
      u += 32768, v += 32768;
      clip_lin(u, du, W << 16, i1, &i0, &i1);
      clip_lin(v, dv, H << 16, i1, &i0, &i1);
      u += du * i0, v += dv * i0;
      for (int bx = bx0 + i0; bx < bx0 + i1; bx++, u += du, v += dv) {
        int ui = u >> 16, vi = v >> 16;
        const uint8_t *t = bi_tile(&bt, ui >> 5, vi >> 4);
        if (!t) continue;
        int ur = ui & 31, vr = vi & 15, ci = t[vr * 8 + (ur >> 2)] >> ((ur & 3) * 2) & 3;
        if (ci) bg_put(d16 + bx, pal[ci], mode);
      }
    } else if (r->fmt == FMT_PAL4) {
      /* bilinear: where any of the four texels is inside */
      clip_lin(u + 65536, du, (W + 1) << 16, i1, &i0, &i1);
      clip_lin(v + 65536, dv, (H + 1) << 16, i1, &i0, &i1);
      u += du * i0, v += dv * i0;
      for (int bx = bx0 + i0; bx < bx0 + i1; bx++, u += du, v += dv) {
        uint32_t s = bi_sample(&bt, u, v);
        if (s >> 24) bg_put(d16 + bx, s, mode);
      }
    } else {
      /* smooth: bilinear, the edges repeated */
      u += du * i0, v += dv * i0;
      for (int bx = bx0 + i0; bx < bx0 + i1; bx++, u += du, v += dv) {
        int c = u >> 16, rr = v >> 16;
        uint32_t wu = (uint32_t)(u >> 8) & 255, wv = (uint32_t)(v >> 8) & 255;
        int c0 = c < 0 ? 0 : c >= W ? W - 1 : c, c1 = c + 1 < 0 ? 0 : c + 1 >= W ? W - 1 : c + 1;
        int r0 = rr < 0 ? 0 : rr >= H ? H - 1 : rr, r1 = rr + 1 < 0 ? 0 : rr + 1 >= H ? H - 1 : rr + 1;
        uint32_t s;
        if (r->fmt == FMT_SOFT)
          s = lerp4(lerp4(soft[r0 * W + c0], soft[r0 * W + c1], wu), lerp4(soft[r1 * W + c0], soft[r1 * W + c1], wu), wv);
        else {
          const uint8_t *al = (const uint8_t *)soft;
          uint32_t top = al[r0 * W + c0] * (256 - wu) + al[r0 * W + c1] * wu, bot = al[r1 * W + c0] * (256 - wu) + al[r1 * W + c1] * wu;
          uint32_t aa = ((top * (256 - wv) + bot * wv) >> 16) * ta >> 8;
          s = aa << 24 | ((crb * aa >> 8) & 0xFF00FF) | ((cg * aa >> 8) & 0xFF00);
        }
        if (s >> 24) bg_put(d16 + bx, s, mode);
      }
    }
  }
}

/* the background shows where light is left; then the strip goes to the screen, dithered. The strip's buffers are
 * left cleared for the next one. */
static const uint8_t bayer[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};
static uint32_t bgline[BG_W + 2];   /* a row of the background (0xRRGGBB), its edges repeated */


/* light (x 128) to 5 or 6 bits (dithering added before): one instruction on the calculator */
#if defined(__arm__)
static inline uint32_t to5(uint32_t v) {
  uint32_t r;
  __asm__("usat %0, #5, %1, asr #10" : "=r"(r) : "r"(v));
  return r;
}
static inline uint32_t to6(uint32_t v) {
  uint32_t r;
  __asm__("usat %0, #6, %1, asr #9" : "=r"(r) : "r"(v));
  return r;
}
#else
static inline uint32_t to5(uint32_t v) { return v >= (32u << 10) ? 31u : v >> 10; }
static inline uint32_t to6(uint32_t v) { return v >= (64u << 9) ? 63u : v >> 9; }
#endif

__attribute__((noinline)) static void finish_strip(int sy0, int rows) {
#ifdef HOST
  if (getenv("PROBE")) {
    int px, py;
    sscanf(getenv("PROBE"), "%d,%d", &px, &py);
    if (py >= sy0 && py < sy0 + rows) {
      int p = (py - sy0) * VIEW_W + px;
      fprintf(stderr, "probe %d,%d: trans %u acc r %u g %u b %u bg_on %d bg565 %04x\n", px, py, trans[p], arb[p] >> 16, ag[p], arb[p] & 0xFFFF, bg_on,
              bgbuf[(py / BG_S) * BG_W + px / BG_S]);
    }
  }
#endif
  uint16_t *out = (uint16_t *)(void *)arb;   /* in place: each 16-bit pixel goes where its 32-bit one was read */
  for (int y = 0; y < rows; y++) {
    int sy = sy0 + y;
    if (bg_on) {
      /* the background's row, between two of its own rows */
      float fy = (sy + 0.5f) / BG_S - 0.5f;
      int r0 = (int)(fy + 4096.0f) - 4096;
      uint32_t wv = (uint32_t)((fy - r0) * 256);
      const uint16_t *a16 = bgbuf + (r0 < 0 ? 0 : r0 >= BG_H ? BG_H - 1 : r0) * BG_W;
      const uint16_t *b16 = bgbuf + (r0 + 1 < 0 ? 0 : r0 + 1 >= BG_H ? BG_H - 1 : r0 + 1) * BG_W;
      for (int i = 0; i < BG_W; i++) bgline[i + 1] = lerp4(rgb_of565(a16[i]), rgb_of565(b16[i]), wv);
      bgline[0] = bgline[1], bgline[BG_W + 1] = bgline[BG_W];
    }
    /* the dither of each of 4 columns: red and blue in the high and low halves (both lanes of arb), green */
    const uint8_t *by = bayer[sy & 3];
    uint32_t drb[4], dg[4];
    for (int i = 0; i < 4; i++) drb[i] = (uint32_t)(by[i] >> 1) * 0x800080u, dg[i] = (uint32_t)(by[i] >> 2) << 7;
    int p = y * VIEW_W;
    uint32_t *A = arb + p;
    uint16_t *G16 = ag + p, *O = out + p;
    uint8_t *T = trans + p;
    if (!bg_on) {
      for (int x = 0; x < VIEW_W; x += 4, A += 4, G16 += 4, O += 4, T += 4) {
        for (int j = 0; j < 4; j++) {
          uint32_t rb = A[j] + drb[j], g = G16[j] + dg[j];
          O[j] = (uint16_t)(to5(rb >> 16) << 11 | to6(g) << 5 | to5(rb & 0xFFFF));
        }
        G16[0] = G16[1] = G16[2] = G16[3] = 0;
        *(uint32_t *)(void *)T = 0xFFFFFFFFu;
      }
      continue;
    }
    /* screen pixel 4i + j samples the background at i - 0.375 + j / 4: between texels i - 1 and i, then i and i + 1
     * (bgline is one texel ahead) */
    const uint32_t *bl = bgline;
    for (int x = 0; x < VIEW_W; x += 4, A += 4, G16 += 4, O += 4, T += 4, bl++) {
      uint32_t t4 = *(const uint32_t *)(const void *)T;
      if (!t4) {
        for (int j = 0; j < 4; j++) {
          uint32_t rb = A[j] + drb[j], g = G16[j] + dg[j];
          O[j] = (uint16_t)(to5(rb >> 16) << 11 | to6(g) << 5 | to5(rb & 0xFFFF));
        }
      } else {
        uint32_t bgs[4] = {lerp4(bl[0], bl[1], 160), lerp4(bl[0], bl[1], 224), lerp4(bl[1], bl[2], 32), lerp4(bl[1], bl[2], 96)};
        for (int j = 0; j < 4; j++) {
          uint32_t t = (T[j] + 1u) >> 1, bg = bgs[j];
          /* red and blue in their lanes together */
          uint32_t rb = A[j] + (bg & 0xFF00FF) * t + drb[j], g = G16[j] + (bg >> 8 & 255) * t + dg[j];
          O[j] = (uint16_t)(to5(rb >> 16) << 11 | to6(g) << 5 | to5(rb & 0xFFFF));
        }
      }
      G16[0] = G16[1] = G16[2] = G16[3] = 0;
      *(uint32_t *)(void *)T = 0xFFFFFFFFu;
    }
  }
  plat_push(0, VIEW_Y + sy0, VIEW_W, rows, out);
  /* (arb held the strip's 16-bit pixels) */
  uint32_t *w = arb;
  for (int i = 0; i < NPX; i += 4) w[i] = w[i + 1] = w[i + 2] = w[i + 3] = 0;
}

/* a rotated item's slots (all its tiles) or an upright one's (a row), in the arena: false if it is full */
static bool arena_take(Item *it, bool keep) {
  const Inst *in = &it->in;
  it->ar = 0xFFFF, it->cty = -1;
  if (in->flags & F_SOLID) return true;
  const TexRec *tr = tex_rec(in->tex);
  int n = (in->flags & F_ROT) ? tr->tw * tr->th : tr->tw;
  if (!n || n > 256 || arena_top + n > ARENA) return true;
  it->ar = (uint16_t)arena_top;
  if (keep) arena_top += n;
  if (in->flags & F_ROT)
    for (int q = 0; q < n; q++) arena[it->ar + q] = UNRES_SLOT;
  return true;
}

/* ---------------------------------------------------------------- the frame */
typedef struct {
  Inst in;
  uint32_t group;
} Actor;
#define MAX_ACTORS 64
static Actor actors[MAX_ACTORS];
static int nactors;

/* the HUD: in front of all, its own camera's (not graded with the room, not faded) */
#define MAX_HUD 48
static Inst hud[MAX_HUD];
static uint8_t hud_clip_of[MAX_HUD];
static int nhud;
#define HUD_Z (-1.342f)   /* (where the room's camera sees as much as the HUD's, 8.7107 units half height) */

bool gfx_hud(const Inst *in, int clip) {
  if (nhud >= MAX_HUD) return false;
  hud_clip_of[nhud] = (uint8_t)clip;
  hud[nhud++] = *in;
  return true;
}

void gfx_hud_clip(int clip, float x, float y, float r) {
  /* (HUD units, from the screen's center, to pixels) */
  float k = FOCAL / (HUD_Z - CAM_Z);
  hud_clips[clip - 1] = (HudClip){VIEW_W / 2 + x * k, VIEW_H / 2 - y * k, r * k, 0, 0, 0, 0, false};
}

void gfx_hud_fill(float x0, float y0, float x1, float y1, uint8_t tint) {
  /* (a solid color: its top left, its size) */
  Inst in;
  memset(&in, 0, sizeof in);
  in.ax = (int16_t)lrintf(x0 * 64), in.ay = (int16_t)lrintf(y1 * 64);
  in.tex = TEX_NONE, in.flags = F_SOLID, in.tint = tint;
  in.a = to_f16(x1 - x0), in.b = to_f16(y0 - y1);
  gfx_hud(&in, 0);
}

void gfx_hud_rect(int clip, float x0, float y0, float x1, float y1) {
  float k = FOCAL / (HUD_Z - CAM_Z);
  hud_clips[clip - 1] = (HudClip){0, 0, 0, VIEW_W / 2 + x0 * k, VIEW_H / 2 - y1 * k, VIEW_W / 2 + x1 * k, VIEW_H / 2 - y0 * k, true};
}

/* text: lines of glyphs (4-bit alpha), drawn in front of what is behind them in their layer */
typedef struct {
  const uint8_t *s;
  const uint8_t *style;
  float x;            /* the pen's start (view pixels) */
  int16_t y, y0, y1;  /* the baseline; the rows it can cover */
  uint8_t n, layer;
  uint32_t rgb;       /* (premultiplied by its alpha: 0xAARRGGBB) */
} TextRun;
#define MAX_RUNS 32   /* (a shop's list, its description, its keys) */
static TextRun runs[MAX_RUNS];
static int nruns;

bool gfx_text(int style, float x, float y, const uint8_t *s, int n, uint32_t argb, int layer) {
  uint32_t a = argb >> 24;
  if (nruns >= MAX_RUNS || n <= 0 || !a) return false;
  const uint8_t *st = font_style(style);
  TextRun *r = &runs[nruns++];
  r->s = s, r->style = st, r->x = x, r->n = (uint8_t)(n > 255 ? 255 : n), r->layer = (uint8_t)layer;
  r->y = (int16_t)lrintf(y);
  r->y0 = (int16_t)(r->y - (int)font_asc(st) - 3), r->y1 = (int16_t)(r->y + (int)font_desc(st) + 3);
  uint32_t cr = (argb >> 16 & 255) * a / 255, cg = (argb >> 8 & 255) * a / 255, cb = (argb & 255) * a / 255;
  r->rgb = a << 24 | cr << 16 | cg << 8 | cb;
  return true;
}

static void draw_runs(int layer, int sy0, int sy1) {
  for (int k = 0; k < nruns; k++) {
    const TextRun *r = &runs[k];
    if (r->layer != layer || r->y0 >= sy1 || r->y1 <= sy0) continue;
    uint32_t a0 = r->rgb >> 24, rb0 = r->rgb & 0xFF00FF, g0 = r->rgb >> 8 & 255;
    float pen = r->x;
    for (int i = 0; i < r->n; i++) {
      float fl = floorf(pen);
      int ip = (int)fl, ph = (int)((pen - fl) * FONT_PHASES + 0.5f);
      if (ph >= FONT_PHASES) ph = 0, ip++;
      const Glyph *g = font_glyph(r->style, r->s[i], ph);
      if (!g) continue;
      int gx = ip + g->left, gy = r->y - g->top;
      pen += g->adv * (1.0f / 64);
      if (!g->w || gy >= sy1 || gy + g->h <= sy0 || gx >= VIEW_W || gx + g->w <= 0) continue;
      int stride = (g->w + 1) >> 1;
      const uint8_t *bits = (const uint8_t *)(g + 1);
      int ya = gy < sy0 ? sy0 : gy, yb = gy + g->h > sy1 ? sy1 : gy + g->h;
      int xa = gx < 0 ? 0 : gx, xb = gx + g->w > VIEW_W ? VIEW_W : gx + g->w;
      for (int y = ya; y < yb; y++) {
        const uint8_t *row = bits + (y - gy) * stride;
        int p = (y - sy0) * VIEW_W;
        for (int x = xa; x < xb; x++) {
          int u = x - gx;
          uint32_t v = row[u >> 1] >> ((u & 1) * 4) & 15;
          if (!v) continue;
          uint32_t w = v * 17 * 257 >> 8;   /* (0 .. 256) */
          uint32_t s = (a0 * w >> 8) << 24 | (((rb0 * w) >> 8) & 0xFF00FF) | ((g0 * w >> 8) << 8);
          take_over(y - sy0, x, p + x, s);
        }
      }
    }
  }
}

bool gfx_actor(const Inst *in, uint32_t group) {
  if (nactors >= MAX_ACTORS) return false;
  /* kept in draw order: sorting layer and order, then far first */
  int i = nactors++;
  while (i > 0 && (actors[i - 1].group > group || (actors[i - 1].group == group && actors[i - 1].in.z < in->z)))
    actors[i] = actors[i - 1], i--;
  actors[i].in = *in, actors[i].group = group;
  return true;
}

static bool overlay_on;   /* (the overlay's instances: the room kept back to leave them room, g_gfx_reserve) */
static bool add_item(const Inst *in, float blur_z) {
  Item cur;
  cur.in = *in;
  cur.clip = 0;
  uint32_t alpha = inst_alpha(in);
  if (!alpha) return false;   /* (a hidden group, or a clear tint) */
  if (!item_box(&cur.in, &cur)) return false;
  cur.pal = (uint8_t)pal_get(cur.in.flags & F_SOLID ? TEX_NONE : cur.in.tex, cur.in.tint, cur.in.flags, alpha);
  if (cur.in.z * (1.0f / 128) > blur_z) {
    arena_take(&cur, false);
    if (!bg_on) memset(bgbuf, 0, sizeof bgbuf), bg_on = true;
    draw_bg_item(&cur);
    return false;
  }
  if (nitems >= MAX_ITEMS - (overlay_on ? 0 : g_gfx_reserve)) {
    g_gfx_dropped++;   /* (never in the game's rooms: checked by tests) */
    return false;
  }
  arena_take(&cur, true);
  items[nitems++] = cur;
  return true;
}

bool g_gfx_no_room;
void (*g_gfx_overlay)(void);
int g_gfx_reserve;

/* the overlay's (as the HUD's, straight into the frame's instances: as many as there is room for) */
bool gfx_overlay(const Inst *src) {
  Inst in = *src;
  in.ax = (int16_t)(in.ax + (int)lrintf(g_cam_x * 64)), in.ay = (int16_t)(in.ay + (int)lrintf(g_cam_y * 64));
  in.z = (int16_t)lrintf(HUD_Z * 128);
  return add_item(&in, 1e9f);
}

void gfx_overlay_fill(float x0, float y0, float x1, float y1, uint8_t tint) {
  Inst in;
  memset(&in, 0, sizeof in);
  in.ax = (int16_t)lrintf(x0 * 64), in.ay = (int16_t)lrintf(y1 * 64);
  in.tex = TEX_NONE, in.flags = F_SOLID, in.tint = tint;
  in.a = to_f16(x1 - x0), in.b = to_f16(y0 - y1);
  gfx_overlay(&in);
}

void gfx_frame(void) {
  bool roomless = g_room.h == NULL || g_gfx_no_room;   /* (the title screen: the HUD's alone) */
  if (!roomless && lut_room != g_room.id) make_luts();
  tex_frame();
  pal_frame++;
  if (!pal_frame) pal_frame = 1;
  nitems = 0;
  arena_top = 0;
  bg_on = false;
  g_gfx_dropped = 0;
  float blur_z = roomless ? 1e9f : g_room.h->blur_z;
  /* the instances near the camera, back to front, and the actors among them: what is behind the blur plane is drawn
   * now (small), the rest kept */
  Item cur;
  uint32_t group;
  int na = 0;
  if (!roomless) room_near(g_cam_x, g_cam_y), room_first();
  /* (an actor behind the blur plane but in front of what is kept, by its sorting layer: kept too) */
  while (!roomless && room_next(&cur.in, &group)) {
    while (na < nactors && (actors[na].group < group || (actors[na].group == group && actors[na].in.z > cur.in.z)))
      add_item(&actors[na++].in, nitems ? 1e9f : blur_z);
    add_item(&cur.in, blur_z);
  }
  while (na < nactors) add_item(&actors[na++].in, nitems ? 1e9f : blur_z);
  nactors = 0;
  /* the HUD's, in front (HUD units from the screen's center: where the camera is) */
  int nscene = nitems;
  for (int i = 0; i < nhud; i++) {
    Inst in = hud[i];
    in.ax = (int16_t)(in.ax + (int)lrintf(g_cam_x * 64)), in.ay = (int16_t)(in.ay + (int)lrintf(g_cam_y * 64));
    in.z = (int16_t)lrintf(HUD_Z * 128);
    if (add_item(&in, 1e9f)) items[nitems - 1].clip = hud_clip_of[i];
  }
  nhud = 0;
  if (g_gfx_overlay) overlay_on = true, g_gfx_overlay(), overlay_on = false;
  g_gfx_items = (uint32_t)nitems;
  if (!strip_ready) {
    memset(arb, 0, sizeof arb), memset(ag, 0, sizeof ag), memset(trans, 255, sizeof trans);
    strip_ready = true;
  }
#ifdef HOST
  {
    extern uint32_t g_peak_pals, g_peak_arena, g_peak_soft;
    uint32_t n = 0;
    for (int i = 0; i < NPAL; i++) {
      bool used = false;
      for (int j = 0; j < nitems && !used; j++) used = items[j].pal == i;
      n += used;
    }
    if (n > g_peak_pals) g_peak_pals = n;
    if ((uint32_t)arena_top > g_peak_arena) g_peak_arena = (uint32_t)arena_top;
  }
#endif
  for (int sy0 = 0; sy0 < VIEW_H; sy0 += STRIP_H) {
    int sy1 = sy0 + STRIP_H;
    memset(cov, 0, sizeof cov);
    if (nruns) draw_runs(0, sy0, sy1);
    for (int i = nitems - 1; i >= nscene; i--)
      if (items[i].y0 < sy1 && items[i].y1 > sy0) draw_item(i, &items[i], sy0, sy1);
    if (g_screen_fade) {
      /* (the camera fading to black: a black layer between the HUD and the room) */
      uint32_t k = 255u - g_screen_fade;
      for (int p = 0; p < NPX; p++) {
        uint32_t t = trans[p] * k / 255;
        if (t < 2 && trans[p] >= 2) t = 0, cover(p / VIEW_W, p % VIEW_W);
        trans[p] = (uint8_t)t;
      }
    }
    if (g_screen_fade < 255 && nruns) draw_runs(1, sy0, sy1);
    if (g_screen_fade < 255)
      for (int i = nscene - 1; i >= 0; i--)
        if (items[i].y0 < sy1 && items[i].y1 > sy0) draw_item(i, &items[i], sy0, sy1);
    finish_strip(sy0, STRIP_H);
  }
  nruns = 0;
#ifdef HOST
  {
    extern uint32_t g_peak_soft;
    uint32_t sn = 0;
    for (int i = 0; i < nsoft; i++)
      if (soft_ent[i].frame == pal_frame) sn += soft_ent[i].n;
    if (sn > g_peak_soft) g_peak_soft = sn;
    if (getenv("SOFTDUMP"))
      for (int i = 0; i < nsoft; i++)
        if (soft_ent[i].frame == pal_frame)
          fprintf(stderr, "soft tex %u key %08x words %u (%dx%d fmt %d)\n", soft_ent[i].key >> 16, soft_ent[i].key, soft_ent[i].n,
                  tex_rec((uint16_t)(soft_ent[i].key >> 16))->w, tex_rec((uint16_t)(soft_ent[i].key >> 16))->h, tex_rec((uint16_t)(soft_ent[i].key >> 16))->fmt);
  }
#endif
}
