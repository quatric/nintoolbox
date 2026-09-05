#include "lib-std.h"
#include "lib-pac.h"
#include <string.h>
#include <ctype.h>
#include <errno.h>

void ResetPAC (pac_t *pac)
{
	if (!pac)
		return;
	FREE (pac->entries);
	memset (pac, 0, sizeof (*pac));
}

enumError ScanPAC (pac_t *pac, const u8 *data, uint size)
{
	if (!pac || !data || size < 0x40 || memcmp (data, "ARC\0", 4))
		return EINVAL;
	if (data[4] != 1 || data[5] != 1)
		return EINVAL;

	const uint n = rd_be16 (data + 6);
	if (!n || n > 0x10000)
		return EINVAL;

	memset (pac, 0, sizeof (*pac));
	pac->data = data;
	pac->size = size;
	memcpy (pac->name, data + 0x10, sizeof (pac->name) - 1);
	pac->name[sizeof (pac->name) - 1] = 0;

	pac_entry_t *entries = CALLOC (n, sizeof (*entries));
	if (!entries)
		return ERR_CANT_CREATE;

	uint off = 0x40, i;
	for (i = 0; i < n; i++)
	{
		if (off + 0x20 > size)
			break;
		const u8 *h = data + off;
		const u16 type = rd_be16 (h);
		const u16 index = rd_be16 (h + 2);
		const u32 fsize = rd_be32 (h + 4);
		const u8 group = h[8];
		const s16 redirect = (s16)rd_be16 (h + 10);

		const u32 data_off = off + 0x20;
		if ((u64)data_off + fsize > size)
			break;

		entries[i].type = type;
		entries[i].index = index;
		entries[i].group_index = group;
		entries[i].redirect_index = redirect;

		const u8 *np = h + 0x10;
		uint nl = 0;
		while (nl < sizeof (entries[i].name) && np[nl] >= 0x20 && np[nl] <= 0x7E && np[nl] != '/')
			nl++;
		if (nl && nl < sizeof (entries[i].name) && !np[nl])
		{
			memcpy (entries[i].name, np, nl);
			entries[i].name[nl] = 0;
		}
		entries[i].size = fsize;
		entries[i].data = data + data_off;

		const u64 next = ((u64)data_off + fsize + 0x1f) & ~(u64)0x1f;
		if (next <= off || next > size)
		{
			i++;
			break;
		}
		off = (uint)next;
	}

	if (!i)
	{
		FREE (entries);
		return EINVAL;
	}
	pac->entries = entries;
	pac->n_entries = i;
	return ERR_OK;
}

enumError CreatePAC (
	u8 **dest, uint *dest_size, const nintendo_sarc_entry_t *entries, uint n_entries)
{
	if (!dest || !dest_size || !entries || !n_entries || n_entries > 0xFFFF)
		return EINVAL;

	uint cur_size = 0x40;
	for (uint i = 0; i < n_entries; i++)
	{
		cur_size += 0x20;
		cur_size += entries[i].size;
		cur_size = (cur_size + 0x1F) & ~0x1Fu;
	}

	u8 *out = CALLOC (1, cur_size);
	if (!out)
		return ERR_CANT_CREATE;

	memcpy (out, "ARC\0", 4);
	out[4] = 1;
	out[5] = 1;
	wr_be16 (out + 6, (u16)n_entries);

	uint off = 0x40;
	for (uint i = 0; i < n_entries; i++)
	{
		ccp full_name = entries[i].name ? entries[i].name : "misc";
		ccp slash = strrchr (full_name, '/');
		ccp fname = slash ? slash + 1 : full_name;

		u16 type = 1;
		if (entries[i].data && entries[i].size >= 4)
		{
			if (!memcmp (entries[i].data, "bres", 4) || !memcmp (entries[i].data, "MDL0", 4))
				type = 2;
			else if (!memcmp (entries[i].data, "TEX0", 4))
				type = 3;
			else if (!memcmp (entries[i].data, "ANIM", 4) || !memcmp (entries[i].data, "CHR0", 4)
				|| !memcmp (entries[i].data, "CLR0", 4) || !memcmp (entries[i].data, "PAT0", 4)
				|| !memcmp (entries[i].data, "SHP0", 4) || !memcmp (entries[i].data, "VIS0", 4)
				|| !memcmp (entries[i].data, "SCN0", 4))
				type = 5;
		}

		u8 *h = out + off;
		wr_be16 (h + 0, type);
		wr_be16 (h + 2, (u16)i);
		wr_be32 (h + 4, entries[i].size);
		h[8] = 0;
		h[9] = 0;
		wr_be16 (h + 10, 0xFFFF);

		size_t nlen = strlen (fname);
		if (nlen > 15)
			nlen = 15;
		memcpy (h + 0x10, fname, nlen);
		h[0x10 + nlen] = 0;

		if (entries[i].size && entries[i].data)
			memcpy (out + off + 0x20, entries[i].data, entries[i].size);

		off += 0x20 + entries[i].size;
		off = (off + 0x1F) & ~0x1Fu;
	}

	*dest = out;
	*dest_size = cur_size;
	return ERR_OK;
}
