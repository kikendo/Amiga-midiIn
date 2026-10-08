/*
 * egui.c - see egui.h
 *
 * Layout: every object reports a minimum size and whether it can grow
 * horizontally/vertically. A row/column group stacks its children with a
 * small gap, gives extra space along its axis to the children that can
 * grow, and stretches children across the other axis when they can grow
 * that way (otherwise they are centred). EQ groups give all children the
 * same size along the axis.
 */
#include <stdarg.h>
#include <string.h>
#include <exec/types.h>
#include <exec/memory.h>
#include <intuition/intuition.h>
#include <intuition/gadgetclass.h>
#include <libraries/gadtools.h>
#include <graphics/gfxmacros.h>
#include <workbench/workbench.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/gadtools.h>
#include <proto/wb.h>
#include <proto/utility.h>

#include "egui.h"

enum {
	EGO_ROWS, EGO_COLS, EGO_EQROWS, EGO_EQCOLS, EGO_BEVEL, EGO_BEVELR,
	EGO_BAR, EGO_SPACE, EGO_SPACEH, EGO_SPACEV,
	EGO_TEXT, EGO_NUM, EGO_BUTTON, EGO_SBUTTON, EGO_CHECK, EGO_SLIDE,
	EGO_MX, EGO_CYCLE, EGO_LISTV, EGO_PLUGIN
};

#define F_DISABLED 1
#define F_LEFT     2
#define F_BORDER   4
#define F_VERT     8
#define F_READONLY 16

#define GAPX   4
#define GAPY   2
#define BEVM   4            /* bevel margin */
#define WINM   4            /* window inner margin */

struct EG_Obj {
	UBYTE type;
	UBYTE flags;
	UBYTE expx, expy;
	EG_Obj *next;           /* sibling */
	EG_Obj *child;          /* first child */
	eg_action action;
	APTR data;
	CONST_STRPTR label;
	LONG key;
	LONG val;
	LONG min, max;
	WORD chars, lines;
	char text[96];          /* TEXT contents */
	char levelfmt[12];      /* slider level, C format */
	WORD levellen;
	CONST_STRPTR *labels;
	struct List *list;
	eg_appproc appproc;
	struct EG_Plugin *plugin;
	/* layout */
	WORD minw, minh;
	WORD x, y, w, h;
	WORD labw;
	struct Gadget *gad;
};

struct EG_GuiPriv {
	struct EG_GuiPriv *nextgui;
	EG_Gui *gh;
	EG_Multi *mh;
	EG_Obj *root;
	struct EG_WinOpts opts;
	struct Screen *scr;
	BOOL scrlocked;
	APTR vi;
	struct TextAttr ta;
	struct TextFont *tf;
	struct Gadget *glist;
	struct Menu *menu;
	struct AppWindow *appwin;
	WORD left, top, width, height;  /* box to (re)open with */
	WORD minw, minh;                /* window minimum (outer) */
	WORD blocked;
	struct Requester req;
	BOOL removed;
};

struct EG_Multi {
	struct MsgPort *port;
	struct MsgPort *appport;
	struct EG_GuiPriv *guis;
};

struct Library *WorkbenchBase;
static LONG quitvalue = -1;
static BOOL quitflag;

/* ================================================================ objects */

static EG_Obj *newobj(UBYTE type)
{
	EG_Obj *o = (EG_Obj *)AllocVec(sizeof(EG_Obj), MEMF_PUBLIC | MEMF_CLEAR);

	if (o)
		o->type = type;
	return o;
}

static EG_Obj *group(UBYTE type, EG_Obj *first, va_list ap)
{
	EG_Obj *g = newobj(type), *c, **tail;

	if (!g)
		return 0;
	tail = &g->child;
	for (c = first; c; c = va_arg(ap, EG_Obj *)) {
		*tail = c;
		tail = &c->next;
	}
	return g;
}

#define GROUPFN(name, type) \
	EG_Obj *name(EG_Obj *first, ...) \
	{ \
		va_list ap; \
		EG_Obj *g; \
		va_start(ap, first); \
		g = group(type, first, ap); \
		va_end(ap); \
		return g; \
	}

GROUPFN(eg_rows, EGO_ROWS)
GROUPFN(eg_cols, EGO_COLS)
GROUPFN(eg_eqrows, EGO_EQROWS)
GROUPFN(eg_eqcols, EGO_EQCOLS)

EG_Obj *eg_bevel(EG_Obj *child)
{
	EG_Obj *o = newobj(EGO_BEVEL);

	if (o)
		o->child = child;
	return o;
}

EG_Obj *eg_bevelr(EG_Obj *child)
{
	EG_Obj *o = newobj(EGO_BEVELR);

	if (o)
		o->child = child;
	return o;
}

EG_Obj *eg_bar(void) { return newobj(EGO_BAR); }
EG_Obj *eg_space(void) { return newobj(EGO_SPACE); }
EG_Obj *eg_spaceh(void) { return newobj(EGO_SPACEH); }
EG_Obj *eg_spacev(void) { return newobj(EGO_SPACEV); }

static void settextbuf(EG_Obj *o, CONST_STRPTR s)
{
	ULONG i = 0;

	if (s)
		for (; i + 1 < sizeof(o->text) && s[i]; i++)
			o->text[i] = s[i];
	o->text[i] = 0;
}

EG_Obj *eg_text(CONST_STRPTR cur, CONST_STRPTR label, BOOL border, WORD minchars)
{
	EG_Obj *o = newobj(EGO_TEXT);

	if (o) {
		settextbuf(o, cur);
		o->label = label;
		o->flags = border ? F_BORDER : 0;
		o->chars = minchars;
	}
	return o;
}

EG_Obj *eg_num(LONG val, CONST_STRPTR label, BOOL border, WORD minchars)
{
	EG_Obj *o = newobj(EGO_NUM);

	if (o) {
		o->val = val;
		o->label = label;
		o->flags = border ? F_BORDER : 0;
		o->chars = minchars;
	}
	return o;
}

EG_Obj *eg_button(eg_action a, CONST_STRPTR label, LONG key)
{
	EG_Obj *o = newobj(EGO_BUTTON);

	if (o) {
		o->action = a;
		o->label = label;
		o->key = key;
	}
	return o;
}

EG_Obj *eg_sbutton(eg_action a, CONST_STRPTR label, LONG key)
{
	EG_Obj *o = eg_button(a, label, key);

	if (o)
		o->type = EGO_SBUTTON;
	return o;
}

EG_Obj *eg_check(eg_action a, CONST_STRPTR label, BOOL checked, BOOL left,
                 LONG key, BOOL disabled)
{
	EG_Obj *o = newobj(EGO_CHECK);

	if (o) {
		o->action = a;
		o->label = label;
		o->val = checked ? 1 : 0;
		o->flags = (left ? F_LEFT : 0) | (disabled ? F_DISABLED : 0);
		o->key = key;
	}
	return o;
}

/* "\d[3]" -> "%3ld" */
static void levelformat(EG_Obj *o, CONST_STRPTR f)
{
	WORD w = 0;
	CONST_STRPTR p;

	o->levelfmt[0] = 0;
	o->levellen = 0;
	if (!f || !*f)
		return;
	for (p = f; *p; p++)
		if (*p == '[') {
			for (p++; *p >= '0' && *p <= '9'; p++)
				w = (WORD)(w * 10 + (*p - '0'));
			break;
		}
	if (w <= 0 || w > 9)
		w = 4;
	o->levelfmt[0] = '%';
	o->levelfmt[1] = (char)('0' + w);
	o->levelfmt[2] = 'l';
	o->levelfmt[3] = 'd';
	o->levelfmt[4] = 0;
	o->levellen = w;
}

