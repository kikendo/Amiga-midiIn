/*
 * report.c - requesters, error reports, the status window and the about
 * window (MBreport.e)
 *
 * Differences from the E code:
 * - Requesters are plain intuition EasyRequests; the animated requester
 *   extension (tools/arq) is not used, so the animation ids are gone.
 * - The requester text and gadget strings are passed as "%s" arguments,
 *   so a '%' in a sample or project name is shown as it is (the E code
 *   used them as the format strings themselves).
 * - reqsumm's "data size" was the size of the E program's own allocation
 *   list. It is now the total byte length of the loaded samples in
 *   smplist.
 * - printstatus() and reqabout() check that their window exists (the E
 *   code assumed it), and printstatus() reports a failed window open with
 *   FALSE instead of catching an exception.
 * - The about picture plugin is freed by egui when its window is removed;
 *   clean_about only forgets the pointers.
 * - A file that is not a project ('oiff') gets its own message (English,
 *   not in the catalog) instead of "Not an Interchange File Format".
 * - The version string is a C constant with the contents of version.bin.
 */
#include <exec/types.h>
#include <exec/memory.h>
#include <intuition/intuition.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/midi.h>

#include "eport.h"
#include "locale.h"
#include "globals.h"
#include "report.h"
#include "sfx.h"
#include "../gui/egui.h"
#include "../gui/progressbar.h"
#include "../gui/aboutpic.h"

char cxhotkey[60];                      /* hotkey text to display */

static EG_Gui *statgh;                  /* status window */
static struct progressbar *prsbar;
static EG_Gui *aboutgh;                 /* about window */
static struct aboutpicture *aboutpict;
static EG_Obj *statgd_stat;             /* status text gadget */
static CONST_STRPTR lasttxt;            /* last text for statgd_stat */
static LONG winblocked;                 /* count of blockallwindows() */

#ifndef MIDIIN_VERSTAG
#define MIDIIN_VERSTAG "$VER:midiIn 32.020b (14-May-99) (c) by Najakotiva Software 1997-99"
#endif
static const char versionstr[] = MIDIIN_VERSTAG;

static LONG doreq(CONST_STRPTR text, CONST_STRPTR gadget, CONST_STRPTR title)
{
	struct EasyStruct es;
	char mytitle[51];
	ULONG args[2];
	LONG result;

	/* ends in a non-breaking space */
	estringf((STRPTR)mytitle, sizeof(mytitle), (CONST_STRPTR)"\\s \\s\\c",
	         (LONG)LOC(STRID_MIDIIN), (LONG)title, 0xa0L);
	es.es_StructSize = sizeof(struct EasyStruct);
	es.es_Flags = 0;
	es.es_Title = (UBYTE *)mytitle;
	es.es_TextFormat = (UBYTE *)"%s";
	es.es_GadgetFormat = (UBYTE *)"%s";
	args[0] = (ULONG)text;
	args[1] = (ULONG)gadget;

	blockallwindows();
	result = EasyRequestArgs(0, &es, 0, args);
	unblockallwindows();
	return result;
}

static void blockgh(EG_Gui *g)
{
	if (g && g->wnd)
		eg_blockwin(g);
}

static void unblockgh(EG_Gui *g)
{
	if (g && g->wnd)
		eg_unblockwin(g);
}

void blockallwindows(void)
{
	winblocked++;
	if (winblocked > 1)
		return;
	blockgh(gh);
	blockgh(setgh);
	blockgh(saugh);
	blockgh(volgh);
	blockgh(envgh);
	blockgh(scopegh);
	blockgh(mongh);
}

void unblockallwindows(void)
{
	if (winblocked <= 0)
		return;
	winblocked--;
	if (winblocked > 0)
		return;
	unblockgh(gh);
	unblockgh(setgh);
	unblockgh(saugh);
	unblockgh(volgh);
	unblockgh(envgh);
	if (scopegh && scopegh->wnd) {
		eg_unblockwin(scopegh);
		ModifyIDCMP(scopegh->wnd, scopegh->wnd->IDCMPFlags | IDCMP_SIZEVERIFY);
	}
	unblockgh(mongh);
	if (dest)
		FlushMDest(dest);       /* the windows may have been blocked long */
}

