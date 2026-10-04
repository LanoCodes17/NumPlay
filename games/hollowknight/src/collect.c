/*
 * A mask shard or a vessel fragment got (Heart Piece Instant, Vessel Fragment Instant: the Knight's clip, the shake),
 * then its UI (Heart Container UI, Vessel Fragment UI): the room darkened, the fleur, the piece fading in and landing,
 * the pieces fused into a new mask (healed first) or a new vessel, the room back, the Knight's clip to its end.
 * The UI follows the camera in front of the room, behind the HUD. (Their particles are left out.)
 */
#include <math.h>
#include "game.h"

#pragma GCC optimize("Os")

#define DT 0.02f
#define HUD_PX (FOCAL / (-1.342f - CAM_Z))
#define BLANK_A 0.5176f   /* (Darken: the Blanker's alpha) */

enum {
  CO_NONE, CO_GET, CO_DARKEN, CO_PIECE_IN, CO_GET_ANIM, CO_PARTICLES, CO_FUSE, CO_TWEEN, CO_GOT, CO_HEAL, CO_MAX_UP,
  CO_FADE, CO_HERO_END
};

static const float piece_at[2][4] = COLLECT_PIECE, fleur_at[4] = COLLECT_FLEUR, mover_at[4] = COLLECT_MOVER,
                   glow_at[4] = COLLECT_GLOW, mover_to[2] = COLLECT_MOVER_TO;

static struct {
  uint8_t st, kind, pieces;    /* kind: 0 a mask shard, 1 a vessel fragment; pieces: how many now (1..4, 1..3) */
  bool piece_on, fleur_on, mover_on, glow_on;
  float t;                     /* in the state */
  float blank_a, piece_a, mover_a, fleur_a;
  float tw;                    /* Tween Mover: 0..1 */
  Anim piece, fleur, mover, glow;
} co;

bool collect_active(void) { return co.st != CO_NONE; }
void collect_reset(void) { memset(&co, 0, sizeof co); }

static void to(int st) { co.st = (uint8_t)st, co.t = 0; }

/* EaseColor, linear: from a to b over time after delay */
static float ease(float a, float b, float time, float delay) {
  float u = (co.t - delay) / time;
  return u <= 0 ? a : u >= 1 ? b : a + (b - a) * u;
}

/* (Get: the Knight's clip, no gravity, the shake; half a second, then the UI) */
void collect_start(int kind) {
  memset(&co, 0, sizeof co);
  co.kind = (uint8_t)kind;
  pd_set_flag(kind ? PDF_VESSEL_FRAGMENT_COLLECTED : PDF_HEART_PIECE_COLLECTED, true);
  hero_relinquish_control();
  hero_stop_anim_control();
  anim_play(&g_hero.anim, CLIP_KNIGHT_COLLECT_HEART_PIECE);
  g_hero.body.gravity_scale = 0;
  cam_shake(SHAKE_AVERAGE);
  g_pd.disable_pause = true;
  to(CO_GET);
}

/* (Fleur: its clip, and the piece as it was before this one) */
static void fleur(void) {
  co.fleur_on = true;
  anim_play(&co.fleur, CLIP_HEARTUI_FLEUR_APPEAR);
  int n = co.pieces;
  static const int16_t before[2][4] = {{CLIP_HEARTUI_GET_0, CLIP_HEARTUI_GOT_1, CLIP_HEARTUI_GOT_2, CLIP_HEARTUI_GOT_3},
                                       {CLIP_VESSELUI_GET_0, CLIP_VESSELUI_GOT_1, CLIP_VESSELUI_GOT_2, CLIP_VESSELUI_GOT_2}};
  anim_play(&co.piece, before[co.kind][n - 1 < 3 ? n - 1 : 3]);
  co.piece_on = true;
  /* (Piece Fade: from nothing over a second; a vessel's first, Piece Ready: there at once) */
  co.piece_a = co.kind && n == 1 ? 1 : 0;
  to(CO_PIECE_IN);
}

