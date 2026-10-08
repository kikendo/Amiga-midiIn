/*
 * envelope.c - volume envelope editor plugin, port of MBenvelope.e
 *
 * Differences from the E code:
 * - E's Line() without a colour argument draws in pen 1 (its default);
 *   the time ticks in prtfixvalue() do the same here, and so leave pen 1
 *   set for the following text, as in E.
 * - The part names in the bottom area are printed with Text(); E passed
 *   them to TextF() as a format string (same result for catalog strings
 *   without format codes).
 * - rmbtrap() changes the window flags in C instead of inline assembly.
 */
#include <exec/types.h>
#include <intuition/intuition.h>
#include <intuition/screens.h>
#include <graphics/rastport.h>
#include <graphics/text.h>
#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <string.h>

#include "egui.h"
#include "envelope.h"
#include "../app/eport.h"
#include "../app/locale.h"

/* mbkeycod.e */
#ifndef KEYCODE_F10
#define KEYCODE_F10 0x59
#endif
#ifndef MYRAWCODE
#define MYRAWCODE 0x100
#endif
#ifndef ESC_CODE
#define ESC_CODE 27
#endif

enum { SETNOTHING = 0, SETATTACK, SETDECAY, SETSUSTAIN, SETRELEASE };

/* ------------------------------------------------------ drawing helpers */

/* E's Line(x1,y1,x2,y2,colour) */
static void eline(struct RastPort *rp, LONG x1, LONG y1, LONG x2, LONG y2,
                  LONG pen)
{
	SetAPen(rp, pen);
	Move(rp, x1, y1);
	Draw(rp, x2, y2);
}

/* E's Box(x1,y1,x2,y2,colour) */
static void ebox(struct RastPort *rp, LONG x1, LONG y1, LONG x2, LONG y2,
                 LONG pen)
{
	SetAPen(rp, pen);
	RectFill(rp, x1, y1, x2, y2);
}

/* E's TextF(x,y,'\s',s) */
static void etext(struct RastPort *rp, LONG x, LONG y, CONST_STRPTR s)
{
	Move(rp, x, y);
	Text(rp, s, strlen((const char *)s));
}

/* sets or clears WFLG_RMBTRAP of the window */
static void rmbtrap(LONG on, struct Window *win)
{
	if (on)
		win->Flags |= WFLG_RMBTRAP;
	else
		win->Flags &= ~WFLG_RMBTRAP;
}

static LONG clamp255(LONG t)
{
	if (t > 255)
		t = 255;
	if (t < 0)
		t = 0;
	return t;
}

/* ------------------------------------------------------------- display */

/*
 * One value below the graph between x1 and x2: a bracket with ticks for
 * a time (tenths of a second), or a percentage for val < 0 (-level-1).
 */
static void prtfixvalue(struct RastPort *rp, LONG x1, LONG x2, LONG val,
                        LONG y1, LONG y2, LONG ytxt)
{
	UBYTE s[8];
	LONG t, c, x, y;

	if (val < 0) {
		estringf(s, sizeof(s), (CONST_STRPTR)"\\d%", 100 * (-val) / 256);
	} else {
		Move(rp, x1, y2);
		Draw(rp, x1, y1);
		Draw(rp, x2, y1);
		Draw(rp, x2, y2);
		t = val;
		if (t < 10)
			t = 10;
		c = ((x2 - x1 + 1) << 16) * 10 / t;    /* tick distance, 16.16 */
		x = x1 << 16;
		y = (y2 - y1) / 2 + y1;
		while ((t = x >> 16) < x2) {
			eline(rp, t, y1, t, y, 1);     /* E's default pen */
			x += c;
		}
		if (val == 0)
			estringf(s, sizeof(s), (CONST_STRPTR)"\\d", 0L);
		else if (val < 10)
			estringf(s, sizeof(s), (CONST_STRPTR)".\\d", val);
		else
			estringf(s, sizeof(s), (CONST_STRPTR)"\\d.\\d",
			         val / 10, val % 10);
	}
	c = TextLength(rp, s, strlen((const char *)s));
	t = x2 - x1;
	etext(rp, t / 2 - (c / 2) + x1, ytxt, s);
}

