// SPDX-License-Identifier: GPL-2.0+
//-----------------------------------------------------------------------------
// Town Factory "PCKG" packages (Little King's Story, Wii). Layout worked out
// from the 2008 .pac/.pcha/.pac0-9/.bin/.dat packages on the retail disc of
// the US release; every one satisfies it. All fields big-endian.
//   0x00 char[4] magic  "PCKG"
//   0x04..0x1f          zero
//   0x20 entry chain: each entry is
//        +0x00 u32 next    distance to the next entry, 0 for the last
//        +0x04 u32 size    payload size in bytes
//        +0x08 u32 data    payload offset from the entry start (0x20)
//        +0x0c char[20]    NUL-padded file name
//        +data payload, plain; the entry is padded so that next is 0x20 aligned
// Members are ordinary files: .brres, .col, .brstm and nested PCKG packages
// (.pac, .pcha), which extract again in turn.
//
// Extract-only.
//-----------------------------------------------------------------------------
#ifndef SZS_LIB_PCKG_H
#define SZS_LIB_PCKG_H 1

#include "lib-nintendo.h"

bool IsPckg (const u8 *data, size_t size);
enumError ScanPckg (nintendo_sarc_entry_t **entries, uint *n_entries, const u8 *data, size_t size);

#endif
