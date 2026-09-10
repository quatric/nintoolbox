// NDS Reverse Overlay compression (LZOvl) -- split out of lib-nintendo.c.

#include "lib-std.h"
#include "lib-nintendo.h"

//-----------------------------------------------------------------------------
///////////////		NDS Reverse Overlay Compression (LZOvl)	///////////////
//-----------------------------------------------------------------------------

int CxIsCompressedLZOvl (const unsigned char *src, unsigned int size)
{
	if (!src || size < 8)
		return 0;
	// LZOvl is raw NDS overlay data with no magic of its own; it is recognised
	// purely from a plausible trailer.  Callers must already have gated this on
	// an explicit .ovl filename (see DetectNintendoFormat), so the long list of
	// container magics that used to be excluded here is no longer needed.
	const u32 extra = (u32)src[size - 4] | ((u32)src[size - 3] << 8) | ((u32)src[size - 2] << 16)
		| ((u32)src[size - 1] << 24);
	if (extra == 0 || extra > NFMT_MAX_OUTPUT)
		return 0;
	const u8 hdr_len = src[size - 5];
	if (hdr_len < 8 || hdr_len > 11 || size <= hdr_len)
		return 0;
	const u32 comp_len
		= (u32)src[size - 8] | ((u32)src[size - 7] << 8) | ((u32)src[size - 6] << 16);
	if (comp_len < hdr_len || comp_len > size - hdr_len)
		return 0;
	return 1;
}

enumError DecodeLZOvl (u8 **dest, uint *dest_size, const u8 *src, uint src_size)
{
	if (!dest || !dest_size || !src || src_size < 8)
		return EINVAL;

	const u32 extra = (u32)src[src_size - 4] | ((u32)src[src_size - 3] << 8)
		| ((u32)src[src_size - 2] << 16) | ((u32)src[src_size - 1] << 24);
	if (extra == 0)
	{
		const uint out_len = src_size - 4;
		u8 *out = MALLOC (out_len);
		if (!out)
			return ERR_CANT_CREATE;
		memcpy (out, src, out_len);
		*dest = out;
		*dest_size = out_len;
		return ERR_OK;
	}

	const u8 hdr_len = src[src_size - 5];
	if (hdr_len < 8 || hdr_len > src_size)
		return EINVAL;

	const u32 comp_len
		= (u32)src[src_size - 8] | ((u32)src[src_size - 7] << 8) | ((u32)src[src_size - 6] << 16);
	const u32 uncomp_len = src_size - hdr_len - comp_len;
	const u32 total_out = src_size + extra;

	if (total_out > NFMT_MAX_OUTPUT || total_out < uncomp_len)
		return EINVAL;

	u8 *out = MALLOC (total_out);
	if (!out)
		return ERR_CANT_CREATE;

	if (uncomp_len)
		memcpy (out, src, uncomp_len);

	uint src_pos = src_size - hdr_len;
	uint out_pos = total_out;

	while (out_pos > uncomp_len && src_pos > uncomp_len)
	{
		const u8 flags = src[--src_pos];
		for (int b = 7; b >= 0 && out_pos > uncomp_len; b--)
		{
			if ((flags & (1 << b)) == 0)
			{
				if (src_pos <= uncomp_len)
					break;
				out[--out_pos] = src[--src_pos];
			}
			else
			{
				if (src_pos < uncomp_len + 2)
					break;
				const u8 b1 = src[--src_pos];
				const u8 b2 = src[--src_pos];
				const uint len = (b1 >> 4) + 3;
				const uint disp = (((b1 & 0xF) << 8) | b2) + 3;
				if (out_pos + disp > total_out || len > out_pos - uncomp_len)
					break;
				for (uint i = 0; i < len; i++)
				{
					out_pos--;
					out[out_pos] = out[out_pos + disp];
				}
			}
		}
	}

	*dest = out;
	*dest_size = total_out;
	return ERR_OK;
}

enumError EncodeLZOvl (u8 **dest, uint *dest_size, const u8 *src, uint src_size)
{
	if (!dest || !dest_size || !src || !src_size || src_size > NFMT_MAX_OUTPUT)
		return EINVAL;

	// Uncompressed overlay format: raw bytes followed by 4 zero bytes
	const uint total_sz = src_size + 4;
	u8 *out = MALLOC (total_sz);
	if (!out)
		return ERR_CANT_CREATE;

	memcpy (out, src, src_size);
	out[src_size + 0] = 0;
	out[src_size + 1] = 0;
	out[src_size + 2] = 0;
	out[src_size + 3] = 0;

	*dest = out;
	*dest_size = total_sz;
	return ERR_OK;
}
