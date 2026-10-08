; softmix.s - 32 voice software mixer, 68020+
;
; This is src/modules/softmix.s (midiIn mixmodule 0.1, Rafal Michalski 1999)
; converted from Barfly/AmigaE conventions to vasm Motorola syntax and the C
; calling convention of m68k-amigaos-gcc:
;   - arguments are read from the stack in C order (first argument lowest)
;   - D2-D7/A2-A6 are preserved
;   - ExecBase is read from address 4 instead of the AmigaE globals (A4)
;   - mxcmd_setup returns only the mixer data; the mixing routine is called
;     through mx_mix(data) instead of a returned code pointer
;   - the work memory is taken from Fast RAM when there is some, otherwise
;     from any memory (the original asked for Fast RAM only, which fails on
;     machines that have none)
; The mixing code itself (from "main engine" on), cvolume and divr are
; unchanged apart from fixed-size branches in the jump tables.
;
; Assemble: vasmm68k_mot -Fhunk -m68020 -quiet -o softmix.o softmix.s
;
; C prototypes: see softmix.h

        section code,code

LVOAllocVec   = -684  ;(D0,D1)
LVOFreeVec    = -690  ;(A1)
MEMF_PUBLIC   = 1
MEMF_FAST     = 4
MEMF_CLEAR    = $10000

; stack offset of the first C argument after "movem.l d2-d7/a2-a6,-(sp)"
ARGS          = 4+11*4

        xdef _mx_setup,_mx_end,_mx_load,_mx_unload,_mx_setmixperiod
        xdef _mx_playchannel,_mx_stopchannel,_mx_stopchannelmask
        xdef _mx_freechannels,_mx_setfrequency,_mx_setvolume,_mx_mix

SMPINFO_ADDRESS = 0
SMPINFO_FRAMES = 4
SMPINFO_STEREO = 8
SMPINFO_SIZEOF = 12

;OBJECT data
;  outsampledata:PTR TO INT ; 16bit, stereo
;  channeldata:PTR TO channel
;  channelmask:LONG          ; bit 31 = channel 0
;  bufwork:PTR TO LONG ; 32bit, stereo
;  workframes:LONG ;numbuffer frames-1
;  mixfreq:LONG
;  sampledata:LONG
;  samplenum:LONG
;ENDOBJECT

;OBJECT channel
;  sampleinfo:LONG
;  actfreq:LONG
;  skip:LONG        ; = xxxxx frequency / remix frequency (xxxxx.yyyyy)
;  addmodulo:LONG   ; = yyyyy
;  loopframes:LONG
;  volumeleft:INT   ; /256 * 100% (less eq than 32767)
;  volumeright:INT  ; /256 * 100% volume left and right
;  current:LONG
;  pointer:LONG
;  modulo:LONG ; current modulo
;  flags:LONG
;ENDOBJECT

DT_OUTSAMPLEDATA=0
DT_CHANNELDATA=4
DT_CHANNELMASK=8
DT_BUFWORK=12
DT_WORKFRAMES=16
DT_MIXFREQ=20
DT_SAMPLEDATA=24
DT_SAMPLENUM=28
DT_SIZEOF=32

CHF_LOOPED=1
CHF_STEREO=2
CHF_BACKWARD=4
CHB_LOOPED=0
CHB_STEREO=1
CHB_BACKWARD=2

CH_SAMPLEDATA=0
CH_ACTFREQ=4
CH_SKIP=8
CH_ADDMODULO=12
CH_LOOPFRAMES=16
CH_VOLUMELEFT=20
CH_VOLUMERIGHT=22
CH_CURRENT=24
CH_POINTER=28
CH_MODULO=32
CH_FLAGS=36
CH_SIZEOF=40

;        $VER:midiIn mixmodule 0.1 (25.03.99) Rafal Michalski (c) 1999

;-----------------------------------------------------------------------------
; APTR mx_setup(ULONG mixfreq, ULONG channels, ULONG maxsamples,
;               APTR buffer, ULONG bufferframes)
; channels is not used, there are always 32. Returns NULL without memory.
_mx_setup:
        movem.l d2-d7/a2-a6,-(sp)
        move.l  ARGS+16(sp),d0          ; bufferframes
        lsl.l   #3,d0                   ; *8
        move.l  ARGS+8(sp),d1           ; maxsamples
        mulu    #SMPINFO_SIZEOF,d1      ; size of sampleinfo
        add.l   d1,d0
        add.l   #CH_SIZEOF*32+DT_SIZEOF,d0
        move.l  d0,d2
        move.l  #MEMF_PUBLIC|MEMF_FAST|MEMF_CLEAR,d1
        move.l  4.w,a6
        jsr     LVOAllocVec(a6)
        tst.l   d0
        bne.s   .got
        move.l  d2,d0
        move.l  #MEMF_PUBLIC|MEMF_CLEAR,d1
        jsr     LVOAllocVec(a6)
        tst.l   d0
        beq.s   .out
.got:   move.l  d0,a1
        move.l  ARGS+0(sp),DT_MIXFREQ(a1)
        move.l  ARGS+12(sp),DT_OUTSAMPLEDATA(a1)
        move.l  ARGS+16(sp),d1
        subq.l  #1,d1
        move.l  d1,DT_WORKFRAMES(a1)
        lea     DT_SIZEOF(a1),a2
        move.l  a2,DT_CHANNELDATA(a1)
        lea     CH_SIZEOF*32(a2),a2
        move.l  a2,DT_BUFWORK(a1)
        addq.l  #1,d1
        lsl.l   #3,d1                   ; *8
        add.l   d1,a2
        move.l  a2,DT_SAMPLEDATA(a1)
        move.l  ARGS+8(sp),DT_SAMPLENUM(a1)
