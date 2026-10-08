/*
 * paula4.c - native 4 channel Paula engine, see paula4.h
 *
 * How a note starts: the voice's DMA is switched off, pointer, length,
 * period and volume are written, DMA is switched on, and after the hardware
 * has latched the first block the pointer/length are rewritten with what
 * Paula should play when that block ends: the loop region for a looped
 * sample, a short block of silence for a one-shot. Writing them any earlier
 * would be picked up by the first fetch instead.
 *
 * Whether a one-shot has finished is not read from the hardware: the 50 Hz
 * tick counts the frames the voice has played at its current rate and frees
 * the voice one tick after the sample must have ended, so a new note can
 * never cut the tail off.
 */
#include <exec/types.h>
#include <exec/memory.h>
#include <exec/interrupts.h>
#include <exec/execbase.h>
#include <devices/audio.h>
#include <hardware/custom.h>
#include <hardware/dmabits.h>
#include <hardware/intbits.h>
#include <proto/exec.h>

#include "paula4.h"

#define CUSTOM        ((volatile struct Custom *)0xDFF000)

#define PAL_CLOCK     3546895UL
#define NTSC_CLOCK    3579545UL
#define MIN_PERIOD    113UL          /* same lower limit as ProTracker */
#define MAX_PERIOD    65535UL
#define LATCH_LINES   5              /* raster lines to let DMA latch */
#define TICK_HZ       50
#define SILENCE_BYTES 4

struct p4sample {
	BYTE  *data;                 /* NULL: free slot */
	ULONG  frames;               /* length in bytes */
	ULONG  bytes;                /* frames rounded up to even */
	BOOL   owned;                /* we allocated data */
};

struct p4voice {
	struct p4sample *sample;
	volatile BOOL    busy;
	BOOL             looped;
	volatile LONG    left;       /* frames still to play, one-shots */
	ULONG            ticklen;    /* frames played per tick */
};

static struct {
	BOOL             open;
	struct p4sample *samples;
	UWORD            max_samples;
	struct p4voice   v[PAULA4_VOICES];
	ULONG            clock;
	WORD             vbl_rate;
	WORD             vbl_acc;
	void           (*tick_cb)(void);
	BYTE            *silence;
	struct MsgPort  *port;
	struct IOAudio  *io;
	BOOL             device_open;
	struct Interrupt vbl;
	BOOL             vbl_added;
} P;

/* ---------------------------------------------------------------- helpers */

static UWORD freq_to_period(LONG freq)
{
	ULONG f = (ULONG)(freq < 0 ? -freq : freq);
	ULONG p;

	if (f == 0)
		f = 1;
	p = (P.clock + f / 2) / f;
	if (p < MIN_PERIOD)
		p = MIN_PERIOD;
	if (p > MAX_PERIOD)
		p = MAX_PERIOD;
	return (UWORD)p;
}

static ULONG period_to_ticklen(UWORD period)
{
	ULONG t = (P.clock / period) / TICK_HZ;

	return t ? t : 1;
}

static UWORD volume64(ULONG volume)
{
	ULONG v = volume >> 10;                 /* 65536 -> 64 */

	return (UWORD)(v > 64 ? 64 : v);
}

static void dma_off(UWORD voice)
{
	CUSTOM->dmacon = (UWORD)(DMAF_AUD0 << voice);
}

static void dma_on(UWORD voice)
{
	CUSTOM->dmacon = (UWORD)(DMAF_SETCLR | (DMAF_AUD0 << voice));
}

static void wait_lines(UWORD n)
{
	while (n--) {
		UWORD line = (UWORD)(CUSTOM->vhposr >> 8);

		while ((UWORD)(CUSTOM->vhposr >> 8) == line)
			;
	}
}

/* ------------------------------------------------------------------- tick */

static void p4_tick(void)
{
	UWORD i;

	for (i = 0; i < PAULA4_VOICES; i++) {
		struct p4voice *v = &P.v[i];

		if (v->busy && !v->looped) {
			v->left -= (LONG)v->ticklen;
			/* one extra tick of margin, see the file comment */
			if (v->left <= -(LONG)v->ticklen) {
				dma_off(i);
				v->busy = FALSE;
			}
		}
	}
	if (P.tick_cb)
		P.tick_cb();
}