static void get_anim(void) {
  static const int16_t get[2][4] = {{CLIP_HEARTUI_GET_1, CLIP_HEARTUI_GET_2, CLIP_HEARTUI_GET_3, CLIP_HEARTUI_GET_4},
                                    {CLIP_VESSELUI_GET_1, CLIP_VESSELUI_GET_2, CLIP_VESSELUI_GET_3, CLIP_VESSELUI_GET_3}};
  int n = co.pieces;
  anim_play(&co.piece, get[co.kind][n - 1 < 3 ? n - 1 : 3]);
  co.piece.events = 0;
  to(CO_GET_ANIM);
}

void collect_tick(void) {
  if (co.st == CO_NONE) return;
  co.t += DT;
  co.piece.events = co.glow.events = 0;
  anim_update(&co.piece, DT);
  anim_update(&co.fleur, DT);
  anim_update(&co.mover, DT);
  anim_update(&co.glow, DT);
  if (co.glow_on && (co.glow.events & ANIM_DONE)) co.glow_on = false;   /* (destroy_after_anim) */
  bool heart = co.kind == 0;
  switch (co.st) {
    case CO_GET:
      if (co.t >= 0.5f) {
        /* (UI: one more piece; the UI made, its Darken) */
        if (heart) co.pieces = ++g_pd.heart_pieces;
        else co.pieces = ++g_pd.vessel_fragments;
        anim_play(&co.mover, CLIP_HEARTUI_HEAD_MOVE);   /* (the mover's, playing as it is made, hidden) */
        to(CO_DARKEN);
      }
      break;
    case CO_DARKEN:
      co.blank_a = ease(0, BLANK_A, 0.5f, 0);
      co.fleur_a = 1;
      if (co.t >= 0.5f) fleur();
      break;
    case CO_PIECE_IN:
      if (!(co.kind && co.pieces == 1)) co.piece_a = ease(0, 1, 1, 0);
      if (co.t >= (co.kind && co.pieces == 1 ? 0.8f : 1.8f)) get_anim();
      break;
    case CO_GET_ANIM: {
      /* (a mask's: at its end; a vessel's first: at its trigger; the others: at either) */
      uint8_t ev = co.piece.events | (co.piece.playing ? 0 : ANIM_DONE);
      bool go = heart ? (ev & ANIM_DONE) : co.pieces == 1 ? (ev & ANIM_TRIGGER) : (ev & (ANIM_DONE | ANIM_TRIGGER));
      if (go) {
        /* (Piece Get Particles: the glow, the shake) */
        co.glow_on = true;
        anim_play_from_frame(&co.glow, CLIP_SHADE_DEATH_GLOW, 0);
        cam_shake(SHAKE_ENEMY_KILL);
        to(CO_PARTICLES);
      }
      break;
    }
    case CO_PARTICLES:
      if (co.t >= (heart ? 1 : 1.5f)) {
        /* (Check Max) */
        if (co.pieces >= (heart ? 4 : 3)) {
          /* (Fuse: none left over; the fleur goes; a mask's mover comes) */
          if (heart) {
            g_pd.heart_pieces = 0;
            anim_play(&co.piece, CLIP_HEARTUI_FUSE);
            co.mover_on = true;
          } else
            g_pd.vessel_fragments = 0;
          anim_play(&co.fleur, CLIP_HEARTUI_FLEUR_DISAPPEAR);
          to(CO_FUSE);
        } else
          to(CO_FADE);
      }
      break;
    case CO_FUSE:
      if (heart) co.mover_a = ease(0, 0.4275f, 0.75f, 0);
      else co.piece_a = ease(1, 0, 0.5f, 0);
      if (co.t >= (heart ? 0.75f : 1)) to(CO_TWEEN);
      break;
    case CO_TWEEN:
      /* (Tween Mover: the mover and the piece to the new mask's place, 0.75 s, ease in back; a vessel's: its trail's) */
      if (heart) {
        co.piece_a = ease(1, 0, 0.5f, 0);
        co.mover_a = ease(0.5255f, 1, 0.5f, 0);
      }
      co.tw = co.t / 0.75f > 1 ? 1 : co.t / 0.75f;
      if (co.t >= 0.75f) {
        /* (Get: the shake; the piece and the mover gone) */
        cam_shake(SHAKE_ENEMY_KILL);
        co.piece_on = co.mover_on = false;
        to(CO_GOT);
      }
      break;
    case CO_GOT:
      if (co.t >= (heart ? 0.25f : 0.25f + DT)) {
        if (heart) to(CO_HEAL);
        else {
          /* (PD Max Up: a vessel more; Max Up: NEW SOUL ORB) */
          g_pd.mp_reserve_max = (int16_t)(g_pd.mp_reserve_max + 33);
          to(CO_MAX_UP);
        }
      }
      break;
    case CO_HEAL:
      /* (Full Health?, then Heal 1 HP: a mask, 0.15 s, again) */
      if (co.t == DT || co.t >= 0.15f) {
        if (g_pd.health >= g_pd.max_health) {
          /* (Max Up: AddToMaxHealth, all masks full; MAX HP UP) */
          g_pd.max_health++;
          g_pd.health = g_pd.max_health;
          to(CO_MAX_UP);
        } else
          g_pd.health++, co.t = DT;
      }
      break;
    case CO_MAX_UP:
      if (co.t >= 1) to(CO_FADE);
      break;
    case CO_FADE:
      /* (Fade: the piece, the fleur and the room's darkness, after half a second, over half a second) */
      co.piece_a = co.fleur_a = ease(1, 0, 0.5f, 0.5f);
      co.blank_a = ease(BLANK_A, 0, 0.5f, 0.5f);
      if (co.t >= 1) {
        co.piece_on = co.fleur_on = false;
        anim_play(&g_hero.anim, CLIP_KNIGHT_COLLECT_HEART_PIECE_END);
        g_hero.anim.events = 0;
        to(CO_HERO_END);
      }
      break;
    case CO_HERO_END:
      if ((g_hero.anim.events & ANIM_DONE) || !anim_is_playing(&g_hero.anim, CLIP_KNIGHT_COLLECT_HEART_PIECE_END)) {
        /* (Destroy Self: gravity back, control back) */
        g_hero.body.gravity_scale = 0.79f;
        hero_regain_control();
        hero_start_anim_control();
        g_pd.disable_pause = false;
        co.st = CO_NONE;
      }
      break;
  }
}

