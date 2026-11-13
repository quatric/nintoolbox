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


//////////////////////////////////////////////////////////////////////////////
// Dictionary registry.  Zelda-style romfs trees (Tears of the Kingdom and its
// Switch 2 successors) compress most *.zs files with one of a handful of
// trained dictionaries stored in Pack/ZsDic.pack.zs (a SARC of *.zsdic
// files).  A frame names its dictionary by ID, so decoding only needs the
// registry to know every dictionary it might meet.

typedef struct zs_dict_t
{
	u32 id;
	u8 *data;
	size_t size;
	ZSTD_DDict *ddict;
} zs_dict_t;

static zs_dict_t *zs_dicts = 0;
static uint zs_dict_n = 0, zs_dict_cap = 0;

#define ZS_DICT_MAGIC 0xEC30A437u

static zs_dict_t *FindDictZSTD (u32 id)
{
	for (uint i = 0; i < zs_dict_n; i++)
		if (zs_dicts[i].id == id)
			return zs_dicts + i;
	return 0;
}

bool RegisterDictZSTD (const void *data, uint size)
{
	if (!data || size < 8 || le32 ((const u8 *)data) != ZS_DICT_MAGIC)
		return false;

	const u32 id = le32 ((const u8 *)data + 4);
	if (!id || FindDictZSTD (id))
		return false;

	if (zs_dict_n == zs_dict_cap)
	{
		const uint ncap = zs_dict_cap ? zs_dict_cap * 2 : 8;
		zs_dict_t *nd = REALLOC (zs_dicts, ncap * sizeof (*nd));
		if (!nd)
			return false;
		zs_dicts = nd;
		zs_dict_cap = ncap;
	}

	u8 *copy = MALLOC (size);
	if (!copy)
		return false;
	memcpy (copy, data, size);

	ZSTD_DDict *dd = ZSTD_createDDict (copy, size);
	if (!dd)
	{
		FREE (copy);
		return false;
	}

	zs_dict_t *d = zs_dicts + zs_dict_n++;
	d->id = id;
	d->data = copy;
	d->size = size;
	d->ddict = dd;
	return true;
}

// Registers every dictionary found in a SARC (the decoded ZsDic.pack).
static uint RegisterDictSarcZSTD (const u8 *d, uint size)
{
	if (size < 0x20 || memcmp (d, "SARC", 4) || memcmp (d + 0x14, "SFAT", 4))
		return 0;

	const bool be = be16 (d + 6) == 0xFEFF;
	#define R16(p) (be ? be16 (p) : le16 (p))
	#define R32(p) (be ? be32 (p) : le32 (p))
	const u32 data_off = R32 (d + 0x0C);
	const uint count = R16 (d + 0x1A);
	if (0x20 + (u64)count * 16 > size)
		return 0;

	uint n = 0;
	for (uint i = 0; i < count; i++)
	{
		const u8 *e = d + 0x20 + i * 16;
		const u32 beg = R32 (e + 8), end = R32 (e + 12);
		if (beg > end || (u64)data_off + end > size)
			continue;
		if (RegisterDictZSTD (d + data_off + beg, end - beg))
			n++;
	}
	#undef R16
	#undef R32
	return n;
}

static bool ReadWholeFileZSTD (ccp path, u8 **out, uint *out_size)
{
	FILE *f = fopen (path, "rb");
	if (!f)
		return false;
	fseek (f, 0, SEEK_END);
	const long sz = ftell (f);
	fseek (f, 0, SEEK_SET);
	if (sz <= 0 || sz > 64 * 1024 * 1024)
	{
		fclose (f);
		return false;
	}
	u8 *buf = MALLOC ((size_t)sz);
	if (!buf || fread (buf, 1, (size_t)sz, f) != (size_t)sz)
	{
		FREE (buf);
		fclose (f);
		return false;
	}
	fclose (f);
	*out = buf;
	*out_size = (uint)sz;
	return true;
}

// Loads dictionaries from a ZsDic.pack(.zs), a bare *.zsdic or a raw SARC.
uint LoadDictFileZSTD (ccp path)
{
	u8 *buf = 0;
	uint size = 0;
	if (!path || !ReadWholeFileZSTD (path, &buf, &size))
		return 0;

	uint n = 0;
	if (IsZSTD (buf, size) > 0)
	{
		// The pack itself is compressed without a dictionary.
		u8 *dec = 0;
		uint dec_n = 0;
		if (!DecodeZSTD (&dec, &dec_n, buf, size) && dec)
			n = RegisterDictSarcZSTD (dec, dec_n);
		FREE (dec);
	}
	else if (!memcmp (buf, "SARC", 4))
		n = RegisterDictSarcZSTD (buf, size);
	else
		n = RegisterDictZSTD (buf, size) ? 1 : 0;

	FREE (buf);
	return n;
}

static bool zs_env_done = false;

static void LoadDictEnvZSTD (void)
{
	if (zs_env_done)
		return;
	zs_env_done = true;
	ccp env = getenv ("ZSDIC");
	if (env && *env)
		LoadDictFileZSTD (env);
}

