/*
 * titlekeys.h - title plugin that also takes keys, port of mbtitle.e and
 * of the title_plugin it extends (the binary E module title.m)
 *
 * Draws a text centred between two etched lines, the width of the area
 * (resizes horizontally only). Keys it takes:
 *   raw keys CURSORUP..KEYCODE_F10 (cursor keys, F1-F10):
 *       keycode = code | MYRAWCODE
 *   vanilla keys ESC, 0-9 . _ ) ( - + = *, tab, return, space:
 *       keycode = the character
 * and then calls the plugin's action.
 */
#ifndef MI_TITLEKEYS_H
#define MI_TITLEKEYS_H

#include <exec/types.h>
#include <intuition/intuition.h>
#include <graphics/text.h>

#include "egui.h"

/* text styles (values as in title.m) */
#define TITLE_NORMAL    0               /* text in pen 1 */
#define TITLE_HIGHLIGHT 1               /* text in pen 2 */
#define TITLE_THREEDEE  2               /* pen 2 over a pen 1 shadow */

struct titlekeys {
	struct EG_Plugin plugin;
	LONG keycode;                   /* read by the action, see above */
	/* the rest is private */
	struct Window *win;
	struct TextAttr *font;          /* the gui's font unless given */
	BOOL hasfont;
	CONST_STRPTR text;              /* not copied */
	LONG textlen;                   /* text width, from min_size */
	LONG textheight;
	LONG type;
};

/* NEW t.setup(text, type, font); font NULL: the gui's font. Raises 'MEM'.
 * Freed by egui (class dispose) when its window is removed. */
struct titlekeys *titlekeys_new(CONST_STRPTR text, LONG type,
                                struct TextAttr *font);
/* frees a plugin that was never given to a window */
void titlekeys_dispose(struct titlekeys *t);

#endif
