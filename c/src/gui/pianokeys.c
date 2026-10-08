/*
 * pianokeys.c - port of mbgui.e (the "pianokeys" EasyGUI plugin)
 *
 * Differences from the E code:
 * - bounds() returns the high bound through a pointer instead of as a
 *   second return value (-1 there when there is no range).
 * - E's render set the global stdrast to the window's rastport; drawing
 *   here goes to the plugin's rport explicitly and nothing else changes.
 * - The plugin is freed by the class's dispose (E never ENDed it).
 * - pianokeypressed() stays with the main window code (mbwindow.e), as it
 *   sets the window's note text gadget.
 */
#include <exec/types.h>
#include <exec/libraries.h>
#include <devices/inputevent.h>
#include <graphics/gfx.h>
#include <graphics/gfxbase.h>
#include <graphics/rastport.h>
#include <graphics/view.h>
#include <intuition/intuition.h>
#include <intuition/screens.h>
#include <utility/tagitem.h>
#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/intuition.h>

#include "../app/eport.h"
#include "../app/play.h"
#include "drawpattern.h"
#include "egui.h"
#include "keycodes.h"
#include "pianokeys.h"

/* pushkey() actions */
enum { KEY_REFRESH, KEY_SET, KEY_UNSET, KEY_PRHI, KEY_PRLO, KEY_PRNO,
       KEY_PLAYON, KEY_PLAYOFF, KEY_CURRENT };

/* status bits; the drawn state is kept in the high nibble */
#define STS_LO          1
#define STS_HI          2
#define STS_SET         4
#define STS_PLAY        8
#define STS_CURRENT     (STS_LO | STS_HI | STS_SET | STS_PLAY)
#define STSMASK_PLAY    STS_PLAY
#define STSMASK_RANGE   (STS_HI | STS_LO | STS_SET)
#define STSMASK_STATUS  (STS_PLAY | STS_HI | STS_LO | STS_SET)
#define STSBITS_SHIFT   4

#define ABS(a) ((a) < 0 ? -(a) : (a))

/* blink phase of the current key while the base note is set */
static LONG lasttick;

/* "midiIn" above the keys: x offset, then x1,y1,x2,y2 lines, -1; -1 */
static const WORD midiin_text[] = {
	0, 0,0,0,4, 1,3,1,4, 1,0,2,0, 3,1,4,1, 5,0,6,0, 7,0,7,4, 6,3,6,4, -1,
	9, 0,0,0,4, 1,3,1,4, -1,
	12, 0,0,0,2, 0,4,2,4, 0,3,1,3, 1,0,2,0, 3,1,4,2, 4,3,3,4, -1,
	18, 0,3,0,4, 1,0,1,4, -1,
	21, 0,0,2,0, 1,1,1,2, 1,3,2,3, 0,4,2,4, -1,
	25, 0,0,0,4, 1,3,1,4, 1,1,3,3, 4,3,4,4, 5,0,5,4, -1,
	-1
};

/* E's Box, Line and Plot */
static void box(struct RastPort *rp, LONG x1, LONG y1, LONG x2, LONG y2,
                LONG col)
{
	SetAPen(rp, col);
	RectFill(rp, x1, y1, x2, y2);
}

static void line(struct RastPort *rp, LONG x1, LONG y1, LONG x2, LONG y2,
                 LONG col)
{
	SetAPen(rp, col);
	Move(rp, x1, y1);
	Draw(rp, x2, y2);
}

static void plot(struct RastPort *rp, LONG x, LONG y, LONG col)
{
	SetAPen(rp, col);
	WritePixel(rp, x, y);
}

/* ------------------------------------------------------------ pushkey */

