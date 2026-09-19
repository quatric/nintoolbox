// BNTX (Switch texture container) -- see lib-bntx.h.
//
// The block-linear address computation is the one given in the Tegra X1
// TRM, matching the reference BNTX tooling; the C here was checked against
// that reference's own swizzle implementation on randomised surfaces.

#include "lib-std.h"
#include "lib-bntx.h"
#include "astc/astc_wrapper.h"
#include "bcn-decoder/bcn_wrapper.h"

#include <math.h>

#define BNTX_MAX_OUTPUT (512u << 20)

static inline u16 brd16 (const u8 *p)
{
	return (u16)p[0] | (u16)p[1] << 8;
}
static inline u32 brd32 (const u8 *p)
{
	return (u32)p[0] | (u32)p[1] << 8 | (u32)p[2] << 16 | (u32)p[3] << 24;
}
static inline u64 brd64 (const u8 *p)
{
	return (u64)brd32 (p) | (u64)brd32 (p + 4) << 32;
}

static inline uint div_round_up (uint n, uint d)
{
	return d ? (n + d - 1) / d : 0;
}
static inline uint round_up (uint x, uint y)
{
	return y ? (((x - 1) | (y - 1)) + 1) : x;
}

//-----------------------------------------------------------------------------
///////////////		block-linear addressing			///////////////
//-----------------------------------------------------------------------------

// Tegra X1 TRM block-linear address for element (x,y).
static u64 addr_block_linear (
	uint x, uint y, uint image_width, uint bytes_per_pixel, u64 base_address, uint block_height)
{
	const uint width_in_gobs = div_round_up (image_width * bytes_per_pixel, 64);

	const u64 gob_address = base_address
		+ (u64)(y / (8 * block_height)) * 512 * block_height * width_in_gobs
		+ (u64)(x * bytes_per_pixel / 64) * 512 * block_height
		+ (u64)((y % (8 * block_height)) / 8) * 512;

	const uint xb = x * bytes_per_pixel;
	return gob_address + (u64)((xb % 64) / 32) * 256 + (u64)((y % 8) / 2) * 64
		+ (u64)((xb % 32) / 16) * 32 + (u64)(y % 2) * 16 + (xb % 16);
}

enumError BntxDeswizzle (u8 **dest, uint *dest_size, const u8 *src, uint src_size, uint width,
	uint height, uint blk_w, uint blk_h, uint bpp, uint tile_mode, uint block_height_log2,
	bool round_pitch)
{
	if (!dest || !src || !bpp || block_height_log2 > 5)
		return EINVAL;

	const uint block_height = 1u << block_height_log2;
	const uint w = div_round_up (width, blk_w);
	const uint h = div_round_up (height, blk_h);
	if (!w || !h)
		return EINVAL;

	const u64 linear_size = (u64)w * h * bpp;
	if (linear_size > BNTX_MAX_OUTPUT)
		return EFBIG;

	u64 pitch, surf_size;
	if (tile_mode == 1)
	{
		pitch = (u64)w * bpp;
		if (round_pitch)
			pitch = round_up ((uint)pitch, 32);
		surf_size = pitch * h;
	}
	else
	{
		pitch = round_up (w * bpp, 64);
		surf_size = pitch * round_up (h, block_height * 8);
	}
	if (surf_size > BNTX_MAX_OUTPUT)
		return EFBIG;

	u8 *out = CALLOC (1, (size_t)linear_size);
	if (!out)
		return ERR_CANT_CREATE;

	for (uint y = 0; y < h; y++)
		for (uint x = 0; x < w; x++)
		{
			const u64 pos = tile_mode == 1 ? (u64)y * pitch + (u64)x * bpp
										   : addr_block_linear (x, y, w, bpp, 0, block_height);
			const u64 pos_linear = ((u64)y * w + x) * bpp;

			// The reference skips any element whose swizzled address runs past
			// the surface; those stay zero rather than aborting the decode.
			if (pos + bpp > surf_size || pos + bpp > src_size)
				continue;
			memcpy (out + pos_linear, src + pos, bpp);
		}

	*dest = out;
	if (dest_size)
		*dest_size = (uint)linear_size;
	return ERR_OK;
}

//-----------------------------------------------------------------------------
///////////////			container parsing		///////////////
//-----------------------------------------------------------------------------

// TextureInfo field offsets, relative to the start of the BRTI block's
// payload (the block header is 16 bytes, the info follows it).
#define TI_FLAGS 0x00
#define TI_TILE_MODE 0x02
#define TI_SWIZZLE 0x04
#define TI_NUM_MIPS 0x06
#define TI_FORMAT 0x0c
#define TI_WIDTH 0x14
#define TI_HEIGHT 0x18
#define TI_LAYOUT 0x24
#define TI_IMAGE_SIZE 0x40
#define TI_ALIGNMENT 0x44
#define TI_COMP_SEL 0x48
#define TI_NAME_ADDR 0x50
#define TI_PTRS_ADDR 0x60
#define TI_SIZE 0x90

void ResetBNTX (bntx_t *bntx)
{
	if (!bntx)
		return;
	if (bntx->textures)
	{
		for (uint i = 0; i < bntx->n_textures; i++)
		{
			FREE (bntx->textures[i].mip_offsets);
			if (bntx->textures[i].user_data)
			{
				for (uint u = 0; u < bntx->textures[i].n_user_data; u++)
				{
					bntx_user_data_t *ud = &bntx->textures[i].user_data[u];
					if ((ud->type == BNTX_UD_STRING && ud->val.str)
						|| (ud->type == BNTX_UD_WSTRING && ud->val.wstr))
					{
						char **list = ud->type == BNTX_UD_STRING ? ud->val.str : ud->val.wstr;
						for (uint k = 0; k < ud->count; k++)
							FREE (list[k]);
						FREE (list);
					}
				}
				FREE (bntx->textures[i].user_data);
			}
		}
		FREE (bntx->textures);
	}
	FREE (bntx->reloc_table.sections);
	FREE (bntx->reloc_table.entries);
	memset (bntx, 0, sizeof (*bntx));
}

// Duplicate one length-prefixed (u16) NUL-terminated UTF-8 string at `addr`.
// Matches BntxLibrary's LoadStrings(count, UTF8): the UserData payload is an
// array of `count` u64 offsets, each pointing at u16 length + bytes + NUL.
static char *bntx_dup_u8_string (const u8 *data, uint size, u64 addr)
{
	if (!addr || addr + 2 > size)
		return NULL;
	const uint len = brd16 (data + addr);
	if ((u64)len + 1 > (u64)size - addr - 2)
		return NULL;
	if (data[addr + 2 + len] != 0)
		return NULL;
	char *out = CALLOC (len + 1, 1);
	if (!out)
		return NULL;
	memcpy (out, data + addr + 2, len);
	return out;
}

// Duplicate one length-prefixed (u16) NUL-terminated UTF-16LE string at
// `addr`, transcoded to UTF-8. Matches BntxLibrary's LoadStrings(Unicode).
static char *bntx_dup_w_string (const u8 *data, uint size, u64 addr)
{
	if (!addr || addr + 2 > size)
		return NULL;
	const uint len = brd16 (data + addr); // UTF-16 code units
	if ((u64)len * 2 + 2 > (u64)size - addr - 2)
		return NULL;
	if (brd16 (data + addr + 2 + (u64)len * 2) != 0)
		return NULL;
	char *out = CALLOC ((size_t)len * 3 + 1, 1);
	if (!out)
		return NULL;
	size_t pos = 0;
	for (uint i = 0; i < len; i++)
	{
		const uint c = brd16 (data + addr + 2 + (u64)i * 2);
		if (c < 0x80)
			out[pos++] = (char)c;
		else if (c < 0x800)
		{
			out[pos++] = (char)(0xC0 | (c >> 6));
			out[pos++] = (char)(0x80 | (c & 0x3F));
		}
		else
		{
			out[pos++] = (char)(0xE0 | (c >> 12));
			out[pos++] = (char)(0x80 | ((c >> 6) & 0x3F));
			out[pos++] = (char)(0x80 | (c & 0x3F));
		}
	}
	return out;
}

enumError ScanBNTX (bntx_t *bntx, const u8 *data, uint size)
{
	if (!bntx || !data || size < 0x40 || memcmp (data, "BNTX", 4))
		return EINVAL;

	// Main header: magic(8) version(4) bom(2) alignShift(1) targetAddrSize(1)
	// fileNameAddr(4) flag(2) firstBlkAddr(2) relocAddr(4) fileSize(4)
	if (brd16 (data + 12) != 0xFEFF)
		return EINVAL; // big-endian BNTX does not occur in practice
	const uint first_blk = brd16 (data + 22);

	// The texture container ("NX  ", "Ounc", "PC  ") follows the 32-byte binary header.
	const uint tc = 0x20;
	if (tc + 0x30 > size)
		return EINVAL;
	(void)first_blk;
	const uint count = brd32 (data + tc + 4);
	const u64 info_ptrs_addr = brd64 (data + tc + 8);
	if (!count || count > 0x10000)
		return EINVAL;
	// Bound the base address before range arithmetic to avoid overflow near UINT64_MAX.
	if (info_ptrs_addr >= size || (u64)count * 8 > size - info_ptrs_addr)
		return EINVAL;

	bntx_texture_t *tex = CALLOC (count, sizeof (*tex));
	if (!tex)
		return ERR_CANT_CREATE;

	uint n = 0;
	for (uint i = 0; i < count; i++)
	{
		const u64 blk = brd64 (data + info_ptrs_addr + i * 8);
		if (blk >= size || 16 + TI_SIZE > size - blk)
			continue;
		if (memcmp (data + blk, "BRTI", 4))
			continue;
		const u8 *ti = data + blk + 16;

		const uint w = brd32 (ti + TI_WIDTH), h = brd32 (ti + TI_HEIGHT);
		if (!w || !h || w > 0x10000 || h > 0x10000)
			continue;

		const u64 name_addr = brd64 (ti + TI_NAME_ADDR);
		ccp name = "texture";
		// Names are length-prefixed (u16) strings in the string table.
		if (name_addr && name_addr < size && size - name_addr > 2)
		{
			const uint len = brd16 (data + name_addr);
			if (len < size - name_addr - 2 && !data[name_addr + 2 + len])
				name = (ccp)(data + name_addr + 2);
		}

		const u64 ptrs_addr = brd64 (ti + TI_PTRS_ADDR);
		if (ptrs_addr >= size || 8 > size - ptrs_addr)
			continue;
		const u64 data_addr = brd64 (data + ptrs_addr);
		const uint image_size = brd32 (ti + TI_IMAGE_SIZE);
		if (!image_size || data_addr >= size || image_size > size - data_addr)
			continue;

		tex[n].name = name;
		tex[n].width = w;
		tex[n].height = h;
		tex[n].dim = ti[1];
		tex[n].flags = ti[TI_FLAGS];
		tex[n].swizzle = brd16 (ti + TI_SWIZZLE);
		tex[n].depth = brd32 (ti + 0x1c);
		if (!tex[n].depth)
			tex[n].depth = 1;
		tex[n].array_count = brd32 (ti + 0x20);
		if (!tex[n].array_count)
			tex[n].array_count = 1;
		tex[n].format = brd32 (ti + TI_FORMAT);
		tex[n].comp_sel = brd32 (ti + TI_COMP_SEL);
		tex[n].tile_mode = brd16 (ti + TI_TILE_MODE);
		tex[n].block_height_log2 = brd32 (ti + TI_LAYOUT) & 7;
		tex[n].alignment = brd32 (ti + TI_ALIGNMENT);
		tex[n].n_mips = brd16 (ti + TI_NUM_MIPS);
		tex[n].data = data + data_addr;
		tex[n].data_size = image_size;

		// Read mip offsets if available
		if (tex[n].n_mips > 1 && ptrs_addr < size && (u64)tex[n].n_mips * 8 <= size - ptrs_addr)
		{
			tex[n].mip_offsets = CALLOC (tex[n].n_mips, sizeof (u64));
			if (tex[n].mip_offsets)
			{
				for (uint m = 0; m < tex[n].n_mips; m++)
				{
					const u64 m_addr = brd64 (data + ptrs_addr + m * 8);
					if (m_addr >= data_addr && m_addr <= size)
						tex[n].mip_offsets[m] = m_addr - data_addr;
				}
			}
		}

		// Read UserData if present (dictionary at 0x88, array at 0x68)
		const u64 ud_dict_addr = brd64 (ti + 0x88);
		const u64 ud_addr = brd64 (ti + 0x68);
		if (ud_dict_addr && ud_dict_addr + 8 <= size && ud_addr && ud_addr < size
			&& !memcmp (data + ud_dict_addr, "_DIC", 4))
		{
			const uint ud_count = brd32 (data + ud_dict_addr + 4);
			if (ud_count > 0 && ud_count <= 256
				&& ud_dict_addr + 8 + (u64)(ud_count + 1) * 16 <= size
				&& ud_addr + (u64)ud_count * 0x40 <= size)
			{
				bntx_user_data_t *uds = CALLOC (ud_count, sizeof (*uds));
				if (uds)
				{
					for (uint u = 0; u < ud_count; u++)
					{
						const u8 *udh = data + ud_addr + u * 0x40;
						const u64 u_name_addr = brd64 (udh + 0x00);
						const u64 u_data_addr = brd64 (udh + 0x08);
						const uint u_cnt = brd32 (udh + 0x10);
						const uint u_type = udh[0x14];

						ccp u_name = "";
						if (u_name_addr && u_name_addr + 2 <= size)
						{
							const uint ulen = brd16 (data + u_name_addr);
							if (ulen < size - u_name_addr - 2 && !data[u_name_addr + 2 + ulen])
								u_name = (ccp)(data + u_name_addr + 2);
						}
						uds[u].name = u_name;
						uds[u].type = (bntx_user_data_type_t)u_type;
						uds[u].count = u_cnt;
						if (u_data_addr && u_data_addr < size)
						{
							if (u_type == BNTX_UD_INT32 && u_data_addr + (u64)u_cnt * 4 <= size)
								uds[u].val.i32 = (const s32 *)(data + u_data_addr);
							else if (u_type == BNTX_UD_SINGLE && u_data_addr + (u64)u_cnt * 4 <= size)
								uds[u].val.f32 = (const float *)(data + u_data_addr);
							else if (u_type == BNTX_UD_BYTE && u_data_addr + u_cnt <= size)
								uds[u].val.bytes = data + u_data_addr;
							else if (u_type == BNTX_UD_STRING && u_cnt <= 256
								&& u_data_addr + (u64)u_cnt * 8 <= size)
							{
								// Legacy BntxLibrary UserDataType.String: payload is
								// `count` u64 offsets to u16-length-prefixed UTF-8
								// strings (LoadStrings(count, UTF8)), not inline.
								char **list = CALLOC (u_cnt ? u_cnt : 1, sizeof (*list));
								if (list)
								{
									for (uint k = 0; k < u_cnt; k++)
									{
										const u64 saddr = brd64 (data + u_data_addr + (u64)k * 8);
										list[k] = bntx_dup_u8_string (data, size, saddr);
										if (!list[k])
											list[k] = CALLOC (1, 1);
									}
									uds[u].val.str = list;
								}
							}
							else if (u_type == BNTX_UD_WSTRING && u_cnt <= 256
								&& u_data_addr + (u64)u_cnt * 8 <= size)
							{
								// Legacy BntxLibrary UserDataType.WString: payload
								// is `count` u64 offsets to u16-length-prefixed
								// UTF-16LE strings (LoadStrings(Unicode)),
								// transcoded here to UTF-8 for C callers.
								char **list = CALLOC (u_cnt ? u_cnt : 1, sizeof (*list));
								if (list)
								{
									for (uint k = 0; k < u_cnt; k++)
									{
										const u64 saddr = brd64 (data + u_data_addr + (u64)k * 8);
										list[k] = bntx_dup_w_string (data, size, saddr);
										if (!list[k])
											list[k] = CALLOC (1, 1);
									}
									uds[u].val.wstr = list;
								}
							}
						}
					}
					tex[n].n_user_data = ud_count;
					tex[n].user_data = uds;
				}
			}
		}

		n++;
	}

	if (!n)
	{
		FREE (tex);
		return EINVAL;
	}
	memset (bntx, 0, sizeof (*bntx));
	bntx->data = data;
	bntx->size = size;
	memcpy (bntx->platform, data + tc, 4);
	bntx->platform[4] = 0;
	bntx->version_micro = data[8];
	bntx->version_minor = data[9];
	bntx->version_major = brd16 (data + 10);
	bntx->textures = tex;
	bntx->n_textures = n;

	// Parse Relocation Table (_RLT) if present
	const u32 rlt_addr = brd32 (data + 24);
	if (rlt_addr && rlt_addr + 16 <= size && !memcmp (data + rlt_addr, "_RLT", 4))
	{
		const u32 sec_count = brd32 (data + rlt_addr + 8);
		if (sec_count > 0 && sec_count <= 256
			&& rlt_addr + 16 + (u64)sec_count * 24 <= size)
		{
			bntx_reloc_section_t *sections = CALLOC (sec_count, sizeof (*sections));
			if (sections)
			{
				uint total_entries = 0;
				for (uint s = 0; s < sec_count; s++)
				{
					const u8 *sp = data + rlt_addr + 16 + s * 24;
					sections[s].pointer = brd64 (sp + 0);
					sections[s].offset = brd32 (sp + 8);
					sections[s].size = (s32)brd32 (sp + 12);
					sections[s].first_entry_index = (s32)brd32 (sp + 16);
					sections[s].entry_count = (s32)brd32 (sp + 20);
					if (sections[s].entry_count > 0)
						total_entries += sections[s].entry_count;
				}
				bntx->reloc_table.offset = rlt_addr;
				bntx->reloc_table.n_sections = sec_count;
				bntx->reloc_table.sections = sections;

				const u64 entries_addr = rlt_addr + 16 + (u64)sec_count * 24;
				if (total_entries > 0 && total_entries <= 65536
					&& entries_addr + (u64)total_entries * 8 <= size)
				{
					bntx_reloc_entry_t *entries = CALLOC (total_entries, sizeof (*entries));
					if (entries)
					{
						for (uint e = 0; e < total_entries; e++)
						{
							const u8 *ep = data + entries_addr + e * 8;
							entries[e].pointers_offset = (s32)brd32 (ep + 0);
							entries[e].array_count = brd16 (ep + 4);
							entries[e].pointer_count = ep[6];
							entries[e].padding_count = ep[7];
						}
						bntx->reloc_table.n_entries = total_entries;
						bntx->reloc_table.entries = entries;
					}
				}
			}
		}
	}

	return ERR_OK;
}

