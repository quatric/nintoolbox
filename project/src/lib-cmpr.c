// Nintendo GameCube/Wii CMPR block-compressed texture format -- a DXT1-style
// S3TC codec (4x4 pixel blocks, two 16-bit colors + a 2-bit-per-pixel index
// grid), used by CMPR/TPLX/BTI/TEX/BREFT and friends.

#include "lib-std.h"
#include "lib-image.h"
#include "lib-image-internal.h"

enumError conv_from_CMPR (Image_t *dest_img, // valid destination
	const Image_t *src_img, // valid source
	image_format_t iform // new image format, only valid IMG_X_*
)
{
	DASSERT (dest_img);
	DASSERT (src_img);
	DASSERT (src_img->iform == IMG_CMPR);
	DASSERT (iform >= IMG_X__MIN && iform <= IMG_X__MAX);

	const uint bits_per_pixel = 4;
	const uint block_width = 8;
	const uint block_height = 8;

	uint h_blocks, v_blocks, img_size;
	enumError err = CalcImageBlock (
		src_img, bits_per_pixel, block_width, block_height, &h_blocks, &v_blocks, &img_size, false);
	if (err)
		return err;

	u16 (*rd16) (const void *data_ptr) = src_img->endian->rd16;

	uint data_size;
	u8 *data = AllocDataIMG (src_img, 4, &data_size);
	u8 *dest1 = data;
	const u8 *src = src_img->data;

	const uint block_size = block_width * 4;
	const uint line_size = EXPAND8 (src_img->width) * 4;
	const uint delta[] = { 0, 16, 4 * line_size, 4 * line_size + 16 };

	while (v_blocks-- > 0)
	{
		u8 *dest2 = dest1;
		uint hblk = h_blocks;
		while (hblk-- > 0)
		{
			uint subb;
			for (subb = 0; subb < 4; subb++)
			{
				u8 palette[4][4], *pal = palette[0];

				const u16 val1 = rd16 (src);
				src += 2;
				*pal++ = cc58[val1 >> 11 & 0x1f];
				*pal++ = cc68[val1 >> 5 & 0x3f];
				*pal++ = cc58[val1 & 0x1f];
				*pal++ = 0xff;

				const u16 val2 = rd16 (src);
				src += 2;
				*pal++ = cc58[val2 >> 11 & 0x1f];
				*pal++ = cc68[val2 >> 5 & 0x3f];
				*pal++ = cc58[val2 & 0x1f];
				*pal++ = 0xff;

				if (val1 > val2)
				{
					*pal++ = (2 * palette[0][0] + palette[1][0]) / 3;
					*pal++ = (2 * palette[0][1] + palette[1][1]) / 3;
					*pal++ = (2 * palette[0][2] + palette[1][2]) / 3;
					*pal++ = 0xff;

					*pal++ = (2 * palette[1][0] + palette[0][0]) / 3;
					*pal++ = (2 * palette[1][1] + palette[0][1]) / 3;
					*pal++ = (2 * palette[1][2] + palette[0][2]) / 3;
					*pal++ = 0xff;
				}
				else
				{
					*pal++ = (palette[0][0] + palette[1][0]) / 2;
					*pal++ = (palette[0][1] + palette[1][1]) / 2;
					*pal++ = (palette[0][2] + palette[1][2]) / 2;
					*pal++ = 0xff;

					*pal++ = 0;
					*pal++ = 0;
					*pal++ = 0;
					*pal++ = 0;
				}

				u8 *dest3 = dest2 + delta[subb];
				uint i;
				for (i = 0; i < 4; i++)
				{
					u8 val = *src++;
					memcpy (dest3 + 12, palette[val & 3], 4);
					val >>= 2;
					memcpy (dest3 + 8, palette[val & 3], 4);
					val >>= 2;
					memcpy (dest3 + 4, palette[val & 3], 4);
					val >>= 2;
					memcpy (dest3, palette[val & 3], 4);
					dest3 += line_size;
				}
			}
			dest2 += block_size;
		}
		dest1 += line_size * block_height;
	}

	DASSERT (src == src_img->data + img_size);
	DASSERT (dest1 <= data + data_size);
	AssignDataRGB (dest_img, src_img, data);
	dest_img->alpha_status = 0;
	return ERR_OK;
}

