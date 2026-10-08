/*
 * midiin.c - main program: arguments, libraries, commodity, timer and the
 * main event loop (midiIn.e)
 *
 * Differences from the E code:
 * - mathieeedoubbas.library and reqtools.library are not opened (the C
 *   code needs neither). intuition, graphics, gadtools, dos and friends
 *   come from libnix's auto-open.
 * - locale.library and the catalog are opened and closed by locale.c
 *   (locale_open/locale_close), at the same points as in the E code.
 * - The commodity objects raise 'MEM' when one can not be created (the E
 *   code did that with RAISE ... IF CreateCxObj()=NIL); each is attached
 *   as soon as it exists, so DeleteCxObjAll() frees it.
 * - The program directory is always restored at exit (the E code did not
 *   if there was no current directory before).
 * - The font name and the TextAttr from the arguments are freed at exit
 *   (the E runtime did that), also when the font could not be opened.
 * - The timer request is only aborted at exit if it was sent.
 * - freeprefs() (setup.c) runs after freeallMRoutes(), for what the E
 *   runtime freed at exit.
 * - The variables used in the cleanup are file statics, so they keep
 *   their values when an exception jumps out of the main loop.
 */
#include <string.h>
#include <exec/types.h>
#include <exec/memory.h>
#include <exec/ports.h>
#include <exec/io.h>
#include <dos/dos.h>
#include <dos/rdargs.h>
#include <devices/timer.h>
#include <intuition/intuition.h>
#include <graphics/text.h>
#include <libraries/commodities.h>
#include <workbench/startup.h>
#include <workbench/workbench.h>
#include <utility/tagitem.h>

#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/utility.h>
#include <proto/commodities.h>
#include <proto/diskfont.h>
#include <proto/layers.h>
#include <proto/icon.h>
#include <proto/midi.h>

#include "eport.h"
#include "locale.h"
#include "globals.h"
#include "report.h"
#include "banks.h"
#include "diskoper.h"
#include "play.h"
#include "undo.h"
#include "sfx.h"
#include "../gui/egui.h"
#include "../gui/setup.h"
#include "../gui/pianokeys.h"

#include "../gui/mainwin.h"

/* ------------------------------------------------------------ globals */

struct bank bd[NUMBANKS];
struct List smplist;
UBYTE keybchannels[32];
struct mbprefs mbprefs;
char prjname[32];
STRPTR mainbartext;
struct MDest *dest;
struct Screen *defscreen;
struct TextAttr *defta;
struct TextFont *deffont;
EG_Multi *mh;
EG_Gui *scopegh;
EG_Gui *setgh;
EG_Gui *saugh;
LONG mareaset = 255;
ULONG timestart;

/* libraries (IconBase: diskoper.c, LocaleBase: locale.c) */
__typeof__(UtilityBase) UtilityBase;
__typeof__(CxBase) CxBase;
__typeof__(DiskfontBase) DiskfontBase;
__typeof__(LayersBase) LayersBase;
struct MidiBase *MidiBase;

extern struct WBStartup *_WBenchMsg;    /* libnix */

static ULONG dummy;
static char pubscreenname[121];
static STRPTR fontname;                 /* FONTNAME, AllocVec'd */
static BPTR oldlock;
static BOOL dirchanged;

#define EVT_HOTKEY 1                    /* Cx related */
static LONG cxpri;
static struct MsgPort *broker_mp;
static CxObj *broker;

static struct MsgPort *timermp;
static struct timerequest *timereq;
static BOOL timersent;

/* ------------------------------------------------------------ helpers */

static void triggertime(struct timerequest *tr, ULONG micros)
{
	tr->tr_node.io_Command = TR_ADDREQUEST;
	tr->tr_time.tv_secs = 0;
	tr->tr_time.tv_micro = micros;
	SendIO((struct IORequest *)tr);
	timersent = TRUE;
}

/* attaches a new commodity object to parent; raises 'MEM' if NULL */
static CxObj *cxattach(CxObj *parent, CxObj *o)
{
	if (!o)
		Raise('MEM');
	AttachCxObj(parent, o);
	return o;
}

static LONG val(CONST_STRPTR s)
{
	LONG v = 0;

	if (StrToLong(s, &v) < 0)
		v = 0;
	return v;
}

static STRPTR strdupvec(CONST_STRPTR s)
{
	ULONG l = strlen((const char *)s) + 1;
	STRPTR d = (STRPTR)AllocVec(l, MEMF_ANY);

	if (d)
		CopyMem((APTR)s, d, l);
	return d;
}