static void pushkey(struct pianokeys *p, LONG key, LONG col)
{
	struct RastPort *rp;
	LONG st, ds, k, a, x, y, bsize, xsize, smallsize, top, bottom;
	LONG col1, col2, col3 = 0, bt2 = 0, tp2 = 0;

	k = key & 127;

	st = p->status[k];
	switch (col) {
	case KEY_SET:
		st |= STS_SET;
		break;
	case KEY_UNSET:
		st &= ~STS_SET;
		break;
	case KEY_PRHI:
		st = (st & ~STS_LO) | STS_HI;
		break;
	case KEY_PRLO:
		st = (st & ~STS_HI) | STS_LO;
		break;
	case KEY_PRNO:
		st &= ~(STS_HI | STS_LO);
		break;
	case KEY_PLAYON:
		p->playingkeys[k] = p->playingkeys[k] + 1;
		st |= STS_PLAY;
		break;
	case KEY_PLAYOFF:
		if ((a = p->playingkeys[k] - 1) <= 0) {
			st &= ~STS_PLAY;
			a = 0;
		} else {
			st |= STS_PLAY;
		}
		p->playingkeys[k] = a;
		break;
	case KEY_CURRENT:
		k = p->currentkey;
		p->currentkey = key;
		if (key != k && k < 128)
			pushkey(p, k, KEY_REFRESH);
		if (key > 127) {
			p->currentkey = k;
			return;
		}
		k = key;
		break;
	default:
		if (col != KEY_REFRESH)
			return;
		break;
	}

	/* nothing to draw if the drawn state is the same */
	if (col != KEY_REFRESH) {
		ds = (st >> STSBITS_SHIFT) & STSMASK_STATUS;
		if (k == p->currentkey) {
			if (ds == STS_CURRENT) {
				p->status[k] = st;
				return;
			}
		} else if (p->loarea > 127) {
			if ((st & STSMASK_PLAY) == ds) {
				p->status[k] = st;
				return;
			}
		} else {
			if ((st & STSMASK_RANGE) == ds) {
				p->status[k] = st;
				return;
			}
			if (st & STS_SET) {
				if (((STSMASK_RANGE - STS_LO) & st) == (ds & ~STS_LO)) {
					p->status[k] = st;
					return;
				}
			}
		}
	}

	smallsize = p->smallsize;
	top = 2;
	bottom = p->keysize - 2;

	if (k == p->currentkey) {
		col1 = p->currentpen;
		col2 = p->currentpen;
		ds = STS_CURRENT;
	} else {
		ds = st & STSMASK_STATUS;
		if (p->loarea > 127) {
			if ((ds = ds & STSMASK_PLAY)) {
				col1 = p->activepen;
				col2 = col1;
			} else {
				col1 = p->backpen;
				col2 = p->darkpen;
			}
		} else {
			ds &= STSMASK_RANGE;
			col1 = p->backpen;
			col2 = p->darkpen;
			col3 = p->activepen;
			if (ds & STS_SET) {
				if (ds & STS_HI)
					tp2 = 2;
				else
					tp2 = smallsize / 2;
				bt2 = (p->keysize - smallsize) / 2 + smallsize;
			} else if (ds & STS_HI) {
				tp2 = 2;
				bt2 = smallsize / 2 + 1;
			} else if (ds & STS_LO) {
				tp2 = smallsize / 2 + 2;
				bt2 = smallsize - 1;
			}
		}
	}
	p->status[k] = (st & STSMASK_STATUS) | (ds << STSBITS_SHIFT);

	if (!(rp = p->rport))
		return;

	xsize = p->xsize;
	bsize = xsize / 3;
	x = k / 12 * (7 * xsize) + p->xx;
	y = p->yy;

	k = k % 12;
	if ((1 << k) & 0x54A) {         /* %010101001010: black keys */
		bsize--;
		x = (k + 2) / 2 * xsize + x;
		box(rp, x - bsize, y + top, x + bsize, y + smallsize - 1, col2);
		if (bt2)
			box(rp, x - bsize, y + tp2, x + bsize,
			    y + (bt2 >= smallsize ? smallsize - 1 : bt2), col3);
		plot(rp, x - bsize, y + 3, p->shadowpen);
		plot(rp, x - bsize, y + 2, p->backpen);
	} else {
		LONG l, r;

		bsize++;
		k = (k + 1) / 2;
		x = k * xsize + x;
		a = (k - k / 5) % 3;

		/* narrower at the top beside black keys */
		l = x + (((a + 1) & 2) ? bsize : 1);
		r = x + xsize - ((a & 2) ? 1 : bsize);
		box(rp, l, y + top, r, y + smallsize, col1);
		if (bt2)
			box(rp, l, y + tp2, r, y + bt2, col3);
		box(rp, x + 1, y + smallsize + 1, x + xsize - 1, y + bottom, col1);
		if (bt2 > smallsize)
			box(rp, x + 1, y + smallsize + 1, x + xsize - 1, y + bt2,
			    col3);
	}
}

/* ------------------------------------------------------------ methods */

static void pk_min_size(struct EG_Plugin *pl, struct TextAttr *ta, WORD fh,
                        WORD *w, WORD *h)
{
	(void)pl;
	(void)ta;
	(void)fh;
	*w = 462;
	*h = 38;
}

