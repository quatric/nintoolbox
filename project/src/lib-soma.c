// SPDX-License-Identifier: GPL-2.0+
// Monolith Soft / Procyon Studio 2D graphics and animation formats
// (.obp / .ntp / .bgp / .dad / .pcs; Soma Bringer, Nintendo DS)

#include "lib-soma.h"
#include "lib-nintendo.h"
#include <string.h>
#include <stdlib.h>

//-----------------------------------------------------------------------------
// (1) ".obp" / ".ntp" OBP1 sprite/object texture

bool IsSomaObp (const u8 *data, size_t size, size_t file_size)
{
	(void)file_size;
	if (!data || size < 16)
		return false;
	if (memcmp (data, "OBP1", 4))
		return false;
	const u16 w = rd_le16 (data + 8);
	const u16 h = rd_le16 (data + 10);
	return w > 0 && w <= 2048 && h > 0 && h <= 2048;
}

enumError DecodeSomaObp_Text (FILE *f, const u8 *data, size_t size, size_t file_size)
{
	(void)file_size;
	if (!f || !data || !IsSomaObp (data, size, file_size))
		return EINVAL;

	const u16 flag_0 = rd_le16 (data + 4);
	const u16 flag_1 = rd_le16 (data + 6);
	const u16 width = rd_le16 (data + 8);
	const u16 height = rd_le16 (data + 10);
	const u16 stride = rd_le16 (data + 12);
	const u16 pal_bytes = rd_le16 (data + 14);

	fprintf (f, "# Soma Bringer OBP1 object/sprite graphic (.obp / .ntp)\n");
	fprintf (f, "magic = \"OBP1\"\n");
	fprintf (f, "width = %u\n", width);
	fprintf (f, "height = %u\n", height);
	fprintf (f, "flags = [0x%04x, 0x%04x]\n", flag_0, flag_1);
	fprintf (f, "stride = %u\n", stride);
	fprintf (f, "palette_bytes = %u  # %u colors (RGB555)\n", pal_bytes, pal_bytes / 2);

	const uint pal_colors = pal_bytes / 2;
	if (pal_colors > 0 && 16 + (u64)pal_bytes <= size)
	{
		for (uint i = 0; i < pal_colors && i < 256; i++)
		{
			const u16 c = rd_le16 (data + 16 + i * 2);
			const uint r = (c & 0x1f) << 3;
			const uint g = ((c >> 5) & 0x1f) << 3;
			const uint b = ((c >> 10) & 0x1f) << 3;
			fprintf (f, "palette[%u] = 0x%04x  # RGB(%u,%u,%u)\n", i, c, r, g, b);
		}
	}

	const u64 pixel_off = 16 + pal_bytes;
	if (pixel_off <= size)
		fprintf (f, "payload_size = %u\n", (uint)(size - pixel_off));

	return ERR_OK;
}

//-----------------------------------------------------------------------------
// (2) ".bgp" BGP1 background graphic

bool IsSomaBgp (const u8 *data, size_t size, size_t file_size)
{
	(void)file_size;
	if (!data || size < 24)
		return false;
	if (memcmp (data, "BGP1", 4))
		return false;
	const u32 fsize = rd_le32 (data + 4);
	if (fsize < 24 || fsize > size)
		return false;
	const u16 w = rd_le16 (data + 8);
	const u16 h = rd_le16 (data + 10);
	return w > 0 && w <= 2048 && h > 0 && h <= 2048;
}

enumError DecodeSomaBgp_Text (FILE *f, const u8 *data, size_t size, size_t file_size)
{
	(void)file_size;
	if (!f || !data || !IsSomaBgp (data, size, file_size))
		return EINVAL;

	const u32 file_sz = rd_le32 (data + 4);
	const u16 width = rd_le16 (data + 8);
	const u16 height = rd_le16 (data + 10);
	const u32 flags = rd_le32 (data + 12);
	const u32 data_offset = rd_le32 (data + 16);
	const u32 pal_size = rd_le32 (data + 20);

	fprintf (f, "# Soma Bringer BGP1 background graphic (.bgp)\n");
	fprintf (f, "magic = \"BGP1\"\n");
	fprintf (f, "file_size = %u\n", file_sz);
	fprintf (f, "width = %u\n", width);
	fprintf (f, "height = %u\n", height);
	fprintf (f, "flags = 0x%08x\n", flags);
	fprintf (f, "data_offset = 0x%x\n", data_offset);
	fprintf (f, "palette_size = %u\n", pal_size);

	return ERR_OK;
}