.out:   movem.l (sp)+,d2-d7/a2-a6
        rts

;-----------------------------------------------------------------------------
; void mx_end(APTR data)
_mx_end:
        move.l  4(sp),d0
        beq.s   .out
        move.l  a6,-(sp)
        move.l  d0,a1
        move.l  4.w,a6
        jsr     LVOFreeVec(a6)
        move.l  (sp)+,a6
.out:   rts

;-----------------------------------------------------------------------------
; ULONG mx_load(APTR address, ULONG frames, ULONG stereo, APTR data)
; 16 bit signed samples. Returns the 1-based sample number, 0 if full.
_mx_load:
        move.l  16(sp),a1
        move.l  DT_SAMPLENUM(a1),d1
        subq.w  #1,d1
        move.l  DT_SAMPLEDATA(a1),a0
        moveq   #SMPINFO_SIZEOF,d0
.loop:  tst.l   (a0)
        beq.s   .found
        adda.l  d0,a0
        dbra    d1,.loop
        moveq   #0,d0
        rts
.found: move.l  4(sp),(a0)+
        move.l  8(sp),(a0)+
        move.l  12(sp),(a0)+
        move.l  DT_SAMPLENUM(a1),d0
        sub.l   d1,d0
        rts

;-----------------------------------------------------------------------------
; void mx_unload(ULONG samplenum, APTR data)
_mx_unload:
        movem.l d2-d7/a2-a6,-(sp)
        move.l  ARGS+0(sp),d0
        subq.l  #1,d0
        move.l  ARGS+4(sp),a1
        mulu    #SMPINFO_SIZEOF,d0
        move.l  DT_SAMPLEDATA(a1),a3
        add.l   d0,a3
        move.l  DT_CHANNELMASK(a1),d1
        move.l  DT_CHANNELDATA(a1),a2
        moveq   #31,d0
.loop:  cmpa.l  CH_SAMPLEDATA(a2),a3
        bne.s   .skip
        bclr    d0,d1
.skip:  lea     CH_SIZEOF(a2),a2
        dbra    d0,.loop
        move.l  d1,DT_CHANNELMASK(a1)
        clr.l   (a3)
        movem.l (sp)+,d2-d7/a2-a6
        rts

;-----------------------------------------------------------------------------
; void mx_setmixperiod(ULONG mixfreq, APTR data)
_mx_setmixperiod:
        movem.l d2-d7/a2-a6,-(sp)
        move.l  ARGS+4(sp),a1
        move.l  ARGS+0(sp),d0
        beq.s   .quit
        move.l  d0,a0
        move.l  d0,DT_MIXFREQ(a1)
        move.l  DT_CHANNELDATA(a1),a2
        moveq   #31,d7
.loop:  move.l  CH_ACTFREQ(a2),d0
        move.l  a0,d1
        bsr     divr                    ;samplefreq/mixfrequency
        movem.l d5/d6,CH_SKIP(a2)
        lea     CH_SIZEOF(a2),a2
        dbra    d7,.loop
.quit:  movem.l (sp)+,d2-d7/a2-a6
        rts

;-----------------------------------------------------------------------------
; void mx_playchannel(LONG offset, LONG freq, ULONG volume, ULONG pan,
;                     LONG loop, ULONG channel, ULONG samplenum, APTR data)
; freq < 0 plays backwards, loop < 0 is a one-shot. volume 65536 = 100%,
; pan 0..65536.
PC_OFFSET  = 0
PC_FREQ    = 4
PC_VOLUME  = 8
PC_PAN     = 12
PC_LOOP    = 16
PC_CHANNEL = 20
PC_SAMPLE  = 24
PC_DATA    = 28

_mx_playchannel:
        movem.l d2-d7/a2-a6,-(sp)
        lea     ARGS(sp),a0
        movea.l PC_DATA(a0),a1
        move.l  PC_SAMPLE(a0),d1
        subq.l  #1,d1
        move.l  PC_CHANNEL(a0),d0
        bfclr   DT_CHANNELMASK(a1){d0:1}
        move.l  d0,d7
        move.l  DT_CHANNELDATA(a1),a2
        mulu    #CH_SIZEOF,d0
        add.l   d0,a2
        move.l  DT_SAMPLEDATA(a1),a3
        mulu    #SMPINFO_SIZEOF,d1
        add.l   d1,a3
        move.l  a3,CH_SAMPLEDATA(a2)
        beq     psquit
        tst.l   SMPINFO_STEREO(a3)
        sne     d2
        andi.w  #CHF_STEREO,d2
        move.l  PC_FREQ(a0),d0
        bpl.s   .forw
        bset    #CHB_BACKWARD,d2
        neg.l   d0
