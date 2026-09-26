#include "lib-adjb.h"
#include "lib-std.h"

#define ADJB_MAX_MESHES 0x10000

bool IsADJB (const u8 *data, size_t size)
{
	if (!data || size < 4)
		return false;
	const int32_t count = (int32_t)rd_le32 (data);
	if (count < 0 || count > ADJB_MAX_MESHES)
		return false;
	const u64 tab_end = 4 + (u64)count * 8;
	if (tab_end > size)
		return false;
	// Every offset must land inside the data area that follows the table,
	// and the trailing buffer of the last mesh must hold whole u16s.
	for (int32_t i = 0; i < count; i++)
	{
		const int32_t off = (int32_t)rd_le32 (data + 4 + (u64)i * 8 + 4);
		if (off < 0 || tab_end + (u64)off > size)
			return false;
	}
	if (count > 0)
	{
		const int32_t last = (int32_t)rd_le32 (data + 4 + (u64)(count - 1) * 8 + 4);
		if ((size - tab_end - (u64)last) & 1)
			return false;
	}
	return true;
}

enumError DecodeADJB_Text (FILE *out, const u8 *data, size_t size)
{
	if (!out || !IsADJB (data, size))
		return ERR_INVALID_DATA;

	const int32_t count = (int32_t)rd_le32 (data);
	const u64 tab_end = 4 + (u64)count * 8;

	fprintf (out, "#ADJB\nmesh_count = %d\n\n[meshes]\n", count);

	for (int32_t i = 0; i < count; i++)
	{
		const int32_t id = (int32_t)rd_le32 (data + 4 + (u64)i * 8);
		const int32_t off = (int32_t)rd_le32 (data + 4 + (u64)i * 8 + 4);
		// The buffer runs to the next higher offset, or to end of file
		// when no higher offset exists -- the same sizing Adjb.cs gets
		// from consecutive offsets, but robust to unsorted tables.
		u64 end = size;
		for (int32_t j = 0; j < count; j++)
		{
			const u64 cand = tab_end + (u64)(int32_t)rd_le32 (data + 4 + (u64)j * 8 + 4);
			if (cand > tab_end + (u64)off && cand < end)
				end = cand;
		}
		const u64 begin = tab_end + (u64)off;
		u64 nidx = end > begin ? (end - begin) / 2 : 0;

		fprintf (out, "  [%d] id = %d, index_count = %llu\n", i, id, (unsigned long long)nidx);
		for (u64 k = 0; k < nidx; k++)
		{
			if (!(k & 7))
				fputs ("   ", out);
			fprintf (out, " %u", rd_le16 (data + begin + k * 2));
			if ((k & 7) == 7 || k + 1 == nidx)
				fputc ('\n', out);
		}
	}

	return ERR_OK;
}
