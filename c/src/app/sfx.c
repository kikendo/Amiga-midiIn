/*
 * sfx.c - port of soundfx_ahi.e
 *
 * Rate maths uses C doubles where the E code called mathieeedoubbas.library.
 * The sound engine is chosen with the AUDIO argument / tooltype (new).
 */
#include <exec/types.h>
#include <exec/memory.h>
#include <utility/tagitem.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/utility.h>

#include "../audio/snd.h"
#include "eport.h"
#include "loaders.h"
#include "sfx.h"

/* ---------------------------------------------------------------- init */

LONG sfx_engine_req = SND_AUTO;

void initsoundfx(UWORD numsamples, LONG engine)
{
	sfx_engine_req = engine;
	if (!snd_init((enum snd_engine)engine, numsamples))
		Raise('AUDB');
}

void freesoundfx(void)
{
	snd_end();
}

LONG audio_attrs(struct TagItem *tags)
{
	struct TagItem *tstate = tags, *t;
	LONG audioon = FALSE;

	while ((t = NextTagItem(&tstate))) {
		ULONG d = t->ti_Data;

		switch (t->ti_Tag) {
		case SFX_SET_AUDIO_STATUS:
			snd_stopchannelmask(0xFFFFFFFFUL);
			if (d)
				audioon = snd_audioon();
			else
				snd_audiooff();
			break;
		case SFX_SET_AUDIO_ID:
			snd_setaudioid(d);
			break;
		case SFX_SET_CHANNELS:
			snd_setnumchannels((UWORD)d);
			break;
		case SFX_SET_MIXFREQ:
			snd_setmixfreq(d);
			break;
		case SFX_SET_APPLYAUDIO:
			if (d && snd_is_on())
				audioon = snd_audioon();
			break;
		case SFX_GET_AUDIO_STATUS:
			audioon = snd_is_on();
			if (d)
				*(LONG *)d = audioon;
			break;
		}
	}
	return audioon;
}

/* -------------------------------------------------------------- object */

struct sfx *sfx_new(void)
{
	return (struct sfx *)e_new(sizeof(struct sfx));
}

void sfx_dispose(struct sfx *s)
{
	if (s) {
		sfx_end(s);
		e_dispose(s);
	}
}

STRPTR sfx_pathname(struct sfx *s)
{
	return (STRPTR)(s->name[0] ? s->name : "");
}

LONG sfx_filetype(struct sfx *s, STRPTR *descr)
{
	struct sampleinfo t;

	if (s->name[0] == 0)
		return 0;
	if (!loader_recon((STRPTR)s->name, &t))
		Raise('UNRE');
	if (descr)
		*descr = t.descr;
	return t.type;
}

LONG sfx_init(struct sfx *s, CONST_STRPTR name, STRPTR *descr)
{
	struct sampleinfo t;

	if (s->start)
		sfx_end(s);
	s->loadcnest = 0;
	s->maxvolume = 0;
	s->id = 0;
	s->myself = s;
	if (!loader_recon(name, &t))
		Raise('UNRE');
	estrcpy((STRPTR)s->name, name, sizeof(s->name));
	s->ln.ln_Name = (char *)FilePart((STRPTR)s->name);
	if (descr)
		*descr = t.descr;
	return t.type;
}

static LONG getmaxvolume(const WORD *start, LONG length)
{
	const WORD *end = (const WORD *)((const UBYTE *)start + length);
	UWORD max = 0;
	LONG m;

	while (start < end) {
		WORD v = *start++;
		UWORD a = (UWORD)(v < 0 ? -v : v);

		if (a > max)
			max = a;
	}
	m = max;
	if (m < 16384)
		return 512;
	return 32767 * 256 / m;
}

void sfx_load(struct sfx *s)
{
	struct sampleinfo t;
	volatile WORD cnst = 0;

	if (s->start) {
		s->loadcnest++;
		return;
	}
	s->myself = s;
	E_TRY {
		if (!loader_recon((STRPTR)s->name, &t))
			Raise('UNRE');
		if (!loader_get((STRPTR)s->name, &t))
			Raise('UNRE');
		s->type = t.channels > 1 ? 1 : 0;
		cnst = 1;
		s->length = t.bytelength;
		s->ln.ln_Name = (char *)FilePart((STRPTR)s->name);
		sfx_setloop(s, t.loop);
		if (t.rate < 3000)
			t.rate = 3000;
		sfx_setrate(s, t.rate);
		s->start = t.start;
		s->maxvolume = (WORD)getmaxvolume(s->start, s->length);
		s->id = snd_setsample(s->start, (ULONG)sfx_frames(s), s->type ? TRUE : FALSE);
		if (s->id == 0) {
			sfx_end(s);
			cnst = 0;
		}
	} E_EXCEPT_DO {
		s->loadcnest = cnst;
		ReThrow();
	} E_END;
}

