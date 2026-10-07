// SPDX-License-Identifier: GPL-2.0+
#include "lib-streamentry.h"
#include <string.h>

void FreeStreamEntries (stream_entry_t *entries, uint n_entries)
{
	if (!entries)
		return;
	for (uint i = 0; i < n_entries; i++)
		FREE (entries[i].name);
	FREE (entries);
}

bool StreamEntryAdd (stream_entry_t *entries, uint idx, ccp name, u64 offset, u32 size)
{
	char *nm = MALLOC (strlen (name) + 1);
	if (!nm)
		return false;
	strcpy (nm, name);
	entries[idx].name = nm;
	entries[idx].offset = offset;
	entries[idx].size = size;
	return true;
}
