/*
 * undo.c - port of mbundo.e
 *
 * A record is a node header (struct undohd, 14 bytes) followed by its data;
 * records are saved in project files byte for byte (exportundo/importundo).
 * Data bytes are written and read one at a time, so 16-bit values at odd
 * offsets also work on a 68000 (the E code wrote words there directly).
 *
 * Changes from the E code: records come from AllocVec instead of an exec
 * memory pool (pools need OS 3.0), with the same size budget; importundo
 * copies plain records from the file data and sets up imported bank lists
 * bank by bank (the E code copied them from the wrong place); the redo
 * record of a whole-project change keeps its start bank and count in the
 * same order as the undo record.
 */
#include <string.h>
#include <exec/types.h>
#include <exec/memory.h>
#include <exec/lists.h>
#include <proto/exec.h>
#include <proto/dos.h>

#include "eport.h"
#include "locale.h"
#include "report.h"
#include "play.h"
#include "undo.h"

#define UNDO_REDO_FLAG 0x4000
#define UNDO_SET_MASK  0x3FFF

struct undohd {
	struct MinNode mln;
	LONG size;              /* including this header */
	WORD type;
};

#define HDSIZE   14             /* sizeof(struct undohd) on the Amiga */
#define MLNSIZE  8
#define BANKSIZE ((LONG)sizeof(struct bank))

typedef char undohd_size_check[sizeof(struct undohd) == HDSIZE ? 1 : -1];

#define DATA(u) ((UBYTE *)(u) + HDSIZE)

static BOOL undo_ready;
static struct MinList undolist;
#define undolh ((struct undohd *)&undolist)
static struct undohd *undocurr;         /* == undolh: nothing to undo */
static struct undohd *undonew;
static struct undohd *undosave;
static LONG undosize;                   /* budget left */

static void getw(const UBYTE *p, LONG *v)
{
	*v = (WORD)((p[0] << 8) | p[1]);
}

static void putw(UBYTE *p, LONG v)
{
	p[0] = (UBYTE)(v >> 8);
	p[1] = (UBYTE)v;
}

static ULONG getl(const UBYTE *p)
{
	return ((ULONG)p[0] << 24) | ((ULONG)p[1] << 16) | ((ULONG)p[2] << 8) | p[3];
}

#define BANKAT(p) ((struct bank *)(p))
#define INSTRNUM(b) ((ULONG)(b).instr)
#define SETNUM(b, n) ((b).instr = (struct sfx *)(ULONG)(n))

/* ------------------------------------------------------------- nodes */

static void freeundonode(struct undohd *u)
{
	if (!u)
		return;
	undosize += (u->size + 7) & ~7L;
	FreeVec(u);
	if (u == undosave)
		undosave = 0;
}

/* returns a pointer to the data of a new record (kept in undonew) */
static UBYTE *allocundonode(LONG size, LONG type, BOOL leavecurr)
{
	struct undohd *u;

	size += HDSIZE;
	if (!undo_ready)
		Raise('MEM');
	if ((u = undonew)) {
		undonew = 0;
		freeundonode(u);
	}
	while (undosize <= size || !undonew) {
		if (undosize > size)
			undonew = (struct undohd *)AllocVec((ULONG)size, MEMF_PUBLIC);
		if (undonew)
			break;
		if (undocurr == (struct undohd *)undolist.mlh_Head || undocurr == undolh) {
			if (!(u = (struct undohd *)RemTail((struct List *)&undolist)))
				Raise('UNDO');
			freeundonode(u);
			if (u == undocurr) {
				undocurr = undolh;
				if (leavecurr)
					Raise('UNDO');
			}
		} else {
			if (!(u = (struct undohd *)RemHead((struct List *)&undolist)))
				Raise('UNDO');
			freeundonode(u);
		}
	}
	undonew->size = size;
	undonew->type = (WORD)type;
	undonew->mln.mln_Succ = 0;
	undonew->mln.mln_Pred = 0;
	undosize -= (size + 7) & ~7L;
	return DATA(undonew);
}

