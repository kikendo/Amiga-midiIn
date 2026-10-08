/*
 * banks.c - port of mbbanks.e
 */
#include <string.h>
#include <exec/types.h>
#include <exec/semaphores.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/utility.h>

#include "eport.h"
#include "locale.h"
#include "report.h"
#include "banks.h"

typedef char bank_size_check[sizeof(struct bank) == 32 ? 1 : -1];

struct bank *prilist[NUMBANKS];

static BOOL lastexcp;
static struct SignalSemaphore banksem;
static BOOL banksem_ready;

void lockbanksaccess(void)
{
	ObtainSemaphore(&banksem);
}

void releasebanksaccess(void)
{
	if (banksem.ss_NestCount)
		ReleaseSemaphore(&banksem);
}

LONG cmpbanks(struct bank *bn1, struct bank *bn2, LONG type)
{
	if (bn1->instr == 0 && bn2->instr != 0)
		return -1;
	if (bn2->instr == 0 && bn1->instr != 0)
		return 1;
	switch (type) {
	case SORT_PRI:
		if (bn2->pri > bn1->pri) return 1;
		if (bn2->pri < bn1->pri) return -1;
		break;
	case SORT_MIDI:
		if (bn2->midi > bn1->midi) return 1;
		if (bn2->midi < bn1->midi) return -1;
		break;
	case SORT_NAME:
		if (bn1->instr && bn2->instr) {
			/* E's OstrCmp: 1 if the first sorts before the second */
			int c = strcmp(bn1->instr->ln.ln_Name, bn2->instr->ln.ln_Name);

			return c < 0 ? 1 : c > 0 ? -1 : 0;
		}
		break;
	case SORT_RANGE:
		if (bn2->lobound > bn1->lobound) return 1;
		if (bn2->lobound < bn1->lobound) return -1;
		if (bn2->hibound > bn1->hibound) return 1;
		if (bn2->hibound < bn1->hibound) return -1;
		break;
	}
	return 0;
}

struct sfx *clearinstr(struct bank *bn)
{
	struct sfx *snd;

	if ((snd = bn->instr)) {
		bn->instr = 0;
		sfx_unload(snd);
	}
	return snd;
}

BOOL setinstr(struct bank *bn, struct sfx *snd)
{
	volatile BOOL ok = TRUE;

	if (snd == bn->instr)
		return TRUE;
	clearinstr(bn);
	if (!snd)
		return TRUE;
	E_TRY {
		printstatus(LOC(STRID_LOADINGSAMPLE), (STRPTR)snd->ln.ln_Name, 0, 0, 0);
		sfx_load(snd);
		bn->instr = snd;
	} E_EXCEPT {
		report_exception();
		ok = FALSE;
	} E_END;
	return ok;
}

BOOL setinstrname(struct bank *bn, struct List *slist, CONST_STRPTR instrname)
{
	struct sfx *snd;
	BOOL x = FALSE;

	if (!instrname || !*instrname || !*FilePart((STRPTR)instrname)) {
		clearinstr(bn);
		x = TRUE;
	} else if ((snd = addsnd(instrname, slist, FALSE, 0))) {
		x = setinstr(bn, snd);
	}
	return x;
}

void xchgbanks(struct bank *bn1, struct bank *bn2)
{
	struct bank bn;

	CopyMem(bn1, &bn, sizeof(struct bank));
	CopyMem(bn2, bn1, sizeof(struct bank));
	CopyMem(&bn, bn2, sizeof(struct bank));
}

BOOL setbank(struct bank *bn, struct bank *fbn)
{
	BOOL x = setinstr(bn, fbn->instr);

	fbn->instr = bn->instr;
	CopyMem(fbn, bn, sizeof(struct bank));
	return x;
}

static void defaults(struct bank *bn)
{
	bn->midi = 1;
	bn->pri = 1;
	bn->type = TYPE_VERSION_32;
	bn->base = 60;
	bn->fine = FINE_CENTR;
	bn->set = 0;
	bn->lobound = 0;
	bn->hibound = 127;
	bn->volume = 256;
	bn->velsens = 0;
	bn->release = 0;
	bn->panorama = 128;
	bn->panwide = 0;
	bn->pitchsens = 0;
	bn->attack = 0;
	bn->decay = 0;
	bn->sustainlev = 255;
	bn->aftersens = 0;
	bn->firstskip = 0;
	bn->mctrlvol = 0;
	bn->mctrlpan = 0;
	bn->group = 0;
	bn->monovsens = 0;
	bn->monoslide = 0;
}

void deletebank(struct bank *bn)
{
	clearinstr(bn);
	defaults(bn);
}

