/*
 * setup.c - port of mbsetup.e: preferences, projects with settings, the
 * advanced MIDI window (MIDI sources, message and channel filters), the
 * advanced audio window (AHI mode, mixing frequency, polyphony) and the
 * MIDI routes of the main task.
 *
 * Differences from the E code:
 * - The audio engine is chosen at run time and AHI may be missing. The
 *   AHI mode requester and mode information open ahi.device (version 4,
 *   AHI_NO_UNIT) for the call and close it after (only with AUDIO=AHI),
 *   or use the base of the AHI engine if that is running. Without AHI the mode button and the
 *   mixing frequency slider are disabled and the info fields stay empty.
 * - Without a frequency table (no AHI) setmixfreq() keeps the requested
 *   frequency instead of leaving mixfreq unchanged (0 at start), so the
 *   saved value is kept and the Paula 14-bit engine gets it.
 * - The AHI info strings are fixed buffers instead of E strings that were
 *   freed while their text gadgets could still show them.
 * - The 16 channel check actions are one action; the channel bit is the
 *   gadget's user data.
 * - The routes array has a count instead of a -1 terminator.
 * - getaudioahimode() also frees the requester when it is cancelled (the
 *   E code returned from inside the handler).
 * - pb_routechange() only toggles a real list node (the E loop could end
 *   on the list's tail sentinel for an index past the end).
 * - Window handles are checked for NULL before use; mysrclist's nodes and
 *   the frequency table are freed by freeprefs() (the E runtime freed
 *   them at exit).
 * - The plugins of the replaced audio window contents are left to
 *   eg_changegui() to free (the E code ENDed the old title plugin itself).
 */
#include <exec/types.h>
#include <exec/memory.h>
#include <exec/lists.h>
#include <exec/nodes.h>
#include <dos/dos.h>
#include <intuition/intuition.h>
#include <utility/tagitem.h>
#include <devices/ahi.h>
#include <midi/midi.h>
#include <midi/midibase.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/midi.h>

/* the AHI calls here use their own base, see ahi_open() */
static struct Library *setup_ahibase;
#define AHI_BASE_NAME setup_ahibase
#include <proto/ahi.h>

#include <string.h>

#include "egui.h"
#include "keycodes.h"
#include "titlekeys.h"
#include "setup.h"
#include "../app/eport.h"
#include "../app/globals.h"
#include "../app/locale.h"
#include "../app/diskoper.h"
#include "../app/play.h"
#include "../app/sfx.h"
#include "../audio/snd.h"
#include "../app/banks.h"
#include "../app/report.h"

#define E_TRUE (-1L)

#define MSGMASK (MMF_NOTEOFF | MMF_NOTEON | MMF_PITCHBEND | MMF_POLYPRESS \
                 | MMF_CTRL | MMF_CHANPRESS)

#define AHISTRLEN 256

LONG msgflags, chanflags;

static struct MRoute **routes;          /* current MIDI routes */
static LONG nroutes;
static struct TextAttr *ahitextattr;    /* for the AHI requester */

/* gadgets */
static EG_Obj *agh_audioid, *agh_chan, *agh_mxfreq, *agh_mxftxt, *agh_ahimode;
static EG_Obj *agh_ahiname, *agh_ahidriver, *agh_ahiauthor, *agh_ahiversion,
              *agh_ahicopyright;
static EG_Obj *sgh_msrclist, *sgh_noteoff, *sgh_noteon, *sgh_keypress,
              *sgh_ctrl, *sgh_chanpress, *sgh_pitchbend;
static EG_Obj *sgh_c[16];

static struct titlekeys *title_m, *title_a;     /* plugins */

/* menu states */
static BOOL setwithprojects, setlayout, setsaveicons, setsaveundo;

static LONG *freqtab, freqnum;
static LONG indexfreq, mixfreq;
static ULONG ahiaudioid;
static BOOL ahi_ok;                     /* ahi.device could be opened */
static UBYTE ahi_mode_str[16];
static UBYTE ahi_name_str[AHISTRLEN], ahi_author_str[AHISTRLEN],
            ahi_driver_str[AHISTRLEN], ahi_copyright_str[AHISTRLEN],
            ahi_version_str[AHISTRLEN];

static void refresh_srclist(void);
static void changeMRoutes(void);
static void modifyMRouteInfos(LONG chnf, LONG msgf);
static void getaudioahiattrs(ULONG audioid);
static ULONG getaudioahimode(struct Window *window);
static void prefs_set(struct mbprefs *prefs);
static void updateaudwin(BOOL chgui);
static void updatemidiwin(void);
static void pb_refresh(EG_Gui *g, EG_Obj *obj, LONG value);

/* Shl(1, maxchan+1)-1 as on the 68000 (a shift by 32 gives 0) */
static LONG chanmask(LONG mc)
{
	return mc + 1 >= 32 ? -1L : (1L << (mc + 1)) - 1;
}

/* audio_attrs() with audio id, channels, mixing frequency [, apply] */
static void setaudio(ULONG audioid, LONG channels, LONG freq, BOOL apply)
{
	struct TagItem tags[5];

	tags[0].ti_Tag = SFX_SET_AUDIO_ID;
	tags[0].ti_Data = audioid;
	tags[1].ti_Tag = SFX_SET_CHANNELS;
	tags[1].ti_Data = (ULONG)channels;
	tags[2].ti_Tag = SFX_SET_MIXFREQ;
	tags[2].ti_Data = (ULONG)freq;
	tags[3].ti_Tag = apply ? SFX_SET_APPLYAUDIO : TAG_DONE;
	tags[3].ti_Data = (ULONG)E_TRUE;
	tags[4].ti_Tag = TAG_DONE;
	tags[4].ti_Data = 0;
	audio_attrs(tags);
}

