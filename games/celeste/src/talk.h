/* TalkComponent: a spot where Madeline can talk or interact, with its prompt above. */
#ifndef TALK_H
#define TALK_H
#include "entities.h"

typedef struct {
  int16_t bx, by, bw, bh;    /* Bounds, from the entity */
  V2 draw_at;                /* DrawAt */
  float cooldown, hover_timer, disable_delay, slide, timer, alpha;
  Wiggler wig;
  bool enabled, must_face, highlighted, inited;
} Talk;
extern Ent *g_talk_over;      /* TalkComponent.PlayerOver: whose spot Madeline stands in */

void talk_init(Talk *t, int x, int y, int w, int h, V2 draw_at);
/* TalkComponent.Update and its UI's: true on the update Madeline talks (OnTalk) */
bool talk_update(Talk *t, Ent *owner);
void talk_render(Talk *t, Ent *owner);   /* TalkComponentUI.Render (the interface's) */
void talk_removed(Ent *owner);           /* Dispose */
#endif
