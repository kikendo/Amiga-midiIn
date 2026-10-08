/*
 * loaders.c - port of extloader.e, simpleiffparse.e, loadssnd.e,
 * loadsvxbody.e, loadwavedata.e and notecalc.e
 *
 * Kept as in the E code, including how WAVE frames are counted from the
 * block alignment. Sample memory is taken from any memory instead of Fast
 * RAM only, so loading also works on machines without Fast RAM.
 */
#include <string.h>
#include <exec/types.h>
#include <exec/memory.h>
#include <dos/dos.h>
#include <proto/exec.h>
#include <proto/dos.h>

#include "eport.h"
#include "locale.h"
#include "loaders.h"
#include "tables.h"

#define SAMPLE_MEM MEMF_PUBLIC

static ULONG be32(const UBYTE *p)
{
	return ((ULONG)p[0] << 24) | ((ULONG)p[1] << 16) | ((ULONG)p[2] << 8) | p[3];
}

static UWORD be16(const UBYTE *p)
{
	return (UWORD)((p[0] << 8) | p[1]);
}

static ULONG le32(const UBYTE *p)
{
	return ((ULONG)p[3] << 24) | ((ULONG)p[2] << 16) | ((ULONG)p[1] << 8) | p[0];
}

static UWORD le16(const UBYTE *p)
{
	return (UWORD)((p[1] << 8) | p[0]);
}

/* ------------------------------------------------------- simpleiffparse.e */

static LONG seekIFFHeader(BPTR file, LONG *tptr, BOOL next)
{
	LONG type = *tptr, left, curr;
	UBYTE buffer[12];
	LONG len;

	if ((curr = Seek(file, 0, OFFSET_END)) == -1)
		Raise('READ');
	if (next)
		if ((curr = curr - 8) < 0)
			Raise('READ');
	if ((left = Seek(file, curr, OFFSET_BEGINNING)) == -1)
		Raise('READ');
	left = left - curr;
	if (next) {
		if (Read(file, buffer, 4) != 4)
			Raise('READ');
		len = (LONG)(((be32(buffer) + 1) >> 1) << 1);
		if ((left = left - 4 - len) < 0)
			Raise('MNGL');
		if (Seek(file, len, OFFSET_CURRENT) == -1)
			Raise('READ');
	}
	while (left > 0) {
		if (left < 8)
			Raise('NIFF');
		if (Read(file, buffer, 12) < 8)
			Raise('READ');
		if ((LONG)be32(buffer) != 'FORM')
			Raise('NIFF');
		if ((LONG)be32(buffer + 8) == type || type == 0) {
			*tptr = (LONG)be32(buffer + 8);
			return (LONG)be32(buffer + 4);
		}
		len = (LONG)(((be32(buffer + 4) + 1) >> 1) << 1);
		if ((left = left - 8 - len) < 0)
			Raise('MNGL');
		if (Seek(file, len - 4, OFFSET_CURRENT) == -1)
			Raise('MNGL');
	}
	return -1;
}

static LONG seekIFFChunk(BPTR file, LONG *tptr, BOOL next)
{
	LONG type = *tptr, left, curr, len;
	UBYTE buffer[8];

	if ((curr = Seek(file, 0, OFFSET_END)) == -1)
		Raise('READ');
	if (next)
		if ((curr = curr - 4) < 0)
			Raise('READ');
	if ((left = Seek(file, curr, OFFSET_BEGINNING)) == -1)
		Raise('READ');
	left = left - curr;
	if (next) {
		if (Read(file, buffer, 4) != 4)
			Raise('READ');
		len = (LONG)(((be32(buffer) + 1) >> 1) << 1);
		if ((left = left - 4 - len) < 0)
			Raise('MNGL');
		if (Seek(file, len, OFFSET_CURRENT) == -1)
			Raise('READ');
	}
	while (left > 0) {
		if (left < 8)
			Raise('NIFF');
		if (Read(file, buffer, 8) < 8)
			Raise('READ');
		if ((LONG)be32(buffer) == 'FORM') {
			if (Seek(file, -8, OFFSET_CURRENT) == -1)
				Raise('READ');
			return -1;
		}
		len = (LONG)(((be32(buffer + 4) + 1) >> 1) << 1);
		if ((LONG)be32(buffer) == type || type == 0) {
			*tptr = (LONG)be32(buffer);
			return len;
		}
		if ((left = left - 8 - len) < 0)
			Raise('MNGL');
		if (Seek(file, len, OFFSET_CURRENT) == -1)
			Raise('MNGL');
	}
	return -1;
}

/* ------------------------------------------- reading into 16-bit samples */

/* loadSSND: AIFF sound data, big-endian, 1..4 bytes per sample; stereo
 * keeps the last channel of each frame for the right side (as E) */
