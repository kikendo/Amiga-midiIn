/*
 * leds.c - the bank "LEDs" plugin, port of MBLeds.e
 *
 * Differences from the E code:
 * - E's Line()/Box() on stdrast are done with SetAPen + Move/Draw or
 *   RectFill on the plugin's rastport (e_line/e_box below).
 * - The shadow and shine pens are file statics as in the E code (shared
 *   by all instances, set by leds_new).
 * - setactive() with an unknown type keeps st 0 instead of an undefined
 *   register value (only LSET* values are used).
 */
#include <exec/types.h>
#include <intuition/intuition.h>
#include <intuition/screens.h>
#include <graphics/rastport.h>
#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/intuition.h>

#include "egui.h"
#include "leds.h"
#include "../app/eport.h"
#include "../app/banks.h"

#define BS_ENABLED 1
#define BS_SETLO   2
#define BS_SETFULL 4

#define SETCURRENT 0xFFFF
#define SETOTHER   0x10000

#define MOVEMENT_NONE 0
#define MOVEMENT_SET  1
#define MOVEMENT_DRAG 2

#define HALF (NUMBANKS / 2)

static LONG shadowpen, shinepen;
static UWORD ledpattern[2] = { 0x5555, 0xAAAA };

static const struct EG_PluginClass leds_class;

static void drawrange(struct leds *l, LONG lo, LONG hi, LONG colr);
static void drawled(struct leds *l, LONG number, LONG type, BOOL drawborder);

/* E's Line() and Box() */
static void e_line(struct RastPort *rp, LONG x1, LONG y1, LONG x2, LONG y2,
                   LONG c)
{
	SetAPen(rp, c);
	Move(rp, x1, y1);
	Draw(rp, x2, y2);
}

static void e_box(struct RastPort *rp, LONG x1, LONG y1, LONG x2, LONG y2,
                  LONG c)
{
	SetAPen(rp, c);
	RectFill(rp, x1, y1, x2, y2);
}

static void leds_min_size(struct EG_Plugin *p, struct TextAttr *ta, WORD fh,
                          WORD *w, WORD *h)
{
	(void)p;
	(void)ta;
	*w = 8 * HALF;
	*h = fh * 2;
}

struct leds *leds_new(struct Screen *screen)
{
	struct leds *l;
	struct DrawInfo *drinfo;
	struct Screen *scr;
	LONG penless, f;
	UWORD *x;

	l = e_new(sizeof(struct leds));
	l->plugin.cls = &leds_class;
	l->current = 0;
	l->lorange = 0;
	l->hirange = 0;
	l->movement = MOVEMENT_NONE;
	l->select = -1;
	l->dragpos = -1;
	l->movepos = 0;
	for (f = 0; f < NUMBANKS; f++)
		l->bankstat[f] = 0;
	l->colrinner = 1;
	l->colrset = 3;
	l->colrback = 0;
	l->colrcurrent = 2;
	scr = screen ? screen : LockPubScreen(NULL);
	if (scr) {
		if ((drinfo = GetScreenDrawInfo(scr)) != NULL) {
			penless = drinfo->dri_NumPens;
			x = drinfo->dri_Pens;
			if (penless > SHINEPEN)
				shinepen = x[SHINEPEN];
			if (penless > SHADOWPEN)
				shadowpen = x[SHADOWPEN];
			if (penless > FILLPEN)
				l->colrset = x[FILLPEN];
			if (penless > SHINEPEN)
				l->colrinner = x[SHINEPEN];
			if (penless > BACKGROUNDPEN)
				l->colrback = x[BACKGROUNDPEN];
			if (penless > HIGHLIGHTTEXTPEN)
				l->colrcurrent = x[HIGHLIGHTTEXTPEN];
			if (l->colrinner == l->colrcurrent)
				l->colrinner = 1;
			FreeScreenDrawInfo(scr, drinfo);
		}
		if (!screen)
			UnlockPubScreen(NULL, scr);
	}
	return l;
}

void leds_dispose(struct leds *l)
{
	e_dispose(l);
}

static void leds_class_dispose(struct EG_Plugin *p)
{
	e_dispose(p);
}

static void leds_clear_render(struct EG_Plugin *p, struct Window *win)
{
	struct leds *l = (struct leds *)p;

	(void)win;
	l->rport = NULL;
	l->win = NULL;
}

