/*
 * paula14.c - 14-bit Paula engine, see paula14.h
 *
 * Port of the E side of play14unlim.e (allocandenable, disposeanddisable,
 * playchannel, setvolume, modifyfreq, stopchannels, stopselectedchan,
 * freechannels) and of the audio device handling and rate maths in
 * soundfx.e. The mixing and the 14-bit output are in play14.s.
 *
 * Differences from the E code: changes to the channel mask are made with
 * interrupts off (the mixer also changes it); the previous AUD1 interrupt
 * vector is put back on audio off; audio off waits for a mix in progress
 * to finish before freeing its buffers; the Paula clock is taken from the
 * machine (PAL or NTSC) instead of always PAL; volume envelopes are done by
 * snd.c, so the mixer's own envelope is kept at a constant 100%.
 */
#include <exec/types.h>
#include <exec/memory.h>
#include <exec/interrupts.h>
#include <exec/execbase.h>
#include <devices/audio.h>
#include <hardware/custom.h>
#include <hardware/dmabits.h>
#include <hardware/intbits.h>
#include <dos/dos.h>
#include <proto/exec.h>
#include <proto/dos.h>

#include "paula14.h"

#define CUSTOM      ((volatile struct Custom *)0xDFF000)
#define BUFFLEN     256UL               /* frames per mixed buffer */
#define FIXEDMAX    0x1000000L          /* envelope 1.0 */
#define PAL_CLOCK   3546895UL
#define NTSC_CLOCK  3579545UL
#define DEF_PERIOD  125UL               /* as in soundfx.e: about 28 kHz */
#define MIN_PERIOD  124UL
#define TICK_HZ     50
#define CALIB_FILE  "ENVARC:CyberSound/SoundDrivers/14Bit_Calibration"

/* --- shared with play14.s, layout must not change --------------------- */

struct p14_envelope {                   /* 20 bytes */
	LONG alt;
	LONG climb;
	LONG hit;
	LONG decay;
	LONG susthit;
};

struct p14_channel {                    /* 56 bytes */
	LONG current;                   /* -frames left, counts up to 0 */
	LONG skip;                      /* integer part of rate/mixrate */
	ULONG addmodulo;                /* fraction, 2^32 = 1 */
	ULONG modulo;
	LONG volumel;                   /* 256 = 100% */
	LONG volumer;
	const WORD *smpend;             /* end of the sample data */
	LONG looplength;
	struct p14_envelope envel;
	WORD loop;
	WORD stereo;
};

struct p14_data {
	struct p14_channel *channels;
	volatile ULONG channelmask;     /* bit n = channel n playing */
	volatile LONG busy;
	struct Interrupt *softint;
	LONG *bufferwork;
	WORD *buffercopy;
	WORD *bufferswap;
	BYTE *calibration;              /* middle of the table */
	BYTE *chip[4];                  /* +4 */
};

typedef char p14_channel_size_check[sizeof(struct p14_channel) == 56 ? 1 : -1];

extern void p14_intplay(void);          /* play14.s */
extern void p14_mixchannels(void);

/* ---------------------------------------------------------------------- */

struct p14sample {
	const WORD *data;               /* NULL: free slot */
	ULONG frames;
	BOOL stereo;
};

static UBYTE calibration[256];
static BOOL calibration_read;

static struct {
	BOOL              open;
	BOOL              on;
	struct p14sample *samples;
	UWORD             max_samples;
	ULONG             clock;
	ULONG             period;
	ULONG             mixrate;
	ULONG             wanted_freq;

	struct MsgPort   *port;
	struct IOAudio   *io;
	BOOL              device_open;

	struct p14_data   *dm;          /* everything below exists while on */
	struct p14_channel *chans;
	struct Interrupt  *mint;
	struct Interrupt  *sint;
	BYTE              *chipbuf[4];
	LONG              *work;
	struct Interrupt  *oldvec;
	BOOL               vector_set;

	struct Interrupt   vbl;
	BOOL               vbl_added;
	WORD               vbl_rate;
	WORD               vbl_acc;
	void             (*tick_cb)(void);
} P;

/* ---------------------------------------------------------------- helpers */

static void read_calibration(void)
{
	BPTR f;
	UWORD i;

	if (calibration_read)
		return;
	for (i = 0; i < 256; i++)
		calibration[i] = 0x40;
	f = Open((CONST_STRPTR)CALIB_FILE, MODE_OLDFILE);
	if (f) {
		Read(f, calibration, 256);
		Close(f);
	}
	calibration_read = TRUE;
}

