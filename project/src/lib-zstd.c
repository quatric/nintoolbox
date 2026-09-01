#define _GNU_SOURCE 1

#include "lib-zstd.h"
#include "lib-szs.h"
#include "zstd.h"
#include "zstd_errors.h"

int IsZSTD (cvp data, uint size)
{
	const u8 *d = (const u8 *)data;
	if (!d || size < 4)
		return -1;

	const u32 magic = le32 (d);
	if (magic == ZSTD_MAGIC_LE)
		return 1;

	// Check for Zstandard skippable frames: 0x184D2A50 to 0x184D2A5F
	if ((magic & 0xFFFFFFF0u) == 0x184D2A50u && size >= 8)
		return 1;

	return -1;
}

int CalcCompressionLevelZSTD (int compr_level)
{
	if (compr_level <= 0)
		return ZSTD_DEFAULT_COMPR;
	if (compr_level > 22)
		return 22;
	return compr_level;
}

ccp GetMessageZSTD (size_t code, ccp unknown_error)
{
	if (ZSTD_isError (code))
	{
		ccp name = ZSTD_getErrorName (code);
		return name ? name : unknown_error;
	}
	return "OK";
}

enumError EncodeZSTDbuf (void *dest, uint dest_size, uint *dest_written,
	const void *src, uint src_size, int compr_level)
{
	DASSERT (dest);
	DASSERT (dest_written);

	*dest_written = 0;
	if (!src || !src_size)
		return ERR_OK;

	const int level = CalcCompressionLevelZSTD (compr_level);
	size_t written = ZSTD_compress (dest, dest_size, src, src_size, level);
	if (ZSTD_isError (written))
		return ERROR0 (ERR_ZSTD, "ZSTD compression error: %s\n", ZSTD_getErrorName (written));

	*dest_written = (uint)written;
	return ERR_OK;
}

enumError EncodeZSTD (u8 **dest_ptr, uint *dest_written,
	const void *src, uint src_size, int compr_level)
{
	DASSERT (dest_ptr);
	DASSERT (dest_written);

	*dest_ptr = 0;
	*dest_written = 0;

	if (!src || !src_size)
		return ERR_OK;

	const size_t bound = ZSTD_compressBound (src_size);
	u8 *dest = MALLOC (bound ? bound : 1);
	if (!dest)
		return ERR_OUT_OF_MEMORY;

	const int level = CalcCompressionLevelZSTD (compr_level);
	size_t written = ZSTD_compress (dest, bound, src, src_size, level);
	if (ZSTD_isError (written))
	{
		FREE (dest);
		return ERROR0 (ERR_ZSTD, "ZSTD compression error: %s\n", ZSTD_getErrorName (written));
	}

	*dest_ptr = dest;
	*dest_written = (uint)written;
	return ERR_OK;
}

enumError DecodeZSTD (u8 **dest_ptr, uint *dest_written, const void *src, uint src_size)
{
	DASSERT (dest_ptr);
	DASSERT (dest_written);

	*dest_ptr = 0;
	*dest_written = 0;

	if (!src || !src_size)
		return ERR_OK;

	unsigned long long content_size = ZSTD_getFrameContentSize (src, src_size);
	if (content_size != ZSTD_CONTENTSIZE_UNKNOWN && content_size != ZSTD_CONTENTSIZE_ERROR
		&& content_size <= UINT_MAX)
	{
		size_t dst_cap = (size_t)content_size;
		u8 *dest = MALLOC (dst_cap ? dst_cap : 1);
		if (!dest)
			return ERR_OUT_OF_MEMORY;

		size_t ret = ZSTD_decompress (dest, dst_cap, src, src_size);
		if (ZSTD_isError (ret))
		{
			FREE (dest);
			return ERROR0 (ERR_INVALID_DATA, "ZSTD decompression error: %s\n", ZSTD_getErrorName (ret));
		}

		*dest_ptr = dest;
		*dest_written = (uint)ret;
		return ERR_OK;
	}

	// Stream / dynamic decompression for unknown uncompressed size
	size_t dst_cap = src_size < 16384 ? 65536 : src_size * 4;
	u8 *dest = MALLOC (dst_cap);
	if (!dest)
		return ERR_OUT_OF_MEMORY;

	ZSTD_DCtx *dctx = ZSTD_createDCtx ();
	if (!dctx)
	{
		FREE (dest);
		return ERR_OUT_OF_MEMORY;
	}

	ZSTD_inBuffer in = { src, src_size, 0 };
	ZSTD_outBuffer out = { dest, dst_cap, 0 };

	size_t ret = 1;
	while (ret > 0)
	{
		ret = ZSTD_decompressStream (dctx, &out, &in);
		if (ZSTD_isError (ret))
		{
			ZSTD_freeDCtx (dctx);
			FREE (dest);
			return ERROR0 (ERR_INVALID_DATA, "ZSTD streaming decompression error: %s\n", ZSTD_getErrorName (ret));
		}

		if (out.pos == out.size && ret > 0)
		{
			dst_cap *= 2;
			u8 *new_dest = REALLOC (dest, dst_cap);
			if (!new_dest)
			{
				ZSTD_freeDCtx (dctx);
				FREE (dest);
				return ERR_OUT_OF_MEMORY;
			}
			dest = new_dest;
			out.dst = dest;
			out.size = dst_cap;
		}
	}

	ZSTD_freeDCtx (dctx);
	*dest_ptr = dest;
	*dest_written = (uint)out.pos;
	return ERR_OK;
}

enumError DecodeZSTDpart (void *dest_buf, uint dest_size, uint *dest_written,
	const void *src, uint src_size)
{
	DASSERT (dest_buf);
	DASSERT (dest_written);

	*dest_written = 0;
	if (!src || !src_size || !dest_size)
		return ERR_OK;

	ZSTD_DCtx *dctx = ZSTD_createDCtx ();
	if (!dctx)
		return ERR_OUT_OF_MEMORY;

	ZSTD_inBuffer in = { src, src_size, 0 };
	ZSTD_outBuffer out = { dest_buf, dest_size, 0 };

	size_t ret = ZSTD_decompressStream (dctx, &out, &in);
	ZSTD_freeDCtx (dctx);

	if (ZSTD_isError (ret))
		return ERROR0 (ERR_INVALID_DATA, "ZSTD partial decompression error: %s\n", ZSTD_getErrorName (ret));

	*dest_written = (uint)out.pos;
	return ret == 0 ? ERR_OK : ERR_WARNING;
}
