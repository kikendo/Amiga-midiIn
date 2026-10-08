/* inline/midi.h - GCC inline calls for midi.library, from midi_lib.fd */
#ifndef _INLINE_MIDI_H
#define _INLINE_MIDI_H

#ifndef __INLINE_MACROS_H
#include <inline/macros.h>
#endif

#ifndef MIDI_BASE_NAME
#define MIDI_BASE_NAME MidiBase
#endif

#define LockMidiBase() \
	LP0NR(0x1e, LockMidiBase, , MIDI_BASE_NAME)

#define UnlockMidiBase() \
	LP0NR(0x24, UnlockMidiBase, , MIDI_BASE_NAME)

#define CreateMSource(___name, ___image) \
	LP2(0x2a, struct MSource *, CreateMSource, CONST_STRPTR, ___name, a0, struct Image *, ___image, a1, , MIDI_BASE_NAME)

#define DeleteMSource(___source) \
	LP1NR(0x30, DeleteMSource, struct MSource *, ___source, a0, , MIDI_BASE_NAME)

#define FindMSource(___name) \
	LP1(0x36, struct MSource *, FindMSource, CONST_STRPTR, ___name, a0, , MIDI_BASE_NAME)

#define CreateMDest(___name, ___image) \
	LP2(0x3c, struct MDest *, CreateMDest, CONST_STRPTR, ___name, a0, struct Image *, ___image, a1, , MIDI_BASE_NAME)

#define DeleteMDest(___dest) \
	LP1NR(0x42, DeleteMDest, struct MDest *, ___dest, a0, , MIDI_BASE_NAME)

#define FindMDest(___name) \
	LP1(0x48, struct MDest *, FindMDest, CONST_STRPTR, ___name, a0, , MIDI_BASE_NAME)

#define CreateMRoute(___source, ___dest, ___routeinfo) \
	LP3(0x4e, struct MRoute *, CreateMRoute, struct MSource *, ___source, a0, struct MDest *, ___dest, a1, struct MRouteInfo *, ___routeinfo, a2, , MIDI_BASE_NAME)

#define ModifyMRoute(___route, ___newrouteinfo) \
	LP2NR(0x54, ModifyMRoute, struct MRoute *, ___route, a0, struct MRouteInfo *, ___newrouteinfo, a1, , MIDI_BASE_NAME)

#define DeleteMRoute(___route) \
	LP1NR(0x5a, DeleteMRoute, struct MRoute *, ___route, a0, , MIDI_BASE_NAME)

#define MRouteSource(___source, ___destname, ___routeinfo) \
	LP3(0x60, struct MRoute *, MRouteSource, struct MSource *, ___source, a0, CONST_STRPTR, ___destname, a1, struct MRouteInfo *, ___routeinfo, a2, , MIDI_BASE_NAME)

#define MRouteDest(___sourcename, ___dest, ___routeinfo) \
	LP3(0x66, struct MRoute *, MRouteDest, CONST_STRPTR, ___sourcename, a0, struct MDest *, ___dest, a1, struct MRouteInfo *, ___routeinfo, a2, , MIDI_BASE_NAME)

#define MRoutePublic(___sourcename, ___destname, ___routeinfo) \
	LP3(0x6c, struct MRoute *, MRoutePublic, CONST_STRPTR, ___sourcename, a0, CONST_STRPTR, ___destname, a1, struct MRouteInfo *, ___routeinfo, a2, , MIDI_BASE_NAME)

#define GetMidiMsg(___dest) \
	LP1(0x72, UBYTE *, GetMidiMsg, struct MDest *, ___dest, a0, , MIDI_BASE_NAME)

#define PutMidiMsg(___source, ___msg) \
	LP2NR(0x78, PutMidiMsg, struct MSource *, ___source, a0, UBYTE *, ___msg, a1, , MIDI_BASE_NAME)

#define FreeMidiMsg(___msg) \
	LP1NR(0x7e, FreeMidiMsg, UBYTE *, ___msg, a0, , MIDI_BASE_NAME)

#define MidiMsgType(___msg) \
	LP1(0x84, UWORD, MidiMsgType, UBYTE *, ___msg, a0, , MIDI_BASE_NAME)

#define MidiMsgLength(___msg) \
	LP1(0x8a, ULONG, MidiMsgLength, UBYTE *, ___msg, a0, , MIDI_BASE_NAME)

#define PutMidiStream(___source, ___fillbuffer, ___buf, ___bufsize, ___cursize) \
	LP5NR(0x90, PutMidiStream, struct MSource *, ___source, a0, APTR, ___fillbuffer, a1, UBYTE *, ___buf, a2, ULONG, ___bufsize, d0, ULONG, ___cursize, d1, , MIDI_BASE_NAME)

#define LockMRoutes() \
	LP0NR(0x96, LockMRoutes, , MIDI_BASE_NAME)

#define UnlockMRoutes() \
	LP0NR(0x9c, UnlockMRoutes, , MIDI_BASE_NAME)

#define FlushMDest(___dest) \
	LP1NR(0xa2, FlushMDest, struct MDest *, ___dest, a0, , MIDI_BASE_NAME)

#define GetMidiPacket(___dest) \
	LP1(0xa8, struct MidiPacket *, GetMidiPacket, struct MDest *, ___dest, a0, , MIDI_BASE_NAME)

#define FreeMidiPacket(___dest) \
	LP1NR(0xae, FreeMidiPacket, struct MidiPacket *, ___dest, a0, , MIDI_BASE_NAME)

#define SetDefaultMRouteInfo(___dest, ___routeinfo) \
	LP2NR(0xb4, SetDefaultMRouteInfo, struct MDest *, ___dest, a0, struct MRouteInfo *, ___routeinfo, a1, , MIDI_BASE_NAME)

#define CreateMListSignal(___flags) \
	LP1(0xba, struct MListSignal *, CreateMListSignal, ULONG, ___flags, d0, , MIDI_BASE_NAME)

#define DeleteMListSignal(___signal) \
	LP1NR(0xc0, DeleteMListSignal, struct MListSignal *, ___signal, a0, , MIDI_BASE_NAME)

#endif
