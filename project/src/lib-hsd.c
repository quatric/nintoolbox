
/***************************************************************************
 *                                                                         *
 *   HAL Laboratory "sysdolphin" (HSD) .dat archive support                 *
 *                                                                         *
 *   The container/relocation layout implemented here is ported from        *
 *   Ploaj/HSDLib (https://github.com/Ploaj/HSDLib), MIT licensed:          *
 *	HSDRaw/HSDRawFile.cs        -- header + relocation/root parsing     *
 *	HSDRaw/Common/HSD_TOBJ.cs   -- HSD_Image / HSD_Tlut field layout    *
 *   cross-checked against real retail bytes (see comments below).          *
 *                                                                         *
 ***************************************************************************/

#include "lib-hsd.h"
#include "lib-image.h"
#include "lib-model-dae.h"

#include <stdlib.h>
#include <string.h>
#include <math.h>

//
///////////////////////////////////////////////////////////////////////////////
///////////////			    container			///////////////
///////////////////////////////////////////////////////////////////////////////

static bool check_hsd_header (const u8 *data, uint size, uint *ret_reloc_off, uint *ret_n_reloc,
	uint *ret_n_root, uint *ret_n_ref)
{
	if (!data || size < 0x40)
		return false;

	// HSDRawFile.cs Open(): fsize, relocOffset+0x20, relocCount, rootCount, refCount
	const u32 fsize = be32 (data);
	const u32 reloc_off = be32 (data + 0x04) + HSD_DATA_BASE;
	const u32 n_reloc = be32 (data + 0x08);
	const u32 n_root = be32 (data + 0x0c);
	const u32 n_ref = be32 (data + 0x10);

	if (fsize != size)
		return false;
	if (reloc_off < HSD_DATA_BASE || reloc_off > size)
		return false;
	if (n_root < 1 || n_root > 0x10000 || n_ref > 0x10000)
		return false;

	// relocation table + the (offset,name offset) pairs must fit
	const u64 need = (u64)reloc_off + 4 * (u64)n_reloc + 8 * ((u64)n_root + n_ref);
	if (need > size)
		return false;

	// The version tag is 4 printable ASCII chars, e.g. "001B" -- but retail
	// files do ship it all-zero (Melee's MnSlChr.usd), so only reject bytes
	// that are neither NUL nor printable.
	for (uint i = 0x14; i < 0x18; i++)
		if (data[i] && (data[i] < 0x20 || data[i] > 0x7e))
			return false;

	if (ret_reloc_off)
		*ret_reloc_off = reloc_off;
	if (ret_n_reloc)
		*ret_n_reloc = n_reloc;
	if (ret_n_root)
		*ret_n_root = n_root;
	if (ret_n_ref)
		*ret_n_ref = n_ref;
	return true;
}

///////////////////////////////////////////////////////////////////////////////

bool IsHSD (const u8 *data, uint size)
{
	return check_hsd_header (data, size, 0, 0, 0, 0);
}

///////////////////////////////////////////////////////////////////////////////

static int cmp_u32 (const void *a, const void *b)
{
	const u32 va = *(const u32 *)a, vb = *(const u32 *)b;
	return va < vb ? -1 : va > vb;
}

bool ScanHSD (hsd_t *hsd, const u8 *data, uint size)
{
	DASSERT (hsd);
	memset (hsd, 0, sizeof (*hsd));

	uint reloc_off, n_reloc, n_root, n_ref;
	if (!check_hsd_header (data, size, &reloc_off, &n_reloc, &n_root, &n_ref))
		return false;

	hsd->data = data;
	hsd->size = size;
	hsd->fsize = be32 (data);
	hsd->reloc_off = reloc_off;
	hsd->n_reloc = n_reloc;
	hsd->n_root = n_root;
	hsd->n_ref = n_ref;
	memcpy (hsd->version, data + 0x14, 4);
	hsd->version[4] = 0;

	if (!n_reloc)
		return true;

	hsd->rel_src = CALLOC (n_reloc, sizeof (*hsd->rel_src));
	hsd->rel_dest = CALLOC (n_reloc, sizeof (*hsd->rel_dest));
	hsd->target = CALLOC (n_reloc, sizeof (*hsd->target));

	// Each relocation entry names a location holding a 0x20-relative pointer;
	// both the location and the value it holds get the data base added.
	for (uint i = 0; i < n_reloc; i++)
	{
		const s32 loc = (s32)be32 (data + reloc_off + 4 * i) + HSD_DATA_BASE;
		if (loc < HSD_DATA_BASE || (u32)loc + 4 > reloc_off)
			continue;
		const s32 dest = (s32)be32 (data + loc) + HSD_DATA_BASE;
		if (dest < HSD_DATA_BASE || (u32)dest >= reloc_off)
			continue; // includes HSDLib's "alternate null pointer" case

		hsd->rel_src[hsd->n_rel] = loc;
		hsd->rel_dest[hsd->n_rel] = dest;
		hsd->target[hsd->n_rel] = dest;
		hsd->n_rel++;
	}

	// sorted unique target list -> lets us bound a pointed-to buffer by the
	// start of the next pointed-to object (buffers are never overlapped)
	qsort (hsd->target, hsd->n_rel, sizeof (*hsd->target), cmp_u32);
	uint n = 0;
	for (uint i = 0; i < hsd->n_rel; i++)
		if (!n || hsd->target[i] != hsd->target[n - 1])
			hsd->target[n++] = hsd->target[i];
	hsd->n_target = n;

	// Build open-addressing hash table for O(1) get_ptr() lookups.
	// Capacity = next power-of-two >= 2*n_rel.
	if (hsd->n_rel)
	{
		uint cap = 1;
		while (cap < 2 * hsd->n_rel)
			cap <<= 1;
		hsd->ptr_cap = cap;
		hsd->ptr_keys = CALLOC (cap, sizeof (*hsd->ptr_keys));
		hsd->ptr_vals = CALLOC (cap, sizeof (*hsd->ptr_vals));
		for (uint i = 0; i < cap; i++)
			hsd->ptr_keys[i] = UINT32_MAX;
		for (uint i = 0; i < hsd->n_rel; i++)
		{
			const u32 key = hsd->rel_src[i];
			const u32 val = hsd->rel_dest[i];
			uint idx = (key >> 2) & (cap - 1); // hash by shifting away low bits
			while (hsd->ptr_keys[idx] != UINT32_MAX)
				idx = (idx + 1) & (cap - 1);
			hsd->ptr_keys[idx] = key;
			hsd->ptr_vals[idx] = val;
		}
	}

	return true;
}

///////////////////////////////////////////////////////////////////////////////

void ResetHSD (hsd_t *hsd)
{
	if (hsd)
	{
		FREE (hsd->rel_src);
		FREE (hsd->rel_dest);
		FREE (hsd->target);
		FREE (hsd->ptr_keys);
		FREE (hsd->ptr_vals);
		memset (hsd, 0, sizeof (*hsd));
	}
}

///////////////////////////////////////////////////////////////////////////////

// pointer stored at the absolute location 'loc', or 0 if that location is not
// relocated (== a null pointer)
static u32 get_ptr (const hsd_t *hsd, u32 loc)
{
	if (!hsd->ptr_keys)
	{
		// Fallback for files with no relocations
		for (uint i = 0; i < hsd->n_rel; i++)
			if (hsd->rel_src[i] == loc)
				return hsd->rel_dest[i];
		return 0;
	}
	const uint cap = hsd->ptr_cap;
	uint idx = (loc >> 2) & (cap - 1);
	for (;;)
	{
		const u32 key = hsd->ptr_keys[idx];
		if (key == UINT32_MAX)
			return 0;
		if (key == loc)
			return hsd->ptr_vals[idx];
		idx = (idx + 1) & (cap - 1);
	}
}

