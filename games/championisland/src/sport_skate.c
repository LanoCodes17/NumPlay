#include "../src/ent.h"
#include "../src/spr.h"
#include <stdio.h>
static void start(void) {
  const uint8_t *al = palalpha(SHEET_SKATE); const uint16_t *pal = pal565(SHEET_SKATE);
  for (int i = 0; i < 24; i++) printf("%d: %04x a%u\n", i, pal[i], al[i]);
}
static void tick(void) {}
const SceneDef scene_skate = {"skate", start, tick, NULL, NULL, NULL, NULL};
