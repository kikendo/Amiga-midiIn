/*
 * snd_backend.h - what an audio engine provides to snd.c
 *
 * Units are the same for every engine: frequencies in Hz (negative =
 * backwards, if the engine can), volume 65536 = 100%, pan 0 = right ..
 * 65536 = left, sample data 16-bit signed, sample ids 1-based, channel
 * masks with bit n = channel n.
 */
#ifndef SND_BACKEND_H
#define SND_BACKEND_H

#include <exec/types.h>

struct snd_backend {
	const char *name;
	UWORD voices;                          /* channels 0 .. voices-1 */

	BOOL  (*init)(UWORD max_samples);      /* samples can be loaded after */
	void  (*end)(void);
	BOOL  (*audio_on)(void);
	void  (*audio_off)(void);
	BOOL  (*is_on)(void);

	ULONG (*load)(const WORD *data, ULONG frames, BOOL stereo);
	void  (*unload)(ULONG id);

	void  (*play)(UWORD ch, ULONG id, LONG offset, LONG freq,
	              ULONG volume, ULONG pan, LONG loop);
	void  (*stop)(UWORD ch);
	void  (*stop_mask)(ULONG mask);
	void  (*set_freq)(UWORD ch, LONG freq);
	void  (*set_volume)(UWORD ch, ULONG volume, ULONG pan);
	ULONG (*free_mask)(ULONG mask);

	/* fn is called 50 times a second while the audio is on, from interrupt
	 * or audio task context */
	void  (*set_tick)(void (*fn)(void));

	/* engine options, NULL if the engine has none */
	void  (*set_audioid)(ULONG id);
	void  (*set_mixfreq)(ULONG freq);

	/* refreshes the snd_scopedata buffer before it is read; NULL if the
	 * engine keeps it up to date itself */
	void  (*scope_update)(void);
};

extern const struct snd_backend snd_backend_ahi;
extern const struct snd_backend snd_backend_paula4;
extern const struct snd_backend snd_backend_paula14;

#endif /* SND_BACKEND_H */
