/*
 * scopes.c - port of mbscopes.e: the oscilloscope window. A plugin marks
 * the drawing area; a low priority process draws the left and right
 * channels of the last mixed buffer there once per frame, only changing
 * the pixels that moved.
 *
 * Locking as in the E code: scoperpsem is held by the main task while the
 * area is not valid (from open_scopewindow until the first render, from
 * clear_render or IDCMP_SIZEVERIFY until the next render); the process
 * only draws when AttemptSemaphore() gets it. scopetasksem is held by the
 * process while it runs.
 *
 * Differences from the E code:
 * - The process is a dos process (CreateNewProcTags) instead of an E code
 *   task; it ends with Forbid() before its last semaphore release, so it
 *   is gone before the main task can go on and unload the code.
 * - The semaphores and the plugin object are static instead of allocated
 *   (the E code never freed them).
 * - The sample scaling is MULS/DIVS as in the E code (inline asm, for
 *   speed); the drawing loop is C with the same pixel order.
 * - The number of frames drawn is also limited to snd_scopelen, and an
 *   empty buffer pointer counts as "no data" (the E code read past or
 *   from address 0 in those cases, which the engines never cause).
 * - clean_scope only touches the process if it was started, and only
 *   releases scoperpsem if the main task holds it (the E code did both
 *   unconditionally, wrong after a failed open_scopewindow).
 * - The unused meter arrow code (commented out in the E file) is not
 *   ported.
 * - snd_scope_update() is called before each frame is drawn, so engines
 *   without a mixed buffer (Paula 4 channel) can fill one.
 */
#include <exec/types.h>
#include <exec/execbase.h>
#include <exec/semaphores.h>
#include <exec/tasks.h>
#include <dos/dostags.h>
#include <intuition/intuition.h>
#include <intuition/screens.h>
#include <graphics/rastport.h>
#include <graphics/view.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/graphics.h>
#include <proto/intuition.h>

#include "egui.h"
#include "scopes.h"
#include "../app/eport.h"
#include "../app/globals.h"
#include "../app/locale.h"
#include "../app/diskoper.h"
#include "../audio/snd.h"

#define MAXWIDTH 256
#define ESC_CODE 27
#define TAB_CODE 9

struct scope {
	struct EG_Plugin base;
	LONG code;
	/* private */
	WORD xpl, xpr, yp;
	WORD radw, radh;
};

static struct Task *volatile scopetask;         /* the drawing process */
static struct SignalSemaphore scoperpsem, scopetasksem;
static struct RastPort *volatile scoperp;
static volatile LONG scopechange;       /* TRUE: do not erase old pixels */
static struct scope scplug;

static LONG greeencolour;               /* pen, colormap for ReleasePen */
static struct ColorMap *colormap;

static void close_scope(EG_Gui *g, EG_Obj *obj, LONG value);
static void clean_scope(EG_Gui *g, EG_Obj *obj, LONG value);
static void scopeact(EG_Gui *g, EG_Obj *obj, LONG value);
static void subtaskscopes(void);
static void scope_init(struct scope *sc, struct Screen *screen);
static const struct EG_PluginClass scope_class;

void open_scopewindow(struct Screen *screen, struct TextAttr *ta)
{
	struct EG_WinOpts opts;
	struct Window *window;

	InitSemaphore(&scoperpsem);
	InitSemaphore(&scopetasksem);
	ObtainSemaphore(&scoperpsem);

	E_TRY {
		scope_init(&scplug, screen);
		opts.menu = NULL;
		opts.awproc = NULL;
		opts.close = close_scope;
		opts.clean = clean_scope;
		opts.info = (APTR)111;
		opts.screen = screen;
		opts.font = ta;
		opts.left = mbprefs.scopewinx;
		opts.top = mbprefs.scopewiny;
		opts.width = mbprefs.scopewinw;
		opts.height = mbprefs.scopewinh;
		opts.wtype = WTYPE_SIZE;
		opts.hide = mbprefs.scopehide ? TRUE : FALSE;
		opts.screentitle = NULL;
		scopegh = eg_add(mh, LOC(STRID_MIDIINSCOPE),
			eg_bevel(eg_plugin(scopeact, &scplug.base)), &opts);
		if (!scopegh)
			Raise('GUI');

		if ((window = scopegh->wnd)) {
			ModifyIDCMP(window, window->IDCMPFlags | IDCMP_SIZEVERIFY);
			SetWindowTitles(window, (CONST_STRPTR)-1, mainbartext);
		}

		ObtainSemaphore(&scopetasksem);
		scopetask = (struct Task *)CreateNewProcTags(
			NP_Entry, (ULONG)subtaskscopes,
			NP_Name, (ULONG)"midiIn_SCOPE",
			NP_Priority, -25,
			NP_StackSize, 8192,
			TAG_DONE);
		if (!scopetask)
			Raise('TASK');
		ReleaseSemaphore(&scopetasksem);
	} E_EXCEPT {
		if (scoperpsem.ss_NestCount)
			ReleaseSemaphore(&scoperpsem);
		if (scopetasksem.ss_NestCount)
			ReleaseSemaphore(&scopetasksem);
		ReThrow();
	} E_END;
}

