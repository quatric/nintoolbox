#include "lib-std.h"
#include "lib-image.h"
#include "lib-image-internal.h"
#include "lib-nintendo.h"
#include "lib-dds.h"
#include "lib-bntx.h"
#include "astc/astc_wrapper.h"
#include "bcn-decoder/bcn_wrapper.h"
#include <math.h>

#define FOURCC_DXT1 0x31545844
#define FOURCC_DXT2 0x32545844
#define FOURCC_DXT3 0x33545844
#define FOURCC_DXT4 0x34545844
#define FOURCC_DXT5 0x35545844
#define FOURCC_ATI1 0x31495441
#define FOURCC_BC4U 0x55344342
#define FOURCC_BC4S 0x53344342
#define FOURCC_ATI2 0x32495441
#define FOURCC_BC5U 0x55354342
#define FOURCC_BC5S 0x53354342
#define FOURCC_DX10 0x30315844
#define FOURCC_RXGB 0x42475852
#define FOURCC_R16F 0x0000006f
#define FOURCC_G16R16F 0x00000070
#define FOURCC_A16B16G16R16F 0x00000071
#define FOURCC_R32F 0x00000072
#define FOURCC_A32B32G32R32F 0x00000074

#define DDPF_ALPHAPIXELS 0x00000001
#define DDPF_ALPHA 0x00000002
#define DDPF_FOURCC 0x00000004
#define DDPF_RGB 0x00000040
#define DDPF_YUV 0x00000200
#define DDPF_LUMINANCE 0x00020000

bool IsDDS (const u8 *data, uint size)
{
	if (!data || size < 128)
		return false;
	return rd_le32 (data) == 0x20534444; // "DDS "
}

static inline u8 float_to_u8 (float v)
{
	if (!(v > 0.0f))
		return 0;
	if (v >= 1.0f)
		return 255;
	return (u8)(v * 255.0f + 0.5f);
}

static inline float half_to_float (u16 value)
{
	const uint exponent = (value >> 10) & 31, mantissa = value & 1023;
	float result;
	if (!exponent)
		result = mantissa ? ldexpf ((float)mantissa, -24) : 0.0f;
	else if (exponent == 31)
		result = mantissa ? 0.0f : INFINITY;
	else
		result = ldexpf (1.0f + (float)mantissa / 1024.0f, (int)exponent - 15);
	return (value & 0x8000) ? -result : result;
}

static inline float unsigned_float_component (uint value, uint mantissa_bits)
{
	const uint mantissa_mask = (1u << mantissa_bits) - 1;
	const uint exponent = (value >> mantissa_bits) & 31, mantissa = value & mantissa_mask;
	if (!exponent)
		return mantissa ? ldexpf ((float)mantissa, 1 - 15 - (int)mantissa_bits) : 0.0f;
	if (exponent == 31)
		return mantissa ? 0.0f : INFINITY;
	return ldexpf (1.0f + (float)mantissa / (1u << mantissa_bits), (int)exponent - 15);
}

static inline u8 mask_to_8 (u32 val, u32 mask)
{
	if (!mask)
		return 0;
	int shift = __builtin_ctz (mask);
	val = (val & mask) >> shift;
	u32 max_val = mask >> shift;
	if (max_val == 0)
		return 0;
	return (u8)(((u64)val * 255 + (max_val / 2)) / max_val);
}

