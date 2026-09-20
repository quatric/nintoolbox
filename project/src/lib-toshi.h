// SPDX-License-Identifier: GPL-2.0+
//-----------------------------------------------------------------------------
// Blue Tongue "Toshi" engine TSFB/TRB containers (Nicktoons, Nickelodeon
// Barnyard Wii). Layout after OpenBarnyard (TTRB.h, TCompress_Decompress.cpp).
//
// TSFB: "TSFB", u32 file size - 8, "FBRT", then big-endian hunks of
//   {char tag[4], u32 size} padded to 4 bytes: "XRDH" header, "TCES" section
//   (or "CCES", BTEC compressed), "CLER" relocations {u32 count, {u32 section,
//   u32 offset} pointer fix-ups; pointers are stored as section offsets}, and
//   "BMYS" symbols {u32 count, count * {u16 section, u16 name offset, u32
//   hash, u32 data offset}, NUL-terminated names}.
// BTEC: "CETB", u16 major, u16 minor (1.2/1.3), u32 compressed size, u32
//   size, [u32 xor for 1.3], then commands: byte {bit 7 no-offset, bit 6
//   14-bit size, bits 0..5 size-1}, optional second size byte, unless
//   no-offset a 7/15-bit back offset - 1 (bit 7 = second byte), and for
//   no-offset commands SIZE literal bytes.
//
// .ttl texture library (symbol "TTL"): {u32 count, u32 ptr to entries, u32
//   ptr pack name}, entries of 13 words: format (0x300 + I4, IA4, I8, IA8,
//   RGB565, RGB5A3, RGBA8, CMPR, Z8, Z16, Z24X8, CI4_RGB565, CI4_RGB5A3,
//   CI8_RGB565, CI8_RGB5A3, CI8_IA8 from 1), name ptr, width, height, 0,
//   data ptr, base level size, palette ptr, palette ptr, palette entries,
//   TLUT format (GX), extra mip levels, total data size (all levels).
//-----------------------------------------------------------------------------
#ifndef SZS_LIB_TOSHI_H
#define SZS_LIB_TOSHI_H 1

#include "lib-std.h"

typedef struct
{
	u8 *sect; // decompressed section, owned
	size_t sect_size;
	const u8 *symb; // points into the file
	size_t symb_size;
} toshi_trb_t;

bool IsToshiTsfb (const u8 *data, size_t size);
enumError OpenToshiTrb (toshi_trb_t *trb, const u8 *data, size_t size);
void ResetToshiTrb (toshi_trb_t *trb);
// Section offset of a named symbol, or -1.
s64 ToshiSymbol (const toshi_trb_t *trb, ccp name);

uint ToshiTtlCount (const toshi_trb_t *trb);
// Name (points into the section) and decoded first level of texture IDX.
enumError DecodeToshiTexture (const toshi_trb_t *trb, uint idx, ccp *name, u8 **rgba, uint *width, uint *height);

#endif