static void undoaddnew(void)
{
	struct undohd *u;

	if (!undonew)
		return;
	while (undocurr != (struct undohd *)undolist.mlh_TailPred)
		if ((u = (struct undohd *)RemTail((struct List *)&undolist)))
			freeundonode(u);
	AddTail((struct List *)&undolist, (struct Node *)undonew);
	undocurr = undonew;
	undonew = 0;
}

/* the E checksum, including its 16-bit loop counter */
static LONG get_chksum(const UBYTE *start, LONG size)
{
	ULONG n = ((ULONG)(size - 1) & 0xFFFF) + 1;
	ULONG csum = 0, d0 = 1;

	while (n--) {
		BOOL carry = (d0 & 0x80000000UL) != 0;

		d0 = (d0 << 8) | *start++;
		if (carry) {
			csum += d0;
			d0 = 1;
		}
	}
	if (d0 != 1 || size == 0)
		csum += d0;
	return (LONG)csum;
}

/* ---------------------------------------------------- pool equivalent */

void init_undo(LONG max)
{
	if (undo_ready)
		return;
	undosize = max - 16;
	newlist((struct List *)&undolist);
	undocurr = undolh;
	undonew = 0;
	undo_ready = TRUE;
	remembersaveundo();
}

void flush_undo(void)
{
	struct undohd *u;

	if (!undo_ready)
		return;
	while ((u = (struct undohd *)RemHead((struct List *)&undolist)))
		freeundonode(u);
	undocurr = undolh;
	remembersaveundo();
}

void free_undo(void)
{
	if (!undo_ready)
		return;
	flush_undo();
	if (undonew) {
		FreeVec(undonew);
		undonew = 0;
	}
	undo_ready = FALSE;
}

void remembersaveundo(void)
{
	undosave = undocurr;
}

BOOL asksaveundo(void)
{
	return undocurr == undosave;
}

LONG undoleft(void)
{
	return undosize;
}

LONG nextundo(void)
{
	return undocurr != undolh ? undocurr->type : 0;
}

/* -------------------------------------------------------- un/redo */

static struct undohd *undoreplace(struct undohd *uhd, struct bank *banks,
                                  struct List *slist, LONG *banknum);

LONG do_undo(struct bank *banks, struct List *slist)
{
	struct undohd *u;
	LONG nbank;

	u = undocurr != undolh ? undocurr : 0;
	if (!u) {
		reqnotundo();
		Raise(0);
	}
	if ((u = undoreplace(u, banks, slist, &nbank))) {
		Insert((struct List *)&undolist, (struct Node *)u, (struct Node *)undocurr);
		Remove((struct Node *)undocurr);
		freeundonode(undocurr);
		undocurr = u;
	}
	undocurr = (struct undohd *)undocurr->mln.mln_Pred;
	return nbank;
}

LONG do_redo(struct bank *banks, struct List *slist)
{
	struct undohd *u;
	LONG nbank;

	u = (struct undohd *)undocurr->mln.mln_Succ;
	if (u->mln.mln_Succ)
		undocurr = u;
	else
		u = 0;
	if (!u) {
		reqnotredo();
		Raise(0);
	}
	if ((u = undoreplace(u, banks, slist, &nbank))) {
		Insert((struct List *)&undolist, (struct Node *)u, (struct Node *)undocurr);
		Remove((struct Node *)undocurr);
		freeundonode(undocurr);
		undocurr = u;
	}
	return nbank;
}

/* ---------------------------------------------------- project files */

