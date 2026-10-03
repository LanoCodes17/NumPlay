/* Host runner: draws rooms and plays the game with scripted keys, saving screenshots.
 *   play DATA.bin --room NAME --cam X,Y --shot PATH */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "../src/game.h"

extern uint32_t host_keys, host_time;
void host_shot(const char *path);

int main(int argc, char **argv) {
  if (argc < 2) return 1;
  FILE *f = fopen(argv[1], "rb");
  if (!f) return 1;
  fseek(f, 0, SEEK_END);
  long n = ftell(f);
  fseek(f, 0, SEEK_SET);
  uint8_t *data = malloc((size_t)n);
  if (fread(data, 1, (size_t)n, f) != (size_t)n) return 1;
  fclose(f);
  hk_bin = data;
  const char *room = "Tutorial_01", *shot = NULL;
  float cx = 40, cy = 14;
  int repeat = 1, pan = 0, sweep = 0;
  float step = 2;
  const char *items_out = NULL, *play = NULL, *shots = NULL;
  float px = 20, py = 20;
  int shot_every = 0, trace = 0;
  float dx = 0.15f;
  for (int i = 2; i < argc; i++) {
    if (!strcmp(argv[i], "--room")) room = argv[++i];
    else if (!strcmp(argv[i], "--cam")) sscanf(argv[++i], "%f,%f", &cx, &cy);
    else if (!strcmp(argv[i], "--shot")) shot = argv[++i];
    else if (!strcmp(argv[i], "--repeat")) repeat = atoi(argv[++i]);
    else if (!strcmp(argv[i], "--pan")) pan = atoi(argv[++i]);
    else if (!strcmp(argv[i], "--items")) items_out = argv[++i];
    else if (!strcmp(argv[i], "--dx")) dx = (float)atof(argv[++i]);
    else if (!strcmp(argv[i], "--sweep2d")) sweep = 2, step = (float)atof(argv[++i]);
    else if (!strcmp(argv[i], "--play")) play = argv[++i];
    else if (!strcmp(argv[i], "--at")) sscanf(argv[++i], "%f,%f", &px, &py);
    else if (!strcmp(argv[i], "--shots")) shots = argv[++i], shot_every = atoi(argv[++i]);
    else if (!strcmp(argv[i], "--trace")) trace = 1;
  }
  int id = -1;
  for (int i = 0; i < NUM_ROOMS; i++)
    if (!strcmp(room_name(i), room)) id = i;
  if (id < 0 || !room_load(id)) {
    fprintf(stderr, "no room %s\n", room);
    return 1;
  }
  g_cam_x = cx, g_cam_y = cy;
  if (play) {
    /* the game with scripted keys: "KEYS*TICKS,..." (L R U D J jump A attack S dash F focus/cast, _ none) */
    game_new();
    g_pd.can_dash = true;
    if (getenv("HKMP")) g_pd.mp = (int16_t)atoi(getenv("HKMP"));
    if (getenv("HKHEALTH")) g_pd.health = (int8_t)atoi(getenv("HKHEALTH"));
    if (getenv("HKGEO")) g_pd.geo = atoi(getenv("HKGEO"));
    if (getenv("HKFIRE")) g_pd.fireball_level = atoi(getenv("HKFIRE"));
    if (getenv("HKHORNETGP")) g_pd.hornet_greenpath = (uint8_t)atoi(getenv("HKHORNETGP"));
    if (getenv("HKDASH")) g_pd.has_dash = true;
    if (getenv("HKTRINKETS"))   /* (relics: "1,2,0,0") */
      sscanf(getenv("HKTRINKETS"), "%hhu,%hhu,%hhu,%hhu", &g_pd.trinkets[0], &g_pd.trinkets[1], &g_pd.trinkets[2], &g_pd.trinkets[3]);
    for (const char *f = getenv("HKFLAGS"); f && *f;) {   /* (PlayerData bools on, by number: "7,8") */
      pd_set_flag(atoi(f), true);
      while (*f && *f != ',') f++;
      if (*f) f++;
    }
    /* (HKMENU: from the title screen, as the calculator starts; HKSAVES: the saves' folder) */
    bool menu = getenv("HKMENU") != NULL;
    if (getenv("HKSAVES")) {
      extern void host_save_dir(const char *d);
      host_save_dir(getenv("HKSAVES"));
    }
    if (menu)
      menu_start();
    /* (HKLOAD=slot: that save loaded, the Knight at its respawn point) */
    else if (getenv("HKLOAD")) {
      if (!save_load(atoi(getenv("HKLOAD"))) || (getenv("HKGEO") && (g_pd.geo = atoi(getenv("HKGEO")), 0)) || !game_respawn()) {
        printf("load failed\n");
        return 1;
      }
    } else
      game_enter(id, px, py, true);
    int tick = 0;
    for (const char *p = play; *p;) {
      uint32_t keys = 0;
      for (; *p && *p != '*'; p++)
        keys |= *p == 'L' ? K_LEFT : *p == 'R' ? K_RIGHT : *p == 'U' ? K_UP : *p == 'D' ? K_DOWN : *p == 'J' ? K_JUMP
              : *p == 'A' ? K_ATTACK : *p == 'S' ? K_DASH : *p == 'F' ? K_FOCUS | K_SPELL
              /* (the calculator's OK, Back, Backspace: also jump, attack) */
              : *p == 'O' ? K_OK | K_JUMP : *p == 'B' ? K_BACK | K_ATTACK : *p == 'P' ? K_PAUSE
              : *p == 'I' ? K_INV : *p == 'M' ? K_MAP : 0;
      int n = *p == '*' ? atoi(++p) : 1;
      while (*p && *p != ',') p++;
      if (*p == ',') p++;
      for (int i = 0; i < n; i++, tick++) {
        /* (HKTALK=tick:text a conversation then; HKPROMPT=tick a prompt over the Knight then) */
        if (getenv("HKTALK") && atoi(getenv("HKTALK")) == tick) {
          dialogue_box_up();
          dialogue_start(atoi(strchr(getenv("HKTALK"), ':') + 1));
        }
        if (getenv("HKKILL") && atoi(getenv("HKKILL")) == tick) hero_take_damage(SIDE_LEFT, 99, HAZ_NORMAL);
        /* (HKFLING=tick: geo flung up, a way to the Knight's right) */
        if (getenv("HKFLING") && atoi(getenv("HKFLING")) == tick)
          geo_fling_at(1, 4, g_hero.body.x + 6, g_hero.body.y + 1, 8, 12, 60, 120, 0);
        /* (HKHURT=tick[:damage]) */
        if (getenv("HKHURT") && atoi(getenv("HKHURT")) == tick)
          hero_take_damage(SIDE_LEFT, strchr(getenv("HKHURT"), ':') ? atoi(strchr(getenv("HKHURT"), ':') + 1) : 1, HAZ_NORMAL);
        /* (HKREENTER=tick: the room entered again then, as it was left) */
        if (getenv("HKREENTER") && atoi(getenv("HKREENTER")) == tick) game_enter(id, px, py, true);
        if (getenv("HKGOD")) g_pd.health = g_pd.max_health;
        if (getenv("HKHIT") && atoi(getenv("HKHIT")) <= tick &&
            tick % (getenv("HKHITN") ? atoi(getenv("HKHITN")) : 10) == 0) {
          extern void enemies_debug_hit(int damage);
          enemies_debug_hit(getenv("HKHITD") ? atoi(getenv("HKHITD")) : 21);
        }
        if (getenv("HKPROMPT") && atoi(getenv("HKPROMPT")) == tick)
          prompt_show(-1, TXT_PROMPT_LISTEN, g_hero.body.x, g_hero.body.y + 1.5f);
        if (!menu || !menu_tick(keys)) game_tick(inv_tick(keys));
        if (trace && menu) printf("   menu in game %d\n", menu_in_game());
        if (trace)
          printf("%4d keys %02x pos %.3f,%.3f v %.3f,%.3f state %d ground %d jump %d fall %d clip %d frame %d cam %.2f,%.2f\n", tick,
                 keys, g_hero.body.x, g_hero.body.y, g_hero.body.vx, g_hero.body.vy, g_hero.state, g_hero.cs.on_ground,
                 g_hero.cs.jumping, g_hero.cs.falling, g_hero.anim.clip, g_hero.anim.frame, g_cam_x, g_cam_y);
        if (trace && getenv("HKCONTACTS"))
          for (int c = 0; c < g_hero.body.ncontacts; c++)
            printf("   contact col %d n %.2f,%.2f\n", g_hero.body.ccol[c], g_hero.body.cnx[c], g_hero.body.cny[c]);
        if (trace && getenv("HKSTATS")) printf("   health %d blue %d mp %d geo %d\n", g_pd.health, g_pd.health_blue, g_pd.mp, (int)g_pd.geo);
        if (trace && getenv("VMDEBUG")) {
          extern void vm_debug(void);
          vm_debug();
        }
        if (trace && getenv("ENEMIES")) {
          extern void enemies_debug(void);
          enemies_debug();
        }
        if (shots && shot_every && tick % shot_every == 0) {
          char path[256];
          snprintf(path, sizeof path, "%s/%05d.ppm", shots, tick);
          game_draw();
          host_shot(path);
        }
      }
    }
    if (shot) {
      int nf = getenv("NF") ? atoi(getenv("NF")) : 3;
      for (int i = 0; i < nf; i++) {   /* (the tile cache warm, as while playing) */
        if (!menu) {
          game_draw();
          continue;
        }
        g_gfx_no_room = !menu_in_game();
        if (menu_in_game()) game_draw_layers();
        menu_draw();
        gfx_frame();
      }
      host_shot(shot);
    }
    if (getenv("HKGROUPS")) {
      printf("fade %d items %u cam %.2f,%.2f groups:", g_screen_fade, g_gfx_items, g_cam_x, g_cam_y);
      for (int g = 1; g < MAX_GROUPS; g++)
        if (g_group_alpha[g] != 255) printf(" %d=%d", g, g_group_alpha[g]);
      printf("\n");
      int n;
      const Ent *e = room_ents(&n);
      for (int i = 0; i < n; i++)
        if (e[i].type == ENT_MASK || e[i].type == ENT_BOX)
          printf("ent %d type %d flags %d group %d,%d box %.1f,%.1f %.1f,%.1f p %.2f %.2f %.2f %.2f\n", i, e[i].type, e[i].flags,
                 e[i].group, e[i].group2, e[i].x0, e[i].y0, e[i].x1, e[i].y1, e[i].p0, e[i].p1, e[i].p2, e[i].p3);
    }
    printf("end: pos %.3f,%.3f state %d\n", g_hero.body.x, g_hero.body.y, g_hero.state);
    if (getenv("HKPD")) {
      /* (the PlayerData bools on, by number; Hornet's Greenpath count) */
      printf("pd:");
      for (int i = 0; i < PDF_COUNT; i++)
        if (pd_flag(i)) printf(" %d", i);
      int np = 0;
      for (int i = 0; i < MAX_PERSIST; i++) np += persist_get(i);
      printf(" hornet_greenpath %d trinkets %d %d %d %d charms %d saved %d health %d+%d mp %d geo %d\n", g_pd.hornet_greenpath,
             g_pd.trinkets[0], g_pd.trinkets[1], g_pd.trinkets[2], g_pd.trinkets[3], g_pd.charms_owned, np, g_pd.health,
             g_pd.health_blue, g_pd.mp, (int)g_pd.geo);
    }
    return 0;
  }
  if (sweep == 2) {
    /* every camera position on a grid, drawn twice (the second frame's tiles are the view's): the most tiles a view
     * uses, and where */
    extern uint32_t g_tex_used;
    const RoomHdr *h = g_room.h;
    float x0 = 14.6f, x1 = h->w - 14.6f, y0 = 8.3f, y1 = h->h - 8.3f;
    if (x1 < x0) x1 = x0;
    if (y1 < y0) y1 = y0;
    uint32_t worst = 0, maxitems = 0, maxfront = 0;
    float wx = 0, wy = 0;
    for (float y = y0; y <= y1 + 0.01f; y += step)
      for (float x = x0; x <= x1 + 0.01f; x += step) {
        g_cam_x = x, g_cam_y = y;
        gfx_frame();
        gfx_frame();
        tex_frame();
        if (g_tex_used > worst) worst = g_tex_used, wx = x, wy = y;
        if (g_gfx_items > maxitems) maxitems = g_gfx_items;
        { extern uint32_t g_gfx_dropped; if (g_gfx_dropped > maxfront) maxfront = g_gfx_dropped; }
      }
    extern uint32_t g_peak_pals, g_peak_arena, g_peak_soft;
    printf("%s %u %.1f %.1f items %u dropped %u pals %u arena %u soft %u\n", room, worst, wx, wy, maxitems, maxfront, g_peak_pals,
           g_peak_arena, g_peak_soft);
    return 0;
  }
  if (pan) {
    extern uint32_t g_tex_used;
    uint32_t d0 = g_tex_decodes, maxd = 0, maxp = 0, maxu = 0, sumu = 0;
    for (int fr = 0; fr < pan; fr++) {
      uint32_t a = g_tex_decodes;
      gfx_frame();
      if (fr > 2 && g_tex_decodes - a > maxd) maxd = g_tex_decodes - a;
      if (g_gfx_pixels > maxp) maxp = g_gfx_pixels;
      if (fr > 0) { sumu += g_tex_used; if (g_tex_used > maxu) maxu = g_tex_used; }
      g_cam_x += dx;
    }
    extern uint32_t g_soft_misses;
    printf("%s pan %d frames: %.1f decodes/frame (max %u), max pixels %u, tiles used avg %u max %u, soft misses %.1f/frame\n", room, pan,
           (g_tex_decodes - d0) / (double)pan, maxd, maxp, sumu / (pan - 1), maxu, g_soft_misses / (double)pan);
    return 0;
  }
#ifdef HOST
  { extern uint32_t g_kind_px[6]; gfx_frame(); memset(g_kind_px, 0, sizeof g_kind_px); gfx_frame();
    printf("run pixels: solid %u axis %u turned %u soft %u bilinear %u (soft mono turned %u)\n", g_kind_px[0], g_kind_px[1], g_kind_px[2], g_kind_px[3], g_kind_px[4], g_kind_px[5]); }
#endif
  clock_t t0 = clock();
  for (int r = 0; r < repeat; r++) gfx_frame();
  double ms = (double)(clock() - t0) * 1000.0 / CLOCKS_PER_SEC / repeat;
  extern uint32_t g_tex_calls;
  printf("%s %.1f,%.1f: %u items, %u pixels, %u decodes, %u misses, %u tile lookups, %.2f ms/frame\n", room, cx, cy, g_gfx_items,
         g_gfx_pixels, g_tex_decodes, g_tex_misses, g_tex_calls / repeat, ms);
  if (shot) host_shot(shot);
  if (getenv("TEXUSED")) {
    void tex_dump_used(const char *);
    tex_dump_used(getenv("TEXUSED"));
  }
  return 0;
}