//-----------------------------------------------------------------------------
///////////////			format decoding			///////////////
//-----------------------------------------------------------------------------

static inline u8 expand5b (uint v)
{
	return (u8)((v << 3) | (v >> 2));
}
static inline u8 expand6b (uint v)
{
	return (u8)((v << 2) | (v >> 4));
}

static u8 float_to_u8 (float value)
{
	if (!(value > 0.0f))
		return 0;
	if (value >= 1.0f)
		return 255;
	return (u8)(value * 255.0f + 0.5f);
}

static float half_to_float (u16 value)
{
	const uint exponent = (value >> 10) & 31, mantissa = value & 1023;
	float result;
	if (!exponent)
		result = mantissa ? ldexpf ((float)mantissa, -24) : 0.0f;
	else if (exponent == 31)
		result = mantissa ? 0.0f : INFINITY;
	else
		result = ldexpf (1.0f + (float)mantissa / 1024.0f, (int)exponent - 15);
	return value & 0x8000 ? -result : result;
}

static float unsigned_float_component (uint value, uint mantissa_bits)
{
	const uint mantissa_mask = (1u << mantissa_bits) - 1;
	const uint exponent = (value >> mantissa_bits) & 31, mantissa = value & mantissa_mask;
	if (!exponent)
		return mantissa ? ldexpf ((float)mantissa, 1 - 15 - (int)mantissa_bits) : 0.0f;
	if (exponent == 31)
		return mantissa ? 0.0f : INFINITY;
	return ldexpf (1.0f + (float)mantissa / (1u << mantissa_bits), (int)exponent - 15);
}

// Decodes one BC1 (DXT1) block to 16 RGBA pixels.
//
// Per the D3D/Khronos spec the c0 <= c1 "punch-through" mode -- three colours
// plus a transparent index 3 -- exists only in BC1. BC2 and BC3 always use
// the four-colour interpretation regardless of how c0 and c1 compare, which
// is why they pass bc1_alpha = false. (Some decoders, including the
// texture2ddecoder library used to check this code, apply BC1's rule to BC3
// as well and treat BC1 itself as fully opaque; in the c0 > c1 regime where
// those interpretations coincide, this implementation matches it exactly on
// randomised blocks.)
void decode_bc1_block (const u8 *b, u8 *out, bool bc1_alpha)
{
	const u16 c0 = brd16 (b), c1 = brd16 (b + 2);
	u8 pal[4][4];
	pal[0][0] = expand5b (c0 >> 11);
	pal[0][1] = expand6b ((c0 >> 5) & 63);
	pal[0][2] = expand5b (c0 & 31);
	pal[0][3] = 255;
	pal[1][0] = expand5b (c1 >> 11);
	pal[1][1] = expand6b ((c1 >> 5) & 63);
	pal[1][2] = expand5b (c1 & 31);
	pal[1][3] = 255;

	if (c0 > c1 || !bc1_alpha)
	{
		for (int i = 0; i < 3; i++)
		{
			pal[2][i] = (u8)((2 * pal[0][i] + pal[1][i]) / 3);
			pal[3][i] = (u8)((pal[0][i] + 2 * pal[1][i]) / 3);
		}
		pal[2][3] = pal[3][3] = 255;
	}
	else
	{
		for (int i = 0; i < 3; i++)
			pal[2][i] = (u8)((pal[0][i] + pal[1][i]) / 2);
		pal[2][3] = 255;
		pal[3][0] = pal[3][1] = pal[3][2] = pal[3][3] = 0;
	}

	const u32 bits = brd32 (b + 4);
	for (int i = 0; i < 16; i++)
		memcpy (out + i * 4, pal[(bits >> (2 * i)) & 3], 4);
}

// BC2 (explicit 4-bit alpha) and BC3 (interpolated alpha) share the BC1
// colour half in their second 8 bytes.
void decode_bc2_block (const u8 *b, u8 *out)
{
	decode_bc1_block (b + 8, out, false);
	for (int i = 0; i < 16; i++)
	{
		const u8 byte = b[i / 2];
		const u8 a = (i & 1) ? (byte >> 4) : (byte & 0xF);
		out[i * 4 + 3] = (u8)(a * 17);
	}
}

void decode_bc3_block (const u8 *b, u8 *out)
{
	decode_bc1_block (b + 8, out, false);
	u8 a[8];
	a[0] = b[0];
	a[1] = b[1];
	if (a[0] > a[1])
		for (int i = 0; i < 6; i++)
			a[2 + i] = (u8)(((6 - i) * a[0] + (1 + i) * a[1]) / 7);
	else
	{
		for (int i = 0; i < 4; i++)
			a[2 + i] = (u8)(((4 - i) * a[0] + (1 + i) * a[1]) / 5);
		a[6] = 0;
		a[7] = 255;
	}
	u64 bits = 0;
	for (int i = 0; i < 6; i++)
		bits |= (u64)b[2 + i] << (8 * i);
	for (int i = 0; i < 16; i++)
		out[i * 4 + 3] = a[(bits >> (3 * i)) & 7];
}

void decode_bc4_block (const u8 *b, u8 *out)
{
	u8 a[8];
	a[0] = b[0];
	a[1] = b[1];
	if (a[0] > a[1])
		for (int i = 0; i < 6; i++)
			a[2 + i] = (u8)(((6 - i) * a[0] + (1 + i) * a[1]) / 7);
	else
	{
		for (int i = 0; i < 4; i++)
			a[2 + i] = (u8)(((4 - i) * a[0] + (1 + i) * a[1]) / 5);
		a[6] = 0;
		a[7] = 255;
	}
	u64 bits = 0;
	for (int i = 0; i < 6; i++)
		bits |= (u64)b[2 + i] << (8 * i);
	for (int i = 0; i < 16; i++)
	{
		const u8 v = a[(bits >> (3 * i)) & 7];
		out[i * 4 + 0] = v;
		out[i * 4 + 1] = v;
		out[i * 4 + 2] = v;
		out[i * 4 + 3] = 255;
	}
}

void decode_bc5_block (const u8 *b, u8 *out)
{
	u8 r[8], g[8];
	r[0] = b[0];
	r[1] = b[1];
	if (r[0] > r[1])
		for (int i = 0; i < 6; i++)
			r[2 + i] = (u8)(((6 - i) * r[0] + (1 + i) * r[1]) / 7);
	else
	{
		for (int i = 0; i < 4; i++)
			r[2 + i] = (u8)(((4 - i) * r[0] + (1 + i) * r[1]) / 5);
		r[6] = 0;
		r[7] = 255;
	}
	u64 rbits = 0;
	for (int i = 0; i < 6; i++)
		rbits |= (u64)b[2 + i] << (8 * i);

	const u8 *gb = b + 8;
	g[0] = gb[0];
	g[1] = gb[1];
	if (g[0] > g[1])
		for (int i = 0; i < 6; i++)
			g[2 + i] = (u8)(((6 - i) * g[0] + (1 + i) * g[1]) / 7);
	else
	{
		for (int i = 0; i < 4; i++)
			g[2 + i] = (u8)(((4 - i) * g[0] + (1 + i) * g[1]) / 5);
		g[6] = 0;
		g[7] = 255;
	}
	u64 gbits = 0;
	for (int i = 0; i < 6; i++)
		gbits |= (u64)gb[2 + i] << (8 * i);

	for (int i = 0; i < 16; i++)
	{
		out[i * 4 + 0] = r[(rbits >> (3 * i)) & 7];
		out[i * 4 + 1] = g[(gbits >> (3 * i)) & 7];
		out[i * 4 + 2] = 255;
		out[i * 4 + 3] = 255;
	}
}

static void decode_bc_signed_channel (const u8 *b, u8 *out, uint channel)
{
	int value[8];
	value[0] = (s8)b[0];
	value[1] = (s8)b[1];
	if (value[0] > value[1])
		for (int i = 0; i < 6; i++)
			value[2 + i] = ((6 - i) * value[0] + (1 + i) * value[1]) / 7;
	else
	{
		for (int i = 0; i < 4; i++)
			value[2 + i] = ((4 - i) * value[0] + (1 + i) * value[1]) / 5;
		value[6] = -127;
		value[7] = 127;
	}
	u64 bits = 0;
	for (int i = 0; i < 6; i++)
		bits |= (u64)b[2 + i] << (8 * i);
	for (int i = 0; i < 16; i++)
	{
		int v = value[(bits >> (3 * i)) & 7];
		if (v < -127)
			v = -127;
		out[i * 4 + channel] = (u8)((v + 127) * 255 / 254);
	}
}

void decode_bc4_signed_block (const u8 *b, u8 *out)
{
	memset (out, 0, 64);
	decode_bc_signed_channel (b, out, 0);
	for (int i = 0; i < 16; i++)
	{
		out[i * 4 + 1] = out[i * 4 + 2] = out[i * 4];
		out[i * 4 + 3] = 255;
	}
}

void decode_bc5_signed_block (const u8 *b, u8 *out)
{
	memset (out, 0, 64);
	decode_bc_signed_channel (b, out, 0);
	decode_bc_signed_channel (b + 8, out, 1);
	for (int i = 0; i < 16; i++)
	{
		out[i * 4 + 2] = 255;
		out[i * 4 + 3] = 255;
	}
}

ccp GetBNTXFormatName (uint format)
{
	const uint fmt = (format >> 8) & 0xff;
	const uint type = format & 0xff;
	switch (fmt)
	{
		case 0x02:
			return type == 2 ? "R8_SNORM" : type == 3 ? "R8_UINT" : type == 4 ? "R8_SINT" : "R8_UNORM";
		case 0x03: return "R4G4B4A4_UNORM";
		case 0x05: return "R5G5B5A1_UNORM";
		case 0x06: return "A1B5G5R5_UNORM";
		case 0x07: return "R5G6B5_UNORM";
		case 0x08: return "B5G6R5_UNORM";
		case 0x09:
			return type == 2 ? "R8G8_SNORM" : type == 3 ? "R8G8_UINT" : type == 4 ? "R8G8_SINT" : "R8G8_UNORM";
		case 0x0a:
			return type == 5 ? "R16_FLOAT" : type == 7 ? "Z16_DEPTH" : type == 2 ? "R16_SNORM" : "R16_UNORM";
		case 0x0b:
			return type == 6 ? "R8G8B8A8_SRGB" : type == 2 ? "R8G8B8A8_SNORM" : "R8G8B8A8_UNORM";
		case 0x0c:
			return type == 6 ? "B8G8R8A8_SRGB" : "B8G8R8A8_UNORM";
		case 0x0d: return "R9G9B9E5F_FLOAT";
		case 0x0e: return type == 3 ? "R10G10B10A2_UINT" : "R10G10B10A2_UNORM";
		case 0x0f: return "R11G11B10F_FLOAT";
		case 0x12:
			return type == 5 ? "R16G16_FLOAT" : type == 2 ? "R16G16_SNORM" : "R16G16_UNORM";
		case 0x13: return "D24S8_DEPTH";
		case 0x14:
			return type == 7 ? "D32F_DEPTH" : type == 5 ? "R32_FLOAT" : type == 3 ? "R32_UINT" : "R32_SINT";
		case 0x15:
			return type == 5 ? "R16G16B16A16_FLOAT" : type == 2 ? "R16G16B16A16_SNORM" : "R16G16B16A16_UNORM";
		case 0x16: return "D32FS8_DEPTH";
		case 0x17: return type == 5 ? "R32G32_FLOAT" : "R32G32_UINT";
		case 0x18: return type == 5 ? "R32G32B32_FLOAT" : "R32G32B32_UINT";
		case 0x19: return type == 5 ? "R32G32B32A32_FLOAT" : "R32G32B32A32_UINT";
		case 0x1a: return type == 6 ? "BC1_SRGB" : "BC1_UNORM";
		case 0x1b: return type == 6 ? "BC2_SRGB" : "BC2_UNORM";
		case 0x1c: return type == 6 ? "BC3_SRGB" : "BC3_UNORM";
		case 0x1d: return type == 2 ? "BC4_SNORM" : "BC4_UNORM";
		case 0x1e: return type == 2 ? "BC5_SNORM" : "BC5_UNORM";
		case 0x1f: return type == 10 ? "BC6H_UF16" : "BC6H_SF16";
		case 0x20: return type == 6 ? "BC7_SRGB" : "BC7_UNORM";
		case 0x21: return "EAC_R11_UNORM";
		case 0x22: return "EAC_R11_G11_UNORM";
		case 0x23: return type == 6 ? "ETC1_SRGB" : "ETC1_UNORM";
		case 0x24: return type == 6 ? "ETC2_SRGB" : "ETC2_UNORM";
		case 0x25: return type == 6 ? "ETC2_MASK_SRGB" : "ETC2_MASK_UNORM";
		case 0x26: return type == 6 ? "ETC2_ALPHA_SRGB" : "ETC2_ALPHA_UNORM";
		case 0x27: return "PVRTC1_28PP_UNORM";
		case 0x28: return "PVRTC1_48PP_UNORM";
		case 0x29: return "PVRTC1_ALPHA_28PP_UNORM";
		case 0x2a: return "PVRTC1_ALPHA_48PP_UNORM";
		case 0x2b: return "PVRTC2_ALPHA_28PP_UNORM";
		case 0x2c: return "PVRTC2_ALPHA_48PP_UNORM";
		case 0x2d: return type == 6 ? "ASTC_4x4_SRGB" : "ASTC_4x4_UNORM";
		case 0x2e: return type == 6 ? "ASTC_5x4_SRGB" : "ASTC_5x4_UNORM";
		case 0x2f: return type == 6 ? "ASTC_5x5_SRGB" : "ASTC_5x5_UNORM";
		case 0x30: return type == 6 ? "ASTC_6x5_SRGB" : "ASTC_6x5_UNORM";
		case 0x31: return type == 6 ? "ASTC_6x6_SRGB" : "ASTC_6x6_UNORM";
		case 0x32: return type == 6 ? "ASTC_8x5_SRGB" : "ASTC_8x5_UNORM";
		case 0x33: return type == 6 ? "ASTC_8x6_SRGB" : "ASTC_8x6_UNORM";
		case 0x34: return type == 6 ? "ASTC_8x8_SRGB" : "ASTC_8x8_UNORM";
		case 0x35: return type == 6 ? "ASTC_10x5_SRGB" : "ASTC_10x5_UNORM";
		case 0x36: return type == 6 ? "ASTC_10x6_SRGB" : "ASTC_10x6_UNORM";
		case 0x37: return type == 6 ? "ASTC_10x8_SRGB" : "ASTC_10x8_UNORM";
		case 0x38: return type == 6 ? "ASTC_10x10_SRGB" : "ASTC_10x10_UNORM";
		case 0x39: return type == 6 ? "ASTC_12x10_SRGB" : "ASTC_12x10_UNORM";
		case 0x3a: return type == 6 ? "ASTC_12x12_SRGB" : "ASTC_12x12_UNORM";
		case 0x3b: return "B5G5R5A1_UNORM";
		default: return "UNKNOWN";
	}
}

void DumpStructureBNTX (FILE *out, const bntx_t *bntx, int indent)
{
	if (!out || !bntx)
		return;
	fprintf (out, "%*sBNTX Container: platform '%s', version %u.%u.%u, textures %u\n",
		indent, "", bntx->platform[0] ? bntx->platform : "NX  ",
		bntx->version_major, bntx->version_minor, bntx->version_micro,
		bntx->n_textures);
	for (uint i = 0; i < bntx->n_textures; i++)
	{
		const bntx_texture_t *t = bntx->textures + i;
		fprintf (out, "%*sTexture [%u] '%s':\n", indent + 2, "", i, t->name ? t->name : "");
		fprintf (out, "%*sSize: %ux%u", indent + 4, "", t->width, t->height);
		if (t->depth > 1)
			fprintf (out, "x%u", t->depth);
		if (t->array_count > 1)
			fprintf (out, " (array count %u)", t->array_count);
		fprintf (out, ", mips %u\n", t->n_mips);
		fprintf (out, "%*sFormat: 0x%04x (%s)\n", indent + 4, "", t->format, GetBNTXFormatName (t->format));
		fprintf (out, "%*sTile mode: %u, Block height log2: %u, Swizzle: %u, Flags: %u, Dim: %u\n",
			indent + 4, "", t->tile_mode, t->block_height_log2, t->swizzle, t->flags, t->dim);
		static const char ch_names[6] = "01RGBA";
		fprintf (out, "%*sChannels: %c%c%c%c\n", indent + 4, "",
			ch_names[(t->comp_sel & 0xff) <= 5 ? (t->comp_sel & 0xff) : 2],
			ch_names[((t->comp_sel >> 8) & 0xff) <= 5 ? ((t->comp_sel >> 8) & 0xff) : 3],
			ch_names[((t->comp_sel >> 16) & 0xff) <= 5 ? ((t->comp_sel >> 16) & 0xff) : 4],
			ch_names[((t->comp_sel >> 24) & 0xff) <= 5 ? ((t->comp_sel >> 24) & 0xff) : 5]);
		fprintf (out, "%*sData size: 0x%x (%u bytes), Alignment: %u\n",
			indent + 4, "", t->data_size, t->data_size, t->alignment);
		if (t->n_user_data > 0)
		{
			fprintf (out, "%*sUserData (%u entries):\n", indent + 4, "", t->n_user_data);
			for (uint u = 0; u < t->n_user_data; u++)
			{
				const bntx_user_data_t *ud = t->user_data + u;
				fprintf (out, "%*s'%s': type %u, count %u", indent + 6, "",
					ud->name ? ud->name : "", (uint)ud->type, ud->count);
				if (ud->count > 0)
				{
					if (ud->type == BNTX_UD_INT32 && ud->val.i32)
						fprintf (out, " = %d", (int)ud->val.i32[0]);
					else if (ud->type == BNTX_UD_SINGLE && ud->val.f32)
						fprintf (out, " = %g", (double)ud->val.f32[0]);
					else if (ud->type == BNTX_UD_STRING && ud->val.str && ud->val.str[0])
						fprintf (out, " = \"%s\"", ud->val.str[0]);
					else if (ud->type == BNTX_UD_WSTRING && ud->val.wstr && ud->val.wstr[0])
						fprintf (out, " = \"%s\"", ud->val.wstr[0]);
					else if (ud->type == BNTX_UD_BYTE && ud->val.bytes)
						fprintf (out, " = 0x%02x", ud->val.bytes[0]);
				}
				fprintf (out, "\n");
			}
		}
	}
	if (bntx->reloc_table.n_sections > 0)
	{
		fprintf (out, "%*sRelocation Table (_RLT): offset 0x%x, sections %u, entries %u\n",
			indent + 2, "", bntx->reloc_table.offset,
			bntx->reloc_table.n_sections, bntx->reloc_table.n_entries);
		for (uint s = 0; s < bntx->reloc_table.n_sections; s++)
		{
			const bntx_reloc_section_t *sec = bntx->reloc_table.sections + s;
			fprintf (out, "%*sSection [%u]: off 0x%x, size 0x%x, first entry %d, count %d\n",
				indent + 4, "", s, sec->offset, sec->size,
				sec->first_entry_index, sec->entry_count);
		}
	}
}

