/*
 * play.c - port of mbplay.e: the task that receives MIDI and plays the
 * banks
 *
 * The play task does not use the E-style exceptions of eport.h (those are
 * for the main task only).
 *
 * Change from the E code: an "all notes off" controller releases each
 * channel with the release time of the bank it plays (the E code used a
 * variable left over from an earlier message).
 */
#include <exec/types.h>
#include <exec/memory.h>
#include <exec/ports.h>
#include <exec/semaphores.h>
#include <dos/dostags.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/midi.h>

#include "eport.h"
#include "banks.h"
#include "sfx.h"
#include "tables.h"
#include "play.h"

#define NOTEON_ON  1
#define NOTEON_OFF 2
#define MM_ALLSOUNDOFF 0x78

struct audiochan {
	struct bank *bank;
	UBYTE noteoff;
	UBYTE midichan;
	UBYTE playnote;
	UBYTE velocity;
};

struct midi_controllers {
	WORD pitchbend[16];
	WORD volume[16];
	WORD pan[16];
};

struct internal_message {
	struct Message mn;
	LONG type;
	LONG val1;
	LONG val2;
};

#define PSG_ALLOCOK     TRUE
#define PSG_ALLOCFAILED FALSE

LONG maxchan, maskmaxchan;
LONG mcontrol, basesetb, rangesetb;
struct List mysrclist;
struct MRouteInfo minfo;

static struct Task *volatile playtask;
static struct MsgPort *playport, *extport;
static struct SignalSemaphore playtasksem;
static struct MDest *pdest;             /* the play task's own MIDI dest */
static struct MRoute **routes;           /* NULL terminated */
static LONG nroutes;

static struct audiochan *ach;           /* 32 */
static struct midi_controllers *mctrl;
static UBYTE *aftch;                    /* 32 */
static LONG lastchan;
static WORD **midicontrolarray;         /* 33 */

/* ------------------------------------------------------- main task side */

LONG getchannelnote(LONG f)
{
	if (ach) {
		if (chanfree((UWORD)f))
			return -1;
		return ach[f].playnote;
	}
	return -1;
}

void whichbankisplaying(UBYTE *bf, struct bank *bd)
{
	LONG i;

	if (!ach)
		return;
	for (i = 0; i <= maxchan; i++)
		if (!chanfree((UWORD)i) && ach[i].bank)
			bf[ach[i].bank - bd] = 1;
}

void clearcontrollers(void)
{
	LONG f;

	if (!mctrl)
		return;
	for (f = 0; f < 16; f++) {
		mctrl->pitchbend[f] = PITCHBENDCENTER;
		mctrl->volume[f] = 16383;
		mctrl->pan[f] = 8192;
	}
}

/* ------------------------------------------------------- volume, pan */

static void calcvolume(LONG f, LONG *volume_out, LONG *pan_out)
{
	struct audiochan *ac = &ach[f];
	struct bank *nb;
	LONG volume, vol, pan, vel, s;

	if (!(nb = ac->bank)) {
		*volume_out = 0;
		*pan_out = 0;
		return;
	}
	/* vel = (127 - velocity) * 16 * velsens^2 / 10000 */
	vel = (127 - ac->velocity) << 4;
	vel = (LONG)(WORD)((ULONG)(UWORD)vel * (UWORD)(nb->velsens * nb->velsens) / 10000);
	s = nb->aftersens * nb->aftersens;
	if (nb->set & B_ADDAFTERT) {
		vol = aftch[f];
		if (vol >= 128) {
			vol = vel;
		} else {
			/* vel - (aftch * 16 * aftersens^2 / 10000) * vel / 2032 */
			vol = (UWORD)((ULONG)(vol << 4) * (UWORD)s / 10000);
			vol = (LONG)(WORD)((ULONG)(UWORD)vol * (UWORD)vel / 2032);
			vol = vel - vol;
		}
	} else {
		vol = 127 - aftch[f];
		if (vol < 0) {
			vol = vel;
		} else {
			/* ((127 - aftch) * 16 - vel) * aftersens^2 / 10000 + vel */
			vol = (vol << 4) - vel;
			vol = (LONG)(WORD)((LONG)(WORD)vol * (LONG)(WORD)s / 10000);
			vol += vel;
		}
	}
	if (vol < 0)
		vol = 0;
	if (vol >= EXPTABLE_LEN)
		vol = EXPTABLE_LEN - 1;
	volume = nb->volume * exptable[vol] / 32767;
	volume <<= 5;
	if (nb->mctrlvol)
		volume = volume * mctrl->volume[ac->midichan - 1] / 16383;
	volume <<= 3;

	pan = nb->panorama + (ac->playnote - (nb->hibound + nb->lobound) / 2) * nb->panwide;
	if (pan > 256)
		pan = 256;
	if (pan < 0)
		pan = 0;
	pan <<= 6;
	if (nb->mctrlpan) {
		if ((vel = mctrl->pan[ac->midichan - 1]) < 8192)
			pan = pan * vel / 8192;
		else if ((vel = vel - 8192) > 0)
			pan = (16384 - pan) * vel / 8192 + pan;
	}
	pan <<= 2;
	*volume_out = volume;
	*pan_out = pan;
}

