/*
 * snd.h - sample playback for midiIn, independent of the audio engine
 *
 * C version of the snd_* interface of playAHI_custom.e, plus engine choice.
 * The volume envelopes and pitch glides run here at 50 Hz on top of whichever
 * engine is in use:
 *
 *   SND_AHI     ahi.device and the 32 voice mixer in softmix.s, 68020+
 *   SND_PAULA4  the four hardware channels, any CPU; one note per channel,
 *               no panning (channels 0 and 3 left, 1 and 2 right), no
 *               backward playback, mono samples up to 131070 frames
 *
 * Units: frequency in Hz (negative = backwards), volume 65536 = 100%,
 * pan 0 = left .. 65536 = right, sample data 16-bit signed, sample ids
 * 1-based, channel masks with bit n = channel n. Times: envelope and release
 * values in tenths of a second, glide time in ticks of 1/50 s.
 */
#ifndef SND_H
#define SND_H

#include <exec/types.h>

enum snd_engine {
	SND_AUTO,          /* AHI if it works, otherwise Paula 4 channel */
	SND_AHI,
	SND_PAULA4
};

struct snd_envelope {
	UBYTE attack;      /* tenths of a second, 0 = instant */
	UBYTE decay;       /* tenths of a second, 0 = instant */
	UBYTE sustain;     /* level 0..255, 0 = hold until release */
};

/* Picks and initialises an engine. FALSE if it cannot be used (no ahi.device,
 * CPU below 68020 for AHI, no memory). */
BOOL  snd_init(enum snd_engine engine, UWORD maxsamples);
void  snd_end(void);
const char *snd_engine_name(void);
UWORD snd_voices(void);                 /* channels the engine has */

BOOL  snd_audioon(void);
void  snd_audiooff(void);
BOOL  snd_is_on(void);
void  snd_setaudioid(ULONG audioid);   /* AHI only, applies at audio on */
void  snd_setmixfreq(ULONG mixfreq);   /* AHI only, applies at audio on */
void  snd_setnumchannels(UWORD channels);

ULONG snd_setsample(const WORD *address, ULONG frames, BOOL stereo);
void  snd_delsample(ULONG id);

void  snd_playsample(UWORD channel, ULONG id, LONG offset, LONG freq,
                     ULONG volume, ULONG pan, LONG loop,
                     const struct snd_envelope *env);
void  snd_stopchannel(UWORD channel);
void  snd_stopchannelmask(ULONG mask);
void  snd_setvolume(UWORD channel, ULONG volume, ULONG pan);
void  snd_release(UWORD channel, UWORD tenths);
void  snd_setfreq(UWORD channel, LONG uptime, LONG destfreq);
ULONG snd_freechannels(ULONG mask);

/* For the scopes (AHI only, NULL otherwise): points to a pointer to the
 * last mixed buffer of snd_scopelen stereo 16-bit frames. */
extern volatile APTR snd_scopedata;
extern ULONG snd_scopelen;

#endif /* SND_H */