/* the pan law of softmix.s (cvolume), so both mixers sound the same */
static void pan_volumes(ULONG volume, ULONG pan, LONG *left, LONG *right)
{
	ULONG p = pan >> 6;
	ULONG v = volume >> 8;
	ULONG q;

	if (p > 1024)
		p = 1024;
	q = 1024 - p;
	*right = (LONG)((v * (0x100000UL - p * p)) / 0x100000UL);
	*left = (LONG)((v * (0x100000UL - q * q)) / 0x100000UL);
}

static void rate(LONG freq, LONG *skip, ULONG *modulo)
{
	ULONG f = (ULONG)(freq < 0 ? -freq : freq);
	ULONG rem;

	*skip = (LONG)(f / P.mixrate);
	rem = f % P.mixrate;
	*modulo = (ULONG)(((unsigned long long)rem << 32) / P.mixrate);
}

static void wait_lines(UWORD n)
{
	while (n--) {
		UWORD line = (UWORD)(CUSTOM->vhposr & 0xFF00);

		while ((UWORD)(CUSTOM->vhposr & 0xFF00) == line)
			;
	}
}

static ULONG vbl_isr(void)
{
	P.vbl_acc += TICK_HZ;
	if (P.vbl_acc >= P.vbl_rate) {
		P.vbl_acc -= P.vbl_rate;
		if (P.tick_cb)
			P.tick_cb();
	}
	return 0;
}

/* ---------------------------------------------------------- open / close */

BOOL paula14_open(UWORD max_samples)
{
	if (P.open)
		return TRUE;
	if (!(SysBase->AttnFlags & AFF_68020) || max_samples == 0)
		return FALSE;
	P.samples = (struct p14sample *)AllocVec(
		(ULONG)max_samples * sizeof(struct p14sample), MEMF_PUBLIC | MEMF_CLEAR);
	if (!P.samples)
		return FALSE;
	P.max_samples = max_samples;
	P.clock = (SysBase->VBlankFrequency == 50) ? PAL_CLOCK : NTSC_CLOCK;
	P.vbl_rate = (SysBase->VBlankFrequency == 50) ? 50 : 60;
	P.open = TRUE;
	return TRUE;
}

void paula14_close(void)
{
	paula14_audio_off();
	if (P.samples) {
		FreeVec(P.samples);
		P.samples = 0;
	}
	P.max_samples = 0;
	P.open = FALSE;
}

void paula14_set_mixfreq(ULONG freq)
{
	P.wanted_freq = freq;
}

/* ---------------------------------------------------------- audio on/off */

static BOOL claim_audio(void)
{
	static UBYTE map[1] = { 15 };           /* all four channels or none */

	P.port = CreateMsgPort();
	if (!P.port)
		return FALSE;
	P.io = (struct IOAudio *)CreateIORequest(P.port, sizeof(struct IOAudio));
	if (!P.io)
		return FALSE;
	P.io->ioa_Request.io_Message.mn_Node.ln_Pri = 127;
	P.io->ioa_Request.io_Command = ADCMD_ALLOCATE;
	P.io->ioa_Request.io_Flags = ADIOF_NOWAIT;
	P.io->ioa_AllocKey = 0;
	P.io->ioa_Data = map;
	P.io->ioa_Length = sizeof(map);
	if (OpenDevice((CONST_STRPTR)"audio.device", 0, (struct IORequest *)P.io, 0) != 0)
		return FALSE;
	P.device_open = TRUE;
	return TRUE;
}

static void release_audio(void)
{
	if (P.device_open) {
		CloseDevice((struct IORequest *)P.io);
		P.device_open = FALSE;
	}
	if (P.io) {
		DeleteIORequest((struct IORequest *)P.io);
		P.io = 0;
	}
	if (P.port) {
		DeleteMsgPort(P.port);
		P.port = 0;
	}
}

static void free_buffers(void)
{
	UWORD i;

	for (i = 0; i < 4; i++) {
		if (P.chipbuf[i]) {
			FreeMem(P.chipbuf[i], BUFFLEN);
			P.chipbuf[i] = 0;
		}
	}
	if (P.work) {
		FreeVec(P.work);
		P.work = 0;
	}
	if (P.chans) {
		FreeVec(P.chans);
		P.chans = 0;
	}
	if (P.dm) {
		FreeVec(P.dm);
		P.dm = 0;
	}
	if (P.mint) {
		FreeVec(P.mint);
		P.mint = 0;
	}
	if (P.sint) {
		FreeVec(P.sint);
		P.sint = 0;
	}
}