.forw:  move.l  DT_MIXFREQ(a1),d1
        move.l  d0,CH_ACTFREQ(a2)
        move.b  d2,CH_FLAGS(a2)
        bsr     divr                    ;samplefreq/mixfrequency
        move.l  d5,CH_SKIP(a2)
        move.l  d6,CH_ADDMODULO(a2)
        clr.l   CH_MODULO(a2)
        move.l  PC_VOLUME(a0),d0
        move.l  PC_PAN(a0),d1
        bsr     cvolume
        move.w  d1,CH_VOLUMELEFT(a2)
        move.w  d2,CH_VOLUMERIGHT(a2)
        move.l  SMPINFO_ADDRESS(a3),a4
        btst    #CHB_BACKWARD,CH_FLAGS(a2)
        bne.s   lpbck
        move.l  SMPINFO_FRAMES(a3),d1
        tst.l   SMPINFO_STEREO(a3)
        beq.s   .nlopm
        lea     0(a4,d1.l*2),a4
.nlopm: lea     0(a4,d1.l*2),a4
        move.l  PC_LOOP(a0),d2
        spl     d0
        andi.w  #CHF_LOOPED,d0
        or.b    d0,CH_FLAGS(a2)
        move.l  d1,d0
        sub.l   d2,d0
        neg.l   d1
nlpbk:  add.l   PC_OFFSET(a0),d1
lpbken: move.l  d0,CH_LOOPFRAMES(a2)
        move.l  d1,CH_CURRENT(a2)
        move.l  a4,CH_POINTER(a2)
        bfset   DT_CHANNELMASK(a1){d7:1}
psquit: movem.l (sp)+,d2-d7/a2-a6
        rts
lpbck:  moveq   #0,d1
        move.l  PC_LOOP(a0),d0
        bmi.s   nlpbk
        bset    #CHB_LOOPED,CH_FLAGS(a2)
        tst.l   SMPINFO_STEREO(a3)
        beq.s   .nlopm
        lea     0(a4,d0.l*2),a4
.nlopm: lea     0(a4,d0.l*2),a4
        move.l  PC_OFFSET(a0),d1
        sub.l   d0,d1
        bpl.s   .nover
        moveq   #0,d1
.nover: neg.l   d0
        add.l   SMPINFO_FRAMES(a3),d0
        bra.s   lpbken

;-----------------------------------------------------------------------------
; void mx_stopchannel(ULONG channel, APTR data)
_mx_stopchannel:
        move.l  8(sp),a1
        move.l  4(sp),d0
        bfclr   DT_CHANNELMASK(a1){d0:1}
        rts

;-----------------------------------------------------------------------------
; void mx_stopchannelmask(ULONG mask, APTR data)     bit n = channel n
_mx_stopchannelmask:
        move.l  8(sp),a1
        move.l  4(sp),d0
        move.l  d2,a0
        moveq   #31,d2
.loop:  lsr.l   #1,d0
        roxl.l  #1,d1
        dbra    d2,.loop
        not.l   d1
        and.l   d1,DT_CHANNELMASK(a1)
        move.l  a0,d2
        rts

;-----------------------------------------------------------------------------
; ULONG mx_freechannels(ULONG mask, APTR data)   bit n set: channel n idle
_mx_freechannels:
        move.l  8(sp),a1
        move.l  d2,a0
        move.l  DT_CHANNELMASK(a1),d1
        moveq   #31,d2
.loop:  lsr.l   #1,d1
        roxl.l  #1,d0
        dbra    d2,.loop
        move.l  a0,d2
        not.l   d0
        and.l   4(sp),d0
        rts

;-----------------------------------------------------------------------------
; void mx_setfrequency(LONG freq, ULONG channel, APTR data)
; a change of sign turns the playing direction around in place
_mx_setfrequency:
        movem.l d2-d7/a2-a6,-(sp)
        move.l  ARGS+8(sp),a1
        move.l  ARGS+0(sp),d1
        move.l  ARGS+4(sp),d0
        move.l  d0,d7
        move.l  DT_CHANNELDATA(a1),a2
        mulu    #CH_SIZEOF,d0
        add.l   d0,a2
        move.l  d1,d0
        smi     d1
        btst    #CHB_BACKWARD,CH_FLAGS(a2)
        sne     d2
        eor.b   d2,d1
        bne.s   .chnge
        tst.l   d0
        bpl.s   .mixit
        neg.l   d0
.mixit: move.l  d0,CH_ACTFREQ(a2)
        move.l  DT_MIXFREQ(a1),d1
        bsr     divr                    ;samplefreq/mixfrequency
        movem.l d5/d6,CH_SKIP(a2)
.quit:  movem.l (sp)+,d2-d7/a2-a6
        rts
.chnge: bfclr   DT_CHANNELMASK(a1){d7:1}
        beq.s   .quit
        move.l  CH_SAMPLEDATA(a2),a3
        move.l  SMPINFO_ADDRESS(a3),d3
        move.l  SMPINFO_FRAMES(a3),d1
        tst.l   d0
        bmi.s   .backward
        move.l  CH_POINTER(a2),d2
        sub.l   d3,d2
        tst.l   SMPINFO_STEREO(a3)
        beq.s   .mono
        add.l   d1,d3
        add.l   d1,d3
        asr.l   #1,d2
.mono:  add.l   d1,d3
        add.l   d1,d3
        asr.l   #1,d2
        sub.l   d1,d2
        add.l   CH_CURRENT(a2),d2
        bclr    #CHB_BACKWARD,CH_FLAGS(a2)
        movem.l d2/d3,CH_CURRENT(a2)
.mixit2:move.l  d0,CH_ACTFREQ(a2)
        move.l  DT_MIXFREQ(a1),d1
        bsr     divr                    ;samplefreq/mixfrequency
        movem.l d5/d6,CH_SKIP(a2)
        bfset   DT_CHANNELMASK(a1){d7:1}
        movem.l (sp)+,d2-d7/a2-a6
        rts