//-----------------------------------------------------------------------------
// (3) ".dad" Monolith Soft DAD LZSS compressed container

bool IsSomaDad (const u8 *data, size_t size)
{
	if (!data || size < 12)
		return false;
	if (memcmp (data, "DAD\x01", 4) && memcmp (data, "DAD\x02", 4))
		return false;
	const u32 uncomp_sz = rd_le32 (data + 4);
	return uncomp_sz > 0 && uncomp_sz <= 0x08000000; // max 128 MB
}

enumError DecompressSomaDad (u8 *dst, size_t dst_len, const u8 *src, size_t src_len)
{
	if (!dst || !src || src_len < 8)
		return ERR_INVALID_DATA;
	if (memcmp (src, "DAD\x01", 4) && memcmp (src, "DAD\x02", 4))
		return ERR_INVALID_DATA;

	const u32 uncomp_sz = rd_le32 (src + 4);
	if (dst_len < uncomp_sz)
		return ERR_INVALID_DATA;

	const u8 *in_ptr = src + 8;
	const u8 *const in_end = src + src_len;
	u8 *out_ptr = dst;
	u8 *const out_end = dst + uncomp_sz;

	u8 flag_byte = 0;
	uint flags_left = 0;

	while (out_ptr < out_end && in_ptr < in_end)
	{
		if (!flags_left)
		{
			flag_byte = *in_ptr++;
			flags_left = 8;
		}

		const uint bit = flag_byte & 1;
		flag_byte >>= 1;
		flags_left--;

		if (bit)
		{
			// Literal byte
			if (in_ptr >= in_end)
				return ERR_INVALID_DATA;
			*out_ptr++ = *in_ptr++;
		}
		else
		{
			// LZSS reference
			if (in_ptr + 2 > in_end)
				return ERR_INVALID_DATA;
			const u8 b1 = *in_ptr++;
			const u8 b2 = *in_ptr++;
			const uint length = (b2 & 0x0f) + 3;
			const uint dist = (uint)b1 | (((uint)b2 & 0xf0) << 4);

			if (!dist || (size_t)dist > (size_t)(out_ptr - dst))
				return ERR_INVALID_DATA;

			const u8 *match_src = out_ptr - dist;
			for (uint k = 0; k < length && out_ptr < out_end; k++)
				*out_ptr++ = *match_src++;
		}
	}

	return (out_ptr == out_end) ? ERR_OK : ERR_INVALID_DATA;
}

enumError DecompressSomaDad_Alloc (u8 **dst_out, size_t *dst_len_out, const u8 *src, size_t src_len)
{
	if (!dst_out || !dst_len_out || !src || src_len < 8)
		return ERR_INVALID_DATA;
	*dst_out = 0;
	*dst_len_out = 0;

	if (!IsSomaDad (src, src_len))
		return ERR_INVALID_DATA;

	const u32 uncomp_sz = rd_le32 (src + 4);
	u8 *buf = MALLOC (uncomp_sz + 1);
	if (!buf)
		return ERR_OUT_OF_MEMORY;

	const enumError err = DecompressSomaDad (buf, uncomp_sz, src, src_len);
	if (err)
	{
		FREE (buf);
		return err;
	}

	buf[uncomp_sz] = 0;
	*dst_out = buf;
	*dst_len_out = uncomp_sz;
	return ERR_OK;
}

//-----------------------------------------------------------------------------
// (4) ".pcs" Monolith Soft 2D animation / layout sequence container

bool IsSomaPcs (const u8 *data, size_t size)
{
	if (!data || size < 16)
		return false;
	if (memcmp (data, "pcs\0", 4))
		return false;

	const u32 fsize = rd_le32 (data + 4);
	const u32 count = rd_le32 (data + 8);

	// Standard PCS: data[4] is file size, data[8] is count
	if (fsize >= 16 && fsize <= size && count > 0 && count <= 256)
		return true;

	// In some variants (e.g. BFldItm.pcs), data[4] is count and data[8] is 0
	if (fsize > 0 && fsize <= 256 && count == 0 && size >= 16 + (size_t)fsize * 4)
		return true;

	return false;
}

