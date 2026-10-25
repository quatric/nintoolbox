// SPDX-License-Identifier: GPL-2.0+
// Metroid Prime Hunters (Nintendo DS) archives and models, after MphRead
// (NoneGiven): Formats/RawFormats.cs, Read.cs (ReadModel, GetTextureData,
// GetPaletteData) and Utility/Archive.cs.
#include "lib-mph.h"
#include "lib-image.h"
#include "lib-nsbmd.h"
#include "lib-std.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

// Model memory is released by FreeModel with libc free(), as in lib-nsbmd.c.
#undef calloc
#undef free

//-----------------------------------------------------------------------------
// SNDFILE archive
//-----------------------------------------------------------------------------

#define MPH_AR_HDR 32
#define MPH_AR_REC 64

bool IsMphArchive (const u8 *data, uint size)
{
	if (!data || size < MPH_AR_HDR + MPH_AR_REC || memcmp (data, "SNDFILE\0", 8))
		return false;
	const u32 count = rd_be32 (data + 8);
	const u32 total = rd_be32 (data + 12);
	if (!count || count > 0xffff || total > size || total < MPH_AR_HDR + MPH_AR_REC * count)
		return false;
	return true;
}

enumError ScanMphArchive (
	nintendo_sarc_entry_t **entries, uint *n_entries, const u8 *data, uint size)
{
	if (!entries || !n_entries || !IsMphArchive (data, size))
		return ERR_INVALID_DATA;
	*entries = 0;
	*n_entries = 0;
	const u32 count = rd_be32 (data + 8);
	const u32 total = rd_be32 (data + 12);

	nintendo_sarc_entry_t *out = CALLOC (count, sizeof (*out));
	if (!out)
		return ERR_CANT_CREATE;
	uint n = 0;
	for (u32 i = 0; i < count; i++)
	{
		const u8 *rec = data + MPH_AR_HDR + (size_t)i * MPH_AR_REC;
		const u32 off = rd_be32 (rec + 32);
		const u32 fsize = rd_be32 (rec + 40);
		if (off < MPH_AR_HDR || (u64)off + fsize > total)
			continue;
		char name[33];
		memcpy (name, rec, 32);
		name[32] = 0;
		if (!name[0] || !OwnedNameOk (name))
			snprintf (name, sizeof (name), "%05u.bin", i);
		if (OwnedEntryAdd (out, n, name, data + off, fsize))
			n++;
	}
	if (!n)
	{
		FREE (out);
		return ERR_NOTHING_TO_DO;
	}
	*entries = out;
	*n_entries = n;
	return ERR_OK;
}

//-----------------------------------------------------------------------------
// Model
//-----------------------------------------------------------------------------

#define H_SCALE_FACTOR 0x00
#define H_SCALE_BASE 0x04
#define H_MATERIAL_OFF 0x10
#define H_DLIST_OFF 0x14
#define H_NODE_OFF 0x18
#define H_FLAGS 0x1e
#define H_MESH_OFF 0x24
#define H_TEX_COUNT 0x28
#define H_TEX_OFF 0x2c
#define H_PAL_COUNT 0x30
#define H_PAL_OFF 0x34
#define H_MATERIAL_COUNT 0x48
#define H_NODE_COUNT 0x4a
#define H_MESH_COUNT 0x60
#define MPH_HDR 100
#define MPH_NODE 240
#define MPH_MAT 132
#define MPH_DLIST 32
#define MPH_TEX 40
#define MPH_PAL 16

static bool mph_range (uint size, u32 off, u32 count, u32 stride)
{
	return !count || ((u64)off + (u64)count * stride <= size && off >= MPH_HDR);
}