static LONG findchannel(LONG pri)
{
	LONG d = lastchan, f = -1, i, p;
	ULONG free;

	if ((free = channelsfree((ULONG)maskmaxchan))) {
		d &= 31;
		free = (free >> d) | (free << ((32 - d) & 31));
		while (!(free & 1)) {
			free >>= 1;
			d++;
		}
		d &= 31;
		lastchan = d;
		return d;
	}
	if (d > maxchan)
		d = 0;
	pri--;
	i = d;
	do {
		i++;
		if (i > maxchan)
			i = 0;
		if (ach[i].bank && (p = ach[i].bank->pri) > pri) {
			pri = p;
			f = i;
		}
	} while (i != d);
	if (f >= 0)
		lastchan = f;
	return f;
}

static void release_or_stop(LONG f, struct audiochan *ac, LONG release)
{
	if (release) {
		ac->noteoff = 0;
		releasechan((UWORD)f, release);
	} else {
		soundoff((UWORD)f);
	}
}

static void playbank(struct sfx *snd, struct bank *nb, LONG note, LONG midichan,
                     LONG vel, LONG pitch)
{
	struct audiochan *ac;
	LONG chan, volume, pan, f, g;
	BOOL loop;

	if ((nb->set & B_MONO) && (g = nb->monoslide) != 0) {
		for (chan = 0; chan <= maxchan; chan++) {
			ac = &ach[chan];
			if (!chanfree((UWORD)chan) && ac->bank == nb && ac->noteoff) {
				for (f = chan + 1; f <= maxchan; f++)
					if (ach[f].bank == nb)
						release_or_stop(f, &ach[f], nb->release);
				if (nb->monovsens) {
					LONG d0 = (LONG)(UWORD)((ULONG)(UWORD)(vel << 7) * nb->monovsens / 100);

					d0 = (LONG)(WORD)((ULONG)(UWORD)d0 * (UWORD)g / 16256);
					g -= d0;
				}
				ac->playnote = (UBYTE)note;
				ac->midichan = (UBYTE)midichan;
				sfx_changepitch(snd, (UWORD)chan, pitch, nb->pitchsens, note,
				                nb->fine - FINE_CENTR + 100, nb->base, g, FALSE);
				return;
			}
		}
	}
	if ((chan = findchannel(nb->pri)) < 0)
		return;
	ac = &ach[chan];
	aftch[chan] = 128;
	ac->velocity = (UBYTE)vel;
	ac->bank = nb;
	ac->playnote = (UBYTE)note;
	ac->midichan = (UBYTE)midichan;
	calcvolume(chan, &volume, &pan);
	if (nb->set & B_DRUM) {
		ac->noteoff = 0;
		loop = FALSE;
	} else {
		ac->noteoff = (nb->set & B_DUR_ON) ? NOTEON_ON : NOTEON_OFF;
		loop = (nb->set & B_LOOP) ? TRUE : FALSE;
	}
	if (nb->set & B_MONO)
		for (f = 0; f <= maxchan; f++)
			if (&ach[f] != ac && ach[f].bank == nb)
				release_or_stop(f, &ach[f], nb->release);
	if ((g = nb->group))
		for (f = 0; f <= maxchan; f++) {
			struct bank *nbt = ach[f].bank;

			if (&ach[f] != ac && nbt != nb && nbt && g == nbt->group)
				release_or_stop(f, &ach[f], nbt->release);
		}
	sfx_play(snd, (UWORD)chan, loop, note, nb->fine - FINE_CENTR + 100, nb->base,
	         volume, pan, pitch, nb->pitchsens, nb->attack, nb->decay,
	         nb->sustainlev, nb->firstskip);
}

/* ------------------------------------------------------------ routes */

