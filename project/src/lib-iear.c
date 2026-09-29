// Luminous Arc IEAR: MAIN header, padded JTBL directory, typed chunks, ENDT.
#include "lib-iear.h"
#include "lib-nintendo.h"

enumError ScanIEAR (iear_entry_t **entries, uint *count, const u8 *src, uint size)
{
	if (!entries || !count)
		return ERR_INVALID_DATA;
	*entries = 0;
	*count = 0;
	if (!src || size < 48 || memcmp (src, "MAIN", 4) || memcmp (src + 16, "JTBL", 4)
		|| memcmp (src + size - 16, "ENDT", 4))
		return ERR_INVALID_DATA;
	const uint n = rd_le32 (src + 4), words = rd_le32 (src + 20);
	if (n > 100000 || words != ((n * 2 + 3) & ~3u) || words > (size - 48) / 4)
		return ERR_INVALID_DATA;
	uint previous_end = 32 + words * 4;
	// Validate every chunk before exposing any payloads to the caller.
	for (uint i = 0; i < n; i++)
	{
		const uint offset = rd_le32 (src + 32 + i * 8), length = rd_le32 (src + 36 + i * 8);
		if (offset < previous_end || offset > size - 16 || length < 16
			|| length > size - 16 - offset || rd_le32 (src + offset + 4) != length - 16)
			return ERR_INVALID_DATA;
		previous_end = offset + length;
	}
	if (previous_end != size - 16)
		return ERR_INVALID_DATA;
	if (!n)
		return ERR_OK;
	iear_entry_t *out = CALLOC (n, sizeof (*out));
	if (!out)
		return ERR_OUT_OF_MEMORY;
	for (uint i = 0; i < n; i++)
	{
		const uint offset = rd_le32 (src + 32 + i * 8);
		out[i].data = src + offset + 16;
		out[i].size = rd_le32 (src + offset + 4);
		bool safe = true;
		for (uint j = 0; j < 4; j++)
		{
			uint c = src[offset + j];
			if (!c)
				break;
			if (c >= 'A' && c <= 'Z')
				c += 'a' - 'A';
			if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_'))
				safe = false;
			out[i].extension[j] = c;
		}
		if (!safe || !out[i].extension[0])
			strcpy (out[i].extension, "bin");
	}
	*entries = out;
	*count = n;
	return ERR_OK;
}
