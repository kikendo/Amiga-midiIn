/*
 * egui.h - a small layout toolkit in the manner of AmigaE's EasyGUI, on
 * GadTools, for porting midiIn's windows
 *
 * A window's contents are a tree built with the constructors below:
 * groups (rows, columns, bevels, bars, spaces), GadTools gadgets and
 * plugins (custom drawn areas). Layout is computed from the font, windows
 * can be resized, and all windows of a multihandle share one message port.
 *
 * Callbacks:
 *   eg_action(gh, obj, value)   gadget used. value: button 0, check 0/1,
 *                               slider level, mx/cycle index, listview
 *                               selection, plugin: the plugin pointer.
 *                               Menu items and window close/clean hooks
 *                               are called with obj = NULL, value = 0.
 *   eg_appproc(gh, obj, msg)    Workbench icon dropped on the window
 *                               (obj NULL) or on a listview.
 *
 * gh->info is the EG_INFO value of the E code, for callbacks that used it.
 */
#ifndef EGUI_H
#define EGUI_H

#include <exec/types.h>
#include <exec/lists.h>
#include <intuition/intuition.h>
#include <libraries/gadtools.h>
#include <graphics/text.h>

typedef struct EG_Obj EG_Obj;
typedef struct EG_Gui EG_Gui;
typedef struct EG_Multi EG_Multi;
struct AppMessage;

typedef void (*eg_action)(EG_Gui *gh, EG_Obj *obj, LONG value);
typedef void (*eg_appproc)(EG_Gui *gh, EG_Obj *obj, struct AppMessage *msg);

/* ------------------------------------------------------------- plugins */

#define EG_RESIZEX 1
#define EG_RESIZEY 2

struct EG_Plugin;

/* any method may be NULL (EasyGUI's default) */
struct EG_PluginClass {
	/* minimum size in pixels; fh is the font height */
	void (*min_size)(struct EG_Plugin *p, struct TextAttr *ta, WORD fh,
	                 WORD *w, WORD *h);
	/* EG_RESIZEX | EG_RESIZEY; NULL: both */
	ULONG (*will_resize)(struct EG_Plugin *p);
	/* draw the plugin at x, y, xs * ys (also stored in the plugin) */
	void (*render)(struct EG_Plugin *p, struct TextAttr *ta, WORD x, WORD y,
	               WORD xs, WORD ys, struct Window *win);
	/* the area is about to be cleared or moved */
	void (*clear_render)(struct EG_Plugin *p, struct Window *win);
	/* TRUE if the plugin wants this message */
	BOOL (*message_test)(struct EG_Plugin *p, struct IntuiMessage *imsg,
	                     struct Window *win);
	/* handle it; TRUE: call the plugin's action */
	BOOL (*message_action)(struct EG_Plugin *p, ULONG class, UWORD qual,
	                       UWORD code, struct Window *win);
	/* free the plugin (E's END); called when its window is removed */
	void (*dispose)(struct EG_Plugin *p);
};

/* embed as the first member of a plugin object */
struct EG_Plugin {
	const struct EG_PluginClass *cls;
	WORD x, y, xs, ys;              /* current area, set by layout */
	EG_Gui *gh;                     /* window it is in */
};

/* ------------------------------------------------------------- objects */

/* groups: NULL-terminated child lists */
EG_Obj *eg_rows(EG_Obj *first, ...);
EG_Obj *eg_cols(EG_Obj *first, ...);
EG_Obj *eg_eqrows(EG_Obj *first, ...);
EG_Obj *eg_eqcols(EG_Obj *first, ...);
EG_Obj *eg_bevel(EG_Obj *child);        /* raised frame */
EG_Obj *eg_bevelr(EG_Obj *child);       /* recessed frame */
EG_Obj *eg_bar(void);                   /* separator line */
EG_Obj *eg_space(void);                 /* stretches both ways */
EG_Obj *eg_spaceh(void);                /* stretches horizontally */
EG_Obj *eg_spacev(void);                /* stretches vertically */

/*
 * Gadgets. label may be NULL or "" (no label); a "_" in it marks the
 * keyboard shortcut, key is that character (0 = none). minchars sizes text
 * fields and sliders in characters. Slider levels use the AmigaE format
 * ("\\d[2]" etc., "" = no level shown).
 */
EG_Obj *eg_text(CONST_STRPTR cur, CONST_STRPTR label, BOOL border, WORD minchars);
EG_Obj *eg_num(LONG val, CONST_STRPTR label, BOOL border, WORD minchars);
EG_Obj *eg_button(eg_action a, CONST_STRPTR label, LONG key);
EG_Obj *eg_sbutton(eg_action a, CONST_STRPTR label, LONG key); /* stretches */
EG_Obj *eg_check(eg_action a, CONST_STRPTR label, BOOL checked, BOOL left,
                 LONG key, BOOL disabled);