APTR exportundo(APTR prev, LONG *size, LONG *cks)
{
	struct undohd *node = 0;
	BOOL redo = FALSE;

	*size = 0;
	*cks = 0;
	if (prev) {
		node = (struct undohd *)((UBYTE *)prev - MLNSIZE);
		redo = (node->type & UNDO_REDO_FLAG) != 0;
		node->type &= UNDO_SET_MASK;
	}
	if (!redo) {
		node = !node ? undocurr : (struct undohd *)node->mln.mln_Pred;
		if (node == undolh) {
			node = 0;
			redo = TRUE;
		}
	}
	if (redo) {
		node = !node ? (struct undohd *)undocurr->mln.mln_Succ
		             : (struct undohd *)node->mln.mln_Succ;
		if (!node->mln.mln_Succ)
			node = 0;
	}
	if (node) {
		UBYTE *p;

		if (redo)
			node->type |= UNDO_REDO_FLAG;
		*size = node->size - MLNSIZE;
		p = (UBYTE *)node + MLNSIZE;
		*cks = get_chksum(p, *size);
		return p;
	}
	return 0;
}

/* ptr points to a saved record (its size and type, then data); returns the
 * address after it, NULL if it does not check out */
UBYTE *importundo(UBYTE *ptr, LONG cksum, LONG numb, LONG bsize)
{
	LONG nsize, ntype, type, l, k, d, i;
	UBYTE *m, *endp = 0;
	struct undohd *volatile prevcurr;

	nsize = (LONG)getl(ptr);
	ntype = (WORD)((ptr[4] << 8) | ptr[5]);
	if (get_chksum(ptr, nsize - MLNSIZE) != cksum)
		return 0;
	ptr = ptr - MLNSIZE + HDSIZE;
	endp = ptr + nsize - HDSIZE;
	type = ntype & UNDO_SET_MASK;

	E_TRY {
		if (type == UNDO_SET_ALL || type == UNDO_SET_BANKS) {
			struct bank *bn;

			d = ptr[1];
			l = d * BANKSIZE + 2;
			k = nsize - HDSIZE - (d * bsize + 2);
			m = allocundonode(l + k, type, FALSE);
			bn = BANKAT(m + 2);
			m[0] = *ptr++;
			m[1] = *ptr++;
			for (i = 0; i < d; i++) {
				bn[i].instr = 0;
				deletebank(&bn[i]);
				CopyMem(ptr, &bn[i], (ULONG)bsize);
				ptr += bsize;
			}
			if (k > 0)
				CopyMem(ptr, m + l, (ULONG)k);
		} else if (type == UNDO_SET_SAMPLELISTINSTR) {
			d = numb * 4;
			l = NUMBANKS * 4;
			k = nsize - HDSIZE - d;
			m = allocundonode(l + k, type, FALSE);
			for (i = 0; i < d; i++)
				m[i] = ptr[i];
			for (i = d; i < l; i++)
				m[i] = 0;
			if (k > 0)
				CopyMem(ptr + d, m + l, (ULONG)k);
		} else {
			m = allocundonode(nsize - HDSIZE, type, FALSE);
			CopyMem(ptr, m, (ULONG)(nsize - HDSIZE));
		}
		prevcurr = undocurr;
		undoaddnew();
		if (ntype & UNDO_REDO_FLAG)
			undocurr = prevcurr;
		remembersaveundo();
	} E_EXCEPT {
		report_exception();
		endp = 0;
	} E_END;
	return endp;
}

/* ---------------------------------------------------------- recording */

/* size of the sample name list of slist */
static LONG namelistsize(struct List *slist)
{
	struct lln *ln;
	LONG l = 0;

	for (ln = (struct lln *)slist->lh_Head; ln->ln.ln_Succ; ln = (struct lln *)ln->ln.ln_Succ)
		l += (LONG)strlen((char *)sfx_pathname((struct sfx *)ln->pointer)) + 1;
	return l;
}

static UBYTE *copynames(UBYTE *m, struct List *slist)
{
	struct lln *ln;

	for (ln = (struct lln *)slist->lh_Head; ln->ln.ln_Succ; ln = (struct lln *)ln->ln.ln_Succ) {
		STRPTR p = sfx_pathname((struct sfx *)ln->pointer);
		LONG i = (LONG)strlen((char *)p) + 1;

		estrcpy(m, p, (ULONG)i);
		m += i;
	}
	return m;
}

