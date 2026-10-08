/*
 * mainwin.c - port of mbwindow.e: the main window (piano keys, bank
 * LEDs, bank settings, sample list), the volume ("details") window, the
 * envelope window and the MIDI monitor window, their menus, keyboard
 * control, bank clipboard, sorting, sample list handling and project
 * load/save/merge.
 *
 * Differences from the E code:
 * - File requesters are asl.library through filereq() instead of
 *   reqtools. Adding samples collects the chosen files first and then
 *   loads them, so the progress total is known as before. filereq() does
 *   not tell a failed requester from a cancelled one, so the DisplayBeep()
 *   the E code gave when reqtools could not allocate one is gone, and the
 *   MIDI flush after adding samples happens in both cases.
 * - Plugins belong to egui, which frees them when their window is
 *   removed (after the window's clean hook). The clean hooks therefore
 *   only clear the plugin pointers (clean_midimonwin's END midimon
 *   becomes that too), and the exported entry points that use a plugin
 *   (checkplayingbanks, updatechannelkeys, pianokeypressed, follow,
 *   updatemidimonitor) do nothing once it is gone.
 * - pianokeypressed() shows no note when there is no current key; the E
 *   code formatted note -1, reading before its name table.
 * - The sample listview is made with no item selected (-1): EasyGUI's
 *   LISTV had "show selected" off, egui always shows the selection.
 * - samplesel() finds the clicked node by index without the E loop's
 *   walk onto the list's tail pseudo-node for an index past the end.
 * - The bank clipboard's path strings are a small C list instead of E
 *   linked strings.
 * - NULL checks on gh before SetWindowTitles() and on the windows'
 *   pointers in open_gui(), where E would have used a NIL window.
 * - E's TRUE (-1) for followb, mcontrol and basesetb is C's TRUE (1);
 *   they are only tested for zero.
 */
#include <string.h>
#include <exec/types.h>
#include <exec/memory.h>
#include <exec/lists.h>
#include <exec/nodes.h>
#include <intuition/intuition.h>
#include <libraries/gadtools.h>
#include <workbench/workbench.h>
#include <workbench/startup.h>
#include <utility/tagitem.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/midi.h>

#include "egui.h"
#include "mainwin.h"
#include "pianokeys.h"
#include "leds.h"
#include "envelope.h"
#include "midimonitor.h"
#include "titlekeys.h"
#include "scopes.h"
#include "setup.h"
#include "keycodes.h"
#include "../app/eport.h"
#include "../app/globals.h"
#include "../app/banks.h"
#include "../app/sfx.h"
#include "../app/undo.h"
#include "../app/diskoper.h"
#include "../app/play.h"
#include "../app/report.h"
#include "../app/locale.h"
#include "../app/filereq.h"

enum { WINDOW_MAIN, WINDOW_VOLUME, WINDOW_ENVELOPE };

#define TAB_CODE    9
#define RETURN_CODE 13
#define SPACE_CODE  32

/* ------------------------------------------------- globals (globals.h) */

EG_Gui *gh, *volgh, *envgh, *mongh;
struct Menu *menuptr;
struct pianokeys *mp;
LONG nbank;
LONG followb;
LONG askquit;
LONG currmcmon;
char notestr[8];
char basestr[8];
char instrumenttext[32];
char timetext[16];
char monosliderstr[20];
char wintext[80];

/* ------------------------------------------------------- module state */

/* bank clipboard: copies of banks whose instr points to a path string */
struct cpstring {
	struct cpstring *next;
	char s[1];
};
static struct bank *cpbd;
static LONG cpsize;
static struct cpstring *cpstr;
static LONG rangelo, rangehi;

static struct Window *volwindow, *envwindow;   /* reopen after hide */

static struct leds *leds;
static struct envel_plugin *envp;
static struct midimonitor *midimon;
static struct titlekeys *title_a;

static EG_Obj *egh_number, *egh_envel;
static EG_Obj *vgh_volum, *vgh_veloc, *vgh_panor, *vgh_pantx, *vgh_pwide,
              *vgh_pitch, *vgh_firstskip, *vgh_banknum, *vgh_freq, *vgh_time,
              *vgh_after, *vgh_subaft, *vgh_fullname, *vgh_skipnum,
              *vgh_mctvol, *vgh_mctpan;
static EG_Obj *mgh_ctrl;
static EG_Obj *gd_notetext, *gd_instrtext, *gd_midichansl, *gd_bankprisl,
              *gd_basetx, *gd_durmx, *gd_groupsl, *gd_monochk, *gd_monoslide,
              *gd_monosltxt, *gd_monovsens, *gd_loopchk, *gd_finetx,
              *gd_finesl, *gd_followck, *gd_smplist, *gd_audiock, *gd_listtx,
              *gd_mcontr, *gd_rangeset, *gd_undotx;

static ULONG seconds, micros;   /* last click on the sample list */
static LONG dblistnum;          /* and its position + 1 */
static CONST_STRPTR fullnameptr = (CONST_STRPTR)"";   /* volume window */
static LONG remlist;            /* sample list in delete mode */
static LONG lastundotype;

static CONST_STRPTR durlabels[4];
static CONST_STRPTR mmlabels[4];
static struct NewMenu mainmenu[48];
static struct NewMenu boommenu[36];

static const UBYTE mcmons[3] = { MC_VOLUME, MC_PAN, 32 };

/* ---------------------------------------------------------- prototypes */

static void open_mainwindow(struct Screen *screen, struct TextAttr *ta);
static void open_volwin(struct Screen *screen, struct TextAttr *ta);
static void open_envwin(struct Screen *screen, struct TextAttr *ta);
static void open_midimonitwin(struct Screen *screen, struct TextAttr *ta,
                              struct TextFont *font);
static void mainwindow(EG_Gui *g, EG_Obj *obj, LONG value);
static void cleanmain(EG_Gui *g, EG_Obj *obj, LONG value);
static void closemain(EG_Gui *g, EG_Obj *obj, LONG value);
static void projectname(void);
static void applistvproc(EG_Gui *g, EG_Obj *obj, struct AppMessage *awmsg);
static void appwindowproc(EG_Gui *g, EG_Obj *obj, struct AppMessage *awmsg);
static void keybact(LONG keybcode, LONG winno);
static void audioset(EG_Gui *g, EG_Obj *obj, LONG aud);
static void followset(EG_Gui *g, EG_Obj *obj, LONG fol);
static void rangeset(EG_Gui *g, EG_Obj *obj, LONG sel);
static void midiset(EG_Gui *g, EG_Obj *obj, LONG sel);
static void ledact(EG_Gui *g, EG_Obj *obj, LONG value);
static void banksel(LONG bank);
static void midichan(EG_Gui *g, EG_Obj *obj, LONG chan);
static void setpri(EG_Gui *g, EG_Obj *obj, LONG bankpri);
static void dur(EG_Gui *g, EG_Obj *obj, LONG duration);
static void setgroup(EG_Gui *g, EG_Obj *obj, LONG group);
static void monophonic(EG_Gui *g, EG_Obj *obj, LONG mono);
static void monoslide(EG_Gui *g, EG_Obj *obj, LONG set);
static void monovsens(EG_Gui *g, EG_Obj *obj, LONG set);
static void loop(EG_Gui *g, EG_Obj *obj, LONG lp);
static void baseset(EG_Gui *g, EG_Obj *obj, LONG value);
static void fine(EG_Gui *g, EG_Obj *obj, LONG fn);
static void samplesel(EG_Gui *g, EG_Obj *obj, LONG num);
static void free_instr(EG_Gui *g, EG_Obj *obj, LONG value);
static void reload(EG_Gui *g, EG_Obj *obj, LONG value);
static void addsfx(EG_Gui *g, EG_Obj *obj, LONG value);
static void remsfx(EG_Gui *g, EG_Obj *obj, LONG value);
static void clearsfx(EG_Gui *g, EG_Obj *obj, LONG value);

static void update_volgh(void);
static void volumewindow(EG_Gui *g, EG_Obj *obj, LONG value);
static void close_volwin(EG_Gui *g, EG_Obj *obj, LONG value);
static void clean_volwin(EG_Gui *g, EG_Obj *obj, LONG value);
static void title_act(EG_Gui *g, EG_Obj *obj, LONG value);
static void ben_slide(EG_Gui *g, EG_Obj *obj, LONG ben);
static void vol_slide(EG_Gui *g, EG_Obj *obj, LONG vol);
static void vol_center(EG_Gui *g, EG_Obj *obj, LONG value);
static void vol_maxvol(EG_Gui *g, EG_Obj *obj, LONG value);
static void vel_slide(EG_Gui *g, EG_Obj *obj, LONG vel);
static void after_slide(EG_Gui *g, EG_Obj *obj, LONG after);
static void after_sub(EG_Gui *g, EG_Obj *obj, LONG on);
static void after_vel(EG_Gui *g, EG_Obj *obj, LONG value);
static void pan_slide(EG_Gui *g, EG_Obj *obj, LONG pan);
static void pan_center(EG_Gui *g, EG_Obj *obj, LONG value);
static void wid_slide(EG_Gui *g, EG_Obj *obj, LONG pwide);
static void skip_slide(EG_Gui *g, EG_Obj *obj, LONG skip);
static void mctvol_set(EG_Gui *g, EG_Obj *obj, LONG set);
static void mctpan_set(EG_Gui *g, EG_Obj *obj, LONG set);

static void update_envgh(void);
static void envel_act(EG_Gui *g, EG_Obj *obj, LONG value);
static void envelwindow(EG_Gui *g, EG_Obj *obj, LONG value);
static void close_envwin(EG_Gui *g, EG_Obj *obj, LONG value);
static void clean_envwin(EG_Gui *g, EG_Obj *obj, LONG value);

static void midimonitoropen(EG_Gui *g, EG_Obj *obj, LONG value);
static void mm_setmctrl(EG_Gui *g, EG_Obj *obj, LONG set);
static void mm_act(EG_Gui *g, EG_Obj *obj, LONG value);
static void close_midimonwin(EG_Gui *g, EG_Obj *obj, LONG value);
static void clean_midimonwin(EG_Gui *g, EG_Obj *obj, LONG value);

static void menu_reloadall(EG_Gui *g, EG_Obj *obj, LONG value);
static void menu_followset(EG_Gui *g, EG_Obj *obj, LONG value);
static void menu_audio(EG_Gui *g, EG_Obj *obj, LONG value);
static void menu_mcontrol(EG_Gui *g, EG_Obj *obj, LONG value);
static void menu_about(EG_Gui *g, EG_Obj *obj, LONG value);
static void menu_summary(EG_Gui *g, EG_Obj *obj, LONG value);
static void menu_copy(EG_Gui *g, EG_Obj *obj, LONG value);
static void menu_paste(EG_Gui *g, EG_Obj *obj, LONG value);
static void menu_delete(EG_Gui *g, EG_Obj *obj, LONG value);
static void menu_sort_pri(EG_Gui *g, EG_Obj *obj, LONG value);
static void menu_sort_midi(EG_Gui *g, EG_Obj *obj, LONG value);
static void menu_sort_name(EG_Gui *g, EG_Obj *obj, LONG value);
static void menu_sort_range(EG_Gui *g, EG_Obj *obj, LONG value);
static void sortbanksrange(LONG type);
static void menu_undo(EG_Gui *g, EG_Obj *obj, LONG value);
static void menu_redo(EG_Gui *g, EG_Obj *obj, LONG value);
static void menu_save(EG_Gui *g, EG_Obj *obj, LONG value);
static void menu_saveas(EG_Gui *g, EG_Obj *obj, LONG value);
static void save(BOOL askr);
static void menu_open(EG_Gui *g, EG_Obj *obj, LONG value);
static void menu_addproject(EG_Gui *g, EG_Obj *obj, LONG value);
static void menu_new(EG_Gui *g, EG_Obj *obj, LONG value);

static void updaterange(LONG l, LONG h);
static void updateleds(void);
static void update_redoundo(void);
static CONST_STRPTR undotypetext(LONG type);
static void checkaskquit(void);
static STRPTR midinote(STRPTR str, LONG n);

/* set_undo() for one field of the current bank, then update the menu */
static void undobank(LONG type)
{
	set_undo(type, bd, nbank, 0, 0);
	update_redoundo();
}

static LONG audiostatus(LONG tag, LONG data)
{
	struct TagItem t[2];

	t[0].ti_Tag = (ULONG)tag;
	t[0].ti_Data = (ULONG)data;
	t[1].ti_Tag = TAG_DONE;
	t[1].ti_Data = 0;
	return audio_attrs(t);
}

static struct MenuItem *settingsitem(LONG item)
{
	return ItemAddress(menuptr, FULLMENUNUM(2, item, 0));
}

/*
  ==========================================================================
                          Global window functions
  ==========================================================================
*/