// Format identifiers and ASTC footprints follow NintendoSDK / BntxLibrary.
// BC6H/BC7 use K0lb3/texture2ddecoder's MIT decoder; ASTC uses the existing
// Apache-2.0 drawElements-derived decoder in src/astc.
enumError DecodeBNTX_Mip_RGBA (
	u8 **dest, uint *width, uint *height, const bntx_t *bntx, uint index, uint mip_level)
{
	if (!dest || !width || !height || !bntx || index >= bntx->n_textures)
		return EINVAL;
	const bntx_texture_t *t = bntx->textures + index;
	if (mip_level >= t->n_mips)
		return EINVAL;

	const uint w = (t->width >> mip_level) ? (t->width >> mip_level) : 1;
	const uint h = (t->height >> mip_level) ? (t->height >> mip_level) : 1;
	const u8 *src_data = (mip_level > 0 && t->mip_offsets) ? t->data + t->mip_offsets[mip_level] : t->data;
	const uint src_size = (mip_level > 0 && t->mip_offsets && t->mip_offsets[mip_level] < t->data_size)
		? (t->data_size - (uint)t->mip_offsets[mip_level]) : t->data_size;
	const uint bh_log2 = t->block_height_log2 > mip_level ? t->block_height_log2 - mip_level : 0;

	const uint fmt = (t->format >> 8) & 0xFF;
	const uint type = t->format & 0xFF;
	uint bpp = 0, blk_w = 1, blk_h = 1;
	enum
	{
		F_R8,
		F_RG8,
		F_R16,
		F_RGBA8,
		F_BGRA8,
		F_RGB565,
		F_BGR565,
		F_RGB5A1,
		F_BGR5A1,
		F_ABGR1555,
		F_RGBA4,
		F_R9G9B9E5F,
		F_R10G10B10A2,
		F_R11G11B10F,
		F_R16G16,
		F_D24S8,
		F_R32,
		F_R16G16B16A16,
		F_D32FS8,
		F_R32G32,
		F_R32G32B32,
		F_R32G32B32A32,
		F_BC1,
		F_BC2,
		F_BC3,
		F_BC4,
		F_BC5,
		F_BC6,
		F_BC7,
		F_ASTC
	} kind;

	switch (fmt)
	{
		case 0x02:
			bpp = 1;
			kind = F_R8;
			break;
		case 0x03:
			bpp = 2;
			kind = F_RGBA4;
			break;
		case 0x05:
			bpp = 2;
			kind = F_RGB5A1;
			break;
		case 0x06:
			bpp = 2;
			kind = F_ABGR1555;
			break;
		case 0x07:
			bpp = 2;
			kind = F_RGB565;
			break;
		case 0x08:
			bpp = 2;
			kind = F_BGR565;
			break;
		case 0x09:
			bpp = 2;
			kind = F_RG8;
			break;
		case 0x0a:
			bpp = 2;
			kind = F_R16;
			break;
		case 0x0b:
			bpp = 4;
			kind = F_RGBA8;
			break;
		case 0x0c:
			bpp = 4;
			kind = F_BGRA8;
			break;
		case 0x0d:
			bpp = 4;
			kind = F_R9G9B9E5F;
			break;
		case 0x0e:
			bpp = 4;
			kind = F_R10G10B10A2;
			break;
		case 0x0f:
			bpp = 4;
			kind = F_R11G11B10F;
			break;
		case 0x12:
			bpp = 4;
			kind = F_R16G16;
			break;
		case 0x13:
			bpp = 4;
			kind = F_D24S8;
			break;
		case 0x14:
			bpp = 4;
			kind = F_R32;
			break;
		case 0x15:
			bpp = 8;
			kind = F_R16G16B16A16;
			break;
		case 0x16:
			bpp = 8;
			kind = F_D32FS8;
			break;
		case 0x17:
			bpp = 8;
			kind = F_R32G32;
			break;
		case 0x18:
			bpp = 12;
			kind = F_R32G32B32;
			break;
		case 0x19:
			bpp = 16;
			kind = F_R32G32B32A32;
			break;
		case 0x1a:
			bpp = 8;
			blk_w = blk_h = 4;
			kind = F_BC1;
			break;
		case 0x1b:
			bpp = 16;
			blk_w = blk_h = 4;
			kind = F_BC2;
			break;
		case 0x1c:
			bpp = 16;
			blk_w = blk_h = 4;
			kind = F_BC3;
			break;
		case 0x1d:
			bpp = 8;
			blk_w = blk_h = 4;
			kind = F_BC4;
			break;
		case 0x1e:
			bpp = 16;
			blk_w = blk_h = 4;
			kind = F_BC5;
			break;
		case 0x1f:
			bpp = 16;
			blk_w = blk_h = 4;
			kind = F_BC6;
			break;
		case 0x20:
			bpp = 16;
			blk_w = blk_h = 4;
			kind = F_BC7;
			break;
		case 0x2d:
			bpp = 16;
			blk_w = 4;
			blk_h = 4;
			kind = F_ASTC;
			break;
		case 0x2e:
			bpp = 16;
			blk_w = 5;
			blk_h = 4;
			kind = F_ASTC;
			break;
		case 0x2f:
			bpp = 16;
			blk_w = 5;
			blk_h = 5;
			kind = F_ASTC;
			break;
		case 0x30:
			bpp = 16;
			blk_w = 6;
			blk_h = 5;
			kind = F_ASTC;
			break;
		case 0x31:
			bpp = 16;
			blk_w = 6;
			blk_h = 6;
			kind = F_ASTC;
			break;
		case 0x32:
			bpp = 16;
			blk_w = 8;
			blk_h = 5;
			kind = F_ASTC;
			break;
		case 0x33:
			bpp = 16;
			blk_w = 8;
			blk_h = 6;
			kind = F_ASTC;
			break;
		case 0x34:
			bpp = 16;
			blk_w = 8;
			blk_h = 8;
			kind = F_ASTC;
			break;
		case 0x35:
			bpp = 16;
			blk_w = 10;
			blk_h = 5;
			kind = F_ASTC;
			break;
		case 0x36:
			bpp = 16;
			blk_w = 10;
			blk_h = 6;
			kind = F_ASTC;
			break;
		case 0x37:
			bpp = 16;
			blk_w = 10;
			blk_h = 8;
			kind = F_ASTC;
			break;
		case 0x38:
			bpp = 16;
			blk_w = 10;
			blk_h = 10;
			kind = F_ASTC;
			break;
		case 0x39:
			bpp = 16;
			blk_w = 12;
			blk_h = 10;
			kind = F_ASTC;
			break;
		case 0x3a:
			bpp = 16;
			blk_w = 12;
			blk_h = 12;
			kind = F_ASTC;
			break;
		case 0x3b:
			bpp = 2;
			kind = F_BGR5A1;
			break;
		default:
			return ERROR0 (ERR_INVALID_IFORM, "Unsupported BNTX texture format 0x%02x in '%s'\n",
				fmt, t->name);
	}

	u8 *linear = 0;
	uint linear_size = 0;
	enumError err = BntxDeswizzle (&linear, &linear_size, src_data, src_size, w,
		h, blk_w, blk_h, bpp, t->tile_mode, bh_log2, true);
	if (err)
		return err;

	if ((u64)w * h > BNTX_MAX_OUTPUT / 4)
	{
		FREE (linear);
		return EFBIG;
	}
	u8 *rgba = CALLOC (1, (size_t)w * h * 4);
	if (!rgba)
	{
		FREE (linear);
		return ERR_CANT_CREATE;
	}

	if (blk_w == 1)
	{
		for (uint y = 0; y < h; y++)
			for (uint x = 0; x < w; x++)
			{
				const u8 *p = linear + ((size_t)y * w + x) * bpp;
				u8 *d = rgba + 4 * ((size_t)y * w + x);
				switch (kind)
				{
					case F_R8:
					{
						int v = type == 2 ? (s8)p[0] : p[0];
						if (v < -127)
							v = -127;
						const u8 c = type == 2 ? (u8)((v + 127) * 255 / 254) : (u8)v;
						d[0] = d[1] = d[2] = c;
						d[3] = 255;
						break;
					}
					case F_RG8:
						if (type == 2)
						{
							int r = (s8)p[0], g = (s8)p[1];
							if (r < -127)
								r = -127;
							if (g < -127)
								g = -127;
							d[0] = (u8)((r + 127) * 255 / 254);
							d[1] = (u8)((g + 127) * 255 / 254);
						}
						else
						{
							d[0] = p[0];
							d[1] = p[1];
						}
						d[2] = 0;
						d[3] = 255;
						break;
					case F_R16:
					{
						const u16 value = brd16 (p);
						u8 c;
						if (type == 5)
							c = float_to_u8 (half_to_float (value));
						else if (type == 2)
						{
							int v = (s16)value;
							if (v < -32767)
								v = -32767;
							c = (u8)(((s64)v + 32767) * 255 / 65534);
						}
						else
							c = (u8)(((u32)value * 255 + 32767) / 65535);
						d[0] = d[1] = d[2] = c;
						d[3] = 255;
						break;
					}
					case F_RGBA8:
						if (type == 2)
							for (int c = 0; c < 4; c++)
							{
								int v = (s8)p[c];
								if (v < -127)
									v = -127;
								d[c] = (u8)((v + 127) * 255 / 254);
							}
						else
							memcpy (d, p, 4);
						break;
					case F_BGRA8:
						d[0] = p[2];
						d[1] = p[1];
						d[2] = p[0];
						d[3] = p[3];
						break;
					case F_RGB565:
					{
						const u16 c = brd16 (p);
						d[0] = expand5b (c >> 11);
						d[1] = expand6b ((c >> 5) & 63);
						d[2] = expand5b (c & 31);
						d[3] = 255;
						break;
					}
					case F_BGR565:
					{
						const u16 c = brd16 (p);
						d[0] = expand5b (c & 31);
						d[1] = expand6b ((c >> 5) & 63);
						d[2] = expand5b (c >> 11);
						d[3] = 255;
						break;
					}
					case F_RGB5A1:
					{
						const u16 c = brd16 (p);
						d[0] = expand5b (c >> 11);
						d[1] = expand5b ((c >> 6) & 31);
						d[2] = expand5b ((c >> 1) & 31);
						d[3] = (c & 1) ? 255 : 0;
						break;
					}
					case F_BGR5A1:
					{
						const u16 c = brd16 (p);
						d[0] = expand5b ((c >> 1) & 31);
						d[1] = expand5b ((c >> 6) & 31);
						d[2] = expand5b (c >> 11);
						d[3] = (c & 1) ? 255 : 0;
						break;
					}
					case F_ABGR1555:
					{
						const u16 c = brd16 (p);
						d[0] = expand5b (c & 31);
						d[1] = expand5b ((c >> 5) & 31);
						d[2] = expand5b ((c >> 10) & 31);
						d[3] = (c & 0x8000) ? 255 : 0;
						break;
					}
					case F_RGBA4:
					{
						const u16 c = brd16 (p);
						d[0] = (u8)(((c >> 12) & 15) * 17);
						d[1] = (u8)(((c >> 8) & 15) * 17);
						d[2] = (u8)(((c >> 4) & 15) * 17);
						d[3] = (u8)((c & 15) * 17);
						break;
					}
					case F_R9G9B9E5F:
					{
						const u32 v = brd32 (p);
						const int exp = (int)((v >> 27) & 31) - 15 - 9;
						const float scale = ldexpf (1.0f, exp);
						d[0] = float_to_u8 ((float)(v & 0x1ff) * scale);
						d[1] = float_to_u8 ((float)((v >> 9) & 0x1ff) * scale);
						d[2] = float_to_u8 ((float)((v >> 18) & 0x1ff) * scale);
						d[3] = 255;
						break;
					}
					case F_R10G10B10A2:
					{
						const u32 v = brd32 (p);
						d[0] = (u8)(((v & 0x3ff) * 255 + 511) / 1023);
						d[1] = (u8)((((v >> 10) & 0x3ff) * 255 + 511) / 1023);
						d[2] = (u8)((((v >> 20) & 0x3ff) * 255 + 511) / 1023);
						d[3] = (u8)(((v >> 30) & 3) * 85);
						break;
					}
					case F_R11G11B10F:
					{
						const u32 value = brd32 (p);
						d[0] = float_to_u8 (unsigned_float_component (value & 0x7ff, 6));
						d[1] = float_to_u8 (unsigned_float_component ((value >> 11) & 0x7ff, 6));
						d[2] = float_to_u8 (unsigned_float_component ((value >> 22) & 0x3ff, 5));
						d[3] = 255;
						break;
					}
					case F_R16G16:
					{
						const u16 r16 = brd16 (p), g16 = brd16 (p + 2);
						if (type == 5)
						{
							d[0] = float_to_u8 (half_to_float (r16));
							d[1] = float_to_u8 (half_to_float (g16));
						}
						else if (type == 2)
						{
							s16 r = (s16)r16, g = (s16)g16;
							if (r < -32767) r = -32767;
							if (g < -32767) g = -32767;
							d[0] = (u8)(((s64)r + 32767) * 255 / 65534);
							d[1] = (u8)(((s64)g + 32767) * 255 / 65534);
						}
						else
						{
							d[0] = (u8)(((u32)r16 * 255 + 32767) / 65535);
							d[1] = (u8)(((u32)g16 * 255 + 32767) / 65535);
						}
						d[2] = 0;
						d[3] = 255;
						break;
					}
					case F_D24S8:
					{
						const u32 v = brd32 (p);
						const u8 depth = (u8)((v & 0xffffff) >> 16);
						d[0] = d[1] = d[2] = depth;
						d[3] = 255;
						break;
					}
					case F_R32:
					{
						float value;
						memcpy (&value, p, sizeof (value));
						d[0] = d[1] = d[2] = float_to_u8 (value);
						d[3] = 255;
						break;
					}
					case F_R16G16B16A16:
					{
						for (int c = 0; c < 4; c++)
						{
							const u16 v16 = brd16 (p + c * 2);
							if (type == 5)
								d[c] = float_to_u8 (half_to_float (v16));
							else if (type == 2)
							{
								s16 s = (s16)v16;
								if (s < -32767) s = -32767;
								d[c] = (u8)(((s64)s + 32767) * 255 / 65534);
							}
							else
								d[c] = (u8)(((u32)v16 * 255 + 32767) / 65535);
						}
						break;
					}
					case F_D32FS8:
					{
						float f;
						memcpy (&f, p, 4);
						const u8 depth = float_to_u8 (f);
						d[0] = d[1] = d[2] = depth;
						d[3] = 255;
						break;
					}
					case F_R32G32:
					{
						float rf, gf;
						memcpy (&rf, p, 4);
						memcpy (&gf, p + 4, 4);
						d[0] = float_to_u8 (rf);
						d[1] = float_to_u8 (gf);
						d[2] = 0;
						d[3] = 255;
						break;
					}
					case F_R32G32B32:
					{
						float rf, gf, bf;
						memcpy (&rf, p, 4);
						memcpy (&gf, p + 4, 4);
						memcpy (&bf, p + 8, 4);
						d[0] = float_to_u8 (rf);
						d[1] = float_to_u8 (gf);
						d[2] = float_to_u8 (bf);
						d[3] = 255;
						break;
					}
					case F_R32G32B32A32:
					{
						float rf, gf, bf, af;
						memcpy (&rf, p, 4);
						memcpy (&gf, p + 4, 4);
						memcpy (&bf, p + 8, 4);
						memcpy (&af, p + 12, 4);
						d[0] = float_to_u8 (rf);
						d[1] = float_to_u8 (gf);
						d[2] = float_to_u8 (bf);
						d[3] = float_to_u8 (af);
						break;
					}
					default:
						break;
				}
			}
	}
	else if (kind == F_BC6 || kind == F_BC7)
	{
		const int ok = kind == F_BC6
			? szs_decode_bc6 (linear, w, h, type == 2 || type == 0x0b, rgba)
			: szs_decode_bc7 (linear, w, h, rgba);
		if (!ok)
		{
			FREE (rgba);
			FREE (linear);
			return ERR_INVALID_DATA;
		}
	}
	else
	{
		const uint bw = div_round_up (w, blk_w), bh = div_round_up (h, blk_h);
		for (uint by = 0; by < bh; by++)
			for (uint bx = 0; bx < bw; bx++)
			{
				const u8 *blk = linear + ((size_t)by * bw + bx) * bpp;
				u8 px[12 * 12 * 4];
				switch (kind)
				{
					case F_BC1:
						decode_bc1_block (blk, px, true);
						break;
					case F_BC2:
						decode_bc2_block (blk, px);
						break;
					case F_BC3:
						decode_bc3_block (blk, px);
						break;
					case F_BC4:
						type == 2 ? decode_bc4_signed_block (blk, px) : decode_bc4_block (blk, px);
						break;
					case F_BC5:
						type == 2 ? decode_bc5_signed_block (blk, px) : decode_bc5_block (blk, px);
						break;
					case F_ASTC:
						astc_decompress_block (px, blk, blk_w, blk_h);
						break;
					default:
						memset (px, 0, sizeof (px));
						break;
				}
				for (uint iy = 0; iy < blk_h; iy++)
					for (uint ix = 0; ix < blk_w; ix++)
					{
						const uint x = bx * blk_w + ix, y = by * blk_h + iy;
						if (x >= w || y >= h)
							continue;
						memcpy (rgba + 4 * ((size_t)y * w + x), px + 4 * (iy * blk_w + ix), 4);
					}
			}
	}

	// BNTX selectors are 0/1 constants or 2..5 for source R/G/B/A. Applying
	// them after decompression also handles single/dual-channel formats and
	// normal-map channel remaps consistently.
	for (size_t i = 0, count = (size_t)w * h; i < count; i++)
	{
		u8 source[4];
		memcpy (source, rgba + i * 4, 4);
		for (uint channel = 0; channel < 4; channel++)
		{
			uint selector = (t->comp_sel >> (8 * channel)) & 0xff;
			if (!selector)
				selector = channel + 2; // old files may omit identity
			rgba[i * 4 + channel] = selector == 1 ? 255
				: selector >= 2 && selector <= 5  ? source[selector - 2]
												  : 0;
		}
	}

	FREE (linear);
	*dest = rgba;
	*width = w;
	*height = h;
	return ERR_OK;
}