/* the SET_BANKS record of d banks starting at bn: bank data with sample
 * numbers, then the sample paths; shared by set_undo and undoreplace */
static LONG banks_record_size(struct bank *bn, LONG d)
{
	LONG l = d * BANKSIZE + 2, i, k;

	for (i = 0; i < d; i++) {
		struct sfx *snd = bn[i].instr;

		if (snd) {
			for (k = 0; k < i; k++)
				if (bn[k].instr == snd)
					snd = 0;
			if (snd)
				l += (LONG)strlen((char *)sfx_pathname(snd)) + 1;
		}
	}
	return l;
}

static void banks_record_fill(UBYTE *m, struct bank *bn, LONG d)
{
	struct bank *bns = BANKAT(m);
	LONG i, k, x;

	CopyMem(bn, bns, (ULONG)(d * BANKSIZE));
	m = (UBYTE *)&bns[d];
	for (i = 0; i < d; i++) {
		struct sfx *snd = bns[i].instr;

		if (snd)
			for (k = i + 1; k < d; k++)
				if (bns[k].instr == snd)
					bns[k].instr = 0;
	}
	x = 1;
	for (i = 0; i < d; i++) {
		struct sfx *snd;

		if ((snd = bns[i].instr)) {
			LONG l = (LONG)strlen((char *)sfx_pathname(snd)) + 1;

			estrcpy(m, sfx_pathname(snd), (ULONG)l);
			SETNUM(bns[i], x);
			x++;
			m += l;
		} else if ((snd = bn[i].instr)) {
			k = 0;
			while (k < i)
				if (bn[k++].instr == snd)
					break;
			if (bn[k - 1].instr == snd)
				bns[i].instr = bns[k - 1].instr;
		}
	}
}

