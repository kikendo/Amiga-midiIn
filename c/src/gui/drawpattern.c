/*
 * drawpattern.c - port of drawpattern.e
 *
 * Differences from the E code: failures (no exact pen, no memory) return
 * NULL directly instead of going through Raise() and the procedure's own
 * EXCEPT DO handler, so the global exception variable is not left set.
 * FreeBitMap() is only called with a bitmap.
 */
#include <exec/types.h>
#include <exec/memory.h>
#include <graphics/gfx.h>
#include <graphics/rastport.h>
#include <graphics/view.h>
#include <utility/tagitem.h>
#include <proto/exec.h>
#include <proto/graphics.h>

#include "../app/eport.h"
#include "drawpattern.h"

LONG *drawpattern(struct RastPort *rp, struct ColorMap *cm, LONG x, LONG y,
                  LONG w, LONG h, const ULONG *ctab, const UBYTE *pattern,
                  LONG pw, LONG ph)
{
	struct RastPort tmprp;
	struct BitMap *bm = NULL;
	LONG *coltab;
	UBYTE *tmpar = NULL;
	const UBYTE *tmpptr;
	LONG i, acth, pch, pcw, cnum, mw, bw;

	cnum = (LONG)(*ctab++ >> 16);
	coltab = AllocVec((cnum + 2) * sizeof(LONG), MEMF_ANY | MEMF_CLEAR);
	if (!coltab)
		return NULL;
	coltab[0] = cnum;
	coltab[1] = (LONG)cm;
	for (i = 2; i <= cnum + 1; i++)
		coltab[i] = -1;
	for (i = 2; i <= cnum + 1; i++) {
		ULONG r = ctab[0], g = ctab[1], b = ctab[2];

		ctab += 3;
		coltab[i] = ObtainBestPen(cm, r, g, b,
		                          OBP_Precision, PRECISION_EXACT, TAG_DONE);
		if (coltab[i] < 0)
			goto fail;
	}
	mw = E_MIN(w, pw);
	bw = (mw + 15) & 0xFFFFFFF0;
	bm = AllocBitMap(bw, 1, 8, BMF_CLEAR | BMF_INTERLEAVED, NULL);
	if (!bm)
		goto fail;
	CopyMem(rp, &tmprp, sizeof(struct RastPort));
	tmprp.Layer = NULL;
	tmprp.BitMap = bm;
	tmpar = AllocVec(bw, MEMF_ANY | MEMF_CLEAR);
	if (!tmpar)
		goto fail;

	/* E: ph-h/2 is (ph-h)/2; pcw is not reset between lines */
	acth = 0;
	pch = (ph - h) / 2;
	while (pch < 0)
		pch += ph;
	pcw = (pw - w) / 2;
	while (pcw < 0)
		pcw += pw;
	while (acth < h) {
		tmpptr = pattern + pch * pw;
		for (i = 0; i <= mw - 1; i++) {
			tmpar[i] = (UBYTE)coltab[tmpptr[pcw++] + 2];
			if (pcw >= pw)
				pcw = 0;
		}
		WritePixelLine8(rp, x, y + acth, mw, tmpar, &tmprp);
		pch++;
		if (pch >= ph)
			pch = 0;
		acth++;
	}
	pcw = x;
	while (w > pw) {
		w -= pw;
		pcw += pw;
		ClipBlit(rp, x, y, rp, pcw, y, E_MIN(w, pw), h, 0xC0);
	}

	FreeBitMap(bm);
	FreeVec(tmpar);
	return coltab;

fail:
	if (bm)
		FreeBitMap(bm);
	if (tmpar)
		FreeVec(tmpar);
	return freepattern(coltab);
}

LONG *freepattern(LONG *coltab)
{
	LONG i, n;
	struct ColorMap *cm;

	if (coltab) {
		n = coltab[0];
		cm = (struct ColorMap *)coltab[1];
		for (i = 2; i <= n + 1; i++)
			if (coltab[i] >= 0)
				ReleasePen(cm, coltab[i]);
		FreeVec(coltab);
	}
	return NULL;
}
