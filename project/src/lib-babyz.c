// SPDX-License-Identifier: GPL-2.0+
// "Imagine: Party Babyz" (Wii) format -- see lib-babyz.h for exactly what
// is and is not understood about the container.

#include "lib-babyz.h"
#include "lib-nintendo.h"
#include "lib-image.h"
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

//-----------------------------------------------------------------------------
// ".wsp" sprite/texture -- once decompressed by DecompressBabyzWiz(), the
// payload is a plain GX hardware texture wrapped in a tiny 0x20-byte
// header. Reverse-engineered from the load path FUN_800a3cf4 (main.dol),
// which decompresses the file, byte-swaps the first three LE header words
// to BE in place, and hands them straight to the GX-texobj setup helper
// FUN_8010c2d8 -- the same bit-packing as libogc's GXInitTexObj(), with
// the format field using GX's own hardware texture-format numbering
// (0=I4, 1=I8, 2=IA4, 3=IA8, 4=RGB565, 5=RGB5A3, 6=RGBA32, 8=C4, 9=C8,
// 0xa=C14X2, 0xe=CMPR -- i.e. this codebase's image_format_t 1:1, see
// lib-camtexbank.c's camelot_gx_image_format() for the same mapping in
// another title). Layout (all fields little-endian in the decompressed
// buffer; the game byte-swaps its own copy to BE only after loading):
//   u32 format;   // GX/image_format_t texture format
//   u32 width;    // low 16 bits used (high 16 unused/zero in samples)
//   u32 height;   // low 16 bits used
//   u8  reserved[0x14]; // unused by the confirmed load path
//   <pixel data, standard GX-tiled encoding for 'format'>
// Confirmed against every sample under files/babyz/overlay/hud/ and
// files/babyz/test_menu/: image_format_t geometry always accounts for
// exactly the remaining decompressed bytes (see IsBabyzWsp()).

#define BABYZ_WSP_HEADER_SIZE 0x20

int IsBabyzWsp (const u8 *dec, size_t dec_size, u32 *ret_format, u32 *ret_width, u32 *ret_height)
{
	if (!dec || dec_size < BABYZ_WSP_HEADER_SIZE)
		return 0;

	u32 format = rd_le32 (dec);
	u32 width = rd_le32 (dec + 4) & 0xffff;
	u32 height = rd_le32 (dec + 8) & 0xffff;
	if (!width || !height)
		return 0;

	const ImageGeometry_t *geo = GetImageGeometry ((image_format_t)format);
	if (!geo || !geo->read_support)
		return 0;

	uint img_size = 0;
	CalcImageGeometry ((image_format_t)format, width, height, 0, 0, 0, 0, &img_size);
	if (!img_size || dec_size < (size_t)BABYZ_WSP_HEADER_SIZE + img_size)
		return 0;

	if (ret_format)
		*ret_format = format;
	if (ret_width)
		*ret_width = width;
	if (ret_height)
		*ret_height = height;
	return 1;
}

// Export a decompressed .wsp payload as a PNG at 'out_path'. Wraps the raw
// GX pixel data in a synthetic single-image TPL (same trick used by
// lib-camtexbank.c) so the existing TPL/GX texel decoder does the actual
// pixel unswizzling.
enumError ExportBabyzWspPng (const u8 *dec, size_t dec_size, ccp out_path)
{
	u32 format, width, height;
	if (!IsBabyzWsp (dec, dec_size, &format, &width, &height))
		return ERROR0 (ERR_INVALID_DATA, "Not a decoded Imagine: Party Babyz .wsp texture: %s\n",
			out_path ? out_path : "?");

	uint img_size = 0;
	CalcImageGeometry ((image_format_t)format, width, height, 0, 0, 0, 0, &img_size);

	const u32 tpl_hdr = sizeof (tpl_header_t);
	const u32 tpl_tab = tpl_hdr + sizeof (tpl_imgtab_t);
	const u32 tpl_data = tpl_tab + sizeof (tpl_img_header_t);
	u8 *tpl = CALLOC (tpl_data + img_size, 1);
	if (!tpl)
		return ERROR0 (ERR_OUT_OF_MEMORY, "Out of memory: %s\n", out_path ? out_path : "?");

	write_be32 (tpl, TPL_MAGIC_NUM);
	write_be32 (tpl + 4, 1);
	write_be32 (tpl + 8, tpl_hdr);
	write_be32 (tpl + tpl_hdr, tpl_tab);
	write_be32 (tpl + tpl_hdr + 4, 0);
	write_be16 (tpl + tpl_tab, height);
	write_be16 (tpl + tpl_tab + 2, width);
	write_be32 (tpl + tpl_tab + 4, format);
	write_be32 (tpl + tpl_tab + 8, tpl_data);
	write_be32 (tpl + tpl_tab + 20, 1);
	write_be32 (tpl + tpl_tab + 24, 1);
	memcpy (tpl + tpl_data, dec + BABYZ_WSP_HEADER_SIZE, img_size);

	Image_t img;
	enumError err = AssignIMG (&img, 1, tpl, tpl_data + img_size, 0, false, &be_func, out_path);
	if (err == ERR_OK)
		err = SaveIMG (&img, FF_PNG, 0, 0, out_path, true);
	ResetIMG (&img);
	FREE (tpl);
	return err;
}

