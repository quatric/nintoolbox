// SPDX-License-Identifier: GPL-2.0+
//-----------------------------------------------------------------------------
// Vblank Entertainment Wii ports (Retro City Rampage DX, Shakedown: Hawaii).
// Layouts recovered from the games' main.dol (filepackage.cpp / bap_load.cpp).
//
// gamedata_wii.bfp ("BFP2", little-endian):
//   0x00 "BFP2", u32 n (<= 192), ... header padded to 0x40
//   0x40 n * {u32 name_hash, u32 offset, u32 size, u32 stored_size}
//        then 256 * {u32 offset, u32 size, u32 stored_size} numbered slots
//        (offset 0 = empty); data starts at the u32 at 0x14 (0x40-aligned).
//   A member is stored raw when size == stored_size, else a zlib stream.
//   name_hash: h = 0; per byte c (upper-cased): h = (h << 1) ^ T[(h & 255) ^ c].
// audio_*_Wii.bap:
//   "BPP3" (Shakedown, Ogg Vorbis / DSP-ADPCM streams, big-endian DSP headers):
//     u32 data_base @4, u16 count @0x10, u32 table @0x14 (0x40);
//     table: count * {u32 header_off, u32 8, u32 meta_off}; header: u8 n_streams,
//     u32 stream_off[n] @8; stream: {u32 kind (0x100 Ogg, 0x20 DSP), u32 size,
//     u32, u32, u32 size, u32 data_off (rel. data_base)}.
//     meta: 4 bytes then two length-prefixed strings (artist, title).
//   "BAP1" (Retro City Rampage): u16 count @6, count * {u32 off, u32 size,
//     u32 pool_off, u32} @0x20, u32 pool @0x18; each chunk at off is
//     {u32 size, u32 stored, zlib} of a "BTRK" tracker module.
//-----------------------------------------------------------------------------
#ifndef SZS_LIB_VBLANK_H
#define SZS_LIB_VBLANK_H 1

#include "lib-nintendo.h"

bool IsVblankBfp (const u8 *data, size_t size);
enumError ScanVblankBfp (
	nintendo_sarc_entry_t **entries, uint *n_entries, const u8 *data, size_t size);

bool IsVblankBap (const u8 *data, size_t size);
enumError ScanVblankBap (
	nintendo_sarc_entry_t **entries, uint *n_entries, const u8 *data, size_t size);

#endif // SZS_LIB_VBLANK_H
