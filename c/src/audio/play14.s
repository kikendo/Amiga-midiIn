; play14.s - 14-bit Paula output and its 32 voice mixer, 68020+
;
; The interrupt and mixing code of src/play14unlim.e (midiIn, Rafal
; Michalski), converted to vasm Motorola syntax. The code is unchanged
; except that the four hand-encoded 68020 instructions are written as
; mnemonics (same encoding) and the E constants are spelled out below.
;
; Output: channels 0 and 3 play the left side, 1 and 2 the right; on each
; side one channel at volume 64 plays the high byte and one at volume 1 the
; low bits, corrected by a 256 byte calibration table. Every AUD1 interrupt
; (one per BUFFLEN output frames) converts the last mixed buffer into the
; four Chip RAM buffers and Causes a software interrupt that mixes the
; next one.
;
; Not called from C: p14_intplay is installed with SetIntVector(INTB_AUD1)
; and p14_mixchannels as the code of a software interrupt, both with the
; datamain structure (see paula14.c) as is_Data.
;
; Assemble: vasmm68k_mot -Fhunk -m68020 -quiet -o play14.o play14.s

        section code,code

        xdef    _p14_intplay,_p14_mixchannels

BUFFLEN       = 256
LVOCause      = -180
INTREQ        = $9C
INTF_AUD1     = $0100

; struct p14_data
DM_CHAN       = 0
DM_CHANNELMASK= 4       ; bit n = channel n playing
DM_BUSY       = 8
DM_SOFTINT    = 12
DM_BUFFERWORK = 16
DM_BUFFERCOPY = 20
DM_BUFFERSWAP = 24
DM_CALIBRATION= 28      ; middle of the 256 byte table
DM_CHIPADR    = 32      ; four Chip RAM buffers (+4)

; struct p14_envelope, fixed point 1.0 = $01000000
EVALT         = 0
EVCLIMB       = 4
EVHIT         = 8
EVDECAY       = 12
EVSUSTHIT     = 16

; struct p14_channel
CH_CURRENT    = 0
CH_SKIP       = 4
CH_ADDMODULO  = 8
CH_MODULO     = 12
CH_VOLUMEL    = 16      ; 256 = 100%
CH_VOLUMER    = 20
CH_SMPENDDATA = 24
CH_LOOPLENGTH = 28
CH_ENVEL      = 32
CH_LOOP       = 52
CH_STEREO     = 54
CH_SIZEOF     = 56

_p14_intplay:
				MOVE.W  #INTF_AUD1,INTREQ(A0)
				MOVEM.L D2-D7/A2-A4/A6,-(A7)
				MOVE.L  DM_BUFFERSWAP(A1),A0
				MOVEM.L DM_CALIBRATION(A1),A2-A6
				MOVE.W  #BUFFLEN/4-2,D7
				MOVEQ   #$40,D5
				MOVEQ   #1,D0
				MOVEQ   #0,D4
ipcalibrloop:
				MOVE.B  D0,D4
ipcloop1:
				LSL.L   #8,D1
				MOVE.L  (A0)+,D0
				MOVE.B  D0,D1
				LSR.B   #2,D1
				ASR.W   #8,D0
				LSL.L   #8,D2
				SUB.B   D5,D1
				ADD.B   0(A2,D0.W),D1
				MOVE.B  D0,D2
				SWAP    D0
				LSL.L   #8,D3
				MOVE.B  D0,D3
				LSR.B   #2,D3
				ASR.W   #8,D0
				SUB.B   D5,D3
				ADD.B   0(A2,D0.W),D3
				LSL.L   #8,D4
				BCC.S   ipcalibrloop
				MOVE.L  D1,(A5)+  ;LO RIGHT
				MOVE.L  D2,(A6)+  ;HI RIGHT
				MOVE.B  D0,D4
				MOVE.L  D4,(A4)+  ;HI LEFT
				MOVEQ   #1,D4
				MOVE.L  D3,(A3)+  ;LO LEFT
				DBRA    D7,ipcloop1
				MOVEM.L (A7)+,D2-D7/A2-A4/A6
				TST.L   DM_BUSY(A1)
				BEQ.S   ipsoftdone
				CLR.L   DM_CHANNELMASK(A1)
				RTS
ipsoftdone:
				SUBQ.L  #1,DM_BUSY(A1)
				MOVE.L  DM_SOFTINT(A1),A1
				JMP     LVOCause(A6)

