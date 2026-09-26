// SPDX-License-Identifier: GPL-2.0+
// Top Trumps - Doctor Who "T3PK4.00" resource pack -- see lib-t3pk.h for
// exactly what is and is not understood.

#include "lib-t3pk.h"
#include "lib-nintendo.h"
#include <string.h>

#define T3PK_TABLE_OFFSET 0x40
#define T3PK_RECORD_SIZE 0x28
#define T3PK_MAX_ENTRIES 0x10000

bool IsT3PK (const u8 *data, size_t size)
{
	if (!data || size < T3PK_TABLE_OFFSET || memcmp (data, "T3PK4.00", 8))
		return false;
	const u32 declared_count = rd_be32 (data + 8);
	return declared_count > 0 && declared_count <= T3PK_MAX_ENTRIES;
}

enumError ScanT3PK (t3pk_t *pk, const u8 *data, size_t size)
{
	if (!pk || !IsT3PK (data, size))
		return ERR_INVALID_DATA;
	memset (pk, 0, sizeof (*pk));

	const u32 declared_count = rd_be32 (data + 8); // real entries + 1 terminator

	t3pk_entry_t *entries = CALLOC (declared_count, sizeof (*entries));
	if (!entries)
		return ERR_CANT_CREATE;

	size_t off = T3PK_TABLE_OFFSET;
	uint n = 0;
	while (n < declared_count && off + T3PK_RECORD_SIZE <= size)
	{
		const u8 *rec = data + off;
		const u32 data_off = rd_be32 (rec);
		const u32 data_size = rd_be32 (rec + 4);
		if (!data_off && !data_size)
			break; // terminator record
		if ((u64)data_off + data_size > size)
		{
			FREE (entries);
			return ERR_INVALID_DATA;
		}

		t3pk_entry_t *e = entries + n++;
		e->data_offset = data_off;
		e->data_size = data_size;
		e->field2 = rd_be32 (rec + 8);
		e->field3 = rd_be32 (rec + 12);
		e->field4 = rd_be32 (rec + 16);
		e->field5 = rd_be32 (rec + 20);
		e->field6 = rd_be32 (rec + 24);
		off += T3PK_RECORD_SIZE;
	}

	if (!n)
	{
		FREE (entries);
		return ERR_INVALID_DATA;
	}

	pk->raw = data;
	pk->raw_size = size;
	pk->n_entries = n;
	pk->entries = entries;
	return ERR_OK;
}

void ResetT3PK (t3pk_t *pk)
{
	if (!pk)
		return;
	FREE (pk->entries);
	memset (pk, 0, sizeof (*pk));
}

enumError DecodeT3PK_Text (FILE *f, const u8 *data, size_t size)
{
	t3pk_t pk;
	enumError err = ScanT3PK (&pk, data, size);
	if (err)
		return err;

	fprintf (f, "T3PK4.00 pack: %u entries, %zu bytes\n", pk.n_entries, pk.raw_size);
	for (uint i = 0; i < pk.n_entries; i++)
	{
		const t3pk_entry_t *e = pk.entries + i;
		fprintf (f,
			"%4u.  off=0x%08x  size=0x%08x (%u)"
			"  f2=0x%08x f3=0x%08x f4=0x%08x f5=0x%08x f6=0x%08x\n",
			i, e->data_offset, e->data_size, e->data_size, e->field2, e->field3, e->field4,
			e->field5, e->field6);
	}

	ResetT3PK (&pk);
	return ERR_OK;
}
