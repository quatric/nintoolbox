#ifndef LIB_GHOSTTRICK_H
#define LIB_GHOSTTRICK_H

#include "file-type.h"
#include "dclib-types.h"

// Ghost Trick: Phantom Detective (Capcom, Nintendo DS) proprietary file formats:
//
// 1. CAPCOM-MODS (FF_CAPCOM_MODS):
//    Full-screen character/cinematic animation stream (*.mods).
//    Header:
//      +0x00..+0x03: "MODS" (ASCII)
//      +0x04..+0x07: "N3\n\0" (version string)
//      +0x08..+0x0B: total frame count (little-endian u32)
//      +0x0C..+0x0F: block/chunk stride (always 256)
//      +0x10..+0x13: header size (always 192 = 0xC0)
//      +0x14..+0x17: animation identifier / CRC
//      +0x28..+0x2B: trailer offset (file_size - trailer_count * 8)
//      +0x2C..+0x2F: trailer count
//    Trailer:
//      Array of { u32 frame_index, u32 keyframe_offset } mapping keyframe
//      indices to offsets in the stream.
//
// 2. CAPCOM-GML1 (FF_CAPCOM_GML1):
//    Capcom localized Game Message / Script Binary (*.xml.bin, *.xml.lz).
//    Header:
//      +0x00..+0x03: "1LMG" (ASCII)
//      +0x04..+0x07: version/flags (u32 little-endian, 0 or 102)
//      +0x08..+0x0B: data/bytecode section byte length
//      +0x0C..+0x0F: entry/control code count
//      +0x10..+0x13: key lookup table offset
//    Followed by localized text blocks and string lookup index tables.

bool IsCapcomMods (const u8 *data, uint data_size, u64 file_size);
bool IsCapcomGML1 (const u8 *data, uint data_size, u64 file_size);

#endif // LIB_GHOSTTRICK_H
