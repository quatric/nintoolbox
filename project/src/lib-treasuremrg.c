#include "lib-treasuremrg.h"
#include <string.h>

bool IsTreasureMrg (const u8 *data, uint data_size, u64 file_size)
{
	if (!data || data_size < 12)
		return false;

	const u32 count = (u32)data[0] | ((u32)data[1] << 8) | ((u32)data[2] << 16) | ((u32)data[3] << 24);
	if (count == 0 || count > TREASURE_MRG_MAX_FILES)
		return false;

	const u64 table_size = 4 + (u64)count * 8;
	if (file_size && table_size > file_size)
		return false;

	// Check table within available buffer
	const uint check_entries = data_size >= table_size ? count : (data_size >= 12 ? (data_size - 4) / 8 : 0);
	if (!check_entries)
		return false;

	const u32 off0 = (u32)data[4] | ((u32)data[5] << 8) | ((u32)data[6] << 16) | ((u32)data[7] << 24);
	// off0 must be at least table_size and reasonably padded (within 16 bytes)
	if ((u64)off0 < table_size || (u64)off0 > table_size + 16)
		return false;

	u32 prev_off = 0;
	for (uint i = 0; i < check_entries; i++)
	{
		const u8 *p = data + 4 + i * 8;
		const u32 off = (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
		const u32 sz = (u32)p[4] | ((u32)p[5] << 8) | ((u32)p[6] << 16) | ((u32)p[7] << 24);

		if ((u64)off < table_size)
			return false;
		if (file_size && (u64)off + sz > file_size)
			return false;
		if (i > 0 && off < prev_off)
			return false;
		prev_off = off;
	}

	return true;
}
