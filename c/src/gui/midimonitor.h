/*
 * midimonitor.h - MIDI controller monitor plugin, port of mbmmonit.e
 *
 * Shows one bar per MIDI channel (16) with the value of a controller
 * (0..16383, drawn from the middle) or "not available" (-1).
 *
 * The plugin's action is called with keycode set to the vanilla key
 * that was pressed: ESC_CODE or 9 (tab).
 */
#ifndef MI_MIDIMONITOR_H
#define MI_MIDIMONITOR_H

#include <exec/types.h>
#include <intuition/screens.h>
#include <graphics/rastport.h>
#include <graphics/text.h>

#include "egui.h"

struct mm_displaystatus {
	WORD value;
	WORD lastx;
	WORD mintxt;
	WORD maxtxt;
	WORD miny;
	WORD maxy;
	WORD txty;
};

struct midimonitor {
	struct EG_Plugin plugin;
	LONG keycode;                   /* read by the action, see above */
	/* the rest is private */
	struct RastPort *rport;         /* NULL while not rendered */
	LONG textpen;
	LONG fillpen;
	LONG shinepen;
	struct TextFont *font;
	WORD minx;
	WORD midx;
	WORD maxx;
	struct mm_displaystatus ds[16];
};

/* NEW midimon.init(screen, font); screen NULL: default public screen,
 * font NULL: the screen's font. Raises 'MEM'. Freed by egui (class
 * dispose) when its window is removed. */
struct midimonitor *midimonitor_new(struct Screen *screen,
                                    struct TextFont *font);
/* frees a plugin that was never given to a window */
void midimonitor_dispose(struct midimonitor *mm);

/* channel num 0..15 shows value (0..16383, -1: not available) */
void midimonitor_setstatus(struct midimonitor *mm, LONG num, LONG value);
/* mc: 16 values as given by getmidicontrolarray(); NULL: all -1 */
void midimonitor_update(struct midimonitor *mm, const WORD *mc);

#endif