/* "a|b" or "a|b|c" from catalog strings */
static void gadgets(STRPTR s, ULONG size, LONG a, LONG b, LONG c)
{
	if (c >= 0)
		estringf(s, size, (CONST_STRPTR)"\\s|\\s|\\s",
		         (LONG)LOC(a), (LONG)LOC(b), (LONG)LOC(c));
	else
		estringf(s, size, (CONST_STRPTR)"\\s|\\s", (LONG)LOC(a), (LONG)LOC(b));
}

LONG reqclear(void)
{
	char s[61];

	gadgets((STRPTR)s, sizeof(s), STRID_UNUSED, STRID_ALL, STRID_CANCEL);
	return doreq(LOC(STRID_AREUSURE), (CONST_STRPTR)s, LOC(STRID_REQUEST));
}

LONG reqnotundo(void)
{
	return doreq(LOC(STRID_NOTTOUNDO), LOC(STRID_CONTINUE), LOC(STRID_TROUBLE));
}

LONG reqnotredo(void)
{
	return doreq(LOC(STRID_NOTTOREDO), LOC(STRID_CONTINUE), LOC(STRID_TROUBLE));
}

LONG reqskipover(void)
{
	char s[61];

	gadgets((STRPTR)s, sizeof(s), STRID_OVERWRITE, STRID_SKIPOVER, STRID_CANCEL);
	return doreq(LOC(STRID_OVERORSKIP), (CONST_STRPTR)s, LOC(STRID_REQUEST));
}

LONG reqdontfit(LONG numleft)
{
	char s[61], t[121];

	gadgets((STRPTR)s, sizeof(s), STRID_CONTINUE, STRID_CANCEL, -1);
	estringf((STRPTR)t, sizeof(t), LOC(STRID_MERGEDONTFIT), numleft);
	return doreq((CONST_STRPTR)t, (CONST_STRPTR)s, LOC(STRID_REQUEST));
}

LONG reqquit(void)
{
	char s[51];

	gadgets((STRPTR)s, sizeof(s), STRID_LOSE, STRID_BACK, -1);
	return doreq(LOC(STRID_UNSAVED), (CONST_STRPTR)s, LOC(STRID_QUESTION));
}

LONG reqexit(void)
{
	char s[51];

	gadgets((STRPTR)s, sizeof(s), STRID_EXIT, STRID_BACK, -1);
	return doreq(LOC(STRID_SUREQUIT), (CONST_STRPTR)s, LOC(STRID_QUESTION));
}

LONG reqsample(CONST_STRPTR sname)
{
	char s[81], g[61], part[58];
	ULONG sl;

	estringf((STRPTR)s, sizeof(s), (CONST_STRPTR)"\\s\n'",
	         (LONG)LOC(STRID_INSTRNOTFOUND));
	for (sl = 0; sname[sl]; sl++)
		;
	if (sl >= 58) {
		estrcat((STRPTR)s, (CONST_STRPTR)"... ", sizeof(s));
		estrcpy((STRPTR)part, sname + sl - 53, 54);
	} else {
		estrcpy((STRPTR)part, sname, 58);
	}
	estrcat((STRPTR)s, (CONST_STRPTR)part, sizeof(s));
	estrcat((STRPTR)s, (CONST_STRPTR)"'", sizeof(s));
	gadgets((STRPTR)g, sizeof(g), STRID_REPLACE, STRID_ABORT, STRID_SKIP);
	return doreq((CONST_STRPTR)s, (CONST_STRPTR)g, LOC(STRID_TROUBLE));
}

