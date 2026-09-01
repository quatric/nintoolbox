// SMDH (3DS application icon + title metadata) -- see lib-smdh.h.
//
// Struct layout verified field-by-field against Gericom's EveryFileExplorer
// (3DS/SMDH.cs: SMDHHeader, ApplicationTitle, ApplicationSettings), since
// 3dbrew.org itself was unreachable while this was written. No retail SMDH
// sample was available either -- this is a from-spec decoder, not yet
// verified against a real title's icon.bin/CIA (see the README note).

#include "lib-std.h"
#include "lib-smdh.h"

static inline u16 srd16 (const u8 *p)
{
	return (u16)p[0] | (u16)p[1] << 8;
}
static inline u32 srd32 (const u8 *p)
{
	return (u32)p[0] | (u32)p[1] << 8 | (u32)p[2] << 16 | (u32)p[3] << 24;
}
static inline u64 srd64 (const u8 *p)
{
	return (u64)srd32 (p) | (u64)srd32 (p + 4) << 32;
}

static inline void swr16 (u8 *p, u16 v)
{
	p[0] = (u8)(v & 0xff);
	p[1] = (u8)((v >> 8) & 0xff);
}

static inline void swr32 (u8 *p, u32 v)
{
	p[0] = (u8)(v & 0xff);
	p[1] = (u8)((v >> 8) & 0xff);
	p[2] = (u8)((v >> 16) & 0xff);
	p[3] = (u8)((v >> 24) & 0xff);
}

static inline void swr64 (u8 *p, u64 v)
{
	swr32 (p, (u32)(v & 0xffffffff));
	swr32 (p + 4, (u32)(v >> 32));
}

//-----------------------------------------------------------------------------

// NUL-terminated (or field-width-truncated) UTF-16LE -> malloc'd UTF-8.
static char *smdh_utf16le_to_utf8 (const u8 *p, uint n_u16)
{
	uint cap = 4, len = 0;
	char *out = MALLOC (cap);
	if (!out)
		return 0;

	const u8 *end = p + 2 * n_u16;
	while (p + 2 <= end)
	{
		const u16 u = srd16 (p);
		p += 2;
		if (!u)
			break;

		u32 cp = u;
		if (u >= 0xd800 && u <= 0xdbff && p + 2 <= end)
		{
			const u16 lo = srd16 (p);
			if (lo >= 0xdc00 && lo <= 0xdfff)
			{
				cp = 0x10000 + ((u - 0xd800) << 10) + (lo - 0xdc00);
				p += 2;
			}
			else
				cp = 0xfffd;
		}
		else if (u >= 0xd800 && u <= 0xdfff)
			cp = 0xfffd;

		char enc[4];
		uint n;
		if (cp < 0x80)
		{
			enc[0] = (char)cp;
			n = 1;
		}
		else if (cp < 0x800)
		{
			enc[0] = (char)(0xc0 | (cp >> 6));
			enc[1] = (char)(0x80 | (cp & 0x3f));
			n = 2;
		}
		else if (cp < 0x10000)
		{
			enc[0] = (char)(0xe0 | (cp >> 12));
			enc[1] = (char)(0x80 | ((cp >> 6) & 0x3f));
			enc[2] = (char)(0x80 | (cp & 0x3f));
			n = 3;
		}
		else
		{
			enc[0] = (char)(0xf0 | (cp >> 18));
			enc[1] = (char)(0x80 | ((cp >> 12) & 0x3f));
			enc[2] = (char)(0x80 | ((cp >> 6) & 0x3f));
			enc[3] = (char)(0x80 | (cp & 0x3f));
			n = 4;
		}

		if (len + n + 1 > cap)
		{
			cap = (len + n + 1) * 2;
			char *grown = REALLOC (out, cap);
			if (!grown)
			{
				FREE (out);
				return 0;
			}
			out = grown;
		}
		memcpy (out + len, enc, n);
		len += n;
	}
	out[len] = 0;
	return out;
}

//-----------------------------------------------------------------------------