void scopewindowopen(EG_Gui *g, EG_Obj *obj, LONG value)
{
	struct Window *window;

	(void)g;
	(void)obj;
	(void)value;
	if (scopegh->wnd) {
		ActivateWindow(scopegh->wnd);
		WindowToFront(scopegh->wnd);
	} else {
		eg_openwin(scopegh);
		if ((window = scopegh->wnd)) {
			ModifyIDCMP(window, window->IDCMPFlags | IDCMP_SIZEVERIFY);
			SetWindowTitles(window, (CONST_STRPTR)-1, mainbartext);
		}
	}
}

static void close_scope(EG_Gui *g, EG_Obj *obj, LONG value)
{
	(void)g;
	(void)obj;
	(void)value;
	if (scopegh->wnd)
		eg_closewin(scopegh);
}

static void clean_scope(EG_Gui *g, EG_Obj *obj, LONG value)
{
	struct Task *t;

	(void)g;
	(void)obj;
	(void)value;
	if (colormap)
		ReleasePen(colormap, greeencolour);
	if ((t = scopetask)) {
		SetTaskPri(FindTask(NULL), 0);
		SetTaskPri(t, 1);
	}
	scopetask = NULL;
	ObtainSemaphore(&scopetasksem);         /* wait for the process */
	ReleaseSemaphore(&scopetasksem);
	scopegh = NULL;
	scoperp = NULL;
	if (scoperpsem.ss_NestCount)
		ReleaseSemaphore(&scoperpsem);
}

/* ---------------------------------------------------------- the process */

/* sample * h / 32767 with the 16 bit MULS/DIVS of the E code */
static inline WORD scale(WORD s, WORD h)
{
	LONG v = s;

	__asm__("muls.w %1,%0\n\tdivs.w #32767,%0" : "+d" (v) : "d" (h) : "cc");
	return (WORD)v;
}

static void subtaskscopes(void)
{
	ULONG buf1[MAXWIDTH], buf2[MAXWIDTH];
	ULONG *olddata = buf1, *tadata = buf2, *t, *o;
	APTR sdata;
	const WORD *p;
	LONG w, h, c, xl, xr, y;
	WORD nl, nr, ol, or;
	ULONG n, od;
	struct RastPort *rp;
	BOOL cleared = TRUE;

	ObtainSemaphore(&scopetasksem);
	while (scopetask) {
		WaitTOF();
		if (!AttemptSemaphore(&scoperpsem))
			continue;
		snd_scope_update();
		sdata = snd_scopedata;
		p = sdata ? *(const WORD **)sdata : NULL;
		if (p) {
			cleared = FALSE;
			w = scplug.radw * 2 - 2;
			if (w >= MAXWIDTH)
				w = MAXWIDTH - 1;
			if (w >= (LONG)snd_scopelen)
				w = (LONG)snd_scopelen - 1;
			h = scplug.radh >> 1;

			/* left in the high word, right in the low word */
			t = tadata;
			for (c = 0; c <= w; c++, p += 2)
				*t++ = ((ULONG)(UWORD)scale(p[0], h) << 16) |
				       (UWORD)scale(p[1], h);

			xl = scplug.xpl;
			xr = scplug.xpr;
			y = scplug.yp - h;
			xl += (w + 1) >> 1;
			xr += (w + 1) >> 1;

			rp = scoperp;
			SetAPen(rp, greeencolour);
			t = tadata;
			o = olddata;
			for (c = w; c >= 0; c--) {
				n = *t++;
				nl = (WORD)(n >> 16);
				nr = (WORD)n;
				if (scopechange) {
					WritePixel(rp, xl - c, y + nl);
					if (nr != 0x7FFF)
						WritePixel(rp, xr - c, y + nr);
					continue;
				}
				od = *o++;
				if (od == n)
					continue;
				ol = (WORD)(od >> 16);
				or = (WORD)od;
				/* erase the old pixels that moved */
				SetAPen(rp, 1);
				if (or != nr)
					WritePixel(rp, xr - c, y + or);
				if (ol != nl)
					WritePixel(rp, xl - c, y + ol);
				SetAPen(rp, greeencolour);
				if (ol != nl)
					WritePixel(rp, xl - c, y + nl);
				if (or != nr)
					WritePixel(rp, xr - c, y + nr);
			}

			t = olddata;
			olddata = tadata;
			tadata = t;
			scopechange = FALSE;
		} else if (!cleared) {
			SetAPen(scoperp, 1);
			xl = scplug.base.x;
			y = scplug.base.y;
			RectFill(scoperp, xl, y, xl + scplug.base.xs - 1,
			         y + scplug.base.ys - 1);
			cleared = TRUE;
			scopechange = TRUE;
		}
		ReleaseSemaphore(&scoperpsem);
	}
	Forbid();               /* the process ends before anyone runs again */
	ReleaseSemaphore(&scopetasksem);
}