void open_gui(struct Screen *defscreen, struct TextAttr *defta,
              struct TextFont *deffont)
{
	E_TRACE("gui: main");
	open_mainwindow(defscreen, defta);
	E_TRACE("gui: audio settings");
	open_sauwin(defscreen, defta);
	E_TRACE("gui: midi settings");
	open_setwin(defscreen, defta);
	E_TRACE("gui: volume");
	open_volwin(defscreen, defta);
	E_TRACE("gui: envelope");
	open_envwin(defscreen, defta);
	E_TRACE("gui: monitor");
	open_midimonitwin(defscreen, defta, deffont);
	E_TRACE("gui: scopes");
	open_scopewindow(defscreen, defta);
	E_TRACE("gui: done");
	if (defscreen)
		ScreenToFront(defscreen);
	if (gh->wnd)
		ActivateWindow(gh->wnd);
	if (strlen(prjname) > 0)
		updategh();
}

void hide(EG_Gui *g, EG_Obj *obj, LONG value)
{
	(void)g; (void)obj; (void)value;
	close_setwin(NULL, NULL, 0);
	if (volgh->wnd)
		eg_closewin(volgh);
	if (envgh->wnd)
		eg_closewin(envgh);
	if (gh->wnd)
		eg_closewin(gh);
}

void show(void)
{
	if (gh->wnd) {
		if (envgh->wnd)
			WindowToFront(envgh->wnd);
		if (volgh->wnd)
			WindowToFront(volgh->wnd);
		WindowToFront(gh->wnd);
		ActivateWindow(gh->wnd);
	} else {
		if (volwindow)
			volumewindow(NULL, NULL, 0);
		if (envwindow)
			envelwindow(NULL, NULL, 0);
		mainwindow(NULL, NULL, 0);
	}
}

/*
  ==========================================================================
                               [Main window]
  ==========================================================================
*/

static void mainwindow(EG_Gui *g, EG_Obj *obj, LONG value)
{
	(void)g; (void)obj; (void)value;
	if (gh->wnd) {
		WindowToFront(gh->wnd);
		ActivateWindow(gh->wnd);
	} else {
		if (remlist)
			remsfx(NULL, NULL, 0);
		eg_settext(gh, gd_instrtext, (CONST_STRPTR)"");
		lastundotype = 0;
		eg_settext(gh, gd_undotx, (CONST_STRPTR)"");
		eg_openwin(gh);
		menuptr = eg_menustrip(gh);
		updategh();
	}
}

/* one NewMenu entry; key: the id for the command key, -1 none */
static struct NewMenu *nm(struct NewMenu *m, UBYTE type, LONG id, LONG key,
                          UWORD flags, eg_action fn)
{
	m->nm_Type = type;
	m->nm_Label = id < 0 ? (STRPTR)NM_BARLABEL : (STRPTR)LOC(id);
	m->nm_CommKey = key < 0 ? NULL : (STRPTR)getLocStr(key, LOC_KEYS);
	m->nm_Flags = flags;
	m->nm_MutualExclude = 0;
	m->nm_UserData = (APTR)fn;
	return m + 1;
}

#define NMI(id, fn)  m = nm(m, NM_ITEM, id, id, 0, fn)
#define NMBAR()      m = nm(m, NM_ITEM, -1, -1, 0, NULL)

static struct NewMenu *projectmenu(struct NewMenu *m)
{
	m = nm(m, NM_TITLE, STRID_MENUPROJECT, -1, 0, NULL);
	NMI(STRID_MENUNEW, menu_new);
	NMI(STRID_OPEN, menu_open);
	NMI(STRID_MERGE, menu_addproject);
	NMI(STRID_RELOADALL, menu_reloadall);
	NMI(STRID_SAVE, menu_save);
	NMI(STRID_SAVEAS, menu_saveas);
	NMI(STRID_SUMM, menu_summary);
	NMBAR();
	NMI(STRID_HIDE, hide);
	NMI(STRID_ABOUT, menu_about);
	NMI(STRID_QUIT, closemain);
	m = nm(m, NM_TITLE, STRID_EDIT, -1, 0, NULL);
	NMI(STRID_COPY, menu_copy);
	NMI(STRID_PASTE, menu_paste);
	NMI(STRID_MENUDELETE, menu_delete);
	NMBAR();
	m = nm(m, NM_ITEM, STRID_MENUSORT, -1, 0, NULL);
	m = nm(m, NM_SUB, STRID_MENUSORTPRI, -1, 0, menu_sort_pri);
	m = nm(m, NM_SUB, STRID_MENUSORTMIDI, -1, 0, menu_sort_midi);
	m = nm(m, NM_SUB, STRID_MENUSORTNAME, -1, 0, menu_sort_name);
	m = nm(m, NM_SUB, STRID_MENUSORTRANGE, -1, 0, menu_sort_range);
	NMBAR();
	NMI(STRID_MENUUNDO, menu_undo);
	NMI(STRID_MENUREDO, menu_redo);
	return m;
}

static struct NewMenu *windowsmenu(struct NewMenu *m)
{
	m = nm(m, NM_TITLE, STRID_WINDOWS, -1, 0, NULL);
	NMI(STRID_MENUMAIN, mainwindow);
	NMI(STRID_MENUVOLUME, volumewindow);
	NMI(STRID_MENUENVELOPE, envelwindow);
	NMI(STRID_SCOPE, scopewindowopen);
	NMI(STRID_MENUMIDIMON, midimonitoropen);
	m->nm_Type = NM_END;
	return m + 1;
}

static void makemainmenu(void)
{
	struct NewMenu *m = projectmenu(mainmenu);

	m = nm(m, NM_TITLE, STRID_SETTINGS, -1, 0, NULL);
	m = nm(m, NM_ITEM, STRID_AUDIOENABLE, STRID_AUDIOENABLE,
	       CHECKIT | MENUTOGGLE, menu_audio);
	m = nm(m, NM_ITEM, STRID_MENUMIDICTRL, STRID_MENUMIDICTRL,
	       mcontrol ? CHECKIT | CHECKED | MENUTOGGLE : CHECKIT | MENUTOGGLE,
	       menu_mcontrol);
	m = nm(m, NM_ITEM, STRID_MENUFOLLOW, STRID_MENUFOLLOW,
	       CHECKIT | MENUTOGGLE, menu_followset);
	NMI(STRID_ADVANCEDMIDI, menu_advancedmidi);
	NMI(STRID_ADVANCEDAUDIO, menu_advancedaudio);
	NMBAR();
	m = nm(m, NM_ITEM, STRID_SAVESETTINGS, -1, 0, menu_settings);
	m = nm(m, NM_ITEM, STRID_MENUSETLAYOUT, STRID_MENUSETLAYOUT,
	       CHECKIT | MENUTOGGLE, menu_setlayout);
	m = nm(m, NM_ITEM, STRID_MENUSETWITHPROJECTS, STRID_MENUSETWITHPROJECTS,
	       CHECKIT | CHECKED | MENUTOGGLE, menu_setwithprojects);
	m = nm(m, NM_ITEM, STRID_MENUSETSAVEICONS, STRID_MENUSETSAVEICONS,
	       CHECKIT | CHECKED | MENUTOGGLE, menu_setsaveicons);
	m = nm(m, NM_ITEM, STRID_MENUSETSAVEUNDO, STRID_MENUSETSAVEUNDO,
	       CHECKIT | CHECKED | MENUTOGGLE, menu_setsaveundo);
	windowsmenu(m);
}

/* the menu of the volume and envelope windows (E's boommenu()) */
static void makeboommenu(void)
{
	windowsmenu(projectmenu(boommenu));
}

static void open_mainwindow(struct Screen *screen, struct TextAttr *ta)
{
	struct EG_WinOpts opts;
	LONG e, g, m, t, i, d, l, s, z, x, a, c, w, q;
	STRPTR le, lg, lm, lt, li, ld, ll, ls, lz, lx, la, lc, lw, lq;
	struct pianokeys *volatile np = NULL;
	struct leds *volatile nl = NULL;

	E_TRY {
		np = pianokeys_new(screen);
		nl = leds_new(screen);
	} E_EXCEPT {
		if (np)
			pianokeys_dispose(np);
		ReThrow();
	} E_END;
	mp = np;
	leds = nl;

	lg = getLocStr(STRID_EDITRANGE, &g);
	lm = getLocStr(STRID_MIDICTRL, &m);
	le = getLocStr(STRID_FOLLOW, &e);
	li = getLocStr(STRID_MIDICHAN, &i);
	lt = getLocStr(STRID_RELOAD, &t);
	lx = getLocStr(STRID_GROUP, &x);
	ld = getLocStr(STRID_DURATION, &d);
	lz = getLocStr(STRID_MONOPHONIC, &z);
	ll = getLocStr(STRID_LOOP, &l);
	lw = getLocStr(STRID_MONOSLIDE, &w);
	lq = getLocStr(STRID_MONOVSENS, &q);
	ls = getLocStr(STRID_SET, &s);
	lc = getLocStr(STRID_CLEAR, &c);
	la = getLocStr(STRID_ADD, &a);
	durlabels[0] = LOC(STRID_ONOFF);
	durlabels[1] = LOC(STRID_ONON);
	durlabels[2] = LOC(STRID_DRUM);
	durlabels[3] = NULL;
	makemainmenu();

	opts.menu = mainmenu;
	opts.awproc = appwindowproc;
	opts.close = closemain;
	opts.clean = cleanmain;
	opts.info = (APTR)999;
	opts.screen = screen;
	opts.font = ta;
	opts.left = mbprefs.mainwinx;
	opts.top = mbprefs.mainwiny;
	opts.width = mbprefs.mainwinw;
	opts.height = mbprefs.mainwinh;
	opts.wtype = WTYPE_SIZE;
	opts.hide = FALSE;
	opts.screentitle = NULL;

	gh = eg_add(mh, mainbartext, eg_rows(
		eg_plugin(plugact, &np->plugin),
		eg_bar(),
		eg_cols(
			gd_notetext = eg_text((CONST_STRPTR)"", (CONST_STRPTR)"", TRUE, 4),
			eg_bar(),
			gd_undotx = eg_text((CONST_STRPTR)"", LOC(STRID_MAINUNDO), TRUE, 12),
			eg_bar(),
			gd_rangeset = eg_check(rangeset, lg, rangesetb ? TRUE : FALSE, TRUE, g, FALSE),
			eg_bar(),
			gd_mcontr = eg_check(midiset, lm, mcontrol ? TRUE : FALSE, TRUE, m, FALSE),
			eg_bar(),
			gd_audiock = eg_check(audioset, LOC(STRID_AUDIO), FALSE, TRUE, 0, FALSE),
			eg_bar(),
			gd_followck = eg_check(followset, le, FALSE, TRUE, e, FALSE),
			NULL),
		eg_bar(),
		eg_cols(
			eg_rows(
				eg_bevelr(
					eg_rows(
						eg_cols(
							eg_rows(
								gd_instrtext = eg_text((CONST_STRPTR)"", LOC(STRID_INSTRUMENT), TRUE, 12),
								eg_cols(
									gd_midichansl = eg_slide(midichan, li, FALSE, 1, 16, 1, 8, (CONST_STRPTR)"\\d[2]", i, FALSE),
									eg_spaceh(),
									eg_bar(),
									NULL),
								NULL),
							eg_rows(
								eg_sbutton(free_instr, LOC(STRID_FREE), 0),
								eg_sbutton(reload, lt, t),
								NULL),
							NULL),
						eg_spacev(),
						eg_cols(
							gd_bankprisl = eg_slide(setpri, LOC(STRID_BANKPRIORITY), FALSE, 1, NUMBANKS, 1, 6, (CONST_STRPTR)"\\d[2]", 0, FALSE),
							gd_groupsl = eg_slide(setgroup, lx, FALSE, 0, 16, 0, 3, (CONST_STRPTR)"\\d[2]", x, FALSE),
							NULL),
						eg_spacev(),
						eg_cols(
							gd_durmx = eg_mx(dur, ld, durlabels, TRUE, 0, d),
							eg_cols(
								eg_rows(
									gd_monochk = eg_check(monophonic, lz, FALSE, TRUE, z, FALSE),
									gd_loopchk = eg_check(loop, ll, FALSE, TRUE, l, FALSE),
									NULL),
								eg_bevelr(eg_rows(
									eg_cols(
										gd_monoslide = eg_slide(monoslide, lw, FALSE, 0, 1500, 0, 8, (CONST_STRPTR)"", w, TRUE),
										gd_monosltxt = eg_text(LOC(STRID_OFF), (CONST_STRPTR)"", FALSE, 4),
										NULL),
									gd_monovsens = eg_slide(monovsens, lq, FALSE, 0, 100, 0, 8, (CONST_STRPTR)"\\d[3]", q, TRUE),
									NULL)),
								NULL),
							NULL),
						eg_spacev(),
						eg_cols(
							eg_button(baseset, ls, s),
							gd_basetx = eg_text((CONST_STRPTR)"c 60", LOC(STRID_BASE), TRUE, 4),
							gd_finesl = eg_slide(fine, LOC(STRID_FINE), FALSE, -100, 100, 0, 8, (CONST_STRPTR)"", 0, FALSE),
							gd_finetx = eg_num(0, (CONST_STRPTR)"", FALSE, 4),
							NULL),
						NULL)),
				eg_bar(),
				eg_plugin(ledact, &nl->plugin),
				NULL),
			eg_rows(
				eg_cols(
					gd_listtx = eg_text(LOC(STRID_INSTRUMENTS), (CONST_STRPTR)"", FALSE, 1),
					eg_sbutton(clearsfx, lc, c),
					NULL),
				gd_smplist = eg_listv(samplesel, NULL, 10, 2, &smplist, FALSE, -1, applistvproc),
				eg_eqcols(
					eg_sbutton(addsfx, la, a),
					eg_sbutton(remsfx, LOC(STRID_DELETE), 0),
					NULL),
				NULL),
			NULL),
		NULL), &opts);
	if (!gh) {
		mp = NULL;
		leds = NULL;
		Raise('GUI');
	}
	if (gh->wnd)
		SetWindowTitles(gh->wnd, (CONST_STRPTR)-1, mainbartext);
	menuptr = eg_menustrip(gh);
}

