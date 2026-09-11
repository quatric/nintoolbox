// SPDX-License-Identifier: GPL-2.0+
//-----------------------------------------------------------------------------
// CRIWARE CPK archives + CRILAYLA decompression.
//
// Container layout per esperknight/CriPakTools' CPK.cs (MIT, re-implemented)
// and verified against four retail Star Fox Zero (Wii U) archives (717
// members total): `CPK ` packet holding a (possibly XOR-encrypted) UTF
// table with TocOffset/ContentOffset/Files/Align, a `TOC ` packet with one
// row per member (DirName/FileName/FileSize/ExtractSize/FileOffset, offsets
// rebased by min(ContentOffset, TocOffset)), and CRILAYLA-compressed
// payloads when ExtractSize != FileSize. ITOC-only archives are declined
// (none on this corpus); ETOC LocalDir mapping is not needed (TOC carries
// real path names).
//
// The scanners hand back malloc-owned entry lists like the other
// QuickBMS-ported formats, sharing write_owned_entries().
//-----------------------------------------------------------------------------
#ifndef SZS_LIB_CPK_H
#define SZS_LIB_CPK_H 1

#include "lib-nintendo.h"

enumError ScanCPK (nintendo_sarc_entry_t **entries, uint *n_entries, const u8 *data, uint size);

// CRILAYLA bitstream (backwards decoder). The 16-byte header carries the
// post-0x100-header output size; *DEST_SIZE ends as size+0x100 on success.
enumError DecodeCRILAYLA (u8 **dest, uint *dest_size, const u8 *src, uint src_size);

#endif
