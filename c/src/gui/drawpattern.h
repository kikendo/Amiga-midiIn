/*
 * drawpattern.h - fills an area with a tiled chunky pattern (drawpattern.e)
 */
#ifndef MI_DRAWPATTERN_H
#define MI_DRAWPATTERN_H

#include <exec/types.h>
#include <graphics/rastport.h>
#include <graphics/view.h>

/*
 * Draws the pw * ph pattern (one byte per pixel, colour index into ctab)
 * centred and tiled into x, y, w * h of rp, with pens obtained exactly
 * from cm (graphics.library V39+). ctab is a LoadRGB32 style record:
 * count << 16, then 32 bit R, G, B per colour.
 * Returns the pen table, to be given to freepattern() when the pens are no
 * longer needed, or NULL on failure (nothing drawn, or partly; no pens kept).
 */
LONG *drawpattern(struct RastPort *rp, struct ColorMap *cm, LONG x, LONG y,
                  LONG w, LONG h, const ULONG *ctab, const UBYTE *pattern,
                  LONG pw, LONG ph);

/* releases the pens and frees the table; NULL ok. Returns NULL. */
LONG *freepattern(LONG *coltab);

#endif
