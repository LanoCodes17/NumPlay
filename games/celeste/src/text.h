/* Dialog: Celeste's Textbox (with portraits) laying out its text like
 * FancyText, from the English dialog in data.bin. */
#ifndef TEXT_H
#define TEXT_H
#include "ent.h"

/* a {trigger n} in the dialog runs the cutscene's event n: called every frame until it returns true */
typedef bool (*TextEvent)(void *ctx, int index);

/* Textbox.Say: opens a textbox (an entity); it is done when textbox_opened() is false */
Ent *textbox_say(const char *key, TextEvent events, void *ctx);
bool textbox_opened(const Ent *tb);
bool textbox_portrait(int *bank, int *idle);   /* Textbox.PortraitName/PortraitAnimation: the portrait's bank, idle anim */
void textbox_set_frozen(Ent *tb);   /* SayWhileFrozen */
Ent *mini_textbox(const char *key);   /* new MiniTextbox(dialogId): at the top, while playing */

/* Dialog.Get / Dialog.Clean: the text of a key (cleaned of its commands), into buf */
int dialog_clean(const char *key, char *buf, int cap);
bool dialog_has(const char *key);
/* ActiveFont.Draw: s at (x, y) in interface units (1920x1080), justified, scaled; d must live until
 * the frame is drawn (its strips are drawn at gfx_end) */
typedef struct {
  const char *s;
  float x, y, jx, jy, scale;
  uint16_t col;
  uint8_t alpha;
} TextDraw;
void text_draw(const TextDraw *d);
float text_measure(const char *s);                             /* ActiveFont.Measure(s).X */
/* ActiveFont.Draw into a strip, from a gfx_custom callback (nothing kept: menus redraw their words per strip) */
void text_into(uint16_t *strip, int sy0, int sy1, const char *s, float x, float y, float jx, float jy, float scale,
               uint16_t col, uint8_t alpha);
const char *ui_str(const char *key);                           /* the menus' words (UISTR) */
void text_auto_newline(char *s, int cap, float width);        /* PixelFontSize.AutoNewline */
#endif
