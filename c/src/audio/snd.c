/*
 * snd.c - engine choice and the 50 Hz voice control (volume envelopes and
 * pitch glides), see snd.h
 *
 * The voice control is a direct port of snd_playsample, snd_setvolume,
 * snd_release, snd_setfreq and realtimeplay from playAHI_custom.e, with the
 * mixer calls replaced by calls to the selected engine.
 */
#include <exec/types.h>

#include "snd.h"
#include "snd_backend.h"

#define MAXCH 32

struct chanctl {
	LONG volume;      /* 0 = off */
	LONG freq;        /* negative = backwards */
	LONG voladd;      /* envelope step per tick, 0 = none */
	LONG volenv;      /* envelope level 1..65535, 0 = no envelope */
	LONG voltarget;
	LONG voldecay;
	LONG pan;
	LONG freqtarget;
	LONG freqadd;     /* glide step per tick, 0 = no glide */
	LONG uptime;      /* glide ticks left */
};

static const struct snd_backend *B;
static struct chanctl chans[MAXCH];

volatile APTR snd_scopedata;
ULONG snd_scopelen;

static LONG labs32(LONG v)
{
	return v < 0 ? -v : v;
}

/* realtimeplay: runs 50 times a second while the audio is on */
static void tick(void)
{
	ULONG chmask = ~B->free_mask(0xFFFFFFFFUL);
	struct chanctl *c = chans;
	UWORD i;

	for (i = 0; i < B->voices; i++, c++, chmask >>= 1) {
		LONG x, b, v;

		if (!(chmask & 1))
			continue;
		if ((x = c->volenv) != 0) {
			if ((b = c->voladd) < 0) {
				x += b;
				if (x <= c->voltarget) {
					x = c->voltarget;
					c->voladd = 0;
				}
				c->volenv = x;
			} else if (b > 0) {
				x += b;
				if (x & 0x10000) {          /* attack done, start decay */
					x = 65535;
					c->voladd = c->voldecay;
				}
				c->volenv = x;
			}
			if (x == 0)
				B->stop(i);
			v = (LONG)((((ULONG)x * ((ULONG)c->volume >> 1)) / 65535UL) * 2);
			B->set_volume(i, (ULONG)v, (ULONG)c->pan);
		}
		if ((b = c->freqadd) != 0) {
			v = labs32(c->freq) + b;
			if ((b = c->uptime) <= 0) {
				v = c->freqtarget;
				c->freqadd = 0;
				c->uptime = 0;
			} else {
				c->uptime = b - 1;
			}
			c->freq = (c->freq < 0) ? -v : v;
			B->set_freq(i, c->freq);
		}
	}
}

/* ------------------------------------------------------------- engines */

static BOOL try_engine(const struct snd_backend *b, UWORD maxsamples)
{
	UWORD i;

	if (!b->init(maxsamples))
		return FALSE;
	for (i = 0; i < MAXCH; i++) {
		struct chanctl *c = &chans[i];

		c->volume = c->freq = c->voladd = c->volenv = 0;
		c->voldecay = c->pan = c->freqtarget = c->freqadd = c->uptime = 0;
		c->voltarget = 1;
	}
	B = b;
	B->set_tick(tick);
	return TRUE;
}

BOOL snd_init(enum snd_engine engine, UWORD maxsamples)
{
	if (B)
		snd_end();
	switch (engine) {
	case SND_AHI:
		return try_engine(&snd_backend_ahi, maxsamples);
	case SND_PAULA14:
		return try_engine(&snd_backend_paula14, maxsamples);
	case SND_PAULA4:
		return try_engine(&snd_backend_paula4, maxsamples);
	default:
		return try_engine(&snd_backend_ahi, maxsamples)
		    || try_engine(&snd_backend_paula14, maxsamples)
		    || try_engine(&snd_backend_paula4, maxsamples);
	}
}

void snd_end(void)
{
	if (!B)
		return;
	B->audio_off();
	B->end();
	B = 0;
	snd_scopedata = 0;
}

void snd_scope_update(void)
{
	const struct snd_backend *b = B;

	if (b && b->scope_update)
		b->scope_update();
}

const char *snd_engine_name(void)
{
	return B ? B->name : "none";
}

UWORD snd_voices(void)
{
	return B ? B->voices : 0;
}

/* ---------------------------------------------------------- audio on/off */

BOOL snd_audioon(void)
{
	return B ? B->audio_on() : FALSE;
}

void snd_audiooff(void)
{
	if (B)
		B->audio_off();
	snd_scopedata = 0;
}

BOOL snd_is_on(void)
{
	return B ? B->is_on() : FALSE;
}

void snd_setaudioid(ULONG audioid)
{
	if (B && B->set_audioid)
		B->set_audioid(audioid);
}

