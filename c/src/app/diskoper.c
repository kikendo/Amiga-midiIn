/*
 * diskoper.c - port of mbdiskoper.e
 *
 * The E version used the IFFParser_oo module; here a small reader (the
 * whole file in memory) and writer produce and read the same FORM MBFF
 * files: BANK, PREF, LSMP and UNDO chunks. The sample file requester uses
 * asl.library instead of reqtools.
 *
 * Change from the E code: the UNDO chunk is read up to its real length
 * (the E code took the length from the chunk's first longword).
 */
#include <string.h>
#include <exec/types.h>
#include <exec/memory.h>
#include <dos/dos.h>
#include <workbench/workbench.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/icon.h>
#include <proto/midi.h>

#include "eport.h"
#include "locale.h"
#include "report.h"
#include "undo.h"
#include "play.h"
#include "filereq.h"
#include "diskoper.h"

typedef char mbprefs_size_check[sizeof(struct mbprefs) == 78 ? 1 : -1];

#define PREFS_SIZE   78
#define PREFS_OLD_SIZE 12
#define VERSION_0_UNDO 1000

char sndpath[256];
char prjpath[256];

struct Library *IconBase;
static struct DiskObject *projecticon;
static const char preferencesname[] = "PROGDIR:midiIn.pref";

static ULONG getl(const UBYTE *p)
{
	return ((ULONG)p[0] << 24) | ((ULONG)p[1] << 16) | ((ULONG)p[2] << 8) | p[3];
}

static UWORD getw(const UBYTE *p)
{
	return (UWORD)((p[0] << 8) | p[1]);
}

/* ------------------------------------------------------------ IFF reading */

struct iffr {
	UBYTE *buf;
	LONG len;
	LONG size;              /* size of the chunk last found */
};

static void iffr_load(struct iffr *r, CONST_STRPTR name)
{
	BPTR fh;
	LONG len;

	r->buf = 0;
	if (!(fh = Open(name, MODE_OLDFILE)))
		Raise('oold');
	Seek(fh, 0, OFFSET_END);
	len = Seek(fh, 0, OFFSET_BEGINNING);
	if (len < 12) {
		Close(fh);
		Raise('oiff');
	}
	r->buf = (UBYTE *)AllocVec((ULONG)len + 1, MEMF_PUBLIC | MEMF_CLEAR);
	if (!r->buf) {
		Close(fh);
		Raise('MEM');
	}
	r->len = len;
	if (Read(fh, r->buf, len) != len) {
		Close(fh);
		Raise('READ');
	}
	Close(fh);
	if (getl(r->buf) != 'FORM' || getl(r->buf + 8) != 'MBFF')
		Raise('oiff');
	if ((LONG)getl(r->buf + 4) + 8 < r->len)
		r->len = (LONG)getl(r->buf + 4) + 8;
}

static void iffr_free(struct iffr *r)
{
	if (r->buf)
		FreeVec(r->buf);
	r->buf = 0;
}

/* data of the first chunk id in the FORM, NULL if none */
static UBYTE *iffr_first(struct iffr *r, LONG id)
{
	LONG pos = 12;

	while (pos + 8 <= r->len) {
		LONG cid = (LONG)getl(r->buf + pos);
		LONG clen = (LONG)getl(r->buf + pos + 4);

		if (clen < 0 || pos + 8 + clen > r->len)
			Raise('MNGL');
		if (cid == id) {
			r->size = clen;
			return r->buf + pos + 8;
		}
		pos += 8 + ((clen + 1) & ~1L);
	}
	r->size = 0;
	return 0;
}

/* ------------------------------------------------------------ IFF writing */

struct iffw {
	BPTR fh;
	LONG start[4];          /* file positions of open chunk sizes */
	WORD depth;
};

static void iffw_write(struct iffw *w, CONST_APTR data, LONG len)
{
	if (len > 0 && Write(w->fh, (APTR)data, len) != len)
		Raise('writ');
}

static void iffw_open(struct iffw *w, CONST_STRPTR name)
{
	w->depth = 0;
	if (!(w->fh = Open(name, MODE_NEWFILE)))
		Raise('onew');
}

