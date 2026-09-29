/* Cutscenes and the doodle's films (intro, each champion's outro, the ending). */
#include <stdio.h>
#include "ent.h"

static void start_cutscene(void) {
  NodeId n = node_new_sym(S_cutscene_pab);
  node_add(game.root, n);
  ent_register_tree(n);
}
static void start_video(void) {
  char k[48];
  snprintf(k, sizeof k, "%s_VIDEO_SEEN", game.variant);
  store_set_bool(k, true);
  game_go("overworld");
}
static void tick(void) {}
const SceneDef scene_cutscene = {"cutscene", start_cutscene, tick, NULL, NULL, NULL, NULL};
const SceneDef scene_video = {"video", start_video, tick, NULL, NULL, NULL, NULL};
