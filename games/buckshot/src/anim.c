/* The animations of the match: key frames rendered from the original's own
 * animations (the player's hands in the table view, the Dealer's in his
 * view), timed like the original. Every action is already settled in the
 * match state when its animation plays (see game.c). */
#include "scene.h"
#include "story.h"
#include <eadk.h>

static void frame(int ms) {
  table_show();
  pause_ms(ms);
}

/* the player's hands: a picture over the table, the shotgun off the table */
static void p_pose(int img, int ms) {
  T.pose = img;
  T.pose_dy = 0;
  frame(ms);
}

/* ... brought up from below the table first */
static void p_raise(int img, int ms) {
  static const int8_t dy[4] = {72, 36, 14, 4};
  T.pose = img;
  for (int k = 0; k < 4; k++) {
    T.pose_dy = dy[k];
    frame(40);
  }
  T.pose_dy = 0;
  frame(ms);
}

static void p_done(void) {
  T.pose = T.pose2 = -1;
  T.pose_dy = 0;
  T.gun_on_table = 1;
}

/* the Dealer's hands, in his view */
static void d_pose(int img, int ms) {
  T.d.pose = img;
  frame(ms);
}

static void d_done(void) {
  view_dealer();
}

static void careful(void) {
  if (G.hp[0] == 1 && !G.wire[0] && !G.careful_said) {
    G.careful_said = 1;
    view_dealer();
    say(T("CAREFUL, NOW ..."), 2500);
  }
}

/* after a change of charges: look at the display */
static void show_charges(void) {
  health_view(-1, 1400);
  careful();
}

/* the screen goes dark: hit by a live round */
static void player_down(int ended) {
  gfx_light(256, 0, 200);
  gfx_present();
  pause_ms(90);
  gfx_light(0, 0, 0);
  gfx_present();
  pause_ms(ended ? 2400 : 1600);
  if (ended) {
    gfx_light(256, 0, 0);
    return;
  }
  view_dealer();
  table_draw();
  fade_to(0, 256, 700);
  show_charges();
}

/* the Dealer is thrown back into the dark */
static void dealer_down(int ended) {
  view_dealer();
  T.gun_on_table = 0;
  for (int k = 0; k < 2; k++) {
    T.d.alone = IMG_D_FLY0 + k;
    frame(60);
  }
  T.d.alone = -1;
  T.d.gone = 1;
  frame(ended ? 1500 : 1200);
  T.gun_on_table = 1;
  if (ended) return;
  anim_dealer_return();
  show_charges();
}

void anim_dealer_return(void) {
  view_dealer();
  for (int k = 3; k >= 0; k--) {
    T.d.gone = 0;
    T.d.dim = k > 2 ? 3 : k;
    T.d.dy = k * 2;
    frame(k == 3 ? 300 : 200);
  }
  view_dealer();
  frame(400);
}

void anim_dealer_arrives(const char *line) {
  table_reset();
  view_dealer();
  T.d.gone = 1;
  table_draw();
  fade_to(0, 256, 600);
  pause_ms(500);
  anim_dealer_return();
  pause_ms(900);
  if (line) say(line, 2300);
}

/* ----------------------------------------------------------------- items */

static void player_item(int it, const act_t *a) {
  view_table();
  T.gun_on_table = it != IT_SAW && it != IT_GLASS && it != IT_BEER;
  switch (it) {
    case IT_SAW:
      p_raise(IMG_P_SAW0, 600);
      p_pose(IMG_P_SAW1, 1000);
      p_done();
      frame(400); /* the barrel, sawn off */
      break;
    case IT_GLASS:
      p_raise(IMG_P_MAG0, 500);
      p_pose(IMG_P_MAG, 500);
      T.pose2 = a->live ? IMG_P_MAG_L : IMG_P_MAG_B;
      frame(2200);
      p_done();
      frame(200);
      break;
    case IT_BEER:
      T.gun_on_table = 1;
      p_raise(IMG_P_BEER0, 600);
      p_pose(IMG_P_BEER1, 1000);
      T.gun_on_table = 0;
      p_pose(IMG_P_RACK, 350);
      T.pose2 = a->live ? IMG_P_EJECT_L : IMG_P_EJECT_B;
      frame(1000);
      p_done();
      frame(300);
      break;
    case IT_CIGS:
      p_raise(IMG_P_CIG0, 600);
      p_pose(IMG_P_CIG1, 1400);
      p_done();
      show_charges();
      break;
    case IT_CUFFS:
      /* the Dealer holds out his wrists */
      frame(300);
      view_dealer();
      T.d.pose = IMG_D_GETCUFF;
      frame(800);
      T.d.pose = IMG_D_CUFFED;
      frame(900);
      view_table();
      frame(300);
      break;
    case IT_MEDS:
      p_raise(IMG_P_MED0, 600);
      p_pose(IMG_P_MED1, 1000);
      p_done();
      if (a->good) {
        show_charges();
      } else {
        table_draw();
        gfx_light(256, 0, 160);
        gfx_shake(2, 1);
        gfx_present();
        pause_ms(250);
        gfx_shake(0, 0);
        gfx_present();
        pause_ms(500);
        gfx_light(256, 0, 0);
        if (a->dead) {
          player_down(1);
          return;
        }
        show_charges();
      }
      break;
    case IT_PHONE:
      p_raise(IMG_P_PHONE0, 450);
      T.pose = IMG_P_PHONE1;
      say(a->text, 3000);
      p_done();
      frame(200);
      break;
    case IT_INVERT:
      p_raise(IMG_P_INV1, 1300);
      p_done();
      frame(200);
      break;
    case IT_ADREN:
      p_raise(IMG_P_ADR0, 600);
      p_pose(IMG_P_ADR1, 800);
      p_done();
      break;
  }
}