static void setaudio2(ULONG tag1, ULONG data1, ULONG tag2, ULONG data2)
{
	struct TagItem tags[3];

	tags[0].ti_Tag = tag1;
	tags[0].ti_Data = data1;
	tags[1].ti_Tag = tag2;
	tags[1].ti_Data = data2;
	tags[2].ti_Tag = TAG_DONE;
	tags[2].ti_Data = 0;
	audio_attrs(tags);
}

/* ======================================================================
 *                                ahi.device
 * ====================================================================== */

static struct MsgPort *ahiport;
static struct AHIRequest *ahiio;
static BOOL ahiown;

/* uses the running AHI engine's base, or opens ahi.device */
static BOOL ahi_open(void)
{
	if (AHIBase) {
		setup_ahibase = AHIBase;
		return TRUE;
	}
	/* AHI not running: only look at it if AUDIO=AHI was given, so a
	 * broken AHI setup is not touched again after the fallback */
	if (sfx_engine_req != SND_AHI)
		return FALSE;
	if (!(ahiport = CreateMsgPort()))
		return FALSE;
	ahiio = (struct AHIRequest *)CreateIORequest(ahiport, sizeof(struct AHIRequest));
	if (ahiio) {
		ahiio->ahir_Version = 4;
		if (OpenDevice((CONST_STRPTR)AHINAME, AHI_NO_UNIT,
		               (struct IORequest *)ahiio, 0) == 0) {
			ahiown = TRUE;
			setup_ahibase = (struct Library *)ahiio->ahir_Std.io_Device;
			return TRUE;
		}
		DeleteIORequest((struct IORequest *)ahiio);
		ahiio = 0;
	}
	DeleteMsgPort(ahiport);
	ahiport = 0;
	return FALSE;
}

static void ahi_close(void)
{
	if (ahiown) {
		CloseDevice((struct IORequest *)ahiio);
		ahiown = FALSE;
	}
	if (ahiio) {
		DeleteIORequest((struct IORequest *)ahiio);
		ahiio = 0;
	}
	if (ahiport) {
		DeleteMsgPort(ahiport);
		ahiport = 0;
	}
	setup_ahibase = 0;
}

/* ======================================================================
 *                                  prefs
 * ====================================================================== */

static void setmixfreq(LONG freq)
{
	LONG i, d, m = 0x7FFFFFFF, x = -1;

	if (freqtab) {
		for (i = 0; i < freqnum; i++) {
			d = freq - freqtab[i];
			if (d < 0)
				d = -d;
			if (d < m) {
				m = d;
				x = i;
			}
		}
		if (x >= 0) {
			indexfreq = x;
			mixfreq = freqtab[x];
		}
	} else {
		mixfreq = freq;
	}
}

/* set initial prefs values and set soundfx up */
void initprefs(CONST_STRPTR pname, struct bank *bnk)
{
	newlist(&mysrclist);    /* this must be the first thing */
	setwithprojects = TRUE;
	setlayout = FALSE;
	setsaveicons = TRUE;
	setsaveundo = TRUE;
	E_TRACE("prefs: midi sources");
	refresh_srclist();
	E_TRACE("prefs: load settings");
	loadsettings(&smplist, &mbprefs, bnk, pname);
	ahiaudioid = (ULONG)mbprefs.ahiaudioid;
	E_TRACE("prefs: ahi mode info");
	getaudioahiattrs(ahiaudioid);
	setmixfreq(mbprefs.mixfreq);
	mcontrol = mbprefs.midictrl != 0 ? TRUE : FALSE;
	if ((maxchan = mbprefs.maxchannels) > 31)
		maxchan = 31;
	if (maxchan < 1)
		maxchan = 1;
	maskmaxchan = chanmask(maxchan);
	mbprefs.maxchannels = (UBYTE)maxchan;
	msgflags = mbprefs.msgflags & MSGMASK;
	mbprefs.msgflags = (WORD)msgflags;
	chanflags = mbprefs.chanflags;
	if ((currmcmon = mbprefs.currentmcm) > 32)
		currmcmon = 32;
	if (currmcmon < 0)
		currmcmon = 0;
	mbprefs.currentmcm = (WORD)currmcmon;
	E_TRACE("prefs: audio on");
	setaudio(ahiaudioid, maxchan + 1, mixfreq, FALSE);
	E_TRACE("prefs: routes");
	refresh_srclist();
	changeMRoutes();
}

/* ======================================================================
 *                         menu and load/save functions
 * ====================================================================== */

static BOOL menuchecked(UWORD item)
{
	struct MenuItem *mi;

	if (!menuptr)
		return FALSE;
	mi = ItemAddress(menuptr, FULLMENUNUM(2, item, 0));
	return mi && (mi->Flags & CHECKED) ? TRUE : FALSE;
}

void menu_setlayout(EG_Gui *g, EG_Obj *obj, LONG value)
{
	(void)g; (void)obj; (void)value;
	setlayout = menuchecked(7);
}

void menu_setwithprojects(EG_Gui *g, EG_Obj *obj, LONG value)
{
	(void)g; (void)obj; (void)value;
	setwithprojects = menuchecked(8);
}