void set_undo(LONG type, APTR obj, LONG data, LONG dat2, LONG dat3)
{
	E_TRY {
		struct bank *bn, *bns;
		struct sfx *snd;
		struct List *lh;
		UBYTE *m;
		LONG l, i, k, v = 0;
		BOOL record = TRUE;

		switch (type) {
		case UNDO_SET_SAMPLEDELETE:     /* obj: sfx, data: all banks */
			snd = (struct sfx *)obj;
			bn = (struct bank *)data;
			l = (LONG)strlen((char *)sfx_pathname(snd)) + 1;
			for (i = 0; i < NUMBANKS; i++)
				if (bn[i].instr == snd)
					l++;
			m = allocundonode(l, type, FALSE);
			estrcpy(m, sfx_pathname(snd), (ULONG)l);
			m += strlen((char *)m) + 1;
			for (i = 0; i < NUMBANKS; i++)
				if (bn[i].instr == snd)
					*m++ = (UBYTE)i;
			break;
		case UNDO_SET_INSTR:            /* obj: all banks, data: bank */
			bn = &((struct bank *)obj)[data];
			l = 257;
			m = allocundonode(l, type, FALSE);
			*m++ = (UBYTE)data;
			if ((snd = bn->instr))
				estrcpy(m, sfx_pathname(snd), (ULONG)(l - 1));
			else
				*m = 0;
			break;
		case UNDO_SET_XCHGBANKS:        /* obj: pairs, data: length */
			l = data + 1;
			m = allocundonode(l, type, FALSE);
			*m++ = 0;
			CopyMem(obj, m, (ULONG)data);
			break;
		case UNDO_SET_BANKS:            /* obj: banks, data: count, dat2: first */
			bn = &((struct bank *)obj)[dat2];
			l = banks_record_size(bn, data);
			m = allocundonode(l, type, FALSE);
			*m++ = (UBYTE)dat2;
			*m++ = (UBYTE)data;
			banks_record_fill(m, bn, data);
			break;
		default:
			if (type == UNDO_PREP_SAMPLELIST || type == UNDO_PREP_ALL
			    || type == UNDO_PREP_SAMPLELISTINSTR) {
				lh = (struct List *)obj;
				k = 0;
				if (dat2 == 0)
					dat2 = NUMBANKS;
				if (type == UNDO_PREP_ALL)
					k = dat2 * BANKSIZE + 2;
				else if (type == UNDO_PREP_SAMPLELISTINSTR)
					k = NUMBANKS * 4;
				l = k + namelistsize(lh);
				m = allocundonode(l, type, FALSE);
				if (type == UNDO_PREP_ALL) {     /* dat2: count, dat3: first */
					bns = (struct bank *)data;
					bn = BANKAT(m + 2);
					CopyMem(&bns[dat3], bn, (ULONG)(dat2 * BANKSIZE));
					for (i = 0; i < dat2; i++)
						if ((snd = bn[i].instr))
							SETNUM(bn[i], convlnptrtonum(snd, lh));
					m[0] = (UBYTE)dat3;
					m[1] = (UBYTE)dat2;
				} else if (type == UNDO_PREP_SAMPLELISTINSTR) {
					ULONG *instr = (ULONG *)m;

					bn = (struct bank *)data;
					for (i = 0; i < NUMBANKS; i++)
						instr[i] = (snd = bn[i].instr)
						           ? (ULONG)convlnptrtonum(snd, lh) : 0;
				}
				copynames(m + k, lh);
				record = FALSE;         /* completed by the SET_ call */
			} else if (type == UNDO_SET_SAMPLELISTINSTR) {
				if (undonew) {
					if (undonew->type != UNDO_PREP_SAMPLELISTINSTR)
						record = FALSE;
					else
						undonew->type = (WORD)type;
				}
			} else if (type == UNDO_SET_SAMPLELIST) {
				if (undonew) {
					if (undonew->type != UNDO_PREP_SAMPLELIST)
						record = FALSE;
					else
						undonew->type = (WORD)type;
				}
			} else if (type == UNDO_SET_ALL) {
				if (undonew) {
					if (undonew->type != UNDO_PREP_ALL)
						record = FALSE;
					else
						undonew->type = (WORD)type;
				}
			} else {                        /* obj: all banks, data: bank */
				if (undocurr != undolh && undocurr->type == type
				    && DATA(undocurr)[0] == (UBYTE)data) {
					record = FALSE;     /* same field of the same bank */
					break;
				}
				bn = &((struct bank *)obj)[data];
				switch (type) {
				case UNDO_SET_MIDI:        l = 2; v = bn->midi; break;
				case UNDO_SET_PRI:         l = 2; v = bn->pri; break;
				case UNDO_SET_BASE:        l = 2; v = bn->base; break;
				case UNDO_SET_FINE:        l = 2; v = bn->fine; break;
				case UNDO_SET_SET_LOOP:
				case UNDO_SET_SET_DUR:
				case UNDO_SET_SET_MONO:
				case UNDO_SET_SET_ADDAFTERT: l = 2; v = bn->set; break;
				case UNDO_SET_BOUNDS:      l = 3; v = (bn->lobound << 8) + bn->hibound; break;
				case UNDO_SET_VOLUME:      l = 3; v = bn->volume; break;
				case UNDO_SET_VELSENS:     l = 2; v = bn->velsens; break;
				case UNDO_SET_RELEASE:     l = 2; v = bn->release; break;
				case UNDO_SET_PANORAMA:    l = 3; v = bn->panorama; break;
				case UNDO_SET_PANWIDE:     l = 2; v = bn->panwide; break;
				case UNDO_SET_PITCHSENS:   l = 2; v = bn->pitchsens; break;
				case UNDO_SET_ATTACK:      l = 2; v = bn->attack; break;
				case UNDO_SET_DECAY:       l = 2; v = bn->decay; break;
				case UNDO_SET_SUSTAINLEV:  l = 2; v = bn->sustainlev; break;
				case UNDO_SET_AFTERSENS:   l = 2; v = bn->aftersens; break;
				case UNDO_SET_FIRSTSKIP:   l = 3; v = bn->firstskip; break;
				case UNDO_SET_MCTRLVOL:    l = 2; v = bn->mctrlvol; break;
				case UNDO_SET_MCTRLPAN:    l = 2; v = bn->mctrlpan; break;
				case UNDO_SET_GROUP:       l = 2; v = bn->group; break;
				case UNDO_SET_MONOVSENS:   l = 2; v = bn->monovsens; break;
				case UNDO_SET_MONOSLIDE:   l = 3; v = bn->monoslide; break;
				default:
					l = 0;
					record = FALSE;
				}
				if (record) {
					m = allocundonode(l, type, FALSE);
					*m++ = (UBYTE)data;
					if (l < 3)
						*m = (UBYTE)v;
					else
						putw(m, v);
				}
			}
		}
		if (record)
			undoaddnew();
	} E_EXCEPT {
		report_exception();
	} E_END;
}

