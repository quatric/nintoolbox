// SPDX-License-Identifier: GPL-2.0+
//-----------------------------------------------------------------------------
// Blue Tongue "TRB\0" packages (de Blob 2, Wii; earlier and later Toshi games
// use the TSFB/TRBF layout of lib-toshi). Big-endian, laid out by hand from
// the Wii disc of de Blob 2:
//   0x00 "TRB\0", u32 0x7d1, u32 2, u32 section count, u32 section bytes
//        (0x30 * count), u32 symbol count, ... u32 0x1c data bytes.
//   0x80 sections, 0x30 bytes each: u32 0, u32 name offset, u32 0, u32 flags,
//        u32 size, u32 size, u32 file offset (0x800 aligned), 5 x u32 0.
//        Section 0 (".text") is the string table the names index into; the
//        other sections are ".data", "__s00000" (the pixel pool), "gpu_data"
//        (GX vertex data), "model_collision", "XUR" (an embedded Xbox UI
//        scene file) and engine tables.
//   after the sections: symbols, 16 bytes each {u32 tag, u32 offset in the
//        section, u32 section index << 16, u32 name offset}.
// A "ttex" symbol is a texture object: {0, size, hash} then 0x0df00bb0 fill
// words, and from the first other word F: +0 GX format (0 I4, 1 I8, 2 IA4, 3
// IA8, 5 RGB5A3, 6 RGBA8, 8 C4, 9 C8, 14 CMPR), +0xc pixel offset in the
// "__s00000" section, +0x14 palette offset, +0x18 palette format, +0x1c size
// of the stored image with its mip levels, and two width / height pairs
// (u16 x4, at F + 0x40, or F + 0x44 in the UI files; lightmaps are not
// powers of two): the first pair is the stored size. The texture's name (a .tga path) follows, 0x54 or 0x58 after F.
//-----------------------------------------------------------------------------
#ifndef SZS_LIB_BTTRB_H
#define SZS_LIB_BTTRB_H 1

#include "lib-std.h"

typedef struct bttrb_tex_t
{
	char name[96];		// unique, file-system safe, no extension
	uint width, height, format;
	u32 off, size;		// absolute file offsets of the pixels
	u32 pal_off, pal_format;
} bttrb_tex_t;

bool IsBlueTongueTrb (const u8 *d, size_t size);

// Malloc-owned texture list, or 0.
bttrb_tex_t *ListBlueTongueTextures (const u8 *d, size_t size, uint *count);
enumError DecodeBlueTongueTexture (u8 **rgba, const u8 *d, size_t size, const bttrb_tex_t *t);

// The bytes of a named section (points into D), or 0.
const u8 *FindBlueTongueSection (const u8 *d, size_t size, ccp name, u32 *len);

#endif