bool IsMphModel (const u8 *data, uint size)
{
	if (!data || size < MPH_HDR + MPH_MAT)
		return false;
	// ScaleFactor is a shift, ScaleBase a positive 20.12 fixed point value.
	if (rd_le32 (data + H_SCALE_FACTOR) > 16)
		return false;
	const s32 base = (s32)rd_le32 (data + H_SCALE_BASE);
	if (base <= 0 || base > 0x100000)
		return false;
	// The first four fields of the counts and offsets a model cannot lack.
	const uint mats = rd_le16 (data + H_MATERIAL_COUNT);
	const uint nodes = rd_le16 (data + H_NODE_COUNT);
	const uint meshes = rd_le16 (data + H_MESH_COUNT);
	const uint texs = rd_le16 (data + H_TEX_COUNT);
	const uint pals = rd_le16 (data + H_PAL_COUNT);
	if (!meshes || !mats || !nodes || data[H_FLAGS])
		return false;
	if (!mph_range (size, rd_le32 (data + H_MATERIAL_OFF), mats, MPH_MAT)
		|| !mph_range (size, rd_le32 (data + H_NODE_OFF), nodes, MPH_NODE)
		|| !mph_range (size, rd_le32 (data + H_MESH_OFF), meshes, 4)
		|| !mph_range (size, rd_le32 (data + H_DLIST_OFF), meshes, MPH_DLIST)
		|| !mph_range (size, rd_le32 (data + H_TEX_OFF), texs, MPH_TEX)
		|| !mph_range (size, rd_le32 (data + H_PAL_OFF), pals, MPH_PAL))
		return false;
	// Every display list lies inside the file and is a whole number of words.
	const u8 *dl = data + rd_le32 (data + H_DLIST_OFF);
	for (uint i = 0; i < meshes; i++)
	{
		const u32 off = rd_le32 (dl + i * MPH_DLIST);
		const u32 len = rd_le32 (dl + i * MPH_DLIST + 4);
		if ((u64)off + len > size || (len & 3) || off < MPH_HDR)
			return false;
	}
	// Meshes name existing materials and display lists.
	const u8 *mesh = data + rd_le32 (data + H_MESH_OFF);
	for (uint i = 0; i < meshes; i++)
		if (rd_le16 (mesh + i * 4) >= mats || rd_le16 (mesh + i * 4 + 2) >= meshes)
			return false;
	// Material names are printable text.
	const u8 *mat = data + rd_le32 (data + H_MATERIAL_OFF);
	for (uint i = 0; i < mats; i++)
		for (uint c = 0; c < 64; c++)
		{
			const u8 ch = mat[i * MPH_MAT + c];
			if (!ch)
				break;
			if (ch < 0x20 || ch > 0x7e)
				return false;
		}
	return true;
}

typedef struct
{
	u8 *rgba;
	uint w, h;
	bool alpha; // any pixel not fully opaque
	char name[64];
} mph_img_t;

static float mph_fx12 (s32 v)
{
	return (float)v / 4096.0f;
}

static u8 mph_5to8 (uint v)
{
	return (u8)((v << 3) | (v >> 2));
}

// One texture + palette pair as RGBA. Same index and alpha rules as MphRead's
// GetTextureData; palette entries are RGB555 with red in the low bits.
static bool mph_decode_texture (mph_img_t *img, const u8 *tdata, uint tsize, const u8 *trec,
	const u8 *pdata, uint psize, const u8 *prec)
{
	const uint fmt = trec[0];
	const uint w = rd_le16 (trec + 2);
	const uint h = rd_le16 (trec + 4);
	const u32 img_off = rd_le32 (trec + 8);
	const bool opaque = rd_le32 (trec + 28) != 0;
	if (!w || !h || w > 2048 || h > 2048)
		return false;
	const size_t px = (size_t)w * h;
	const uint bpp = fmt == 0 ? 2 : fmt == 1 ? 4 : fmt == 5 ? 16 : 8;
	if (fmt == 3 || fmt == 7 || fmt > 6 || (px * bpp) / 8 + img_off > tsize)
		return false;

	u16 pal[256];
	memset (pal, 0, sizeof (pal));
	if (fmt != 5 && prec)
	{
		const u32 poff = rd_le32 (prec);
		u32 plen = rd_le32 (prec + 4) / 2;
		if (plen > 256)
			plen = 256;
		if ((u64)poff + plen * 2 > psize)
			return false;
		for (u32 i = 0; i < plen; i++)
			pal[i] = rd_le16 (pdata + poff + i * 2);
	}

	img->w = w;
	img->h = h;
	img->rgba = CALLOC (px, 4);
	if (!img->rgba)
		return false;
	const u8 *src = tdata + img_off;
	for (size_t i = 0; i < px; i++)
	{
		uint idx = 0, alpha = 255;
		u16 col;
		if (fmt == 5)
		{
			col = rd_le16 (src + i * 2);
			alpha = col & 0x8000 ? 255 : 0;
		}
		else
		{
			if (fmt == 0)
				idx = (src[i >> 2] >> ((i & 3) * 2)) & 3;
			else if (fmt == 1)
				idx = (src[i >> 1] >> ((i & 1) * 4)) & 15;
			else if (fmt == 2)
				idx = src[i];
			else if (fmt == 4)
			{
				idx = src[i] & 7;
				alpha = (src[i] >> 3) * 255 / 31;
			}
			else
			{
				idx = src[i] & 31;
				alpha = (src[i] >> 5) * 255 / 7;
			}
			if (fmt <= 2 && !opaque && !idx)
				alpha = 0;
			col = pal[idx];
		}
		u8 *o = img->rgba + i * 4;
		o[0] = mph_5to8 (col & 31);
		o[1] = mph_5to8 ((col >> 5) & 31);
		o[2] = mph_5to8 ((col >> 10) & 31);
		o[3] = (u8)alpha;
		if (alpha != 255)
			img->alpha = true;
	}
	return true;
}