void menu_setsaveicons(EG_Gui *g, EG_Obj *obj, LONG value)
{
	(void)g; (void)obj; (void)value;
	setsaveicons = menuchecked(9);
}

void menu_setsaveundo(EG_Gui *g, EG_Obj *obj, LONG value)
{
	(void)g; (void)obj; (void)value;
	setsaveundo = menuchecked(10);
}

void menu_settings(EG_Gui *g, EG_Obj *obj, LONG value)
{
	(void)g; (void)obj; (void)value;
	prefs_set(&mbprefs);
	savesettings(&smplist, &mbprefs);
}

void saveproject(CONST_STRPTR name, struct List *slist, struct bank *bnk)
{
	struct mbprefs tmprefs;

	CopyMem(&mbprefs, &tmprefs, sizeof(struct mbprefs));
	prefs_set(&tmprefs);
	save_project(name, slist, bnk, &tmprefs, setsaveicons, setsaveundo);
}

void loadproject(CONST_STRPTR name, struct List *slist, struct bank *bnk)
{
	struct mbprefs tmpprefs;
	struct mbprefs *tp;

	CopyMem(&mbprefs, &tmpprefs, sizeof(struct mbprefs));
	E_TRY {
		if (setwithprojects) {
			if (setgh)
				eg_setlistvlabels(setgh, sgh_msrclist, NULL);
			refresh_srclist();
			if ((tp = load_project(name, slist, bnk, &tmpprefs))) {
				ahiaudioid = (ULONG)tp->ahiaudioid;
				getaudioahiattrs(ahiaudioid);
				setmixfreq(tp->mixfreq);
				/* mcontrol from the project: not used */
				if ((maxchan = tp->maxchannels) > 31)
					maxchan = 31;
				if (maxchan < 1)
					maxchan = 1;
				maskmaxchan = chanmask(maxchan);
				msgflags = tp->msgflags & MSGMASK;
				chanflags = tp->chanflags;
				setaudio(ahiaudioid, maxchan + 1, mixfreq, TRUE);
			}
			updateaudwin(TRUE);
			updatemidiwin();
		} else {
			load_project(name, slist, bnk, NULL);
		}
	} E_EXCEPT_DO {
		if (setwithprojects && setgh)
			eg_setlistvlabels(setgh, sgh_msrclist, &mysrclist);
		ReThrow();
	} E_END;
}

static void prefs_set(struct mbprefs *prefs)
{
	prefs->mixfreq = mixfreq;
	prefs->ahiaudioid = (LONG)ahiaudioid;
	prefs->midictrl = (UBYTE)mcontrol;
	prefs->activeb = (UBYTE)nbank;
	prefs->maxchannels = (UBYTE)maxchan;
	prefs->msgflags = (WORD)msgflags;
	prefs->chanflags = (WORD)chanflags;
	prefs->currentmcm = (WORD)currmcmon;
	if (setlayout) {
		if (gh && gh->wnd)
			eg_winbox(gh, &prefs->mainwinx, &prefs->mainwiny,
			          &prefs->mainwinw, &prefs->mainwinh);
		if (volgh && volgh->wnd) {
			eg_winbox(volgh, &prefs->volumewinx, &prefs->volumewiny,
			          &prefs->volumewinw, &prefs->volumewinh);
			prefs->volumehide = FALSE;
		} else {
			prefs->volumehide = (WORD)E_TRUE;
		}
		if (envgh && envgh->wnd) {
			eg_winbox(envgh, &prefs->envelwinx, &prefs->envelwiny,
			          &prefs->envelwinw, &prefs->envelwinh);
			prefs->envelhide = FALSE;
		} else {
			prefs->envelhide = (WORD)E_TRUE;
		}
		if (scopegh && scopegh->wnd) {
			eg_winbox(scopegh, &prefs->scopewinx, &prefs->scopewiny,
			          &prefs->scopewinw, &prefs->scopewinh);
			prefs->scopehide = FALSE;
		} else {
			prefs->scopehide = (WORD)E_TRUE;
		}
		if (mongh && mongh->wnd) {
			eg_winbox(mongh, &prefs->midimonwinx, &prefs->midimonwiny,
			          &prefs->midimonwinw, &prefs->midimonwinh);
			prefs->midimonhide = FALSE;
		} else {
			prefs->midimonhide = (WORD)E_TRUE;
		}
	}
}

void menu_advancedmidi(EG_Gui *g, EG_Obj *obj, LONG value)
{
	(void)g; (void)obj; (void)value;
	if (!setgh)
		return;
	if (setgh->wnd) {
		ActivateWindow(setgh->wnd);
		WindowToFront(setgh->wnd);
	} else {
		eg_openwin(setgh);
		if (setgh->wnd)
			SetWindowTitles(setgh->wnd, (CONST_STRPTR)-1, mainbartext);
	}
}

void menu_advancedaudio(EG_Gui *g, EG_Obj *obj, LONG value)
{
	(void)g; (void)obj; (void)value;
	if (!saugh)
		return;
	if (saugh->wnd) {
		ActivateWindow(saugh->wnd);
		WindowToFront(saugh->wnd);
	} else {
		eg_settext(saugh, agh_audioid, (CONST_STRPTR)"");
		eg_settext(saugh, agh_ahiname, (CONST_STRPTR)"");
		eg_settext(saugh, agh_ahidriver, (CONST_STRPTR)"");
		eg_settext(saugh, agh_ahiauthor, (CONST_STRPTR)"");
		eg_settext(saugh, agh_ahiversion, (CONST_STRPTR)"");
		eg_settext(saugh, agh_ahicopyright, (CONST_STRPTR)"");
		eg_openwin(saugh);
		updateaudwin(FALSE);
		if (saugh->wnd)
			SetWindowTitles(saugh->wnd, (CONST_STRPTR)-1, mainbartext);
	}
}