/* ---------------------------------------------------------- replaying */

static struct undohd *undoreplace(struct undohd *uhd, struct bank *banks,
                                  struct List *slist, LONG *banknum)
{
	LONG type, l, i, k, x, d = 0, prgrs, total;
	UBYTE *m, *end;
	struct sfx *snd;
	struct bank *bn, *bns;
	struct lln *ln;

	*banknum = -1;
	type = uhd->type & UNDO_SET_MASK;
	l = uhd->size - HDSIZE;
	m = DATA(uhd);

	if (type == UNDO_SET_SAMPLEDELETE) {    /* sample name + its banks */
		uhd->type = UNDO_SET_SAMPLEUNDELETE;
		if (!(snd = addsnd(m, slist, FALSE, 0))) {
			undosave = 0;
		} else {
			end = (UBYTE *)uhd + uhd->size;
			m += strlen((char *)m) + 1;
			while (m < end)
				if ((i = *m++) < NUMBANKS)
					if (!setinstr(&banks[i], snd))
						undosave = 0;
		}
		addingsamplesover();
		return 0;
	}
	if (type == UNDO_SET_SAMPLEUNDELETE) {
		ln = (struct lln *)FindName(slist, FilePart(m));
		snd = ln ? (struct sfx *)ln->pointer : 0;
		if (snd)
			delsnd(snd, banks, FALSE);
		uhd->type = UNDO_SET_SAMPLEDELETE;
		return 0;
	}
	if (type == UNDO_SET_SAMPLELIST || type == UNDO_SET_ALL
	    || type == UNDO_SET_SAMPLELISTINSTR) {
		ULONG *instr;
		UBYTE *nm;

		/* first the opposite record, from the current state */
		k = 0;
		if (type == UNDO_SET_ALL) {
			d = m[1];
			x = m[0];
			k = d * BANKSIZE + 2;
		} else {
			x = 0;
			if (type == UNDO_SET_SAMPLELISTINSTR)
				k = NUMBANKS * 4;
		}
		l = k + namelistsize(slist);
		nm = allocundonode(l, type, TRUE);
		if (type == UNDO_SET_ALL) {
			bn = BANKAT(nm + 2);
			CopyMem(&banks[x], bn, (ULONG)(d * BANKSIZE));
			for (i = 0; i < d; i++)
				if ((snd = bn[i].instr))
					SETNUM(bn[i], convlnptrtonum(snd, slist));
			nm[0] = (UBYTE)x;
			nm[1] = (UBYTE)d;
		} else if (type == UNDO_SET_SAMPLELISTINSTR) {
			instr = (ULONG *)nm;
			for (i = 0; i < NUMBANKS; i++)
				instr[i] = (snd = banks[i].instr) ? (ULONG)convlnptrtonum(snd, slist) : 0;
		}
		copynames(nm + k, slist);

		/* then the real un/redo */
		end = (UBYTE *)uhd + uhd->size;
		instr = (ULONG *)DATA(uhd);
		bn = BANKAT(DATA(uhd) + 2);
		ln = (struct lln *)slist->lh_Head;
		while (ln->ln.ln_Succ) {
			m = DATA(uhd) + k;
			while (m < end) {
				if (strcmp((char *)FilePart(m), ln->ln.ln_Name) == 0)
					break;
				m += strlen((char *)m) + 1;
			}
			snd = (struct sfx *)ln->pointer;
			ln = (struct lln *)ln->ln.ln_Succ;
			if (m >= end)
				delsnd(snd, banks, FALSE);
		}
		m = DATA(uhd) + k;
		total = 0;
		for (nm = m; nm < end; nm += strlen((char *)nm) + 1)
			total++;
		prgrs = 0;
		k = 1;
		while (m < end) {
			printstatus(LOC(STRID_LOOKINGFORSAMPLE), FilePart(m), 0, prgrs++, total);
			if (!(snd = addsnd(m, slist, FALSE, 0)))
				undosave = 0;
			if (type == UNDO_SET_ALL)
				for (i = 0; i < d; i++)
					if (INSTRNUM(bn[i]) == (ULONG)k)
						bn[i].instr = snd;
			if (type == UNDO_SET_SAMPLELISTINSTR)
				for (i = 0; i < NUMBANKS; i++)
					if (instr[i] == (ULONG)k)
						instr[i] = (ULONG)snd;
			m += strlen((char *)m) + 1;
			k++;
		}
		total = 0;
		prgrs = 0;
		if (type == UNDO_SET_ALL) {
			for (i = 0; i < d; i++)
				if (bn[i].instr)
					total++;
			for (i = 0; i < d; i++) {
				if (bn[i].instr)
					printstatus(0, 0, 0, prgrs++, total);
				if (!setbank(&banks[i + x], &bn[i]))
					undosave = 0;
			}
		} else if (type == UNDO_SET_SAMPLELISTINSTR) {
			for (i = 0; i < NUMBANKS; i++)
				if (instr[i])
					total++;
			for (i = 0; i < NUMBANKS; i++) {
				if (instr[i])
					printstatus(0, 0, 0, prgrs++, total);
				if (!setinstr(&banks[i], (struct sfx *)instr[i]))
					undosave = 0;
			}
		}
		addingsamplesover();
		uhd = undonew;
		undonew = 0;
		return uhd;
	}
	if (type == UNDO_SET_BANKS) {           /* first bank, count, banks, names */
		UBYTE *nm;

		x = m[0];
		bn = &banks[x];
		d = m[1];
		nm = allocundonode(banks_record_size(bn, d), type, TRUE);
		nm[0] = (UBYTE)x;
		nm[1] = (UBYTE)d;
		banks_record_fill(nm + 2, bn, d);

		/* the real set */
		bns = BANKAT(DATA(uhd) + 2);
		end = (UBYTE *)uhd + uhd->size;
		total = 0;
		prgrs = 0;
		for (i = 0; i < d; i++)
			if (bns[i].instr)
				total++;
		for (i = 0; i < d; i++) {
			if ((x = (LONG)INSTRNUM(bns[i])) != 0) {
				printstatus(0, 0, 0, prgrs++, total);
				m = (UBYTE *)&bns[d];
				while (m < end && --x > 0)
					m += strlen((char *)m) + 1;
				if (!setinstrname(&bn[i], slist, m))
					undosave = 0;
				bns[i].instr = bn[i].instr;
			}
			setbank(&bn[i], &bns[i]);
		}
		addingsamplesover();
		uhd = undonew;
		undonew = 0;
		return uhd;
	}
	if (type == UNDO_SET_XCHGBANKS) {       /* direction flag, bank pairs */
		LONG dir;

		l = l - 1;
		if (m[0] != 0) {
			i = 0;
			l = l - 2;
			dir = 2;
		} else {
			i = l - 2;
			l = 0;
			dir = -2;
		}
		m[0] = (UBYTE)~m[0];
		m++;
		while ((dir > 0 && i <= l) || (dir < 0 && i >= l)) {
			k = m[i];
			x = m[i + 1];
			if (k < NUMBANKS && x < NUMBANKS)
				xchgbanksachn(&banks[k], &banks[x]);
			i += dir;
		}
		return 0;
	}

