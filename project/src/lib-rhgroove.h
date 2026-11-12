// SPDX-License-Identifier: GPL-2.0+
#ifndef SZS_LIB_RHGROOVE_H
#define SZS_LIB_RHGROOVE_H 1

#include "lib-std.h"

// Rhythm Heaven Groove (Switch) romfs formats. All little endian.
//
// ".cwv" sound (sound/sm/**, 2209 files): sample data followed by a 0x100-byte footer:
//   u32 codec (2 = mono, 3 = stereo DPCM), u32 sample rate, u32 frames, u32 2, u32 loop start,
//   u32 loop length (0 = no loop; start + length is often exactly the frame count), then floats
//   (volume, 1.0, ...). One byte per sample per channel, channels interleaved; each channel keeps a
//   running s16 sum of a 256-entry delta table indexed by the byte. Codec and table follow the
//   game's loader, which expands the data to PCM16 before playback; the loop fields were read off
//   the data (they balance on all 2209 files).
//
// ".wmb" wave mark (wavmark/**, tempo map of the ".cwv" of the same name): u32 0, u32 sample rate,
//   s32 (-1 or 0), u32 a, u32 b, u32 0, then {u32 ticks, u32 end sample} segments and a u32 0.
//   480 ticks per beat (all 271 files give whole-number tempos, e.g. the BPM in the tutorial
//   names).
//
// ".tex" texture (graph/**, zstd compressed): a Tegra block-linear surface followed by a
//   0x1000-byte trailer {u16 format (101 = BC7, 99 = RGBA8), u16, u16 width, u16 height,
//   u16 mip count}. Block height = largest power of two <= the surface height in GOBs (max 16);
//   every file's size matches this. Mip level 0 is decoded.
//
// ".spr" sprite set (beside every ".tex"): u32 flags, 13 u32 counts, 27 u64 section offsets
//   (from 0x38, relative to the file); section 0 holds counts[1] 16-byte atlas rectangles
//   {s32 x, y, width, height} into the ".tex" (negative x for entries not on the atlas).

// Validate and decode a ".cwv". PCM is MALLOC'd, interleaved s16.
bool IsRHGrooveCwv (const u8 *data, size_t size);
enumError DecodeRHGrooveCwv (const u8 *data, size_t size, s16 **pcm, uint *channels, uint *rate,
	uint *frames, uint *loop_start, uint *loop_end);

bool IsRHGrooveWmb (const u8 *data, size_t size);
enumError ConvertRHGrooveWmbToYaml (const u8 *data, size_t size, char **text, size_t *text_size);

// DATA is the decompressed ".tex". RGBA is MALLOC'd (width * height * 4 bytes).
bool IsRHGrooveTex (const u8 *data, size_t size);
enumError DecodeRHGrooveTex (const u8 *data, size_t size, u8 **rgba, uint *width, uint *height);

bool IsRHGrooveSpr (const u8 *data, size_t size);
enumError ConvertRHGrooveSprToYaml (const u8 *data, size_t size, char **text, size_t *text_size);

#endif // SZS_LIB_RHGROOVE_H
