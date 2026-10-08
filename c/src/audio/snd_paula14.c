/*
 * snd_paula14.c - the 14-bit Paula engine (paula14.c) behind the
 * snd_backend interface
 */
#include <exec/types.h>

#include "snd.h"
#include "snd_backend.h"
#include "paula14.h"

static BOOL p_audio_on(void)
{
	if (!paula14_audio_on())
		return FALSE;
	snd_scopedata = paula14_scopedata();
	snd_scopelen = PAULA14_SCOPELEN;
	return TRUE;
}

static void p_audio_off(void)
{
	snd_scopedata = 0;
	paula14_audio_off();
}

const struct snd_backend snd_backend_paula14 = {
	"Paula 14-bit",
	PAULA14_VOICES,
	paula14_open,
	paula14_close,
	p_audio_on,
	p_audio_off,
	paula14_is_on,
	paula14_load,
	paula14_unload,
	paula14_play,
	paula14_stop,
	paula14_stop_mask,
	paula14_set_freq,
	paula14_set_volume,
	paula14_free_voices,
	paula14_set_tick,
	0,
	paula14_set_mixfreq
};
