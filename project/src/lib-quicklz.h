#ifndef SZS_LIB_QUICKLZ_H
#define SZS_LIB_QUICKLZ_H 1

#include "types.h"

// QuickLZ, as used by a number of Nintendo-adjacent tools and game archives.
//
// Two stream versions exist in the wild and they are NOT compatible: 1.20 and
// 1.4.0. Both are supported here. Decoding auto-detects which one a buffer is;
// encoding always emits 1.4.0, since that is what current producers write.

// True when SRC looks like a QuickLZ stream. This is an exact test, not a
// guess: it applies the vendor decoders' own header validation and then
// requires the size the header records to equal the buffer actually given.
bool IsQuickLZ (const u8 *src, uint src_size);

// Both allocate *dest with MALLOC(); the caller FREE()s it.
enumError DecodeQuickLZ (u8 **dest, uint *dest_size, const u8 *src, uint src_size);
enumError EncodeQuickLZ (u8 **dest, uint *dest_size, const u8 *src, uint src_size);

#endif
