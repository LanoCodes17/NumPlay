/* Everything around the table: the bathroom, the hallway, the waiver, the
 * revival, the endings and Double or Nothing's offer. */
#ifndef BR_STORY_H
#define BR_STORY_H
#include <stdint.h>

int story_intro(void);          /* the bathroom: door (story) or pills (Double or Nothing); -1 to leave */
void story_enter(int mode);     /* hallway, the Dealer, the waiver */
void story_revive(void);        /* died in round 1 or 2: the same round again */
void story_retry(void);         /* RETRY in heaven: back to the final round */
int story_death(void);          /* died for good: 1 to try again */
int story_double_or_nothing(void);
void story_ending(void);
char *story_money(char *out, uint64_t v);
#endif