.backward:
        btst    #CHB_LOOPED,CH_FLAGS(a2)
        beq.s   .nolop
        move.l  d1,d2
        sub.l   CH_LOOPFRAMES(a2),d2
        sub.l   d2,d1
        tst.l   SMPINFO_STEREO(a3)
        beq.s   .mono2
        add.l   d2,d2
.mono2: add.l   d2,d2
        add.l   d2,d3
.nolop: add.l   CH_CURRENT(a2),d1
        bpl.s   .nlok
        moveq   #0,d1
.nlok:  bset    #CHB_BACKWARD,CH_FLAGS(a2)
        movem.l d1/d3,CH_CURRENT(a2)
        neg.l   d0
        bra.s   .mixit2

;-----------------------------------------------------------------------------
; void mx_setvolume(ULONG volume, ULONG pan, ULONG channel, APTR data)
_mx_setvolume:
        movem.l d2-d7/a2-a6,-(sp)
        move.l  ARGS+0(sp),d0
        move.l  ARGS+4(sp),d1
        bsr.s   cvolume
        move.l  ARGS+12(sp),a1
        move.l  DT_CHANNELDATA(a1),a0
        move.l  ARGS+8(sp),d0
        mulu    #CH_SIZEOF,d0
        add.l   d0,a0
        movem.w d1/d2,CH_VOLUMELEFT(a0)
        movem.l (sp)+,d2-d7/a2-a6
        rts

;-----------------------------------------------------------------------------
; void mx_mix(APTR data)
; Mixes DT_WORKFRAMES+1 frames into DT_OUTSAMPLEDATA (16 bit stereo).
_mx_mix:
        movem.l d2-d7/a2-a6,-(sp)
        move.l  ARGS(sp),a1
        bsr     mixptr
        movem.l (sp)+,d2-d7/a2-a6
        rts

;#############################################################################33
;                        calculate left and right volume
;#############################################################################33
cvolume:   ;<D0=volume(65536=100%), D1=panorama(0:65536) >D1,D2 l,r(256=100%)
        LSR.L   #6,D1
        MOVE.W  #1024,D2
        SUB.W   D1,D2
        MOVE.L  #$100000,D3
        MULU    D1,D1
        MULU    D2,D2
        NEG.L   D1
        ADD.L   D3,D1
        NEG.L   D2
        ADD.L   D3,D2
        LSR.L   #8,D0
        MULU.L  D0,D1
        DIVU.L  D3,D1
        MULU.L  D0,D2
        DIVU.L  D3,D2
        RTS
;#############################################################################33
;                        divide xxxx/yyyy=aaaa.bbbb
;#############################################################################33
divr:   MOVEQ   #32,D3      ;D5.D6=D0/D1
        BFFFO   D0{0:32},D4
        SUB.W   D4,D3
        LSL.L   D4,D0
        DIVUL.L D1,D2:D0
        MOVE.L  D0,D5
        LSR.L   D4,D5       ;xxx.
        MOVE.L  D0,D6       ;xxx.yyy
        MOVE.L  D2,D0
        TST.W   D3
        BLE.S   dkoniec
dloop:  BFFFO   D0{0:32},D4
        SUB.W   D4,D3
        BPL.S   dok
        ADD.W   D3,D4
dok:    LSL.L   D4,D0
        DIVUL.L D1,D2:D0
        LSL.L   D4,D6
        ADD.L   D0,D6
        MOVE.L  D2,D0
        TST.W   D3
        BGT.S   dloop
dkoniec: RTS

;###############################################################################
;                                main engine
;###############################################################################

;  (A1) = data
mixptr: MOVE.L  DT_CHANNELDATA(A1),A2
        MOVE.L  DT_CHANNELMASK(A1),D0
        BNE.S   something
        MOVE.L  DT_OUTSAMPLEDATA(A1),A0
        MOVE.L  DT_WORKFRAMES(A1),D0
zerolp: CLR.L   (A0)+
        DBRA    D0,zerolp
        RTS
something:
        MOVEQ   #0,D1
        BFFFO   D0{0:32},D1
        MOVE.L  D0,A4
        MOVEQ   #CH_SIZEOF,D0
        MULU    D1,D0
        ADD.L   D0,A2
        NOT.W   D1          ;MSB-LSB <=> LSB-MSB
        MOVE.L  DT_BUFWORK(A1),A5
        MOVE.L  DT_WORKFRAMES(A1),D3
        MOVE.L  CH_MODULO(A2),D4
        MOVE.L  CH_ADDMODULO(A2),A6
        MOVE.L  CH_SKIP(A2),D2
        MOVEM.W CH_VOLUMELEFT(A2),D5/D6
        MOVE.L  CH_POINTER(A2),A3
        MOVE.B  CH_FLAGS(A2),D7
        MOVE.L  CH_CURRENT(A2),A0
        ANDI.W  #CHF_BACKWARD|CHF_STEREO,D7
        ADD.W   D7,D7
        EXG     D1,A0
        JMP    mix_jumpf(PC,D7.W)
mix_jumpf:
        bra.w   firstloopm
        bra.w   firstloop
        bra.w   firstloopmbk
        bra.w   firstloopbk
nextloopend1:
        MOVE.L  A0,D1
        MOVE.L  A4,D0
        BCLR    D1,D0
        BRA.S   nextloopskip1