_p14_mixchannels:                ; executed by softint
				MOVEM.L D2-D7/A2-A4/A6,-(A7)
				MOVE.L  DM_CHAN(A1),A2           ; first channel
				SUBA.L  A4,A4
				MOVE.L  DM_CHANNELMASK(A1),D7
				BNE.S   mfirst
				MOVE.L  DM_BUFFERCOPY(A1),A4
				MOVE.W  #BUFFLEN-1,D1
				MOVEQ   #0,D0
mclearstuff:
				MOVE.L  D0,(A4)+
				DBRA    D1,mclearstuff
				BRA     ipendofint
mfirstloop:
				ADDQ.L  #1,A4
				LEA     CH_SIZEOF(A2),A2
mfirst: LSR.L   #1,D7
				BCC.S   mfirstloop
				MOVE.L  DM_BUFFERWORK(A1),A5
				LEA     BUFFLEN*8(A5),A6
				LEA     CH_ENVEL(A2),A3
				MOVEM.L CH_VOLUMEL(A2),D5/D6
				MOVEM.L (A3),D1/D2
				TST.L   D2
				BEQ.S   mfev_nochange
				ADD.L   D2,(A3)
				CMP.L   EVHIT(A3),D1
				BNE.S   mfev_nochange
				TST.L   EVHIT(A3)
				BEQ.S   mfirstenvend
				MOVEM.L EVDECAY(A3),D2/D3
				MOVEM.L D2/D3,EVCLIMB(A3)
				CLR.L   EVDECAY(A3)
mfev_nochange:
				SWAP    D1
				MULU    D1,D5
				MULU    D1,D6
				LSR.L   #8,D5
				LSR.L   #8,D6
				MOVEM.L (A2),D1-D4  ;D1-curr D2-skip D3-addm D4-mod D5/D6-vol A3-end
				MOVE.L  CH_SMPENDDATA(A2),A3
				TST.W   CH_STEREO(A2)
				BNE     firstloop
				BRA     firstloopm
mnextloop:
				BNE.S   mnochange1    ; not end of sample
mfirstenvend:
				MOVE.L  A4,D0
				MOVE.L  DM_CHANNELMASK(A1),D1
				BCLR    D0,D1
				MOVE.L  D1,DM_CHANNELMASK(A1)
mnochange1:
				MOVE.L  DM_CHANNELMASK(A1),D7
				ADDQ.L  #1,A4
				MOVE.L  A4,D0
				LSR.L   D0,D7
				BNE.S   mnochange2
mnextlastenvzero:
				MOVE.L  DM_BUFFERWORK(A1),A5
				MOVE.L  DM_BUFFERCOPY(A1),A4
				LEA     BUFFLEN*8(A5),A6
mrestloop:
				MOVE.L  (A5)+,D0         ; the xdata 24 bit RAW LEFT
				ASR.L  #8,D0            ; convert to 16 bit
				MOVE.L D0,D7
				EXT.L  D7
				CMP.L  D7,D0
				BEQ.S  mnothigh
				ROL.L  #8,D0
				EXT.W  D0
				EORI.W  #$7FFF,D0        ;FFFF to 8000,  0000 to 7FFF
mnothigh:
				MOVE.W D0,(A4)+
				CMPA.L A6,A5            ; A6 end of resample area
				BCS.S  mrestloop
				BRA    ipendofint
mnextdo:
				BNE.S   mnochangedo
mnextdo2:
				MOVE.L  A4,D0
				MOVE.L  DM_CHANNELMASK(A1),D1
				BCLR    D0,D1
				MOVE.L  D1,DM_CHANNELMASK(A1)
mnochangedo:
				MOVE.L  DM_CHANNELMASK(A1),D7
				MOVE.L  A4,D0
				ADDQ.L  #1,D0
				LSR.L   D0,D7
mnochloop2:
				ADDQ.L  #1,A4
mnochange2:
				LEA     CH_SIZEOF(A2),A2
				LSR.L   #1,D7
				BCC.S   mnochloop2
				MOVE.L  DM_BUFFERWORK(A1),A5
				LEA     BUFFLEN*8(A5),A6
				LEA     CH_ENVEL(A2),A3
				MOVEM.L CH_VOLUMEL(A2),D5/D6
				MOVEM.L (A3),D1/D2
				TST.L   D2
				BEQ.S   mnev_nochange
				ADD.L   D2,(A3)
				CMP.L   EVHIT(A3),D1
				BNE.S   mnev_nochange
				TST.L   EVHIT(A3)
				BEQ.S   mnextenvend
				MOVEM.L EVDECAY(A3),D2/D3
				MOVEM.L D2/D3,EVCLIMB(A3)
				CLR.L   EVDECAY(A3)
