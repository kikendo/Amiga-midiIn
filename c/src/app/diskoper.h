/*
 * diskoper.h - projects, preferences and icons (mbdiskoper.e)
 *
 * struct mbprefs is stored in project and preference files as it is; its
 * layout (78 bytes) must not change.
 */
#ifndef MI_DISKOPER_H
#define MI_DISKOPER_H

#include <exec/types.h>
#include <exec/lists.h>

#include "banks.h"

#define MAGIC_VERSION_0_PREFS 'mIn0'
#define MAGIC_VERSION_1_PREFS 'mIn1'

struct mbprefs {
	LONG magic;
	WORD mainwinx, mainwiny, mainwinw, mainwinh;
	WORD volumewinx, volumewiny, volumewinw, volumewinh, volumehide;
	WORD envelwinx, envelwiny, envelwinw, envelwinh, envelhide;
	WORD scopewinx, scopewiny, scopewinw, scopewinh, scopehide;
	WORD midimonwinx, midimonwiny, midimonwinw, midimonwinh, midimonhide;
	WORD dmaper;
	WORD routeoffs;         /* private: offsets of the strings after it */
	WORD sndoffs;
	WORD prjoffs;
	WORD currentmcm;
	WORD msgflags;
	WORD chanflags;
	UBYTE maxchannels;
	UBYTE led;
	UBYTE midictrl;
	UBYTE activeb;
	LONG ahiaudioid;        /* new in version 1 */
	LONG mixfreq;
};

extern char sndpath[256];
extern char prjpath[256];

/* all raise on errors */
void save_project(CONST_STRPTR name, struct List *slist, struct bank *bnk,
                  struct mbprefs *prefs, BOOL saveicons, BOOL saveundo);
BOOL loadinstrumentsfrombank(CONST_STRPTR name, struct List *slist);
struct mbprefs *load_project(CONST_STRPTR name, struct List *slist,
                             struct bank *bnk, struct mbprefs *prefs);
void mergeproject(CONST_STRPTR name, struct List *slist, struct bank *bnk, LONG numbnk);

/* these report their errors themselves */
void savesettings(struct List *slist, struct mbprefs *prefs);
LONG loadsettings(struct List *slist, struct mbprefs *prefs, struct bank *bnk,
                  CONST_STRPTR projectname);

void freemidiin_icon(void);

#endif
