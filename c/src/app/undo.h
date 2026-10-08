/*
 * undo.h - undo/redo (mbundo.e). Undo records are also saved in project
 * files, so their layout must not change.
 */
#ifndef MI_UNDO_H
#define MI_UNDO_H

#include <exec/types.h>
#include <exec/lists.h>

#include "banks.h"

enum {
	UNDO_PREP_SAMPLELIST = 1,
	UNDO_SET_SAMPLELIST,
	UNDO_PREP_SAMPLELISTINSTR,
	UNDO_SET_SAMPLELISTINSTR,
	UNDO_PREP_ALL,
	UNDO_SET_ALL,
	UNDO_SET_BANKS,
	UNDO_SET_XCHGBANKS,
	UNDO_SET_SAMPLEDELETE,
	UNDO_SET_SAMPLEUNDELETE,
	UNDO_SET_INSTR,
	UNDO_SET_MIDI,
	UNDO_SET_PRI,
	UNDO_SET_BASE,
	UNDO_SET_FINE,
	UNDO_SET_SET_LOOP,
	UNDO_SET_SET_DUR,
	UNDO_SET_SET_MONO,
	UNDO_SET_SET_ADDAFTERT,
	UNDO_SET_BOUNDS,
	UNDO_SET_VOLUME,
	UNDO_SET_VELSENS,
	UNDO_SET_RELEASE,
	UNDO_SET_PANORAMA,
	UNDO_SET_PANWIDE,
	UNDO_SET_PITCHSENS,
	UNDO_SET_ATTACK,
	UNDO_SET_DECAY,
	UNDO_SET_SUSTAINLEV,
	UNDO_SET_AFTERSENS,
	UNDO_SET_FIRSTSKIP,
	UNDO_SET_MCTRLVOL,
	UNDO_SET_MCTRLPAN,
	UNDO_SET_GROUP,
	UNDO_SET_MONOVSENS,
	UNDO_SET_MONOSLIDE,
	UNDO_SET_MAX
};

void init_undo(LONG max);               /* raises 'MEM' */
void flush_undo(void);
void free_undo(void);
void remembersaveundo(void);
BOOL asksaveundo(void);                 /* TRUE: nothing changed since save */
LONG undoleft(void);
LONG nextundo(void);                    /* type of the next undo, 0 = none */
LONG do_undo(struct bank *banks, struct List *slist);   /* raises */
LONG do_redo(struct bank *banks, struct List *slist);   /* raises */

/* project files: walk the records (node NULL to start), returning the
 * record data, its size and checksum; NULL at the end */
APTR exportundo(APTR node, LONG *size, LONG *cks);
UBYTE *importundo(UBYTE *ptr, LONG cksum, LONG numb, LONG bsize);

void set_undo(LONG type, APTR obj, LONG data, LONG dat2, LONG dat3);

#endif
