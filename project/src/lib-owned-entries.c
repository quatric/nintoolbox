// Shared owned-archive-entry list helpers -- split out of lib-nintendo.c.

#include "lib-std.h"
#include "lib-nintendo.h"

void ResetOwnedEntries (nintendo_sarc_entry_t *entries, uint n_entries)
{
	if (!entries)
		return;
	for (uint i = 0; i < n_entries; i++)
	{
		FREE ((void *)entries[i].name);
		FREE ((void *)entries[i].data);
	}
	FREE (entries);
}

// Append one entry with owned copies of both name and payload.
bool OwnedEntryAdd (nintendo_sarc_entry_t *entries, uint idx, ccp name, const u8 *data, uint size)
{
	char *nm = MALLOC (strlen (name) + 1);
	u8 *pl = size ? MALLOC (size) : CALLOC (1, 1);
	if (!nm || !pl)
	{
		FREE (nm);
		FREE (pl);
		return false;
	}
	strcpy (nm, name);
	if (size)
		memcpy (pl, data, size);
	entries[idx].name = nm;
	entries[idx].data = pl;
	entries[idx].size = size;
	return true;
}

// Reject names that would escape the extraction directory or contain
// characters no real member name uses.
bool OwnedNameOk (ccp name)
{
	if (!name || !*name || *name == '/')
		return false;
	if (strstr (name, "..") || strchr (name, '\\') || strchr (name, ':'))
		return false;
	for (ccp p = name; *p; p++)
		if ((u8)*p < 0x20 || (u8)*p == 0x7f)
			return false;
	return strlen (name) < 240;
}
