/*
 * paula14.h - 14-bit Paula engine, 68020 or better
 *
 * Mixes up to 32 voices in software (play14.s) and plays the result in
 * 14-bit stereo through all four audio channels, without AHI. Port of
 * play14unlim.e and the audio parts of soundfx.e.
 *
 * Samples are 16-bit signed, mono or interleaved stereo, in any memory; they
 * are played in place and must stay valid until unloaded. Units as in
 * snd_backend.h: freq in Hz, volume 65536 = 100%, pan 0 = right .. 65536 =
 * left. Backward playback is not supported (a negative frequency plays
 * forwards).
 *
 * If ENVARC:CyberSound/SoundDrivers/14Bit_Calibration exists (256 bytes, as
 * written by the CyberSound calibration tool) it is used, otherwise an
 * uncalibrated table.
 */
#ifndef PAULA14_H
#define PAULA14_H

#include <exec/types.h>

#define PAULA14_VOICES 32

BOOL  paula14_open(UWORD max_samples);  /* FALSE below 68020 or no memory */
void  paula14_close(void);
BOOL  paula14_audio_on(void);           /* claims the four audio channels */
void  paula14_audio_off(void);
BOOL  paula14_is_on(void);

/* output rate in Hz, applied at the next audio on (default about 28 kHz) */
void  paula14_set_mixfreq(ULONG freq);

ULONG paula14_load(const WORD *data, ULONG frames, BOOL stereo);
void  paula14_unload(ULONG id);

void  paula14_play(UWORD ch, ULONG id, LONG offset, LONG freq,
                   ULONG volume, ULONG pan, LONG loop);
void  paula14_stop(UWORD ch);
void  paula14_stop_mask(ULONG mask);
void  paula14_set_volume(UWORD ch, ULONG volume, ULONG pan);
void  paula14_set_freq(UWORD ch, LONG freq);
ULONG paula14_free_voices(ULONG mask);

/* called at 50 Hz from the vertical blank interrupt while the audio is on */
void  paula14_set_tick(void (*fn)(void));

/* for the scopes: pointer to a pointer to the last mixed buffer of
 * PAULA14_SCOPELEN stereo 16-bit frames, NULL while the audio is off */
APTR  paula14_scopedata(void);
#define PAULA14_SCOPELEN 256

#endif /* PAULA14_H */
