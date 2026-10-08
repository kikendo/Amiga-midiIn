/*
 * progressbar.c - progress bar plugin, port of mbprogressbar.e
 *
 * Differences from the E code:
 * - E's Box() and TextF() on stdrast are SetAPen + RectFill and
 *   Move + Text on the window's rastport (e_box, e_text below).
 * - If the screen's DrawInfo cannot be had and no font is given, the E
 *   code took the screen's TextAttr as a TextFont; here the graphics
 *   default font is used.
 * - The left text is only drawn when there is one (the E code could pass
 *   a NIL string to TextF there, which printed nothing).
 */
#include <string.h>
#include <exec/types.h>
#include <intuition/intuition.h>
#include <intuition/screens.h>
#include <graphics/gfx.h>
#include <graphics/gfxbase.h>
#include <graphics/rastport.h>
#include <graphics/regions.h>
#include <graphics/text.h>
#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <proto/layers.h>

#include "egui.h"
#include "progressbar.h"
#include "../app/eport.h"

static const struct EG_PluginClass progressbar_class;

static void e_box(struct RastPort *rp, LONG x1, LONG y1, LONG x2, LONG y2,
                  LONG c)
{
	SetAPen(rp, c);
	RectFill(rp, x1, y1, x2, y2);
}

static void e_text(struct RastPort *rp, LONG x, LONG y, CONST_STRPTR s)
{
	if (s) {
		Move(rp, x, y);
		Text(rp, s, strlen((const char *)s));
	}
}

static LONG textlen(struct RastPort *rp, CONST_STRPTR s)
{
	return TextLength(rp, s, strlen((const char *)s));
}

static void pb_min_size(struct EG_Plugin *p, struct TextAttr *ta, WORD fh,
                        WORD *w, WORD *h)
{
	(void)ta;
	(void)fh;
	*w = 100;
	*h = ((struct progressbar *)p)->font->tf_YSize;
}

static void pb_render(struct EG_Plugin *p, struct TextAttr *ta, WORD x,
                      WORD y, WORD xs, WORD ys, struct Window *win)
{
	struct progressbar *pb = (struct progressbar *)p;

	(void)ta;
	(void)x;
	(void)y;
	(void)xs;
	(void)ys;
	pb->rport = win->RPort;
	pb->win = win;
	progressbar_settext(pb, pb->text1, pb->text2, pb->progress, pb->full);
}

static void pb_clear_render(struct EG_Plugin *p, struct Window *win)
{
	(void)win;
	((struct progressbar *)p)->rport = NULL;
}

struct progressbar *progressbar_new(struct Screen *screen,
                                    struct TextFont *font)
{
	struct progressbar *pb;
	struct Screen *scr;
	struct DrawInfo *drinfo;
	LONG penless;
	UWORD *x;

	pb = e_new(sizeof(struct progressbar));
	pb->plugin.cls = &progressbar_class;
	pb->textpen = 1;
	pb->shinepen = 2;
	pb->backpen = 0;
	pb->fillpen = 3;
	pb->progress = 0;
	pb->full = 1;
	scr = screen ? screen : LockPubScreen(NULL);
	if (scr) {
		if ((drinfo = GetScreenDrawInfo(scr)) != NULL) {
			penless = drinfo->dri_NumPens;
			x = drinfo->dri_Pens;
			if (penless > BACKGROUNDPEN)
				pb->backpen = x[BACKGROUNDPEN];
			if (penless > TEXTPEN)
				pb->textpen = x[TEXTPEN];
			if (penless > FILLPEN)
				pb->fillpen = x[FILLPEN];
			if (penless > SHINEPEN)
				pb->shinepen = x[SHINEPEN];
			if (font == NULL)
				font = drinfo->dri_Font;
			FreeScreenDrawInfo(scr, drinfo);
		}
		if (!screen)
			UnlockPubScreen(NULL, scr);
	}
	if (font == NULL)
		font = GfxBase->DefaultFont;
	pb->font = font;
	return pb;
}

void progressbar_dispose(struct progressbar *pb)
{
	e_dispose(pb);
}

static void pb_class_dispose(struct EG_Plugin *p)
{
	e_dispose(p);
}