void snd_setmixfreq(ULONG mixfreq)
{
	if (B && B->set_mixfreq)
		B->set_mixfreq(mixfreq);
}

/* stops every channel from 'channels' on */
void snd_setnumchannels(UWORD channels)
{
	if (B && channels < 32)
		B->stop_mask(~((1UL << channels) - 1));
}

/* -------------------------------------------------------------- samples */

ULONG snd_setsample(const WORD *address, ULONG frames, BOOL stereo)
{
	return B ? B->load(address, frames, stereo) : 0;
}

void snd_delsample(ULONG id)
{
	if (B && id)
		B->unload(id);
}

/* -------------------------------------------------------------- voices */

void snd_playsample(UWORD channel, ULONG id, LONG offset, LONG freq,
                    ULONG volume, ULONG pan, LONG loop,
                    const struct snd_envelope *env)
{
	struct chanctl *c;
	LONG a = 0, b = 0, s = 0;

	if (!B || !B->is_on() || channel >= B->voices)
		return;
	c = &chans[channel];
	B->stop(channel);

	if (env) {
		/* per tick steps: attack to full, decay from full to sustain */
		if ((a = (LONG)env->attack * 5) != 0)
			a = (LONG)(65535UL / (ULONG)a);
		else
			a = 65535;
		if ((s = env->sustain) != 0)
			s = (s << 8) | s;
		else
			s = 1;
		if ((b = (LONG)env->decay * 5) != 0)
			b = (LONG)((65535UL - (ULONG)s) / (ULONG)b);
		else
			b = 65535;
	}
	c->volume = (LONG)volume;
	c->freqadd = 0;
	c->uptime = 0;
	c->pan = (LONG)pan;
	c->freq = freq;
	if (env) {
		c->voladd = a;
		c->volenv = 1;
		c->voldecay = -b;
		c->voltarget = s;
		volume = 0;                     /* the tick raises it */
	} else {
		c->voladd = 0;
		c->volenv = 0;
		c->voltarget = 1;
	}
	if (id)
		B->play(channel, id, offset, freq, volume, pan, loop);
}

void snd_stopchannel(UWORD channel)
{
	if (B && channel < B->voices)
		B->stop(channel);
}

void snd_stopchannelmask(ULONG mask)
{
	if (B)
		B->stop_mask(mask);
}

void snd_setvolume(UWORD channel, ULONG volume, ULONG pan)
{
	struct chanctl *c;

	if (!B || !B->is_on() || channel >= B->voices)
		return;
	c = &chans[channel];
	c->volume = (LONG)volume;
	c->pan = (LONG)pan;
	if (c->volenv == 0)
		B->set_volume(channel, volume, pan);
}

void snd_release(UWORD channel, UWORD tenths)
{
	struct chanctl *c;
	LONG a, b, v;

	if (!B || !B->is_on() || channel >= B->voices)
		return;
	c = &chans[channel];
	if (c->voltarget == 0)
		return;                         /* already released */
	v = c->volenv;
	b = v ? v : 65535;
	if ((a = (LONG)tenths * 5) != 0) {
		a = (LONG)((ULONG)b / (ULONG)a);
		c->voladd = 0;
		c->voltarget = 0;
		if (v == 0)
			c->volenv = b;
		c->voladd = a ? -a : -1;
	} else {
		B->stop(channel);
	}
}

/* Glides to destfreq over uptime ticks; uptime 0 keeps a glide that is
 * running and retargets it, or sets the frequency at once. The sign of
 * destfreq gives the direction. As in playAHI_custom.e, the immediate case
 * passes the frequency without its sign and does not update the stored
 * frequency. */
void snd_setfreq(UWORD channel, LONG uptime, LONG destfreq)
{
	struct chanctl *c;

	if (!B || !B->is_on() || channel >= B->voices)
		return;
	c = &chans[channel];
	if (destfreq < 0) {
		if (c->freq > 0)
			c->freq = -c->freq;
		destfreq = -destfreq;
	} else {
		if (c->freq < 0)
			c->freq = -c->freq;
	}
	c->freqtarget = destfreq;
	if (uptime) {
		c->uptime = uptime;
		c->freqadd = (destfreq - labs32(c->freq)) / uptime;
	} else if (c->uptime) {
		c->freqadd = (destfreq - labs32(c->freq)) / c->uptime;
	} else {
		B->set_freq(channel, destfreq);
	}
}

ULONG snd_freechannels(ULONG mask)
{
	ULONG all;

	if (!B)
		return mask;
	all = B->voices >= 32 ? 0xFFFFFFFFUL : ((1UL << B->voices) - 1);
	if (!B->is_on())
		return mask & all;
	return B->free_mask(mask) & all;
}
