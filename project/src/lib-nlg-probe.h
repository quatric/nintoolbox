// SPDX-License-Identifier: GPL-2.0+
// Dependency-free probes for Next Level Games containers (see lib-nlg-lm.h
// for the full LM2/LM3/SANIM support). Kept in its own translation unit so
// core tools (e.g. gen-ui via lib-file.o) link without the model/texture
// decoders.
#ifndef SZS_LIB_NLG_PROBE_H
#define SZS_LIB_NLG_PROBE_H 1

#include "types.h"

#ifdef __cplusplus
extern "C"
{
#endif

// Standalone FEDM/FEDS/FEDT containers, versions 1..3 (Federation Force /
// LM2 / LM3). Shape checks only; full parsing lives in lib-nlg-lm.c.
bool IsNLGModel (const u8 *data, size_t size); // "FEDM"
bool IsNLGSkeleton (const u8 *data, size_t size); // "FEDS"
bool IsNLGTexture (const u8 *data, size_t size); // "FEDT"

// Mario Strikers SANIM animation stream: strict structural chunk walk.
bool IsSANIM (const u8 *data, size_t size);

#ifdef __cplusplus
}
#endif

#endif
