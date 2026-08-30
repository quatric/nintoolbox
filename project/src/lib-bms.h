#ifndef SZS_LIB_BMS_H
#define SZS_LIB_BMS_H 1

#include "types.h"

// QuickBMS script runner. If the bundled QuickBMS runtime is built, scripts
// run through its complete compatibility engine. The native interpreter is a
// source-tree fallback, enough for the common
// "open, walk a table, extract entries" extraction scripts.
// Supported: IDSTRING, GET/GETDSTRING/GETCT, PUT/PUTDSTRING/PUTCT, GOTO,
// SAVEPOS, MATH/XMATH (full expressions with parens), SET, STRING/STRLEN,
// GETVARCHR/PUTVARCHR, REVERSESHORT/LONG/LONGLONG, GETBITS/PUTBITS,
// PADDING, FINDLOC, APPEND, OPEN (multi-file, incl. MEMORY_FILE),
// FOR/NEXT, WHILE/ENDWHILE, IF/ELSE/ENDIF (with AND/OR), LOG, CLOG,
// COMTYPE, ENDIAN, PRINT. Not a full QuickBMS clone -- see lib-bms.c's
// header for what's out of scope (CallDLL, the crypto suite, array ops,
// most compression plugins beyond this fork's own native decoders).
// Set WBMSX_QUICKBMS to override the bundled runtime path.

enumError RunBmsScript (ccp script_path, ccp infile, ccp outdir);

#endif