static void changeMRouteInfo(void)
{
	LONG i;

	if (routes)
		for (i = 0; i < nroutes; i++)
			if (routes[i])
				ModifyMRoute(routes[i], &minfo);
}

static void freeMRoutes(void)
{
	LONG i;

	if (routes) {
		for (i = 0; i < nroutes; i++)
			if (routes[i])
				DeleteMRoute(routes[i]);
		FreeVec(routes);
		routes = 0;
		nroutes = 0;
	}
}

static void makeMRoutes(void)
{
	struct Node *n;
	LONG i = 0;

	for (n = mysrclist.lh_Head; n->ln_Succ; n = n->ln_Succ)
		if (n->ln_Name[0] == '+')
			i++;
	freeMRoutes();
	if (i) {
		routes = (struct MRoute **)AllocVec((ULONG)i * sizeof(*routes), MEMF_PUBLIC | MEMF_CLEAR);
		if (!routes)
			return;
		nroutes = i;
		i = 0;
		for (n = mysrclist.lh_Head; n->ln_Succ; n = n->ln_Succ)
			if (n->ln_Name[0] == '+')
				routes[i++] = MRouteDest((CONST_STRPTR)n->ln_Name + 2, pdest, &minfo);
	}
}

/* ------------------------------------------------------- the task */

/* frees playing notes on (note, midichan); TRUE if some used a release */
static LONG noteoff(LONG a, LONG b, UBYTE which)
{
	ULONG freech = 0;
	LONG f, i = 0;

	for (f = 0; f <= maxchan; f++) {
		struct audiochan *ac = &ach[f];

		if (!chanfree((UWORD)f) && (ac->noteoff & which) && ac->playnote == a
		    && ac->midichan == b) {
			if ((i = ac->bank->release)) {
				ac->noteoff = 0;
				releasechan((UWORD)f, i);
			} else {
				freech |= 1UL << f;
			}
		}
	}
	if (freech)
		soundsoff(freech);
	return (LONG)freech | i;
}

static void midimessage(LONG type, LONG a, LONG b, LONG c)
{
	struct audiochan *ac;
	struct bank *nb;
	struct sfx *snd;
	LONG f, i, v, p;

	switch (type) {
	case MMF_NOTEON:
		if (!(rangesetb || basesetb)) {
			if (noteoff(a, b, NOTEON_ON) == 0)
				for (i = 0; i < NUMBANKS; i++) {
					nb = prilist[i];
					if ((snd = nb->instr) && nb->midi == b
					    && a >= nb->lobound && a <= nb->hibound)
						playbank(snd, nb, a, b, c, mctrl->pitchbend[b - 1]);
				}
		}
		break;
	case MMF_NOTEOFF:
		if (!(rangesetb || basesetb))
			noteoff(a, b, NOTEON_OFF);
		break;
	case MMF_PITCHBEND:
		a = c * 128 + a;
		mctrl->pitchbend[b - 1] = (WORD)a;
		for (f = 0; f <= maxchan; f++) {
			ac = &ach[f];
			if (!chanfree((UWORD)f) && ac->midichan == b && (nb = ac->bank) && (snd = nb->instr))
				sfx_changepitch(snd, (UWORD)f, a, nb->pitchsens, ac->playnote,
				                nb->fine - FINE_CENTR + 100, nb->base, 0, FALSE);
		}
		break;
	case MMF_POLYPRESS:
		for (f = 0; f <= maxchan; f++) {
			ac = &ach[f];
			if (!chanfree((UWORD)f) && ac->playnote == a && ac->midichan == b) {
				aftch[f] = (UBYTE)c;
				calcvolume(f, &v, &p);
				changevolume((UWORD)f, v, p);
			}
		}
		break;
	case MMF_CHANPRESS:
		for (f = 0; f <= maxchan; f++) {
			ac = &ach[f];
			if (!chanfree((UWORD)f) && ac->midichan == b) {
				aftch[f] = (UBYTE)a;
				calcvolume(f, &v, &p);
				changevolume((UWORD)f, v, p);
			}
		}
		break;
	case MMF_CTRL:
		if (a == MM_ALLSOUNDOFF) {
			soundsoff((ULONG)maskmaxchan);
		} else if (a == MM_ALLOFF) {
			ULONG freech = 0;

			for (f = 0; f <= maxchan; f++) {
				if (chanfree((UWORD)f))
					continue;
				ac = &ach[f];
				if (ac->bank && (i = ac->bank->release)) {
					ac->noteoff = 0;
					releasechan((UWORD)f, i);
				} else {
					freech |= 1UL << f;
				}
			}
			if (freech)
				soundsoff(freech);
		} else {
			switch (a) {
			case MM_RESETCTRL:
				clearcontrollers();
				break;
			case MC_VOLUME:
				mctrl->volume[b - 1] = (WORD)((c << 7) + c);
				break;
			case MC_VOLUME + 0x20:
				mctrl->volume[b - 1] = (WORD)((mctrl->volume[b - 1] & 0x3F80) + c);
				break;
			case MC_PAN:
				mctrl->pan[b - 1] = (WORD)((c << 7) + c);
				break;
			case MC_PAN + 0x20:
				mctrl->pan[b - 1] = (WORD)((mctrl->pan[b - 1] & 0x3F80) + c);
				break;
			default:
				a = -1;
			}
			if (a != -1)
				for (f = 0; f <= maxchan; f++)
					if (!chanfree((UWORD)f) && ach[f].midichan == b) {
						calcvolume(f, &v, &p);
						changevolume((UWORD)f, v, p);
					}
		}
		break;
	}
}

