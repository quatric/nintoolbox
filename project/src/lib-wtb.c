#include "lib-wtb.h"
#include "lib-bntx.h"
#include "lib-std.h"

// WTB ("WTA" header), little-endian, verified against Switch-Toolbox
// File_Format_Library/FileFormats/Texture/WTB.cs Load()/TextureInfo:
//
//   char magic[3+1];       // "WTB" (4th byte not checked by the reference either)
//   u32  version;
//   u32  num_textures;
//   u32  data_offset_table;
//   u32  data_size_table;
//   u32  unk_table;
//   u32  unk2_table;
//   u32  texture_info_table;
//
//   -- per texture i --
//   u32 data_offset_table[i];   // offset of texture i's pixels
//   u32 data_size_table[i];     // byte size of texture i's pixels
//   TextureInfo texture_info_table[i*56]:
//     char magic[4];    // "XT1 "
//     u32  unknown;
//     u64  image_size;
//     u32  header_size;
//     u32  mip_count;
//     u32  type;        // SurfaceType
//     u32  format;      // GX2-style format code
//     u32  width, height, depth;
//     u32  unknown4;
//     u32  texture_layout;   // low 3 bits: block-height-log2, same as BNTX
//     u32  texture_layout2;
//
// If entry 0's data_offset is 0, every texture's pixels live in a sibling .wtp file at that
// same offset/size instead of this buffer (WTB.cs's UseExternalBinary); we record that instead
// of guessing a filename here.

#define WTB_HDR_SIZE 0x20
#define WTB_INFO_SIZE 56
#define WTB_MAX_TEXTURES 0x4000

bool IsWTB (const u8 *data, size_t size)
{
	return data && size >= 3 && !memcmp (data, "WTB", 3);
}

void ResetWTB (wtb_t *wtb)
{
	if (!wtb)
		return;
	FREE (wtb->textures);
	memset (wtb, 0, sizeof (*wtb));
}

enumError ScanWTB (wtb_t *wtb, const u8 *data, uint size)
{
	if (!wtb || !data || !IsWTB (data, size))
		return EINVAL;
	memset (wtb, 0, sizeof (*wtb));
	if (size < WTB_HDR_SIZE)
		return ERROR0 (ERR_INVALID_DATA, "WTB: file shorter than the fixed header\n");

	const u32 num_textures = rd_le32 (data + 8);
	const u32 data_offset_table = rd_le32 (data + 0x0c);
	const u32 data_size_table = rd_le32 (data + 0x10);
	const u32 texture_info_table = rd_le32 (data + 0x1c);

	if (!num_textures || num_textures > WTB_MAX_TEXTURES)
		return ERROR0 (ERR_INVALID_DATA, "WTB: invalid texture count %u\n", num_textures);

	if ((u64)data_offset_table + (u64)num_textures * 4 > size
		|| (u64)data_size_table + (u64)num_textures * 4 > size
		|| (u64)texture_info_table + (u64)num_textures * WTB_INFO_SIZE > size)
		return ERROR0 (ERR_INVALID_DATA, "WTB: texture tables run past end of file\n");

	wtb_texture_t *tex = CALLOC (num_textures, sizeof (*tex));
	if (!tex)
		return ERR_CANT_CREATE;

	for (u32 i = 0; i < num_textures; i++)
	{
		const u8 *info = data + texture_info_table + (u64)i * WTB_INFO_SIZE;
		if (memcmp (info, "XT1 ", 4))
		{
			FREE (tex);
			return ERROR0 (
				ERR_INVALID_DATA, "WTB: texture %u has an invalid TextureInfo magic\n", i);
		}

		tex[i].data_offset = rd_le32 (data + data_offset_table + (u64)i * 4);
		tex[i].data_size = rd_le32 (data + data_size_table + (u64)i * 4);
		tex[i].mip_count = rd_le32 (info + 20);
		tex[i].type = rd_le32 (info + 24);
		tex[i].format = rd_le32 (info + 28);
		tex[i].width = rd_le32 (info + 32);
		tex[i].height = rd_le32 (info + 36);
		tex[i].depth = rd_le32 (info + 40);
		tex[i].texture_layout = rd_le32 (info + 48);
	}

	wtb->data = data;
	wtb->size = size;
	wtb->n_textures = num_textures;
	wtb->textures = tex;
	wtb->external_data = num_textures == 0 || tex[0].data_offset == 0;

	return ERR_OK;
}

