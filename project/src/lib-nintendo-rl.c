#include "lib-std.h"
#include "lib-nintendo-rl.h"
#include <string.h>
#include <errno.h>
#include <limits.h>

enumError DecodeNintendoRL (u8 **dest, uint *dest_size, const u8 *src, uint src_size)
{
	if (!src || src_size < 4 || src[0] != 0x30)
		return EINVAL;
	const uint out_size = (uint)src[1] | (uint)src[2] << 8 | (uint)src[3] << 16;
	enumError err = AllocOutput (dest, dest_size, out_size);
	if (err)
		return err;
	uint sp = 4, dp = 0;
	while (dp < out_size)
	{
		if (sp >= src_size)
			goto invalid_rl;
		const u8 control = src[sp++];
		const uint len = (control & 0x7f) + (control >> 7 ? 3 : 1);
		if (len > out_size - dp || sp + (control >> 7 ? 1 : len) > src_size)
			goto invalid_rl;
		if (control >> 7)
			memset (*dest + dp, src[sp++], len);
		else
		{
			memcpy (*dest + dp, src + sp, len);
			sp += len;
		}
		dp += len;
	}
	return ERR_OK;
invalid_rl:
	FREE (*dest);
	*dest = 0;
	*dest_size = 0;
	return EINVAL;
}

enumError EncodeNintendoRL (u8 **dest, uint *dest_size, const u8 *src, uint src_size)
{
	if (!src || !src_size || src_size > 0xffffff || !dest || !dest_size)
		return EINVAL;
	const uint cap = src_size + (src_size + 127) / 128 + 4;
	u8 *out = MALLOC (cap);
	if (!out)
		return ERR_CANT_CREATE;
	out[0] = 0x30;
	out[1] = src_size;
	out[2] = src_size >> 8;
	out[3] = src_size >> 16;
	uint sp = 0, dp = 4;
	while (sp < src_size)
	{
		uint run = 1;
		while (run < 130 && sp + run < src_size && src[sp + run] == src[sp])
			run++;
		if (run >= 3)
		{
			out[dp++] = 0x80 | (run - 3);
			out[dp++] = src[sp];
			sp += run;
			continue;
		}
		const uint start = sp++;
		while (sp - start < 128 && sp < src_size)
		{
			run = 1;
			while (run < 3 && sp + run < src_size && src[sp + run] == src[sp])
				run++;
			if (run >= 3)
				break;
			sp++;
		}
		const uint len = sp - start;
		out[dp++] = len - 1;
		memcpy (out + dp, src + start, len);
		dp += len;
	}
	*dest = out;
	*dest_size = dp;
	return ERR_OK;
}

// BLZ ("backward LZSS"): used to compress a DS ROM's ARM9/ARM7 executable
// and overlay files, ported from CUE's reference blz.c
// (github.com/PeterLemon/Nintendo_DS_Compressors). Unlike this file's other
// LZ variants it has no magic/header at the *start* -- everything needed to
// decode is an 8-11 byte footer at the *end*, and the compressed span is
// itself byte-reversed (encode walks the source backward, building matches
// against what -- once reversed back -- reads as ordinary forward LZSS with
// a min match length of 3 stored as len-3 and a 12-bit back-reference).
// This means BLZ can't be identified by a header-magic table lookup the way
// the rest of DetectNintendoFormat() works: any file could coincidentally
// have a plausible-looking footer, so this decoder is only invoked where
// the caller already has other context that a file might be BLZ (an