mnev_nochange:
				SWAP    D1
				MULU    D1,D5
				MULU    D1,D6
				LSR.L   #8,D5
				LSR.L   #8,D6
				MOVEM.L (A2),D1-D4  ;D1-curr D2-skip D3-addm D4-mod D5/D6-vol A3-end
				MOVE.L  CH_SMPENDDATA(A2),A3
				TST.L   D7
				BEQ.S   mcloseend
				TST.W   CH_STEREO(A2)
				BNE     secloop
				BRA     secloopm
mcloseend:
				MOVE.L  A4,-(A7)
				MOVE.L  DM_BUFFERCOPY(A1),A4
				TST.W   CH_STEREO(A2)
				BNE     lastloop
				BRA     lastloopm
mnextenvend:
				TST.L   D7
				BNE     mnextdo2
				MOVE.L  A4,D0
				MOVE.L  DM_CHANNELMASK(A1),D1
				BCLR    D0,D1
				MOVE.L  D1,DM_CHANNELMASK(A1)
				BRA     mnextlastenvzero
mthisend:
				MOVE.L  (A7)+,A4
				BNE.S   ipendofint
				MOVE.L  A4,D0
				MOVE.L  DM_CHANNELMASK(A1),D1
				BCLR    D0,D1
				MOVE.L  D1,DM_CHANNELMASK(A1)

ipendofint:
				MOVE.L  DM_BUFFERSWAP(A1),A0
				MOVE.L  DM_BUFFERCOPY(A1),DM_BUFFERSWAP(A1)
				MOVE.L  A0,DM_BUFFERCOPY(A1)
				LEA     BUFFLEN*4-16(A0),A0
				MOVEM.L  DM_CALIBRATION(A1),A2-A6
				MOVEQ   #$40,D5
				MOVEQ   #1,D0
				MOVEQ   #0,D4
ipcalibrloop2:
				MOVE.B  D0,D4
				LSL.L   #8,D1
				MOVE.L  (A0)+,D0
				MOVE.B  D0,D1
				LSR.B   #2,D1
				ASR.W   #8,D0
				LSL.L   #8,D2
				SUB.B   D5,D1
				ADD.B   0(A2,D0.W),D1
				MOVE.B  D0,D2
				SWAP    D0
				LSL.L   #8,D3
				MOVE.B  D0,D3
				LSR.B   #2,D3
				ASR.W   #8,D0
				SUB.B   D5,D3
				ADD.B   0(A2,D0.W),D3
				LSL.L   #8,D4
				BCC.S   ipcalibrloop2
				MOVE.L  D1,-(A5)  ;LO RIGHT
				MOVE.L  D2,-(A6)  ;HI RIGHT
				MOVE.B  D0,D4
				MOVE.L  D4,-(A4)  ;HI LEFT
				MOVE.L  D3,-(A3)  ;LO LEFT
				MOVEM.L (A7)+,D2-D7/A2-A4/A6
				MOVEQ   #0,D0
				MOVE.L  D0,DM_BUSY(A1)
				RTS


firstloop:
        movem.w 0(a3,d1.l*4),d0/d7   ; was INT $4CB3,$0081,$1C00 ;  MOVEM.W 0(A3,D1.L*4),D0/D7   ; D1 < 0 and increases *2 / *4 stereo
				MULS    D5,D0             ; D5 = leftvolume
				MULS    D6,D7             ; D6 = rightvolume
				MOVEM.L D0/D7,(A5)      ; the xdata 24 bit RAW LEFT
				ADDQ.L  #8,A5           ; the xdata 24 bit RAW RIGHT
				ADD.L  D3,D4            ; D4=modulo, D3=addmodulo
				ADDX.L D2,D1            ; D2=skip
				BPL.S  itsendofsample1
notendofsample1:
				CMPA.L  A6,A5            ; A6 end of resample area
				BCS.S  firstloop
				MOVE.L D4,CH_MODULO(A2)
				MOVE.L D1,CH_CURRENT(A2)
				BRA    mnextloop
itsendofsample1:
				SUB.L  CH_LOOPLENGTH(A2),D1
				TST.W  CH_LOOP(A2)
				BNE.S  notendofsample1
				MOVEQ  #0,D1
				CMPA.L  A6,A5            ; A6 end of resample area
				BCC.S  endofsample1
clearloop1:
				MOVE.L D1,(A5)+
				MOVE.L D1,(A5)+
				CMPA.L  A6,A5            ; A6 end of resample area
				BCS.S  clearloop1