/* clears the area below the graph; update: all values, else the name of
 * the part being edited (its value follows at secposx) */
static void updatebottomarea(struct envel_plugin *e, BOOL update)
{
	struct RastPort *rp = e->rport;
	struct envel_coord *coords;
	LONG y, y2, tx;
	STRPTR name;
	static const LONG names[4] = {
		STRID_EATTACK, STRID_EDECAY, STRID_ESUSTAIN, STRID_ERELEASE
	};

	if (!rp)
		return;
	coords = e->coords;
	y = e->y2 + 3;
	y2 = y + (e->plugin.ys / 20);
	ebox(rp, e->plugin.x, y, e->plugin.x + e->plugin.xs,
	     e->plugin.y + e->plugin.ys - 1, rp->BgPen);
	SetAPen(rp, e->textpen);
	if (update) {
		tx = e->bottomtexty;
		prtfixvalue(rp, coords[0].x, coords[1].x, e->t1, y, y2, tx);
		prtfixvalue(rp, coords[1].x, coords[2].x, e->t2, y, y2, tx);
		prtfixvalue(rp, coords[2].x, coords[3].x, -(LONG)e->slev - 1,
		            y, y2, tx);
		prtfixvalue(rp, coords[3].x, coords[4].x, e->t3, y, y2, tx);
	} else {
		name = LOC(names[e->whichm - 1]);
		e->secposx = TextLength(rp, name, strlen((const char *)name)) + e->x1;
		etext(rp, e->x1, e->bottomtexty, name);
	}
}

/* the value being edited, after its name */
static void updatebottomvalue(struct envel_plugin *e, LONG val)
{
	struct RastPort *rp = e->rport;
	UBYTE buf[64];

	if (!rp)
		return;
	SetAPen(rp, e->textpen);
	if (val >= 0) {
		estringf(buf, sizeof(buf), (CONST_STRPTR)"\\d.\\d \\s ",
		         val / 10, val % 10, (LONG)LOC(STRID_SECONDS));
	} else {
		val = 100 * (-val) / 256;
		estringf(buf, sizeof(buf), (CONST_STRPTR)"\\d% ", val);
	}
	etext(rp, e->secposx, e->bottomtexty, buf);
}

/* the five corner points of the envelope for the current values */
static void updatecoords(struct envel_plugin *e, struct envel_coord *coords)
{
	LONG w, s, susth, attx, decx, relx, n;

	susth = (e->y2 - e->y1 - 2) * (256 - e->slev) / 256 + e->y1 + 1;
	w = e->x2 - e->x1 - e->ssize;
	s = e->t1 + e->t2 + e->t3 + 3;
	n = e->minw;
	if ((attx = w * e->t1 / s) < n)
		attx = n;
	if ((decx = w * e->t2 / s) < n)
		decx = n;
	if ((relx = w * e->t3 / s) < n)
		relx = n;
	while ((attx + decx + relx) > w) {
		if (relx >= decx) {
			if (relx >= attx)
				relx--;
			else
				attx--;
		} else {
			if (attx >= decx)
				attx--;
			else
				decx--;
		}
	}
	w = e->x1;
	s = e->x2;
	coords[0].x = w;               coords[0].y = e->y2;
	coords[1].x = w + attx;        coords[1].y = e->y1 + 1;
	coords[2].x = w + attx + decx; coords[2].y = susth;
	coords[3].x = s - relx;        coords[3].y = susth;
	coords[4].x = s;               coords[4].y = e->y2;
}

/*
 * Draws the envelope. still: complete redraw; else only the parts that
 * changed since the last call are erased and drawn again.
 */
