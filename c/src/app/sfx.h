/*
 * sfx.h - a sample (instrument) object, port of soundfx_ahi.e, on top of
 * the engine-independent snd layer
 */
#ifndef MI_SFX_H
#define MI_SFX_H

#include <exec/types.h>
#include <exec/nodes.h>
#include <utility/tagitem.h>

/* starts like struct lln: node, then a pointer to itself */
struct sfx {
	struct Node ln;
	struct sfx *myself;
	WORD *start;            /* sample data, NULL while not loaded */
	LONG loop;              /* loop start frame */
	LONG length;            /* bytes */
	WORD type;              /* 1 = stereo */
	WORD loadcnest;         /* load count */
	ULONG id;               /* snd sample id */
	LONG rate;              /* recorded rate */
	WORD maxvolume;
	char name[256];         /* path */
};

/* audio_attrs() tags */
#define SFX_SET_AUDIO_STATUS (TAG_USER + 0)
#define SFX_SET_AUDIO_ID     (TAG_USER + 1)
#define SFX_SET_CHANNELS     (TAG_USER + 2)
#define SFX_SET_MIXFREQ      (TAG_USER + 3)
#define SFX_SET_APPLYAUDIO   (TAG_USER + 4)
#define SFX_GET_AUDIO_STATUS (TAG_USER + 5)

/* engine: SND_AUTO etc. (snd.h); raises 'AUDB' if it cannot be used */
void initsoundfx(UWORD numsamples, LONG engine);
/* the engine asked for (AUDIO argument / tooltype) */
extern LONG sfx_engine_req;
void freesoundfx(void);
LONG audio_attrs(struct TagItem *tags);

struct sfx *sfx_new(void);              /* NEW snd (raises 'MEM') */
void sfx_dispose(struct sfx *s);        /* END snd: end() and free */

STRPTR sfx_pathname(struct sfx *s);
LONG sfx_filetype(struct sfx *s, STRPTR *descr);   /* raises 'UNRE' */
LONG sfx_init(struct sfx *s, CONST_STRPTR name, STRPTR *descr); /* raises */
void sfx_load(struct sfx *s);           /* raises */
void sfx_unload(struct sfx *s);
void sfx_end(struct sfx *s);
BOOL sfx_changepitch(struct sfx *s, UWORD chan, LONG pitch, LONG range,
                     LONG note, LONG fine, LONG base, LONG uptime, BOOL tyl);
BOOL sfx_play(struct sfx *s, UWORD chan, BOOL repeat, LONG note, LONG fine,
              LONG base, LONG volume, LONG pan, LONG pitch, LONG prange,
              LONG t1, LONG t2, LONG stn, LONG tskip);
LONG sfx_setrate(struct sfx *s, LONG newrate);   /* returns the old rate */
LONG sfx_basefreq(struct sfx *s);
LONG sfx_frames(struct sfx *s);         /* -1 if not loaded */
LONG sfx_length(struct sfx *s);         /* -1 if not loaded */
BOOL sfx_stereo(struct sfx *s);
LONG sfx_maxvolume(struct sfx *s);
LONG sfx_setloop(struct sfx *s, LONG loopframe); /* <0: returns the loop */

void changevolume(UWORD chan, LONG volume, LONG pan);
void releasechan(UWORD chan, LONG tenths);
void soundsoff(ULONG chanmask);
void soundoff(UWORD chan);
BOOL chanfree(UWORD chan);
ULONG channelsfree(ULONG chanmask);

#endif
