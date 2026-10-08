/*
 * loaders.h - sample file loading: AIFF, 8SVX, WAVE (extloader.e and the
 * modules it uses). All samples come out as 16-bit signed, mono or
 * interleaved stereo.
 */
#ifndef MI_LOADERS_H
#define MI_LOADERS_H

#include <exec/types.h>

struct sampleinfo {
	STRPTR descr;       /* description string */
	LONG type;          /* 'AIFF', '8SVX', 'WAVE' */
	WORD *start;        /* sample data (e_dispose() to free) */
	LONG loop;          /* loop start frame, 0 = whole sample */
	LONG frames;
	LONG channels;      /* 1 or 2 */
	LONG bytelength;
	LONG rate;          /* frames per second */
};

/* Recognises the file type; TRUE and descr/type set if known. Raises on a
 * file that cannot be opened. */
BOOL loader_recon(CONST_STRPTR name, struct sampleinfo *si);

/* Loads the data of a file loader_recon() recognised. Raises on errors. */
BOOL loader_get(CONST_STRPTR name, struct sampleinfo *si);

/* notecalc.e: playback rate in Hz of note (with fine 0..200, 100 =
 * centre) for a sample recorded at rate that sounds base */
double noterate(double rate, LONG note, LONG fine, LONG base);

#endif