static void iffw_chunk(struct iffw *w, LONG id)
{
	UBYTE hd[8] = { (UBYTE)(id >> 24), (UBYTE)(id >> 16), (UBYTE)(id >> 8), (UBYTE)id, 0, 0, 0, 0 };

	iffw_write(w, hd, 8);
	w->start[w->depth++] = Seek(w->fh, 0, OFFSET_CURRENT);
	if (id == 'FORM') {
		static const UBYTE type[4] = { 'M', 'B', 'F', 'F' };

		iffw_write(w, type, 4);
	}
}

static void iffw_close(struct iffw *w)
{
	LONG end = Seek(w->fh, 0, OFFSET_CURRENT), start = w->start[--w->depth];
	LONG len = end - start;
	UBYTE l[4] = { (UBYTE)(len >> 24), (UBYTE)(len >> 16), (UBYTE)(len >> 8), (UBYTE)len };

	Seek(w->fh, start - 4, OFFSET_BEGINNING);
	iffw_write(w, l, 4);
	Seek(w->fh, end, OFFSET_BEGINNING);
	if (len & 1) {
		static const UBYTE pad = 0;

		iffw_write(w, &pad, 1);
	}
}

static void iffw_end(struct iffw *w)
{
	if (w->fh)
		Close(w->fh);
	w->fh = 0;
}

/* ---------------------------------------------------------------- icons */

void freemidiin_icon(void)
{
	if (IconBase) {
		if (projecticon)
			FreeDiskObject(projecticon);
		projecticon = 0;
	}
}

static void getmidiin_icon(CONST_STRPTR name)
{
	char s[300];

	if (!IconBase)
		return;
	freemidiin_icon();
	if (name) {
		projecticon = GetDiskObject(name);
		return;
	}
	estrcpy((STRPTR)s, (CONST_STRPTR)prjpath, sizeof(s));
	AddPart((STRPTR)s, (STRPTR)"def_midiIn_project", sizeof(s));
	if (!(projecticon = GetDiskObject((CONST_STRPTR)s)))
		if (!(projecticon = GetDiskObject((CONST_STRPTR)"ENV:Sys/def_midiIn_project")))
			if (!(projecticon = GetDiskObject((CONST_STRPTR)"PROGDIR:Icons/def_midiIn_project")))
				projecticon = GetDefDiskObject(WBPROJECT);
}

static void savemidiin_icon(CONST_STRPTR name)
{
	APTR olddeftool;

	if (!IconBase)
		return;
	if (!projecticon)
		getmidiin_icon(0);
	if (projecticon) {
		olddeftool = (APTR)projecticon->do_DefaultTool;
		projecticon->do_DefaultTool = (APTR)"midiIn:midiIn";
		PutDiskObject(name, projecticon);
		projecticon->do_DefaultTool = olddeftool;
	}
}

/* ------------------------------------------------------------ preferences */

static void writeprefs(struct iffw *w, struct mbprefs *prefs)
{
	struct Node *ln;

	prefs->magic = MAGIC_VERSION_1_PREFS;
	prefs->sndoffs = PREFS_SIZE;
	prefs->prjoffs = (WORD)(strlen(sndpath) + 1 + PREFS_SIZE);
	prefs->routeoffs = (WORD)(prefs->prjoffs + strlen(prjpath) + 1);
	iffw_write(w, prefs, PREFS_SIZE);
	iffw_write(w, sndpath, (LONG)strlen(sndpath) + 1);
	iffw_write(w, prjpath, (LONG)strlen(prjpath) + 1);
	for (ln = mysrclist.lh_Head; ln->ln_Succ; ln = ln->ln_Succ)
		if (ln->ln_Name[0] != ' ')
			iffw_write(w, ln->ln_Name + 2, (LONG)strlen(ln->ln_Name + 2) + 1);
	iffw_write(w, "", 1);
}