EG_Obj *eg_slide(eg_action a, CONST_STRPTR label, BOOL vertical, LONG min,
                 LONG max, LONG cur, WORD chars, CONST_STRPTR levelfmt,
                 LONG key, BOOL disabled);
EG_Obj *eg_mx(eg_action a, CONST_STRPTR label, CONST_STRPTR *labels, BOOL left,
              LONG cur, LONG key);
EG_Obj *eg_cycle(eg_action a, CONST_STRPTR label, CONST_STRPTR *labels,
                 LONG cur, LONG key);
EG_Obj *eg_listv(eg_action a, CONST_STRPTR label, WORD minchars, WORD minlines,
                 struct List *list, BOOL readonly, LONG selected,
                 eg_appproc appproc);
EG_Obj *eg_plugin(eg_action a, struct EG_Plugin *p);

/* user data on any object */
void eg_setdata(EG_Obj *o, APTR data);
APTR eg_getdata(EG_Obj *o);

/* ------------------------------------------------------------- windows */

#define WTYPE_NOBORDER 0
#define WTYPE_BASIC    1                /* borderless, no title bar */
#define WTYPE_NOSIZE   2
#define WTYPE_SIZE     3                /* default */

struct EG_WinOpts {
	struct NewMenu *menu;           /* nm_UserData: eg_action, or NULL */
	eg_appproc awproc;              /* icons dropped on the window */
	eg_action close;                /* close gadget; NULL: closewin */
	eg_action clean;                /* gui being removed */
	APTR info;
	struct Screen *screen;          /* NULL: default public screen */
	struct TextAttr *font;          /* NULL: the screen's font */
	WORD left, top, width, height;  /* -1: automatic */
	WORD wtype;
	BOOL hide;                      /* do not open the window yet */
	CONST_STRPTR screentitle;
};

struct EG_Gui {
	struct Window *wnd;             /* NULL while closed */
	APTR info;
	CONST_STRPTR title;
	/* the rest is private to egui.c */
	struct EG_GuiPriv *priv;
};

EG_Multi *eg_multiinit(void);
/* signal mask of the shared port (valid after the first eg_add) */
ULONG eg_multisig(EG_Multi *mh);
/* handles all pending messages; -1 normally, else the value given to
 * eg_quitgui() */
LONG eg_multimessage(EG_Multi *mh);
void eg_cleanmulti(EG_Multi *mh);       /* removes all guis */
void eg_quitgui(LONG result);           /* make eg_multimessage return */

/* adds a gui (opens its window unless hide); NULL on failure */
EG_Gui *eg_add(EG_Multi *mh, CONST_STRPTR title, EG_Obj *root,
               const struct EG_WinOpts *opts);
void eg_remove(EG_Gui *gh);             /* closes and frees (cleangui) */
BOOL eg_openwin(EG_Gui *gh);
void eg_closewin(EG_Gui *gh);
void eg_blockwin(EG_Gui *gh);           /* busy pointer, input ignored */
void eg_unblockwin(EG_Gui *gh);
void eg_changegui(EG_Gui *gh, EG_Obj *root);   /* new contents */
struct Menu *eg_menustrip(EG_Gui *gh);
/* current window box, for saving window positions */
void eg_winbox(EG_Gui *gh, WORD *left, WORD *top, WORD *width, WORD *height);

/* ------------------------------------------------------------- setters */

void eg_settext(EG_Gui *gh, EG_Obj *o, CONST_STRPTR text);
void eg_setnum(EG_Gui *gh, EG_Obj *o, LONG val);
void eg_setcheck(EG_Gui *gh, EG_Obj *o, BOOL checked);
void eg_setslide(EG_Gui *gh, EG_Obj *o, LONG val);
void eg_setmx(EG_Gui *gh, EG_Obj *o, LONG val);
void eg_setcycle(EG_Gui *gh, EG_Obj *o, LONG val);
/* list NULL: detach (E's -1, before changing the list) */
void eg_setlistvlabels(EG_Gui *gh, EG_Obj *o, struct List *list);
void eg_setlistvselected(EG_Gui *gh, EG_Obj *o, LONG sel);
void eg_setdisabled(EG_Gui *gh, EG_Obj *o, BOOL disabled);

/* the current values */
BOOL eg_getcheck(EG_Obj *o);
LONG eg_getslide(EG_Obj *o);
LONG eg_getmx(EG_Obj *o);

/* redraws a plugin (calls its render with its area), if shown */
void eg_renderplugin(EG_Gui *gh, struct EG_Plugin *p);

#endif /* EGUI_H */