static void cleanmain(EG_Gui *g, EG_Obj *obj, LONG value)
{
	(void)g; (void)obj; (void)value;
	gh = NULL;
	mp = NULL;              /* freed by egui */
	leds = NULL;
}

static void closemain(EG_Gui *g, EG_Obj *obj, LONG value)
{
	LONG qt = TRUE;

	(void)g; (void)obj; (void)value;
	if (askquit)
		qt = reqquit();
	else if (audiostatus(SFX_GET_AUDIO_STATUS, 0))
		qt = reqexit();
	if (qt)
		eg_quitgui(0);
}

static void projectname(void)
{
	CONST_STRPTR t;

	if (strlen(prjname) == 0)
		t = LOC(STRID_UNNAMED);
	else
		t = (CONST_STRPTR)prjname;
	estringf((STRPTR)wintext, sizeof(wintext), (CONST_STRPTR)"\\s \"\\s\"",
	         (LONG)LOC(STRID_PROJECT), (LONG)t);
	if (askquit)
		estrcat((STRPTR)wintext, (CONST_STRPTR)" *", sizeof(wintext));
	if (gh && gh->wnd)
		SetWindowTitles(gh->wnd, (CONST_STRPTR)wintext, mainbartext);
}

/*
  ==========================================================================
                          Main window GUI handlers
  ==========================================================================
*/

static void applistvproc(EG_Gui *g, EG_Obj *obj, struct AppMessage *awmsg)
{
	struct WBArg *args;
	char name[512];
	LONG i;
	BOOL test, loadbool = FALSE;

	(void)g; (void)obj;
	args = awmsg->am_ArgList;
	blockallwindows();
	set_undo(UNDO_PREP_SAMPLELIST, &smplist, (LONG)bd, 0, 0);
	for (i = 1; i <= awmsg->am_NumArgs; i++) {
		if (args->wa_Lock) {
			NameFromLock(args->wa_Lock, (STRPTR)name, 512);
			AddPart((STRPTR)name, args->wa_Name, 512);
			eg_setlistvlabels(gh, gd_smplist, NULL);
			if (loadinstrumentsfrombank((CONST_STRPTR)name, &smplist)) {
				test = TRUE;
			} else {
				printstatus(LOC(STRID_ADDINGINSTRUMENTS), FilePart((STRPTR)name),
				            0, i - 1, awmsg->am_NumArgs);
				addsnd((CONST_STRPTR)name, &smplist, FALSE, &test);
			}
			if (test)
				loadbool = TRUE;
			eg_setlistvlabels(gh, gd_smplist, &smplist);
		}
		args++;
	}
	if (loadbool) {
		if (remlist)
			remsfx(NULL, NULL, 0);
		set_undo(UNDO_SET_SAMPLELIST, &smplist, (LONG)bd, 0, 0);
		update_redoundo();
		checkaskquit();
	}
	addingsamplesover();
	unblockallwindows();
	if (dest)
		FlushMDest(dest);       /* flush midi for any case */
}

static void appwindowproc(EG_Gui *g, EG_Obj *obj, struct AppMessage *awmsg)
{
	struct WBArg *args;
	char name[512];
	LONG ans = TRUE;

	(void)g; (void)obj;
	E_TRY {
		if (askquit)
			ans = reqquit();
		blockallwindows();
		if (ans) {
			args = awmsg->am_ArgList;
			if (args->wa_Lock) {
				NameFromLock(args->wa_Lock, (STRPTR)name, 512);
				AddPart((STRPTR)name, args->wa_Name, 512);
				eg_setlistvlabels(gh, gd_smplist, NULL);
				prjname[0] = 0;
				askquit = FALSE;
				flush_undo();
				loadproject((CONST_STRPTR)name, &smplist, bd);
				estrcpy((STRPTR)prjname, (CONST_STRPTR)args->wa_Name, sizeof(prjname));
			}
		}
	} E_EXCEPT_DO {
		unblockallwindows();
		sortbanks();
		updategh();
		eg_setlistvlabels(gh, gd_smplist, &smplist);
		if (exception)
			report_exception();
		else
			FlushMDest(dest);       /* flush midi for any case */
	} E_END;
}

void plugact(EG_Gui *g, EG_Obj *obj, LONG value)
{
	struct pianokeys *p = (struct pianokeys *)value;
	LONG a, b, l, h, keybcode;
	struct bank *nb;

	(void)g; (void)obj;
	if ((keybcode = p->keycode) == -1) {
		if (basesetb) {
			nb = &bd[nbank];
			basesetb = FALSE;
			a = pianokeypressed(p, -1);
			if (rangesetb) {
				undobank(UNDO_SET_BOUNDS);
				b = a - nb->base;
				l = nb->lobound + b;
				h = nb->hibound + b;
				if (h > 127) {
					b = h - 127;
					l = l - b;
					h = h - b;
					a = a - b;
				}
				if (l < 0) {
					b = -l;
					l = l + b;
					h = h + b;
					a = a + b;
				}
				nb->lobound = (UBYTE)l;
				nb->hibound = (UBYTE)h;
				pianokeys_boundset(p, TRUE);
				pianokeys_bounds(p, l, h, NULL, NULL);
			}
			undobank(UNDO_SET_BASE);
			nb->base = (UBYTE)a;
			eg_settext(gh, gd_basetx, midinote((STRPTR)basestr, a));
			checkaskquit();
		} else if (rangesetb) {
			a = pianokeys_bounds(p, -1, 127, NULL, &b);
			if (a >= 0) {
				undobank(UNDO_SET_BOUNDS);
				bd[nbank].lobound = (UBYTE)a;
				bd[nbank].hibound = (UBYTE)b;
				checkaskquit();
			}
		} else {
			a = pianokeypressed(p, -1);
			if (mcontrol == 0)
				signal_playtask(PSG_PLAY, &bd[nbank], a);
		}
	} else if (keybcode >= 0) {
		keybact(keybcode, WINDOW_MAIN);
	} else if (keybcode == -2) {
		if (basesetb)
			baseset(NULL, NULL, 0);
	}
	p->keycode = -1;
}

static void keybact(LONG keybcode, LONG winno)
{
	LONG a, b, l, h;
	BOOL shift;

	if (keybcode & MYRAWCODE) {
		shift = (keybcode & SHIFTQUAL) ? TRUE : FALSE;
		keybcode &= 0xFF;
		leds_getrange(leds, &l, &h);
		a = 0;
		b = 0;
		switch (keybcode) {
		case CURSORLEFT:
			if (shift) {
				if (h > nbank)
					h--;
				else if (l > 0)
					l--;
				leds_setrange(leds, l, h);
			} else {
				a = nbank;
				if (a < 1)
					a = NUMBANKS;
			}
			break;
		case CURSORRIGHT:
			if (shift) {
				if (l < nbank)
					l++;
				else if (h < NUMBANKS - 1)
					h++;
				leds_setrange(leds, l, h);
			} else {
				a = nbank + 2;
				if (a > NUMBANKS)
					a = 1;
			}
			break;
		case CURSORUP:
			if (shift) {
				if (h > nbank)
					h = h - 10;
				else
					l = l - 10;
				if (h < nbank)
					h = nbank;
				if (l < 0)
					l = 0;
				leds_setrange(leds, l, h);
			} else {
				a = nbank - 9;
				if (a < 1)
					a = a + NUMBANKS;
			}
			break;
		case CURSORDOWN:
			if (shift) {
				if (l < nbank)
					l = l + 10;
				else
					h = h + 10;
				if (l > nbank)
					l = nbank;
				if (h >= NUMBANKS)
					h = NUMBANKS - 1;
				leds_setrange(leds, l, h);
			} else {
				a = nbank + 11;
				if (a > NUMBANKS)
					a = a - NUMBANKS;
			}
			break;
		default:
			/* F1 - F10: nothing */
			break;
		}
		if (a) {
			leds_setcurrent(leds, a - 1);
			banksel(a - 1);
		}
	} else {
		switch (keybcode) {
		case ESC_CODE:
			switch (winno) {
			case WINDOW_VOLUME:
				close_volwin(NULL, NULL, 0);
				break;
			case WINDOW_ENVELOPE:
				close_envwin(NULL, NULL, 0);
				break;
			}
			break;
		case '0': case '1': case '2': case '3': case '4':
		case '5': case '6': case '7': case '8': case '9':
			a = nbank / 10;
			b = a * 10;
			a = (keybcode - '0') & 0xF;
			if (a == 0)
				a = 10;
			a = a + b;
			leds_setcurrent(leds, a - 1);
			banksel(a - 1);
			break;
		case '.':
			a = nbank + 11;
			if (a > NUMBANKS)
				a = a - NUMBANKS;
			leds_setcurrent(leds, a - 1);
			banksel(a - 1);
			break;
		case ')':
			b = bd[nbank].pri + 1;
			if (b > NUMBANKS)
				b = NUMBANKS;
			eg_setslide(gh, gd_bankprisl, b);
			setpri(NULL, NULL, b);
			break;
		case '(':
			b = bd[nbank].pri - 1;
			if (b < 1)
				b = 1;
			eg_setslide(gh, gd_bankprisl, b);
			setpri(NULL, NULL, b);
			break;
		case '-':
		case '_':
			b = bd[nbank].fine - 1 - FINE_CENTR;
			if (b < -100)
				b = -100;
			eg_setslide(gh, gd_finesl, b);
			fine(NULL, NULL, b);
			break;
		case '+':
		case '=':
			b = bd[nbank].fine + 1 - FINE_CENTR;
			if (b > 100)
				b = 100;
			eg_setslide(gh, gd_finesl, b);
			fine(NULL, NULL, b);
			break;
		case '*':
			eg_setslide(gh, gd_finesl, 0);
			fine(NULL, NULL, 0);
			break;
		case TAB_CODE:
			if (winno == WINDOW_MAIN) {
				if (volgh->wnd)
					volumewindow(NULL, NULL, 0);
				else if (envgh->wnd)
					envelwindow(NULL, NULL, 0);
			} else if (winno == WINDOW_VOLUME) {
				if (envgh->wnd)
					envelwindow(NULL, NULL, 0);
				else
					mainwindow(NULL, NULL, 0);
			} else if (winno == WINDOW_ENVELOPE) {
				mainwindow(NULL, NULL, 0);
			}
			break;
		case RETURN_CODE:
			pianokeypressed(mp, a = bd[nbank].base);
			if (mcontrol == 0)
				signal_playtask(PSG_PLAY, &bd[nbank], a);
			break;
		case SPACE_CODE:
			if (mcontrol == 0)
				signal_playtask(PSG_PLAY, &bd[nbank], 255);
			break;
		case DEL_CODE:
			free_instr(NULL, NULL, 0);
			break;
		default:
			break;
		}
	}
}

static void audioset(EG_Gui *g, EG_Obj *obj, LONG aud)
{
	struct MenuItem *item;
	UWORD f;

	(void)g; (void)obj;
	E_TRY {
		item = settingsitem(0);
		if (rangesetb == 0)
			leds_setactive(leds, -1, LSETNONE);
		f = (UWORD)(item->Flags & ~CHECKED);
		if (aud) {
			if (audiostatus(SFX_SET_AUDIO_STATUS, TRUE) == FALSE)
				Raise('AUDB');
			else
				item->Flags = (UWORD)(f | CHECKED);
		} else {
			audiostatus(SFX_SET_AUDIO_STATUS, FALSE);
			item->Flags = f;
		}
	} E_EXCEPT {
		eg_setcheck(gh, gd_audiock, FALSE);
		report_exception();
	} E_END;
}

static void followset(EG_Gui *g, EG_Obj *obj, LONG fol)
{
	struct MenuItem *item;
	UWORD f;

	(void)g; (void)obj;
	item = settingsitem(2);
	followb = fol ? TRUE : FALSE;
	f = (UWORD)(item->Flags & ~CHECKED);
	item->Flags = (UWORD)(f | (followb ? CHECKED : 0));
}