static void redrawenvelope(struct envel_plugin *e, BOOL still)
{
	struct RastPort *rp = e->rport;
	struct envel_coord *coords = e->coords;
	struct envel_coord newcoords[ENVEL_COORDSNUM];
	LONG i, c, p, x, y, x2, y2, tx, nx, y3, f, chgx;
	LONG paperp, textp, barp;

	if (!rp) {
		updatecoords(e, e->coords);
		return;
	}
	if (still) {
		updatecoords(e, coords);
		SetAPen(rp, e->barpen);
		y2 = e->y2;
		for (i = 1; i <= ENVEL_COORDSNUM - 2; i++) {
			tx = coords[i].x;
			Move(rp, tx, coords[i].y);
			Draw(rp, tx, y2);
		}
		SetAPen(rp, e->textpen);
		Move(rp, coords[0].x, coords[0].y);
		PolyDraw(rp, ENVEL_COORDSNUM - 1, (WORD *)&coords[1]);
		eline(rp, coords[2].x, coords[2].y, coords[3].x, y2, e->barpen);
		eline(rp, coords[2].x, y2, coords[3].x, coords[3].y, e->barpen);
		return;
	}

	paperp = e->paperpen;
	textp = e->textpen;
	barp = e->barpen;
	updatecoords(e, newcoords);
	c = 0;
	p = 1;
	x2 = coords[0].x;
	x = newcoords[0].x;
	y = e->y1;
	y2 = e->y2;
	chgx = 0;                       /* 0: no, 1: to erase, -1: erased */
	for (i = 1; i <= ENVEL_COORDSNUM - 2; i++) {
		tx = coords[i].x;
		nx = newcoords[i].x;
		if (tx != nx) {
			eline(rp, tx, y, tx, y2, paperp);
			c = -1;
			if (chgx == 0 && (i == 2 || i == 3))
				chgx = 1;
		} else if (coords[i].y != (y3 = newcoords[i].y)) {
			eline(rp, tx, y, tx, y3, paperp);
			c = -1;
			if (chgx == 0 && (i == 2 || i == 3))
				chgx = 1;
		} else {
			if (c > 0) {
				SetAPen(rp, paperp);
				Move(rp, x2, coords[p - 1].y);
				PolyDraw(rp, i - p, (WORD *)&coords[p]);
				if (chgx == 1) {
					chgx = -1;
					eline(rp, coords[2].x, coords[2].y,
					      coords[3].x, y2, paperp);
					eline(rp, coords[2].x, y2,
					      coords[3].x, coords[3].y, paperp);
				}
				SetAPen(rp, textp);
				Move(rp, x, newcoords[p - 1].y);
				PolyDraw(rp, i - p, (WORD *)&newcoords[p]);
				for (f = p - 1; f <= i - 2; f++)
					eline(rp, newcoords[f].x, newcoords[f].y,
					      newcoords[f].x, y2, barp);
				c = 0;
			} else if (c < 0) {
				c = 1;
			}
			if (c == 0) {
				p = i + 1;
				x2 = tx;
				x = nx;
			}
		}
	}
	/* i is ENVEL_COORDSNUM - 1 here, as after E's FOR loop */
	if (c) {
		if (c < 0)
			i++;
		SetAPen(rp, paperp);
		Move(rp, x2, coords[p - 1].y);
		PolyDraw(rp, i - p, (WORD *)&coords[p]);
		if (chgx == 1) {
			chgx = -1;
			eline(rp, coords[2].x, coords[2].y, coords[3].x, y2, paperp);
			eline(rp, coords[2].x, y2, coords[3].x, coords[3].y, paperp);
		}
		SetAPen(rp, textp);
		Move(rp, x, newcoords[p - 1].y);
		PolyDraw(rp, i - p, (WORD *)&newcoords[p]);
		for (f = p - 1; f <= i - 2; f++)
			eline(rp, newcoords[f].x, newcoords[f].y,
			      newcoords[f].x, y2, barp);
		if (chgx == -1) {
			eline(rp, newcoords[2].x, newcoords[2].y,
			      newcoords[3].x, y2, barp);
			eline(rp, newcoords[2].x, y2,
			      newcoords[3].x, newcoords[3].y, barp);
		}
	}
	CopyMem(&newcoords[1], &coords[1],
	        (ENVEL_COORDSNUM - 2) * sizeof(struct envel_coord));
}

/* ------------------------------------------------------------- methods */