/* ======================================================================
 *                              gui definitions
 * ====================================================================== */

void close_setwin(EG_Gui *g, EG_Obj *obj, LONG value)
{
	(void)g; (void)obj; (void)value;
	if (setgh && setgh->wnd)
		eg_closewin(setgh);
}

void close_sauwin(EG_Gui *g, EG_Obj *obj, LONG value)
{
	(void)g; (void)obj; (void)value;
	if (saugh && saugh->wnd)
		eg_closewin(saugh);
}

static void clean_setwin(EG_Gui *g, EG_Obj *obj, LONG value)
{
	(void)g; (void)obj; (void)value;
	setgh = 0;
}

static void clean_sauwin(EG_Gui *g, EG_Obj *obj, LONG value)
{
	(void)g; (void)obj; (void)value;
	saugh = 0;
}

static void setwin_act(EG_Gui *g, EG_Obj *obj, LONG value)
{
	if (((struct titlekeys *)value)->keycode == ESC_CODE)
		close_setwin(g, obj, 0);
}

static void sauwin_act(EG_Gui *g, EG_Obj *obj, LONG value)
{
	if (((struct titlekeys *)value)->keycode == ESC_CODE)
		close_sauwin(g, obj, 0);
}

static void undoset(EG_Gui *g, EG_Obj *obj, LONG value);
static void ahimodeset(EG_Gui *g, EG_Obj *obj, LONG value);
static void mfreqset(EG_Gui *g, EG_Obj *obj, LONG value);
static void chanset(EG_Gui *g, EG_Obj *obj, LONG value);
static void applyaudio(EG_Gui *g, EG_Obj *obj, LONG value);

static struct titlekeys *newtitle(LONG strid)
{
	struct titlekeys *t = titlekeys_new(getLocStr(strid, 0), TITLE_NORMAL, NULL);

	if (!t)
		Raise('MEM');
	return t;
}

static EG_Obj *saudmgui(void)
{
	LONG a, b, c, d, e;
	STRPTR la, lb, lc, ld, le;

	la = getLocStr(STRID_AHIMODE, &a);
	lb = getLocStr(STRID_MFREQ, &b);
	lc = getLocStr(STRID_CHANPOLY, &c);
	ld = getLocStr(STRID_UNDO, &d);
	le = getLocStr(STRID_APPLY, &e);
	title_a = newtitle(STRID_AHIAUDIOINFO);

	return eg_bevelr(eg_rows(
		eg_cols(
			agh_ahimode = eg_sbutton(ahimodeset, la, a),
			agh_audioid = eg_text(ahi_mode_str, (CONST_STRPTR)"", TRUE, 8),
			NULL),
		eg_cols(
			agh_mxfreq = eg_slide(mfreqset, lb, FALSE, 0, E_MAX(freqnum - 1, 0),
			                      0, 6, (CONST_STRPTR)"", b, freqtab ? FALSE : TRUE),
			agh_mxftxt = eg_num(0, (CONST_STRPTR)"", FALSE, 6),
			NULL),
		agh_chan = eg_slide(chanset, lc, FALSE, 2, 32, maxchan + 1, 6,
		                    (CONST_STRPTR)"\\d[2]", c, FALSE),
		eg_cols(
			eg_sbutton(undoset, ld, d),
			eg_sbutton(applyaudio, le, e),
			NULL),
		eg_plugin(sauwin_act, &title_a->plugin),
		agh_ahiname = eg_text(ahi_name_str, getLocStr(STRID_AHIINFONAME, 0), TRUE, 12),
		agh_ahidriver = eg_text(ahi_driver_str, getLocStr(STRID_AHIINFODRIVER, 0), TRUE, 12),
		agh_ahiauthor = eg_text(ahi_author_str, getLocStr(STRID_AHIINFOAUTHOR, 0), TRUE, 12),
		agh_ahiversion = eg_text(ahi_version_str, getLocStr(STRID_AHIINFOVERSION, 0), TRUE, 12),
		agh_ahicopyright = eg_text(ahi_copyright_str, getLocStr(STRID_AHIINFOCOPYRIGHT, 0), TRUE, 12),
		NULL));
}

static void winopts(struct EG_WinOpts *o, eg_action close, eg_action clean,
                    struct Screen *screen, struct TextAttr *ta)
{
	o->menu = NULL;
	o->awproc = NULL;
	o->close = close;
	o->clean = clean;
	o->info = NULL;
	o->screen = screen;
	o->font = ta;
	o->left = o->top = o->width = o->height = -1;
	o->wtype = WTYPE_SIZE;
	o->hide = TRUE;
	o->screentitle = NULL;
}

void open_sauwin(struct Screen *screen, struct TextAttr *ta)
{
	struct EG_WinOpts opts;

	ahitextattr = ta;
	winopts(&opts, close_sauwin, clean_sauwin, screen, ta);
	saugh = eg_add(mh, getLocStr(STRID_MIDIINADVANCEDAUDIO, 0), saudmgui(), &opts);
	if (!saugh)
		Raise('GUI');
	if (!ahi_ok)
		eg_setdisabled(saugh, agh_ahimode, TRUE);
}