static WORD *loadSSND(BPTR fh, LONG frames, LONG smpsize, LONG channels, LONG *bytes)
{
	WORD *volatile p = 0;
	UBYTE *volatile buff = 0;
	LONG k, z, buffsize;

	k = frames << 1;
	if (channels > 1)
		k <<= 1;
	*bytes = k;
	z = smpsize <= 8 ? 1 : smpsize <= 16 ? 2 : smpsize <= 24 ? 3 : 4;
	buffsize = 8192 * z * channels;

	E_TRY {
		WORD *t;
		LONG l = frames * channels * z;

		p = (WORD *)e_newm((ULONG)k, SAMPLE_MEM);
		buff = (UBYTE *)e_new((ULONG)buffsize);
		t = p;
		while (l > 0) {
			LONG x = E_MIN(l, buffsize);
			UBYTE *a = buff, *end = buff + x;

			if (Read(fh, buff, x) != x)
				Raise('READ');
			l -= x;
			while (a < end) {
				LONG c;
				WORD v = 0;

				/* first channel */
				v = (WORD)((a[0] << 8) | (z > 1 ? a[1] : 0));
				a += z;
				*t++ = v;
				if (channels > 1) {
					for (c = 1; c < channels; c++) {
						v = (WORD)((a[0] << 8) | (z > 1 ? a[1] : 0));
						a += z;
					}
					*t++ = v;
				}
			}
		}
	} E_EXCEPT_DO {
		e_dispose(buff);
		if (exception) {
			e_dispose(p);
			ReThrow();
		}
	} E_END;
	return p;
}

/* loadsvxBODY: 8SVX body, 8 or 16 bit; stereo as two blocks, left first */
static WORD *loadsvxBODY(BPTR fh, LONG frames, LONG smpsize, LONG chanoffset, LONG *bytes)
{
	WORD *volatile p = 0;
	UBYTE *volatile buff = 0;
	LONG k, z, buffsize, skip = chanoffset ? 1 : 0;

	k = (frames << 1) << skip;
	*bytes = k;
	z = smpsize <= 8 ? 1 : 2;
	buffsize = 16384 * z;

	E_TRY {
		LONG f;

		p = (WORD *)e_newm((ULONG)k, SAMPLE_MEM);
		buff = (UBYTE *)e_new((ULONG)buffsize);
		for (f = 0; f <= skip; f++) {
			WORD *t = p + f;
			LONG l = frames * z;

			while (l > 0) {
				LONG x = E_MIN(l, buffsize);
				UBYTE *a = buff, *end = buff + x;

				if (Read(fh, buff, x) != x)
					Raise('READ');
				l -= x;
				chanoffset -= x;
				while (a < end) {
					if (z == 1) {
						*t = (WORD)(a[0] << 8);
						a += 1;
					} else {
						*t = (WORD)be16(a);
						a += 2;
					}
					t += 1 + skip;
				}
			}
			if (skip && f == 0)
				if (Seek(fh, chanoffset, OFFSET_CURRENT) == -1)
					Raise('READ');
		}
	} E_EXCEPT_DO {
		e_dispose(buff);
		if (exception) {
			e_dispose(p);
			ReThrow();
		}
	} E_END;
	return p;
}

/* loadWAVEdata: little-endian, bytesize bytes per sample read as in E (the
 * last two bytes of each make the 16-bit value); 8-bit is unsigned */
static WORD *loadWAVEdata(BPTR fh, LONG frames, LONG channels, LONG bytesize, LONG *bytes)
{
	WORD *volatile p = 0;
	UBYTE *volatile buff = 0;
	LONG k, buffsize;
	const LONG l0 = frames * channels * bytesize;
	const LONG skip = channels > 2 ? (channels - 2) * bytesize : 0;
	const LONG outch = channels > 2 ? 2 : channels;

	k = frames << 1;
	if (channels > 1)
		k <<= 1;
	*bytes = k;
	buffsize = 8192 * bytesize * channels;

	E_TRY {
		WORD *t;
		LONG l = l0;

		p = (WORD *)e_newm((ULONG)k, SAMPLE_MEM);
		buff = (UBYTE *)e_new((ULONG)buffsize);
		t = p;
		while (l > 0) {
			LONG x = l < buffsize ? l : buffsize;
			UBYTE *a = buff, *end = buff + x;

			if (Read(fh, buff, x) != x)
				Raise('READ');
			l -= x;
			while (a < end) {
				LONG c;

				for (c = 0; c < outch; c++) {
					if (bytesize == 1) {
						*t++ = (WORD)((UBYTE)(a[0] - 128) << 8);
						a += 1;
					} else {
						UWORD v = 0;
						LONG b;

						for (b = 0; b < bytesize; b++)
							v = (UWORD)((v >> 8) | (a[b] << 8));
						*t++ = (WORD)v;
						a += bytesize;
					}
				}
				a += skip;
			}
		}
	} E_EXCEPT_DO {
		e_dispose(buff);
		if (exception) {
			e_dispose(p);
			ReThrow();
		}
	} E_END;
	return p;
}