void report_exception(void)
{
	char s[81];
	char e[5];
	char *ep;
	LONG x = exception;
	ULONG size = sizeof(s);

	if (!x)
		return;
	if (x < 10000) {
		estringf((STRPTR)s, size, (CONST_STRPTR)"\\s #\\d!",
		         (LONG)LOC(STRID_UNKNOWN), x);
	} else if (x == 'OPEN' || x == 'oold' || x == 'onew') {
		estrcpy((STRPTR)s, LOC(STRID_WRONGFILENAME), size);
	} else if (x == 'oiff') {
		/* raised only when a project file is read; no catalog string */
		estrcpy((STRPTR)s, (CONST_STRPTR)"This is not a midiIn project file!", size);
	} else if (x == 'NIFF' || x == 'MNGL') {
		estrcpy((STRPTR)s, LOC(STRID_NOTINTERCHANGEFF), size);
	} else if (x == 'init' || x == 'coll' || x == 'exit' || x == 'iffa'
	           || x == 'popc') {
		estrcpy((STRPTR)s, LOC(STRID_IFFMANGLED), size);
	} else if (x == 'push' || x == 'writ') {
		estrcpy((STRPTR)s, LOC(STRID_WRITEERROR), size);
	} else {
		switch (x) {
		case 'RNGE':
			estringf((STRPTR)s, size, LOC(STRID_SETRANGEFIRST));
			break;
		case 'CXER':
			estringf((STRPTR)s, size, LOC(STRID_POPKEYERROR), (LONG)cxhotkey);
			break;
		case 'bprj': estrcpy((STRPTR)s, LOC(STRID_BADPROJECT), size); break;
		case 'AUDB': estrcpy((STRPTR)s, LOC(STRID_AUDIOERROR), size); break;
		case 'NAIF': estrcpy((STRPTR)s, LOC(STRID_NOCOMMONERROR), size); break;
		case 'N8SV': estrcpy((STRPTR)s, LOC(STRID_NOAHDRERROR), size); break;
		case 'UNRE': estrcpy((STRPTR)s, LOC(STRID_UNKNOWNSOUNDFILE), size); break;
		case 'READ': estrcpy((STRPTR)s, LOC(STRID_READERROR), size); break;
		case 'NSND': estrcpy((STRPTR)s, LOC(STRID_NOSSNDERROR), size); break;
		case 'NBDY': estrcpy((STRPTR)s, LOC(STRID_NOBODYERROR), size); break;
		case 'FIBO':
			estrcpy((STRPTR)s, (CONST_STRPTR)"Fibonacci-delta compression not supported!", size);
			break;
		case 'BWAV': estrcpy((STRPTR)s, LOC(STRID_NOWAVECHUNKS), size); break;
		case 'WAVN': estrcpy((STRPTR)s, LOC(STRID_UNSUPPORTEDWAVE), size); break;
		case 'MEM': estrcpy((STRPTR)s, LOC(STRID_MEMERROR), size); break;
		case 'MATH': estrcpy((STRPTR)s, LOC(STRID_MATHERROR), size); break;
		case 'LIB':
			estringf((STRPTR)s, size, LOC(STRID_LIBRARYERROR), (LONG)exceptioninfo);
			break;
		case 'ROUT':
			estringf((STRPTR)s, size, LOC(STRID_MIDISOURCEERR), (LONG)exceptioninfo);
			break;
		case 'GT': estrcpy((STRPTR)s, LOC(STRID_GADTOOLSERR), size); break;
		case 'bigg': estrcpy((STRPTR)s, LOC(STRID_WINDOWERROR), size); break;
		case 'GUI': estrcpy((STRPTR)s, LOC(STRID_GADMEMERROR), size); break;
		case 'PREF': estrcpy((STRPTR)s, LOC(STRID_PREFSERROR), size); break;
		case 'iffp': estrcpy((STRPTR)s, LOC(STRID_IFFPARSEERROR), size); break;
		case '^C': estrcpy((STRPTR)s, LOC(STRID_USERBREAK), size); break;
		default:
			/* the id as text, without its leading zero bytes */
			e[0] = (char)(x >> 24);
			e[1] = (char)(x >> 16);
			e[2] = (char)(x >> 8);
			e[3] = (char)x;
			e[4] = 0;
			ep = e;
			while (*ep == 0)
				ep++;
			estringf((STRPTR)s, size,
			         (CONST_STRPTR)"Exception: \"\\s\" 0x\\h[8]!\\h[8] ",
			         (LONG)ep, x, (LONG)exceptioninfo);
			break;
		}
	}
	doreq((CONST_STRPTR)s, LOC(STRID_NOWAY), LOC(STRID_ERROR));
}

