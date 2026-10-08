/*
 * scopes.h - the oscilloscope window (mbscopes.e)
 */
#ifndef MI_SCOPES_H
#define MI_SCOPES_H

#include <exec/types.h>
#include <intuition/screens.h>
#include <graphics/text.h>

#include "egui.h"

/* adds the scope window to mh (hidden if mbprefs.scopehide), sets
 * scopegh and starts the drawing process; raises 'GUI' or 'TASK' */
void open_scopewindow(struct Screen *screen, struct TextAttr *ta);

/* menu action: open the scope window or bring it to front */
void scopewindowopen(EG_Gui *g, EG_Obj *obj, LONG value);

#endif