static void chk_mc(EG_Gui *g, EG_Obj *obj, LONG value);
static void chk_noteoff(EG_Gui *g, EG_Obj *obj, LONG value);
static void chk_noteon(EG_Gui *g, EG_Obj *obj, LONG value);
static void chk_keypress(EG_Gui *g, EG_Obj *obj, LONG value);
static void chk_ctrl(EG_Gui *g, EG_Obj *obj, LONG value);
static void chk_chanpress(EG_Gui *g, EG_Obj *obj, LONG value);
static void chk_pitchbend(EG_Gui *g, EG_Obj *obj, LONG value);
static void undomidi(EG_Gui *g, EG_Obj *obj, LONG value);
static void pb_routechange(EG_Gui *g, EG_Obj *obj, LONG value);

static EG_Obj *chancheck(LONG n)
{
	EG_Obj *o = eg_check(chk_mc, getLocStr(STRID_C01 + n, 0),
	                     (chanflags & (1L << n)) ? TRUE : FALSE, FALSE, 0, FALSE);

	eg_setdata(o, (APTR)(1UL << n));
	sgh_c[n] = o;
	return o;
}

void open_setwin(struct Screen *screen, struct TextAttr *ta)
{
	struct EG_WinOpts opts;
	LONG aa, bb, cc, dd, ee, ff, p, u;
	STRPTR laa, lbb, lcc, ldd, lee, lff, lp, lu;
	EG_Obj *col1, *col2;

	lbb = getLocStr(STRID_NOTEOFF, &bb);
	laa = getLocStr(STRID_NOTEON, &aa);
	lcc = getLocStr(STRID_KEYPRESS, &cc);
	ldd = getLocStr(STRID_CTRL, &dd);
	lee = getLocStr(STRID_CHANPRESS, &ee);
	lff = getLocStr(STRID_SETPITCHBEND, &ff);
	lp = getLocStr(STRID_REFRESH, &p);
	lu = getLocStr(STRID_UNDO, &u);
	title_m = newtitle(STRID_MIDIMESANDCHAN);

	col1 = eg_eqrows(chancheck(0), chancheck(1), chancheck(2), chancheck(3),
	                 chancheck(4), chancheck(5), chancheck(6), chancheck(7),
	                 NULL);
	col2 = eg_eqrows(chancheck(8), chancheck(9), chancheck(10), chancheck(11),
	                 chancheck(12), chancheck(13), chancheck(14), chancheck(15),
	                 NULL);

	winopts(&opts, close_setwin, clean_setwin, screen, ta);
	setgh = eg_add(mh, getLocStr(STRID_MIDIINADVANCED, 0), eg_rows(
		eg_plugin(setwin_act, &title_m->plugin),
		eg_cols(
			eg_rows(
				eg_bevel(eg_eqrows(
					sgh_noteoff = eg_check(chk_noteoff, lbb, (msgflags & MMF_NOTEOFF) ? TRUE : FALSE, FALSE, bb, FALSE),
					sgh_noteon = eg_check(chk_noteon, laa, (msgflags & MMF_NOTEON) ? TRUE : FALSE, FALSE, aa, FALSE),
					sgh_keypress = eg_check(chk_keypress, lcc, (msgflags & MMF_POLYPRESS) ? TRUE : FALSE, FALSE, cc, FALSE),
					sgh_ctrl = eg_check(chk_ctrl, ldd, (msgflags & MMF_CTRL) ? TRUE : FALSE, FALSE, dd, FALSE),
					sgh_chanpress = eg_check(chk_chanpress, lee, (msgflags & MMF_CHANPRESS) ? TRUE : FALSE, FALSE, ee, FALSE),
					sgh_pitchbend = eg_check(chk_pitchbend, lff, (msgflags & MMF_PITCHBEND) ? TRUE : FALSE, FALSE, ff, FALSE),
					NULL)),
				sgh_msrclist = eg_listv(pb_routechange, getLocStr(STRID_PATCHBAY, 0),
				                        8, 3, &mysrclist, FALSE, 0, NULL),
				eg_sbutton(pb_refresh, lp, p),
				NULL),
			eg_rows(
				eg_sbutton(undomidi, lu, u),
				eg_bevel(eg_eqcols(col1, col2, NULL)),
				NULL),
			NULL),
		NULL), &opts);
	if (!setgh)
		Raise('GUI');
}

static void updateaudwin(BOOL chgui)
{
	if (!saugh)
		return;
	if (chgui) {
		eg_changegui(saugh, saudmgui());
		if (!ahi_ok)
			eg_setdisabled(saugh, agh_ahimode, TRUE);
	}
	eg_settext(saugh, agh_audioid, ahi_mode_str);
	eg_setslide(saugh, agh_chan, maxchan + 1);
	eg_setslide(saugh, agh_mxfreq, indexfreq);
	eg_setnum(saugh, agh_mxftxt, mixfreq);
	eg_settext(saugh, agh_ahiname, ahi_name_str);
	eg_settext(saugh, agh_ahidriver, ahi_driver_str);
	eg_settext(saugh, agh_ahiauthor, ahi_author_str);
	eg_settext(saugh, agh_ahiversion, ahi_version_str);
	eg_settext(saugh, agh_ahicopyright, ahi_copyright_str);
}

