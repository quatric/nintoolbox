// SPDX-License-Identifier: GPL-2.0+
// "Imagine: Party Babyz" (Wii) format -- see lib-babyz.h for exactly what
// is and is not understood about the container.

#include "lib-babyz.h"
#include "lib-nintendo.h"
#include <string.h>

//-----------------------------------------------------------------------------
// "!Ce\x87" compressed container

#define BABYZ_HEADER_SIZE 12
static const u8 BABYZ_MAGIC[4] = { 0x21, 0x43, 0x65, 0x87 };

// LZSS parameters used by the decoder at main.dol+0x8001987c -- see
// lib-babyz.h for the full writeup.
#define BABYZ_LZSS_N 4096 // ring buffer / window size (must be a power of 2)
#define BABYZ_LZSS_F 18 // max match length
#define BABYZ_LZSS_THRESHOLD 3
#define BABYZ_LZSS_INITPOS (BABYZ_LZSS_N - BABYZ_LZSS_F) // 0xfee

// Core LZSS decoder, reused by both DecompressBabyzWiz() and the format
// probe below (which only needs to confirm the stream decodes cleanly).
// Decodes 'payload' (the raw bitstream starting right after the 12-byte
// header) into 'dest_buf', stopping once either 'payload_size' bytes of
// input have been consumed (matching the real decoder's loop condition)
// or 'dest_buf_size' bytes have been written. Returns the number of
// bytes actually written.
static size_t babyz_lzss_decode (
	const u8 *payload, size_t payload_size, u8 *dest_buf, size_t dest_buf_size)
{
	u8 ring[BABYZ_LZSS_N];
	memset (ring, 0x20, sizeof (ring));
	uint r = BABYZ_LZSS_INITPOS;

	const u8 *src = payload;
	const u8 *src_end = payload + payload_size;
	u8 *dest = dest_buf;
	u8 *dest_end = dest_buf + dest_buf_size;

	uint flags = 0;
	while (src < src_end && dest < dest_end)
	{
		flags >>= 1;
		if (!(flags & 0x100))
		{
			if (src == src_end)
				break;
			flags = *src++ | 0xff00;
		}

		if (flags & 1)
		{
			if (src == src_end)
				break;
			const u8 c = *src++;
			*dest++ = c;
			ring[r] = c;
			r = (r + 1) & (BABYZ_LZSS_N - 1);
		}
		else
		{
			if (src + 2 > src_end)
				break;
			const u8 b0 = *src++;
			const u8 b1 = *src++;
			uint i = b0 | (uint)(b1 & 0xf0) << 4;
			uint len = (b1 & 0x0f) + BABYZ_LZSS_THRESHOLD;
			for (uint k = 0; k < len && dest < dest_end; k++)
			{
				const u8 c = ring[(i + k) & (BABYZ_LZSS_N - 1)];
				*dest++ = c;
				ring[r] = c;
				r = (r + 1) & (BABYZ_LZSS_N - 1);
			}
		}
	}

	return dest - dest_buf;
}

int IsBabyzWiz (const u8 *data, size_t size, size_t file_size)
{
	if (!data || size < BABYZ_HEADER_SIZE || memcmp (data, BABYZ_MAGIC, 4))
		return 0;

	u32 comp_size = rd_le32 (data + 4);
	u32 decomp_size = rd_le32 (data + 8);
	if (!decomp_size)
		return 0;

	// The compressed bitstream must fit in the file right after the
	// header -- true for every real sample seen.
	if (file_size < (u64)BABYZ_HEADER_SIZE + comp_size)
		return 0;

	// Decode just enough of the LZSS stream to confirm it produces
	// exactly the announced output size (cheap: decode is linear and
	// this is the same check DecompressBabyzWiz() would fail on
	// mismatch, done here up front so probing stays side-effect free).
	if (size >= (size_t)BABYZ_HEADER_SIZE + comp_size)
	{
		u8 probe[64];
		size_t want = decomp_size < sizeof (probe) ? decomp_size : sizeof (probe);
		size_t got = babyz_lzss_decode (data + BABYZ_HEADER_SIZE, comp_size, probe, want);
		if (got != want)
			return 0;
	}

	return 1;
}

u32 GetDecompressedSizeBabyzWiz (const void *data, size_t data_size)
{
	if (!data || data_size < BABYZ_HEADER_SIZE || memcmp (data, BABYZ_MAGIC, 4))
		return 0;
	return rd_le32 ((const u8 *)data + 4 + 4);
}

enumError DecompressBabyzWiz (const void *data, size_t data_size, void *dest_buf,
	size_t dest_buf_size, size_t *write_status, ccp fname, bool silent)
{
	DASSERT (data);
	DASSERT (dest_buf);

	const u8 *data8 = data;
	if (data_size < BABYZ_HEADER_SIZE || memcmp (data8, BABYZ_MAGIC, 4))
	{
		if (write_status)
			*write_status = 0;
		return silent ? ERR_WARNING
					  : ERROR0 (ERR_INVALID_DATA, "Not an Imagine: Party Babyz \"!Ce\" container: %s\n",
							fname ? fname : "?");
	}

	u32 comp_size = rd_le32 (data8 + 4);
	if (data_size < (size_t)BABYZ_HEADER_SIZE + comp_size)
		comp_size = (u32)(data_size - BABYZ_HEADER_SIZE);

	size_t written
		= babyz_lzss_decode (data8 + BABYZ_HEADER_SIZE, comp_size, dest_buf, dest_buf_size);
	if (write_status)
		*write_status = written;

	if (written != dest_buf_size)
		return silent ? ERR_WARNING
					  : ERROR0 (ERR_INVALID_DATA,
							"Imagine: Party Babyz container data corrupted: decompressed %zu of "
							"%zu expected bytes: %s\n",
							written, dest_buf_size, fname ? fname : "?");

	return ERR_OK;
}

enumError DecodeBabyzWiz_Text (FILE *f, const u8 *data, size_t size, size_t file_size)
{
	if (!f || !data || !IsBabyzWiz (data, size, file_size))
		return EINVAL;

	u32 comp_size = rd_le32 (data + 4);
	u32 decomp_size = rd_le32 (data + 8);

	fprintf (f, "# Imagine: Party Babyz \"!Ce\" compressed container\n");
	fprintf (f, "comp_size = %u (0x%x)\n", comp_size, comp_size);
	fprintf (f, "decompressed_size = %u (0x%x)\n", decomp_size, decomp_size);

	u8 *decoded = decomp_size ? MALLOC (decomp_size) : 0;
	size_t written = 0;
	enumError err = decoded
		? DecompressBabyzWiz (data, size, decoded, decomp_size, &written, 0, true)
		: ERR_INVALID_DATA;

	if (err == ERR_OK)
		fprintf (f,
			"# LZSS payload decompressed successfully (%zu bytes) -- classic Okumura LZSS\n"
			"# (N=%u, F=%u, THRESHOLD=%u); see lib-babyz.h and DecompressBabyzWiz() for the\n"
			"# algorithm/implementation.\n",
			written, BABYZ_LZSS_N, BABYZ_LZSS_F, BABYZ_LZSS_THRESHOLD);
	else
		fprintf (f, "# ERROR: LZSS payload failed to decompress (%zu of %u bytes produced)\n",
			written, decomp_size);

	if (decoded)
		FREE (decoded);

	return ERR_OK;
}
