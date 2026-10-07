// SPDX-License-Identifier: GPL-2.0+
//-----------------------------------------------------------------------------
// Member list for archives that are streamed from disk instead of being
// loaded into memory (used by the multi-GB Wii game archives).
//-----------------------------------------------------------------------------
#ifndef SZS_LIB_STREAMENTRY_H
#define SZS_LIB_STREAMENTRY_H 1

#include "lib-std.h"

typedef struct stream_entry_t
{
	char *name; // relative path, '/' separated, never escapes the root
	u64 offset; // absolute offset of the payload
	u32 size;   // payload size
} stream_entry_t;

// Free the list and its names; safe on NULL.
void FreeStreamEntries (stream_entry_t *entries, uint n_entries);

// Append a member. The name is copied. Returns false on out-of-memory.
bool StreamEntryAdd (stream_entry_t *entries, uint idx, ccp name, u64 offset, u32 size);

#endif
