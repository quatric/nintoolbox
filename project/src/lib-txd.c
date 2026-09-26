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
	u32 data_off; // pixel data start = magic_off + (TXD_REC_DATA_OFF-TXD_REC_MAGIC_OFF)
	ccp name; // pointer into 'raw', NUL-terminated; NULL if unavailable
	u32 width, height;
	u32 format; // GX-native image_format_t value
} txd_tex_t;

//-----------------------------------------------------------------------------
// Locate every texture record by scanning for the fixed per-record magic.
// This is the one structural invariant that held across every real sample
// checked -- see lib-txd.h. Returns the number of records found.
//
// The width/height/format triplet sits at a variable offset from the magic:
// magic+0x04 holds a u32 "plane count" (2 zero-filled u32s are inserted per
// unit above 1 -- verified against every real sample: plane_count==1 puts
// the triplet at +0x14, plane_count==2 at +0x1c, i.e. offset = 0x0c+8*n).
// The original implementation assumed a fixed +0x14 and only decoded
// plane_count==1 records; accounting for the real offset raised per-texture
// PNG coverage on the source disc from 182/757 (24%) to 728/757 (96%) with
// no other change -- see lib-txd.h for the full before/after numbers.
//
// Pixel data always starts a fixed TXD_REC_DATA_OFF-TXD_REC_MAGIC_OFF (0x40)
// bytes after the magic -- that offset is derived straight from the magic,
// so it holds even for the one case where the "record start" concept itself
// doesn't (the very first record in a file can have a shorter pre-magic
// preamble than the usual ~0xA0-0xE0 bytes between later records, since
// there's no previous record's trailing name-teaser eating space before
// it). The name lookup still needs a record start to anchor on, so it's
// simply skipped (falls back to an index-based name) when the magic is too
// close to the start of the file for TXD_REC_MAGIC_OFF to make sense.

static uint find_txd_records (const u8 *raw, size_t size, txd_tex_t **ret_tex)
{
	txd_tex_t *tex = MALLOC (sizeof (txd_tex_t) * TXD_MAX_RECORDS);
	uint n = 0;

	for (size_t off = 0; off + 4 <= size && n < TXD_MAX_RECORDS; off++)
	{
		if (rd_be32 (raw + off) != TXD_REC_MAGIC)
			continue;

		const u32 data_off = (u32)(off + (TXD_REC_DATA_OFF - TXD_REC_MAGIC_OFF));
		if (data_off > size)
			continue;

		const u32 plane_count = rd_be32 (raw + off + 0x04);
		if (plane_count > 16) // sanity clamp; real samples only ever use 1-2
			continue;
		const size_t dim_off = off + 0x0c + 8 * (size_t)plane_count;
		if (dim_off + 12 > size)
			continue;

		txd_tex_t *t = tex + n;
		t->data_off = data_off;
		t->name = off >= TXD_REC_MAGIC_OFF ? (ccp)(raw + off - TXD_REC_MAGIC_OFF + 0x08) : 0;
		t->height = rd_be16 (raw + dim_off);
		t->width = rd_be16 (raw + dim_off + 2);
		t->format = rd_be32 (raw + dim_off + 4);
		n++;
	}

	*ret_tex = tex;
	return n;
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
		fprintf (stdlog, "%s%sEXTRACT TXD:%s (%u textures) -> %s/\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", arg, n_tex, dest);

	enumError err = ERR_OK;
	uint n_png = 0, n_raw = 0;
	if (!testmode)
	{
		for (uint i = 0; i < n_tex; i++)
		{
			const txd_tex_t *t = tex + i;
			const u32 data_off = t->data_off;
			// Only used for the raw-carve fallback path below, where an
			// approximate byte range is good enough (see the "verified"
			// bound's own comment for why the gap to the next record is
			// NOT trusted for the real decode any more).
			const u32 seg_end = i + 1 < n_tex ? tex[i + 1].data_off : data_end;
			const u32 seg_size = seg_end > data_off ? seg_end - data_off : 0;

			// A record with no real name has pixel/header noise sitting at
			// the name offset instead (or, for the rare record whose magic
			// sits too close to the start of the file, no name pointer at
			// all -- see find_txd_records()) -- accept a name only if every
			// byte up to the NUL is plain printable ASCII, otherwise fall
			// back to an index-based name rather than emitting garbage
			// filenames. t->name, when non-NULL, points into 'raw' with at
			// least TXD_REC_DATA_OFF-0x08 bytes guaranteed available; cap
			// the scan there so a name with no NUL in range can't run past
			// the buffer.
			const uint name_max = TXD_REC_DATA_OFF - 0x08;
			bool name_ok = t->name && *t->name != 0;
			for (uint k = 0; name_ok && k < name_max && t->name[k]; k++)
				if ((u8)t->name[k] < 0x20 || (u8)t->name[k] > 0x7e)
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
				if (*p == '/' || *p == '\\' || (u8)*p < 0x20)
					*p = '_';

			// Decode just the base image. A per-record mip-level-count byte
			// was tried (rec_off+0x7c) but reads as clear pixel-data noise
			// on most real records (170, 255, 0x55/0xAA-style fill, ...),
			// so it isn't trustworthy -- and reconciling the base size
			// against the gap to the next record turned out to be actively
			// wrong for large single-texture files (the gap comes up
			// exactly 0xA0 bytes short there, i.e. the next record's own
			// pre-magic header isn't a fixed size either). The only bound
			// that held across all 757 real records on disc is simply
			// "the declared base image fits in the file" -- so that, plus
			// a sane width/height/format, is what gates the PNG path here.
			uint base_size = 0;
			const bool verified = t->width && t->height
				&& CalcImageGeometry (t->format, t->width, t->height, 0, 0, 0, 0, &base_size)
				&& base_size && data_off + base_size <= raw_size;

			char path[PATH_MAX];
			bool wrote_png = false;
			if (verified)
			{
				Image_t img;
				InitializeIMG (&img);
				img.data = CALLOC (1, base_size + 64);
				memcpy (img.data, raw + data_off, base_size);
				img.data_alloced = true;
				img.data_size = base_size;
				img.width = t->width;
				img.height = t->height;
				img.iform = img.info_iform = (image_format_t)t->format;
				img.info_fform = FF_PNG;
				img.info_n_image = 1;
				img.endian = &be_func;
				CalcImageGeometry (
					img.iform, img.width, img.height, &img.xwidth, &img.xheight, 0, 0, 0);

				snprintf (path, sizeof (path), "%s/%s.png", dest, name);
				if (!ConvertIMG (&img, false, 0, IMG_X_RGB, PAL_INVALID)
					&& !SavePNG (&img, false, 0, path, 0, 0, true, 0))
				{
					n_png++;
					wrote_png = true;
				}
				ResetIMG (&img);
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
			fprintf (stdlog,
				"  %u texture(s) decoded to PNG, %u carved out raw (unrecovered size)\n", n_png,
				n_raw);
	}

	FREE (tex);
	FREE (raw);
	(void)depth;
	return err;
}