EG_Obj *eg_slide(eg_action a, CONST_STRPTR label, BOOL vertical, LONG min,
                 LONG max, LONG cur, WORD chars, CONST_STRPTR levelfmt,
                 LONG key, BOOL disabled)
{
	EG_Obj *o = newobj(EGO_SLIDE);

	if (o) {
		o->action = a;
		o->label = label;
		o->flags = (vertical ? F_VERT : 0) | (disabled ? F_DISABLED : 0);
		o->min = min;
		o->max = max;
		o->val = cur;
		o->chars = chars;
		o->key = key;
		levelformat(o, levelfmt);
	}
	return o;
}

EG_Obj *eg_mx(eg_action a, CONST_STRPTR label, CONST_STRPTR *labels, BOOL left,
              LONG cur, LONG key)
{
	EG_Obj *o = newobj(EGO_MX);

	if (o) {
		o->action = a;
		o->label = label;
		o->labels = labels;
		o->flags = left ? F_LEFT : 0;
		o->val = cur;
		o->key = key;
	}
	return o;
}

EG_Obj *eg_cycle(eg_action a, CONST_STRPTR label, CONST_STRPTR *labels,
                 LONG cur, LONG key)
{
	EG_Obj *o = newobj(EGO_CYCLE);

	if (o) {
		o->action = a;
		o->label = label;
		o->labels = labels;
		o->val = cur;
		o->key = key;
	}
	return o;
}

EG_Obj *eg_listv(eg_action a, CONST_STRPTR label, WORD minchars, WORD minlines,
                 struct List *list, BOOL readonly, LONG selected,
                 eg_appproc appproc)
{
	EG_Obj *o = newobj(EGO_LISTV);

	if (o) {
		o->action = a;
		o->label = label;
		o->chars = minchars;
		o->lines = minlines;
		o->list = list;
		o->flags = readonly ? F_READONLY : 0;
		o->val = selected;
		o->appproc = appproc;
	}
	return o;
}

EG_Obj *eg_plugin(eg_action a, struct EG_Plugin *p)
{
	EG_Obj *o = newobj(EGO_PLUGIN);

	if (o) {
		o->action = a;
		o->plugin = p;
	}
	return o;
}

void eg_setdata(EG_Obj *o, APTR data)
{
	if (o)
		o->data = data;
}

APTR eg_getdata(EG_Obj *o)
{
	return o ? o->data : 0;
}

static BOOL isgroup(EG_Obj *o)
{
	return o->type <= EGO_EQCOLS;
}

/* frees a tree; plugins are disposed */
static void freeobjs(EG_Obj *o)
{
	while (o) {
		EG_Obj *n = o->next;

		if (o->child)
			freeobjs(o->child);
		if (o->type == EGO_PLUGIN && o->plugin && o->plugin->cls && o->plugin->cls->dispose)
			o->plugin->cls->dispose(o->plugin);
		FreeVec(o);
		o = n;
	}
}

/* walks all objects, calling fn on each */
static void walk(EG_Obj *o, void (*fn)(EG_Obj *, APTR), APTR ud)
{
	for (; o; o = o->next) {
		fn(o, ud);
		if (o->child)
			walk(o->child, fn, ud);
	}
}

/* ================================================================= layout */

struct measure {
	struct RastPort rp;
	WORD fh, cw;            /* font height, average char width */
};

static WORD textw(struct measure *m, CONST_STRPTR s)
{
	char buf[128];
	WORD n = 0;

	if (!s)
		return 0;
	for (; *s && n < (WORD)sizeof(buf) - 1; s++)
		if (*s != '_')
			buf[n++] = *s;
	return n ? (WORD)TextLength(&m->rp, (STRPTR)buf, n) : 0;
}

static BOOL haslabel(EG_Obj *o)
{
	return o->label && o->label[0];
}

static void measure(EG_Obj *o, struct measure *m, BOOL inrow)
{
	EG_Obj *c;
	WORD n, w, h, fh = m->fh;

	o->labw = haslabel(o) ? (WORD)(textw(m, o->label) + 8) : 0;
	o->expx = o->expy = 0;
	switch (o->type) {
	case EGO_ROWS:
	case EGO_COLS:
	case EGO_EQROWS:
	case EGO_EQCOLS: {
		BOOL rows = o->type == EGO_ROWS || o->type == EGO_EQROWS;
		BOOL eq = o->type == EGO_EQROWS || o->type == EGO_EQCOLS;
		WORD maxa = 0, sum = 0, maxo = 0;

		n = 0;
		for (c = o->child; c; c = c->next) {
			measure(c, m, rows);
			w = rows ? c->minh : c->minw;
			h = rows ? c->minw : c->minh;
			sum += w;
			if (w > maxa)
				maxa = w;
			if (h > maxo)
				maxo = h;
			o->expx |= c->expx;
			o->expy |= c->expy;
			n++;
		}
		if (eq)
			sum = (WORD)(maxa * n);
		if (n > 1)
			sum += (WORD)((n - 1) * (rows ? GAPY : GAPX));
		o->minw = rows ? maxo : sum;
		o->minh = rows ? sum : maxo;
		break;
	}
	case EGO_BEVEL:
	case EGO_BEVELR:
		if (o->child) {
			measure(o->child, m, inrow);
			o->minw = (WORD)(o->child->minw + 2 * BEVM);
			o->minh = (WORD)(o->child->minh + 2 * BEVM);
			o->expx = o->child->expx;
			o->expy = o->child->expy;
		}
		break;
	case EGO_BAR:
		if (inrow) {
			o->minw = 0;
			o->minh = 2;
			o->expx = 1;
		} else {
			o->minw = 2;
			o->minh = 0;
			o->expy = 1;
		}
		break;
	case EGO_SPACE:
		o->minw = o->minh = 0;
		o->expx = o->expy = 1;
		break;
	case EGO_SPACEH:
		o->minw = o->minh = 0;
		o->expx = 1;
		break;
	case EGO_SPACEV:
		o->minw = o->minh = 0;
		o->expy = 1;
		break;
	case EGO_TEXT:
	case EGO_NUM:
		o->minw = (WORD)(o->labw + m->cw * (o->chars > 0 ? o->chars : 1) + 8);
		o->minh = (WORD)(fh + 4);
		o->expx = 1;
		break;
	case EGO_BUTTON:
	case EGO_SBUTTON:
		o->minw = (WORD)(textw(m, o->label) + 16);
		o->minh = (WORD)(fh + 6);
		o->expx = o->type == EGO_SBUTTON;
		break;
	case EGO_CHECK:
		h = (WORD)(fh + 3);
		o->minw = (WORD)(o->labw + h * 26 / 11);
		o->minh = h;
		break;
	case EGO_SLIDE:
		if (o->flags & F_VERT) {
			o->minw = 18;
			o->minh = (WORD)(fh * (o->chars > 0 ? o->chars : 4));
			if (o->labw)
				o->minh += (WORD)(fh + 2);
			o->expy = 1;
		} else {
			o->minw = (WORD)(o->labw + m->cw * (o->chars > 0 ? o->chars : 8));
			if (o->levellen)
				o->minw += (WORD)(m->cw * o->levellen + 8);
			o->minh = (WORD)(fh + 2);
			o->expx = 1;
		}
		break;
	case EGO_MX: {
		WORD maxl = 0;

		n = 0;
		if (o->labels)
			for (; o->labels[n]; n++)
				if ((w = textw(m, o->labels[n])) > maxl)
					maxl = w;
		h = (WORD)(fh + 1);
		o->minw = (WORD)(o->labw + 17 + 8 + maxl);
		o->minh = (WORD)(n * (h + 1));
		break;
	}
	case EGO_CYCLE: {
		WORD maxl = 0;

		if (o->labels)
			for (n = 0; o->labels[n]; n++)
				if ((w = textw(m, o->labels[n])) > maxl)
					maxl = w;
		o->minw = (WORD)(o->labw + maxl + 36);
		o->minh = (WORD)(fh + 6);
		o->expx = 1;
		break;
	}
	case EGO_LISTV:
		o->minw = (WORD)(m->cw * (o->chars > 0 ? o->chars : 10) + 24);
		o->minh = (WORD)((fh + 1) * (o->lines > 0 ? o->lines : 3) + 6);
		if (o->labw) {
			o->minh += (WORD)(fh + 2);
			if (o->labw > o->minw)
				o->minw = o->labw;
		}
		o->expx = o->expy = 1;
		break;
	case EGO_PLUGIN: {
		struct EG_Plugin *p = o->plugin;
		ULONG r = EG_RESIZEX | EG_RESIZEY;

		w = h = 0;
		if (p && p->cls) {
			if (p->cls->min_size)
				p->cls->min_size(p, 0, fh, &w, &h);
			if (p->cls->will_resize)
				r = p->cls->will_resize(p);
		}
		o->minw = w;
		o->minh = h;
		o->expx = (r & EG_RESIZEX) ? 1 : 0;
		o->expy = (r & EG_RESIZEY) ? 1 : 0;
		break;
	}
	}
}

