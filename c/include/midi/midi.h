#ifndef MIDI_MIDI_H
#define MIDI_MIDI_H

/*
    midi.library (Bill Barton, v2.0 of the package, library version 7)
    structures and defines, from include.h/midi/midi.h of MidiLib20.lha.
    The K&R function declarations are replaced by proto/midi.h.

    Definitions pertaining to MIDI are derived from MIDI 1.0 Detailed
    Specification v4.0 (published by the Internation MIDI Association)
    and is current as of June, 1988.

    v2.0 - 23-Oct-88
*/

#ifndef EXEC_LISTS_H
 #include <exec/lists.h>
#endif

#ifndef EXEC_PORTS_H
 #include <exec/ports.h>
#endif


/* midi.library structures & defines */

#define MIDINAME    "midi.library"
#define MIDIVERSION 7L


/* Routes */

#define RIM_MAXCOUNT 3

struct RIMatch {
    UBYTE Flags;		/* flag bits defined below */
    UBYTE Match[RIM_MAXCOUNT];
};

#define RIMF_COUNTBITS 0x03	/* mask for # of match values (0 for match all) */
#define RIMF_EXTID     0x40	/* indicates all 3 bytes of match[] == one 3 byte manuf. id (not valid for CtrlMatch) */
#define RIMF_EXCLUDE   0x80	/* reverses logic of RIMatch so that all except those specified pass */


struct MRouteInfo {
    UWORD MsgFlags;		/* flags enabling message types defined below (MMF_) (msg filters) */
    UWORD ChanFlags;		/* incoming channel enable flags (LSB = chan 1, MSB = chan 16) (channel filters) */
    BYTE  ChanOffset;		/* signed offset applied to channels (simple channelizing) */
    BYTE  NoteOffset;		/* signed offset applied to note numbers (transposition) */
    struct RIMatch SysExMatch;	/* Sys/Ex manufacturer id filtering */
    struct RIMatch CtrlMatch;	/* Controller number filtering */
};

/* Msg Flags for MRouteInfo structure and returned by MidiMsgType */

#define MMF_CHAN	0x00ff
#define MMF_NOTEOFF	0x0001
#define MMF_NOTEON	0x0002
#define MMF_POLYPRESS	0x0004
#define MMF_CTRL	0x0008
#define MMF_PROG	0x0010
#define MMF_CHANPRESS	0x0020
#define MMF_PITCHBEND	0x0040
#define MMF_MODE	0x0080

#define MMF_SYSCOM	0x0100
#define MMF_SYSRT	0x0200
#define MMF_SYSEX	0x0400

#define MMF_ALL 	0x07ff	    /* all normal messages */


struct MRoutePtr {
    struct MinNode node;
    struct MRoute *Route;
};

struct MRoute {
    struct MSource *Source;
    struct MDest *Dest;
    struct MRoutePtr SRoutePtr, DRoutePtr;
    struct MRouteInfo RouteInfo;
};


/* Nodes */

struct MSource {
    struct Node Node;
    struct Image *Image;
    struct MinList RPList;
    APTR UserData;		/* user data extension */

    /* new stuff for v2.0 */
    UWORD RouteMsgFlags;	/* mask of all route->MsgFlags for this MSource */
    UWORD RouteChanFlags;	/* mask of all route->ChanFlags for this MSource */
};

/* node types for Source */
#define NT_MSOURCE	0x20
#define NT_RESMSOURCE	0x21


struct MDest {
    struct Node Node;
    struct Image *Image;
    struct MinList RPList;
    struct MsgPort *DestPort;
    APTR UserData;		/* user data extension */

    /* new stuff for v2.0 */
    struct MRouteInfo DefaultRouteInfo;     /* used when Routing function doesn't supply a RouteInfo */
};

/* node types for Dest */
#define NT_MDEST	0x22
#define NT_RESMDEST	0x23


/* MIDI Packet (new for v2.0) */

struct MidiPacket {	    /* returned by GetMidiPacket() */
    struct Message ExecMsg;
    UWORD Type; 	    /* MMF_ bit for this message (as returned by MidiMsgType()) */
    UWORD Length;	    /* length of msg in bytes (as returned by MidiMsgLength()) */
    ULONG reserved;	    /* reserved for future expansion */
    UBYTE MidiMsg[4];	    /* actual MIDI message (real length of this array is Length, always at least this much memory allocated) */
};


/* Public List Change Signal */

struct MListSignal {
    struct MinNode Node;
    struct Task *SigTask;	/* task to signal */
    UBYTE SigBit;		/* signal bit to use */
    UBYTE Flags;		/* flags, see below */
};

    /* user flags */
#define MLSF_SOURCE   0x01L	/* causes signal when SourceList changes */
#define MLSF_DEST     0x02L	/* causes signal when DestList changes */


/* MIDI message defininition */

/* Status Bytes */

    /* Channel Voice Messages (1sssnnnn) (OR with channel number) */
