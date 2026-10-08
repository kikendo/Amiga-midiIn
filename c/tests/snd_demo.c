/*
 * snd_demo.c - listening test for the engine-independent snd_* layer.
 *
 *   snd_demo          AHI if it works, otherwise Paula 4 channel
 *   snd_demo ahi      AHI only (68020+, ahi.device)
 *   snd_demo paula    Paula 4 channel only
 *
 * What you should hear:
 *   1. an A major chord, four voices, fading in over about 0.3 s, settling a
 *      little quieter, held, then fading out over 1 s. With AHI the voices
 *      are spread across the stereo field; with Paula, voices 0 and 3 are
 *      left and 1 and 2 right.
 *   2. one tone sliding up an octave over 2 s and back down over 2 s
 *   3. AHI only: eight voices at once (more than Paula can play), a wide
 *      chord across the stereo field, fading out over 2 s
 */
#include <stdio.h>
#include <string.h>
#include <exec/types.h>
#include <exec/memory.h>
#include <proto/exec.h>
#include <proto/dos.h>

#include "../src/audio/snd.h"

#define CYCLE  32UL                      /* frames in one triangle cycle */

static const ULONG chord_hz[4] = { 440, 554, 659, 880 };
static const ULONG pans[4] = { 0, 65536, 16384, 49152 };
static const ULONG wide_hz[8] = { 131, 196, 262, 330, 392, 494, 587, 740 };

static void say_free(const char *what)
{
	printf("%s: free channels = 0x%08lX\n", what,
	       (unsigned long)snd_freechannels(0xFFFFFFFFUL));
}

int main(int argc, char **argv)
{
	enum snd_engine engine = SND_AUTO;
	struct snd_envelope pad = { 3, 5, 160 };
	WORD *tri;
	ULONG id;
	UWORD i;

	if (argc > 1) {
		if (!strcmp(argv[1], "ahi"))
			engine = SND_AHI;
		else if (!strcmp(argv[1], "paula"))
			engine = SND_PAULA4;
	}

	tri = (WORD *)AllocVec(CYCLE * 2, MEMF_PUBLIC | MEMF_CLEAR);
	if (!tri) {
		printf("out of memory\n");
		return 0;
	}
	for (i = 0; i < CYCLE / 2; i++) {
		tri[i] = (WORD)(-30000 + (LONG)i * 60000 / (LONG)(CYCLE / 2));
		tri[CYCLE - 1 - i] = tri[i];
	}

	if (!snd_init(engine, 8)) {
		printf("no usable audio engine (AHI needs a 68020 and ahi.device)\n");
		goto done;
	}
	printf("engine: %s, %u channels\n", snd_engine_name(), (unsigned)snd_voices());
	id = snd_setsample(tri, CYCLE, FALSE);
	if (!id) {
		printf("sample load failed\n");
		goto done;
	}
	if (!snd_audioon()) {
		printf("could not start the audio (in use by another program?)\n");
		goto done;
	}

	/* 1: chord with envelope, then release */
	for (i = 0; i < 4; i++)
		snd_playsample(i, id, 0, (LONG)(chord_hz[i] * CYCLE), 65536 / 2,
		               pans[i], 0, &pad);
	say_free("chord playing");
	Delay(100);
	for (i = 0; i < 4; i++)
		snd_release(i, 10);
	Delay(60);
	say_free("chord released");
	Delay(25);

	/* 2: glide up an octave and back */
	snd_playsample(0, id, 0, (LONG)(440 * CYCLE), 65536 / 2, 32768, 0, 0);
	Delay(25);
	snd_setfreq(0, 100, (LONG)(880 * CYCLE));
	Delay(110);
	snd_setfreq(0, 100, (LONG)(440 * CYCLE));
	Delay(110);
	snd_release(0, 3);
	Delay(25);

	/* 3: eight voices, AHI only */
	if (snd_voices() >= 8) {
		for (i = 0; i < 8; i++)
			snd_playsample(i, id, 0, (LONG)(wide_hz[i] * CYCLE), 65536 / 4,
			               (ULONG)i * 65536 / 7, 0, 0);
		say_free("eight voices playing");
		Delay(100);
		for (i = 0; i < 8; i++)
			snd_release(i, 20);
		Delay(120);
	}
	say_free("end");

done:
	snd_end();
	FreeVec(tri);
	return 0;
}