nextloop1:
        MOVE.L  A0,D1
        MOVE.L  A4,D0
nextloopskip1:
        NEG.W   D1      ;LSB-MSB <=> MSB-LSB +1
        MOVEQ   #32,D2
        SUB.W   D1,D2
        BEQ.S   firstz
        BFFFO   D0{D1:D2},D1
        BNE.S   middle
firstz:
        MOVE.L  D0,DT_CHANNELMASK(A1)
        MOVE.L  DT_OUTSAMPLEDATA(A1),A4
        MOVE.L  DT_BUFWORK(A1),A5
        MOVE.L  DT_WORKFRAMES(A1),D3
        ADDQ.W #1,D3
        ADD.W  D3,D3
        SUBQ.W #1,D3
        MOVE.W #$7FFF,D1
firstzl:
        MOVE.L  (A5)+,D0         ; the xdata 24 bit RAW LEFT
        ASR.L  #8,D0            ; convert to 16 bit
        MOVE.L D0,D7
        EXT.L  D7
        CMP.L  D7,D0
        BEQ.S  fznothigh
        ROL.L  #8,D0
        EXT.W  D0
        EOR.W  D1,D0        ;FFFF to 8000,  0000 to 7FFF
fznothigh:
        MOVE.W D0,(A4)+
        DBRA   D3,firstzl
        RTS

nextloopend2:
        MOVE.L  A0,D1
        MOVE.L  A4,D0
        NOT.W   D1        ;MSB-LSB <=> LSB-MSB
        BCLR    D1,D0
        BRA.S   nextloopskip2
nextloop2:
        MOVE.L  A0,D1
        MOVE.L  A4,D0
nextloopskip2:
        SWAP    D1
middle: MOVEQ   #CH_SIZEOF,D7
        MULU    D1,D7
        MOVE.L  DT_CHANNELDATA(A1),A2
        ADD.L   D7,A2

        MOVE.W  D1,D7
        ADDQ.W  #1,D1
        MOVEQ   #32,D2
        SUB.W   D1,D2
        BEQ.S   midskip
        BFFFO   D0{D1:D2},D1
        MOVEA.L D0,A4
midskip:SNE     D0
        SWAP    D1
        MOVE.W  D7,D1

        MOVE.L  DT_BUFWORK(A1),A5
        MOVE.L  DT_WORKFRAMES(A1),D3
        MOVE.L  CH_MODULO(A2),D4
        MOVE.L  CH_ADDMODULO(A2),A6
        MOVE.L  CH_SKIP(A2),D2
        MOVEM.W CH_VOLUMELEFT(A2),D5/D6
        MOVE.L  CH_POINTER(A2),A3
        MOVE.B  CH_FLAGS(A2),D7
        MOVE.L  CH_CURRENT(A2),A0
        EXG     D1,A0
        ANDI.W  #CHF_BACKWARD|CHF_STEREO,D7
        ADD.W   D7,D7
        TST.B   D0
        BEQ.S   last
        JMP     mix_jumps(PC,D7.W)
mix_jumps:
        bra.w   secloopm
        bra.w   secloop
        bra.w   secloopmbk
        bra.w   secloopbk

last:   MOVEM.L A0/A4,-(A7)
        MOVE.L  DT_OUTSAMPLEDATA(A1),A4
        JMP     mix_jumpl(PC,D7.W)
mix_jumpl:
        bra.w   lastloopm
        bra.w   lastloop
        bra.w   lastloopmbk
        bra.w   lastloopbk
nextloopend3:
        MOVEM.L (A7)+,D0/D1
        NOT.W   D0
        BCLR    D0,D1
        MOVE.L  D1,DT_CHANNELMASK(A1)
        RTS
nextloop3:
        MOVEM.L (A7)+,A0/A4
        MOVE.L  A4,DT_CHANNELMASK(A1)
        RTS

        

; A2 = channel data, A5 = buffer32, D3=buflen-1 (in frames), D4 = modulo, A6 = addmodulo, D2 = skip, D5,D6 = voll,volr
; first & second
; A3 = end sample, D1 = -offset
; A3 = start loop, D1 = +offset
; last
; A4 = buffer16, A3 = end sample, D1 = -offset
; A4 = buffer16, A3 = start loop, D1 = +offset


firstloop:
        MOVEM.W 0(A3,D1.L*4),D0/D7   ; D1 < 0 and increases *2 / *4 stereo
        MULS    D6,D0             ; D6 = leftvolume
        MOVE.L  D0,(A5)+          ; the xdata 24 bit RAW LEFT
        MULS    D5,D7             ; D5 = rightvolume
        MOVE.L  D7,(A5)+          ; the xdata 24 bit RAW RIGHT
        ADD.L  A6,D4            ; D4=modulo, A6=addmodulo
        ADDX.L D2,D1            ; D2=skip
        BPL.S  itsendofsample1
notendofsample1:
        DBRA   D3,firstloop
        MOVE.L D4,CH_MODULO(A2)
        MOVE.L D1,CH_CURRENT(A2)
        BRA    nextloop1
itsendofsample1:
        SUB.L  CH_LOOPFRAMES(A2),D1
        BTST   #CHB_LOOPED,CH_FLAGS(A2)
        BNE.S  notendofsample1
        BRA.S  endofsample1
clearloop1:
        CLR.L  (A5)+
        CLR.L  (A5)+