static void updatemidiwin(void)
{
	LONG i;

	if (!setgh)
		return;
	pb_refresh(setgh, NULL, 0);
	eg_setcheck(setgh, sgh_noteoff, (msgflags & MMF_NOTEOFF) ? TRUE : FALSE);
	eg_setcheck(setgh, sgh_noteon, (msgflags & MMF_NOTEON) ? TRUE : FALSE);
	eg_setcheck(setgh, sgh_keypress, (msgflags & MMF_POLYPRESS) ? TRUE : FALSE);
	eg_setcheck(setgh, sgh_ctrl, (msgflags & MMF_CTRL) ? TRUE : FALSE);
	eg_setcheck(setgh, sgh_chanpress, (msgflags & MMF_CHANPRESS) ? TRUE : FALSE);
	eg_setcheck(setgh, sgh_pitchbend, (msgflags & MMF_PITCHBEND) ? TRUE : FALSE);
	for (i = 0; i < 16; i++)
		eg_setcheck(setgh, sgh_c[i], (chanflags & (1L << i)) ? TRUE : FALSE);
}

/* ======================================================================
 *                         audio advanced window
 * ====================================================================== */

static void undoset(EG_Gui *g, EG_Obj *obj, LONG value)
{
	(void)g; (void)obj; (void)value;
	maxchan = mbprefs.maxchannels;
	maskmaxchan = chanmask(maxchan);
	ahiaudioid = (ULONG)mbprefs.ahiaudioid;
	getaudioahiattrs(ahiaudioid);
	setmixfreq(mbprefs.mixfreq);
	setaudio(ahiaudioid, maxchan + 1, mixfreq, TRUE);
	updateaudwin(TRUE);
}

static void ahimodeset(EG_Gui *g, EG_Obj *obj, LONG value)
{
	ULONG auid;

	(void)g; (void)obj; (void)value;
	blockallwindows();
	auid = getaudioahimode(saugh ? saugh->wnd : NULL);
	unblockallwindows();
	if (auid != AHI_INVALID_ID) {
		ahiaudioid = auid;
		getaudioahiattrs(ahiaudioid);
		setmixfreq(mixfreq);
		setaudio2(SFX_SET_AUDIO_ID, ahiaudioid, SFX_SET_MIXFREQ, (ULONG)mixfreq);
		updateaudwin(TRUE);
	}
}

static void mfreqset(EG_Gui *g, EG_Obj *obj, LONG value)
{
	(void)g; (void)obj;
	if (freqtab && value >= 0 && value < freqnum) {
		setmixfreq(freqtab[value]);
		if (saugh)
			eg_setnum(saugh, agh_mxftxt, mixfreq);
		setaudio2(SFX_SET_MIXFREQ, (ULONG)mixfreq, TAG_DONE, 0);
	}
}

static void chanset(EG_Gui *g, EG_Obj *obj, LONG value)
{
	(void)g; (void)obj;
	maxchan = value - 1;
	maskmaxchan = chanmask(maxchan);
	setaudio2(SFX_SET_CHANNELS, (ULONG)(maxchan + 1), TAG_DONE, 0);
}

static void applyaudio(EG_Gui *g, EG_Obj *obj, LONG value)
{
	(void)g; (void)obj; (void)value;
	setaudio2(SFX_SET_APPLYAUDIO, (ULONG)E_TRUE, TAG_DONE, 0);
}

/* ======================================================================
 *                          MIDI advanced window
 * ====================================================================== */

static void undomidi(EG_Gui *g, EG_Obj *obj, LONG value)
{
	(void)g; (void)obj; (void)value;
	chanflags = mbprefs.chanflags;
	msgflags = mbprefs.msgflags;
	modifyMRouteInfos(chanflags, msgflags);
	updatemidiwin();
}

/* chk_mc1..chk_mc16: the gadget's data is the channel bit */
static void chk_mc(EG_Gui *g, EG_Obj *obj, LONG value)
{
	LONG bit = (LONG)(ULONG)eg_getdata(obj);

	(void)g;
	chanflags = value ? chanflags | bit : chanflags & (0xFFFF & ~bit);
	modifyMRouteInfos(chanflags, msgflags);
}

static void setmsgflag(LONG flag, LONG check)
{
	msgflags = check ? msgflags | flag : msgflags & ~flag;
	modifyMRouteInfos(chanflags, msgflags);
}

static void chk_noteoff(EG_Gui *g, EG_Obj *obj, LONG value)
{
	(void)g; (void)obj;
	setmsgflag(MMF_NOTEOFF, value);
}

static void chk_noteon(EG_Gui *g, EG_Obj *obj, LONG value)
{
	(void)g; (void)obj;
	setmsgflag(MMF_NOTEON, value);
}

static void chk_keypress(EG_Gui *g, EG_Obj *obj, LONG value)
{
	(void)g; (void)obj;
	setmsgflag(MMF_POLYPRESS, value);
}

static void chk_ctrl(EG_Gui *g, EG_Obj *obj, LONG value)
{
	(void)g; (void)obj;
	setmsgflag(MMF_CTRL, value);
}

static void chk_chanpress(EG_Gui *g, EG_Obj *obj, LONG value)
{
	(void)g; (void)obj;
	setmsgflag(MMF_CHANPRESS, value);
}

static void chk_pitchbend(EG_Gui *g, EG_Obj *obj, LONG value)
{
	(void)g; (void)obj;
	setmsgflag(MMF_PITCHBEND, value);
}

