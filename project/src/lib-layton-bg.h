#ifndef LIB_LAYTON_BG_H
#define LIB_LAYTON_BG_H
#include "lib-std.h"
// Raw or double-typed Nitro-compressed Layton backgrounds. No unique magic:
// callers should restrict this probe to .arc/.arb image candidates.
enumError DecodeLaytonBG_RGBA (u8 **rgba, uint *width, uint *height, const u8 *src, uint size);
#endif
