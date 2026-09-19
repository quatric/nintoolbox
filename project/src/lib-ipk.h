// SPDX-License-Identifier: GPL-2.0+
//-----------------------------------------------------------------------------
// Ubisoft UbiArt IPK archives (Just Dance, Rayman Origins/Legends,
// Child of Light; Wii / Wii U / Switch / PC).
//
// Container layout from Luigi Auriemma's just_dance_2014.bms (QuickBMS,
// script 0.1.1) and the PartyService/ubiart-archive-tools packer/unpacker
// (MIT, re-implemented -- no code copied):
//   Header (0x30 bytes), all fields big-endian:
//     0x00 u32 magic 0x50EC12BA
//     0x04 u32 version (3, 5, 7 observed)
//     0x08 u32 platform/unknown
//     0x0C u32 base_offset (data area = header + directory size)
//     0x10 u32 num_files
//     0x14 u32[7] tail (compressed/binaryscene/binarylogic/datasignature/
//            enginesignature/engineversion/num_files2 on newer titles,
//            opaque on v3 -- never validated)
//   Per entry, big-endian:
//     u32 flag1 (1 normally; 2 means 8 extra unknown bytes follow)
//     u32 size (uncompressed)
//     u32 zsize (stored bytes; 0 = stored uncompressed)
//     u64 timestamp (ignored)
//     u64 offset (relative to base_offset)
//     [u32 extra1, u32 extra2] iff flag1 == 2
//     u32 s1len + s1[s1len]
//     u32 s2len + s2[s2len]
//     u32 crc (Jenkins-style path hash -- never validated)
//     u32 flag2 (0 normally, 2 for .ckd members)
//
// The two strings are path and name, but their ORDER depends on the game:
// older titles (Just Dance 2014, Rayman Origins) store name first, newer
// ones (Just Dance 2015-2022, Switch titles) store path first (the packer's
// `switchTitle` swap). There is no version flag for the order, so members
// are assigned with the same heuristic just_dance_2014.bms uses: the
// dotted string is the file name, the slash-bearing string is the path.
// Compressed members are zlib (older) or LZMA-alone (later titles); both
// are decompressed, anything else is emitted as stored bytes so no member
// is ever silently dropped.
//
// The scanner hands back a malloc-owned entry list like the other
// QuickBMS-ported formats, sharing write_owned_entries().
//-----------------------------------------------------------------------------
#ifndef SZS_LIB_IPK_H
#define SZS_LIB_IPK_H 1

#include "lib-nintendo.h"

enumError ScanIPK (nintendo_sarc_entry_t **entries, uint *n_entries, const u8 *data, uint size);

#endif