void follow(LONG note, LONG midi)
{
	LONG i, p = -1;
	struct bank *bn;

	if (!leds)
		return;
	for (i = 0; i <= NUMBANKS - 1; i++) {
		bn = &bd[i];
		if (bn->instr && bn->midi == midi) {
			if (note >= bn->lobound && note <= bn->hibound) {
				if (p == -1 && i < nbank)
					p = i;
				if (i > nbank) {
					p = i;
					break;
				}
			}
		}
	}
	if (p > -1) {
		leds_setcurrent(leds, p);
		banksel(p);
	}
}

static void rangeset(EG_Gui *g, EG_Obj *obj, LONG sel)
{
	struct bank *bn;

	(void)g; (void)obj;
	bn = &bd[nbank];
	mareaset = 255;
	if ((rangesetb = sel)) {
		updaterange(bn->lobound, bn->hibound);
		updateleds();
		if (basesetb == FALSE)
			pianokeys_boundset(mp, TRUE);
	} else {
		checkplayingbanks();
		pianokeys_bounds(mp, 255, 127, NULL, NULL);
		/* boundset(FALSE) is done by bounds(255) */
	}
}

static void midiset(EG_Gui *g, EG_Obj *obj, LONG sel)
{
	struct MenuItem *item;
	UWORD f;

	(void)g; (void)obj;
	mcontrol = sel;
	item = settingsitem(1);
	f = (UWORD)(item->Flags & ~CHECKED);
	if (mcontrol)
		item->Flags = (UWORD)(f | CHECKED);
	else
		item->Flags = f;
	clearcontrollers();
}

static void ledact(EG_Gui *g, EG_Obj *obj, LONG value)
{
	struct leds *ld = (struct leds *)value;
	LONG i, f, b, c, l = 0;
	UBYTE xpt[NUMBANKS * 2];

	(void)g; (void)obj;
	if (ld->act == LEDACT_SETBANK) {
		banksel(leds_setcurrent(ld, -1));
	} else if (ld->act == LEDACT_SETRANGE) {
		leds_getrange(ld, &rangelo, &rangehi);
	} else if (ld->act == LEDACT_MOVERANGE) {
		f = leds_getrange(ld, NULL, NULL);
		if (f > rangelo) {
			i = rangehi - rangelo;
			b = 0;
			c = -1;
		} else {
			i = 0;
			b = rangehi - rangelo;
			c = 1;
		}
		while ((c > 0 && i <= b) || (c < 0 && i >= b)) {
			xpt[l] = (UBYTE)(rangelo + i);
			xpt[l + 1] = (UBYTE)(f + i);
			xchgbanksachn(&bd[xpt[l]], &bd[xpt[l + 1]]);
			l += 2;
			i = i + c;
		}
		set_undo(UNDO_SET_XCHGBANKS, xpt, l, 0, 0);
		updategh();
	}
}

static void banksel(LONG bank)
{
	struct sfx *snd;
	struct bank *bn, *bno;
	LONG v;

	bno = &bd[nbank];
	nbank = bank;
	rangelo = nbank;
	rangehi = nbank;
	bn = &bd[nbank];
	if ((snd = bn->instr)) {
		estrcpy((STRPTR)instrumenttext, (CONST_STRPTR)(sfx_stereo(snd) ? "= " : "- "),
		        sizeof(instrumenttext));
		estrcat((STRPTR)instrumenttext, (CONST_STRPTR)snd->ln.ln_Name, sizeof(instrumenttext));
		eg_settext(gh, gd_instrtext, (CONST_STRPTR)instrumenttext);
	} else {
		eg_settext(gh, gd_instrtext, (CONST_STRPTR)"");
	}
	if ((v = bn->midi) != bno->midi)
		eg_setslide(gh, gd_midichansl, v);
	if ((v = bn->pri) != bno->pri)
		eg_setslide(gh, gd_bankprisl, v);
	if ((v = bn->set & (B_DUR_ON | B_DRUM)) != (bno->set & (B_DUR_ON | B_DRUM))) {
		if (v)
			v = (v & B_DRUM) ? 2 : 1;
		eg_setmx(gh, gd_durmx, v);
		eg_setdisabled(gh, gd_loopchk, v == 2 ? TRUE : FALSE);
	}
	if ((v = bn->group) != bno->group)
		eg_setslide(gh, gd_groupsl, v);
	if ((v = bn->set & B_LOOP) != (bno->set & B_LOOP))
		eg_setcheck(gh, gd_loopchk, v ? TRUE : FALSE);
	if ((v = bn->set & B_MONO) != (bno->set & B_MONO))
		eg_setcheck(gh, gd_monochk, v ? TRUE : FALSE);
	v = (v == 0 || (bn->set & B_DRUM)) ? TRUE : FALSE;
	eg_setdisabled(gh, gd_monoslide, v);
	eg_setdisabled(gh, gd_monovsens, (v || bn->monoslide == 0) ? TRUE : FALSE);
	if ((v = bn->monovsens) != bno->monovsens)
		eg_setslide(gh, gd_monovsens, v);
	if ((v = bn->monoslide) != bno->monoslide) {
		eg_setslide(gh, gd_monoslide, v = bn->monoslide);
		if (v)
			estringf((STRPTR)monosliderstr, sizeof(monosliderstr),
			         (CONST_STRPTR)"\\d.\\z\\d[2] ", v / 50, (v % 50) * 2);
		else
			estrcpy((STRPTR)monosliderstr, LOC(STRID_OFF), sizeof(monosliderstr));
		eg_settext(gh, gd_monosltxt, (CONST_STRPTR)monosliderstr);
	}
	v = bn->base;
	if (rangesetb || basesetb || followb == 0)
		pianokeypressed(mp, v);
	if (v != bno->base)
		eg_settext(gh, gd_basetx, midinote((STRPTR)basestr, v));
	if ((v = bn->fine) != bno->fine) {
		eg_setslide(gh, gd_finesl, v - FINE_CENTR);
		eg_setnum(gh, gd_finetx, v - FINE_CENTR);
	}
	update_volgh();
	update_envgh();
	if (rangesetb) {
		updateleds();
		updaterange(bn->lobound, bn->hibound);
	} else {
		checkplayingbanks();
	}
}

static void midichan(EG_Gui *g, EG_Obj *obj, LONG chan)
{
	(void)g; (void)obj;
	undobank(UNDO_SET_MIDI);
	bd[nbank].midi = (UBYTE)chan;
	if (rangesetb) {
		updateleds();
		updaterange(bd[nbank].lobound, bd[nbank].hibound);
	}
	checkaskquit();
}

static void setpri(EG_Gui *g, EG_Obj *obj, LONG bankpri)
{
	(void)g; (void)obj;
	undobank(UNDO_SET_PRI);
	bd[nbank].pri = (UBYTE)bankpri;
	sortbanks();
	if (rangesetb) {
		updateleds();
		updaterange(bd[nbank].lobound, bd[nbank].hibound);
	}
	checkaskquit();
}

static void dur(EG_Gui *g, EG_Obj *obj, LONG duration)
{
	LONG f;
	BOOL disable = FALSE;

	(void)g; (void)obj;
	undobank(UNDO_SET_SET_DUR);
	f = bd[nbank].set & ~(B_DRUM | B_DUR_ON);
	switch (duration) {
	case 1:
		f = f | B_DUR_ON;
		break;
	case 2:
		f = f | B_DRUM;
		disable = TRUE;
		break;
	}
	bd[nbank].set = (UBYTE)f;
	eg_setdisabled(gh, gd_loopchk, disable);
	disable = (disable || (bd[nbank].set & B_MONO) == 0) ? TRUE : FALSE;
	eg_setdisabled(gh, gd_monoslide, disable);
	eg_setdisabled(gh, gd_monovsens, (disable || bd[nbank].monoslide == 0) ? TRUE : FALSE);
	checkaskquit();
}

static void setgroup(EG_Gui *g, EG_Obj *obj, LONG group)
{
	(void)g; (void)obj;
	undobank(UNDO_SET_GROUP);
	bd[nbank].group = (UBYTE)group;
	checkaskquit();
}

static void monophonic(EG_Gui *g, EG_Obj *obj, LONG mono)
{
	LONG f;
	BOOL dis;

	(void)g; (void)obj;
	undobank(UNDO_SET_SET_MONO);
	f = bd[nbank].set;
	bd[nbank].set = (UBYTE)(mono ? f | B_MONO : f & ~B_MONO);
	dis = (mono == FALSE || (bd[nbank].set & B_DRUM)) ? TRUE : FALSE;
	eg_setdisabled(gh, gd_monoslide, dis);
	eg_setdisabled(gh, gd_monovsens, (dis || bd[nbank].monoslide == 0) ? TRUE : FALSE);
	checkaskquit();
}

static void monoslide(EG_Gui *g, EG_Obj *obj, LONG set)
{
	(void)g; (void)obj;
	undobank(UNDO_SET_MONOSLIDE);
	bd[nbank].monoslide = (WORD)set;
	if (set)
		estringf((STRPTR)monosliderstr, sizeof(monosliderstr),
		         (CONST_STRPTR)"\\d.\\z\\d[2]", set / 50, (set % 50) * 2);
	else
		estrcpy((STRPTR)monosliderstr, LOC(STRID_OFF), sizeof(monosliderstr));
	eg_settext(gh, gd_monosltxt, (CONST_STRPTR)monosliderstr);
	eg_setdisabled(gh, gd_monovsens, set ? FALSE : TRUE);
	checkaskquit();
}

static void monovsens(EG_Gui *g, EG_Obj *obj, LONG set)
{
	(void)g; (void)obj;
	undobank(UNDO_SET_MONOVSENS);
	bd[nbank].monovsens = (UBYTE)set;
	checkaskquit();
}

static void loop(EG_Gui *g, EG_Obj *obj, LONG lp)
{
	LONG f;

	(void)g; (void)obj;
	undobank(UNDO_SET_SET_LOOP);
	f = bd[nbank].set;
	bd[nbank].set = (UBYTE)(lp ? f | B_LOOP : f & ~B_LOOP);
	checkaskquit();
}

static void baseset(EG_Gui *g, EG_Obj *obj, LONG value)
{
	(void)g; (void)obj; (void)value;
	pianokeypressed(mp, bd[nbank].base);   /* force to draw */
	pianokeys_boundset(mp, FALSE);
	mareaset = 255;
	basesetb = basesetb ? FALSE : TRUE;
	if (basesetb == 0) {
		if (rangesetb)
			pianokeys_boundset(mp, TRUE);
	}
}

static void fine(EG_Gui *g, EG_Obj *obj, LONG fn)
{
	(void)g; (void)obj;
	undobank(UNDO_SET_FINE);
	bd[nbank].fine = (UBYTE)(fn + FINE_CENTR);
	eg_setnum(gh, gd_finetx, fn);
	checkaskquit();
	signal_playtask(PSG_TUNE, &bd[nbank], 0);
}

static void samplesel(EG_Gui *g, EG_Obj *obj, LONG num)
{
	struct lln *ln;
	struct sfx *snd;
	ULONG oldseconds, oldmicros;
	LONG i;

	(void)g; (void)obj;
	E_TRY {
		i = num;
		ln = (struct lln *)smplist.lh_Head;
		while (ln->ln.ln_Succ && --i >= 0)
			ln = (struct lln *)ln->ln.ln_Succ;
		if (!ln->ln.ln_Succ)
			ln = NULL;
		num++;
		if (ln == NULL) {
			dblistnum = 0;
		} else {
			oldseconds = seconds;
			oldmicros = micros;
			CurrentTime(&seconds, &micros);     /* for comparison */
			if (remlist) {
				if (num == dblistnum && DoubleClick(oldseconds, oldmicros, seconds, micros)) {
					eg_setlistvlabels(gh, gd_smplist, NULL);
					set_undo(UNDO_SET_SAMPLEDELETE, ln->pointer, (LONG)bd, 0, 0);
					delsnd((struct sfx *)ln->pointer, bd, FALSE);
					checkaskquit();
					eg_setlistvlabels(gh, gd_smplist, &smplist);
					if (smplist.lh_Head->ln_Succ == NULL)
						remsfx(NULL, NULL, 0);
					updategh();
					seconds = 0;
					micros = 0;
				}
			} else if ((snd = bd[nbank].instr) == NULL
			           || (num == dblistnum && DoubleClick(oldseconds, oldmicros, seconds, micros))) {
				seconds = 0;
				micros = 0;
				blockallwindows();
				set_undo(UNDO_SET_INSTR, bd, nbank, 0, 0);
				setinstr(&bd[nbank], (struct sfx *)ln->pointer);
				updategh();
				checkaskquit();
				addingsamplesover();
				unblockallwindows();
			}
			dblistnum = num;
		}
	} E_EXCEPT {
		unblockallwindows();
		report_exception();
	} E_END;
}

