#ifndef MIDI_MIDIBASE_H
#define MIDI_MIDIBASE_H

/* midi.library MidiBase & related definitions (not for the casual midi user) */

#ifndef EXEC_LIBRARIES_H
 #include <exec/libraries.h>
#endif

#ifndef EXEC_SEMAPHORES_H
 #include <exec/semaphores.h>
#endif

#ifndef DOS_DOS_H
 #include <dos/dos.h>
#endif

#ifndef MIDI_MIDI_H
 #include <midi/midi.h>
#endif


struct MidiBase {
    struct Library libnode;
    struct List SourceList,DestList;
    struct SignalSemaphore ListSemaphore;   /* blocks Source/Dest list management */
    struct SignalSemaphore RouteSemaphore;  /* blocks RPList management & msg routing */
    ULONG SegList;
    APTR  SysBase,DosBase;
    struct MinList SignalList;		    /* list of MListSignal's */
};

struct MTaskInfo {
	/* set by caller */
    char *Name;
    WORD Pri;
    void (*Entry)();
    UWORD Stack;

    UWORD Sources;
    struct MNodeInfo *SourceList;
    UWORD Dests;
    struct MNodeInfo *DestList;

	/* set by midi.library */
    struct SignalSemaphore Semaphore;	/* locks task */

    UWORD UsageCount;			/* task's usage count */
    struct MsgPort *TaskPort;		/* task's MsgPort */
    BPTR Segment;			/* task's Segment */
};

struct MNodeInfo {
    char *Name;
    struct Image *Image;
    APTR Node;
};

#endif