static void getargs(void)
{
	LONG a[6] = { 0, 0, 0, 0, 0, 0 };
	struct RDArgs *rdargs;
	STRPTR s;
	LONG l, fsize = 0, i;

	if (!_WBenchMsg) {
		rdargs = ReadArgs((CONST_STRPTR)"PROJECT, PUBSCREENNAME, FONTNAME, FONTSIZE/N, CX_POPKEY, CX_PRIORITY/N",
		                  a, 0);
		if (rdargs) {
			if ((s = (STRPTR)a[0])) {
				if (strlen((const char *)s) > 0) {
					estrcpy((STRPTR)prjname, FilePart(s), sizeof(prjname));
					if ((l = (LONG)(PathPart(s) - s)))
						estrcpy((STRPTR)prjpath, s,
						        E_MIN(l + 1, (LONG)sizeof(prjpath)));
				}
			}
			if ((s = (STRPTR)a[2]))
				fontname = strdupvec(s);
			if ((s = (STRPTR)a[3]))
				fsize = *(LONG *)s;
			if ((s = (STRPTR)a[1]))
				estrcpy((STRPTR)pubscreenname, s, sizeof(pubscreenname));
			if ((s = (STRPTR)a[4]))
				estrcpy((STRPTR)cxhotkey, s, sizeof(cxhotkey));
			if ((s = (STRPTR)a[5]))
				cxpri = *(LONG *)s;
			FreeArgs(rdargs);
		}
	} else if (IconBase) {
		struct WBArg *wb_arg = _WBenchMsg->sm_ArgList;
		struct DiskObject *diskobj;
		STRPTR *tt;
		BPTR olddir = 0;
		BOOL changed;

		i = _WBenchMsg->sm_NumArgs - 1;
		do {
			changed = FALSE;
			if (wb_arg[i].wa_Lock) {
				olddir = CurrentDir(wb_arg[i].wa_Lock);
				changed = TRUE;
			}
			if ((diskobj = GetDiskObject(wb_arg[i].wa_Name))) {
				tt = (STRPTR *)diskobj->do_ToolTypes;
				if ((s = FindToolType((APTR)tt, (CONST_STRPTR)"PUBSCREENNAME")))
					if (pubscreenname[0] == 0)
						estrcpy((STRPTR)pubscreenname, s, sizeof(pubscreenname));
				if ((s = FindToolType((APTR)tt, (CONST_STRPTR)"FONTNAME")))
					if (!fontname)
						fontname = strdupvec(s);
				if ((s = FindToolType((APTR)tt, (CONST_STRPTR)"FONTSIZE")))
					if (fsize < 4)
						fsize = val(s);
				if ((s = FindToolType((APTR)tt, (CONST_STRPTR)"CX_POPKEY")))
					if (cxhotkey[0] == 0)
						estrcpy((STRPTR)cxhotkey, s, sizeof(cxhotkey));
				if ((s = FindToolType((APTR)tt, (CONST_STRPTR)"CX_PRIORITY")))
					cxpri = val(s);
				FreeDiskObject(diskobj);
			}
			if (i > 0) {
				BPTR lk;

				if ((lk = wb_arg[i].wa_Lock)) {
					if (!NameFromLock(lk, (STRPTR)prjpath, 256))
						prjpath[0] = 0;
					estrcpy((STRPTR)prjname, (CONST_STRPTR)wb_arg[i].wa_Name, sizeof(prjname));
				}
			}
			if (changed)
				CurrentDir(olddir);
			olddir = 0;
			i = i > 0 ? 0 : -1;
		} while (i >= 0);
	}
	if (cxhotkey[0] == 0)
		estrcpy((STRPTR)cxhotkey, (CONST_STRPTR)"control shift m", sizeof(cxhotkey));
	if (fontname && fsize > 3) {
		defta = (struct TextAttr *)e_new(sizeof(struct TextAttr));
		defta->ta_Name = fontname;
		defta->ta_YSize = (UWORD)fsize;
	}
}

/* ------------------------------------------------------------ the MIDI */

