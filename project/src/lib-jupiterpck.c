#include "lib-jupiterpck.h"
#include <string.h>

bool IsJupiterPck (const u8 *data, uint data_size, u64 file_size)
{
	if (!data || data_size < 12 || file_size < 12)
		return false;

	const u32 hdr_len = (u32)data[0] | ((u32)data[1] << 8) | ((u32)data[2] << 16) | ((u32)data[3] << 24);
	const u32 count = (u32)data[4] | ((u32)data[5] << 8) | ((u32)data[6] << 16) | ((u32)data[7] << 24);

	if (count == 0 || count > JUPITER_PCK_MAX_FILES)
		return false;

	const u64 tbl_req = 8 + (u64)count * 4;
	if (hdr_len < tbl_req || (u64)hdr_len > file_size)
		return false;

	// If we have enough header bytes loaded, verify all sizes
	if (data_size >= tbl_req)
	{
		u64 sum_payload = 0;
		for (u32 i = 0; i < count; i++)
		{
			const u8 *p = data + 8 + i * 4;
			const u32 sz = (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
			if (sz == 0 || (u64)sz > file_size)
				return false;
			sum_payload += sz;
		}

		if ((u64)hdr_len + sum_payload != file_size)
			return false;

		// If the first payload is within read buffer, verify valid 4-character tag
		if ((u64)data_size >= (u64)hdr_len + 4)
		{
			const u8 *m = data + hdr_len;
			for (int k = 0; k < 4; k++)
			{
				if (!((m[k] >= 'A' && m[k] <= 'Z') || (m[k] >= '0' && m[k] <= '9')))
					return false;
			}
		}
	}

	return true;
}