/* returns TRUE on PSG_QUIT */
static BOOL internalmessage(LONG type, struct bank *ctrlbank, LONG playnote)
{
	struct audiochan *ac;
	struct bank *nb = ctrlbank;
	struct sfx *snd;
	LONG f, v, p, a, b;

	switch (type) {
	case PSG_TUNE:
		for (f = 0; f <= maxchan; f++) {
			ac = &ach[f];
			if (!chanfree((UWORD)f) && nb == ac->bank && (snd = nb->instr))
				sfx_changepitch(snd, (UWORD)f, mctrl->pitchbend[ac->midichan - 1],
				                nb->pitchsens, ac->playnote,
				                nb->fine - FINE_CENTR + 100, nb->base, 0, FALSE);
		}
		break;
	case PSG_CHANGE:
		makeMRoutes();
		break;
	case PSG_MINFO:
		changeMRouteInfo();
		break;
	case PSG_PLAY:
		if (mcontrol == 0) {
			if (nb && (snd = nb->instr) && playnote < 128) {
				ULONG freech = 0;
				LONG i = 0;

				a = playnote;
				b = nb->midi;
				for (f = 0; f <= maxchan; f++) {
					ac = &ach[f];
					if (!chanfree((UWORD)f) && ac->noteoff && ac->playnote == a
					    && ac->bank == nb) {
						if ((i = nb->release)) {
							ac->noteoff = 0;
							releasechan((UWORD)f, i);
						} else {
							freech |= 1UL << f;
						}
					}
				}
				if (freech)
					soundsoff(freech);
				if ((freech | (ULONG)i) == 0)
					playbank(snd, nb, a, b, 127, mctrl->pitchbend[b - 1]);
			} else if (playnote > 127) {
				soundsoff((ULONG)maskmaxchan);
			}
		}
		break;
	case PSG_VOLUME:
		for (f = 0; f <= maxchan; f++)
			if (!chanfree((UWORD)f) && ach[f].bank == nb) {
				calcvolume(f, &v, &p);
				changevolume((UWORD)f, v, p);
			}
		break;
	case PSG_QUIT:
		return TRUE;
	}
	return FALSE;
}

static void subtaskplay(void)
{
	struct internal_message mymsg;
	ULONG sigmidi = 0, sigport = 0;
	BOOL quit = FALSE;
	LONG f;

	ObtainSemaphore(&playtasksem);
	mymsg.mn.mn_Node.ln_Pri = 0;
	mymsg.mn.mn_ReplyPort = 0;
	mymsg.mn.mn_Length = sizeof(mymsg);
	playport = CreateMsgPort();
	pdest = playport ? CreateMDest(0, 0) : 0;
	mymsg.type = (playport && pdest) ? PSG_ALLOCOK : PSG_ALLOCFAILED;
	PutMsg(extport, &mymsg.mn);

	if (mymsg.type == PSG_ALLOCOK) {
		makeMRoutes();
		for (f = 0; f < 32; f++) {
			ach[f].midichan = 1;
			ach[f].noteoff = 0;
			ach[f].playnote = 255;
			ach[f].bank = 0;
		}
		clearcontrollers();
		sigmidi = 1UL << pdest->DestPort->mp_SigBit;
		sigport = 1UL << playport->mp_SigBit;
		do {
			ULONG sig = Wait(sigmidi | sigport);

			lockbanksaccess();
			if (sig & sigmidi) {
				struct MidiPacket *packet;

				while ((packet = GetMidiPacket(pdest))) {
					LONG type = packet->Type;
					LONG a = packet->MidiMsg[1];
					LONG b = (packet->MidiMsg[0] & 15) + 1;
					LONG c = packet->MidiMsg[2];

					FreeMidiPacket(packet);
					if (mcontrol)
						midimessage(type, a, b, c);
				}
			}
			if (sig & sigport) {
				struct internal_message *msg;

				while ((msg = (struct internal_message *)GetMsg(playport))) {
					LONG type = msg->type;
					struct bank *ctrlbank = (struct bank *)msg->val1;
					LONG playnote = msg->val2;

					ReplyMsg(&msg->mn);
					if (internalmessage(type, ctrlbank, playnote))
						quit = TRUE;
					if (SetSignal(0, 0) & sigport)
						break;
				}
			}
			releasebanksaccess();
		} while (!quit);
	}
	freeMRoutes();
	if (pdest)
		DeleteMDest(pdest);
	pdest = 0;
	if (playport)
		DeleteMsgPort(playport);
	playport = 0;
	Forbid();               /* the process ends before anyone runs again */
	playtask = 0;
	ReleaseSemaphore(&playtasksem);
}

