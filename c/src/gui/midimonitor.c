/*
 * midimonitor.c - MIDI controller monitor plugin, port of mbmmonit.e
 *
 * Differences from the E code:
 * - E's Box() on stdrast is SetAPen + RectFill on the window's rastport
 *   (e_box below).
 * - updatemidimonitor(NIL) walked an uninitialised pointer; here it goes
 *   through the 16 channels as intended (setstatus() compares the value
 *   again, so the result on screen is the same).
 * - If the screen's DrawInfo cannot be had and no font is given, the E
 *   code took the screen's TextAttr as a TextFont; here the font stays
 *   NULL and the window's own font is used.
 */
#include <string.h>
#include <exec/types.h>
#include <intuition/intuition.h>
#include <intuition/screens.h>
#include <graphics/rastport.h>
#include <graphics/gfx.h>
#include <graphics/text.h>
#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/intuition.h>

#include "egui.h"
#include "midimonitor.h"
#include "keycodes.h"
#include "../app/eport.h"
#include "../app/locale.h"

#define MM_MIN(a, b) ((a) < (b) ? (a) : (b))
#define MM_MAX(a, b) ((a) > (b) ? (a) : (b))

/* E's STRING s[10] */
#define SLEN 11

static const struct EG_PluginClass midimonitor_class;

static void e_box(struct RastPort *rp, LONG x1, LONG y1, LONG x2, LONG y2,
                  LONG c)
{
	SetAPen(rp, c);
	RectFill(rp, x1, y1, x2, y2);
}

/* the value text: right aligned number or "not available" */
static void valuestr(STRPTR s, LONG v)
{
	estringf(s, SLEN, v < 0 ? (CONST_STRPTR)LOC(STRID_NOTAVAILABLE)
	                        : (CONST_STRPTR)"\\r\\d[5]", v);
}

static void mm_min_size(struct EG_Plugin *p, struct TextAttr *ta, WORD fh,
                        WORD *w, WORD *h)
{
	(void)p;
	(void)ta;
	*w = fh * 8;
	*h = (fh + 3) * 16 + 1;
}

static void mm_render(struct EG_Plugin *p, struct TextAttr *ta, WORD x,
                      WORD y, WORD xs, WORD ys, struct Window *win)
{
	struct midimonitor *mm = (struct midimonitor *)p;
	struct RastPort *rp;
	struct mm_displaystatus *dss;
	UBYTE s[SLEN];
	LONG h, yo, i, v = 0, t, y1, y2, c, w, len;

	(void)ta;
	rp = win->RPort;
	if (mm->font)
		SetFont(rp, mm->font);
	h = (ys - 1) / 16;
	yo = (h - 2 - rp->TxHeight) / 2 + y;
	for (i = 1; i <= 16; i++) {
		estringf(s, SLEN, (CONST_STRPTR)"\\d[2]", i);
		if ((t = TextLength(rp, s, 2)) > v)
			v = t;
	}
	SetAPen(rp, mm->textpen);
	SetDrMd(rp, JAM1);
	t = yo + rp->TxBaseline + 1;
	mm->minx = x + v + 3;
	mm->maxx = x + xs - 3;
	dss = mm->ds;
	for (i = 0; i < 16; i++) {
		dss->txty = h * i + t;
		Move(rp, x + 1, dss->txty);
		estringf(s, SLEN, (CONST_STRPTR)"\\d[2]", i + 1);
		Text(rp, s, 2);
		y1 = i * h + yo;
		Move(rp, mm->minx - 1, y1);
		Draw(rp, mm->maxx + 1, y1);
		y2 = y1 + rp->TxHeight + 1;
		Draw(rp, mm->maxx + 1, y2);
		Draw(rp, mm->minx - 1, y2);
		Draw(rp, mm->minx - 1, y1);
		dss->miny = y1 + 1;
		dss->maxy = y2 - 1;
		v = dss->value;
		valuestr(s, v);
		w = mm->maxx - mm->minx;
		mm->midx = w / 2 + mm->minx;
		dss->lastx = v < 0 ? mm->midx : w * v / 16383 + mm->minx;
		e_box(rp, mm->minx, dss->miny, mm->maxx, dss->maxy, mm->shinepen);
		if (mm->midx < dss->lastx)
			e_box(rp, mm->midx, dss->miny, dss->lastx, dss->maxy,
			      mm->fillpen);
		else if (v != -1)
			e_box(rp, dss->lastx, dss->miny, mm->midx, dss->maxy,
			      mm->fillpen);
		len = strlen((char *)s);
		c = TextLength(rp, s, len - 2);
		dss->mintxt = mm->midx - c;
		dss->maxtxt = dss->mintxt + TextLength(rp, s, len) - 1;
		SetAPen(rp, mm->textpen);
		Move(rp, dss->mintxt, dss->txty);
		Text(rp, s, len);
		dss++;
	}
	mm->rport = rp;
}