enumError DecodeDDS_RGBA (u8 **dest, uint *width, uint *height, const u8 *data, uint size)
{
	if (!dest || !width || !height || !data || size < 128)
		return EINVAL;

	if (!IsDDS (data, size))
		return ERROR0 (ERR_INVALID_DATA, "Not a DDS image (missing 'DDS ' magic)\n");

	const uint hdr_size = rd_le32 (data + 4);
	if (hdr_size != 124)
		return ERROR0 (ERR_INVALID_DATA, "Invalid DDS header size %u (expected 124)\n", hdr_size);

	const uint h = rd_le32 (data + 12);
	const uint w = rd_le32 (data + 16);

	if (w == 0 || h == 0 || w > 16384 || h > 16384)
		return ERROR0 (ERR_INVALID_DATA, "Invalid DDS dimensions %ux%u\n", w, h);

	const uint pf_flags = rd_le32 (data + 80);
	const uint fourcc = rd_le32 (data + 84);
	const uint rgb_bit_count = rd_le32 (data + 88);
	const uint r_mask = rd_le32 (data + 92);
	const uint g_mask = rd_le32 (data + 96);
	const uint b_mask = rd_le32 (data + 100);
	const uint a_mask = rd_le32 (data + 104);

	uint payload_offset = 128;
	uint dxgi_fmt = 0;

	if ((pf_flags & DDPF_FOURCC) && fourcc == FOURCC_DX10)
	{
		if (size < 148)
			return ERROR0 (ERR_INVALID_DATA, "DDS DX10 header truncated\n");
		dxgi_fmt = rd_le32 (data + 128);
		payload_offset = 148;
	}

	const u8 *payload = data + payload_offset;
	const uint payload_size = size - payload_offset;

	u8 *rgba = CALLOC (1, (size_t)w * h * 4);
	if (!rgba)
		return ERR_CANT_CREATE;

	// Handle DX10 formats or FourCC
	if ((pf_flags & DDPF_FOURCC) && fourcc == FOURCC_DX10)
	{
		switch (dxgi_fmt)
		{
			// BC1
			case 70: // BC1_TYPELESS
			case 71: // BC1_UNORM
			case 72: // BC1_UNORM_SRGB
			{
				const uint bw = (w + 3) / 4, bh = (h + 3) / 4;
				if ((u64)bw * bh * 8 > payload_size)
				{
					FREE (rgba);
					return ERROR0 (ERR_INVALID_DATA, "DDS BC1 payload truncated\n");
				}
				for (uint by = 0; by < bh; by++)
					for (uint bx = 0; bx < bw; bx++)
					{
						u8 block[64];
						decode_bc1_block (payload + ((u64)by * bw + bx) * 8, block, true);
						for (uint py = 0; py < 4 && by * 4 + py < h; py++)
							for (uint px = 0; px < 4 && bx * 4 + px < w; px++)
								memcpy (rgba + 4 * ((by * 4 + py) * w + (bx * 4 + px)),
									block + 4 * (py * 4 + px), 4);
					}
				break;
			}
			// BC2
			case 73: // BC2_TYPELESS
			case 74: // BC2_UNORM
			case 75: // BC2_UNORM_SRGB
			{
				const uint bw = (w + 3) / 4, bh = (h + 3) / 4;
				if ((u64)bw * bh * 16 > payload_size)
				{
					FREE (rgba);
					return ERROR0 (ERR_INVALID_DATA, "DDS BC2 payload truncated\n");
				}
				for (uint by = 0; by < bh; by++)
					for (uint bx = 0; bx < bw; bx++)
					{
						u8 block[64];
						decode_bc2_block (payload + ((u64)by * bw + bx) * 16, block);
						for (uint py = 0; py < 4 && by * 4 + py < h; py++)
							for (uint px = 0; px < 4 && bx * 4 + px < w; px++)
								memcpy (rgba + 4 * ((by * 4 + py) * w + (bx * 4 + px)),
									block + 4 * (py * 4 + px), 4);
					}
				break;
			}
			// BC3
			case 76: // BC3_TYPELESS
			case 77: // BC3_UNORM
			case 78: // BC3_UNORM_SRGB
			{
				const uint bw = (w + 3) / 4, bh = (h + 3) / 4;
				if ((u64)bw * bh * 16 > payload_size)
				{
					FREE (rgba);
					return ERROR0 (ERR_INVALID_DATA, "DDS BC3 payload truncated\n");
				}
				for (uint by = 0; by < bh; by++)
					for (uint bx = 0; bx < bw; bx++)
					{
						u8 block[64];
						decode_bc3_block (payload + ((u64)by * bw + bx) * 16, block);
						for (uint py = 0; py < 4 && by * 4 + py < h; py++)
							for (uint px = 0; px < 4 && bx * 4 + px < w; px++)
								memcpy (rgba + 4 * ((by * 4 + py) * w + (bx * 4 + px)),
									block + 4 * (py * 4 + px), 4);
					}
				break;
			}
			// BC4
			case 79: // BC4_TYPELESS
			case 80: // BC4_UNORM
			{
				const uint bw = (w + 3) / 4, bh = (h + 3) / 4;
				if ((u64)bw * bh * 8 > payload_size)
				{
					FREE (rgba);
					return ERROR0 (ERR_INVALID_DATA, "DDS BC4 payload truncated\n");
				}
				for (uint by = 0; by < bh; by++)
					for (uint bx = 0; bx < bw; bx++)
					{
						u8 block[64];
						decode_bc4_block (payload + ((u64)by * bw + bx) * 8, block);
						for (uint py = 0; py < 4 && by * 4 + py < h; py++)
							for (uint px = 0; px < 4 && bx * 4 + px < w; px++)
								memcpy (rgba + 4 * ((by * 4 + py) * w + (bx * 4 + px)),
									block + 4 * (py * 4 + px), 4);
					}
				break;
			}
			case 81: // BC4_SNORM
			{
				const uint bw = (w + 3) / 4, bh = (h + 3) / 4;
				if ((u64)bw * bh * 8 > payload_size)
				{
					FREE (rgba);
					return ERROR0 (ERR_INVALID_DATA, "DDS BC4_SNORM payload truncated\n");
				}
				for (uint by = 0; by < bh; by++)
					for (uint bx = 0; bx < bw; bx++)
					{
						u8 block[64];
						decode_bc4_signed_block (payload + ((u64)by * bw + bx) * 8, block);
						for (uint py = 0; py < 4 && by * 4 + py < h; py++)
							for (uint px = 0; px < 4 && bx * 4 + px < w; px++)
								memcpy (rgba + 4 * ((by * 4 + py) * w + (bx * 4 + px)),
									block + 4 * (py * 4 + px), 4);
					}
				break;
			}
			// BC5
			case 82: // BC5_TYPELESS
			case 83: // BC5_UNORM
			{
				const uint bw = (w + 3) / 4, bh = (h + 3) / 4;
				if ((u64)bw * bh * 16 > payload_size)
				{
					FREE (rgba);
					return ERROR0 (ERR_INVALID_DATA, "DDS BC5 payload truncated\n");
				}
				for (uint by = 0; by < bh; by++)
					for (uint bx = 0; bx < bw; bx++)
					{
						u8 block[64];
						decode_bc5_block (payload + ((u64)by * bw + bx) * 16, block);
						for (uint py = 0; py < 4 && by * 4 + py < h; py++)
							for (uint px = 0; px < 4 && bx * 4 + px < w; px++)
								memcpy (rgba + 4 * ((by * 4 + py) * w + (bx * 4 + px)),
									block + 4 * (py * 4 + px), 4);
					}
				break;
			}
			case 84: // BC5_SNORM
			{
				const uint bw = (w + 3) / 4, bh = (h + 3) / 4;
				if ((u64)bw * bh * 16 > payload_size)
				{
					FREE (rgba);
					return ERROR0 (ERR_INVALID_DATA, "DDS BC5_SNORM payload truncated\n");
				}
				for (uint by = 0; by < bh; by++)
					for (uint bx = 0; bx < bw; bx++)
					{
						u8 block[64];
						decode_bc5_signed_block (payload + ((u64)by * bw + bx) * 16, block);
						for (uint py = 0; py < 4 && by * 4 + py < h; py++)
							for (uint px = 0; px < 4 && bx * 4 + px < w; px++)
								memcpy (rgba + 4 * ((by * 4 + py) * w + (bx * 4 + px)),
									block + 4 * (py * 4 + px), 4);
					}
				break;
			}
			// BC6H
			case 94: // BC6H_TYPELESS
			case 95: // BC6H_UF16
			case 96: // BC6H_SF16
				szs_decode_bc6 (payload, w, h, dxgi_fmt == 96, rgba);
				break;

			// BC7
			case 97: // BC7_TYPELESS
			case 98: // BC7_UNORM
			case 99: // BC7_UNORM_SRGB
				szs_decode_bc7 (payload, w, h, rgba);
				break;

			// ASTC
			case 134:
			case 135: // 4x4
			case 138:
			case 139: // 5x4
			case 142:
			case 143: // 5x5
			case 146:
			case 147: // 6x5
			case 150:
			case 151: // 6x6
			case 154:
			case 155: // 8x5
			case 158:
			case 159: // 8x6
			case 162:
			case 163: // 8x8
			case 166:
			case 167: // 10x5
			case 170:
			case 171: // 10x6
			case 174:
			case 175: // 10x8
			case 178:
			case 179: // 10x10
			case 182:
			case 183: // 12x10
			case 186:
			case 187: // 12x12
			{
				uint blk_w = 4, blk_h = 4;
				if (dxgi_fmt <= 135)
				{
					blk_w = 4;
					blk_h = 4;
				}
				else if (dxgi_fmt <= 139)
				{
					blk_w = 5;
					blk_h = 4;
				}
				else if (dxgi_fmt <= 143)
				{
					blk_w = 5;
					blk_h = 5;
				}
				else if (dxgi_fmt <= 147)
				{
					blk_w = 6;
					blk_h = 5;
				}
				else if (dxgi_fmt <= 151)
				{
					blk_w = 6;
					blk_h = 6;
				}
				else if (dxgi_fmt <= 155)
				{
					blk_w = 8;
					blk_h = 5;
				}
				else if (dxgi_fmt <= 159)
				{
					blk_w = 8;
					blk_h = 6;
				}
				else if (dxgi_fmt <= 163)
				{
					blk_w = 8;
					blk_h = 8;
				}
				else if (dxgi_fmt <= 167)
				{
					blk_w = 10;
					blk_h = 5;
				}
				else if (dxgi_fmt <= 171)
				{
					blk_w = 10;
					blk_h = 6;
				}
				else if (dxgi_fmt <= 175)
				{
					blk_w = 10;
					blk_h = 8;
				}
				else if (dxgi_fmt <= 179)
				{
					blk_w = 10;
					blk_h = 10;
				}
				else if (dxgi_fmt <= 183)
				{
					blk_w = 12;
					blk_h = 10;
				}
				else
				{
					blk_w = 12;
					blk_h = 12;
				}

				const uint bw = (w + blk_w - 1) / blk_w;
				const uint bh = (h + blk_h - 1) / blk_h;
				u8 blk_rgba[12 * 12 * 4];

				for (uint by = 0; by < bh; by++)
					for (uint bx = 0; bx < bw; bx++)
					{
						const u8 *bdata = payload + ((u64)by * bw + bx) * 16;
						astc_decompress_block (blk_rgba, bdata, blk_w, blk_h);
						for (uint py = 0; py < blk_h && by * blk_h + py < h; py++)
							for (uint px = 0; px < blk_w && bx * blk_w + px < w; px++)
								memcpy (rgba + 4 * ((by * blk_h + py) * w + (bx * blk_w + px)),
									blk_rgba + 4 * (py * blk_w + px), 4);
					}
				break;
			}

			// RGBA8 / BGRA8 / BGRX8
			case 27: // RGBA8_TYPELESS
			case 28: // RGBA8_UNORM
			case 29: // RGBA8_UNORM_SRGB
			case 30: // RGBA8_UINT
				memcpy (rgba, payload,
					(size_t)w * h * 4 < payload_size ? (size_t)w * h * 4 : payload_size);
				break;

			case 87: // BGRA8_UNORM
			case 90: // BGRA8_TYPELESS
			case 91: // BGRA8_UNORM_SRGB
				for (size_t i = 0; i < (size_t)w * h && (i + 1) * 4 <= payload_size; i++)
				{
					rgba[i * 4 + 0] = payload[i * 4 + 2];
					rgba[i * 4 + 1] = payload[i * 4 + 1];
					rgba[i * 4 + 2] = payload[i * 4 + 0];
					rgba[i * 4 + 3] = payload[i * 4 + 3];
				}
				break;

			case 88: // BGRX8_UNORM
			case 92: // BGRX8_TYPELESS
			case 93: // BGRX8_UNORM_SRGB
				for (size_t i = 0; i < (size_t)w * h && (i + 1) * 4 <= payload_size; i++)
				{
					rgba[i * 4 + 0] = payload[i * 4 + 2];
					rgba[i * 4 + 1] = payload[i * 4 + 1];
					rgba[i * 4 + 2] = payload[i * 4 + 0];
					rgba[i * 4 + 3] = 255;
				}
				break;

			// R10G10B10A2
			case 24: // R10G10B10A2_UNORM
			case 25: // R10G10B10A2_UINT
				for (size_t i = 0; i < (size_t)w * h && (i + 1) * 4 <= payload_size; i++)
				{
					u32 val = rd_le32 (payload + i * 4);
					rgba[i * 4 + 0] = (u8)(((val & 0x3ff) * 255 + 511) / 1023);
					rgba[i * 4 + 1] = (u8)((((val >> 10) & 0x3ff) * 255 + 511) / 1023);
					rgba[i * 4 + 2] = (u8)((((val >> 20) & 0x3ff) * 255 + 511) / 1023);
					rgba[i * 4 + 3] = (u8)((((val >> 30) & 0x3) * 255 + 1) / 3);
				}
				break;

			// R11G11B10_FLOAT
			case 26:
				for (size_t i = 0; i < (size_t)w * h && (i + 1) * 4 <= payload_size; i++)
				{
					u32 val = rd_le32 (payload + i * 4);
					rgba[i * 4 + 0] = float_to_u8 (unsigned_float_component (val & 0x7ff, 6));
					rgba[i * 4 + 1]
						= float_to_u8 (unsigned_float_component ((val >> 11) & 0x7ff, 6));
					rgba[i * 4 + 2]
						= float_to_u8 (unsigned_float_component ((val >> 22) & 0x3ff, 5));
					rgba[i * 4 + 3] = 255;
				}
				break;

			// R9G9B9E5_SHAREDEXP
			case 67:
				for (size_t i = 0; i < (size_t)w * h && (i + 1) * 4 <= payload_size; i++)
				{
					u32 val = rd_le32 (payload + i * 4);
					int r = val & 0x1ff;
					int g = (val >> 9) & 0x1ff;
					int b = (val >> 18) & 0x1ff;
					int e = (int)((val >> 27) & 0x1f) - 24;
					float scale = ldexpf (1.0f, e);
					rgba[i * 4 + 0] = float_to_u8 (r * scale);
					rgba[i * 4 + 1] = float_to_u8 (g * scale);
					rgba[i * 4 + 2] = float_to_u8 (b * scale);
					rgba[i * 4 + 3] = 255;
				}
				break;

			// Half float
			case 10: // RGBA16_FLOAT
				for (size_t i = 0; i < (size_t)w * h && (i + 1) * 8 <= payload_size; i++)
				{
					rgba[i * 4 + 0] = float_to_u8 (half_to_float (rd_le16 (payload + i * 8)));
					rgba[i * 4 + 1] = float_to_u8 (half_to_float (rd_le16 (payload + i * 8 + 2)));
					rgba[i * 4 + 2] = float_to_u8 (half_to_float (rd_le16 (payload + i * 8 + 4)));
					rgba[i * 4 + 3] = float_to_u8 (half_to_float (rd_le16 (payload + i * 8 + 6)));
				}
				break;

			case 34: // RG16_FLOAT
				for (size_t i = 0; i < (size_t)w * h && (i + 1) * 4 <= payload_size; i++)
				{
					rgba[i * 4 + 0] = float_to_u8 (half_to_float (rd_le16 (payload + i * 4)));
					rgba[i * 4 + 1] = float_to_u8 (half_to_float (rd_le16 (payload + i * 4 + 2)));
					rgba[i * 4 + 2] = 0;
					rgba[i * 4 + 3] = 255;
				}
				break;

			case 54: // R16_FLOAT
				for (size_t i = 0; i < (size_t)w * h && (i + 1) * 2 <= payload_size; i++)
				{
					u8 c = float_to_u8 (half_to_float (rd_le16 (payload + i * 2)));
					rgba[i * 4 + 0] = rgba[i * 4 + 1] = rgba[i * 4 + 2] = c;
					rgba[i * 4 + 3] = 255;
				}
				break;

			// Single float
			case 2: // RGBA32_FLOAT
				for (size_t i = 0; i < (size_t)w * h && (i + 1) * 16 <= payload_size; i++)
				{
					float rf, gf, bf, af;
					memcpy (&rf, payload + i * 16, 4);
					memcpy (&gf, payload + i * 16 + 4, 4);
					memcpy (&bf, payload + i * 16 + 8, 4);
					memcpy (&af, payload + i * 16 + 12, 4);
					rgba[i * 4 + 0] = float_to_u8 (rf);
					rgba[i * 4 + 1] = float_to_u8 (gf);
					rgba[i * 4 + 2] = float_to_u8 (bf);
					rgba[i * 4 + 3] = float_to_u8 (af);
				}
				break;

			case 41: // R32_FLOAT
			case 40: // D32_FLOAT
				for (size_t i = 0; i < (size_t)w * h && (i + 1) * 4 <= payload_size; i++)
				{
					float rf;
					memcpy (&rf, payload + i * 4, 4);
					u8 c = float_to_u8 (rf);
					rgba[i * 4 + 0] = rgba[i * 4 + 1] = rgba[i * 4 + 2] = c;
					rgba[i * 4 + 3] = 255;
				}
				break;

			// R8 / RG8
			case 49: // RG8_UNORM
				for (size_t i = 0; i < (size_t)w * h && (i + 1) * 2 <= payload_size; i++)
				{
					rgba[i * 4 + 0] = payload[i * 2 + 0];
					rgba[i * 4 + 1] = payload[i * 2 + 1];
					rgba[i * 4 + 2] = 0;
					rgba[i * 4 + 3] = 255;
				}
				break;

			case 61: // R8_UNORM
				for (size_t i = 0; i < (size_t)w * h && i < payload_size; i++)
				{
					u8 c = payload[i];
					rgba[i * 4 + 0] = rgba[i * 4 + 1] = rgba[i * 4 + 2] = c;
					rgba[i * 4 + 3] = 255;
				}
				break;

			case 65: // A8_UNORM
				for (size_t i = 0; i < (size_t)w * h && i < payload_size; i++)
				{
					rgba[i * 4 + 0] = rgba[i * 4 + 1] = rgba[i * 4 + 2] = 255;
					rgba[i * 4 + 3] = payload[i];
				}
				break;

			default:
				FREE (rgba);
				return ERROR0 (ERR_INVALID_DATA, "Unsupported DXGI format %u in DDS\n", dxgi_fmt);
		}
	}
	else if (pf_flags & DDPF_FOURCC)
	{
		switch (fourcc)
		{
			case FOURCC_DXT1:
			{
				const uint bw = (w + 3) / 4, bh = (h + 3) / 4;
				if ((u64)bw * bh * 8 > payload_size)
				{
					FREE (rgba);
					return ERROR0 (ERR_INVALID_DATA, "DDS DXT1 payload truncated\n");
				}
				for (uint by = 0; by < bh; by++)
					for (uint bx = 0; bx < bw; bx++)
					{
						u8 block[64];
						decode_bc1_block (payload + ((u64)by * bw + bx) * 8, block, true);
						for (uint py = 0; py < 4 && by * 4 + py < h; py++)
							for (uint px = 0; px < 4 && bx * 4 + px < w; px++)
								memcpy (rgba + 4 * ((by * 4 + py) * w + (bx * 4 + px)),
									block + 4 * (py * 4 + px), 4);
					}
				break;
			}
			case FOURCC_DXT2:
			case FOURCC_DXT3:
			{
				const uint bw = (w + 3) / 4, bh = (h + 3) / 4;
				if ((u64)bw * bh * 16 > payload_size)
				{
					FREE (rgba);
					return ERROR0 (ERR_INVALID_DATA, "DDS DXT3 payload truncated\n");
				}
				for (uint by = 0; by < bh; by++)
					for (uint bx = 0; bx < bw; bx++)
					{
						u8 block[64];
						decode_bc2_block (payload + ((u64)by * bw + bx) * 16, block);
						for (uint py = 0; py < 4 && by * 4 + py < h; py++)
							for (uint px = 0; px < 4 && bx * 4 + px < w; px++)
								memcpy (rgba + 4 * ((by * 4 + py) * w + (bx * 4 + px)),
									block + 4 * (py * 4 + px), 4);
					}
				break;
			}
			case FOURCC_DXT4:
			case FOURCC_DXT5:
			{
				const uint bw = (w + 3) / 4, bh = (h + 3) / 4;
				if ((u64)bw * bh * 16 > payload_size)
				{
					FREE (rgba);
					return ERROR0 (ERR_INVALID_DATA, "DDS DXT5 payload truncated\n");
				}
				for (uint by = 0; by < bh; by++)
					for (uint bx = 0; bx < bw; bx++)
					{
						u8 block[64];
						decode_bc3_block (payload + ((u64)by * bw + bx) * 16, block);
						for (uint py = 0; py < 4 && by * 4 + py < h; py++)
							for (uint px = 0; px < 4 && bx * 4 + px < w; px++)
								memcpy (rgba + 4 * ((by * 4 + py) * w + (bx * 4 + px)),
									block + 4 * (py * 4 + px), 4);
					}
				break;
			}
			case FOURCC_ATI1:
			case FOURCC_BC4U:
			{
				const uint bw = (w + 3) / 4, bh = (h + 3) / 4;
				if ((u64)bw * bh * 8 > payload_size)
				{
					FREE (rgba);
					return ERROR0 (ERR_INVALID_DATA, "DDS BC4 payload truncated\n");
				}
				for (uint by = 0; by < bh; by++)
					for (uint bx = 0; bx < bw; bx++)
					{
						u8 block[64];
						decode_bc4_block (payload + ((u64)by * bw + bx) * 8, block);
						for (uint py = 0; py < 4 && by * 4 + py < h; py++)
							for (uint px = 0; px < 4 && bx * 4 + px < w; px++)
								memcpy (rgba + 4 * ((by * 4 + py) * w + (bx * 4 + px)),
									block + 4 * (py * 4 + px), 4);
					}
				break;
			}
			case FOURCC_BC4S:
			{
				const uint bw = (w + 3) / 4, bh = (h + 3) / 4;
				if ((u64)bw * bh * 8 > payload_size)
				{
					FREE (rgba);
					return ERROR0 (ERR_INVALID_DATA, "DDS BC4S payload truncated\n");
				}
				for (uint by = 0; by < bh; by++)
					for (uint bx = 0; bx < bw; bx++)
					{
						u8 block[64];
						decode_bc4_signed_block (payload + ((u64)by * bw + bx) * 8, block);
						for (uint py = 0; py < 4 && by * 4 + py < h; py++)
							for (uint px = 0; px < 4 && bx * 4 + px < w; px++)
								memcpy (rgba + 4 * ((by * 4 + py) * w + (bx * 4 + px)),
									block + 4 * (py * 4 + px), 4);
					}
				break;
			}
			case FOURCC_ATI2:
			case FOURCC_BC5U:
			{
				const uint bw = (w + 3) / 4, bh = (h + 3) / 4;
				if ((u64)bw * bh * 16 > payload_size)
				{
					FREE (rgba);
					return ERROR0 (ERR_INVALID_DATA, "DDS BC5 payload truncated\n");
				}
				for (uint by = 0; by < bh; by++)
					for (uint bx = 0; bx < bw; bx++)
					{
						u8 block[64];
						decode_bc5_block (payload + ((u64)by * bw + bx) * 16, block);
						for (uint py = 0; py < 4 && by * 4 + py < h; py++)
							for (uint px = 0; px < 4 && bx * 4 + px < w; px++)
								memcpy (rgba + 4 * ((by * 4 + py) * w + (bx * 4 + px)),
									block + 4 * (py * 4 + px), 4);
					}
				break;
			}
			case FOURCC_BC5S:
			{
				const uint bw = (w + 3) / 4, bh = (h + 3) / 4;
				if ((u64)bw * bh * 16 > payload_size)
				{
					FREE (rgba);
					return ERROR0 (ERR_INVALID_DATA, "DDS BC5S payload truncated\n");
				}
				for (uint by = 0; by < bh; by++)
					for (uint bx = 0; bx < bw; bx++)
					{
						u8 block[64];
						decode_bc5_signed_block (payload + ((u64)by * bw + bx) * 16, block);
						for (uint py = 0; py < 4 && by * 4 + py < h; py++)
							for (uint px = 0; px < 4 && bx * 4 + px < w; px++)
								memcpy (rgba + 4 * ((by * 4 + py) * w + (bx * 4 + px)),
									block + 4 * (py * 4 + px), 4);
					}
				break;
			}
			case FOURCC_RXGB:
			{
				const uint bw = (w + 3) / 4, bh = (h + 3) / 4;
				if ((u64)bw * bh * 16 > payload_size)
				{
					FREE (rgba);
					return ERROR0 (ERR_INVALID_DATA, "DDS RXGB payload truncated\n");
				}
				for (uint by = 0; by < bh; by++)
					for (uint bx = 0; bx < bw; bx++)
					{
						u8 block[64];
						decode_bc3_block (payload + ((u64)by * bw + bx) * 16, block);
						for (uint py = 0; py < 4 && by * 4 + py < h; py++)
							for (uint px = 0; px < 4 && bx * 4 + px < w; px++)
							{
								const u8 *src_px = block + 4 * (py * 4 + px);
								u8 *dst_px = rgba + 4 * ((by * 4 + py) * w + (bx * 4 + px));
								dst_px[0] = src_px[3]; // R from A
								dst_px[1] = src_px[1]; // G from G
								dst_px[2] = 255;
								dst_px[3] = 255;
							}
					}
				break;
			}
			case FOURCC_R16F:
				for (size_t i = 0; i < (size_t)w * h && (i + 1) * 2 <= payload_size; i++)
				{
					u8 c = float_to_u8 (half_to_float (rd_le16 (payload + i * 2)));
					rgba[i * 4 + 0] = rgba[i * 4 + 1] = rgba[i * 4 + 2] = c;
					rgba[i * 4 + 3] = 255;
				}
				break;
			case FOURCC_G16R16F:
				for (size_t i = 0; i < (size_t)w * h && (i + 1) * 4 <= payload_size; i++)
				{
					rgba[i * 4 + 0] = float_to_u8 (half_to_float (rd_le16 (payload + i * 4)));
					rgba[i * 4 + 1] = float_to_u8 (half_to_float (rd_le16 (payload + i * 4 + 2)));
					rgba[i * 4 + 2] = 0;
					rgba[i * 4 + 3] = 255;
				}
				break;
			case FOURCC_A16B16G16R16F:
				for (size_t i = 0; i < (size_t)w * h && (i + 1) * 8 <= payload_size; i++)
				{
					rgba[i * 4 + 0] = float_to_u8 (half_to_float (rd_le16 (payload + i * 8)));
					rgba[i * 4 + 1] = float_to_u8 (half_to_float (rd_le16 (payload + i * 8 + 2)));
					rgba[i * 4 + 2] = float_to_u8 (half_to_float (rd_le16 (payload + i * 8 + 4)));
					rgba[i * 4 + 3] = float_to_u8 (half_to_float (rd_le16 (payload + i * 8 + 6)));
				}
				break;
			case FOURCC_R32F:
				for (size_t i = 0; i < (size_t)w * h && (i + 1) * 4 <= payload_size; i++)
				{
					float rf;
					memcpy (&rf, payload + i * 4, 4);
					u8 c = float_to_u8 (rf);
					rgba[i * 4 + 0] = rgba[i * 4 + 1] = rgba[i * 4 + 2] = c;
					rgba[i * 4 + 3] = 255;
				}
				break;
			case FOURCC_A32B32G32R32F:
				for (size_t i = 0; i < (size_t)w * h && (i + 1) * 16 <= payload_size; i++)
				{
					float rf, gf, bf, af;
					memcpy (&rf, payload + i * 16, 4);
					memcpy (&gf, payload + i * 16 + 4, 4);
					memcpy (&bf, payload + i * 16 + 8, 4);
					memcpy (&af, payload + i * 16 + 12, 4);
					rgba[i * 4 + 0] = float_to_u8 (rf);
					rgba[i * 4 + 1] = float_to_u8 (gf);
					rgba[i * 4 + 2] = float_to_u8 (bf);
					rgba[i * 4 + 3] = float_to_u8 (af);
				}
				break;
			default:
				FREE (rgba);
				return ERROR0 (ERR_INVALID_DATA, "Unsupported DDS FourCC 0x%08x\n", fourcc);
		}
	}
	else // Uncompressed format via bitmasks
	{
		const uint bytes_per_pixel = (rgb_bit_count + 7) / 8;
		if (bytes_per_pixel == 0 || (u64)w * h * bytes_per_pixel > payload_size)
		{
			FREE (rgba);
			return ERROR0 (ERR_INVALID_DATA, "DDS uncompressed payload truncated\n");
		}

		const bool has_alpha = (pf_flags & DDPF_ALPHAPIXELS) || (a_mask != 0);
		const bool is_luminance = (pf_flags & DDPF_LUMINANCE);
		const bool is_alpha_only
			= (pf_flags & DDPF_ALPHA) && !is_luminance && !(pf_flags & DDPF_RGB);

		for (size_t i = 0; i < (size_t)w * h; i++)
		{
			const u8 *p = payload + i * bytes_per_pixel;
			u32 val = 0;
			for (uint b = 0; b < bytes_per_pixel; b++)
				val |= (u32)p[b] << (b * 8);

			if (is_alpha_only)
			{
				rgba[i * 4 + 0] = 255;
				rgba[i * 4 + 1] = 255;
				rgba[i * 4 + 2] = 255;
				rgba[i * 4 + 3] = mask_to_8 (val, a_mask ? a_mask : 0xff);
			}
			else if (is_luminance)
			{
				u8 lum = mask_to_8 (val, r_mask ? r_mask : 0xff);
				rgba[i * 4 + 0] = lum;
				rgba[i * 4 + 1] = lum;
				rgba[i * 4 + 2] = lum;
				rgba[i * 4 + 3] = has_alpha ? mask_to_8 (val, a_mask) : 255;
			}
			else
			{
				rgba[i * 4 + 0] = mask_to_8 (val, r_mask);
				rgba[i * 4 + 1] = mask_to_8 (val, g_mask);
				rgba[i * 4 + 2] = mask_to_8 (val, b_mask);
				rgba[i * 4 + 3] = has_alpha ? mask_to_8 (val, a_mask) : 255;
			}
		}
	}

	*dest = rgba;
	*width = w;
	*height = h;
	return ERR_OK;
}

