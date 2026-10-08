/*
 * filereq.c - see filereq.h
 */
#include <exec/types.h>
#include <libraries/asl.h>
#include <workbench/startup.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/asl.h>

#include "eport.h"
#include "filereq.h"

struct Library *AslBase;

BOOL filereq(struct Window *win, CONST_STRPTR title, ULONG flags,
             STRPTR drawer, ULONG dsize, STRPTR file, ULONG fsize,
             void (*fn)(CONST_STRPTR path, APTR userdata), APTR userdata)
{
	struct FileRequester *fr;
	BOOL ok = FALSE;
	char path[512];

	if (!AslBase)
		AslBase = OpenLibrary((CONST_STRPTR)"asl.library", 37);
	if (!AslBase)
		return FALSE;
	fr = (struct FileRequester *)AllocAslRequestTags(ASL_FileRequest,
		ASLFR_TitleText, (ULONG)title,
		ASLFR_InitialDrawer, (ULONG)drawer,
		ASLFR_InitialFile, (ULONG)FilePart(file),
		ASLFR_Window, (ULONG)win,
		ASLFR_DoSaveMode, (flags & FR_SAVE) ? TRUE : FALSE,
		ASLFR_DoPatterns, (flags & FR_PATTERN) ? TRUE : FALSE,
		ASLFR_DoMultiSelect, (flags & FR_MULTI) ? TRUE : FALSE,
		TAG_DONE);
	if (!fr)
		return FALSE;
	if (AslRequest(fr, 0)) {
		ok = TRUE;
		estrcpy(drawer, (CONST_STRPTR)fr->fr_Drawer, dsize);
		if ((flags & FR_MULTI) && fr->fr_NumArgs > 0 && fn) {
			LONG i;

			for (i = 0; i < fr->fr_NumArgs; i++) {
				estrcpy((STRPTR)path, (CONST_STRPTR)fr->fr_Drawer, sizeof(path));
				AddPart((STRPTR)path, (STRPTR)fr->fr_ArgList[i].wa_Name, sizeof(path));
				fn((CONST_STRPTR)path, userdata);
			}
		} else {
			estrcpy((STRPTR)path, (CONST_STRPTR)fr->fr_Drawer, sizeof(path));
			AddPart((STRPTR)path, (STRPTR)fr->fr_File, sizeof(path));
			if (fn)
				fn((CONST_STRPTR)path, userdata);
			else
				estrcpy(file, (CONST_STRPTR)path, fsize);
		}
	}
	FreeAslRequest(fr);
	return ok;
}
