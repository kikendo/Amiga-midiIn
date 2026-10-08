/*
 * aboutpic.c - about picture plugin, port of mbabout.e
 *
 * The INCBIN data (abouttext.bin, bodyraw.bin, cmapraw.bin) is in the
 * generated aboutdata.c (c/tools/bin2c.py). The picture is decoded by the
 * CPU and drawn with WritePixelLine8() through a one-line bitmap from
 * AllocBitMap()/AllocRaster() (Chip RAM), so the C arrays need no copy in
 * Chip RAM.
 *
 * Differences from the E code:
 * - The inline assembly (ByteRun1 unpacking, planar to chunky, colour
 *   lookup, 8 to 32 bit colour values) is C with the same results.
 * - If ObtainBestPen() fails part way, the pens obtained so far are
 *   released (E kept them); the fallback colours are the same.
 * - clear_render() clears obtainpenflag after releasing the pens, so the
 *   pens are not released twice.
 * - Without screen DrawInfo the screen's TextFont is used (E took the
 *   screen's TextAttr pointer as a font).
 * - If init fails ('MEM') the object is freed before the exception.
 */
#include <exec/types.h>
#include <exec/memory.h>
#include <intuition/intuition.h>
#include <intuition/screens.h>
#include <graphics/gfx.h>
#include <graphics/gfxbase.h>
#include <graphics/rastport.h>
#include <graphics/regions.h>
#include <graphics/text.h>
#include <graphics/view.h>
#include <utility/tagitem.h>
#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <proto/layers.h>

#include "egui.h"
#include "aboutpic.h"
#include "../app/eport.h"

#define P_WIDTH  256
#define P_HEIGHT 192
#define P_DEPTH  6

/* aboutdata.c */
extern const UBYTE about_scrollertxt[];  /* NUL terminated */
extern const UBYTE about_picturebody[];  /* ByteRun1, 6 planes per row */
extern const UBYTE about_palette[];      /* 64 * r,g,b */

/* order in which the colours are obtained (most important first) */
static const UBYTE order[64] = {
	1, 2, 0, 7, 8, 6, 19, 3, 27, 52, 18, 30, 51, 58, 14, 4, 29, 61, 5, 10,
	35, 63, 23, 9, 12, 21, 40, 17, 57, 22, 47, 42, 62, 15, 11, 44, 32, 24,
	31, 59, 25, 48, 13, 34, 26, 43, 38, 55, 56, 28, 36, 39, 50, 33, 54, 46,
	20, 53, 41, 45, 49, 60, 16, 37
};

/* ------------------------------------------------------------- methods */

static ULONG ab_will_resize(struct EG_Plugin *p)
{
	(void)p;
	return 0;
}

static void ab_min_size(struct EG_Plugin *p, struct TextAttr *ta, WORD fh,
                        WORD *w, WORD *h)
{
	(void)p;
	(void)ta;
	*w = P_WIDTH;
	*h = P_HEIGHT + fh;
}

/* unpacks one ByteRun1 row of P_WIDTH / 8 * P_DEPTH bytes into dst;
 * returns the next source byte */
static const UBYTE *unpackrow(const UBYTE *src, UBYTE *dst)
{
	UBYTE *end = dst + P_WIDTH / 8 * P_DEPTH;
	WORD n;
	UBYTE b;

	while (dst < end) {
		n = *src++;
		if (n < 128) {
			do
				*dst++ = *src++;
			while (--n >= 0);
		} else {
			n = (UBYTE)-n;          /* NEG.B as in E: $80 gives 129 */
			b = *src++;
			do
				*dst++ = b;
			while (--n >= 0);
		}
	}
	return src;
}

/* planes (P_WIDTH / 8 bytes each) to pens via colors[] */
static void chunkyrow(const UBYTE *planes, UBYTE *out, const UBYTE *colors)
{
	WORD col, j;
	UBYTE p0, p1, p2, p3, p4, p5, c;

	for (col = 0; col < P_WIDTH / 8; col++) {
		p0 = planes[col];
		p1 = planes[P_WIDTH / 8 + col];
		p2 = planes[P_WIDTH / 8 * 2 + col];
		p3 = planes[P_WIDTH / 8 * 3 + col];
		p4 = planes[P_WIDTH / 8 * 4 + col];
		p5 = planes[P_WIDTH / 8 * 5 + col];
		for (j = 0; j < 8; j++) {
			c = 0;
			if (p0 & 0x80)
				c |= 1;
			if (p1 & 0x80)
				c |= 2;
			if (p2 & 0x80)
				c |= 4;
			if (p3 & 0x80)
				c |= 8;
			if (p4 & 0x80)
				c |= 16;
			if (p5 & 0x80)
				c |= 32;
			p0 <<= 1;
			p1 <<= 1;
			p2 <<= 1;
			p3 <<= 1;
			p4 <<= 1;
			p5 <<= 1;
			*out++ = colors[c];
		}
	}
}

/* 8 bit colour value to 32 bit */
static ULONG rgb32(UBYTE v)
{
	return (ULONG)v * 0x01010101UL;
}

