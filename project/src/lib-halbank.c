/***************************************************************************
 *                                                                         *
 *   HAL Laboratory "A2" bank archive support (Kirby Air Ride)              *
 *                                                                         *
 *   Unlike the sysdolphin object graphs handled by lib-hsd.c, this format  *
 *   is not documented anywhere the author could find; the layout below was *
 *   reverse engineered from retail bytes of Kirby Air Ride (USA, GKYE01),  *
 *   /files/A2*.dat.  The evidence for each field is cited in the comments. *
 *                                                                         *
 ***************************************************************************/

#include "lib-halbank.h"
#include "lib-image.h"

#include <stdlib.h>
#include <string.h>
#include <ctype.h>

//
///////////////////////////////////////////////////////////////////////////////
///////////////			    container			///////////////
///////////////////////////////////////////////////////////////////////////////
//
// Header of A2Texture.dat (45248 bytes), verbatim:
//
//   00000000: 0000 0000 0000 0007 0000 0040 0000 00fc
//   00000010: 0000 005b 0000 061c 0000 0076 0000 0b3c
//   ...
//   00000040: 6132 6443 5531 702e 4338 5247 4235 4133   "a2dCU1p.C8RGB5A3
//   00000050: 5f33 325f 3234 2e74 6578 0061 3264 4355   _32_24.tex\0a2dCU
//
// +0x04 is 7 and exactly seven (u32,u32) pairs follow, ending at 0x40, which
// is precisely where the first name offset (0x40) points -- so the string
// pool starts immediately behind the pair table.  That "first name offset ==
// end of pair table" identity holds in all 14 bank files of the disc and is
// what makes the otherwise magic-less format safely identifiable.
//
// +0x00 is 0 in 13 of the 14 files and 0x0000a840 == the exact file size in
// the fourteenth (A2Window_new.dat, 43072 bytes), so it is a file-size field
// that the tool chain usually left unwritten.  Both spellings are accepted.
//
// Offsets are absolute from the start of file (0x40 and 0xfc above both are).
//
///////////////////////////////////////////////////////////////////////////////

#define HALBANK_MIN_SIZE 0x18
#define HALBANK_MAX_ENTRY 0x10000

static bool check_halbank (const u8 *data, uint size, uint *ret_n_entry)
{
	if (!data || size < HALBANK_MIN_SIZE)
		return false;

	const u32 fsize = be32 (data);
	const u32 n_entry = be32 (data + 0x04);

	if (fsize && fsize != size)
		return false;
	if (!n_entry || n_entry > HALBANK_MAX_ENTRY)
		return false;

	const u64 tab_end = 8 + 8 * (u64)n_entry;
	if (tab_end + 2 > size)
		return false;

	// the string pool directly follows the pair table
	if (be32 (data + 8) != tab_end)
		return false;

	for (uint i = 0; i < n_entry; i++)
	{
		const u32 name_off = be32 (data + 8 + 8 * i);
		const u32 data_off = be32 (data + 12 + 8 * i);

		if (name_off < tab_end || name_off >= size)
			return false;
		if (data_off < tab_end || data_off >= size)
			return false;

		// the name must be a non-empty, NUL-terminated printable ASCII string
		uint len = 0;
		while (name_off + len < size && data[name_off + len])
		{
			const u8 ch = data[name_off + len];
			if (ch < 0x20 || ch > 0x7e)
				return false;
			if (++len > 200)
				return false;
		}
		if (!len || name_off + len >= size)
			return false;
	}
	return ret_n_entry ? (*ret_n_entry = n_entry, true) : true;
}

///////////////////////////////////////////////////////////////////////////////

bool IsHALBank (const u8 *data, uint size)
{
	return check_halbank (data, size, 0);
}

///////////////////////////////////////////////////////////////////////////////

bool ScanHALBank (halbank_t *bank, const u8 *data, uint size)
{
	DASSERT (bank);
	memset (bank, 0, sizeof (*bank));

	uint n_entry;
	if (!check_halbank (data, size, &n_entry))
		return false;

	bank->data = data;
	bank->size = size;
	bank->n_entry = n_entry;
	bank->entry = CALLOC (n_entry, sizeof (*bank->entry));

	for (uint i = 0; i < n_entry; i++)
	{
		halbank_entry_t *e = bank->entry + i;
		e->name = (ccp)data + be32 (data + 8 + 8 * i);
		e->data_off = be32 (data + 12 + 8 * i);
	}

	// Payloads carry no length of their own -- the u32 in front of the first
	// one is 0 in the retail files -- so each blob runs up to whichever
	// payload starts next, and the last one runs to the end of file. The pair
	// table is not sorted by data offset, so scan all entries for the nearest
	// higher start rather than trusting entry order.
	for (uint i = 0; i < n_entry; i++)
	{
		halbank_entry_t *e = bank->entry + i;
		u32 end = size;
		for (uint j = 0; j < n_entry; j++)
		{
			const u32 off = bank->entry[j].data_off;
			if (off > e->data_off && off < end)
				end = off;
		}
		e->data_size = end - e->data_off;
	}
	return true;
}