static void env_min_size(struct EG_Plugin *p, struct TextAttr *ta, WORD fh,
                         WORD *w, WORD *h)
{
	(void)p;
	(void)ta;
	*w = 18 * fh;
	*h = 10 * fh;
}

static void env_clear_render(struct EG_Plugin *p, struct Window *win)
{
	(void)win;
	((struct envel_plugin *)p)->rport = NULL;
}

static void env_render(struct EG_Plugin *p, struct TextAttr *ta, WORD x,
                       WORD y, WORD xs, WORD ys, struct Window *win)
{
	struct envel_plugin *e = (struct envel_plugin *)p;
	struct RastPort *rp = win->RPort;
	LONG ypos, axh, axix, a, s, t, temp, i;
	STRPTR str;

	(void)ta;
	e->rport = rp;
	e->whichm = SETNOTHING;
	a = xs / 20;
	str = LOC(STRID_HUNPERCENT);
	e->ssize = TextLength(rp, str, strlen((const char *)str));
	e->minw = rp->TxWidth;
	e->x1 = x + a + 5;
	e->x2 = x + xs - 3;
	e->y1 = y + 3 + rp->TxHeight;
	e->bottomtexty = y + ys - rp->TxHeight + rp->TxBaseline - 2;
	e->y2 = e->bottomtexty - rp->TxBaseline - 5 - (ys / 20);
	t = e->textpen;

	/* volume axis with ticks */
	axix = a / 3 * 2 + x + 3;
	eline(rp, axix, e->y1, axix, e->y2, t);
	axix--;
	a = a / 2;
	axh = ((LONG)(e->y2 - e->y1) << 16) / 20;
	ypos = (LONG)e->y1 << 16;
	s = a;
	for (i = 0; i <= 19; i++) {
		if (i == 10)
			s = a / 2;
		temp = (WORD)(ypos >> 16);
		eline(rp, axix, temp, axix - s, temp, t);
		ypos += axh;
		s = a / 4;
	}
	eline(rp, axix, e->y2, axix - a, e->y2, t);
	etext(rp, x, y + rp->TxBaseline + 1, LOC(STRID_OFVOLUME));

	ebox(rp, e->x1 - 1, e->y1 - 1, e->x2 + 1, e->y2 + 1, e->barpen);
	ebox(rp, e->x1, e->y1, e->x2, e->y2, e->paperpen);
	redrawenvelope(e, TRUE);
	updatebottomarea(e, TRUE);
	ReportMouse(TRUE, win);
}

static BOOL inbox(struct envel_plugin *e, LONG xm, LONG ym)
{
	return (ym >= e->y1) && (ym <= e->y2) && (xm >= e->x1) && (xm <= e->x2);
}

static BOOL env_message_test(struct EG_Plugin *p, struct IntuiMessage *imsg,
                             struct Window *win)
{
	struct envel_plugin *e = (struct envel_plugin *)p;
	LONG xm, ym, w;
	UWORD code = imsg->Code;

	switch (imsg->Class) {
	case IDCMP_MOUSEBUTTONS:
		xm = imsg->MouseX;
		ym = imsg->MouseY;
		if (code == SELECTDOWN || code == MENUDOWN) {
			if (e->whichm == SETNOTHING) {
				if (inbox(e, xm, ym)) {
					e->xm = xm;
					e->ym = ym;
					return TRUE;
				}
				rmbtrap(FALSE, win);
			} else {
				return TRUE;
			}
		} else if (code == SELECTUP || code == MENUUP) {
			if (e->whichm != SETNOTHING)
				return TRUE;
		}
		break;
	case IDCMP_MOUSEMOVE:
		xm = imsg->MouseX;
		ym = imsg->MouseY;
		if (e->whichm == SETNOTHING) {
			rmbtrap(inbox(e, xm, ym), win);
		} else if (e->wait != 1 && e->wait != -1) {
			e->xm = xm;
			e->ym = ym;
			return TRUE;
		}
		break;
	case IDCMP_ACTIVEWINDOW:
		rmbtrap(inbox(e, imsg->MouseX, imsg->MouseY), win);
		break;
	case IDCMP_INACTIVEWINDOW:
		rmbtrap(FALSE, win);
		if (e->whichm != SETNOTHING)
			return TRUE;
		break;
	case IDCMP_INTUITICKS:
		/* a button held still: count down, then step on every tick */
		if (e->whichm != SETNOTHING) {
			if ((w = e->wait) > 1) {
				e->wait = w - 1;
				if (w == 2)
					e->lastm = 0;
			} else if (w < -1) {
				e->wait = w + 1;
				if (w == -2)
					e->lastm = 0;
			} else if (w != 0) {
				return TRUE;
			}
		}
		break;
	case IDCMP_RAWKEY:
		if (code >= CURSORUP && code <= KEYCODE_F10)
			return TRUE;
		break;
	case IDCMP_VANILLAKEY:
		switch (code) {
		case ESC_CODE:
		case '0': case '1': case '2': case '3': case '4':
		case '5': case '6': case '7': case '8': case '9':
		case '.': case '_': case ')': case '(': case '-':
		case '+': case '=': case '*':
		case 9: case 13: case 32:
			return TRUE;
		default:
			return FALSE;
		}
	}
	return FALSE;
}

