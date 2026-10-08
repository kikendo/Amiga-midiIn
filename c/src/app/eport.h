/*
 * eport.h - small runtime pieces the AmigaE sources relied on
 *
 * Exceptions: AmigaE's Raise()/HANDLE/EXCEPT are kept, built on setjmp, so
 * the error flow of the original code (errors raised deep down, reported
 * by report_exception() at the top) stays the same.
 *
 *   E_TRY {
 *       ... code that may call Raise() ...      (no return/break out!)
 *   } E_EXCEPT_DO {
 *       ... runs always; exception is 0 if nothing was raised ...
 *   } E_END;
 *
 *   E_TRY { ... } E_EXCEPT { ... only after a Raise ... } E_END;
 *
 * Locals changed inside the E_TRY block and read after a Raise must be
 * volatile. Exceptions are per program, not per task: code running in the
 * play task must not use them.
 *
 * Exception codes are four-character ids as in E: 'READ', 'MEM', ...
 */
#ifndef EPORT_H
#define EPORT_H

#include <setjmp.h>
#include <exec/types.h>
#include <exec/lists.h>
#include <exec/nodes.h>

struct e_frame {
	struct e_frame *prev;
	jmp_buf jb;
};

extern struct e_frame *e_top;
extern LONG exception;
extern APTR exceptioninfo;

void Raise(LONG code) __attribute__((noreturn));
void Throw(LONG code, APTR info) __attribute__((noreturn));
void ReThrow(void);                     /* Raise(exception) if exception */

#define E_TRY \
	do { \
		struct e_frame e_fr_; \
		e_fr_.prev = e_top; \
		e_top = &e_fr_; \
		if (setjmp(e_fr_.jb) == 0) {
#define E_EXCEPT_DO \
			e_top = e_fr_.prev; \
			exception = 0; \
		} else { \
			e_top = e_fr_.prev; \
		} \
		{
#define E_EXCEPT \
			e_top = e_fr_.prev; \
			exception = 0; \
		} else { \
			e_top = e_fr_.prev; \
		} \
		if (exception) {
#define E_END \
		} \
	} while (0)

/* memory: like E's New/NewM, raise 'MEM' on failure; cleared */
APTR e_new(ULONG size);
APTR e_newm(ULONG size, ULONG flags);   /* AllocVec(size, flags|CLEAR) */
void e_dispose(APTR p);                 /* FreeVec, NULL ok */

/*
 * AmigaE StringF formatting into buf (size bytes, always terminated).
 * Codes: \d decimal, \h hex, \s string, \c char, \z zero fill for the next
 * field, \l left align, \r right align, [n] field width after the code.
 * Arguments are LONGs (strings as STRPTR cast to LONG is fine).
 */
STRPTR estringf(STRPTR buf, ULONG size, CONST_STRPTR fmt, ...);

/* string copy with size limit, always terminated (E's AstrCopy) */
void estrcpy(STRPTR dst, CONST_STRPTR src, ULONG size);
void estrcat(STRPTR dst, CONST_STRPTR src, ULONG size);

/* --- MidiBPlists.e ---------------------------------------------------- */

/* a list node followed by a pointer, as E's lln */
struct lln {
	struct Node ln;
	APTR pointer;
};

struct List *newlist(struct List *lh);  /* initialise (lh != NULL) */
void addsorted(struct List *lh, struct Node *ln);
LONG convlnptrtonum(APTR ptr, struct List *lh); /* 1-based, 0 = not found */

/*
 * Startup trace for finding crashes: built with make DEBUG=1, E_TRACE()
 * writes a line to the Shell window and Throw() reports each exception.
 */
#ifdef MI_DEBUG
void e_trace(CONST_STRPTR s);
#define E_TRACE(s) e_trace((CONST_STRPTR)(s))
#else
#define E_TRACE(s) ((void)0)
#endif

#define E_MIN(a, b) ((a) < (b) ? (a) : (b))
#define E_MAX(a, b) ((a) > (b) ? (a) : (b))

#endif /* EPORT_H */