void progressbar_settext(struct progressbar *pb, CONST_STRPTR text1,
                         CONST_STRPTR text2, LONG progress, LONG full)
{
	struct RastPort *rp;
	struct Region *region;
	struct Rectangle rect;
	struct Layer *layer;
	LONG l = 0, k = 0, xp, t1x, t2x, xmax, ymax, ytx;
	LONG px = pb->plugin.x, py = pb->plugin.y, xs = pb->plugin.xs;

	if (text1)
		pb->text1 = text1;
	else
		text1 = pb->text1;
	if (text2)
		pb->text2 = text2;
	else
		text2 = pb->text2;
	if (full > 0) {
		while (full > 32767) {
			full >>= 1;
			progress >>= 1;
		}
		if (progress > full)
			progress = full;
		pb->progress = progress;
		pb->full = full;
	} else {
		progress = pb->progress;
		full = pb->full;
	}

	if ((rp = pb->rport) == NULL)
		return;

	xmax = px + xs - 1;
	ymax = py + pb->plugin.ys - 1;
	ytx = py + pb->font->tf_Baseline;
	/* cut texts from the left until they fit (measured in the current
	 * rastport font, before SetFont below, as in the E code) */
	if (text2) {
		while ((l = textlen(rp, text2)) > xs / 2)
			text2++;
	}
	if (text1) {
		while ((k = textlen(rp, text1)) > xs - l - 13)
			text1++;
	}
	t2x = xmax - l;
	t1x = px + 3;
	xp = (xs - 1) * progress / full + px;
	SetDrMd(rp, JAM2);
	SetFont(rp, pb->font);
	layer = pb->win->WLayer;
	if (xp > px) {
		if ((region = NewRegion()) != NULL) {
			/* the filled part: texts in inverse colours */
			rect.MinX = px;
			rect.MinY = py;
			rect.MaxX = xp;
			rect.MaxY = ymax;
			if (OrRectRegion(region, &rect)) {
				region = InstallClipRegion(layer, region);
			} else {
				DisposeRegion(region);
				return;
			}
			if (t1x > px)
				e_box(rp, px, py, t1x - 1, ymax, pb->textpen);
			SetBPen(rp, pb->textpen);
			SetAPen(rp, pb->shinepen);
			if (k > 0)
				e_text(rp, t1x, ytx, text1);
			if (xp >= t1x + k)
				e_box(rp, t1x + k, py, E_MIN(xp, t2x - 1), ymax,
				      pb->textpen);
			SetAPen(rp, pb->shinepen);
			if (l > 0 && t2x <= xp)
				e_text(rp, t2x, ytx, text2);
			region = InstallClipRegion(layer, region);
			ClearRegion(region);
			if (xp < xmax) {
				/* the empty part */
				rect.MinX = xp + 1;
				rect.MinY = py;
				rect.MaxX = xmax;
				rect.MaxY = ymax;
				if (OrRectRegion(region, &rect)) {
					region = InstallClipRegion(layer, region);
				} else {
					DisposeRegion(region);
					return;
				}
				SetAPen(rp, pb->textpen);
				SetBPen(rp, pb->backpen);
				if (t1x + k - 1 > xp)
					e_text(rp, t1x, ytx, text1);
				if (t2x > xp + 1)
					e_box(rp, E_MAX(xp + 1, t1x + k), py, t2x, ymax,
					      pb->backpen);
				SetAPen(rp, pb->textpen);
				if (l > 0)
					e_text(rp, t2x, ytx, text2);
				region = InstallClipRegion(layer, region);
				DisposeRegion(region);
			} else {
				DisposeRegion(region);
			}
		}
	} else {
		e_box(rp, t1x + k, py, t2x, ymax, pb->backpen);
		SetAPen(rp, pb->textpen);
		SetBPen(rp, pb->backpen);
		if (k > 0)
			e_text(rp, t1x, ytx, text1);
		if (l > 0)
			e_text(rp, t2x, ytx, text2);
	}
}

static const struct EG_PluginClass progressbar_class = {
	pb_min_size,
	NULL,                   /* will_resize: both */
	pb_render,
	pb_clear_render,
	NULL,                   /* message_test */
	NULL,                   /* message_action */
	pb_class_dispose,
};