/* -------------------------------------------------------------- formats */

static BOOL load8svx(BPTR fh, struct sampleinfo *si)
{
	UBYTE vhdr[20];
	LONG t, l, k, chn = 1, bits = 8, oneshot, repeat, bytes;
	WORD *data;

	t = '8SVX';
	if (seekIFFHeader(fh, &t, FALSE) == -1)
		Raise('READ');
	t = 'VHDR';
	if (seekIFFChunk(fh, &t, FALSE) != 20)
		Raise('N8SV');
	if (Read(fh, vhdr, 20) != 20)
		Raise('READ');
	if (vhdr[15] != 0)                      /* sCompression */
		Raise('FIBO');
	t = 0;
	if ((l = seekIFFChunk(fh, &t, FALSE)) == -1)
		Raise('NBDY');
	while (t != 'BODY') {
		if ((t == 'CHAN' || t == 'BITS') && l == 4) {
			UBYTE v[4];

			if (Read(fh, v, 4) != 4)
				Raise('READ');
			k = (LONG)be32(v);
			if (t == 'BITS') {
				bits = k;
			} else {
				ULONG m = (ULONG)k;

				chn = 0;
				while (m) {
					chn += (LONG)(m & 1);
					m >>= 1;
				}
				if (chn == 0)
					chn = 1;        /* E would divide by zero */
			}
			t = 0;
			if ((l = seekIFFChunk(fh, &t, FALSE)) == -1)
				Raise('NBDY');
		} else {
			t = 0;
			if ((l = seekIFFChunk(fh, &t, TRUE)) == -1)
				Raise('NBDY');
		}
	}

	oneshot = (LONG)be32(vhdr);
	repeat = (LONG)be32(vhdr + 4);
	if ((k = oneshot + repeat) == 0) {
		k = l / chn;
		if (bits > 8)
			k = k / 2;
	}
	chn = chn > 1 ? l / chn : 0;            /* offset of the second channel */
	data = loadsvxBODY(fh, k, bits, chn, &bytes);
	si->start = data;
	si->loop = repeat ? oneshot : 0;
	si->bytelength = bytes;
	si->channels = chn ? 2 : 1;
	si->frames = k;
	si->rate = be16(vhdr + 12);
	return TRUE;
}

/* 80 bit IEEE 754 extended to a whole number of Hz */
static LONG extended_to_long(const UBYTE *v)
{
	WORD expo = (WORD)(((v[0] & 0x7F) << 8) | v[1]);
	ULONG hi = be32(v + 2);
	LONG shift;

	if (hi == 0 && be32(v + 6) == 0)
		return 0;
	shift = 16383 + 31 - expo;
	if (shift < 0)
		return 0x7FFFFFFF;
	if (shift > 31)
		return 0;
	return (LONG)(hi >> shift);
}

static BOOL loadaiff(BPTR fh, struct sampleinfo *si)
{
	UBYTE com[18];
	LONG t, k, rate;
	WORD *data;
	WORD chn, smpsize;
	LONG frames;

	t = 'AIFF';
	if (seekIFFHeader(fh, &t, FALSE) == -1)
		Raise('READ');
	t = 'COMM';
	if (seekIFFChunk(fh, &t, FALSE) != 18)
		Raise('NAIF');
	if (Read(fh, com, 18) != 18)
		Raise('READ');
	t = 'SSND';
	if (seekIFFChunk(fh, &t, FALSE) < 9)
		Raise('NSND');
	if (Seek(fh, 8, OFFSET_CURRENT) == -1)
		Raise('READ');

	chn = (WORD)be16(com);
	frames = (LONG)be32(com + 2);
	smpsize = (WORD)be16(com + 6);
	data = loadSSND(fh, frames, smpsize, chn, &k);
	si->start = data;
	si->loop = 0;
	si->bytelength = k;
	si->channels = chn > 1 ? 2 : 1;
	si->frames = frames;
	si->rate = 16576;
	if ((rate = extended_to_long(com + 8)) != 0)
		si->rate = rate;
	return TRUE;
}