enumError DecodeWTB_Text (FILE *out, const wtb_t *wtb)
{
	if (!out || !wtb)
		return ERR_INVALID_DATA;

	fprintf (out,
		"#WTB\n"
		"texture_count = %u\n"
		"external_data = %s\n\n"
		"[textures]\n",
		wtb->n_textures, wtb->external_data ? "yes (look for a sibling .wtp)" : "no");

	for (uint i = 0; i < wtb->n_textures; i++)
	{
		const wtb_texture_t *t = wtb->textures + i;
		fprintf (out,
			"  [%u] %ux%ux%u, format = 0x%02x, type = %u, mip_count = %u, "
			"block_height_log2 = %u\n"
			"       data_offset = %u, data_size = %u\n",
			i, t->width, t->height, t->depth, t->format, t->type, t->mip_count,
			t->texture_layout & 7, t->data_offset, t->data_size);
	}
	return ERR_OK;
}

enumError DecodeWTB_RGBA (u8 **dest, uint *width, uint *height, const wtb_t *wtb,
	const u8 *image_data, uint image_data_size, uint index)
{
	if (!dest || !width || !height || !wtb || !image_data || index >= wtb->n_textures)
		return EINVAL;
	const wtb_texture_t *t = wtb->textures + index;
	if (!t->width || !t->height)
		return EINVAL;
	if ((u64)t->data_offset + t->data_size > image_data_size)
		return ERROR0 (ERR_INVALID_DATA, "WTB: texture %u's image data is out of bounds\n", index);

	// Translate the GX2-style format code to the equivalent BNTX format word, the same way
	// lib-nutexb.c reuses DecodeBNTX_RGBA for NUTEXB rather than reimplementing block decode.
	uint bntx_fmt = 0, bntx_type = 1;
	switch (t->format)
	{
		case 0x25:
			bntx_fmt = 0x0b;
			break; // R8G8B8A8_UNORM
		case 0x42:
		case 0x46: // ...and _SRGB
			bntx_fmt = 0x1a;
			break; // BC1
		case 0x43:
		case 0x47:
			bntx_fmt = 0x1b;
			break; // BC2
		case 0x44:
		case 0x48:
			bntx_fmt = 0x1c;
			break; // BC3
		case 0x45:
			bntx_fmt = 0x1d;
			break; // BC4_UNORM
		case 0x49:
			bntx_fmt = 0x1d;
			bntx_type = 2;
			break; // BC4_SNORM
		case 0x50:
			bntx_fmt = 0x1f;
			break; // BC6H_UF16
		case 0x4b:
			bntx_fmt = 0x1e;
			break; // BC5_UNORM
		case 0x4c:
			bntx_fmt = 0x1e;
			bntx_type = 2;
			break; // BC5_SNORM
		case 0x4d:
			bntx_fmt = 0x20;
			break; // BC7_UNORM
		case 0x79:
		case 0x87:
			bntx_fmt = 0x2d;
			break; // ASTC 4x4
		case 0x80:
		case 0x8e:
			bntx_fmt = 0x34;
			break; // ASTC 8x8
		default:
			// Other ASTC footprints aren't in the Switch-Toolbox format table this was
			// ported from; reported honestly instead of guessed at.
			return ERROR0 (ERR_INVALID_IFORM, "Unsupported WTB texture format 0x%02x\n", t->format);
	}

	bntx_texture_t bt;
	memset (&bt, 0, sizeof (bt));
	bt.name = "wtb";
	bt.width = t->width;
	bt.height = t->height;
	bt.format = bntx_fmt << 8 | bntx_type;
	bt.tile_mode = 0; // block-linear, same Tegra layout as BNTX
	bt.block_height_log2 = t->texture_layout & 7;
	bt.n_mips = t->mip_count ? t->mip_count : 1;
	bt.data = image_data + t->data_offset;
	bt.data_size = t->data_size;

	bntx_t bntx;
	memset (&bntx, 0, sizeof (bntx));
	bntx.data = image_data;
	bntx.size = image_data_size;
	bntx.n_textures = 1;
	bntx.textures = &bt;

	return DecodeBNTX_RGBA (dest, width, height, &bntx, 0);
}
