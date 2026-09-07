#define _GNU_SOURCE 1

#include "lib-lz4.h"
#include "lib-szs.h"
#include "lz4.h"
#include "lz4hc.h"
#include "lz4frame.h"

int IsLZ4 (cvp data, uint size)
{
	const u8 *d = (const u8 *)data;
	if (!d || size < 4)
		return -1;

	const u32 magic = le32 (d);
	if (magic == LZ4_MAGIC_LE)
		return 1;

	// LZ4 skippable frames: 0x184D2A50 to 0x184D2A5F
	if ((magic & 0xFFFFFFF0u) == 0x184D2A50u && size >= 8)
		return 1;

	return -1;
}

int CalcCompressionLevelLZ4 (int compr_level)
{
	if (compr_level <= 0)
		return LZ4_DEFAULT_COMPR;
	if (compr_level > LZ4HC_CLEVEL_MAX)
		return LZ4HC_CLEVEL_MAX;
	return compr_level;
}

ccp GetMessageLZ4 (size_t code, ccp unknown_error)
{
	if (LZ4F_isError (code))
	{
		ccp name = LZ4F_getErrorName (code);
		return name ? name : unknown_error;
	}
	return "OK";
}

enumError EncodeLZ4buf (
	void *dest, uint dest_size, uint *dest_written, const void *src, uint src_size, int compr_level)
{
	DASSERT (dest);
	DASSERT (dest_written);

	*dest_written = 0;
	if (!src || !src_size)
		return ERR_OK;

	const int level = CalcCompressionLevelLZ4 (compr_level);

	LZ4F_preferences_t prefs;
	memset (&prefs, 0, sizeof (prefs));
	prefs.frameInfo.contentSize = src_size;
	prefs.compressionLevel = level;

	size_t written = LZ4F_compressFrame (dest, dest_size, src, src_size, &prefs);
	if (LZ4F_isError (written))
		return ERROR0 (ERR_LZ4, "LZ4 compression error: %s\n", LZ4F_getErrorName (written));

	*dest_written = (uint)written;
	return ERR_OK;
}

enumError EncodeLZ4 (
	u8 **dest_ptr, uint *dest_written, const void *src, uint src_size, int compr_level)
{
	DASSERT (dest_ptr);
	DASSERT (dest_written);

	*dest_ptr = 0;
	*dest_written = 0;

	if (!src || !src_size)
		return ERR_OK;

	const int level = CalcCompressionLevelLZ4 (compr_level);

	LZ4F_preferences_t prefs;
	memset (&prefs, 0, sizeof (prefs));
	prefs.frameInfo.contentSize = src_size;
	prefs.compressionLevel = level;

	const size_t bound = LZ4F_compressFrameBound (src_size, &prefs);
	u8 *dest = MALLOC (bound ? bound : 1);
	if (!dest)
		return ERR_OUT_OF_MEMORY;

	size_t written = LZ4F_compressFrame (dest, bound, src, src_size, &prefs);
	if (LZ4F_isError (written))
	{
		FREE (dest);
		return ERROR0 (ERR_LZ4, "LZ4 compression error: %s\n", LZ4F_getErrorName (written));
	}

	*dest_ptr = dest;
	*dest_written = (uint)written;
	return ERR_OK;
}

enumError DecodeLZ4 (u8 **dest_ptr, uint *dest_written, const void *src, uint src_size)
{
	DASSERT (dest_ptr);
	DASSERT (dest_written);

	*dest_ptr = 0;
	*dest_written = 0;

	if (!src || !src_size)
		return ERR_OK;

	LZ4F_dctx *dctx = 0;
	size_t cerr = LZ4F_createDecompressionContext (&dctx, LZ4F_VERSION);
	if (LZ4F_isError (cerr))
		return ERROR0 (ERR_INVALID_DATA, "LZ4 decompression error: %s\n", LZ4F_getErrorName (cerr));

	LZ4F_frameInfo_t info;
	memset (&info, 0, sizeof (info));
	size_t src_consumed = src_size;
	size_t hint = LZ4F_getFrameInfo (dctx, &info, src, &src_consumed);
	if (LZ4F_isError (hint))
	{
		LZ4F_freeDecompressionContext (dctx);
		return ERROR0 (
			ERR_INVALID_DATA, "LZ4 decompression error: %s\n", LZ4F_getErrorName (hint));
	}

	const u8 *src_ptr = (const u8 *)src + src_consumed;
	size_t src_remain = src_size - src_consumed;

	size_t dst_cap = info.contentSize ? (size_t)info.contentSize
		: (src_size < 16384 ? 65536 : (size_t)src_size * 4);
	u8 *dest = MALLOC (dst_cap ? dst_cap : 1);
	if (!dest)
	{
		LZ4F_freeDecompressionContext (dctx);
		return ERR_OUT_OF_MEMORY;
	}

	size_t dst_pos = 0;
	size_t ret = 1;
	while (ret > 0 && src_remain > 0)
	{
		if (dst_pos == dst_cap)
		{
			dst_cap = dst_cap ? dst_cap * 2 : 65536;
			u8 *new_dest = REALLOC (dest, dst_cap);
			if (!new_dest)
			{
				LZ4F_freeDecompressionContext (dctx);
				FREE (dest);
				return ERR_OUT_OF_MEMORY;
			}
			dest = new_dest;
		}

		size_t dst_avail = dst_cap - dst_pos;
		size_t src_avail = src_remain;
		ret = LZ4F_decompress (dctx, dest + dst_pos, &dst_avail, src_ptr, &src_avail, 0);
		if (LZ4F_isError (ret))
		{
			LZ4F_freeDecompressionContext (dctx);
			FREE (dest);
			return ERROR0 (
				ERR_INVALID_DATA, "LZ4 decompression error: %s\n", LZ4F_getErrorName (ret));
		}

		dst_pos += dst_avail;
		src_ptr += src_avail;
		src_remain -= src_avail;

		if (!ret)
			break;
		if (!dst_avail && !src_avail)
			break; // no progress; avoid infinite loop
	}

	LZ4F_freeDecompressionContext (dctx);
	*dest_ptr = dest;
	*dest_written = (uint)dst_pos;
	return ERR_OK;
}

enumError DecodeLZ4part (
	void *dest_buf, uint dest_size, uint *dest_written, const void *src, uint src_size)
{
	DASSERT (dest_buf);
	DASSERT (dest_written);

	*dest_written = 0;
	if (!src || !src_size || !dest_size)
		return ERR_OK;

	LZ4F_dctx *dctx = 0;
	size_t cerr = LZ4F_createDecompressionContext (&dctx, LZ4F_VERSION);
	if (LZ4F_isError (cerr))
		return ERROR0 (
			ERR_INVALID_DATA, "LZ4 partial decompression error: %s\n", LZ4F_getErrorName (cerr));

	size_t dst_avail = dest_size;
	size_t src_avail = src_size;
	size_t ret = LZ4F_decompress (dctx, dest_buf, &dst_avail, src, &src_avail, 0);
	LZ4F_freeDecompressionContext (dctx);

	if (LZ4F_isError (ret))
		return ERROR0 (
			ERR_INVALID_DATA, "LZ4 partial decompression error: %s\n", LZ4F_getErrorName (ret));

	*dest_written = (uint)dst_avail;
	return ret == 0 ? ERR_OK : ERR_WARNING;
}