static void leds_render(struct EG_Plugin *p, struct TextAttr *ta, WORD x,
                        WORD y, WORD xs, WORD ys, struct Window *win)
{
	struct leds *l = (struct leds *)p;
	LONG f;

	(void)ta;
	(void)x;
	(void)y;
	f = xs / HALF;
	l->ledx = f;
	l->margl = f / 4;
	f = ys / 3;
	l->ledh = f;
	l->margt = (ys / 2 - f) / 2;
	l->nextrow = ys / 2;

	l->rport = win->RPort;
	l->win = win;

	for (f = 0; f < NUMBANKS; f++)
		drawled(l, f, l->bankstat[f], TRUE);

	l->movement = MOVEMENT_NONE;
	l->dragpos = -1;
	drawrange(l, l->lorange, l->hirange, SETCURRENT);
}

LONG leds_setcurrent(struct leds *l, LONG v)
{
	if (v >= 0 && v < NUMBANKS) {
		if (l->movement != MOVEMENT_NONE) {
			if (l->win)
				ReportMouse(FALSE, l->win);
			l->movement = MOVEMENT_NONE;
		}
		leds_setrange(l, v, v);
		drawled(l, v, SETCURRENT, FALSE);
	}
	return l->current;
}

void leds_setenabled(struct leds *l, LONG v, BOOL enable)
{
	LONG f, st, b, e;

	if (v >= 0 && v < NUMBANKS) {
		b = v;
		e = v;
	} else if (v == -1) {
		b = 0;
		e = NUMBANKS - 1;
	} else {
		return;
	}

	st = enable ? BS_ENABLED : 0;
	for (f = b; f <= e; f++) {
		if (l->bankstat[f] != st)
			drawled(l, f, st, FALSE);
	}
}

void leds_setactive(struct leds *l, LONG v, LONG typ)
{
	LONG f, st = 0, b, e;

	if (v >= 0 && v < NUMBANKS) {
		b = v;
		e = v;
	} else if (v == -1) {
		b = 0;
		e = NUMBANKS - 1;
	} else {
		return;
	}

	if (typ == LSETINNER)
		st = BS_SETLO | BS_ENABLED;
	else if (typ == LSETFULL)
		st = BS_SETFULL | BS_ENABLED;

	for (f = b; f <= e; f++) {
		if (typ == LSETNONE)
			st = l->bankstat[f] & BS_ENABLED;
		if (l->bankstat[f] != st)
			drawled(l, f, st, FALSE);
	}
}

static BOOL leds_message_test(struct EG_Plugin *p, struct IntuiMessage *imsg,
                              struct Window *win)
{
	struct leds *l = (struct leds *)p;
	LONG x, y, code;
	ULONG class;

	(void)win;
	class = imsg->Class;
	if (class & (IDCMP_MOUSEBUTTONS | IDCMP_MOUSEMOVE)) {
		x = imsg->MouseX - p->x;
		y = imsg->MouseY - p->y;
		if (class & IDCMP_MOUSEMOVE) {
			if (l->movement != MOVEMENT_NONE) {
				if (x >= 0 && y >= 0 && x < p->xs && y < p->ys)
					x = x / l->ledx + (y < l->nextrow ? 0 : HALF);
				else
					x = -1;
				if (l->select != x) {
					l->select = x;
					return TRUE;
				}
			}
		} else if ((code = imsg->Code) == SELECTDOWN) {
			if (x >= 0 && y >= 0 && x < p->xs && y < p->ys) {
				l->select = x / l->ledx +
				            (y < l->nextrow ? 0 : HALF);
				return TRUE;
			}
		} else if (code == SELECTUP) {
			if (l->movement != MOVEMENT_NONE)
				return TRUE;
		}
	} else if (class & IDCMP_INACTIVEWINDOW) {
		if (l->movement)
			return TRUE;
	}
	return FALSE;
}