endofsample1:
        DBRA   D3,clearloop1
        BRA    nextloopend1

firstloopm:
        MOVE.W 0(A3,D1.L*2),D0   ; D1 < 0 and increases *2 / *4 stereo
        MOVE.W  D0,D7
        MULS    D6,D0             ; D6 = leftvolume
        MOVE.L  D0,(A5)+          ; the xdata 24 bit RAW LEFT
        MULS    D5,D7             ; D5 = rightvolume
        MOVE.L  D7,(A5)+          ; the xdata 24 bit RAW RIGHT
        ADD.L  A6,D4            ; D4=modulo, A6=addmodulo
        ADDX.L D2,D1            ; D2=skip
        BPL.S  itsendofsample1m
notendofsample1m:
        DBRA   D3,firstloopm
        MOVE.L D4,CH_MODULO(A2)
        MOVE.L D1,CH_CURRENT(A2)
        BRA    nextloop1
itsendofsample1m:
        SUB.L  CH_LOOPFRAMES(A2),D1
        BTST   #CHB_LOOPED,CH_FLAGS(A2)
        BNE.S  notendofsample1m
        BRA.S  endofsample1

secloop:
        MOVEM.W 0(A3,D1.L*4),D0/D7   ; D1 < 0 and increases *2 / *4 stereo
        MULS   D6,D0             ; D6 = leftvolume
        ADD.L  D0,(A5)+         ; the xdata 24 bit RAW LEFT
        MULS   D5,D7             ; D5 = rightvolume
        ADD.L  D7,(A5)+         ; the xdata 24 bit RAW RIGHT
        ADD.L  A6,D4            ; D4=modulo, A6=addmodulo
        ADDX.L D2,D1            ; D2=skip
        BPL.S  itsendofsample2
notendofsample2:
        DBRA   D3,secloop
        MOVE.L D4,CH_MODULO(A2)
        MOVE.L D1,CH_CURRENT(A2)
        BRA    nextloop2
itsendofsample2:
        SUB.L  CH_LOOPFRAMES(A2),D1
        BTST   #CHB_LOOPED,CH_FLAGS(A2)
        BNE.S  notendofsample2
        BRA    nextloopend2

secloopm:
        MOVE.W 0(A3,D1.L*2),D0   ; D1 < 0 and increases *2 / *4 stereo
        MOVE.W D0,D7
        MULS   D6,D0             ; D6 = leftvolume
        ADD.L  D0,(A5)+         ; the xdata 24 bit RAW LEFT
        MULS   D5,D7             ; D5 = rightvolume
        ADD.L  D7,(A5)+         ; the xdata 24 bit RAW RIGHT
        ADD.L  A6,D4            ; D4=modulo, A6=addmodulo
        ADDX.L D2,D1            ; D2=skip
        BPL.S  itsendofsample2m
notendofsample2m:
        DBRA   D3,secloopm
        MOVE.L D4,CH_MODULO(A2)
        MOVE.L D1,CH_CURRENT(A2)
        BRA    nextloop2
itsendofsample2m:
        SUB.L  CH_LOOPFRAMES(A2),D1
        BTST   #CHB_LOOPED,CH_FLAGS(A2)
        BNE.S  notendofsample2m
        BRA    nextloopend2

lastloop:
        MOVEM.W 0(A3,D1.L*4),D0/D7   ; D1 < 0 and increases *2 / *4 stereo
        MOVE.L D7,A0
        MULS   D6,D0             ; D6 = leftvolume
        ADD.L  (A5)+,D0         ; the xdata 24 bit RAW LEFT
        ASR.L  #8,D0            ; convert to 16 bit
        MOVE.L D0,D7
        EXT.L  D7
        CMP.L  D7,D0
        BEQ.S  nothigh1
        ROL.L  #8,D0
        EXT.W  D0
        EORI.W  #$7FFF,D0        ;FFFF to 8000,  0000 to 7FFF
nothigh1:
        MOVE.W D0,(A4)+
        MOVE.L A0,D0
        MULS   D5,D0             ; D5 = rightvolume
        ADD.L  (A5)+,D0         ; the xdata 24 bit RAW LEFT
        ASR.L  #8,D0            ; convert to 16 bit
        MOVE.L D0,D7
        EXT.L  D7
        CMP.L  D7,D0
        BEQ.S  nothigh2
        ROL.L  #8,D0
        EXT.W  D0
        EORI.W  #$7FFF,D0        ;FFFF to 8000,  0000 to 7FFF
nothigh2:
        MOVE.W D0,(A4)+
        ADD.L  A6,D4            ; D4=modulo, D3=addmodulo
        ADDX.L D2,D1            ; D2=skip
        BPL.S  itsendofsample3
notendofsample3:
        DBRA   D3,lastloop
        MOVE.L D4,CH_MODULO(A2)
        MOVE.L D1,CH_CURRENT(A2)
        BRA    nextloop3
itsendofsample3:
        SUB.L  CH_LOOPFRAMES(A2),D1
        BTST   #CHB_LOOPED,CH_FLAGS(A2)
        BNE.S  notendofsample3
        ADD.W  D3,D3
        BEQ.S  endofsample3
restuniskip:
        SUBQ.W #1,D3
        MOVE.W #$7FFF,D1