static void place(EG_Obj *o, WORD x, WORD y, WORD w, WORD h)
{
	EG_Obj *c;

	o->x = x;
	o->y = y;
	o->w = w;
	o->h = h;
	switch (o->type) {
	case EGO_ROWS:
	case EGO_COLS:
	case EGO_EQROWS:
	case EGO_EQCOLS: {
		BOOL rows = o->type == EGO_ROWS || o->type == EGO_EQROWS;
		BOOL eq = o->type == EGO_EQROWS || o->type == EGO_EQCOLS;
		WORD n = 0, nexp = 0, avail, extra, pos, gap = rows ? GAPY : GAPX;

		for (c = o->child; c; c = c->next) {
			n++;
			if (rows ? c->expy : c->expx)
				nexp++;
		}
		if (!n)
			return;
		avail = (WORD)((rows ? h : w) - (n - 1) * gap);
		extra = (WORD)(avail - (rows ? o->minh : o->minw) + (n - 1) * gap);
		pos = rows ? y : x;
		for (c = o->child; c; c = c->next) {
			WORD size, cross, cw, ch, cx, cy;

			if (eq) {
				size = (WORD)(avail / n);
			} else {
				size = rows ? c->minh : c->minw;
				if (nexp && (rows ? c->expy : c->expx)) {
					WORD add = (WORD)(extra / nexp);

					size += add;
					extra -= add;
					nexp--;
				}
			}
			cross = rows ? w : h;
			if (rows) {
				cw = c->expx ? cross : c->minw;
				ch = size;
				cx = (WORD)(x + (cross - cw) / 2);
				cy = pos;
			} else {
				cw = size;
				ch = c->expy ? cross : c->minh;
				cx = pos;
				cy = (WORD)(y + (cross - ch) / 2);
			}
			if (!rows && c->type == EGO_BAR)
				ch = cross, cy = y;
			if (rows && c->type == EGO_BAR)
				cw = cross, cx = x;
			place(c, cx, cy, cw, ch);
			pos = (WORD)(pos + size + gap);
		}
		break;
	}
	case EGO_BEVEL:
	case EGO_BEVELR:
		if (o->child) {
			c = o->child;
			place(c, (WORD)(x + BEVM), (WORD)(y + BEVM),
			      (WORD)(c->expx ? w - 2 * BEVM : c->minw),
			      (WORD)(c->expy ? h - 2 * BEVM : c->minh));
			if (!c->expx)
				c->x = (WORD)(x + (w - c->minw) / 2), place(c, c->x, c->y, c->minw, c->h);
			if (!c->expy)
				place(c, c->x, (WORD)(y + (h - c->minh) / 2), c->w, c->minh);
		}
		break;
	case EGO_PLUGIN:
		if (o->plugin) {
			o->plugin->x = x;
			o->plugin->y = y;
			o->plugin->xs = w;
			o->plugin->ys = h;
		}
		break;
	}
}

/* ================================================================ gadgets */

static struct Gadget *mkgad(struct EG_GuiPriv *pv, struct Gadget *prev, EG_Obj *o)
{
	struct NewGadget ng;
	ULONG kind;
	struct TagItem tags[10];
	int t = 0;

	memset(&ng, 0, sizeof(ng));
	ng.ng_TextAttr = &pv->ta;
	ng.ng_VisualInfo = pv->vi;
	ng.ng_UserData = o;
	ng.ng_GadgetText = (UBYTE *)(haslabel(o) ? o->label : 0);
	ng.ng_Flags = PLACETEXT_LEFT;
	ng.ng_LeftEdge = (WORD)(o->x + o->labw);
	ng.ng_TopEdge = o->y;
	ng.ng_Width = (WORD)(o->w - o->labw);
	ng.ng_Height = o->h;