static BOOL leds_message_action(struct EG_Plugin *p, ULONG class, UWORD qual,
                                UWORD code, struct Window *win)
{
	struct leds *l = (struct leds *)p;
	LONG sel, mv;

	(void)qual;
	sel = l->select;
	if ((mv = l->movement) != MOVEMENT_NONE)
		ReportMouse(FALSE, win);
	if (class & IDCMP_MOUSEMOVE) {
		if (mv == MOVEMENT_SET) {
			if (sel == -1)
				leds_setrange(l, l->current, l->current);
			else if (sel < l->current)
				leds_setrange(l, sel, l->current);
			else
				leds_setrange(l, l->current, sel);
			ReportMouse(TRUE, win);
		} else if (mv == MOVEMENT_DRAG) {
			leds_setrange(l, -1, -1);
			ReportMouse(TRUE, win);
		}
	} else if (class & IDCMP_MOUSEBUTTONS) {
		if (code == SELECTDOWN) {
			if (sel >= l->lorange && sel <= l->hirange &&
			    (l->hirange - l->lorange) < (NUMBANKS - 1)) {
				l->movepos = sel - l->lorange;
				l->movement = MOVEMENT_DRAG;
				leds_setrange(l, -1, -1);
				ReportMouse(TRUE, win);
			} else {
				l->movement = MOVEMENT_SET;
				leds_setrange(l, sel, sel);
				drawled(l, sel, SETCURRENT, FALSE);
				ReportMouse(TRUE, win);
				l->act = LEDACT_SETBANK;
				return TRUE;
			}
		} else if (code == SELECTUP) {
			l->movement = MOVEMENT_NONE;
			if (mv == MOVEMENT_SET) {
				l->act = LEDACT_SETRANGE;
			} else if (mv == MOVEMENT_DRAG) {
				if (l->dragpos == l->lorange) {
					leds_setrange(l, sel, sel);
					drawled(l, sel, SETCURRENT, FALSE);
					l->act = LEDACT_SETBANK;
					return TRUE;
				}
				leds_setrange(l, -1, -1);
				if (l->select < 0)
					return FALSE;
				l->act = LEDACT_MOVERANGE;
			} else {
				return FALSE;
			}
			return TRUE;
		}
	} else if (class & IDCMP_INACTIVEWINDOW) {
		l->movement = MOVEMENT_NONE;
		leds_setrange(l, -1, -1);
	}
	return FALSE;
}

LONG leds_getrange(struct leds *l, LONG *lo, LONG *hi)
{
	LONG x;

	if (lo)
		*lo = l->lorange;
	if (hi)
		*hi = l->hirange;
	if ((x = l->select - l->movepos) < 0)
		x = 0;
	if ((l->hirange - l->lorange + x) >= 60)
		x = 59 - l->hirange + l->lorange;
	return x;
}

void leds_setrange(struct leds *l, LONG lo, LONG hi)
{
	LONG lr, h, x;

	lr = l->lorange;
	h = l->hirange;
	if ((x = l->dragpos) >= 0) {
		l->dragpos = -1;
		drawrange(l, x, h - lr + x, -1);
	}
	if (lo >= 0 && hi < NUMBANKS && lo <= hi) {
		drawrange(l, lr, h, -1);
		drawrange(l, lo, hi, SETCURRENT);
		l->lorange = lo;
		l->hirange = hi;
	} else {
		if (x - 1 <= h && h - lr + x + 1 >= lr)
			drawrange(l, lr, h, SETCURRENT);
		if (l->movement == MOVEMENT_DRAG && l->select >= 0) {
			x = l->select - l->movepos;
			if (x < 0)
				x = 0;
			if (h - lr + x >= NUMBANKS)
				x = lr - h + NUMBANKS - 1;
			drawrange(l, x, h - lr + x, SETOTHER);
			l->dragpos = x;
		}
	}
}

/* frame around banks lo..hi; colr SETCURRENT, SETOTHER or -1 (erase) */
static void drawrange(struct leds *l, LONG lo, LONG hi, LONG colr)
{
	struct RastPort *rp;
	LONG x1, x2, y1, y2, ye, maxx, minx, col;

	if ((rp = l->rport) == NULL)
		return;
	x1 = (HALF <= lo ? lo - HALF : lo) * l->ledx + l->margl +
	     l->plugin.x - 2;
	x2 = (HALF <= hi ? hi - HALF : hi) * l->ledx + l->margl + l->plugin.x;
	x2 = x2 + l->ledx - l->margl;
	y1 = l->plugin.y + l->margt - 2;
	y2 = y1;
	if (HALF <= lo)
		y1 = y1 + l->nextrow;
	if (HALF <= hi)
		y2 = y2 + l->nextrow;
	maxx = HALF * l->ledx + l->plugin.x;
	minx = l->plugin.x + l->margl - 2;
	ye = l->ledh + 3;
	switch (colr) {
	case SETCURRENT:
		col = l->colrcurrent;
		break;
	case SETOTHER:
		col = l->colrset;
		break;
	default:
		col = l->colrback;
		break;
	}
	if (y1 == y2) {
		e_line(rp, x1, y1, x2, y1, col);
		e_line(rp, x1, y1 + ye, x2, y1 + ye, col);
	} else {
		e_line(rp, x1, y1, maxx, y1, col);
		e_line(rp, x1, y1 + ye, maxx, y1 + ye, col);
		e_line(rp, minx, y2, x2, y2, col);
		e_line(rp, minx, y2 + ye, x2, y2 + ye, col);
	}
	e_line(rp, x1, y1, x1, y1 + ye, col);
	e_line(rp, x2, y2, x2, y2 + ye, col);
	if (colr == SETOTHER && lo <= l->hirange && hi >= l->lorange) {
		/* the part over the current range: dashed */
		SetBPen(rp, l->colrcurrent);
		rp->LinePtrn = 0x9999;
		lo = l->lorange;
		hi = l->hirange;
		x1 = E_MAX(x1, (HALF <= lo ? lo - HALF : lo) * l->ledx +
		               l->margl + l->plugin.x - 2);
		x2 = E_MIN(x2, (HALF <= hi ? hi - HALF : hi) * l->ledx +
		               l->margl + l->plugin.x + l->ledx - l->margl);
		if (y1 == y2) {
			e_line(rp, x1, y1, x2, y1, col);
			e_line(rp, x1, y1 + ye, x2, y1 + ye, col);
		} else {
			e_line(rp, x1, y1, maxx, y1, col);
			e_line(rp, x1, y1 + ye, maxx, y1 + ye, col);
			e_line(rp, minx, y2, x2, y2, col);
			e_line(rp, minx, y2 + ye, x2, y2 + ye, col);
		}
		SetBPen(rp, l->colrback);
		rp->LinePtrn = 0xFFFF;
	}
}

