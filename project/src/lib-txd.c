// SPDX-License-Identifier: GPL-2.0+
// See lib-txd.h for the layout and what is/isn't recovered.
#include "lib-txd.h"
#include "lib-archive-util.h"
#include "lib-image.h"
#include "lib-nintendo.h"
#include <string.h>

#define TXD_REC_MAGIC 0x0020af30
#define TXD_REC_MAGIC_OFF 0xA0 // magic offset relative to record start
#define TXD_REC_DATA_OFF 0xE0 // pixel data offset relative to record start
#define TXD_MAX_RECORDS 100000

//-----------------------------------------------------------------------------

typedef struct txd_tex_t
{
	u32 rec_off; // record start (magic_off - TXD_REC_MAGIC_OFF)
	ccp name; // pointer into 'raw', NUL-terminated (may be empty)
	u32 width, height;
	u32 format; // GX-native image_format_t value
	u8 mip_count;
} txd_tex_t;

//-----------------------------------------------------------------------------
// Locate every texture record by scanning for the fixed per-record magic.
// This is the one structural invariant that held across every real sample
// checked -- see lib-txd.h. Returns the number of records found.

static uint find_txd_records (const u8 *raw, size_t size, txd_tex_t **ret_tex)
{
	txd_tex_t *tex = MALLOC (sizeof (txd_tex_t) * TXD_MAX_RECORDS);
	uint n = 0;

	for (size_t off = 0; off + 4 <= size && n < TXD_MAX_RECORDS; off++)
	{
		if (rd_be32 (raw + off) != TXD_REC_MAGIC)
			continue;
		if (off < TXD_REC_MAGIC_OFF)
			continue;
		const u32 rec_off = (u32) (off - TXD_REC_MAGIC_OFF);
		if (rec_off + TXD_REC_DATA_OFF > size || off + 0x18 > size)
			continue;

		txd_tex_t *t = tex + n;
		t->rec_off = rec_off;
		t->name = (ccp) (raw + rec_off + 0x08);
		t->width = rd_be16 (raw + off + 0x16);
		t->height = rd_be16 (raw + off + 0x14);
		t->format = rd_be32 (raw + off + 0x18);
		t->mip_count = raw[rec_off + 0x7c];
		n++;
	}

	*ret_tex = tex;
	return n;
}

//-----------------------------------------------------------------------------
// Sum the tile-padded GX pixel-data size of 'mip_count' mip levels starting
// at 'width'x'height' in GX-native format 'format', using this project's
// own geometry table (the authority on GX tile padding) rather than a
// hand-rolled one. Returns 0 if 'format' isn't a valid intern GX format.

static u32 calc_txd_data_size (image_format_t format, u32 width, u32 height, uint mip_count)
{
	if (format >= IMG_N_INTERN)
		return 0;

	u32 total = 0;
	u32 w = width, h = height;
	for (uint m = 0; m < mip_count; m++)
	{
		uint img_size = 0;
		if (!CalcImageGeometry (format, w ? w : 1, h ? h : 1, 0, 0, 0, 0, &img_size))
			return 0;
		total += img_size;
		w /= 2;
		h /= 2;
	}
	return total;
}

//-----------------------------------------------------------------------------

