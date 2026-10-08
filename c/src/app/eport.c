/*
 * eport.c - see eport.h
 */
#include <stdarg.h>
#include <stdlib.h>
#include <exec/types.h>
#include <exec/memory.h>
#include <proto/exec.h>
#include <proto/utility.h>

#include "eport.h"

struct e_frame *e_top;
LONG exception;
APTR exceptioninfo;

void Throw(LONG code, APTR info)
{
	struct e_frame *f = e_top;

	exception = code;
	exceptioninfo = info;
	if (!f)
		exit(20);                       /* nothing to catch it */
	longjmp(f->jb, 1);
}

void Raise(LONG code)
{
	Throw(code, exceptioninfo);
}

void ReThrow(void)
{
	if (exception)
		Throw(exception, exceptioninfo);
}

/* ------------------------------------------------------------------ memory */

APTR e_newm(ULONG size, ULONG flags)
{
	APTR p = AllocVec(size ? size : 1, flags | MEMF_CLEAR);

	if (!p)
		Raise('MEM');
	return p;
}

APTR e_new(ULONG size)
{
	return e_newm(size, MEMF_PUBLIC);
}

void e_dispose(APTR p)
{
	if (p)
		FreeVec(p);
}

/* ----------------------------------------------------------------- strings */

void estrcpy(STRPTR dst, CONST_STRPTR src, ULONG size)
{
	ULONG i = 0;

	if (!size)
		return;
	if (src)
		for (; i + 1 < size && src[i]; i++)
			dst[i] = src[i];
	dst[i] = 0;
}

void estrcat(STRPTR dst, CONST_STRPTR src, ULONG size)
{
	ULONG l = 0;

	while (l < size && dst[l])
		l++;
	if (l < size)
		estrcpy(dst + l, src, size - l);
}

static void put(STRPTR buf, ULONG size, ULONG *pos, char c)
{
	if (*pos + 1 < size)
		buf[(*pos)++] = c;
}

STRPTR estringf(STRPTR buf, ULONG size, CONST_STRPTR fmt, ...)
{
	va_list ap;
	ULONG pos = 0;
	char tmp[16];
	BOOL zero = FALSE, left = FALSE;

	if (!size)
		return buf;
	va_start(ap, fmt);
	while (*fmt) {
		char c = *fmt++;
		const char *s = 0;
		int n = 0, width = 0, i;

		if (c != '\\' || !*fmt) {
			put(buf, size, &pos, c);
			continue;
		}
		c = *fmt++;
		switch (c) {
		case 'z': zero = TRUE; continue;
		case 'l': left = TRUE; continue;
		case 'r': left = FALSE; continue;
		case 'd': {
			LONG v = va_arg(ap, LONG);
			ULONG u = v < 0 ? (ULONG)-v : (ULONG)v;

			do {
				tmp[n++] = (char)('0' + u % 10);
				u /= 10;
			} while (u);
			if (v < 0)
				tmp[n++] = '-';
			break;
		}
		case 'h': {
			ULONG u = va_arg(ap, ULONG);

			do {
				tmp[n++] = "0123456789ABCDEF"[u & 15];
				u >>= 4;
			} while (u);
			break;
		}
		case 'c':
			tmp[n++] = (char)va_arg(ap, LONG);
			break;
		case 's':
			s = (const char *)va_arg(ap, LONG);
			if (!s)
				s = "";
			break;
		default:
			put(buf, size, &pos, '\\');
			put(buf, size, &pos, c);
			continue;
		}
		if (*fmt == '[') {
			fmt++;
			while (*fmt >= '0' && *fmt <= '9')
				width = width * 10 + (*fmt++ - '0');
			if (*fmt == ']')
				fmt++;
		}
		if (s) {
			int l = 0;

			while (s[l] && (!width || l < width))
				l++;
			if (!left)
				for (i = l; i < width; i++)
					put(buf, size, &pos, ' ');
			for (i = 0; i < l; i++)
				put(buf, size, &pos, s[i]);
			if (left)
				for (i = l; i < width; i++)
					put(buf, size, &pos, ' ');
		} else {
			int len = n;

			if (!left)
				for (i = len; i < width; i++)
					put(buf, size, &pos, zero ? '0' : ' ');
			while (n)
				put(buf, size, &pos, tmp[--n]);
			if (left)
				for (i = len; i < width; i++)
					put(buf, size, &pos, ' ');
		}
		zero = FALSE;
		left = FALSE;
	}
	va_end(ap);
	buf[pos] = 0;
	return buf;
}

/* ------------------------------------------------------------------- lists */

struct List *newlist(struct List *lh)
{
	lh->lh_Head = (struct Node *)&lh->lh_Tail;
	lh->lh_Tail = 0;
	lh->lh_TailPred = (struct Node *)&lh->lh_Head;
	return lh;
}

void addsorted(struct List *lh, struct Node *ln)
{
	struct Node *g;

	for (g = lh->lh_Head; g->ln_Succ; g = g->ln_Succ) {
		if (Stricmp((CONST_STRPTR)g->ln_Name, (CONST_STRPTR)ln->ln_Name) >= 0) {
			Insert(lh, ln, g->ln_Pred);
			return;
		}
	}
	AddTail(lh, ln);
}

LONG convlnptrtonum(APTR ptr, struct List *lh)
{
	struct lln *ln;
	LONG i = 1;

	for (ln = (struct lln *)lh->lh_Head; ln->ln.ln_Succ;
	     ln = (struct lln *)ln->ln.ln_Succ, i++)
		if (ln->pointer == ptr)
			return i;
	return 0;
}
