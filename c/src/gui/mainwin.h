/*
 * mainwin.h - the main window, the volume (details) and envelope windows
 * and the MIDI monitor window, with their menus (mbwindow.e)
 *
 * Defines these globals of globals.h: gh, volgh, envgh, mongh, menuptr,
 * mp, nbank, followb, askquit, currmcmon, notestr, basestr,
 * instrumenttext, timetext, monosliderstr, wintext.
 */
#ifndef MI_MAINWIN_H
#define MI_MAINWIN_H

#include <exec/types.h>
#include <intuition/screens.h>
#include <graphics/text.h>

#include "egui.h"
#include "pianokeys.h"

/*
 * Adds all windows to mh (main, audio and MIDI settings, volume,
 * envelope, MIDI monitor, scope) and shows the visible ones. Raises 'GUI'
 * if a window cannot be added, 'MEM' if a plugin cannot be made.
 */
void open_gui(struct Screen *defscreen, struct TextAttr *defta,
              struct TextFont *deffont);

/* menu action (also the commodity "hide"): closes the windows */
void hide(EG_Gui *g, EG_Obj *obj, LONG value);
/* brings the windows to front, or opens those hide() closed */
void show(void);

/*
 * The piano keys plugin's action; value is the struct pianokeys pointer.
 * The main loop calls it as plugact(NULL, NULL, (LONG)mp) after setting
 * mp->keycode = -1 (E: plugact(0, mp)).
 */
void plugact(EG_Gui *g, EG_Obj *obj, LONG value);

/* selects the next bank (after the current one) playing note on MIDI
 * channel midi (1-16) */
void follow(LONG note, LONG midi);

/* timer and MIDI updates of the main loop */
void updatechannelkeys(void);
void checkplayingbanks(void);
void updatemidimonitor(void);

/*
 * keypressed(key) of the piano keys, also showing the note in the main
 * window. key -1 (E's default): only read. Returns the current key or -1.
 */
LONG pianokeypressed(struct pianokeys *p, LONG key);

/* refreshes all gadgets of the main, volume and envelope windows */
void updategh(void);

#endif