void initbanks(struct bank *bd)
{
	LONG f;

	for (f = 0; f < NUMBANKS; f++) {
		bd[f].instr = 0;
		defaults(&bd[f]);
		prilist[f] = &bd[f];
	}
	if (!banksem_ready) {
		InitSemaphore(&banksem);
		banksem_ready = TRUE;
	}
}

void sortbanks(void)
{
	LONG i, f;

	for (i = 1; i < NUMBANKS; i++) {
		struct bank *cb = prilist[i], *bn;
		UBYTE bankpri = cb->pri;

		f = i;
		bn = prilist[f - 1];
		while (bn->pri > bankpri) {
			prilist[f] = bn;
			f--;
			prilist[f] = cb;
			if (f == 0)
				break;
			bn = prilist[f - 1];
		}
	}
}

BOOL checkbank(struct bank *bn)
{
	LONG l, h;

	if ((UBYTE)(bn->midi - 1) >= 16)
		return FALSE;
	if ((UBYTE)(bn->pri - 1) >= NUMBANKS)
		return FALSE;
	if ((bn->type & TYPE_VERSION_32) == 0) {
		if (bn->type == LEFTFIX)
			bn->panorama = 0;
		else if (bn->type == RIGHTFIX)
			bn->panorama = 256;
		else if (bn->type == ANYFIX)
			bn->panorama = 128;
		else
			return FALSE;
		bn->type |= TYPE_VERSION_32;
	}
	if (bn->base > 127)
		return FALSE;
	if (bn->fine < FINE_CENTR - 100)        /* old finetune */
		bn->fine = (UBYTE)(bn->fine * 200 / 16 + FINE_CENTR - 100);
	if (bn->fine > FINE_CENTR + 100)
		return FALSE;
	if ((l = bn->lobound) > 127)
		return FALSE;
	if ((h = bn->hibound) > 127)
		return FALSE;
	if (l > h)
		return FALSE;
	if (bn->volume > 512 || bn->volume < 0)
		return FALSE;
	if (bn->velsens > 100)
		return FALSE;
	if (bn->panorama > 256 || bn->panorama < 0)
		return FALSE;
	if (bn->panwide > 30)
		return FALSE;
	if (bn->pitchsens > 12)
		return FALSE;
	if (bn->aftersens > 100)
		return FALSE;
	if (bn->firstskip > 3000 || bn->firstskip < 0)
		return FALSE;
	if (bn->mctrlvol > 100)
		return FALSE;
	if (bn->mctrlpan > 100)
		return FALSE;
	if (bn->group > 16)
		return FALSE;
	if (bn->monovsens > 100)
		bn->monovsens = 0;
	if (bn->monoslide > 1500 || bn->monoslide < 0)
		bn->monoslide = 0;
	return TRUE;
}

/* ----------------------------------------------------------- sample list */

struct sfx *addsnd(CONST_STRPTR name, struct List *slist, BOOL quiet, BOOL *added)
{
	struct sfx *volatile snd = 0;
	struct lln *ln;

	if (added)
		*added = FALSE;
	if ((ln = (struct lln *)FindName(slist, FilePart((STRPTR)name))))
		return (struct sfx *)ln->pointer;
	E_TRY {
		STRPTR descr = 0;

		snd = sfx_new();
		sfx_init(snd, name, &descr);
		printstatus(0, (STRPTR)snd->ln.ln_Name, descr, 0, 0);
		addsorted(slist, &snd->ln);
	} E_EXCEPT {
		if (snd) {
			sfx_dispose(snd);
			snd = 0;
		}
		printstatus(0, FilePart((STRPTR)name), (STRPTR)"????", 0, 0);
		if (exception != 'UNRE' || !lastexcp) {
			lastexcp = exception == 'UNRE';
			if (!quiet)
				report_exception();
		} else {
			Delay(1);
		}
	} E_END;
	if (added)
		*added = snd ? TRUE : FALSE;
	return snd;
}

void addingsamplesover(void)
{
	closestatus();
	lastexcp = FALSE;
}

BOOL delsnd(struct sfx *snd, struct bank *banks, BOOL notused)
{
	LONG f = 0;

	while (f < NUMBANKS) {
		if (banks[f].instr == snd) {
			if (!notused)
				banks[f].instr = 0;
			else
				f = NUMBANKS + 1;
		}
		f++;
	}
	if (f == NUMBANKS) {
		Remove(&snd->ln);
		sfx_dispose(snd);
		return TRUE;
	}
	return FALSE;
}

BOOL clearsmplist(struct bank *bn, struct List *slist, BOOL notused)
{
	struct lln *ln = (struct lln *)slist->lh_Head, *ln2;
	BOOL cleared = FALSE;

	while ((ln2 = (struct lln *)ln->ln.ln_Succ)) {
		if (delsnd((struct sfx *)ln->pointer, bn, notused))
			cleared = TRUE;
		ln = ln2;
	}
	return cleared;
}