/* returns the (even rounded) chunk length, 0 if not found */
static LONG seek_RIFF_CHUNK(BPTR fh, LONG chunk)
{
	UBYTE buff[12];
	LONG k, t, l;

	if (Seek(fh, 0, OFFSET_BEGINNING) == -1)
		Raise('READ');
	if (Read(fh, buff, 12) != 12)
		Raise('READ');
	l = (LONG)le32(buff + 4) + 12;
	for (;;) {
		if (Read(fh, buff, 8) != 8)
			return 0;
		t = ((LONG)le32(buff + 4) + 1) & ~1L;
		if ((LONG)be32(buff) == chunk)
			return t;
		if ((k = Seek(fh, t, OFFSET_CURRENT)) == -1)
			return 0;
		if (k + t > l)
			return 0;
	}
}

static BOOL loadwave(BPTR fh, struct sampleinfo *si)
{
	UBYTE fmt[14];
	LONG len, chan, align, frames, bytes;
	WORD *data;

	if (seek_RIFF_CHUNK(fh, 'fmt ') < 14)
		Raise('BWAV');
	if (Read(fh, fmt, 14) != 14)
		Raise('READ');
	if ((len = seek_RIFF_CHUNK(fh, 'data')) <= 0)
		Raise('BWAV');
	if (le16(fmt) != 0x0001)                /* WAVE_FORMAT_PCM */
		Raise('WAVN');
	if ((chan = le16(fmt + 2)) < 1)
		Raise('BWAV');
	if ((align = le16(fmt + 12)) < 1)
		Raise('BWAV');
	frames = len / chan / align;

	data = loadWAVEdata(fh, frames, chan, align, &bytes);
	si->start = data;
	si->loop = 0;
	si->bytelength = bytes;
	si->channels = chan > 1 ? 2 : 1;
	si->frames = frames;
	si->rate = (LONG)le32(fmt + 4);
	return TRUE;
}

/* ------------------------------------------------------------ extloader */

struct sigcheck {
	LONG offset;
	const char *text;
};

struct filetype {
	LONG strid;
	LONG type;
	struct sigcheck check[2];
};

static const struct filetype types[] = {
	{ STRID_AIFFNAME, 'AIFF', { { 0, "FORM" }, { 8, "AIFF" } } },
	{ STRID_8SVXNAME, '8SVX', { { 0, "FORM" }, { 8, "8SVX" } } },
	{ STRID_WAVENAME, 'WAVE', { { 0, "RIFF" }, { 8, "WAVE" } } },
};

BOOL loader_recon(CONST_STRPTR name, struct sampleinfo *si)
{
	BPTR fh;
	UWORD i, c;
	BOOL status = FALSE;

	if (!(fh = Open(name, MODE_OLDFILE)))
		Raise('OPEN');
	for (i = 0; i < sizeof(types) / sizeof(types[0]) && !status; i++) {
		status = TRUE;
		for (c = 0; c < 2 && status; c++) {
			char buff[8];
			LONG l = (LONG)strlen(types[i].check[c].text);

			if (Seek(fh, types[i].check[c].offset, OFFSET_BEGINNING) == -1
			    || Read(fh, buff, l) != l
			    || memcmp(buff, types[i].check[c].text, (size_t)l) != 0)
				status = FALSE;
		}
		if (status && si) {
			si->descr = LOC(types[i].strid);
			si->type = types[i].type;
		}
	}
	Close(fh);
	return status;
}

BOOL loader_get(CONST_STRPTR name, struct sampleinfo *si)
{
	BPTR fh;
	volatile BOOL status = FALSE;

	if (!(fh = Open(name, MODE_OLDFILE)))
		Raise('OPEN');
	E_TRY {
		switch (si->type) {
		case 'AIFF':
			status = loadaiff(fh, si);
			break;
		case '8SVX':
			status = load8svx(fh, si);
			break;
		case 'WAVE':
			status = loadwave(fh, si);
			break;
		default:
			status = FALSE;
		}
	} E_EXCEPT_DO {
		Close(fh);
		ReThrow();
	} E_END;
	return status;
}

/* ------------------------------------------------------------ notecalc */

double noterate(double rate, LONG note, LONG fine, LONG base)
{
	LONG no, bo, n, ba, d;
	double c, a;

	if (note == base && (UWORD)fine == 100)
		return rate;

	no = (note + 3) / 12;
	n = (note + 3) - no * 12;
	if (n < 0 || n >= 12)
		n = 0;
	c = tonetable(n * 100 + (fine & 0xFF));

	bo = (base + 3) / 12;
	ba = (base + 3) - bo * 12;
	if (ba < 0 || ba >= 12)
		ba = 0;
	a = tonetable(ba * 100 + 100);

	d = bo - no;
	if (d < 0)
		c = c * (double)(1L << (-d & 15));
	else if (d > 0)
		c = c / (double)(1L << (d & 15));
	return c * rate / a;
}
