/* Checks of the item rules: crafting, stacking, smelting, drops. */
#include <stdio.h>
#include "nb.h"
#include "edits.h"

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static int craft(const int *ids, int w) {
  Stack g[9] = {{0}};
  for (int i = 0; i < w * w; i++) g[i].id = (uint16_t)ids[i], g[i].aux = ids[i] ? 1 : 0;
  int r = craft_find(g, w);
  return r < 0 ? 0 : recipes[r].out * 100 + recipes[r].n;
}

int main(void) {
  /* planks from a log, anywhere in the grid */
  int a[4] = {0, B_LOG_BIRCH, 0, 0};
  CHECK(craft(a, 2) == B_PLANKS_BIRCH * 100 + 4);
  /* sticks from any planks */
  int b[4] = {B_PLANKS_JUNGLE, 0, B_PLANKS_OAK, 0};
  CHECK(craft(b, 2) == I_STICK * 100 + 4);
  /* crafting table */
  int c[4] = {B_PLANKS_OAK, B_PLANKS_SPRUCE, B_PLANKS_OAK, B_PLANKS_OAK};
  CHECK(craft(c, 2) == B_CRAFTING_TABLE * 100 + 1);
  /* a pickaxe needs the 3 x 3 grid */
  int p[9] = {B_COBBLESTONE, B_COBBLESTONE, B_COBBLESTONE, 0, I_STICK, 0, 0, I_STICK, 0};
  CHECK(craft(p, 3) == I_STONE_PICKAXE * 100 + 1);
  /* an axe, mirrored */
  int x1[9] = {I_IRON_INGOT, I_IRON_INGOT, 0, I_IRON_INGOT, I_STICK, 0, 0, I_STICK, 0};
  int x2[9] = {0, I_IRON_INGOT, I_IRON_INGOT, 0, I_STICK, I_IRON_INGOT, 0, I_STICK, 0};
  CHECK(craft(x1, 3) == I_IRON_AXE * 100 + 1);
  CHECK(craft(x2, 3) == I_IRON_AXE * 100 + 1);
  /* torches from coal or charcoal */
  int t1[4] = {I_COAL, 0, I_STICK, 0}, t2[4] = {0, I_CHARCOAL, 0, I_STICK};
  CHECK(craft(t1, 2) == B_TORCH * 100 + 4);
  CHECK(craft(t2, 2) == B_TORCH * 100 + 4);
  /* shapeless: stew, in any order */
  int s[9] = {I_BOWL, 0, 0, 0, B_RED_MUSHROOM, 0, 0, 0, B_BROWN_MUSHROOM};
  CHECK(craft(s, 3) == I_MUSHROOM_STEW * 100 + 1);
  /* nothing */
  int n[4] = {B_DIRT, 0, 0, 0};
  CHECK(craft(n, 2) == 0);
  /* a furnace and a chest */
  int f[9] = {B_COBBLESTONE, B_COBBLESTONE, B_COBBLESTONE, B_COBBLESTONE, 0, B_COBBLESTONE, B_COBBLESTONE,
              B_COBBLESTONE, B_COBBLESTONE};
  CHECK(craft(f, 3) == B_FURNACE * 100 + 1);
  int bed[9] = {B_WOOL_RED, B_WOOL_WHITE, B_WOOL_RED, B_PLANKS_OAK, B_PLANKS_BIRCH, B_PLANKS_OAK, 0, 0, 0};
  CHECK(craft(bed, 3) == I_BED * 100 + 1);
  /* stacking: 64 at most, the same items first */
  Stack inv[36] = {{0}};
  CHECK(inv_add(inv, 36, B_DIRT, 100, 0) == 0);
  CHECK(inv[0].id == B_DIRT && inv[0].aux == 64 && inv[1].aux == 36);
  CHECK(inv_add(inv, 36, I_IRON_PICKAXE, 1, 5) == 0 && inv[2].id == I_IRON_PICKAXE && inv[2].aux == 5);
  CHECK(inv_add(inv, 36, B_DIRT, 30, 0) == 0 && inv[1].aux == 64 && inv[3].aux == 2);
  CHECK(inv_add(inv, 36, I_EGG, 20, 0) == 0 && inv[4].aux == 16 && inv[5].aux == 4);
  /* smelting */
  CHECK(smelt_of(B_IRON_ORE) == I_IRON_INGOT);
  CHECK(smelt_of(B_LOG_ACACIA) == I_CHARCOAL);
  CHECK(smelt_of(B_DIRT) == 0);
  CHECK(item_fuel(I_COAL) == 1600 && item_fuel(B_PLANKS_OAK) == 300 && item_fuel(I_STICK) == 100);
  /* harvesting */
  CHECK(!can_harvest(B_STONE, 0) && can_harvest(B_STONE, I_WOODEN_PICKAXE));
  CHECK(!can_harvest(B_DIAMOND_ORE, I_STONE_PICKAXE) && can_harvest(B_DIAMOND_ORE, I_IRON_PICKAXE));
  CHECK(can_harvest(B_DIRT, 0) && dig_speed(B_DIRT, I_DIAMOND_SHOVEL) == 8 && dig_speed(B_DIRT, I_DIAMOND_PICKAXE) == 1);
  Stack out[2];
  CHECK(block_drops(B_STONE, I_WOODEN_PICKAXE, out) == 1 && out[0].id == B_COBBLESTONE);
  CHECK(block_drops(B_STONE, 0, out) == 0);
  CHECK(block_drops(B_GRASS, 0, out) == 1 && out[0].id == B_DIRT);
  CHECK(block_drops(B_LOG_OAK_X, 0, out) == 1 && out[0].id == B_LOG_OAK);
  CHECK(block_drops(B_COAL_ORE, I_GOLDEN_PICKAXE, out) == 1 && out[0].id == I_COAL);
  CHECK(block_drops(B_TORCH_E, 0, out) == 1 && out[0].id == B_TORCH);
  /* changes survive: through the journal, into storage, back after a reload */
  {
    extern void host_save_dir(const char *d);
    host_save_dir("build/unit-saves");
    remove("build/unit-saves/ut1r0_0.nbe");
    remove("build/unit-saves/ut1r-1_0.nbe");
    world_new(0, "ut1");
    world_follow(8, 70, 8);
    int base = world_get(5, 62, 5);
    world_set(5, 62, 5, B_GOLD_BLOCK);
    world_set(-3, 63, 7, B_GLASS);
    for (int i = 0; i < 300; i++) world_set(i % 20 - 5, 61 + i / 20, 12, B_DIAMOND_BLOCK);   /* more than the journal */
    CHECK(world_get(5, 62, 5) == B_GOLD_BLOCK);
    CHECK(edits_flush());
    world_new(0, "ut1");
    world_follow(8, 70, 8);
    CHECK(world_get(5, 62, 5) == B_GOLD_BLOCK);
    CHECK(world_get(-3, 63, 7) == B_GLASS);
    CHECK(world_get(14, 75, 12) == B_DIAMOND_BLOCK && world_get(-5, 61, 12) == B_DIAMOND_BLOCK);
    world_set(5, 62, 5, base);
    CHECK(edits_flush());
    world_new(0, "ut1");
    world_follow(8, 70, 8);
    CHECK(world_get(5, 62, 5) == base);
  }
  /* water: 7 blocks out on a flat floor, then dry when the source is gone; lava meets water */
  {
    extern uint32_t game_time;
    world_new(0, "ut2");
    world_follow(8, 70, 8);
    int y = 70;
    for (int z = -12; z <= 12; z++)
      for (int x = -12; x <= 12; x++) {
        world_set(x + 8, y - 1, z + 8, B_STONE);
        for (int k = 0; k < 3; k++) world_set(x + 8, y + k, z + 8, B_AIR);
      }
    world_set(8, y, 8, B_WATER);
    neighbours_changed(8, y, 8);
    for (int t = 0; t < 400; t++) game_time++, world_tick();
    CHECK(world_get(9, y, 8) == B_FLOWING_WATER);
    CHECK(world_get(15, y, 8) == B_FLOWING_WATER_7);
    CHECK(world_get(16, y, 8) == B_AIR);
    CHECK(world_get(11, y, 10) == B_FLOWING_WATER_5);
    world_set(8, y, 8, B_AIR);
    neighbours_changed(8, y, 8);
    for (int t = 0; t < 600; t++) game_time++, world_tick();
    CHECK(world_get(9, y, 8) == B_AIR && world_get(12, y, 8) == B_AIR);
    /* lava source next to water: obsidian */
    world_set(4, y, 4, B_WATER);
    world_set(5, y, 4, B_LAVA);
    neighbours_changed(5, y, 4);
    for (int t = 0; t < 60; t++) game_time++, world_tick();
    CHECK(world_get(5, y, 4) == B_OBSIDIAN);
    plat_remove_prefix("ut2");
  }
  /* two cows fed wheat make a calf (EntityAIMate) */
  {
    extern uint32_t game_time;
    world_new(0, "ut3");
    world_follow(8, 70, 8);
    int y = 70;
    for (int z = 0; z <= 16; z++)
      for (int x = 0; x <= 16; x++) {
        world_set(x, y - 1, z, B_STONE);
        for (int k = 0; k < 3; k++) world_set(x, y + k, z, B_AIR);
      }
    memset(ents, 0, sizeof ents);
    opt.difficulty = 0;   /* (no monsters) */
    Entity *a = ent_new(E_COW, 6.5f, (float)y, 8.5f), *b = ent_new(E_COW, 9.5f, (float)y, 8.5f);
    a->health = b->health = 10;
    pl.inv[0].id = I_WHEAT, pl.inv[0].aux = 5, pl.slot = 0, pl.mode = 0;
    CHECK(mob_use(a) && mob_use(b) && pl.inv[0].aux == 3);
    int calves = 0;
    for (int t = 0; t < 400; t++) ents_tick(), game_time++;
    for (int i = 0; i < N_ENT; i++)
      if (ents[i].type == E_COW && ents[i].growth < 0) calves++;
    CHECK(calves == 1);
    plat_remove_prefix("ut3");
  }
  {
    /* weather: the clock, the strength, the light, and where rain falls */
    extern uint32_t game_time;
    world_new(0, "ut4");
    world_follow(8, 70, 8);
    for (int i = 0; i < VCX * VCZ; i++) vbiome[i] = 1;   /* plains */
    memset(&weather, 0, sizeof weather);
    rain_str = thunder_str = 0;
    game_time = 6000;   /* noon */
    CHECK(sky_sub() == 0);
    world_tick();
    CHECK(!weather.raining && weather.rain_time >= 12000 - 1 && weather.rain_time < 180000);
    weather.rain_time = 1;
    world_tick();
    CHECK(weather.raining && rain_str > 0 && rain_str < 0.05f);
    for (int t = 0; t < 120; t++) world_tick();
    CHECK(rain_str == 1 && sky_sub() == 3);   /* rain alone: still day, no sleeping */
    weather.thundering = 1, thunder_str = 1;
    CHECK(sky_sub() >= 4);                    /* a storm: dark enough to sleep */
    int y = 70;
    for (int z = 0; z <= 4; z++)
      for (int x = 0; x <= 4; x++) {
        world_set(x, y - 1, z, B_STONE);
        for (int k = 0; k < 6; k++) world_set(x, y + k, z, B_AIR);
      }
    CHECK(rain_at(2, y, 2));
    world_set(2, y + 3, 2, B_LEAVES_OAK);      /* leaves keep it off */
    CHECK(!rain_at(2, y, 2) && world_rain_top(2, 2) == y + 4);
    world_set(3, y, 3, B_TALL_GRASS);          /* plants do not */
    CHECK(world_rain_top(3, 3) == y);
    vbiome[(2 - vc_z0) * VCX + 1 - vc_x0] = 2;  /* deserts get none */
    CHECK(!rain_at(1, y, 2));
    vbiome[(2 - vc_z0) * VCX + 1 - vc_x0] = 12; /* snowy biomes get snow */
    CHECK(!rain_at(1, y, 2));
    /* a bolt on the player: 5 damage and fire, and the sky flashes */
    pl.x = 2.5f, pl.y = (float)y, pl.z = 2.5f, pl.mode = 0, pl.dead = false, pl.health = 20, pl.invuln = 0, pl.fire = 0;
    memset(pl.armor, 0, sizeof pl.armor);
    bolt = (Bolt){2.5f, (float)y, 2.5f, 2, 0, 1, 7};
    world_tick();
    CHECK(pl.health == 15 && pl.fire > 0 && last_bolt == 2 && bolt.on);
    for (int t = 0; t < 3; t++) world_tick();
    CHECK(!bolt.on);
    weather_clear();
    world_tick();
    CHECK(!weather.raining && !weather.thundering);
    plat_remove_prefix("ut4");
  }
  printf("%s (%d recipes)\n", fails ? "FAILED" : "all good", N_RECIPES);
  return fails != 0;
}
