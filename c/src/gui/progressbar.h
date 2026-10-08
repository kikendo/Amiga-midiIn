/*
 * progressbar.h - progress bar plugin, port of mbprogressbar.e
 *
 * A bar filled from the left in proportion progress/full, with one text
 * at its left end and one at its right end, drawn in inverse colours
 * where the bar covers them.
 */
#ifndef MI_PROGRESSBAR_H
#define MI_PROGRESSBAR_H

#include <exec/types.h>
#include <intuition/intuition.h>
#include <intuition/screens.h>
#include <graphics/rastport.h>
#include <graphics/text.h>

#include "egui.h"

struct progressbar {
	struct EG_Plugin plugin;
	/* all private */
	struct RastPort *rport;         /* NULL while not rendered */
	struct Window *win;
	struct TextFont *font;
	CONST_STRPTR text1;             /* left text */
	CONST_STRPTR text2;             /* right text */
	WORD progress;
	WORD full;
	LONG textpen;
	LONG shinepen;
	LONG backpen;
	LONG fillpen;
};

/* NEW prsbar.init(screen, font); screen NULL: default public screen, font
 * NULL: the screen's font. Raises 'MEM'. Freed by egui (class dispose)
 * when its window is removed. */
struct progressbar *progressbar_new(struct Screen *screen,
                                    struct TextFont *font);
/* frees a plugin that was never given to a window */
void progressbar_dispose(struct progressbar *pb);

/*
 * Sets the texts (NULL: keep the old one; the strings are not copied and
 * must stay valid) and the progress (full <= 0: keep the old values), and
 * redraws if shown.
 */
void progressbar_settext(struct progressbar *pb, CONST_STRPTR text1,
                         CONST_STRPTR text2, LONG progress, LONG full);

#endif
