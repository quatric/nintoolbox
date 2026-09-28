// SPDX-License-Identifier: GPL-2.0+
// "Muramasa - The Demon Blade" (Wii) formats -- see lib-muramasa.h for
// exactly what is and is not understood about each of them.

#include "lib-muramasa.h"
#include "lib-nintendo.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>

//-----------------------------------------------------------------------------
// (1) "FCMP" compressed container

#define FCMP_HEADER_SIZE 12
#define FCMP_RESERVED 0x12340000u

// LZSS parameters used by the decoder at main.dol+0x802c09e0 -- see
// lib-muramasa.h for the full writeup.
#define FCMP_LZSS_N 4096 // ring buffer / window size (must be a power of 2)
#define FCMP_LZSS_F 18 // max match length
#define FCMP_LZSS_THRESHOLD 2
#define FCMP_LZSS_INITPOS (FCMP_LZSS_N - FCMP_LZSS_F) // 0xfee

typedef struct fcmp_inner_map_t
{
	const char *inner_tag; // 4-byte inner sub-blob magic (not NUL terminated)
	const char *ext; // on-disc extension it corresponds to
} fcmp_inner_map_t;

// Confirmed 1:1 mapping between the inner sub-blob tag and the on-disc
// extension, verified by re-decompressing every FCMP file on the retail
// disc (1454/1454) -- see lib-muramasa.h.
static const fcmp_inner_map_t fcmp_inner_map[] = { { "FMBS", ".mbs" }, { "FTEX", ".ftx" },
	{ "EMBP", ".esb" }, { "NSBD", ".nsb" }, { "MLIB", ".abf" }, { "NMSB", ".nms" },
	{ "WOLD", ".wbf" }, { 0, 0 } };

static const fcmp_inner_map_t *fcmp_find_inner (const u8 *tag)
{
	for (const fcmp_inner_map_t *m = fcmp_inner_map; m->inner_tag; m++)
		if (!memcmp (tag, m->inner_tag, 4))
			return m;
	return 0;
}

// Core LZSS decoder, reused by both DecompressMuramasaFcmp() and the
// format probe below (which only needs the first 4 output bytes to
// confirm the inner tag). Decodes 'payload' (the raw bitstream starting
// right after the 12-byte FCMP header) into 'dest_buf', stopping once
// either 'dest_buf_size' bytes have been written or the source is
// exhausted. Returns the number of bytes actually written; the caller can
// tell an early/truncated stop apart from success by comparing that
// against 'dest_buf_size'.
static size_t fcmp_lzss_decode (
	const u8 *payload, size_t payload_size, u8 *dest_buf, size_t dest_buf_size)
{
	u8 ring[FCMP_LZSS_N];
	memset (ring, 0, sizeof (ring));
	uint r = FCMP_LZSS_INITPOS;

	const u8 *src = payload;
	const u8 *src_end = payload + payload_size;
	u8 *dest = dest_buf;
	u8 *dest_end = dest_buf + dest_buf_size;

	uint flags = 0;
	while (dest < dest_end)
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
			r = (r + 1) & (FCMP_LZSS_N - 1);
		}
		else
		{
			if (src + 2 > src_end)
				break;
			const u8 b0 = *src++;
			const u8 b1 = *src++;
			uint i = b0 | (uint)(b1 & 0xf0) << 4;
			uint len = (b1 & 0x0f) + FCMP_LZSS_THRESHOLD + 1;
			for (uint k = 0; k < len && dest < dest_end; k++)
			{
				const u8 c = ring[(i + k) & (FCMP_LZSS_N - 1)];
				*dest++ = c;
				ring[r] = c;
				r = (r + 1) & (FCMP_LZSS_N - 1);
			}
		}
	}

	return dest - dest_buf;
}

int IsMuramasaFcmp (const u8 *data, size_t size, size_t file_size)
{
	if (!data || size < FCMP_HEADER_SIZE || memcmp (data, "FCMP", 4))
		return 0;

	u32 decomp_size = rd_le32 (data + 4);
	u32 reserved = rd_le32 (data + 8);
	if (reserved != FCMP_RESERVED)
		return 0;

	// Decompressed size must be at least as large as the compressed
	// payload that remains in the file -- true for every real sample on
	// the disc (compression never expands data here).
	if (file_size >= FCMP_HEADER_SIZE && decomp_size < file_size - FCMP_HEADER_SIZE)
		return 0;

	// Decode just enough of the LZSS stream to see the inner sub-blob
	// tag and confirm it is one of the confirmed seven -- cheap (stops
	// after 4 output bytes) and, unlike a raw byte peek, correct
	// regardless of the first control byte's value.
	if (size > FCMP_HEADER_SIZE)
	{
		u8 tag[4];
		size_t got = fcmp_lzss_decode (
			data + FCMP_HEADER_SIZE, size - FCMP_HEADER_SIZE, tag, sizeof (tag));
		if (got == sizeof (tag) && !fcmp_find_inner (tag))
			return 0;
	}

	return 1;
}

u32 GetDecompressedSizeMuramasaFcmp (const void *data, size_t data_size)
{
	if (!data || data_size < FCMP_HEADER_SIZE || memcmp (data, "FCMP", 4))
		return 0;
	return rd_le32 ((const u8 *)data + 4);
}