// size available at pointed-to offset 'off': up to the next pointed-to
// object, or up to the relocation table if it is the last one
static u32 get_blob_size (const hsd_t *hsd, u32 off)
{
	uint lo = 0, hi = hsd->n_target;
	while (lo < hi)
	{
		const uint mid = (lo + hi) / 2;
		if (hsd->target[mid] <= off)
			lo = mid + 1;
		else
			hi = mid;
	}
	const u32 end = lo < hsd->n_target ? hsd->target[lo] : hsd->reloc_off;
	return end > off ? end - off : 0;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			    textures			///////////////
///////////////////////////////////////////////////////////////////////////////
//
// HSD_Image (HSD_TOBJ.cs class HSD_Image):
//	+0x00 ptr  pixel data
//	+0x04 u16  width
//	+0x06 u16  height
//	+0x08 u32  GXTexFmt
//
// The GX texture format numbering is identical to this project's own
// image_format_t (IMG_I4=0 .. IMG_CMPR=0x0e), so the raw value is used as-is
// and the existing GameCube/Wii pixel decoders in lib-image1.c do the work.
//
// Melee stores the format as a plain big-endian int32 at +0x08.  The Wii
// channel "TV no Tomo" uses a compacted variant of the same struct that packs
// the format into the *high* byte at +0x08 with a mipmap-level count in the
// next byte (verified on every .dat of the TV no Tomo channel: the low three
// bytes are always 0x01 0x00 0x00 there while the high byte is a valid GX
// format that exactly predicts the pixel buffer's real size).  Both spellings
// are accepted; the size check below rejects a wrong guess.
//
// HSD_Tlut (HSD_TOBJ.cs class HSD_Tlut):
//	+0x00 ptr  palette data
//	+0x04 u32  GXTlutFmt (0=IA8, 1=RGB565, 2=RGB5A3, == palette_format_t)
//	+0x08 u32  GXTlut
//	+0x0C u16  color count
//
///////////////////////////////////////////////////////////////////////////////

#define HSD_COPY_SLACK 64 // zero padding, see ExportHSDTextures()

typedef struct hsd_tex_t
{
	u32 off; // offset of the HSD_Image struct
	u32 data_off; // offset of the pixel data
	uint width;
	uint height;
	image_format_t iform;
	uint img_size; // decoded-from size in bytes

	u32 pal_off; // 0 or offset of the palette data
	palette_format_t pform;
	uint n_pal;
} hsd_tex_t;

//-----------------------------------------------------------------------------

static bool is_gx_format (u32 fmt)
{
	switch (fmt)
	{
		case IMG_I4:
		case IMG_I8:
		case IMG_IA4:
		case IMG_IA8:
		case IMG_RGB565:
		case IMG_RGB5A3:
		case IMG_RGBA32:
		case IMG_C4:
		case IMG_C8:
		case IMG_C14X2:
		case IMG_CMPR:
			return true;
		default:
			return false;
	}
}

static uint gx_image_size (uint width, uint height, image_format_t iform)
{
	uint size = 0;
	CalcImageGeometry (iform, width, height, 0, 0, 0, 0, &size);
	return size;
}

//-----------------------------------------------------------------------------

#define HSD_IMAGE_SIZE 0x0c // packed HSD_Image, compacted variant
#define HSD_TLUT_SIZE 0x10 // packed HSD_Tlut

// already identified HSD_Image at exactly this offset, or 0
static const hsd_tex_t *find_tex_at (const hsd_tex_t *tex, uint n_tex, u32 off)
{
	for (uint i = 0; i < n_tex; i++)
		if (tex[i].off == off)
			return tex + i;
	return 0;
}

// Is 'off' a relocated pointer location holding a usable HSD_Tlut with at
// least 'min_pal' colors? Optionally returns the palette it names.
static bool is_hsd_tlut (
	const hsd_t *hsd, u32 off, uint min_pal, u32 *ret_pal_off, u32 *ret_pform, uint *ret_n_pal)
{
	if (off + 0x0e > hsd->reloc_off)
		return false;

	const u32 pal_off = get_ptr (hsd, off);
	if (!pal_off)
		return false;

	const u8 *s = hsd->data + off;
	const u32 pform = be32 (s + 0x04);
	const uint n_pal = be16 (s + 0x0c);
	if (pform > PAL_RGB5A3 || n_pal < min_pal || n_pal > 0x4000)
		return false;
	if (pal_off + 2 * n_pal > hsd->reloc_off)
		return false;
	if (get_blob_size (hsd, pal_off) < 2 * n_pal)
		return false;

	if (ret_pal_off)
		*ret_pal_off = pal_off;
	if (ret_pform)
		*ret_pform = pform;
	if (ret_n_pal)
		*ret_n_pal = n_pal;
	return true;
}

//-----------------------------------------------------------------------------

// Scan for HSD_Image structs. Every candidate must be a relocated pointer
// location whose target is large enough to hold exactly the pixel data the
// width/height/format triple describes -- the graph carries no type tags, so
// this size agreement (plus the format enum check) is the identification.
static uint find_textures (const hsd_t *hsd, hsd_tex_t **ret_tex)
{
	*ret_tex = 0;
	if (!hsd->n_rel)
		return 0;

	hsd_tex_t *tex = CALLOC (hsd->n_rel, sizeof (*tex));
	uint n_tex = 0;

	// pass 1: images
	for (uint i = 0; i < hsd->n_rel; i++)
	{
		const u32 off = hsd->rel_src[i];
		if (off + 0x0c > hsd->reloc_off)
			continue;

		const u8 *s = hsd->data + off;
		const uint width = be16 (s + 0x04);
		const uint height = be16 (s + 0x06);
		if (!width || !height || width > 4096 || height > 4096)
			continue;

		const u32 fmt_word = be32 (s + 0x08);
		const u32 fmt = fmt_word < 0x100 ? fmt_word : fmt_word >> 24;
		if (!is_gx_format (fmt))
			continue;

		const u32 data_off = hsd->rel_dest[i];
		const uint img_size = gx_image_size (width, height, fmt);
		if (!img_size)
			continue;
		const u32 blob = get_blob_size (hsd, data_off);
		// mipmaps may follow the main level, so accept anything at least as
		// large, but require the main level to be present in full
		if (blob < img_size || data_off + img_size > hsd->reloc_off)
			continue;

		hsd_tex_t *t = tex + n_tex++;
		t->off = off;
		t->data_off = data_off;
		t->width = width;
		t->height = height;
		t->iform = fmt;
		t->img_size = img_size;
		t->pform = PAL_INVALID;
	}

	// pass 2: palettes for the CI formats.
	for (uint i = 0; i < n_tex; i++)
	{
		hsd_tex_t *t = tex + i;
		uint min_pal, max_pal;
		switch (t->iform)
		{
			case IMG_C4:
				min_pal = max_pal = 1 << 4;
				break;
			case IMG_C8:
				min_pal = max_pal = 1 << 8;
				break;
			case IMG_C14X2:
				min_pal = 1;
				max_pal = 1 << 14;
				break; // sparse
			default:
				continue;
		}

		// (a) the exact route: a HSD_TOBJ references its image at +0x4C and
		// its tlut at +0x50 (HSD_TOBJ.cs ImageData/TlutData), so whatever
		// points at this image has the matching tlut in the very next word.
		for (uint j = 0; j < hsd->n_rel && !t->pal_off; j++)
		{
			if (hsd->rel_dest[j] != t->off)
				continue;
			const u32 loc = hsd->rel_src[j] + 4;
			for (uint k = 0; k < hsd->n_rel; k++)
			{
				if (hsd->rel_src[k] != loc)
					continue;
				const u32 off = hsd->rel_dest[k];
				if (off + 0x0e > hsd->reloc_off)
					break;
				const u8 *s = hsd->data + off;
				const u32 pform = be32 (s + 0x04);
				const uint n_pal = be16 (s + 0x0c);
				if (pform > PAL_RGB5A3 || !n_pal || n_pal > 0x4000)
					break;
				const u32 pal_off = get_ptr (hsd, off);
				if (!pal_off || pal_off + 2 * n_pal > hsd->reloc_off)
					break;
				t->pal_off = pal_off;
				t->pform = pform;
				t->n_pal = n_pal < max_pal ? n_pal : max_pal;
				break;
			}
		}
		if (t->pal_off)
			continue;

		// (b) fallback for the compacted variant, where nothing points at the
		// image struct at all (TV no Tomo): the HSD_Image structs of one object
		// form a packed array that is directly followed by the packed array of
		// their HSD_Tlut structs, image N pairing with tlut N (verified on
		// MiiButton.dat: 8 images at 0x93b4+0x0c*N -> 8 tluts at 0x9484+0x10*N
		// naming 8 distinct 256-color palettes at 0x8260+0x200*N).  So find this
		// image's index inside its own run, then step that far into the tlut
		// run.  Taking simply the nearest following tlut -- which is what this
		// used to do -- hands every image of such a run the very same palette.
		uint idx = 0;
		for (u32 p = t->off; p >= HSD_DATA_BASE + HSD_IMAGE_SIZE; p -= HSD_IMAGE_SIZE)
		{
			const hsd_tex_t *prev = find_tex_at (tex, n_tex, p - HSD_IMAGE_SIZE);
			if (!prev || prev->iform != t->iform)
				break;
			idx++;
		}
		u32 run_end = t->off + HSD_IMAGE_SIZE;
		for (;;)
		{
			const hsd_tex_t *next = find_tex_at (tex, n_tex, run_end);
			if (!next || next->iform != t->iform)
				break;
			run_end += HSD_IMAGE_SIZE;
		}

		// the first plausible tlut behind the whole image run starts the run
		u32 first = 0;
		for (uint j = 0; j < hsd->n_rel; j++)
		{
			const u32 off = hsd->rel_src[j];
			if (off < run_end || (first && off >= first))
				continue;
			if (is_hsd_tlut (hsd, off, min_pal, 0, 0, 0))
				first = off;
		}
		if (!first)
			continue;

		u32 pal_off = 0, pform = 0;
		uint n_pal = 0;
		if (!idx
			|| !is_hsd_tlut (hsd, first + HSD_TLUT_SIZE * idx, min_pal, &pal_off, &pform, &n_pal))
		{
			// no run behind this one (or a shorter one): stay with the old,
			// single-palette behaviour instead of inventing a pairing
			is_hsd_tlut (hsd, first, min_pal, &pal_off, &pform, &n_pal);
		}

		// lib-image1.c TransformPalette() sizes its working palette by the
		// index width of the target IMG_X_PAL* format, so more entries than
		// the indices can address must not be handed over; sysdolphin does
		// store oversized TLUTs (TV no Tomo pairs 256-color tables with CI4
		// images), and only the addressable prefix is ever sampled.
		t->pal_off = pal_off;
		t->pform = pform;
		t->n_pal = n_pal < max_pal ? n_pal : max_pal;
	}

	// pass 3: de-duplicate. Several HSD_Image structs legitimately share one
	// pixel buffer (Melee's character-select portraits are referenced from
	// more than one menu object), and the structural identification can also
	// latch onto a second, unreferenced struct describing the same buffer.
	// Keep one entry per (buffer,geometry,format), preferring the one that a
	// HSD_TOBJ actually points at -- that is the one with the right palette.
	uint n_keep = 0;
	for (uint i = 0; i < n_tex; i++)
	{
		bool dup = false;
		for (uint j = 0; j < n_keep; j++)
		{
			hsd_tex_t *k = tex + j;
			if (k->data_off != tex[i].data_off || k->width != tex[i].width
				|| k->height != tex[i].height || k->iform != tex[i].iform)
				continue;
			if (!k->pal_off && tex[i].pal_off)
				*k = tex[i]; // the better identified one wins
			dup = true;
			break;
		}
		if (!dup)
			tex[n_keep++] = tex[i];
	}

	*ret_tex = tex;
	return n_keep;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			     export			///////////////
///////////////////////////////////////////////////////////////////////////////

int ExportHSDTextures (const hsd_t *hsd, ccp dest_dir, ccp basename)
{
	if (!hsd || !hsd->data || !dest_dir)
		return -1;

	hsd_tex_t *tex = 0;
	const uint n_tex = find_textures (hsd, &tex);
	if (!n_tex)
	{
		FREE (tex);
		return 0;
	}

	char dir[PATH_MAX];
	StringCopyS (dir, sizeof (dir), dest_dir);
	CreatePath (dir, true);

	char base[80];
	StringCopyS (base, sizeof (base), basename && *basename ? basename : "hsd");
	char *dot = strrchr (base, '.');
	if (dot && dot > base && !strcasecmp (dot, ".dat"))
		*dot = 0;

	int written = 0;
	for (uint i = 0; i < n_tex; i++)
	{
		const hsd_tex_t *t = tex + i;

		Image_t img;
		InitializeIMG (&img);
		// The native decoders in lib-image1.c walk whole blocks and the
		// palette helpers peek one entry past the used range (they normally
		// operate on a pointer into a much larger loaded file), so both
		// buffers get zero padding instead of an exact-size copy.
		img.data = CALLOC (1, t->img_size + HSD_COPY_SLACK);
		memcpy (img.data, hsd->data + t->data_off, t->img_size);
		img.data_alloced = true;
		img.data_size = t->img_size;
		img.width = t->width;
		img.height = t->height;
		img.iform = img.info_iform = t->iform;
		img.info_fform = FF_PNG;
		img.info_n_image = 1;
		img.endian = &be_func;
		CalcImageGeometry (img.iform, img.width, img.height, &img.xwidth, &img.xheight, 0, 0, 0);

		if (t->pal_off)
		{
			img.pal = CALLOC (1, 4 * t->n_pal + HSD_COPY_SLACK);
			memcpy (img.pal, hsd->data + t->pal_off, 2 * t->n_pal);
			img.pal_alloced = true;
			img.pal_size = 2 * t->n_pal;
			img.n_pal = t->n_pal;
			img.pform = img.info_pform = t->pform;
		}

		char path[PATH_MAX];
		snprintf (path, sizeof (path), "%s/%s.tex%03u_%ux%u_%s.png", dir, base, i, t->width,
			t->height, GetImageFormatName (t->iform, "?"));

		if (!ConvertIMG (&img, false, 0, IMG_X_RGB, PAL_INVALID)
			&& !SavePNG (&img, false, 0, path, 0, 0, true, 0))
		{
			written++;
		}
		ResetIMG (&img);
	}

	FREE (tex);
	return written;
}

///////////////////////////////////////////////////////////////////////////////

int ExportHSDTexturesFromData (const u8 *data, uint size, ccp dest_dir, ccp basename)
{
	hsd_t hsd;
	if (!ScanHSD (&hsd, data, size))
		return -1;
	const int stat = ExportHSDTextures (&hsd, dest_dir, basename);
	ResetHSD (&hsd);
	return stat;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			     model geometry		///////////////
///////////////////////////////////////////////////////////////////////////////
//
// JOBJ (0x40 bytes; HSD_JOBJ.cs):
//	+0x00 ptr  class name (unused here)
//	+0x04 u32  flags
//	+0x08 ptr  child JOBJ (first-child/next-sibling tree)
//	+0x0C ptr  next JOBJ (sibling)
//	+0x10 ptr  DOBJ list head
//	+0x14 f32  RX  +0x18 RY  +0x1C RZ  (radians)
//	+0x20 f32  SX  +0x24 SY  +0x28 SZ
//	+0x2C f32  TX  +0x30 TY  +0x34 TZ
//	+0x38 ptr  inverse world transform (unused here)
//	+0x3C ptr  ROBJ (unused here)
//
// DOBJ (0x10 bytes; HSD_DOBJ.cs): class name ptr, next DOBJ ptr, MOBJ
// (material, unused here) ptr @0x08, POBJ ptr @0x0C.
//
// POBJ (0x18 bytes; HSD_POBJ.cs): unused @0x00, next POBJ ptr @0x04,
// GX_Attribute array ptr @0x08, flags (u16) @0x0C, display-list size in
// 32-byte units (s16) @0x0E, display-list buffer ptr @0x10, and a
// flags-dependent union @0x14 (SingleBoundJOBJ / envelope weights / shape
// set -- not consumed here, see scope note below).
//
// GX_Attribute (0x18 bytes; GX/GX_Attribute.cs): name (u32, GX attribute ID),
// type (u32: 0 NONE, 1 DIRECT, 2 INDEX8, 3 INDEX16), component-count enum
// (u32, unused here -- stride/component-size already gives the real count,
// see below), component-type enum (u32: 0 U8, 1 S8, 2 U16, 3 S16, 4 F32),
// fixed-point scale (u8), per-element byte stride (s16), buffer ptr. The
// array is terminated by an entry named GX_VA_NULL (0xff).
//
// All three struct layouts, the attribute array shape, and the real GX
// display-list opcode stream were confirmed byte-for-byte against a real
// file (Super Smash Bros. Melee's TyBox.dat item-box model) before being
// trusted -- not copied from Ploaj/HSDLib (MIT licensed) on faith alone:
// root table -> "ToyBoxModel_TopN_joint" resolved correctly; its DOBJ/POBJ
// chain's GX_Attribute array (POS index8/S16, NBT index8/F32, TEX0/TEX1
// index8/S16 both sharing one buffer) predicted the real per-vertex tuple
// width (4 bytes) the display list actually uses; and the position buffer
// -- found via the relocation table, not a raw-offset guess -- decoded to
// plausible small-object coordinates.
//
// Static geometry export: JOBJ/DOBJ/POBJ skeleton, DOBJ material/texture
// binding via MOBJ+TOBJ, POBJ envelope skinning (ENVELOPE flag), shape
// animation morph targets (SHAPEANIM flag), vertex colors (CLR0+CLR1),
// texture transforms (SRT), and NBT tangent/binormal.  Cull/depth/blend
// state from MOBJ rendermode and PEDesc are not yet exported.

static inline float bef32 (const u8 *p)
{
	const u32 v = be32 (p);
	float f;
	memcpy (&f, &v, 4);
	return f;
}

//--------------------------------------------------------------------------
// HSD material/polygon/envelope/texture struct constants and on-disk layouts
// (doldecomp/melee decompilation, MIT licensed; TOBJ layout cross-checked
//  against Ploaj/HSDLib (MIT) HSD_TOBJ.cs ImageData/TlutData offsets and
//  confirmed against real retail bytes in TyBox.dat, PlMr.dat etc.)
//--------------------------------------------------------------------------

// HSD_PObjDesc flags @+0x0C (bits 12-13 = type mask)
enum
{
	HSD_POBJ_SKIN = 0x0000, // bits 12-13 == 0: rigid/single-bound
	HSD_POBJ_SHAPEANIM = 0x1000, // bits 12-13 == 1: shape animation
	HSD_POBJ_ENVELOPE = 0x2000, // bits 12-13 == 2: envelope weighted
	HSD_POBJ_TYPE_MASK = 0x3000,
	HSD_POBJ_CULLFRONT = 0x4000,
	HSD_POBJ_CULLBACK = 0x8000,
};

// HSD_MObjDesc on-disk (0x18 bytes): class_name, rendermode, texdesc, mat, renderdesc, pedesc
// HSD_Material on-disk (0x14 bytes): ambient(GXColor), diffuse(GXColor), specular(GXColor),
// alpha(f32), shininess(f32) HSD_PEDesc on-disk (0x0C bytes): flags, ref0, ref1, dst_alpha, type,
// src_factor, dst_factor, logic_op, z_comp, alpha_comp0, alpha_op, alpha_comp1 HSD_SList on-disk
// (0x08 bytes): next(ptr), data(ptr) HSD_EnvelopeDesc on-disk (0x08 bytes): joint(ptr), weight(f32)

// HSD_TObj on-disk / runtime form (0x5C+ bytes):
// Based on doldecomp tobj.h and tockdom wiki:
//	+0x00 ptr  class_name
//	+0x04 ptr  next TOBJ (chain; null = last)
//	+0x08 u32  GXTexMapID (0=TEXMAP0, 1=TEXMAP1, ...)
//	+0x0C u32  coord (GX_TexGenSrc: 4=TEX0, 5=TEX1, ...)
//	+0x10 f32  Rotation X (radians)
//	+0x14 f32  Rotation Y (radians)
//	+0x18 f32  Rotation Z (radians)
//	+0x1C f32  Scale X (U)
//	+0x20 f32  Scale Y (V)
//	+0x24 f32  Scale Z (W, typically 1.0)
//	+0x28 f32  Translate X (U)
//	+0x2C f32  Translate Y (V)
//	+0x30 f32  Translate Z (W, typically 0.0)
//	+0x34 u32  wrap_s (0=clamp, 1=repeat, 2=mirror)
//	+0x38 u32  wrap_t (0=clamp, 1=repeat, 2=mirror)
//	+0x3C u8   repeat_s, u8 repeat_t, u16 pad
//	+0x40 u32  blend_flags
//	+0x44 f32  blending
//	+0x48 u32  mag_filter (GXTexFilter)
//	+0x4C ptr  ImageData (HSD_Image*)
//	+0x50 ptr  TlutData (HSD_Tlut*)

// HSD_ShapeSetDesc on-disk (0x10 bytes; doldecomp MIT):
//	+0x00 ptr  next shape set
//	+0x04 ptr  shapes (HSD_ShapeDesc*)
//	+0x08 ptr  current_shape
//	+0x0C u32  num_shapes

// HSD_ShapeDesc on-disk (0x10 bytes):
//	+0x00 ptr  next shape
//	+0x04 ptr  display_list (position deltas)
//	+0x08 ptr  vertex descriptor (GX_Attribute*)
//	+0x0C u16  flags
//	+0x0E u16  n_display (DL word count)

#define HSD_MOBJ_DESC_SIZE 0x18
#define HSD_MATERIAL_SIZE 0x14
#define HSD_PEDESC_SIZE 0x0C
#define HSD_SLIST_SIZE 0x08
#define HSD_ENVELOPE_DESC_SIZE 0x08
#define HSD_TOBJ_IMAGE_OFF 0x4C
#define HSD_TOBJ_TLUT_OFF 0x50
#define HSD_TOBJ_COORD_OFF 0x0C
#define HSD_TOBJ_NEXT_OFF 0x04
#define HSD_TOBJ_WRAP_S_OFF 0x34
#define HSD_TOBJ_WRAP_T_OFF 0x38
#define HSD_TOBJ_ROT_X_OFF 0x10
#define HSD_TOBJ_SCALE_X_OFF 0x1C
#define HSD_TOBJ_TRANS_X_OFF 0x28
#define HSD_SHAPESET_SIZE 0x10
#define HSD_SHAPE_DESC_SIZE 0x10

// Temporary storage for decoded shape display lists (morph targets).
// Accumulated during POBJ processing, converted to position deltas
// after the base mesh is finalized.
typedef struct hsd_vtx_t hsd_vtx_t;
typedef struct
{
	hsd_vtx_t *verts;
	uint n_verts;
} hsd_shape_slot_t;
#define HSD_FTDATA_SKELETON_OFF 0x5C

enum
{
	HSD_GX_VA_POS = 9,
	HSD_GX_VA_NRM = 10,
	HSD_GX_VA_CLR0 = 11,
	HSD_GX_VA_CLR1 = 12,
	HSD_GX_VA_TEX0 = 13,
	HSD_GX_VA_TEX1 = 14,
	HSD_GX_VA_TEX2 = 15,
	HSD_GX_VA_TEX3 = 16,
	HSD_GX_VA_TEX4 = 17,
	HSD_GX_VA_TEX5 = 18,
	HSD_GX_VA_TEX6 = 19,
	HSD_GX_VA_TEX7 = 20,
	HSD_GX_VA_NBT = 25,
	HSD_GX_VA_NULL = 0xff,
};
enum
{
	HSD_GX_NONE = 0,
	HSD_GX_DIRECT = 1,
	HSD_GX_INDEX8 = 2,
	HSD_GX_INDEX16 = 3
};
enum
{
	HSD_GX_U8 = 0,
	HSD_GX_S8 = 1,
	HSD_GX_U16 = 2,
	HSD_GX_S16 = 3,
	HSD_GX_F32 = 4
};

typedef struct hsd_attr_t
{
	u32 name, type, ctype;
	u8 scale;
	int stride; // bytes per element (buffer stride, or DIRECT width)
	u32 buf_off; // absolute; 0 if unresolved/not applicable
} hsd_attr_t;

#define HSD_MAX_ATTR 16

// Reads the GX_Attribute array at 'off' (relative to the JOBJ graph, i.e.
// already resolved through get_ptr()) until the GX_VA_NULL terminator.
static uint read_gx_attrs (const hsd_t *hsd, u32 off, hsd_attr_t *out)
{
	uint n = 0;
	while (n < HSD_MAX_ATTR && off && (u64)off + 0x18 <= hsd->reloc_off)
	{
		const u8 *s = hsd->data + off;
		const u32 name = be32 (s);
		if (name == HSD_GX_VA_NULL)
			break;
		out[n].name = name;
		out[n].type = be32 (s + 4);
		out[n].ctype = be32 (s + 0xC);
		out[n].scale = s[0x10];
		out[n].stride = (s16)be16 (s + 0x12);
		out[n].buf_off = get_ptr (hsd, off + 0x14);
		n++;
		off += 0x18;
	}
	return n;
}

static uint hsd_ctype_size (u32 ctype)
{
	switch (ctype)
	{
		case HSD_GX_U16:
		case HSD_GX_S16:
			return 2;
		case HSD_GX_F32:
			return 4;
		default:
			return 1;
	}
}

// Compute byte size of one element for a GX attribute when stride==0
// (DIRECT mode). Colour attributes have special packed formats.
static uint hsd_gx_comp_size (const hsd_attr_t *a)
{
	if (a->name == HSD_GX_VA_CLR0 || a->name == HSD_GX_VA_CLR1)
	{
		switch (a->ctype)
		{
			case 0:
				return 2; // RGB565
			case 1:
			case 2:
				return 4; // RGB8 / RGBX8
			case 3:
				return 2; // RGBA4
			case 4:
				return 3; // RGBA6 (packed 3 bytes)
			case 5:
				return 4; // RGBA8
			default:
				return 4;
		}
	}
	// NBT packs 3×3 = 9 floats = 36 bytes (but stored as 3 normals × comp_size).
	const uint esz = hsd_ctype_size (a->ctype);
	const uint ncomp
		= a->stride > 0 ? a->stride / (esz ? esz : 1) : (a->name == HSD_GX_VA_NBT ? 9 : 3);
	return ncomp * esz;
}

// Decodes one attribute's element at 'src' ('stride' bytes) into up to
// 'max_comp' floats. Integer types are fixed-point (/2^scale, matching
// GX_Attribute.GetDecodedDataAt()); colour attributes (CLR0/CLR1) always
// decode to 4 RGBA floats regardless of nominal component count, matching
// GetColorAt() -- 'ctype' there reuses GXCompTypeClr's own numbering
// (0 RGB565, 1 RGB8, 2 RGBX8, 3 RGBA4, 4 RGBA6, 5 RGBA8).
static uint decode_gx_elem (const hsd_attr_t *a, const u8 *src, float *out, uint max_comp)
{
	if (a->name == HSD_GX_VA_CLR0 || a->name == HSD_GX_VA_CLR1)
	{
		out[0] = out[1] = out[2] = out[3] = 1;
		switch (a->ctype)
		{
			case 0:
			{
				const u32 v = be16 (src);
				out[0] = ((v & 0x1F) << 3) / 255.0f;
				out[1] = (((v >> 5) & 0x3F) << 2) / 255.0f;
				out[2] = (((v >> 11) & 0x1F) << 3) / 255.0f;
				break;
			}
			case 1:
			case 2:
				out[0] = src[0] / 255.0f;
				out[1] = src[1] / 255.0f;
				out[2] = src[2] / 255.0f;
				out[3] = a->ctype == 2 ? src[3] / 255.0f : 1;
				break;
			case 3:
				out[0] = (src[0] >> 4) / 15.0f;
				out[1] = (src[0] & 0xF) / 15.0f;
				out[2] = (src[1] >> 4) / 15.0f;
				out[3] = (src[1] & 0xF) / 15.0f;
				break;
			case 4:
			{
				const u32 v = (src[0] << 16) | (src[1] << 8) | src[2];
				out[0] = ((v >> 18) & 0x3F) / 63.0f;
				out[1] = ((v >> 12) & 0x3F) / 63.0f;
				out[2] = ((v >> 6) & 0x3F) / 63.0f;
				out[3] = (v & 0x3F) / 63.0f;
				break;
			}
			case 5:
				out[0] = src[0] / 255.0f;
				out[1] = src[1] / 255.0f;
				out[2] = src[2] / 255.0f;
				out[3] = src[3] / 255.0f;
				break;
			default:
				break;
		}
		return 4;
	}

	const uint esz = hsd_ctype_size (a->ctype);
	uint n = a->stride > 0 ? (uint)a->stride / (esz ? esz : 1) : 0;
	if (n > max_comp)
		n = max_comp;
	const float fscale = a->ctype == HSD_GX_F32 ? 1.0f : 1.0f / (float)(1u << a->scale);
	for (uint i = 0; i < n; i++)
	{
		if (a->ctype == HSD_GX_F32)
		{
			const u32 b = be32 (src + 4 * i);
			memcpy (out + i, &b, 4);
			continue;
		}
		s32 raw;
		switch (a->ctype)
		{
			case HSD_GX_U8:
				raw = src[i];
				break;
			case HSD_GX_S8:
				raw = (s8)src[i];
				break;
			case HSD_GX_U16:
				raw = be16 (src + 2 * i);
				break;
			case HSD_GX_S16:
				raw = (s16)be16 (src + 2 * i);
				break;
			default:
				raw = src[i];
				break;
		}
		out[i] = raw * fscale;
	}
	return n;
}

typedef struct hsd_vtx_t
{
	float pos[3];
	float nrm[3];
	bool has_nrm;
	float tan[3];
	bool has_tan;
	float uv[2];
	bool has_uv;
	float clr[4];
	bool has_clr;
	float clr1[4];
	bool has_clr1;
	float extra_uv[7][2];
	bool has_extra_uv[7];
} hsd_vtx_t;

// Reads one vertex's worth of attribute data from the display-list cursor
// '*dl' and advances it. DIRECT attributes are consumed inline; INDEX8/
// INDEX16 read an index and fetch the element from the attribute's own
// buffer. Returns false (leaving '*dl' unspecified) on any out-of-bounds
// read, which the caller treats as "stop decoding this display list".
static bool hsd_fetch_vertex (const hsd_t *hsd, const hsd_attr_t *attrs, uint n_attr, const u8 **dl,
	const u8 *dl_end, hsd_vtx_t *v)
{
	memset (v, 0, sizeof (*v));
	for (uint a = 0; a < n_attr; a++)
	{
		const hsd_attr_t *at = attrs + a;
		if (at->type == HSD_GX_NONE)
			continue;

		const u8 *src;
		if (at->type == HSD_GX_DIRECT)
		{
			// stride==0 for DIRECT means "1 byte inline" in HSD display lists
			// (matrix-index attributes: PNMTXIDX, TEX0MTXIDX, TEX1MTXIDX).
			const int effective_stride = at->stride > 0 ? at->stride : 1;
			if (effective_stride < 0 || *dl + effective_stride > dl_end)
				return false;
			src = *dl;
			*dl += effective_stride;
		}
		else
		{
			uint idx;
			if (at->type == HSD_GX_INDEX8)
			{
				if (*dl + 1 > dl_end)
					return false;
				idx = **dl;
				*dl += 1;
			}
			else if (at->type == HSD_GX_INDEX16)
			{
				if (*dl + 2 > dl_end)
					return false;
				idx = be16 (*dl);
				*dl += 2;
			}
			else
				continue;

			if (!at->buf_off || at->stride <= 0)
				continue;
			src = hsd->data + at->buf_off + (u64)idx * (uint)at->stride;
			if (src < hsd->data || (u64)(src - hsd->data) + (uint)at->stride > hsd->reloc_off)
				return false;
		}

		float comp[9];
		decode_gx_elem (at, src, comp, 9);
		switch (at->name)
		{
			case HSD_GX_VA_POS:
				memcpy (v->pos, comp, 12);
				break;
			case HSD_GX_VA_NRM:
				memcpy (v->nrm, comp, 12);
				v->has_nrm = true;
				break;
			case HSD_GX_VA_NBT:
				memcpy (v->nrm, comp, 12);
				memcpy (v->tan, comp + 6, 12);
				v->has_nrm = true;
				v->has_tan = true;
				break;
			case HSD_GX_VA_TEX0:
				memcpy (v->uv, comp, 8);
				v->has_uv = true;
				break;
			case HSD_GX_VA_TEX1:
			case HSD_GX_VA_TEX2:
			case HSD_GX_VA_TEX3:
			case HSD_GX_VA_TEX4:
			case HSD_GX_VA_TEX5:
			case HSD_GX_VA_TEX6:
			case HSD_GX_VA_TEX7:
			{
				const uint ch = at->name - HSD_GX_VA_TEX0 - 1;
				if (ch < 7)
				{
					memcpy (v->extra_uv[ch], comp, 8);
					v->has_extra_uv[ch] = true;
				}
				break;
			}
			case HSD_GX_VA_CLR0:
				memcpy (v->clr, comp, 16);
				v->has_clr = true;
				break;
			case HSD_GX_VA_CLR1:
				memcpy (v->clr1, comp, 16);
				v->has_clr1 = true;
				break;
			default:
				break;
		}
	}
	return true;
}

// Growable triangle-corner buffer: every corner gets its own entry (no
// vertex deduplication) so building it needs no hashing, at the cost of a
// larger-than-necessary but perfectly valid glTF primitive.
static void hsd_emit_tri (
	hsd_vtx_t **tri, uint *n, uint *cap, const hsd_vtx_t *v, uint a, uint b, uint c)
{
	if (*n + 3 > *cap)
	{
		*cap = *cap ? *cap * 2 : 64;
		*tri = REALLOC (*tri, *cap * sizeof (**tri));
	}
	(*tri)[(*n)++] = v[a];
	(*tri)[(*n)++] = v[b];
	(*tri)[(*n)++] = v[c];
}

// GX primitive opcode -> triangle fan-out. kind: 0 QUADS, 2 TRIANGLES,
// 3 TRISTRIP, 4 TRIFAN (numbering local to this file, not a GX constant).
static void hsd_triangulate (
	int kind, const hsd_vtx_t *v, uint cnt, hsd_vtx_t **tri, uint *n_tri, uint *cap_tri)
{
	switch (kind)
	{
		case 2: // TRIANGLES
			for (uint i = 0; i + 3 <= cnt; i += 3)
				hsd_emit_tri (tri, n_tri, cap_tri, v, i, i + 1, i + 2);
			break;
		case 3: // TRISTRIP
			for (uint i = 0; i + 3 <= cnt; i++)
				if (i & 1)
					hsd_emit_tri (tri, n_tri, cap_tri, v, i, i + 2, i + 1);
				else
					hsd_emit_tri (tri, n_tri, cap_tri, v, i, i + 1, i + 2);
			break;
		case 4: // TRIFAN
			for (uint i = 1; i + 2 <= cnt; i++)
				hsd_emit_tri (tri, n_tri, cap_tri, v, 0, i, i + 1);
			break;
		case 0: // QUADS
			for (uint i = 0; i + 4 <= cnt; i += 4)
			{
				hsd_emit_tri (tri, n_tri, cap_tri, v, i, i + 1, i + 2);
				hsd_emit_tri (tri, n_tri, cap_tri, v, i, i + 2, i + 3);
			}
			break;
		default:
			break;
	}
}

// Walks one POBJ's raw display list, decoding every recognised draw call
// into flat triangle corners. Unrecognised opcodes (lines/points -- no
// faces to emit -- or anything else) stop decoding cleanly rather than
// desyncing the byte stream; whatever triangles were already decoded are
// still returned.
static bool hsd_decode_display_list (const hsd_t *hsd, const u8 *dl, uint dl_size,
	const hsd_attr_t *attrs, uint n_attr, hsd_vtx_t **out_tri, uint *out_n_tri)
{
	uint cap_tri = 0, n_tri = 0;
	hsd_vtx_t *tri = 0;
	const u8 *p = dl, *end = dl + dl_size;
	bool any = false;

	while (p < end)
	{
		const u8 op = *p;
		if (op == 0)
		{
			p++;
			continue;
		}
		if (op == 0x61)
		{
			if (p + 5 > end)
				break;
			p += 5;
			continue;
		}
		if (op == 0x08)
		{
			if (p + 6 > end)
				break;
			p += 6;
			continue;
		}
		if (op == 0x10)
		{
			if (p + 5 > end)
				break;
			const u64 adv = 5 + ((u64)be16 (p + 1) + 1) * 4;
			if (p + adv > end)
				break;
			p += adv;
			continue;
		}

		int kind;
		switch (op & 0xf8)
		{
			case 0x80:
				kind = 0;
				break; // QUADS
			case 0x88:
				kind = 0;
				break; // QUADS2 (same layout as QUADS)
			case 0x90:
				kind = 2;
				break; // TRIANGLES
			case 0x98:
				kind = 3;
				break; // TRISTRIP
			case 0xA0:
				kind = 4;
				break; // TRIFAN
			default: // LINES/LINESTRIP/POINTS/unknown: skip vertex data
			{
				if (p + 3 > end)
					break;
				const uint skip_cnt = be16 (p + 1);
				p += 3;
				uint skip_bytes = 0;
				for (uint a = 0; a < n_attr; a++)
					skip_bytes += attrs[a].stride ? attrs[a].stride : hsd_gx_comp_size (&attrs[a]);
				if (skip_bytes && skip_cnt <= 0xFFFF / skip_bytes)
					p += (u64)skip_cnt * skip_bytes;
				continue;
			}
		}

		if (p + 3 > end)
			break;
		const uint cnt = be16 (p + 1);
		p += 3;
		if (!cnt || cnt > 4096)
			break;

		hsd_vtx_t *verts = MALLOC (cnt * sizeof (*verts));
		bool ok = true;
		for (uint i = 0; i < cnt && ok; i++)
			ok = hsd_fetch_vertex (hsd, attrs, n_attr, &p, end, verts + i);
		if (ok)
		{
			hsd_triangulate (kind, verts, cnt, &tri, &n_tri, &cap_tri);
			any = true;
		}
		FREE (verts);
		if (!ok)
			break;
	}

done:
	*out_tri = tri;
	*out_n_tri = n_tri;
	if (!any)
		FREE (tri);
	return any;
}

//-----------------------------------------------------------------------------

typedef struct hsd_jobj_map_t
{
	u32 jobj_off; // absolute offset of the JOBJ in the data
	int joint_idx; // index into ctx->joints
} hsd_jobj_map_t;

typedef struct hsd_mat_slot_t
{
	u32 mobj_off; // absolute offset of the MOBJ struct
	uint mat_idx; // index into ctx->model.materials
} hsd_mat_slot_t;

// Texture lookup entry: maps an HSD_Image struct offset to its index in the
// texture list built by find_textures().  Used to bind TOBJ chain textures
// to material_t during model export.
typedef struct hsd_tex_lookup_t
{
	u32 data_off; // pixel data offset (== hsd_tex_t.data_off)
	uint idx; // index into ctx->tex_names[]
} hsd_tex_lookup_t;

typedef struct hsd_model_ctx_t
{
	const hsd_t *hsd;
	joint_t *joints;
	uint n_joints, cap_joints;
	mesh_t *meshes;
	uint n_meshes, cap_meshes;
	material_t *materials;
	uint n_materials, cap_materials;
	node_influence_t *node_influences;
	uint n_node_influences, cap_node_influences;
	hsd_jobj_map_t *jobj_map;
	uint n_jobj_map, cap_jobj_map;
	hsd_mat_slot_t *mat_slots;
	uint n_mat_slots, cap_mat_slots;
	// Texture binding: names[] are the texture filenames (without path/extension),
	// tex_lookup[] maps HSD_Image offsets to indices in names[].
	char **tex_names;
	uint n_tex_names;
	hsd_tex_lookup_t *tex_lookup;
	uint n_tex_lookup;
} hsd_model_ctx_t;

static int hsd_find_joint_by_offset (const hsd_model_ctx_t *ctx, u32 jobj_off)
{
	for (uint i = 0; i < ctx->n_jobj_map; i++)
		if (ctx->jobj_map[i].jobj_off == jobj_off)
			return ctx->jobj_map[i].joint_idx;
	return -1;
}

static int hsd_add_joint (hsd_model_ctx_t *ctx, u32 jobj_off, ccp name, int parent_idx, float rx,
	float ry, float rz, float sx, float sy, float sz, float tx, float ty, float tz)
{
	if (ctx->n_joints == ctx->cap_joints)
	{
		ctx->cap_joints = ctx->cap_joints ? ctx->cap_joints * 2 : 16;
		ctx->joints = REALLOC (ctx->joints, ctx->cap_joints * sizeof (*ctx->joints));
	}
	joint_t *j = ctx->joints + ctx->n_joints;
	memset (j, 0, sizeof (*j));
	StringCopyS (j->name, sizeof (j->name), name);
	j->parent_idx = parent_idx;
	// HSD stores radians; joint_t.rotate is degrees (see lib-model-dae.c).
	const double r2d = 180.0 / M_PI;
	j->rotate = (vec3_t) { (float)(rx * r2d), (float)(ry * r2d), (float)(rz * r2d) };
	j->scale = (vec3_t) { sx, sy, sz };
	j->translate = (vec3_t) { tx, ty, tz };
	const int idx = (int)ctx->n_joints++;
	// Track JOBJ offset -> joint index for envelope weight lookups
	if (jobj_off)
	{
		if (ctx->n_jobj_map == ctx->cap_jobj_map)
		{
			ctx->cap_jobj_map = ctx->cap_jobj_map ? ctx->cap_jobj_map * 2 : 64;
			ctx->jobj_map = REALLOC (ctx->jobj_map, ctx->cap_jobj_map * sizeof (*ctx->jobj_map));
		}
		ctx->jobj_map[ctx->n_jobj_map].jobj_off = jobj_off;
		ctx->jobj_map[ctx->n_jobj_map].joint_idx = idx;
		ctx->n_jobj_map++;
	}
	return idx;
}

//--------------------------------------------------------------------------
// MOBJ material reading + TOBJ texture chain binding
//
// HSD_MObjDesc on-disk (0x18 bytes):
//   +0x00 class_name ptr
//   +0x04 u32  rendermode
//   +0x08 ptr  texdesc (HSD_TObj*, texture chain)
//   +0x0C ptr  mat (HSD_Material*)
//   +0x10 ptr  renderdesc
//   +0x14 ptr  pedesc (HSD_PEDesc*)
//
// HSD_Material on-disk (0x14 bytes):
//   +0x00 u8[4] ambient (GXColor: R,G,B,A)
//   +0x04 u8[4] diffuse (GXColor: R,G,B,A)
//   +0x08 u8[4] specular (GXColor: R,G,B,A)
//   +0x0C f32   alpha
//   +0x10 f32   shininess
//
// Returns material index in ctx->materials, or -1 if MOBJ couldn't be read.
//--------------------------------------------------------------------------
static int hsd_read_mobj (hsd_model_ctx_t *ctx, u32 mobj_off)
{
	if (!mobj_off || mobj_off + HSD_MOBJ_DESC_SIZE > ctx->hsd->reloc_off)
		return -1;

	// Check if this MOBJ was already read (dedup by offset)
	for (uint i = 0; i < ctx->n_mat_slots; i++)
		if (ctx->mat_slots[i].mobj_off == mobj_off)
			return (int)ctx->mat_slots[i].mat_idx;

	// const u32 rendermode = be32(s+0x04); // available but not stored in material_t
	const u32 mat_ptr = get_ptr (ctx->hsd, mobj_off + 0x0C);

	// Allocate material
	if (ctx->n_materials == ctx->cap_materials)
	{
		ctx->cap_materials = ctx->cap_materials ? ctx->cap_materials * 2 : 16;
		ctx->materials = REALLOC (ctx->materials, ctx->cap_materials * sizeof (*ctx->materials));
	}
	material_t *mat = ctx->materials + ctx->n_materials;
	memset (mat, 0, sizeof (*mat));
	snprintf (mat->name, sizeof (mat->name), "mat_%u", ctx->n_materials);

	if (mat_ptr && mat_ptr + HSD_MATERIAL_SIZE <= ctx->hsd->reloc_off)
	{
		const u8 *m = ctx->hsd->data + mat_ptr;
		// HSD_Material: ambient(+0), diffuse(+4), specular(+8), alpha(+C), shininess(+10)
		// GXColor is {R,G,B,A} as u8, all four packed into one u32.
		const u32 amb = be32 (m + 0x00);
		const u32 dif = be32 (m + 0x04);
		const u32 spc = be32 (m + 0x08);
		const float alpha = bef32 (m + 0x0C);
		const float shin = bef32 (m + 0x10);

		mat->diffuse[0] = (dif >> 24 & 0xFF) / 255.0f;
		mat->diffuse[1] = (dif >> 16 & 0xFF) / 255.0f;
		mat->diffuse[2] = (dif >> 8 & 0xFF) / 255.0f;
		mat->diffuse[3] = alpha > 0 ? alpha : 1.0f;

		mat->ambient[0] = (amb >> 24 & 0xFF) / 255.0f;
		mat->ambient[1] = (amb >> 16 & 0xFF) / 255.0f;
		mat->ambient[2] = (amb >> 8 & 0xFF) / 255.0f;

		mat->specular[0] = (spc >> 24 & 0xFF) / 255.0f;
		mat->specular[1] = (spc >> 16 & 0xFF) / 255.0f;
		mat->specular[2] = (spc >> 8 & 0xFF) / 255.0f;

		mat->shininess = shin;

		snprintf (mat->name, sizeof (mat->name), "mat_d%02X%02X%02X_a%02X", dif >> 24 & 0xFF,
			dif >> 16 & 0xFF, dif >> 8 & 0xFF, (uint)(alpha * 255) & 0xFF);
	}

	//------------------------------------------------------------------
	// Walk the TOBJ chain from MOBJ+0x08 (texdesc) to bind textures.
	// Each TOBJ carries: next(+0x04), coord(+0x0C), SRT(+0x10..+0x30),
	// wrap_s/t(+0x34/+0x38), ImageData(+0x4C), TlutData(+0x50).
	//------------------------------------------------------------------
	const u32 tobj_off = get_ptr (ctx->hsd, mobj_off + 0x08);
	if (tobj_off && ctx->n_tex_names)
	{
		u32 t = tobj_off;
		for (uint layer = 0; layer < 8 && t && (u64)t + 0x58 <= ctx->hsd->reloc_off; layer++)
		{
			const u32 img_off = get_ptr (ctx->hsd, t + HSD_TOBJ_IMAGE_OFF);
			const u32 img_data = img_off ? get_ptr (ctx->hsd, img_off) : 0;
			// Match this ImageData pixel buffer to our texture lookup
			for (uint j = 0; j < ctx->n_tex_lookup; j++)
			{
				if (ctx->tex_lookup[j].data_off != img_data)
					continue;
				const uint tex_idx = ctx->tex_lookup[j].idx;
				snprintf (mat->textures[layer], sizeof (mat->textures[layer]), "tex%03u", tex_idx);
				mat->texture_coord[layer] = (int)be32 (ctx->hsd->data + t + HSD_TOBJ_COORD_OFF);
				mat->wrap_s[layer] = (uint8_t)be32 (ctx->hsd->data + t + HSD_TOBJ_WRAP_S_OFF);
				mat->wrap_t[layer] = (uint8_t)be32 (ctx->hsd->data + t + HSD_TOBJ_WRAP_T_OFF);

				// Read SRT (Scale/Rotate/Translate) texture transforms
				const float rot_x = bef32 (ctx->hsd->data + t + HSD_TOBJ_ROT_X_OFF);
				const float rot_y = bef32 (ctx->hsd->data + t + HSD_TOBJ_ROT_X_OFF + 4);
				const float rot_z = bef32 (ctx->hsd->data + t + HSD_TOBJ_ROT_X_OFF + 8);
				const float sc_u = bef32 (ctx->hsd->data + t + HSD_TOBJ_SCALE_X_OFF);
				const float sc_v = bef32 (ctx->hsd->data + t + HSD_TOBJ_SCALE_X_OFF + 4);
				const float tr_u = bef32 (ctx->hsd->data + t + HSD_TOBJ_TRANS_X_OFF);
				const float tr_v = bef32 (ctx->hsd->data + t + HSD_TOBJ_TRANS_X_OFF + 4);
				// Only Z rotation matters for 2D texture coords
				mat->tex_rotate[layer] = rot_z;
				mat->tex_scale_s[layer] = sc_u;
				mat->tex_scale_t[layer] = sc_v;
				mat->tex_translate_s[layer] = tr_u;
				mat->tex_translate_t[layer] = tr_v;
				mat->has_tex_transform[layer] = (rot_z != 0.0f) || (sc_u != 1.0f) || (sc_v != 1.0f)
						|| (tr_u != 0.0f) || (tr_v != 0.0f)
					? 1
					: 0;

				mat->num_textures = layer + 1;
				break;
			}
			// Follow TOBJ chain: next ptr at +0x04
			const u32 next = get_ptr (ctx->hsd, t + HSD_TOBJ_NEXT_OFF);
			if (!next || next == t)
				break; // safety: no circular chains
			t = next;
		}
	}

	// Track slot for dedup
	if (ctx->n_mat_slots == ctx->cap_mat_slots)
	{
		ctx->cap_mat_slots = ctx->cap_mat_slots ? ctx->cap_mat_slots * 2 : 16;
		ctx->mat_slots = REALLOC (ctx->mat_slots, ctx->cap_mat_slots * sizeof (*ctx->mat_slots));
	}
	const uint idx = ctx->n_materials++;
	ctx->mat_slots[ctx->n_mat_slots].mobj_off = mobj_off;
	ctx->mat_slots[ctx->n_mat_slots].mat_idx = idx;
	ctx->n_mat_slots++;

	return (int)idx;
}

//--------------------------------------------------------------------------
// POBJ envelope weight reading
//
// When POBJ flags bits 12-13 == 0x2000 (POBJ_ENVELOPE), the union at +0x14
// is a pointer-to-pointer (HSD_EnvelopeDesc**). That pointer itself is a
// relocated pointer to a NULL-terminated array of HSD_EnvelopeDesc* entries,
// each pointing to an HSD_EnvelopeDesc { joint(ptr), weight(f32) }.
//
// Populates ctx->node_influences and fills per-position weights into the
// mesh's position_node array.  Returns true if at least one envelope was read.
//--------------------------------------------------------------------------
static bool hsd_read_pobj_envelope (
	hsd_model_ctx_t *ctx, u32 pobj_off, int joint_idx, int *position_node, uint n_positions)
{
	const hsd_t *hsd = ctx->hsd;
	// The union at +0x14 is a relocated pointer to a NULL-terminated array
	// of HSD_EnvelopeDesc* pointers.
	const u32 arr_off = get_ptr (hsd, pobj_off + 0x14);
	if (!arr_off || arr_off >= hsd->reloc_off)
		return false;

	// Read the NULL-terminated array of envelope descriptor pointers
	uint n_env = 0;
	u32 env_desc_offs[64]; // stack buffer; Melee rarely exceeds 4-8 envelopes per POBJ

	for (uint i = 0; i < 64; i++)
	{
		// Each entry is a relocated pointer (4 bytes) in the array
		const u32 entry_loc = arr_off + 4 * i;
		if (entry_loc + 4 > hsd->reloc_off)
			break;
		const u32 env_desc_ptr = get_ptr (hsd, entry_loc);
		if (!env_desc_ptr)
			break; // NULL terminator
		if (env_desc_ptr + HSD_ENVELOPE_DESC_SIZE > hsd->reloc_off)
			break;
		env_desc_offs[n_env++] = env_desc_ptr;
	}

	if (!n_env)
		return false;

	// Build a single node_influence from all envelopes
	influence_t *weights = CALLOC (n_env, sizeof (*weights));
	uint n_weights = 0;
	float total_weight = 0;

	for (uint i = 0; i < n_env; i++)
	{
		const u8 *e = hsd->data + env_desc_offs[i];
		const u32 jobj_ptr = get_ptr (hsd, env_desc_offs[i]); // joint field at +0x00
		const float w = bef32 (e + 0x04);

		if (!jobj_ptr || w <= 0.0f)
			continue;

		const int mapped = hsd_find_joint_by_offset (ctx, jobj_ptr);
		if (mapped < 0)
			continue; // joint not yet in skeleton

		weights[n_weights].bone_idx = mapped;
		weights[n_weights].weight = w;
		total_weight += w;
		n_weights++;
	}

	if (!n_weights)
	{
		FREE (weights);
		return false;
	}

	// Normalize weights to sum to 1.0
	if (total_weight > 0.0f && total_weight != 1.0f)
	{
		const float inv = 1.0f / total_weight;
		for (uint i = 0; i < n_weights; i++)
			weights[i].weight *= inv;
	}

	// Allocate node_influence
	if (ctx->n_node_influences == ctx->cap_node_influences)
	{
		ctx->cap_node_influences = ctx->cap_node_influences ? ctx->cap_node_influences * 2 : 64;
		ctx->node_influences = REALLOC (
			ctx->node_influences, ctx->cap_node_influences * sizeof (*ctx->node_influences));
	}
	const uint ni_idx = ctx->n_node_influences++;
	ctx->node_influences[ni_idx].weights = weights;
	ctx->node_influences[ni_idx].num_weights = n_weights;

	// Assign this node influence to all positions in the mesh
	if (position_node)
	{
		for (uint i = 0; i < n_positions; i++)
			position_node[i] = (int)ni_idx;
	}

	return true;
}

//--------------------------------------------------------------------------
// Decodes every POBJ of 'dobj_off's own DOBJ list (dobj->next chain) into
// one mesh_t bound to 'joint_idx'. Appends to ctx->meshes for each DOBJ that
// yields at least one triangle; DOBJs with no decodable POBJ are skipped.
// Now reads MOBJ material and handles POBJ envelope/shape flags.
//--------------------------------------------------------------------------
static void hsd_build_dobj_meshes (hsd_model_ctx_t *ctx, u32 dobj_off, int joint_idx, ccp jobj_name)
{
	const hsd_t *hsd = ctx->hsd;
	uint dobj_n = 0;

	while (dobj_off && (u64)dobj_off + 0x18 <= hsd->reloc_off && dobj_n < 4096)
	{
		dobj_n++;
		const u32 next_dobj = get_ptr (hsd, dobj_off + 0x04);
		const u32 mobj_off = get_ptr (hsd, dobj_off + 0x08); // MOBJ (material)
		u32 pobj_off = get_ptr (hsd, dobj_off + 0x0C);

		// Read MOBJ material for this DOBJ
		const int mat_idx = hsd_read_mobj (ctx, mobj_off);

		hsd_vtx_t *all_tri = 0;
		int *all_tri_node = 0; // per-position node_influence index
		uint n_all_tri = 0, cap_all_tri = 0;
		hsd_shape_slot_t *sh_slots = 0;
		uint n_sh_slots = 0, cap_sh_slots = 0;
		uint pobj_n = 0;

		// POBJ fields: +0x04 next, +0x08 attrs, +0x0C flags(u16), +0x0E n_display(u16),
		//              +0x10 display_list, +0x14 union (SingleBoundJOBJ / envelope / shape)
		while (pobj_off && (u64)pobj_off + 0x18 <= hsd->reloc_off && pobj_n < 4096)
		{
			pobj_n++;
			const u32 next_pobj = get_ptr (hsd, pobj_off + 0x04);
			const u32 attrs_off = get_ptr (hsd, pobj_off + 0x08);
			const u16 pobj_flags = be16 (hsd->data + pobj_off + 0x0C);
			const s16 dl_words = (s16)be16 (hsd->data + pobj_off + 0x0E);
			const u32 dl_off = get_ptr (hsd, pobj_off + 0x10);
			const u32 dl_size = dl_words > 0 ? (u32)dl_words * 32 : 0;

			const uint pobj_type = pobj_flags & HSD_POBJ_TYPE_MASK;

			// Determine joint binding: SingleBoundJOBJ overrides the DOBJ's owning joint
			int bind_joint = joint_idx;
			if (pobj_type == HSD_POBJ_SKIN)
			{
				const u32 sbj = get_ptr (hsd, pobj_off + 0x14);
				if (sbj && sbj + 0x40 <= hsd->reloc_off)
				{
					// Validate it looks like a JOBJ (finite TRS floats)
					const float rx = bef32 (hsd->data + sbj + 0x14);
					if (isfinite (rx))
					{
						const int mapped = hsd_find_joint_by_offset (ctx, sbj);
						if (mapped >= 0)
							bind_joint = mapped;
					}
				}
			}

			if (attrs_off && dl_off && dl_size && (u64)dl_off + dl_size <= hsd->reloc_off)
			{
				hsd_attr_t attrs[HSD_MAX_ATTR];
				const uint n_attr = read_gx_attrs (hsd, attrs_off, attrs);

				hsd_vtx_t *tri = 0;
				uint n_tri = 0;
				if (n_attr
					&& hsd_decode_display_list (
						hsd, hsd->data + dl_off, dl_size, attrs, n_attr, &tri, &n_tri))
				{
					// Grow all_tri + all_tri_node arrays
					if (n_all_tri + n_tri > cap_all_tri)
					{
						const uint newcap = cap_all_tri ? cap_all_tri * 2 : 64;
						cap_all_tri = newcap < n_all_tri + n_tri ? n_all_tri + n_tri : newcap;
						all_tri = REALLOC (all_tri, cap_all_tri * sizeof (*all_tri));
						all_tri_node = REALLOC (all_tri_node, cap_all_tri * sizeof (*all_tri_node));
					}

					// For envelope POBJs, read multi-bone weights and write
					// node_influence indices into the per-position array.
					// For single-bound POBJs (SKIN/SHAPEANIM), create a
					// single-weight node_influence for the resolved joint.
					if (pobj_type == HSD_POBJ_ENVELOPE)
					{
						hsd_read_pobj_envelope (
							ctx, pobj_off, joint_idx, all_tri_node + n_all_tri, n_tri);
					}
					else
					{
						// Create single-weight node_influence for this joint
						if (ctx->n_node_influences == ctx->cap_node_influences)
						{
							ctx->cap_node_influences
								= ctx->cap_node_influences ? ctx->cap_node_influences * 2 : 64;
							ctx->node_influences = REALLOC (ctx->node_influences,
								ctx->cap_node_influences * sizeof (*ctx->node_influences));
						}
						influence_t *w = CALLOC (1, sizeof (*w));
						w->bone_idx = bind_joint;
						w->weight = 1.0f;
						const uint ni_idx = ctx->n_node_influences++;
						ctx->node_influences[ni_idx].weights = w;
						ctx->node_influences[ni_idx].num_weights = 1;
						for (uint i = 0; i < n_tri; i++)
							all_tri_node[n_all_tri + i] = (int)ni_idx;
					}
					memcpy (all_tri + n_all_tri, tri, n_tri * sizeof (*tri));
					n_all_tri += n_tri;
					FREE (tri);
				}
			}

			// For shape-animated POBJs, decode the base mesh here and the
			// morph targets from HSD_ShapeSetDesc below (if present).
			// The base DL is decoded above; the shape deltas follow.
			if (pobj_type == HSD_POBJ_SHAPEANIM)
			{
				const u32 ss_off = get_ptr (hsd, pobj_off + 0x14);
				if (ss_off && ss_off + HSD_SHAPESET_SIZE <= hsd->reloc_off)
				{
					// Decode each shape's display list; store vertices
					// for later delta computation against the base mesh.
					u32 shapes = get_ptr (hsd, ss_off + 0x04);
					const u32 n_shapes = be32 (hsd->data + ss_off + 0x0C);
					for (uint si = 0; si < n_shapes && shapes
						&& (u64)shapes + HSD_SHAPE_DESC_SIZE <= hsd->reloc_off;
						si++)
					{
						const u32 sh_dl = get_ptr (hsd, shapes + 0x04);
						const u32 sh_attrs = get_ptr (hsd, shapes + 0x08);
						const s16 sh_dl_words = (s16)be16 (hsd->data + shapes + 0x0E);
						const u32 sh_dl_size = sh_dl_words > 0 ? (u32)sh_dl_words * 32 : 0;

						if (sh_dl && sh_attrs && sh_dl_size
							&& (u64)sh_dl + sh_dl_size <= hsd->reloc_off)
						{
							hsd_attr_t sh_attr_arr[HSD_MAX_ATTR];
							const uint n_sh_attr = read_gx_attrs (hsd, sh_attrs, sh_attr_arr);
							hsd_vtx_t *sh_tri = 0;
							uint n_sh_tri = 0;
							if (n_sh_attr
								&& hsd_decode_display_list (hsd, hsd->data + sh_dl, sh_dl_size,
									sh_attr_arr, n_sh_attr, &sh_tri, &n_sh_tri))
							{
								if (n_sh_slots == cap_sh_slots)
								{
									cap_sh_slots = cap_sh_slots ? cap_sh_slots * 2 : 4;
									sh_slots
										= REALLOC (sh_slots, cap_sh_slots * sizeof (*sh_slots));
								}
								sh_slots[n_sh_slots].verts = sh_tri;
								sh_slots[n_sh_slots].n_verts = n_sh_tri;
								n_sh_slots++;
							}
						}
						shapes = get_ptr (hsd, shapes); // next shape
					}
				}
			}

			pobj_off = next_pobj;
		}

		if (n_all_tri)
		{
			if (ctx->n_meshes == ctx->cap_meshes)
			{
				ctx->cap_meshes = ctx->cap_meshes ? ctx->cap_meshes * 2 : 16;
				ctx->meshes = REALLOC (ctx->meshes, ctx->cap_meshes * sizeof (*ctx->meshes));
			}
			mesh_t *m = ctx->meshes + ctx->n_meshes++;
			memset (m, 0, sizeof (*m));
			snprintf (m->name, sizeof (m->name), "%s_dobj%u", jobj_name, dobj_n - 1);

			m->num_positions = m->num_vertices = n_all_tri;
			m->positions = MALLOC (n_all_tri * sizeof (*m->positions));
			m->position_node = all_tri_node;
			all_tri_node = 0; // transfer ownership
			m->vertices = CALLOC (n_all_tri, sizeof (*m->vertices));

			bool any_nrm = false, any_uv = false, any_clr = false, any_clr1 = false,
				 any_tan = false;
			bool any_extra_uv[7] = { 0 };
			for (uint i = 0; i < n_all_tri; i++)
			{
				any_nrm |= all_tri[i].has_nrm;
				any_uv |= all_tri[i].has_uv;
				any_clr |= all_tri[i].has_clr;
				any_clr1 |= all_tri[i].has_clr1;
				any_tan |= all_tri[i].has_tan;
				for (uint k = 0; k < 7; k++)
					any_extra_uv[k] |= all_tri[i].has_extra_uv[k];
			}
			if (any_nrm)
			{
				m->num_normals = n_all_tri;
				m->normals = MALLOC (n_all_tri * sizeof (*m->normals));
			}
			if (any_tan)
			{
				m->num_tangents = n_all_tri;
				m->tangents = MALLOC (n_all_tri * sizeof (*m->tangents));
			}
			if (any_uv)
			{
				m->num_texcoords = n_all_tri;
				m->texcoords = MALLOC (n_all_tri * sizeof (*m->texcoords));
			}
			if (any_clr)
			{
				m->num_colors[0] = n_all_tri;
				m->colors[0] = MALLOC (n_all_tri * sizeof (*m->colors[0]));
			}
			if (any_clr1)
			{
				m->num_colors[1] = n_all_tri;
				m->colors[1] = MALLOC (n_all_tri * sizeof (*m->colors[1]));
			}
			for (uint k = 0; k < 7; k++)
				if (any_extra_uv[k])
				{
					m->num_extra_texcoords[k] = n_all_tri;
					m->extra_texcoords[k] = MALLOC (n_all_tri * sizeof (*m->extra_texcoords[k]));
				}

			for (uint i = 0; i < n_all_tri; i++)
			{
				const hsd_vtx_t *v = all_tri + i;
				m->positions[i] = (vec3_t) { v->pos[0], v->pos[1], v->pos[2] };
				m->vertices[i].position_idx = (int)i;
				m->vertices[i].normal_idx = any_nrm ? (int)i : -1;
				m->vertices[i].tangent_idx = any_tan ? (int)i : -1;
				m->vertices[i].texcoord_idx = any_uv ? (int)i : -1;
				m->vertices[i].matrix_idx = -1;
				m->vertices[i].color_idx[0] = any_clr ? (int)i : -1;
				m->vertices[i].color_idx[1] = any_clr1 ? (int)i : -1;
				for (int k = 0; k < 7; k++)
					m->vertices[i].extra_texcoord_idx[k] = -1;
				if (any_nrm)
					m->normals[i] = (vec3_t) { v->nrm[0], v->nrm[1], v->nrm[2] };
				if (any_tan)
					m->tangents[i] = (vec3_t) { v->tan[0], v->tan[1], v->tan[2] };
				if (any_uv)
					m->texcoords[i] = (vec2_t) { v->uv[0], v->uv[1] };
				if (any_clr)
					m->colors[0][i] = (color4_t) { v->clr[0], v->clr[1], v->clr[2], v->clr[3] };
				if (any_clr1)
					m->colors[1][i] = (color4_t) { v->clr1[0], v->clr1[1], v->clr1[2], v->clr1[3] };
				for (uint k = 0; k < 7; k++)
				{
					if (any_extra_uv[k])
					{
						m->extra_texcoords[k][i]
							= (vec2_t) { v->extra_uv[k][0], v->extra_uv[k][1] };
						m->vertices[i].extra_texcoord_idx[k] = (int)i;
					}
				}
			}
			m->material_idx = mat_idx;

			// Populate morph targets from accumulated shape data.
			// Each shape's decoded vertices map 1:1 to the base mesh vertices
			// (same display list winding).  Morph deltas = shape.pos - base.pos.
			if (n_sh_slots > 0)
			{
				m->num_morph_targets = n_sh_slots;
				m->morph_targets = CALLOC (n_sh_slots, sizeof (*m->morph_targets));
				m->morph_weights = CALLOC (n_sh_slots, sizeof (*m->morph_weights));
				for (uint si = 0; si < n_sh_slots; si++)
				{
					morph_target_t *mt = m->morph_targets + si;
					snprintf (mt->name, sizeof (mt->name), "shape%u", si);
					const uint n
						= n_all_tri < sh_slots[si].n_verts ? n_all_tri : sh_slots[si].n_verts;
					mt->num_positions = n;
					mt->position_deltas = CALLOC (n, sizeof (*mt->position_deltas));
					for (uint vi = 0; vi < n; vi++)
					{
						const vec3_t base = m->positions[vi];
						const float *sp = sh_slots[si].verts[vi].pos;
						mt->position_deltas[vi]
							= (vec3_t) { sp[0] - base.x, sp[1] - base.y, sp[2] - base.z };
					}
				}
			}
		}
		// Free accumulated shape slots (ownership transferred to morph_targets)
		for (uint si = 0; si < n_sh_slots; si++)
			FREE (sh_slots[si].verts);
		FREE (sh_slots);
		FREE (all_tri);
		FREE (all_tri_node);

		dobj_off = next_dobj;
	}
}

// Recursively walks the child/next-sibling JOBJ tree, adding joints to the
// map but NOT building meshes.  Called in a first pass so that all joints
// are discoverable when envelopes reference each other.
static void hsd_walk_jobj_skeleton (hsd_model_ctx_t *ctx, u32 off, int parent_idx, uint depth)
{
	const hsd_t *hsd = ctx->hsd;
	if (depth > 64 || !off || (u64)off + 0x40 > hsd->reloc_off)
		return;

	// Cycle detection: if this JOBJ was already registered, stop.
	if (hsd_find_joint_by_offset (ctx, off) >= 0)
		return;

	const u8 *s = hsd->data + off;
	const float rx = bef32 (s + 0x14), ry = bef32 (s + 0x18), rz = bef32 (s + 0x1C);
	const float sx = bef32 (s + 0x20), sy = bef32 (s + 0x24), sz = bef32 (s + 0x28);
	const float tx = bef32 (s + 0x2C), ty = bef32 (s + 0x30), tz = bef32 (s + 0x34);

	if (!isfinite (rx) || !isfinite (ry) || !isfinite (rz) || !isfinite (sx) || !isfinite (sy)
		|| !isfinite (sz) || !isfinite (tx) || !isfinite (ty) || !isfinite (tz)
		|| (rx && fpclassify (rx) == FP_SUBNORMAL) || (ry && fpclassify (ry) == FP_SUBNORMAL)
		|| (rz && fpclassify (rz) == FP_SUBNORMAL) || (sx && fpclassify (sx) == FP_SUBNORMAL)
		|| (sy && fpclassify (sy) == FP_SUBNORMAL) || (sz && fpclassify (sz) == FP_SUBNORMAL)
		|| (tx && fpclassify (tx) == FP_SUBNORMAL) || (ty && fpclassify (ty) == FP_SUBNORMAL)
		|| (tz && fpclassify (tz) == FP_SUBNORMAL))
	{
		if (off + HSD_FTDATA_SKELETON_OFF + 4 <= hsd->reloc_off)
		{
			const u32 skel_off = get_ptr (hsd, off + HSD_FTDATA_SKELETON_OFF);
			if (skel_off && skel_off + 0x40 <= hsd->reloc_off)
			{
				const u8 *sk = hsd->data + skel_off;
				const float frx = bef32 (sk + 0x14), fry = bef32 (sk + 0x18),
							frz = bef32 (sk + 0x1C);
				const float fsx = bef32 (sk + 0x20), fsy = bef32 (sk + 0x24),
							fsz = bef32 (sk + 0x28);
				if (isfinite (frx) && isfinite (fry) && isfinite (frz) && isfinite (fsx)
					&& isfinite (fsy) && isfinite (fsz))
				{
					hsd_walk_jobj_skeleton (ctx, skel_off, parent_idx, depth);
					return;
				}
			}
		}
		return;
	}

	char name[64];
	snprintf (name, sizeof (name), "jobj_%u", off);
	hsd_add_joint (ctx, off, name, parent_idx, rx, ry, rz, sx, sy, sz, tx, ty, tz);

	const u32 child_off = get_ptr (hsd, off + 0x08);
	const u32 next_off = get_ptr (hsd, off + 0x0C);

	if (child_off)
		hsd_walk_jobj_skeleton (ctx, child_off, (int)ctx->n_joints - 1, depth + 1);
	if (next_off)
		hsd_walk_jobj_skeleton (ctx, next_off, parent_idx, depth + 1);
}

//--------------------------------------------------------------------------
// Second pass: walks JOBJ tree again, building meshes from DOBJs.
// All joints are already in the jobj_map from the skeleton pass.
static void hsd_walk_jobj_meshes (hsd_model_ctx_t *ctx, u32 off, int parent_idx, uint depth)
{
	const hsd_t *hsd = ctx->hsd;
	if (depth > 64 || !off || (u64)off + 0x40 > hsd->reloc_off)
		return;

	const u8 *s = hsd->data + off;
	const float rx = bef32 (s + 0x14), ry = bef32 (s + 0x18), rz = bef32 (s + 0x1C);
	const float sx = bef32 (s + 0x20), sy = bef32 (s + 0x24), sz = bef32 (s + 0x28);
	const float tx = bef32 (s + 0x2C), ty = bef32 (s + 0x30), tz = bef32 (s + 0x34);

	if (!isfinite (rx) || !isfinite (ry) || !isfinite (rz) || !isfinite (sx) || !isfinite (sy)
		|| !isfinite (sz) || !isfinite (tx) || !isfinite (ty) || !isfinite (tz)
		|| (rx && fpclassify (rx) == FP_SUBNORMAL) || (ry && fpclassify (ry) == FP_SUBNORMAL)
		|| (rz && fpclassify (rz) == FP_SUBNORMAL) || (sx && fpclassify (sx) == FP_SUBNORMAL)
		|| (sy && fpclassify (sy) == FP_SUBNORMAL) || (sz && fpclassify (sz) == FP_SUBNORMAL)
		|| (tx && fpclassify (tx) == FP_SUBNORMAL) || (ty && fpclassify (ty) == FP_SUBNORMAL)
		|| (tz && fpclassify (tz) == FP_SUBNORMAL))
	{
		if (off + HSD_FTDATA_SKELETON_OFF + 4 <= hsd->reloc_off)
		{
			const u32 skel_off = get_ptr (hsd, off + HSD_FTDATA_SKELETON_OFF);
			if (skel_off && skel_off + 0x40 <= hsd->reloc_off)
			{
				const u8 *sk = hsd->data + skel_off;
				const float frx = bef32 (sk + 0x14), fry = bef32 (sk + 0x18),
							frz = bef32 (sk + 0x1C);
				const float fsx = bef32 (sk + 0x20), fsy = bef32 (sk + 0x24),
							fsz = bef32 (sk + 0x28);
				if (isfinite (frx) && isfinite (fry) && isfinite (frz) && isfinite (fsx)
					&& isfinite (fsy) && isfinite (fsz))
				{
					hsd_walk_jobj_meshes (ctx, skel_off, parent_idx, depth);
					return;
				}
			}
		}
		return;
	}

	// Find this JOBJ's index from the pre-built map
	const int idx = hsd_find_joint_by_offset (ctx, off);
	char name[64];
	snprintf (name, sizeof (name), "jobj_%u", off);

	const u32 dobj_off = get_ptr (hsd, off + 0x10);
	if (dobj_off)
		hsd_build_dobj_meshes (ctx, dobj_off, idx >= 0 ? idx : parent_idx, name);

	const u32 child_off = get_ptr (hsd, off + 0x08);
	const u32 next_off = get_ptr (hsd, off + 0x0C);

	if (child_off)
		hsd_walk_jobj_meshes (ctx, child_off, idx >= 0 ? idx : parent_idx, depth + 1);
	if (next_off)
		hsd_walk_jobj_meshes (ctx, next_off, parent_idx, depth + 1);
}

//-----------------------------------------------------------------------------

int ExportHSDModel (const hsd_t *hsd, ccp out_glb_file)
{
	if (!hsd || !hsd->data || !out_glb_file)
		return -1;

	// Root table: (offset,name-offset) pairs right after the relocation
	// table, offsets 0x20-relative like everywhere else (verified: root[0]
	// of TyBox.dat resolves to "ToyBoxModel_TopN_joint", a real JOBJ).
	const u32 root_table = hsd->reloc_off + 4 * hsd->n_reloc;

	hsd_model_ctx_t ctx = { .hsd = hsd };

	// Build texture lookup so hsd_read_mobj() can bind TOBJ textures
	hsd_tex_t *tex = 0;
	const uint n_tex = find_textures (hsd, &tex);
	if (n_tex)
	{
		ctx.tex_names = CALLOC (n_tex, sizeof (*ctx.tex_names));
		ctx.tex_lookup = CALLOC (n_tex, sizeof (*ctx.tex_lookup));
		ctx.n_tex_names = n_tex;

		char base[80];
		// extract basename from out_glb_file path for texture naming
		ccp slash = strrchr (out_glb_file, '/');
		ccp bname = slash ? slash + 1 : out_glb_file;
		StringCopyS (base, sizeof (base), bname);
		char *dot = strrchr (base, '.');
		if (dot)
			*dot = 0;

		for (uint i = 0; i < n_tex; i++)
		{
			char name[80];
			snprintf (name, sizeof (name), "%s.tex%03u", base, i);
			// Tex_names[] is char** not ccp* so we can FREE it later.
			// Use CALLOC + StringCopyS to avoid the banned strdup().
			char *s = CALLOC (1, strlen (name) + 1);
			StringCopyS (s, strlen (name) + 1, name);
			ctx.tex_names[i] = s;

			ctx.tex_lookup[i].data_off = tex[i].data_off;
			ctx.tex_lookup[i].idx = i;
		}
		ctx.n_tex_lookup = n_tex;
	}

	// Two-pass approach: first discover all joints, then build meshes.
	// This ensures all joints are in jobj_map before envelope resolution.
	for (uint i = 0; i < hsd->n_root; i++)
	{
		if ((u64)root_table + 8 * (i + 1) > hsd->size)
			break;
		const s32 root_off_raw = (s32)be32 (hsd->data + root_table + 8 * i);
		const u32 root_off = (u32)root_off_raw + HSD_DATA_BASE;
		if (root_off < HSD_DATA_BASE || root_off >= hsd->reloc_off)
			continue;
		hsd_walk_jobj_skeleton (&ctx, root_off, -1, 0);
	}
	for (uint i = 0; i < hsd->n_root; i++)
	{
		if ((u64)root_table + 8 * (i + 1) > hsd->size)
			break;
		const s32 root_off_raw = (s32)be32 (hsd->data + root_table + 8 * i);
		const u32 root_off = (u32)root_off_raw + HSD_DATA_BASE;
		if (root_off < HSD_DATA_BASE || root_off >= hsd->reloc_off)
			continue;
		hsd_walk_jobj_meshes (&ctx, root_off, -1, 0);
	}

	int written = -1;
	if (ctx.n_meshes)
	{
		model_t model;
		memset (&model, 0, sizeof (model));
		model.meshes = ctx.meshes;
		model.num_meshes = ctx.n_meshes;
		model.joints = ctx.joints;
		model.num_joints = ctx.n_joints;
		model.materials = ctx.materials;
		model.num_materials = ctx.n_materials;
		model.node_influences = ctx.node_influences;
		model.num_node_influences = ctx.n_node_influences;
		ComputeModelTRSBinds (&model);

		const uint path_len = strlen (out_glb_file);
		const bool is_dae = path_len > 4 && !strcasecmp (out_glb_file + path_len - 4, ".dae");
		written = (is_dae ? ExportModelToDAE (&model, out_glb_file)
						  : ExportModelToGLB (&model, out_glb_file))
				== 0
			? (int)ctx.n_meshes
			: -1;
	}

	for (uint i = 0; i < ctx.n_meshes; i++)
	{
		mesh_t *m = ctx.meshes + i;
		FREE (m->positions);
		FREE (m->normals);
		FREE (m->tangents);
		FREE (m->texcoords);
		FREE (m->colors[0]);
		FREE (m->colors[1]);
		FREE (m->position_node);
		FREE (m->vertices);
		FREE (m->triangle_materials);
		for (uint t = 0; t < m->num_morph_targets; t++)
			FREE (m->morph_targets[t].position_deltas);
		FREE (m->morph_targets);
		FREE (m->morph_weights);
	}
	FREE (ctx.meshes);
	FREE (ctx.joints);
	for (uint i = 0; i < ctx.n_node_influences; i++)
		FREE (ctx.node_influences[i].weights);
	FREE (ctx.node_influences);
	FREE (ctx.materials);
	FREE (ctx.jobj_map);
	FREE (ctx.mat_slots);
	FREE (tex);
	for (uint i = 0; i < ctx.n_tex_names; i++)
		FREE (ctx.tex_names[i]);
	FREE (ctx.tex_names);
	FREE (ctx.tex_lookup);
	return written;
}

///////////////////////////////////////////////////////////////////////////////

int ExportHSDModelFromData (const u8 *data, uint size, ccp out_glb_file)
{
	hsd_t hsd;
	if (!ScanHSD (&hsd, data, size))
		return -1;
	const int stat = ExportHSDModel (&hsd, out_glb_file);
	ResetHSD (&hsd);
	return stat;
}

///////////////////////////////////////////////////////////////////////////////
///////////////			     encoder			///////////////
///////////////////////////////////////////////////////////////////////////////

typedef struct hsd_builder_t
{
	u8 *buf;
	uint size;
	uint cap;

	u32 *relocs;
	uint n_relocs;
	uint cap_relocs;
} hsd_builder_t;

static void hsd_bld_grow (hsd_builder_t *bld, uint need)
{
	if (bld->size + need > bld->cap)
	{
		uint newcap = bld->cap ? bld->cap * 2 : 4096;
		while (newcap < bld->size + need)
			newcap *= 2;
		bld->buf = REALLOC (bld->buf, newcap);
		bld->cap = newcap;
	}
}

static void hsd_bld_align (hsd_builder_t *bld, uint alignment)
{
	while (bld->size % alignment != 0)
	{
		hsd_bld_grow (bld, 1);
		bld->buf[bld->size++] = 0;
	}
}

static inline u32 hsd_bld_pos (const hsd_builder_t *bld)
{
	return bld->size;
}

static void hsd_bld_u8 (hsd_builder_t *bld, u8 val)
{
	hsd_bld_grow (bld, 1);
	bld->buf[bld->size++] = val;
}

static void hsd_bld_u16 (hsd_builder_t *bld, u16 val)
{
	hsd_bld_grow (bld, 2);
	bld->buf[bld->size++] = (val >> 8) & 0xff;
	bld->buf[bld->size++] = val & 0xff;
}

static void hsd_bld_u32 (hsd_builder_t *bld, u32 val)
{
	hsd_bld_grow (bld, 4);
	bld->buf[bld->size++] = (val >> 24) & 0xff;
	bld->buf[bld->size++] = (val >> 16) & 0xff;
	bld->buf[bld->size++] = (val >> 8) & 0xff;
	bld->buf[bld->size++] = val & 0xff;
}

static void hsd_bld_f32 (hsd_builder_t *bld, float val)
{
	u32 u;
	memcpy (&u, &val, 4);
	hsd_bld_u32 (bld, u);
}

static void hsd_bld_bytes (hsd_builder_t *bld, const void *data, uint len)
{
	if (len)
	{
		hsd_bld_grow (bld, len);
		memcpy (bld->buf + bld->size, data, len);
		bld->size += len;
	}
}

static void hsd_bld_ptr (hsd_builder_t *bld, u32 target_off)
{
	u32 loc = bld->size;
	if (bld->n_relocs == bld->cap_relocs)
	{
		bld->cap_relocs = bld->cap_relocs ? bld->cap_relocs * 2 : 256;
		bld->relocs = REALLOC (bld->relocs, bld->cap_relocs * sizeof (*bld->relocs));
	}
	bld->relocs[bld->n_relocs++] = loc;
	hsd_bld_u32 (bld, target_off);
}

enumError EncodeModelToHSD (const model_t *model, ccp out_path)
{
	if (!model || !out_path || !model->num_meshes)
		return ERR_NOTHING_TO_DO;

	hsd_builder_t bld;
	memset (&bld, 0, sizeof (bld));

	char out_dir[PATH_MAX] = "";
	ccp slash = strrchr (out_path, '/');
	if (slash)
		snprintf (out_dir, sizeof (out_dir), "%.*s", (int)(slash - out_path + 1), out_path);

	// 1. Textures & Materials
	const uint n_mat = model->num_materials;
	u32 *mat_mobj_offs = CALLOC (n_mat ? n_mat : 1, sizeof (*mat_mobj_offs));

	for (uint m = 0; m < n_mat; m++)
	{
		const material_t *mat = model->materials + m;
		u32 tobj_off = 0;

		// Load first texture layer if present
		if (mat->num_textures > 0 && *mat->textures[0])
		{
			ccp tex_name = mat->textures[0];
			Image_t src;
			InitializeIMG (&src);
			enumError e = LoadPNG (&src, false, false, tex_name, 0);
			if (e > ERR_WARNING && *out_dir)
				e = LoadPNG (&src, false, false, out_dir, tex_name);

			if (e <= ERR_WARNING)
			{
				Image_t dst;
				InitializeIMG (&dst);
				if (ConvertIMG (&dst, false, &src, IMG_RGBA32, PAL_INVALID) <= ERR_WARNING)
				{
					hsd_bld_align (&bld, 32);
					u32 pixel_data_off = hsd_bld_pos (&bld);
					hsd_bld_bytes (&bld, dst.data, dst.data_size);

					hsd_bld_align (&bld, 4);
					u32 img_struct_off = hsd_bld_pos (&bld);
					hsd_bld_ptr (&bld, pixel_data_off);
					hsd_bld_u16 (&bld, dst.width);
					hsd_bld_u16 (&bld, dst.height);
					hsd_bld_u32 (&bld, dst.iform);

					hsd_bld_align (&bld, 4);
					tobj_off = hsd_bld_pos (&bld);
					hsd_bld_u32 (&bld, 0); // class_name
					hsd_bld_u32 (&bld, 0); // next_tobj
					hsd_bld_u32 (&bld, 0); // GXTexMapID (TEXMAP0)
					hsd_bld_u32 (&bld, 4); // coord (GX_TEX0)
					// SRT: rot, scale, trans
					for (int k = 0; k < 3; k++)
						hsd_bld_f32 (&bld, 0.0f);
					hsd_bld_f32 (&bld, 1.0f);
					hsd_bld_f32 (&bld, 1.0f);
					hsd_bld_f32 (&bld, 1.0f);
					hsd_bld_f32 (&bld, 0.0f);
					hsd_bld_f32 (&bld, 0.0f);
					hsd_bld_f32 (&bld, 0.0f);
					hsd_bld_u32 (&bld, mat->wrap_s[0]);
					hsd_bld_u32 (&bld, mat->wrap_t[0]);
					while (hsd_bld_pos (&bld) < tobj_off + HSD_TOBJ_IMAGE_OFF)
						hsd_bld_u8 (&bld, 0);
					hsd_bld_ptr (&bld, img_struct_off);
					hsd_bld_u32 (&bld, 0); // TlutData
					while (hsd_bld_pos (&bld) < tobj_off + 0x5C)
						hsd_bld_u8 (&bld, 0);
				}
				ResetIMG (&dst);
			}
			ResetIMG (&src);
		}

		hsd_bld_align (&bld, 4);
		u32 mat_struct_off = hsd_bld_pos (&bld);
		// ambient RGBA8
		hsd_bld_u8 (&bld,
			(u8)(mat->ambient[0] <= 0	   ? 0
					: mat->ambient[0] >= 1 ? 255
										   : mat->ambient[0] * 255 + 0.5f));
		hsd_bld_u8 (&bld,
			(u8)(mat->ambient[1] <= 0	   ? 0
					: mat->ambient[1] >= 1 ? 255
										   : mat->ambient[1] * 255 + 0.5f));
		hsd_bld_u8 (&bld,
			(u8)(mat->ambient[2] <= 0	   ? 0
					: mat->ambient[2] >= 1 ? 255
										   : mat->ambient[2] * 255 + 0.5f));
		hsd_bld_u8 (&bld, 255);
		// diffuse RGBA8
		hsd_bld_u8 (&bld,
			(u8)(mat->diffuse[0] <= 0	   ? 0
					: mat->diffuse[0] >= 1 ? 255
										   : mat->diffuse[0] * 255 + 0.5f));
		hsd_bld_u8 (&bld,
			(u8)(mat->diffuse[1] <= 0	   ? 0
					: mat->diffuse[1] >= 1 ? 255
										   : mat->diffuse[1] * 255 + 0.5f));
		hsd_bld_u8 (&bld,
			(u8)(mat->diffuse[2] <= 0	   ? 0
					: mat->diffuse[2] >= 1 ? 255
										   : mat->diffuse[2] * 255 + 0.5f));
		hsd_bld_u8 (&bld,
			(u8)(mat->diffuse[3] <= 0	   ? 0
					: mat->diffuse[3] >= 1 ? 255
										   : mat->diffuse[3] * 255 + 0.5f));
		// specular RGBA8
		hsd_bld_u8 (&bld,
			(u8)(mat->specular[0] <= 0		? 0
					: mat->specular[0] >= 1 ? 255
											: mat->specular[0] * 255 + 0.5f));
		hsd_bld_u8 (&bld,
			(u8)(mat->specular[1] <= 0		? 0
					: mat->specular[1] >= 1 ? 255
											: mat->specular[1] * 255 + 0.5f));
		hsd_bld_u8 (&bld,
			(u8)(mat->specular[2] <= 0		? 0
					: mat->specular[2] >= 1 ? 255
											: mat->specular[2] * 255 + 0.5f));
		hsd_bld_u8 (&bld, 255);
		// alpha & shininess f32
		hsd_bld_f32 (&bld, mat->diffuse[3] > 0 ? mat->diffuse[3] : 1.0f);
		hsd_bld_f32 (&bld, mat->shininess > 0 ? mat->shininess : 50.0f);

		hsd_bld_align (&bld, 4);
		u32 mobj_off = hsd_bld_pos (&bld);
		hsd_bld_u32 (&bld, 0); // class_name
		hsd_bld_u32 (&bld, 0x40004); // rendermode
		if (tobj_off)
			hsd_bld_ptr (&bld, tobj_off);
		else
			hsd_bld_u32 (&bld, 0);
		hsd_bld_ptr (&bld, mat_struct_off);
		hsd_bld_u32 (&bld, 0); // renderdesc
		hsd_bld_u32 (&bld, 0); // pedesc

		mat_mobj_offs[m] = mobj_off;
	}

	// 2. Meshes -> Buffers, Attributes, DL, POBJ, DOBJ
	const uint nm = model->num_meshes;
	u32 *mesh_dobj_offs = CALLOC (nm ? nm : 1, sizeof (*mesh_dobj_offs));

	for (uint m = 0; m < nm; m++)
	{
		const mesh_t *mesh = model->meshes + m;

		// Position buffer (f32)
		hsd_bld_align (&bld, 32);
		u32 pos_buf_off = hsd_bld_pos (&bld);
		for (uint i = 0; i < mesh->num_positions; i++)
		{
			hsd_bld_f32 (&bld, mesh->positions[i].x);
			hsd_bld_f32 (&bld, mesh->positions[i].y);
			hsd_bld_f32 (&bld, mesh->positions[i].z);
		}

		// Normal buffer (f32)
		u32 nrm_buf_off = 0;
		if (mesh->num_normals > 0)
		{
			hsd_bld_align (&bld, 32);
			nrm_buf_off = hsd_bld_pos (&bld);
			for (uint i = 0; i < mesh->num_normals; i++)
			{
				hsd_bld_f32 (&bld, mesh->normals[i].x);
				hsd_bld_f32 (&bld, mesh->normals[i].y);
				hsd_bld_f32 (&bld, mesh->normals[i].z);
			}
		}

		// UV buffer (f32)
		u32 uv_buf_off = 0;
		if (mesh->num_texcoords > 0)
		{
			hsd_bld_align (&bld, 32);
			uv_buf_off = hsd_bld_pos (&bld);
			for (uint i = 0; i < mesh->num_texcoords; i++)
			{
				hsd_bld_f32 (&bld, mesh->texcoords[i].u);
				hsd_bld_f32 (&bld, 1.0f - mesh->texcoords[i].v);
			}
		}

		// Color buffer (RGBA8)
		u32 clr_buf_off = 0;
		if (mesh->num_colors[0] > 0)
		{
			hsd_bld_align (&bld, 32);
			clr_buf_off = hsd_bld_pos (&bld);
			for (uint i = 0; i < mesh->num_colors[0]; i++)
			{
				color4_t c = mesh->colors[0][i];
				hsd_bld_u8 (&bld, (u8)(c.r <= 0 ? 0 : c.r >= 1 ? 255 : c.r * 255 + 0.5f));
				hsd_bld_u8 (&bld, (u8)(c.g <= 0 ? 0 : c.g >= 1 ? 255 : c.g * 255 + 0.5f));
				hsd_bld_u8 (&bld, (u8)(c.b <= 0 ? 0 : c.b >= 1 ? 255 : c.b * 255 + 0.5f));
				hsd_bld_u8 (&bld, (u8)(c.a <= 0 ? 0 : c.a >= 1 ? 255 : c.a * 255 + 0.5f));
			}
		}

		const bool use_idx16 = mesh->num_positions > 256 || mesh->num_normals > 256
			|| mesh->num_texcoords > 256 || mesh->num_colors[0] > 256;
		const u32 itype = use_idx16 ? HSD_GX_INDEX16 : HSD_GX_INDEX8;

		// GX_Attribute array
		hsd_bld_align (&bld, 4);
		u32 attr_arr_off = hsd_bld_pos (&bld);

		// POS attribute
		hsd_bld_u32 (&bld, HSD_GX_VA_POS);
		hsd_bld_u32 (&bld, itype);
		hsd_bld_u32 (&bld, 1); // 1 = POS_XYZ
		hsd_bld_u32 (&bld, HSD_GX_F32);
		hsd_bld_u8 (&bld, 0); // scale
		hsd_bld_u8 (&bld, 0); // pad
		hsd_bld_u16 (&bld, 12); // stride
		hsd_bld_ptr (&bld, pos_buf_off);

		// NRM attribute
		if (nrm_buf_off)
		{
			hsd_bld_u32 (&bld, HSD_GX_VA_NRM);
			hsd_bld_u32 (&bld, itype);
			hsd_bld_u32 (&bld, 0); // 0 = NRM
			hsd_bld_u32 (&bld, HSD_GX_F32);
			hsd_bld_u8 (&bld, 0);
			hsd_bld_u8 (&bld, 0);
			hsd_bld_u16 (&bld, 12);
			hsd_bld_ptr (&bld, nrm_buf_off);
		}

		// CLR0 attribute
		if (clr_buf_off)
		{
			hsd_bld_u32 (&bld, HSD_GX_VA_CLR0);
			hsd_bld_u32 (&bld, itype);
			hsd_bld_u32 (&bld, 0);
			hsd_bld_u32 (&bld, 5); // 5 = RGBA8
			hsd_bld_u8 (&bld, 0);
			hsd_bld_u8 (&bld, 0);
			hsd_bld_u16 (&bld, 4);
			hsd_bld_ptr (&bld, clr_buf_off);
		}

		// TEX0 attribute
		if (uv_buf_off)
		{
			hsd_bld_u32 (&bld, HSD_GX_VA_TEX0);
			hsd_bld_u32 (&bld, itype);
			hsd_bld_u32 (&bld, 1); // 1 = TEX_ST
			hsd_bld_u32 (&bld, HSD_GX_F32);
			hsd_bld_u8 (&bld, 0);
			hsd_bld_u8 (&bld, 0);
			hsd_bld_u16 (&bld, 8);
			hsd_bld_ptr (&bld, uv_buf_off);
		}

		// Terminate GX_Attribute array
		hsd_bld_u32 (&bld, HSD_GX_VA_NULL);
		hsd_bld_u32 (&bld, 0);
		hsd_bld_u32 (&bld, 0);
		hsd_bld_u32 (&bld, 0);
		hsd_bld_u8 (&bld, 0);
		hsd_bld_u8 (&bld, 0);
		hsd_bld_u16 (&bld, 0);
		hsd_bld_u32 (&bld, 0);

		// Display List (GX_DRAW_TRIANGLES)
		hsd_bld_align (&bld, 32);
		u32 dl_off = hsd_bld_pos (&bld);

		uint v_remain = (uint)mesh->num_vertices;
		uint v_pos = 0;
		while (v_remain > 0)
		{
			uint v_chunk = v_remain > 3072 ? 3072 : v_remain;
			hsd_bld_u8 (&bld, 0x90); // GX_DRAW_TRIANGLES
			hsd_bld_u16 (&bld, (u16)v_chunk);

			for (uint v = 0; v < v_chunk; v++, v_pos++)
			{
				const vertex_t *vx = mesh->vertices + v_pos;
				if (use_idx16)
					hsd_bld_u16 (&bld, (u16)vx->position_idx);
				else
					hsd_bld_u8 (&bld, (u8)vx->position_idx);

				if (nrm_buf_off)
				{
					int nidx = vx->normal_idx >= 0 ? vx->normal_idx : 0;
					if (use_idx16)
						hsd_bld_u16 (&bld, (u16)nidx);
					else
						hsd_bld_u8 (&bld, (u8)nidx);
				}
				if (clr_buf_off)
				{
					int cidx = vx->color_idx[0] >= 0 ? vx->color_idx[0] : 0;
					if (use_idx16)
						hsd_bld_u16 (&bld, (u16)cidx);
					else
						hsd_bld_u8 (&bld, (u8)cidx);
				}
				if (uv_buf_off)
				{
					int uidx = vx->texcoord_idx >= 0 ? vx->texcoord_idx : 0;
					if (use_idx16)
						hsd_bld_u16 (&bld, (u16)uidx);
					else
						hsd_bld_u8 (&bld, (u8)uidx);
				}
			}
			v_remain -= v_chunk;
		}
		hsd_bld_align (&bld, 32);
		s16 dl_words = (s16)((hsd_bld_pos (&bld) - dl_off) / 32);

		// HSD_POBJ (0x18 bytes)
		hsd_bld_align (&bld, 4);
		u32 pobj_off = hsd_bld_pos (&bld);
		hsd_bld_u32 (&bld, 0); // class_name
		hsd_bld_u32 (&bld, 0); // next_pobj
		hsd_bld_ptr (&bld, attr_arr_off);
		hsd_bld_u16 (&bld, 0); // flags (HSD_POBJ_SKIN)
		hsd_bld_u16 (&bld, (u16)dl_words);
		hsd_bld_ptr (&bld, dl_off);
		hsd_bld_u32 (&bld, 0); // union

		// HSD_DOBJ (0x10 bytes)
		hsd_bld_align (&bld, 4);
		u32 dobj_off = hsd_bld_pos (&bld);
		hsd_bld_u32 (&bld, 0); // class_name
		hsd_bld_u32 (&bld, 0); // next_dobj
		u32 mobj_off = (mesh->material_idx >= 0 && (uint)mesh->material_idx < n_mat)
			? mat_mobj_offs[mesh->material_idx]
			: (n_mat ? mat_mobj_offs[0] : 0);
		if (mobj_off)
			hsd_bld_ptr (&bld, mobj_off);
		else
			hsd_bld_u32 (&bld, 0);
		hsd_bld_ptr (&bld, pobj_off);

		mesh_dobj_offs[m] = dobj_off;
	}

	// Link DOBJs in chain if multiple meshes
	for (uint m = 0; m + 1 < nm; m++)
	{
		u32 loc = mesh_dobj_offs[m] + 0x04;
		u32 target = mesh_dobj_offs[m + 1];
		bld.buf[loc] = (target >> 24) & 0xff;
		bld.buf[loc + 1] = (target >> 16) & 0xff;
		bld.buf[loc + 2] = (target >> 8) & 0xff;
		bld.buf[loc + 3] = target & 0xff;
		if (bld.n_relocs == bld.cap_relocs)
		{
			bld.cap_relocs = bld.cap_relocs ? bld.cap_relocs * 2 : 256;
			bld.relocs = REALLOC (bld.relocs, bld.cap_relocs * sizeof (*bld.relocs));
		}
		bld.relocs[bld.n_relocs++] = loc;
	}

	// 3. Root JOBJ (0x40 bytes)
	hsd_bld_align (&bld, 4);
	u32 root_jobj_off = hsd_bld_pos (&bld);
	hsd_bld_u32 (&bld, 0); // class_name
	hsd_bld_u32 (&bld, 0x80000010); // flags (ROOT|USER)
	hsd_bld_u32 (&bld, 0); // child
	hsd_bld_u32 (&bld, 0); // next
	if (nm)
		hsd_bld_ptr (&bld, mesh_dobj_offs[0]);
	else
		hsd_bld_u32 (&bld, 0);
	// RX, RY, RZ
	hsd_bld_f32 (&bld, 0.0f);
	hsd_bld_f32 (&bld, 0.0f);
	hsd_bld_f32 (&bld, 0.0f);
	// SX, SY, SZ
	hsd_bld_f32 (&bld, 1.0f);
	hsd_bld_f32 (&bld, 1.0f);
	hsd_bld_f32 (&bld, 1.0f);
	// TX, TY, TZ
	hsd_bld_f32 (&bld, 0.0f);
	hsd_bld_f32 (&bld, 0.0f);
	hsd_bld_f32 (&bld, 0.0f);
	hsd_bld_u32 (&bld, 0); // inv_world
	hsd_bld_u32 (&bld, 0); // robj

	// 4. Relocation Table (sorted in ascending order)
	qsort (bld.relocs, bld.n_relocs, sizeof (u32), cmp_u32);
	hsd_bld_align (&bld, 4);
	u32 reloc_table_off = hsd_bld_pos (&bld);
	for (uint i = 0; i < bld.n_relocs; i++)
		hsd_bld_u32 (&bld, bld.relocs[i]);

	// 5. Root Table: (root_jobj_off, root_name_off)
	u32 root_table_off = hsd_bld_pos (&bld);
	u32 root_name_off = root_table_off + 8;
	hsd_bld_u32 (&bld, root_jobj_off);
	hsd_bld_u32 (&bld, root_name_off);

	// 6. String Pool
	hsd_bld_bytes (&bld, "TopN_joint\0", 11);
	hsd_bld_align (&bld, 4);

	// 7. Full File Buffer with 0x20 Header
	u32 file_size = 0x20 + bld.size;
	u8 *file_buf = CALLOC (file_size, 1);
	// Header
	file_buf[0] = (file_size >> 24) & 0xff;
	file_buf[1] = (file_size >> 16) & 0xff;
	file_buf[2] = (file_size >> 8) & 0xff;
	file_buf[3] = file_size & 0xff;
	file_buf[4] = (reloc_table_off >> 24) & 0xff;
	file_buf[5] = (reloc_table_off >> 16) & 0xff;
	file_buf[6] = (reloc_table_off >> 8) & 0xff;
	file_buf[7] = reloc_table_off & 0xff;
	file_buf[8] = (bld.n_relocs >> 24) & 0xff;
	file_buf[9] = (bld.n_relocs >> 16) & 0xff;
	file_buf[10] = (bld.n_relocs >> 8) & 0xff;
	file_buf[11] = bld.n_relocs & 0xff;
	file_buf[12] = 0;
	file_buf[13] = 0;
	file_buf[14] = 0;
	file_buf[15] = 1; // root_count = 1
	file_buf[16] = 0;
	file_buf[17] = 0;
	file_buf[18] = 0;
	file_buf[19] = 0; // ref_count = 0
	memcpy (file_buf + 0x14, "001B", 4);

	memcpy (file_buf + 0x20, bld.buf, bld.size);

	enumError rc = SaveFILE (out_path, 0, true, file_buf, file_size, 0);

	FREE (file_buf);
	FREE (bld.buf);
	FREE (bld.relocs);
	FREE (mat_mobj_offs);
	FREE (mesh_dobj_offs);
	return rc;
}

///////////////////////////////////////////////////////////////////////////////
