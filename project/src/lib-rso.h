// SPDX-License-Identifier: GPL-2.0+
//-----------------------------------------------------------------------------
// Nintendo RSO ("Relocatable Static Object") modules -- PowerPC dynamically
// loadable code/data modules used by several GameCube/Wii SDK-based titles
// as an alternative to REL/DOL. Seen here in *Skylanders: SuperChargers
// Racing* (Wii) as `DATA/files/Data/gamelogic.rso`. All fields big-endian.
//
// The format has no magic signature; every offset is relative and the
// module-linked-list pointers are always zero on disk (they are patched in
// place by the loader once the module is mapped), so detection here relies
// on that on-disk invariant plus bounds-checking every offset/size field
// against the file size -- the same "no magic, verify structurally" approach
// already used elsewhere in this repo (RPAK, Mii resource archives, KCL).
//
//   0x00 u32 next               (module list pointer; always 0 on disk)
//   0x04 u32 prev               (module list pointer; always 0 on disk)
//   0x08 u32 num_sections
//   0x0c u32 section_info_offset  (absolute file offset of the section table)
//   0x10 u32 name_offset          (absolute file offset of the module name)
//   0x14 u32 name_size             (byte length, excluding the NUL)
//   0x18 u32 version
//   0x1c u32 bss_size
//   (further prolog/epilog/unresolved/import/export fields exist in the
//   full RSO header but are not needed to recover the section payloads and
//   are not parsed here)
//
// Section table (num_sections entries of 8 bytes each, at section_info_offset):
//   u32 offset  -- absolute file offset of the section data, OR'd with 1 in
//                  its low bit when the section is BSS (zero-initialized,
//                  no on-disk payload; the low bit must be masked off before
//                  using the value as a real offset for any *other* field,
//                  but BSS sections are skipped entirely here since they
//                  carry no bytes to extract)
//   u32 size    -- byte length of the section (bss_size for the BSS entry)
// Verified against the retail `gamelogic.rso` (4,745,824 bytes): 28 section
// entries, 5 with real on-disk payloads (sizes 3,094,492 / 3,232 / 12 /
// 83,716 / 83,352 bytes, all within file bounds), one BSS-flagged entry
// whose size matches the header's `bss_size` (367,020) exactly, and the
// module name string ("Y:/sky2015-3ds-wii/gameassets/Builds/WII/Data/
// gamelogic.plf") landing on a clean, fully-printable, NUL-terminated run
// at `name_offset`. Extract-only: sections are dumped as raw numbered
// blobs (their internal code/relocation layout is out of scope, matching
// how this repo already treats other opaque payload formats such as AGI's
// .igz members or RPAK's raw entries) plus one small text summary member.
//-----------------------------------------------------------------------------
#ifndef SZS_LIB_RSO_H
#define SZS_LIB_RSO_H 1

#include "lib-nintendo.h"

enumError ScanRSO (nintendo_sarc_entry_t **entries, uint *n_entries, const u8 *data, size_t size);

#endif