restuniloop:
        MOVE.L  (A5)+,D0         ; the xdata 24 bit RAW LEFT
        ASR.L  #8,D0            ; convert to 16 bit
        MOVE.L D0,D7
        EXT.L  D7
        CMP.L  D7,D0
        BEQ.S  nothigh3
        ROL.L  #8,D0
        EXT.W  D0
        EOR.W  D1,D0        ;FFFF to 8000,  0000 to 7FFF
nothigh3:
        MOVE.W D0,(A4)+
        DBRA   D3,restuniloop
endofsample3:
        BRA    nextloopend3

lastloopm:
        MOVE.W 0(A3,D1.L*2),D0   ; D1 < 0 and increases *2 / *4 stereo
        MOVE.L D0,A0
        MULS   D6,D0             ; D6 = leftvolume
        ADD.L  (A5)+,D0         ; the xdata 24 bit RAW LEFT
        ASR.L  #8,D0            ; convert to 16 bit
        MOVE.L D0,D7
        EXT.L  D7
        CMP.L  D7,D0
        BEQ.S  nothigh1m
        ROL.L  #8,D0
        EXT.W  D0
        EORI.W #$7FFF,D0        ;FFFF to 8000,  0000 to 7FFF
nothigh1m:
        MOVE.W D0,(A4)+
        MOVE.L A0,D0
        MULS   D5,D0             ; D5 = rightvolume
        ADD.L  (A5)+,D0         ; the xdata 24 bit RAW LEFT
        ASR.L  #8,D0            ; convert to 16 bit
        MOVE.L D0,D7
        EXT.L  D7
        CMP.L  D7,D0
        BEQ.S  nothigh2m
        ROL.L  #8,D0
        EXT.W  D0
        EORI.W #$7FFF,D0        ;FFFF to 8000,  0000 to 7FFF
nothigh2m:
        MOVE.W D0,(A4)+
        ADD.L  A6,D4            ; D4=modulo, D3=addmodulo
        ADDX.L D2,D1            ; D2=skip
        BPL.S  itsendofsample3m
notendofsample3m:
        DBRA   D3,lastloopm
        MOVE.L D4,CH_MODULO(A2)
        MOVE.L D1,CH_CURRENT(A2)
        BRA    nextloop3
itsendofsample3m:
        SUB.L  CH_LOOPFRAMES(A2),D1
        BTST   #CHB_LOOPED,CH_FLAGS(A2)
        BNE.S  notendofsample3m
        ADD.W  D3,D3
        BEQ    nextloopend3
        BRA    restuniskip



;/* bckwrd */

; A3 = start loop, A5 = buffer, D3=buflen-1 (in frames), D1 = +offset, D4 = modulo, A6 = addmodulo, D2 = skip, D6,D5 ; voll,volr
firstloopbk:
        MOVEM.W 0(A3,D1.L*4),D0/D7   ; D1 < 0 and increases *2 / *4 stereo
        MULS   D6,D0             ; D6 = leftvolume
        MOVE.L D0,(A5)+          ; the xdata 24 bit RAW LEFT
        MULS   D5,D7             ; D5 = rightvolume
        MOVE.L D7,(A5)+          ; the xdata 24 bit RAW RIGHT
        ADD.L  A6,D4            ; D4=modulo, A6=addmodulo
        SUBX.L D2,D1            ; D2=skip
        BMI.S  itsendofsample1bk
notendofsample1bk:
        DBRA   D3,firstloopbk
        MOVE.L D4,CH_MODULO(A2)
        MOVE.L D1,CH_CURRENT(A2)
        BRA    nextloop1
itsendofsample1bk:
        ADD.L  CH_LOOPFRAMES(A2),D1
        BTST   #CHB_LOOPED,CH_FLAGS(A2)
        BNE.S  notendofsample1bk
        BRA    endofsample1

firstloopmbk:
        MOVE.W 0(A3,D1.L*2),D0   ; D1 < 0 and increases *2 / *4 stereo
        MOVE.W  D0,D7
        MULS    D6,D0             ; D6 = leftvolume
        MOVE.L  D0,(A5)+          ; the xdata 24 bit RAW LEFT
        MULS    D5,D7             ; D5 = rightvolume
        MOVE.L  D7,(A5)+          ; the xdata 24 bit RAW RIGHT
        ADD.L  A6,D4            ; D4=modulo, A6=addmodulo
        SUBX.L D2,D1            ; D2=skip
        BMI.S  itsendofsample1mbk
notendofsample1mbk:
        DBRA   D3,firstloopmbk
        MOVE.L D4,CH_MODULO(A2)
        MOVE.L D1,CH_CURRENT(A2)
        BRA    nextloop1
itsendofsample1mbk:
        ADD.L  CH_LOOPFRAMES(A2),D1
        BTST   #CHB_LOOPED,CH_FLAGS(A2)
        BNE.S  notendofsample1mbk
        BRA    endofsample1

secloopbk:
        MOVEM.W 0(A3,D1.L*4),D0/D7   ; D1 < 0 and increases *2 / *4 stereo
        MULS   D6,D0             ; D6 = leftvolume
        ADD.L  D0,(A5)+         ; the xdata 24 bit RAW LEFT
        MULS   D5,D7             ; D5 = rightvolume
        ADD.L  D7,(A5)+         ; the xdata 24 bit RAW RIGHT
        ADD.L  A6,D4            ; D4=modulo, A6=addmodulo
        SUBX.L D2,D1            ; D2=skip
        BMI.S  itsendofsample2bk
notendofsample2bk:
        DBRA   D3,secloopbk
        MOVE.L D4,CH_MODULO(A2)
        MOVE.L D1,CH_CURRENT(A2)
        BRA    nextloop2