static void free_instr(EG_Gui *g, EG_Obj *obj, LONG value)
{
	(void)g; (void)obj; (void)value;
	if (bd[nbank].instr != NULL) {
		set_undo(UNDO_SET_INSTR, bd, nbank, 0, 0);
		clearinstr(&bd[nbank]);
		updategh();
		checkaskquit();
	}
}

static void reload(EG_Gui *g, EG_Obj *obj, LONG value)
{
	struct sfx *instr[NUMBANKS], *snd;
	LONG f;

	(void)g; (void)obj; (void)value;
	if ((snd = bd[nbank].instr)) {
		blockallwindows();
		for (f = 0; f <= NUMBANKS - 1; f++)
			instr[f] = bd[f].instr == snd ? clearinstr(&bd[f]) : NULL;
		for (f = 0; f <= NUMBANKS - 1; f++)
			if (instr[f] == snd)
				setinstr(&bd[f], snd);
		unblockallwindows();
		updategh();
		addingsamplesover();
	}
}

/* files chosen in addsfx's requester, loaded after it closed */
struct pathnode {
	struct pathnode *next;
	char path[1];
};

static struct {
	struct pathnode *head, *tail;
	LONG total;
	BOOL nomem;
} addctx;

static void addsfx_file(CONST_STRPTR path, APTR ud)
{
	struct pathnode *n;

	(void)ud;
	n = AllocVec(sizeof(struct pathnode) + strlen((const char *)path),
	             MEMF_PUBLIC | MEMF_CLEAR);
	if (!n) {
		addctx.nomem = TRUE;
		return;
	}
	strcpy(n->path, (const char *)path);
	if (addctx.tail)
		addctx.tail->next = n;
	else
		addctx.head = n;
	addctx.tail = n;
	addctx.total++;
}

static void addsfx(EG_Gui *g, EG_Obj *obj, LONG value)
{
	char file[256];
	char name[260];
	struct pathnode *fentry;
	BOOL test;
	volatile BOOL loadbool = FALSE;
	LONG i = 0;

	(void)g; (void)obj; (void)value;
	addctx.head = addctx.tail = NULL;
	addctx.total = 0;
	addctx.nomem = FALSE;
	E_TRY {
		blockallwindows();
		set_undo(UNDO_PREP_SAMPLELIST, &smplist, (LONG)bd, 0, 0);
		file[0] = 0;
		name[0] = 0;
		if (filereq(gh->wnd, LOC(STRID_ADDSAMPLES), FR_MULTI | FR_PATTERN,
		            (STRPTR)sndpath, sizeof(sndpath), (STRPTR)file, sizeof(file),
		            addsfx_file, NULL)) {
			for (fentry = addctx.head; fentry; fentry = fentry->next) {
				estrcpy((STRPTR)name, (CONST_STRPTR)fentry->path, 260);
				eg_setlistvlabels(gh, gd_smplist, NULL);
				if (loadinstrumentsfrombank((CONST_STRPTR)name, &smplist)) {
					test = TRUE;
				} else {
					printstatus(LOC(STRID_ADDINGINSTRUMENTS), FilePart((STRPTR)name),
					            0, i++, addctx.total);
					addsnd((CONST_STRPTR)name, &smplist, FALSE, &test);
				}
				if (test)
					loadbool = TRUE;
				eg_setlistvlabels(gh, gd_smplist, &smplist);
			}
			if (addctx.nomem)
				Raise('MEM');
		}
		if (dest)
			FlushMDest(dest);       /* lockwin was too long */
	} E_EXCEPT_DO {
		if (loadbool) {
			if (remlist)
				remsfx(NULL, NULL, 0);
			set_undo(UNDO_SET_SAMPLELIST, &smplist, (LONG)bd, 0, 0);
			update_redoundo();
			checkaskquit();
		}
		addingsamplesover();
		unblockallwindows();
		while ((fentry = addctx.head)) {
			addctx.head = fentry->next;
			FreeVec(fentry);
		}
		addctx.tail = NULL;
		if (exception)
			report_exception();
	} E_END;
}

static void remsfx(EG_Gui *g, EG_Obj *obj, LONG value)
{
	(void)g; (void)obj; (void)value;
	if (remlist) {
		remlist = FALSE;
		eg_settext(gh, gd_listtx, LOC(STRID_INSTRUMENTS));
	} else {
		if (smplist.lh_Head->ln_Succ) {
			remlist = TRUE;
			eg_settext(gh, gd_listtx, LOC(STRID_DELETELIST));
		}
	}
}

static void clearsfx(EG_Gui *g, EG_Obj *obj, LONG value)
{
	LONG x;

	(void)g; (void)obj; (void)value;
	if (smplist.lh_Head->ln_Succ) {
		if ((x = reqclear())) {
			set_undo(x == 1 ? UNDO_PREP_SAMPLELIST : UNDO_PREP_SAMPLELISTINSTR,
			         &smplist, (LONG)bd, 0, 0);
			eg_setlistvlabels(gh, gd_smplist, NULL);
			if (clearsmplist(bd, &smplist, x == 1 ? TRUE : FALSE)) {
				checkaskquit();
				set_undo(x == 1 ? UNDO_SET_SAMPLELIST : UNDO_SET_SAMPLELISTINSTR,
				         &smplist, (LONG)bd, 0, 0);
			}
			eg_setlistvlabels(gh, gd_smplist, &smplist);
			updategh();
		}
	}
	if (remlist)
		remsfx(NULL, NULL, 0);
}

/*
  ==========================================================================
                               [Volume window]
  ==========================================================================
*/

static void update_volgh(void)
{
	struct bank *bn;
	struct sfx *snd;
	LONG a, b, n = 0;

	bn = &bd[nbank];
	eg_setnum(volgh, vgh_banknum, nbank + 1);
	eg_setnum(volgh, vgh_freq, (snd = bn->instr) ? sfx_basefreq(snd) : 0);
	if (snd) {
		n = (LONG)((ULONG)sfx_frames(snd) * 1000UL) / sfx_basefreq(snd);
		b = n;
		a = b / 1000;
		b = b - a * 1000;
		fullnameptr = sfx_pathname(snd);
	} else {
		a = 0;
		b = 0;
		fullnameptr = (CONST_STRPTR)"";
	}
	estringf((STRPTR)timetext, sizeof(timetext), (CONST_STRPTR)"\\d.\\d s", a, b);
	eg_settext(volgh, vgh_time, (CONST_STRPTR)timetext);
	eg_settext(volgh, vgh_fullname, fullnameptr);
	eg_setslide(volgh, vgh_volum, (100 * bn->volume + 128) / 256);
	eg_setslide(volgh, vgh_veloc, bn->velsens);
	eg_setslide(volgh, vgh_after, bn->aftersens);
	eg_setcheck(volgh, vgh_subaft, (bn->set & B_ADDAFTERT) == 0 ? TRUE : FALSE);
	eg_setslide(volgh, vgh_panor, bn->panorama - 128);
	eg_setnum(volgh, vgh_pantx, bn->panorama - 128);
	eg_setslide(volgh, vgh_pwide, bn->panwide);
	eg_setslide(volgh, vgh_pitch, bn->pitchsens);
	eg_setslide(volgh, vgh_firstskip, bn->firstskip);
	eg_setcheck(volgh, vgh_mctvol, bn->mctrlvol ? TRUE : FALSE);
	eg_setcheck(volgh, vgh_mctpan, bn->mctrlpan ? TRUE : FALSE);
	a = snd ? n : bn->firstskip;
	eg_setnum(volgh, vgh_skipnum, bn->firstskip < a ? bn->firstskip : a);
}

static void volumewindow(EG_Gui *g, EG_Obj *obj, LONG value)
{
	(void)g; (void)obj; (void)value;
	if (volgh->wnd) {
		ActivateWindow(volgh->wnd);
		WindowToFront(volgh->wnd);
	} else {
		eg_settext(volgh, vgh_fullname, (CONST_STRPTR)"");
		eg_openwin(volgh);
		eg_settext(volgh, vgh_fullname, fullnameptr);
		if ((volwindow = volgh->wnd))
			SetWindowTitles(volwindow, (CONST_STRPTR)-1, mainbartext);
	}
}

static void close_volwin(EG_Gui *g, EG_Obj *obj, LONG value)
{
	(void)g; (void)obj; (void)value;
	if (volgh->wnd) {
		volwindow = NULL;
		eg_closewin(volgh);
	}
}

static void clean_volwin(EG_Gui *g, EG_Obj *obj, LONG value)
{
	(void)g; (void)obj; (void)value;
	volgh = NULL;
	volwindow = NULL;
	title_a = NULL;         /* freed by egui */
}

static void open_volwin(struct Screen *screen, struct TextAttr *ta)
{
	struct EG_WinOpts opts;
	LONG v, l, p, w, c, r, s, z, x, y, q, a, b, d;
	STRPTR lv, ll, lp, lw, lc, lr, ls, lz, lx, ly, lq, la, lb, ld;

	title_a = titlekeys_new(LOC(STRID_PITCHBENDER), TITLE_NORMAL, NULL);
	ls = getLocStr(STRID_FIRSTSKIP, &s);
	lz = getLocStr(STRID_HUNPERCENT, &z);
	lv = getLocStr(STRID_VOLUMESET, &v);
	lx = getLocStr(STRID_MAXVOLUME, &x);
	ll = getLocStr(STRID_VELOCITY, &l);
	lq = getLocStr(STRID_AFTERVEL, &q);
	ly = getLocStr(STRID_AFTERTOUCH, &y);
	ld = getLocStr(STRID_SUBAFTERT, &d);
	lp = getLocStr(STRID_PANORAMA, &p);
	lw = getLocStr(STRID_WIDE, &w);
	lc = getLocStr(STRID_CENTER, &c);
	la = getLocStr(STRID_MCTRLVOL, &a);
	lb = getLocStr(STRID_MCTRLPAN, &b);
	lr = getLocStr(STRID_NOTERANGE, &r);
	makeboommenu();

	opts.menu = boommenu;
	opts.awproc = NULL;
	opts.close = close_volwin;
	opts.clean = clean_volwin;
	opts.info = NULL;
	opts.screen = screen;
	opts.font = ta;
	opts.left = mbprefs.volumewinx;
	opts.top = mbprefs.volumewiny;
	opts.width = mbprefs.volumewinw;
	opts.height = mbprefs.volumewinh;
	opts.wtype = WTYPE_SIZE;
	opts.hide = mbprefs.volumehide ? TRUE : FALSE;
	opts.screentitle = NULL;

	volgh = eg_add(mh, LOC(STRID_VOLUMECTRLWIN), eg_rows(
		eg_bevel(
			eg_rows(
				eg_cols(
					vgh_banknum = eg_num(1, LOC(STRID_BANKNUM), TRUE, 3),
					vgh_freq = eg_num(0, LOC(STRID_FREQNUM), TRUE, 7),
					vgh_time = eg_text((CONST_STRPTR)"", LOC(STRID_TIMENUM), TRUE, 5),
					NULL),
				vgh_fullname = eg_text((CONST_STRPTR)"", NULL, TRUE, 15),
				eg_bevelr(eg_bevel(
					eg_cols(
						vgh_firstskip = eg_slide(skip_slide, ls, FALSE, 0, 3000, 0, 6, (CONST_STRPTR)"", s, FALSE),
						vgh_skipnum = eg_num(0, NULL, TRUE, 4),
						NULL))),
				eg_bevelr(
					eg_rows(
						eg_cols(
							eg_button(vol_center, lz, z),
							vgh_volum = eg_slide(vol_slide, lv, FALSE, 0, 200, 100, 6, (CONST_STRPTR)"\\d[3]", v, FALSE),
							eg_button(vol_maxvol, lx, x),
							NULL),
						eg_rows(
							eg_cols(
								vgh_veloc = eg_slide(vel_slide, ll, FALSE, 0, 100, 0, 6, (CONST_STRPTR)"\\d[3]", l, FALSE),
								eg_button(after_vel, lq, q),
								NULL),
							eg_cols(
								vgh_after = eg_slide(after_slide, ly, FALSE, 0, 100, 0, 6, (CONST_STRPTR)"\\d[3]", y, FALSE),
								vgh_subaft = eg_check(after_sub, ld, FALSE, TRUE, d, FALSE),
								NULL),
							NULL),
						eg_cols(
							vgh_panor = eg_slide(pan_slide, lp, FALSE, -128, 128, 0, 6, (CONST_STRPTR)"", p, FALSE),
							vgh_pantx = eg_num(0, (CONST_STRPTR)"", FALSE, 4),
							NULL),
						eg_cols(
							vgh_pwide = eg_slide(wid_slide, lw, FALSE, 0, 30, 0, 6, (CONST_STRPTR)"\\d[2]", w, FALSE),
							eg_button(pan_center, lc, c),
							NULL),
						eg_eqcols(
							vgh_mctvol = eg_check(mctvol_set, la, FALSE, TRUE, a, FALSE),
							vgh_mctpan = eg_check(mctpan_set, lb, FALSE, TRUE, b, FALSE),
							NULL),
						NULL)),
				eg_plugin(title_act, &title_a->plugin),
				vgh_pitch = eg_slide(ben_slide, lr, FALSE, 0, 12, 0, 6, (CONST_STRPTR)"\\d[2]", r, FALSE),
				NULL)),
		NULL), &opts);
	if (!volgh) {
		title_a = NULL;
		Raise('GUI');
	}

	update_volgh();
	if ((volwindow = volgh->wnd))
		SetWindowTitles(volwindow, (CONST_STRPTR)-1, mainbartext);
}