enumError DecodeBNTX_RGBA (u8 **dest, uint *width, uint *height, const bntx_t *bntx, uint index)
{
	return DecodeBNTX_Mip_RGBA (dest, width, height, bntx, index, 0);
}

//-----------------------------------------------------------------------------
///////////////			format encoding			///////////////
//-----------------------------------------------------------------------------

static inline void bwr16 (u8 *p, u16 v)
{
	p[0] = (u8)v;
	p[1] = (u8)(v >> 8);
}
static inline void bwr32 (u8 *p, u32 v)
{
	p[0] = (u8)v;
	p[1] = (u8)(v >> 8);
	p[2] = (u8)(v >> 16);
	p[3] = (u8)(v >> 24);
}
static inline void bwr64 (u8 *p, u64 v)
{
	bwr32 (p, (u32)v);
	bwr32 (p + 4, (u32)(v >> 32));
}

enumError EncodeBNTX_RGBA (
	u8 **dest, uint *dest_size, const u8 *rgba, uint width, uint height, ccp name)
{
	if (!dest || !rgba || !width || !height)
		return EINVAL;

	if (!name || !*name)
		name = "texture";

	uint bh_log2;
	if (height <= 16)
		bh_log2 = 0;
	else if (height <= 32)
		bh_log2 = 1;
	else if (height <= 64)
		bh_log2 = 2;
	else if (height <= 128)
		bh_log2 = 3;
	else
		bh_log2 = 4;

	const uint block_height = 1u << bh_log2;
	const uint bpp = 4;
	const uint pitch = round_up (width * bpp, 64);
	const uint surf_h = round_up (height, block_height * 8);
	const u64 surf_size = (u64)pitch * surf_h;

	if (surf_size > BNTX_MAX_OUTPUT)
		return EFBIG;

	u8 *swizzled = CALLOC (1, (size_t)surf_size);
	if (!swizzled)
		return ERR_CANT_CREATE;

	for (uint y = 0; y < height; y++)
		for (uint x = 0; x < width; x++)
		{
			const u64 pos = addr_block_linear (x, y, width, bpp, 0, block_height);
			if (pos + bpp <= surf_size)
				memcpy (swizzled + pos, rgba + 4 * ((size_t)y * width + x), 4);
		}

	ccp file_name = "output.bntx";
	const size_t file_name_len = strlen (file_name);
	const size_t tex_name_len = strlen (name);

	// String table: entry 0 = "", entry 1 = name, entry 2 = file_name
	// Each string entry: u16 length, string bytes, 0 terminator, 2-byte aligned
	const uint s0_len = 0;
	const uint s0_size = 2 + s0_len + 1 + 1; // 4 bytes (aligned)
	const uint s1_size = round_up (2 + (uint)tex_name_len + 1, 2);
	const uint s2_size = round_up (2 + (uint)file_name_len + 1, 2);

	const uint str_payload_size = 4 + s0_size + s1_size + s2_size; // count (4) + entries
	const uint str_block_size = round_up (16 + str_payload_size, 8); // aligned to 8

	// Dictionary table (_DIC): count (4) + root node (16) + texture node (16) = 36 -> 40 (aligned to 8)
	const uint dic_block_size = 8 + 16 * 2;

	// Layout offsets
	const uint ofs_bin_hdr = 0;
	const uint ofs_bntx_hdr = 0x20;
	const uint ofs_mem_pool = 0x58; // 0x140 bytes
	const uint ofs_tex_ptrs = ofs_mem_pool + 0x140; // 0x198, 8 bytes
	const uint ofs_str = ofs_tex_ptrs + 8; // 0x1a0
	const uint ofs_dic = ofs_str + str_block_size;
	const uint ofs_brti = ofs_dic + dic_block_size;

	// String positions
	const uint abs_str_base = ofs_str + 16 + 4;
	const uint abs_str_empty = abs_str_base;
	const uint abs_str_tex = abs_str_empty + s0_size;
	const uint abs_str_file = abs_str_tex + s1_size;

	// BRTI layout:
	// 16-byte block header
	// 144-byte ResBntxTextureInfo
	// 256-byte texture runtime data
	// 256-byte texture view runtime data
	// 8-byte mip offsets array
	const uint ofs_ti = ofs_brti + 16;
	const uint ofs_tex_rt = ofs_ti + 144;
	const uint ofs_tex_view_rt = ofs_tex_rt + 256;
	const uint ofs_mip_offsets = ofs_tex_view_rt + 256;
	const uint ofs_sec1_end = ofs_mip_offsets + 8;
	const uint brti_block_size = ofs_sec1_end - ofs_brti;
	const uint ofs_sec1_aligned = round_up (ofs_sec1_end, 8);

	// BRTD block is placed immediately before data aligned to 4096:
	// ofs_brtd_data % 4096 == 0, and ofs_brtd = ofs_brtd_data - 16
	const uint ofs_brtd_data = round_up (ofs_sec1_aligned + 16, 4096);
	const uint ofs_brtd = ofs_brtd_data - 16;
	const uint ofs_sec2_start = ofs_brtd;
	const uint ofs_sec2_end = ofs_brtd_data + (uint)surf_size;

	// Relocation table placed at 4096 alignment following BRTD
	const uint ofs_rlt = round_up (ofs_sec2_end, 4096);

	const uint n_sec1_entries = 8;
	const uint n_sec2_entries = 2;
	const uint total_rlt_entries = n_sec1_entries + n_sec2_entries;
	const uint rlt_size = 16 + 2 * 24 + total_rlt_entries * 8;
	const uint total_size = ofs_rlt + rlt_size;

	if (total_size > BNTX_MAX_OUTPUT)
	{
		FREE (swizzled);
		return EFBIG;
	}

	u8 *buf = CALLOC (1, (size_t)total_size);
	if (!buf)
	{
		FREE (swizzled);
		return ERR_CANT_CREATE;
	}

	// 1. Binary Header (0x00)
	memcpy (buf, "BNTX\0\0\0\0", 8);
	buf[0x08] = 0; // micro
	buf[0x09] = 0; // minor
	bwr16 (buf + 0x0a, 4); // major = 4
	bwr16 (buf + 0x0c, 0xfeff); // BOM
	buf[0x0e] = 12; // align shift (4096)
	buf[0x0f] = 64; // target addr size
	bwr32 (buf + 0x10, abs_str_file + 2); // NameOffset (points directly to string content)
	bwr16 (buf + 0x14, 0); // Flag
	bwr16 (buf + 0x16, (u16)ofs_str); // BlockOffset (points to first block _STR)
	bwr32 (buf + 0x18, ofs_rlt); // RelocationTableOffset
	bwr32 (buf + 0x1c, total_size); // FileSize

	// 2. BNTX Header (0x20)
	memcpy (buf + ofs_bntx_hdr, "NX  ", 4);
	bwr32 (buf + ofs_bntx_hdr + 4, 1); // TextureCount
	bwr64 (buf + ofs_bntx_hdr + 8, ofs_tex_ptrs); // TextureInfoPointer
	bwr64 (buf + ofs_bntx_hdr + 16, ofs_brtd); // TextureDataPointer
	bwr64 (buf + ofs_bntx_hdr + 24, ofs_dic); // TextureDictionaryPointer
	bwr64 (buf + ofs_bntx_hdr + 32, ofs_mem_pool); // MemoryPoolPointer

	// 3. Texture Pointers array
	bwr64 (buf + ofs_tex_ptrs, ofs_brti);

	// 4. _STR Block
	memcpy (buf + ofs_str, "_STR", 4);
	bwr32 (buf + ofs_str + 4, str_block_size);
	bwr64 (buf + ofs_str + 8, str_block_size);
	bwr32 (buf + ofs_str + 16, 2); // 2 non-empty strings (or count - 1)

	// String 0: ""
	bwr16 (buf + abs_str_empty, 0);
	buf[abs_str_empty + 2] = 0;

	// String 1: name
	bwr16 (buf + abs_str_tex, (u16)tex_name_len);
	memcpy (buf + abs_str_tex + 2, name, tex_name_len);
	buf[abs_str_tex + 2 + tex_name_len] = 0;

	// String 2: file_name
	bwr16 (buf + abs_str_file, (u16)file_name_len);
	memcpy (buf + abs_str_file + 2, file_name, file_name_len);
	buf[abs_str_file + 2 + file_name_len] = 0;

	// 5. _DIC Block
	memcpy (buf + ofs_dic, "_DIC", 4);
	bwr32 (buf + ofs_dic + 4, 1); // count = 1
	// Node 0 (root): ref = 0xffffffff, left = 1, right = 0, name_ptr = abs_str_empty
	bwr32 (buf + ofs_dic + 8, 0xffffffff);
	bwr16 (buf + ofs_dic + 12, 1);
	bwr16 (buf + ofs_dic + 14, 0);
	bwr64 (buf + ofs_dic + 16, abs_str_empty);
	// Node 1: ref = 1, left = 0, right = 1, name_ptr = abs_str_tex
	bwr32 (buf + ofs_dic + 24, 1);
	bwr16 (buf + ofs_dic + 28, 0);
	bwr16 (buf + ofs_dic + 30, 1);
	bwr64 (buf + ofs_dic + 32, abs_str_tex);

	// 6. BRTI Block
	memcpy (buf + ofs_brti, "BRTI", 4);
	bwr32 (buf + ofs_brti + 4, brti_block_size);
	bwr64 (buf + ofs_brti + 8, brti_block_size);

	// ResBntxTextureInfo at ofs_ti
	u8 *ti = buf + ofs_ti;
	ti[0] = 0; // Flags
	ti[1] = 2; // Dim = 2D
	bwr16 (ti + 0x02, 0); // TileMode
	bwr16 (ti + 0x04, 0); // Swizzle
	bwr16 (ti + 0x06, 1); // MipCount = 1
	bwr32 (ti + 0x08, 1); // SampleCount = 1
	bwr32 (ti + 0x0c, 0x0b01); // ImageFormat = RGBA8_UNORM
	bwr32 (ti + 0x10, 0x20); // GpuAccessFlags = Texture
	bwr32 (ti + 0x14, width);
	bwr32 (ti + 0x18, height);
	bwr32 (ti + 0x1c, 1); // Depth
	bwr32 (ti + 0x20, 1); // ArrayCount
	bwr32 (ti + 0x24, bh_log2); // TextureLayout
	bwr32 (ti + 0x28, 2); // TextureLayout2
	bwr32 (ti + 0x40, (u32)surf_size); // ImageSize
	bwr32 (ti + 0x44, 512); // Alignment
	bwr32 (ti + 0x48, 0x05040302); // Channels R,G,B,A (2,3,4,5)
	bwr64 (ti + 0x50, abs_str_tex); // NameOffset
	bwr64 (ti + 0x58, ofs_bntx_hdr); // BntxHeaderOffset (0x20)
	bwr64 (ti + 0x60, ofs_mip_offsets); // DataPointersOffset
	bwr64 (ti + 0x68, 0); // UserDataOffset
	bwr64 (ti + 0x70, ofs_tex_rt); // TexturePointer
	bwr64 (ti + 0x78, ofs_tex_view_rt); // TextureViewPointer
	bwr64 (ti + 0x80, 0); // DescSlotOffset
	bwr64 (ti + 0x88, 0); // UserDataDictionaryOffset

	// Mip offset pointer
	bwr64 (buf + ofs_mip_offsets, ofs_brtd_data);

	// 7. BRTD Block
	const uint brtd_block_size = 16 + (uint)surf_size;
	memcpy (buf + ofs_brtd, "BRTD", 4);
	bwr32 (buf + ofs_brtd + 4, brtd_block_size);
	bwr64 (buf + ofs_brtd + 8, brtd_block_size);
	memcpy (buf + ofs_brtd_data, swizzled, (size_t)surf_size);
	FREE (swizzled);

	// 8. Relocation Table (_RLT)
	u8 *rlt = buf + ofs_rlt;
	memcpy (rlt, "_RLT", 4);
	bwr32 (rlt + 4, ofs_rlt);
	bwr32 (rlt + 8, 2); // 2 sections
	bwr32 (rlt + 12, 0); // padding

	// Section 0
	bwr64 (rlt + 16, 0); // pointer
	bwr32 (rlt + 24, 0); // offset
	bwr32 (rlt + 28, ofs_sec2_start); // size
	bwr32 (rlt + 32, 0); // first entry
	bwr32 (rlt + 36, n_sec1_entries); // entry count

	// Section 1
	bwr64 (rlt + 40, 0); // pointer
	bwr32 (rlt + 48, ofs_sec2_start); // offset
	bwr32 (rlt + 52, ofs_rlt - ofs_sec2_start); // size
	bwr32 (rlt + 56, n_sec1_entries); // first entry
	bwr32 (rlt + 60, n_sec2_entries); // entry count

	// Relocation Entries
	u8 *ep = rlt + 16 + 2 * 24;
	struct {
		s32 ofs;
		u16 arr;
		u8 pcnt;
		u8 padc;
	} entries[10] = {
		{ 40, 2, 1, 1 },
		{ 64, 1, 1, 0 },
		{ (s32)ofs_tex_ptrs, 1, 1, 0 },
		{ (s32)ofs_dic + 16, 2, 1, 1 },
		{ (s32)ofs_ti + 0x50, 1, 3, 0 },
		{ (s32)ofs_ti + 0x50 + 24, 1, 1, 0 },
		{ (s32)ofs_ti + 0x50 + 32, 1, 2, 0 },
		{ (s32)ofs_ti + 0x50 + 56, 1, 1, 0 },
		{ 48, 1, 1, 0 },
		{ (s32)ofs_mip_offsets, 1, 1, 0 }
	};

	for (uint e = 0; e < total_rlt_entries; e++)
	{
		bwr32 (ep + e * 8 + 0, (u32)entries[e].ofs);
		bwr16 (ep + e * 8 + 4, entries[e].arr);
		ep[e * 8 + 6] = entries[e].pcnt;
		ep[e * 8 + 7] = entries[e].padc;
	}

	*dest = buf;
	if (dest_size)
		*dest_size = total_size;
	return ERR_OK;
}

//-----------------------------------------------------------------------------
///////////////		block-preserving DDS -> BNTX encoding		///////////////
//-----------------------------------------------------------------------------
//
// SourceToBinaryCmd `-bntx` parity: that tool builds a Switch BNTX container
// directly from a DDS file's native blocks (FromDDS + SwizzleSurfaceMipMaps
// in Source2Binary/FileFormats/Nintendo/BNTX.cs), preserving the block
// compression, the mip chain and the sRGB variant instead of decoding to
// RGBA8 first. wimgt's SaveBNTX path could only emit a single RGBA8 texture,
// so every BC DDS source was flattened through DecodeDDS_RGBA.
//
// EncodeBNTX_FromDDS closes that gap: it parses the DDS header (legacy
// FourCC codes as well as the DX10 extension), maps the payload to a BNTX
// format word, swizzles each mip into the Tegra block-linear layout with the
// same block-height derivation as EncodeBNTX_RGBA, and writes a standard
// single-texture BNTX container with the full mip chain.
//
// DDS variants outside the directly storable set (uncompressed pixel formats
// other than RGBA8, volume/array textures, ...) decline with
// ERR_NOTHING_TO_DO so the caller can fall back to the RGBA8 path.

// DDS FourCC codes (see Source2Binary/FileFormats/Generic/DDS/DDS.cs and
// lib-dds.c).
#define BNTX_DDS_FOURCC_DXT1 0x31545844
#define BNTX_DDS_FOURCC_DXT3 0x33545844
#define BNTX_DDS_FOURCC_DXT5 0x35545844
#define BNTX_DDS_FOURCC_ATI1 0x31495441
#define BNTX_DDS_FOURCC_BC4U 0x55344342
#define BNTX_DDS_FOURCC_BC4S 0x53344342
#define BNTX_DDS_FOURCC_ATI2 0x32495441
#define BNTX_DDS_FOURCC_BC5U 0x55354342
#define BNTX_DDS_FOURCC_BC5S 0x53354342
#define BNTX_DDS_FOURCC_DX10 0x30315844