enumError ScanSMDH (smdh_t *smdh, const u8 *data, uint size)
{
	DASSERT (smdh);
	memset (smdh, 0, sizeof (*smdh));

	if (!data || size < SMDH_SIZE || memcmp (data, "SMDH", 4))
		return ERROR0 (ERR_INVALID_IFORM, "Not a SMDH file (bad magic or size < 0x%x)\n", SMDH_SIZE);

	smdh->version = srd16 (data + 4);

	const u8 *p = data + 8;
	for (uint i = 0; i < SMDH_N_TITLES; i++, p += 0x200)
	{
		smdh->title[i].short_desc = smdh_utf16le_to_utf8 (p, 0x40);
		smdh->title[i].long_desc = smdh_utf16le_to_utf8 (p + 0x80, 0x80);
		smdh->title[i].publisher = smdh_utf16le_to_utf8 (p + 0x180, 0x40);
	}

	// p now points at the application-settings block, offset 0x2008
	memcpy (smdh->game_ratings, p, 0x10);
	smdh->region_lock = srd32 (p + 0x10);
	smdh->matchmaker_id = srd32 (p + 0x14);
	smdh->matchmaker_bit_id = srd64 (p + 0x18);
	smdh->flags = srd32 (p + 0x20);
	smdh->eula_version = srd16 (p + 0x24);
	// p+0x26: reserved u16
	u32 frame_bits = srd32 (p + 0x28);
	memcpy (&smdh->banner_frame, &frame_bits, 4);
	smdh->streetpass_id = srd32 (p + 0x2c);
	p += 0x30; // application_settings_t is 0x30 bytes
	p += 8; // 8 reserved bytes

	smdh->small_icon = p;
	smdh->large_icon = p + SMDH_SMALL_ICON_SIZE;

	DASSERT (smdh->large_icon + SMDH_LARGE_ICON_SIZE == data + SMDH_SIZE);
	return ERR_OK;
}

//-----------------------------------------------------------------------------

void ResetSMDH (smdh_t *smdh)
{
	if (!smdh)
		return;
	for (uint i = 0; i < SMDH_N_TITLES; i++)
	{
		FREE ((void *)smdh->title[i].short_desc);
		FREE ((void *)smdh->title[i].long_desc);
		FREE ((void *)smdh->title[i].publisher);
	}
	memset (smdh, 0, sizeof (*smdh));
}

//-----------------------------------------------------------------------------
///////////////			icon decode (RGB565, Morton tiled)	///////////////
//-----------------------------------------------------------------------------

// Same 8x8-tile Z-order (Morton) scheme used by this codebase's other 3DS
// GPU-tiled textures (BCLIM/CTPK/BCH -- see morton8() in lib-nintendo.c);
// duplicated here as its own tiny static helper rather than exporting the
// other file's private one, matching how this codebase already duplicates
// such small per-format helpers (e.g. div_round_up/round_up).
static uint smdh_morton8 (uint x, uint y)
{
	return (x & 1) | (y & 1) << 1 | (x & 2) << 1 | (y & 2) << 2 | (x & 4) << 2 | (y & 4) << 3;
}

static inline u8 smdh_expand5b (uint v)
{
	return (u8)((v << 3) | (v >> 2));
}
static inline u8 smdh_expand6b (uint v)
{
	return (u8)((v << 2) | (v >> 4));
}

