// SPDX-License-Identifier: GPL-2.0+
#include "lib-hbdf.h"
#include "lib-archive-util.h"
#include "lib-std.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool IsHBDF (const u8 *data, uint size)
{
	if (!data || size < 8)
		return false;
	if (memcmp (data, "HBDF", 4) && memcmp (data, "HSDF", 4))
		return false;
	const u32 fsize = rd_le32 (data + 4);
	if (fsize < 8 || fsize > size)
		return false;
	return true;
}

// Simple LZ77 decompressor for DS HBDF chunks
static u8 *hbdf_decompress_lz77 (const u8 *src, size_t src_len, size_t *out_len)
{
	if (!src || src_len < 4)
		return NULL;
	const size_t declen = (size_t)src[1] | ((size_t)src[2] << 8) | ((size_t)src[3] << 16);
	if (declen == 0 || declen > 16 * 1024 * 1024)
		return NULL;

	u8 *dest = MALLOC (declen);
	if (!dest)
		return NULL;

	size_t sidx = 4;
	size_t didx = 0;
	size_t remaining = declen;

	while (remaining > 0 && sidx < src_len)
	{
		u8 flag_byte = src[sidx++];
		for (int b = 0; b < 8 && remaining > 0; b++)
		{
			if (flag_byte & 0x80)
			{
				if (sidx + 2 > src_len)
					break;
				const u16 pair = ((u16)src[sidx] << 8) | (u16)src[sidx + 1];
				sidx += 2;
				const size_t copy_len = (pair >> 12) + 3;
				const size_t disp = (pair & 0x0FFF) + 1;
				if (disp > didx)
					break;
				size_t cpos = didx - disp;
				for (size_t c = 0; c < copy_len && remaining > 0; c++)
				{
					dest[didx++] = dest[cpos++];
					remaining--;
				}
			}
			else
			{
				if (sidx >= src_len)
					break;
				dest[didx++] = src[sidx++];
				remaining--;
			}
			flag_byte <<= 1;
		}
	}

	if (out_len)
		*out_len = didx;
	return dest;
}

// Reads a NAME sub-block and returns allocated string
static char *hbdf_read_name_block (const u8 *data, size_t size, size_t *pos)
{
	if (*pos + 8 > size)
		return NULL;
	if (memcmp (data + *pos, "NAME", 4))
		return NULL;
	const u32 nsize = rd_le32 (data + *pos + 4);
	if (*pos + 8 + nsize > size)
		return NULL;

	char *str = CALLOC (nsize + 1, 1);
	if (str)
		memcpy (str, data + *pos + 8, nsize);
	*pos += 8 + nsize;
	return str;
}

