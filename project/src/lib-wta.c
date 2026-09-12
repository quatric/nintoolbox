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


// ----------------------------------------------------------------------------
// 16. PlatinumGames WT Archive (.wta / WTA )
// ----------------------------------------------------------------------------
enumError ExtractWTAArchive (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext_match (arg, ".wta") && !is_ext_match (arg, ".bin"))
		return ERR_NOTHING_TO_DO;

	u8 *raw = 0;
	size_t raw_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false);
	if (err)
		return ERR_NOTHING_TO_DO;

	if (raw_size < 32 || memcmp (raw, "WTA ", 4))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	// WTA Header:
	// 0x00: Magic ("WTA\x20" or "WTB\x00")
	// 0x04: Version (usually 1)
	// 0x08: Number of textures / files
	// 0x0C: Offset to data offset table (or texture info table)
	// 0x10: Offset to data size table
	// 0x14: Unk table / flags table
	// 0x18: Unk2 table / idx table
	// 0x1C: Texture info table
	const u32 ver_be = rd_be32 (raw + 4);
	const u32 ver_le = rd_le32 (raw + 4);
	const bool big = (ver_be > 0 && ver_be <= 0xFFFF && ver_le > 0xFFFF);

	const u32 file_count = big ? rd_be32 (raw + 8) : rd_le32 (raw + 8);
	const u32 offset_pos_table = big ? rd_be32 (raw + 12) : rd_le32 (raw + 12);
	const u32 offset_size_table = big ? rd_be32 (raw + 16) : rd_le32 (raw + 16);

	if (!file_count || file_count > 100000)
	{
		FREE (raw);
		return ERR_INVALID_DATA;
	}

	char dest[PATH_MAX];
	get_dest_dir (dest, sizeof (dest), arg, basedir);
	CreatePath (dest, true);

	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT WTA:%s (%u files, %s-endian) -> %s/\n",
			verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, file_count,
			big ? "big" : "little", dest);

	for (uint i = 0; i < file_count; i++)
	{
		u32 cur_data_offset = 0;
		u32 comp_sz = 0;
		u32 uncomp_sz = 0;

		if (offset_pos_table >= 32 && offset_pos_table + (i + 1) * 4 <= raw_size
			&& offset_size_table >= 32 && offset_size_table + (i + 1) * 4 <= raw_size)
		{
			cur_data_offset = big ? rd_be32 (raw + offset_pos_table + i * 4)
								  : rd_le32 (raw + offset_pos_table + i * 4);
			comp_sz = big ? rd_be32 (raw + offset_size_table + i * 4)
						  : rd_le32 (raw + offset_size_table + i * 4);
			uncomp_sz = comp_sz;
		}
		else
		{
			// Fallback: 32-byte descriptor table starting at offset 32
			const uint entry_pos = 32 + i * 32;
			if (entry_pos + 32 > raw_size)
				break;
			uncomp_sz = big ? rd_be32 (raw + entry_pos + 12) : rd_le32 (raw + entry_pos + 12);
			comp_sz = big ? rd_be32 (raw + entry_pos + 16) : rd_le32 (raw + entry_pos + 16);
			cur_data_offset = 32 + file_count * 32;
		}

		char name[PATH_MAX];
		snprintf (name, sizeof (name), "texture_%04u.bin", i);

		if (cur_data_offset < raw_size && comp_sz > 0)
		{
			if (cur_data_offset + comp_sz > raw_size)
				comp_sz = (u32)(raw_size - cur_data_offset);

			char out_path[PATH_MAX];
			snprintf (out_path, sizeof (out_path), "%s/%s", dest, name);

			char *slash = strrchr (out_path, '/');
			if (slash)
			{
				*slash = 0;
				CreatePath (out_path, true);
				*slash = '/';
			}

			if (!testmode)
			{
				if (comp_sz != uncomp_sz && comp_sz >= 2 && raw[cur_data_offset] == 0x78)
				{
					u8 *decomp = 0;
					uint decomp_sz = 0;
					err = DecodeZlibGrow (&decomp, &decomp_sz, raw + cur_data_offset, comp_sz);
					if (!err && decomp)
					{
						SaveFile (out_path, 0, 0, decomp, decomp_sz, 0);
						FREE (decomp);
					}
					else
					{
						SaveFile (out_path, 0, 0, raw + cur_data_offset, comp_sz, 0);
					}
				}
				else
				{
					SaveFile (out_path, 0, 0, raw + cur_data_offset, comp_sz, 0);
				}
			}
		}
	}

	FREE (raw);
	return ERR_OK;
}

// ----------------------------------------------------------------------------
// Wii U big-endian WTA/WTP texture bundles (Star Fox Zero, \0BTW).
// ----------------------------------------------------------------------------