static void writesamplelist(struct iffw *w, struct List *slist)
{
	struct lln *ln;

	if (!slist->lh_Head->ln_Succ)
		return;
	iffw_chunk(w, 'LSMP');
	for (ln = (struct lln *)slist->lh_Head; ln->ln.ln_Succ; ln = (struct lln *)ln->ln.ln_Succ) {
		STRPTR p = sfx_pathname((struct sfx *)ln->pointer);

		if (p)
			iffw_write(w, p, (LONG)strlen((char *)p) + 1);
	}
	iffw_close(w);
}

static void markroute(CONST_STRPTR name)
{
	struct Node *ln;

	for (ln = mysrclist.lh_Head; ln->ln_Succ; ln = ln->ln_Succ)
		if (strcmp(ln->ln_Name + 2, (const char *)name) == 0)
			ln->ln_Name[0] = '+';
}

static BOOL convertprefs(const UBYTE *s, LONG l, struct mbprefs *prefs)
{
	LONG magic = (LONG)getl(s);
	const UBYTE *v;
	LONG f;

	if (magic == MAGIC_VERSION_1_PREFS) {
		if (l < PREFS_SIZE)
			return FALSE;
		CopyMem((APTR)s, prefs, PREFS_SIZE);
	} else if (magic == MAGIC_VERSION_0_PREFS) {
		if (l < PREFS_SIZE - 8)
			return FALSE;
		CopyMem((APTR)s, prefs, PREFS_SIZE - 8);
		if (prefs->dmaper)
			prefs->mixfreq = 3546895 / prefs->dmaper;
		prefs->dmaper = 0;
	} else {
		/* oldest format: dmaper, routeoffs, sndoffs, prjoffs, buffsize,
		 * led, midictrl, activeb */
		if ((WORD)getw(s + 2) != PREFS_OLD_SIZE)
			return FALSE;
		prefs->dmaper = (WORD)getw(s);
		if (prefs->dmaper)
			prefs->mixfreq = 3546895 / prefs->dmaper;
		prefs->dmaper = 0;
		prefs->routeoffs = 0;
		markroute((CONST_STRPTR)s + getw(s + 2));
		prefs->sndoffs = (WORD)getw(s + 4);
		prefs->prjoffs = (WORD)getw(s + 6);
		prefs->led = s[9];
		prefs->midictrl = s[10];
		prefs->activeb = s[11];
	}
	if (prefs->sndoffs) {
		v = s + prefs->sndoffs;
		if (*v)
			estrcpy((STRPTR)sndpath, (CONST_STRPTR)v, sizeof(sndpath));
	}
	if (prefs->prjoffs) {
		v = s + prefs->prjoffs;
		if (*v)
			estrcpy((STRPTR)prjpath, (CONST_STRPTR)v, sizeof(prjpath));
	}
	if (prefs->routeoffs) {
		v = s + prefs->routeoffs;
		while ((f = (LONG)strlen((const char *)v)) > 0) {
			markroute((CONST_STRPTR)v);
			v += f + 1;
			if (v >= s + l)
				return TRUE;
		}
	}
	return TRUE;
}

static void defaultsettings(struct mbprefs *prefs)
{
	prefs->mainwinx = prefs->mainwiny = prefs->mainwinh = prefs->mainwinw = -1;
	prefs->volumewinx = prefs->volumewiny = prefs->volumewinh = prefs->volumewinw = -1;
	prefs->volumehide = TRUE;
	prefs->envelwinx = prefs->envelwiny = prefs->envelwinh = prefs->envelwinw = -1;
	prefs->envelhide = TRUE;
	prefs->scopewinx = prefs->scopewiny = prefs->scopewinh = prefs->scopewinw = -1;
	prefs->scopehide = TRUE;
	prefs->midimonwinx = prefs->midimonwiny = prefs->midimonwinh = prefs->midimonwinw = -1;
	prefs->midimonhide = TRUE;
	prefs->dmaper = 0;
	prefs->routeoffs = 0;
	prefs->sndoffs = 0;
	prefs->prjoffs = 0;
	prefs->currentmcm = 32;
	prefs->led = TRUE;
	prefs->midictrl = TRUE;
	prefs->activeb = 0;
	prefs->maxchannels = 4;
	prefs->msgflags = MMF_NOTEOFF | MMF_NOTEON | MMF_PITCHBEND | MMF_POLYPRESS
	                  | MMF_CTRL | MMF_CHANPRESS;
	prefs->chanflags = (WORD)0xFFFF;
	prefs->ahiaudioid = 0;
	prefs->mixfreq = 28375;
	estrcpy((STRPTR)prjpath, (CONST_STRPTR)"PROGDIR:Projects", sizeof(prjpath));
	sndpath[0] = 0;
}

