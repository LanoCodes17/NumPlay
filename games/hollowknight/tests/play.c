/* Host runner: draws rooms and plays the game with scripted keys, saving screenshots.
 *   play DATA.bin --room NAME --cam X,Y --shot PATH */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include "../src/game.h"

extern uint32_t host_keys, host_time;
void host_shot(const char *path);

/* every way into every room (each gate or door of every room, to where it leads), as the game goes through it: the
 * Knight must come in seen, in the room, free, and able to move (--gates all, or the rooms one leads from) */
static int gate_kind_of(const Ent *e) {
  if (e->flags & G_DOOR) return GATE_DOOR;
  const char *n = str_at(e->s0);
  return strstr(n, "top") ? GATE_TOP : strstr(n, "right") ? GATE_RIGHT : strstr(n, "left") ? GATE_LEFT
         : strstr(n, "bot") ? GATE_BOTTOM : GATE_DOOR;
}
static bool shielded;   /* (the enemies can't hurt the Knight: what is checked is the way, not the fights) */
static void run_ticks(int n, uint32_t keys) {
  for (int i = 0; i < n; i++) {
    if (shielded) g_hero.cs.invulnerable = true;
    game_tick(stag_tick(shop_tick(inv_tick(keys))));
    if (getenv("HKGATETRACE"))
      printf("   %s pos %.3f,%.3f v %.2f,%.2f state %d ts %d ground %d hidden %d\n", room_name(g_room.id), g_hero.body.x,
             g_hero.body.y, g_hero.body.vx, g_hero.body.vy, g_hero.state, g_hero.transition_state, g_hero.cs.on_ground,
             g_hero.hidden);
  }
}
/* every hazard respawn point of every room (the gates' and the hazard triggers'): ground under it, in the room; and
 * whether the room has hazards at all */
static int markers_test(void) {
  int bad = 0;
  for (int r = 0; r < NUM_ROOMS; r++) {
    if (!room_load(r)) continue;
    int n;
    const Ent *es = room_ents(&n);
    int hazards = 0;
    for (int i = 0; i < n; i++)
      if (es[i].type == ENT_DAMAGE && (int)es[i].p0 >= HAZ_SPIKES) hazards++;
    for (int i = 0; i < n; i++) {
      int m = -1;
      if (es[i].type == ENT_GATE && es[i].p3 > 0) m = (int)es[i].p3 - 1;
      if (es[i].type == ENT_HAZARD_TRIGGER) m = es[i].a;
      if (m < 0) continue;
      if (m >= n || es[m].type != ENT_HAZARD_MARKER) {
        printf("BAD %s ent %d: marker %d is not a hazard marker\n", room_name(r), i, m);
        bad++;
        continue;
      }
      PhysHit hit;
      float x = es[m].x0, y = es[m].y0;
      bool ground = phys_ray(x, y, 0, -1, 50, CF_TERRAIN, &hit);
      bool in = x > 0 && x < g_room.h->w && y > 0 && y < g_room.h->h;
      if (!ground || !in || (ground && hit.dist > 15))
        printf("%s %s ent %d (%s) marker %d at %.2f,%.2f: %s%s (hazards in the room: %d)\n", hazards ? "BAD " : "note", room_name(r), i,
               es[i].type == ENT_GATE ? str_at(es[i].s0) : "trigger", m, x, y, ground ? "" : "no ground below", in ? "" : " outside the room", hazards);
      bad += hazards && (!ground || !in);
    }
  }
  printf("markers: %d bad\n", bad);
  return bad ? 1 : 0;
}

/* from a room, a long walk of made up keys (runs, jumps, dashes, slashes, as a player might), following the Knight
 * from room to room: he must never leave the room's bounds, be without control long, or be wedged in place */