static void ab_render(struct EG_Plugin *p, struct TextAttr *ta, WORD x,
                      WORD y, WORD xs, WORD ys, struct Window *win)
{
	struct aboutpicture *a = (struct aboutpicture *)p;
	struct ColorMap *cmap;
	const UBYTE *src, *ptr;
	struct RastPort temprp, *rp;
	UBYTE array[P_WIDTH], tmparray[P_WIDTH];
	struct Region *region;
	struct Rectangle rect;
	struct TextExtent txex;
	LONG n, r, colour;
	static const struct TagItem obptags[] = {
		{ OBP_Precision, PRECISION_EXACT },
		{ TAG_DONE, 0 }
	};

	(void)ta;
	(void)ys;
	ptr = about_palette;
	cmap = win->WScreen->ViewPort.ColorMap;
	if (a->gfxversion >= 39) {
		a->obtainpenflag = TRUE;
		for (n = 0; n <= 63; n++) {
			const UBYTE *c = ptr + order[n] * 3;

			colour = ObtainBestPenA(cmap, rgb32(c[0]), rgb32(c[1]),
			                        rgb32(c[2]), (struct TagItem *)obptags);
			if (colour == -1) {
				/* give back what was obtained */
				while (--n >= 0)
					ReleasePen(cmap, a->colors[order[n]]);
				break;
			}
			a->colors[order[n]] = colour;
		}
	} else {
		colour = -1;
	}
	if (colour == -1) {
		for (n = 0; n <= 63; n++)
			a->colors[n] = a->fillpen;
		a->colors[1] = a->highlightpen;
		a->colors[2] = a->shinepen;
		a->obtainpenflag = FALSE;
	}

	src = about_picturebody;
	rp = win->RPort;
	if (a->font)
		SetFont(rp, a->font);
	a->regionbottom = y + rp->TxHeight - 1;
	a->regionright = x + xs - 1;
	a->texty = y + rp->TxBaseline;
	SetAPen(rp, a->fillpen);
	RectFill(rp, x, y, a->regionright, a->regionbottom);

	CopyMem(rp, &temprp, sizeof(struct RastPort));
	temprp.Layer = NULL;
	temprp.BitMap = a->tmpbitmap;
	for (n = 0; n <= P_HEIGHT - 1; n++) {
		src = unpackrow(src, tmparray);
		chunkyrow(tmparray, array, a->colors);
		WritePixelLine8(rp, x, a->regionbottom + n + 1, P_WIDTH, array,
		                &temprp);
	}

	/* only the scroller strip is drawn from now on */
	if ((region = NewRegion())) {
		rect.MinX = x;
		rect.MinY = y;
		rect.MaxX = a->regionright;
		rect.MaxY = a->regionbottom;
		if (OrRectRegion(region, &rect)) {
			region = InstallClipRegion(win->WLayer, region);
		} else {
			DisposeRegion(region);
			region = NULL;
		}
	}
	a->oldregion = region;
	SetAPen(rp, a->highlightpen);
	SetBPen(rp, a->fillpen);
	/* the scroller text shown before, right aligned */
	if ((n = a->txtoffset)) {
		if ((r = TextFit(rp, about_scrollertxt + n - 1, n, &txex, NULL, -1,
		                 a->regionright + a->xd - x + 2, 32767))) {
			Move(rp, a->regionright + a->xd - txex.te_Width + 1,
			     a->texty);
			Text(rp, about_scrollertxt + n - r, r);
		}
	}
}

static void ab_clear_render(struct EG_Plugin *p, struct Window *win)
{
	struct aboutpicture *a = (struct aboutpicture *)p;
	struct ColorMap *cmap;
	struct Region *region;
	WORD i;

	if (a->obtainpenflag) {
		cmap = win->WScreen->ViewPort.ColorMap;
		for (i = 0; i <= 63; i++)
			ReleasePen(cmap, a->colors[i]);
		a->obtainpenflag = FALSE;
	}
	if ((region = InstallClipRegion(win->WLayer, a->oldregion)))
		DisposeRegion(region);
}

static BOOL ab_message_test(struct EG_Plugin *p, struct IntuiMessage *imsg,
                            struct Window *win)
{
	struct aboutpicture *a = (struct aboutpicture *)p;
	ULONG class = imsg->Class;
	LONG dx, x, y;

	if (class & (IDCMP_INTUITICKS | IDCMP_RAWKEY | IDCMP_VANILLAKEY |
	             IDCMP_INACTIVEWINDOW)) {
		return TRUE;
	} else if (class & IDCMP_MOUSEMOVE) {
		/* moving left speeds up the scroller */
		x = imsg->MouseX;
		if ((dx = a->mx - x) <= 0)
			dx = 1;
		if (dx > 25)
			dx = 25;
		a->mx = x;
		a->dx = dx;
		return dx > 1;
	} else if (class & IDCMP_MOUSEBUTTONS) {
		x = imsg->MouseX;
		y = imsg->MouseY;
		if (imsg->Code == SELECTDOWN) {
			if (x >= a->plugin.x && x <= a->regionright &&
			    y >= a->plugin.y && y <= a->regionbottom) {
				a->mx = x;
				ReportMouse(TRUE, win);
				return FALSE;
			}
		} else if (imsg->Code == SELECTUP) {
			ReportMouse(FALSE, win);
			return FALSE;
		}
		return TRUE;
	}
	return FALSE;
}