/* a piece at (x, y, z) from the camera, at scale k: on the HUD as big as in front of the room */
static void piece(const Anim *an, float x, float y, float z, float k, float a, int slot) {
  uint8_t al = (uint8_t)(a * 255 + 0.5f);
  if (!al || an->sprite < 0) return;
  float r = FOCAL / (z - CAM_Z) / HUD_PX;
  Inst in;
  sprite_inst(an->sprite, x * r, y * r, 0, k * r, k * r, gfx_dyn_tint(slot, 255, 255, 255, al), &in);
  gfx_hud(&in, 0);
}

void collect_draw(void) {
  if (co.st <= CO_GET) return;
  /* the Blanker (in front of the room, the Knight too) */
  uint8_t bl = (uint8_t)(co.blank_a * 255 + 0.5f);
  if (bl) {
    float hw = VIEW_W / 2 / HUD_PX + 1, hh = VIEW_H / 2 / HUD_PX + 1;
    gfx_hud_fill(-hw, -hh, hw, hh, gfx_dyn_tint(2, 0, 0, 0, bl));
  }
  if (co.fleur_on) piece(&co.fleur, fleur_at[0], fleur_at[1], fleur_at[2], fleur_at[3], co.fleur_a, 3);
  if (co.piece_on) {
    const float *p = piece_at[co.kind];
    float x = p[0], y = p[1];
    if (co.st == CO_TWEEN && co.kind == 0) {
      float q = co.tw, s = 1.70158f, e = q * q * ((s + 1) * q - s);   /* (easeInBack) */
      x += (mover_to[0] - x) * e, y += (mover_to[1] - y) * e;
    }
    piece(&co.piece, x, y, p[2], p[3], co.piece_a, 4);
  }
  if (co.mover_on) {
    float x = mover_at[0], y = mover_at[1], k = mover_at[3];
    if (co.st == CO_TWEEN) {
      float q = co.tw, s = 1.70158f, e = q * q * ((s + 1) * q - s);
      x += (mover_to[0] - x) * e, y += (mover_to[1] - y) * e, k += (1 - k) * e;
    }
    piece(&co.mover, x, y, mover_at[2], k, co.mover_a, 5);
  }
  if (co.glow_on) piece(&co.glow, glow_at[0], glow_at[1], glow_at[2], glow_at[3], 1, 6);
}