static void dealer_item(int it, const act_t *a) {
  view_dealer();
  switch (it) {
    case IT_SAW:
      T.gun_on_table = 1;
      d_pose(IMG_D_SAW0, 700);
      T.gun_on_table = 0;
      d_pose(IMG_D_SAW1, 500);
      d_pose(IMG_D_SAW2, 700);
      d_done();
      frame(400);
      break;
    case IT_GLASS:
      d_pose(IMG_D_MAG0, 600);
      d_pose(IMG_D_MAG1, 900);
      say(T("VERY INTERESTING ..."), 1800);
      d_done();
      break;
    case IT_BEER:
      d_pose(IMG_D_BEER0, 700);
      d_pose(IMG_D_BEER1, 1000);
      d_done();
      T.pose2 = a->live ? IMG_D_EJECT_L : IMG_D_EJECT_B;
      frame(900);
      T.pose2 = -1;
      frame(300);
      break;
    case IT_CIGS:
      d_pose(IMG_D_CIG0, 800);
      d_pose(IMG_D_CIG1, 1300);
      d_done();
      frame(200);
      show_charges();
      break;
    case IT_CUFFS:
      d_pose(IMG_D_CUFFS0, 600);
      d_pose(IMG_D_CUFFS1, 600);
      d_done();
      /* your wrists */
      view_table();
      frame(200);
      for (int k = 0; k < 4; k++) {
        gfx_shake(0, k & 1 ? 2 : 0);
        frame(120);
      }
      gfx_shake(0, 0);
      frame(400);
      view_dealer();
      break;
    case IT_MEDS:
      d_pose(IMG_D_MED0, 900);
      d_pose(IMG_D_MED1, 1100);
      if (!a->good) {
        if (a->dead) {
          T.d.pose = -1;
          T.d.alone = IMG_D_MEDFALL;
          frame(250);
          T.d.alone = -1;
          T.d.gone = 1;
          frame(1500);
          return;
        }
        gfx_shake(3, 0);
        frame(90);
        gfx_shake(-2, 1);
        frame(90);
        gfx_shake(0, 0);
      }
      d_done();
      frame(300);
      show_charges();
      break;
    case IT_PHONE:
      d_pose(IMG_D_PHONE0, 600);
      d_pose(IMG_D_PHONE1, 1700);
      d_done();
      break;
    case IT_INVERT:
      d_pose(IMG_D_INV0, 700);
      d_pose(IMG_D_INV1, 900);
      d_done();
      break;
    case IT_ADREN:
      d_pose(IMG_D_ADR0, 600);
      d_pose(IMG_D_ADR1, 700);
      d_done();
      break;
  }
  frame(250);
}

void anim_item(int side, int it, const act_t *a) {
  if (side) dealer_item(it, a);
  else player_item(it, a);
}

/* ---------------------------------------------------------------- shotgun */

int anim_player_aim(void) {
  view_table();
  T.gun_on_table = 0;
  T.pose = IMG_P_HOLD;
  T.lbl = 1;
  for (;;) {
    T.hint = T("UP: DEALER   DOWN: YOU   OK: FIRE");
    table_show();
    int k;
    while ((k = idle_key()) < 0) {}
    if (k == KEY_UP) T.lbl = 1;
    else if (k == KEY_DOWN) T.lbl = 2;
    else if (k == KEY_BACK) app_pause_menu();
    else if (IS_OK(k)) break;
  }
  int self = T.lbl == 2;
  T.lbl = 0;
  T.hint = 0;
  return self;
}