//-----------------------------------------------------------------------------

static uint calc_distance (const u8 *v1, const u8 *v2)
{
	const int d0 = (int)*v1++ - (int)*v2++;
	const int d1 = (int)*v1++ - (int)*v2++;
	const int d2 = (int)*v1++ - (int)*v2++;
	return abs (d0) + abs (d1) + abs (d2);
}

//-----------------------------------------------------------------------------

static uint calc_distance_square (const u8 *v1, const u8 *v2)
{
	const int d0 = (int)*v1++ - (int)*v2++;
	const int d1 = (int)*v1++ - (int)*v2++;
	const int d2 = (int)*v1++ - (int)*v2++;
	return d0 * d0 + d1 * d1 + d2 * d2;
}

///////////////////////////////////////////////////////////////////////////////

void CMPR_close_info (const u8 *data, // source data
	cmpr_info_t *info, // info data structure
	u8 *dest, // store destination data here (never 0)
	bool fill_info // true: fill info with statistics
)
{
	DASSERT (info);
	DASSERT (dest);

	u8 *pal0 = info->p[0];
	u8 *pal1 = info->p[1];

	if (!info->opaque_count)
	{
		// all pixel are transparent

		memcpy (dest, info->default_vector, sizeof (info->default_vector));
		if (fill_info)
			memset (info->index, 3, sizeof (info->index));
		return;
	}
	else if (info->opaque_count < CMPR_MAX_COL)
	{
		// we have at least one transparent pixel

		u16 p0 = cc85[pal0[0]] << 11 | cc86[pal0[1]] << 5 | cc85[pal0[2]];
		u16 p1 = cc85[pal1[0]] << 11 | cc86[pal1[1]] << 5 | cc85[pal1[2]];
		if (p0 == p1)
		{
			// make p0 < p1
			p0 &= ~0x0020; // modify least significant bit of green
			p1 |= 0x0020;
		}
		else if (p0 > p1)
		{
			u16 ptemp = p0;
			p0 = p1;
			p1 = ptemp;
		}
		DASSERT (p0 < p1);
		write_be16 (dest, p0);
		dest += 2;
		write_be16 (dest, p1);
		dest += 2;

		// re calculate palette colors

		pal0[0] = cc58[p0 >> 11];
		pal0[1] = cc68[p0 >> 5 & 0x3f];
		pal0[2] = cc58[p0 & 0x1f];
		pal0[3] = 0xff;

		pal1[0] = cc58[p1 >> 11];
		pal1[1] = cc68[p1 >> 5 & 0x3f];
		pal1[2] = cc58[p1 & 0x1f];
		pal1[3] = 0xff;

		// calculate median palette value

		u8 *pal2 = info->p[2];
		pal2[0] = (pal0[0] + pal1[0]) / 2;
		pal2[1] = (pal0[1] + pal1[1]) / 2;
		pal2[2] = (pal0[2] + pal1[2]) / 2;
		pal2[3] = 0xff;
		memcpy (info->p[3], pal2, 4);

		uint i;
		for (i = 0; i < 4; i++)
		{
			u8 val = 0;
			uint j;
			for (j = 0; j < 4; j++, data += 4)
			{
				val <<= 2;
				if (data[3] & 0x80)
				{
					const uint d0 = calc_distance (data, pal0);
					const uint d1 = calc_distance (data, pal1);
					const uint d2 = calc_distance (data, pal2);
					if (d1 <= d2)
						val |= d1 <= d0;
					else if (d2 < d0)
						val |= 2;
				}
				else
					val |= 3;
			}
			*dest++ = val;
		}
	}
	else
	{
		// we haven't any transparent pixel

		u16 p0 = cc85[pal0[0]] << 11 | cc86[pal0[1]] << 5 | cc85[pal0[2]];
		u16 p1 = cc85[pal1[0]] << 11 | cc86[pal1[1]] << 5 | cc85[pal1[2]];
		if (p0 == p1)
		{
			// make p0 > p1
			p0 |= 1;
			p1 &= ~(u16)1;
		}
		else if (p0 < p1)
		{
			u16 ptemp = p0;
			p0 = p1;
			p1 = ptemp;
		}
		DASSERT (p0 > p1);
		write_be16 (dest, p0);
		dest += 2;
		write_be16 (dest, p1);
		dest += 2;

		// re calculate palette colors

		pal0[0] = cc58[p0 >> 11];
		pal0[1] = cc68[p0 >> 5 & 0x3f];
		pal0[2] = cc58[p0 & 0x1f];
		pal0[3] = 0xff;

		pal1[0] = cc58[p1 >> 11];
		pal1[1] = cc68[p1 >> 5 & 0x3f];
		pal1[2] = cc58[p1 & 0x1f];
		pal1[3] = 0xff;

		// calculate median palette values

		u8 *pal2 = info->p[2];
		pal2[0] = (2 * pal0[0] + pal1[0]) / 3;
		pal2[1] = (2 * pal0[1] + pal1[1]) / 3;
		pal2[2] = (2 * pal0[2] + pal1[2]) / 3;
		pal2[3] = 0xff;

		u8 *pal3 = info->p[3];
		pal3[0] = (pal0[0] + 2 * pal1[0]) / 3;
		pal3[1] = (pal0[1] + 2 * pal1[1]) / 3;
		pal3[2] = (pal0[2] + 2 * pal1[2]) / 3;
		pal3[3] = 0xff;

		uint i;
		for (i = 0; i < 4; i++)
		{
			u8 val = 0;
			uint j;
			for (j = 0; j < 4; j++, data += 4)
			{
				val <<= 2;
				const uint d0 = calc_distance (data, pal0);
				const uint d1 = calc_distance (data, pal1);
				const uint d2 = calc_distance (data, pal2);
				const uint d3 = calc_distance (data, pal3);
				if (d0 <= d1)
				{
					if (d2 <= d3)
						val |= d0 <= d2 ? 0 : 2;
					else
						val |= d0 <= d3 ? 0 : 3;
				}
				else
				{
					if (d2 <= d3)
						val |= d1 <= d2 ? 1 : 2;
					else
						val |= d1 <= d3 ? 1 : 3;
				}
			}
			*dest++ = val;
		}
	}

	if (fill_info)
	{
		uint i;
		for (i = 0; i < 4; i++)
		{
			info->p[i][0] = cc58[cc85[info->p[i][0]]];
			info->p[i][1] = cc68[cc86[info->p[i][1]]];
			info->p[i][2] = cc58[cc85[info->p[i][2]]];
		}

		memcpy (info->cmpr, dest - 8, 8);
		dest -= 4;
		data -= CMPR_DATA_SIZE;

		uint val = 0;
		for (i = 0; i < CMPR_MAX_COL; i++)
		{
			if (!(i & 3))
				val = *dest++;
			uint dist = 0;
			switch (val & 0xc0)
			{
				case 0x00:
					info->index[i] = 0;
					dist = calc_distance (data, info->p[0]);
					break;

				case 0x40:
					info->index[i] = 1;
					dist = calc_distance (data, info->p[1]);
					break;

				case 0x80:
					info->index[i] = 2;
					dist = calc_distance (data, info->p[2]);
					break;

				case 0xc0:
					info->index[i] = 3;
					if (info->opaque_count == CMPR_MAX_COL)
						dist = calc_distance (data, info->p[3]);
					break;
			}
			info->total_dist += dist;
			info->dist[i] = dist;
			data += 4;
			val <<= 2;
		}
	}
}