// Maps a DDS payload description to a BNTX format word and its native
// storage layout. IS_DX10 selects the DXGI_FORMAT table (DXGI_FMT), otherwise
// the legacy FourCC table (FOURCC). Returns false for payloads with no direct
// BNTX representation.
static bool dds_to_bntx_format (u32 fourcc, u32 dxgi_fmt, bool is_dx10,
	uint *bntx_fmt, uint *bpp, uint *blk_w, uint *blk_h)
{
	uint fmt = 0, b = 0, bw = 4, bh = 4;
	if (!is_dx10)
	{
		switch (fourcc)
		{
			case BNTX_DDS_FOURCC_DXT1: fmt = 0x1a01; b = 8; break;
			case BNTX_DDS_FOURCC_DXT3: fmt = 0x1b01; b = 16; break;
			case BNTX_DDS_FOURCC_DXT5: fmt = 0x1c01; b = 16; break;
			case BNTX_DDS_FOURCC_ATI1:
			case BNTX_DDS_FOURCC_BC4U: fmt = 0x1d01; b = 8; break;
			case BNTX_DDS_FOURCC_BC4S: fmt = 0x1d02; b = 8; break;
			case BNTX_DDS_FOURCC_ATI2:
			case BNTX_DDS_FOURCC_BC5U: fmt = 0x1e01; b = 16; break;
			case BNTX_DDS_FOURCC_BC5S: fmt = 0x1e02; b = 16; break;
			default: return false;
		}
	}
	else
	{
		switch (dxgi_fmt)
		{
			case 70: // BC1_TYPELESS
			case 71: fmt = 0x1a01; b = 8; break; // BC1_UNORM
			case 72: fmt = 0x1a06; b = 8; break; // BC1_SRGB
			case 73: // BC2_TYPELESS
			case 74: fmt = 0x1b01; b = 16; break; // BC2_UNORM
			case 75: fmt = 0x1b06; b = 16; break; // BC2_SRGB
			case 76: // BC3_TYPELESS
			case 77: fmt = 0x1c01; b = 16; break; // BC3_UNORM
			case 78: fmt = 0x1c06; b = 16; break; // BC3_SRGB
			case 79: // BC4_TYPELESS
			case 80: fmt = 0x1d01; b = 8; break; // BC4_UNORM
			case 81: fmt = 0x1d02; b = 8; break; // BC4_SNORM
			case 82: // BC5_TYPELESS
			case 83: fmt = 0x1e01; b = 16; break; // BC5_UNORM
			case 84: fmt = 0x1e02; b = 16; break; // BC5_SNORM
			case 94: // BC6H_TYPELESS
			case 95: fmt = 0x1f0a; b = 16; break; // BC6H_UF16
			case 96: fmt = 0x1f0b; b = 16; break; // BC6H_SF16
			case 97: // BC7_TYPELESS
			case 98: fmt = 0x2001; b = 16; break; // BC7_UNORM
			case 99: fmt = 0x2006; b = 16; break; // BC7_SRGB
			case 27: // RGBA8_TYPELESS
			case 28: // RGBA8_UNORM
			case 30: fmt = 0x0b01; b = 4; bw = bh = 1; break; // RGBA8_UINT
			case 29: fmt = 0x0b06; b = 4; bw = bh = 1; break; // RGBA8_SRGB
			default: return false;
		}
	}
	if (bntx_fmt)
		*bntx_fmt = fmt;
	if (bpp)
		*bpp = b;
	if (blk_w)
		*blk_w = bw;
	if (blk_h)
		*blk_h = bh;
	return true;
}

enumError EncodeBNTX_FromDDS (
	u8 **dest, uint *dest_size, const u8 *dds, uint dds_size, ccp name)
{
	if (!dest || !dds || dds_size < 128)
		return EINVAL;
	if (memcmp (dds, "DDS ", 4))
		return ERROR0 (ERR_INVALID_DATA, "Not a DDS image (missing 'DDS ' magic)\n");
	if (brd32 (dds + 4) != 124)
		return ERROR0 (ERR_INVALID_DATA, "Invalid DDS header size %u (expected 124)\n",
			brd32 (dds + 4));

	const uint height = brd32 (dds + 12);
	const uint width = brd32 (dds + 16);
	if (!width || !height || width > 16384 || height > 16384)
		return ERROR0 (ERR_INVALID_DATA, "Invalid DDS dimensions %ux%u\n", width, height);

	const uint pf_flags = brd32 (dds + 80);
	const uint fourcc = brd32 (dds + 84);
	const bool is_dx10 = (pf_flags & 0x04) && fourcc == BNTX_DDS_FOURCC_DX10;

	uint payload_off = 128, dxgi_fmt = 0;
	if (is_dx10)
	{
		if (dds_size < 148)
			return ERROR0 (ERR_INVALID_DATA, "DDS DX10 header truncated\n");
		dxgi_fmt = brd32 (dds + 128);
		if (brd32 (dds + 140) != 1) // array size
			return ERR_NOTHING_TO_DO;
		if (brd32 (dds + 132) != 3) // not a 2D texture
			return ERR_NOTHING_TO_DO;
		payload_off = 148;
	}
	else if (brd32 (dds + 24) > 1) // depth > 1: volume texture
		return ERR_NOTHING_TO_DO;

	uint bntx_fmt = 0, bpp = 0, blk_w = 4, blk_h = 4;
	if (!dds_to_bntx_format (fourcc, dxgi_fmt, is_dx10, &bntx_fmt, &bpp, &blk_w, &blk_h))
		return ERR_NOTHING_TO_DO;

	if (payload_off >= dds_size)
		return ERROR0 (ERR_INVALID_DATA, "DDS payload missing\n");
	const u8 *payload = dds + payload_off;
	const uint payload_size = dds_size - payload_off;

	// Mip chain: honour the header count, clamped to the dimension-derived
	// maximum and to whatever the payload actually holds (simple single-mip
	// files often carry count 0/1; over-declared counts are truncated).
	uint mip_count = brd32 (dds + 28);
	if (!mip_count)
		mip_count = 1;
	uint max_mips = 1;
	for (uint m = width > height ? width : height; m > 1; m >>= 1)
		max_mips++;
	if (mip_count > max_mips)
		mip_count = max_mips;

	uint lin_off[16];
	uint lin_size[16];
	uint n_mips = 0;
	uint cursor = 0;
	for (uint m = 0; m < mip_count && m < 16; m++)
	{
		const uint w = width >> m ? width >> m : 1;
		const uint h = height >> m ? height >> m : 1;
		const u64 sz = (u64)div_round_up (w, blk_w) * div_round_up (h, blk_h) * bpp;
		if (sz > payload_size - cursor)
			break;
		lin_off[m] = cursor;
		lin_size[m] = (uint)sz;
		cursor += (uint)sz;
		n_mips++;
	}
	if (!n_mips)
		return ERROR0 (ERR_INVALID_DATA, "DDS payload truncated\n");
	mip_count = n_mips;

	if (!name || !*name)
		name = "texture";

	// Block-height derivation mirrors EncodeBNTX_RGBA so RGBA8 files stay
	// consistent; per-mip levels follow the decoder's bh - mip rule in
	// DecodeBNTX_Mip_RGBA.
	uint bh_log2;
	if (height <= 16)
		bh_log2 = 0;
	else if (height <= 32)
		bh_log2 = 1;
	else if (height <= 64)
		bh_log2 = 2;
	else if (height <= 128)
		bh_log2 = 3;
	else
		bh_log2 = 4;

	// Swizzle every mip; later mips start 512-aligned like the reference
	// SwizzleSurfaceMipMaps implementation.
	u64 mip_rel[16];
	u64 total_surf = 0;
	for (uint m = 0; m < mip_count; m++)
	{
		if (m)
			total_surf = round_up ((uint)total_surf, 512);
		if (total_surf > BNTX_MAX_OUTPUT)
			return EFBIG;
		mip_rel[m] = total_surf;
		const uint w = width >> m ? width >> m : 1;
		const uint h = height >> m ? height >> m : 1;
		const uint bh = bh_log2 > m ? bh_log2 - m : 0;
		const uint block_height = 1u << bh;
		const uint wb = div_round_up (w, blk_w);
		const uint hb = div_round_up (h, blk_h);
		const u64 pitch = round_up (wb * bpp, 64);
		const u64 rows = round_up (hb, block_height * 8);
		if (pitch > BNTX_MAX_OUTPUT / (rows ? rows : 1))
			return EFBIG;
		total_surf += pitch * rows;
	}
	if (!total_surf || total_surf > BNTX_MAX_OUTPUT)
		return EFBIG;

	u8 *swizzled = CALLOC (1, (size_t)total_surf);
	if (!swizzled)
		return ERR_CANT_CREATE;
	for (uint m = 0; m < mip_count; m++)
	{
		const uint w = width >> m ? width >> m : 1;
		const uint h = height >> m ? height >> m : 1;
		const uint bh = bh_log2 > m ? bh_log2 - m : 0;
		const uint block_height = 1u << bh;
		const uint wb = div_round_up (w, blk_w);
		const uint hb = div_round_up (h, blk_h);
		const uint pitch = round_up (wb * bpp, 64);
		const uint rows = round_up (hb, block_height * 8);
		const u64 surf_size = (u64)pitch * rows;
		const u8 *src = payload + lin_off[m];
		u8 *base = swizzled + mip_rel[m];
		for (uint y = 0; y < hb; y++)
			for (uint x = 0; x < wb; x++)
			{
				const u64 pos = addr_block_linear (x, y, wb, bpp, 0, block_height);
				if (pos + bpp > surf_size)
					continue;
				memcpy (base + pos, src + ((u64)y * wb + x) * bpp, bpp);
			}
	}

	// Container layout: same single-texture structure as EncodeBNTX_RGBA,
	// with an 8-byte mip offset per level.
	ccp file_name = "output.bntx";
	const size_t file_name_len = strlen (file_name);
	const size_t tex_name_len = strlen (name);

	const uint s0_size = 2 + 0 + 1 + 1;
	const uint s1_size = round_up (2 + (uint)tex_name_len + 1, 2);
	const uint s2_size = round_up (2 + (uint)file_name_len + 1, 2);

	const uint str_payload_size = 4 + s0_size + s1_size + s2_size;
	const uint str_block_size = round_up (16 + str_payload_size, 8);
	const uint dic_block_size = 8 + 16 * 2;

	const uint ofs_bin_hdr = 0;
	const uint ofs_bntx_hdr = 0x20;
	const uint ofs_mem_pool = 0x58;
	const uint ofs_tex_ptrs = ofs_mem_pool + 0x140;
	const uint ofs_str = ofs_tex_ptrs + 8;
	const uint ofs_dic = ofs_str + str_block_size;
	const uint ofs_brti = ofs_dic + dic_block_size;

	const uint abs_str_base = ofs_str + 16 + 4;
	const uint abs_str_empty = abs_str_base;
	const uint abs_str_tex = abs_str_empty + s0_size;
	const uint abs_str_file = abs_str_tex + s1_size;

	const uint ofs_ti = ofs_brti + 16;
	const uint ofs_tex_rt = ofs_ti + 144;
	const uint ofs_tex_view_rt = ofs_tex_rt + 256;
	const uint ofs_mip_offsets = ofs_tex_view_rt + 256;
	const uint ofs_sec1_end = ofs_mip_offsets + 8 * mip_count;
	const uint brti_block_size = ofs_sec1_end - ofs_brti;
	const uint ofs_sec1_aligned = round_up (ofs_sec1_end, 8);

	const uint ofs_brtd_data = round_up (ofs_sec1_aligned + 16, 4096);
	const uint ofs_brtd = ofs_brtd_data - 16;
	const uint ofs_sec2_start = ofs_brtd;
	const u64 ofs_sec2_end64 = (u64)ofs_brtd_data + total_surf;
	if (ofs_sec2_end64 > BNTX_MAX_OUTPUT)
	{
		FREE (swizzled);
		return EFBIG;
	}
	const uint ofs_sec2_end = (uint)ofs_sec2_end64;
	const uint ofs_rlt = round_up (ofs_sec2_end, 4096);

	const uint n_sec1_entries = 8;
	const uint n_sec2_entries = 2;
	const uint total_rlt_entries = n_sec1_entries + n_sec2_entries;
	const uint rlt_size = 16 + 2 * 24 + total_rlt_entries * 8;
	const u64 total_size64 = (u64)ofs_rlt + rlt_size;
	if (total_size64 > BNTX_MAX_OUTPUT)
	{
		FREE (swizzled);
		return EFBIG;
	}
	const uint total_size = (uint)total_size64;

	(void)ofs_bin_hdr;

	u8 *buf = CALLOC (1, (size_t)total_size);
	if (!buf)
	{
		FREE (swizzled);
		return ERR_CANT_CREATE;
	}

	memcpy (buf, "BNTX\0\0\0\0", 8);
	buf[0x08] = 0;
	buf[0x09] = 0;
	bwr16 (buf + 0x0a, 4);
	bwr16 (buf + 0x0c, 0xfeff);
	buf[0x0e] = 12;
	buf[0x0f] = 64;
	bwr32 (buf + 0x10, abs_str_file + 2);
	bwr16 (buf + 0x14, 0);
	bwr16 (buf + 0x16, (u16)ofs_str);
	bwr32 (buf + 0x18, ofs_rlt);
	bwr32 (buf + 0x1c, total_size);

	memcpy (buf + ofs_bntx_hdr, "NX  ", 4);
	bwr32 (buf + ofs_bntx_hdr + 4, 1);
	bwr64 (buf + ofs_bntx_hdr + 8, ofs_tex_ptrs);
	bwr64 (buf + ofs_bntx_hdr + 16, ofs_brtd);
	bwr64 (buf + ofs_bntx_hdr + 24, ofs_dic);
	bwr64 (buf + ofs_bntx_hdr + 32, ofs_mem_pool);

	bwr64 (buf + ofs_tex_ptrs, ofs_brti);

	memcpy (buf + ofs_str, "_STR", 4);
	bwr32 (buf + ofs_str + 4, str_block_size);
	bwr64 (buf + ofs_str + 8, str_block_size);
	bwr32 (buf + ofs_str + 16, 2);

	bwr16 (buf + abs_str_empty, 0);
	buf[abs_str_empty + 2] = 0;

	bwr16 (buf + abs_str_tex, (u16)tex_name_len);
	memcpy (buf + abs_str_tex + 2, name, tex_name_len);
	buf[abs_str_tex + 2 + tex_name_len] = 0;

	bwr16 (buf + abs_str_file, (u16)file_name_len);
	memcpy (buf + abs_str_file + 2, file_name, file_name_len);
	buf[abs_str_file + 2 + file_name_len] = 0;

	memcpy (buf + ofs_dic, "_DIC", 4);
	bwr32 (buf + ofs_dic + 4, 1);
	bwr32 (buf + ofs_dic + 8, 0xffffffff);
	bwr16 (buf + ofs_dic + 12, 1);
	bwr16 (buf + ofs_dic + 14, 0);
	bwr64 (buf + ofs_dic + 16, abs_str_empty);
	bwr32 (buf + ofs_dic + 24, 1);
	bwr16 (buf + ofs_dic + 28, 0);
	bwr16 (buf + ofs_dic + 30, 1);
	bwr64 (buf + ofs_dic + 32, abs_str_tex);

	memcpy (buf + ofs_brti, "BRTI", 4);
	bwr32 (buf + ofs_brti + 4, brti_block_size);
	bwr64 (buf + ofs_brti + 8, brti_block_size);

	u8 *ti = buf + ofs_ti;
	ti[0] = 0;
	ti[1] = 2;
	bwr16 (ti + 0x02, 0);
	bwr16 (ti + 0x04, 0);
	bwr16 (ti + 0x06, (u16)mip_count);
	bwr32 (ti + 0x08, 1);
	bwr32 (ti + 0x0c, bntx_fmt);
	bwr32 (ti + 0x10, 0x20);
	bwr32 (ti + 0x14, width);
	bwr32 (ti + 0x18, height);
	bwr32 (ti + 0x1c, 1);
	bwr32 (ti + 0x20, 1);
	bwr32 (ti + 0x24, bh_log2);
	bwr32 (ti + 0x28, 2);
	bwr32 (ti + 0x40, (u32)total_surf);
	bwr32 (ti + 0x44, 512);
	bwr32 (ti + 0x48, 0x05040302);
	bwr64 (ti + 0x50, abs_str_tex);
	bwr64 (ti + 0x58, ofs_bntx_hdr);
	bwr64 (ti + 0x60, ofs_mip_offsets);
	bwr64 (ti + 0x68, 0);
	bwr64 (ti + 0x70, ofs_tex_rt);
	bwr64 (ti + 0x78, ofs_tex_view_rt);
	bwr64 (ti + 0x80, 0);
	bwr64 (ti + 0x88, 0);

	for (uint m = 0; m < mip_count; m++)
		bwr64 (buf + ofs_mip_offsets + 8 * m, (u64)ofs_brtd_data + mip_rel[m]);

	const uint brtd_block_size = (uint)(16 + total_surf);
	memcpy (buf + ofs_brtd, "BRTD", 4);
	bwr32 (buf + ofs_brtd + 4, brtd_block_size);
	bwr64 (buf + ofs_brtd + 8, brtd_block_size);
	memcpy (buf + ofs_brtd_data, swizzled, (size_t)total_surf);
	FREE (swizzled);

	u8 *rlt = buf + ofs_rlt;
	memcpy (rlt, "_RLT", 4);
	bwr32 (rlt + 4, ofs_rlt);
	bwr32 (rlt + 8, 2);
	bwr32 (rlt + 12, 0);

	bwr64 (rlt + 16, 0);
	bwr32 (rlt + 24, 0);
	bwr32 (rlt + 28, ofs_sec2_start);
	bwr32 (rlt + 32, 0);
	bwr32 (rlt + 36, n_sec1_entries);

	bwr64 (rlt + 40, 0);
	bwr32 (rlt + 48, ofs_sec2_start);
	bwr32 (rlt + 52, ofs_rlt - ofs_sec2_start);
	bwr32 (rlt + 56, n_sec1_entries);
	bwr32 (rlt + 60, n_sec2_entries);

	u8 *ep = rlt + 16 + 2 * 24;
	struct {
		s32 ofs;
		u16 arr;
		u8 pcnt;
		u8 padc;
	} entries[10] = {
		{ 40, 2, 1, 1 },
		{ 64, 1, 1, 0 },
		{ (s32)ofs_tex_ptrs, 1, 1, 0 },
		{ (s32)ofs_dic + 16, 2, 1, 1 },
		{ (s32)ofs_ti + 0x50, 1, 3, 0 },
		{ (s32)ofs_ti + 0x50 + 24, 1, 1, 0 },
		{ (s32)ofs_ti + 0x50 + 32, 1, 2, 0 },
		{ (s32)ofs_ti + 0x50 + 56, 1, 1, 0 },
		{ 48, 1, 1, 0 },
		{ (s32)ofs_mip_offsets, 1, 1, 0 }
	};

	for (uint e = 0; e < total_rlt_entries; e++)
	{
		bwr32 (ep + e * 8 + 0, (u32)entries[e].ofs);
		bwr16 (ep + e * 8 + 4, entries[e].arr);
		ep[e * 8 + 6] = entries[e].pcnt;
		ep[e * 8 + 7] = entries[e].padc;
	}

	*dest = buf;
	if (dest_size)
		*dest_size = total_size;
	return ERR_OK;
}