//-----------------------------------------------------------------------------
// ".msk" morph/blend mask -- a binary (0/1) stencil bitmap, encoded as a
// standard GX IMG_I8 texture (8x4 tiled blocks -- same hardware texel
// order as .wsp, just always format I8 and with a smaller/simpler
// header). Not traced through main.dol like .wsp was; identified purely
// structurally (all confirmed by direct inspection, not guessed):
//   u32 width;          // LE
//   u32 height;         // LE
//   u8  reserved[16];   // 0xCC filler in every sample (uninitialized
//                       // pointer slots, same pattern as .wik/.eff/.wan/
//                       // .wsn/.tan/.lmc below)
//   <mask data, width*height bytes, GX IMG_I8 tiled (8x4 blocks)>
// Confirmed on every sample under files/babyz/motion/*/*.msk: reserved+
// width*height always accounts for exactly the remaining decompressed
// bytes, every pixel value is 0 or 1 (checked byte-exhaustively), and
// I8-tiled decode of base_mask.msk (512x512) produces a clean body-
// silhouette shape (a naive row-major read instead produces scanline
// garbage, confirming the GX tiling).

#define BABYZ_MSK_HEADER_SIZE 24

int IsBabyzMsk (const u8 *dec, size_t dec_size, u32 *ret_width, u32 *ret_height)
{
	if (!dec || dec_size < BABYZ_MSK_HEADER_SIZE)
		return 0;

	u32 width = rd_le32 (dec);
	u32 height = rd_le32 (dec + 4);
	if (!width || !height || (u64)width * height + BABYZ_MSK_HEADER_SIZE != dec_size)
		return 0;

	if (ret_width)
		*ret_width = width;
	if (ret_height)
		*ret_height = height;
	return 1;
}

// Export a decompressed .msk payload as a PNG at 'out_path' (as an 8-bit
// grayscale image with mask value 1 mapped to white, matching the
// natural "visible/opaque" reading of the stencil).
enumError ExportBabyzMskPng (const u8 *dec, size_t dec_size, ccp out_path)
{
	u32 width, height;
	if (!IsBabyzMsk (dec, dec_size, &width, &height))
		return ERROR0 (ERR_INVALID_DATA, "Not a decoded Imagine: Party Babyz .msk mask: %s\n",
			out_path ? out_path : "?");

	uint img_size = 0;
	CalcImageGeometry (IMG_I8, width, height, 0, 0, 0, 0, &img_size);

	const u32 tpl_hdr = sizeof (tpl_header_t);
	const u32 tpl_tab = tpl_hdr + sizeof (tpl_imgtab_t);
	const u32 tpl_data = tpl_tab + sizeof (tpl_img_header_t);
	u8 *tpl = CALLOC (tpl_data + img_size, 1);
	if (!tpl)
		return ERROR0 (ERR_OUT_OF_MEMORY, "Out of memory: %s\n", out_path ? out_path : "?");

	write_be32 (tpl, TPL_MAGIC_NUM);
	write_be32 (tpl + 4, 1);
	write_be32 (tpl + 8, tpl_hdr);
	write_be32 (tpl + tpl_hdr, tpl_tab);
	write_be32 (tpl + tpl_hdr + 4, 0);
	write_be16 (tpl + tpl_tab, height);
	write_be16 (tpl + tpl_tab + 2, width);
	write_be32 (tpl + tpl_tab + 4, IMG_I8);
	write_be32 (tpl + tpl_tab + 8, tpl_data);
	write_be32 (tpl + tpl_tab + 20, 1);
	write_be32 (tpl + tpl_tab + 24, 1);

	const u8 *mask = dec + BABYZ_MSK_HEADER_SIZE;
	u8 *scaled = CALLOC (width * height, 1);
	for (u32 i = 0; i < width * height; i++)
		scaled[i] = mask[i] ? 0xff : 0;
	memcpy (tpl + tpl_data, scaled, img_size < width * height ? img_size : width * height);
	FREE (scaled);

	Image_t img;
	enumError err = AssignIMG (&img, 1, tpl, tpl_data + img_size, 0, false, &be_func, out_path);
	if (err == ERR_OK)
		err = SaveIMG (&img, FF_PNG, 0, 0, out_path, true);
	ResetIMG (&img);
	FREE (tpl);
	return err;
}
