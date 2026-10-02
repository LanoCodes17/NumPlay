/* Screen wipes: ScreenWipe and the chapters' own (AreaData.Wipe). */
#ifndef WIPE_H
#define WIPE_H
#include "celeste.h"

enum { WIPE_FADE, WIPE_SPOTLIGHT, WIPE_CURTAIN, WIPE_ANGLED, WIPE_DREAM, WIPE_KEYDOOR, WIPE_WIND, WIPE_DROP, WIPE_FALL,
       WIPE_MOUNTAIN, WIPE_HEART, WIPE_STARFIELD };
typedef void (*WipeDone)(void);
typedef struct {
  uint8_t type;
  bool in, active, completed, linear;
  float percent, duration, end_timer, modifier;
  V2 focus;          /* SpotlightWipe.FocusPoint, screen pixels */
  WipeDone done;
} Wipe;
extern Wipe g_wipe;   /* Level.Wipe */

void wipe_start(int type, bool in, WipeDone done);
void wipe_area(bool in, WipeDone done);   /* Level.DoScreenWipe */
void wipe_update(void);
void wipe_render(void);
#endif