/* ---------------------------------------------------------------- samples */

/* reqsample: 1 = replace (choose another file), 2 = abort, 0 = skip */
static LONG asksfx(STRPTR name, ULONG size)
{
	LONG ask;
	char file[109];

	if ((ask = reqsample(name)) == 1) {
		estrcpy((STRPTR)file, name, sizeof(file));
		if (filereq(0, LOC(STRID_CHOOSEANOTHERSAMPLE), FR_PATTERN,
		            (STRPTR)sndpath, sizeof(sndpath), (STRPTR)file, sizeof(file), 0, 0))
			estrcpy(name, (CONST_STRPTR)file, size);
		else
			ask = 0;
	}
	return ask;
}

static void load_instruments(const UBYTE *m, LONG size, struct List *slist,
                             struct bank *bn, const LONG *instr)
{
	const UBYTE *i;
	LONG x, l, all = 0, k;
	char buf[512];
	struct sfx *snd;

	for (i = m; i < m + size; i += strlen((const char *)i) + 1)
		all++;
	x = 1;
	while (size > 0) {
		if ((size = size - (LONG)strlen((const char *)m) - 1) >= 0) {
			printstatus(LOC(STRID_LOOKINGFORSAMPLE), FilePart((STRPTR)m), 0, x, all);
			if (!(snd = addsnd((CONST_STRPTR)m, slist, TRUE, 0))) {
				estrcpy((STRPTR)buf, (CONST_STRPTR)sndpath, sizeof(buf));
				AddPart((STRPTR)buf, FilePart((STRPTR)m), sizeof(buf));
				if (!(snd = addsnd((CONST_STRPTR)buf, slist, TRUE, 0))) {
					estrcpy((STRPTR)buf, FilePart((STRPTR)m), sizeof(buf));
					if ((l = asksfx((STRPTR)buf, sizeof(buf))) == 1)
						snd = addsnd((CONST_STRPTR)buf, slist, FALSE, 0);
					else if (l == 2)
						size = 0;
				}
			}
			if (bn && instr && snd)
				for (k = 0; k < NUMBANKS; k++)
					if (instr[k] == x)
						setinstr(&bn[k], snd);
		}
		m += strlen((const char *)m) + 1;
		x++;
	}
	addingsamplesover();
}

/* ---------------------------------------------------------------- projects */

void save_project(CONST_STRPTR name, struct List *slist, struct bank *bnk,
                  struct mbprefs *prefs, BOOL saveicons, BOOL saveundo)
{
	struct iffw w;

	w.fh = 0;
	E_TRY {
		struct bank nb;
		LONG f, size, cks;
		UBYTE head[4];
		APTR node;

		iffw_open(&w, name);
		iffw_chunk(&w, 'FORM');

		iffw_chunk(&w, 'BANK');
		f = sizeof(struct bank);
		head[0] = 0; head[1] = 0; head[2] = 0; head[3] = (UBYTE)f;
		iffw_write(&w, head, 4);
		for (f = 0; f < NUMBANKS; f++) {
			CopyMem(&bnk[f], &nb, sizeof(struct bank));
			if (nb.instr)
				nb.instr = (struct sfx *)convlnptrtonum(nb.instr, slist);
			iffw_write(&w, &nb, sizeof(struct bank));
		}
		iffw_close(&w);

		iffw_chunk(&w, 'PREF');
		writeprefs(&w, prefs);
		iffw_close(&w);

		writesamplelist(&w, slist);

		if (saveundo) {
			WORD hd[4];

			iffw_chunk(&w, 'UNDO');
			hd[0] = VERSION_0_UNDO;
			hd[1] = UNDO_SET_MAX;
			hd[2] = sizeof(struct bank);
			hd[3] = NUMBANKS;
			iffw_write(&w, hd, 8);
			node = exportundo(0, &size, &cks);
			while (node) {
				iffw_write(&w, &cks, 4);
				iffw_write(&w, node, size);
				node = exportundo(node, &size, &cks);
			}
			iffw_close(&w);
		}
		iffw_close(&w);                 /* FORM */
		iffw_end(&w);
		if (saveicons)
			savemidiin_icon(name);
	} E_EXCEPT_DO {
		iffw_end(&w);
		ReThrow();
	} E_END;
}