#define MS_NOTEOFF    0x80
#define MS_NOTEON     0x90
#define MS_POLYPRESS  0xA0
#define MS_CTRL       0xB0
#define MS_MODE       0xB0
#define MS_PROG       0xC0
#define MS_CHANPRESS  0xD0
#define MS_PITCHBEND  0xE0

    /* System Common Messages (11110sss) */
#define MS_SYSEX      0xF0
#define MS_QTRFRAME   0xF1
#define MS_SONGPOS    0xF2
#define MS_SONGSELECT 0xF3
#define MS_TUNEREQ    0xF6
#define MS_EOX	      0xF7

    /* System Real Time Messages (11111sss) */
#define MS_CLOCK      0xF8
#define MS_START      0xFA
#define MS_CONTINUE   0xFB
#define MS_STOP       0xFC
#define MS_ACTVSENSE  0xFE
#define MS_RESET      0xFF

/* Miscellaneous */

#define MIDDLEC 	60	/* middle C note value */
#define DEFAULTVELOCITY 64	/* default Note On or Off velocity */
#define PITCHBENDCENTER 0x2000	/* pitch bend center position as a 14 bit word */
#define MCLKSPERQTR	24	/* MIDI clocks per qtr-note */
#define MCLKSPERSP	6	/* MIDI clocks per song position index */
#define MCCENTER	64	/* center value for controllers like Pan and Balance */


/* Standard Controllers */

    /* continuous 14 bit - MSB: 0-1f, LSB: 20-3f */
#define MC_MODWHEEL  0x01
#define MC_BREATH    0x02
#define MC_FOOT      0x04
#define MC_PORTATIME 0x05
#define MC_DATAENTRY 0x06
#define MC_VOLUME    0x07
#define MC_BALANCE   0x08
#define MC_PAN	     0x0a
#define MC_EXPRESSION 0x0b
#define MC_GENERAL1  0x10
#define MC_GENERAL2  0x11
#define MC_GENERAL3  0x12
#define MC_GENERAL4  0x13

    /* continuous 7 bit (switches: 0-3f=off, 40-7f=on) */
#define MC_SUSTAIN   0x40
#define MC_PORTA     0x41
#define MC_SUSTENUTO 0x42
#define MC_SOFTPEDAL 0x43
#define MC_HOLD2     0x45
#define MC_GENERAL5  0x50
#define MC_GENERAL6  0x51
#define MC_GENERAL7  0x52
#define MC_GENERAL8  0x53
#define MC_EXTDEPTH	0x5b
#define MC_TREMOLODEPTH 0x5c
#define MC_CHORUSDEPTH	0x5d
#define MC_CELESTEDEPTH 0x5e
#define MC_PHASERDEPTH	0x5f

    /* parameters */
#define MC_DATAINCR  0x60
#define MC_DATADECR  0x61
#define MC_NRPNL     0x62
#define MC_NRPNH     0x63
#define MC_RPNL      0x64
#define MC_RPNH      0x65

#define MC_MAX	     0x78	/* max controller value */


/* Channel Modes */

#define MM_MIN	     0x79	/* min mode value */

#define MM_RESETCTRL 0x79
#define MM_LOCAL     0x7a
#define MM_ALLOFF    0x7b
#define MM_OMNIOFF   0x7c
#define MM_OMNION    0x7d
#define MM_MONO      0x7e
#define MM_POLY      0x7f


/* Registered Parameter Numbers */
/*
    These are 16 bit values that need to be separated into two bytes for
    use with the MC_RPNH & MC_RPNL messages using 8 bit math (hi = MRP_
    >> 8, lo = MRP_ & 0xff) as opposed to 7 bit math.  This is done
    so that the defines match the numbers from the MMA.  See MIDI 1.0
    Detailed Spec v4.0 pp 12, 23 for more info.
*/

#define MRP_PBSENS	0x0000
#define MRP_FINETUNE	0x0001
#define MRP_COURSETUNE	0x0002


/* MTC Quarter Frame messages */
/*
    Qtr Frame message is F1 0nnndddd where

	nnn is a message type defined below
	dddd is 4 bit data nibble for those message types

    Each pair of nibbles is combined by the receiver into a single byte.
    There are masks and type values defined for some of these data bytes
    below.
*/

    /* message types */
#define MTCQ_FRAMEL 0x00
#define MTCQ_FRAMEH 0x10
#define MTCQ_SECL   0x20
#define MTCQ_SECH   0x30
#define MTCQ_MINL   0x40
#define MTCQ_MINH   0x50
#define MTCQ_HOURL  0x60
#define MTCQ_HOURH  0x70	/* also contains time code type */

    /* message masks */
#define MTCQ_TYPEMASK 0x70	/* mask for type bits in message */
#define MTCQ_DATAMASK 0x0f	/* mask for data bits in message */

    /* hour byte */
#define MTCH_TYPEMASK 0x60	/* mask for time code type */
#define MTCH_HOURMASK 0x1f	/* hours mask (range 0-23) */

    /* time code type values for hour byte */
#define MTCT_24FPS	    0x00
#define MTCT_25FPS	    0x20
#define MTCT_30FPS_DROP     0x40
#define MTCT_30FPS_NONDROP  0x60


