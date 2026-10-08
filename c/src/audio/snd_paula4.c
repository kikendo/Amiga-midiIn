/*
 * snd_paula4.c - the native 4 channel Paula engine (paula4.c) behind the
 * snd_backend interface. Pan is ignored: channels 0 and 3 are left, 1 and 2
 * right. Stereo samples are refused.
 */
#include <exec/types.h>

#include "snd_backend.h"
#include "paula4.h"

static BOOL p_init(UWORD max_samples)
{
	return paula4_open(max_samples);
}

static ULONG p_load(const WORD *data, ULONG frames, BOOL stereo)
{
	if (stereo)
		return 0;
	return paula4_load16(data, frames);
}

static void p_play(UWORD ch, ULONG id, LONG offset, LONG freq,
                   ULONG volume, ULONG pan, LONG loop)
{
	(void)pan;
	paula4_play(ch, id, offset, freq, volume, loop);
}

static void p_set_volume(UWORD ch, ULONG volume, ULONG pan)
{
	(void)pan;
	paula4_set_volume(ch, volume);
}

const struct snd_backend snd_backend_paula4 = {
	"Paula 4 channel",
	PAULA4_VOICES,
	p_init,
	paula4_close,
	paula4_audio_on,
	paula4_audio_off,
	paula4_is_on,
	p_load,
	paula4_unload,
	p_play,
	paula4_stop,
	paula4_stop_mask,
	paula4_set_freq,
	p_set_volume,
	paula4_free_voices,
	paula4_set_tick,
	0,
	0
};
