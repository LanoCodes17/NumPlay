#include "story.h"
#include <eadk.h>
#include "game.h"
#include "scene.h"

char *story_money(char *out, uint64_t v) {
  char t[24];
  int n = 0;
  do t[n++] = '0' + v % 10; while (v /= 10);
  while (n) *out++ = t[--n];
  *out = 0;
  return out;
}

static char *cat(char *o, const char *s) {
  while (*s) *o++ = *s++;
  *o = 0;
  return o;
}

/* OK goes on; Back is the pause menu, then the screen is drawn again */
static void wait_ok(void (*draw)(void)) {
  for (;;) {
    int k = key_wait(-1);
    if (IS_OK(k)) return;
    if (k == KEY_BACK) {
      redraw = draw;
      app_pause_menu();
    }
  }
}

/* a plate that fades in from black */
static void show_plate(int img, int ms) {
  gfx_plate(img);
  fade_to(0, 256, ms);
}

static void kick_door(void) {
  G.doors += 0;
  static const int8_t sy[5] = {6, -4, 3, -2, 0};
  for (int k = 0; k < 5; k++) {
    gfx_shake(0, sy[k]);
    gfx_present();
    pause_ms(40);
  }
  gfx_shake(0, 0);
  fade_to(256, 0, 500);
}

/* -------------------------------------------------------------- bathroom */

static const int16_t DOOR[4] = {172, 70, 76, 124};

static void bath_draw(int sel, int pills) {
  gfx_plate(IMG_PL_BATH);
  if (pills) gfx_sprite(IMG_B_PILLS, 0, 0);
  if (sel) {
    const img_t *m = &art_img[IMG_B_PILLS];
    gfx_brackets(m->x - 3, m->y - 3, m->w + 6, m->h + 6, C_WHITE);
  } else {
    gfx_brackets(DOOR[0], DOOR[1], DOOR[2], DOOR[3], C_WHITE);
  }
}

static int pills_choice(void) {
  int yes = 1;
  for (;;) {
    bath_draw(1, 1);
    gfx_fill(96, 176, 128, 50, C_BLACK);
    gfx_text_c(&font_small, "CONSUME PILLS?", 160, 182, C_WHITE);
    gfx_text(&font_big, "YES", 112, 202, yes ? C_WHITE : C_GREY);
    gfx_text(&font_big, "NO", 184, 202, yes ? C_GREY : C_WHITE);
    gfx_present();
    int k = key_wait(-1);
    if (k == KEY_LEFT || k == KEY_RIGHT) yes = !yes;
    else if (IS_OK(k)) return yes;
    else if (k == KEY_BACK) return 0;
  }
}

int story_intro(void) {
  int pills = P.don_unlocked, sel = 0;
  gfx_plate(IMG_PL_BATH);
  if (pills) gfx_sprite(IMG_B_PILLS, 0, 0);
  fade_to(0, 256, 900);
  for (;;) {
    bath_draw(sel, pills);
    gfx_text(&font_small, pills ? "LEFT/RIGHT: CHOOSE   OK: INTERACT" : "OK: OPEN THE DOOR", 4, 3, C_GREY);
    gfx_present();
    int k = key_wait(-1);
    if (k == KEY_BACK) return -1;
    if (pills && (k == KEY_LEFT || k == KEY_RIGHT)) sel = !sel;
    if (IS_OK(k)) {
      if (sel == 0) {
        kick_door();
        return MODE_STORY;
      }
      if (pills_choice()) {
        fade_to(256, 0, 700);
        pause_ms(400);
        bath_draw(0, 0);
        fade_to(0, 256, 500);
        pause_ms(500);
        kick_door();
        return MODE_DON;
      }
    }
  }
}

static void hall_draw(void) {
  gfx_plate(IMG_PL_HALL);
  gfx_text(&font_small, "OK: OPEN THE DOOR", 4, 3, C_GREY);
}

static void hallway(void) {
  show_plate(IMG_PL_HALL, 900);
  pause_ms(700);
  hall_draw();
  gfx_present();
  wait_ok(hall_draw);
  kick_door();
}

/* --------------------------------------------------------------- waiver */

static const int16_t KEY_XY[28][2] = {
    {204, 53}, {218, 53}, {205, 66}, {221, 66}, {236, 67}, {206, 79}, {221, 79}, {208, 94}, {224, 94},
    {240, 94}, {256, 95}, {209, 109}, {225, 108}, {241, 108}, {259, 109}, {211, 125}, {229, 126}, {245, 126},
    {262, 125}, {212, 142}, {229, 142}, {246, 142}, {265, 141}, {214, 159}, {232, 159}, {251, 159},
    {247, 78}, /* enter */
    {240, 53}, /* backspace */
};

