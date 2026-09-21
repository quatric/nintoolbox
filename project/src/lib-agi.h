// SPDX-License-Identifier: GPL-2.0+
//-----------------------------------------------------------------------------
// Toys for Bob "AGI" archives (Skylanders: Swap Force and likely other
// Toys for Bob titles; every *.pak under files/ starts with this magic).
// All fields big-endian.
//
//   0x00 u32 magic            0x1A414749 ("\x1AAGI")
//   0x04 u32 version           (9 in all samples)
//   0x08 u32 entry_table_size  (byte length of the entry table at 0x40)
//   0x0c u32 name_count        (number of packed members / named entries)
//   0x10 u32 block_align       (0x800 in all samples; data offsets tend to
//                               be aligned to this)
//   0x14 u32 table_hash        (unclear purpose, ignored)
//   0x18 u32 unk1
//   0x1c..0x28 reserved / zero in samples
//   0x2c u32 name_table_offset (absolute file offset of the name table)
//   0x30 u32 name_table_size   (byte length of the name table)
//   0x34 u32 unk2              (3 in all samples)
//   0x38 u32 hash1
//   0x3c u32 hash2
//   -- header is 0x40 (64) bytes total --
//
//   name_table_offset + name_table_size == file size  (verified invariant,
//   used here as the format sanity check).
//
// Entry table (bytes [0x40 .. 0x40+entry_table_size)): per-entry records are
// NOT a fixed stride (audio entries appear to carry extra metadata words
// incl. an 0xffffffff sentinel; other entry kinds don't). This scanner uses
// a heuristic instead of a fixed record layout: it reads the region as
// big-endian u32 words and looks for adjacent pairs (offset, size) with
// 0x40 <= offset < name_table_offset and offset+size <= file size. Every
// such pair found, in table order, is treated as one packed member's raw
// (offset, size), and on well-formed pure-audio-bank .pak files this yields
// exactly name_count pairs in the same order as the name table.
//
// LIMITATION (verified empirically on ~2300 retail .pak files from the
// Swap Force DATA partition): about a third of the files -- mostly ones
// that pack .igz/.igx model or material entries rather than pure audio
// banks -- have entry records that are NOT simple adjacent (offset,size)
// word pairs (some records appear to be 4-word groups with a leading type
// flag, and at least one observed file has more named entries than the
// heuristic finds data pairs for, e.g. glitteringtiara_hatdata.pak:
// name_count=8 but only 7 adjacent pairs are discoverable this way).
// Rather than guess, ScanAGI() requires exact agreement between the number
// of pairs found and name_count and fails cleanly (EINVAL) otherwise, so
// extraction never emits wrong or truncated data for a file it does not
// actually understand -- it simply skips it.
//
// Name table (bytes [name_table_offset .. name_table_offset+name_table_size)):
//   [0 .. name_count*4): name_count big-endian u32 byte offsets, each
//     relative to the start of the name table, pointing at that entry's
//     name string.
//   At each such offset: a NUL-terminated backslash-path string (Windows
//     dev path, e.g. "U:\temporary\wii\sounds\...\EVENT_Cannon_Launch"),
//     immediately followed by 4 more bytes (a hash/CRC, purpose unknown,
//     skipped here) before the next string's bytes begin.
//
// Sub-formats are extracted raw, not decoded: audio bank members start with
// an FSB5 (FMOD Sound Bank 5) header -- this repo's existing FSB4/FSB5
// decoder (lib-fsb.c) can decode those once extracted. Model/material
// members use Toys for Bob's proprietary "IGZ" object-graph format
// (unrelated to any other "IGZ"-like format in this repo) and are simply
// extracted as raw .igz/.igx blobs; IGZ internals are out of scope here.
//-----------------------------------------------------------------------------
#ifndef SZS_LIB_AGI_H
#define SZS_LIB_AGI_H 1

#include "lib-nintendo.h"

enumError ScanAGI (nintendo_sarc_entry_t **entries, uint *n_entries, const u8 *data, size_t size);

#endif