/* ------------------------------------------------------- task control */

void install_playtask(void)
{
	struct internal_message *msg;

	ach = (struct audiochan *)e_new(32 * sizeof(struct audiochan));
	mctrl = (struct midi_controllers *)e_new(sizeof(struct midi_controllers));
	aftch = (UBYTE *)e_new(32);
	midicontrolarray = (WORD **)e_new(33 * sizeof(WORD *));
	InitSemaphore(&playtasksem);
	if (!(extport = CreateMsgPort()))
		Raise('MEM');
	SetTaskPri(FindTask(0), 0);
	ObtainSemaphore(&playtasksem);
	playtask = (struct Task *)CreateNewProcTags(
		NP_Entry, (ULONG)subtaskplay,
		NP_Name, (ULONG)"midiIn_PLAY",
		NP_Priority, 5,
		NP_StackSize, 8192,
		TAG_DONE);
	if (!playtask) {
		ReleaseSemaphore(&playtasksem);
		Raise('TASK');
	}
	ReleaseSemaphore(&playtasksem);
	for (;;) {
		WaitPort(extport);
		if ((msg = (struct internal_message *)GetMsg(extport)))
			break;
	}
	if (msg->type == PSG_ALLOCFAILED) {
		ObtainSemaphore(&playtasksem);  /* wait for the task to end */
		ReleaseSemaphore(&playtasksem);
		Raise('TASK');
	}
	midicontrolarray[MC_VOLUME] = mctrl->volume;
	midicontrolarray[MC_PAN] = mctrl->pan;
	midicontrolarray[32] = mctrl->pitchbend;
}

void deinstall_playtask(void)
{
	if (playtask) {
		signal_playtask(PSG_QUIT, 0, 0);
		ObtainSemaphore(&playtasksem);
		ReleaseSemaphore(&playtasksem);
	}
	if (extport)
		DeleteMsgPort(extport);
	extport = 0;
	e_dispose(mctrl);
	e_dispose(ach);
	e_dispose(aftch);
	e_dispose(midicontrolarray);
	mctrl = 0;
	ach = 0;
	aftch = 0;
	midicontrolarray = 0;
}

BOOL signal_playtask(LONG type, APTR bn, LONG v)
{
	struct internal_message msg;

	if (playtask) {
		msg.mn.mn_Node.ln_Pri = 0;
		msg.mn.mn_ReplyPort = extport;
		msg.mn.mn_Length = sizeof(msg);
		msg.type = type;
		msg.val1 = (LONG)bn;
		msg.val2 = v;
		PutMsg(playport, &msg.mn);
		for (;;) {
			WaitPort(extport);
			if (GetMsg(extport))
				break;
		}
	}
	return TRUE;
}

WORD *getmidicontrolarray(LONG mcm)
{
	return midicontrolarray ? midicontrolarray[mcm] : 0;
}

void xchgbanksachn(struct bank *bank1, struct bank *bank2)
{
	LONG i;

	lockbanksaccess();
	xchgbanks(bank1, bank2);
	if (ach)
		for (i = 0; i < 32; i++) {
			if (ach[i].bank == bank1)
				ach[i].bank = bank2;
			else if (ach[i].bank == bank2)
				ach[i].bank = bank1;
		}
	releasebanksaccess();
}