static uint32_t wander_rng = 1;
static uint32_t wander_rand(void) { return wander_rng = wander_rng * 1103515245u + 12345u, wander_rng >> 16; }
static int wander_test(const char *which, int seed, int ticks) {
  int fails = 0;
  for (int r = 0; r < NUM_ROOMS; r++) {
    if (strcmp(which, "all") && strcmp(which, room_name(r))) continue;
    game_new();
    g_pd.can_dash = true, g_pd.has_dash = true;
    if (!room_load(r)) continue;
    /* (in at its first gate that leads somewhere, else its middle) */
    int n;
    const Ent *es = room_ents(&n);
    float sx = g_room.h->w / 2, sy = g_room.h->h / 2;
    for (int i = 0; i < n; i++)
      if (es[i].type == ENT_GATE && !(es[i].flags & G_DOOR)) {
        sx = (es[i].x0 + es[i].x1) / 2, sy = (es[i].y0 + es[i].y1) / 2;
        sx = sx < 3 ? 3 : sx > g_room.h->w - 3 ? g_room.h->w - 3 : sx;
        sy = sy < 3 ? 3 : sy > g_room.h->h - 3 ? g_room.h->h - 3 : sy;
        break;
      }
    char start[64];
    snprintf(start, sizeof start, "%s", room_name(r));
    if (!game_enter(r, sx, sy, true)) continue;
    g_hero.body.y = hero_ground_y(sx, sy);
    wander_rng = (uint32_t)(seed * 7919 + r * 104729 + 1);
    uint32_t keys = 0;
    int left = 0, no_input = 0, out = 0, still = 0, unseen = 0, reports = 0;
    float lx = g_hero.body.x, ly = g_hero.body.y;
    const Hero *h = &g_hero;
    for (int t = 0; t < ticks && reports < 3; t++) {
      if (--left <= 0) {
        uint32_t q = wander_rand();
        keys = (q & 3) == 0 ? 0 : (q & 3) == 1 ? K_LEFT : K_RIGHT;
        if (q >> 2 & 1) keys |= K_JUMP;
        if ((q >> 3 & 7) == 0) keys |= K_DASH;
        if ((q >> 6 & 3) == 0) keys |= K_ATTACK;
        if ((q >> 8 & 15) == 0) keys |= K_DOWN;
        if ((q >> 12 & 15) == 0) keys |= K_UP;
        left = 5 + (int)(wander_rand() % 60);
      }
      uint32_t k = keys;
      if ((keys & (K_JUMP | K_ATTACK | K_DASH)) && (t & 15) > 11) k &= ~(K_JUMP | K_ATTACK | K_DASH);   /* (pressed again) */
      game_tick(stag_tick(shop_tick(inv_tick(k))));
      if (getenv("HKWANDERTRACE") && t >= atoi(getenv("HKWANDERTRACE")) - 60 && t <= atoi(getenv("HKWANDERTRACE")) + 20)
        printf("   %d %s keys %02x pos %.3f,%.3f v %.2f,%.2f state %d ground %d wall %d\n", t, room_name(g_room.id), k, h->body.x,
               h->body.y, h->body.vx, h->body.vy, h->state, h->cs.on_ground, h->cs.touching_wall);
      bool busy = h->cs.transitioning || h->cs.dead || h->cs.hazard_death || h->cs.hazard_respawning || game_changing_room() ||
                  h->hidden;
      float w = g_room.h->w, hh = g_room.h->h, x = h->body.x, y = h->body.y;
      if (!busy && (x < -3 || x > w + 3 || y < -3 || y > hh + 3)) {
        if (++out == 25) printf("FAIL wander %s (seed %d) tick %d: out of %s at %.2f,%.2f\n", start, seed, t, room_name(g_room.id), x, y), fails++, reports++;
      } else
        out = 0;
      if (!busy && h->state == HS_NO_INPUT && !h->control_relinquished) {
        if (++no_input == 500) printf("FAIL wander %s (seed %d) tick %d: no control for 10 s in %s at %.2f,%.2f\n", start, seed, t, room_name(g_room.id), x, y), fails++, reports++;
      } else
        no_input = 0;
      if (!busy && fabsf(x - lx) < 0.01f && fabsf(y - ly) < 0.01f && (k & (K_LEFT | K_RIGHT | K_JUMP))) {
        if (still == 399 && getenv("HKWANDERWHO")) {
          printf("   (shop %d inv %d msg %d dialogue %d map %d collect %d accepting %d relinq %d)\n", shop_open(), inv_open(), msg_shown(),
                 dialogue_box_shown(), map_shown(), collect_active(), h->accepting_input, h->control_relinquished);
        }
        if (++still == 400) printf("note wander %s (seed %d) tick %d: in place 8 s, keys moving, in %s at %.2f,%.2f state %d\n", start, seed, t, room_name(g_room.id), x, y, h->state), reports++;
      } else
        still = 0;
      /* (and seen: drawn every so often, inside the view) */
      if (!busy && t % 10 == 0) {
        extern uint32_t g_hero_undrawn;
        uint32_t before = g_hero_undrawn;
        game_draw();
        if (g_hero_undrawn != before) printf("FAIL wander %s (seed %d) tick %d: not drawn in %s at %.2f,%.2f\n", start, seed, t, room_name(g_room.id), x, y), fails++, reports++;
      }
      if (!busy && (fabsf(x - g_cam_x) > (VIEW_W / 2) / FOCAL * -CAM_Z || fabsf(y - g_cam_y) > (VIEW_H / 2) / FOCAL * -CAM_Z)) {
        if (++unseen == 100) printf("FAIL wander %s (seed %d) tick %d: out of view 2 s in %s at %.2f,%.2f, view at %.2f,%.2f\n", start, seed, t, room_name(g_room.id), x, y, g_cam_x, g_cam_y), fails++, reports++;
      } else
        unseen = 0;
      lx = x, ly = y;
    }
    if (getenv("HKWANDERLOG")) printf("   %s: ended in %s at %.2f,%.2f\n", start, room_name(g_room.id), h->body.x, h->body.y);
  }
  printf("wander: %d failing\n", fails);
  return fails ? 1 : 0;
}

