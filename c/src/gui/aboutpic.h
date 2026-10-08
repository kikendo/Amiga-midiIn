/*
 * aboutpic.h - about picture plugin, port of mbabout.e
 *
 * A 256x192 picture (64 colours, pens obtained on V39+, else drawn in
 * three screen pens) under a one-line text scroller. The scroller moves on
 * every tick; dragging the mouse to the left over it speeds it up.
 *
 * The plugin's action is called on a key, on a click outside the scroller
 * (or any other button) and when the window becomes inactive: midiIn
 * closes the window then.
 */
#ifndef MI_ABOUTPIC_H
#define MI_ABOUTPIC_H

#include <exec/types.h>
#include <intuition/screens.h>
#include <graphics/gfx.h>
#include <graphics/text.h>

#include "egui.h"

struct aboutpicture {
	struct EG_Plugin plugin;
	/* private */
	struct BitMap *tmpbitmap;       /* one-line bitmap for WritePixelLine8 */
	UBYTE colors[64];               /* picture colour -> pen */
	struct TextFont *font;
	struct Region *oldregion;       /* clip region before render */
	LONG fillpen;
	LONG shinepen;
	LONG highlightpen;
	WORD gfxversion;
	WORD obtainpenflag;             /* colors[] hold obtained pens */
	WORD regionbottom;              /* scroller strip: x,y - right,bottom */
	WORD regionright;
	WORD texty;
	WORD txtoffset;                 /* next character to show */
	WORD xd;
	WORD lsx;
	WORD dx;                        /* scroll step */
	WORD mx;                        /* last mouse x */
};

/* NEW aboutpict.init(screen); screen NULL: default public screen. Raises
 * 'MEM'. Freed by egui (class dispose, E's END) when its window is
 * removed. */
struct aboutpicture *aboutpic_new(struct Screen *screen);
/* frees a plugin that was never given to a window */
void aboutpic_dispose(struct aboutpicture *a);

#endif