itsendofsample2bk:
        ADD.L  CH_LOOPFRAMES(A2),D1
        BTST   #CHB_LOOPED,CH_FLAGS(A2)
        BNE.S  notendofsample2bk
        BRA    nextloopend2

secloopmbk:
        MOVE.W 0(A3,D1.L*2),D0   ; D1 < 0 and increases *2 / *4 stereo
        MOVE.W D0,D7
        MULS   D6,D0             ; D6 = leftvolume
        ADD.L  D0,(A5)+         ; the xdata 24 bit RAW LEFT
        MULS   D5,D7             ; D5 = rightvolume
        ADD.L  D7,(A5)+         ; the xdata 24 bit RAW RIGHT
        ADD.L  A6,D4            ; D4=modulo, A6=addmodulo
        SUBX.L D2,D1            ; D2=skip
        BMI.S  itsendofsample2mbk
notendofsample2mbk:
        DBRA   D3,secloopmbk
        MOVE.L D4,CH_MODULO(A2)
        MOVE.L D1,CH_CURRENT(A2)
        BRA    nextloop2
itsendofsample2mbk:
        ADD.L  CH_LOOPFRAMES(A2),D1
        BTST   #CHB_LOOPED,CH_FLAGS(A2)
        BNE.S  notendofsample2mbk
        BRA    nextloopend2

lastloopbk:
        MOVEM.W 0(A3,D1.L*4),D0/D7   ; D1 < 0 and increases *2 / *4 stereo
        MOVE.L D7,A0
        MULS   D6,D0             ; D6 = leftvolume
        ADD.L  (A5)+,D0         ; the xdata 24 bit RAW LEFT
        ASR.L  #8,D0            ; convert to 16 bit
        MOVE.L D0,D7
        EXT.L  D7
        CMP.L  D7,D0
        BEQ.S  nothigh1bk
        ROL.L  #8,D0
        EXT.W  D0
        EORI.W  #$7FFF,D0        ;FFFF to 8000,  0000 to 7FFF
nothigh1bk:
        MOVE.W D0,(A4)+
        MOVE.L A0,D0
        MULS   D5,D0             ; D5 = rightvolume
        ADD.L  (A5)+,D0         ; the xdata 24 bit RAW LEFT
        ASR.L  #8,D0            ; convert to 16 bit
        MOVE.L D0,D7
        EXT.L  D7
        CMP.L  D7,D0
        BEQ.S  nothigh2bk
        ROL.L  #8,D0
        EXT.W  D0
        EORI.W  #$7FFF,D0        ;FFFF to 8000,  0000 to 7FFF
nothigh2bk:
        MOVE.W D0,(A4)+
        ADD.L  A6,D4            ; D4=modulo, D3=addmodulo
        SUBX.L D2,D1            ; D2=skip
        BMI.S  itsendofsample3bk
notendofsample3bk:
        DBRA   D3,lastloopbk
        MOVE.L D4,CH_MODULO(A2)
        MOVE.L D1,CH_CURRENT(A2)
        BRA    nextloop3
itsendofsample3bk:
        ADD.L  CH_LOOPFRAMES(A2),D1
        BTST   #CHB_LOOPED,CH_FLAGS(A2)
        BNE.S  notendofsample3bk
        ADD.W  D3,D3
        BEQ    nextloopend3
        BRA    restuniskip

lastloopmbk:
        MOVE.W 0(A3,D1.L*2),D0   ; D1 < 0 and increases *2 / *4 stereo
        MOVE.L D0,A0
        MULS   D6,D0             ; D6 = leftvolume
        ADD.L  (A5)+,D0         ; the xdata 24 bit RAW LEFT
        ASR.L  #8,D0            ; convert to 16 bit
        MOVE.L D0,D7
        EXT.L  D7
        CMP.L  D7,D0
        BEQ.S  nothigh1mbk
        ROL.L  #8,D0
        EXT.W  D0
        EORI.W #$7FFF,D0        ;FFFF to 8000,  0000 to 7FFF
nothigh1mbk:
        MOVE.W D0,(A4)+
        MOVE.L A0,D0
        MULS   D5,D0             ; D5 = rightvolume
        ADD.L  (A5)+,D0         ; the xdata 24 bit RAW LEFT
        ASR.L  #8,D0            ; convert to 16 bit
        MOVE.L D0,D7
        EXT.L  D7
        CMP.L  D7,D0
        BEQ.S  nothigh2mbk
        ROL.L  #8,D0
        EXT.W  D0
        EORI.W #$7FFF,D0        ;FFFF to 8000,  0000 to 7FFF
nothigh2mbk:
        MOVE.W D0,(A4)+
        ADD.L  A6,D4            ; D4=modulo, D3=addmodulo
        SUBX.L D2,D1            ; D2=skip
        BMI.S  itsendofsample3mbk
notendofsample3mbk:
        DBRA   D3,lastloopmbk
        MOVE.L D4,CH_MODULO(A2)
        MOVE.L D1,CH_CURRENT(A2)
        BRA    nextloop3
itsendofsample3mbk:
        ADD.L  CH_LOOPFRAMES(A2),D1
        BTST   #CHB_LOOPED,CH_FLAGS(A2)
        BNE.S  notendofsample3mbk
        ADD.W  D3,D3
        BEQ    nextloopend3
        BRA    restuniskip
