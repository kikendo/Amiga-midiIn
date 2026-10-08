/*
 * leds.h - the bank "LEDs" plugin, port of MBLeds.e
 *
 * Shows NUMBANKS banks as two rows of LEDs: the current bank, enabled
 * banks, banks in the active set (inner or full) and the bank range.
 * Clicking a LED selects a bank; dragging from the current bank sets a
 * range; dragging inside the range moves it.
 *
 * The plugin's action is called with act set to:
 *   LEDACT_SETBANK    a bank was clicked: leds_setcurrent(l, -1) gives it
 *   LEDACT_SETRANGE   a new range: leds_getrange(l, &lo, &hi)
 *   LEDACT_MOVERANGE  the range was dragged: leds_getrange(l, NULL, NULL)
 *                     gives the new first bank
 */
#ifndef MI_LEDS_H
#define MI_LEDS_H

#include <exec/types.h>
#include <intuition/intuition.h>
#include <intuition/screens.h>
#include <graphics/rastport.h>

#include "egui.h"
#include "../app/banks.h"

/* leds_setactive() types */
#define LSETNONE  0
#define LSETINNER 1
#define LSETFULL  2

/* act values */
#define LEDACT_SETBANK   1
#define LEDACT_SETRANGE  2
#define LEDACT_MOVERANGE 3

struct leds {
	struct EG_Plugin plugin;
	LONG act;                       /* read by the action, see above */
	/* the rest is private */
	struct RastPort *rport;         /* NULL while not rendered */
	struct Window *win;
	WORD select;                    /* bank under the mouse, -1 none */
	WORD dragpos;                   /* first bank of the dragged range */
	WORD ledx;                      /* LED pitch */
	WORD ledh;                      /* LED height */
	WORD margl, margt;
	WORD nextrow;                   /* y offset of the second row */
	UBYTE current;
	UBYTE lorange;                  /* [==== */
	UBYTE hirange;                  /* ====] */
	UBYTE movement;                 /* none, setting range, dragging */
	UBYTE movepos;
	UBYTE bankstat[NUMBANKS];
	UBYTE colrinner;
	UBYTE colrback;
	UBYTE colrcurrent;
	UBYTE colrset;
};

/* NEW leds.init(screen); screen NULL: default public screen. Raises
 * 'MEM'. Freed by egui (class dispose) when its window is removed. */
struct leds *leds_new(struct Screen *screen);
/* frees a plugin that was never given to a window */
void leds_dispose(struct leds *l);

/* v 0..NUMBANKS-1: make it the current bank and the range; v -1: only
 * read. Returns the current bank. */
LONG leds_setcurrent(struct leds *l, LONG v);
/* v -1: all banks */
void leds_setenabled(struct leds *l, LONG v, BOOL enable);
/* v -1: all banks; typ LSETNONE, LSETINNER or LSETFULL */
void leds_setactive(struct leds *l, LONG v, LONG typ);
/* lo, hi may be NULL; returns the first bank of the dragged range */
LONG leds_getrange(struct leds *l, LONG *lo, LONG *hi);
/* lo -1, hi -1 (E's default): only redraw the range and the drag frame */
void leds_setrange(struct leds *l, LONG lo, LONG hi);

#endif