	tags[t].ti_Tag = GT_Underscore;
	tags[t++].ti_Data = '_';
	switch (o->type) {
	case EGO_TEXT:
		kind = TEXT_KIND;
		tags[t].ti_Tag = GTTX_Text;
		tags[t++].ti_Data = (ULONG)o->text;
		tags[t].ti_Tag = GTTX_CopyText;
		tags[t++].ti_Data = TRUE;
		tags[t].ti_Tag = GTTX_Border;
		tags[t++].ti_Data = (o->flags & F_BORDER) ? TRUE : FALSE;
		break;
	case EGO_NUM:
		kind = NUMBER_KIND;
		tags[t].ti_Tag = GTNM_Number;
		tags[t++].ti_Data = (ULONG)o->val;
		tags[t].ti_Tag = GTNM_Border;
		tags[t++].ti_Data = (o->flags & F_BORDER) ? TRUE : FALSE;
		break;
	case EGO_BUTTON:
	case EGO_SBUTTON:
		kind = BUTTON_KIND;
		ng.ng_Flags = PLACETEXT_IN;
		ng.ng_LeftEdge = o->x;
		ng.ng_Width = o->w;
		break;
	case EGO_CHECK:
		kind = CHECKBOX_KIND;
		ng.ng_Width = (WORD)(o->h * 26 / 11);
		ng.ng_LeftEdge = (o->flags & F_LEFT) ? (WORD)(o->x + o->labw) : o->x;
		ng.ng_Flags = (o->flags & F_LEFT) ? PLACETEXT_LEFT : PLACETEXT_RIGHT;
		tags[t].ti_Tag = GTCB_Checked;
		tags[t++].ti_Data = o->val ? TRUE : FALSE;
		tags[t].ti_Tag = GTCB_Scaled;
		tags[t++].ti_Data = TRUE;
		break;
	case EGO_SLIDE:
		kind = SLIDER_KIND;
		if (o->flags & F_VERT) {
			ng.ng_LeftEdge = o->x;
			ng.ng_Width = o->w;
			ng.ng_Flags = PLACETEXT_ABOVE;
			if (o->labw) {
				ng.ng_TopEdge = (WORD)(o->y + pv->tf->tf_YSize + 2);
				ng.ng_Height = (WORD)(o->h - pv->tf->tf_YSize - 2);
			}
		} else if (o->levellen) {
			ng.ng_Width -= (WORD)(pv->tf->tf_XSize * o->levellen + 8);
		}
		tags[t].ti_Tag = GTSL_Min;
		tags[t++].ti_Data = (ULONG)o->min;
		tags[t].ti_Tag = GTSL_Max;
		tags[t++].ti_Data = (ULONG)o->max;
		tags[t].ti_Tag = GTSL_Level;
		tags[t++].ti_Data = (ULONG)o->val;
		tags[t].ti_Tag = PGA_Freedom;
		tags[t++].ti_Data = (o->flags & F_VERT) ? LORIENT_VERT : LORIENT_HORIZ;
		tags[t].ti_Tag = GA_RelVerify;
		tags[t++].ti_Data = TRUE;
		if (o->levellen) {
			tags[t].ti_Tag = GTSL_LevelFormat;
			tags[t++].ti_Data = (ULONG)o->levelfmt;
			tags[t].ti_Tag = GTSL_MaxLevelLen;
			tags[t++].ti_Data = (ULONG)o->levellen;
			tags[t].ti_Tag = GTSL_LevelPlace;
			tags[t++].ti_Data = PLACETEXT_RIGHT;
		}
		break;
	case EGO_MX:
		kind = MX_KIND;
		ng.ng_Width = 17;
		ng.ng_Height = (WORD)(pv->tf->tf_YSize + 1);
		ng.ng_Flags = PLACETEXT_RIGHT;
		ng.ng_GadgetText = 0;
		tags[t].ti_Tag = GTMX_Labels;
		tags[t++].ti_Data = (ULONG)o->labels;
		tags[t].ti_Tag = GTMX_Active;
		tags[t++].ti_Data = (ULONG)o->val;
		tags[t].ti_Tag = GTMX_Spacing;
		tags[t++].ti_Data = 1;
		break;
	case EGO_CYCLE:
		kind = CYCLE_KIND;
		tags[t].ti_Tag = GTCY_Labels;
		tags[t++].ti_Data = (ULONG)o->labels;
		tags[t].ti_Tag = GTCY_Active;
		tags[t++].ti_Data = (ULONG)o->val;
		break;
	case EGO_LISTV:
		kind = LISTVIEW_KIND;
		ng.ng_LeftEdge = o->x;
		ng.ng_Width = o->w;
		ng.ng_Flags = PLACETEXT_ABOVE;
		if (o->labw) {
			ng.ng_TopEdge = (WORD)(o->y + pv->tf->tf_YSize + 2);
			ng.ng_Height = (WORD)(o->h - pv->tf->tf_YSize - 2);
		}
		tags[t].ti_Tag = GTLV_Labels;
		tags[t++].ti_Data = (ULONG)o->list;
		tags[t].ti_Tag = GTLV_ReadOnly;
		tags[t++].ti_Data = (o->flags & F_READONLY) ? TRUE : FALSE;
		if (!(o->flags & F_READONLY)) {
			tags[t].ti_Tag = GTLV_ShowSelected;
			tags[t++].ti_Data = 0;
			tags[t].ti_Tag = GTLV_Selected;
			tags[t++].ti_Data = (ULONG)o->val;
		}
		break;
	default:
		return prev;
	}
	tags[t].ti_Tag = GA_Disabled;
	tags[t++].ti_Data = (o->flags & F_DISABLED) ? TRUE : FALSE;
	tags[t].ti_Tag = TAG_DONE;
	o->gad = CreateGadgetA(kind, prev, &ng, tags);
	return o->gad ? o->gad : prev;
}

struct mkctx {
	struct EG_GuiPriv *pv;
	struct Gadget *prev;
};

static void mkgad_walk(EG_Obj *o, APTR ud)
{
	struct mkctx *c = (struct mkctx *)ud;

	o->gad = 0;
	if (!isgroup(o) && o->type >= EGO_TEXT && o->type != EGO_PLUGIN)
		c->prev = mkgad(c->pv, c->prev, o);
}

static void cleargad_walk(EG_Obj *o, APTR ud)
{
	(void)ud;
	o->gad = 0;
}

/* ================================================================ drawing */

static void drawdecor_walk(EG_Obj *o, APTR ud)
{
	struct EG_GuiPriv *pv = (struct EG_GuiPriv *)ud;
	struct Window *w = pv->gh->wnd;
	struct RastPort *rp = w->RPort;
	struct DrawInfo *di = GetScreenDrawInfo(pv->scr);
	UWORD shine = di ? di->dri_Pens[SHINEPEN] : 2;
	UWORD shadow = di ? di->dri_Pens[SHADOWPEN] : 1;

	switch (o->type) {
	case EGO_BEVEL:
	case EGO_BEVELR:
		DrawBevelBox(rp, o->x, o->y, o->w, o->h,
		             GT_VisualInfo, (ULONG)pv->vi,
		             GTBB_Recessed, o->type == EGO_BEVELR,
		             TAG_DONE);
		break;
	case EGO_BAR:
		if (o->w > o->h) {
			WORD y = (WORD)(o->y + o->h / 2 - 1);

			SetAPen(rp, shadow);
			Move(rp, o->x, y);
			Draw(rp, (WORD)(o->x + o->w - 1), y);
			SetAPen(rp, shine);
			Move(rp, o->x, (WORD)(y + 1));
			Draw(rp, (WORD)(o->x + o->w - 1), (WORD)(y + 1));
		} else {
			WORD x = (WORD)(o->x + o->w / 2 - 1);

			SetAPen(rp, shadow);
			Move(rp, x, o->y);
			Draw(rp, x, (WORD)(o->y + o->h - 1));
			SetAPen(rp, shine);
			Move(rp, (WORD)(x + 1), o->y);
			Draw(rp, (WORD)(x + 1), (WORD)(o->y + o->h - 1));
		}
		break;
	case EGO_MX:
		if (haslabel(o)) {
			struct IntuiText it;

			it.FrontPen = di ? di->dri_Pens[TEXTPEN] : 1;
			it.BackPen = 0;
			it.DrawMode = JAM1;
			it.LeftEdge = 0;
			it.TopEdge = 0;
			it.ITextFont = &pv->ta;
			it.IText = (UBYTE *)o->label;
			it.NextText = 0;
			PrintIText(rp, &it, o->x, (WORD)(o->y + 1));
		}
		break;
	}
	if (di)
		FreeScreenDrawInfo(pv->scr, di);
}