static void midiinput(void)
{
	struct MidiPacket *packet;
	LONG type, a, b, mid;

	while ((packet = GetMidiPacket(dest))) {
		type = packet->Type;
		a = packet->MidiMsg[1];
		b = packet->MidiMsg[0];
		FreeMidiPacket(packet);
		mid = -1;

		if ((type == MMF_NOTEON || type == MMF_NOTEOFF) && mcontrol) {
			if ((rangesetb | basesetb) == 0)
				checkplayingbanks();
			updatechannelkeys();
		}

		if (type == MMF_NOTEON && mcontrol) {
			pianokeys_autokey(mp, a, SELECTDOWN);
			pianokeypressed(mp, -1);
			if ((rangesetb | basesetb) == 0) {
				if (followb)
					follow(a, (b & 0xF) + 1);
			} else {
				mid = 1;
			}
			if (rangesetb && !basesetb) {
				if (mareaset < 128) {
					if (mareaset < a)
						pianokeys_bounds(mp, mareaset, a, NULL, NULL);
					else
						pianokeys_bounds(mp, a, mareaset, NULL, NULL);
					mareaset = 255;
				} else {
					mareaset = a;
				}
			}
		} else if (type == MMF_NOTEOFF && mcontrol) {
			if ((rangesetb | basesetb) == 0) {
				if (pianokeypressed(mp, -1) == a)
					pianokeypressed(mp, 255);
			} else if (rangesetb && !basesetb) {
				if (mareaset == a)
					mareaset = 255;
			}
		} else if (mcontrol == 0) {
			mareaset = 255;
		}
		if (mid == 1) {         /* process every midi message */
			mp->keycode = -1;
			plugact(NULL, NULL, (LONG)mp);
		}
	}
}

static void showgui(void)
{
	show();
	if (defscreen)
		ScreenToFront(defscreen);
}

static void cxinput(void)
{
	CxMsg *msg;
	LONG msgid;
	ULONG msgtype;

	while ((msg = (CxMsg *)GetMsg(broker_mp))) {
		msgid = CxMsgID(msg);
		msgtype = CxMsgType(msg);
		ReplyMsg((struct Message *)msg);
		switch (msgtype) {
		case CXM_IEVENT:
			if (msgid == EVT_HOTKEY)        /* from the sender */
				showgui();
			break;
		case CXM_COMMAND:
			switch (msgid) {
			case CXCMD_DISABLE:
				ActivateCxObj(broker, FALSE);
				break;
			case CXCMD_ENABLE:
				ActivateCxObj(broker, TRUE);
				break;
			case CXCMD_KILL:
				if (askquit) {
					if (reqquit())
						Raise(0);
				} else {
					Raise(0);
				}
				break;
			case CXCMD_UNIQUE:
			case CXCMD_APPEAR:
				showgui();
				break;
			case CXCMD_DISAPPEAR:
				hide(NULL, NULL, 0);
				break;
			}
			break;
		}
	}
}

/* ------------------------------------------------------------ main */

