#ifndef SZS_LIB_BMS_H
#define SZS_LIB_BMS_H 1

#include "types.h"

// Minimal QuickBMS script interpreter, enough for the common
// "open, walk a table, extract entries" extraction scripts.
// Supported: IDSTRING, GET, GETDSTRING, GOTO, SAVEPOS, MATH, SET,
// FOR/NEXT, IF/ELSE/ENDIF, LOG, CLOG, COMTYPE, ENDIAN, PRINT.
// No quickbms binary is involved; scripts run in-process.

enumError RunBmsScript (ccp script_path, ccp infile, ccp outdir);

#endif
