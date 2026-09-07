// Wii banner family: IMET / IMD5 / "LZ77" wrappers -- see lib-wii-banner.h.
//
// Offsets verified against six real retail /opening.bnr files pulled from
// WBFS images with the wit pass-through (Excite Truck, Donkey Kong: Barrel
// Blast, Mario Party 8, Wii Sports Resort, Newer Super Mario Bros. Wii and a
// Brawl mod), cross-read with WiiBrew's "Opening.bnr" and "IMD5" pages.
//
// One correction to the commonly cited layout came out of those samples: the
// IMET MD5 covers file bytes 0x000..0x600 -- the 0x40 bytes of leading zero
// padding included -- with the 16 MD5 bytes at 0x5f0 zeroed, not the
// 0x40..0x640 range usually documented. All six agree on this; none matches
// the documented range.

#include "lib-std.h"
#include "lib-wii-banner.h"
#include "lib-lz10.h"

//-----------------------------------------------------------------------------
///////////////			MD5					///////////////
//-----------------------------------------------------------------------------

// Compact RFC 1321 MD5. This codebase links OpenSSL's SHA1 only, and MD5 is
// needed here purely to *verify* Nintendo's own integrity fields, never to
// produce a security guarantee -- if these two uses ever grow a third,
// promote it to its own lib-md5.c.

typedef struct md5_ctx_t
{
	u32 state[4];
	u64 count; // message length in bytes
	u8 buf[64];
} md5_ctx_t;

static const u8 md5_shift[64] = { 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 5, 9,
	14, 20, 5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
	4, 11, 16, 23, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21 };

static const u32 md5_sine[64] = { 0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf,
	0x4787c62a, 0xa8304613, 0xfd469501, 0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be, 0x6b901122,
	0xfd987193, 0xa679438e, 0x49b40821, 0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa, 0xd62f105d,
	0x02441453, 0xd8a1e681, 0xe7d3fbc8, 0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905,
	0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a, 0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c, 0xa4beea44,
	0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70, 0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05, 0xd9d4d039,
	0xe6db99e5, 0x1fa27cf8, 0xc4ac5665, 0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3,
	0x8f0ccc92, 0xffeff47d, 0x85845dd1, 0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1, 0xf7537e82,
	0xbd3af235, 0x2ad7d2bb, 0xeb86d391 };

static inline u32 md5_rotl (u32 v, uint n)
{
	return v << n | v >> (32 - n);
}

static void md5_block (u32 state[4], const u8 *p)
{
	u32 m[16];
	for (uint i = 0; i < 16; i++)
		m[i] = (u32)p[4 * i] | (u32)p[4 * i + 1] << 8 | (u32)p[4 * i + 2] << 16
			| (u32)p[4 * i + 3] << 24;

	u32 a = state[0], b = state[1], c = state[2], d = state[3];
	for (uint i = 0; i < 64; i++)
	{
		u32 f;
		uint g;
		if (i < 16)
		{
			f = (b & c) | (~b & d);
			g = i;
		}
		else if (i < 32)
		{
			f = (d & b) | (~d & c);
			g = (5 * i + 1) & 15;
		}
		else if (i < 48)
		{
			f = b ^ c ^ d;
			g = (3 * i + 5) & 15;
		}
		else
		{
			f = c ^ (b | ~d);
			g = (7 * i) & 15;
		}
		const u32 tmp = d;
		d = c;
		c = b;
		b = b + md5_rotl (a + f + md5_sine[i] + m[g], md5_shift[i]);
		a = tmp;
	}
	state[0] += a;
	state[1] += b;
	state[2] += c;
	state[3] += d;
}

static void md5_calc (u8 digest[16], const u8 *data, uint size)
{
	md5_ctx_t ctx;
	ctx.state[0] = 0x67452301;
	ctx.state[1] = 0xefcdab89;
	ctx.state[2] = 0x98badcfe;
	ctx.state[3] = 0x10325476;
	ctx.count = size;

	uint pos = 0;
	while (size - pos >= 64)
	{
		md5_block (ctx.state, data + pos);
		pos += 64;
	}

	memset (ctx.buf, 0, sizeof (ctx.buf));
	const uint rest = size - pos;
	memcpy (ctx.buf, data + pos, rest);
	ctx.buf[rest] = 0x80;
	if (rest >= 56)
	{
		md5_block (ctx.state, ctx.buf);
		memset (ctx.buf, 0, sizeof (ctx.buf));
	}
	const u64 bits = ctx.count * 8;
	for (uint i = 0; i < 8; i++)
		ctx.buf[56 + i] = (u8)(bits >> (8 * i));
	md5_block (ctx.state, ctx.buf);

	for (uint i = 0; i < 4; i++)
	{
		digest[4 * i] = (u8)ctx.state[i];
		digest[4 * i + 1] = (u8)(ctx.state[i] >> 8);
		digest[4 * i + 2] = (u8)(ctx.state[i] >> 16);
		digest[4 * i + 3] = (u8)(ctx.state[i] >> 24);
	}
}