/* num with a comma every three digits */
static STRPTR dotnum(LONG num, STRPTR s)
{
	char m[12], n[20];
	LONG x = 3, l, i, max;

	estringf((STRPTR)m, sizeof(m), (CONST_STRPTR)"\\d", num);
	for (l = 0; m[l]; l++)
		;
	max = l + (l - 1) / 3;
	n[max] = 0;
	max--;
	for (i = l - 1; i >= 0; i--) {
		n[max] = m[i];
		max--;
		x--;
		if (x == 0 && i != 0) {
			x = 3;
			n[max] = ',';
			max--;
		}
	}
	estrcpy(s, (CONST_STRPTR)n, 16);
	return s;
}

void reqsumm(CONST_STRPTR n, LONG a, LONG b, LONG c, LONG d, LONG t, LONG usl)
{
	char s[401], m[51], ds[16], es[16];
	struct lln *ln;
	struct sfx *snd;
	LONG count = 0, len;

	/* data size: the loaded samples */
	for (ln = (struct lln *)smplist.lh_Head; ln->ln.ln_Succ;
	     ln = (struct lln *)ln->ln.ln_Succ) {
		snd = (struct sfx *)ln->pointer;
		if (snd && (len = sfx_length(snd)) > 0)
			count += len;
	}

	estringf((STRPTR)s, sizeof(s),
	         (CONST_STRPTR)"\\s \"\\s\"\n\n\\s \\d\n\\s \\d\n\\s \\d\n\\s \\s \\s\n",
	         (LONG)LOC(STRID_PROJECTNAME), (LONG)n,
	         (LONG)LOC(STRID_ACTIVEBANKS), a,
	         (LONG)LOC(STRID_SAMPLESINLIST), b,
	         (LONG)LOC(STRID_SAMPLESINMEMORY), c,
	         (LONG)LOC(STRID_TOTALLENGTH), (LONG)dotnum(d, (STRPTR)ds),
	         (LONG)LOC(STRID_BYTES));

	estringf((STRPTR)m, sizeof(m), (CONST_STRPTR)"\n * \\s\n",
	         (LONG)LOC(STRID_FREEMEMORY));
	estrcat((STRPTR)s, (CONST_STRPTR)m, sizeof(s));
	estringf((STRPTR)m, sizeof(m), (CONST_STRPTR)"\\s \\s  \\s \\s \\s\n",
	         (LONG)LOC(STRID_FAST), (LONG)dotnum((LONG)AvailMem(MEMF_FAST), (STRPTR)ds),
	         (LONG)LOC(STRID_LARGEST),
	         (LONG)dotnum((LONG)AvailMem(MEMF_FAST | MEMF_LARGEST), (STRPTR)es),
	         (LONG)LOC(STRID_BYTES));
	estrcat((STRPTR)s, (CONST_STRPTR)m, sizeof(s));
	estringf((STRPTR)m, sizeof(m), (CONST_STRPTR)"\\s \\s  \\s \\s \\s\n",
	         (LONG)LOC(STRID_CHIP), (LONG)dotnum((LONG)AvailMem(MEMF_CHIP), (STRPTR)ds),
	         (LONG)LOC(STRID_LARGEST),
	         (LONG)dotnum((LONG)AvailMem(MEMF_CHIP | MEMF_LARGEST), (STRPTR)es),
	         (LONG)LOC(STRID_BYTES));
	estrcat((STRPTR)s, (CONST_STRPTR)m, sizeof(s));
	estringf((STRPTR)m, sizeof(m), (CONST_STRPTR)"\n\\s \\s \\s\n",
	         (LONG)LOC(STRID_DATASIZE), (LONG)dotnum(count, (STRPTR)ds),
	         (LONG)LOC(STRID_BYTES));
	estrcat((STRPTR)s, (CONST_STRPTR)m, sizeof(s));
	estringf((STRPTR)m, sizeof(m), (CONST_STRPTR)"\n\\s \\d \\s\n",
	         (LONG)LOC(STRID_UNDOMEMLEFT), usl, (LONG)LOC(STRID_BYTES));
	estrcat((STRPTR)s, (CONST_STRPTR)m, sizeof(s));
	estringf((STRPTR)m, sizeof(m), (CONST_STRPTR)"\n\\s \\d:\\z\\d[2]:\\z\\d[2] \\s",
	         (LONG)LOC(STRID_RUNTIME), t / 3600, (t / 60) % 60, t % 60,
	         (LONG)LOC(STRID_SECONDS));
	estrcat((STRPTR)s, (CONST_STRPTR)m, sizeof(s));

	doreq((CONST_STRPTR)s, (CONST_STRPTR)"OK", LOC(STRID_PROJECTSUMMARY));
}