static void render_walk(EG_Obj *o, APTR ud)
{
	struct EG_GuiPriv *pv = (struct EG_GuiPriv *)ud;
	struct EG_Plugin *p;

	if (o->type == EGO_PLUGIN && (p = o->plugin) && p->cls && p->cls->render) {
		p->gh = pv->gh;
		p->cls->render(p, &pv->ta, p->x, p->y, p->xs, p->ys, pv->gh->wnd);
	}
}

static void clear_walk(EG_Obj *o, APTR ud)
{
	struct EG_GuiPriv *pv = (struct EG_GuiPriv *)ud;
	struct EG_Plugin *p;

	if (o->type == EGO_PLUGIN && (p = o->plugin) && p->cls && p->cls->clear_render)
		p->cls->clear_render(p, pv->gh->wnd);
}

/* lays out for the window's inner size and builds and shows the gadgets */
static BOOL build(struct EG_GuiPriv *pv)
{
	struct Window *w = pv->gh->wnd;
	struct mkctx c;
	WORD iw, ih;

	iw = (WORD)(w->Width - w->BorderLeft - w->BorderRight - 2 * WINM);
	ih = (WORD)(w->Height - w->BorderTop - w->BorderBottom - 2 * WINM);
	if (iw < pv->root->minw)
		iw = pv->root->minw;
	if (ih < pv->root->minh)
		ih = pv->root->minh;
	place(pv->root, (WORD)(w->BorderLeft + WINM), (WORD)(w->BorderTop + WINM), iw, ih);

	pv->glist = 0;
	c.pv = pv;
	c.prev = CreateContext(&pv->glist);
	if (!c.prev)
		return FALSE;
	walk(pv->root, mkgad_walk, &c);

	EraseRect(w->RPort, w->BorderLeft, w->BorderTop,
	          (WORD)(w->Width - w->BorderRight - 1), (WORD)(w->Height - w->BorderBottom - 1));
	RefreshWindowFrame(w);
	AddGList(w, pv->glist, (UWORD)~0, -1, 0);
	RefreshGList(pv->glist, w, 0, -1);
	GT_RefreshWindow(w, 0);
	walk(pv->root, drawdecor_walk, pv);
	walk(pv->root, render_walk, pv);
	return TRUE;
}

static void unbuild(struct EG_GuiPriv *pv)
{
	struct Window *w = pv->gh->wnd;

	walk(pv->root, clear_walk, pv);
	if (pv->glist) {
		if (w)
			RemoveGList(w, pv->glist, -1);
		FreeGadgets(pv->glist);
		pv->glist = 0;
	}
	walk(pv->root, cleargad_walk, pv);
}

/* ================================================================ windows */

EG_Multi *eg_multiinit(void)
{
	EG_Multi *mh = (EG_Multi *)AllocVec(sizeof(EG_Multi), MEMF_PUBLIC | MEMF_CLEAR);

	if (!mh)
		return 0;
	mh->port = CreateMsgPort();
	if (!mh->port) {
		FreeVec(mh);
		return 0;
	}
	if (!WorkbenchBase)
		WorkbenchBase = OpenLibrary((CONST_STRPTR)"workbench.library", 36);
	if (WorkbenchBase)
		mh->appport = CreateMsgPort();
	return mh;
}

ULONG eg_multisig(EG_Multi *mh)
{
	ULONG s = 0;

	if (mh) {
		s = 1UL << mh->port->mp_SigBit;
		if (mh->appport)
			s |= 1UL << mh->appport->mp_SigBit;
	}
	return s;
}

static void reply_pending(struct MsgPort *port, struct Window *win)
{
	struct IntuiMessage *msg, *succ;

	msg = (struct IntuiMessage *)port->mp_MsgList.lh_Head;
	while ((succ = (struct IntuiMessage *)msg->ExecMessage.mn_Node.ln_Succ)) {
		if (msg->IDCMPWindow == win) {
			Remove((struct Node *)msg);
			ReplyMsg((struct Message *)msg);
		}
		msg = succ;
	}
}

static ULONG idcmpflags(void)
{
	return BUTTONIDCMP | CHECKBOXIDCMP | SLIDERIDCMP | MXIDCMP | CYCLEIDCMP
	       | LISTVIEWIDCMP | IDCMP_CLOSEWINDOW | IDCMP_NEWSIZE
	       | IDCMP_REFRESHWINDOW | IDCMP_MENUPICK | IDCMP_VANILLAKEY
	       | IDCMP_RAWKEY | IDCMP_MOUSEBUTTONS | IDCMP_MOUSEMOVE
	       | IDCMP_INTUITICKS | IDCMP_ACTIVEWINDOW | IDCMP_INACTIVEWINDOW
	       | IDCMP_GADGETDOWN | IDCMP_GADGETUP;
}

static BOOL wantsapp(EG_Obj *o)
{
	for (; o; o = o->next) {
		if (o->type == EGO_LISTV && o->appproc)
			return TRUE;
		if (o->child && wantsapp(o->child))
			return TRUE;
	}
	return FALSE;
}

static void computemin(struct EG_GuiPriv *pv)
{
	struct measure m;
	WORD bl, br, bt, bb;

	InitRastPort(&m.rp);
	SetFont(&m.rp, pv->tf);
	m.fh = pv->tf->tf_YSize;
	m.cw = (WORD)TextLength(&m.rp, (STRPTR)"n", 1);
	if (m.cw < pv->tf->tf_XSize)
		m.cw = pv->tf->tf_XSize;
	measure(pv->root, &m, TRUE);

	if (pv->opts.wtype == WTYPE_BASIC || pv->opts.wtype == WTYPE_NOBORDER) {
		bl = br = bt = bb = 0;
	} else {
		bl = pv->scr->WBorLeft;
		br = pv->scr->WBorRight;
		bt = (WORD)(pv->scr->WBorTop + pv->scr->Font->ta_YSize + 1);
		bb = pv->scr->WBorBottom;
		if (pv->opts.wtype != WTYPE_NOSIZE && (pv->root->expx || pv->root->expy)) {
			br = 18;                /* size gadget */
			bb = 10;
		}
	}
	pv->minw = (WORD)(pv->root->minw + 2 * WINM + bl + br);
	pv->minh = (WORD)(pv->root->minh + 2 * WINM + bt + bb);
}

