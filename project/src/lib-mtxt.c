// SPDX-License-Identifier: GPL-2.0+
// Split out of lib-nintendo-archives.c -- one archive format per file.
#include "lib-nintendo-archives.h"
#include "lib-nintendo.h"
#include "lib-image.h"
#include "lib-camelot.h"
#include "lib-yay0.h"
#include "lib-flim.h"
#include "lib-szs.h"
#include "lib-std.h"
#include "lib-zstd.h"
#include "lib-archive-util.h"
#include <zlib.h>
#include <stdlib.h>
#include <string.h>


// Extract Nintendo Switch MTXT Texture Archive (.mtxt / MTXT)
enumError ExtractMTXTArchive (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext_match (arg, ".mtxt") && !is_ext_match (arg, ".bin"))
		return ERR_NOTHING_TO_DO;

	u8 *raw = 0;
	size_t raw_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false);
	if (err)
		return ERR_NOTHING_TO_DO;

	if (raw_size < 16 || memcmp (raw, "MTXT", 4))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	char dest[PATH_MAX];
	get_dest_dir (dest, sizeof (dest), arg, basedir);
	CreatePath (dest, true);

	// MTXT format:
	// 0x00: "MTXT"
	// 0x04: u32 Flags
	// 0x08: Gzip compressed stream or uncompressed payload containing inner texture/XTX data.
	// We decompress from offset 8 using zlib inflate (with gzip support).
	z_stream strm;
	memset (&strm, 0, sizeof (strm));
	strm.next_in = (Bytef *)(raw + 8);
	strm.avail_in = raw_size - 8;

	uint decomp_cap = 64 * 1024;
	u8 *decomp = MALLOC (decomp_cap);
	bool decompressed = false;

	if (decomp && (inflateInit2 (&strm, 15 + 32) == Z_OK || inflateInit2 (&strm, -15) == Z_OK))
	{
		for (;;)
		{
			strm.next_out = decomp + strm.total_out;
			strm.avail_out = decomp_cap - strm.total_out;

			int ret = inflate (&strm, Z_NO_FLUSH);
			if (ret == Z_STREAM_END)
			{
				decompressed = true;
				break;
			}
			if (ret != Z_OK)
				break;

			if (strm.avail_out == 0)
			{
				decomp_cap *= 2;
				u8 *n = REALLOC (decomp, decomp_cap);
				if (!n)
					break;
				decomp = n;
			}
		}
		inflateEnd (&strm);
	}

	if (decompressed && strm.total_out > 0)
	{
		// Extracted decompressed payload
		const uint decomp_size = (uint)strm.total_out;
		// Check if payload contains inner XTX ("DFvN")
		uint xtx_off = 0;
		bool has_xtx = false;
		for (uint i = 0; i + 4 <= decomp_size; i += 4)
		{
			if (!memcmp (decomp + i, "DFvN", 4))
			{
				xtx_off = i;
				has_xtx = true;
				break;
			}
		}

		if (has_xtx && xtx_off + 16 <= decomp_size)
		{
			char out_xtx[PATH_MAX];
			snprintf (out_xtx, sizeof (out_xtx), "%s/texture.xtx", dest);
			if (!testmode)
				SaveFile (out_xtx, 0, 0, decomp + xtx_off, decomp_size - xtx_off, 0);

			// Also extract any texture data block inside inner XTX
			const u32 header_size = rd_le32 (decomp + xtx_off + 4);
			if (header_size >= 16 && xtx_off + header_size < decomp_size)
			{
				uint bpos = xtx_off + header_size;
				uint img_idx = 0;
				while (bpos + 32 <= decomp_size)
				{
					if (memcmp (decomp + bpos, "HBvN", 4))
						break;
					const u32 block_size = rd_le32 (decomp + bpos + 4);
					const u64 data_size = rd_le64 (decomp + bpos + 8);
					const s64 data_offset = (s64)rd_le64 (decomp + bpos + 16);
					const u32 block_type = rd_le32 (decomp + bpos + 24);

					if (block_type == 3 && data_size > 0)
					{
						const s64 abs_payload = (s64)bpos + data_offset;
						if (abs_payload >= 0 && (size_t)abs_payload + data_size <= decomp_size)
						{
							char out_bin[PATH_MAX];
							snprintf (
								out_bin, sizeof (out_bin), "%s/surface_%04u.bin", dest, img_idx++);
							if (!testmode)
								SaveFile (out_bin, 0, 0, decomp + abs_payload, (uint)data_size, 0);
						}
					}
					if (block_size == 0)
						break;
					bpos += block_size;
				}
			}
		}
		else
		{
			char out_bin[PATH_MAX];
			snprintf (out_bin, sizeof (out_bin), "%s/payload.bin", dest);
			if (!testmode)
				SaveFile (out_bin, 0, 0, decomp, decomp_size, 0);
		}
		FREE (decomp);
	}
	else
	{
		if (decomp)
			FREE (decomp);
		// If decompression failed, save raw payload after 8-byte header
		if (raw_size > 8)
		{
			char out_bin[PATH_MAX];
			snprintf (out_bin, sizeof (out_bin), "%s/payload.bin", dest);
			if (!testmode)
				SaveFile (out_bin, 0, 0, raw + 8, (uint)(raw_size - 8), 0);
		}
	}

	FREE (raw);
	return ERR_OK;
}