static BOOL ab_message_action(struct EG_Plugin *p, ULONG class, UWORD qual,
                              UWORD code, struct Window *win)
{
	struct aboutpicture *a = (struct aboutpicture *)p;
	struct RastPort *rp = win->RPort;
	const UBYTE *txt;
	LONG l, t, v, dx;

	(void)qual;
	(void)code;
	if (class & (IDCMP_INTUITICKS | IDCMP_MOUSEMOVE)) {
		txt = about_scrollertxt;
		dx = a->dx;
		ScrollRaster(rp, dx, 0, a->plugin.x, a->plugin.y, a->regionright,
		             a->regionbottom);
		t = a->txtoffset;
		l = a->xd - dx;
		/* draw characters at the right edge until one is not done */
		do {
			Move(rp, a->regionright + l + 1, a->texty);
			Text(rp, txt + t, 1);
			if ((v = l + a->lsx) <= 0) {
				l = v;
				t++;
				if (txt[t] == 0)
					t = 0;
				a->lsx = TextLength(rp, txt + t, 1);
			}
		} while (v < 0);
		a->xd = l;
		a->txtoffset = t;
		dx--;
		a->dx = (dx < 1) ? 1 : dx;
		return FALSE;
	}
	return TRUE;
}

static void freebitmap(struct aboutpicture *a)
{
	struct BitMap *bitmap;

	if ((bitmap = a->tmpbitmap)) {
		a->tmpbitmap = NULL;
		if (a->gfxversion >= 39) {
			FreeBitMap(bitmap);
		} else {
			FreeRaster(bitmap->Planes[0], P_WIDTH, 8);
			e_dispose(bitmap);
		}
	}
}

static void ab_dispose(struct EG_Plugin *p)
{
	aboutpic_dispose((struct aboutpicture *)p);
}

static const struct EG_PluginClass aboutpic_class = {
	ab_min_size,
	ab_will_resize,
	ab_render,
	ab_clear_render,
	ab_message_test,
	ab_message_action,
	ab_dispose
};

/* ------------------------------------------------------------- public */

struct aboutpicture *aboutpic_new(struct Screen *screen)
{
	struct aboutpicture *a;
	struct BitMap *bitmap;
	struct Screen *scr;
	struct DrawInfo *drinfo;
	struct TextFont *font = NULL;
	PLANEPTR raster;
	UWORD *pens;
	LONG penless, i;

	a = e_new(sizeof(*a));
	a->plugin.cls = &aboutpic_class;
	a->gfxversion = GfxBase->LibNode.lib_Version;
	if (a->gfxversion >= 39) {
		if (!(bitmap = AllocBitMap(P_WIDTH, 1, 8,
		                           BMF_CLEAR | BMF_INTERLEAVED, NULL))) {
			e_dispose(a);
			Raise('MEM');
		}
	} else {
		bitmap = AllocVec(sizeof(struct BitMap), MEMF_CLEAR);
		if (!bitmap) {
			e_dispose(a);
			Raise('MEM');
		}
		InitBitMap(bitmap, 8, P_WIDTH, 1);
		if ((raster = AllocRaster(P_WIDTH, 8))) {
			for (i = 0; i <= 7; i++)
				bitmap->Planes[i] = raster + P_WIDTH / 8 * i;
		} else {
			e_dispose(bitmap);
			e_dispose(a);
			Raise('MEM');
		}
	}
	a->tmpbitmap = bitmap;

	scr = screen ? screen : LockPubScreen(NULL);
	a->shinepen = 2;
	a->fillpen = 3;
	a->highlightpen = 2;
	if (scr) {
		if ((drinfo = GetScreenDrawInfo(scr))) {
			penless = drinfo->dri_NumPens;
			pens = drinfo->dri_Pens;
			if (penless > FILLPEN)
				a->fillpen = pens[FILLPEN];
			if (penless > SHINEPEN)
				a->shinepen = pens[SHINEPEN];
			if (penless > HIGHLIGHTTEXTPEN)
				a->highlightpen = pens[HIGHLIGHTTEXTPEN];
			if (!font)
				font = drinfo->dri_Font;
			FreeScreenDrawInfo(scr, drinfo);
		}
		if (!font)
			font = scr->RastPort.Font;
		if (!screen)
			UnlockPubScreen(NULL, scr);
	}
	a->font = font;
	a->txtoffset = 0;
	a->lsx = 0;
	a->xd = 0;
	a->dx = 1;
	return a;
}

void aboutpic_dispose(struct aboutpicture *a)
{
	if (!a)
		return;
	freebitmap(a);
	e_dispose(a);
}
