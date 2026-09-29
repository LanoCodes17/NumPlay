/* The archery scene (a stub until it is ported). */
#include "../src/ent.h"

static void start(void) {
  NodeId n = node_new_sym(S_archery_pT);
  node_add(game.root, n);
  ent_register_tree(n);
}
static void tick(void) { sys_back_pauses(); }
const SceneDef scene_archery = {"archery", start, tick, NULL, NULL, NULL, NULL};