BOOL paula14_audio_on(void)
{
	volatile struct Custom *c = CUSTOM;
	struct p14_data *dm;
	UWORD i;

	if (!P.open)
		return FALSE;
	if (P.on)
		return TRUE;

	read_calibration();
	P.period = DEF_PERIOD;
	if (P.wanted_freq)
		P.period = (P.clock + P.wanted_freq / 2) / P.wanted_freq;
	if (P.period < MIN_PERIOD)
		P.period = MIN_PERIOD;
	if (P.period > 65535)
		P.period = 65535;
	P.mixrate = P.clock / P.period;

	P.dm = (struct p14_data *)AllocVec(sizeof(struct p14_data), MEMF_PUBLIC | MEMF_CLEAR);
	P.chans = (struct p14_channel *)AllocVec(32 * sizeof(struct p14_channel),
	                                         MEMF_PUBLIC | MEMF_CLEAR);
	P.mint = (struct Interrupt *)AllocVec(sizeof(struct Interrupt), MEMF_PUBLIC | MEMF_CLEAR);
	P.sint = (struct Interrupt *)AllocVec(sizeof(struct Interrupt), MEMF_PUBLIC | MEMF_CLEAR);
	P.work = (LONG *)AllocVec(16 * BUFFLEN, MEMF_PUBLIC | MEMF_CLEAR);
	for (i = 0; i < 4; i++)
		P.chipbuf[i] = (BYTE *)AllocMem(BUFFLEN, MEMF_CHIP | MEMF_CLEAR);
	if (!P.dm || !P.chans || !P.mint || !P.sint || !P.work
	    || !P.chipbuf[0] || !P.chipbuf[1] || !P.chipbuf[2] || !P.chipbuf[3])
		goto fail;
	if (!claim_audio())
		goto fail;

	dm = P.dm;
	P.mint->is_Node.ln_Type = NT_INTERRUPT;
	P.mint->is_Node.ln_Name = (char *)"Play14_interrupt";
	P.mint->is_Data = dm;
	P.mint->is_Code = (void (*)())p14_intplay;
	P.sint->is_Node.ln_Type = NT_INTERRUPT;
	P.sint->is_Node.ln_Name = (char *)"Play14_slave_int";
	P.sint->is_Node.ln_Pri = 32;
	P.sint->is_Data = dm;
	P.sint->is_Code = (void (*)())p14_mixchannels;

	dm->channels = P.chans;
	dm->bufferwork = P.work;
	dm->buffercopy = (WORD *)((BYTE *)P.work + 8 * BUFFLEN);
	dm->bufferswap = (WORD *)((BYTE *)P.work + 12 * BUFFLEN);
	dm->calibration = (BYTE *)calibration + 128;
	for (i = 0; i < 4; i++)
		dm->chip[i] = P.chipbuf[i] + 4;
	dm->softint = P.sint;

	c->intena = INTF_AUD0 | INTF_AUD1 | INTF_AUD2 | INTF_AUD3;
	c->dmacon = DMAF_AUD0 | DMAF_AUD1 | DMAF_AUD2 | DMAF_AUD3;
	c->aud[2].ac_vol = 1;               /* right, low bits */
	c->aud[1].ac_vol = 64;              /* right, high byte */
	c->aud[3].ac_vol = 1;               /* left, low bits */
	c->aud[0].ac_vol = 64;              /* left, high byte */
	for (i = 0; i < 4; i++) {
		c->aud[i].ac_per = (UWORD)P.period;
		c->aud[i].ac_len = BUFFLEN / 2;
	}
	c->aud[3].ac_ptr = (UWORD *)P.chipbuf[0];
	c->aud[0].ac_ptr = (UWORD *)P.chipbuf[1];
	c->aud[2].ac_ptr = (UWORD *)P.chipbuf[2];
	c->aud[1].ac_ptr = (UWORD *)P.chipbuf[3];
	P.oldvec = SetIntVector(INTB_AUD1, P.mint);
	P.vector_set = TRUE;

	wait_lines(30);

	c->dmacon = DMAF_SETCLR | DMAF_AUD0 | DMAF_AUD1 | DMAF_AUD2 | DMAF_AUD3;
	c->intreq = INTF_AUD1;
	c->intena = INTF_SETCLR | INTF_AUD1;

	P.vbl_acc = 0;
	P.vbl.is_Node.ln_Type = NT_INTERRUPT;
	P.vbl.is_Node.ln_Pri = 0;
	P.vbl.is_Node.ln_Name = (char *)"paula14 tick";
	P.vbl.is_Data = 0;
	P.vbl.is_Code = (void (*)())vbl_isr;
	AddIntServer(INTB_VERTB, &P.vbl);
	P.vbl_added = TRUE;

	P.on = TRUE;
	return TRUE;

fail:
	release_audio();
	free_buffers();
	return FALSE;
}

BOOL paula14_is_on(void)
{
	return P.on;
}

