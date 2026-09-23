// SPDX-License-Identifier: GPL-2.0+
//-----------------------------------------------------------------------------
// Nordcurrent MPT Texture (.mpt)
// Used in Wii titles developed by Nordcurrent (101-in-1 Party Megamix, etc.)
//-----------------------------------------------------------------------------
#include "lib-mpt.h"
#include "lib-image.h"
#include "lib-std.h"

#include <string.h>

bool IsMPT (const u8 *data, size_t size)
{
	if (!data || size < 0x28)
		return false;

	if (memcmp (data, "MPT ", 4) != 0)
		return false;

	// Check version 0x00020100
	const u32 vers = *(const u32 *)(data + 4);
	if (vers != 0x00020100)
		return false;

	const u32 codec = *(const u32 *)(data + 8);
	if (codec != 0x504d4357 && codec != 0x32336957) // "WCMP" or "Wi32"
		return false;

	if (memcmp (data + 0x10, "RGBA", 4) != 0)
		return false;

	const u32 total_size = *(const u32 *)(data + 0x14);
	const u32 data_size = *(const u32 *)(data + 0x24);
	if (size < total_size || total_size != data_size + 0x28)
		return false;

	return true;
}

enumError SaveMPT (Image_t *img, FILE *f, ccp fname, bool overwrite)
{
	if (!img)
		return ERR_INVALID_IFORM;

	Image_t conv_img;
	InitializeIMG (&conv_img);
	Image_t *src = img;

	// MPT expects either CMPR (WCMP) or RGBA32 (Wi32)
	if (src->iform != IMG_CMPR && src->iform != IMG_RGBA32)
	{
		enumError err = ConvertIMG (&conv_img, 1, img, IMG_CMPR, -1);
		if (err)
		{
			ResetIMG (&conv_img);
			return err;
		}
		src = &conv_img;
	}

	const u32 codec = (src->iform == IMG_RGBA32) ? 0x32336957 : 0x504d4357; // "Wi32" or "WCMP"
	const u32 data_size = src->data_size;
	const u32 total_size = data_size + 0x28;

	u8 header[0x28] = { 0 };
	memcpy (header, "MPT ", 4);
	header[4] = 0x00;
	header[5] = 0x01;
	header[6] = 0x02;
	header[7] = 0x00;
	memcpy (header + 8, &codec, 4);
	const u32 extra = 0x18;
	memcpy (header + 12, &extra, 4);
	memcpy (header + 16, "RGBA", 4);

	header[20] = (u8)total_size;
	header[21] = (u8)(total_size >> 8);
	header[22] = (u8)(total_size >> 16);
	header[23] = (u8)(total_size >> 24);

	const u32 width = src->width;
	const u32 height = src->height;
	header[24] = (u8)width;
	header[25] = (u8)(width >> 8);
	header[26] = (u8)(width >> 16);
	header[27] = (u8)(width >> 24);

	header[28] = (u8)height;
	header[29] = (u8)(height >> 8);
	header[30] = (u8)(height >> 16);
	header[31] = (u8)(height >> 24);

	const u32 mips = 1;
	header[32] = (u8)mips;
	header[33] = (u8)(mips >> 8);
	header[34] = (u8)(mips >> 16);
	header[35] = (u8)(mips >> 24);

	header[36] = (u8)data_size;
	header[37] = (u8)(data_size >> 8);
	header[38] = (u8)(data_size >> 16);
	header[39] = (u8)(data_size >> 24);

	enumError res = ERR_OK;
	if (f)
	{
		if (fwrite (header, 1, 0x28, f) != 0x28 ||
		    fwrite (src->data, 1, data_size, f) != data_size)
			res = ERR_WRITE_FAILED;
	}
	else
	{
		u8 *buf = MALLOC (total_size);
		if (!buf)
			res = ERR_OUT_OF_MEMORY;
		else
		{
			memcpy (buf, header, 0x28);
			memcpy (buf + 0x28, src->data, data_size);
			res = SaveFile (fname, 0, overwrite ? FM_OVERWRITE : 0, buf, total_size, 0);
			FREE (buf);
		}
	}

	ResetIMG (&conv_img);
	return res;
}