	/* a single field of one bank: swap the stored and current values */
	if ((i = *m++) >= NUMBANKS)
		return 0;
	*banknum = i;
	bn = &banks[i];
	switch (type) {
	case UNDO_SET_INSTR:
		snd = bn->instr;
		if (!setinstrname(bn, slist, m))
			undosave = 0;
		if (snd)
			estrcpy(m, sfx_pathname(snd), (ULONG)(l - 1));
		else
			*m = 0;
		addingsamplesover();
		break;
	case UNDO_SET_MIDI:      i = bn->midi; bn->midi = m[0]; break;
	case UNDO_SET_PRI:       i = bn->pri; bn->pri = m[0]; break;
	case UNDO_SET_BASE:      i = bn->base; bn->base = m[0]; break;
	case UNDO_SET_FINE:
		i = bn->fine; bn->fine = m[0];
		signal_playtask(PSG_TUNE, bn, 0);
		break;
	case UNDO_SET_SET_LOOP:
	case UNDO_SET_SET_DUR:
	case UNDO_SET_SET_MONO:
		i = bn->set; bn->set = m[0];
		break;
	case UNDO_SET_SET_ADDAFTERT:
		i = bn->set; bn->set = m[0];
		signal_playtask(PSG_VOLUME, bn, 0);
		break;
	case UNDO_SET_BOUNDS:
		i = (bn->lobound << 8) + bn->hibound;
		bn->lobound = m[0];
		bn->hibound = m[1];
		break;
	case UNDO_SET_VOLUME:
		i = bn->volume; getw(m, &x); bn->volume = (WORD)x;
		signal_playtask(PSG_VOLUME, bn, 0);
		break;
	case UNDO_SET_VELSENS:
		i = bn->velsens; bn->velsens = m[0];
		signal_playtask(PSG_VOLUME, bn, 0);
		break;
	case UNDO_SET_RELEASE:   i = bn->release; bn->release = m[0]; break;
	case UNDO_SET_PANORAMA:
		i = bn->panorama; getw(m, &x); bn->panorama = (WORD)x;
		signal_playtask(PSG_VOLUME, bn, 0);
		break;
	case UNDO_SET_PANWIDE:
		i = bn->panwide; bn->panwide = m[0];
		signal_playtask(PSG_VOLUME, bn, 0);
		break;
	case UNDO_SET_PITCHSENS: i = bn->pitchsens; bn->pitchsens = m[0]; break;
	case UNDO_SET_ATTACK:    i = bn->attack; bn->attack = m[0]; break;
	case UNDO_SET_DECAY:     i = bn->decay; bn->decay = m[0]; break;
	case UNDO_SET_SUSTAINLEV: i = bn->sustainlev; bn->sustainlev = m[0]; break;
	case UNDO_SET_AFTERSENS:
		i = bn->aftersens; bn->aftersens = m[0];
		signal_playtask(PSG_VOLUME, bn, 0);
		break;
	case UNDO_SET_FIRSTSKIP:
		i = bn->firstskip; getw(m, &x); bn->firstskip = (WORD)x;
		break;
	case UNDO_SET_MCTRLVOL:
		i = bn->mctrlvol; bn->mctrlvol = m[0];
		signal_playtask(PSG_VOLUME, bn, 0);
		break;
	case UNDO_SET_MCTRLPAN:
		i = bn->mctrlpan; bn->mctrlpan = m[0];
		signal_playtask(PSG_VOLUME, bn, 0);
		break;
	case UNDO_SET_GROUP:     i = bn->group; bn->group = m[0]; break;
	case UNDO_SET_MONOVSENS: i = bn->monovsens; bn->monovsens = m[0]; break;
	case UNDO_SET_MONOSLIDE:
		i = bn->monoslide; getw(m, &x); bn->monoslide = (WORD)x;
		break;
	default:
		return 0;
	}
	if (l == 2)
		m[0] = (UBYTE)i;
	if (l == 3)
		putw(m, i);
	return 0;
}
