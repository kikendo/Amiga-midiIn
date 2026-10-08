/*
 * globals.h - program-wide variables shared between modules (the EXPORT
 * DEFs of midiIn.e, mbwindow.e and friends). Defined in midiin.c unless
 * noted.
 */
#ifndef MI_GLOBALS_H
#define MI_GLOBALS_H

#include <exec/types.h>
#include <exec/lists.h>
#include <intuition/intuition.h>
#include <graphics/text.h>

#include "banks.h"
#include "diskoper.h"
#include "../gui/egui.h"

struct pianokeys;

extern struct bank bd[NUMBANKS];        /* the banks */
extern LONG nbank;                      /* current bank 0..NUMBANKS-1 */
extern struct List smplist;             /* struct sfx list (lln) */
extern UBYTE keybchannels[32];          /* note per channel for display */

extern struct mbprefs mbprefs;
extern char prjname[32];                /* current project file name */
/* sndpath, prjpath: diskoper.c; mysrclist, minfo, maxchan, maskmaxchan,
 * mcontrol, basesetb, rangesetb: play.c; cxhotkey: report.c */

extern STRPTR mainbartext;              /* screen title for all windows */
extern char instrumenttext[32];
extern char timetext[16];
extern char wintext[80];
extern char monosliderstr[20];
extern char notestr[8];
extern char basestr[8];

extern LONG followb;                    /* follow MIDI notes */
extern LONG mareaset;                   /* range setting from MIDI */
extern LONG askquit;                    /* project changed */
extern LONG currmcmon;                  /* MIDI controller shown in monitor */
extern struct MDest *dest;              /* main task's MIDI dest */

extern struct Screen *defscreen;
extern struct TextAttr *defta;
extern struct TextFont *deffont;

extern EG_Multi *mh;
extern EG_Gui *gh;                      /* main window */
extern EG_Gui *volgh;                   /* volume */
extern EG_Gui *envgh;                   /* envelope */
extern EG_Gui *scopegh;                 /* scopes */
extern EG_Gui *mongh;                   /* MIDI monitor */
extern EG_Gui *setgh;                   /* MIDI settings */
extern EG_Gui *saugh;                   /* audio settings */
extern struct Menu *menuptr;            /* main window menu strip */
extern struct pianokeys *mp;            /* piano keys plugin */

extern ULONG timestart;                 /* program start (seconds) */

#endif
