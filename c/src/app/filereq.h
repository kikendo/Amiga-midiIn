/*
 * filereq.h - file requesters on asl.library (the E version used reqtools)
 */
#ifndef MI_FILEREQ_H
#define MI_FILEREQ_H

#include <exec/types.h>

struct Window;

#define FR_SAVE    1
#define FR_PATTERN 2
#define FR_MULTI   4

/*
 * Shows a file requester starting in drawer (size bytes, updated to the
 * chosen drawer) with file preselected (fsize bytes, updated). For each
 * chosen file, fn(fullpath, userdata) is called (with FR_MULTI possibly
 * several times); without fn the full path of the one file is left in
 * file. Returns FALSE if cancelled.
 */
BOOL filereq(struct Window *win, CONST_STRPTR title, ULONG flags,
             STRPTR drawer, ULONG dsize, STRPTR file, ULONG fsize,
             void (*fn)(CONST_STRPTR path, APTR userdata), APTR userdata);

#endif
