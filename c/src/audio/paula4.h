/*
 * paula4.h - native 4 channel Paula engine
 *
 * Plays 8-bit samples straight through the four Paula DMA channels, no
 * software mixing, so it runs on a plain 68000. The CPU only writes
 * hardware registers.
 *
 * Voices map 1:1 to the hardware channels. Paula wires channels 0 and 3 to
 * the left output and 1 and 2 to the right, so stereo position is decided by
 * which voice a note is played on, not by a pan value.
 *
 * Samples: signed 8-bit mono, at most PAULA4_MAX_BYTES long. They are played
 * from Chip RAM; data that is not already in Chip RAM is copied there on load.
 *
 * Limits of this first version, all deliberate:
 *   - no backward playback (a negative frequency is played forwards)
 *   - no stereo or 16-bit samples
 *   - samples longer than one DMA run (PAULA4_MAX_BYTES) are refused
 *   - loop start and play offset are rounded down to an even byte
 */
#ifndef PAULA4_H
#define PAULA4_H

#include <exec/types.h>

#define PAULA4_VOICES     4
#define PAULA4_MAX_BYTES  131070UL    /* AUDxLEN is a 16 bit word count */

/*
 * paula4_open() sets up the sample table (max_samples entries); samples can
 * be loaded from then on. paula4_audio_on() claims all four audio channels
 * through audio.device and starts the 50 Hz tick; paula4_audio_off() hands
 * them back, the samples stay loaded. paula4_close() does both and frees
 * the samples. The open calls return FALSE on failure.
 */
BOOL paula4_open(UWORD max_samples);
void paula4_close(void);
BOOL paula4_is_open(void);
BOOL paula4_audio_on(void);
void paula4_audio_off(void);
BOOL paula4_is_on(void);

/*
 * Registers a sample. frames is the length in bytes. Returns a 1-based sample
 * id, or 0 on failure (table full, too long, no Chip RAM). If data is already
 * in Chip RAM, even aligned and of even length it is used in place and must
 * stay valid until paula4_unload(); otherwise a private copy is made.
 */
ULONG paula4_load(const BYTE *data, ULONG frames);
/* same, from 16-bit mono data: the high byte of each frame is kept, always
 * into a private Chip RAM copy */
ULONG paula4_load16(const WORD *data, ULONG frames);
void  paula4_unload(ULONG id);          /* stops any voice playing it */

/*
 * freq is the playback rate in Hz (sample rate times pitch ratio) and is
 * turned into a Paula period. volume: 65536 = 100% (Paula has 64 steps).
 * loop is the loop start in bytes, or negative for a one-shot.
 */
void paula4_play(UWORD voice, ULONG id, LONG offset, LONG freq,
                 ULONG volume, LONG loop);
void paula4_stop(UWORD voice);
void paula4_stop_mask(ULONG mask);      /* bit n = voice n */
void paula4_set_volume(UWORD voice, ULONG volume);
void paula4_set_freq(UWORD voice, LONG freq);

/* bit n set: voice n is idle (and was asked for in mask) */
ULONG paula4_free_voices(ULONG mask);

/*
 * Called at 50 Hz from the vertical blank interrupt (also on NTSC), after the
 * engine's own bookkeeping, while the audio is on. Runs in interrupt context: keep it short and do
 * not call exec functions that may wait. NULL to clear.
 */
void paula4_set_tick(void (*fn)(void));

#endif /* PAULA4_H */