//-----------------------------------------------------------------------------
///////////////			helpers					///////////////
//-----------------------------------------------------------------------------

static inline u32 wb_rd32 (const u8 *p)
{
	return (u32)p[0] << 24 | (u32)p[1] << 16 | (u32)p[2] << 8 | (u32)p[3];
}

// NUL-terminated (or field-width-truncated) UTF-16BE -> malloc'd UTF-8.
static char *wb_utf16be_to_utf8 (const u8 *p, uint n_u16)
{
	FastBuf_t fb;
	InitializeFastBufAlloc (&fb, 2 * n_u16 + 8);
	const u8 *end = p + 2 * n_u16;
	while (p + 2 <= end)
	{
		u32 u = (u32)p[0] << 8 | p[1];
		p += 2;
		if (!u)
			break;
		if (u >= 0xd800 && u < 0xdc00 && p + 2 <= end)
		{
			const u32 lo = (u32)p[0] << 8 | p[1];
			if (lo >= 0xdc00 && lo < 0xe000)
			{
				u = 0x10000 + ((u - 0xd800) << 10) + (lo - 0xdc00);
				p += 2;
			}
			else
				u = 0xfffd;
		}
		else if (u >= 0xd800 && u < 0xe000)
			u = 0xfffd;
		AppendUTF8CharFastBuf (&fb, u);
	}
	char *out = STRDUP (GetFastBufString (&fb));
	ResetFastBuf (&fb);
	return out;
}

//-----------------------------------------------------------------------------
///////////////			IMET					///////////////
//-----------------------------------------------------------------------------

// An IMET header normally sits behind 0x40 zero bytes; a few tools emit it
// without that padding, so accept both and remember which.
static uint imet_header_offset (const u8 *data, uint size)
{
	if (size >= IMET_MAGIC_OFFSET + 4 && !memcmp (data + IMET_MAGIC_OFFSET, "IMET", 4))
		return IMET_MAGIC_OFFSET;
	if (size >= 4 && !memcmp (data, "IMET", 4))
		return 0;
	return ~(uint)0;
}

bool IsIMET (const u8 *data, uint size)
{
	if (!data)
		return false;
	const uint hoff = imet_header_offset (data, size);
	if (hoff == ~(uint)0)
		return false;

	// The header's own size field says where the U8 archive begins; require
	// that the archive is actually there, so a stray "IMET" in the middle of
	// some other file cannot claim the format.
	const uint hsize = wb_rd32 (data + hoff + 4);
	if (hsize < 0x40 || hsize > 0x10000)
		return false;
	const uint u8_off = hoff ? hsize : hsize - IMET_MAGIC_OFFSET;
	return (size_t)u8_off + 4 <= size && wb_rd32 (data + u8_off) == 0x55aa382d;
}

enumError ScanIMET (imet_t *imet, const u8 *data, uint size)
{
	DASSERT (imet);
	memset (imet, 0, sizeof (*imet));
	if (!IsIMET (data, size))
		return ERR_INVALID_DATA;

	const uint hoff = imet_header_offset (data, size);
	const u8 *h = data + hoff;
	imet->header_offset = hoff;
	imet->header_size = wb_rd32 (h + 4);
	imet->n_files = wb_rd32 (h + 8);
	imet->icon_size = wb_rd32 (h + 0x0c);
	imet->banner_size = wb_rd32 (h + 0x10);
	imet->sound_size = wb_rd32 (h + 0x14);
	imet->u8_offset = hoff ? imet->header_size : imet->header_size - IMET_MAGIC_OFFSET;

	for (uint i = 0; i < IMET_N_TITLES; i++)
	{
		const uint off = hoff + 0x1c + i * IMET_TITLE_SIZE;
		imet->title[i] = (size_t)off + IMET_TITLE_SIZE <= size
			? wb_utf16be_to_utf8 (data + off, IMET_TITLE_SIZE / 2)
			: STRDUP ("");
	}

	// MD5 over the whole header including the leading padding, with the MD5
	// field itself zeroed (see the file comment: this differs from the range
	// WiiBrew documents, and matches every real sample).
	const uint md5_pos = hoff ? IMET_MD5_OFFSET : IMET_MD5_OFFSET - IMET_MAGIC_OFFSET;
	const uint hashed = hoff ? IMET_SIZE : IMET_SIZE - IMET_MAGIC_OFFSET;
	if ((size_t)md5_pos + 16 <= size && hashed <= size)
	{
		memcpy (imet->md5, data + md5_pos, 16);
		u8 *tmp = MEMDUP (data, hashed);
		memset (tmp + md5_pos, 0, 16);
		u8 calc[16];
		md5_calc (calc, tmp, hashed);
		FREE (tmp);
		imet->md5_ok = !memcmp (calc, imet->md5, 16);
	}

	return ERR_OK;
}

