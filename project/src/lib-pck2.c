// Level-5 PCK2: sequential variable-length records, often LZ10-wrapped as .plz.
#include "lib-pck2.h"
#include "lib-nintendo.h"

enumError ScanPCK2 (pck2_entry_t **entries, uint *count, const u8 *src, uint size)
{
	if (!entries || !count)
		return ERR_INVALID_DATA;
	*entries = 0;
	*count = 0;
	if (!src || size < 16 || memcmp (src + 8, "PCK2", 4))
		return ERR_INVALID_DATA;
	const uint head = rd_le32 (src), total = rd_le32 (src + 4);
	if (head < 16 || head > total || total > size)
		return ERR_INVALID_DATA;
	uint n = 0;
	for (uint pos = head; pos < total;)
	{
		if (total - pos < 16)
			return ERR_INVALID_DATA;
		const uint header = rd_le32 (src + pos), record = rd_le32 (src + pos + 4);
		const uint payload = rd_le32 (src + pos + 12);
		if (header <= 16 || header > record || record > total - pos || payload > record - header)
			return ERR_INVALID_DATA;
		if (!src[pos + 16] || !memchr (src + pos + 16, 0, header - 16))
			return ERR_INVALID_DATA;
		pos += record;
		n++;
	}
	if (!n)
		return ERR_OK;
	pck2_entry_t *out = CALLOC (n, sizeof (*out));
	if (!out)
		return ERR_OUT_OF_MEMORY;
	uint pos = head;
	for (uint i = 0; i < n; i++)
	{
		out[i].name = (ccp)src + pos + 16;
		out[i].data = src + pos + rd_le32 (src + pos);
		out[i].size = rd_le32 (src + pos + 12);
		pos += rd_le32 (src + pos + 4);
	}
	*entries = out;
	*count = n;
	return ERR_OK;
}