static void pk_clear_render(struct EG_Plugin *pl, struct Window *win)
{
	struct pianokeys *p = (struct pianokeys *)pl;
	struct ColorMap *cmap;
	LONG x;

	p->rport = NULL;
	if (p->allocpens != 0) {
		cmap = win->WScreen->ViewPort.ColorMap;
		x = 1;
		if (p->allocpens & x)
			ReleasePen(cmap, p->darkpen);
		x <<= 1;
		if (p->allocpens & x)
			ReleasePen(cmap, p->activepen);
		x <<= 1;
		if (p->allocpens & x)
			ReleasePen(cmap, p->backpen);
		x <<= 1;
		if (p->allocpens & x)
			ReleasePen(cmap, p->currentpen);
		x <<= 1;
		if (p->allocpens & x)
			ReleasePen(cmap, p->shadowpen);
		p->allocpens = 0;
	}
	p->ptrnalloc = freepattern(p->ptrnalloc);
}

static LONG bestpen(struct ColorMap *cmap, ULONG r, ULONG g, ULONG b)
{
	return ObtainBestPen(cmap, r, g, b,
	                     OBP_Precision, PRECISION_EXACT, TAG_DONE);
}

static void pk_render(struct EG_Plugin *pl, struct TextAttr *ta, WORD wx,
                      WORD wy, WORD wxs, WORD wys, struct Window *win)
{
	struct pianokeys *p = (struct pianokeys *)pl;
	struct RastPort *rp;
	struct ColorMap *cmap;
	const WORD *pt;
	LONG x = wx, y = wy, xs = wxs, ys = wys;
	LONG keysize, smallkey, xsize, f, xx, yy, t, b, sh, c, bsize, yn, xn;
	LONG col, bit, x1, x2, y1, y2;

	(void)ta;

	if (((struct Library *)GfxBase)->lib_Version >= 39) {
		cmap = win->WScreen->ViewPort.ColorMap;
		bit = 1;
		if ((col = bestpen(cmap, 0x0, 0x0, 0x0)) >= 0)
			p->darkpen = col;
		if (col >= 0)
			p->allocpens = bit;
		bit <<= 1;
		if ((col = bestpen(cmap, 0x00000000, 0x88888888, 0xFFFFFFFF)) >= 0)
			p->activepen = col;
		if (col >= 0)
			p->allocpens |= bit;
		bit <<= 1;
		if ((col = bestpen(cmap, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF)) >= 0)
			p->backpen = col;
		if (col >= 0)
			p->allocpens |= bit;
		bit <<= 1;
		if ((col = bestpen(cmap, 0xFFFFFFFF, 0xC0C0C0C0, 0x00000000)) >= 0)
			p->currentpen = col;
		if (col >= 0)
			p->allocpens |= bit;
		bit <<= 1;
		if ((col = bestpen(cmap, 0x80808080, 0x80808080, 0x80808080)) >= 0)
			p->shadowpen = col;
		if (col >= 0)
			p->allocpens |= bit;
		p->ptrnalloc = drawpattern(win->RPort, cmap, x, y, xs, ys,
		                           pianokeys_patterncmap, pianokeys_pattern,
		                           100, 100);
	}
	t = p->darkpen;
	b = p->backpen;
	sh = p->shadowpen;

	keysize = ys / 4 * 3;
	smallkey = keysize / 3 * 2 - 1;
	xsize = (xs - 12) / 75;
	bsize = xsize / 3;

	p->xsize = xsize;

	xx = (xs - xsize * 75) / 2 + x;
	yy = ys - keysize + y;
	p->xx = xx;
	p->yy = yy;

	keysize--;
	p->keysize = keysize;

	p->rport = rp = win->RPort;

	if (!p->ptrnalloc)
		box(rp, x, y, x + xs - 1, y + ys - 1, p->activepen);
	box(rp, xx - 1, yy - 9, xx + 33, yy - 1, t);
	line(rp, xx + 34, yy - 1, xx + xsize * 75 + 1, yy - 1, t);
	line(rp, xx - 1, yy, xx - 1, yy + keysize - 2, t);
	line(rp, xx + xsize * 75 + 1, yy, xx + xsize * 75 + 1, yy + keysize - 2, t);

	line(rp, x, y, x + xs - 2, y, p->shinepen);
	line(rp, x, y + 1, x, y + ys - 1, p->shinepen);
	line(rp, x + 1, y + ys - 1, x + xs - 1, y + ys - 1, p->darkpen);
	line(rp, x + xs - 1, y, x + xs - 1, y + ys - 1, p->darkpen);

	pt = midiin_text;
	yn = yy - 7;
	while ((c = *pt++) != -1) {
		while ((x1 = *pt++) != -1) {
			y1 = *pt++;
			x2 = *pt++;
			y2 = *pt++;
			line(rp, xx + x1 + c + 1, yn + y1, xx + x2 + c + 1, yn + y2,
			     p->currentpen);
		}
	}

	c = 0;
	yn = yy + keysize - 1;
	xn = xx + xsize - 1;
	box(rp, xx + 1, yy, 74 * xsize + xn, yn, b);
	for (f = 0; f <= 74; f++) {
		line(rp, xx, yy, xx, yn, t);
		line(rp, xx + 2, yn + 1, xn, yn + 1, t);
		line(rp, xx + 2, yn, xn, yn, sh);
		line(rp, xx, yn + 1, xx + 1, yn + 1, sh);
		if (c != 0 && c != 3) {         /* black key left of this one */
			box(rp, xx - bsize, yy + 1, xx + bsize, yy + smallkey, t);
			plot(rp, xx - bsize + 1, yy + 2, b);
			plot(rp, xx - bsize, yy + 1, sh);
		}
		c++;
		if (c > 6)
			c = 0;
		xx += xsize;
		xn += xsize;
	}
	line(rp, xx, yy, xx, yy + keysize - 1, t);
	p->smallsize = smallkey;

	for (f = 0; f <= 127; f++)
		pushkey(p, f, KEY_REFRESH);
}