BOOL loadinstrumentsfrombank(CONST_STRPTR name, struct List *slist)
{
	volatile BOOL ok = TRUE;

	E_TRY {
		load_project(name, slist, 0, 0);
	} E_EXCEPT {
		ok = FALSE;
	} E_END;
	return ok;
}

struct mbprefs *load_project(CONST_STRPTR name, struct List *slist,
                             struct bank *bnk, struct mbprefs *prefs)
{
	struct iffr r;
	struct mbprefs *volatile ret = prefs;

	r.buf = 0;
	E_TRY {
		LONG instr[NUMBANKS], f, z, v;
		UBYTE *s;

		iffr_load(&r, name);
		for (f = 0; f < NUMBANKS; f++)
			instr[f] = 0;
		if (bnk) {
			clearsmplist(bnk, slist, FALSE);
			if (ret) {
				if ((s = iffr_first(&r, 'PREF'))) {
					if (!convertprefs(s, r.size, ret))
						ret = 0;
				} else {
					ret = 0;
				}
			}
			if (!(s = iffr_first(&r, 'BANK')))
				Raise('bprj');
			if ((v = (LONG)getl(s)) > (LONG)sizeof(struct bank) || v <= 0)
				Raise('bprj');
			if ((z = (r.size - 4) / v) >= 1) {
				if (z > NUMBANKS)
					z = NUMBANKS;
				for (f = 0; f < z; f++) {
					struct bank *nb = (struct bank *)(s + 4 + f * v);

					instr[f] = (LONG)nb->instr;
					nb->instr = 0;
					deletebank(&bnk[f]);
					CopyMem(nb, &bnk[f], (ULONG)v);
					if (!checkbank(&bnk[f]))
						Raise('bprj');
				}
			}
			if ((s = iffr_first(&r, 'UNDO')) && r.size > 8) {
				UBYTE *end = s + r.size, *p;
				LONG version = (WORD)getw(s), typemax = (WORD)getw(s + 2);
				LONG bsize = (WORD)getw(s + 4), bcount = (WORD)getw(s + 6);

				if (version == VERSION_0_UNDO && typemax <= UNDO_SET_MAX
				    && bsize <= (LONG)sizeof(struct bank) && bcount <= NUMBANKS) {
					p = s + 8;
					while (p && p + 4 < end) {
						LONG x = (LONG)getl(p);

						p = importundo(p + 4, x, bcount, bsize);
					}
				}
			}
		} else {
			if (!iffr_first(&r, 'BANK'))
				Raise('bprj');
		}
		if ((s = iffr_first(&r, 'LSMP')))
			load_instruments(s, r.size, slist, bnk, instr);
		iffr_free(&r);
		if (bnk)
			getmidiin_icon(name);
	} E_EXCEPT_DO {
		iffr_free(&r);
		if (exception == 'bprj' && bnk)
			initbanks(bnk);
		ReThrow();
	} E_END;
	return ret;
}