#define WTA_MAX_TEXTURES 4096
#define WTA_MAX_OUTPUT NFMT_MAX_OUTPUT

enumError ScanWTA (nintendo_sarc_entry_t **entries, uint *n_entries, const u8 *wta, uint wta_size,
	const u8 *wtp, uint wtp_size)
{
	if (!entries || !n_entries || !wta || !wtp || wta_size < 32
		|| memcmp (wta, "\0BTW", 4))
		return EINVAL;
	*entries = 0;
	*n_entries = 0;

	const u32 total = rd_be32 (wta + 8);
	const u32 o1 = rd_be32 (wta + 12);
	const u32 o5 = rd_be32 (wta + 28);
	if (!total || total > WTA_MAX_TEXTURES)
		return EINVAL;
	// Every table offset must land inside the file.
	for (uint k = 0; k < 5; k++)
	{
		const u32 o = rd_be32 (wta + 12 + k * 4);
		if (o >= wta_size)
			return EINVAL;
	}
	if ((u64)o1 + (u64)total * 4 > wta_size || (u64)o5 + (u64)total * 0xc0 > wta_size)
		return EINVAL;

	nintendo_sarc_entry_t *out = CALLOC (total, sizeof (*out));
	if (!out)
		return ERR_CANT_CREATE;

	uint n = 0;
	for (uint i = 0; i < total; i++)
	{
		const u32 start = rd_be32 (wta + o1 + i * 4);
		const u32 rec = o5 + i * 0xc0;
		if ((u64)rec + 0x20 + 4 > wta_size)
			continue;
		const u32 size = rd_be32 (wta + rec + 0x20);
		if (!size || size > WTA_MAX_OUTPUT || (u64)start + size > wtp_size)
			continue;
		if ((u64)o5 + 0x9c > wta_size || rec + 0x9c > wta_size)
			continue;

		// Gfx2 v7.1 shell (matches ScanGTX: 32-byte header, gpu 2).
		u8 shell[0x40];
		memset (shell, 0, sizeof (shell));
		memcpy (shell, "Gfx2", 4);
		wr_be32 (shell + 4, 0x20);
		wr_be32 (shell + 8, 7);
		wr_be32 (shell + 12, 1);
		wr_be32 (shell + 16, 2);
		// First BLK{ (surface-list block): header size, count 1, then
		// the surface type/size the decoder keys off (0x0B/0x9C).
		memcpy (shell + 0x20, "BLK{", 4);
		wr_be32 (shell + 0x24, 0x20);
		wr_be32 (shell + 0x28, 1);
		wr_be32 (shell + 0x30, 0x0b);
		wr_be32 (shell + 0x34, 0x9c);
		// Second BLK{ (payload block): type 0x0c, size patched below.
		u8 blk2[0x20];
		memset (blk2, 0, sizeof (blk2));
		memcpy (blk2, "BLK{", 4);
		wr_be32 (blk2 + 4, 0x20);
		wr_be32 (blk2 + 8, 1);
		wr_be32 (blk2 + 16, 0x0c);
		wr_be32 (blk2 + 20, size);
		u8 blk3[0x20];
		memset (blk3, 0, sizeof (blk3));
		memcpy (blk3, "BLK{", 4);
		wr_be32 (blk3 + 4, 0x20);
		wr_be32 (blk3 + 8, 1);
		wr_be32 (blk3 + 16, 1);

		const u64 total_size = (u64)sizeof (shell) + 0x9c + sizeof (blk2) + size + sizeof (blk3);
		if (total_size > WTA_MAX_OUTPUT)
			continue;
		u8 *gtx = MALLOC ((size_t)total_size);
		if (!gtx)
		{
			ResetOwnedEntries (out, n);
			FREE (out);
			return ERR_CANT_CREATE;
		}
		u8 *wp = gtx;
		memcpy (wp, shell, sizeof (shell));
		wp += sizeof (shell);
		memcpy (wp, wta + rec, 0x9c);
		wp += 0x9c;
		memcpy (wp, blk2, sizeof (blk2));
		wp += sizeof (blk2);
		// Surface-list count + payload size, as the reference layout.
		wr_be32 (gtx + 0x50, 1);
		wr_be32 (gtx + 0xf0, size);
		memcpy (wp, wtp + start, size);
		wp += size;
		memcpy (wp, blk3, sizeof (blk3));

		char name[64];
		snprintf (name, sizeof (name), "%04u.gtx", i);
		bool ok = OwnedEntryAdd (out, n, name, gtx, (uint)total_size);
		FREE (gtx);
		if (!ok)
		{
			ResetOwnedEntries (out, n);
			FREE (out);
			return ERR_CANT_CREATE;
		}
		n++;
	}

	if (!n)
	{
		FREE (out);
		return EINVAL;
	}
	*entries = out;
	*n_entries = n;
	return ERR_OK;
}