BOOL eg_openwin(EG_Gui *gh)
{
	struct EG_GuiPriv *pv;
	struct Window *w;
	BOOL sizable, basic;
	WORD left, top, width, height;

	if (!gh || !(pv = gh->priv))
		return FALSE;
	if (gh->wnd) {
		WindowToFront(gh->wnd);
		return TRUE;
	}
	computemin(pv);
	basic = pv->opts.wtype == WTYPE_BASIC || pv->opts.wtype == WTYPE_NOBORDER;
	sizable = !basic && pv->opts.wtype != WTYPE_NOSIZE
	          && (pv->root->expx || pv->root->expy);
	width = pv->width > pv->minw ? pv->width : pv->minw;
	height = pv->height > pv->minh ? pv->height : pv->minh;
	if (!pv->root->expx)
		width = pv->minw;
	if (!pv->root->expy)
		height = pv->minh;
	if (width > pv->scr->Width)
		width = pv->scr->Width;
	if (height > pv->scr->Height)
		height = pv->scr->Height;
	left = pv->left >= 0 ? pv->left : (WORD)((pv->scr->Width - width) / 2);
	top = pv->top >= 0 ? pv->top : (WORD)((pv->scr->Height - height) / 2);
	if (left + width > pv->scr->Width)
		left = (WORD)(pv->scr->Width - width);
	if (top + height > pv->scr->Height)
		top = (WORD)(pv->scr->Height - height);

	w = OpenWindowTags(0,
		WA_Left, left, WA_Top, top, WA_Width, width, WA_Height, height,
		WA_MinWidth, pv->minw, WA_MinHeight, pv->minh,
		WA_MaxWidth, pv->root->expx ? ~0 : pv->minw,
		WA_MaxHeight, pv->root->expy ? ~0 : pv->minh,
		WA_Title, basic ? 0 : (ULONG)gh->title,
		WA_ScreenTitle, (ULONG)pv->opts.screentitle,
		WA_PubScreen, (ULONG)pv->scr,
		WA_DragBar, !basic,
		WA_DepthGadget, !basic,
		WA_CloseGadget, !basic,
		WA_SizeGadget, sizable,
		WA_SizeBBottom, sizable,
		WA_Borderless, basic,
		WA_Activate, TRUE,
		WA_SmartRefresh, TRUE,
		WA_ReportMouse, TRUE,
		WA_NewLookMenus, TRUE,
		WA_IDCMP, 0,
		TAG_DONE);
	if (!w)
		return FALSE;
	gh->wnd = w;
	w->UserData = (BYTE *)gh;
	w->UserPort = pv->mh->port;
	ModifyIDCMP(w, idcmpflags());
	if (!build(pv)) {
		eg_closewin(gh);
		return FALSE;
	}
	if (pv->menu)
		SetMenuStrip(w, pv->menu);
	if (pv->mh->appport && (pv->opts.awproc || wantsapp(pv->root)))
		pv->appwin = AddAppWindowA(0, (ULONG)gh, w, pv->mh->appport, 0);
	return TRUE;
}

void eg_closewin(EG_Gui *gh)
{
	struct EG_GuiPriv *pv;
	struct Window *w;

	if (!gh || !(pv = gh->priv) || !(w = gh->wnd))
		return;
	walk(pv->root, clear_walk, pv);
	pv->left = w->LeftEdge;
	pv->top = w->TopEdge;
	pv->width = w->Width;
	pv->height = w->Height;
	if (pv->appwin) {
		RemoveAppWindow(pv->appwin);
		pv->appwin = 0;
	}
	while (pv->blocked > 0)
		eg_unblockwin(gh);
	if (w->MenuStrip)
		ClearMenuStrip(w);
	Forbid();
	reply_pending(pv->mh->port, w);
	w->UserPort = 0;
	ModifyIDCMP(w, 0);
	Permit();
	if (pv->glist) {
		RemoveGList(w, pv->glist, -1);
		FreeGadgets(pv->glist);
		pv->glist = 0;
	}
	walk(pv->root, cleargad_walk, pv);
	gh->wnd = 0;
	CloseWindow(w);
}

EG_Gui *eg_add(EG_Multi *mh, CONST_STRPTR title, EG_Obj *root,
               const struct EG_WinOpts *opts)
{
	EG_Gui *gh;
	struct EG_GuiPriv *pv;

	if (!mh || !root)
		return 0;
	gh = (EG_Gui *)AllocVec(sizeof(EG_Gui), MEMF_PUBLIC | MEMF_CLEAR);
	pv = (struct EG_GuiPriv *)AllocVec(sizeof(*pv), MEMF_PUBLIC | MEMF_CLEAR);
	if (!gh || !pv) {
		FreeVec(gh);
		FreeVec(pv);
		freeobjs(root);
		return 0;
	}
	gh->priv = pv;
	gh->title = title;
	pv->gh = gh;
	pv->mh = mh;
	pv->root = root;
	if (opts)
		pv->opts = *opts;
	else
		pv->opts.left = pv->opts.top = pv->opts.width = pv->opts.height = -1;
	if (!opts)
		pv->opts.wtype = WTYPE_SIZE;
	gh->info = pv->opts.info;
	pv->left = pv->opts.left;
	pv->top = pv->opts.top;
	pv->width = pv->opts.width;
	pv->height = pv->opts.height;

	if (pv->opts.screen) {
		pv->scr = pv->opts.screen;
	} else {
		pv->scr = LockPubScreen(0);
		pv->scrlocked = TRUE;
	}
	if (!pv->scr)
		goto fail;
	pv->ta = pv->opts.font ? *pv->opts.font : *pv->scr->Font;
	if (!(pv->tf = OpenFont(&pv->ta)))
		goto fail;
	pv->ta.ta_YSize = pv->tf->tf_YSize;
	if (!(pv->vi = GetVisualInfoA(pv->scr, 0)))
		goto fail;
	if (pv->opts.menu) {
		pv->menu = CreateMenusA(pv->opts.menu, 0);
		if (!pv->menu || !LayoutMenus(pv->menu, pv->vi, GTMN_NewLookMenus, TRUE, TAG_DONE))
			goto fail;
	}
	pv->nextgui = mh->guis;
	mh->guis = pv;
	if (!pv->opts.hide)
		eg_openwin(gh);
	return gh;

fail:
	pv->nextgui = 0;
	mh->guis = mh->guis;    /* not linked yet */
	if (pv->menu)
		FreeMenus(pv->menu);
	if (pv->vi)
		FreeVisualInfo(pv->vi);
	if (pv->tf)
		CloseFont(pv->tf);
	if (pv->scrlocked && pv->scr)
		UnlockPubScreen(0, pv->scr);
	freeobjs(root);
	FreeVec(pv);
	FreeVec(gh);
	return 0;
}

void eg_remove(EG_Gui *gh)
{
	struct EG_GuiPriv *pv, **pp;

	if (!gh || !(pv = gh->priv) || pv->removed)
		return;
	pv->removed = TRUE;
	eg_closewin(gh);
	if (pv->opts.clean)
		pv->opts.clean(gh, 0, 0);
	for (pp = &pv->mh->guis; *pp; pp = &(*pp)->nextgui)
		if (*pp == pv) {
			*pp = pv->nextgui;
			break;
		}
	freeobjs(pv->root);
	if (pv->menu)
		FreeMenus(pv->menu);
	FreeVisualInfo(pv->vi);
	CloseFont(pv->tf);
	if (pv->scrlocked)
		UnlockPubScreen(0, pv->scr);
	FreeVec(pv);
	gh->priv = 0;
	FreeVec(gh);
}

void eg_cleanmulti(EG_Multi *mh)
{
	if (!mh)
		return;
	while (mh->guis)
		eg_remove(mh->guis->gh);
	if (mh->appport) {
		struct Message *m;

		while ((m = GetMsg(mh->appport)))
			ReplyMsg(m);
		DeleteMsgPort(mh->appport);
	}
	DeleteMsgPort(mh->port);
	FreeVec(mh);
}

void eg_quitgui(LONG result)
{
	quitvalue = result;
	quitflag = TRUE;
}