void mergeproject(CONST_STRPTR name, struct List *slist, struct bank *bnk, LONG numbnk)
{
	struct iffr r;

	r.buf = 0;
	E_TRY {
		LONG instr[NUMBANKS], f, v, z, l, n;
		BOOL skip = FALSE;
		UBYTE *s;

		iffr_load(&r, name);
		for (f = 0; f < NUMBANKS; f++)
			instr[f] = 0;
		if (!(s = iffr_first(&r, 'BANK')))
			Raise('bprj');
		if ((v = (LONG)getl(s)) > (LONG)sizeof(struct bank) || v <= 0)
			Raise('bprj');
		if ((z = (r.size - 4) / v) >= 1) {
			n = 0;
			for (f = 0; f < z; f++) {
				struct bank *nb = (struct bank *)(s + 4 + f * v);

				if (nb->instr) {
					n++;
					if (!checkbank(nb))
						Raise('bprj');
				}
			}
			if (n > 0) {
				l = E_MIN(numbnk + n - 1, NUMBANKS - 1);
				for (f = numbnk; f <= l; f++)
					if (bnk[f].instr)
						break;
				l = (f != l + 1) ? reqskipover() : TRUE;
				if (l == 0)
					Raise('cncl');
				if (l == 2)
					skip = TRUE;
				f = numbnk;
				l = n;
				do {
					if (!bnk[f].instr || !skip)
						l--;
					f++;
				} while (l != 0 && f != NUMBANKS);
				if (l > 0 && reqdontfit(l) == 0)
					Raise('cncl');
				n = numbnk;
				f = 0;
				do {
					if (skip && bnk[n].instr) {
						n++;
					} else {
						struct bank *nb = (struct bank *)(s + 4 + f * v);

						if (nb->instr) {
							instr[n] = (LONG)nb->instr;
							nb->instr = 0;
							deletebank(&bnk[n]);
							CopyMem(nb, &bnk[n], (ULONG)v);
							n++;
						}
						f++;
					}
				} while (n != NUMBANKS && f != z);
			}
		}
		if ((s = iffr_first(&r, 'LSMP')))
			load_instruments(s, r.size, slist, bnk, instr);
	} E_EXCEPT_DO {
		iffr_free(&r);
		ReThrow();
	} E_END;
}

/* ------------------------------------------------------------- settings */

void savesettings(struct List *slist, struct mbprefs *prefs)
{
	struct iffw w;

	w.fh = 0;
	E_TRY {
		iffw_open(&w, (CONST_STRPTR)preferencesname);
		iffw_chunk(&w, 'FORM');
		iffw_chunk(&w, 'PREF');
		writeprefs(&w, prefs);
		iffw_close(&w);
		writesamplelist(&w, slist);
		iffw_close(&w);
		iffw_end(&w);
	} E_EXCEPT_DO {
		iffw_end(&w);
		report_exception();
	} E_END;
}

/* loads the prefs only once, at the start */
static struct mbprefs *load_project_bis(CONST_STRPTR name, struct List *slist,
                                        struct bank *bnk, struct mbprefs *prefs)
{
	struct mbprefs *volatile ret = 0;

	E_TRY {
		ret = load_project(name, slist, bnk, prefs);
	} E_EXCEPT {
		report_exception();
	} E_END;
	return ret;
}

LONG loadsettings(struct List *slist, struct mbprefs *prefs, struct bank *bnk,
                  CONST_STRPTR projectname)
{
	char name[300];
	struct iffr r;
	volatile LONG ret = 0;

	if (projectname && *projectname) {
		estrcpy((STRPTR)name, (CONST_STRPTR)prjpath, sizeof(name));
		AddPart((STRPTR)name, (STRPTR)projectname, sizeof(name));
		if (load_project_bis((CONST_STRPTR)name, slist, bnk, prefs))
			return 0;
	}
	defaultsettings(prefs);
	{
		BPTR lock = Lock((CONST_STRPTR)preferencesname, ACCESS_READ);

		if (!lock)
			return -1;
		UnLock(lock);
	}
	r.buf = 0;
	E_TRY {
		UBYTE *s;

		iffr_load(&r, (CONST_STRPTR)preferencesname);
		if ((s = iffr_first(&r, 'PREF'))) {
			if (r.size == 0 || s[r.size - 1] != 0)
				Raise('PREF');
			if (!convertprefs(s, r.size, prefs))
				Raise('PREF');
		}
		if ((s = iffr_first(&r, 'LSMP')))
			load_instruments(s, r.size, slist, 0, 0);
	} E_EXCEPT_DO {
		iffr_free(&r);
		if (exception) {
			defaultsettings(prefs);
			report_exception();
		}
	} E_END;
	return ret;
}
