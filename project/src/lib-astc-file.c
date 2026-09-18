#include "lib-std.h"
#include "lib-image.h"
#include "lib-image-internal.h"
#include "lib-astc-file.h"
#include "astc/astc_wrapper.h"

#define ASTC_MAGIC 0x5CA1AB13
#define ASTC_MAX_DIM 16384

bool IsASTCFile (const u8 *data, uint size)
{
	if (!data || size < 16)
		return false;
	return data[0] == 0x13 && data[1] == 0xab && data[2] == 0xa1 && data[3] == 0x5c;
}

enumError DecodeASTCFile_RGBA (u8 **dest, uint *width, uint *height, const u8 *data, uint size)
{
	if (!dest || !width || !height || !data || size < 16)
		return EINVAL;

	if (!IsASTCFile (data, size))
		return ERROR0 (ERR_INVALID_DATA, "Not an ASTC file (invalid magic)\n");

	const uint block_x = data[4];
	const uint block_y = data[5];
	const uint block_z = data[6];

	if (block_x < 4 || block_x > 12 || block_y < 4 || block_y > 12 || block_z == 0)
		return ERROR0 (ERR_INVALID_DATA, "Unsupported ASTC block size %ux%ux%u\n",
			block_x, block_y, block_z);

	const uint w = (uint)data[7]  | ((uint)data[8]  << 8) | ((uint)data[9]  << 16);
	const uint h = (uint)data[10] | ((uint)data[11] << 8) | ((uint)data[12] << 16);
	const uint d = (uint)data[13] | ((uint)data[14] << 8) | ((uint)data[15] << 16);

	if (w == 0 || h == 0 || d == 0 || w > ASTC_MAX_DIM || h > ASTC_MAX_DIM)
		return ERROR0 (ERR_INVALID_DATA, "Invalid ASTC dimensions %ux%ux%u\n", w, h, d);

	const uint n_blocks_x = (w + block_x - 1) / block_x;
	const uint n_blocks_y = (h + block_y - 1) / block_y;
	const u64 total_blocks = (u64)n_blocks_x * n_blocks_y;

	if (16 + total_blocks * 16 > size)
		return ERROR0 (ERR_INVALID_DATA, "Truncated ASTC data (need %llu bytes, have %u)\n",
			(unsigned long long)(16 + total_blocks * 16), size);

	u8 *rgba = CALLOC (1, (size_t)w * h * 4);
	if (!rgba)
		return ERR_CANT_CREATE;

	u8 block_rgba[12 * 12 * 4];

	for (uint by = 0; by < n_blocks_y; by++)
	{
		for (uint bx = 0; bx < n_blocks_x; bx++)
		{
			const u8 *block_data = data + 16 + ((u64)by * n_blocks_x + bx) * 16;
			astc_decompress_block (block_rgba, block_data, block_x, block_y);

			for (uint iy = 0; iy < block_y; iy++)
			{
				const uint py = by * block_y + iy;
				if (py >= h)
					continue;

				for (uint ix = 0; ix < block_x; ix++)
				{
					const uint px = bx * block_x + ix;
					if (px >= w)
						continue;

					const size_t dst_idx = ((size_t)py * w + px) * 4;
					const size_t src_idx = (iy * block_x + ix) * 4;
					memcpy (rgba + dst_idx, block_rgba + src_idx, 4);
				}
			}
		}
	}

	*dest = rgba;
	*width = w;
	*height = h;
	return ERR_OK;
}