enumError DecodeSMDHIcon_RGBA (u8 **dest, uint *width, uint *height, const smdh_t *smdh, bool large)
{
	if (!dest || !smdh)
		return EINVAL;

	const uint dim = large ? 48 : 24;
	const u8 *src = large ? smdh->large_icon : smdh->small_icon;
	if (!src)
		return ERROR0 (ERR_INVALID_IFORM, "SMDH icon not scanned\n");

	u8 *rgba = MALLOC ((size_t)dim * dim * 4);
	if (!rgba)
		return ERR_CANT_CREATE;

	for (uint y = 0; y < dim; y++)
		for (uint x = 0; x < dim; x++)
		{
			const uint pos = ((y / 8) * (dim / 8) + x / 8) * 128 + smdh_morton8 (x & 7, y & 7) * 2;
			const u16 c = srd16 (src + pos);
			u8 *d = rgba + 4 * ((size_t)y * dim + x);
			d[0] = smdh_expand5b (c >> 11);
			d[1] = smdh_expand6b ((c >> 5) & 0x3f);
			d[2] = smdh_expand5b (c & 0x1f);
			d[3] = 0xff;
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

static const char *const smdh_lang_name[SMDH_LANG__N] = {
	"Japanese",
	"English",
	"French",
	"German",
	"Italian",
	"Spanish",
	"Chinese (Simplified)",
	"Korean",
	"Dutch",
	"Portuguese",
	"Russian",
	"Chinese (Traditional)",
};

char *TextSMDH (const smdh_t *smdh)
{
	if (!smdh)
		return STRDUP ("");

	char buf[0x4000];
	uint n = 0;

#undef ADD
#define ADD(...) n += n < sizeof (buf) ? snprintf (buf + n, sizeof (buf) - n, __VA_ARGS__) : 0

	ADD ("# SMDH: 3DS application icon/title metadata\n");
	ADD ("version = %u\n", smdh->version);
	ADD ("region-lock = 0x%02x\n", smdh->region_lock);
	ADD ("matchmaker-id = 0x%08x\n", smdh->matchmaker_id);
	ADD ("matchmaker-bit-id = 0x%016llx\n", (unsigned long long)smdh->matchmaker_bit_id);
	ADD ("flags = 0x%08x\n", smdh->flags);
	ADD ("eula-version = %u\n", smdh->eula_version);
	ADD ("streetpass-id = 0x%08x\n", smdh->streetpass_id);

	for (uint i = 0; i < SMDH_LANG__N; i++)
	{
		const smdh_title_t *t = smdh->title + i;
		if ((t->short_desc && *t->short_desc) || (t->long_desc && *t->long_desc)
			|| (t->publisher && *t->publisher))
		{
			ADD ("\n[%s]\n", smdh_lang_name[i]);
			ADD ("short-title = %s\n", t->short_desc ? t->short_desc : "");
			ADD ("long-title  = %s\n", t->long_desc ? t->long_desc : "");
			ADD ("publisher   = %s\n", t->publisher ? t->publisher : "");
		}
	}
#undef ADD

	if (n >= sizeof (buf))
		n = sizeof (buf) - 1;
	char *out = MALLOC (n + 1);
	if (out)
	{
		memcpy (out, buf, n);
		out[n] = 0;
	}
	return out;
}

//-----------------------------------------------------------------------------

static void smdh_utf8_to_utf16le (u8 *dest, uint max_u16, ccp src)
{
	memset (dest, 0, max_u16 * 2);
	if (!src)
		return;

	uint out_idx = 0;
	const u8 *s = (const u8 *)src;
	while (*s && out_idx < max_u16)
	{
		u32 cp = 0;
		if (*s < 0x80)
			cp = *s++;
		else if ((*s & 0xe0) == 0xc0)
		{
			cp = (*s++ & 0x1f) << 6;
			if (*s) cp |= (*s++ & 0x3f);
		}
		else if ((*s & 0xf0) == 0xe0)
		{
			cp = (*s++ & 0x0f) << 12;
			if (*s) cp |= (*s++ & 0x3f) << 6;
			if (*s) cp |= (*s++ & 0x3f);
		}
		else
		{
			s++;
			continue;
		}

		if (cp < 0x10000)
		{
			dest[out_idx * 2] = (u8)(cp & 0xFF);
			dest[out_idx * 2 + 1] = (u8)((cp >> 8) & 0xFF);
			out_idx++;
		}
	}
}

enumError EncodeSMDH (u8 **dest, uint *dest_size, const smdh_t *smdh)
{
	if (!dest || !dest_size || !smdh)
		return EINVAL;

	u8 *out = MALLOC (SMDH_SIZE);
	if (!out)
		return ERR_CANT_CREATE;

	memset (out, 0, SMDH_SIZE);
	memcpy (out, "SMDH", 4);
	swr16 (out + 4, smdh->version);

	u8 *p = out + 8;
	for (uint i = 0; i < SMDH_N_TITLES; i++, p += 0x200)
	{
		smdh_utf8_to_utf16le (p, 0x40, smdh->title[i].short_desc);
		smdh_utf8_to_utf16le (p + 0x80, 0x80, smdh->title[i].long_desc);
		smdh_utf8_to_utf16le (p + 0x180, 0x40, smdh->title[i].publisher);
	}

	// Application settings at offset 0x2008
	memcpy (p, smdh->game_ratings, 0x10);
	swr32 (p + 0x10, smdh->region_lock);
	swr32 (p + 0x14, smdh->matchmaker_id);
	swr64 (p + 0x18, smdh->matchmaker_bit_id);
	swr32 (p + 0x20, smdh->flags);
	swr16 (p + 0x24, smdh->eula_version);

	u32 frame_bits = 0;
	memcpy (&frame_bits, &smdh->banner_frame, 4);
	swr32 (p + 0x28, frame_bits);
	swr32 (p + 0x2c, smdh->streetpass_id);

	p += 0x30;
	p += 8; // 8 reserved bytes

	if (smdh->small_icon)
		memcpy (p, smdh->small_icon, SMDH_SMALL_ICON_SIZE);
	if (smdh->large_icon)
		memcpy (p + SMDH_SMALL_ICON_SIZE, smdh->large_icon, SMDH_LARGE_ICON_SIZE);

	*dest = out;
	*dest_size = SMDH_SIZE;
	return ERR_OK;
}
