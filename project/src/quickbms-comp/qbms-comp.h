
/***************************************************************************
 *                                                                         *
 *   Vendored QuickBMS decompression codecs -- dispatch shim.               *
 *                                                                         *
 *   This wires a curated subset of the compression codecs shipped with     *
 *   QuickBMS (aluigi.altervista.org, quickbms-src-0.12.0) into the native  *
 *   BMS interpreter in lib-bms.c, so CLOG/COMTYPE directives naming these  *
 *   algorithms decompress natively instead of falling back to raw copy.    *
 *                                                                         *
 *   The upstream codec sources under codecs/ are unmodified except for     *
 *   local #include path fixes; see codecs/README for provenance and the    *
 *   per-file licences.  QuickBMS itself is GPL-2.0, matching this project. *
 *                                                                         *
 ***************************************************************************/

#ifndef WSZST_QBMS_COMP_H
#define WSZST_QBMS_COMP_H 1

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

//
// Decompress `src` (`comp_size` bytes) with the QuickBMS COMTYPE named by
// `comtype` (matched case-insensitively).
//
//   *out       receives a malloc()'d buffer owned by the caller (free() it)
//   *out_size  receives the number of valid bytes in that buffer
//   hint_size  script-supplied uncompressed size, or 0 when unknown; used
//              only to pick the initial output buffer size
//
// Returns 1 on success.  Returns 0 when `comtype` is not one this build
// handles or when the codec reported a failure -- in both cases *out is
// left untouched and the caller should fall back to its own handling.
//
int QbmsDecompress ( const char *comtype, const unsigned char *src,
	unsigned int comp_size, unsigned int hint_size,
	unsigned char **out, unsigned int *out_size );

//
// Returns 1 if `comtype` is handled by QbmsDecompress(), 0 otherwise.
// Does not attempt decompression.
//
int QbmsHandlesCompType ( const char *comtype );

//
// A space-separated, lowercase list of every COMTYPE name this build
// accepts.  Intended for --help / diagnostics.  The returned string is
// static storage and must not be freed.
//
const char * QbmsCompTypeList ( void );

#ifdef __cplusplus
}
#endif

#endif // WSZST_QBMS_COMP_H
