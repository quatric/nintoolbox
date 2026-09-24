// SPDX-License-Identifier: GPL-2.0+
// Sakura Wars: So Long, My Love (Wii, Red Entertainment/Sega) in-house "G3"
// resource-chunk family: .g3n/.g3r model files and the .gdn/.gdr/.gde
// "garden" scene files that wrap them.
//
// No public documentation of this format exists anywhere (XeNTaX/zenhax
// archives, GBAtemp, Models-Resource and romhacking.net were all checked and
// are empty on this title/engine). Everything below was reverse-engineered
// from scratch against the retail USA disc's own SLG/*.g3n, *.g3r, *.gdn,
// *.gdr and *.gde files, cross-checked across dozens of samples.
//
// What is understood and implemented here: a generic, recursive chunk tree.
// Every container chunk uses a fixed 16-byte little-endian header:
//   char tag[4];        // ASCII, e.g. "GRO3", "G3TX", "HTEX", "HMDL", ...
//   u32  size;           // size of this chunk's body, NOT counting this
//                         // 16-byte header (next sibling is at
//                         // this_chunk_start + 16 + size)
//   u32  child_offset;    // offset of the first child chunk, relative to
//                         // this chunk's own start; 0 if this chunk has no
//                         // (recognized) children
//   u32  flags;           // top bits often 0x80000000/0xc0000000 on chunks
//                         // that do have children; not otherwise decoded
//
// Two root magics use this exact scheme: "GRO3" (.g3n/.g3r model files, and
// nested inside "GDEN" files) and "GDEN" (.gdn/.gdr/.gde garden/scene
// files). A file ends with a zero-size "EOFC" marker chunk at the top level.
//
// Only a fixed whitelist of tags are known (from repeated cross-file
// observation) to actually be containers following this header shape; any
// other tag is reported as an opaque leaf (tag + start offset only) and is
// never recursed into or trusted to yield a further size/offset, since
// several such leaf tags (e.g. bone-index / animation data chunks) were
// observed to hold arbitrary binary data immediately after their 4-byte
// tag with NO generic header -- interpreting their bytes as size/offset/
// flags produces obvious garbage. This module does not guess at those.
//
// One specific leaf payload is decoded beyond its tag: "PVRT", which is the
// well-known Imagination Technologies/Sega "PVR" compressed-texture header
// (pixel format byte, data-flags byte, width/height u16s at fixed offsets
// after the tag) -- a well-documented third-party format, not something
// reverse-engineered here.
#ifndef LIB_G3RES_H
#define LIB_G3RES_H 1

#include "lib-std.h"

//-----------------------------------------------------------------------------
// Magic-based detection: exactly "GRO3" or "GDEN" at offset 0.
int IsG3Res (const u8 *data, size_t size);

// Recursively lists the chunk tree (tag, offset, size, and decoded PVRT
// sub-header where present). Does not decode mesh/animation/texel payloads.
enumError DecodeG3Res_Text (FILE *f, const u8 *data, size_t size);

#endif // LIB_G3RES_H
