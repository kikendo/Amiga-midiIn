/*
 * pianokeys.h - the piano keyboard plugin of the main window (mbgui.e)
 *
 * 128 keys (75 white) drawn with graphics.library. Shows the current key
 * (blinking while the base note is being set), the keys being played and
 * the key range of the bank, which can be set with the mouse.
 *
 * Add it with eg_plugin(action, &pianokeys_new(screen)->plugin). The action
 * is called with keycode set to:
 *   -1   a key was clicked (or a range bound set): read pianokeys_keypressed()
 *        or pianokeys_bounds()
 *   -2   the window became inactive while basesetb was set
 *   >=0  a key: vanilla code, or raw code | MYRAWCODE (| SHIFTQUAL)
 * The action should set keycode back to -1.
 */
#ifndef MI_PIANOKEYS_H
#define MI_PIANOKEYS_H

#include <exec/types.h>
#include <intuition/screens.h>

#include "egui.h"
#include "keycodes.h"

/* EXPORT ENUM of mbgui.e (not used by the plugin itself) */
enum { KB_NONE = 0, KB_FUN, KB_SPACE, KB_LOOP, KB_DUR, KB_RETURN, KB_PRI,
       KB_ELSE };

/* values of the rangeother array given to pianokeys_bounds() */
#define RANGE_LOPRI 1           /* key in a lower priority bank's range */
#define RANGE_HIPRI 2           /* key in a higher (or equal) one's */

struct pianokeys {
	struct EG_Plugin plugin;
	LONG keycode;           /* see above */

	/* private */
	struct RastPort *rport; /* NULL while not shown */
	LONG darkpen, activepen, backpen, bgrnpen, currentpen, shadowpen;
	LONG *ptrnalloc;        /* drawpattern() pens */
	LONG shinepen;
	WORD xm, ym;            /* mouse position from message_test */
	WORD xx, yy;            /* top left of the keys */
	WORD xsize;             /* white key width */
	WORD keysize, smallsize;        /* key height, black key height */
	WORD windowreport;      /* ReportMouse() on */
	UBYTE currentkey;       /* >127: none */
	UBYTE loarea, hiarea;   /* range; loarea >127: no range */
	UBYTE boundset;         /* mouse sets the range */
	UBYTE boundselect;      /* 0, 1 low bound being set, 2 high bound */
	UBYTE allocpens;        /* pens obtained in render, bit per pen */
	UBYTE status[128];      /* per key: state, and drawn state << 4 */
	UBYTE playingkeys[128]; /* count of notes playing per key */
};

/* NEW mp.init(screen): screen NULL = default public screen. Raises 'MEM'. */
struct pianokeys *pianokeys_new(struct Screen *screen);
/* frees it (also the class's dispose) */
void pianokeys_dispose(struct pianokeys *p);

/*
 * keypressed(key): returns the current key, or -1 if none. key >= 0
 * makes (key & 255) the current key; a value > 127 only redraws the current
 * key as not current, the current key stays. -1: only read.
 */
LONG pianokeys_keypressed(struct pianokeys *p, LONG key);

/*
 * bounds(l, h, rangeother): returns the range before the call: low bound,
 * high bound in *hi (hi may be NULL), or -1 (then *hi is -1) if there is
 * none. l = -1: only read (E default h = 127, rangeother = NULL).
 * l > 127: clears the range and stops range setting.
 * 0 <= l <= h: sets the range l..h. rangeother, if not NULL, is 128 bytes
 * of 0 / RANGE_LOPRI / RANGE_HIPRI marking other banks' ranges.
 */
LONG pianokeys_bounds(struct pianokeys *p, LONG l, LONG h,
                      const UBYTE *rangeother, LONG *hi);

/* set != 0: mouse clicks set the range bounds instead of the current key */
void pianokeys_boundset(struct pianokeys *p, LONG set);

/* note x (0..127) starts (on != 0) or stops playing; counted per key */
void pianokeys_setplaying(struct pianokeys *p, LONG x, LONG on);

/*
 * As a mouse click on key with button code (SELECTDOWN, SELECTUP): sets
 * the current key, or a range bound when boundset. Returns TRUE.
 */
LONG pianokeys_autokey(struct pianokeys *p, LONG key, LONG code);

/* background pattern data (pianokeys_data.c) */
extern const UBYTE pianokeys_pattern[10000];   /* 100 x 100 */
extern const ULONG pianokeys_patterncmap[];

#endif