/* exec calls this with A1 = is_Data and A6 = ExecBase; neither is needed.
 * A server must return with Z set when it did not handle the interrupt
 * exclusively: returning 0 lets the rest of the vblank chain run. */
static ULONG vbl_isr(void)
{
	P.vbl_acc += TICK_HZ;
	if (P.vbl_acc >= P.vbl_rate) {
		P.vbl_acc -= P.vbl_rate;
		p4_tick();
	}
	return 0;
}

/* ---------------------------------------------------------- open / close */

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
	P.io->ioa_Request.io_Flags = ADIOF_NOWAIT;  /* fail instead of hanging */
	P.io->ioa_AllocKey = 0;
	P.io->ioa_Data = map;
	P.io->ioa_Length = sizeof(map);

	if (OpenDevice((CONST_STRPTR)"audio.device", 0,
	               (struct IORequest *)P.io, 0) != 0)
		return FALSE;
	P.device_open = TRUE;
	return TRUE;
}

BOOL paula4_open(UWORD max_samples)
{
	UWORD i;

	if (P.open)
		return TRUE;
	if (max_samples == 0)
		return FALSE;

	P.max_samples = max_samples;
	P.samples = (struct p4sample *)AllocVec(
		(ULONG)max_samples * sizeof(struct p4sample), MEMF_PUBLIC | MEMF_CLEAR);
	P.silence = (BYTE *)AllocMem(SILENCE_BYTES, MEMF_CHIP | MEMF_CLEAR);
	if (!P.samples || !P.silence)
		goto fail;

	P.vbl_rate = (SysBase->VBlankFrequency == 50) ? 50 : 60;
	P.clock = (SysBase->VBlankFrequency == 50) ? PAL_CLOCK : NTSC_CLOCK;
	P.vbl_acc = 0;
	P.tick_cb = 0;
	for (i = 0; i < PAULA4_VOICES; i++) {
		P.v[i].sample = 0;
		P.v[i].busy = FALSE;
	}

	if (!claim_audio())
		goto fail;
	CUSTOM->dmacon = DMAF_AUD0 | DMAF_AUD1 | DMAF_AUD2 | DMAF_AUD3;

	P.vbl.is_Node.ln_Type = NT_INTERRUPT;
	P.vbl.is_Node.ln_Pri = 0;
	P.vbl.is_Node.ln_Name = (char *)"paula4 tick";
	P.vbl.is_Data = 0;
	P.vbl.is_Code = (void (*)())vbl_isr;
	AddIntServer(INTB_VERTB, &P.vbl);
	P.vbl_added = TRUE;

	P.open = TRUE;
	return TRUE;

fail:
	paula4_close();
	return FALSE;
}

BOOL paula4_is_open(void)
{
	return P.open;
}

