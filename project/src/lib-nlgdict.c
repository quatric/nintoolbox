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


// Extract Next Level Games Dictionary Archive (.dict / LM2 / LM3 / Punch-Out!!)
enumError ExtractNLGDictArchive (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext_match (arg, ".dict") && !is_ext_match (arg, ".bin"))
		return ERR_NOTHING_TO_DO;

	u8 *raw = 0;
	size_t raw_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false);
	if (err)
		return ERR_NOTHING_TO_DO;

	if (raw_size < 16)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	const u32 m_be = rd_be32 (raw);
	const u32 m_le = rd_le32 (raw);

	// Supported magics:
	// 0x5824F3A9: LM2 / LM3
	// 0xA9F32458: Punch-Out!! Wii
	bool is_lm = (m_be == 0x5824F3A9 || m_le == 0x5824F3A9);
	bool is_po = (m_be == 0xA9F32458 || m_le == 0xA9F32458);

	if (!is_lm && !is_po)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	// Determine companion .data file
	char data_path[PATH_MAX];
	snprintf (data_path, sizeof (data_path), "%s", arg);
	char *dot = strrchr (data_path, '.');
	if (dot && !strcasecmp (dot, ".dict"))
		strcpy (dot, ".data");
	else
		snprintf (data_path, sizeof (data_path), "%s.data", arg);

	u8 *data_raw = 0;
	size_t data_raw_size = 0;
	LoadFileAlloc (data_path, 0, 0, &data_raw, &data_raw_size, 0, 0, 0, false);

	char dest[PATH_MAX];
	get_dest_dir (dest, sizeof (dest), arg, basedir);
	CreatePath (dest, true);

	uint extracted_count = 0;

	if (is_lm)
	{
		// LM2 / LM3 Dictionary format
		// Check LM3 indicator at offset 12: 0x78340300 (or BE equivalent)
		const bool is_lm3 = (raw_size >= 16
			&& (rd_be32 (raw + 12) == 0x78340300 || rd_le32 (raw + 12) == 0x78340300));
		const bool is_compressed = (raw[6] == 1);

		uint num_files = 0;
		uint file_table_offset = 0;

		if (is_lm3)
		{
			num_files = raw[16];
			const uint num_chunk_infos = raw[17];
			file_table_offset = 20 + num_chunk_infos * 24;
		}
		else
		{
			num_files = rd_le32 (raw + 8);
			if (num_files == 0 || num_files > 100000)
				num_files = rd_be32 (raw + 8);
			file_table_offset = 0x2C + num_files;
		}

		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT %s:%s (%u files) -> %s/\n", verbose > 0 ? "\n" : "",
				testmode ? "WOULD " : "", is_lm3 ? "LM3-DICT" : "LM2-DICT", arg, num_files, dest);

		for (uint i = 0; i < num_files; i++)
		{
			const uint eoff = file_table_offset + i * 16;
			if (eoff + 16 > raw_size)
				break;

			const u32 offset = rd_le32 (raw + eoff);
			const u32 decomp_size = rd_le32 (raw + eoff + 4);
			const u32 comp_size = rd_le32 (raw + eoff + 8);

			if (decomp_size == 0)
				continue;

			// Source buffer can come from .data file if present, or from .dict itself if within
			// raw_size
			const u8 *src = 0;
			size_t src_avail = 0;
			if (data_raw && offset < data_raw_size)
			{
				src = data_raw + offset;
				src_avail = data_raw_size - offset;
			}
			else if (offset < raw_size)
			{
				src = raw + offset;
				src_avail = raw_size - offset;
			}

			if (!src)
				continue;

			char out_path[PATH_MAX];
			snprintf (out_path, sizeof (out_path), "%s/file_%04u.bin", dest, i);

			if (!testmode)
			{
				if (is_compressed && comp_size > 0 && src_avail >= comp_size)
				{
					// Check zlib header
					if (comp_size >= 2
						&& (src[0] == 0x78
							&& (src[1] == 0x9c || src[1] == 0xda || src[1] == 0x01
								|| src[1] == 0x5e)))
					{
						u8 *decomp = MALLOC (decomp_size);
						if (decomp)
						{
							uLongf dest_len = decomp_size;
							if (uncompress (decomp, &dest_len, src, comp_size) == Z_OK)
							{
								SaveFile (out_path, 0, 0, decomp, (uint)dest_len, 0);
								extracted_count++;
								FREE (decomp);
								continue;
							}
							FREE (decomp);
						}
					}
					// Raw dump if decompression fails or not compressed
					SaveFile (out_path, 0, 0, src, comp_size, 0);
					extracted_count++;
				}
				else
				{
					const u32 sz = decomp_size <= src_avail ? decomp_size : (u32)src_avail;
					SaveFile (out_path, 0, 0, src, sz, 0);
					extracted_count++;
				}
			}
			else
				extracted_count++;
		}
	}
	else if (is_po)
	{
		// Punch-Out!! Wii Dictionary
		const bool is_compressed = (raw[6] == 1);
		const u32 num_files = rd_be32 (raw + 16);
		const u32 file_table_size = rd_be32 (raw + 20);

		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT PO-DICT:%s (%u files) -> %s/\n", verbose > 0 ? "\n" : "",
				testmode ? "WOULD " : "", arg, num_files, dest);

		// Calculate block table offset
		u32 cur_blk_off = 0;
		u32 blk_offsets[8] = { 0 };
		u32 blk_sizes[8] = { 0 };
		for (uint b = 0; b < 8; b++)
		{
			const uint boff = 24 + b * 8;
			if (boff + 8 <= raw_size)
			{
				blk_offsets[b] = cur_blk_off;
				blk_sizes[b] = rd_be32 (raw + boff);
				cur_blk_off += blk_sizes[b];
			}
		}
		const u32 file_table_offset = cur_blk_off;

		// File table is in .data file at file_table_offset, or in .dict if present
		const u8 *tbl_src = 0;
		size_t tbl_avail = 0;
		if (data_raw && file_table_offset < data_raw_size)
		{
			tbl_src = data_raw + file_table_offset;
			tbl_avail = data_raw_size - file_table_offset;
		}
		else if (file_table_offset < raw_size)
		{
			tbl_src = raw + file_table_offset;
			tbl_avail = raw_size - file_table_offset;
		}

		if (tbl_src && num_files > 0)
		{
			for (uint i = 0; i < num_files; i++)
			{
				const uint eoff = i * 10;
				if (eoff + 10 > tbl_avail)
					break;

				const u8 chunk_flags = tbl_src[eoff];
				const u16 chunk_type = rd_be16 (tbl_src + eoff + 2);
				const u32 chunk_sz = rd_be32 (tbl_src + eoff + 4);
				const u32 chunk_off = rd_be32 (tbl_src + eoff + 8);

				int blk_idx = -1;
				if (chunk_flags == 0x12)
					blk_idx = 0;
				else if (chunk_flags == 0x25)
					blk_idx = 1;
				else if (chunk_flags == 2)
					blk_idx = 2;
				else if (chunk_flags == 0x42)
					blk_idx = 3;
				else if (chunk_flags == 3)
					blk_idx = 0;
				else
				{
					const u8 bf = chunk_flags >> 4;
					if (bf < 8)
						blk_idx = bf;
				}

				if (blk_idx < 0 || blk_idx >= 8 || chunk_sz == 0)
					continue;

				const u32 abs_off = blk_offsets[blk_idx] + chunk_off;
				const u8 *f_src = 0;
				size_t f_avail = 0;
				if (data_raw && abs_off < data_raw_size)
				{
					f_src = data_raw + abs_off;
					f_avail = data_raw_size - abs_off;
				}
				else if (abs_off < raw_size)
				{
					f_src = raw + abs_off;
					f_avail = raw_size - abs_off;
				}

				if (!f_src)
					continue;

				const u32 to_write = chunk_sz <= f_avail ? chunk_sz : (u32)f_avail;
				char out_path[PATH_MAX];
				snprintf (out_path, sizeof (out_path), "%s/chunk_%04u_type_%04x.bin", dest, i,
					chunk_type);

				if (!testmode && to_write > 0)
					SaveFile (out_path, 0, 0, f_src, to_write, 0);

				extracted_count++;
			}
		}
	}

	if (data_raw)
		FREE (data_raw);
	FREE (raw);

	return ERR_OK;
}
