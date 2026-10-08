/*
 * setup.h - preferences, projects with settings, the advanced MIDI and
 * audio windows and the MIDI routes of the main task (mbsetup.e)
 */
#ifndef MI_SETUP_H
#define MI_SETUP_H

#include <exec/types.h>
#include <exec/lists.h>
#include <intuition/screens.h>
#include <graphics/text.h>

#include "egui.h"
#include "../app/banks.h"

extern LONG msgflags;           /* MMF_ message filter of the routes */
extern LONG chanflags;          /* MIDI channel filter, bit 0 = channel 1 */

/* initialises mysrclist, loads the settings (and project pname, may be
 * ""), sets the audio engine up and makes the MIDI routes */
void initprefs(CONST_STRPTR pname, struct bank *bnk);

/* menu actions */
void menu_setlayout(EG_Gui *g, EG_Obj *obj, LONG value);
void menu_setwithprojects(EG_Gui *g, EG_Obj *obj, LONG value);
void menu_setsaveicons(EG_Gui *g, EG_Obj *obj, LONG value);
void menu_setsaveundo(EG_Gui *g, EG_Obj *obj, LONG value);
void menu_settings(EG_Gui *g, EG_Obj *obj, LONG value);
void menu_advancedmidi(EG_Gui *g, EG_Obj *obj, LONG value);
void menu_advancedaudio(EG_Gui *g, EG_Obj *obj, LONG value);

/* project save/load with the current settings; raise on errors */
void saveproject(CONST_STRPTR name, struct List *slist, struct bank *bnk);
void loadproject(CONST_STRPTR name, struct List *slist, struct bank *bnk);

/* the two settings windows: added hidden to mh; raise 'GUI' */
void open_sauwin(struct Screen *screen, struct TextAttr *ta);
void open_setwin(struct Screen *screen, struct TextAttr *ta);
void close_setwin(EG_Gui *g, EG_Obj *obj, LONG value);
void close_sauwin(EG_Gui *g, EG_Obj *obj, LONG value);

/* deletes the main task's MIDI routes */
void freeallMRoutes(void);

/* frees mysrclist's nodes and the AHI frequency table (the E runtime
 * freed them at exit); call at exit after freeallMRoutes() and
 * deinstall_playtask() */
void freeprefs(void);

#endif
