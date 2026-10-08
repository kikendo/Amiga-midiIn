/*
 * titlekeys.c - title plugin that also takes keys, port of mbtitle.e
 *
 * mbtitle.e only adds message_test/message_action (keys) to title_plugin
 * from the binary module title.m. That plugin is rebuilt here from the
 * module's code:
 *   setup(text, type, font=NIL)   font given: used instead of the gui's
 *   min_size: text width (IntuiTextLength) + 24, font height (+1 for
 *             TITLE_THREEDEE)
 *   will_resize: horizontally only
 *   render: etched lines (pen 1 over pen 2) left and right of the text at
 *           half the font height; the text with PrintIText, 8 pixels from
 *           each line, at the top of the area
 *
 * Differences from the E code:
 * - The IntuiTexts are built on the stack instead of static lists that
 *   were changed in place.
 * - E's Line() on stdrast is SetAPen + Move/Draw on the window's
 *   rastport (e_line below).
 */
#include <exec/types.h>
#include <intuition/intuition.h>
#include <graphics/rastport.h>
#include <graphics/text.h>
#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/intuition.h>

#include "egui.h"
#include "titlekeys.h"
#include "keycodes.h"
#include "../app/eport.h"

static const struct EG_PluginClass titlekeys_class;

static void e_line(struct RastPort *rp, LONG x1, LONG y1, LONG x2, LONG y2,
                   LONG c)
{
	SetAPen(rp, c);
	Move(rp, x1, y1);
	Draw(rp, x2, y2);
}

static void setitext(struct IntuiText *it, UBYTE pen, WORD offset,
                     struct TextAttr *font, CONST_STRPTR text)
{
	it->FrontPen = pen;
	it->BackPen = 0;
	it->DrawMode = JAM1;
	it->LeftEdge = offset;
	it->TopEdge = offset;
	it->ITextFont = font;
	it->IText = (STRPTR)text;
	it->NextText = NULL;
}

struct titlekeys *titlekeys_new(CONST_STRPTR text, LONG type,
                                struct TextAttr *font)
{
	struct titlekeys *t;

	t = e_new(sizeof(struct titlekeys));
	t->plugin.cls = &titlekeys_class;
	t->text = text;
	t->type = type;
	if (font) {
		t->font = font;
		t->hasfont = TRUE;
	} else {
		t->hasfont = FALSE;
	}
	return t;
}

void titlekeys_dispose(struct titlekeys *t)
{
	e_dispose(t);
}

static void tk_class_dispose(struct EG_Plugin *p)
{
	e_dispose(p);
}

static void tk_min_size(struct EG_Plugin *p, struct TextAttr *ta, WORD fh,
                        WORD *w, WORD *h)
{
	struct titlekeys *t = (struct titlekeys *)p;
	struct IntuiText it;

	setitext(&it, 1, 0, t->hasfont ? t->font : ta, t->text);
	t->textlen = IntuiTextLength(&it);
	t->textheight = t->hasfont ? t->font->ta_YSize : fh;
	*w = t->textlen + 24;
	*h = t->textheight + (t->type == TITLE_THREEDEE ? 1 : 0);
}

static ULONG tk_will_resize(struct EG_Plugin *p)
{
	(void)p;
	return EG_RESIZEX;
}

static void tk_render(struct EG_Plugin *p, struct TextAttr *ta, WORD x,
                      WORD y, WORD xs, WORD ys, struct Window *win)
{
	struct titlekeys *t = (struct titlekeys *)p;
	struct RastPort *rp;
	struct IntuiText it;
	LONG a, b, tx;

	(void)ys;
	t->win = win;
	if (!t->hasfont)
		t->font = ta;
	rp = win->RPort;
	a = (xs - t->textlen) / 2 - 8;  /* length of each line */
	b = t->textheight / 2;

	e_line(rp, x, y + b - 1, x + a, y + b - 1, 1);
	e_line(rp, x, y + b, x + a, y + b, 2);

	tx = x + a + 8;
	switch (t->type) {
	case TITLE_NORMAL:
		setitext(&it, 1, 0, t->font, t->text);
		PrintIText(rp, &it, tx, y);
		break;
	case TITLE_HIGHLIGHT:
		setitext(&it, 2, 0, t->font, t->text);
		PrintIText(rp, &it, tx, y);
		break;
	case TITLE_THREEDEE:
		setitext(&it, 1, 1, t->font, t->text);
		PrintIText(rp, &it, tx, y);
		setitext(&it, 2, 0, t->font, t->text);
		PrintIText(rp, &it, tx, y);
		break;
	}

	e_line(rp, x + xs - a - 1, y + b - 1, x + xs - 1, y + b - 1, 1);
	e_line(rp, x + xs - a - 1, y + b, x + xs - 1, y + b, 2);
}

/* mbtitle.e */
static BOOL tk_message_test(struct EG_Plugin *p, struct IntuiMessage *imsg,
                            struct Window *win)
{
	LONG code;
	ULONG class;

	(void)p;
	(void)win;
	code = imsg->Code;
	class = imsg->Class;
	if (class & IDCMP_RAWKEY) {
		if (code >= CURSORUP && code <= KEYCODE_F10)
			return TRUE;
	} else if (class & IDCMP_VANILLAKEY) {
		switch (code) {
		case ESC_CODE:
		case '0': case '1': case '2': case '3': case '4':
		case '5': case '6': case '7': case '8': case '9':
		case '.':
		case '_':
		case ')':
		case '(':
		case '-':
		case '+':
		case '=':
		case '*':
		case 9:
		case 13:
		case 32:
			return TRUE;
		default:
			return FALSE;
		}
	}
	return FALSE;
}

static BOOL tk_message_action(struct EG_Plugin *p, ULONG class, UWORD qual,
                              UWORD code, struct Window *win)
{
	struct titlekeys *t = (struct titlekeys *)p;

	(void)qual;
	(void)win;
	switch (class) {
	case IDCMP_RAWKEY:
		t->keycode = code | MYRAWCODE;
		break;
	case IDCMP_VANILLAKEY:
		t->keycode = code;
		break;
	default:
		return FALSE;
	}
	return TRUE;
}

static const struct EG_PluginClass titlekeys_class = {
	tk_min_size,
	tk_will_resize,
	tk_render,
	NULL,                   /* clear_render */
	tk_message_test,
	tk_message_action,
	tk_class_dispose,
};