static void mm_clear_render(struct EG_Plugin *p, struct Window *win)
{
	(void)win;
	((struct midimonitor *)p)->rport = NULL;
}

/*
 * Redraws only what changed: the bar between the old and new end and the
 * value text, which stays centred on midx (the last two characters right
 * of it, the rest left of it).
 */
void midimonitor_setstatus(struct midimonitor *mm, LONG num, LONG value)
{
	struct mm_displaystatus *dss;
	struct RastPort *rp;
	UBYTE s[SLEN];
	LONG l, t, otx1, otx2, y1, y2, ox, midx, nx, tx1, tx2, shp, flp, txp;

	dss = &mm->ds[num];
	if (dss->value == value)
		return;
	dss->value = value;
	if ((rp = mm->rport) == NULL)
		return;
	shp = mm->shinepen;
	flp = mm->fillpen;
	txp = mm->textpen;
	y1 = dss->miny;
	y2 = dss->maxy;
	midx = mm->midx;
	ox = dss->lastx;
	nx = value < 0 ? midx
	               : (mm->maxx - mm->minx) * value / 16383 + mm->minx;
	dss->lastx = nx;
	otx1 = dss->mintxt;
	otx2 = dss->maxtxt;
	valuestr(s, value);
	l = strlen((char *)s) - 2;
	tx1 = midx - TextLength(rp, s, l);
	dss->mintxt = tx1;
	tx2 = midx + TextLength(rp, s + l, 2) - 1;
	dss->maxtxt = tx2;
	SetDrMd(rp, JAM2);
	SetBPen(rp, shp);
	if (value < 0) {
		SetAPen(rp, txp);
		Move(rp, tx1, dss->txty);
		Text(rp, s, l + 2);
		if ((t = MM_MIN(ox, otx1)) < tx1)
			e_box(rp, t, y1, tx1 - 1, y2, shp);
		else if ((t = MM_MAX(ox, otx2)) > tx2)
			e_box(rp, tx2 + 1, y1, t, y2, shp);
	} else if (nx >= midx) {
		if ((t = MM_MIN(ox, otx1)) < tx1)
			e_box(rp, t, y1, tx1 - 1, y2, shp);
		SetAPen(rp, txp);
		Move(rp, tx1, dss->txty);
		Text(rp, s, l);
		if (nx >= tx2) {
			SetBPen(rp, flp);
			Move(rp, midx, dss->txty);
			Text(rp, s + l, 2);
			if (ox <= nx) {
				if (nx < otx2) {
					e_box(rp, nx + 1, y1, otx2, y2, shp);
					if (nx > tx2)
						e_box(rp, tx2 + 1, y1, nx, y2, flp);
				} else if (ox <= MM_MAX(otx2, tx2)) {
					if (nx > tx2)
						e_box(rp, tx2 + 1, y1, nx, y2, flp);
				} else {
					if (ox < nx)
						e_box(rp, ox + 1, y1, nx, y2, flp);
					if (tx2 < otx2)
						e_box(rp, tx2 + 1, y1, otx2, y2, flp);
				}
			} else {
				e_box(rp, nx + 1, y1, MM_MAX(ox, otx2), y2, shp);
				if (tx2 < (t = MM_MIN(nx, otx2)))
					e_box(rp, tx2 + 1, y1, t, y2, flp);
			}
		} else {
			e_box(rp, midx, y1, nx, y2, flp);
			if (nx < (t = MM_MAX(otx2, ox)))
				e_box(rp, nx + 1, y1, t, y2, shp);
			SetDrMd(rp, JAM1);
			SetAPen(rp, txp);
			Move(rp, midx, dss->txty);
			Text(rp, s + l, 2);
		}
	} else {
		if ((t = MM_MAX(ox, otx2)) > tx2)
			e_box(rp, tx2 + 1, y1, t, y2, shp);
		SetAPen(rp, txp);
		Move(rp, midx, dss->txty);
		Text(rp, s + l, 2);
		if (nx <= tx1) {
			SetBPen(rp, flp);
			Move(rp, tx1, dss->txty);
			Text(rp, s, l);
			if (ox >= nx) {
				if (nx > otx1) {
					e_box(rp, otx1, y1, nx - 1, y2, shp);
					if (nx < tx1)
						e_box(rp, nx, y1, tx1 - 1, y2, flp);
				} else if (ox >= MM_MIN(otx1, tx1)) {
					if (nx < tx1)
						e_box(rp, nx, y1, tx1 - 1, y2, flp);
				} else {
					if (ox > nx)
						e_box(rp, nx, y1, ox - 1, y2, flp);
					if (tx1 > otx1)
						e_box(rp, otx1, y1, tx1 - 1, y2, flp);
				}
			} else {
				e_box(rp, MM_MIN(ox, otx1), y1, nx - 1, y2, shp);
				if (tx1 > (t = MM_MAX(nx, otx1)))
					e_box(rp, t, y1, tx1 - 1, y2, flp);
			}
		} else {
			e_box(rp, nx, y1, midx, y2, flp);
			if (nx > (t = MM_MIN(otx1, ox)))
				e_box(rp, t, y1, nx - 1, y2, shp);
			SetDrMd(rp, JAM1);
			SetAPen(rp, txp);
			Move(rp, tx1, dss->txty);
			Text(rp, s, l);
		}
	}
}