enumError DecompressMuramasaFcmp (const void *data, size_t data_size, void *dest_buf,
	size_t dest_buf_size, size_t *write_status, ccp fname, bool silent)
{
	DASSERT (data);
	DASSERT (dest_buf);

	const u8 *data8 = data;
	if (data_size < FCMP_HEADER_SIZE || memcmp (data8, "FCMP", 4)
		|| rd_le32 (data8 + 8) != FCMP_RESERVED)
	{
		if (write_status)
			*write_status = 0;
		return silent
			? ERR_WARNING
			: ERROR0 (ERR_INVALID_DATA, "Not a Muramasa FCMP container: %s\n", fname ? fname : "?");
	}

	size_t written = fcmp_lzss_decode (
		data8 + FCMP_HEADER_SIZE, data_size - FCMP_HEADER_SIZE, dest_buf, dest_buf_size);
	if (write_status)
		*write_status = written;

	if (written != dest_buf_size)
		return silent ? ERR_WARNING
					  : ERROR0 (ERR_INVALID_DATA,
							"Muramasa FCMP data corrupted: decompressed %zu of %zu expected "
							"bytes: %s\n",
							written, dest_buf_size, fname ? fname : "?");

	return ERR_OK;
}

enumError DecodeMuramasaFcmp_Text (FILE *f, const u8 *data, size_t size, size_t file_size)
{
	if (!f || !data || !IsMuramasaFcmp (data, size, file_size))
		return EINVAL;

	u32 decomp_size = rd_le32 (data + 4);

	fprintf (f, "# Muramasa - The Demon Blade FCMP compressed container\n");
	fprintf (f, "decompressed_size = %u (0x%x)\n", decomp_size, decomp_size);
	fprintf (f, "reserved = 0x%08x  # constant in every sample seen\n", rd_le32 (data + 8));

	u8 *decoded = decomp_size ? MALLOC (decomp_size) : 0;
	size_t written = 0;
	enumError err = decoded ? DecompressMuramasaFcmp (data, size, decoded, decomp_size, &written,
							 0, true)
							 : ERR_INVALID_DATA;

	if (err == ERR_OK)
	{
		const fcmp_inner_map_t *m = fcmp_find_inner (decoded);
		if (m)
			fprintf (f, "inner_tag = \"%.4s\"  # matches on-disc extension %s\n", decoded, m->ext);
		else
			fprintf (f, "inner_tag = \"%.4s\"  # NOT one of the confirmed tags\n", decoded);

		fprintf (f, "# LZSS payload decompressed successfully (%zu bytes) -- classic Okumura\n",
			written);
		fprintf (f, "# LZSS (N=%u, F=%u, THRESHOLD=%u); see lib-muramasa.h and\n", FCMP_LZSS_N,
			FCMP_LZSS_F, FCMP_LZSS_THRESHOLD);
		fprintf (f, "# DecompressMuramasaFcmp() for the algorithm/implementation.\n");
	}
	else
		fprintf (f, "# ERROR: LZSS payload failed to decompress (%zu of %u bytes produced)\n",
			written, decomp_size);

	if (decoded)
		FREE (decoded);

	return ERR_OK;
}

//-----------------------------------------------------------------------------
// (2) ".otb" "OTB " table (header only)

int IsMuramasaOtb (const u8 *data, size_t size, size_t file_size)
{
	if (!data || size < 16 || memcmp (data, "OTB ", 4))
		return 0;

	u32 body_size = rd_le32 (data + 4);
	u32 header_size = rd_le32 (data + 8);
	if ((u64)body_size + header_size != file_size)
		return 0;

	u32 count = rd_le32 (data + 12);
	if (!count || count > 1000000)
		return 0;

	return 1;
}

enumError DecodeMuramasaOtb_Text (FILE *f, const u8 *data, size_t size, size_t file_size)
{
	if (!f || !data || !IsMuramasaOtb (data, size, file_size))
		return EINVAL;

	fprintf (f, "# Muramasa - The Demon Blade .otb table\n");
	fprintf (f, "body_size = %u (0x%x)\n", rd_le32 (data + 4), rd_le32 (data + 4));
	fprintf (f, "header_size = %u (0x%x)  # body_size + header_size == file_size\n",
		rd_le32 (data + 8), rd_le32 (data + 8));
	fprintf (f, "count = %u (0x%x)\n", rd_le32 (data + 12), rd_le32 (data + 12));
	fprintf (f, "# entry table body not reverse-engineered (too few distinct samples on\n");
	fprintf (f, "# this disc -- see lib-muramasa.h) -- only the header is decoded\n");
	return ERR_OK;
}

//-----------------------------------------------------------------------------
// (3) ".nsi" "NSI " sound info table (header only)

int IsMuramasaNsi (const u8 *data, size_t size, size_t file_size)
{
	if (!data || size < 12 || memcmp (data, "NSI ", 4))
		return 0;

	u32 body_size = rd_le32 (data + 4);
	u32 tail_size = rd_le32 (data + 8);
	if ((u64)body_size + tail_size != file_size)
		return 0;

	return 1;
}

enumError DecodeMuramasaNsi_Text (FILE *f, const u8 *data, size_t size, size_t file_size)
{
	if (!f || !data || !IsMuramasaNsi (data, size, file_size))
		return EINVAL;

	fprintf (f, "# Muramasa - The Demon Blade .nsi sound info table\n");
	fprintf (f, "body_size = %u (0x%x)\n", rd_le32 (data + 4), rd_le32 (data + 4));
	fprintf (f, "tail_size = %u (0x%x)  # body_size + tail_size == file_size\n", rd_le32 (data + 8),
		rd_le32 (data + 8));
	fprintf (f, "# entry table body not reverse-engineered (only one real sample on this\n");
	fprintf (f, "# disc -- see lib-muramasa.h) -- only the header is decoded\n");
	return ERR_OK;
}
