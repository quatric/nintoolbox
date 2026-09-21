// SPDX-License-Identifier: GPL-2.0+
//-----------------------------------------------------------------------------
// FMOD sound banks (.fsb) as used by Wii titles, big-endian sample data:
//   FSB4: 0x30 header {"FSB4", u32 n_samples, u32 header_size, u32 data_size,
//         u32 version, u32 mode}; n sample headers, each `size` (u16 at +0)
//         bytes: name[30], u32 samples, u32 stored_bytes, u32 loop_start,
//         u32 loop_end, u32 mode, i32 frequency, u16 vol, i16 pan, u16 pri,
//         u16 channels, ...; from +0x50 one 0x2e-byte block per channel for
//         GCADPCM (16 big-endian s16 coefficients + state). Data follows the
//         headers, samples back to back.
//   FSB3: 0x18 header {"FSB3", u32 n_samples, u32 header_size, u32 data_size,
//         u32 version (0x30001), u32 mode}, then the same sample headers as FSB4.
//   FSB5: 0x3c header {"FSB5", u32 version(1), u32 n_samples, u32 header_size,
//         u32 name_size, u32 data_size, u32 codec}; per sample a u64
//         {hasChunks:1, frequency:4, twoChannels:1, offset/16:28,
//         samples:30} plus chunks {hasNext:1, size:24, type:7}: 1 channels,
//         2 frequency, 3 loop, 7 DSP coefficients; then a name offset table
//         and names, then the data.
// GCADPCM (DSP) samples with several channels interleave every 2 bytes.
// Decoded to 16-bit PCM WAV; other codecs are copied out as .bin.
//-----------------------------------------------------------------------------
#ifndef SZS_LIB_FSB_H
#define SZS_LIB_FSB_H 1

#include "lib-nintendo.h"

enumError ScanFSB (nintendo_sarc_entry_t **entries, uint *n_entries, const u8 *data, size_t size);

enumError ScanKRAW (nintendo_sarc_entry_t **entries, uint *n_entries, const u8 *data, size_t size);

// Extraction entry point (dest.inc): decodes any file whose content is an
// FSB3/FSB4/FSB5 bank or a kRAW stream, whatever its extension -- both are
// used on real Wii discs as unlabelled/oddly-extensioned blobs (e.g.
// Collision Studios' "Brave: A Warrior's Tale", which stores FSB4 banks as
// .csa/.psk/.mib). Writes one WAV (or .bin for an undecodable codec) per
// sample.
enumError ExtractFSBArchive (ccp arg, ccp basedir, uint depth);

#endif
