/*
 * report.h - requesters, error reports, status and about windows
 * (MBreport.e)
 */
#ifndef MI_REPORT_H
#define MI_REPORT_H

#include <exec/types.h>

struct Screen;
struct TextAttr;
struct TextFont;

extern char cxhotkey[60];

LONG reqclear(void);
LONG reqnotundo(void);
LONG reqnotredo(void);
LONG reqskipover(void);
LONG reqdontfit(LONG numleft);
LONG reqquit(void);
LONG reqexit(void);
LONG reqsample(CONST_STRPTR sname);
void report_exception(void);
void reqsumm(CONST_STRPTR n, LONG a, LONG b, LONG c, LONG d, LONG t, LONG usl);
STRPTR string_info(void);

void blockallwindows(void);
void unblockallwindows(void);

void open_status(struct Screen *screen, struct TextAttr *ta, struct TextFont *font);
void closestatus(void);
BOOL printstatus(CONST_STRPTR statustext, CONST_STRPTR infotext,
                 CONST_STRPTR typetext, LONG progrs, LONG full);
void open_aboutpic(struct Screen *screen, struct TextAttr *ta);
void reqabout(void);

#endif