void anim_player_shot(int self, int live, int dmg, int ended) {
  (void)dmg;
  if (self) {
    /* the barrel turned to you */
    view_table();
    T.gun_on_table = 0;
    p_pose(dmg == 2 ? IMG_P_AIMS_SAW : IMG_P_AIMS, 1700);
  } else {
    view_dealer();
    T.gun_on_table = 0;
    p_pose(dmg == 2 ? IMG_P_AIMD_SAW : IMG_P_AIMD, 1600);
  }
  if (live) {
    flash(1, 1);
    p_done();
    if (self) player_down(ended);
    else dealer_down(ended);
    return;
  }
  /* click */
  pause_ms(700);
  view_table();
  T.gun_on_table = 0;
  p_pose(IMG_P_RACK, 300);
  T.pose2 = IMG_P_EJECT_B;
  frame(600);
  p_done();
  frame(300);
}

static void zaim_draw(void) { gfx_plate(IMG_PL_ZAIM); }

void anim_dealer_shot(int self, int live, int dmg, int ended) {
  view_dealer();
  T.gun_on_table = 0;
  d_pose(IMG_D_GUN_LIFT, 220);
  d_pose(IMG_D_GUN_LOAD, 800);
  if (self) {
    /* the barrel under his own chin */
    d_pose(IMG_D_GUN_AIMS0, 300);
    d_pose(dmg == 2 ? IMG_D_GUN_AIMS_SAW : IMG_D_GUN_AIMS, 1900);
  } else {
    /* the barrel turned to you, then up close */
    d_pose(IMG_D_GUN_AIMP0, 300);
    d_pose(IMG_D_GUN_AIMP, 500);
    zaim_draw();
    gfx_present();
    redraw = zaim_draw;
    pause_ms(1700);
  }
  if (live) {
    flash(1, 1);
    if (self) {
      dealer_down(ended);
    } else {
      T.d.pose = IMG_D_GUN_LOAD;
      player_down(ended);
    }
    if (!ended) {
      view_dealer();
      frame(300);
    }
    return;
  }
  /* click */
  pause_ms(700);
  d_pose(self ? IMG_D_GUN_RACKS : IMG_D_GUN_RACK, 500);
  T.pose2 = IMG_D_EJECT_B;
  frame(700);
  T.pose2 = -1;
  d_done();
  frame(400);
}

void anim_player_cuffed(int broke) {
  view_table();
  for (int k = 0; k < 4; k++) {
    gfx_shake(0, k & 1 ? 2 : 0);
    frame(160);
  }
  gfx_shake(0, 0);
  frame(broke ? 400 : 200);
}

void anim_dealer_cuffed(int broke) {
  view_dealer();
  frame(500);
  for (int k = 0; k < 4; k++) {
    T.d.dy = k & 1;
    gfx_shake(k & 1 ? 1 : -1, 0);
    frame(170);
  }
  gfx_shake(0, 0);
  T.d.dy = 0;
  if (broke) {
    T.d.pose = IMG_D_GETCUFF;
    frame(500);
    d_done();
  }
  frame(400);
}

void anim_wire_cut(int side, int say_it) {
  view_dealer();
  frame(1200);
  if (say_it) say(T("ARE YOU READY?"), 3000);
  wire_cut(side);
}

void anim_god_waiver(void) {
  T.hint = 0;
  view_table();
  T.pose = IMG_P_GOD;
  frame(2800);
  T.pose = -1;
}

/* ---------------------------------------------------------------- loading */

void anim_dealer_load(int n, int told, int fast) {
  view_dealer();
  frame(300);
  T.gun_on_table = 0;
  d_pose(IMG_D_GUN_LIFT, 220);
  d_pose(IMG_D_GUN_LOAD, 700);
  if (told >= 0) say(told == 0 ? T("I INSERT THE SHELLS\nIN AN UNKNOWN ORDER.") : T("THEY ENTER THE CHAMBER\nIN A HIDDEN SEQUENCE."), 3000);
  for (int i = 0; i < n; i++) {
    d_pose(IMG_D_GUN_LOAD1, fast ? 110 : 200);
    d_pose(IMG_D_GUN_LOAD, fast ? 60 : 120);
  }
}

void anim_dealer_rack(void) {
  d_pose(IMG_D_GUN_RACK, 800);
  d_done();
  frame(400);
}