/* Sys/Ex ID numbers */
/*
    Now includes 3 byte extension for the American Group.  This new
    format uses a 0x00 as the sys/ex id followed by two additional bytes
    that actually identify the manufacturer.  These new extended id
    constants are 32 bit values (24 significant bits) that can be
    managed using SPLIT_MIDX() and MAKE_MIDX() macros defined below.

    You can match or filter off one of the extended id's when using the
    RIMF_EXTID bit described above.

    example RIMatch
	{
	    RIMF_EXTID | 1,	    extend id, match one manufacturer
	    SPLIT_MIDX (MIDX_IOTA)  splits id into 3 bytes
	}
*/

    /* American Group */
#define MID_XAMERICA	    0x00
#define MID_SEQUENTIAL	    0x01
#define MID_IDP 	    0x02
#define MID_OCTAVEPLATEAU   0x03
#define MID_MOOG	    0x04
#define MID_PASSPORT	    0x05
#define MID_LEXICON	    0x06
#define MID_KURZWEIL	    0x07
#define MID_FENDER	    0x08
#define MID_AKG 	    0x0a
#define MID_VOYCE	    0x0b
#define MID_WAVEFRAME	    0x0c
#define MID_ADA 	    0x0d
#define MID_GARFIELD	    0x0e
#define MID_ENSONIQ	    0x0f
#define MID_OBERHEIM	    0x10
#define MID_APPLE	    0x11
#define MID_GREYMATTER	    0x12
#define MID_PALMTREE	    0x14
#define MID_JLCOOPER	    0x15
#define MID_LOWREY	    0x16
#define MID_ADAMSSMITH	    0x17
#define MID_EMU 	    0x18
#define MID_HARMONY	    0x19
#define MID_ART 	    0x1a
#define MID_BALDWIN	    0x1b
#define MID_EVENTIDE	    0x1c
#define MID_INVENTRONICS    0x1d
#define MID_CLARITY	    0x1f

#define MIDX_DIGITALMUSIC   0x000007L
#define MIDX_IOTA	    0x000008L
#define MIDX_IVL	    0x00000bL
#define MIDX_SOUTHERNMUSIC  0x00000cL
#define MIDX_LAKEBUTLER     0x00000dL
#define MIDX_DOD	    0x000010L
#define MIDX_PERFECTFRET    0x000014L
#define MIDX_OPCODE	    0x000016L
#define MIDX_SPATIALSOUND   0x000018L
#define MIDX_KMX	    0x000019L
#define MIDX_AXXES	    0x000020L

    /* European Group */
#define MID_PASSAC	    0x20
#define MID_SIEL	    0x21
#define MID_SYNTHAXE	    0x22
#define MID_HOHNER	    0x24
#define MID_TWISTER	    0x25
#define MID_SOLTON	    0x26
#define MID_JELLINGHAUS     0x27
#define MID_SOUTHWORTH	    0x28
#define MID_PPG 	    0x29
#define MID_JEN 	    0x2a
#define MID_SSL 	    0x2b
#define MID_AUDIOVERITRIEB  0x2c
#define MID_ELKA	    0x2f
#define MID_DYNACORD	    0x30

    /* Japanese Group */
#define MID_KAWAI	    0x40
#define MID_ROLAND	    0x41
#define MID_KORG	    0x42
#define MID_YAMAHA	    0x43
#define MID_CASIO	    0x44
#define MID_MORIDAIRA	    0x45
#define MID_KAMIYA	    0x46
#define MID_AKAI	    0x47
#define MID_JAPANVICTOR     0x48
#define MID_MEISOSHA	    0x49
#define MID_HOSHINOGAKKI    0x4a
#define MID_FUJITSU	    0x4b
#define MID_SONY	    0x4c
#define MID_NISSHINONPA     0x4d
#define MID_SYSTEMPRODUCT   0x4f

    /* Universal ID Numbers */
#define MID_UNC     0x7d
#define MID_UNRT    0x7e
#define MID_URT     0x7f


/* handy macros */

    /* pack high/low bytes of a word into midi format (7/14 bit math) */
#define MIDI_HIBYTE(word) ( (word) >> 7 & 0x7f )
#define MIDI_LOBYTE(word) ( (word) & 0x7f )

    /* unpack 2 midi bytes into a word (7/14 bit math) */
#define MIDI_WORD(hi,lo) ( (hi & 0x7f) << 7 | (lo & 0x7f) )

    /* unpack a 3 byte sys/ex id into single bytes for argument lists and RIMatch initializers */
#define SPLIT_MIDX(id)         (UBYTE)((id)>>16), (UBYTE)((id)>>8), (UBYTE)(id)

    /* make a 3 byte sys/ex id from single bytes (MAKE_MIDX(msg[1],msg[2],msg[3]) */
#define MAKE_MIDX(id0,id1,id2) ((ULONG)((id0) & 0xff)<<16 | (ULONG)((id1) & 0xff)<<8 | (ULONG)((id2) & 0xff))



#endif