/* calculator keys for A..Z, as printed on the NumWorks keyboard */
static const uint8_t ALPHA_KEYS[26] = {18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30,
                                       31, 32, 33, 34, 36, 37, 38, 39, 40, 42, 43, 44, 45};

static int key_pos(int i, int *x, int *y) {
  *x = KEY_XY[i][0];
  *y = KEY_XY[i][1];
  return 1;
}

static void waiver_draw(const char *name, int cur, int punched) {
  gfx_plate(IMG_PL_WAIVER);
  /* the machine's little screen */
  if (!punched) {
    gfx_text(&font_small, name, 199, 32, C_DOT);
    int n = 0;
    while (name[n]) n++;
    if (n < 6 && (eadk_timing_millis() / 400) & 1) gfx_fill(199 + gfx_text_w(&font_small, name), 41, 6, 1, C_DOT);
  }
  /* the name, punched on the paper */
  gfx_text(&font_big, punched ? name : "", 110, 172, C_BLACK);
  if (cur >= 0) {
    int x, y;
    key_pos(cur, &x, &y);
    gfx_brackets(x - 9, y - 9, 18, 18, C_WHITE);
  }
}

static int nav_key(int cur, int k) {
  int cx, cy, best = cur, bd = 1 << 30;
  key_pos(cur, &cx, &cy);
  for (int i = 0; i < 28; i++) {
    if (i == cur) continue;
    int x, y;
    key_pos(i, &x, &y);
    int dx = x - cx, dy = y - cy, main, side;
    if (k == KEY_LEFT) main = -dx, side = dy;
    else if (k == KEY_RIGHT) main = dx, side = dy;
    else if (k == KEY_UP) main = -dy, side = dx;
    else main = dy, side = dx;
    if (main <= 3) continue;
    if (side < 0) side = -side;
    int d = main + side * 3;
    if (d < bd) bd = d, best = i;
  }
  return best;
}

static int streq(const char *a, const char *b) {
  while (*a && *a == *b) a++, b++;
  return *a == *b;
}

static void waiver(void) {
  char name[7] = {0};
  int n = 0, cur = 0;
  redraw = 0;
  show_plate(IMG_PL_WAIVER, 600);
  for (;;) {
    waiver_draw(name, cur, 0);
    gfx_text(&font_small, "ARROWS+OK OR LETTER KEYS  EXE: SIGN", 4, 3, C_GREY);
    gfx_present();
    int k = key_wait(400);
    if (k < 0) continue;
    int letter = k >= KEY_LETTER && k < KEY_LETTER + 26 ? k - KEY_LETTER : -1;
    for (int i = 0; i < 26; i++)
      if (ALPHA_KEYS[i] == k) letter = i;
    if (k <= KEY_RIGHT) {
      cur = nav_key(cur, k);
      continue;
    }
    if (k == KEY_OK) {
      if (cur < 26) letter = cur;
      else if (cur == 27) k = KEY_BACKSPACE;
      else k = KEY_EXE;
    }
    if (k == KEY_BACK) app_pause_menu();
    if (letter >= 0 && n < 6) {
      name[n++] = 'A' + letter;
      name[n] = 0;
      cur = letter;
    } else if (k == KEY_BACKSPACE && n) {
      name[--n] = 0;
    } else if (k == KEY_EXE && n && !streq(name, "GOD") && !streq(name, "DEALER")) {
      break;
    }
  }
  for (int i = 0; i < 7; i++) G.name[i] = name[i], P.last_name[i] = name[i];
  /* the letters are punched onto the waiver one at a time */
  char part[7] = {0};
  for (int i = 0; i < n; i++) {
    part[i] = name[i];
    waiver_draw(part, -1, 1);
    gfx_present();
    pause_ms(170);
  }
  pause_ms(700);
  fade_to(256, 0, 400);
}

/* -------------------------------------------------------- into the room */

void story_enter(int mode) {
  (void)mode;
  hallway();
  anim_dealer_arrives("PLEASE SIGN THE WAIVER.");
  waiver();
  table_reset();
  view_dealer();
  table_draw();
  fade_to(0, 256, 500);
  pause_ms(600);
}

static void bath_door_draw(void) {
  gfx_plate(IMG_PL_BATH);
  gfx_brackets(DOOR[0], DOOR[1], DOOR[2], DOOR[3], C_WHITE);
  gfx_text(&font_small, "OK: OPEN THE DOOR", 4, 3, C_GREY);
}

