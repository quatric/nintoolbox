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
#include "lib-fedforce.h"
#include "lib-nlg-lm.h"
#include "lib-model-glb.h"
#include "lib-ctpk.h"
#include <zlib.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

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
	// The companion .data is optional (block dumps fall back to the .dict
	// itself); probe quietly so a missing file is not an error.
	if (!access (data_path, R_OK))
		LoadFileAlloc (data_path, 0, 0, &data_raw, &data_raw_size, 0, 0, 0, false);

	char dest[PATH_MAX];
	get_dest_dir (dest, sizeof (dest), arg, basedir);
	CreatePath (dest, true);

	uint extracted_count = 0;

	if (is_lm)
	{
		// LM2 / LM3 / Federation Force / StrikersBLF Dictionary format
		// Check LM3 indicator at offset 12: 0x78340300
		const bool is_lm3 = (raw_size >= 16
			&& (rd_be32 (raw + 12) == 0x78340300 || rd_le32 (raw + 12) == 0x78340300));
		// Metroid Prime: Federation Force indicator at offset 16: 0x297B947A
		const bool is_fed = (raw_size >= 20
			&& (rd_be32 (raw + 16) == 0x297B947A || rd_le32 (raw + 16) == 0x297B947A));
		// Mario Strikers: Battle League Football indicator at offset 0x40
		const bool is_strikers = (raw_size >= 0x48
			&& (rd_be32 (raw + 0x40) == 4247762216u || rd_le32 (raw + 0x40) == 4247762216u));
		const bool is_lm2hd
			= (!is_lm3 && !is_fed && !is_strikers && raw_size >= 9 && (raw[8] % 7 == 0));
		const bool is_compressed = (raw[6] == 1);

		nlg_variant_t nlg_variant = NLGDetectVariant (raw, (uint)raw_size);

		ccp dict_name = nlg_variant == NLG_FEDFORCE
			? "FEDFORCE-DICT"
			: (nlg_variant == NLG_LM3
					  ? "LM3-DICT"
					  : (nlg_variant == NLG_LM2
								? "LM2-DICT"
								: (is_lm2hd ? "LM2HD-DICT"
											: (is_strikers ? "STRIKERS-DICT" : "LM2-DICT"))));

		// Resolved block table for the file_*.bin dump: structural scans
		// for the known variants, legacy offset heuristic otherwise.
		typedef struct
		{
			u32 off, dec, comp;
		} dump_blk_t;
		dump_blk_t *dump_blks = 0;
		uint num_files = 0;
		if (nlg_variant == NLG_LM3 || nlg_variant == NLG_LM2 || nlg_variant == NLG_FEDFORCE)
		{
			nlg_block_t *bl = 0;
			uint n_blocks = 0;
			bool ok = false;
			if (nlg_variant == NLG_LM3)
				ok = ScanLM3Dict (raw, (uint)raw_size, &bl, &n_blocks, 0, 0, 0) == ERR_OK;
			else if (nlg_variant == NLG_LM2)
				ok = ScanLM2Dict (raw, (uint)raw_size, &bl, &n_blocks, 0, 0, 0) == ERR_OK;
			else
			{
				fed_dict_block_t *fb = 0;
				uint nfb = 0;
				bool is_fed = false;
				if (ScanFedForceDict (raw, (uint)raw_size, &is_fed, &fb, &nfb, 0, 0, 0) == ERR_OK
					&& is_fed)
				{
					bl = CALLOC (nfb ? nfb : 1, sizeof (*bl));
					if (bl)
					{
						for (uint i = 0; i < nfb; i++)
						{
							bl[i].offset = fb[i].offset;
							bl[i].decomp_size = fb[i].decomp_size;
							bl[i].comp_size = fb[i].comp_size;
						}
						n_blocks = nfb;
						ok = true;
					}
				}
				FreeFedForceDict (fb, 0, 0);
			}
			if (ok && n_blocks)
			{
				dump_blks = CALLOC (n_blocks, sizeof (*dump_blks));
				if (dump_blks)
					for (uint i = 0; i < n_blocks; i++)
					{
						dump_blks[i].off = bl[i].offset;
						dump_blks[i].dec = bl[i].decomp_size;
						dump_blks[i].comp = bl[i].comp_size;
					}
				else
					ok = false;
				num_files = ok ? n_blocks : 0;
			}
			FreeNLGDict (bl, 0, 0);
		}
		else
		{
			// Legacy heuristic for Strikers-BLF / LM2HD / unknown LM.
			uint file_table_offset = 0;
			if (is_lm3 || is_fed)
			{
				const uint ref_size = is_fed ? 16 : 24;
				num_files = raw[12] > 0 ? raw[12] : raw[16];
				const uint num_chunk_infos = raw[13] > 0 ? raw[13] : raw[17];
				file_table_offset = 20 + num_chunk_infos * ref_size;
			}
			else
			{
				num_files = rd_le32 (raw + 8);
				if (num_files == 0 || num_files > 100000)
					num_files = rd_be32 (raw + 8);
				file_table_offset = 0x2C + num_files;
			}
			if (num_files && num_files <= 100000)
			{
				dump_blks = CALLOC (num_files, sizeof (*dump_blks));
				if (dump_blks)
					for (uint i = 0; i < num_files; i++)
					{
						const uint eoff = file_table_offset + i * 16;
						if (eoff + 16 > raw_size)
						{
							num_files = i;
							break;
						}
						dump_blks[i].off = rd_le32 (raw + eoff);
						dump_blks[i].dec = rd_le32 (raw + eoff + 4);
						dump_blks[i].comp = rd_le32 (raw + eoff + 8);
					}
				else
					num_files = 0;
			}
			else
				num_files = 0;
		}

		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT %s:%s (%u files) -> %s/\n", verbose > 0 ? "\n" : "",
				testmode ? "WOULD " : "", dict_name, arg, num_files, dest);

		for (uint i = 0; i < num_files; i++)
		{
			const u32 offset = dump_blks[i].off;
			const u32 decomp_size = dump_blks[i].dec;
			const u32 comp_size = dump_blks[i].comp;

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
					// Check Zstandard header first
					if (comp_size >= 4 && IsZSTD (src, comp_size) > 0)
					{
						u8 *decomp = MALLOC (decomp_size);
						if (decomp)
						{
							uint written = 0;
							if (DecodeZSTDpart (decomp, decomp_size, &written, src, comp_size)
								== ERR_OK)
							{
								SaveFile (out_path, 0, 0, decomp, written, 0);
								extracted_count++;
								FREE (decomp);
								continue;
							}
							FREE (decomp);
						}
					}
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
		FREE (dump_blks);

		// Typed chunk pass (NextLevelLibrary layouts): models -> FEDM+GLB,
		// textures -> FEDT+PNG, skeletons -> FEDS, animations/scripts ->
		// text, everything else hash-resolved raw dumps. Best effort and
		// silent on structural surprises; the block dumps above stay.
		if (!testmode)
		{
			nlg_variant_t variant = NLGDetectVariant (raw, (uint)raw_size);
			if (variant != NLG_UNKNOWN && data_raw && data_raw_size)
				ExtractNLGTyped (
					dest, raw, (uint)raw_size, data_raw, data_raw_size, variant, is_compressed);
		}
	}
	else if (is_po)
	{
		// Punch-Out!! Wii Dictionary
		const u32 num_files = rd_be32 (raw + 16);

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
	(void)extracted_count; // tallied for future summary reporting, unused for now

	return ERR_OK;
}