void paula4_close(void)
{
	UWORD i;

	if (P.vbl_added) {
		RemIntServer(INTB_VERTB, &P.vbl);
		P.vbl_added = FALSE;
	}
	if (P.device_open) {
		CUSTOM->dmacon = DMAF_AUD0 | DMAF_AUD1 | DMAF_AUD2 | DMAF_AUD3;
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
	if (P.samples) {
		for (i = 0; i < P.max_samples; i++) {
			if (P.samples[i].data && P.samples[i].owned)
				FreeMem(P.samples[i].data, P.samples[i].bytes);
		}
		FreeVec(P.samples);
		P.samples = 0;
	}
	if (P.silence) {
		FreeMem(P.silence, SILENCE_BYTES);
		P.silence = 0;
	}
	P.max_samples = 0;
	P.open = FALSE;
}

/* ---------------------------------------------------------------- samples */

ULONG paula4_load(const BYTE *data, ULONG frames)
{
	struct p4sample *s = 0;
	UWORD i;
	ULONG bytes;

	if (!P.open || !data || frames < 2 || frames > PAULA4_MAX_BYTES)
		return 0;
	for (i = 0; i < P.max_samples; i++) {
		if (!P.samples[i].data) {
			s = &P.samples[i];
			break;
		}
	}
	if (!s)
		return 0;

	bytes = (frames + 1) & ~1UL;
	if ((TypeOfMem((APTR)data) & MEMF_CHIP) && (((ULONG)data & 1) == 0)
	    && bytes == frames) {
		s->data = (BYTE *)data;
		s->owned = FALSE;
	} else {
		BYTE *chip = (BYTE *)AllocMem(bytes, MEMF_CHIP | MEMF_CLEAR);

		if (!chip)
			return 0;
		CopyMem((APTR)data, chip, frames);
		s->data = chip;
		s->owned = TRUE;
	}
	s->frames = frames;
	s->bytes = bytes;
	return (ULONG)i + 1;
}

void paula4_unload(ULONG id)
{
	struct p4sample *s;
	UWORD n;

	if (!P.open || id == 0 || id > P.max_samples)
		return;
	s = &P.samples[id - 1];
	if (!s->data)
		return;
	for (n = 0; n < PAULA4_VOICES; n++) {
		if (P.v[n].sample == s)
			paula4_stop(n);
	}
	if (s->owned)
		FreeMem(s->data, s->bytes);
	s->data = 0;
	s->owned = FALSE;
}

/* ---------------------------------------------------------------- control */

void paula4_play(UWORD voice, ULONG id, LONG offset, LONG freq,
                 ULONG volume, LONG loop)
{
	struct p4sample *s;
	struct p4voice *v;
	volatile struct AudChannel *ch;
	ULONG start;
	UWORD period;
	BOOL looped = (loop >= 0);

	if (!P.open || voice >= PAULA4_VOICES || id == 0 || id > P.max_samples)
		return;
	s = &P.samples[id - 1];
	if (!s->data)
		return;

	if (offset < 0)
		offset = 0;
	start = (ULONG)offset & ~1UL;
	if (start >= s->frames)
		return;                         /* nothing left to play */

	v = &P.v[voice];
	ch = &CUSTOM->aud[voice];
	period = freq_to_period(freq);

	Disable();
	dma_off(voice);
	v->busy = FALSE;
	v->sample = s;
	v->looped = looped;
	v->left = (LONG)(s->frames - start);
	v->ticklen = period_to_ticklen(period);
	ch->ac_ptr = (UWORD *)(s->data + start);
	ch->ac_len = (UWORD)((s->bytes - start) >> 1);
	ch->ac_per = period;
	ch->ac_vol = volume64(volume);
	dma_on(voice);
	v->busy = TRUE;
	Enable();

	wait_lines(LATCH_LINES);

	if (looped) {
		ULONG ls = (ULONG)loop & ~1UL;

		if (ls >= s->frames)
			ls = 0;
		ch->ac_ptr = (UWORD *)(s->data + ls);
		ch->ac_len = (UWORD)((s->bytes - ls) >> 1);
	} else {
		ch->ac_ptr = (UWORD *)P.silence;
		ch->ac_len = SILENCE_BYTES / 2;
	}
}

void paula4_stop(UWORD voice)
{
	if (!P.open || voice >= PAULA4_VOICES)
		return;
	Disable();
	dma_off(voice);
	P.v[voice].busy = FALSE;
	Enable();
}

void paula4_stop_mask(ULONG mask)
{
	UWORD i;

	for (i = 0; i < PAULA4_VOICES; i++) {
		if (mask & (1UL << i))
			paula4_stop(i);
	}
}

void paula4_set_volume(UWORD voice, ULONG volume)
{
	if (!P.open || voice >= PAULA4_VOICES)
		return;
	CUSTOM->aud[voice].ac_vol = volume64(volume);
}

void paula4_set_freq(UWORD voice, LONG freq)
{
	UWORD period;

	if (!P.open || voice >= PAULA4_VOICES)
		return;
	period = freq_to_period(freq);
	Disable();
	P.v[voice].ticklen = period_to_ticklen(period);
	CUSTOM->aud[voice].ac_per = period;
	Enable();
}

ULONG paula4_free_voices(ULONG mask)
{
	ULONG free_mask = 0;
	UWORD i;

	for (i = 0; i < PAULA4_VOICES; i++) {
		if (!P.v[i].busy)
			free_mask |= 1UL << i;
	}
	return free_mask & mask;
}

void paula4_set_tick(void (*fn)(void))
{
	P.tick_cb = fn;
}