// Looks for Pack/ZsDic.pack.zs in the directories above 'path' (the file
// about to be processed), so that a romfs tree on disk just works.
void HintSourcePathZSTD (ccp path)
{
	LoadDictEnvZSTD ();
	if (!path || !*path)
		return;

	static char last_dir[PATH_MAX];
	char dir[PATH_MAX];
	if (!realpath (path, dir))
		snprintf (dir, sizeof (dir), "%s", path);

	// Visit each directory above the file; remember the nearest dir we
	// already searched so batches over one tree stay cheap.
	char *slash = strrchr (dir, '/');
	if (!slash)
		return;
	*slash = 0;
	if (!strcmp (dir, last_dir))
		return;
	snprintf (last_dir, sizeof (last_dir), "%s", dir);

	for (int depth = 0; depth < 24 && *dir; depth++)
	{
		static const ccp tail[] = { "/Pack/ZsDic.pack.zs", "/romfs/Pack/ZsDic.pack.zs", 0 };
		for (const ccp *t = tail; *t; t++)
		{
			char cand[PATH_MAX + 32];
			snprintf (cand, sizeof (cand), "%s%s", dir, *t);
			struct stat st;
			if (!stat (cand, &st) && S_ISREG (st.st_mode))
			{
				static char loaded[8][PATH_MAX + 32];
				static uint nloaded = 0;
				bool seen = false;
				for (uint i = 0; i < nloaded; i++)
					if (!strcmp (loaded[i], cand))
						seen = true;
				if (!seen)
				{
					if (nloaded < 8)
						snprintf (loaded[nloaded++], sizeof (loaded[0]), "%s", cand);
					LoadDictFileZSTD (cand);
				}
				return;
			}
		}
		slash = strrchr (dir, '/');
		if (!slash)
			break;
		*slash = 0;
	}
}

static const ZSTD_DDict *DDictForFrameZSTD (const void *src, uint src_size, u32 *missing_id)
{
	const u32 id = ZSTD_getDictID_fromFrame (src, src_size);
	if (missing_id)
		*missing_id = 0;
	if (!id)
		return 0;
	const zs_dict_t *d = FindDictZSTD (id);
	if (d)
		return d->ddict;
	if (missing_id)
		*missing_id = id;
	return 0;
}

static ccp MissingDictMsgZSTD (u32 id, char *buf, uint size)
{
	snprintf (buf, size,
		"frame needs zstd dictionary #%u (ZsDic.pack.zs not found above the input; "
		"set ZSDIC=<path to ZsDic.pack.zs>)",
		id);
	return buf;
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

enumError EncodeZSTDbuf (
	void *dest, uint dest_size, uint *dest_written, const void *src, uint src_size, int compr_level)
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

enumError EncodeZSTD (
	u8 **dest_ptr, uint *dest_written, const void *src, uint src_size, int compr_level)
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

		u32 missing = 0;
		const ZSTD_DDict *dd = DDictForFrameZSTD (src, src_size, &missing);
		size_t ret;
		if (dd)
		{
			ZSTD_DCtx *dc = ZSTD_createDCtx ();
			ret = dc ? ZSTD_decompress_usingDDict (dc, dest, dst_cap, src, src_size, dd)
				 : ZSTD_error_memory_allocation;
			if (dc)
				ZSTD_freeDCtx (dc);
		}
		else
			ret = ZSTD_decompress (dest, dst_cap, src, src_size);
		if (ZSTD_isError (ret))
		{
			FREE (dest);
			char mb[256];
			if (missing)
				return ERROR0 (ERR_INVALID_DATA, "ZSTD decompression error: %s\n",
					MissingDictMsgZSTD (missing, mb, sizeof (mb)));
			return ERROR0 (
				ERR_INVALID_DATA, "ZSTD decompression error: %s\n", ZSTD_getErrorName (ret));
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

	{
		const ZSTD_DDict *dd = DDictForFrameZSTD (src, src_size, 0);
		if (dd)
			ZSTD_DCtx_refDDict (dctx, dd);
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
			return ERROR0 (ERR_INVALID_DATA, "ZSTD streaming decompression error: %s\n",
				ZSTD_getErrorName (ret));
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

enumError DecodeZSTDpart (
	void *dest_buf, uint dest_size, uint *dest_written, const void *src, uint src_size)
{
	DASSERT (dest_buf);
	DASSERT (dest_written);

	*dest_written = 0;
	if (!src || !src_size || !dest_size)
		return ERR_OK;

	ZSTD_DCtx *dctx = ZSTD_createDCtx ();
	if (!dctx)
		return ERR_OUT_OF_MEMORY;

	{
		const ZSTD_DDict *dd = DDictForFrameZSTD (src, src_size, 0);
		if (dd)
			ZSTD_DCtx_refDDict (dctx, dd);
	}

	ZSTD_inBuffer in = { src, src_size, 0 };
	ZSTD_outBuffer out = { dest_buf, dest_size, 0 };

	size_t ret = ZSTD_decompressStream (dctx, &out, &in);
	ZSTD_freeDCtx (dctx);

	if (ZSTD_isError (ret))
		return ERROR0 (
			ERR_INVALID_DATA, "ZSTD partial decompression error: %s\n", ZSTD_getErrorName (ret));

	*dest_written = (uint)out.pos;
	return ret == 0 ? ERR_OK : ERR_WARNING;
}