int main(void)
{
	E_TRY {
		LONG res = -1;
		ULONG sigmidi, sigwnd, cxsigflag, sigtime, signal, signalmask;
		BOOL reqfirsttime = TRUE;
		struct NewBroker nb;
		CxObj *filter;
		BPTR lock;

		newlist(&smplist);      /* this must be the first thing */
		initbanks(bd);
		memset(keybchannels, 255, sizeof(keybchannels));
		if (!(mh = eg_multiinit()))
			Raise('MEM');
		if ((lock = GetProgramDir())) {
			oldlock = CurrentDir(lock);
			dirchanged = TRUE;
		}
		locale_open();
		IconBase = OpenLibrary((CONST_STRPTR)"icon.library", 36);
		if (!(UtilityBase = (__typeof__(UtilityBase))OpenLibrary((CONST_STRPTR)"utility.library", 37)))
			Throw('LIB', (APTR)"utility v37+");
		if (!(CxBase = (__typeof__(CxBase))OpenLibrary((CONST_STRPTR)"commodities.library", 37)))
			Throw('LIB', (APTR)"commodities v37+");
		if (!(DiskfontBase = (__typeof__(DiskfontBase))OpenLibrary((CONST_STRPTR)"diskfont.library", 37)))
			Throw('LIB', (APTR)"diskfont v37+");
		if (!(LayersBase = (__typeof__(LayersBase))OpenLibrary((CONST_STRPTR)"layers.library", 37)))
			Throw('LIB', (APTR)"layers v37+");
		if (!(MidiBase = (struct MidiBase *)OpenLibrary((CONST_STRPTR)"midi.library", MIDIVERSION)))
			Throw('LIB', (APTR)"midi v7.-1");
		if (!(dest = CreateMDest(0, 0)))
			Raise('MEM');
		if (!(timermp = CreateMsgPort()))
			Raise('MEM');
		if (!(timereq = (struct timerequest *)CreateIORequest(timermp, sizeof(struct timerequest))))
			Raise('MEM');
		if (OpenDevice((CONST_STRPTR)"timer.device", UNIT_VBLANK, (struct IORequest *)timereq, 0)) {
			DeleteIORequest((struct IORequest *)timereq);
			timereq = NULL;
			Raise('TIME');
		}
		sigtime = 1UL << timermp->mp_SigBit;
		triggertime(timereq, 1000000 / 10);

		initsoundfx(NUMBANKS);

		init_undo(10000);

		getargs();
		if (defta)
			if (!(deffont = OpenDiskFont(defta))) {
				e_dispose(defta);
				defta = NULL;
			}
		if (!(broker_mp = CreateMsgPort()))
			Raise('MEM');
		cxsigflag = 1UL << broker_mp->mp_SigBit;

		nb.nb_Version = NB_VERSION;
		nb.nb_Name = LOC(STRID_MIDIIN);         /* identifies this broker */
		nb.nb_Title = LOC(STRID_CXTITLE);
		nb.nb_Descr = LOC(STRID_CXDESCR);
		nb.nb_Unique = NBU_UNIQUE | NBU_NOTIFY;
		nb.nb_Flags = COF_SHOW_HIDE;
		nb.nb_Pri = (BYTE)cxpri;
		nb.nb_Port = broker_mp;
		nb.nb_ReservedChannel = 0;
		if (!(broker = CxBroker(&nb, NULL)))
			Raise(0);

		filter = cxattach(broker, CxFilter(cxhotkey));
		cxattach(filter, CxSender(broker_mp, EVT_HOTKEY));
		cxattach(filter, CxTranslate(NULL));
		if (CxObjError(filter))
			Raise('CXER');
		ActivateCxObj(broker, TRUE);

		defscreen = LockPubScreen((CONST_STRPTR)pubscreenname);
		open_aboutpic(defscreen, defta);
		open_status(defscreen, defta, deffont);
		initprefs((CONST_STRPTR)prjname, bd);

		CurrentTime(&timestart, &dummy);
		mainbartext = string_info();
		open_gui(defscreen, defta, deffont);

		sigwnd = eg_multisig(mh);
		sigmidi = 1UL << dest->DestPort->mp_SigBit;

		install_playtask();

		signalmask = sigtime | sigwnd | sigmidi | cxsigflag | SIGBREAKF_CTRL_C;
		while (res != 0) {
			res = -1;
			signal = Wait(signalmask);
			if (signal & sigmidi)
				midiinput();
			if (signal & sigwnd) {
				res = eg_multimessage(mh);
				if (reqfirsttime) {
					reqabout();
					reqfirsttime = FALSE;
				}
			}
			if (signal & sigtime) {
				WaitIO((struct IORequest *)timereq);
				timersent = FALSE;
				updatechannelkeys();
				checkplayingbanks();
				updatemidimonitor();
				triggertime(timereq, 1000000 / 10);
			}
			if (signal & cxsigflag)
				cxinput();
			if (signal & SIGBREAKF_CTRL_C)
				Raise('^C');
		}
	} E_EXCEPT_DO {
		struct TagItem off[] = {
			{ SFX_SET_AUDIO_STATUS, FALSE },
			{ TAG_DONE, 0 }
		};
		struct Message *msg;

		audio_attrs(off);
		if (mh)
			eg_cleanmulti(mh);
		mh = NULL;
		if (defscreen)
			UnlockPubScreen(0, defscreen);
		defscreen = NULL;
		deinstall_playtask();
		clearsmplist(bd, &smplist, FALSE);
		freeallMRoutes();
		freeprefs();
		report_exception();
		if (broker)
			DeleteCxObjAll(broker);
		if (broker_mp) {
			while ((msg = GetMsg(broker_mp)))
				ReplyMsg(msg);
			DeleteMsgPort(broker_mp);
		}
		free_undo();
		freesoundfx();
		if (timereq) {
			if (timersent) {
				AbortIO((struct IORequest *)timereq);
				WaitIO((struct IORequest *)timereq);
			}
			CloseDevice((struct IORequest *)timereq);
			DeleteIORequest((struct IORequest *)timereq);
		}
		if (timermp)
			DeleteMsgPort(timermp);
		if (dest)
			DeleteMDest(dest);
		dest = NULL;
		if (MidiBase)
			CloseLibrary((struct Library *)MidiBase);
		if (LayersBase)
			CloseLibrary((struct Library *)LayersBase);
		if (deffont)
			CloseFont(deffont);
		e_dispose(defta);
		if (fontname)
			FreeVec(fontname);
		if (DiskfontBase)
			CloseLibrary((struct Library *)DiskfontBase);
		if (CxBase)
			CloseLibrary((struct Library *)CxBase);
		if (UtilityBase)
			CloseLibrary((struct Library *)UtilityBase);
		freemidiin_icon();
		if (IconBase)
			CloseLibrary(IconBase);
		IconBase = NULL;
		locale_close();
		if (dirchanged)
			CurrentDir(oldlock);
	} E_END;
	return 0;
}