void sfx_unload(struct sfx *s)
{
	if (s->start) {
		WORD c = (WORD)(s->loadcnest - 1);

		if (c <= 0)
			sfx_end(s);
		else
			s->loadcnest = c;
	} else {
		s->loadcnest = 0;
	}
}

void sfx_end(struct sfx *s)
{
	WORD *a;

	s->loadcnest = 0;
	s->maxvolume = 0;
	if ((a = s->start)) {
		s->start = 0;
		snd_delsample(s->id);
		e_dispose(a);
	}
}

/* pitch 0..16383, 8192 = no change; frequency between note and
 * note +/- range */
static LONG bent_rate(struct sfx *s, LONG pitch, LONG range, LONG note, LONG fine, LONG base)
{
	double e, a, c, rate = (double)s->rate;

	pitch -= 8192;
	if (pitch >= 0) {
		e = (double)pitch / 8192.0;
		a = noterate(rate, note + range, fine, base);
	} else {
		e = (double)-pitch / 8192.0;
		a = noterate(rate, note - range, fine, base);
	}
	c = noterate(rate, note, fine, base);
	return (LONG)((a - c) * e + c);
}

BOOL sfx_changepitch(struct sfx *s, UWORD chan, LONG pitch, LONG range,
                     LONG note, LONG fine, LONG base, LONG uptime, BOOL tyl)
{
	LONG a;

	if (s->start) {
		a = bent_rate(s, pitch, range, note, fine, base);
		if (tyl)
			a = -a;
		snd_setfreq(chan, uptime, a);
	}
	return TRUE;
}

BOOL sfx_play(struct sfx *s, UWORD chan, BOOL repeat, LONG note, LONG fine,
              LONG base, LONG volume, LONG pan, LONG pitch, LONG prange,
              LONG t1, LONG t2, LONG stn, LONG tskip)
{
	struct snd_envelope ev, *evp = 0;
	LONG a;

	if (s->start) {
		if (!((t1 | t2) == 0 && stn == 255)) {
			ev.attack = (UBYTE)t1;
			ev.decay = (UBYTE)t2;
			ev.sustain = (UBYTE)stn;
			evp = &ev;
		}
		a = bent_rate(s, pitch, prange, note, fine, base);
		snd_playsample(chan, s->id, tskip, a, (ULONG)volume, (ULONG)pan,
		               repeat ? s->loop : -1, evp);
	}
	return TRUE;
}

LONG sfx_setrate(struct sfx *s, LONG newrate)
{
	LONG ret = s->rate;

	if (newrate > 0)
		s->rate = newrate;
	return ret;
}

LONG sfx_basefreq(struct sfx *s)
{
	return sfx_setrate(s, 0);
}

LONG sfx_frames(struct sfx *s)
{
	if (s->start)
		return s->length >> ((s->type & 1) ? 2 : 1);
	return -1;
}

LONG sfx_length(struct sfx *s)
{
	return s->start ? s->length : -1;
}

BOOL sfx_stereo(struct sfx *s)
{
	return s->start && (s->type & 1) ? TRUE : FALSE;
}

LONG sfx_maxvolume(struct sfx *s)
{
	return s->maxvolume < 256 ? 256 : s->maxvolume;
}

LONG sfx_setloop(struct sfx *s, LONG loopframe)
{
	if (loopframe < 0)
		return s->loop;
	if ((loopframe << (s->type ? 2 : 1)) >= s->length)
		loopframe = 0;
	s->loop = loopframe;
	return -1;
}

/* ------------------------------------------------------- channel helpers */

void changevolume(UWORD chan, LONG volume, LONG pan)
{
	snd_setvolume(chan, (ULONG)volume, (ULONG)pan);
}

void releasechan(UWORD chan, LONG tenths)
{
	snd_release(chan, (UWORD)tenths);
}

void soundsoff(ULONG chanmask)
{
	if (chanmask)
		snd_stopchannelmask(chanmask);
}

void soundoff(UWORD chan)
{
	snd_stopchannel(chan);
}

BOOL chanfree(UWORD chan)
{
	return snd_freechannels(1UL << chan) ? TRUE : FALSE;
}

ULONG channelsfree(ULONG chanmask)
{
	return snd_freechannels(chanmask);
}
