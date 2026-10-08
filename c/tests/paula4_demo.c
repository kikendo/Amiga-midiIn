/*
 * paula4_demo.c - listening test for the native 4 channel Paula engine.
 *
 * Run it from a shell on an Amiga or in an emulator. What you should hear:
 *   1. four looped triangle waves entering one by one, voice 0 to 3
 *      (A4, C#5, E5, A5: an A major chord), held, then fading out together
 *   2. a short burst of noise on voice 0 that fades out (loudness only, the
 *      pitch does not change), played once. The program prints when the
 *      engine reports the voice free again; the burst lasts about half a
 *      second.
 *   3. a single triangle tone on voice 0 sliding up one octave over about
 *      two seconds, then back down, to check paula4_set_freq()
 */
#include <stdio.h>
#include <exec/types.h>
#include <exec/memory.h>
#include <proto/exec.h>
#include <proto/dos.h>

#include "../src/audio/paula4.h"

#define CYCLE   32UL            /* bytes in one triangle cycle */
#define BURST   4000UL

static const ULONG note_hz[4] = { 440, 554, 659, 880 };

/* sample data is built in Chip RAM so the engine can use it in place */
static BYTE *make_triangle(void)
{
	BYTE *p = (BYTE *)AllocMem(CYCLE, MEMF_CHIP | MEMF_CLEAR);
	UWORD i;

	if (!p)
		return 0;
	for (i = 0; i < CYCLE / 2; i++) {
		p[i] = (BYTE)(-120 + (i * 240) / (CYCLE / 2));
		p[CYCLE - 1 - i] = p[i];
	}
	return p;
}

static BYTE *make_burst(void)
{
	BYTE *p = (BYTE *)AllocMem(BURST, MEMF_CHIP | MEMF_CLEAR);
	ULONG i, seed = 12345;

	if (!p)
		return 0;
	for (i = 0; i < BURST; i++) {
		LONG r;

		seed = seed * 1103515245UL + 12345UL;
		r = (LONG)((seed >> 16) & 0xFF) - 128;
		p[i] = (BYTE)((r * (LONG)(BURST - i)) / (LONG)BURST);
	}
	return p;
}

static void say_free(const char *what)
{
	printf("%s: free voices = 0x%lX\n", what, (unsigned long)paula4_free_voices(0xF));
}

int main(void)
{
	BYTE *tri = make_triangle(), *burst = make_burst();
	ULONG tri_id, burst_id;
	UWORD i;
	LONG t;

	if (!tri || !burst) {
		printf("out of Chip RAM\n");
		goto done;
	}
	if (!paula4_open(8)) {
		printf("out of memory\n");
		goto done;
	}
	if (!paula4_audio_on()) {
		printf("could not open the audio hardware (audio.device busy?)\n");
		goto done;
	}
	tri_id = paula4_load(tri, CYCLE);
	burst_id = paula4_load(burst, BURST);
	if (!tri_id || !burst_id) {
		printf("sample load failed\n");
		goto done;
	}
	say_free("start");

	/* 1: chord */
	for (i = 0; i < 4; i++) {
		paula4_play(i, tri_id, 0, (LONG)(note_hz[i] * CYCLE), 65536 / 2, 0);
		Delay(40);
	}
	say_free("chord playing");
	Delay(100);
	for (t = 32; t >= 0; t--) {       /* the chord plays at half volume (32/64) */
		for (i = 0; i < 4; i++)
			paula4_set_volume(i, (ULONG)t << 10);
		Delay(1);
	}
	paula4_stop_mask(0xF);
	say_free("after stop");
	Delay(50);

	/* 2: one-shot, watch the voice free itself */
	paula4_play(0, burst_id, 0, 8000, 65536, -1);
	for (t = 0; t < 100; t++) {
		if (paula4_free_voices(1)) {
			printf("burst finished after %ld ticks (%ld ms), expected about 500 ms\n",
			       (long)t, (long)t * 20);
			break;
		}
		Delay(1);
	}
	if (t == 100)
		printf("burst never reported finished\n");
	Delay(50);

	/* 3: glide, A4 up to A5 and back, 100 steps each way at 50 Hz */
	paula4_play(0, tri_id, 0, (LONG)(440 * CYCLE), 65536 / 2, 0);
	Delay(25);
	for (t = 0; t <= 100; t++) {
		paula4_set_freq(0, (LONG)((440 + (440 * t) / 100) * CYCLE));
		Delay(1);
	}
	for (t = 100; t >= 0; t--) {
		paula4_set_freq(0, (LONG)((440 + (440 * t) / 100) * CYCLE));
		Delay(1);
	}
	Delay(25);
	paula4_stop(0);
	say_free("end");

done:
	paula4_close();
	if (tri)
		FreeMem(tri, CYCLE);
	if (burst)
		FreeMem(burst, BURST);
	return 0;
}