static void pb_refresh(EG_Gui *g, EG_Obj *obj, LONG value)
{
	(void)g; (void)obj; (void)value;
	if (setgh)
		eg_setlistvlabels(setgh, sgh_msrclist, NULL);
	refresh_srclist();
	changeMRoutes();
	if (setgh)
		eg_setlistvlabels(setgh, sgh_msrclist, &mysrclist);
}

/* toggles source i between used ("+"/"-") and not used (" ") */
static void pb_routechange(EG_Gui *g, EG_Obj *obj, LONG value)
{
	struct Node *n;
	LONG i = value;

	(void)g; (void)obj;
	for (n = mysrclist.lh_Head; n->ln_Succ; n = n->ln_Succ)
		if (--i < 0)
			break;
	if (setgh)
		eg_setlistvlabels(setgh, sgh_msrclist, NULL);
	if (n->ln_Succ)
		n->ln_Name[0] = n->ln_Name[0] == ' ' ? '+' : ' ';
	refresh_srclist();
	changeMRoutes();
	if (setgh)
		eg_setlistvlabels(setgh, sgh_msrclist, &mysrclist);
}

/* ======================================================================
 *                           source list functions
 * ====================================================================== */

static void setrouteinfo(LONG chnf, LONG msgf)
{
	minfo.MsgFlags = (UWORD)(msgf & (MMF_NOTEOFF | MMF_NOTEON));
	minfo.ChanFlags = (UWORD)chnf;
	minfo.ChanOffset = 0;
	minfo.NoteOffset = 0;
	minfo.SysExMatch.Flags = 0;
	minfo.CtrlMatch.Flags = 0;
}

static void modifyMRouteInfos(LONG chnf, LONG msgf)
{
	LONG i;

	setrouteinfo(chnf, msgf);
	if (routes)
		for (i = 0; i < nroutes; i++)
			if (routes[i])
				ModifyMRoute(routes[i], &minfo);
	minfo.MsgFlags = (UWORD)msgf;
	signal_playtask(PSG_MINFO, 0, 0);       /* the play task's routes too */
}

/*
 * Nodes are "  name" (2 characters of state and the source name). While
 * the list is checked, ln_Name points at the name itself so FindName()
 * and addsorted() work on it.
 */
static void refresh_srclist(void)
{
	struct Node *n, *next;
	struct MSource *source;
	STRPTR name;
	ULONG len;

	E_TRY {
		LockMidiBase();

		/* remove sources that are gone */
		for (n = mysrclist.lh_Head; (next = n->ln_Succ); n = next) {
			n->ln_Name += 2;
			if (!FindMSource((CONST_STRPTR)n->ln_Name)) {
				Remove(n);
				e_dispose(n);
			}
		}

		/* add new sources */
		for (source = (struct MSource *)MidiBase->SourceList.lh_Head;
		     source->Node.ln_Succ;
		     source = (struct MSource *)source->Node.ln_Succ) {
			name = (STRPTR)source->Node.ln_Name;
			if (!FindName(&mysrclist, name)) {
				len = strlen((char *)name) + 1;
				n = (struct Node *)e_new(sizeof(struct Node) + 2 + len);
				n->ln_Name = (char *)(n + 1) + 2;
				n->ln_Name[-2] = ' ';
				n->ln_Name[-1] = ' ';
				CopyMem(name, n->ln_Name, len);
				addsorted(&mysrclist, n);
			}
		}
	} E_EXCEPT_DO {
		UnlockMidiBase();
		for (n = mysrclist.lh_Head; (next = n->ln_Succ); n = next)
			n->ln_Name -= 2;
		if (exception)
			report_exception();
	} E_END;
}

static void changeMRoutes(void)
{
	struct Node *n;
	struct MRoute *mr;
	LONG i = 0;

	/* count the routes */
	for (n = mysrclist.lh_Head; n->ln_Succ; n = n->ln_Succ)
		if (n->ln_Name[0] != ' ')
			i++;

	freeallMRoutes();

	/* make the routes of the selected sources */
	setrouteinfo(chanflags, msgflags);
	if (i) {
		routes = (struct MRoute **)e_new((ULONG)i * sizeof(*routes));
		nroutes = i;
		i = 0;
		for (n = mysrclist.lh_Head; n->ln_Succ; n = n->ln_Succ)
			if (n->ln_Name[0] != ' ') {
				mr = MRouteDest((CONST_STRPTR)n->ln_Name + 2, dest, &minfo);
				n->ln_Name[0] = mr ? '+' : '-';
				routes[i++] = mr;
			}
	}

	/* the play task makes its own */
	minfo.MsgFlags = (UWORD)msgflags;
	signal_playtask(PSG_CHANGE, 0, 0);
}

void freeallMRoutes(void)
{
	LONG i;

	if (routes) {
		for (i = 0; i < nroutes; i++)
			if (routes[i])
				DeleteMRoute(routes[i]);
		e_dispose(routes);
		routes = 0;
		nroutes = 0;
	}
}

void freeprefs(void)
{
	struct Node *n;

	if (mysrclist.lh_Head)
		while ((n = RemHead(&mysrclist)))
			e_dispose(n);
	e_dispose(freqtab);
	freqtab = 0;
	freqnum = 0;
}

/* ======================================================================
 *                               AHI requester
 * ====================================================================== */

