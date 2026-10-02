#include "lib-ghosttrick.h"
#include <string.h>

bool IsCapcomMods (const u8 *data, uint data_size, u64 file_size)
{
	if (!data || data_size < 48)
		return false;

	// Check magic "MODS" and version "N3\n\0"
	if (memcmp (data, "MODSN3\n\0", 8) != 0)
		return false;

	const u32 frames = (u32)data[8] | ((u32)data[9] << 8) | ((u32)data[10] << 16) | ((u32)data[11] << 24);
	const u32 blk_sz = (u32)data[12] | ((u32)data[13] << 8) | ((u32)data[14] << 16) | ((u32)data[15] << 24);
	const u32 hdr_sz = (u32)data[16] | ((u32)data[17] << 8) | ((u32)data[18] << 16) | ((u32)data[19] << 24);

	if (frames == 0 || blk_sz != 256 || hdr_sz != 192)
		return false;

	const u32 tr_off = (u32)data[40] | ((u32)data[41] << 8) | ((u32)data[42] << 16) | ((u32)data[43] << 24);
	const u32 tr_cnt = (u32)data[44] | ((u32)data[45] << 8) | ((u32)data[46] << 16) | ((u32)data[47] << 24);

	if (tr_off < hdr_sz || tr_cnt == 0 || tr_cnt > 10000)
		return false;

	if (file_size && ((u64)tr_off + (u64)tr_cnt * 8 != file_size))
		return false;

	return true;
}

bool IsCapcomGML1 (const u8 *data, uint data_size, u64 file_size)
{
	if (!data || data_size < 20)
		return false;

	// Check magic "1LMG" (little-endian 0x474d4c31)
	if (memcmp (data, "1LMG", 4) != 0)
		return false;

	const u32 data_len = (u32)data[8] | ((u32)data[9] << 8) | ((u32)data[10] << 16) | ((u32)data[11] << 24);
	const u32 key_off = (u32)data[16] | ((u32)data[17] << 8) | ((u32)data[18] << 16) | ((u32)data[19] << 24);

	if (data_len == 0)
		return false;

	if (file_size && (48 + (u64)data_len > file_size))
		return false;

	if (key_off < 48 || (data_size >= 48 + data_len && key_off > 48 + data_len))
		return false;

	return true;
}
