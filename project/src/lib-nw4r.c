// Embedded NW4R (RLYT/RLAN) fallback scan -- split out of lib-nintendo.c.

#include "lib-std.h"
#include "lib-nintendo.h"

//-----------------------------------------------------------------------------
///////////////	Embedded NW4R RLYT/RLAN fallback scan		///////////////
//-----------------------------------------------------------------------------

void ResetEmbeddedNW4R (nw4r_embedded_t *found)
{
	if (!found)
		return;
	FREE (found->entries);
	memset (found, 0, sizeof (*found));
}

void ScanEmbeddedNW4R (nw4r_embedded_t *found, const u8 *data, uint size)
{
	memset (found, 0, sizeof (*found));
	if (!data || size < 0x10)
		return;

	uint cap = 0;
	for (uint pos = 0; pos + 0x10 <= size; pos++)
	{
		const bool is_rlan = !memcmp (data + pos, "RLAN", 4);
		if (!is_rlan && memcmp (data + pos, "RLYT", 4))
			continue;

		if (rd_be16 (data + pos + 4) != 0xfeff)
			continue;
		const u32 fsize = rd_be32 (data + pos + 8);
		const u16 hsize = rd_be16 (data + pos + 12);
		if (hsize != 0x10 || fsize < 0x10 || (u64)pos + fsize > size)
			continue;

		if (found->n_entries == cap)
		{
			cap = cap ? cap * 2 : 16;
			found->entries = REALLOC (found->entries, cap * sizeof (*found->entries));
		}
		nw4r_embedded_entry_t *e = found->entries + found->n_entries++;
		e->data = data + pos;
		e->size = fsize;
		e->is_brlan = is_rlan;

		pos += fsize - 1; // skip past this resource; the outer loop's pos++ lands right after it
	}
}