static void mph_save_png (mph_img_t *img, ccp path)
{
	Image_t out;
	InitializeIMG (&out);
	const uint xw = EXPAND8 (img->w), xh = EXPAND8 (img->h);
	u8 *buf = img->rgba;
	if (xw != img->w || xh != img->h)
	{
		buf = CALLOC (1, (size_t)xw * xh * 4);
		if (!buf)
			return;
		for (uint y = 0; y < img->h; y++)
			memcpy (buf + (size_t)y * xw * 4, img->rgba + (size_t)y * img->w * 4, (size_t)img->w * 4);
	}
	out.data = buf;
	out.data_alloced = buf != img->rgba;
	out.data_size = xw * xh * 4;
	out.width = img->w;
	out.xwidth = xw;
	out.height = img->h;
	out.xheight = xh;
	out.iform = out.info_iform = IMG_X_RGB;
	out.info_fform = FF_PNG;
	out.info_n_image = 1;
	out.endian = &le_func;
	SavePNG (&out, false, 0, path, 0, 0, true, 0);
	if (out.data_alloced)
		ResetIMG (&out);
}

model_t *ParseMphModel (const u8 *data, uint size, const u8 *tex, uint tex_size,
	ccp out_png_dir, ccp stem)
{
	if (!data || !IsMphModel (data, size))
		return 0;
	model_t *model = calloc (1, sizeof (*model));
	if (!model)
		return 0;

	const uint n_mat = rd_le16 (data + H_MATERIAL_COUNT);
	const uint n_mesh = rd_le16 (data + H_MESH_COUNT);
	const float scale = mph_fx12 ((s32)rd_le32 (data + H_SCALE_BASE))
		* (float)(1u << rd_le32 (data + H_SCALE_FACTOR));
	const u8 *mat = data + rd_le32 (data + H_MATERIAL_OFF);
	const u8 *mesh = data + rd_le32 (data + H_MESH_OFF);
	const u8 *dl = data + rd_le32 (data + H_DLIST_OFF);

	// Textures come from the companion file when the model has none (or when
	// the caller supplies one, as MPH does for recolors).
	const u8 *tsrc = data;
	uint tsrc_size = size;
	if (tex && tex_size >= MPH_HDR)
	{
		tsrc = tex;
		tsrc_size = tex_size;
	}
	const uint n_tex = rd_le16 (tsrc + H_TEX_COUNT);
	const uint n_pal = rd_le16 (tsrc + H_PAL_COUNT);
	const u8 *trecs = tsrc + rd_le32 (tsrc + H_TEX_OFF);
	const u8 *precs = tsrc + rd_le32 (tsrc + H_PAL_OFF);
	const bool tex_ok = tsrc_size >= MPH_HDR && mph_range (tsrc_size, rd_le32 (tsrc + H_TEX_OFF), n_tex, MPH_TEX)
		&& mph_range (tsrc_size, rd_le32 (tsrc + H_PAL_OFF), n_pal, MPH_PAL);

	model->materials = calloc (n_mat, sizeof (*model->materials));
	if (!model->materials)
	{
		FreeModel (model);
		return 0;
	}
	model->num_materials = n_mat;
	uint tw[1024], th[1024];
	memset (tw, 0, sizeof (tw));
	memset (th, 0, sizeof (th));
	for (uint i = 0; i < n_mat; i++)
	{
		const u8 *m = mat + i * MPH_MAT;
		material_t *mt = model->materials + i;
		snprintf (mt->name, sizeof (mt->name), "%.63s", (ccp)m);
		if (!mt->name[0])
			snprintf (mt->name, sizeof (mt->name), "material%u", i);
		mt->diffuse[0] = m[0x4a] / 255.0f;
		mt->diffuse[1] = m[0x4b] / 255.0f;
		mt->diffuse[2] = m[0x4c] / 255.0f;
		mt->diffuse[3] = 1.0f;
		if (mt->diffuse[0] + mt->diffuse[1] + mt->diffuse[2] == 0.0f)
			mt->diffuse[0] = mt->diffuse[1] = mt->diffuse[2] = 0.8f;
		mt->has_alpha = m[0x42] < 31;
		const int tid = (s16)rd_le16 (m + 0x46);
		const int pid = (s16)rd_le16 (m + 0x44);
		if (tex_ok && tid >= 0 && (uint)tid < n_tex)
		{
			const u8 *prec = pid >= 0 && (uint)pid < n_pal ? precs + pid * MPH_PAL : 0;
			mph_img_t img;
			memset (&img, 0, sizeof (img));
			if (mph_decode_texture (&img, tsrc, tsrc_size, trecs + tid * MPH_TEX, tsrc, tsrc_size, prec))
			{
				snprintf (mt->textures[0], sizeof (mt->textures[0]), "%s_t%d_p%d.png",
					stem && stem[0] ? stem : "mph", tid, pid);
				mt->num_textures = 1;
				mt->wrap_s[0] = m[0x48] == 1 ? 1 : m[0x48] == 2 ? 2 : 0;
				mt->wrap_t[0] = m[0x49] == 1 ? 1 : m[0x49] == 2 ? 2 : 0;
				mt->min_filter[0] = mt->mag_filter[0] = 0;
				if (i < 1024)
				{
					tw[i] = img.w;
					th[i] = img.h;
				}
				if (img.alpha)
					mt->has_alpha = 1;
				if (out_png_dir && out_png_dir[0])
				{
					char path[PATH_MAX];
					snprintf (path, sizeof (path), "%s/%s", out_png_dir, mt->textures[0]);
					mph_save_png (&img, path);
				}
			}
			FREE (img.rgba);
		}
	}

	for (uint i = 0; i < n_mesh; i++)
	{
		const uint mat_id = rd_le16 (mesh + i * 4);
		const uint dl_id = rd_le16 (mesh + i * 4 + 2);
		const u32 off = rd_le32 (dl + dl_id * MPH_DLIST);
		const u32 len = rd_le32 (dl + dl_id * MPH_DLIST + 4);
		char name[64];
		snprintf (name, sizeof (name), "mesh%u", i);
		const int idx = AppendDSGXMesh (
			model, data + off, len, name, mat_id < 1024 ? tw[mat_id] : 0, mat_id < 1024 ? th[mat_id] : 0);
		if (idx < 0)
			continue;
		mesh_t *m = model->meshes + idx;
		m->material_idx = (int)mat_id;
		for (size_t p = 0; p < m->num_positions; p++)
		{
			m->positions[p].x *= scale;
			m->positions[p].y *= scale;
			m->positions[p].z *= scale;
		}
	}
	if (!model->num_meshes)
	{
		FreeModel (model);
		return 0;
	}
	return model;
}