enumError EncodeDDS_RGBA (
	u8 **dest, uint *dest_size, const u8 *rgba, uint width, uint height, uint dxgi_fmt)
{
	if (!dest || !dest_size || !rgba || !width || !height)
		return EINVAL;

	const size_t pixel_data_size = (size_t)width * height * 4;
	const size_t total_size = 128 + pixel_data_size;

	u8 *out = CALLOC (1, total_size);
	if (!out)
		return ERR_CANT_CREATE;

	// Magic "DDS "
	wr_le32 (out + 0, 0x20534444);

	// Header size
	wr_le32 (out + 4, 124);

	// Flags: DDSD_CAPS | DDSD_HEIGHT | DDSD_WIDTH | DDSD_PIXELFORMAT | DDSD_PITCH
	wr_le32 (out + 8, 0x0000100f);

	wr_le32 (out + 12, height);
	wr_le32 (out + 16, width);
	wr_le32 (out + 20, width * 4); // Pitch
	wr_le32 (out + 24, 0); // Depth
	wr_le32 (out + 28, 1); // MipMapCount

	// Pixel format (offset 76)
	wr_le32 (out + 76, 32); // pf_size
	wr_le32 (out + 80, DDPF_RGB | DDPF_ALPHAPIXELS); // pf_flags
	wr_le32 (out + 84, 0); // FourCC
	wr_le32 (out + 88, 32); // rgb_bit_count
	wr_le32 (out + 92, 0x000000ff); // r_mask
	wr_le32 (out + 96, 0x0000ff00); // g_mask
	wr_le32 (out + 100, 0x00ff0000); // b_mask
	wr_le32 (out + 104, 0xff000000); // a_mask

	// Caps (offset 108)
	wr_le32 (out + 108, 0x1000); // DDSCAPS_TEXTURE

	// Copy pixels directly as RGBA
	memcpy (out + 128, rgba, pixel_data_size);

	*dest = out;
	*dest_size = (uint)total_size;
	return ERR_OK;
}

enumError SaveDDS (Image_t *img, ccp dest, ccp source)
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
	enumError err = EncodeDDS_RGBA (&data, &size, rgba, img->width, img->height, 0);
	FREE (rgba);
	if (err)
		return ERROR0 (ERR_INVALID_DATA, "Can't encode DDS image: %s\n", source);

	File_t F;
	err = CreateFileOpt (&F, true, dest, false, source);
	if (F.f && fwrite (data, 1, size, F.f) != size)
		err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", size, dest);
	ResetFile (&F, opt_preserve);
	FREE (data);
	return err;
}

enumError SaveDDSFile (Image_t *img, FILE *fo, ccp path, bool overwrite)
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
	enumError err = EncodeDDS_RGBA (&data, &size, rgba, img->width, img->height, 0);
	FREE (rgba);
	if (err)
		return err;

	err = SaveImageBuffer (img, fo, path, overwrite, data, size, "DDS");
	FREE (data);
	return err;
}
