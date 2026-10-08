/*
 * play.h - the MIDI play task (mbplay.e)
 */
#ifndef MI_PLAY_H
#define MI_PLAY_H

#include <exec/types.h>
#include <exec/lists.h>

#include "banks.h"

enum {
	PSG_QUIT = 11111,
	PSG_CHANGE,
	PSG_PLAY,
	PSG_VOLUME,
	PSG_MINFO,
	PSG_TUNE
};

extern LONG maxchan, maskmaxchan;        /* highest channel, mask; from prefs */
extern LONG mcontrol, basesetb, rangesetb;
extern struct List mysrclist;            /* MIDI source names, "+ name" = used */
extern struct MRouteInfo minfo;

LONG getchannelnote(LONG f);
void whichbankisplaying(UBYTE *bf, struct bank *bd);
void clearcontrollers(void);
void install_playtask(void);            /* raises */
void deinstall_playtask(void);
BOOL signal_playtask(LONG type, APTR bn, LONG v);
WORD *getmidicontrolarray(LONG mcm);
void xchgbanksachn(struct bank *bank1, struct bank *bank2);

#endif