enumError DecodeMphModel (const u8 *data, uint size, const u8 *tex, uint tex_size, ccp out_path)
{
	if (!data || !out_path)
		return ERR_INVALID_DATA;
	char dir[PATH_MAX], stem[64];
	snprintf (dir, sizeof (dir), "%s", out_path);
	char *slash = strrchr (dir, '/');
	if (slash)
		*slash = 0;
	else
		snprintf (dir, sizeof (dir), ".");
	ccp base = strrchr (out_path, '/');
	base = base ? base + 1 : out_path;
	snprintf (stem, sizeof (stem), "%s", base);
	char *dot = strrchr (stem, '.');
	if (dot)
		*dot = 0;

	model_t *model = ParseMphModel (data, size, tex, tex_size, dir, stem);
	if (!model)
		return ERR_INVALID_DATA;
	const int rc = ExportModelToGLB (model, out_path);
	FreeModel (model);
	return rc == 0 ? ERR_OK : ERR_CANT_CREATE;
}

u8 *LoadMphTexFile (ccp model_path, uint *size)
{
	*size = 0;
	const size_t len = model_path ? strlen (model_path) : 0;
	static const char model_tail[] = "_Model.bin";
	const size_t tail = sizeof (model_tail) - 1;
	if (len <= tail || strcasecmp (model_path + len - tail, model_tail) || len + 8 > PATH_MAX)
		return 0;
	char path[PATH_MAX];
	snprintf (path, sizeof (path), "%.*s_Tex.bin", (int)(len - tail), model_path);
	FILE *f = fopen (path, "rb");
	if (!f)
		return 0;
	fseek (f, 0, SEEK_END);
	const long n = ftell (f);
	fseek (f, 0, SEEK_SET);
	u8 *buf = n >= MPH_HDR && n < 0x4000000 ? MALLOC (n) : 0;
	if (buf && fread (buf, 1, n, f) != (size_t)n)
	{
		FREE (buf);
		buf = 0;
	}
	fclose (f);
	if (buf)
		*size = (uint)n;
	return buf;
}
