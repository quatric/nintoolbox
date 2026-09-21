// SPDX-License-Identifier: GPL-2.0+
//-----------------------------------------------------------------------------
// Nintendo RSO module scanner; see lib-rso.h for the layout and the
// documented detection heuristic (no magic; validated structurally).
//-----------------------------------------------------------------------------
#include "lib-std.h"
#include "lib-rso.h"
#include <string.h>
#include <stdio.h>
#include <ctype.h>

#define RSO_HEADER_SIZE   0x20
#define RSO_MAX_SECTIONS  256
#define RSO_MAX_NAME      1024

static u32 rso_rd32 (const u8 *p) { return (u32)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]; }

enumError ScanRSO (nintendo_sarc_entry_t **entries, uint *n_entries, const u8 *data, size_t size)
{
	if (!entries || !n_entries || !data || size < RSO_HEADER_SIZE)
		return EINVAL;
	*entries = 0;
	*n_entries = 0;

	const u32 next               = rso_rd32 (data + 0x00);
	const u32 prev                = rso_rd32 (data + 0x04);
	const u32 num_sections        = rso_rd32 (data + 0x08);
	const u32 section_info_offset = rso_rd32 (data + 0x0c);
	const u32 name_offset         = rso_rd32 (data + 0x10);
	const u32 name_size           = rso_rd32 (data + 0x14);
	const u32 version             = rso_rd32 (data + 0x18);
	const u32 bss_size            = rso_rd32 (data + 0x1c);

	// The module list pointers only ever get a real value once the loader
	// has mapped the module into memory; on disk they are always zero.
	if (next || prev)
		return EINVAL;
	if (!num_sections || num_sections > RSO_MAX_SECTIONS)
		return EINVAL;
	if ((u64)section_info_offset + (u64)num_sections * 8 > size)
		return EINVAL;
	if (version > 16)
		return EINVAL;
	if (name_size > RSO_MAX_NAME || (u64)name_offset + name_size > size)
		return EINVAL;

	// If a module name is present, require it to be a clean, fully-printable
	// run (dev build paths are always plain ASCII in every sample seen).
	for (u32 i = 0; i < name_size; i++)
	{
		const u8 c = data[name_offset + i];
		if (!isprint (c) && c != '\t')
			return EINVAL;
	}

	const u8 *tbl = data + section_info_offset;
	struct { u32 off, size; bool is_bss; } sec[RSO_MAX_SECTIONS];
	uint n_payload = 0;
	bool have_bss = false;

	for (u32 i = 0; i < num_sections; i++)
	{
		const u32 off = rso_rd32 (tbl + i * 8);
		const u32 sz  = rso_rd32 (tbl + i * 8 + 4);

		if (!sz)
			continue;
		if (!off)
		{
			// The BSS (zero-initialized) section carries no on-disk
			// payload and is conventionally recorded with a zero offset;
			// sanity-check its size against the header's bss_size but
			// don't try to extract anything for it.
			if (sz != bss_size)
				return EINVAL;
			have_bss = true;
			continue;
		}
		if ((u64)off + sz > size)
			return EINVAL;
		sec[n_payload].off    = off;
		sec[n_payload].size   = sz;
		sec[n_payload].is_bss = false;
		n_payload++;
	}

	// Require at least one real payload section, otherwise this is almost
	// certainly not an RSO module (an all-zero/empty section table matches
	// the bounds checks above trivially).
	if (!n_payload)
		return EINVAL;
	(void)have_bss;

	nintendo_sarc_entry_t *out = CALLOC (n_payload + 1, sizeof (*out));
	if (!out)
		return ERR_CANT_CREATE;

	uint n = 0;

	// One small text summary member with the header fields and module name,
	// mirroring how other magic-less archive scanners in this repo surface
	// their header metadata (e.g. RPAK's STRG dump).
	char info[RSO_MAX_NAME + 256];
	int len = snprintf (info, sizeof (info),
		"# Nintendo RSO module\n"
		"version      = %u\n"
		"num_sections = %u\n"
		"bss_size     = %u\n"
		"name         = %.*s\n",
		version, num_sections, bss_size, (int)name_size, name_size ? (ccp)(data + name_offset) : "");
	if (len < 0)
		len = 0;
	if ((uint)len >= sizeof (info))
		len = sizeof (info) - 1;
	if (!OwnedEntryAdd (out, n, "rso_info.txt", (const u8 *)info, (uint)len))
	{
		ResetOwnedEntries (out, n);
		FREE (out);
		return ERR_CANT_CREATE;
	}
	n++;

	for (uint i = 0; i < n_payload; i++)
	{
		char name[32];
		snprintf (name, sizeof (name), "section_%02u.bin", i);
		if (!OwnedEntryAdd (out, n, name, data + sec[i].off, sec[i].size))
		{
			ResetOwnedEntries (out, n);
			FREE (out);
			return ERR_CANT_CREATE;
		}
		n++;
	}

	*entries = out;
	*n_entries = n;
	return ERR_OK;
}