static BOOL pk_message_test(struct EG_Plugin *pl, struct IntuiMessage *imsg,
                            struct Window *win)
{
	struct pianokeys *p = (struct pianokeys *)pl;
	ULONG class = imsg->Class;
	LONG code = imsg->Code;
	LONG xm, ym;

	(void)win;

	if (class & (IDCMP_MOUSEBUTTONS | IDCMP_MOUSEMOVE)) {
		xm = imsg->MouseX - p->xx;
		ym = imsg->MouseY - p->yy;
		if (code == SELECTDOWN) {
			if (xm < 0 || ym < 0 || xm >= p->xsize * 75 ||
			    ym >= p->keysize)
				return FALSE;
		}
		if (code == SELECTUP || (class & IDCMP_MOUSEMOVE)) {
			if (!p->windowreport)
				return FALSE;
		}
		p->xm = xm;
		p->ym = ym;
		return TRUE;
	} else if (class & IDCMP_RAWKEY) {
		if (code >= CURSORUP && code <= KEYCODE_F10)
			return TRUE;
	} else if (class & IDCMP_VANILLAKEY) {
		switch (code) {
		case '0': case '1': case '2': case '3': case '4':
		case '5': case '6': case '7': case '8': case '9':
		case '.': case '_': case ')': case '(': case '-':
		case '+': case '=': case '*':
		case 9: case 13: case 32: case DEL_CODE:
			return TRUE;
		default:
			return FALSE;
		}
	} else if (class & IDCMP_INTUITICKS) {
		if (basesetb)
			return TRUE;
	} else if (class & IDCMP_INACTIVEWINDOW) {
		return TRUE;
	}
	return FALSE;
}