//-----------------------------------------------------------------------------
///////////////		multi-texture DDS -> BNTX (combine)		///////////////
//-----------------------------------------------------------------------------
//
// SourceToBinaryCmd `-bntx` without `--split` merges every input DDS into one
// BNTX container (CreateBNTX(path, textures) in
// Source2Binary/FileFormats/Nintendo/BNTX.cs). wimgt's per-file loop is the
// `--split` equivalent; this writer closes the combine gap so
// `wimgt ENCODE a.dds b.dds --dest out.bntx` keeps every source's native
// blocks, format word and mip chain in a single file.
//
// Container layout generalizes the single-texture writer above: one _STR with
// N+1 names, one _DIC Patricia trie with N entries, N BRTI blocks (each with
// its own mip-offset array), one BRTD holding all swizzled surfaces
// (each texture base 4096-aligned, later mips 512-aligned within the texture
// like SwizzleSurfaceMipMaps), and a programmatic _RLT covering every file
// offset pointer.

//--- Patricia trie for _DIC (port of lib-bea.c BuildBeaDict, same NintendoSDK
//--- ResDict format: ref u32, left u16, right u16, name u64 LE). ---
typedef struct bntx_dic_node_t
{
	ccp data;
	int data_len;
	int bit_idx;
	struct bntx_dic_node_t *parent;
	struct bntx_dic_node_t *child[2];
} bntx_dic_node_t;

typedef struct bntx_dic_tree_t
{
	bntx_dic_node_t **entries;
	uint n_entries, cap_entries;
	bntx_dic_node_t *root;
	bntx_dic_node_t **all_nodes;
	uint n_all, cap_all;
} bntx_dic_tree_t;

static int bntx_dic_bit (ccp str, int len, int b)
{
	if (b < 0)
		return 0;
	const int byte_from_end = b / 8;
	if (byte_from_end >= len)
		return 0;
	const u8 byte = (u8)str[len - 1 - byte_from_end];
	return (byte >> (b % 8)) & 1;
}

static int bntx_dic_bit_length (ccp str, int len)
{
	for (int i = 0; i < len; i++)
	{
		if ((u8)str[i])
		{
			int hi = 7;
			while (!(((u8)str[i] >> hi) & 1))
				hi--;
			return (len - i - 1) * 8 + hi + 1;
		}
	}
	return 1;
}

static int bntx_dic_first_1bit (ccp str, int len)
{
	const int bl = bntx_dic_bit_length (str, len);
	for (int i = 0; i < bl; i++)
		if (bntx_dic_bit (str, len, i))
			return i;
	return 0;
}

static int bntx_dic_mismatch (ccp a, int alen, ccp b, int blen)
{
	const int bla = bntx_dic_bit_length (a, alen);
	const int blb = bntx_dic_bit_length (b, blen);
	const int hi = bla > blb ? bla : blb;
	for (int i = 0; i < hi; i++)
		if (bntx_dic_bit (a, alen, i) != bntx_dic_bit (b, blen, i))
			return i;
	return -1;
}

static bntx_dic_node_t *bntx_dic_new_node (
	bntx_dic_tree_t *tree, ccp data, int data_len, int bit_idx, bntx_dic_node_t *parent)
{
	bntx_dic_node_t *n = CALLOC (1, sizeof (*n));
	if (!n)
		return NULL;
	n->data = data;
	n->data_len = data_len;
	n->bit_idx = bit_idx;
	n->parent = parent ? parent : n;
	n->child[0] = n->child[1] = n;
	if (tree->n_all == tree->cap_all)
	{
		const uint ncap = tree->cap_all ? tree->cap_all * 2 : 16;
		void *mem = REALLOC (tree->all_nodes, ncap * sizeof (*tree->all_nodes));
		if (!mem)
		{
			FREE (n);
			return NULL;
		}
		tree->all_nodes = mem;
		tree->cap_all = ncap;
	}
	tree->all_nodes[tree->n_all++] = n;
	return n;
}

static uint bntx_dic_insert_entry (bntx_dic_tree_t *tree, ccp data, int data_len,
	bntx_dic_node_t *node)
{
	for (uint i = 0; i < tree->n_entries; i++)
	{
		bntx_dic_node_t *e = tree->entries[i];
		if (e->data_len == data_len && !memcmp (e->data, data, data_len))
		{
			tree->entries[i] = node;
			return i;
		}
	}
	if (tree->n_entries == tree->cap_entries)
	{
		const uint ncap = tree->cap_entries ? tree->cap_entries * 2 : 16;
		void *mem = REALLOC (tree->entries, ncap * sizeof (*tree->entries));
		if (!mem)
			return tree->n_entries;
		tree->entries = mem;
		tree->cap_entries = ncap;
	}
	tree->entries[tree->n_entries] = node;
	return tree->n_entries++;
}

static uint bntx_dic_index_of (bntx_dic_tree_t *tree, ccp data, int data_len)
{
	for (uint i = 0; i < tree->n_entries; i++)
		if (tree->entries[i]->data_len == data_len
			&& !memcmp (tree->entries[i]->data, data, data_len))
			return i;
	return 0;
}

static bntx_dic_node_t *bntx_dic_search (
	bntx_dic_tree_t *tree, ccp data, int data_len, bool want_prev)
{
	if (tree->root->child[0] == tree->root)
		return tree->root;
	bntx_dic_node_t *node = tree->root->child[0];
	bntx_dic_node_t *prev_node = node;
	for (;;)
	{
		prev_node = node;
		node = node->child[bntx_dic_bit (data, data_len, node->bit_idx)];
		if (node->bit_idx <= prev_node->bit_idx)
			break;
	}
	return want_prev ? prev_node : node;
}

static bool bntx_dic_insert (bntx_dic_tree_t *tree, ccp key, int key_len)
{
	bntx_dic_node_t *current = bntx_dic_search (tree, key, key_len, true);
	int bit_idx = bntx_dic_mismatch (current->data, current->data_len, key, key_len);
	while (bit_idx < current->parent->bit_idx)
		current = current->parent;

	if (bit_idx < current->bit_idx)
	{
		bntx_dic_node_t *nn = bntx_dic_new_node (tree, key, key_len, bit_idx, current->parent);
		if (!nn)
			return false;
		nn->child[bntx_dic_bit (key, key_len, bit_idx) ^ 1] = current;
		current->parent->child[bntx_dic_bit (key, key_len, current->parent->bit_idx)] = nn;
		current->parent = nn;
		bntx_dic_insert_entry (tree, key, key_len, nn);
	}
	else if (bit_idx > current->bit_idx)
	{
		bntx_dic_node_t *nn = bntx_dic_new_node (tree, key, key_len, bit_idx, current);
		if (!nn)
			return false;
		const int b = bntx_dic_bit (key, key_len, bit_idx) ^ 1;
		nn->child[b] = bntx_dic_bit (current->data, current->data_len, bit_idx) == b
			? current
			: tree->root;
		current->child[bntx_dic_bit (key, key_len, current->bit_idx)] = nn;
		bntx_dic_insert_entry (tree, key, key_len, nn);
	}
	else
	{
		int new_bit_idx = bntx_dic_first_1bit (key, key_len);
		bntx_dic_node_t *branch = current->child[bntx_dic_bit (key, key_len, bit_idx)];
		if (branch != tree->root)
			new_bit_idx = bntx_dic_mismatch (branch->data, branch->data_len, key, key_len);
		bntx_dic_node_t *nn = bntx_dic_new_node (tree, key, key_len, new_bit_idx, current);
		if (!nn)
			return false;
		nn->child[bntx_dic_bit (key, key_len, new_bit_idx) ^ 1] = branch;
		current->child[bntx_dic_bit (key, key_len, bit_idx)] = nn;
		bntx_dic_insert_entry (tree, key, key_len, nn);
	}
	return true;
}

typedef struct bntx_dic_entry_t
{
	u32 reference;
	u16 idx_left, idx_right;
	ccp key;
	int key_len;
} bntx_dic_entry_t;

static void bntx_dic_free_tree (bntx_dic_tree_t *tree)
{
	for (uint i = 0; i < tree->n_all; i++)
		FREE (tree->all_nodes[i]);
	FREE (tree->all_nodes);
	FREE (tree->entries);
	memset (tree, 0, sizeof (*tree));
}

// Builds a Patricia node table for NAMES[0..n-1]. Returns CALLOC-owned array
// of n+1 entries (index 0 is the root with empty key); NULL on OOM. Caller
// frees the array (keys are borrowed, not owned).
static bntx_dic_entry_t *bntx_dic_build (ccp const *names, uint n)
{
	bntx_dic_tree_t tree = { 0 };
	tree.root = bntx_dic_new_node (&tree, "", 0, -1, 0);
	if (!tree.root)
		return NULL;
	tree.root->parent = tree.root;
	tree.root->child[0] = tree.root->child[1] = tree.root;
	bntx_dic_insert_entry (&tree, "", 0, tree.root);

	for (uint i = 0; i < n; i++)
	{
		if (!bntx_dic_insert (&tree, names[i], (int)strlen (names[i])))
		{
			bntx_dic_free_tree (&tree);
			return NULL;
		}
	}

	bntx_dic_entry_t *out = CALLOC (tree.n_entries, sizeof (*out));
	if (!out)
	{
		bntx_dic_free_tree (&tree);
		return NULL;
	}
	for (uint i = 0; i < tree.n_entries; i++)
	{
		bntx_dic_node_t *node = tree.entries[i];
		out[i].reference = (u32)node->bit_idx;
		out[i].idx_left = (u16)bntx_dic_index_of (&tree, node->child[0]->data,
			node->child[0]->data_len);
		out[i].idx_right = (u16)bntx_dic_index_of (&tree, node->child[1]->data,
			node->child[1]->data_len);
		out[i].key = node->data;
		out[i].key_len = node->data_len;
	}
	bntx_dic_free_tree (&tree);
	return out;
}

//--- Per-texture parsed + swizzled state. ---
typedef struct bntx_combine_tex_t
{
	char name[128];
	uint width, height;
	uint format;
	uint bpp, blk_w, blk_h;
	uint bh_log2;
	uint mip_count;
	u64 mip_rel[16];
	u8 *swizzled;
	u64 total_surf;
} bntx_combine_tex_t;

static void bntx_combine_free (bntx_combine_tex_t *texs, uint n)
{
	if (!texs)
		return;
	for (uint i = 0; i < n; i++)
		FREE (texs[i].swizzled);
	FREE (texs);
}

// Parses one DDS into TEX (native blocks preserved). Returns ERR_NOTHING_TO_DO
// for variants with no direct BNTX representation (caller falls back).
static enumError bntx_combine_parse_dds (
	const u8 *dds, uint dds_size, ccp name, bntx_combine_tex_t *tex)
{
	if (!dds || dds_size < 128 || !tex)
		return EINVAL;
	if (memcmp (dds, "DDS ", 4))
		return ERROR0 (ERR_INVALID_DATA, "Not a DDS image (missing 'DDS ' magic)\n");
	if (brd32 (dds + 4) != 124)
		return ERROR0 (ERR_INVALID_DATA, "Invalid DDS header size %u (expected 124)\n",
			brd32 (dds + 4));

	const uint width = brd32 (dds + 16);
	const uint height = brd32 (dds + 12);
	if (!width || !height || width > 16384 || height > 16384)
		return ERROR0 (ERR_INVALID_DATA, "Invalid DDS dimensions %ux%u\n", width, height);

	const uint pf_flags = brd32 (dds + 80);
	const uint fourcc = brd32 (dds + 84);
	const bool is_dx10 = (pf_flags & 0x04) && fourcc == BNTX_DDS_FOURCC_DX10;

	uint payload_off = 128, dxgi_fmt = 0;
	if (is_dx10)
	{
		if (dds_size < 148)
			return ERROR0 (ERR_INVALID_DATA, "DDS DX10 header truncated\n");
		dxgi_fmt = brd32 (dds + 128);
		if (brd32 (dds + 140) != 1)
			return ERR_NOTHING_TO_DO;
		if (brd32 (dds + 132) != 3)
			return ERR_NOTHING_TO_DO;
		payload_off = 148;
	}
	else if (brd32 (dds + 24) > 1)
		return ERR_NOTHING_TO_DO;

	uint bntx_fmt = 0, bpp = 0, blk_w = 4, blk_h = 4;
	if (!dds_to_bntx_format (fourcc, dxgi_fmt, is_dx10, &bntx_fmt, &bpp, &blk_w, &blk_h))
		return ERR_NOTHING_TO_DO;

	if (payload_off >= dds_size)
		return ERROR0 (ERR_INVALID_DATA, "DDS payload missing\n");
	const u8 *payload = dds + payload_off;
	const uint payload_size = dds_size - payload_off;

	uint mip_count = brd32 (dds + 28);
	if (!mip_count)
		mip_count = 1;
	uint max_mips = 1;
	for (uint m = width > height ? width : height; m > 1; m >>= 1)
		max_mips++;
	if (mip_count > max_mips)
		mip_count = max_mips;

	uint lin_off[16];
	uint lin_size[16];
	uint n_mips = 0;
	uint cursor = 0;
	for (uint m = 0; m < mip_count && m < 16; m++)
	{
		const uint w = width >> m ? width >> m : 1;
		const uint h = height >> m ? height >> m : 1;
		const u64 sz = (u64)div_round_up (w, blk_w) * div_round_up (h, blk_h) * bpp;
		if (sz > payload_size - cursor)
			break;
		lin_off[m] = cursor;
		lin_size[m] = (uint)sz;
		cursor += (uint)sz;
		n_mips++;
	}
	if (!n_mips)
		return ERROR0 (ERR_INVALID_DATA, "DDS payload truncated\n");
	(void)lin_size;

	uint bh_log2;
	if (height <= 16)
		bh_log2 = 0;
	else if (height <= 32)
		bh_log2 = 1;
	else if (height <= 64)
		bh_log2 = 2;
	else if (height <= 128)
		bh_log2 = 3;
	else
		bh_log2 = 4;

	u64 mip_rel[16];
	u64 total_surf = 0;
	for (uint m = 0; m < n_mips; m++)
	{
		if (m)
			total_surf = round_up ((uint)total_surf, 512);
		if (total_surf > BNTX_MAX_OUTPUT)
			return EFBIG;
		mip_rel[m] = total_surf;
		const uint w = width >> m ? width >> m : 1;
		const uint h = height >> m ? height >> m : 1;
		const uint bh = bh_log2 > m ? bh_log2 - m : 0;
		const uint block_height = 1u << bh;
		const uint wb = div_round_up (w, blk_w);
		const uint hb = div_round_up (h, blk_h);
		const u64 pitch = round_up (wb * bpp, 64);
		const u64 rows = round_up (hb, block_height * 8);
		if (pitch > BNTX_MAX_OUTPUT / (rows ? rows : 1))
			return EFBIG;
		total_surf += pitch * rows;
	}
	if (!total_surf || total_surf > BNTX_MAX_OUTPUT)
		return EFBIG;

	u8 *swizzled = CALLOC (1, (size_t)total_surf);
	if (!swizzled)
		return ERR_CANT_CREATE;
	for (uint m = 0; m < n_mips; m++)
	{
		const uint w = width >> m ? width >> m : 1;
		const uint h = height >> m ? height >> m : 1;
		const uint bh = bh_log2 > m ? bh_log2 - m : 0;
		const uint block_height = 1u << bh;
		const uint wb = div_round_up (w, blk_w);
		const uint hb = div_round_up (h, blk_h);
		const uint pitch = round_up (wb * bpp, 64);
		const uint rows = round_up (hb, block_height * 8);
		const u64 surf_size = (u64)pitch * rows;
		const u8 *src = payload + lin_off[m];
		u8 *base = swizzled + mip_rel[m];
		for (uint y = 0; y < hb; y++)
			for (uint x = 0; x < wb; x++)
			{
				const u64 pos = addr_block_linear (x, y, wb, bpp, 0, block_height);
				if (pos + bpp > surf_size)
					continue;
				memcpy (base + pos, src + ((u64)y * wb + x) * bpp, bpp);
			}
	}

	memset (tex, 0, sizeof (*tex));
	snprintf (tex->name, sizeof (tex->name), "%s", name && *name ? name : "texture");
	tex->width = width;
	tex->height = height;
	tex->format = bntx_fmt;
	tex->bpp = bpp;
	tex->blk_w = blk_w;
	tex->blk_h = blk_h;
	tex->bh_log2 = bh_log2;
	tex->mip_count = n_mips;
	memcpy (tex->mip_rel, mip_rel, sizeof (mip_rel));
	tex->swizzled = swizzled;
	tex->total_surf = total_surf;
	return ERR_OK;
}

