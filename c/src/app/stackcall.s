; stackcall.s - run a function on a stack of its own (68000)
;
; LONG stack_call(struct StackSwapStruct *sss, LONG (*func)(void))
;
; Swaps to the stack described by sss with exec's StackSwap(), calls
; func, swaps back and returns func's result.
;
; Assemble: vasmm68k_mot -Fhunk -m68000 -quiet -o stackcall.o stackcall.s

        section code,code

        xdef    _stack_call

LVOStackSwap = -732

_stack_call:
        movem.l d2/a2/a3/a6,-(sp)
        move.l  20(sp),a2               ; sss
        move.l  24(sp),a3               ; func
        move.l  4.w,a6
        move.l  a2,a0
        jsr     LVOStackSwap(a6)
        jsr     (a3)
        move.l  d0,d2
        move.l  4.w,a6
        move.l  a2,a0
        jsr     LVOStackSwap(a6)
        move.l  d2,d0
        movem.l (sp)+,d2/a2/a3/a6
        rts