endofsample1:
				MOVEQ  #0,D0             ;signal fZ end of sample
				BRA    mnextloop

secloop:
        movem.w 0(a3,d1.l*4),d0/d7   ; was INT $4CB3,$0081,$1C00 ;  MOVEM.W 0(A3,D1.L*4),D0/D7   ; D1 < 0 and increases *2 / *4 stereo
				MULS   D5,D0             ; D5 = leftvolume
				ADD.L  D0,(A5)+         ; the xdata 24 bit RAW LEFT
				MULS   D6,D7             ; D6 = rightvolume
				ADD.L  D7,(A5)+         ; the xdata 24 bit RAW RIGHT
				ADD.L  D3,D4            ; D4=modulo, D3=addmodulo
				ADDX.L D2,D1            ; D2=skip
				BPL.S  itsendofsample2
notendofsample2:
				CMPA.L  A6,A5            ; A6 end of resample area
				BCS.S  secloop
				MOVE.L D4,CH_MODULO(A2)
				MOVE.L D1,CH_CURRENT(A2)
				BRA    mnextdo
itsendofsample2:
				SUB.L  CH_LOOPLENGTH(A2),D1
				TST.W  CH_LOOP(A2)
				BNE.S  notendofsample2
				MOVEQ  #0,D0
				BRA    mnextdo

lastloop:
        move.w  0(a3,d1.l*4),d0   ; was LONG $30331C00 ; MOVE.W 0(A3,D1.L*4),D0   ; D1 < 0 and increases *2 / *4 stereo
				MULS   D5,D0             ; D5 = leftvolume
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
        move.w  2(a3,d1.l*4),d0   ; was LONG $30331C02 ; MOVE.W 2(A3,D1.L*4),D0   ; D1 < 0 and increases *2 / *4 stereo
				MULS   D6,D0             ; D6 = rightvolume
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
				ADD.L  D3,D4            ; D4=modulo, D3=addmodulo
				ADDX.L D2,D1            ; D2=skip
				BPL.S  itsendofsample3
notendofsample3:
				CMPA.L  A6,A5            ; A6 end of resample area
				BCS.S  lastloop
				MOVE.L D4,CH_MODULO(A2)
				MOVE.L D1,CH_CURRENT(A2)
				BRA    mthisend
itsendofsample3:
				SUB.L  CH_LOOPLENGTH(A2),D1
				TST.W  CH_LOOP(A2)
				BNE.S  notendofsample3
				CMPA.L  A6,A5            ; A6 end of resample area
				BCC.S  endofsample3
restuniloop:
				MOVE.L  (A5)+,D0         ; the xdata 24 bit RAW LEFT
				ASR.L  #8,D0            ; convert to 16 bit
				MOVE.L D0,D7
				EXT.L  D7
				CMP.L  D7,D0
				BEQ.S  nothigh3
				ROL.L  #8,D0
				EXT.W  D0
				EORI.W  #$7FFF,D0        ;FFFF to 8000,  0000 to 7FFF
nothigh3:
				MOVE.W D0,(A4)+
				CMPA.L  A6,A5            ; A6 end of resample area
				BCS.S  restuniloop
endofsample3:
				MOVEQ  #0,D0
				BRA    mthisend


firstloopm:
        move.w  0(a3,d1.l*2),d0   ; was LONG $30331A00 ; MOVE.W 0(A3,D1.L*2),D0   ; D1 < 0 and increases *2 / *4 stereo
				MOVE.W  D0,D7
				MULS    D5,D0             ; D5 = leftvolume
				MULS    D6,D7             ; D6 = rightvolume
				MOVEM.L D0/D7,(A5)      ; the xdata 24 bit RAW LEFT
				ADDQ.L  #8,A5           ; the xdata 24 bit RAW RIGHT
				ADD.L  D3,D4            ; D4=modulo, D3=addmodulo
				ADDX.L D2,D1            ; D2=skip
				BPL.S  itsendofsample1m
notendofsample1m:
				CMPA.L  A6,A5            ; A6 end of resample area
				BCS.S  firstloopm
				MOVE.L D4,CH_MODULO(A2)
				MOVE.L D1,CH_CURRENT(A2)
				BRA    mnextloop
itsendofsample1m:
				SUB.L  CH_LOOPLENGTH(A2),D1
				TST.W  CH_LOOP(A2)
				BNE.S  notendofsample1m
				MOVEQ  #0,D1
				CMPA.L  A6,A5            ; A6 end of resample area
				BCC.S  endofsample1m
