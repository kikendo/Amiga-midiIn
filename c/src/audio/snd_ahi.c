/*
 * snd_ahi.c - AHI engine: softmix.s mixes 32 voices into a double buffer
 * that AHI plays as one dynamic sample. Port of the device and buffer
 * handling in playAHI_custom.e. Needs a 68020 or better (softmix.s).
 *
 * The buffer holds two halves of BUFFRAMES stereo 16-bit frames. Each time
 * AHI starts playing a half it calls the sound hook, which queues the other
 * half and mixes into it. The player hook runs at 50 Hz and drives the
 * voice control in snd.c.
 */
#include <exec/types.h>
#include <exec/memory.h>
#include <exec/execbase.h>
#include <utility/hooks.h>
#include <utility/tagitem.h>
#include <devices/ahi.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/ahi.h>

#include "snd.h"
#include "snd_backend.h"
#include "softmix.h"

#define BUFFRAMES   256UL
#define MIXCHANNELS 32
#define TICK_HZ     50

extern ULONG snd_hook_entry(void);          /* hookentry.s */

struct Library *AHIBase;

static struct {
	struct MsgPort      *port;
	struct AHIRequest   *io;
	BOOL                 device_open;
	struct AHIAudioCtrl *ctrl;
	APTR                 mx;                /* mixer data */
	LONG                *outbuf;            /* 2 * BUFFRAMES stereo frames */
	ULONG                swap;              /* half being filled: 0 or BUFFRAMES */
	volatile ULONG       cntmix;
	ULONG                audioid;
	ULONG                mixfreq;
	struct Hook          soundhook;
	struct Hook          playerhook;
	void               (*tick)(void);
} A;

/* ------------------------------------------------------------------ hooks */

static ULONG sound_func(struct Hook *h, struct AHIAudioCtrl *ctrl, APTR msg)
{
	(void)h;
	(void)msg;
	A.cntmix++;
	A.swap ^= BUFFRAMES;
	AHI_SetSound(0, 0, A.swap, BUFFRAMES, ctrl, 0);
	*(LONG **)A.mx = A.outbuf + A.swap;     /* DT_OUTSAMPLEDATA */
	snd_scopedata = A.mx;
	mx_mix(A.mx);
	return 0;
}

static ULONG player_func(struct Hook *h, struct AHIAudioCtrl *ctrl, APTR msg)
{
	(void)h;
	(void)ctrl;
	(void)msg;
	if (A.tick)
		A.tick();
	return 0;
}

/* ------------------------------------------------------------ init / end */

static void ahi_end(void);
static BOOL ahi_audio_on(void);
static void free_audio(BOOL settle);

static BOOL ahi_init(UWORD max_samples)
{
	if (A.mx)
		return TRUE;
	if (!(SysBase->AttnFlags & AFF_68020))
		return FALSE;                       /* softmix.s is 68020 code */
	if (!A.mixfreq)
		A.mixfreq = 28000;

	A.outbuf = (LONG *)AllocVec(2 * BUFFRAMES * 4, MEMF_PUBLIC | MEMF_CLEAR);
	if (!A.outbuf)
		goto fail;
	A.mx = mx_setup(A.mixfreq, MIXCHANNELS, max_samples, A.outbuf, BUFFRAMES);
	if (!A.mx)
		goto fail;

	A.port = CreateMsgPort();
	if (!A.port)
		goto fail;
	A.io = (struct AHIRequest *)CreateIORequest(A.port, sizeof(struct AHIRequest));
	if (!A.io)
		goto fail;
	A.io->ahir_Version = 4;
	if (OpenDevice((CONST_STRPTR)AHINAME, AHI_NO_UNIT, (struct IORequest *)A.io, 0) != 0)
		goto fail;
	A.device_open = TRUE;
	AHIBase = (struct Library *)A.io->ahir_Std.io_Device;

	A.soundhook.h_Entry = (APTR)snd_hook_entry;
	A.soundhook.h_SubEntry = (APTR)sound_func;
	A.playerhook.h_Entry = (APTR)snd_hook_entry;
	A.playerhook.h_SubEntry = (APTR)player_func;

	/* ahi.device can be installed without a working audio mode; only count
	 * AHI as usable once it has actually played */
	if (!ahi_audio_on())
		goto fail;
	free_audio(FALSE);
	return TRUE;

fail:
	ahi_end();
	return FALSE;
}

static void ahi_audio_off(void);

static void ahi_end(void)
{
	if (A.ctrl)
		ahi_audio_off();
	if (A.device_open) {
		CloseDevice((struct IORequest *)A.io);
		A.device_open = FALSE;
		AHIBase = 0;
	}
	if (A.io) {
		DeleteIORequest((struct IORequest *)A.io);
		A.io = 0;
	}
	if (A.port) {
		DeleteMsgPort(A.port);
		A.port = 0;
	}
	if (A.mx) {
		mx_end(A.mx);
		A.mx = 0;
	}
	if (A.outbuf) {
		FreeVec(A.outbuf);
		A.outbuf = 0;
	}
}

/* ---------------------------------------------------------- audio on/off */