void ResetIMET (imet_t *imet)
{
	if (!imet)
		return;
	for (uint i = 0; i < IMET_N_TITLES; i++)
		FREE ((void *)imet->title[i]);
	memset (imet, 0, sizeof (*imet));
}

static const char *const imet_lang_name[IMET_LANG__N] = {
	"Japanese",
	"English",
	"German",
	"French",
	"Spanish",
	"Italian",
	"Dutch",
	"Chinese (simplified)",
	"Chinese (traditional)",
	"Korean",
};

char *TextIMET (const imet_t *imet)
{
	if (!imet)
		return 0;

	FastBuf_t fb;
	InitializeFastBufAlloc (&fb, 0x800);
	char line[0x200];

	int n = snprintf (line, sizeof (line),
		"# Wii channel banner (IMET)\n"
		"header_offset = 0x%x\n"
		"header_size   = 0x%x\n"
		"icon_size     = 0x%x  # uncompressed icon.bin\n"
		"banner_size   = 0x%x  # uncompressed banner.bin\n"
		"sound_size    = 0x%x  # uncompressed sound.bin\n"
		"md5           = %s\n",
		imet->header_offset, imet->header_size, imet->icon_size, imet->banner_size,
		imet->sound_size, imet->md5_ok ? "ok" : "MISMATCH");
	AppendFastBuf (&fb, line, n);

	for (uint i = 0; i < IMET_N_TITLES; i++)
	{
		ccp t = imet->title[i];
		if (!t || !*t)
			continue;
		n = snprintf (line, sizeof (line), "\n[%s]\n  %s\n", imet_lang_name[i], t);
		AppendFastBuf (&fb, line, n);
	}

	char *out = STRDUP (GetFastBufString (&fb));
	ResetFastBuf (&fb);
	return out;
}

//-----------------------------------------------------------------------------
///////////////			IMD5 + LZ77 wrapper			///////////////
//-----------------------------------------------------------------------------

bool IsIMD5 (const u8 *data, uint size)
{
	if (!data || size < IMD5_SIZE + 1 || memcmp (data, "IMD5", 4))
		return false;
	const u32 payload = wb_rd32 (data + 4);
	// The stored size is the payload only; allow a shorter buffer for a
	// truncated file to still be recognized, but not a wildly wrong one.
	return payload && (u64)payload <= 0x10000000;
}

enumError ScanIMD5 (imd5_t *imd5, const u8 *data, uint size)
{
	DASSERT (imd5);
	memset (imd5, 0, sizeof (*imd5));
	if (!IsIMD5 (data, size))
		return ERR_INVALID_DATA;

	imd5->payload_offset = IMD5_SIZE;
	imd5->payload_size = wb_rd32 (data + 4);
	memcpy (imd5->md5, data + 0x10, 16);
	if ((u64)IMD5_SIZE + imd5->payload_size <= size)
	{
		u8 calc[16];
		md5_calc (calc, data + IMD5_SIZE, imd5->payload_size);
		imd5->md5_ok = !memcmp (calc, imd5->md5, 16);
	}
	return ERR_OK;
}

enumError UnwrapWiiBannerFile (
	u8 **dest, uint *dest_size, bool *was_compressed, const u8 *data, uint size)
{
	if (!dest || !dest_size || !data)
		return EINVAL;
	if (was_compressed)
		*was_compressed = false;

	const u8 *body = data;
	uint body_size = size;

	if (IsIMD5 (data, size))
	{
		imd5_t imd5;
		if (ScanIMD5 (&imd5, data, size))
			return ERR_NOTHING_TO_DO;
		body = data + imd5.payload_offset;
		// Trust the file's real length over the stored size when the file has
		// been truncated; a short read is better than reading past the end.
		body_size = size - imd5.payload_offset;
		if (imd5.payload_size < body_size)
			body_size = imd5.payload_size;
	}

	// "LZ77" + the standard LZ10/LZ11 header word (type byte + 24-bit
	// uncompressed size), i.e. exactly what DecodeLZ10LZ11 already expects
	// once the 4 magic bytes are gone.
	if (body_size > 8 && !memcmp (body, "LZ77", 4))
	{
		u8 *out = 0;
		uint out_size = 0;
		if (DecodeLZ10LZ11 (&out, &out_size, body + 4, body_size - 4))
			return ERR_INVALID_DATA;
		if (was_compressed)
			*was_compressed = true;
		*dest = out;
		*dest_size = out_size;
		return ERR_OK;
	}

	if (body == data)
		return ERR_NOTHING_TO_DO; // neither wrapper present

	*dest = MEMDUP (body, body_size);
	*dest_size = body_size;
	return ERR_OK;
}