clearloop1m:
				MOVE.L D1,(A5)+
				MOVE.L D1,(A5)+
				CMPA.L  A6,A5            ; A6 end of resample area
				BCS.S  clearloop1m
endofsample1m:
				MOVEQ  #0,D0             ;signal fZ end of sample
				BRA    mnextloop

secloopm:
        move.w  0(a3,d1.l*2),d0   ; was LONG $30331A00 ; MOVE.W 0(A3,D1.L*2),D0   ; D1 < 0 and increases *2 / *4 stereo
				MOVE.W  D0,D7
				MULS    D5,D0             ; D5 = leftvolume
				ADD.L   D0,(A5)+         ; the xdata 24 bit RAW LEFT
				MULS    D6,D7             ; D6 = rightvolume
				ADD.L   D7,(A5)+         ; the xdata 24 bit RAW RIGHT
				ADD.L   D3,D4            ; D4=modulo, D3=addmodulo
				ADDX.L  D2,D1            ; D2=skip
				BPL.S  itsendofsample2m
notendofsample2m:
				CMPA.L  A6,A5            ; A6 end of resample area
				BCS.S  secloopm
				MOVE.L D4,CH_MODULO(A2)
				MOVE.L D1,CH_CURRENT(A2)
				BRA    mnextdo
itsendofsample2m:
				SUB.L  CH_LOOPLENGTH(A2),D1
				TST.W  CH_LOOP(A2)
				BNE.S  notendofsample2m
				MOVEQ  #0,D0
				BRA    mnextdo

lastloopm:
        move.w  0(a3,d1.l*2),d0   ; was LONG $30331A00 ; MOVE.W 0(A3,D1.L*2),D0   ; D1 < 0 and increases *2 / *4 stereo
				MULS   D5,D0             ; D5 = leftvolume
				ADD.L  (A5)+,D0         ; the xdata 24 bit RAW LEFT
				ASR.L  #8,D0            ; convert to 16 bit
				MOVE.L D0,D7
				EXT.L  D7
				CMP.L  D7,D0
				BEQ.S  nothigh1m
				ROL.L  #8,D0
				EXT.W  D0
				EORI.W  #$7FFF,D0        ;FFFF to 8000,  0000 to 7FFF
nothigh1m:
				MOVE.W D0,(A4)+
        move.w  0(a3,d1.l*2),d0   ; was LONG $30331A00 ; MOVE.W 0(A3,D1.L*2),D0   ; D1 < 0 and increases *2 / *4 stereo
				MULS   D6,D0             ; D6 = rightvolume
				ADD.L  (A5)+,D0         ; the xdata 24 bit RAW LEFT
				ASR.L  #8,D0            ; convert to 16 bit
				MOVE.L D0,D7
				EXT.L  D7
				CMP.L  D7,D0
				BEQ.S  nothigh2m
				ROL.L  #8,D0
				EXT.W  D0
				EORI.W  #$7FFF,D0        ;FFFF to 8000,  0000 to 7FFF
nothigh2m:
				MOVE.W D0,(A4)+
				ADD.L  D3,D4            ; D4=modulo, D3=addmodulo
				ADDX.L D2,D1            ; D2=skip
				BPL.S  itsendofsample3m
notendofsample3m:
				CMPA.L  A6,A5            ; A6 end of resample area
				BCS.S  lastloopm
				MOVE.L D4,CH_MODULO(A2)
				MOVE.L D1,CH_CURRENT(A2)
				BRA    mthisend
itsendofsample3m:
				SUB.L  CH_LOOPLENGTH(A2),D1
				TST.W  CH_LOOP(A2)
				BNE.S  notendofsample3m
				CMPA.L  A6,A5            ; A6 end of resample area
				BCC.S  endofsample3m
restuniloopm:
				MOVE.L  (A5)+,D0         ; the xdata 24 bit RAW LEFT
				ASR.L  #8,D0            ; convert to 16 bit
				MOVE.L D0,D7
				EXT.L  D7
				CMP.L  D7,D0
				BEQ.S  nothigh3m
				ROL.L  #8,D0
				EXT.W  D0
				EORI.W  #$7FFF,D0        ;FFFF to 8000,  0000 to 7FFF
nothigh3m:
				MOVE.W D0,(A4)+
				CMPA.L  A6,A5            ; A6 end of resample area
				BCS.S  restuniloopm
endofsample3m:
				MOVEQ  #0,D0
				BRA    mthisend