///////////////////////////////////////////////////////////////////////////////

void ResetHALBank (halbank_t *bank)
{
	if (bank)
	{
		FREE (bank->entry);
		memset (bank, 0, sizeof (*bank));
	}
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			    name parser			///////////////
///////////////////////////////////////////////////////////////////////////////
//
// A texture entry name is a dot separated list ending in "tex".  The field
// directly in front of "tex" carries format and geometry:
//
//	<FORMAT>_<width>_<height>
//
// and an optional field before that holds four numbers, which are the
// 9-slice margins (left,right,top,bottom) of a stretchable UI panel:
//
//	arrow_r.12_12_12_12.C8RGB5A3_24_24.tex
//
// For the colour-indexed formats the token is the index format immediately
// followed by the palette format, "C8"+"RGB5A3".  The tokens map 1:1 onto
// this project's own image_format_t / palette_format_t, so the existing
// GameCube/Wii decoders in lib-image1.c do all the pixel work.
//
// Verified against A2Texture.dat, where the payload sizes match exactly:
//	replay.C8RGB5A3_160_40.tex  payload 6912 = 160*40 + 2*256
//	shadow.RGBA8_64_64.tex	    payload 16384 = 64*64*4
//	a2dCU1p.C8RGB5A3_32_24.tex  payload 1280 = 32*24 + 2*256
//
///////////////////////////////////////////////////////////////////////////////

typedef struct halbank_fmt_t
{
	ccp name;
	int fmt; // image_format_t or palette_format_t
} halbank_fmt_t;

static const halbank_fmt_t halbank_iform_tab[] = { { "I4", IMG_I4 }, { "I8", IMG_I8 },
	{ "IA4", IMG_IA4 }, { "IA8", IMG_IA8 }, { "RGB565", IMG_RGB565 }, { "RGB5A3", IMG_RGB5A3 },
	{ "RGBA8", IMG_RGBA32 }, // HAL's spelling of GX_TF_RGBA8
	{ "RGBA32", IMG_RGBA32 }, { "CMPR", IMG_CMPR }, { 0, 0 } };

static const halbank_fmt_t halbank_cform_tab[]
	= { { "C4", IMG_C4 }, { "C8", IMG_C8 }, { "C14X2", IMG_C14X2 }, { 0, 0 } };

static const halbank_fmt_t halbank_pform_tab[]
	= { { "IA8", PAL_IA8 }, { "RGB565", PAL_RGB565 }, { "RGB5A3", PAL_RGB5A3 }, { 0, 0 } };

//-----------------------------------------------------------------------------

// Parse "<FORMAT>[<PALETTE>]" of length 'len'.
static bool parse_format (ccp tok, uint len, image_format_t *ret_iform, palette_format_t *ret_pform)
{
	*ret_pform = PAL_INVALID;

	for (const halbank_fmt_t *f = halbank_iform_tab; f->name; f++)
		if (strlen (f->name) == len && !memcmp (tok, f->name, len))
		{
			*ret_iform = f->fmt;
			return true;
		}

	for (const halbank_fmt_t *c = halbank_cform_tab; c->name; c++)
	{
		const uint clen = strlen (c->name);
		if (clen >= len || memcmp (tok, c->name, clen))
			continue;
		for (const halbank_fmt_t *p = halbank_pform_tab; p->name; p++)
			if (strlen (p->name) == len - clen && !memcmp (tok + clen, p->name, len - clen))
			{
				*ret_iform = c->fmt;
				*ret_pform = p->fmt;
				return true;
			}
	}
	return false;
}

//-----------------------------------------------------------------------------

typedef struct halbank_tex_t
{
	image_format_t iform;
	palette_format_t pform;
	int width; // <0: not stated by the name
	int height;
	uint margin[4]; // 9-slice margins, 0 if absent
	bool have_margin;
	bool guessed; // geometry recovered, see below
} halbank_tex_t;

// Split 'name' at dots and identify the format field. Returns false for any
// entry that is not a texture (e.g. the ".tm" 2D scene blobs).
static bool parse_tex_name (ccp name, halbank_tex_t *tex)
{
	memset (tex, 0, sizeof (*tex));
	tex->pform = PAL_INVALID;
	tex->width = -1;
	tex->height = -1;

	const uint len = strlen (name);
	if (len < 5 || strcasecmp (name + len - 4, ".tex"))
		return false;

	// the format field is the last dot separated field before ".tex"
	ccp fend = name + len - 4;
	ccp fbeg = fend;
	while (fbeg > name && fbeg[-1] != '.')
		fbeg--;
	if (fbeg == name)
		return false;

	// ... which is "<FORMAT>_<w>_<h>": chop the two trailing _<number> parts.
	// The numbers are signed because the retail tool chain does emit
	// uninitialised ones, see below.
	ccp hbeg = fend;
	while (hbeg > fbeg && hbeg[-1] != '_')
		hbeg--;
	if (hbeg <= fbeg + 1)
		return false;
	ccp wbeg = hbeg - 1;
	while (wbeg > fbeg && wbeg[-1] != '_')
		wbeg--;
	if (wbeg <= fbeg + 1)
		return false;

	if (!parse_format (fbeg, (uint)(wbeg - 1 - fbeg), &tex->iform, &tex->pform))
		return false;

	char *end;
	const long w = strtol (wbeg, &end, 10);
	if (end != hbeg - 1)
		return false;
	const long h = strtol (hbeg, &end, 10);
	if (end != fend)
		return false;
	if (w > 0 && w <= 4096 && h > 0 && h <= 4096)
	{
		tex->width = (int)w;
		tex->height = (int)h;
	}

	// optional 9-slice margin field "<l>_<r>_<t>_<b>" in front of the format
	if (fbeg - 1 > name)
	{
		ccp mend = fbeg - 1;
		ccp mbeg = mend;
		while (mbeg > name && mbeg[-1] != '.')
			mbeg--;
		uint n = 0;
		ccp p = mbeg;
		while (n < 4)
		{
			const long v = strtol (p, &end, 10);
			if (end == p || v < 0 || v > 4096)
				break;
			tex->margin[n++] = (uint)v;
			p = end;
			if (n < 4)
			{
				if (*p != '_')
					break;
				p++;
			}
		}
		tex->have_margin = n == 4 && p == mend;
	}
	return true;
}

//-----------------------------------------------------------------------------

static uint gx_image_size (uint width, uint height, image_format_t iform)
{
	uint size = 0;
	CalcImageGeometry (iform, width, height, 0, 0, 0, 0, &size);
	return size;
}

// A2Window_new.dat stores 0xcccccccc (== -858993460, the MSVC uninitialised
// stack fill) in place of both dimensions of all 46 of its entries, so the
// names there read "a_disable.8_8_8_8.RGB5A3_-858993460_-858993460.tex".
// The payload size still pins down width*height, and the 9-slice margins give
// width >= left+right and height >= top+bottom -- on the whole 46-entry set
// that leaves exactly one candidate for 40 of them and a squarest-first
// choice among 3 for the remaining 6.  Geometry recovered this way is flagged
// in the output file name, because it is inference and not read from the file.
static bool guess_geometry (const halbank_tex_t *tex, uint payload, uint *rw, uint *rh)
{
	if (tex->pform != PAL_INVALID || !tex->have_margin)
		return false; // palette split unknown / no constraint to work with

	const uint min_w = tex->margin[0] + tex->margin[1];
	const uint min_h = tex->margin[2] + tex->margin[3];

	uint best_w = 0, best_h = 0, best_delta = 0;
	for (uint w = 4; w <= 1024; w += 4)
	{
		if (w < min_w)
			continue;
		for (uint h = 4; h <= 1024; h += 4)
		{
			if (h < min_h || gx_image_size (w, h, tex->iform) != payload)
				continue;
			const uint delta = w > h ? w - h : h - w;
			if (!best_w || delta < best_delta)
			{
				best_w = w;
				best_h = h;
				best_delta = delta;
			}
		}
	}
	if (!best_w)
		return false;
	*rw = best_w;
	*rh = best_h;
	return true;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			     export			///////////////
///////////////////////////////////////////////////////////////////////////////

#define HALBANK_COPY_SLACK 64 // zero padding, see lib-hsd.c for the reason

static void sanitize (char *buf, uint bufsize, ccp name)
{
	uint i = 0;
	for (; name[i] && i + 1 < bufsize; i++)
	{
		const char ch = name[i];
		buf[i] = isalnum ((int)(u8)ch) || ch == '-' || ch == '_' ? ch : '_';
	}
	buf[i] = 0;
}

///////////////////////////////////////////////////////////////////////////////

int ExportHALBankTextures (const halbank_t *bank, ccp dest_dir, ccp basename)
{
	if (!bank || !bank->data || !dest_dir)
		return -1;

	char dir[PATH_MAX];
	StringCopyS (dir, sizeof (dir), dest_dir);

	char base[80];
	StringCopyS (base, sizeof (base), basename && *basename ? basename : "halbank");
	char *dot = strrchr (base, '.');
	if (dot && dot > base && (!strcasecmp (dot, ".dat") || !strcasecmp (dot, ".usd")))
		*dot = 0;

	int written = 0;
	bool path_done = false;

	for (uint i = 0; i < bank->n_entry; i++)
	{
		const halbank_entry_t *e = bank->entry + i;

		halbank_tex_t tex;
		if (!parse_tex_name (e->name, &tex))
			continue;

		uint width, height;
		if (tex.width > 0)
		{
			width = tex.width;
			height = tex.height;
		}
		else
		{
			if (!guess_geometry (&tex, e->data_size, &width, &height))
				continue;
			tex.guessed = true;
		}

		const uint img_size = gx_image_size (width, height, tex.iform);
		if (!img_size || img_size > e->data_size)
			continue;

		// the remainder of the payload is the palette, two bytes per colour
		uint n_pal = 0;
		if (tex.pform != PAL_INVALID)
		{
			uint max_pal;
			switch (tex.iform)
			{
				case IMG_C4:
					max_pal = 1 << 4;
					break;
				case IMG_C8:
					max_pal = 1 << 8;
					break;
				default:
					max_pal = 1 << 14;
					break;
			}
			n_pal = (e->data_size - img_size) / 2;
			if (!n_pal)
				continue;
			// lib-image1.c's TransformPalette() sizes its working palette by
			// the index width of the target format, so never hand it more
			// colours than the indices can address (same trap as in lib-hsd.c)
			if (n_pal > max_pal)
				n_pal = max_pal;
		}
		// A plain (non-indexed) texture needs no palette; blobs are padded up
		// to a 32-byte boundary, so it does not fill its slot exactly and the
		// "img_size > data_size" guard above is the only size test possible.

		if (!path_done)
		{
			CreatePath (dir, true);
			path_done = true;
		}

		Image_t img;
		InitializeIMG (&img);
		img.data = CALLOC (1, img_size + HALBANK_COPY_SLACK);
		memcpy (img.data, bank->data + e->data_off, img_size);
		img.data_alloced = true;
		img.data_size = img_size;
		img.width = width;
		img.height = height;
		img.iform = img.info_iform = tex.iform;
		img.info_fform = FF_PNG;
		img.info_n_image = 1;
		img.endian = &be_func;
		CalcImageGeometry (img.iform, img.width, img.height, &img.xwidth, &img.xheight, 0, 0, 0);

		if (n_pal)
		{
			img.pal = CALLOC (1, 4 * n_pal + HALBANK_COPY_SLACK);
			memcpy (img.pal, bank->data + e->data_off + img_size, 2 * n_pal);
			img.pal_alloced = true;
			img.pal_size = 2 * n_pal;
			img.n_pal = n_pal;
			img.pform = img.info_pform = tex.pform;
		}

		char sname[120];
		sanitize (sname, sizeof (sname), e->name);

		char path[PATH_MAX];
		snprintf (path, sizeof (path), "%s/%s.%s_%ux%u_%s%s.png", dir, base, sname, width, height,
			GetImageFormatName (tex.iform, "?"), tex.guessed ? "_guessed-size" : "");

		if (!ConvertIMG (&img, false, 0, IMG_X_RGB, PAL_INVALID)
			&& !SavePNG (&img, false, 0, path, 0, 0, true, 0))
		{
			written++;
		}
		ResetIMG (&img);
	}

	return written;
}

///////////////////////////////////////////////////////////////////////////////

int ExportHALBankTexturesFromData (const u8 *data, uint size, ccp dest_dir, ccp basename)
{
	halbank_t bank;
	if (!ScanHALBank (&bank, data, size))
		return -1;
	const int stat = ExportHALBankTextures (&bank, dest_dir, basename);
	ResetHALBank (&bank);
	return stat;
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