/* every arena (Battle Control; the False Knight's is enemy.c's own): started and its enemies beaten (each hit from
 * afar, the sleeping woken, the Knight shielded); then its fight must be over and the room's battle gates open, and
 * stay so as the Knight comes back to the room, and after a save and a load */
static int gates_shut(float *gx, float *gy) {
  int n, shut = 0;
  const Ent *es = room_ents(&n);
  for (int i = 0; i < n; i++) {
    const Ent *g = &es[i];
    if (g->type != ENT_OBJ || g->flags != OK_BGATE || !g->group) continue;
    /* (rays across it, either way, at a few places: one meets a collider of its own) */
    PhysHit hit;
    bool on = false;
    for (float d = -3; d <= 3 && !on; d += 0.5f)
      for (int s = -1; s <= 1 && !on; s += 2)
        on = (phys_ray(g->x0 - 4 * s, g->y0 + d, s, 0, 8, 0xFF, &hit) && hit.col >= g->a && hit.col < g->a + g->group) ||
             (phys_ray(g->x0 + d, g->y0 - 4 * s, 0, s, 8, 0xFF, &hit) && hit.col >= g->a && hit.col < g->a + g->group);
    if (on && !shut++) *gx = g->x0, *gy = g->y0;
  }
  return shut;
}
static int arenas_test(void) {
  extern void enemies_debug_hit(int damage);
  int fails = 0, total = 0;
  setenv("HKHITR", "200", 1);   /* (every enemy of the room hit) */
  if (getenv("HKSAVES")) {
    extern void host_save_dir(const char *d);
    host_save_dir(getenv("HKSAVES"));
  }
  for (int r = 0; r < NUM_ROOMS; r++) {
    if (!room_load(r)) continue;
    int n, ar = -1;
    const Ent *es = room_ents(&n);
    for (int i = 0; i < n; i++)
      if (es[i].type == ENT_OBJ && es[i].flags == OK_ARENA) ar = i;
    if (ar < 0) continue;
    total++;
    const Ent a = es[ar];
    /* (in its trigger, else at the room's first gate) */
    float x = (a.x0 + a.x1) / 2, y = (a.y0 + a.y1) / 2;
    if (a.x1 <= a.x0)
      for (int i = 0; i < n; i++)
        if (es[i].type == ENT_GATE && !(es[i].flags & G_DOOR)) {
          x = (es[i].x0 + es[i].x1) / 2, y = (es[i].y0 + es[i].y1) / 2;
          x = x < 3 ? 3 : x > g_room.h->w - 3 ? g_room.h->w - 3 : x;
          y = y < 3 ? 3 : y > g_room.h->h - 3 ? g_room.h->h - 3 : y;
          break;
        }
    char name[64], why[256] = "";
    snprintf(name, sizeof name, "%s", room_name(r));
    game_new();
    if (!game_enter(r, x, y, true)) continue;
    shielded = true;
    run_ticks(5, 0);
    arena_start();            /* (START: from its trigger or an enemy) */
    enemies_battle_start();   /* (sleepers woken) */
    int t = 0, shut_in_fight = 0;
    float gx = 0, gy = 0;
    for (; t < 6000 && !persist_get(a.persist); t++) {
      if (t % 10 == 0) enemies_debug_hit(30);
      run_ticks(1, 0);
      if (t == 10) shut_in_fight = gates_shut(&gx, &gy);
    }
    run_ticks(250, 0);   /* (End Wait, the gates opening) */
    int shut = gates_shut(&gx, &gy);
    if (!shut_in_fight) strcat(why, " no gate seen shut in the fight;");
    if (!persist_get(a.persist)) strcat(why, " fight not over;");
    if (shut) snprintf(why + strlen(why), sizeof why - strlen(why), " %d gates shut after it (at %.1f,%.1f);", shut, gx, gy);
    /* (the room again, as the Knight comes back) */
    game_enter(r, x, y, true);
    run_ticks(100, 0);
    if ((shut = gates_shut(&gx, &gy)))
      snprintf(why + strlen(why), sizeof why - strlen(why), " back: %d gates shut (at %.1f,%.1f);", shut, gx, gy);
    /* (saved, a new game, loaded, the room again) */
    save_select(SAVE_SLOTS - 1);
    if (!save_game()) strcat(why, " not saved;");
    game_new();
    if (!save_load(SAVE_SLOTS - 1)) strcat(why, " not loaded;");
    if (!persist_get(a.persist)) strcat(why, " loaded: fight not over;");
    game_enter(r, x, y, true);
    run_ticks(100, 0);
    if ((shut = gates_shut(&gx, &gy)))
      snprintf(why + strlen(why), sizeof why - strlen(why), " loaded: %d gates shut (at %.1f,%.1f);", shut, gx, gy);
    shielded = false;
    fails += why[0] != 0;
    printf("%s %s: won in %.1f s, %d gates shut in the fight%s\n", why[0] ? "FAIL" : "ok  ", name, t * 0.02f, shut_in_fight, why);
  }
  printf("arenas: %d, %d failing\n", total, fails);
  return fails || !total ? 1 : 0;
}