enumError EncodeBNTX_FromDDSList (u8 **dest, uint *dest_size, const u8 **dds_datas,
	const uint *dds_sizes, ccp const *names, uint n_tex)
{
	if (!dest || !dds_datas || !dds_sizes || !names || !n_tex || n_tex > 256)
		return EINVAL;

	bntx_combine_tex_t *texs = CALLOC (n_tex, sizeof (*texs));
	if (!texs)
		return ERR_CANT_CREATE;

	// Parse every DDS first so a single unsupported variant declines the
	// whole batch (caller falls back to per-file RGBA8) instead of writing
	// a partial container. Duplicate names get _1, _2 suffixes.
	for (uint i = 0; i < n_tex; i++)
	{
		char uname[128];
		snprintf (uname, sizeof (uname), "%s",
			names[i] && *names[i] ? names[i] : "texture");
		// Deduplicate against earlier textures.
		for (uint k = 0; k < i; k++)
		{
			if (!strcmp (texs[k].name, uname))
			{
				uint suffix = 1;
				char cand[128];
				do
					snprintf (cand, sizeof (cand), "%s_%u", uname, suffix++);
				while (0);
				// Re-check against all earlier names.
				bool clash = false;
				for (uint j = 0; j < i; j++)
					if (!strcmp (texs[j].name, cand))
					{
						clash = true;
						break;
					}
				if (!clash)
				{
					snprintf (uname, sizeof (uname), "%s", cand);
					break;
				}
				// On repeated clash keep bumping the suffix.
				for (;; suffix++)
				{
					snprintf (cand, sizeof (cand), "%s_%u", names[i], suffix);
					clash = false;
					for (uint j = 0; j < i; j++)
						if (!strcmp (texs[j].name, cand))
						{
							clash = true;
							break;
						}
					if (!clash)
					{
						snprintf (uname, sizeof (uname), "%s", cand);
						break;
					}
				}
				break;
			}
		}
		const enumError err = bntx_combine_parse_dds (
			dds_datas[i], dds_sizes[i], uname, &texs[i]);
		if (err)
		{
			bntx_combine_free (texs, i);
			return err;
		}
	}

	ccp file_name = "output.bntx";

	// String table: "" + N names + file name.
	uint *str_sizes = CALLOC (n_tex + 2, sizeof (*str_sizes));
	uint *str_offs = CALLOC (n_tex + 2, sizeof (*str_offs));
	ccp *str_vals = CALLOC (n_tex + 2, sizeof (*str_vals));
	if (!str_sizes || !str_offs || !str_vals)
	{
		FREE (str_sizes);
		FREE (str_offs);
		FREE (str_vals);
		bntx_combine_free (texs, n_tex);
		return ERR_CANT_CREATE;
	}
	str_vals[0] = "";
	for (uint i = 0; i < n_tex; i++)
		str_vals[1 + i] = texs[i].name;
	str_vals[1 + n_tex] = file_name;
	for (uint i = 0; i < n_tex + 2; i++)
		str_sizes[i] = round_up (2 + (uint)strlen (str_vals[i]) + 1, 2);
	uint str_payload = 4;
	for (uint i = 0; i < n_tex + 2; i++)
		str_payload += str_sizes[i];
	const uint str_block_size = round_up (16 + str_payload, 8);

	// Dictionary via Patricia trie over the N texture names.
	ccp *dic_names = CALLOC (n_tex, sizeof (*dic_names));
	if (!dic_names)
	{
		FREE (str_sizes);
		FREE (str_offs);
		FREE (str_vals);
		bntx_combine_free (texs, n_tex);
		return ERR_CANT_CREATE;
	}
	for (uint i = 0; i < n_tex; i++)
		dic_names[i] = texs[i].name;
	bntx_dic_entry_t *dic = bntx_dic_build (dic_names, n_tex);
	FREE (dic_names);
	if (!dic)
	{
		FREE (str_sizes);
		FREE (str_offs);
		FREE (str_vals);
		bntx_combine_free (texs, n_tex);
		return ERR_CANT_CREATE;
	}
	const uint dic_block_size = 8 + 16 * (n_tex + 1);

	const uint ofs_bntx_hdr = 0x20;
	const uint ofs_mem_pool = 0x58;
	const uint ofs_tex_ptrs = ofs_mem_pool + 0x140;
	const uint ofs_str = ofs_tex_ptrs + 8 * n_tex;
	const uint ofs_dic = ofs_str + str_block_size;
	uint ofs_brti_cur = ofs_dic + dic_block_size;

	uint *ofs_brti = CALLOC (n_tex, sizeof (*ofs_brti));
	uint *ofs_ti = CALLOC (n_tex, sizeof (*ofs_ti));
	uint *ofs_mip_offs = CALLOC (n_tex, sizeof (*ofs_mip_offs));
	uint *brti_size = CALLOC (n_tex, sizeof (*brti_size));
	if (!ofs_brti || !ofs_ti || !ofs_mip_offs || !brti_size)
	{
		FREE (ofs_brti);
		FREE (ofs_ti);
		FREE (ofs_mip_offs);
		FREE (brti_size);
		FREE (str_sizes);
		FREE (str_offs);
		FREE (str_vals);
		FREE (dic);
		bntx_combine_free (texs, n_tex);
		return ERR_CANT_CREATE;
	}
	for (uint i = 0; i < n_tex; i++)
	{
		ofs_brti[i] = round_up (ofs_brti_cur, 8);
		ofs_ti[i] = ofs_brti[i] + 16;
		ofs_mip_offs[i] = ofs_ti[i] + 144 + 256 + 256;
		brti_size[i] = 16 + 144 + 256 + 256 + 8 * texs[i].mip_count;
		ofs_brti_cur = ofs_brti[i] + brti_size[i];
	}
	const uint ofs_sec1_aligned = round_up (ofs_brti_cur, 8);

	// BRTD data: each texture base 4096-aligned, mips 512-aligned within.
	u64 *tex_base_rel = CALLOC (n_tex, sizeof (*tex_base_rel));
	if (!tex_base_rel)
	{
		FREE (ofs_brti);
		FREE (ofs_ti);
		FREE (ofs_mip_offs);
		FREE (brti_size);
		FREE (str_sizes);
		FREE (str_offs);
		FREE (str_vals);
		FREE (dic);
		bntx_combine_free (texs, n_tex);
		return ERR_CANT_CREATE;
	}
	u64 brtd_payload = 0;
	for (uint i = 0; i < n_tex; i++)
	{
		brtd_payload = round_up ((uint)brtd_payload, 4096);
		// First texture keeps the single-texture invariant that BRTD data
		// starts 4096-aligned right after the header; later textures follow
		// at the next 4096 boundary.
		tex_base_rel[i] = brtd_payload;
		brtd_payload += texs[i].total_surf;
		if (brtd_payload > BNTX_MAX_OUTPUT)
		{
			FREE (tex_base_rel);
			FREE (ofs_brti);
			FREE (ofs_ti);
			FREE (ofs_mip_offs);
			FREE (brti_size);
			FREE (str_sizes);
			FREE (str_offs);
			FREE (str_vals);
			FREE (dic);
			bntx_combine_free (texs, n_tex);
			return EFBIG;
		}
	}
	const uint ofs_brtd_data = round_up (ofs_sec1_aligned + 16, 4096);
	const uint ofs_brtd = ofs_brtd_data - 16;
	const uint ofs_sec2_start = ofs_brtd;
	const u64 sec2_end64 = (u64)ofs_brtd_data + brtd_payload;
	if (sec2_end64 > BNTX_MAX_OUTPUT)
	{
		FREE (tex_base_rel);
		FREE (ofs_brti);
		FREE (ofs_ti);
		FREE (ofs_mip_offs);
		FREE (brti_size);
		FREE (str_sizes);
		FREE (str_offs);
		FREE (str_vals);
		FREE (dic);
		bntx_combine_free (texs, n_tex);
		return EFBIG;
	}
	const uint ofs_sec2_end = (uint)sec2_end64;
	const uint ofs_rlt = round_up (ofs_sec2_end, 4096);

	// Relocation: one entry per pointer (unmerged runs stay valid, just
	// larger). Collect: BNTX hdr (4) + tex ptrs (N) + dict names (N+1) +
	// per texture (name, hdr, mip array ptr, tex/view ptrs = 5 + desc/user
	// slots covered as singletons) + mip offsets (sum mips).
	uint total_mips = 0;
	for (uint i = 0; i < n_tex; i++)
		total_mips += texs[i].mip_count;
	const uint n_ptr_entries = 4 + n_tex + (n_tex + 1) + n_tex * 5 + total_mips;
	const uint rlt_size = 16 + 2 * 24 + n_ptr_entries * 8;
	const u64 total_size64 = (u64)ofs_rlt + rlt_size;
	if (total_size64 > BNTX_MAX_OUTPUT)
	{
		FREE (tex_base_rel);
		FREE (ofs_brti);
		FREE (ofs_ti);
		FREE (ofs_mip_offs);
		FREE (brti_size);
		FREE (str_sizes);
		FREE (str_offs);
		FREE (str_vals);
		FREE (dic);
		bntx_combine_free (texs, n_tex);
		return EFBIG;
	}
	const uint total_size = (uint)total_size64;

	u8 *buf = CALLOC (1, (size_t)total_size);
	if (!buf)
	{
		FREE (tex_base_rel);
		FREE (ofs_brti);
		FREE (ofs_ti);
		FREE (ofs_mip_offs);
		FREE (brti_size);
		FREE (str_sizes);
		FREE (str_offs);
		FREE (str_vals);
		FREE (dic);
		bntx_combine_free (texs, n_tex);
		return ERR_CANT_CREATE;
	}

	// Binary + BNTX headers.
	memcpy (buf, "BNTX\0\0\0\0", 8);
	buf[0x08] = 0;
	buf[0x09] = 0;
	bwr16 (buf + 0x0a, 4);
	bwr16 (buf + 0x0c, 0xfeff);
	buf[0x0e] = 12;
	buf[0x0f] = 64;
	// NameOffset patched after string layout below.
	bwr16 (buf + 0x14, 0);
	bwr16 (buf + 0x16, (u16)ofs_str);
	bwr32 (buf + 0x18, ofs_rlt);
	bwr32 (buf + 0x1c, total_size);

	memcpy (buf + ofs_bntx_hdr, "NX  ", 4);
	bwr32 (buf + ofs_bntx_hdr + 4, n_tex);
	bwr64 (buf + ofs_bntx_hdr + 8, ofs_tex_ptrs);
	bwr64 (buf + ofs_bntx_hdr + 16, ofs_brtd);
	bwr64 (buf + ofs_bntx_hdr + 24, ofs_dic);
	bwr64 (buf + ofs_bntx_hdr + 32, ofs_mem_pool);

	for (uint i = 0; i < n_tex; i++)
		bwr64 (buf + ofs_tex_ptrs + 8 * i, ofs_brti[i]);

	// String table.
	memcpy (buf + ofs_str, "_STR", 4);
	bwr32 (buf + ofs_str + 4, str_block_size);
	bwr64 (buf + ofs_str + 8, str_block_size);
	bwr32 (buf + ofs_str + 16, n_tex + 1);
	uint str_cur = ofs_str + 16 + 4;
	for (uint i = 0; i < n_tex + 2; i++)
	{
		str_offs[i] = str_cur;
		const size_t L = strlen (str_vals[i]);
		bwr16 (buf + str_cur, (u16)L);
		memcpy (buf + str_cur + 2, str_vals[i], L);
		buf[str_cur + 2 + L] = 0;
		str_cur += str_sizes[i];
	}
	bwr32 (buf + 0x10, str_offs[n_tex + 1] + 2);

	// Dictionary: entries follow the Patricia build order; each node's name
	// pointer resolves via its key (root key "" -> str_offs[0], texture keys
	// -> matching str_offs[1+i]).
	memcpy (buf + ofs_dic, "_DIC", 4);
	bwr32 (buf + ofs_dic + 4, n_tex);
	for (uint i = 0; i < n_tex + 1; i++)
	{
		u64 name_ptr = str_offs[0];
		if (dic[i].key_len)
		{
			for (uint k = 0; k < n_tex; k++)
				if ((int)strlen (texs[k].name) == dic[i].key_len
					&& !memcmp (texs[k].name, dic[i].key, dic[i].key_len))
				{
					name_ptr = str_offs[1 + k];
					break;
				}
		}
		bwr32 (buf + ofs_dic + 8 + 16 * i, dic[i].reference);
		bwr16 (buf + ofs_dic + 8 + 16 * i + 4, dic[i].idx_left);
		bwr16 (buf + ofs_dic + 8 + 16 * i + 6, dic[i].idx_right);
		bwr64 (buf + ofs_dic + 8 + 16 * i + 8, name_ptr);
	}
	FREE (dic);

	// BRTI blocks.
	for (uint i = 0; i < n_tex; i++)
	{
		memcpy (buf + ofs_brti[i], "BRTI", 4);
		bwr32 (buf + ofs_brti[i] + 4, brti_size[i]);
		bwr64 (buf + ofs_brti[i] + 8, brti_size[i]);
		u8 *ti = buf + ofs_ti[i];
		ti[0] = 0;
		ti[1] = 2;
		bwr16 (ti + 0x02, 0);
		bwr16 (ti + 0x04, 0);
		bwr16 (ti + 0x06, (u16)texs[i].mip_count);
		bwr32 (ti + 0x08, 1);
		bwr32 (ti + 0x0c, texs[i].format);
		bwr32 (ti + 0x10, 0x20);
		bwr32 (ti + 0x14, texs[i].width);
		bwr32 (ti + 0x18, texs[i].height);
		bwr32 (ti + 0x1c, 1);
		bwr32 (ti + 0x20, 1);
		bwr32 (ti + 0x24, texs[i].bh_log2);
		bwr32 (ti + 0x28, 2);
		bwr32 (ti + 0x40, (u32)texs[i].total_surf);
		bwr32 (ti + 0x44, 512);
		bwr32 (ti + 0x48, 0x05040302);
		bwr64 (ti + 0x50, str_offs[1 + i]);
		bwr64 (ti + 0x58, ofs_bntx_hdr);
		bwr64 (ti + 0x60, ofs_mip_offs[i]);
		bwr64 (ti + 0x68, 0);
		bwr64 (ti + 0x70, ofs_ti[i] + 144);
		bwr64 (ti + 0x78, ofs_ti[i] + 144 + 256);
		bwr64 (ti + 0x80, 0);
		bwr64 (ti + 0x88, 0);
		for (uint m = 0; m < texs[i].mip_count; m++)
			bwr64 (buf + ofs_mip_offs[i] + 8 * m,
				(u64)ofs_brtd_data + tex_base_rel[i] + texs[i].mip_rel[m]);
	}

	// BRTD with all surfaces.
	const uint brtd_block_size = (uint)(16 + brtd_payload);
	memcpy (buf + ofs_brtd, "BRTD", 4);
	bwr32 (buf + ofs_brtd + 4, brtd_block_size);
	bwr64 (buf + ofs_brtd + 8, brtd_block_size);
	for (uint i = 0; i < n_tex; i++)
		memcpy (buf + ofs_brtd_data + tex_base_rel[i], texs[i].swizzled,
			(size_t)texs[i].total_surf);

	// Relocation table (two sections, one entry per pointer).
	u8 *rlt = buf + ofs_rlt;
	memcpy (rlt, "_RLT", 4);
	bwr32 (rlt + 4, ofs_rlt);
	bwr32 (rlt + 8, 2);
	bwr32 (rlt + 12, 0);
	bwr64 (rlt + 16, 0);
	bwr32 (rlt + 24, 0);
	bwr32 (rlt + 28, ofs_sec2_start);
	bwr32 (rlt + 32, 0);
	// Section 0 entry count patched below; section 1 covers BRTD->_RLT.
	const uint sec0_count = n_ptr_entries - 1;
	bwr32 (rlt + 36, sec0_count);
	bwr64 (rlt + 40, 0);
	bwr32 (rlt + 48, ofs_sec2_start);
	bwr32 (rlt + 52, ofs_rlt - ofs_sec2_start);
	bwr32 (rlt + 56, sec0_count);
	bwr32 (rlt + 60, 1);

	u32 *ptr_list = CALLOC (n_ptr_entries, sizeof (*ptr_list));
	if (!ptr_list)
	{
		FREE (tex_base_rel);
		FREE (ofs_brti);
		FREE (ofs_ti);
		FREE (ofs_mip_offs);
		FREE (brti_size);
		FREE (str_sizes);
		FREE (str_offs);
		FREE (str_vals);
		bntx_combine_free (texs, n_tex);
		FREE (buf);
		return ERR_CANT_CREATE;
	}
	uint pi = 0;
	ptr_list[pi++] = ofs_bntx_hdr + 8;
	ptr_list[pi++] = ofs_bntx_hdr + 16;
	ptr_list[pi++] = ofs_bntx_hdr + 24;
	ptr_list[pi++] = ofs_bntx_hdr + 32;
	for (uint i = 0; i < n_tex; i++)
		ptr_list[pi++] = ofs_tex_ptrs + 8 * i;
	for (uint i = 0; i < n_tex + 1; i++)
		ptr_list[pi++] = ofs_dic + 8 + 16 * i + 8;
	for (uint i = 0; i < n_tex; i++)
	{
		ptr_list[pi++] = ofs_ti[i] + 0x50;
		ptr_list[pi++] = ofs_ti[i] + 0x58;
		ptr_list[pi++] = ofs_ti[i] + 0x60;
		ptr_list[pi++] = ofs_ti[i] + 0x70;
		ptr_list[pi++] = ofs_ti[i] + 0x78;
		for (uint m = 0; m < texs[i].mip_count; m++)
			ptr_list[pi++] = ofs_mip_offs[i] + 8 * m;
	}
	// Sort for deterministic output (insertion sort, n is small).
	for (uint i = 1; i < pi; i++)
	{
		const u32 v = ptr_list[i];
		uint j = i;
		while (j > 0 && ptr_list[j - 1] > v)
		{
			ptr_list[j] = ptr_list[j - 1];
			j--;
		}
		ptr_list[j] = v;
	}
	u8 *ep = rlt + 16 + 2 * 24;
	for (uint e = 0; e < pi; e++)
	{
		bwr32 (ep + e * 8 + 0, ptr_list[e]);
		bwr16 (ep + e * 8 + 4, 1);
		ep[e * 8 + 6] = 1;
		ep[e * 8 + 7] = 0;
	}
	FREE (ptr_list);

	FREE (tex_base_rel);
	FREE (ofs_brti);
	FREE (ofs_ti);
	FREE (ofs_mip_offs);
	FREE (brti_size);
	FREE (str_sizes);
	FREE (str_offs);
	FREE (str_vals);
	bntx_combine_free (texs, n_tex);

	*dest = buf;
	if (dest_size)
		*dest_size = total_size;
	return ERR_OK;
}