static BOOL pk_message_action(struct EG_Plugin *pl, ULONG class, UWORD qual,
                              UWORD ucode, struct Window *win)
{
	struct pianokeys *p = (struct pianokeys *)pl;
	LONG code = ucode;
	LONG xsize, xm, ym, oct, mod, key;

	if (class & IDCMP_RAWKEY) {
		p->keycode = code | MYRAWCODE |
		             ((qual & (IEQUALIFIER_LSHIFT | IEQUALIFIER_RSHIFT)) ?
		              SHIFTQUAL : 0);
	} else if (class & IDCMP_VANILLAKEY) {
		p->keycode = code;
	} else if (class & IDCMP_INTUITICKS) {
		if (basesetb) {
			if (lasttick)
				pushkey(p, 255, KEY_CURRENT);
			else
				pushkey(p, p->currentkey, KEY_CURRENT);
			lasttick = ~lasttick;
		}
		return FALSE;
	} else if (class & IDCMP_INACTIVEWINDOW) {
		ReportMouse(FALSE, win);
		p->windowreport = FALSE;
		if (basesetb) {
			p->keycode = -2;
			return TRUE;
		}
		return FALSE;
	} else if (class & (IDCMP_MOUSEBUTTONS | IDCMP_MOUSEMOVE)) {
		p->keycode = -1;

		if (code == SELECTUP) {
			if (p->windowreport)
				ReportMouse(FALSE, win);
			p->windowreport = FALSE;
			if (p->boundselect == 0)
				return FALSE;
		}
		if (class & IDCMP_MOUSEMOVE) {
			if (p->windowreport)
				ReportMouse(FALSE, win);
			if (p->boundselect == 0) {
				p->windowreport = FALSE;
				return FALSE;
			}
			code = SELECTDOWN;
		}

		xm = p->xm;
		ym = p->ym;
		if (xm < 0 || ym < 0 || xm >= p->xsize * 75 || ym >= p->keysize)
			return FALSE;

		xsize = p->xsize;

		/* in the upper part: a black key, if near one */
		key = -1;
		oct = 0;
		if (ym < p->smallsize) {
			mod = (xm + xsize / 2) / xsize;
			oct = mod / 7;
			mod = mod % 7;
			if (mod != 3)
				key = mod * 2 - mod / 4 - 1;
		}

		if (key < 0) {
			mod = xm / xsize;
			oct = mod / 7;
			mod = mod % 7;
			key = mod * 2 - (mod + 1) / 4;
		}

		key = key + oct * 12;
		if (key > 127)
			key = 127;

		pianokeys_autokey(p, key, code);

		if (code == SELECTDOWN) {
			p->windowreport = TRUE;
			ReportMouse(TRUE, win);
		}
	} else {
		return FALSE;
	}
	return TRUE;
}

static void pk_dispose(struct EG_Plugin *pl)
{
	pianokeys_dispose((struct pianokeys *)pl);
}

static const struct EG_PluginClass pianokeys_class = {
	pk_min_size,
	NULL,                   /* will_resize: default */
	pk_render,
	pk_clear_render,
	pk_message_test,
	pk_message_action,
	pk_dispose
};

/* ------------------------------------------------------------ public */

struct pianokeys *pianokeys_new(struct Screen *screen)
{
	struct pianokeys *p;
	struct DrawInfo *drinfo;
	struct Screen *scr;
	LONG penless;
	UWORD *x;
	LONG f;

	p = e_new(sizeof(struct pianokeys));
	p->plugin.cls = &pianokeys_class;

	for (f = 0; f <= 127; f++) {
		p->status[f] = 0;
		p->playingkeys[f] = 0;
	}
	lasttick = 0;
	p->keycode = -1;
	p->boundset = 0;
	p->boundselect = 0;
	p->currentkey = 255;
	p->loarea = 255;
	p->hiarea = 127;
	p->darkpen = 1;
	p->activepen = 2;
	p->backpen = 0;
	p->bgrnpen = 0;
	p->currentpen = 3;
	p->shadowpen = 0;
	p->allocpens = 0;
	p->shinepen = 3;
	if (screen == NULL)
		scr = LockPubScreen(NULL);
	else
		scr = screen;
	if (scr) {
		if ((drinfo = GetScreenDrawInfo(scr))) {
			penless = drinfo->dri_NumPens;
			x = drinfo->dri_Pens;
			if (penless > SHADOWPEN)
				p->darkpen = x[SHADOWPEN];
			if (penless > FILLPEN)
				p->activepen = x[FILLPEN];
			if (penless > BACKGROUNDPEN)
				p->backpen = x[BACKGROUNDPEN];
			if (penless > BACKGROUNDPEN)
				p->bgrnpen = x[BACKGROUNDPEN];
			if (penless > BACKGROUNDPEN)
				p->shadowpen = x[BACKGROUNDPEN];
			if (penless > HIGHLIGHTTEXTPEN)
				p->currentpen = x[HIGHLIGHTTEXTPEN];
			if (penless > SHINEPEN)
				if (x[SHINEPEN] != p->currentpen)
					p->backpen = x[SHINEPEN];
			if (penless > SHINEPEN)
				p->shinepen = x[SHINEPEN];
			FreeScreenDrawInfo(scr, drinfo);
		}
		if (screen == NULL)
			UnlockPubScreen(NULL, scr);
	}
	return p;
}

void pianokeys_dispose(struct pianokeys *p)
{
	e_dispose(p);
}

LONG pianokeys_keypressed(struct pianokeys *p, LONG key)
{
	LONG x = p->currentkey;

	if (key >= 0)
		pushkey(p, key & 255, KEY_CURRENT);
	return x < 128 ? x : -1;
}

