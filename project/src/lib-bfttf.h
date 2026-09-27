#ifndef LIB_BFTTF_H
#define LIB_BFTTF_H

#include "lib-nintendo.h"

// Nintendo Binary Font TrueType Font (.bfttf)
// Used in NintendoSDK (Siglo) across NX (Switch), Cafe (Wii U), and Windows.
//
// Layout:
//   u32 signature_scrambled (unscrambled == 0x7f9a0218)
//   u32 ttf_size_scrambled  (unscrambled == original TTF byte length)
//   u32 words[]             (scrambled TTF 4-byte big-endian chunks)
//
// Scrambling:
//   word = (orig_word ^ key)
//   word = ((word & 0xff00ff00) >> 8) | ((word << 8) & 0xff00ff00)
//   word = (word >> 16) | (word << 16)
// Stored as little-endian uint32.
//
// Platform keys:
//   NX:   0x49621806
//   Cafe: 0x8cf2dcd9
//   Win:  0xa6018502

#define BFTTF_SIGNATURE 0x7f9a0218
#define BFTTF_KEY_NX 0x49621806
#define BFTTF_KEY_CAFE 0x8cf2dcd9
#define BFTTF_KEY_WIN 0xa6018502

bool IsBFTTF (const u8 *data, uint size);
enumError DecodeBFTTF (u8 **out_ttf, uint *out_size, const u8 *data, uint size);
enumError EncodeBFTTF (u8 **out_bfttf, uint *out_size, const u8 *ttf_data, uint ttf_size, u32 key);

#endif // LIB_BFTTF_H
