#include "lib-zip.h"
#include <zlib.h>

// A fixed-size window avoids allocating the untrusted advertised output size.
// A null file validates the full payload before any archive member is written.
enumError DecodeZIPMember (
	FILE *file, const u8 *data, uint stored_size, uint size, uint expected_crc, uint method)
{
	if (!data || (method != 0 && method != 8))
		return ERR_INVALID_DATA;
	u8 buffer[65536];
	z_stream stream = { 0 };
	uLong crc = crc32 (0, Z_NULL, 0);
	u64 total = 0;
	if (!method)
	{
		if (stored_size != size)
			return ERR_INVALID_DATA;
		uint pos = 0;
		while (pos < size)
		{
			const uint count = size - pos < sizeof (buffer) ? size - pos : sizeof (buffer);
			crc = crc32 (crc, data + pos, count);
			if (file && fwrite (data + pos, 1, count, file) != count)
				return ERR_WRITE_FAILED;
			pos += count;
		}
		return crc == expected_crc ? ERR_OK : ERR_INVALID_DATA;
	}
	stream.next_in = (Bytef *)data;
	stream.avail_in = stored_size;
	if (inflateInit2 (&stream, -MAX_WBITS) != Z_OK)
		return ERR_INVALID_DATA;
	enumError err = ERR_OK;
	int status;
	do
	{
		stream.next_out = buffer;
		stream.avail_out = sizeof (buffer);
		const uLong before = stream.total_in;
		status = inflate (&stream, Z_NO_FLUSH);
		const uint count = sizeof (buffer) - stream.avail_out;
		total += count;
		if (total > size || (status != Z_OK && status != Z_STREAM_END)
			|| (status == Z_OK && !count && stream.total_in == before))
		{
			err = ERR_INVALID_DATA;
			break;
		}
		crc = crc32 (crc, buffer, count);
		if (file && count && fwrite (buffer, 1, count, file) != count)
		{
			err = ERR_WRITE_FAILED;
			break;
		}
	} while (status != Z_STREAM_END);
	if (!err && (total != size || stream.avail_in || crc != expected_crc))
		err = ERR_INVALID_DATA;
	inflateEnd (&stream);
	return err;
}