static BOOL ahi_audio_on(void)
{
	struct AHISampleInfo si;
	struct TagItem alloc_tags[] = {
		{ AHIA_AudioID,       0 },
		{ AHIA_MixFreq,       0 },
		{ AHIA_Channels,      1 },
		{ AHIA_Sounds,        1 },
		{ AHIA_SoundFunc,     0 },
		{ AHIA_PlayerFunc,    0 },
		{ AHIA_PlayerFreq,    TICK_HZ << 16 },
		{ AHIA_MinPlayerFreq, (TICK_HZ << 16) - 65536 },
		{ AHIA_MaxPlayerFreq, (TICK_HZ << 16) + 65536 },
		{ AHIA_UserData,      0 },
		{ TAG_DONE,           0 }
	};
	struct TagItem ctrl_tags[] = {
		{ AHIC_Play,          TRUE },
		{ AHIC_MixFreq_Query, 0 },
		{ TAG_DONE,           0 }
	};
	struct TagItem play_tags[] = {
		{ AHIP_BeginChannel,  0 },
		{ AHIP_Freq,          AHI_MIXFREQ },
		{ AHIP_Vol,           0x10000 },
		{ AHIP_Pan,           0x8000 },
		{ AHIP_Sound,         0 },
		{ AHIP_Offset,        0 },
		{ AHIP_Length,        BUFFRAMES },
		{ AHIP_EndChannel,    0 },
		{ TAG_DONE,           0 }
	};

	if (!A.device_open)
		return FALSE;
	if (A.ctrl)
		ahi_audio_off();

	alloc_tags[0].ti_Data = A.audioid;
	alloc_tags[1].ti_Data = A.mixfreq;
	alloc_tags[4].ti_Data = (ULONG)&A.soundhook;
	alloc_tags[5].ti_Data = (ULONG)&A.playerhook;
	A.ctrl = AHI_AllocAudioA(alloc_tags);
	if (!A.ctrl)
		return FALSE;

	ctrl_tags[1].ti_Data = (ULONG)&A.mixfreq;
	if (AHI_ControlAudioA(A.ctrl, ctrl_tags) != AHIE_OK)
		goto fail;
	mx_setmixperiod(A.mixfreq, A.mx);

	{
		ULONG i;

		for (i = 0; i < 2 * BUFFRAMES; i++)
			A.outbuf[i] = 0;
	}
	si.ahisi_Type = AHIST_S16S;
	si.ahisi_Address = A.outbuf;
	si.ahisi_Length = 2 * BUFFRAMES;
	if (AHI_LoadSound(0, AHIST_DYNAMICSAMPLE, &si, A.ctrl) != AHIE_OK)
		goto fail;

	snd_scopelen = BUFFRAMES;
	play_tags[5].ti_Data = A.swap;
	A.cntmix = 0;
	AHI_PlayA(A.ctrl, play_tags);

	/* the sound hook must start firing, otherwise the mode is not working
	 * (missing driver, no hardware); wait up to half a second */
	{
		WORD wait;

		for (wait = 0; wait < 25 && A.cntmix < 2; wait++)
			Delay(1);
		if (A.cntmix < 2)
			goto fail;
	}
	return TRUE;

fail:
	free_audio(FALSE);
	return FALSE;
}

/* settle: let two more buffers be mixed before freeing, so channels stopped
 * just before are silent in what AHI still plays; gives up after a second */
static void free_audio(BOOL settle)
{
	struct AHIAudioCtrl *c = A.ctrl;
	WORD wait;

	if (c) {
		if (settle) {
			A.cntmix = 0;
			for (wait = 0; wait < 50 && A.cntmix < 2; wait++)
				Delay(1);
		}
		A.ctrl = 0;
		AHI_FreeAudio(c);
	}
	snd_scopedata = 0;
}

static void ahi_audio_off(void)
{
	free_audio(TRUE);
}

static BOOL ahi_is_on(void)
{
	return A.ctrl != 0;
}

/* ------------------------------------------------------- samples, voices */

static ULONG ahi_load(const WORD *data, ULONG frames, BOOL stereo)
{
	if (!A.device_open)
		return 0;
	return mx_load((APTR)data, frames, stereo ? 1 : 0, A.mx);
}

static void ahi_unload(ULONG id)
{
	if (A.device_open)
		mx_unload(id, A.mx);
}

static void ahi_play(UWORD ch, ULONG id, LONG offset, LONG freq,
                     ULONG volume, ULONG pan, LONG loop)
{
	mx_playchannel(offset, freq, volume, pan, loop, ch, id, A.mx);
}

static void ahi_stop(UWORD ch)
{
	if (A.mx)
		mx_stopchannel(ch, A.mx);
}

static void ahi_stop_mask(ULONG mask)
{
	if (A.mx)
		mx_stopchannelmask(mask, A.mx);
}

static void ahi_set_freq(UWORD ch, LONG freq)
{
	mx_setfrequency(freq, ch, A.mx);
}

static void ahi_set_volume(UWORD ch, ULONG volume, ULONG pan)
{
	mx_setvolume(volume, pan, ch, A.mx);
}

static ULONG ahi_free_mask(ULONG mask)
{
	return A.mx ? mx_freechannels(mask, A.mx) : mask;
}

static void ahi_set_tick(void (*fn)(void))
{
	A.tick = fn;
}

static void ahi_set_audioid(ULONG id)
{
	if (id != AHI_INVALID_ID)
		A.audioid = id;
}

static void ahi_set_mixfreq(ULONG freq)
{
	if (freq)
		A.mixfreq = freq;
}

const struct snd_backend snd_backend_ahi = {
	"AHI",
	MIXCHANNELS,
	ahi_init,
	ahi_end,
	ahi_audio_on,
	ahi_audio_off,
	ahi_is_on,
	ahi_load,
	ahi_unload,
	ahi_play,
	ahi_stop,
	ahi_stop_mask,
	ahi_set_freq,
	ahi_set_volume,
	ahi_free_mask,
	ahi_set_tick,
	ahi_set_audioid,
	ahi_set_mixfreq,
	0
};
