/*
 * softmix.h - C interface to softmix.s, the 32 voice software mixer
 * (68020 or better).
 *
 * Samples are 16-bit signed, mono or interleaved stereo. Channel numbers are
 * 0..31, sample numbers are 1-based. volume: 65536 = 100%. pan: 0 = left,
 * 65536 = right. A negative frequency plays backwards; a negative loop start
 * is a one-shot.
 */
#ifndef SOFTMIX_H
#define SOFTMIX_H

#include <exec/types.h>

/* buffer: where mx_mix() writes bufferframes stereo 16-bit frames. The first
 * long of the returned data is that output pointer and may be changed
 * between calls (double buffering). */
APTR  mx_setup(ULONG mixfreq, ULONG channels, ULONG maxsamples,
               APTR buffer, ULONG bufferframes);
void  mx_end(APTR data);
ULONG mx_load(APTR address, ULONG frames, ULONG stereo, APTR data);
void  mx_unload(ULONG samplenum, APTR data);
void  mx_setmixperiod(ULONG mixfreq, APTR data);
void  mx_playchannel(LONG offset, LONG freq, ULONG volume, ULONG pan,
                     LONG loop, ULONG channel, ULONG samplenum, APTR data);
void  mx_stopchannel(ULONG channel, APTR data);
void  mx_stopchannelmask(ULONG mask, APTR data);       /* bit n = channel n */
ULONG mx_freechannels(ULONG mask, APTR data);          /* bit n set = idle */
void  mx_setfrequency(LONG freq, ULONG channel, APTR data);
void  mx_setvolume(ULONG volume, ULONG pan, ULONG channel, APTR data);
void  mx_mix(APTR data);

#endif /* SOFTMIX_H */