/*
 * Sets the state of a LED (type: BS_* bits) and draws it if shown.
 * type SETCURRENT: make it the current bank instead, keeping its state.
 */
static void drawled(struct leds *l, LONG number, LONG type, BOOL drawborder)
{
	struct RastPort *rp;
	LONG coltype, colin, colshad, colshin, x, y, xe, ye, num;

	num = number;
	if (num >= NUMBANKS || num < 0)
		return;
	if (type == SETCURRENT) {
		x = l->current;
		l->current = num;
		if (x != num)
			drawled(l, x, l->bankstat[x], FALSE);
		type = l->bankstat[num];
	} else {
		l->bankstat[num] = type;
	}

	if ((rp = l->rport) == NULL)
		return;

	if (type & BS_ENABLED) {
		if (type & BS_SETFULL) {
			coltype = 1;
			colin = l->colrset;
		} else if (type & BS_SETLO) {
			coltype = 2;
			colin = l->colrset;
		} else {
			coltype = 1;
			colin = l->colrcurrent;
		}
	} else {
		coltype = 0;
		colin = l->colrinner;
	}

	if (l->current == num) {
		colshin = l->colrback;
		colshad = shinepen;
	} else {
		colshin = shinepen;
		colshad = l->colrback;
	}

	x = (HALF <= num ? num - HALF : num) * l->ledx + l->margl + l->plugin.x;
	xe = x + l->ledx - l->margl - 2;
	y = l->plugin.y + l->margt;
	if (HALF <= num)
		y = y + l->nextrow;
	ye = y + l->ledh - 1;

	if (drawborder) {
		e_box(rp, x, y - 1, xe, ye + 1, shadowpen);
		e_box(rp, x - 1, y, xe + 1, ye, shadowpen);
	}
	e_line(rp, x + 1, y, xe - 1, y, colshin);
	e_line(rp, x, y + 1, x, ye - 1, colshin);
	e_line(rp, xe, y + 1, xe, ye - 1, colshad);
	e_line(rp, x + 1, ye, xe - 1, ye, colshad);

	switch (coltype) {
	case 1:
		e_box(rp, x + 1, y + 1, xe - 1, ye - 1, colin);
		break;
	case 2:
		e_box(rp, x + 1, y + 1, xe - 1, ye - 1, l->colrback);
		e_line(rp, x + 1, y + 1, xe - 1, ye - 1, colin);
		e_line(rp, xe - 1, y + 1, x + 1, ye - 1, colin);
		break;
	default:
		/* disabled: checkered */
		e_box(rp, x + 1, y + 1, xe - 1, ye - 1, l->colrback);
		rp->AreaPtrn = ledpattern;
		rp->AreaPtSz = 1;
		e_box(rp, x + 1, y + 1, xe - 1, ye - 1, colin);
		rp->AreaPtrn = NULL;
		rp->AreaPtSz = 0;
		break;
	}
}

static const struct EG_PluginClass leds_class = {
	leds_min_size,
	NULL,                   /* will_resize: both */
	leds_render,
	leds_clear_render,
	leds_message_test,
	leds_message_action,
	leds_class_dispose,
};
