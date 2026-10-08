/* tables.h - data tables of the E version */
#ifndef MI_TABLES_H
#define MI_TABLES_H

#include <exec/types.h>

#define TONETABLE_LEN 1301
extern const ULONG tonetable_raw[TONETABLE_LEN * 2];
double tonetable(LONG i);

#define EXPTABLE_LEN 2048
extern const WORD exptable[EXPTABLE_LEN];

#endif