enumError DecodeSomaPcs_Text (FILE *f, const u8 *data, size_t size)
{
	if (!f || !data || !IsSomaPcs (data, size))
		return EINVAL;

	const u32 word1 = rd_le32 (data + 4);
	const u32 word2 = rd_le32 (data + 8);

	uint count = 0;
	if (word1 >= 16 && word1 <= size && word2 > 0 && word2 <= 256)
		count = word2;
	else if (word1 > 0 && word1 <= 256 && word2 == 0)
		count = word1;
	else
		return ERR_INVALID_DATA;

	fprintf (f, "# Monolith Soft PCS 2D Layout/Animation Sequence (.pcs)\n");
	fprintf (f, "magic = \"pcs\\0\"\n");
	fprintf (f, "component_count = %u\n\n", count);

	for (uint i = 0; i < count; i++)
	{
		const u64 off_pos = 16 + (u64)i * 4;
		if (off_pos + 4 > size)
			break;
		const uint chunk_off = rd_le32 (data + off_pos);
		if (chunk_off + 32 > size)
			continue;

		const u8 *pcn = data + chunk_off;
		if (memcmp (pcn, "pcn\0", 4))
			continue;

		const uint pcn_sz = rd_le32 (pcn + 16);
		const uint n_sub = rd_le16 (pcn + 30);

		fprintf (f, "[[component]]\n");
		fprintf (f, "index = %u\n", i);
		fprintf (f, "offset = 0x%x\n", chunk_off);
		fprintf (f, "size = %u\n", pcn_sz);
		fprintf (f, "track_count = %u\n", n_sub);

		for (uint t = 0; t < n_sub; t++)
		{
			const u64 track_off_pos = chunk_off + 32 + (u64)t * 4;
			if (track_off_pos + 4 > size)
				break;
			const uint sub_rel = rd_le32 (data + track_off_pos);
			const uint sub_off = chunk_off + sub_rel;
			if (sub_off + 16 > size)
				continue;

			const u8 *sub = data + sub_off;
			char tag[5] = { 0 };
			memcpy (tag, sub, 4);
			const uint track_sz = rd_le32 (sub + 4);
			const uint k_count = rd_le32 (sub + 12);

			fprintf (f, "  [component.track_%u]\n", t);
			fprintf (f, "  tag = \"%s\"\n", tag);
			fprintf (f, "  size = %u\n", track_sz);
			fprintf (f, "  keyframes = %u\n", k_count);

			if (!memcmp (tag, "pos", 3) || !memcmp (tag, "ang", 3) || !memcmp (tag, "sca", 3))
			{
				for (uint k = 0; k < k_count && sub_off + 16 + (u64)k * 16 + 16 <= size; k++)
				{
					const u8 *kf = sub + 16 + k * 16;
					const double frame = (double)rd_le32 (kf) / 4096.0;
					const double x = (double)(int)rd_le32 (kf + 4) / 4096.0;
					const double y = (double)(int)rd_le32 (kf + 8) / 4096.0;
					const double z = (double)(int)rd_le32 (kf + 12) / 4096.0;
					fprintf (f, "    key[%u] = { frame = %.3f, x = %.3f, y = %.3f, z = %.3f }\n",
						k, frame, x, y, z);
				}
			}
			else if (!memcmp (tag, "col", 3))
			{
				for (uint k = 0; k < k_count && sub_off + 16 + (u64)k * 8 + 8 <= size; k++)
				{
					const u8 *kf = sub + 16 + k * 8;
					const double frame = (double)rd_le32 (kf) / 4096.0;
					const u16 color555 = rd_le16 (kf + 4);
					const u16 alpha = rd_le16 (kf + 6);
					const uint r = (color555 & 0x1f) << 3;
					const uint g = ((color555 >> 5) & 0x1f) << 3;
					const uint b = ((color555 >> 10) & 0x1f) << 3;
					fprintf (f, "    key[%u] = { frame = %.3f, rgb = [%u, %u, %u], alpha = %u }\n",
						k, frame, r, g, b, alpha);
				}
			}
		}
		fprintf (f, "\n");
	}

	return ERR_OK;
}