//
///////////////////////////////////////////////////////////////////////////////

void CMPR_wiimm (const u8 *data, // source data
	cmpr_info_t *info // info data structure
)
{
	DASSERT (info);
	InitializeCmprInfo (info);
	info->name = "Wiimm";

	typedef struct sum_t
	{
		u8 col[4];
		uint count;
	} sum_t;

	sum_t sum[CMPR_MAX_COL];
	uint n_sum = 0, opaque_count = 0;
	uint col[3] = { 0, 0, 0 };

	const u8 *data_end = data + CMPR_DATA_SIZE;
	const u8 *dat;
	for (dat = data; dat < data_end; dat += 4)
	{
		col[0] += dat[0];
		col[1] += dat[1];
		col[2] += dat[2];

		if (dat[3] & 0x80)
		{
			opaque_count++;
			u8 col[4];
			col[0] = cc58[cc85[dat[0]]];
			col[1] = cc68[cc86[dat[1]]];
			col[2] = cc58[cc85[dat[2]]];

			uint s;
			for (s = 0; s < n_sum; s++)
			{
				if (!memcmp (sum[s].col, col, 3))
				{
					sum[s].count++;
					goto abort_s;
				}
			}
			col[3] = 0xff;
			memcpy (sum[n_sum].col, col, 4);
			sum[n_sum].count = 1;
			n_sum++;
		abort_s:;
		}
	}

	if (!opt_cmpr_valid)
	{
		const u32 rgb
			= col[0] / CMPR_MAX_COL << 16 | col[1] / CMPR_MAX_COL << 8 | col[2] / CMPR_MAX_COL;

		// modify least significant bit of green
		const u16 c1 = RGB_to_RGB565 (rgb) & ~0x0020;
		const u16 c2 = c1 | 0x0020;
		write_be16 (info->default_vector, c1);
		write_be16 (info->default_vector + 2, c2);
	}

	info->opaque_count = opaque_count;
	if (!opaque_count)
		return;

	DASSERT (n_sum);
	if (n_sum < 3)
	{
		memcpy (info->p[0], sum[0].col, 4);
		memcpy (info->p[1], sum[n_sum - 1].col, 4);
		return;
	}

	DASSERT (opaque_count >= 3);
	// HEXDUMP16(0,0,sum,sizeof(sum));

	uint best0 = 0, best1 = 0, max_dist = UINT_MAX;
	if (info->opaque_count < CMPR_MAX_COL)
	{
		// we have transparent points -> 1 middle point

		uint s0;
		for (s0 = 0; s0 < n_sum; s0++)
		{
			u8 *pal0 = sum[s0].col;
			uint s1;
			for (s1 = s0 + 1; s1 < n_sum; s1++)
			{
				u8 *pal1 = sum[s1].col;
				u8 pal2[4];
				pal2[0] = (pal0[0] + pal1[0]) / 2;
				pal2[1] = (pal0[1] + pal1[1]) / 2;
				pal2[2] = (pal0[2] + pal1[2]) / 2;

				uint dist = 0;
				const u8 *dat;
				for (dat = data; dat < data_end && dist < max_dist; dat += 4)
				{
					if (dat[3] & 0x80)
					{
						const uint d0 = calc_distance (dat, pal0);
						const uint d1 = calc_distance (dat, pal1);
						const uint d2 = calc_distance (dat, pal2);
						if (d0 <= d1)
							dist += d0 < d2 ? d0 : d2;
						else
							dist += d1 < d2 ? d1 : d2;
					}
				}
				if (max_dist > dist)
				{
					max_dist = dist;
					best0 = s0;
					best1 = s1;
				}
			}
		}
	}
	else
	{
		// no transparent points -> 2 middle point

		uint s0;
		for (s0 = 0; s0 < n_sum; s0++)
		{
			u8 *pal0 = sum[s0].col;
			uint s1;
			for (s1 = s0 + 1; s1 < n_sum; s1++)
			{
				u8 *pal1 = sum[s1].col;
				u8 pal2[4];
				pal2[0] = (2 * pal0[0] + pal1[0]) / 3;
				pal2[1] = (2 * pal0[1] + pal1[1]) / 3;
				pal2[2] = (2 * pal0[2] + pal1[2]) / 3;
				u8 pal3[4];
				pal3[0] = (pal0[0] + 2 * pal1[0]) / 3;
				pal3[1] = (pal0[1] + 2 * pal1[1]) / 3;
				pal3[2] = (pal0[2] + 2 * pal1[2]) / 3;

				uint dist = 0;
				const u8 *dat;
				for (dat = data; dat < data_end && dist < max_dist; dat += 4)
				{
					const uint d0 = calc_distance (dat, pal0);
					const uint d1 = calc_distance (dat, pal1);
					const uint d2 = calc_distance (dat, pal2);
					const uint d3 = calc_distance (dat, pal3);
					if (d0 <= d1)
					{
						if (d2 <= d3)
							dist += d0 < d2 ? d0 : d2;
						else
							dist += d0 < d3 ? d0 : d3;
					}
					else
					{
						if (d2 <= d3)
							dist += d1 <= d2 ? d1 : d2;
						else
							dist += d1 <= d3 ? d1 : d3;
					}
				}
				if (max_dist > dist)
				{
					max_dist = dist;
					best0 = s0;
					best1 = s1;
				}
			}
		}
	}

	memcpy (info->p[0], sum[best0].col, 4);
	memcpy (info->p[1], sum[best1].col, 4);
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

enumError conv_to_CMPR (Image_t *dest_img, // valid destination
	const Image_t *src_img, // valid source
	palette_format_t pform // wanted palette format => ignored
)
{
	DASSERT (dest_img);
	DASSERT (src_img);
	DASSERT (src_img->iform >= IMG_X__MIN && src_img->iform <= IMG_X__MAX);

	//--- first convert to IMG_X_RGB

	if (src_img->iform != IMG_X_RGB)
	{
		enumError err = ConvertIMG (dest_img, false, src_img, IMG_X_RGB, PAL_INVALID);
		if (err)
			return err;
	}
	else
		CopyIMG (dest_img, false, src_img, true);
	DASSERT (dest_img->iform == IMG_X_RGB);
	NormalizeFrameIMG (dest_img);

	//--- and now convert IMG_X_RGB -> CMPR

	const uint bits_per_pixel = 4;
	const uint block_width = 8;
	const uint block_height = 8;

	uint h_blocks, v_blocks, img_size;
	CalcImageBlock (
		dest_img, bits_per_pixel, block_width, block_height, &h_blocks, &v_blocks, &img_size, true);

	u8 *data = CALLOC (1, img_size);
	u8 *dest = data;
	const u8 *src1 = dest_img->data;

	const uint block_size = block_width * 4;
	DASSERT (EXPAND8 (dest_img->width) == dest_img->xwidth);
	const uint line_size = dest_img->xwidth * 4;
	const uint delta[] = { 0, 16, 4 * line_size, 4 * line_size + 16 };

	while (v_blocks-- > 0)
	{
		const u8 *src2 = src1;
		uint hblk = h_blocks;
		while (hblk-- > 0)
		{
			uint subb;
			for (subb = 0; subb < 4; subb++)
			{
				//---- first collect the data of the 16 pixel

				u8 vector[16 * 4], *vect = vector;
				const u8 *src3 = src2 + delta[subb];
				uint i;
				for (i = 0; i < 4; i++)
				{
					memcpy (vect, src3, 16);
					vect += 16;
					src3 += line_size;
				}
				DASSERT (vect == vector + sizeof (vector));

				//--- analyze data

				cmpr_info_t info;
				CMPR_wiimm (vector, &info);
				PRINT ("CMPR: no=%u, %08x %08x %08x %08x\n", info.opaque_count, *(u32 *)info.p[0],
					*(u32 *)info.p[1], *(u32 *)info.p[2], *(u32 *)info.p[3]);
				CMPR_close_info (vector, &info, dest, false);
				dest += 8;
			}
			src2 += block_size;
		}
		src1 += line_size * block_height;
	}

	DASSERT (dest == data + img_size);
	DASSERT (src1 <= dest_img->data + dest_img->data_size);

	AssignData (dest_img, dest_img, data, img_size, IMG_CMPR);
	return ERR_OK;
}

