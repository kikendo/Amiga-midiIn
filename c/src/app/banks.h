/*
 * banks.h - instrument banks and the sample list (mbbanks.e)
 *
 * struct bank is saved in project files as it is; its layout (32 bytes)
 * must not change.
 */
#ifndef MI_BANKS_H
#define MI_BANKS_H

#include <exec/types.h>
#include <exec/lists.h>

#include "sfx.h"

#define LEFTFIX  0                      /* obsolete bank types */
#define RIGHTFIX 1
#define ANYFIX   2
#define TYPE_VERSION_32 0x80

#define B_LOOP      1
#define B_DUR_ON    2
#define B_DRUM      4
#define B_MONO      8
#define B_ADDAFTERT 16

#define FINE_CENTR 116
#define NUMBANKS   60

enum { SORT_PRI, SORT_MIDI, SORT_NAME, SORT_RANGE };

struct bank {
	struct sfx *instr;      /* in files and undo data: 1-based sample number */
	UBYTE midi;             /* 1-16 */
	UBYTE pri;              /* 1-60 */
	UBYTE type;             /* TYPE_VERSION_32 */
	UBYTE base;             /* base note, 60 = C */
	UBYTE fine;             /* fine - FINE_CENTR = finetune -100..100 */
	UBYTE set;              /* B_ flags */
	UBYTE lobound;
	UBYTE hibound;
	WORD volume;            /* 0-512, 512 = 200% */
	UBYTE velsens;          /* 0-100 */
	UBYTE release;          /* tenths of a second */
	WORD panorama;          /* 0-256, 128 = centre */
	UBYTE panwide;          /* 0-30 */
	UBYTE pitchsens;        /* 0-12 */
	UBYTE attack;           /* tenths of a second */
	UBYTE decay;            /* tenths of a second */
	UBYTE sustainlev;       /* 0-255 */
	UBYTE aftersens;        /* 0-100 */
	WORD firstskip;         /* 0-3000 */
	UBYTE mctrlvol;         /* 0 or 100 */
	UBYTE mctrlpan;         /* 0 or 100 */
	UBYTE group;            /* 0 = none */
	UBYTE monovsens;        /* 0-100 */
	WORD monoslide;         /* 0-1500 */
};

extern struct bank *prilist[NUMBANKS];  /* banks sorted by priority */

void lockbanksaccess(void);
void releasebanksaccess(void);
LONG cmpbanks(struct bank *bn1, struct bank *bn2, LONG type);
struct sfx *clearinstr(struct bank *bn);
BOOL setinstr(struct bank *bn, struct sfx *snd);
BOOL setinstrname(struct bank *bn, struct List *slist, CONST_STRPTR instrname);
void xchgbanks(struct bank *bn1, struct bank *bn2);
BOOL setbank(struct bank *bn, struct bank *fbn);
void deletebank(struct bank *bn);
void initbanks(struct bank *bd);
void sortbanks(void);
BOOL checkbank(struct bank *bn);
struct sfx *addsnd(CONST_STRPTR name, struct List *slist, BOOL quiet, BOOL *added);
void addingsamplesover(void);
BOOL delsnd(struct sfx *snd, struct bank *banks, BOOL notused);
BOOL clearsmplist(struct bank *bn, struct List *slist, BOOL notused);

#endif