static BOOL mm_message_test(struct EG_Plugin *p, struct IntuiMessage *imsg,
                            struct Window *win)
{
	LONG code;

	(void)p;
	(void)win;
	code = imsg->Code;
	if (imsg->Class & IDCMP_VANILLAKEY) {
		if (code == 9 || code == ESC_CODE)
			return TRUE;
	}
	return FALSE;
}

static BOOL mm_message_action(struct EG_Plugin *p, ULONG class, UWORD qual,
                              UWORD code, struct Window *win)
{
	(void)qual;
	(void)win;
	if (class & IDCMP_VANILLAKEY) {
		((struct midimonitor *)p)->keycode = code;
		return TRUE;
	}
	return FALSE;
}

void midimonitor_update(struct midimonitor *mm, const WORD *mc)
{
	LONG i;

	if (mc) {
		for (i = 0; i < 16; i++) {
			if (mm->ds[i].value != mc[i])
				midimonitor_setstatus(mm, i, mc[i]);
		}
	} else {
		for (i = 0; i < 16; i++) {
			if (mm->ds[i].value != -1)
				midimonitor_setstatus(mm, i, -1);
		}
	}
}

struct midimonitor *midimonitor_new(struct Screen *screen,
                                    struct TextFont *font)
{
	struct midimonitor *mm;
	struct Screen *scr;
	struct DrawInfo *drinfo;
	LONG penless, i;
	UWORD *x;

	mm = e_new(sizeof(struct midimonitor));
	mm->plugin.cls = &midimonitor_class;
	scr = screen ? screen : LockPubScreen(NULL);
	mm->textpen = 1;
	mm->shinepen = 2;
	mm->fillpen = 3;
	if (scr) {
		if ((drinfo = GetScreenDrawInfo(scr)) != NULL) {
			penless = drinfo->dri_NumPens;
			x = drinfo->dri_Pens;
			if (penless > TEXTPEN)
				mm->textpen = x[TEXTPEN];
			if (penless > FILLPEN)
				mm->fillpen = x[FILLPEN];
			if (penless > SHINEPEN)
				mm->shinepen = x[SHINEPEN];
			if (font == NULL)
				font = drinfo->dri_Font;
			FreeScreenDrawInfo(scr, drinfo);
		}
		if (!screen)
			UnlockPubScreen(NULL, scr);
	}
	mm->font = font;
	for (i = 0; i < 16; i++)
		mm->ds[i].value = -1;
	return mm;
}

void midimonitor_dispose(struct midimonitor *mm)
{
	e_dispose(mm);
}

static void mm_class_dispose(struct EG_Plugin *p)
{
	e_dispose(p);
}

static const struct EG_PluginClass midimonitor_class = {
	mm_min_size,
	NULL,                   /* will_resize: both */
	mm_render,
	mm_clear_render,
	mm_message_test,
	mm_message_action,
	mm_class_dispose,
};