void paula14_audio_off(void)
{
	volatile struct Custom *c = CUSTOM;
	WORD wait;

	if (P.vbl_added) {
		RemIntServer(INTB_VERTB, &P.vbl);
		P.vbl_added = FALSE;
	}
	if (P.vector_set) {
		c->dmacon = DMAF_AUD0 | DMAF_AUD1 | DMAF_AUD2 | DMAF_AUD3;
		c->intena = INTF_AUD0 | INTF_AUD1 | INTF_AUD2 | INTF_AUD3;
		SetIntVector(INTB_AUD1, P.oldvec);
		P.vector_set = FALSE;
		/* a mix that was already caused may still be running */
		for (wait = 0; wait < 10 && P.dm && P.dm->busy; wait++)
			Delay(1);
	}
	P.on = FALSE;
	release_audio();
	free_buffers();
}

APTR paula14_scopedata(void)
{
	return P.on ? (APTR)&P.dm->bufferswap : 0;
}

/* ---------------------------------------------------------------- samples */

ULONG paula14_load(const WORD *data, ULONG frames, BOOL stereo)
{
	UWORD i;

	if (!P.open || !data || frames == 0)
		return 0;
	for (i = 0; i < P.max_samples; i++) {
		if (!P.samples[i].data) {
			P.samples[i].data = data;
			P.samples[i].frames = frames;
			P.samples[i].stereo = stereo ? TRUE : FALSE;
			return (ULONG)i + 1;
		}
	}
	return 0;
}

/* stopselectedchan + forgetting the slot */
void paula14_unload(ULONG id)
{
	struct p14sample *s;

	if (!P.open || id == 0 || id > P.max_samples)
		return;
	s = &P.samples[id - 1];
	if (!s->data)
		return;
	if (P.on) {
		const WORD *end = s->data + s->frames * (s->stereo ? 2 : 1);
		ULONG mask = 0;
		UWORD n;

		for (n = 0; n < 32; n++)
			if (P.chans[n].smpend == end)
				mask |= 1UL << n;
		paula14_stop_mask(mask);
	}
	s->data = 0;
}

/* ---------------------------------------------------------------- voices */

void paula14_play(UWORD ch, ULONG id, LONG offset, LONG freq,
                  ULONG volume, ULONG pan, LONG loop)
{
	struct p14sample *s;
	struct p14_channel *c;
	ULONG frames, left, looplen = 0, bit;
	LONG skip, voll, volr;
	ULONG modulo;

	if (!P.on || ch >= 32 || id == 0 || id > P.max_samples)
		return;
	s = &P.samples[id - 1];
	if (!s->data)
		return;
	frames = s->frames;
	if (offset < 0)
		offset = 0;
	if ((ULONG)offset >= frames)
		return;
	left = frames - (ULONG)offset;
	if (loop >= 0)
		looplen = frames - ((ULONG)loop < frames ? (ULONG)loop : 0);
	rate(freq, &skip, &modulo);
	pan_volumes(volume, pan, &voll, &volr);

	c = &P.chans[ch];
	bit = 1UL << ch;
	Disable();
	P.dm->channelmask &= ~bit;
	c->envel.alt = FIXEDMAX;            /* constant 100% */
	c->envel.climb = 0;
	c->envel.hit = 0;
	c->envel.decay = 0;
	c->envel.susthit = 0;
	c->smpend = s->data + frames * (s->stereo ? 2 : 1);
	c->skip = skip;
	c->addmodulo = modulo;
	c->modulo = 0;
	c->stereo = s->stereo ? 1 : 0;
	c->volumel = voll;
	c->volumer = volr;
	c->current = -(LONG)left;
	if (looplen) {
		c->looplength = (LONG)looplen;
		c->loop = -1;
	} else {
		c->looplength = (LONG)left;
		c->loop = 0;
	}
	P.dm->channelmask |= bit;
	Enable();
}

void paula14_stop_mask(ULONG mask)
{
	if (!P.on)
		return;
	Disable();
	P.dm->channelmask &= ~mask;
	Enable();
}

void paula14_stop(UWORD ch)
{
	if (ch < 32)
		paula14_stop_mask(1UL << ch);
}

void paula14_set_volume(UWORD ch, ULONG volume, ULONG pan)
{
	LONG l, r;

	if (!P.on || ch >= 32)
		return;
	pan_volumes(volume, pan, &l, &r);
	Disable();
	P.chans[ch].volumel = l;
	P.chans[ch].volumer = r;
	Enable();
}

void paula14_set_freq(UWORD ch, LONG freq)
{
	LONG skip;
	ULONG modulo;

	if (!P.on || ch >= 32)
		return;
	rate(freq, &skip, &modulo);
	Disable();
	P.chans[ch].skip = skip;
	P.chans[ch].addmodulo = modulo;
	Enable();
}

ULONG paula14_free_voices(ULONG mask)
{
	if (!P.on)
		return mask;
	return ~P.dm->channelmask & mask;
}

void paula14_set_tick(void (*fn)(void))
{
	P.tick_cb = fn;
}
