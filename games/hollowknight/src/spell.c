/* The Knight's Spell Control FSM: holding the cast key focuses soul into health; a tap casts a spell. */
#include "game.h"

#define DT 0.02f
#define BUTTON_DOWN_TIME 0.25f
#define FOCUS_START_TIME 0.25f
#define TIME_PER_MP_DRAIN 0.027f   /* (Time Per MP Drain UnCH: no charm) */
#define GRACE_TIME 0.45f
#define MP_COST 33

enum {
  SP_INACTIVE, SP_BUTTON_DOWN, SP_CANCEL, SP_FOCUS_START, SP_FOCUS, SP_FOCUS_HEAL, SP_FOCUS_GET_FINISH, SP_FOCUS_CANCEL,
  SP_BACK_IN
};

static struct {
  uint8_t state;
  float t, grace;
  bool pressed_up, pressed_down, refocusing, didnt_hit_full, can_cancel;
  int start_mp;
  /* (HeroController's MP drain) */
  bool drain;
  float drain_timer;
  int drained;
} sp;

static bool cast_held(void) { return g_hero.keys & K_SPELL; }
static bool cast_pressed(void) { return (g_hero.keys & K_SPELL) && !(g_hero.prev_keys & K_SPELL); }

void spell_reset(void) { memset(&sp, 0, sizeof sp); }

/* StartMPDrain, StopMPDrain; the drain itself is HeroController.Update's */
static void drain_start(void) { sp.drain = true, sp.drain_timer = 0, sp.drained = 0; }

static void focus_end_common(void) {
  sp.drain = false;
  g_hero.cs.focusing = false;
}

static void regain_control(void) {
  /* Regain Control, then Back In?: holding still (and not stopped at full health) goes on focusing */
  g_hero.cs.focusing = false;
  hero_regain_control();
  hero_start_anim_control();
  sp.can_cancel = false;
  sp.state = SP_BACK_IN;
}

static void can_focus(void) {
  /* Can Focus? */
  if (g_pd.mp < MP_COST || !hero_can_focus()) {
    sp.didnt_hit_full = false;
    sp.state = SP_CANCEL, sp.t = DT;   /* (Cancel: the next frame) */
    return;
  }
  /* Start Slug Anim, Focus Start */
  sp.can_cancel = true;
  sp.grace = 0, sp.didnt_hit_full = true, sp.refocusing = false;
  g_hero.cs.focusing = true;
  hero_relinquish_control();
  hero_stop_anim_control();
  g_hero.body.vx = 0;
  anim_play(&g_hero.anim, CLIP_KNIGHT_FOCUS);
  sp.state = SP_FOCUS_START, sp.t = FOCUS_START_TIME;
}

static void focus_cancel(void) {
  focus_end_common();
  anim_play_from_frame(&g_hero.anim, CLIP_KNIGHT_FOCUS_END, 0);
  sp.state = SP_FOCUS_CANCEL;
}

static void focus_enter(void) {
  sp.start_mp = g_pd.mp;
  sp.state = SP_FOCUS;
  drain_start();
}

/* Grace Check: let go soon after starting (or after a heal), the soul is given back */
static void grace_check(void) {
  float limit = sp.refocusing ? GRACE_TIME : 0.2f;
  if (sp.grace <= limit) g_pd.mp = (int16_t)sp.start_mp;
  focus_cancel();
}

void spell_update(void) {
  Hero *h = &g_hero;
  /* HeroController.Update's MP drain */
  if (sp.drain) {
    sp.drain_timer += DT;
    while (sp.drain_timer >= TIME_PER_MP_DRAIN) {
      sp.drained++;
      sp.drain_timer -= TIME_PER_MP_DRAIN;
      if (g_pd.mp > 0) g_pd.mp--;
      if (sp.drained == MP_COST && sp.state == SP_FOCUS) {
        /* FOCUS COMPLETED: Spore Cloud, Set HP Amount, Focus Heal */
        sp.drain = false;
        sp.grace = 0, sp.refocusing = true;
        anim_play_from_frame(&h->anim, CLIP_KNIGHT_FOCUS_GET, 0);
        cam_shake(SHAKE_ENEMY_KILL);
        hero_add_health(1);
        sp.state = SP_FOCUS_HEAL, sp.t = 0.2f;
      }
    }
  }
  switch (sp.state) {
    case SP_INACTIVE:
      sp.can_cancel = false;
      if (cast_pressed()) {
        sp.state = SP_BUTTON_DOWN, sp.t = BUTTON_DOWN_TIME;
        sp.pressed_up = sp.pressed_down = false;
      } else if (cast_held())
        can_focus();   /* (ALREADY DOWN) */
      break;
    case SP_BUTTON_DOWN:
      if (h->keys & K_UP) sp.pressed_up = true;
      if (h->keys & K_DOWN) sp.pressed_down = true;
      if (!cast_held()) {
        /* Can Cast?, Spell Choice: no spells yet, back to Inactive */
        sp.state = SP_INACTIVE;
        spell_cast(sp.pressed_up, sp.pressed_down);
      } else if ((sp.t -= DT) <= 0)
        can_focus();
      break;
    case SP_CANCEL:
      if ((sp.t -= DT) <= 0) sp.state = SP_INACTIVE;
      break;
    case SP_FOCUS_START:
      h->body.vx = 0;
      if (!cast_held() || !h->cs.on_ground) focus_cancel();
      else if ((sp.t -= DT) <= 0) focus_enter();
      break;
    case SP_FOCUS:
      h->body.vx = 0;
      sp.grace += DT;
      if (!cast_held() || !h->cs.on_ground) {
        focus_end_common();
        grace_check();
      }
      break;
    case SP_FOCUS_HEAL:
      h->body.vx = 0;
      if ((sp.t -= DT) <= 0) {
        /* Full HP? */
        if (g_pd.health >= g_pd.max_health || g_pd.mp < MP_COST) {
          sp.didnt_hit_full = false;
          focus_end_common();
          anim_play_from_frame(&h->anim, CLIP_KNIGHT_FOCUS_GET_ONCE, 0);
          sp.state = SP_FOCUS_GET_FINISH, sp.t = 0.23f;
        } else
          focus_enter();
      }
      break;
    case SP_FOCUS_GET_FINISH:
      if ((sp.t -= DT) <= 0) regain_control();
      break;
    case SP_FOCUS_CANCEL:
      if (h->anim.events & ANIM_DONE || !h->anim.playing) regain_control();
      break;
    case SP_BACK_IN:
      sp.state = SP_INACTIVE;
      if (cast_held() && sp.didnt_hit_full) can_focus();
      break;
  }
}

/* the FSM's global events: the Knight hit (Cancel All), leaving the room (Cancel Some) */
void spell_cancel(void) {
  if (sp.state == SP_INACTIVE) return;
  bool focusing = sp.state >= SP_FOCUS_START && sp.state <= SP_FOCUS_CANCEL;
  sp.drain = false;
  sp.didnt_hit_full = false;
  g_hero.cs.focusing = false;
  sp.state = SP_INACTIVE;
  if (focusing) hero_start_anim_control();
}

bool spell_busy(void) { return sp.state != SP_INACTIVE && sp.state != SP_BUTTON_DOWN; }

/* Can Cast?, Spell Choice: up a scream, down a quake, else a fireball (the ones the Knight has) */
void spell_cast(bool up, bool down) {
  (void)up, (void)down;
  if (g_pd.mp < MP_COST || !hero_can_cast()) return;
  if (g_pd.fireball_level > 0 && !up && !down) {
    /* (Vengeful Spirit: fireball.c) */
  }
}
