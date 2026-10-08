; hookentry.s - entry for utility.library hooks written in C (68000)
;
; A hook is called with A0 = hook, A2 = object, A1 = message. This calls
; hook->h_SubEntry(hook, object, message) with the arguments on the stack,
; as a C function expects them. Set h_Entry to snd_hook_entry and
; h_SubEntry to the C function.
;
; Assemble: vasmm68k_mot -Fhunk -m68000 -quiet -o hookentry.o hookentry.s

        section code,code

        xdef    _snd_hook_entry

H_SUBENTRY = 12

_snd_hook_entry:
        move.l  a1,-(sp)
        move.l  a2,-(sp)
        move.l  a0,-(sp)
        move.l  H_SUBENTRY(a0),a0
        jsr     (a0)
        lea     12(sp),sp
        rts