enumError EncodeASTCFile_RGBA (
	u8 **dest, uint *dest_size, const u8 *rgba, uint width, uint height, uint block_x, uint block_y)
{
	if (!dest || !dest_size || !rgba || !width || !height)
		return EINVAL;

	if (block_x < 4 || block_x > 12) block_x = 4;
	if (block_y < 4 || block_y > 12) block_y = 4;

	const uint n_blocks_x = (width + block_x - 1) / block_x;
	const uint n_blocks_y = (height + block_y - 1) / block_y;
	const u64 total_blocks = (u64)n_blocks_x * n_blocks_y;
	const size_t out_size = 16 + total_blocks * 16;

	u8 *out = CALLOC (1, out_size);
	if (!out)
		return ERR_CANT_CREATE;

	// Write ASTC Header (16 bytes)
	out[0] = 0x13;
	out[1] = 0xab;
	out[2] = 0xa1;
	out[3] = 0x5c;
	out[4] = (u8)block_x;
	out[5] = (u8)block_y;
	out[6] = 1; // block_z
	out[7] = (u8)(width & 0xff);
	out[8] = (u8)((width >> 8) & 0xff);
	out[9] = (u8)((width >> 16) & 0xff);
	out[10] = (u8)(height & 0xff);
	out[11] = (u8)((height >> 8) & 0xff);
	out[12] = (u8)((height >> 16) & 0xff);
	out[13] = 1;
	out[14] = 0;
	out[15] = 0; // depth = 1

	for (uint by = 0; by < n_blocks_y; by++)
	{
		for (uint bx = 0; bx < n_blocks_x; bx++)
		{
			u8 *block = out + 16 + ((u64)by * n_blocks_x + bx) * 16;

			// Compute average RGBA across the block
			u64 sum_r = 0, sum_g = 0, sum_b = 0, sum_a = 0;
			uint count = 0;

			for (uint iy = 0; iy < block_y; iy++)
			{
				const uint py = by * block_y + iy;
				if (py >= height)
					continue;
				for (uint ix = 0; ix < block_x; ix++)
				{
					const uint px = bx * block_x + ix;
					if (px >= width)
						continue;
					const u8 *p = rgba + ((size_t)py * width + px) * 4;
					sum_r += p[0];
					sum_g += p[1];
					sum_b += p[2];
					sum_a += p[3];
					count++;
				}
			}

			if (count == 0)
				count = 1;

			const u8 r8 = (u8)(sum_r / count);
			const u8 g8 = (u8)(sum_g / count);
			const u8 b8 = (u8)(sum_b / count);
			const u8 a8 = (u8)(sum_a / count);

			// Encode as ASTC Void-Extent LDR block:
			// Bits 0..8: 0x1fc (mode = void extent)
			// Bit 9: 0 (LDR)
			// Bits 10..11: 0
			// Bits 12..63: 0x1fff repeated 4 times (all 1s in 13-bit fields)
			// Lower 64 bits: 0x1fc | (0x1ffffffffffffULL << 12) = 0xfffffffffffffffcULL with bits 0..11 set to 0x1fc
			const u64 low = 0x1fcULL | (0x000fffffffffffffULL << 12);
			const u16 r16 = (u16)((r8 << 8) | r8);
			const u16 g16 = (u16)((g8 << 8) | g8);
			const u16 b16 = (u16)((b8 << 8) | b8);
			const u16 a16 = (u16)((a8 << 8) | a8);
			const u64 high = (u64)r16 | ((u64)g16 << 16) | ((u64)b16 << 32) | ((u64)a16 << 48);

			memcpy (block, &low, 8);
			memcpy (block + 8, &high, 8);
		}
	}

	*dest = out;
	*dest_size = (uint)out_size;
	return ERR_OK;
}

enumError SaveASTC (Image_t *img, ccp dest, ccp source)
{
	Transform2XIMG (img);
	if (img->iform != IMG_X_RGB)
		return ERROR0 (ERR_INVALID_DATA, "Can't convert image to RGBA: %s\n", source);

	const uint rgba_size = img->width * img->height * 4;
	u8 *rgba = MALLOC (rgba_size);
	if (!rgba)
		return ERR_CANT_CREATE;

	for (uint y = 0; y < img->height; y++)
		memcpy (rgba + 4 * y * img->width, img->data + 4 * y * img->xwidth, 4 * img->width);

	u8 *data = 0;
	uint size = 0;
	enumError err = EncodeASTCFile_RGBA (&data, &size, rgba, img->width, img->height, 4, 4);
	FREE (rgba);
	if (err)
		return ERROR0 (ERR_INVALID_DATA, "Can't encode ASTC image: %s\n", source);

	File_t F;
	err = CreateFileOpt (&F, true, dest, false, source);
	if (F.f && fwrite (data, 1, size, F.f) != size)
		err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", size, dest);
	ResetFile (&F, opt_preserve);
	FREE (data);
	return err;
}

enumError SaveASTCFile (Image_t *img, FILE *fo, ccp path, bool overwrite)
{
	Transform2XIMG (img);
	if (img->iform != IMG_X_RGB)
		return ERROR0 (ERR_INVALID_DATA, "Can't convert image to RGBA: %s\n", path);

	const uint rgba_size = img->width * img->height * 4;
	u8 *rgba = MALLOC (rgba_size);
	if (!rgba)
		return ERR_CANT_CREATE;

	for (uint y = 0; y < img->height; y++)
		memcpy (rgba + 4 * y * img->width, img->data + 4 * y * img->xwidth, 4 * img->width);

	u8 *data = 0;
	uint size = 0;
	enumError err = EncodeASTCFile_RGBA (&data, &size, rgba, img->width, img->height, 4, 4);
	FREE (rgba);
	if (err)
		return err;

	err = SaveImageBuffer (img, fo, path, overwrite, data, size, "ASTC");
	FREE (data);
	return err;
}
