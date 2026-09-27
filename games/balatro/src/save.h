#ifndef SAVE_H
#define SAVE_H
#include <stdint.h>
typedef struct {
  uint8_t speed;      /* 0: x0.5, 1: x1, 2: x2, 3: x4 */
  uint8_t pad;
  int16_t best_ante;
  int32_t wins, runs;
  double best_hand;
} settings_t;
extern settings_t S;
void save_write(int with_run);
int save_read(void);
#endif