void eg_blockwin(EG_Gui *gh)
{
	struct EG_GuiPriv *pv;

	if (!gh || !(pv = gh->priv) || !gh->wnd)
		return;
	if (pv->blocked++ == 0) {
		InitRequester(&pv->req);
		Request(&pv->req, gh->wnd);
		if (((struct Library *)IntuitionBase)->lib_Version >= 39)
			SetWindowPointer(gh->wnd, WA_BusyPointer, TRUE, TAG_DONE);
	}
}

void eg_unblockwin(EG_Gui *gh)
{
	struct EG_GuiPriv *pv;

	if (!gh || !(pv = gh->priv) || !gh->wnd || pv->blocked <= 0)
		return;
	if (--pv->blocked == 0) {
		EndRequest(&pv->req, gh->wnd);
		if (((struct Library *)IntuitionBase)->lib_Version >= 39)
			SetWindowPointer(gh->wnd, TAG_DONE);
	}
}

static void relayout(struct EG_GuiPriv *pv)
{
	struct Window *w = pv->gh->wnd;

	unbuild(pv);
	computemin(pv);
	if (w->Width < pv->minw || w->Height < pv->minh
	    || (!pv->root->expx && w->Width != pv->minw)
	    || (!pv->root->expy && w->Height != pv->minh)) {
		WORD nw = w->Width < pv->minw || !pv->root->expx ? pv->minw : w->Width;
		WORD nh = w->Height < pv->minh || !pv->root->expy ? pv->minh : w->Height;

		WindowLimits(w, 1, 1, ~0, ~0);
		ChangeWindowBox(w, w->LeftEdge, w->TopEdge, nw, nh);
		/* build again on the NEWSIZE that follows */
		WindowLimits(w, pv->minw, pv->minh, pv->root->expx ? ~0 : pv->minw,
		             pv->root->expy ? ~0 : pv->minh);
	}
	build(pv);
}

void eg_changegui(EG_Gui *gh, EG_Obj *root)
{
	struct EG_GuiPriv *pv;

	if (!gh || !(pv = gh->priv) || !root)
		return;
	if (gh->wnd)
		unbuild(pv);
	freeobjs(pv->root);
	pv->root = root;
	if (gh->wnd)
		relayout(pv);
}

struct Menu *eg_menustrip(EG_Gui *gh)
{
	return gh && gh->priv ? gh->priv->menu : 0;
}

void eg_winbox(EG_Gui *gh, WORD *left, WORD *top, WORD *width, WORD *height)
{
	struct EG_GuiPriv *pv = gh ? gh->priv : 0;

	if (!pv)
		return;
	if (gh->wnd) {
		*left = gh->wnd->LeftEdge;
		*top = gh->wnd->TopEdge;
		*width = gh->wnd->Width;
		*height = gh->wnd->Height;
	} else {
		*left = pv->left;
		*top = pv->top;
		*width = pv->width;
		*height = pv->height;
	}
}

/* ================================================================ setters */

static void setattr(EG_Gui *gh, EG_Obj *o, ULONG tag, ULONG data)
{
	if (gh && gh->wnd && o->gad)
		GT_SetGadgetAttrs(o->gad, gh->wnd, 0, tag, data, TAG_DONE);
}

void eg_settext(EG_Gui *gh, EG_Obj *o, CONST_STRPTR text)
{
	if (!o)
		return;
	settextbuf(o, text);
	setattr(gh, o, GTTX_Text, (ULONG)o->text);
}

void eg_setnum(EG_Gui *gh, EG_Obj *o, LONG val)
{
	if (!o)
		return;
	o->val = val;
	setattr(gh, o, GTNM_Number, (ULONG)val);
}

void eg_setcheck(EG_Gui *gh, EG_Obj *o, BOOL checked)
{
	if (!o)
		return;
	o->val = checked ? 1 : 0;
	setattr(gh, o, GTCB_Checked, checked ? TRUE : FALSE);
}

void eg_setslide(EG_Gui *gh, EG_Obj *o, LONG val)
{
	if (!o)
		return;
	o->val = val;
	setattr(gh, o, GTSL_Level, (ULONG)val);
}

void eg_setmx(EG_Gui *gh, EG_Obj *o, LONG val)
{
	if (!o)
		return;
	o->val = val;
	setattr(gh, o, GTMX_Active, (ULONG)val);
}

void eg_setcycle(EG_Gui *gh, EG_Obj *o, LONG val)
{
	if (!o)
		return;
	o->val = val;
	setattr(gh, o, GTCY_Active, (ULONG)val);
}

void eg_setlistvlabels(EG_Gui *gh, EG_Obj *o, struct List *list)
{
	if (!o)
		return;
	if (list)
		o->list = list;
	setattr(gh, o, GTLV_Labels, list ? (ULONG)list : ~0UL);
}

void eg_setlistvselected(EG_Gui *gh, EG_Obj *o, LONG sel)
{
	if (!o)
		return;
	o->val = sel;
	if (gh && gh->wnd && o->gad)
		GT_SetGadgetAttrs(o->gad, gh->wnd, 0, GTLV_Selected, (ULONG)sel,
		                  GTLV_Top, (ULONG)(sel >= 0 ? sel : 0), TAG_DONE);
}

void eg_setdisabled(EG_Gui *gh, EG_Obj *o, BOOL disabled)
{
	if (!o)
		return;
	if (disabled)
		o->flags |= F_DISABLED;
	else
		o->flags &= (UBYTE)~F_DISABLED;
	setattr(gh, o, GA_Disabled, disabled ? TRUE : FALSE);
}

BOOL eg_getcheck(EG_Obj *o) { return o && o->val ? TRUE : FALSE; }
LONG eg_getslide(EG_Obj *o) { return o ? o->val : 0; }
LONG eg_getmx(EG_Obj *o) { return o ? o->val : 0; }

void eg_renderplugin(EG_Gui *gh, struct EG_Plugin *p)
{
	if (gh && gh->wnd && gh->priv && p && p->cls && p->cls->render) {
		p->gh = gh;
		p->cls->render(p, &gh->priv->ta, p->x, p->y, p->xs, p->ys, gh->wnd);
	}
}

/* ================================================================ events */

static BOOL guialive(EG_Multi *mh, EG_Gui *gh)
{
	struct EG_GuiPriv *pv;

	for (pv = mh->guis; pv; pv = pv->nextgui)
		if (pv->gh == gh)
			return gh->wnd != 0;
	return FALSE;
}

/* plugins in the window, in order */
struct plugctx {
	struct IntuiMessage *imsg;
	EG_Obj *hit;
};

static void plugtest_walk(EG_Obj *o, APTR ud)
{
	struct plugctx *c = (struct plugctx *)ud;
	struct EG_Plugin *p;

	if (c->hit || o->type != EGO_PLUGIN || !(p = o->plugin) || !p->cls || !p->cls->message_test)
		return;
	if (p->cls->message_test(p, c->imsg, c->imsg->IDCMPWindow))
		c->hit = o;
}

struct keyctx {
	LONG key;
	EG_Obj *hit;
};

static void key_walk(EG_Obj *o, APTR ud)
{
	struct keyctx *c = (struct keyctx *)ud;

	if (!c->hit && o->key && o->type >= EGO_TEXT && o->type != EGO_PLUGIN
	    && !(o->flags & F_DISABLED) && (LONG)ToLower((ULONG)o->key) == c->key)
		c->hit = o;
}