//-----------------------------------------------------------------------------
///////////////		native DDS / ASTC export		///////////////
//-----------------------------------------------------------------------------
//
// Lossless export in the spirit of aboood40091/BNTX-Extractor ("saves them
// as DDS"): deswizzle the Tegra surface but keep the native block
// compression, wrapping it in a DDS header -- or in a raw .astc file for
// ASTC textures -- instead of decoding to RGBA8.
//
// The DDS header layout matches the reference extractor's dds.py so the
// files open in the same viewers: legacy FourCC codes for BC1 (DXT1),
// BC2 (DXT3) and BC3 (DXT5), a DX10 extension header for BC4S/BC5S/BC6H/
// BC7, and uncompressed bitmasks derived from the BNTX channel selectors.

// DDS pixel-format flag bits (dds.py pflags).
#define BNTX_DDS_PF_ALPHA 0x00000001
#define BNTX_DDS_PF_ALPHA_ONLY 0x00000002
#define BNTX_DDS_PF_FOURCC 0x00000004
#define BNTX_DDS_PF_RGB 0x00000040
#define BNTX_DDS_PF_LUMINANCE 0x00020000

typedef enum bntx_native_dds_kind_t
{
	BNTX_DDSK_RGBA8 = 28, // dds.py format_ codes for uncompressed types
	BNTX_DDSK_RGB565 = 85,
	BNTX_DDSK_R8 = 61,
	BNTX_DDSK_R8G8 = 49,
	BNTX_DDSK_BC1,
	BNTX_DDSK_BC2,
	BNTX_DDSK_BC3,
	BNTX_DDSK_BC4U,
	BNTX_DDSK_BC4S,
	BNTX_DDSK_BC5U,
	BNTX_DDSK_BC5S,
	BNTX_DDSK_BC6UF,
	BNTX_DDSK_BC6SF,
	BNTX_DDSK_BC7
} bntx_native_dds_kind_t;

// Resolves a BNTX format word to its native storage layout. Returns false
// for formats the reference extractor cannot export either (anything
// outside its formats table). IS_ASTC selects the raw .astc path;
// otherwise DDSK selects the DDS header variant.
static bool bntx_native_layout (uint format, uint *bpp, uint *blk_w, uint *blk_h,
	uint *ddsk, bool *is_astc)
{
	const uint fmt = (format >> 8) & 0xff, type = format & 0xff;
	uint b = 0, bw = 1, bh = 1, kind = 0;
	bool astc = false;

	switch (fmt)
	{
		case 0x0b:
			b = 4;
			kind = BNTX_DDSK_RGBA8;
			break;
		case 0x07:
			if (type != 1)
				return false;
			b = 2;
			kind = BNTX_DDSK_RGB565;
			break;
		case 0x02:
			if (type != 1)
				return false;
			b = 1;
			kind = BNTX_DDSK_R8;
			break;
		case 0x09:
			if (type != 1)
				return false;
			b = 2;
			kind = BNTX_DDSK_R8G8;
			break;
		case 0x1a: b = 8; bw = bh = 4; kind = BNTX_DDSK_BC1; break;
		case 0x1b: b = 16; bw = bh = 4; kind = BNTX_DDSK_BC2; break;
		case 0x1c: b = 16; bw = bh = 4; kind = BNTX_DDSK_BC3; break;
		case 0x1d:
			b = 8;
			bw = bh = 4;
			kind = type == 2 ? BNTX_DDSK_BC4S : BNTX_DDSK_BC4U;
			break;
		case 0x1e:
			b = 16;
			bw = bh = 4;
			kind = type == 2 ? BNTX_DDSK_BC5S : BNTX_DDSK_BC5U;
			break;
		case 0x1f:
			// UF16 vs SF16: the reference extractor uses type 1/2 while
			// other descriptions of the same files use 0x0a/0x0b (see
			// GetBNTXFormatName and the signed BC6 path in
			// DecodeBNTX_Mip_RGBA); accept both spellings.
			b = 16;
			bw = bh = 4;
			if (type == 2 || type == 0x0b)
				kind = BNTX_DDSK_BC6SF;
			else if (type == 1 || type == 0x0a)
				kind = BNTX_DDSK_BC6UF;
			else
				return false;
			break;
		case 0x20: b = 16; bw = bh = 4; kind = BNTX_DDSK_BC7; break;
		case 0x2d: b = 16; bw = 4; bh = 4; astc = true; break;
		case 0x2e: b = 16; bw = 5; bh = 4; astc = true; break;
		case 0x2f: b = 16; bw = 5; bh = 5; astc = true; break;
		case 0x30: b = 16; bw = 6; bh = 5; astc = true; break;
		case 0x31: b = 16; bw = 6; bh = 6; astc = true; break;
		case 0x32: b = 16; bw = 8; bh = 5; astc = true; break;
		case 0x33: b = 16; bw = 8; bh = 6; astc = true; break;
		case 0x34: b = 16; bw = 8; bh = 8; astc = true; break;
		case 0x35: b = 16; bw = 10; bh = 5; astc = true; break;
		case 0x36: b = 16; bw = 10; bh = 6; astc = true; break;
		case 0x37: b = 16; bw = 10; bh = 8; astc = true; break;
		case 0x38: b = 16; bw = 10; bh = 10; astc = true; break;
		case 0x39: b = 16; bw = 12; bh = 10; astc = true; break;
		case 0x3a: b = 16; bw = 12; bh = 12; astc = true; break;
		default:
			return false;
	}

	if (bpp)
		*bpp = b;
	if (blk_w)
		*blk_w = bw;
	if (blk_h)
		*blk_h = bh;
	if (ddsk)
		*ddsk = kind;
	if (is_astc)
		*is_astc = astc;
	return true;
}

// Deswizzles mip 0 of texture T and returns the first SIZE bytes of linear
// surface data (the reference extractor's `result[:size]` truncation).
static enumError bntx_native_linear (u8 **dest, const bntx_texture_t *t,
	uint bpp, uint blk_w, uint blk_h, uint size)
{
	u8 *linear = 0;
	uint linear_size = 0;
	const uint bh_log2 = t->block_height_log2 > 5 ? 5 : t->block_height_log2;
	const enumError err = BntxDeswizzle (&linear, &linear_size, t->data, t->data_size,
		t->width, t->height, blk_w, blk_h, bpp, t->tile_mode, bh_log2, true);
	if (err)
		return err;
	if (linear_size < size)
	{
		FREE (linear);
		return ERROR0 (ERR_INVALID_DATA, "Truncated BNTX texture '%s'\n", t->name);
	}
	// Shrink the buffer to the exact payload size.
	u8 *out = MALLOC (size ? size : 1);
	if (!out)
	{
		FREE (linear);
		return ERR_CANT_CREATE;
	}
	memcpy (out, linear, size);
	FREE (linear);
	*dest = out;
	return ERR_OK;
}

// Writes the 128-byte DDS header (+ 20-byte DX10 extension for BC4S/BC5S/
// BC6H/BC7) into HDR, matching dds.py generateHeader() with num_mipmaps=1.
// SEL holds the four BNTX channel selectors, low byte (red) first, with
// the reference tool's 0-means-identity fix already applied.
static void bntx_dds_header (u8 *hdr, uint *hdr_len, uint w, uint h,
	uint ddsk, const uint sel[4], uint payload_size)
{
	static const u8 dxgi_ext[7][4] = {
		{ 0x50, 0x00, 0x00, 0x00 }, // BC4U -> DXGI 80
		{ 0x51, 0x00, 0x00, 0x00 }, // BC4S -> DXGI 81
		{ 0x53, 0x00, 0x00, 0x00 }, // BC5U -> DXGI 83
		{ 0x54, 0x00, 0x00, 0x00 }, // BC5S -> DXGI 84
		{ 0x5f, 0x00, 0x00, 0x00 }, // BC6H_UF16 -> DXGI 95
		{ 0x60, 0x00, 0x00, 0x00 }, // BC6H_SF16 -> DXGI 96
		{ 0x62, 0x00, 0x00, 0x00 }, // BC7 -> DXGI 98
	};

	memset (hdr, 0, 148);
	memcpy (hdr, "DDS ", 4);
	bwr32 (hdr + 4, 124); // header size
	uint flags = 0x00000001 | 0x00001000 | 0x00000004 | 0x00000002;
	bwr32 (hdr + 12, h);
	bwr32 (hdr + 16, w);
	bwr32 (hdr + 28, 1); // mip count
	bwr32 (hdr + 76, 32); // pixel-format size

	const bool compressed = ddsk != BNTX_DDSK_RGBA8 && ddsk != BNTX_DDSK_RGB565
		&& ddsk != BNTX_DDSK_R8 && ddsk != BNTX_DDSK_R8G8;

	if (!compressed)
	{
		// Bitmasks selected per channel like dds.py's compSels tables.
		static const u32 rgb_masks[4][6] = {
			{ 0, 0, 0xff, 0xff00, 0xff0000, 0xff000000 }, // RGBA8
			{ 0, 0, 0xf800, 0x07e0, 0x001f, 0x00000000 }, // RGB565
			{ 0, 0, 0xff, 0x0000, 0x0000, 0x00000000 }, // R8 (luminance)
			{ 0, 0, 0xff, 0xff00, 0x0000, 0x00000000 }, // R8G8 (luminance+alpha)
		};
		const uint row = ddsk == BNTX_DDSK_RGB565 ? 1 : ddsk == BNTX_DDSK_R8 ? 2
			: ddsk == BNTX_DDSK_R8G8 ? 3 : 0;
		const uint fmtbpp = ddsk == BNTX_DDSK_RGBA8 ? 4 : ddsk == BNTX_DDSK_RGB565 ? 2
			: ddsk == BNTX_DDSK_R8 ? 1 : 2;
		const bool luminance = row == 2 || row == 3;
		const bool rgb = row <= 1;
		bool has_alpha = true;
		if (ddsk == BNTX_DDSK_RGB565)
			has_alpha = false;
		else if (ddsk == BNTX_DDSK_R8 && sel[3] != 2)
			has_alpha = false;

		flags |= 0x00000008; // pitch
		uint pflags;
		if (sel[0] != 2 && sel[1] != 2 && sel[2] != 2 && sel[3] == 2)
			pflags = BNTX_DDS_PF_ALPHA_ONLY; // alpha-only image
		else if (luminance)
			pflags = BNTX_DDS_PF_LUMINANCE;
		else if (rgb)
			pflags = BNTX_DDS_PF_RGB;
		else
			pflags = BNTX_DDS_PF_RGB;
		if (has_alpha && pflags != BNTX_DDS_PF_ALPHA_ONLY)
			pflags |= BNTX_DDS_PF_ALPHA;

		bwr32 (hdr + 20, w * fmtbpp); // pitch, like dds.py's size rewrite
		bwr32 (hdr + 80, pflags);
		bwr32 (hdr + 88, fmtbpp << 3); // bit count
		bwr32 (hdr + 92, rgb_masks[row][sel[0] <= 5 ? sel[0] : 2]);
		bwr32 (hdr + 96, rgb_masks[row][sel[1] <= 5 ? sel[1] : 3]);
		bwr32 (hdr + 100, rgb_masks[row][sel[2] <= 5 ? sel[2] : 4]);
		bwr32 (hdr + 104, rgb_masks[row][sel[3] <= 5 ? sel[3] : 5]);
	}
	else
	{
		flags |= 0x00080000; // compressed (linearsize)
		bwr32 (hdr + 20, payload_size);
		bwr32 (hdr + 80, BNTX_DDS_PF_FOURCC);
		uint dxgi = 6; // BC7 default
		switch (ddsk)
		{
			case BNTX_DDSK_BC1:
				memcpy (hdr + 84, "DXT1", 4);
				break;
			case BNTX_DDSK_BC2:
				memcpy (hdr + 84, "DXT3", 4);
				break;
			case BNTX_DDSK_BC3:
				memcpy (hdr + 84, "DXT5", 4);
				break;
			case BNTX_DDSK_BC4U: dxgi = 0; break;
			case BNTX_DDSK_BC4S: dxgi = 1; break;
			case BNTX_DDSK_BC5U: dxgi = 2; break;
			case BNTX_DDSK_BC5S: dxgi = 3; break;
			case BNTX_DDSK_BC6UF: dxgi = 4; break;
			case BNTX_DDSK_BC6SF: dxgi = 5; break;
			default: break; // BC7
		}
		if (ddsk == BNTX_DDSK_BC1 || ddsk == BNTX_DDSK_BC2 || ddsk == BNTX_DDSK_BC3)
		{
			if (hdr_len)
				*hdr_len = 128;
		}
		else
		{
			memcpy (hdr + 84, "DX10", 4);
			memcpy (hdr + 128, dxgi_ext[dxgi], 4);
			bwr32 (hdr + 132, 3); // D3D10_RESOURCE_DIMENSION_TEXTURE2D
			bwr32 (hdr + 136, 0); // misc flag
			bwr32 (hdr + 140, 1); // array size
			bwr32 (hdr + 144, 0); // misc flags 2
			if (hdr_len)
				*hdr_len = 148;
		}
	}

	bwr32 (hdr + 8, flags);
	bwr32 (hdr + 108, 0x00001000); // caps: TEXTURE
	if (!compressed && hdr_len)
		*hdr_len = 128;
}

bool BntxCanNativeExport (const bntx_t *bntx, uint index, bool want_dds)
{
	if (!bntx || index >= bntx->n_textures)
		return false;
	const bntx_texture_t *t = bntx->textures + index;
	if (t->array_count > 1 || t->depth > 1)
		return false;
	uint bpp = 0, blk_w = 1, blk_h = 1, ddsk = 0;
	bool is_astc = false;
	if (!bntx_native_layout (t->format, &bpp, &blk_w, &blk_h, &ddsk, &is_astc))
		return false;
	return want_dds ? !is_astc : is_astc;
}

enumError EncodeBNTXNativeDDS (
	u8 **dest, uint *dest_size, const bntx_t *bntx, uint index)
{
	if (!dest || !bntx || index >= bntx->n_textures)
		return EINVAL;
	const bntx_texture_t *t = bntx->textures + index;

	// Like the reference tool's "Unsupported number of faces" refusal.
	if (t->array_count > 1 || t->depth > 1)
		return ERROR0 (ERR_INVALID_IFORM,
			"Can't export '%s' as DDS: multi-face/array textures are not supported\n",
			t->name);

	uint bpp = 0, blk_w = 1, blk_h = 1, ddsk = 0;
	bool is_astc = false;
	if (!bntx_native_layout (t->format, &bpp, &blk_w, &blk_h, &ddsk, &is_astc) || is_astc)
		return ERROR0 (ERR_INVALID_IFORM,
			"Can't export '%s' as DDS: unsupported BNTX format 0x%04x (%s)\n",
			t->name, t->format, GetBNTXFormatName (t->format));

	const uint size = div_round_up (t->width, blk_w) * div_round_up (t->height, blk_h) * bpp;
	u8 *linear = 0;
	const enumError err = bntx_native_linear (&linear, t, bpp, blk_w, blk_h, size);
	if (err)
		return err;

	// BNTX selectors are 0/1 constants or 2..5 for source R/G/B/A; apply
	// the reference tool's 0-means-identity fix before deriving masks.
	uint sel[4];
	for (uint c = 0; c < 4; c++)
	{
		sel[c] = (t->comp_sel >> (8 * c)) & 0xff;
		if (!sel[c])
			sel[c] = c + 2;
	}

	u8 hdr[148];
	uint hdr_len = 128;
	bntx_dds_header (hdr, &hdr_len, t->width, t->height, ddsk, sel, size);

	u8 *out = MALLOC ((size_t)hdr_len + size);
	if (!out)
	{
		FREE (linear);
		return ERR_CANT_CREATE;
	}
	memcpy (out, hdr, hdr_len);
	memcpy (out + hdr_len, linear, size);
	FREE (linear);

	*dest = out;
	if (dest_size)
		*dest_size = hdr_len + size;
	return ERR_OK;
}

enumError EncodeBNTXNativeASTC (
	u8 **dest, uint *dest_size, const bntx_t *bntx, uint index)
{
	if (!dest || !bntx || index >= bntx->n_textures)
		return EINVAL;
	const bntx_texture_t *t = bntx->textures + index;

	if (t->array_count > 1 || t->depth > 1)
		return ERROR0 (ERR_INVALID_IFORM,
			"Can't export '%s' as ASTC: multi-face/array textures are not supported\n",
			t->name);

	uint bpp = 0, blk_w = 1, blk_h = 1, ddsk = 0;
	bool is_astc = false;
	if (!bntx_native_layout (t->format, &bpp, &blk_w, &blk_h, &ddsk, &is_astc) || !is_astc)
		return ERROR0 (ERR_INVALID_IFORM,
			"Can't export '%s' as ASTC: unsupported BNTX format 0x%04x (%s)\n",
			t->name, t->format, GetBNTXFormatName (t->format));

	const uint size = div_round_up (t->width, blk_w) * div_round_up (t->height, blk_h) * bpp;
	u8 *linear = 0;
	const enumError err = bntx_native_linear (&linear, t, bpp, blk_w, blk_h, size);
	if (err)
		return err;

	// Raw .astc file: 16-byte header + linear blocks, like the reference.
	u8 *out = MALLOC (16 + (size_t)size);
	if (!out)
	{
		FREE (linear);
		return ERR_CANT_CREATE;
	}
	out[0] = 0x13;
	out[1] = 0xab;
	out[2] = 0xa1;
	out[3] = 0x5c;
	out[4] = (u8)blk_w;
	out[5] = (u8)blk_h;
	out[6] = 1;
	out[7] = (u8)(t->width & 0xff);
	out[8] = (u8)((t->width >> 8) & 0xff);
	out[9] = (u8)((t->width >> 16) & 0xff);
	out[10] = (u8)(t->height & 0xff);
	out[11] = (u8)((t->height >> 8) & 0xff);
	out[12] = (u8)((t->height >> 16) & 0xff);
	out[13] = 1;
	out[14] = out[15] = 0;
	memcpy (out + 16, linear, size);
	FREE (linear);

	*dest = out;
	if (dest_size)
		*dest_size = 16 + size;
	return ERR_OK;
}