enumError ExtractTXDArchive (ccp arg, ccp basedir, uint depth)
{
	u8 *raw = 0;
	size_t raw_size = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false))
		return ERR_NOTHING_TO_DO;

	if (raw_size < 0x10 || memcmp (raw, "TDCT", 4))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	// The one invariant this decoder trusts at the file level: the header's
	// own declared total size must equal the real file size -- verified on
	// all 183 real .txd samples found on the Wii disc.
	const u32 declared_size = rd_be32 (raw + 0x08);
	if (declared_size != raw_size)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	const u32 data_end = rd_be32 (raw + 0x0c); // end of texture-data region

	txd_tex_t *tex = 0;
	const uint n_tex = find_txd_records (raw, raw_size, &tex);
	if (!n_tex)
	{
		FREE (tex);
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	char dest[PATH_MAX];
	get_dest_dir (dest, sizeof (dest), arg, basedir);
	CreatePath (dest, true);

	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT TXD:%s (%u textures) -> %s/\n",
			verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, n_tex, dest);

	enumError err = ERR_OK;
	uint n_png = 0, n_raw = 0;
	if (!testmode)
	{
		for (uint i = 0; i < n_tex; i++)
		{
			const txd_tex_t *t = tex + i;
			const u32 data_off = t->rec_off + TXD_REC_DATA_OFF;
			const u32 seg_end = i + 1 < n_tex ? tex[i + 1].rec_off : data_end;
			const u32 seg_size = seg_end > data_off ? seg_end - data_off : 0;

			// A record with no real name has pixel/header noise sitting at
			// the name offset instead -- accept it only if every byte up
			// to the NUL is plain printable ASCII, otherwise fall back to
			// an index-based name rather than emitting garbage filenames.
			// t->name points into 'raw' with at least TXD_REC_DATA_OFF-0x08
			// bytes guaranteed available (checked in find_txd_records());
			// cap the scan there so a name with no NUL in range can't run
			// past the buffer.
			const uint name_max = TXD_REC_DATA_OFF - 0x08;
			bool name_ok = *t->name != 0;
			for (uint k = 0; name_ok && k < name_max && t->name[k]; k++)
				if ((u8) t->name[k] < 0x20 || (u8) t->name[k] > 0x7e)
					name_ok = false;
			if (name_ok && !memchr (t->name, 0, name_max))
				name_ok = false;

			char name[64];
			if (name_ok)
				StringCopyS (name, sizeof (name), t->name);
			else
				snprintf (name, sizeof (name), "tex%03u", i);
			// sanitize: this is only ever used as a filename component
			for (char *p = name; *p; p++)
				if (*p == '/' || *p == '\\' || (u8) *p < 0x20)
					*p = '_';

			const u32 mip_size = calc_txd_data_size (t->format, t->width, t->height, t->mip_count ? t->mip_count : 1);
			const bool verified = t->width && t->height && data_off + seg_size <= raw_size
				&& mip_size && mip_size == seg_size;

			char path[PATH_MAX];
			bool wrote_png = false;
			if (verified)
			{
				uint base_size = 0;
				CalcImageGeometry (t->format, t->width, t->height, 0, 0, 0, 0, &base_size);
				if (base_size && data_off + base_size <= raw_size)
				{
					Image_t img;
					InitializeIMG (&img);
					img.data = CALLOC (1, base_size + 64);
					memcpy (img.data, raw + data_off, base_size);
					img.data_alloced = true;
					img.data_size = base_size;
					img.width = t->width;
					img.height = t->height;
					img.iform = img.info_iform = (image_format_t) t->format;
					img.info_fform = FF_PNG;
					img.info_n_image = 1;
					img.endian = &be_func;
					CalcImageGeometry (img.iform, img.width, img.height, &img.xwidth, &img.xheight, 0, 0, 0);

					snprintf (path, sizeof (path), "%s/%s.png", dest, name);
					if (!ConvertIMG (&img, false, 0, IMG_X_RGB, PAL_INVALID)
						&& !SavePNG (&img, false, 0, path, 0, 0, true, 0))
					{
						n_png++;
						wrote_png = true;
					}
					ResetIMG (&img);
				}
			}
			if (!wrote_png && data_off < raw_size)
			{
				// unrecovered size, or the reconciled decode still failed --
				// carve the raw bytes out untouched rather than guess
				snprintf (path, sizeof (path), "%s/%s.raw.bin", dest, name);
				if (!SaveFile (path, 0, 0, raw + data_off, seg_size, 0))
					n_raw++;
			}
		}

		if (verbose >= 0)
			fprintf (stdlog, "  %u texture(s) decoded to PNG, %u carved out raw (unrecovered size)\n",
				n_png, n_raw);
	}

	FREE (tex);
	FREE (raw);
	(void) depth;
	return err;
}