// Helper to parse TEXS chunk and unpack embedded images / palettes
static void hbdf_unpack_texs (nintendo_sarc_entry_t *out, uint *out_cnt, const u8 *data, size_t texs_size, uint blk_idx)
{
	if (texs_size < 16)
		return;

	// Structure: [magic:4 "TEXS"][size:4][numInfos:2][numImages:2][numPalettes:2][pad:2]
	const u16 num_infos = rd_le16 (data + 8);
	const u16 num_images = rd_le16 (data + 10);
	const u16 num_palettes = rd_le16 (data + 12);

	size_t pos = 16;
	const uint total_subblocks = (uint)num_infos + (uint)num_images + (uint)num_palettes;

	for (uint s = 0; s < total_subblocks && pos + 8 <= texs_size; s++)
	{
		char subtag[5];
		memcpy (subtag, data + pos, 4);
		subtag[4] = '\0';
		const u32 sub_len = rd_le32 (data + pos + 4);
		if (pos + 8 + sub_len > texs_size)
			break;

		const u8 *sub_ptr = data + pos + 8;
		pos += 8;

		if (!strcmp (subtag, "IMGO"))
		{
			// IMGO format: [NAME block] [Format:4][Width:2][Height:2][Params:4][texSize:4][tex4x4Size:4][compFlags:4][ImageData...]
			size_t ipos = 0;
			char *tex_name = hbdf_read_name_block (sub_ptr, sub_len, &ipos);
			if (ipos + 24 <= sub_len)
			{
				const u16 w = rd_le16 (sub_ptr + ipos + 4);
				const u16 h = rd_le16 (sub_ptr + ipos + 6);
				const u32 tex_size = rd_le32 (sub_ptr + ipos + 12);
				const u32 comp_flags = rd_le32 (sub_ptr + ipos + 20);
				const size_t raw_data_pos = ipos + 24;

				if (raw_data_pos + tex_size <= sub_len)
				{
					char entry_name[128];
					snprintf (entry_name, sizeof (entry_name), "%02u_TEXS_%s_%ux%u.bin",
						blk_idx, tex_name && tex_name[0] ? tex_name : "image", w, h);

					if (comp_flags == 1)
					{
						size_t dec_sz = 0;
						u8 *dec = hbdf_decompress_lz77 (sub_ptr + raw_data_pos, tex_size, &dec_sz);
						if (dec)
						{
							OwnedEntryAdd (out, (*out_cnt)++, entry_name, dec, (u32)dec_sz);
							FREE (dec);
						}
						else
						{
							OwnedEntryAdd (out, (*out_cnt)++, entry_name, sub_ptr + raw_data_pos, tex_size);
						}
					}
					else
					{
						OwnedEntryAdd (out, (*out_cnt)++, entry_name, sub_ptr + raw_data_pos, tex_size);
					}
				}
			}
			if (tex_name)
				FREE (tex_name);
		}
		else if (!strcmp (subtag, "PLTO"))
		{
			// PLTO format: [NAME block][size:4][compFlags:4][PaletteData...]
			size_t ppos = 0;
			char *pal_name = hbdf_read_name_block (sub_ptr, sub_len, &ppos);
			if (ppos + 8 <= sub_len)
			{
				const u32 pal_sz = rd_le32 (sub_ptr + ppos);
				const u32 comp_flags = rd_le32 (sub_ptr + ppos + 4);
				const size_t pal_data_pos = ppos + 8;
				if (pal_data_pos + pal_sz <= sub_len)
				{
					char entry_name[128];
					snprintf (entry_name, sizeof (entry_name), "%02u_PLTO_%s.bin",
						blk_idx, pal_name && pal_name[0] ? pal_name : "pal");

					if (comp_flags == 1)
					{
						size_t dec_sz = 0;
						u8 *dec = hbdf_decompress_lz77 (sub_ptr + pal_data_pos, pal_sz, &dec_sz);
						if (dec)
						{
							OwnedEntryAdd (out, (*out_cnt)++, entry_name, dec, (u32)dec_sz);
							FREE (dec);
						}
						else
						{
							OwnedEntryAdd (out, (*out_cnt)++, entry_name, sub_ptr + pal_data_pos, pal_sz);
						}
					}
					else
					{
						OwnedEntryAdd (out, (*out_cnt)++, entry_name, sub_ptr + pal_data_pos, pal_sz);
					}
				}
			}
			if (pal_name)
				FREE (pal_name);
		}
		pos += sub_len;
	}
}

enumError ScanHBDF (nintendo_sarc_entry_t **entries, uint *n_entries, const u8 *data, uint size)
{
	if (!entries || !n_entries || !data || !IsHBDF (data, size))
		return ERR_INVALID_DATA;

	*entries = 0;
	*n_entries = 0;

	// Count maximum potential entries
	uint block_cnt = 0;
	uint pos = 8;
	const u32 total_size = rd_le32 (data + 4);
	const uint limit = total_size <= size ? total_size : size;

	while (pos + 8 <= limit)
	{
		const u32 bsize = rd_le32 (data + pos + 4);
		if (bsize < 8 || pos + bsize > limit)
			break;
		block_cnt += 64; // Allow extra slots for texture/palette sub-blocks
		pos += bsize;
	}

	if (!block_cnt)
		return ERR_NOTHING_TO_DO;

	nintendo_sarc_entry_t *out = CALLOC (block_cnt, sizeof (*out));
	if (!out)
		return ERR_CANT_CREATE;

	uint out_cnt = 0;
	pos = 8;
	uint i = 0;
	while (pos + 8 <= limit)
	{
		char tag[5];
		memcpy (tag, data + pos, 4);
		tag[4] = 0;

		const u32 bsize = rd_le32 (data + pos + 4);
		if (bsize < 8 || pos + bsize > limit)
			break;

		char name[64];
		snprintf (name, sizeof (name), "%02u_%s.bin", i, tag);
		OwnedEntryAdd (out, out_cnt++, name, data + pos, bsize);

		if (!strcmp (tag, "TEXS"))
		{
			hbdf_unpack_texs (out, &out_cnt, data + pos, bsize, i);
		}

		pos += bsize;
		i++;
	}

	*entries = out;
	*n_entries = out_cnt;
	return ERR_OK;
}
