// Nintendo DS ROM banner (icon + per-language titles) -- see lib-nds-banner.h.

#include "lib-std.h"
#include "lib-nds-banner.h"

//-----------------------------------------------------------------------------
///////////////			CRC16 (CRC-16/MODBUS)			///////////////
//-----------------------------------------------------------------------------

// Nintendo's DS header/banner checksum: reflected, poly 0xa001, init 0xffff.
// Table-free -- the banner regions checked here are at most 0x1180 bytes, so
// the bitwise loop costs nothing worth a static table for.
static u16 nds_crc16 (const u8 *p, uint size)
{
	u16 crc = 0xffff;
	for (uint i = 0; i < size; i++)
	{
		crc ^= p[i];
		for (uint b = 0; b < 8; b++)
			crc = (crc & 1) ? (u16)((crc >> 1) ^ 0xa001) : (u16)(crc >> 1);
	}
	return crc;
}

static inline u16 nb_rd16 (const u8 *p)
{
	return (u16)p[0] | (u16)p[1] << 8;
}

//-----------------------------------------------------------------------------
///////////////			version / detection			///////////////
//-----------------------------------------------------------------------------

// The size and title count a version defines, or 0 for an unknown version.
static uint nds_banner_version_size (u16 version, uint *n_titles)
{
	uint size = 0, titles = 0;
	switch (version)
	{
		case 0x0001:
			size = NDS_BANNER_SIZE_V1;
			titles = 6;
			break;
		case 0x0002:
			size = NDS_BANNER_SIZE_V2;
			titles = 7;
			break;
		case 0x0003:
			size = NDS_BANNER_SIZE_V3;
			titles = 8;
			break;
		case 0x0103:
			size = NDS_BANNER_SIZE_DSI;
			titles = 8;
			break;
	}
	if (n_titles)
		*n_titles = titles;
	return size;
}

// The stored CRC16 for a version, and the region it covers. Every version
// stores the v1 CRC at 0x02 over 0x20..0x840; later versions add their own
// wider one, which is the one checked here (a v3 banner with a good v1 CRC
// but a broken v3 CRC is not a v3 banner).
static bool nds_banner_crc_ok (const u8 *data, uint size, u16 version)
{
	uint crc_off = 2, region = 0, region_size = 0;
	switch (version)
	{
		case 0x0001:
			region = 0x20;
			region_size = NDS_BANNER_SIZE_V1 - 0x20;
			break;
		case 0x0002:
			crc_off = 4;
			region = 0x20;
			region_size = NDS_BANNER_SIZE_V2 - 0x20;
			break;
		case 0x0003:
		case 0x0103:
			crc_off = 6;
			region = 0x20;
			region_size = NDS_BANNER_SIZE_V3 - 0x20;
			break;
		default:
			return false;
	}
	if ((size_t)region + region_size > size)
		return false;
	return nb_rd16 (data + crc_off) == nds_crc16 (data + region, region_size);
}

bool IsNDSBanner (const u8 *data, uint size)
{
	if (!data || size < NDS_BANNER_SIZE_V1)
		return false;
	const u16 version = nb_rd16 (data);
	uint n_titles = 0;
	const uint want = nds_banner_version_size (version, &n_titles);
	if (!want)
		return false;
	// A v0x103 banner is accepted at v3 size too: real DSi banners are
	// sometimes stored (and extracted by ndstool) truncated to 0xa40, with
	// only the animated-icon block missing.
	if (size < (version == 0x0103 ? NDS_BANNER_SIZE_V3 : want))
		return false;
	return nds_banner_crc_ok (data, size, version);
}

//-----------------------------------------------------------------------------
///////////////			scan					///////////////
//-----------------------------------------------------------------------------

