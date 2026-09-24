// SPDX-License-Identifier: GPL-2.0+
//-----------------------------------------------------------------------------
// Pony Friends 2 (Wii, Ubisoft) ".rbh" asset container. See lib-pf2-piff.c
// for exactly what is and is not understood.
//-----------------------------------------------------------------------------
#ifndef LIB_PF2_PIFF_H
#define LIB_PF2_PIFF_H 1

#include "lib-std.h"

// Top-level shell only: magic "FFIP" at offset 0, followed by a big-endian
// u32 "body size" at +4 that is always exactly (file_size - 8) -- i.e. a
// classic RIFF-style [tag][BE size-of-rest] header, confirmed byte-exact
// against every .rbh sample checked (both plain header .rbh files and the
// paired *VRAM.rbh texel-data files). The original hypothesis that the
// whole file/tag set is a byte-order-flipped standard RIFF/WAVE container
// is NOT confirmed: "FFIP" does not decode into a meaningful RIFF form-type
// once reversed ("PIFF"), and no "RIFF"/"WAVE"/"fmt "/"data" tags appear
// anywhere. What IS confirmed is that this engine's convention is to store
// FourCC tags as their MIRROR-IMAGE character string versus their intended
// reading: "FFIP" = "PIFF" reversed, and inside *VRAM.rbh files the same
// convention gives "YDOB" = "BODY" reversed and "KCAP" = "PACK" reversed
// (texture pixel data is apparently wrapped in a reversed-spelled "PACK"
// sub-block, presumably compressed/tiled GX texel data -- payload bytes
// after KCAP were NOT decoded, no known pixel format was matched).
//
// Beyond the outer 8-byte FFIP shell, the internal layout is NOT reliably
// decoded: two more reversed tags ("FHBR"="RBHF" reversed, "HHBR"="RBHH"
// reversed) appear back-to-back with no size field between them, and are
// followed by a long run of 12-byte (value, type_code, zero) reflection-
// style records whose type_code values (e.g. 0x02000004, 0x07000010,
// 0x07000020) look like a generic serialized-object encoding, but no rule
// relating them to file size / nested chunk boundaries could be confirmed
// across samples, so that region is only reported as an opaque hexdump-
// style scan of the known tag strings, never structurally parsed.
bool IsPF2Piff (const u8 *data, size_t size);

// Prints the magic, confirmed body size, and the offset of every
// occurrence of the other confirmed reversed-tag strings ("FHBR","HHBR",
// "YDOB","KCAP") found in the file, each with its following 4 bytes shown
// both as hex and as a big-endian u32 -- documentation only, not a real
// chunk walk (see above).
enumError DecodePF2Piff_Text (FILE *f, const u8 *data, size_t size);

#endif // LIB_PF2_PIFF_H