static BOOL env_message_action(struct EG_Plugin *p, ULONG class, UWORD qual,
                               UWORD code, struct Window *win)
{
	struct envel_plugin *e = (struct envel_plugin *)p;
	LONG v = 0, t, w;
	struct envel_coord *coords;

	(void)qual;
	if (class & IDCMP_RAWKEY) {
		e->keycode = code | MYRAWCODE;
		return TRUE;
	} else if (class & IDCMP_VANILLAKEY) {
		e->keycode = code;
		return TRUE;
	} else if (class & IDCMP_INTUITICKS) {
		/* button held: steps of 1 (with a pause after the first),
		 * after 30 ticks steps of 10 */
		w = e->whichm;
		if ((t = e->lastm) < 30) {
			e->lastm = t + 1;
			if (t == 0 || t > 3)
				v = e->wait;
			else
				v = 0;
		} else {
			v = e->wait * 10;
		}
		switch (w) {
		case SETATTACK:
			e->t1 = t = clamp255(e->t1 + v);
			break;
		case SETDECAY:
			e->t2 = t = clamp255(e->t2 + v);
			break;
		case SETRELEASE:
			e->t3 = t = clamp255(e->t3 + v);
			break;
		case SETSUSTAIN:
			e->slev = t = clamp255(e->slev + v);
			t = -t - 1;
			break;
		}
		redrawenvelope(e, FALSE);
		updatebottomvalue(e, t);
	} else if (class & IDCMP_INACTIVEWINDOW) {
		e->whichm = SETNOTHING;
		updatebottomarea(e, TRUE);
		e->keycode = -2;
		return TRUE;
	} else if (class & IDCMP_MOUSEBUTTONS) {
		if (code == SELECTDOWN || code == MENUDOWN) {
			if (e->whichm == SETNOTHING) {
				v = e->xm;
				coords = e->coords;
				if (v < coords[1].x) {
					e->whichm = SETATTACK;
					t = e->t1;
				} else if (v < coords[2].x) {
					e->whichm = SETDECAY;
					t = e->t2;
				} else if (v > coords[3].x) {
					e->whichm = SETRELEASE;
					t = e->t3;
				} else {
					e->whichm = SETSUSTAIN;
					v = e->ym;
					t = -(LONG)e->slev - 1;
				}
				e->lastm = v;
				updatebottomarea(e, FALSE);
				updatebottomvalue(e, t);
				e->wait = (code == SELECTDOWN) ? 4 : -4;
				return FALSE;
			} else {
				/* other button while editing: cancel */
				e->whichm = SETNOTHING;
				updatebottomarea(e, TRUE);
				e->keycode = -2;
				return TRUE;
			}
		} else if (code == SELECTUP || code == MENUUP) {
			e->whichm = SETNOTHING;
			updatebottomarea(e, TRUE);
			e->keycode = -1;
			return TRUE;
		}
	} else if (class & IDCMP_MOUSEMOVE) {
		ReportMouse(FALSE, win);
		e->wait = 0;
		w = e->whichm;
		if (w == SETSUSTAIN) {
			v = e->y2 - e->ym;
			t = e->y2 - e->y1;
			t = 256 * v / t;
			e->slev = t = clamp255(t);
			t = -t - 1;
		} else {
			/* small moves are ignored, medium ones step by 1 */
			v = e->xm;
			t = v - e->lastm;
			v = e->minw;
			if (-v < t && t < v) {
				t = 0;
			} else if (-(v * 2) < t && t < (v * 2)) {
				t = (t > 0) ? 1 : -1;
				e->lastm = e->xm;
			} else {
				e->lastm = e->xm;
			}
			switch (w) {
			case SETATTACK:
				e->t1 = t = clamp255(e->t1 + t);
				break;
			case SETDECAY:
				e->t2 = t = clamp255(e->t2 + t);
				break;
			case SETRELEASE:
				e->t3 = t = clamp255(e->t3 + t);
				break;
			}
		}
		redrawenvelope(e, FALSE);
		updatebottomvalue(e, t);
		ReportMouse(TRUE, win);
	}
	return FALSE;
}