static void title_act(EG_Gui *g, EG_Obj *obj, LONG value)
{
	struct titlekeys *tl = (struct titlekeys *)value;

	(void)g; (void)obj;
	if (tl->keycode >= 0)
		keybact(tl->keycode, WINDOW_VOLUME);
}

static void ben_slide(EG_Gui *g, EG_Obj *obj, LONG ben)
{
	(void)g; (void)obj;
	undobank(UNDO_SET_PITCHSENS);
	bd[nbank].pitchsens = (UBYTE)ben;
	checkaskquit();
}

static void vol_slide(EG_Gui *g, EG_Obj *obj, LONG vol)
{
	(void)g; (void)obj;
	undobank(UNDO_SET_VOLUME);
	bd[nbank].volume = (WORD)(256 * vol / 100);
	checkaskquit();
	signal_playtask(PSG_VOLUME, &bd[nbank], 0);
}

static void vol_center(EG_Gui *g, EG_Obj *obj, LONG value)
{
	(void)g; (void)obj; (void)value;
	if (bd[nbank].volume != 256) {
		undobank(UNDO_SET_VOLUME);
		bd[nbank].volume = 256;
		checkaskquit();
		update_volgh();
		signal_playtask(PSG_VOLUME, &bd[nbank], 0);
	}
}

static void vol_maxvol(EG_Gui *g, EG_Obj *obj, LONG value)
{
	struct sfx *snd;
	LONG v;

	(void)g; (void)obj; (void)value;
	v = (snd = bd[nbank].instr) ? sfx_maxvolume(snd) : 512;
	if (bd[nbank].volume != v) {
		undobank(UNDO_SET_VOLUME);
		bd[nbank].volume = (WORD)v;
		checkaskquit();
		update_volgh();
		signal_playtask(PSG_VOLUME, &bd[nbank], 0);
	}
}

static void vel_slide(EG_Gui *g, EG_Obj *obj, LONG vel)
{
	(void)g; (void)obj;
	undobank(UNDO_SET_VELSENS);
	bd[nbank].velsens = (UBYTE)vel;
	checkaskquit();
	signal_playtask(PSG_VOLUME, &bd[nbank], 0);
}

static void after_slide(EG_Gui *g, EG_Obj *obj, LONG after)
{
	(void)g; (void)obj;
	undobank(UNDO_SET_AFTERSENS);
	bd[nbank].aftersens = (UBYTE)after;
	checkaskquit();
	signal_playtask(PSG_VOLUME, &bd[nbank], 0);
}

static void after_sub(EG_Gui *g, EG_Obj *obj, LONG on)
{
	LONG b;

	(void)g; (void)obj;
	undobank(UNDO_SET_SET_ADDAFTERT);
	b = bd[nbank].set;
	bd[nbank].set = (UBYTE)(on ? b & ~B_ADDAFTERT : b | B_ADDAFTERT);
	checkaskquit();
	signal_playtask(PSG_VOLUME, &bd[nbank], 0);
}

static void after_vel(EG_Gui *g, EG_Obj *obj, LONG value)
{
	(void)g; (void)obj; (void)value;
	if (bd[nbank].aftersens != bd[nbank].velsens) {
		undobank(UNDO_SET_AFTERSENS);
		bd[nbank].aftersens = bd[nbank].velsens;
		checkaskquit();
		update_volgh();
		signal_playtask(PSG_VOLUME, &bd[nbank], 0);
	}
}

static void pan_slide(EG_Gui *g, EG_Obj *obj, LONG pan)
{
	(void)g; (void)obj;
	eg_setnum(volgh, vgh_pantx, pan);
	undobank(UNDO_SET_PANORAMA);
	bd[nbank].panorama = (WORD)(pan + 128);
	checkaskquit();
	signal_playtask(PSG_VOLUME, &bd[nbank], 0);
}

static void pan_center(EG_Gui *g, EG_Obj *obj, LONG value)
{
	(void)g; (void)obj; (void)value;
	if (bd[nbank].panorama != 128) {
		undobank(UNDO_SET_PANORAMA);
		bd[nbank].panorama = 128;
		checkaskquit();
		update_volgh();
		signal_playtask(PSG_VOLUME, &bd[nbank], 0);
	}
}

static void wid_slide(EG_Gui *g, EG_Obj *obj, LONG pwide)
{
	(void)g; (void)obj;
	undobank(UNDO_SET_PANWIDE);
	bd[nbank].panwide = (UBYTE)pwide;
	checkaskquit();
	signal_playtask(PSG_VOLUME, &bd[nbank], 0);
}

static void skip_slide(EG_Gui *g, EG_Obj *obj, LONG skip)
{
	struct sfx *snd;
	LONG n;

	(void)g; (void)obj;
	undobank(UNDO_SET_FIRSTSKIP);
	bd[nbank].firstskip = (WORD)skip;
	checkaskquit();
	n = (snd = bd[nbank].instr)
	    ? (LONG)((ULONG)sfx_frames(snd) * 1000UL) / sfx_basefreq(snd) : skip;
	eg_setnum(volgh, vgh_skipnum, skip < n ? skip : n);
}

static void mctvol_set(EG_Gui *g, EG_Obj *obj, LONG set)
{
	(void)g; (void)obj;
	undobank(UNDO_SET_MCTRLVOL);
	bd[nbank].mctrlvol = set ? 100 : 0;
	checkaskquit();
	signal_playtask(PSG_VOLUME, &bd[nbank], 0);
}

static void mctpan_set(EG_Gui *g, EG_Obj *obj, LONG set)
{
	(void)g; (void)obj;
	undobank(UNDO_SET_MCTRLPAN);
	bd[nbank].mctrlpan = set ? 100 : 0;
	checkaskquit();
	signal_playtask(PSG_VOLUME, &bd[nbank], 0);
}

/*
  ==========================================================================
                              [Envelope window]
  ==========================================================================
*/

static void update_envgh(void)
{
	struct bank *bn = &bd[nbank];

	eg_setnum(envgh, egh_number, nbank + 1);
	if (envp)
		envel_setenvelope(envp, bn->attack, bn->decay, bn->sustainlev, bn->release);
}

static void open_envwin(struct Screen *screen, struct TextAttr *ta)
{
	struct EG_WinOpts opts;

	envp = envel_new(screen);
	makeboommenu();

	opts.menu = boommenu;
	opts.awproc = NULL;
	opts.close = close_envwin;
	opts.clean = clean_envwin;
	opts.info = NULL;
	opts.screen = screen;
	opts.font = ta;
	opts.left = mbprefs.envelwinx;
	opts.top = mbprefs.envelwiny;
	opts.width = mbprefs.envelwinw;
	opts.height = mbprefs.envelwinh;
	opts.wtype = WTYPE_SIZE;
	opts.hide = mbprefs.envelhide ? TRUE : FALSE;
	opts.screentitle = NULL;

	envgh = eg_add(mh, LOC(STRID_VOLUMEENVELWIN), eg_bevel(
		eg_rows(
			eg_eqcols(
				eg_text((CONST_STRPTR)"", (CONST_STRPTR)"", FALSE, 1),
				egh_number = eg_num(0, LOC(STRID_BANKNUM), TRUE, 3),
				eg_text((CONST_STRPTR)"", (CONST_STRPTR)"", FALSE, 1),
				NULL),
			eg_bevelr(eg_bevel(
				egh_envel = eg_plugin(envel_act, &envp->plugin))),
			NULL)), &opts);
	if (!envgh) {
		envp = NULL;
		Raise('GUI');
	}

	update_envgh();
	if ((envwindow = envgh->wnd))
		SetWindowTitles(envwindow, (CONST_STRPTR)-1, mainbartext);
}

static void envel_act(EG_Gui *g, EG_Obj *obj, LONG value)
{
	struct envel_plugin *ep = (struct envel_plugin *)value;
	LONG attack, decay, sustain, release, keybcode;
	struct bank *nb;
	BOOL change = FALSE;

	(void)g; (void)obj;
	nb = &bd[nbank];
	if ((keybcode = ep->keycode) == -1) {
		envel_getenvelope(ep, &attack, &decay, &sustain, &release);
		if (nb->attack != attack) {
			set_undo(UNDO_SET_ATTACK, bd, nbank, 0, 0);
			nb->attack = (UBYTE)attack;
			change = TRUE;
		}
		if (nb->decay != decay) {
			set_undo(UNDO_SET_DECAY, bd, nbank, 0, 0);
			nb->decay = (UBYTE)decay;
			change = TRUE;
		}
		if (nb->sustainlev != sustain) {
			set_undo(UNDO_SET_SUSTAINLEV, bd, nbank, 0, 0);
			nb->sustainlev = (UBYTE)sustain;
			change = TRUE;
		}
		if (nb->release != release) {
			set_undo(UNDO_SET_RELEASE, bd, nbank, 0, 0);
			nb->release = (UBYTE)release;
			change = TRUE;
		}
		if (change) {
			checkaskquit();
			update_redoundo();
		}
	} else if (keybcode == -2) {
		envel_setenvelope(ep, nb->attack, nb->decay, nb->sustainlev, nb->release);
	} else if (keybcode >= 0) {
		keybact(keybcode, WINDOW_ENVELOPE);
	}
}

static void envelwindow(EG_Gui *g, EG_Obj *obj, LONG value)
{
	(void)g; (void)obj; (void)value;
	if (envgh->wnd) {
		ActivateWindow(envgh->wnd);
		WindowToFront(envgh->wnd);
	} else {
		eg_openwin(envgh);
		if ((envwindow = envgh->wnd))
			SetWindowTitles(envwindow, (CONST_STRPTR)-1, mainbartext);
	}
}

static void close_envwin(EG_Gui *g, EG_Obj *obj, LONG value)
{
	(void)g; (void)obj; (void)value;
	if (envgh->wnd) {
		envwindow = NULL;
		eg_closewin(envgh);
	}
}

static void clean_envwin(EG_Gui *g, EG_Obj *obj, LONG value)
{
	(void)g; (void)obj; (void)value;
	envgh = NULL;
	envwindow = NULL;
	envp = NULL;            /* freed by egui */
}

/*
  ==========================================================================
                               [Midimonitor]
  ==========================================================================
*/

static void midimonitoropen(EG_Gui *g, EG_Obj *obj, LONG value)
{
	(void)g; (void)obj; (void)value;
	if (mongh->wnd) {
		WindowToFront(mongh->wnd);
		ActivateWindow(mongh->wnd);
	} else {
		eg_openwin(mongh);
	}
}

/* E's mm_setmctrl(0, Not(currmcmon)): the cycle index of currmcmon */
static LONG mm_index(LONG mc)
{
	LONG i;

	for (i = 0; i <= 2; i++)
		if (mc == mcmons[i])
			return i;
	return 0;
}

static void open_midimonitwin(struct Screen *screen, struct TextAttr *ta,
                              struct TextFont *font)
{
	struct EG_WinOpts opts;
	LONG a;
	STRPTR la;

	midimon = midimonitor_new(screen, font);
	la = getLocStr(STRID_MIDICONTROLLER, &a);
	mmlabels[0] = LOC(STRID_MIDIVOLUME);
	mmlabels[1] = LOC(STRID_MIDIPAN);
	mmlabels[2] = LOC(STRID_MIDIPITCHBEND);
	mmlabels[3] = NULL;

	opts.menu = NULL;
	opts.awproc = NULL;
	opts.close = close_midimonwin;
	opts.clean = clean_midimonwin;
	opts.info = NULL;
	opts.screen = screen;
	opts.font = ta;
	opts.left = mbprefs.midimonwinx;
	opts.top = mbprefs.midimonwiny;
	opts.width = mbprefs.midimonwinw;
	opts.height = mbprefs.midimonwinh;
	opts.wtype = WTYPE_SIZE;
	opts.hide = mbprefs.midimonhide ? TRUE : FALSE;
	opts.screentitle = NULL;

	mongh = eg_add(mh, LOC(STRID_MIDIMONITORWIN), eg_rows(
		eg_bevel(
			mgh_ctrl = eg_cycle(mm_setmctrl, la, mmlabels, mm_index(currmcmon), a)),
		eg_bevel(
			eg_plugin(mm_act, &midimon->plugin)),
		NULL), &opts);
	if (!mongh) {
		midimon = NULL;
		Raise('GUI');
	}