static void dokey(EG_Gui *gh, UWORD code, UWORD qual)
{
	struct keyctx c;
	EG_Obj *o;
	BOOL shift = (qual & (IEQUALIFIER_LSHIFT | IEQUALIFIER_RSHIFT)) != 0;
	LONG n;

	c.key = (LONG)ToLower(code);
	c.hit = 0;
	walk(gh->priv->root, key_walk, &c);
	if (!(o = c.hit))
		return;
	switch (o->type) {
	case EGO_BUTTON:
	case EGO_SBUTTON:
		if (o->action)
			o->action(gh, o, 0);
		break;
	case EGO_CHECK:
		eg_setcheck(gh, o, !o->val);
		if (o->action)
			o->action(gh, o, o->val);
		break;
	case EGO_SLIDE:
		n = o->val + (shift ? -1 : 1);
		if (n < o->min)
			n = o->min;
		if (n > o->max)
			n = o->max;
		if (n != o->val) {
			eg_setslide(gh, o, n);
			if (o->action)
				o->action(gh, o, n);
		}
		break;
	case EGO_MX:
	case EGO_CYCLE:
		for (n = 0; o->labels && o->labels[n]; n++)
			;
		if (n) {
			LONG v = (o->val + (shift ? n - 1 : 1)) % n;

			if (o->type == EGO_MX)
				eg_setmx(gh, o, v);
			else
				eg_setcycle(gh, o, v);
			if (o->action)
				o->action(gh, o, v);
		}
		break;
	}
}

static void domenu(EG_Gui *gh, UWORD code)
{
	struct EG_GuiPriv *pv = gh->priv;
	EG_Multi *mh = pv->mh;

	while (code != MENUNULL && guialive(mh, gh) && pv->menu) {
		struct MenuItem *item = ItemAddress(pv->menu, code);
		eg_action fn;

		if (!item)
			break;
		fn = (eg_action)GTMENUITEM_USERDATA(item);
		code = item->NextSelect;
		if (fn)
			fn(gh, 0, 0);
	}
}

static void dogadget(EG_Gui *gh, struct Gadget *g, ULONG class, UWORD code)
{
	EG_Obj *o = (EG_Obj *)g->UserData;
	LONG v = 0;

	if (!o)
		return;
	switch (o->type) {
	case EGO_BUTTON:
	case EGO_SBUTTON:
		if (class != IDCMP_GADGETUP)
			return;
		break;
	case EGO_CHECK:
		if (class != IDCMP_GADGETUP)
			return;
		o->val = (g->Flags & GFLG_SELECTED) ? 1 : 0;
		v = o->val;
		break;
	case EGO_SLIDE:
		if ((LONG)(WORD)code == o->val && class != IDCMP_GADGETUP)
			return;
		o->val = (WORD)code;
		v = o->val;
		break;
	case EGO_MX:
		if (class != IDCMP_GADGETDOWN)
			return;
		o->val = code;
		v = code;
		break;
	case EGO_CYCLE:
	case EGO_LISTV:
		if (class != IDCMP_GADGETUP)
			return;
		o->val = code;
		v = code;
		break;
	default:
		return;
	}
	if (o->action)
		o->action(gh, o, v);
}

static void dowindow(EG_Gui *gh, struct IntuiMessage *imsg)
{
	struct EG_GuiPriv *pv = gh->priv;
	struct plugctx pc;
	ULONG class = imsg->Class;
	UWORD code = imsg->Code, qual = imsg->Qualifier;
	APTR iaddr = imsg->IAddress;
	struct Window *w = imsg->IDCMPWindow;

	if (class == IDCMP_REFRESHWINDOW) {
		GT_ReplyIMsg(imsg);
		GT_BeginRefresh(w);
		GT_EndRefresh(w, TRUE);
		return;
	}
	if (class == IDCMP_NEWSIZE) {
		GT_ReplyIMsg(imsg);
		unbuild(pv);
		build(pv);
		return;
	}
	if (pv->blocked) {
		GT_ReplyIMsg(imsg);
		return;
	}
	pc.imsg = imsg;
	pc.hit = 0;
	walk(pv->root, plugtest_walk, &pc);
	GT_ReplyIMsg(imsg);
	if (pc.hit) {
		struct EG_Plugin *p = pc.hit->plugin;

		if (p->cls->message_action && p->cls->message_action(p, class, qual, code, w)
		    && pc.hit->action && guialive(pv->mh, gh))
			pc.hit->action(gh, pc.hit, (LONG)p);
		return;
	}
	switch (class) {
	case IDCMP_CLOSEWINDOW:
		if (pv->opts.close)
			pv->opts.close(gh, 0, 0);
		else
			eg_closewin(gh);
		break;
	case IDCMP_MENUPICK:
		domenu(gh, code);
		break;
	case IDCMP_VANILLAKEY:
		dokey(gh, code, qual);
		break;
	case IDCMP_GADGETUP:
	case IDCMP_GADGETDOWN:
	case IDCMP_MOUSEMOVE:
		if (iaddr && (class != IDCMP_MOUSEMOVE
		              || (((struct Gadget *)iaddr)->UserData
		                  && ((EG_Obj *)((struct Gadget *)iaddr)->UserData)->type == EGO_SLIDE)))
			if (class != IDCMP_MOUSEMOVE || ((struct Gadget *)iaddr)->Flags & GFLG_SELECTED
			    || ((EG_Obj *)((struct Gadget *)iaddr)->UserData)->type == EGO_SLIDE)
				dogadget(gh, (struct Gadget *)iaddr, class, code);
		break;
	}
}

static void doapp(EG_Multi *mh, struct AppMessage *am)
{
	EG_Gui *gh = (EG_Gui *)am->am_UserData;
	struct EG_GuiPriv *pv;
	EG_Obj *hit = 0;

	if (!guialive(mh, gh))
		return;
	pv = gh->priv;
	if (pv->blocked)
		return;
	{
		/* a listview with an app proc under the drop point */
		EG_Obj *stack[32];
		int sp = 0;
		EG_Obj *o = pv->root;

		while (o || sp) {
			if (!o) {
				o = stack[--sp];
				continue;
			}
			if (o->type == EGO_LISTV && o->appproc
			    && am->am_MouseX >= o->x && am->am_MouseX < o->x + o->w
			    && am->am_MouseY >= o->y && am->am_MouseY < o->y + o->h)
				hit = o;
			if (o->child && sp < 32)
				stack[sp++] = o->child;
			o = o->next;
		}
	}
	if (hit)
		hit->appproc(gh, hit, am);
	else if (pv->opts.awproc)
		pv->opts.awproc(gh, 0, am);
}

LONG eg_multimessage(EG_Multi *mh)
{
	struct IntuiMessage *imsg;
	struct Message *am;

	if (!mh)
		return -1;
	quitflag = FALSE;
	while (!quitflag && (imsg = GT_GetIMsg(mh->port))) {
		EG_Gui *gh = (EG_Gui *)imsg->IDCMPWindow->UserData;

		if (gh && guialive(mh, gh))
			dowindow(gh, imsg);
		else
			GT_ReplyIMsg(imsg);
	}
	while (!quitflag && mh->appport && (am = GetMsg(mh->appport))) {
		doapp(mh, (struct AppMessage *)am);
		ReplyMsg(am);
	}
	if (quitflag) {
		quitflag = FALSE;
		return quitvalue;
	}
	return -1;
}
