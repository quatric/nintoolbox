// SPDX-License-Identifier: GPL-2.0+
//-----------------------------------------------------------------------------
// Terminal Reality POD3/4/5 archive scanner; see lib-termpod.h for the layout.
//-----------------------------------------------------------------------------
#include "lib-std.h"
#include "lib-termpod.h"
#include <string.h>

#define POD_MAX_ENTRIES 0x100000

static u32 pod_rd32 (const u8 *p) { return p[0] | p[1] << 8 | p[2] << 16 | (u32)p[3] << 24; }

enumError ScanTermPod (nintendo_sarc_entry_t **entries, uint *n_entries, const u8 *data, size_t size)
{
	if (!entries || !n_entries || !data || size < 0x120 || memcmp (data, "POD", 3))
		return EINVAL;
	const int ver = data[3] - '0';
	if (ver < 3 || ver > 5)
		return EINVAL;
	*entries = 0;
	*n_entries = 0;

	const u32 count = pod_rd32 (data + 0x58);
	const u64 eoff = pod_rd32 (data + 0x108);
	const u32 names_size = pod_rd32 (data + 0x110);
	const uint esize = ver == 3 ? 20 : 28;
	if (!count || count > POD_MAX_ENTRIES || eoff + (u64)esize * count + names_size > size)
		return EINVAL;
	const u8 *ent = data + eoff;
	const u8 *names = ent + (size_t)esize * count;

	nintendo_sarc_entry_t *out = CALLOC (count, sizeof (*out));
	if (!out)
		return ERR_CANT_CREATE;
	uint n = 0;
	for (uint i = 0; i < count; i++)
	{
		const u8 *e = ent + (size_t)esize * i;
		const u32 noff = pod_rd32 (e), fsize = pod_rd32 (e + 4), off = pod_rd32 (e + 8);
		if (ver > 3 && (pod_rd32 (e + 16) || pod_rd32 (e + 12) != fsize))
			continue; // compressed member
		if (noff >= names_size || (u64)off + fsize > size)
		{
			ResetOwnedEntries (out, n);
			return EINVAL;
		}
		char name[512];
		const size_t l = strnlen ((ccp)names + noff, names_size - noff);
		if (l >= sizeof (name))
			snprintf (name, sizeof (name), "%05u.bin", i);
		else
		{
			for (size_t k = 0; k <= l; k++)
				name[k] = names[noff + k] == '\\' ? '/' : names[noff + k];
			if (!OwnedNameOk (name))
				snprintf (name, sizeof (name), "%05u.bin", i);
		}
		if (!OwnedEntryAdd (out, n, name, data + off, fsize))
		{
			ResetOwnedEntries (out, n);
			return ERR_CANT_CREATE;
		}
		n++;
	}
	if (!n)
	{
		FREE (out);
		return EINVAL;
	}
	*entries = out;
	*n_entries = n;
	return ERR_OK;
}