	if (mongh->wnd)
		SetWindowTitles(mongh->wnd, (CONST_STRPTR)-1, mainbartext);
}

static void mm_setmctrl(EG_Gui *g, EG_Obj *obj, LONG set)
{
	(void)g; (void)obj;
	if (set >= 0 && set <= 2)
		currmcmon = mcmons[set];
}

static void mm_act(EG_Gui *g, EG_Obj *obj, LONG value)
{
	struct midimonitor *mm = (struct midimonitor *)value;

	(void)g; (void)obj;
	if (mm->keycode == ESC_CODE) {
		close_midimonwin(NULL, NULL, 0);
	} else if (mm->keycode == TAB_CODE) {
		if (gh->wnd) {
			WindowToFront(gh->wnd);
			ActivateWindow(gh->wnd);
		}
	}
}

static void close_midimonwin(EG_Gui *g, EG_Obj *obj, LONG value)
{
	(void)g; (void)obj; (void)value;
	if (mongh->wnd)
		eg_closewin(mongh);
}

static void clean_midimonwin(EG_Gui *g, EG_Obj *obj, LONG value)
{
	(void)g; (void)obj; (void)value;
	midimon = NULL;         /* E: END midimon; egui frees it */
	mongh = NULL;
}

/*
  ==========================================================================
                                   M E N U S
  ==========================================================================
*/

static void menu_reloadall(EG_Gui *g, EG_Obj *obj, LONG value)
{
	struct sfx *instr[NUMBANKS];
	LONG i, f = 0, l = 0;

	(void)g; (void)obj; (void)value;
	blockallwindows();
	for (i = 0; i <= NUMBANKS - 1; i++) {
		instr[i] = clearinstr(&bd[i]);
		if (instr[i] != NULL)
			l++;
	}
	for (i = 0; i <= NUMBANKS - 1; i++) {
		if (instr[i] != NULL)
			printstatus(0, 0, 0, f++, l);
		setinstr(&bd[i], instr[i]);
	}
	unblockallwindows();
	updategh();
	addingsamplesover();
}

static void menu_followset(EG_Gui *g, EG_Obj *obj, LONG value)
{
	struct MenuItem *item;

	(void)g; (void)obj; (void)value;
	item = settingsitem(2);
	followb = (item->Flags & CHECKED) ? TRUE : FALSE;
	eg_setcheck(gh, gd_followck, followb ? TRUE : FALSE);
}

static void menu_audio(EG_Gui *g, EG_Obj *obj, LONG value)
{
	struct MenuItem *item;
	BOOL aud;

	(void)g; (void)obj; (void)value;
	item = settingsitem(0);
	E_TRY {
		if (rangesetb == 0)
			leds_setactive(leds, -1, LSETNONE);
		aud = (item->Flags & CHECKED) ? TRUE : FALSE;
		if (aud) {
			if (audiostatus(SFX_SET_AUDIO_STATUS, TRUE) == FALSE)
				Raise('AUDB');
			else
				eg_setcheck(gh, gd_audiock, TRUE);
		} else {
			audiostatus(SFX_SET_AUDIO_STATUS, FALSE);
			eg_setcheck(gh, gd_audiock, FALSE);
		}
	} E_EXCEPT {
		item->Flags = (UWORD)(item->Flags & ~CHECKED);
		report_exception();
	} E_END;
}

static void menu_mcontrol(EG_Gui *g, EG_Obj *obj, LONG value)
{
	struct MenuItem *item;

	(void)g; (void)obj; (void)value;
	item = settingsitem(1);
	mcontrol = (item->Flags & CHECKED) ? TRUE : FALSE;
	eg_setcheck(gh, gd_mcontr, mcontrol ? TRUE : FALSE);
	clearcontrollers();
}

static void menu_about(EG_Gui *g, EG_Obj *obj, LONG value)
{
	(void)g; (void)obj; (void)value;
	reqabout();
}

static void menu_summary(EG_Gui *g, EG_Obj *obj, LONG value)
{
	LONG a = 0, b = 0, c = 0, d = 0, i, l, t;
	struct lln *ln;
	struct sfx *snd;
	ULONG time, dummy;

	(void)g; (void)obj; (void)value;
	for (i = 0; i <= NUMBANKS - 1; i++)
		if (bd[i].instr != NULL)
			a++;
	ln = (struct lln *)smplist.lh_Head;
	while (ln->ln.ln_Succ) {
		b++;
		snd = (struct sfx *)ln->pointer;
		if ((l = sfx_length(snd)) != -1) {
			c++;
			d = d + l;
		}
		ln = (struct lln *)ln->ln.ln_Succ;
	}
	CurrentTime(&time, &dummy);
	if ((t = (LONG)(time - timestart)) < 0)
		t = -t;
	reqsumm(strlen(prjname) == 0 ? LOC(STRID_UNNAMED) : (CONST_STRPTR)prjname,
	        a, b, c, d, t, undoleft());
}

static void freeclip(void)
{
	struct cpstring *n;

	while ((n = cpstr)) {
		cpstr = n->next;
		FreeVec(n);
	}
	e_dispose(cpbd);
	cpbd = NULL;
}

static void menu_copy(EG_Gui *g, EG_Obj *obj, LONG value)
{
	struct sfx *snd;
	struct cpstring *n, *t;
	CONST_STRPTR p, s;
	LONG i;

	(void)g; (void)obj; (void)value;
	E_TRY {
		if (cpbd != NULL)
			freeclip();
		cpsize = rangehi - rangelo + 1;
		cpbd = e_new((ULONG)cpsize * sizeof(struct bank));
		CopyMem(&bd[rangelo], cpbd, (ULONG)cpsize * sizeof(struct bank));
		for (i = 0; i <= cpsize - 1; i++) {
			if ((snd = cpbd[i].instr)) {
				p = sfx_pathname(snd);
				/* look for the path among the strings so far */
				s = (CONST_STRPTR)"";
				n = cpstr;
				if (n) {
					while (strcmp(n->s, (const char *)p) != 0 && n->next)
						n = n->next;
					s = (CONST_STRPTR)n->s;
				}
				if (strcmp((const char *)s, (const char *)p) == 0) {
					cpbd[i].instr = (struct sfx *)s;
				} else {
					t = AllocVec(sizeof(struct cpstring) + strlen((const char *)p),
					             MEMF_PUBLIC | MEMF_CLEAR);
					if (t == NULL)
						Raise('MEM');
					strcpy(t->s, (const char *)p);
					if (cpstr)
						n->next = t;
					else
						cpstr = t;
					cpbd[i].instr = (struct sfx *)t->s;
				}
			}
		}
	} E_EXCEPT {
		freeclip();
		cpsize = 0;
		report_exception();
	} E_END;
}

static void menu_paste(EG_Gui *g, EG_Obj *obj, LONG value)
{
	LONG i, prgrs = 0, total = 0;
	STRPTR name;

	(void)g; (void)obj; (void)value;
	if (cpsize == 0 || cpbd == NULL)
		return;
	blockallwindows();
	set_undo(UNDO_PREP_ALL, &smplist, (LONG)bd, rangehi - rangelo + 1, rangelo);
	for (i = 0; i <= E_MIN(rangehi - rangelo, cpsize - 1); i++)
		if (cpbd[i].instr != NULL)
			total++;
	for (i = rangelo; i <= E_MIN(rangehi, rangelo + cpsize - 1); i++) {
		if ((name = (STRPTR)cpbd[i - rangelo].instr)) {
			eg_setlistvlabels(gh, gd_smplist, NULL);
			printstatus(0, 0, 0, prgrs++, total);
			setinstrname(&bd[i], &smplist, name);
			cpbd[i - rangelo].instr = bd[i].instr;
			eg_setlistvlabels(gh, gd_smplist, &smplist);
		}
		setbank(&bd[i], &cpbd[i - rangelo]);
		cpbd[i - rangelo].instr = (struct sfx *)name;
	}
	for (; i <= rangehi; i++)
		deletebank(&bd[i]);
	set_undo(UNDO_SET_ALL, &smplist, (LONG)bd, 0, 0);
	checkaskquit();
	sortbanks();
	updategh();
	unblockallwindows();
	addingsamplesover();
}

static void menu_delete(EG_Gui *g, EG_Obj *obj, LONG value)
{
	LONG i;

	(void)g; (void)obj; (void)value;
	set_undo(UNDO_SET_BANKS, bd, rangehi - rangelo + 1, rangelo, 0);
	for (i = rangelo; i <= rangehi; i++)
		deletebank(&bd[i]);
	checkaskquit();
	sortbanks();
	updategh();
}

static void menu_sort_pri(EG_Gui *g, EG_Obj *obj, LONG value)
{
	(void)g; (void)obj; (void)value;
	sortbanksrange(SORT_PRI);
}

static void menu_sort_midi(EG_Gui *g, EG_Obj *obj, LONG value)
{
	(void)g; (void)obj; (void)value;
	sortbanksrange(SORT_MIDI);
}

static void menu_sort_name(EG_Gui *g, EG_Obj *obj, LONG value)
{
	(void)g; (void)obj; (void)value;
	sortbanksrange(SORT_NAME);
}

static void menu_sort_range(EG_Gui *g, EG_Obj *obj, LONG value)
{
	(void)g; (void)obj; (void)value;
	sortbanksrange(SORT_RANGE);
}

static void sortbanksrange(LONG type)
{
	LONG i, f, c;
	UBYTE *volatile xpt = NULL;

	E_TRY {
		if ((f = rangehi - rangelo) == 0)
			Raise('RNGE');
		xpt = e_new((ULONG)(f * (f + 1)));
		i = rangelo + 1;
		c = 0;
		while (i <= rangehi) {
			f = i;
			while (cmpbanks(&bd[f - 1], &bd[f], type) == -1) {
				xpt[c++] = (UBYTE)(f - 1);
				xpt[c++] = (UBYTE)f;
				xchgbanksachn(&bd[f - 1], &bd[f]);
				if (--f <= rangelo)
					break;
			}
			i++;
		}
		if (c) {
			set_undo(UNDO_SET_XCHGBANKS, xpt, c, 0, 0);
			checkaskquit();
		}
		updategh();
	} E_EXCEPT_DO {
		if (xpt)
			e_dispose(xpt);
		if (exception)
			report_exception();
	} E_END;
}

static void menu_undo(EG_Gui *g, EG_Obj *obj, LONG value)
{
	LONG n;

	(void)g; (void)obj; (void)value;
	E_TRY {
		eg_setlistvlabels(gh, gd_smplist, NULL);
		blockallwindows();
		if ((n = do_undo(bd, &smplist)) >= 0)
			leds_setcurrent(leds, nbank = n);
		if (asksaveundo())
			askquit = FALSE;
		else
			askquit = TRUE;
	} E_EXCEPT_DO {
		unblockallwindows();
		eg_setlistvlabels(gh, gd_smplist, &smplist);
		if (remlist)
			remsfx(NULL, NULL, 0);
		updategh();
		if (exception)
			report_exception();
	} E_END;
}

static void menu_redo(EG_Gui *g, EG_Obj *obj, LONG value)
{
	LONG n;

	(void)g; (void)obj; (void)value;
	E_TRY {
		eg_setlistvlabels(gh, gd_smplist, NULL);
		blockallwindows();
		if ((n = do_redo(bd, &smplist)) >= 0)
			leds_setcurrent(leds, nbank = n);
		if (asksaveundo())
			askquit = FALSE;
		else
			askquit = TRUE;
	} E_EXCEPT_DO {
		unblockallwindows();
		eg_setlistvlabels(gh, gd_smplist, &smplist);
		if (remlist)
			remsfx(NULL, NULL, 0);
		updategh();
		if (exception)
			report_exception();
	} E_END;
}

static void menu_save(EG_Gui *g, EG_Obj *obj, LONG value)
{
	(void)g; (void)obj; (void)value;
	if (strlen(prjname) == 0)
		save(TRUE);
	else
		save(FALSE);
}

static void menu_saveas(EG_Gui *g, EG_Obj *obj, LONG value)
{
	(void)g; (void)obj; (void)value;
	save(TRUE);
}

static void save(BOOL askr)
{
	char file[512];
	char name[260];
	BOOL ans = TRUE;

	E_TRY {
		blockallwindows();
		if (askr) {
			ans = FALSE;
			estrcpy((STRPTR)file, (CONST_STRPTR)prjname, 32);
			if (filereq(gh->wnd, LOC(STRID_SAVEPROJECTAS), FR_SAVE,
			            (STRPTR)prjpath, sizeof(prjpath), (STRPTR)file, sizeof(file),
			            NULL, NULL)) {
				estrcpy((STRPTR)prjname, FilePart((STRPTR)file), sizeof(prjname));
				ans = TRUE;
			}
		}
		if (ans) {
			estrcpy((STRPTR)name, (CONST_STRPTR)prjpath, 260);
			AddPart((STRPTR)name, (STRPTR)prjname, 260);
			saveproject((CONST_STRPTR)name, &smplist, bd);
			askquit = FALSE;
			remembersaveundo();
			projectname();
		}
	} E_EXCEPT_DO {
		unblockallwindows();
		if (exception)
			report_exception();
		else
			FlushMDest(dest);       /* flush midi for any case */
	} E_END;
}