static int gates_test(const char *which) {
  if (!strcmp(which, "markers")) return markers_test();
  if (!strcmp(which, "arenas")) return arenas_test();
  if (!strncmp(which, "wander:", 7)) {   /* (wander:ROOM|all:seed:ticks) */
    char room[64];
    int seed = 1, ticks = 6000;
    sscanf(which + 7, "%63[^:]:%d:%d", room, &seed, &ticks);
    return wander_test(room, seed, ticks);
  }
  int fails = 0, total = 0;
  for (int from = 0; from < NUM_ROOMS; from++) {
    if (strcmp(which, "all") && strcmp(which, room_name(from))) continue;
    if (!room_load(from)) continue;
    int n;
    const Ent *es = room_ents(&n);
    struct { int ent, kind, to; uint16_t entry; float delay, x, y; } ways[64];
    int nw = 0;
    for (int i = 0; i < n && nw < 64; i++) {
      if (es[i].type != ENT_GATE || (es[i].flags & G_ENTRY_ONLY)) continue;
      int to = es[i].a;
      uint16_t entry = es[i].s1;
      float delay = es[i].p0;
      if (es[i].flags & G_DOOR) {   /* (a door's record: the box flagged 4 after it) */
        to = 0xFFFF;
        for (int j = i + 1; j < n && es[j].type == ENT_BOX; j++)
          if (es[j].flags == 4) to = es[j].a, entry = es[j].s1, delay = es[j].p0;
      }
      if (to == 0xFFFF || to >= NUM_ROOMS) continue;
      ways[nw].ent = i, ways[nw].kind = gate_kind_of(&es[i]), ways[nw].to = to, ways[nw].entry = entry;
      ways[nw].delay = delay, ways[nw].x = (es[i].x0 + es[i].x1) / 2, ways[nw].y = (es[i].y0 + es[i].y1) / 2;
      nw++;
    }
    char from_name[64];
    snprintf(from_name, sizeof from_name, "%s", room_name(from));
    for (int w = 0; w < nw; w++) {
      game_new();
      if (!game_enter(from, ways[w].x, ways[w].y, true)) continue;
      /* (a side gate behind a collider of its own room, not the ground: walled off until something opens it, as
       * Crossroads_33's to the pillar) */
      bool walled = false;
      if (ways[w].kind == GATE_LEFT || ways[w].kind == GATE_RIGHT) {
        PhysHit hit;
        float dir = ways[w].kind == GATE_LEFT ? 1 : -1;
        walled = phys_ray(ways[w].x, hero_ground_y(ways[w].x, ways[w].y) + 0.5f, dir, 0, 8, 0xFF, &hit) && hit.col > 0;
      }
      char gate_name[64];
      snprintf(gate_name, sizeof gate_name, "%s", str_at(room_ents(&n)[ways[w].ent].s0));
      shielded = true;
      run_ticks(5, 0);
      game_transition(ways[w].to, ways[w].entry, ways[w].kind, ways[w].delay, false);
      run_ticks(400, 0);   /* (the fade, the room, the Knight walking, dropping or jumping in, landing) */
      total++;
      const Hero *h = &g_hero;
      char why[256] = "";
      if (g_room.id != ways[w].to) snprintf(why + strlen(why), sizeof why - strlen(why), " in %s, not there", room_name(g_room.id));
      if (h->hidden) strcat(why, " hidden");
      if (h->cs.transitioning) strcat(why, " still entering");
      if (!h->accepting_input) strcat(why, " no input");
      if (h->state == HS_NO_INPUT) strcat(why, " no-input state");
      if (h->cs.dead || h->cs.hazard_death) strcat(why, " dead");
      if (h->body.x < 0 || h->body.x > g_room.h->w || h->body.y < 0 || h->body.y > g_room.h->h) strcat(why, " out of the room");
      if (!h->cs.on_ground) strcat(why, " not on the ground");
      float x0 = h->body.x, y0 = h->body.y;
      int room0 = g_room.id;
      if (getenv("HKGATETRACE")) {
        printf("   arrived in %s by entry %s\n", room_name(g_room.id), str_at(ways[w].entry));
        int ne;
        const Ent *e2 = room_ents(&ne);
        for (int i = 0; i < ne; i++)
          if (e2[i].type == ENT_GATE || (e2[i].x0 < 8 && e2[i].x1 > -4))
            printf("   ent %d type %d flags %d %s box %.2f,%.2f..%.2f,%.2f a %d p %.2f %.2f %.2f %.2f\n", i, e2[i].type, e2[i].flags,
                   e2[i].type == ENT_GATE ? str_at(e2[i].s0) : "", e2[i].x0, e2[i].y0, e2[i].x1, e2[i].y1, e2[i].a, e2[i].p0,
                   e2[i].p1, e2[i].p2, e2[i].p3);
        PhysHit hit;
        for (float yy = 3; yy < 7; yy += 0.5f)
          if (phys_ray(-3, yy, 1, 0, 10, 0xFF, &hit)) printf("   ray right from -3,%.1f: hit %.2f,%.2f col %d flags %d\n", yy, hit.x, hit.y, hit.col, phys_col_flags(hit.col));
        for (float xx = -2; xx < 4; xx += 0.5f)
          if (phys_ray(xx, 6, 0, -1, 10, 0xFF, &hit)) printf("   ray down from %.1f,6: hit %.2f,%.2f col %d flags %d\n", xx, hit.x, hit.y, hit.col, phys_col_flags(hit.col));
      }
      /* (then spikes, where the room has some: back at the room's hazard respawn point, seen, free, on the ground) */
      int ne, hazards = 0;
      const Ent *e1 = room_ents(&ne);
      for (int i = 0; i < ne; i++)
        if (e1[i].type == ENT_DAMAGE && (int)e1[i].p0 >= HAZ_SPIKES) hazards++;
      if (g_room.id == room0 && !why[0] && hazards) {
        run_ticks(10, 0);
        hero_take_damage(SIDE_LEFT, 1, HAZ_SPIKES);
        if (getenv("HKSPIKETRACE")) setenv("HKGATETRACE", "1", 1);
        run_ticks(300, 0);
        if (getenv("HKSPIKETRACE")) unsetenv("HKGATETRACE");
        if (g_room.id != room0) strcat(why, " spikes: another room");
        if (h->hidden) strcat(why, " spikes: hidden");
        if (!h->accepting_input || h->state == HS_NO_INPUT) strcat(why, " spikes: no input");
        if (h->cs.dead || h->cs.hazard_death) strcat(why, " spikes: dead");
        if (h->body.x < 0 || h->body.x > g_room.h->w || h->body.y < 0 || h->body.y > g_room.h->h) strcat(why, " spikes: out of the room");
        if (!h->cs.on_ground) strcat(why, " spikes: not on the ground");
        if (why[0]) snprintf(why + strlen(why), sizeof why - strlen(why), " (respawn %.2f,%.2f)", h->body.x, h->body.y);
        if (why[0] && getenv("HKGATETRACE")) {
          room_load(room0);
          int ne;
          const Ent *e2 = room_ents(&ne);
          for (int i = 0; i < ne; i++)
            if (e2[i].type == ENT_GATE && e2[i].p3 > 0) {
              const Ent *m = &e2[(int)e2[i].p3 - 1];
              printf("   gate %s marker %d type %d at %.2f,%.2f\n", str_at(e2[i].s0), (int)e2[i].p3 - 1, m->type, m->x0, m->y0);
            }
        }
      }
      /* (then: can he move? right, left, a jump) */
      run_ticks(30, K_RIGHT);
      float xr = h->body.x;
      run_ticks(60, K_LEFT);
      float xl = h->body.x;
      run_ticks(1, 0);
      run_ticks(15, K_JUMP);
      float yj = h->body.y;
      if (g_room.id == room0 && fabsf(xr - x0) < 0.05f && fabsf(xl - xr) < 0.05f) strcat(why, " can't walk");
      if (g_room.id == room0 && fabsf(yj - y0) < 0.05f && fabsf(xl - x0) < 0.05f) strcat(why, " can't jump");
      shielded = false;
      bool bad = why[0] != 0 && !walled;
      fails += bad;
      printf("%s %s %s -> %s: at %.2f,%.2f%s%s\n", bad ? "FAIL" : why[0] ? "note" : "ok  ", from_name, gate_name, room_name(ways[w].to),
             x0, y0, why, bad || !why[0] ? "" : " (the gate is walled off in a new game)");
      room_load(from);
      es = room_ents(&n);
    }
  }
  printf("gates: %d ways, %d failing\n", total, fails);
  return fails ? 1 : 0;
}

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
  g_gfx_fast = getenv("HKFAST") != NULL;   /* (as the calculator draws when it is slow) */
  const char *room = "Tutorial_01", *shot = NULL;
  float cx = 40, cy = 14;
  int repeat = 1, pan = 0, sweep = 0;
  float step = 2;
  const char *items_out = NULL, *play = NULL, *shots = NULL;
  float px = 20, py = 20;
  int shot_every = 0, trace = 0;
  const char *gates = NULL;
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
    else if (!strcmp(argv[i], "--gates")) gates = argv[++i];
    else if (!strcmp(argv[i], "--list")) {   /* (the rooms' names) */
      for (int r = 0; r < NUM_ROOMS; r++) printf("%s\n", room_name(r));
      return 0;
    }
  }
  if (gates) return gates_test(gates);
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
    if (getenv("HKGRUBS")) g_pd.grubs_collected = (uint8_t)atoi(getenv("HKGRUBS"));
    if (getenv("HKPIECES")) {
      int hp = 0, vf = 0;
      sscanf(getenv("HKPIECES"), "%d,%d", &hp, &vf);
      g_pd.heart_pieces = (uint8_t)hp, g_pd.vessel_fragments = (uint8_t)vf;
    }
    if (getenv("HKGEO")) g_pd.geo = atoi(getenv("HKGEO"));
    if (getenv("HKSTAG")) g_pd.stag_position1 = (uint8_t)(atoi(getenv("HKSTAG")) + 1);   /* (stagPosition) */
    if (getenv("HKFIRE")) g_pd.fireball_level = atoi(getenv("HKFIRE"));
    if (getenv("HKHORNETGP")) g_pd.hornet_greenpath = (uint8_t)atoi(getenv("HKHORNETGP"));
    if (getenv("HKDASH")) g_pd.has_dash = true;
    if (getenv("HKMENDERSTATE")) g_pd.mender_state = (uint8_t)atoi(getenv("HKMENDERSTATE"));
    if (getenv("HKMAPPED")) memset(g_pd.rooms_mapped, 0xFF, sizeof g_pd.rooms_mapped);   /* (every room mapped) */
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
          geo_fling_at(1, getenv("HKFLINGN") ? atoi(getenv("HKFLINGN")) : 4, g_hero.body.x + 6, g_hero.body.y + 1, 8, 12, 60, 120, 0);
        /* (HKHURT=tick[:damage]) */
        if (getenv("HKHURT") && atoi(getenv("HKHURT")) == tick)
          hero_take_damage(SIDE_LEFT, strchr(getenv("HKHURT"), ':') ? atoi(strchr(getenv("HKHURT"), ':') + 1) : 1, HAZ_NORMAL);
        /* (HKSPIKES=tick: spikes then) */
        if (getenv("HKSPIKES") && atoi(getenv("HKSPIKES")) == tick) hero_take_damage(SIDE_LEFT, 1, HAZ_SPIKES);
        /* (HKREENTER=tick: the room entered again then, as it was left) */
        if (getenv("HKREENTER") && atoi(getenv("HKREENTER")) == tick) game_enter(id, px, py, true);
        if (getenv("HKGOD")) g_pd.health = g_pd.max_health;
        if (getenv("HKHIT") && atoi(getenv("HKHIT")) <= tick && (!getenv("HKHITEND") || tick < atoi(getenv("HKHITEND"))) &&
            tick % (getenv("HKHITN") ? atoi(getenv("HKHITN")) : 10) == 0) {
          extern void enemies_debug_hit(int damage);
          enemies_debug_hit(getenv("HKHITD") ? atoi(getenv("HKHITD")) : 21);
        }
        if (getenv("HKPROMPT") && atoi(getenv("HKPROMPT")) == tick)
          prompt_show(-1, TXT_PROMPT_LISTEN, g_hero.body.x, g_hero.body.y + 1.5f);
        if (!menu || !menu_tick(keys)) game_tick(stag_tick(shop_tick(inv_tick(keys))));
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
    if (getenv("HKACTORS")) {
      extern uint32_t g_actors_dropped, g_actors_peak, g_items_dropped_total, g_items_peak, g_hero_undrawn;
      printf("actors: peak %u, dropped %u; items: peak %u, dropped %u; the Knight not drawn in %u frames\n", g_actors_peak,
             g_actors_dropped, g_items_peak, g_items_dropped_total, g_hero_undrawn);
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
      extern uint32_t g_gfx_dropped;
      printf("fade %d items %u dropped %u cam %.2f,%.2f groups:", g_screen_fade, g_gfx_items, g_gfx_dropped, g_cam_x, g_cam_y);
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
    if (getenv("HKROOM")) printf("room: %s\n", room_name(g_room.id));
    if (getenv("HKCAM")) cam_debug();
    if (getenv("HKPD")) {
      /* (the PlayerData bools on, by number; Hornet's Greenpath count) */
      printf("pd:");
      for (int i = 0; i < PDF_COUNT; i++)
        if (pd_flag(i)) printf(" %d", i);
      int np = 0;
      for (int i = 0; i < MAX_PERSIST; i++) np += persist_get(i);
      printf(" hornet_greenpath %d trinkets %d %d %d %d charms %d saved %d health %d+%d mp %d geo %d grubs %d"
             " max %d pieces %d %d reserve %d\n", g_pd.hornet_greenpath,
             g_pd.trinkets[0], g_pd.trinkets[1], g_pd.trinkets[2], g_pd.trinkets[3], g_pd.charms_owned, np, g_pd.health,
             g_pd.health_blue, g_pd.mp, (int)g_pd.geo, g_pd.grubs_collected, g_pd.max_health, g_pd.heart_pieces,
             g_pd.vessel_fragments, g_pd.mp_reserve_max);
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