/* the bathroom and the hallway again, the Dealer waiting */
static void back_to_the_table(const char *greeting) {
  show_plate(IMG_PL_BATH, 700);
  pause_ms(500);
  bath_door_draw();
  gfx_present();
  wait_ok(bath_door_draw);
  kick_door();
  hallway();
  anim_dealer_arrives(greeting);
}

static void revive_draw(void) { gfx_plate(IMG_PL_REVIVE); }

void story_revive(void) {
  gfx_clear(C_BLACK);
  gfx_light(256, 0, 0);
  gfx_present();
  pause_ms(1500);
  show_plate(IMG_PL_REVIVE, 1500);
  static char line[24];
  char *o = cat(line, "GET UP, ");
  o = cat(o, G.name);
  cat(o, "!");
  const char *lines[3] = {"YOU'RE LUCKY IT LEFT YOU\nWITH A CHARGE!", line, "THE NIGHT IS YOUNG."};
  redraw = revive_draw;
  for (int i = 0; i < 3; i++) {
    gfx_plate(IMG_PL_REVIVE);
    ui_dialogue(lines[i], 99);
    gfx_present();
    pause_skip(i == 0 ? 3000 : 2400);
  }
  fade_to(256, 0, 800);
  back_to_the_table("WELCOME BACK.");
}

void story_retry(void) {
  gfx_clear(C_BLACK);
  gfx_light(256, 0, 0);
  gfx_present();
  pause_ms(1200);
  back_to_the_table("...");
}

int story_death(void) {
  if (G.mode == MODE_DON) {
    /* no doctor in Double or Nothing: back to the bathroom */
    gfx_clear(C_BLACK);
    gfx_light(256, 0, 0);
    gfx_present();
    pause_ms(2500);
    return 1;
  }
  gfx_clear(C_BLACK);
  gfx_present();
  gfx_light(256, 0, 0);
  pause_ms(1800);
  show_plate(IMG_PL_HEAVEN, 3000);
  pause_ms(2500);
  int retry = 1;
  for (;;) {
    gfx_plate(IMG_PL_HEAVEN);
    gfx_text_shadow(&font_big, "YOU ARE DEAD", 160, 92, C_WHITE);
    gfx_text_shadow(&font_small, "RETRY", 130, 124, retry ? C_WHITE : C_GREY);
    gfx_text_shadow(&font_small, "EXIT", 190, 124, retry ? C_GREY : C_WHITE);
    gfx_present();
    int k = key_wait(-1);
    if (k == KEY_LEFT || k == KEY_RIGHT) retry = !retry;
    else if (IS_OK(k)) break;
    else if (k == KEY_BACK) retry = 0;
  }
  fade_to(256, 0, 800);
  gfx_light(256, 0, 0);
  return retry;
}

/* ------------------------------------------------------ double or nothing */

/* the offer machine's green display */
#define DON_X 158
#define DON_Y 171

static void don_display(const char *s) {
  gfx_plate(IMG_PL_DON);
  int lines = gfx_lines(s), h = lines * (font_dot.h + 3) - 3;
  gfx_text_c(&font_dot, s, DON_X, DON_Y - h / 2, C_DOT);
}

static int don_yes = 1;

static void don_choice_draw(void) {
  don_display("DOUBLE OR\nNOTHING?");
  static const int16_t B[2][4] = {{58, 10, 104, 46}, {168, 10, 104, 46}};
  const int16_t *b = B[!don_yes];
  gfx_brackets(b[0], b[1], b[2], b[3], C_WHITE);
  gfx_text_c(&font_small, "LEFT/RIGHT: CHOOSE   OK: PRESS", 160, 228, C_GREY);
}

int story_double_or_nothing(void) {
  /* the winnings (already settled) count up on the machine's display */
  uint64_t before = G.score_before, after = G.score;
  char s[24];
  story_money(s, before);
  don_display(s);
  fade_to(0, 256, 600);
  for (int t = 0; t <= 40; t++) {
    story_money(s, before + (after - before) * t / 40);
    don_display(s);
    gfx_present();
    if (pause_skip(60)) t = 39;
  }
  pause_ms(1200);
  don_display("DOUBLE OR\nNOTHING?");
  gfx_present();
  pause_ms(1500);
  don_yes = 1;
  redraw = don_choice_draw;
  for (;;) {
    don_choice_draw();
    gfx_present();
    int k = key_wait(-1);
    if (k == KEY_LEFT || k == KEY_RIGHT) don_yes = !don_yes;
    else if (IS_OK(k)) break;
    else if (k == KEY_BACK) app_pause_menu();
  }
  fade_to(256, 0, 500);
  gfx_light(256, 0, 0);
  if (don_yes) {
    table_reset();
    view_dealer();
    table_draw();
    fade_to(0, 256, 500);
  }
  return don_yes;
}