STRPTR string_info(void)
{
	return (STRPTR)versionstr + 5;
}

/* ------------------------------------------------------- status window */

static void clean_status(EG_Gui *g, EG_Obj *obj, LONG value)
{
	(void)g;
	(void)obj;
	(void)value;
	statgh = NULL;
	prsbar = NULL;                  /* freed by egui */
}

static void basicopts(struct EG_WinOpts *o, eg_action clean, BOOL hide,
                      struct Screen *screen, struct TextAttr *ta)
{
	o->menu = NULL;
	o->awproc = NULL;
	o->close = NULL;
	o->clean = clean;
	o->info = NULL;
	o->screen = screen;
	o->font = ta;
	o->left = o->top = o->width = o->height = -1;
	o->wtype = WTYPE_BASIC;
	o->hide = hide;
	o->screentitle = NULL;
}

void open_status(struct Screen *screen, struct TextAttr *ta, struct TextFont *font)
{
	struct EG_WinOpts opts;

	prsbar = progressbar_new(screen, font);
	basicopts(&opts, clean_status, TRUE, screen, ta);
	statgh = eg_add(mh, (CONST_STRPTR)"",
		eg_eqrows(
			statgd_stat = eg_text((CONST_STRPTR)"", (CONST_STRPTR)"midiIn:", FALSE, 25),
			eg_bevelr(eg_plugin(NULL, &prsbar->plugin)),
			NULL), &opts);
	if (!statgh) {
		prsbar = NULL;          /* freed with the objects */
		Raise('GUI');
	}
}

void closestatus(void)
{
	if (statgh && statgh->wnd) {
		printstatus((CONST_STRPTR)"", (CONST_STRPTR)"", (CONST_STRPTR)"", 0, 0);
		eg_closewin(statgh);
	}
}

BOOL printstatus(CONST_STRPTR statustext, CONST_STRPTR infotext,
                 CONST_STRPTR typetext, LONG progrs, LONG full)
{
	if (!statgh)
		return FALSE;
	if (!statgh->wnd) {
		progressbar_settext(prsbar, infotext, typetext, 0, 1);
		if (!eg_openwin(statgh))
			return FALSE;
	}
	if (statustext) {
		if (statustext != lasttxt)
			eg_settext(statgh, statgd_stat, statustext);
		lasttxt = statustext;
	}
	progressbar_settext(prsbar, infotext, typetext, progrs, full);
	return TRUE;
}

/* -------------------------------------------------------- about window */

static void about_act(EG_Gui *g, EG_Obj *obj, LONG value)
{
	(void)g;
	(void)obj;
	(void)value;
	eg_closewin(aboutgh);
}

static void clean_about(EG_Gui *g, EG_Obj *obj, LONG value)
{
	(void)g;
	(void)obj;
	(void)value;
	aboutgh = NULL;
	aboutpict = NULL;               /* freed by egui */
}

void open_aboutpic(struct Screen *screen, struct TextAttr *ta)
{
	struct EG_WinOpts opts;

	aboutpict = aboutpic_new(screen);
	basicopts(&opts, clean_about, FALSE, screen, ta);
	aboutgh = eg_add(mh, (CONST_STRPTR)"",
		eg_plugin(about_act, &aboutpict->plugin), &opts);
	if (!aboutgh) {
		aboutpict = NULL;       /* freed with the objects */
		Raise('GUI');
	}
}

void reqabout(void)
{
	if (!aboutgh)
		return;
	if (!aboutgh->wnd) {
		eg_openwin(aboutgh);
	} else {
		WindowToFront(aboutgh->wnd);
		ActivateWindow(aboutgh->wnd);
	}
}