static void env_dispose(struct EG_Plugin *p)
{
	e_dispose(p);
}

static const struct EG_PluginClass envel_class = {
	env_min_size,
	NULL,                   /* will_resize: both ways */
	env_render,
	env_clear_render,
	env_message_test,
	env_message_action,
	env_dispose
};

/* ------------------------------------------------------------- public */

void envel_setenvelope(struct envel_plugin *e, LONG attack, LONG decay,
                       LONG sustain, LONG release)
{
	BOOL w = FALSE;
	LONG val = 0;

	if (e->t1 != attack) {
		e->t1 = attack;
		w = TRUE;
	}
	if (e->t2 != decay) {
		e->t2 = decay;
		w = TRUE;
	}
	if (e->slev != sustain) {
		e->slev = sustain;
		w = TRUE;
	}
	if (e->t3 != release) {
		e->t3 = release;
		w = TRUE;
	}
	if (!w)
		return;
	redrawenvelope(e, FALSE);
	if (e->whichm == SETNOTHING) {
		updatebottomarea(e, TRUE);
	} else {
		switch (e->whichm) {
		case SETSUSTAIN:
			val = -(LONG)e->slev - 1;
			break;
		case SETATTACK:
			val = e->t1;
			break;
		case SETDECAY:
			val = e->t2;
			break;
		case SETRELEASE:
			val = e->t3;
			break;
		}
		updatebottomvalue(e, val);
	}
}

void envel_getenvelope(struct envel_plugin *e, LONG *attack, LONG *decay,
                       LONG *sustain, LONG *release)
{
	if (attack)
		*attack = e->t1;
	if (decay)
		*decay = e->t2;
	if (sustain)
		*sustain = e->slev;
	if (release)
		*release = e->t3;
}

struct envel_plugin *envel_new(struct Screen *screen)
{
	struct envel_plugin *e;
	struct DrawInfo *drinfo;
	struct Screen *scr;
	UWORD *pens;
	LONG penless;

	e = e_new(sizeof(*e));
	e->plugin.cls = &envel_class;
	e->keycode = -1;
	e->slev = 255;
	e->textpen = 1;
	e->paperpen = 2;
	e->barpen = 3;
	scr = screen ? screen : LockPubScreen(NULL);
	if (scr) {
		if ((drinfo = GetScreenDrawInfo(scr))) {
			penless = drinfo->dri_NumPens;
			pens = drinfo->dri_Pens;
			if (penless > TEXTPEN)
				e->textpen = pens[TEXTPEN];
			if (penless > FILLPEN)
				e->barpen = pens[FILLPEN];
			if (penless > SHINEPEN)
				e->paperpen = pens[SHINEPEN];
			FreeScreenDrawInfo(scr, drinfo);
		}
		if (!screen)
			UnlockPubScreen(NULL, scr);
	}
	return e;
}

void envel_dispose(struct envel_plugin *e)
{
	e_dispose(e);
}