/* ----------------------------------------------------------- the plugin */

static void scope_min_size(struct EG_Plugin *p, struct TextAttr *ta, WORD fh,
                           WORD *w, WORD *h)
{
	(void)p;
	(void)ta;
	(void)fh;
	*w = 200;
	*h = 20;
}

static void scope_init(struct scope *sc, struct Screen *screen)
{
	struct Screen *scr;

	sc->base.cls = &scope_class;
	scr = screen ? screen : LockPubScreen(NULL);
	if (scr) {
		colormap = scr->ViewPort.ColorMap;
		if (!screen)
			UnlockPubScreen(NULL, scr);
		if (SysBase->LibNode.lib_Version >= 39)
			greeencolour = ObtainBestPenA(colormap, 0x10101010,
				0xaaaaaaaa, 0x30303030, NULL);
		else
			greeencolour = -1;
		if (greeencolour == -1) {
			greeencolour = 0;
			colormap = NULL;
		}
	}
}

static void scope_clear_render(struct EG_Plugin *p, struct Window *win)
{
	(void)p;
	(void)win;
	ObtainSemaphore(&scoperpsem);
}

static void scope_render(struct EG_Plugin *p, struct TextAttr *ta, WORD x,
                         WORD y, WORD xs, WORD ys, struct Window *win)
{
	struct scope *sc = (struct scope *)p;

	(void)ta;
	sc->radh = ys - 1;
	sc->yp = y + sc->radh;
	sc->xpl = xs / 4 + x;
	sc->radw = xs / 4 - 4;
	sc->xpr = xs / 2 + sc->xpl;
	scoperp = win->RPort;
	SetAPen(scoperp, 1);
	RectFill(scoperp, x, y, x + xs - 1, y + ys - 1);
	scopechange = TRUE;
	ReleaseSemaphore(&scoperpsem);
	if (scoperpsem.ss_NestCount)
		ReleaseSemaphore(&scoperpsem);
}

static BOOL scope_message_test(struct EG_Plugin *p, struct IntuiMessage *imsg,
                               struct Window *win)
{
	ULONG class = imsg->Class;
	UWORD code = imsg->Code;

	(void)p;
	(void)win;
	if (class & IDCMP_SIZEVERIFY)
		ObtainSemaphore(&scoperpsem);
	if (class & IDCMP_VANILLAKEY)
		if (code == TAB_CODE || code == ESC_CODE)
			return TRUE;
	return FALSE;
}

static BOOL scope_message_action(struct EG_Plugin *p, ULONG class, UWORD qual,
                                 UWORD code, struct Window *win)
{
	(void)qual;
	(void)win;
	if (class & IDCMP_VANILLAKEY) {
		((struct scope *)p)->code = code;
		return TRUE;
	}
	return FALSE;
}

static const struct EG_PluginClass scope_class = {
	scope_min_size,
	NULL,                   /* will_resize: both */
	scope_render,
	scope_clear_render,
	scope_message_test,
	scope_message_action,
	NULL                    /* static object, nothing to free */
};

static void scopeact(EG_Gui *g, EG_Obj *obj, LONG value)
{
	struct scope *sc = (struct scope *)value;

	(void)g;
	(void)obj;
	if (sc->code == ESC_CODE) {
		close_scope(NULL, NULL, 0);
	} else if (sc->code == TAB_CODE) {
		if (gh->wnd) {
			WindowToFront(gh->wnd);
			ActivateWindow(gh->wnd);
		}
	}
}