LONG pianokeys_bounds(struct pianokeys *p, LONG l, LONG h,
                      const UBYTE *rangeother, LONG *hi)
{
	LONG lo, oldhi, f;

	lo = p->loarea;
	oldhi = p->hiarea;
	if (l >= 0) {
		if (l > 127) {
			p->loarea = 255;
			p->hiarea = 127;
			p->boundselect = 0;
			p->boundset = 0;
			if (lo < 128)
				for (f = 0; f <= 127; f++)
					pushkey(p, f, KEY_UNSET);
		} else if (l <= h) {
			p->loarea = l;
			p->hiarea = h;
			if (rangeother != NULL) {
				for (f = 0; f <= 127; f++) {
					if (rangeother[f] == STS_LO)
						p->status[f] = (p->status[f] & ~(STS_LO | STS_HI)) | STS_LO;
					else if (rangeother[f] == STS_HI)
						p->status[f] = (p->status[f] & ~(STS_LO | STS_HI)) | STS_HI;
					else
						p->status[f] = p->status[f] & ~(STS_LO | STS_HI);
					pushkey(p, f, (f >= l && f <= h) ? KEY_SET : KEY_UNSET);
				}
			} else {
				if (l > 0)
					for (f = 0; f <= l - 1; f++)
						pushkey(p, f, KEY_UNSET);
				for (f = l; f <= h; f++)
					pushkey(p, f, KEY_SET);
				if (h < 127)
					for (f = h + 1; f <= 127; f++)
						pushkey(p, f, KEY_UNSET);
			}
		}
	}
	if (lo < 128 && lo <= oldhi) {
		if (hi)
			*hi = oldhi;
		return lo;
	}
	if (hi)
		*hi = -1;
	return -1;
}

void pianokeys_boundset(struct pianokeys *p, LONG set)
{
	if (set)
		p->boundset = 1;
	else
		p->boundset = 0;
	p->boundselect = 0;
}

void pianokeys_setplaying(struct pianokeys *p, LONG x, LONG on)
{
	if ((x & 127) != x)
		return;
	pushkey(p, x, on ? KEY_PLAYON : KEY_PLAYOFF);
}

LONG pianokeys_autokey(struct pianokeys *p, LONG key, LONG code)
{
	LONG lo, hi, bnd, f;

	if (code == SELECTDOWN)
		p->boundselect = 0;
	lo = p->loarea;
	hi = p->hiarea;
	if (p->boundset > 0) {
		bnd = p->boundselect;
		if (bnd == 0) {                 /* key down: pick the nearer bound */
			if (lo < 128) {
				if (hi - lo == 1 && key == lo) {
					p->hiarea = key;
					p->boundselect = 2;
					pushkey(p, hi, KEY_UNSET);
				} else if (ABS(key - lo) < ABS(key - hi) ||
				           (lo == hi && key <= lo)) {
					p->loarea = key;
					p->boundselect = 1;
					if (key < lo)
						for (f = key; f <= lo - 1; f++)
							pushkey(p, f, KEY_SET);
					if (lo < key)
						for (f = lo; f <= key - 1; f++)
							pushkey(p, f, KEY_UNSET);
				} else {
					p->hiarea = key;
					p->boundselect = 2;
					if (key > hi)
						for (f = hi + 1; f <= key; f++)
							pushkey(p, f, KEY_SET);
					if (hi > key)
						for (f = key + 1; f <= hi; f++)
							pushkey(p, f, KEY_UNSET);
				}
			}
		} else if (bnd == 1) {          /* key up: low bound */
			if (key > hi)
				key = hi;
			p->loarea = key;
			p->boundselect = 0;
			if (key < lo)
				for (f = key; f <= lo - 1; f++)
					pushkey(p, f, KEY_SET);
			if (lo < key)
				for (f = lo; f <= key - 1; f++)
					pushkey(p, f, KEY_UNSET);
		} else if (bnd == 2) {          /* key up: high bound */
			if (key < lo)
				key = lo;
			p->hiarea = key;
			p->boundselect = 0;
			if (key > hi)
				for (f = hi + 1; f <= key; f++)
					pushkey(p, f, KEY_SET);
			if (hi > key)
				for (f = key + 1; f <= hi; f++)
					pushkey(p, f, KEY_UNSET);
		} else {
			p->boundselect = 0;
		}
	} else {
		pushkey(p, key, KEY_CURRENT);
	}
	return TRUE;
}