static void menu_open(EG_Gui *g, EG_Obj *obj, LONG value)
{
	char file[512];
	LONG ans = TRUE;

	(void)g; (void)obj; (void)value;
	E_TRY {
		if (askquit)
			ans = reqquit();
		blockallwindows();
		if (ans) {
			file[0] = 0;
			if (filereq(gh->wnd, LOC(STRID_CHOOSEPROJECT), 0,
			            (STRPTR)prjpath, sizeof(prjpath), (STRPTR)file, sizeof(file),
			            NULL, NULL)) {
				/* file: prjpath and the file name */
				eg_setlistvlabels(gh, gd_smplist, NULL);
				prjname[0] = 0;
				askquit = FALSE;
				flush_undo();
				loadproject((CONST_STRPTR)file, &smplist, bd);
				estrcpy((STRPTR)prjname, FilePart((STRPTR)file), sizeof(prjname));
			}
		}
	} E_EXCEPT_DO {
		unblockallwindows();
		sortbanks();
		updategh();
		eg_setlistvlabels(gh, gd_smplist, &smplist);
		if (exception)
			report_exception();
		else
			FlushMDest(dest);       /* flush midi for any case */
	} E_END;
}

static void menu_addproject(EG_Gui *g, EG_Obj *obj, LONG value)
{
	char file[512];

	(void)g; (void)obj; (void)value;
	E_TRY {
		blockallwindows();
		set_undo(UNDO_PREP_ALL, &smplist, (LONG)bd, NUMBANKS - nbank, nbank);
		file[0] = 0;
		if (filereq(gh->wnd, LOC(STRID_CHOOSEPROJECT), 0,
		            (STRPTR)prjpath, sizeof(prjpath), (STRPTR)file, sizeof(file),
		            NULL, NULL)) {
			eg_setlistvlabels(gh, gd_smplist, NULL);
			mergeproject((CONST_STRPTR)file, &smplist, bd, nbank);
			set_undo(UNDO_SET_ALL, &smplist, (LONG)bd, 0, 0);
			checkaskquit();
		}
	} E_EXCEPT_DO {
		unblockallwindows();
		sortbanks();
		updategh();
		eg_setlistvlabels(gh, gd_smplist, &smplist);
		if (exception != 0 && exception != 'cncl')
			report_exception();
		else
			FlushMDest(dest);       /* flush midi for any case */
	} E_END;
}

static void menu_new(EG_Gui *g, EG_Obj *obj, LONG value)
{
	LONG answer = TRUE;
	struct lln *ln;

	(void)g; (void)obj; (void)value;
	if (askquit)
		answer = reqquit();
	if (answer) {
		prjname[0] = 0;
		initbanks(bd);
		ln = (struct lln *)smplist.lh_Head;
		while (ln->ln.ln_Succ) {
			sfx_end((struct sfx *)ln->pointer);
			ln = (struct lln *)ln->ln.ln_Succ;
		}
		freemidiin_icon();
		askquit = FALSE;
		flush_undo();
		updategh();
	}
}

/*
  ==========================================================================
                             updating functions
  ==========================================================================
*/

void updatemidimonitor(void)
{
	if (midimon)
		midimonitor_update(midimon, getmidicontrolarray(currmcmon));
}

void updatechannelkeys(void)
{
	LONG a, b, f;

	if (!mp)
		return;
	for (a = 0; a <= 31; a++) {
		b = getchannelnote(a) & 255;
		f = keybchannels[a];
		if (f != b) {
			if (f < 128)
				pianokeys_setplaying(mp, f, FALSE);
			if (b < 128)
				pianokeys_setplaying(mp, b, TRUE);
			keybchannels[a] = (UBYTE)b;
		}
	}
}

static void updaterange(LONG l, LONG h)
{
	LONG f, chan, i, pri;
	struct bank *nb;
	UBYTE rangeother[128];

	for (f = 0; f <= 127; f++)
		rangeother[f] = 0;
	chan = bd[nbank].midi;
	pri = bd[nbank].pri;
	for (f = 0; f <= NUMBANKS - 1; f++) {
		if (f != nbank) {                       /* not the current bank */
			nb = &bd[f];
			if (nb->instr && nb->midi == chan) {    /* same channel */
				if (nb->hibound < 128 && nb->lobound <= nb->hibound) {
					for (i = nb->lobound; i <= nb->hibound; i++) {
						if (nb->pri <= pri)
							rangeother[i] = RANGE_HIPRI;
						else if (rangeother[i] == 0)
							rangeother[i] = RANGE_LOPRI;
					}
				}
			}
		}
	}
	pianokeys_bounds(mp, l, h, rangeother, NULL);
}

static void updateleds(void)
{
	LONG f, chan, pri;
	struct bank *bn;

	chan = bd[nbank].midi;
	pri = bd[nbank].pri;
	for (f = 0; f <= NUMBANKS - 1; f++) {
		bn = &bd[f];
		if (bn->instr == NULL) {
			leds_setenabled(leds, f, FALSE);
		} else {
			if (rangesetb && bn->midi == chan) {
				if (bn->pri <= pri)
					leds_setactive(leds, f, LSETFULL);
				else
					leds_setactive(leds, f, LSETINNER);
			} else {
				leds_setenabled(leds, f, TRUE);
			}
		}
	}
}

void checkplayingbanks(void)
{
	UBYTE a[NUMBANKS];
	LONG i;

	if (!leds)
		return;
	if (rangesetb == FALSE) {
		for (i = 0; i <= NUMBANKS - 1; i++)
			a[i] = 0;
		whichbankisplaying(a, bd);
		for (i = 0; i <= NUMBANKS - 1; i++)
			leds_setactive(leds, i, a[i] ? LSETFULL : LSETNONE);
	}
}

void updategh(void)
{
	struct bank *bn;
	struct sfx *snd;
	LONG v;

	bn = &bd[nbank];
	if ((snd = bn->instr)) {
		estrcpy((STRPTR)instrumenttext, (CONST_STRPTR)(sfx_stereo(snd) ? "= " : "- "),
		        sizeof(instrumenttext));
		estrcat((STRPTR)instrumenttext, (CONST_STRPTR)snd->ln.ln_Name, sizeof(instrumenttext));
		eg_settext(gh, gd_instrtext, (CONST_STRPTR)instrumenttext);
	} else {
		eg_settext(gh, gd_instrtext, (CONST_STRPTR)"");
	}
	eg_setslide(gh, gd_midichansl, bn->midi);
	eg_setslide(gh, gd_bankprisl, bn->pri);
	if ((v = bn->set & (B_DUR_ON | B_DRUM)))
		v = (v & B_DRUM) ? 2 : 1;
	eg_setmx(gh, gd_durmx, v);
	eg_setdisabled(gh, gd_loopchk, v == 2 ? TRUE : FALSE);
	eg_setslide(gh, gd_groupsl, bn->group);
	v = bn->set & B_LOOP;
	eg_setcheck(gh, gd_loopchk, v ? TRUE : FALSE);
	v = bn->set & B_MONO;
	eg_setcheck(gh, gd_monochk, v ? TRUE : FALSE);
	v = (v == 0 || (bn->set & B_DRUM)) ? TRUE : FALSE;
	eg_setdisabled(gh, gd_monoslide, v);
	eg_setdisabled(gh, gd_monovsens, (v || bn->monoslide == 0) ? TRUE : FALSE);
	eg_setslide(gh, gd_monovsens, bn->monovsens);
	eg_setslide(gh, gd_monoslide, v = bn->monoslide);
	if (v)
		estringf((STRPTR)monosliderstr, sizeof(monosliderstr),
		         (CONST_STRPTR)"\\d.\\z\\d[2] ", v / 50, (v % 50) * 2);
	else
		estrcpy((STRPTR)monosliderstr, LOC(STRID_OFF), sizeof(monosliderstr));
	eg_settext(gh, gd_monosltxt, (CONST_STRPTR)monosliderstr);
	v = bn->base;
	pianokeypressed(mp, v);
	eg_settext(gh, gd_basetx, midinote((STRPTR)basestr, v));
	v = bn->fine;
	eg_setslide(gh, gd_finesl, v - FINE_CENTR);
	eg_setnum(gh, gd_finetx, v - FINE_CENTR);
	updateleds();
	if (rangesetb)
		updaterange(bn->lobound, bn->hibound);
	update_redoundo();
	projectname();
	update_volgh();
	update_envgh();
}

static void update_redoundo(void)
{
	LONG t;

	if ((t = nextundo()) != lastundotype) {
		lastundotype = t;
		eg_settext(gh, gd_undotx, undotypetext(t));
	}
}

static CONST_STRPTR undotypetext(LONG type)
{
	switch (type) {
	case UNDO_SET_SAMPLELIST:       return LOC(STRID_SET_SAMPLELIST);
	case UNDO_SET_SAMPLELISTINSTR:  return LOC(STRID_SET_SAMPLELIST);
	case UNDO_SET_ALL:              return LOC(STRID_SET_ALL);
	case UNDO_SET_BANKS:            return LOC(STRID_SET_BANKS);
	case UNDO_SET_XCHGBANKS:        return LOC(STRID_SET_XCHGBANKS);
	case UNDO_SET_SAMPLEDELETE:     return LOC(STRID_SET_SAMPLEDELETE);
	case UNDO_SET_SAMPLEUNDELETE:   return LOC(STRID_SET_SAMPLEDELETE);
	case UNDO_SET_INSTR:            return LOC(STRID_SET_INSTR);
	case UNDO_SET_MIDI:             return LOC(STRID_SET_MIDI);
	case UNDO_SET_PRI:              return LOC(STRID_SET_PRI);
	case UNDO_SET_BASE:             return LOC(STRID_SET_BASE);
	case UNDO_SET_FINE:             return LOC(STRID_SET_FINE);
	case UNDO_SET_SET_LOOP:         return LOC(STRID_SET_SET_LOOP);
	case UNDO_SET_SET_DUR:          return LOC(STRID_SET_SET_DUR);
	case UNDO_SET_SET_MONO:         return LOC(STRID_SET_SET_MONO);
	case UNDO_SET_SET_ADDAFTERT:    return LOC(STRID_SET_SET_ADDAFTERT);
	case UNDO_SET_BOUNDS:           return LOC(STRID_SET_BOUNDS);
	case UNDO_SET_VOLUME:           return LOC(STRID_SET_VOLUME);
	case UNDO_SET_VELSENS:          return LOC(STRID_SET_VELSENS);
	case UNDO_SET_RELEASE:          return LOC(STRID_SET_RELEASE);
	case UNDO_SET_PANORAMA:         return LOC(STRID_SET_PANORAMA);
	case UNDO_SET_PANWIDE:          return LOC(STRID_SET_PANWIDE);
	case UNDO_SET_PITCHSENS:        return LOC(STRID_SET_PITCHSENS);
	case UNDO_SET_ATTACK:           return LOC(STRID_SET_ATTACK);
	case UNDO_SET_DECAY:            return LOC(STRID_SET_DECAY);
	case UNDO_SET_SUSTAINLEV:       return LOC(STRID_SET_SUSTAINLEV);
	case UNDO_SET_AFTERSENS:        return LOC(STRID_SET_AFTERSENS);
	case UNDO_SET_FIRSTSKIP:        return LOC(STRID_SET_FIRSTSKIP);
	case UNDO_SET_MCTRLVOL:         return LOC(STRID_SET_MCTRLVOL);
	case UNDO_SET_MCTRLPAN:         return LOC(STRID_SET_MCTRLPAN);
	case UNDO_SET_GROUP:            return LOC(STRID_SET_GROUP);
	case UNDO_SET_MONOVSENS:        return LOC(STRID_SET_MONOVSENS);
	case UNDO_SET_MONOSLIDE:        return LOC(STRID_SET_MONOSLIDE);
	}
	return (CONST_STRPTR)"";
}

/*
  ==========================================================================
                           other useful functions
  ==========================================================================
*/

static void checkaskquit(void)
{
	if (askquit == FALSE) {
		askquit = TRUE;
		projectname();
	}
}

LONG pianokeypressed(struct pianokeys *p, LONG key)
{
	LONG x;

	if (!p)
		return -1;
	x = pianokeys_keypressed(p, key);
	if (key == -1)
		key = x;
	eg_settext(gh, gd_notetext,
	           key >= 0 && key < 128 ? midinote((STRPTR)notestr, key) : (CONST_STRPTR)"");
	return x;
}

static STRPTR midinote(STRPTR str, LONG n)
{
	static const char *const names[12] = {
		"c ", "c#", "d ", "d#", "e ", "f ", "f#", "g ", "g#", "a ", "a#", "h "
	};

	return estringf(str, 8, (CONST_STRPTR)"\\s\\d", (LONG)names[n % 12], n);
}
