/*
 * envelope.h - volume envelope editor plugin, port of MBenvelope.e
 *
 * Shows attack, decay, sustain level and release as a graph with the
 * values below it. A click in the graph picks the part under the mouse;
 * moving the mouse changes it, holding the left button still raises it
 * and holding the right button lowers it.
 *
 * The plugin's action is called with keycode set to:
 *   -1  the user changed the envelope: read it with envel_getenvelope()
 *   -2  editing was cancelled: set the bank's envelope again
 *  >=0  a key: vanilla code, or raw code OR MYRAWCODE ($100)
 */
#ifndef MI_ENVELOPE_H
#define MI_ENVELOPE_H

#include <exec/types.h>
#include <intuition/screens.h>
#include <graphics/rastport.h>

#include "egui.h"

#define ENVEL_COORDSNUM 5

struct envel_coord {
	WORD x, y;
};

struct envel_plugin {
	struct EG_Plugin plugin;
	LONG keycode;                   /* read by the action, see above */
	/* the rest is private */
	struct RastPort *rport;         /* NULL while not rendered */
	LONG textpen;
	LONG paperpen;
	LONG barpen;
	WORD x1, x2, y1, y2;            /* graph area */
	WORD ssize;                     /* width of "100%" */
	WORD bottomtexty;
	WORD minw;                      /* font width */
	struct envel_coord coords[ENVEL_COORDSNUM];
	UBYTE t1, t2, slev, t3;         /* attack, decay, sustain, release */
	WORD xm, ym;                    /* mouse position */
	WORD whichm;                    /* value being edited */
	WORD lastm;
	WORD secposx;
	WORD wait;
};

/* NEW envp.init(screen); screen NULL: default public screen. Raises
 * 'MEM'. Freed by egui (class dispose) when its window is removed. */
struct envel_plugin *envel_new(struct Screen *screen);
/* frees a plugin that was never given to a window */
void envel_dispose(struct envel_plugin *e);

/* values 0..255; redraws if shown */
void envel_setenvelope(struct envel_plugin *e, LONG attack, LONG decay,
                       LONG sustain, LONG release);
/* any pointer may be NULL */
void envel_getenvelope(struct envel_plugin *e, LONG *attack, LONG *decay,
                       LONG *sustain, LONG *release);

#endif