// NUL-terminated (or field-width-truncated) UTF-16LE -> malloc'd UTF-8.
// Surrogate pairs are combined; a lone surrogate is passed through as
// U+FFFD rather than emitting invalid UTF-8.
static char *nds_utf16le_to_utf8 (const u8 *p, uint n_u16)
{
	uint cap = 32, len = 0;
	char *out = MALLOC (cap);
	const u8 *end = p + 2 * n_u16;

	while (p + 2 <= end)
	{
		u32 u = nb_rd16 (p);
		p += 2;
		if (!u)
			break;
		if (u >= 0xd800 && u < 0xdc00 && p + 2 <= end)
		{
			const u16 lo = nb_rd16 (p);
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

		if (len + 5 > cap)
		{
			cap *= 2;
			out = REALLOC (out, cap);
		}
		// Same encoder shape as dclib's PrintUTF8Char(), inlined so this
		// stays a plain byte-buffer append.
		if (u < 0x80)
			out[len++] = (char)u;
		else if (u < 0x800)
		{
			out[len++] = (char)(0xc0 | u >> 6);
			out[len++] = (char)(0x80 | (u & 0x3f));
		}
		else if (u < 0x10000)
		{
			out[len++] = (char)(0xe0 | u >> 12);
			out[len++] = (char)(0x80 | (u >> 6 & 0x3f));
			out[len++] = (char)(0x80 | (u & 0x3f));
		}
		else
		{
			out[len++] = (char)(0xf0 | u >> 18);
			out[len++] = (char)(0x80 | (u >> 12 & 0x3f));
			out[len++] = (char)(0x80 | (u >> 6 & 0x3f));
			out[len++] = (char)(0x80 | (u & 0x3f));
		}
	}

	out[len] = 0;
	return out;
}

enumError ScanNDSBanner (nds_banner_t *banner, const u8 *data, uint size)
{
	DASSERT (banner);
	memset (banner, 0, sizeof (*banner));
	if (!IsNDSBanner (data, size))
		return ERR_INVALID_DATA;

	banner->version = nb_rd16 (data);
	banner->size = nds_banner_version_size (banner->version, &banner->n_titles);

	for (uint i = 0; i < banner->n_titles; i++)
		banner->title[i] = nds_utf16le_to_utf8 (
			data + 0x240 + i * NDS_BANNER_TITLE_SIZE, NDS_BANNER_TITLE_SIZE / 2);

	banner->bitmap[0] = data + 0x20;
	banner->palette[0] = data + 0x220;
	banner->n_bitmaps = 1;

	// The animated block is optional even at version 0x103 (see IsNDSBanner):
	// only claim it when the whole region is present and its own CRC16 holds.
	if (banner->version == 0x0103 && size >= NDS_BANNER_SIZE_DSI
		&& nb_rd16 (data + 8) == nds_crc16 (data + 0x1240, NDS_BANNER_SIZE_DSI - 0x1240))
	{
		banner->animated = true;
		banner->n_bitmaps = NDS_BANNER_DSI_FRAMES;
		for (uint i = 0; i < NDS_BANNER_DSI_FRAMES; i++)
		{
			banner->bitmap[i] = data + 0x1240 + i * NDS_BANNER_BITMAP_SIZE;
			banner->palette[i] = data + 0x2240 + i * NDS_BANNER_PALETTE_SIZE;
		}

		for (uint i = 0; i < NDS_BANNER_DSI_SEQ_LEN; i++)
		{
			const u16 tok = nb_rd16 (data + 0x2340 + 2 * i);
			const u8 duration = (u8)(tok & 0xff);
			if (!duration)
				break; // end of sequence
			nds_banner_frame_t *f = banner->frame + banner->n_frames++;
			f->duration = duration;
			f->bitmap = (u8)(tok >> 8 & 7);
			f->palette = (u8)(tok >> 11 & 7);
			f->flip_h = (tok & 0x4000) != 0;
			f->flip_v = (tok & 0x8000) != 0;
		}
	}

	return ERR_OK;
}

void ResetNDSBanner (nds_banner_t *banner)
{
	if (!banner)
		return;
	for (uint i = 0; i < NDS_BANNER_MAX_TITLES; i++)
		FREE ((void *)banner->title[i]);
	memset (banner, 0, sizeof (*banner));
}

//-----------------------------------------------------------------------------
///////////////			icon decode				///////////////
//-----------------------------------------------------------------------------

static inline u8 nb_expand5b (uint v)
{
	return (u8)((v << 3) | (v >> 2));
}

enumError DecodeNDSBannerIcon_RGBA (
	u8 **dest, uint *width, uint *height, const nds_banner_t *banner, uint frame)
{
	if (!dest || !banner || !banner->bitmap[0])
		return EINVAL;

	// The static icon and the DSi animated icon are independent images: an
	// animation step names its own bitmap and palette out of the 0x1240 block
	// and is not a variation on the 0x20 one.
	const u8 *bitmap = banner->bitmap[0], *palette = banner->palette[0];
	bool flip_h = false, flip_v = false;
	if (frame != NDS_BANNER_ICON_STATIC)
	{
		if (!banner->animated || frame >= banner->n_frames)
			return ERROR0 (ERR_INVALID_DATA,
				"NDS banner has no animated icon frame #%u (%u available)\n", frame,
				banner->animated ? banner->n_frames : 0);
		const nds_banner_frame_t *f = banner->frame + frame;
		bitmap = banner->bitmap[f->bitmap];
		palette = banner->palette[f->palette];
		flip_h = f->flip_h;
		flip_v = f->flip_v;
	}

	u32 pal[16];
	for (uint i = 0; i < 16; i++)
	{
		const u16 c = nb_rd16 (palette + 2 * i);
		// BGR555: red in the low bits. Entry 0 is the transparent color and
		// is never drawn, whatever RGB it happens to hold.
		pal[i] = (u32)nb_expand5b (c & 0x1f) << 16 | (u32)nb_expand5b (c >> 5 & 0x1f) << 8
			| (u32)nb_expand5b (c >> 10 & 0x1f) | (i ? 0xff000000u : 0u);
	}

	const uint dim = NDS_BANNER_ICON_DIM;
	u8 *rgba = CALLOC ((size_t)dim * dim, 4);

	for (uint y = 0; y < dim; y++)
		for (uint x = 0; x < dim; x++)
		{
			// 4bpp in 8x8 tiles, 4 tiles per row, low nibble first.
			const uint sx = flip_h ? dim - 1 - x : x;
			const uint sy = flip_v ? dim - 1 - y : y;
			const uint tile = (sy / 8) * (dim / 8) + sx / 8;
			const uint idx = (sy & 7) * 8 + (sx & 7);
			const u8 byte = bitmap[tile * 32 + idx / 2];
			const uint ci = (idx & 1) ? byte >> 4 : byte & 0x0f;
			const u32 c = pal[ci];
			u8 *d = rgba + 4 * ((size_t)y * dim + x);
			d[0] = (u8)(c >> 16);
			d[1] = (u8)(c >> 8);
			d[2] = (u8)c;
			d[3] = (u8)(c >> 24);
		}

	*dest = rgba;
	if (width)
		*width = dim;
	if (height)
		*height = dim;
	return ERR_OK;
}

//-----------------------------------------------------------------------------
///////////////			text dump				///////////////
//-----------------------------------------------------------------------------

static const char *const nds_banner_lang_name[NDS_BANNER_LANG__N] = {
	"Japanese",
	"English",
	"French",
	"German",
	"Italian",
	"Spanish",
	"Chinese",
	"Korean",
};

char *TextNDSBanner (const nds_banner_t *banner)
{
	if (!banner)
		return 0;

	FastBuf_t fb;
	InitializeFastBufAlloc (&fb, 0x800);

	AppendFastBuf (&fb, "# Nintendo DS ROM banner\n", 25);
	char line[0x200];
	int n = snprintf (line, sizeof (line), "version = 0x%04x (%u title%s%s)\n", banner->version,
		banner->n_titles, banner->n_titles == 1 ? "" : "s",
		banner->animated ? ", DSi animated icon" : "");
	AppendFastBuf (&fb, line, n);

	for (uint i = 0; i < banner->n_titles && i < NDS_BANNER_LANG__N; i++)
	{
		ccp t = banner->title[i];
		if (!t || !*t)
			continue;
		n = snprintf (line, sizeof (line), "\n[%s]\n", nds_banner_lang_name[i]);
		AppendFastBuf (&fb, line, n);
		// A title is up to three newline-separated lines; keep them, but
		// prefix each so the block stays readable as a flat text file.
		while (*t)
		{
			ccp eol = strchr (t, '\n');
			const int len = eol ? (int)(eol - t) : (int)strlen (t);
			AppendFastBuf (&fb, "  ", 2);
			AppendFastBuf (&fb, t, len);
			AppendCharFastBuf (&fb, '\n');
			t = eol ? eol + 1 : t + len;
		}
	}

	if (banner->animated)
	{
		n = snprintf (line, sizeof (line), "\n[animation]\n# %u frame%s, looping\n",
			banner->n_frames, banner->n_frames == 1 ? "" : "s");
		AppendFastBuf (&fb, line, n);
		for (uint i = 0; i < banner->n_frames; i++)
		{
			const nds_banner_frame_t *f = banner->frame + i;
			n = snprintf (line, sizeof (line), "  frame %2u: bitmap %u, palette %u, %3u/60 s%s%s\n",
				i, f->bitmap, f->palette, f->duration, f->flip_h ? ", flip-h" : "",
				f->flip_v ? ", flip-v" : "");
			AppendFastBuf (&fb, line, n);
		}
	}

	char *out = STRDUP (GetFastBufString (&fb));
	ResetFastBuf (&fb);
	return out;
}