// Nintendo Switch MTXT texture archive (.mtxt): a four-byte magic, a flags
// word, then a gzip stream wrapping one XTX texture container.
//
// The reader also slices the XTX's own mip surfaces out into
// "surface_%04u.bin" side-products for convenience; those are copies of
// bytes already inside texture.xtx, so the writer consumes only the XTX
// itself and lets the reader regenerate them.
enumError CreateMTXTArchive (
	u8 **dest, uint *dest_size, const nintendo_sarc_entry_t *entries, uint n_entries)
{
	if (!dest || !dest_size || !entries || !n_entries)
		return ERR_INVALID_DATA;

	const nintendo_sarc_entry_t *xtx = 0;
	for (uint i = 0; i < n_entries; i++)
	{
		ccp name = leaf_name (entries[i].name);
		if (!strcasecmp (name, "texture.xtx"))
		{
			xtx = entries + i;
			break;
		}
	}
	// Fall back to the sole member when it isn't named texture.xtx, but
	// never guess between several candidates.
	if (!xtx && n_entries == 1)
		xtx = entries;
	if (!xtx || !xtx->data || !xtx->size)
		return ERR_NOTHING_TO_DO;

	z_stream zs;
	memset (&zs, 0, sizeof (zs));
	// Pin the gzip parameters so the same XTX always deflates to the same
	// bytes: level 9, the default strategy, and a gzip wrapper.
	if (deflateInit2 (&zs, 9, Z_DEFLATED, 15 + 16, 8, Z_DEFAULT_STRATEGY) != Z_OK)
		return ERR_CANT_CREATE;

	const uLong bound = deflateBound (&zs, xtx->size);
	u8 *buf = MALLOC (8 + bound);
	if (!buf)
	{
		deflateEnd (&zs);
		return ERR_OUT_OF_MEMORY;
	}

	memcpy (buf, "MTXT", 4);
	wr_le32 (buf + 4, 0); // flags

	zs.next_in = (Bytef *)xtx->data;
	zs.avail_in = xtx->size;
	zs.next_out = buf + 8;
	zs.avail_out = bound;

	if (deflate (&zs, Z_FINISH) != Z_STREAM_END)
	{
		deflateEnd (&zs);
		FREE (buf);
		return ERR_CANT_CREATE;
	}
	const uint out_size = 8 + (uint)zs.total_out;
	deflateEnd (&zs);

	*dest = buf;
	*dest_size = out_size;
	return ERR_OK;
}