/* ------------------------------------------------------------------ ending */

static char *stat(char *o, const char *label, uint32_t v) {
  o = cat(o, label);
  o = cat(o, " ");
  o = story_money(o, v);
  return cat(o, "\n");
}

static void ending_frame(int img) {
  table_draw();
  gfx_sprite(img, 0, 0);
  gfx_present();
}

void story_ending(void) {
  /* the lights go out: the Dealer's red eyes in the dark */
  table_reset();
  view_dealer();
  T.d.gone = 1;
  table_draw();
  gfx_present();
  pause_ms(700);
  table_draw();
  gfx_darken(0, 0, GFX_W, GFX_H, 4);
  gfx_present();
  pause_ms(900);
  gfx_sprite(IMG_D_EYES, 0, 0);
  gfx_present();
  pause_ms(1500);
  gfx_darken(0, 0, GFX_W, GFX_H, 4);
  gfx_present();
  pause_ms(900);
  /* the lights come back: a briefcase comes down onto the table, and opens */
  ending_frame(IMG_E_BRIEF0);
  pause_ms(700);
  for (int k = 0; k < 3; k++) {
    gfx_shake(0, k == 0 ? 3 : k == 1 ? -1 : 0);
    ending_frame(IMG_E_BRIEF1);
    pause_ms(k == 2 ? 1300 : 60);
  }
  ending_frame(IMG_E_BRIEF2);
  pause_ms(2600);
  fade_to(256, 0, 1500);
  pause_ms(600);
  uint64_t cash;
  if (G.mode == MODE_STORY) {
    int32_t c = 70000 - 220 * G.cigs - G.beer_ml * 3 / 2 - (G.doors > 2 ? 1000 * G.doors : 0);
    cash = c < 0 ? 0 : (uint64_t)c;
  } else {
    cash = G.score;
  }
  if (!G.ended) {
    /* counted once, even if the ending is seen again after quitting */
    G.ended = 1 + (G.mode == MODE_STORY && !P.don_unlocked);
    if (G.mode == MODE_STORY) P.wins++, P.don_unlocked = 1;
    else if (cash > P.best) P.best = cash;
    save_match();
  }
  static char text[256];
  char *o = cat(text, "");
  if (G.mode == MODE_STORY) o = stat(o, "SHOTS FIRED ........", G.shots);
  else o = stat(o, "ROUNDS BEAT ........", G.rounds_beat);
  o = stat(o, "SHELLS EJECTED .....", G.ejected);
  o = stat(o, "DOORS KICKED .......", G.doors);
  o = stat(o, "CIGARETTES SMOKED ..", G.cigs);
  o = stat(o, "ML OF BEER DRANK ...", G.beer_ml);
  static char title[32], total[32];
  cat(cat(cat(title, "CONGRATULATIONS, "), G.name), "!");
  cat(story_money(cat(total, "TOTAL CASH: "), cash), " $");
  /* on the road, the cash on the seat */
  gfx_plate(IMG_PL_CAR);
  fade_to(0, 256, 2000);
  pause_ms(1000);
  gfx_text_shadow(&font_big, title, 160, 10, C_WHITE);
  gfx_present();
  pause_ms(1500);
  const char *p = text;
  for (int line = 0; line < 5; line++) {
    char one[40];
    int n = 0;
    while (*p && *p != '\n') one[n++] = *p++;
    one[n] = 0;
    if (*p) p++;
    gfx_text(&font_small, one, 13, 37 + line * (font_small.h + 3), C_BLACK);
    gfx_text(&font_small, one, 12, 36 + line * (font_small.h + 3), C_WHITE);
    gfx_present();
    pause_ms(330);
  }
  pause_ms(400);
  int ty = 36 + 5 * (font_small.h + 3) + 6;
  gfx_text(&font_big, total, 13, ty + 1, C_BLACK);
  gfx_text(&font_big, total, 12, ty, C_WHITE);
  gfx_present();
  pause_ms(1200);
  if (G.ended == 2) {
    gfx_text_shadow(&font_small, "\"DOUBLE OR NOTHING\" UNLOCKED!", 160, 212, C_YELLOW);
    gfx_present();
  }
  redraw = 0;
  for (;;) {
    int k = key_wait(-1);
    if (IS_OK(k) || k == KEY_BACK) break;
  }
  fade_to(256, 0, 800);
  gfx_light(256, 0, 0);
}