static ULONG getaudioahimode(struct Window *window)
{
	struct AHIAudioModeRequester *volatile arequest = NULL;
	volatile ULONG audioid = AHI_INVALID_ID;
	struct TagItem dizzy[4], filter[5], tags[6];

	if (!ahi_open())
		return AHI_INVALID_ID;
	E_TRY {
		dizzy[0].ti_Tag = AHIDB_Stereo;   dizzy[0].ti_Data = (ULONG)E_TRUE;
		dizzy[1].ti_Tag = AHIDB_Panning;  dizzy[1].ti_Data = (ULONG)E_TRUE;
		dizzy[2].ti_Tag = AHIDB_Bits;     dizzy[2].ti_Data = 9;
		dizzy[3].ti_Tag = TAG_DONE;       dizzy[3].ti_Data = 0;
		filter[0].ti_Tag = AHIDB_Stereo;  filter[0].ti_Data = (ULONG)E_TRUE;
		filter[1].ti_Tag = AHIDB_Panning; filter[1].ti_Data = (ULONG)E_TRUE;
		filter[2].ti_Tag = AHIDB_Bits;    filter[2].ti_Data = 9;
		filter[3].ti_Tag = AHIB_Dizzy;    filter[3].ti_Data = (ULONG)dizzy;
		filter[4].ti_Tag = TAG_DONE;      filter[4].ti_Data = 0;
		tags[0].ti_Tag = AHIR_Window;     tags[0].ti_Data = (ULONG)window;
		tags[1].ti_Tag = AHIR_TextAttr;   tags[1].ti_Data = (ULONG)ahitextattr;
		tags[2].ti_Tag = AHIR_TitleText;
		tags[2].ti_Data = (ULONG)getLocStr(STRID_AHIREQUESTER, 0);
		tags[3].ti_Tag = AHIR_InitialAudioID; tags[3].ti_Data = audioid;
		tags[4].ti_Tag = AHIR_FilterTags; tags[4].ti_Data = (ULONG)filter;
		tags[5].ti_Tag = TAG_DONE;        tags[5].ti_Data = 0;
		if (!(arequest = AHI_AllocAudioRequestA(tags)))
			Raise('MEM');
		if (!AHI_AudioRequestA(arequest, NULL)) {
			if (IoErr() == ERROR_NO_FREE_STORE)
				Raise('MEM');
		} else {
			audioid = arequest->ahiam_AudioID;
		}
	} E_EXCEPT_DO {
		if (arequest)
			AHI_FreeAudioRequest(arequest);
		if (exception) {
			report_exception();
			audioid = AHI_INVALID_ID;
		}
	} E_END;
	ahi_close();
	return audioid;
}

/* AHIDB string attribute into dst, control characters made spaces */
static void getahistr(ULONG audioid, ULONG tag, UBYTE *dst)
{
	UBYTE buf[256];
	struct TagItem tags[3];
	LONG i;

	dst[0] = 0;
	buf[0] = 0;
	tags[0].ti_Tag = AHIDB_BufferLen; tags[0].ti_Data = 255;
	tags[1].ti_Tag = tag;             tags[1].ti_Data = (ULONG)buf;
	tags[2].ti_Tag = TAG_DONE;        tags[2].ti_Data = 0;
	if (AHI_GetAudioAttrsA(audioid, NULL, tags)) {
		buf[255] = 0;
		for (i = 0; buf[i]; i++)
			if (buf[i] < ' ')
				buf[i] = ' ';
		estrcpy(dst, buf, AHISTRLEN);
	}
}

static void getaudioahiattrs(ULONG audioid)
{
	ULONG fnum = 0, f;
	LONG *ftab, i;
	struct TagItem tags[4];

	if (audioid == AHI_DEFAULT_ID)
		estrcpy(ahi_mode_str, (CONST_STRPTR)"DEFAULT", sizeof(ahi_mode_str));
	else
		estringf(ahi_mode_str, sizeof(ahi_mode_str), (CONST_STRPTR)"0x\\h", audioid);

	ahi_name_str[0] = 0;
	ahi_driver_str[0] = 0;
	ahi_author_str[0] = 0;
	ahi_version_str[0] = 0;
	ahi_copyright_str[0] = 0;
	ahi_ok = ahi_open();
	if (!ahi_ok)
		return;
	E_TRY {
		tags[0].ti_Tag = AHIDB_Frequencies; tags[0].ti_Data = (ULONG)&fnum;
		tags[1].ti_Tag = TAG_DONE;          tags[1].ti_Data = 0;
		AHI_GetAudioAttrsA(audioid, NULL, tags);
		if ((LONG)fnum > 0) {
			if ((LONG)fnum != freqnum) {
				ftab = (LONG *)e_new(fnum * sizeof(LONG));
				e_dispose(freqtab);
				freqtab = ftab;
				freqnum = (LONG)fnum;
			}
			for (i = 0; i < freqnum; i++) {
				tags[0].ti_Tag = AHIDB_FrequencyArg; tags[0].ti_Data = (ULONG)i;
				tags[1].ti_Tag = AHIDB_Frequency;    tags[1].ti_Data = (ULONG)&f;
				tags[2].ti_Tag = TAG_DONE;           tags[2].ti_Data = 0;
				if (AHI_GetAudioAttrsA(audioid, NULL, tags))
					freqtab[i] = (LONG)f;
			}
		}
		getahistr(audioid, AHIDB_Driver, ahi_driver_str);
		getahistr(audioid, AHIDB_Name, ahi_name_str);
		getahistr(audioid, AHIDB_Author, ahi_author_str);
		getahistr(audioid, AHIDB_Copyright, ahi_copyright_str);
		getahistr(audioid, AHIDB_Version, ahi_version_str);
	} E_EXCEPT_DO {
	} E_END;
	ahi_close();
}
