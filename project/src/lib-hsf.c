// SPDX-License-Identifier: GPL-2.0+
#include "lib-std.h"
#include "lib-hsf.h"
#include "lib-model-glb.h"
#include "lib-excite.h"
#include "lib-image.h"

//-----------------------------------------------------------------------------
///////////////		HSFV037 model decode				///////////////
//-----------------------------------------------------------------------------
//
// HSFV037 is HAL Laboratory's tool-export model format (their "sysdolphin"
// GX runtime -- the same JObj/DObj/PObj/MObj/TObj object model documented in
// the public Kirby Air Ride decompilation, doldecomp/kar). An earlier pass
// through this file correctly found the flat top-level table right after
// the magic (20 entries of { u32 offset; u32 count; }, big-endian, starting
// at file offset 8) and correctly identified table index 4 = vertex
// positions, index 7 = triangle faces -- but wrongly guessed that a
// count > 1 meant "N repeated raw data blocks whose sizes aren't in the top
// table" and gave up on that case (ERR_NOTHING_TO_DO), which is every
// multi-part / skinned retail character model.
//
// What was actually missing, found by porting the field layout of
// Ploaj/Metanoia's open-source, independently-working C# HSF reader
// (Metanoia/Formats/GameCube/HSF.cs, MIT-licensed 3D-model conversion
// tool): table indices 4 (positions), 5 (normals), 6 (UVs) and 7
// (primitives) don't point straight at a data block -- they point at an
// array of `count` 12-byte AttributeHeader records { u32 name_str_off;
// u32 data_count; u32 data_off (relative to the end of this very header
// array) }, ONE PER NAMED MESH-OBJECT. So "count > 1" means "N
// independently-named mesh parts", not "N repeated blocks" -- this *is*
// the multi-part structure, just addressed indirectly through per-part
// headers instead of raw sizes in the top table.
//
// This reconciles cleanly with what the old count==1 code already had
// right: for a single-part file, the position table is *still* an
// AttributeHeader array, just of length 1 -- and its one header's 12 bytes
// (name_off, data_count, data_off=20 in every sample seen) plus 20 bytes of
// slack sum to exactly the "32-byte sub-header" the old code assumed. Same
// story for the "16-byte face sub-header": a length-1 AttributeHeader array
// over the primitive table. So the byte layout the old code walked was
// real; it just wasn't generalised to N>1.
//
// Verified against REAL retail data, not just the reference reader's
// say-so: Mario Party 4 board-piece and character (Luigi, Daisy) .hsf
// models extracted from a legitimately-owned disc dump in a prior session
// (kept as tests/fixtures/hsf_multipart_test.hsf, one small board-piece
// model, for regression). For that file and others inspected, the
// AttributeHeader math above lines up exactly with independently-derived
// section boundaries (e.g. one mesh's primitive-table byte length lands
// precisely on the next mesh's recorded data_off). The Luigi sample
// (13 named mesh parts) round-trips through this decoder to a 13-mesh DAE.
//
// Primitive records are `s16 type; s16 material&0xff;` followed by a
// VertexGroup[3] (type 2, triangle) or VertexGroup[4] (type 3, quad; two
// triangles 0-1-2 and 1-3-2) of 4x big-endian s16 each
// {position_idx,normal_idx,color_idx,uv_idx}, then 3x f32 (unused here);
// fixed 48-byte stride either way (confirmed: a mesh's primitive-count *
// 48 exactly matches the offset gap to the next mesh's data in every real
// sample checked). Type 4 (indexed triangle-strip: VertexGroup[3] + s32
// extra_count + u32 extra_offset into a shared Ext-VertexGroup pool right
// after ALL primitive-table data) IS implemented and was NOT hypothetical
// -- it's what real character models actually use for skinned parts (a
// retail Luigi model's eyelid mesh, and a board-piece "obj1" mesh, both
// use it and were rejected before this was added). The corner sequence
// emitted for a type-4 record is [v0,v1,v2,v1,ext0,ext1,...] fan/strip-
// triangulated with alternating winding, matching the reference decoder.
// Any primitive type other than 2/3/4 still aborts the whole file with
// ERR_NOTHING_TO_DO rather than guessing further.
//
// Normals have TWO on-disk variants, and this was only discovered by
// checking a real file byte-for-byte (not obvious from the reference
// reader alone): the common case is 3x big-endian f32 XYZ (12 bytes each).
// But some real sections instead pack each normal as 3 SIGNED BYTES
// (value/127.0), 0x20-byte aligned. Detected exactly as Metanoia's reader
// does: with >=2 per-mesh normal headers, if header[1]'s data_off equals
// header[0]'s byte-packed end rounded up to 0x20, the whole table is
// byte-packed. Confirmed on a real board-piece .hsf where treating it as
// f32 produced non-finite (NaN/Inf) normal values -- i.e. this branch is
// not a hypothetical, it fires on real retail data.
//
// A second pass cross-checked Hudson's GPL hsfview runtime structures with
// Nokonoko Estate's parser and retail MP4 Mario data, adding the 0x144-byte
// object hierarchy, materials, native GX textures, CENV skinning, motions,
// and the HSF-only part/cluster/shape/replica metadata.

#define HSF_MAGIC "HSFV037"
#define HSF_NUM_ENTRIES 20
#define HSF_IDX_SCENE 0
#define HSF_IDX_POSITIONS 4
#define HSF_IDX_NORMALS 5
#define HSF_IDX_UVS 6
#define HSF_IDX_FACES 7
#define HSF_IDX_MATERIALS 2
#define HSF_IDX_COLORS 1
#define HSF_IDX_ATTRIBUTES 3
#define HSF_IDX_NODES 8
#define HSF_IDX_TEXTURES 9
#define HSF_IDX_PALETTES 10
#define HSF_IDX_MOTIONS 11
#define HSF_IDX_ENVELOPES 12
#define HSF_IDX_PARTS 14
#define HSF_IDX_CLUSTERS 15
#define HSF_IDX_SHAPES 16
#define HSF_IDX_MAP_ATTR 17
#define HSF_IDX_MATRICES 18
#define HSF_IDX_SYMBOLS 19

#define HSF_MAX_PARTS 512 // sanity cap against corrupt/hostile input

static inline u32 hsf_be32 (const u8 *p)
{
	return (u32)p[0] << 24 | (u32)p[1] << 16 | (u32)p[2] << 8 | p[3];
}

static inline float hsf_bef32 (const u8 *p)
{
	u32 bits = hsf_be32 (p);
	float f;
	memcpy (&f, &bits, 4);
	return f;
}

static inline s16 hsf_be16s (const u8 *p)
{
	return (s16)((u16)p[0] << 8 | p[1]);
}

typedef struct
{
	u32 name_off, count, data_off;
} hsf_attr_hdr_t;

// Read 'count' consecutive 12-byte AttributeHeader records at file offset
// 'off'. Returns NULL if that range doesn't fit in the file.
static hsf_attr_hdr_t *hsf_read_headers (const u8 *data, uint size, u32 off, u32 count)
{
	if (!count || count > HSF_MAX_PARTS || (u64)off + (u64)count * 12 > size)
		return 0;
	hsf_attr_hdr_t *h = MALLOC (count * sizeof (*h));
	const u8 *p = data + off;
	for (u32 i = 0; i < count; i++, p += 12)
	{
		h[i].name_off = hsf_be32 (p);
		h[i].count = hsf_be32 (p + 4);
		h[i].data_off = hsf_be32 (p + 8);
	}
	return h;
}

static ccp hsf_str (const u8 *data, uint size, u32 str_table_off, u32 rel_off)
{
	const u64 abs = (u64)str_table_off + rel_off;
	return abs < size && memchr (data + abs, 0, size - abs) ? (ccp)(data + abs) : "";
}

static void hsf_safe_name (char *dest, uint cap, ccp src, ccp fallback)
{
	uint n = 0;
	for (; src && *src && n + 1 < cap; src++)
	{
		const u8 c = *src;
		dest[n++] = isalnum (c) || c == '_' || c == '-' ? c : '_';
	}
	if (!n)
		snprintf (dest, cap, "%s", fallback);
	else
		dest[n] = 0;
}

static uint hsf_gx_size (uint fmt, uint w, uint h)
{
	uint bw, bh, bpp;
	switch (fmt)
	{
		case 0:
			bw = 8;
			bh = 8;
			bpp = 4;
			break;
		case 1:
		case 2:
			bw = 8;
			bh = 4;
			bpp = 8;
			break;
		case 3:
		case 4:
		case 5:
			bw = 4;
			bh = 4;
			bpp = 16;
			break;
		case 6:
			bw = 4;
			bh = 4;
			bpp = 32;
			break;
		case 8:
			bw = 8;
			bh = 8;
			bpp = 4;
			break;
		case 9:
			bw = 8;
			bh = 4;
			bpp = 8;
			break;
		case 14:
			bw = 8;
			bh = 8;
			bpp = 4;
			break;
		default:
			return 0;
	}
	return ((w + bw - 1) / bw) * bw * ((h + bh - 1) / bh) * bh * bpp / 8;
}

static bool hsf_set_influences (model_t *model, mesh_t *mesh, uint first, uint count,
	const int *bones, const float *weights, uint n_weights)
{
	if (!mesh->position_node || !model->node_influences || !n_weights || n_weights > 8
		|| first >= mesh->num_positions || count > mesh->num_positions - first)
		return false;
	for (uint p = first; p < first + count; p++)
	{
		const int ni = mesh->position_node[p];
		if (ni < 0 || (size_t)ni >= model->num_node_influences)
			return false;
		node_influence_t *inf = model->node_influences + ni;
		FREE (inf->weights);
		inf->weights = CALLOC (n_weights, sizeof (*inf->weights));
		if (!inf->weights)
		{
			inf->num_weights = 0;
			return false;
		}
		inf->num_weights = n_weights;
		float sum = 0;
		for (uint i = 0; i < n_weights; i++)
			if (bones[i] >= 0 && (size_t)bones[i] < model->num_joints && weights[i] > 0)
			{
				inf->weights[i].bone_idx = bones[i];
				inf->weights[i].weight = weights[i];
				sum += weights[i];
			}
		if (sum > 0.000001f)
			for (uint i = 0; i < n_weights; i++)
				inf->weights[i].weight /= sum;
	}
	return true;
}

static void hsf_json_string (FILE *f, ccp s)
{
	fputc ('"', f);
	for (; s && *s; s++)
	{
		u8 c = *s;
		if (c == '"' || c == '\\')
			fputc ('\\', f);
		fputc (c >= 32 ? c : '_', f);
	}
	fputc ('"', f);
}

static void hsf_export_motions (
	const u8 *data, uint size, const u32 *off, const u32 *cnt, u32 str_off, ccp out_path)
{
	const uint n = cnt[11];
	if (!n || n > 256 || (u64)off[11] + (u64)n * 16 > size)
		return;
	const u64 track_base = (u64)off[11] + (u64)n * 16;
	u64 key_base = track_base;
	for (uint m = 0; m < n; m++)
	{
		const u8 *h = data + off[11] + m * 16;
		u64 e = track_base + hsf_be32 (h + 8) + (u64)hsf_be32 (h + 4) * 16;
		if (e > key_base)
			key_base = e;
	}
	char path[PATH_MAX];
	if (snprintf (path, sizeof (path), "%s.motion.json", out_path) >= (int)sizeof (path))
		return;
	FILE *f = fopen (path, "wb");
	if (!f)
		return;
	fprintf (f, "{\n  \"format\":\"HSFV037-motion\",\n  \"motions\":[");
	for (uint m = 0; m < n; m++)
	{
		const u8 *h = data + off[11] + m * 16;
		uint nt = hsf_be32 (h + 4), rel = hsf_be32 (h + 8);
		if (m)
			fputc (',', f);
		fprintf (f, "\n    {\"name\":");
		hsf_json_string (f, hsf_str (data, size, str_off, hsf_be32 (h)));
		fprintf (f, ",\"frames\":%g,\"tracks\":[", hsf_bef32 (h + 12));
		for (uint t = 0; t < nt; t++)
		{
			u64 o = track_base + rel + (u64)t * 16;
			if (o + 16 > size)
				break;
			const u8 *p = data + o;
			s16 target = hsf_be16s (p + 2), value = hsf_be16s (p + 4), effect = hsf_be16s (p + 6),
				interp = hsf_be16s (p + 8), nk = hsf_be16s (p + 10);
			if (t)
				fputc (',', f);
			fprintf (f, "{\"mode\":%u,\"target\":", p[0]);
			if (target >= 0)
				hsf_json_string (f, hsf_str (data, size, str_off, target));
			else
				fprintf (f, "null");
			fprintf (
				f, ",\"valueIndex\":%d,\"effect\":%d,\"interpolation\":%d", value, effect, interp);
			if (nk > 0 && interp != 4)
			{
				u32 ko = hsf_be32 (p + 12);
				uint stride = interp == 2 ? 16 : 8;
				fprintf (f, ",\"keys\":[");
				for (int k = 0; k < nk; k++)
				{
					u64 x = key_base + ko + (u64)k * stride;
					if (x + stride > size)
						break;
					if (k)
						fputc (',', f);
					fprintf (f, "[%g,", hsf_bef32 (data + x));
					if (interp == 3)
						fprintf (f, "%d", (s32)hsf_be32 (data + x + 4));
					else
						fprintf (f, "%g", hsf_bef32 (data + x + 4));
					if (interp == 2)
						fprintf (f, ",%g,%g", hsf_bef32 (data + x + 8), hsf_bef32 (data + x + 12));
					fputc (']', f);
				}
				fputc (']', f);
			}
			else
				fprintf (f, ",\"constant\":%g", hsf_bef32 (p + 12));
			fputc ('}', f);
		}
		fprintf (f, "]}");
	}
	fprintf (f, "\n  ]\n}\n");
	fclose (f);
}

static float hsf_eval_track (const u8 *data, uint size, const u8 *p, u64 key_base, float time)
{
	int interp = hsf_be16s (p + 8), n = hsf_be16s (p + 10);
	if (interp == 4 || n <= 0)
		return hsf_bef32 (p + 12);
	uint stride = interp == 2 ? 16 : 8;
	u64 base = key_base + hsf_be32 (p + 12);
	if (n > 0x4000 || base + (u64)n * stride > size)
		return 0;
	int hi = 0;
	while (hi < n && time >= hsf_bef32 (data + base + (u64)hi * stride))
		hi++;
	if (hi <= 0)
		return hsf_bef32 (data + base + 4);
	if (hi >= n)
		return hsf_bef32 (data + base + (u64)(n - 1) * stride + 4);
	const u8 *a = data + base + (u64)(hi - 1) * stride, *b = a + stride;
	float ta = hsf_bef32 (a), tb = hsf_bef32 (b), va = hsf_bef32 (a + 4), vb = hsf_bef32 (b + 4);
	if (interp == 0 || tb <= ta)
		return va;
	float x = (time - ta) / (tb - ta);
	if (interp == 1)
		return va + x * (vb - va);
	float x2 = x * x, x3 = x2 * x;
	return va * (2 * x3 - 3 * x2 + 1) + vb * (-2 * x3 + 3 * x2)
		+ hsf_bef32 (a + 8) * (x3 - 2 * x2 + x) + hsf_bef32 (b + 12) * (x3 - x2);
}

static void hsf_build_animations (
	model_t *model, const u8 *data, uint size, const u32 *off, const u32 *cnt, u32 str_off)
{
	uint nm = cnt[HSF_IDX_MOTIONS];
	if (!nm || nm > 256 || (u64)off[HSF_IDX_MOTIONS] + (u64)nm * 16 > size)
		return;
	u64 track_base = (u64)off[HSF_IDX_MOTIONS] + (u64)nm * 16, key_base = track_base;
	for (uint m = 0; m < nm; m++)
	{
		const u8 *h = data + off[HSF_IDX_MOTIONS] + m * 16;
		u64 e = track_base + hsf_be32 (h + 8) + (u64)hsf_be32 (h + 4) * 16;
		if (e > key_base)
			key_base = e;
	}
	model->animations = CALLOC (nm, sizeof (*model->animations));
	if (!model->animations)
		return;
	model->num_animations = nm;
	for (uint m = 0; m < nm; m++)
	{
		const u8 *h = data + off[HSF_IDX_MOTIONS] + m * 16;
		uint nt = hsf_be32 (h + 4), rel = hsf_be32 (h + 8),
			 frames = (uint)floorf (hsf_bef32 (h + 12) + 0.0001f) + 1;
		if (!frames || frames > 100000)
			continue;
		model_animation_t *an = model->animations + m;
		hsf_safe_name (
			an->name, sizeof (an->name), hsf_str (data, size, str_off, hsf_be32 (h)), "motion");
		an->channels = CALLOC (
			model->num_joints * 3 + model->num_instances * 3 + model->num_meshes * 4,
			sizeof (*an->channels));
		if (!an->channels)
			continue;
		for (size_t j = 0; j < model->num_joints; j++)
			for (int path = 0; path < 3; path++)
			{
				bool found = false;
				for (uint t = 0; t < nt; t++)
				{
					u64 x = track_base + rel + (u64)t * 16;
					if (x + 16 > size)
						break;
					const u8 *p = data + x;
					int ef = hsf_be16s (p + 6);
					if (p[0] == 2
						&& !strcmp (hsf_str (data, size, str_off, (u16)hsf_be16s (p + 2)),
							model->joints[j].name)
						&& ((path == 0 && ef >= 8 && ef <= 10)
							|| (path == 1 && ef >= 28 && ef <= 30)
							|| (path == 2 && ef >= 31 && ef <= 33)))
					{
						found = true;
						break;
					}
				}
				if (!found)
					continue;
				model_anim_channel_t *ch = an->channels + an->num_channels++;
				ch->node_idx = j;
				ch->path = path == 0 ? MODEL_ANIM_TRANSLATION
					: path == 1		 ? MODEL_ANIM_ROTATION
									 : MODEL_ANIM_SCALE;
				ch->count = frames;
				ch->components = path == 1 ? 4 : 3;
				ch->times = MALLOC (frames * sizeof (float));
				ch->values = MALLOC ((u64)frames * ch->components * sizeof (float));
				if (!ch->times || !ch->values)
					continue;
				for (uint f = 0; f < frames; f++)
				{
					float v[3] = { path == 0 ? model->joints[j].translate.x
							: path == 1		 ? model->joints[j].rotate.x
											 : model->joints[j].scale.x,
						path == 0		? model->joints[j].translate.y
							: path == 1 ? model->joints[j].rotate.y
										: model->joints[j].scale.y,
						path == 0		? model->joints[j].translate.z
							: path == 1 ? model->joints[j].rotate.z
										: model->joints[j].scale.z };
					for (uint t = 0; t < nt; t++)
					{
						u64 x = track_base + rel + (u64)t * 16;
						if (x + 16 > size)
							break;
						const u8 *p = data + x;
						if (p[0] != 2
							|| strcmp (hsf_str (data, size, str_off, (u16)hsf_be16s (p + 2)),
								model->joints[j].name))
							continue;
						int ef = hsf_be16s (p + 6), base = path == 0 ? 8 : path == 1 ? 28 : 31;
						if (ef >= base && ef < base + 3)
							v[ef - base] = hsf_eval_track (data, size, p, key_base, f);
					}
					ch->times[f] = f / 60.0f;
					if (path != 1)
						memcpy (ch->values + (u64)f * 3, v, 12);
					else
					{
						double hx = v[0] * M_PI / 360, hy = v[1] * M_PI / 360,
							   hz = v[2] * M_PI / 360, cx = cos (hx), sx = sin (hx), cy = cos (hy),
							   sy = sin (hy), cz = cos (hz), sz = sin (hz);
						float *q = ch->values + (u64)f * 4;
						q[0] = sx * cy * cz - cx * sy * sz;
						q[1] = cx * sy * cz + sx * cy * sz;
						q[2] = cx * cy * sz - sx * sy * cz;
						q[3] = cx * cy * cz + sx * sy * sz;
					}
				}
			}
		// Replica/instance nodes (type 1) can carry their own TRS animation
		// tracks, same as true joints (type 0) -- model->joints was filtered
		// down to true joints only, so instance nodes must be scanned here
		// separately or their TRS tracks (e.g. an animated replica rotation)
		// are silently dropped.
		for (size_t ii = 0; ii < model->num_instances; ii++)
			for (int path = 0; path < 3; path++)
			{
				bool found = false;
				for (uint t = 0; t < nt; t++)
				{
					u64 x = track_base + rel + (u64)t * 16;
					if (x + 16 > size)
						break;
					const u8 *p = data + x;
					int ef = hsf_be16s (p + 6);
					if (p[0] == 2
						&& !strcmp (hsf_str (data, size, str_off, (u16)hsf_be16s (p + 2)),
							model->instances[ii].name)
						&& ((path == 0 && ef >= 8 && ef <= 10)
							|| (path == 1 && ef >= 28 && ef <= 30)
							|| (path == 2 && ef >= 31 && ef <= 33)))
					{
						found = true;
						break;
					}
				}
				if (!found)
					continue;
				model_anim_channel_t *ch = an->channels + an->num_channels++;
				ch->node_idx = (int)(model->num_joints + model->num_meshes + ii);
				ch->path = path == 0 ? MODEL_ANIM_TRANSLATION
					: path == 1		 ? MODEL_ANIM_ROTATION
									 : MODEL_ANIM_SCALE;
				ch->count = frames;
				ch->components = path == 1 ? 4 : 3;
				ch->times = MALLOC (frames * sizeof (float));
				ch->values = MALLOC ((u64)frames * ch->components * sizeof (float));
				if (!ch->times || !ch->values)
					continue;
				for (uint f = 0; f < frames; f++)
				{
					float v[3] = { path == 0 ? model->instances[ii].translate.x
							: path == 1		 ? model->instances[ii].rotate.x
											 : model->instances[ii].scale.x,
						path == 0		? model->instances[ii].translate.y
							: path == 1 ? model->instances[ii].rotate.y
										: model->instances[ii].scale.y,
						path == 0		? model->instances[ii].translate.z
							: path == 1 ? model->instances[ii].rotate.z
										: model->instances[ii].scale.z };
					for (uint t = 0; t < nt; t++)
					{
						u64 x = track_base + rel + (u64)t * 16;
						if (x + 16 > size)
							break;
						const u8 *p = data + x;
						if (p[0] != 2
							|| strcmp (hsf_str (data, size, str_off, (u16)hsf_be16s (p + 2)),
								model->instances[ii].name))
							continue;
						int ef = hsf_be16s (p + 6), base = path == 0 ? 8 : path == 1 ? 28 : 31;
						if (ef >= base && ef < base + 3)
							v[ef - base] = hsf_eval_track (data, size, p, key_base, f);
					}
					ch->times[f] = f / 60.0f;
					if (path != 1)
						memcpy (ch->values + (u64)f * 3, v, 12);
					else
					{
						double hx = v[0] * M_PI / 360, hy = v[1] * M_PI / 360,
							   hz = v[2] * M_PI / 360, cx = cos (hx), sx = sin (hx), cy = cos (hy),
							   sy = sin (hy), cz = cos (hz), sz = sin (hz);
						float *q = ch->values + (u64)f * 4;
						q[0] = sx * cy * cz - cx * sy * sz;
						q[1] = cx * sy * cz + sx * cy * sz;
						q[2] = cx * cy * sz - sx * sy * cz;
						q[3] = cx * cy * cz + sx * sy * sz;
					}
				}
			}
		// Mesh-definition nodes (HSF type 2) can carry their own TRS animation
		// tracks in addition to (or instead of) morph-weight tracks -- e.g. a
		// mesh node animated with both a weight track and a rotate track. Scan
		// for those here, independent of whether the mesh has morph targets.
		for (size_t mi = 0; mi < model->num_meshes; mi++)
		{
			mesh_t *mesh = model->meshes + mi;
			for (int path = 0; path < 3; path++)
			{
				bool found = false;
				for (uint t = 0; t < nt; t++)
				{
					u64 x = track_base + rel + (u64)t * 16;
					if (x + 16 > size)
						break;
					const u8 *p = data + x;
					int ef = hsf_be16s (p + 6);
					ccp target = hsf_str (data, size, str_off, (u16)hsf_be16s (p + 2));
					bool mesh_target = !strcmp (target, mesh->name)
						|| (*target && !strncmp (mesh->name, target, strlen (target))
							&& strstr (mesh->name, "_mat"));
					if (p[0] == 2 && mesh_target
						&& ((path == 0 && ef >= 8 && ef <= 10)
							|| (path == 1 && ef >= 28 && ef <= 30)
							|| (path == 2 && ef >= 31 && ef <= 33)))
					{
						found = true;
						break;
					}
				}
				if (!found)
					continue;
				model_anim_channel_t *ch = an->channels + an->num_channels++;
				ch->node_idx = (int)(model->num_joints + mi);
				ch->path = path == 0 ? MODEL_ANIM_TRANSLATION
					: path == 1		 ? MODEL_ANIM_ROTATION
									 : MODEL_ANIM_SCALE;
				ch->count = frames;
				ch->components = path == 1 ? 4 : 3;
				ch->times = MALLOC (frames * sizeof (float));
				ch->values = MALLOC ((u64)frames * ch->components * sizeof (float));
				if (!ch->times || !ch->values)
					continue;
				for (uint f = 0; f < frames; f++)
				{
					float v[3] = { 0, 0, 0 };
					for (uint t = 0; t < nt; t++)
					{
						u64 x = track_base + rel + (u64)t * 16;
						if (x + 16 > size)
							break;
						const u8 *p = data + x;
						ccp target = hsf_str (data, size, str_off, (u16)hsf_be16s (p + 2));
						bool mesh_target = !strcmp (target, mesh->name)
							|| (*target && !strncmp (mesh->name, target, strlen (target))
								&& strstr (mesh->name, "_mat"));
						if (p[0] != 2 || !mesh_target)
							continue;
						int ef = hsf_be16s (p + 6), base = path == 0 ? 8 : path == 1 ? 28 : 31;
						if (ef >= base && ef < base + 3)
							v[ef - base] = hsf_eval_track (data, size, p, key_base, f);
					}
					ch->times[f] = f / 60.0f;
					if (path != 1)
						memcpy (ch->values + (u64)f * 3, v, 12);
					else
					{
						double hx = v[0] * M_PI / 360, hy = v[1] * M_PI / 360,
							   hz = v[2] * M_PI / 360, cx = cos (hx), sx = sin (hx), cy = cos (hy),
							   sy = sin (hy), cz = cos (hz), sz = sin (hz);
						float *q = ch->values + (u64)f * 4;
						q[0] = sx * cy * cz - cx * sy * sz;
						q[1] = cx * sy * cz + sx * cy * sz;
						q[2] = cx * cy * sz - sx * sy * cz;
						q[3] = cx * cy * cz + sx * sy * sz;
					}
				}
			}
			if (!mesh->num_morph_targets)
				continue;
			bool found = false;
			for (uint t = 0; t < nt && !found; t++)
			{
				u64 x = track_base + rel + (u64)t * 16;
				if (x + 16 > size)
					break;
				const u8 *p = data + x;
				ccp target = hsf_str (data, size, str_off, (u16)hsf_be16s (p + 2));
				bool mesh_target = !strcmp (target, mesh->name)
					|| (*target && !strncmp (mesh->name, target, strlen (target))
						&& strstr (mesh->name, "_mat"));
				if ((p[0] == 3 || (p[0] == 2 && hsf_be16s (p + 6) == 40)) && mesh_target)
					found = true;
				else if (p[0] == 5 || p[0] == 6)
					for (u32 c = 0; c < cnt[HSF_IDX_CLUSTERS]; c++)
					{
						u64 co = (u64)off[HSF_IDX_CLUSTERS] + (u64)c * 0xa0;
						if (co + 0xa0 > size)
							break;
						if (!strcmp (target, hsf_str (data, size, str_off, hsf_be32 (data + co)))
							&& !strncmp (mesh->name,
								hsf_str (data, size, str_off, hsf_be32 (data + co + 8)),
								strlen (hsf_str (data, size, str_off, hsf_be32 (data + co + 8)))))
						{
							found = true;
							break;
						}
					}
			}
			if (!found)
				continue;
			model_anim_channel_t *ch = an->channels + an->num_channels++;
			ch->node_idx = model->num_joints + mi;
			ch->path = MODEL_ANIM_WEIGHTS;
			ch->count = frames;
			ch->components = mesh->num_morph_targets;
			ch->times = MALLOC (frames * sizeof (float));
			ch->values = MALLOC ((u64)frames * ch->components * sizeof (float));
			if (!ch->times || !ch->values)
				continue;
			for (uint f = 0; f < frames; f++)
			{
				float *wv = ch->values + (u64)f * ch->components;
				memcpy (wv, mesh->morph_weights, ch->components * sizeof (float));
				ch->times[f] = f / 60.0f;
				for (uint t = 0; t < nt; t++)
				{
					u64 x = track_base + rel + (u64)t * 16;
					if (x + 16 > size)
						break;
					const u8 *p = data + x;
					ccp target = hsf_str (data, size, str_off, (u16)hsf_be16s (p + 2));
					bool mesh_target = !strcmp (target, mesh->name)
						|| (*target && !strncmp (mesh->name, target, strlen (target))
							&& strstr (mesh->name, "_mat"));
					if (p[0] == 3 && mesh_target)
					{
						int wi = hsf_be16s (p + 6);
						if (wi >= 0 && (size_t)wi < ch->components)
							wv[wi] = hsf_eval_track (data, size, p, key_base, f);
						continue;
					}
					if (p[0] == 2 && hsf_be16s (p + 6) == 40 && mesh_target)
					{
						u32 sn = 0;
						for (u32 ni = 0; ni < cnt[HSF_IDX_NODES]; ni++)
						{
							u64 no = (u64)off[HSF_IDX_NODES] + (u64)ni * 0x144;
							if (no + 0x144 > size)
								break;
							ccp nn = hsf_str (data, size, str_off, hsf_be32 (data + no));
							if (!strcmp (nn, target))
							{
								sn = hsf_be32 (data + no + 292);
								break;
							}
						}
						if (sn > ch->components)
							sn = ch->components;
						for (u32 q = 0; q < sn; q++)
							wv[q] = 0;
						float val = hsf_eval_track (data, size, p, key_base, f);
						int lo = floorf (val);
						float frac = val - lo;
						if (lo >= 0 && (u32)lo < sn)
							wv[lo] = 1 - frac;
						if (lo + 1 >= 0 && (u32)(lo + 1) < sn)
							wv[lo + 1] = frac;
						continue;
					}
					if (p[0] != 5 && p[0] != 6)
						continue;
					size_t base_target = 0;
					for (u32 ni = 0; ni < cnt[HSF_IDX_NODES]; ni++)
					{
						u64 no = (u64)off[HSF_IDX_NODES] + (u64)ni * 0x144;
						if (no + 0x144 > size)
							break;
						ccp nn = hsf_str (data, size, str_off, hsf_be32 (data + no));
						if (!strcmp (nn, mesh->name)
							|| (*nn && !strncmp (mesh->name, nn, strlen (nn))
								&& strstr (mesh->name, "_mat")))
						{
							base_target = hsf_be32 (data + no + 292);
							break;
						}
					}
					for (u32 c = 0; c < cnt[HSF_IDX_CLUSTERS]; c++)
					{
						u64 co = (u64)off[HSF_IDX_CLUSTERS] + (u64)c * 0xa0;
						if (co + 0xa0 > size)
							break;
						ccp cn = hsf_str (data, size, str_off, hsf_be32 (data + co)),
							ct = hsf_str (data, size, str_off, hsf_be32 (data + co + 8));
						u32 vn = hsf_be32 (data + co + 0x98);
						bool same = !strcmp (ct, mesh->name)
							|| (*ct && !strncmp (mesh->name, ct, strlen (ct))
								&& strstr (mesh->name, "_mat"));
						if (!strcmp (cn, target) && same)
						{
							if (base_target + vn > ch->components)
								break;
							float val = hsf_eval_track (data, size, p, key_base, f);
							if (p[0] == 5)
							{
								for (u32 q = 0; q < vn; q++)
									wv[base_target + q] = 0;
								int lo = floorf (val);
								float frac = val - lo;
								if (lo >= 0 && (u32)lo < vn)
									wv[base_target + lo] = 1 - frac;
								if (lo + 1 >= 0 && (u32)(lo + 1) < vn)
									wv[base_target + lo + 1] = frac;
							}
							else
							{
								int wi = (s32)hsf_be32 (p + 4);
								if (wi >= 0 && (u32)wi < vn)
									wv[base_target + wi] = val;
							}
							break;
						}
						if (same)
							base_target += vn;
					}
				}
				// Hudson's type-2 cluster loop performs successive lerps. Convert
				// its runtime weights to equivalent coefficients over our absolute
				// alternate-position deltas, including animated mode-6 weights.
				size_t bt = 0;
				for (u32 ni = 0; ni < cnt[HSF_IDX_NODES]; ni++)
				{
					u64 no = (u64)off[HSF_IDX_NODES] + (u64)ni * 0x144;
					if (no + 0x144 > size)
						break;
					ccp nn = hsf_str (data, size, str_off, hsf_be32 (data + no));
					if (!strcmp (nn, mesh->name)
						|| (*nn && !strncmp (mesh->name, nn, strlen (nn))
							&& strstr (mesh->name, "_mat")))
					{
						bt = hsf_be32 (data + no + 292);
						break;
					}
				}
				for (u32 c = 0; c < cnt[HSF_IDX_CLUSTERS]; c++)
				{
					u64 co = (u64)off[HSF_IDX_CLUSTERS] + (u64)c * 0xa0;
					if (co + 0xa0 > size)
						break;
					ccp ct = hsf_str (data, size, str_off, hsf_be32 (data + co + 8));
					bool same = !strcmp (ct, mesh->name)
						|| (*ct && !strncmp (mesh->name, ct, strlen (ct))
							&& strstr (mesh->name, "_mat"));
					u32 vn = hsf_be32 (data + co + 0x98);
					if (!same)
						continue;
					if (bt + vn > ch->components)
						break;
					if ((u16)hsf_be16s (data + co + 0x96) == 2 && vn <= 32)
					{
						float raw[32], sum = 0;
						for (u32 q = 0; q < vn; q++)
						{
							raw[q] = hsf_bef32 (data + co + 20 + q * 4);
							if (!isfinite (raw[q]))
								raw[q] = 0;
						}
						ccp cn = hsf_str (data, size, str_off, hsf_be32 (data + co));
						for (uint t = 0; t < nt; t++)
						{
							u64 x = track_base + rel + (u64)t * 16;
							if (x + 16 > size)
								break;
							const u8 *p = data + x;
							if (p[0] == 6
								&& !strcmp (
									cn, hsf_str (data, size, str_off, (u16)hsf_be16s (p + 2))))
							{
								int wi = (s32)hsf_be32 (p + 4);
								if (wi >= 0 && (u32)wi < vn)
									raw[wi] = hsf_eval_track (data, size, p, key_base, f);
							}
						}
						for (u32 q = 0; q < vn; q++)
							sum += raw[q];
						float carry = 1;
						for (int q = vn - 1; q >= 1; q--)
						{
							float w = raw[q] < 0 ? 0 : sum > 1 ? raw[q] / sum : raw[q];
							wv[bt + q] = w * carry;
							carry *= 1 - w;
						}
						wv[bt] = carry;
					}
					bt += vn;
				}
			}
		}
	}
}

// Preserve the HSF-only scene features which have no direct counterpart in
// model_t yet.  These layouts come from Hudson's loader itself (PartLoad,
// ClusterLoad, ShapeLoad and the HSF_OBJ_REPLICA branch), rather than inferred
// record sizes.  Indices/pointers are left in their original HSF namespace so
// downstream tooling can reproduce the loader fixups exactly.
static void hsf_export_extensions (
	const u8 *data, uint size, const u32 *off, const u32 *cnt, u32 str_off, ccp out_path)
{
	const u32 np = cnt[HSF_IDX_PARTS], nc = cnt[HSF_IDX_CLUSTERS];
	const u32 ns = cnt[HSF_IDX_SHAPES], nn = cnt[HSF_IDX_NODES];
	const u32 nscene = cnt[HSF_IDX_SCENE], nmap = cnt[HSF_IDX_MAP_ATTR];
	const u32 nmatrix = cnt[HSF_IDX_MATRICES];
	uint nr = 0, ncamera = 0, nlight = 0;
	if (nn <= HSF_MAX_PARTS && (u64)off[HSF_IDX_NODES] + (u64)nn * 0x144 <= size)
		for (u32 i = 0; i < nn; i++)
		{
			const u32 type = hsf_be32 (data + off[HSF_IDX_NODES] + (u64)i * 0x144 + 4);
			nr += type == 1;
			ncamera += type == 7;
			nlight += type == 8;
		}
	if (!np && !nc && !ns && !nr && !nscene && !nmap && !nmatrix && !ncamera && !nlight)
		return;

	char path[PATH_MAX];
	if (snprintf (path, sizeof (path), "%s.hsf.json", out_path) >= (int)sizeof (path))
		return;
	FILE *f = fopen (path, "wb");
	if (!f)
		return;
	fprintf (f, "{\n  \"format\":\"HSFV037-extensions\",\n  \"replicas\":[");
	bool comma = false;
	if (nn <= HSF_MAX_PARTS && (u64)off[HSF_IDX_NODES] + (u64)nn * 0x144 <= size)
		for (u32 i = 0; i < nn; i++)
		{
			const u8 *p = data + off[HSF_IDX_NODES] + (u64)i * 0x144;
			if (hsf_be32 (p + 4) != 1)
				continue;
			if (comma)
				fputc (',', f);
			comma = true;
			fprintf (f, "{\"node\":%u,\"name\":", i);
			hsf_json_string (f, hsf_str (data, size, str_off, hsf_be32 (p)));
			fprintf (f, ",\"targetNode\":%d}", (s32)hsf_be32 (p + 0x64));
		}
	fprintf (f, "],\n  \"cameras\":[");
	comma = false;
	if (nn <= HSF_MAX_PARTS && (u64)off[HSF_IDX_NODES] + (u64)nn * 0x144 <= size)
		for (u32 i = 0; i < nn; i++)
		{
			const u8 *p = data + off[HSF_IDX_NODES] + (u64)i * 0x144;
			if (hsf_be32 (p + 4) != 7)
				continue;
			if (comma)
				fputc (',', f);
			comma = true;
			fprintf (f, "{\"node\":%u,\"name\":", i);
			hsf_json_string (f, hsf_str (data, size, str_off, hsf_be32 (p)));
			fprintf (f,
				",\"position\":[%g,%g,%g],\"target\":[%g,%g,%g],\"upRotation\":%g,\"fov\":%g,"
				"\"near\":%g,\"far\":%g}",
				hsf_bef32 (p + 16), hsf_bef32 (p + 20), hsf_bef32 (p + 24), hsf_bef32 (p + 28),
				hsf_bef32 (p + 32), hsf_bef32 (p + 36), hsf_bef32 (p + 40), hsf_bef32 (p + 44),
				hsf_bef32 (p + 48), hsf_bef32 (p + 52));
		}
	fprintf (f, "],\n  \"lights\":[");
	comma = false;
	if (nn <= HSF_MAX_PARTS && (u64)off[HSF_IDX_NODES] + (u64)nn * 0x144 <= size)
		for (u32 i = 0; i < nn; i++)
		{
			const u8 *p = data + off[HSF_IDX_NODES] + (u64)i * 0x144;
			if (hsf_be32 (p + 4) != 8)
				continue;
			if (comma)
				fputc (',', f);
			comma = true;
			fprintf (f, "{\"node\":%u,\"name\":", i);
			hsf_json_string (f, hsf_str (data, size, str_off, hsf_be32 (p)));
			fprintf (f,
				",\"position\":[%g,%g,%g],\"target\":[%g,%g,%g],\"type\":%u,\"color\":[%u,%u,%u],"
				"\"unknown\":%g,\"referenceDistance\":%g,\"referenceBrightness\":%g,\"cutoff\":%g}",
				hsf_bef32 (p + 16), hsf_bef32 (p + 20), hsf_bef32 (p + 24), hsf_bef32 (p + 28),
				hsf_bef32 (p + 32), hsf_bef32 (p + 36), p[40], p[41], p[42], p[43],
				hsf_bef32 (p + 44), hsf_bef32 (p + 48), hsf_bef32 (p + 52), hsf_bef32 (p + 56));
		}
	fprintf (f, "],\n  \"scene\":[");
	for (u32 i = 0; i < nscene; i++)
	{
		u64 o = (u64)off[HSF_IDX_SCENE] + (u64)i * 16;
		if (o + 16 > size)
			break;
		if (i)
			fputc (',', f);
		fprintf (f, "{\"fogType\":%u,\"fogStart\":%g,\"fogEnd\":%g,\"fogColor\":[%u,%u,%u,%u]}",
			hsf_be32 (data + o), hsf_bef32 (data + o + 4), hsf_bef32 (data + o + 8), data[o + 12],
			data[o + 13], data[o + 14], data[o + 15]);
	}
	fprintf (f, "],\n  \"mapAttributes\":[");
	for (u32 i = 0; i < nmap; i++)
	{
		u64 o = (u64)off[HSF_IDX_MAP_ATTR] + (u64)i * 24;
		if (o + 24 > size)
			break;
		if (i)
			fputc (',', f);
		fprintf (f, "{\"bounds\":[%g,%g,%g,%g],\"dataIndex\":%u,\"dataLength\":%u}",
			hsf_bef32 (data + o), hsf_bef32 (data + o + 4), hsf_bef32 (data + o + 8),
			hsf_bef32 (data + o + 12), hsf_be32 (data + o + 16), hsf_be32 (data + o + 20));
	}
	fprintf (f, "],\n  \"matrices\":[");
	// HSF's matrix section begins with base index/count/data index, followed
	// by count affine 3x4 matrices. Preserve both the loader header and data.
	if (nmatrix && (u64)off[HSF_IDX_MATRICES] + 12 <= size)
	{
		u64 o = off[HSF_IDX_MATRICES];
		u32 base = hsf_be32 (data + o), num = hsf_be32 (data + o + 4),
			rel = hsf_be32 (data + o + 8);
		u64 mb = o + 12 + rel;
		fprintf (f, "{\"baseIndex\":%u,\"count\":%u,\"dataIndex\":%u,\"values\":[", base, num, rel);
		for (u32 i = 0; i < num && mb + (u64)(i + 1) * 48 <= size; i++)
		{
			if (i)
				fputc (',', f);
			fputc ('[', f);
			for (u32 j = 0; j < 12; j++)
			{
				if (j)
					fputc (',', f);
				fprintf (f, "%g", hsf_bef32 (data + mb + (u64)i * 48 + j * 4));
			}
			fputc (']', f);
		}
		fprintf (f, "]}");
	}
	fprintf (f, "],\n  \"parts\":[");
	for (u32 i = 0; i < np && i < HSF_MAX_PARTS; i++)
	{
		const u64 o = (u64)off[HSF_IDX_PARTS] + (u64)i * 12;
		if (o + 12 > size)
			break;
		if (i)
			fputc (',', f);
		fprintf (f, "{\"name\":");
		hsf_json_string (f, hsf_str (data, size, str_off, hsf_be32 (data + o)));
		fprintf (f, ",\"vertexCount\":%u,\"vertexOffset\":%u}", hsf_be32 (data + o + 4),
			hsf_be32 (data + o + 8));
	}
	fprintf (f, "],\n  \"clusters\":[");
	// 0xa0-byte HSFCLUSTER: three string offsets, part index, current value,
	// 32 weights, flags/type, vertex-buffer count and symbol-list index.
	for (u32 i = 0; i < nc && i < HSF_MAX_PARTS; i++)
	{
		const u64 o = (u64)off[HSF_IDX_CLUSTERS] + (u64)i * 0xa0;
		if (o + 0xa0 > size)
			break;
		if (i)
			fputc (',', f);
		fprintf (f, "{\"name\":[");
		hsf_json_string (f, hsf_str (data, size, str_off, hsf_be32 (data + o)));
		fputc (',', f);
		hsf_json_string (f, hsf_str (data, size, str_off, hsf_be32 (data + o + 4)));
		fprintf (f, "],\"target\":");
		hsf_json_string (f, hsf_str (data, size, str_off, hsf_be32 (data + o + 8)));
		fprintf (f, ",\"part\":%d,\"index\":%g,\"weights\":[", (s32)hsf_be32 (data + o + 12),
			hsf_bef32 (data + o + 16));
		for (uint w = 0; w < 32; w++)
		{
			if (w)
				fputc (',', f);
			fprintf (f, "%g", hsf_bef32 (data + o + 20 + w * 4));
		}
		fprintf (f,
			"],\"adjusted\":%u,\"unknown95\":%u,\"type\":%u,\"vertexBufferCount\":%u,"
			"\"vertexSymbolIndex\":%u}",
			data[o + 0x94], data[o + 0x95], (u16)hsf_be16s (data + o + 0x96),
			hsf_be32 (data + o + 0x98), hsf_be32 (data + o + 0x9c));
	}
	fprintf (f, "],\n  \"shapes\":[");
	for (u32 i = 0; i < ns && i < HSF_MAX_PARTS; i++)
	{
		const u64 o = (u64)off[HSF_IDX_SHAPES] + (u64)i * 12;
		if (o + 12 > size)
			break;
		if (i)
			fputc (',', f);
		fprintf (f, "{\"name\":");
		hsf_json_string (f, hsf_str (data, size, str_off, hsf_be32 (data + o)));
		fprintf (f, ",\"kind\":%u,\"vertexBufferCount\":%u,\"vertexSymbolIndex\":%u}",
			(u16)hsf_be16s (data + o + 4), (u16)hsf_be16s (data + o + 6), hsf_be32 (data + o + 8));
	}
	fprintf (f, "]\n}\n");
	fclose (f);
}

static void hsf_free_mesh_content (mesh_t *m)
{
	FREE (m->positions);
	FREE (m->normals);
	FREE (m->tangents);
	FREE (m->texcoords);
	FREE (m->colors[0]);
	FREE (m->vertices);
	FREE (m->triangle_materials);
	FREE (m->position_node);
	for (size_t t = 0; t < m->num_morph_targets; t++)
		FREE (m->morph_targets[t].position_deltas);
	FREE (m->morph_targets);
	FREE (m->morph_weights);
}

static bool hsf_copy_array (void **d, const void *s, size_t n)
{
	if (!n)
	{
		*d = 0;
		return true;
	}
	*d = MALLOC (n);
	if (!*d)
		return false;
	memcpy (*d, s, n);
	return true;
}

static enumError hsf_split_material_meshes (mesh_t **list, u32 *count, u32 **rig_map)
{
	mesh_t *old = *list;
	const u32 old_n = *count;
	u32 total = 0;
	for (u32 m = 0; m < old_n; m++)
	{
		int seen[64], ns = 0;
		for (size_t t = 0; t < old[m].num_vertices / 3; t++)
		{
			int x = old[m].triangle_materials[t], j = 0;
			for (; j < ns; j++)
				if (seen[j] == x)
					break;
			if (j == ns && ns < 64)
				seen[ns++] = x;
		}
		total += ns ? ns : 1;
	}
	mesh_t *out = CALLOC (total, sizeof (*out));
	u32 *map = MALLOC (total * sizeof (*map));
	if (!out || !map)
	{
		FREE (out);
		FREE (map);
		return ERR_CANT_CREATE;
	}
	u32 n = 0;
	for (u32 m = 0; m < old_n; m++)
	{
		int seen[64], ns = 0;
		for (size_t t = 0; t < old[m].num_vertices / 3; t++)
		{
			int x = old[m].triangle_materials[t], j = 0;
			for (; j < ns; j++)
				if (seen[j] == x)
					break;
			if (j == ns && ns < 64)
				seen[ns++] = x;
		}
		if (!ns)
			seen[ns++] = old[m].material_idx;
		for (int si = 0; si < ns; si++)
		{
			mesh_t *d = out + n;
			*d = old[m];
			d->positions = 0;
			d->normals = 0;
			d->texcoords = 0;
			d->colors[0] = 0;
			d->vertices = 0;
			d->triangle_materials = 0;
			d->position_node = 0;
			d->morph_targets = 0;
			d->morph_weights = 0;
			if (ns > 1)
				snprintf (d->name, sizeof (d->name), "%.48s_mat%d", old[m].name, seen[si]);
			d->material_idx = seen[si];
			if (!hsf_copy_array ((void **)&d->positions, old[m].positions,
					old[m].num_positions * sizeof (*d->positions))
				|| !hsf_copy_array (
					(void **)&d->normals, old[m].normals, old[m].num_normals * sizeof (*d->normals))
				|| !hsf_copy_array ((void **)&d->texcoords, old[m].texcoords,
					old[m].num_texcoords * sizeof (*d->texcoords))
				|| !hsf_copy_array ((void **)&d->colors[0], old[m].colors[0],
					old[m].num_colors[0] * sizeof (*d->colors[0]))
				|| !hsf_copy_array ((void **)&d->morph_weights, old[m].morph_weights,
					old[m].num_morph_targets * sizeof (*d->morph_weights)))
			{
				for (u32 q = 0; q <= n; q++)
					hsf_free_mesh_content (out + q);
				FREE (out);
				FREE (map);
				return ERR_CANT_CREATE;
			}
			if (old[m].num_morph_targets)
			{
				d->morph_targets = CALLOC (old[m].num_morph_targets, sizeof (*d->morph_targets));
				if (!d->morph_targets)
				{
					for (u32 q = 0; q <= n; q++)
						hsf_free_mesh_content (out + q);
					FREE (out);
					FREE (map);
					return ERR_CANT_CREATE;
				}
				for (size_t t = 0; t < old[m].num_morph_targets; t++)
				{
					d->morph_targets[t] = old[m].morph_targets[t];
					d->morph_targets[t].position_deltas = 0;
					if (!hsf_copy_array ((void **)&d->morph_targets[t].position_deltas,
							old[m].morph_targets[t].position_deltas,
							old[m].morph_targets[t].num_positions * sizeof (vec3_t)))
					{
						for (u32 q = 0; q <= n; q++)
							hsf_free_mesh_content (out + q);
						FREE (out);
						FREE (map);
						return ERR_CANT_CREATE;
					}
				}
			}
			size_t nv = 0;
			for (size_t t = 0; t < old[m].num_vertices / 3; t++)
				if (old[m].triangle_materials[t] == seen[si])
					nv += 3;
			d->vertices = MALLOC (nv * sizeof (*d->vertices));
			d->triangle_materials = MALLOC ((nv / 3) * sizeof (*d->triangle_materials));
			if ((nv && !d->vertices) || (nv && !d->triangle_materials))
			{
				for (u32 q = 0; q <= n; q++)
					hsf_free_mesh_content (out + q);
				FREE (out);
				FREE (map);
				return ERR_CANT_CREATE;
			}
			size_t v = 0;
			for (size_t t = 0; t < old[m].num_vertices / 3; t++)
				if (old[m].triangle_materials[t] == seen[si])
				{
					memcpy (d->vertices + v, old[m].vertices + t * 3, 3 * sizeof (*d->vertices));
					d->triangle_materials[v / 3] = seen[si];
					v += 3;
				}
			d->num_vertices = nv;
			map[n++] = m;
		}
	}
	for (u32 m = 0; m < old_n; m++)
		hsf_free_mesh_content (old + m);
	FREE (old);
	*list = out;
	*count = n;
	*rig_map = map;
	return ERR_OK;
}

static void hsf_node_matrix (float m[16], const u8 *p)
{
	double rx = hsf_bef32 (p + 40) * M_PI / 180, ry = hsf_bef32 (p + 44) * M_PI / 180,
		   rz = hsf_bef32 (p + 48) * M_PI / 180;
	double cx = cos (rx), sx = sin (rx), cy = cos (ry), sy = sin (ry), cz = cos (rz), sz = sin (rz);
	float x = hsf_bef32 (p + 52), y = hsf_bef32 (p + 56), z = hsf_bef32 (p + 60);
	m[0] = cy * cz * x;
	m[1] = cy * sz * x;
	m[2] = -sy * x;
	m[3] = 0;
	m[4] = (cz * sx * sy - cx * sz) * y;
	m[5] = (cx * cz + sx * sy * sz) * y;
	m[6] = cy * sx * y;
	m[7] = 0;
	m[8] = (sx * sz + cx * cz * sy) * z;
	m[9] = (cx * sy * sz - cz * sx) * z;
	m[10] = cx * cy * z;
	m[11] = 0;
	m[12] = hsf_bef32 (p + 28);
	m[13] = hsf_bef32 (p + 32);
	m[14] = hsf_bef32 (p + 36);
	m[15] = 1;
}

static void hsf_mat_mul (float out[16], const float a[16], const float b[16])
{
	float r[16];
	for (int c = 0; c < 4; c++)
		for (int row = 0; row < 4; row++)
		{
			r[c * 4 + row] = 0;
			for (int k = 0; k < 4; k++)
				r[c * 4 + row] += a[k * 4 + row] * b[c * 4 + k];
		}
	memcpy (out, r, sizeof (r));
}

static int hsf_mesh_for_node (const u8 *data, uint size, const u32 *off, u32 str_off, u32 node,
	const mesh_t *meshes, u32 n_meshes)
{
	const u8 *p = data + off[HSF_IDX_NODES] + (u64)node * 0x144;
	ccp name = hsf_str (data, size, str_off, hsf_be32 (p));
	for (u32 m = 0; m < n_meshes; m++)
		if (!strcmp (meshes[m].name, name)
			|| (*name && !strncmp (meshes[m].name, name, strlen (name))
				&& strstr (meshes[m].name, "_mat")))
			return m;
	return -1;
}

// Expand one replica target without duplicating mesh payloads. Nested
// replicas are flattened to model_instance_t nodes; `active` rejects cyclic
// replica graphs found in malformed or self-referential files.
static bool hsf_expand_replica (model_t *model, size_t *cap, const u8 *data, uint size,
	const u32 *off, u32 str_off, u32 node_cnt, u32 replica, const float base[16], int parent,
	u8 *active, uint depth)
{
	if (replica >= node_cnt || depth >= HSF_MAX_PARTS || active[replica])
		return true;
	const u8 *rp = data + off[HSF_IDX_NODES] + (u64)replica * 0x144;
	const s32 target = (s32)hsf_be32 (rp + 0x64);
	if (target < 0 || (u32)target >= node_cnt)
		return true;
	active[replica] = 1;
	for (u32 n = 0; n < node_cnt; n++)
	{
		int path[HSF_MAX_PARTS], np = 0, cur = n;
		while (cur >= 0 && (u32)cur < node_cnt && cur != target && np < HSF_MAX_PARTS)
		{
			path[np++] = cur;
			cur = (s32)hsf_be32 (data + off[HSF_IDX_NODES] + (u64)cur * 0x144 + 16);
		}
		if (cur != target)
			continue;
		const u8 *p = data + off[HSF_IDX_NODES] + (u64)n * 0x144;
		const u32 type = hsf_be32 (p + 4);
		if (type != 1 && type != 2)
			continue;
		float composed[16];
		memcpy (composed, base, sizeof (composed));
		for (int k = np - 1; k >= 0; k--)
		{
			float local[16];
			hsf_node_matrix (local, data + off[HSF_IDX_NODES] + (u64)path[k] * 0x144);
			hsf_mat_mul (composed, composed, local);
		}
		if (type == 1)
		{
			// If the target itself is a replica its local transform was not in
			// `path`; include it before following that replica's reference.
			if (n == (u32)target)
			{
				float local[16];
				hsf_node_matrix (local, p);
				hsf_mat_mul (composed, composed, local);
			}
			hsf_expand_replica (model, cap, data, size, off, str_off, node_cnt, n, composed, parent,
				active, depth + 1);
			continue;
		}
		int mi = hsf_mesh_for_node (data, size, off, str_off, n, model->meshes, model->num_meshes);
		if (mi < 0)
			continue;
		if (model->num_instances == *cap)
		{
			size_t nc = *cap ? *cap * 2 : 8;
			model_instance_t *q = REALLOC (model->instances, nc * sizeof (*q));
			if (!q)
			{
				active[replica] = 0;
				return false;
			}
			model->instances = q;
			*cap = nc;
		}
		model_instance_t *in = model->instances + model->num_instances++;
		memset (in, 0, sizeof (*in));
		char rn[40];
		hsf_safe_name (rn, sizeof (rn), hsf_str (data, size, str_off, hsf_be32 (rp)), "replica");
		snprintf (in->name, sizeof (in->name), "%.32s_%.28s", rn, model->meshes[mi].name);
		in->mesh_idx = mi;
		in->parent_idx = parent;
		in->has_matrix = 1;
		memcpy (in->matrix, composed, sizeof (in->matrix));
	}
	active[replica] = 0;
	return true;
}

// glTF cameras and punctual lights look down local -Z with local +Y as up.
static void hsf_look_at_matrix (float m[16], const u8 *p, float roll_deg)
{
	float px = hsf_bef32 (p), py = hsf_bef32 (p + 4), pz = hsf_bef32 (p + 8);
	float fx = hsf_bef32 (p + 12) - px, fy = hsf_bef32 (p + 16) - py, fz = hsf_bef32 (p + 20) - pz;
	float n = sqrtf (fx * fx + fy * fy + fz * fz);
	if (n < 1e-8f)
	{
		fx = 0;
		fy = 0;
		fz = -1;
	}
	else
	{
		fx /= n;
		fy /= n;
		fz /= n;
	}
	float rx = -fz, ry = 0, rz = fx;
	n = sqrtf (rx * rx + rz * rz);
	if (n < 1e-8f)
	{
		rx = 1;
		rz = 0;
	}
	else
	{
		rx /= n;
		rz /= n;
	}
	float ux = fy * rz - fz * ry, uy = fz * rx - fx * rz, uz = fx * ry - fy * rx;
	const float a = roll_deg * (float)M_PI / 180.0f, c = cosf (a), s = sinf (a);
	float r2x = rx * c + ux * s, r2y = ry * c + uy * s, r2z = rz * c + uz * s;
	float u2x = ux * c - rx * s, u2y = uy * c - ry * s, u2z = uz * c - rz * s;
	m[0] = r2x;
	m[1] = r2y;
	m[2] = r2z;
	m[3] = 0;
	m[4] = u2x;
	m[5] = u2y;
	m[6] = u2z;
	m[7] = 0;
	m[8] = -fx;
	m[9] = -fy;
	m[10] = -fz;
	m[11] = 0;
	m[12] = px;
	m[13] = py;
	m[14] = pz;
	m[15] = 1;
}

enumError DecodeHSF (const u8 *data, uint size, ccp out_path)
{
	if (!data || size < 8 + HSF_NUM_ENTRIES * 8 || memcmp (data, HSF_MAGIC, 7))
		return ERR_NOTHING_TO_DO;

	u32 entry_off[HSF_NUM_ENTRIES], entry_cnt[HSF_NUM_ENTRIES];
	const u8 *table = data + 8;
	for (int i = 0; i < HSF_NUM_ENTRIES; i++)
	{
		entry_off[i] = hsf_be32 (table + i * 8);
		entry_cnt[i] = hsf_be32 (table + i * 8 + 4);
	}

	if (size < 0xAC + 4)
		return ERR_NOTHING_TO_DO;
	const u32 str_off = hsf_be32 (data + 0xA8);
	if (str_off >= size)
		return ERR_NOTHING_TO_DO;

	// Metadata-only HSFs are common (camera cuts, lights and fog presets).
	// Emit their lossless sidecars before requiring a geometry section.
	hsf_export_motions (data, size, entry_off, entry_cnt, str_off, out_path);
	hsf_export_extensions (data, size, entry_off, entry_cnt, str_off, out_path);

	const u32 pos_cnt = entry_cnt[HSF_IDX_POSITIONS];
	const u32 col_cnt = entry_cnt[HSF_IDX_COLORS];
	const u32 nrm_cnt = entry_cnt[HSF_IDX_NORMALS];
	const u32 uv_cnt = entry_cnt[HSF_IDX_UVS];
	const u32 prim_cnt = entry_cnt[HSF_IDX_FACES];
	if (!pos_cnt || !prim_cnt)
		return ERR_NOTHING_TO_DO;

	// Resolve the material -> symbol -> attribute -> texture chain. These
	// fixed record sizes and indices come from Hudson's hsfview runtime
	// loader and are independently corroborated by Nokonoko Estate.
	const u32 mat_cnt = entry_cnt[HSF_IDX_MATERIALS];
	const u32 attr_cnt = entry_cnt[HSF_IDX_ATTRIBUTES];
	const u32 tex_cnt = entry_cnt[HSF_IDX_TEXTURES];
	const u32 pal_cnt = entry_cnt[HSF_IDX_PALETTES];
	// Hudson writes the symbol section's top-level count as the number of
	// symbol groups, not the number of s32 indices. Its actual index pool
	// runs from the symbol offset to the string table.
	const u32 sym_cnt = entry_off[HSF_IDX_SYMBOLS] && str_off > entry_off[HSF_IDX_SYMBOLS]
		? (str_off - entry_off[HSF_IDX_SYMBOLS]) / 4
		: 0;
	material_t *materials
		= mat_cnt && mat_cnt <= HSF_MAX_PARTS ? CALLOC (mat_cnt, sizeof (*materials)) : 0;
	char (*tex_names)[64] = tex_cnt && tex_cnt <= HSF_MAX_PARTS ? CALLOC (tex_cnt, 64) : 0;
	if ((mat_cnt && !materials) || (tex_cnt && !tex_names))
	{
		FREE (materials);
		FREE (tex_names);
		return ERR_CANT_CREATE;
	}
	for (u32 i = 0; i < tex_cnt; i++)
	{
		const u64 o = (u64)entry_off[HSF_IDX_TEXTURES] + i * 32;
		if (o + 32 > size)
			break;
		char base[48];
		hsf_safe_name (
			base, sizeof (base), hsf_str (data, size, str_off, hsf_be32 (data + o)), "texture");
		snprintf (tex_names[i], 64, "%s.png", base);
		uint fmt = data[o + 8], gx = fmt == 7 ? 14 : fmt, bpp = data[o + 9];
		if (fmt >= 9 && fmt <= 11)
			gx = bpp == 4 ? 8 : 9;
		const uint w = ((uint)data[o + 10] << 8) | data[o + 11],
				   h = ((uint)data[o + 12] << 8) | data[o + 13];
		const s32 pi = (s32)hsf_be32 (data + o + 20);
		const uint rel = hsf_be32 (data + o + 28);
		const u64 pix_base = (u64)entry_off[HSF_IDX_TEXTURES] + (u64)tex_cnt * 32 + rel;
		const uint pix_size = hsf_gx_size (gx, w, h);
		const u8 *pal = 0;
		uint pn = 0, pf = 0;
		if (pi >= 0 && (u32)pi < pal_cnt)
		{
			const u64 po = (u64)entry_off[HSF_IDX_PALETTES] + (u32)pi * 16;
			if (po + 16 <= size)
			{
				pf = hsf_be32 (data + po + 4);
				pn = hsf_be32 (data + po + 8);
				u64 pd = (u64)entry_off[HSF_IDX_PALETTES] + (u64)pal_cnt * 16
					+ hsf_be32 (data + po + 12);
				if (pd + (u64)pn * 2 <= size)
					pal = data + pd;
			}
		}
		if (w && h && pix_size && pix_base + pix_size <= size)
		{
			u8 *rgba = 0;
			if (!DecodeGXTexture_RGBA (&rgba, w, h, gx, data + pix_base, pix_size, pal, pn, pf))
			{
				char path[PATH_MAX];
				ccp slash = strrchr (out_path, '/');
				uint dlen = slash ? (uint)(slash - out_path + 1) : 0;
				if (dlen + strlen (tex_names[i]) + 1 < sizeof (path))
				{
					memcpy (path, out_path, dlen);
					strcpy (path + dlen, tex_names[i]);
					SaveDecodedRGBAToPNG (rgba, w, h, &be_func, path, 0, true);
				}
				else
					FREE (rgba);
			}
		}
	}
	for (u32 i = 0; i < mat_cnt; i++)
	{
		const u64 o = (u64)entry_off[HSF_IDX_MATERIALS] + i * 60;
		if (o + 60 > size)
			break;
		hsf_safe_name (materials[i].name, sizeof (materials[i].name),
			hsf_str (data, size, str_off, hsf_be32 (data + o)), "material");
		// HsfMaterial_s on-disk layout (from Mario Party decomp):
		// +0x0B: u8 litColor[3], +0x0E: u8 color[3] (diffuse RGB),
		// +0x11: u8 shadowColor[3], +0x14: float hiliteScale, +0x1C: float invAlpha.
		materials[i].diffuse[0] = data[o + 0x0E] / 255.0f;
		materials[i].diffuse[1] = data[o + 0x0F] / 255.0f;
		materials[i].diffuse[2] = data[o + 0x10] / 255.0f;
		materials[i].diffuse[3] = 1.0f - hsf_bef32 (data + o + 0x1C);
		materials[i].shininess = hsf_bef32 (data + o + 0x14);
		materials[i].ambient[0] = data[o + 0x0B] / 255.0f;
		materials[i].ambient[1] = data[o + 0x0C] / 255.0f;
		materials[i].ambient[2] = data[o + 0x0D] / 255.0f;
		const uint nt = hsf_be32 (data + o + 52), first = hsf_be32 (data + o + 56);
		for (uint j = 0; j < nt && j < 8; j++)
		{
			u64 so = (u64)entry_off[HSF_IDX_SYMBOLS] + (u64)(first + j) * 4;
			if (first + j >= sym_cnt || so + 4 > size)
				break;
			u32 ai = hsf_be32 (data + so);
			u64 ao = (u64)entry_off[HSF_IDX_ATTRIBUTES] + (u64)ai * 132;
			if (ai >= attr_cnt || ao + 132 > size)
				continue;
			s32 ti = (s32)hsf_be32 (data + ao + 128);
			if (ti < 0 || (u32)ti >= tex_cnt)
				continue;
			uint k = materials[i].num_textures++;
			snprintf (materials[i].textures[k], 64, "%s", tex_names[ti]);
			materials[i].wrap_s[k] = (u8)hsf_be32 (data + ao + 100);
			materials[i].wrap_t[k] = (u8)hsf_be32 (data + ao + 104);
			materials[i].min_filter[k] = materials[i].mag_filter[k] = 1;
			// HsfAttribute_s: +0x28/0x2C = HuVec2f scale, +0x30/0x34 = HuVec2f trans.
			materials[i].tex_scale_s[k] = hsf_bef32 (data + ao + 0x28);
			materials[i].tex_scale_t[k] = hsf_bef32 (data + ao + 0x2C);
			materials[i].tex_translate_s[k] = hsf_bef32 (data + ao + 0x30);
			materials[i].tex_translate_t[k] = hsf_bef32 (data + ao + 0x34);
			materials[i].has_tex_transform[k] = (materials[i].tex_scale_s[k] != 1.0f
				|| materials[i].tex_scale_t[k] != 1.0f || materials[i].tex_translate_s[k] != 0.0f
				|| materials[i].tex_translate_t[k] != 0.0f);
		}
	}

	// Transform-bearing object kinds (0..6) use the hierarchy/TRS union.
	// Camera/light records (7/8) reuse those bytes for position/target fields
	// and must not be interpreted as parents or scales.
	const u32 node_cnt = entry_cnt[HSF_IDX_NODES];
	joint_t *joints
		= node_cnt && node_cnt <= HSF_MAX_PARTS ? CALLOC (node_cnt, sizeof (*joints)) : 0;
	// Raw HSF node-table entries mix true hierarchy joints (type 0) with
	// mesh-definition nodes (type 2, written at model->num_joints+m on
	// encode) and replica/instance nodes (type 1, written at
	// model->num_joints+nm+i). All of these share the node index space so
	// parent_idx lookups below work, but only type-0 entries are real
	// skeleton joints -- `ntype` remembers each entry's type so they can be
	// filtered back out before being handed to model.joints (see below);
	// otherwise every mesh/replica node round-trips into an extra spurious
	// joint on every decode->encode cycle, breaking the canonical fixed
	// point.
	u8 *ntype = node_cnt && node_cnt <= HSF_MAX_PARTS ? CALLOC (node_cnt, 1) : 0;
	if (node_cnt && (!joints || !ntype))
	{
		FREE (joints);
		FREE (ntype);
		FREE (materials);
		FREE (tex_names);
		return ERR_CANT_CREATE;
	}
	for (u32 i = 0; i < node_cnt; i++)
	{
		const u64 o = (u64)entry_off[HSF_IDX_NODES] + (u64)i * 0x144;
		if (o + 0x144 > size)
			break;
		ccp n = hsf_str (data, size, str_off, hsf_be32 (data + o));
		hsf_safe_name (joints[i].name, sizeof (joints[i].name), n, "node");
		const u32 type = hsf_be32 (data + o + 4);
		ntype[i] = type <= 6 ? (u8)type : 7;
		joints[i].parent_idx = type <= 6 ? (s32)hsf_be32 (data + o + 16) : -2;
		if (type > 6)
		{
			joints[i].scale = (vec3_t) { 1, 1, 1 };
			continue;
		}
		joints[i].translate = (vec3_t) { hsf_bef32 (data + o + 28), hsf_bef32 (data + o + 32),
			hsf_bef32 (data + o + 36) };
		joints[i].rotate = (vec3_t) { hsf_bef32 (data + o + 40), hsf_bef32 (data + o + 44),
			hsf_bef32 (data + o + 48) };
		joints[i].scale = (vec3_t) { hsf_bef32 (data + o + 52), hsf_bef32 (data + o + 56),
			hsf_bef32 (data + o + 60) };
	}
	hsf_attr_hdr_t *pos_hdr = hsf_read_headers (data, size, entry_off[HSF_IDX_POSITIONS], pos_cnt);
	if (!pos_hdr)
		return ERR_NOTHING_TO_DO;
	const u32 pos_base = entry_off[HSF_IDX_POSITIONS] + pos_cnt * 12;
	hsf_attr_hdr_t *col_hdr
		= col_cnt ? hsf_read_headers (data, size, entry_off[HSF_IDX_COLORS], col_cnt) : 0;
	const u32 col_base = entry_off[HSF_IDX_COLORS] + col_cnt * 12;

	hsf_attr_hdr_t *nrm_hdr
		= nrm_cnt ? hsf_read_headers (data, size, entry_off[HSF_IDX_NORMALS], nrm_cnt) : 0;
	const u32 nrm_base = entry_off[HSF_IDX_NORMALS] + nrm_cnt * 12;
	bool nrm_packed = false;
	if (nrm_hdr && nrm_cnt >= 2)
	{
		// The 0x20 rounding is against the ABSOLUTE file offset, not the
		// offset-within-section -- confirmed against real data (a wrong,
		// section-relative rounding is off by a few bytes and was caught by
		// the DAE validator flagging non-finite normals on a real sample).
		u64 abs_end = (u64)nrm_base + nrm_hdr[0].data_off + (u64)nrm_hdr[0].count * 3;
		if (abs_end % 0x20)
			abs_end += 0x20 - abs_end % 0x20;
		nrm_packed = (nrm_hdr[1].data_off == abs_end - nrm_base);
	}
	const uint nrm_stride = nrm_packed ? 3 : 12;

	hsf_attr_hdr_t *uv_hdr
		= uv_cnt ? hsf_read_headers (data, size, entry_off[HSF_IDX_UVS], uv_cnt) : 0;
	const u32 uv_base = entry_off[HSF_IDX_UVS] + uv_cnt * 12;

	hsf_attr_hdr_t *prim_hdr = hsf_read_headers (data, size, entry_off[HSF_IDX_FACES], prim_cnt);
	if (!prim_hdr)
	{
		FREE (pos_hdr);
		if (nrm_hdr)
			FREE (nrm_hdr);
		if (uv_hdr)
			FREE (uv_hdr);
		return ERR_NOTHING_TO_DO;
	}
	const u32 prim_base = entry_off[HSF_IDX_FACES] + prim_cnt * 12;
	// Type-4 (indexed triangle-strip) primitive records point at a shared
	// "Ext" VertexGroup pool located right after ALL primitive-table data
	// (every mesh's, not just the current one's).
	u64 ext_pool_off = prim_base;
	for (u32 i = 0; i < prim_cnt; i++)
		ext_pool_off += (u64)prim_hdr[i].count * 48;

	// One output mesh per primitive-table entry, matched by name to the
	// position (and, when present, normal/UV) table entry of the same
	// name. A name repeated in the primitive table is skipped after its
	// first occurrence (matches the reference decoder).
	const u32 n_meshes = prim_cnt > HSF_MAX_PARTS ? HSF_MAX_PARTS : prim_cnt;
	mesh_t *meshes = MALLOC (n_meshes * sizeof (mesh_t));
	memset (meshes, 0, n_meshes * sizeof (mesh_t));
	u32 out_n = 0;
	bool bad = false;

	for (u32 m = 0; m < prim_cnt && !bad && out_n < n_meshes; m++)
	{
		ccp name = hsf_str (data, size, str_off, prim_hdr[m].name_off);

		bool dup = false;
		for (u32 j = 0; j < out_n; j++)
			if (!strncmp (meshes[j].name, name, sizeof (meshes[j].name) - 1))
			{
				dup = true;
				break;
			}
		if (dup)
			continue;

		int pi = -1;
		for (u32 j = 0; j < pos_cnt; j++)
			if (!strcmp (hsf_str (data, size, str_off, pos_hdr[j].name_off), name))
			{
				pi = (int)j;
				break;
			}
		if (pi < 0 && m < pos_cnt)
			// Positional fallback: real retail files give every attribute
			// table entry for one mesh-part the SAME name, but our synthetic
			// single-object test fixture (predating this reconciliation) used
			// distinct descriptive names per attribute ("cube_vtxs" vs
			// "cube_faces"); pair by table index when name matching fails and
			// there's a same-index candidate, so both conventions work.
			pi = (int)m;
		if (pi < 0)
		{
			bad = true;
			break;
		}

		const u32 n_verts = pos_hdr[pi].count;
		const u64 pos_data_off = (u64)pos_base + pos_hdr[pi].data_off;
		if (!n_verts || pos_data_off + (u64)n_verts * 12 > size)
		{
			bad = true;
			break;
		}

		mesh_t *mesh = meshes + out_n;
		snprintf (mesh->name, sizeof (mesh->name), "%s", *name ? name : "hsf_mesh");
		mesh->material_idx = -1;

		mesh->positions = MALLOC (n_verts * sizeof (vec3_t));
		mesh->num_positions = n_verts;
		{
			const u8 *p = data + pos_data_off;
			for (u32 i = 0; i < n_verts; i++, p += 12)
			{
				mesh->positions[i].x = hsf_bef32 (p);
				mesh->positions[i].y = hsf_bef32 (p + 4);
				mesh->positions[i].z = hsf_bef32 (p + 8);
			}
		}

		if (nrm_hdr)
		{
			int ni = -1;
			for (u32 j = 0; j < nrm_cnt; j++)
				if (!strcmp (hsf_str (data, size, str_off, nrm_hdr[j].name_off), name))
				{
					ni = (int)j;
					break;
				}
			if (ni >= 0)
			{
				const u32 n_nrm = nrm_hdr[ni].count;
				const u64 nrm_data_off = (u64)nrm_base + nrm_hdr[ni].data_off;
				if (n_nrm && nrm_data_off + (u64)n_nrm * nrm_stride <= size)
				{
					mesh->normals = MALLOC (n_nrm * sizeof (vec3_t));
					mesh->num_normals = n_nrm;
					const u8 *p = data + nrm_data_off;
					for (u32 i = 0; i < n_nrm; i++, p += nrm_stride)
					{
						if (nrm_packed)
						{
							mesh->normals[i].x = (s8)p[0] / 127.0f;
							mesh->normals[i].y = (s8)p[1] / 127.0f;
							mesh->normals[i].z = (s8)p[2] / 127.0f;
						}
						else
						{
							mesh->normals[i].x = hsf_bef32 (p);
							mesh->normals[i].y = hsf_bef32 (p + 4);
							mesh->normals[i].z = hsf_bef32 (p + 8);
						}
					}
				}
			}
		}

		if (uv_hdr)
		{
			int ui = -1;
			for (u32 j = 0; j < uv_cnt; j++)
				if (!strcmp (hsf_str (data, size, str_off, uv_hdr[j].name_off), name))
				{
					ui = (int)j;
					break;
				}
			if (ui >= 0)
			{
				const u32 n_uv = uv_hdr[ui].count;
				const u64 uv_data_off = (u64)uv_base + uv_hdr[ui].data_off;
				if (n_uv && uv_data_off + (u64)n_uv * 8 <= size)
				{
					mesh->texcoords = MALLOC (n_uv * sizeof (vec2_t));
					mesh->num_texcoords = n_uv;
					const u8 *p = data + uv_data_off;
					for (u32 i = 0; i < n_uv; i++, p += 8)
					{
						mesh->texcoords[i].u = hsf_bef32 (p);
						// HSF-native texcoords are top-left origin; COLLADA/
						// mesh_t is bottom-left. The encoder flips V
						// (1.0 - v) when writing HSF data (see hsf_putf
						// call below), so mirror that flip here on decode
						// -- otherwise decode -> encode nets one extra
						// flip instead of cancelling out.
						mesh->texcoords[i].v = 1.0f - hsf_bef32 (p + 4);
					}
				}
			}
		}

		if (col_hdr)
		{
			int ci = -1;
			for (u32 j = 0; j < col_cnt; j++)
				if (!strcmp (hsf_str (data, size, str_off, col_hdr[j].name_off), name))
				{
					ci = j;
					break;
				}
			if (ci >= 0
				&& (u64)col_base + col_hdr[ci].data_off + (u64)col_hdr[ci].count * 4 <= size)
			{
				mesh->num_colors[0] = col_hdr[ci].count;
				mesh->colors[0] = MALLOC (mesh->num_colors[0] * sizeof (*mesh->colors[0]));
				const u8 *p = data + col_base + col_hdr[ci].data_off;
				for (size_t i = 0; i < mesh->num_colors[0]; i++, p += 4)
					mesh->colors[0][i]
						= (color4_t) { p[0] / 255.f, p[1] / 255.f, p[2] / 255.f, p[3] / 255.f };
			}
		}

		// Primitive records, all a fixed 48 bytes regardless of type (a
		// mesh's primitive-count * 48 exactly matches the offset gap to the
		// next mesh's data.off in every real sample checked). Types 2
		// (triangle) and 3 (quad) hold their corners directly; type 4 is an
		// indexed triangle-strip whose first 3 VertexGroups are read the same
		// way, followed by an s32 extra-count + u32 extra-offset pointing
		// into the shared Ext pool (ext_pool_off + offset*8) -- see the
		// ext_pool_off comment above. Any other type bails the whole file
		// with ERR_NOTHING_TO_DO rather than guessing further.
		const u32 n_prims = prim_hdr[m].count;
		const u64 prim_data_off = (u64)prim_base + prim_hdr[m].data_off;
		if (n_prims && prim_data_off + (u64)n_prims * 48 > size)
		{
			bad = true;
			break;
		}

		u32 tri_cap = n_prims * 2 + 8, tri_n = 0;
		vertex_t *tris = MALLOC (tri_cap * sizeof (vertex_t));
		int *vertex_mats = MALLOC (tri_cap * sizeof (*vertex_mats));
		int current_material = -1;

#define HSF_EMIT(idx)                                                                              \
	do                                                                                             \
	{                                                                                              \
		if (tri_n == tri_cap)                                                                      \
		{                                                                                          \
			tri_cap *= 2;                                                                          \
			tris = REALLOC (tris, tri_cap * sizeof (vertex_t));                                    \
			vertex_mats = REALLOC (vertex_mats, tri_cap * sizeof (*vertex_mats));                  \
		}                                                                                          \
		vertex_t *dv = tris + tri_n;                                                               \
		vertex_mats[tri_n++] = current_material;                                                   \
		dv->position_idx = pidx[idx] >= 0 && (u32)pidx[idx] < mesh->num_positions ? pidx[idx] : 0; \
		/* DAE export writes these indices verbatim whenever the mesh has                          \
		 * any of that attribute at all, so a per-corner "no data" (-1 in                          \
		 * the source record) has to fall back to index 0. */                                      \
		dv->normal_idx = mesh->num_normals                                                         \
			? (nidx[idx] >= 0 && (u32)nidx[idx] < mesh->num_normals ? nidx[idx] : 0)               \
			: -1;                                                                                  \
		dv->tangent_idx = -1;                                                                      \
		dv->texcoord_idx = mesh->num_texcoords                                                     \
			? (uidx[idx] >= 0 && (u32)uidx[idx] < mesh->num_texcoords ? uidx[idx] : 0)             \
			: -1;                                                                                  \
		dv->matrix_idx = -1;                                                                       \
		dv->color_idx[0] = mesh->num_colors[0]                                                     \
			? (cidx[idx] >= 0 && (u32)cidx[idx] < mesh->num_colors[0] ? cidx[idx] : 0)             \
			: -1;                                                                                  \
		dv->color_idx[1] = -1;                                                                     \
		for (int k = 0; k < 7; k++)                                                                \
			dv->extra_texcoord_idx[k] = -1;                                                        \
	} while (0)

		const u8 *rp = data + prim_data_off;
		for (u32 i = 0; i < n_prims && !bad; i++, rp += 48)
		{
			const s16 ptype = hsf_be16s (rp);
			if (ptype != 2 && ptype != 3 && ptype != 4)
			{
				bad = true;
				break;
			}
			const uint material = hsf_be16s (rp + 2) & 0xfff;
			current_material = (int)material;
			if (mesh->material_idx < 0 && material < mat_cnt)
				mesh->material_idx = (int)material;

			s16 pidx_stack[64], nidx_stack[64], cidx_stack[64], uidx_stack[64];
			s16 *pidx = pidx_stack, *nidx = nidx_stack, *cidx = cidx_stack, *uidx = uidx_stack;
			s16 *allocated = 0;

			const int n_explicit = ptype == 4 ? 3 : 4;
			const u8 *gp = rp + 4;
			for (int g = 0; g < n_explicit; g++, gp += 8)
			{
				pidx[g] = hsf_be16s (gp);
				nidx[g] = hsf_be16s (gp + 2);
				cidx[g] = hsf_be16s (gp + 4);
				uidx[g] = hsf_be16s (gp + 6);
			}
			int n_vg = n_explicit;

			if (ptype == 4)
			{
				const s32 ecount = (s32)hsf_be32 (gp);
				const u32 eoff = hsf_be32 (gp + 4);
				if (ecount > 0)
				{
					if (ext_pool_off + (u64)eoff * 8 + (u64)ecount * 8 > size)
					{
						bad = true;
						break;
					}
					const int total_vg = 4 + ecount;
					if (total_vg > 64)
					{
						allocated = MALLOC (total_vg * 4 * sizeof (s16));
						pidx = allocated;
						nidx = allocated + total_vg;
						cidx = allocated + total_vg * 2;
						uidx = allocated + total_vg * 3;
						for (int g = 0; g < 3; g++)
						{
							pidx[g] = pidx_stack[g];
							nidx[g] = nidx_stack[g];
							cidx[g] = cidx_stack[g];
							uidx[g] = uidx_stack[g];
						}
					}
					pidx[3] = pidx[1];
					nidx[3] = nidx[1];
					cidx[3] = cidx[1];
					uidx[3] = uidx[1];
					const u8 *ep = data + ext_pool_off + (u64)eoff * 8;
					for (int g = 0; g < ecount; g++, ep += 8)
					{
						pidx[4 + g] = hsf_be16s (ep);
						nidx[4 + g] = hsf_be16s (ep + 2);
						cidx[4 + g] = hsf_be16s (ep + 4);
						uidx[4 + g] = hsf_be16s (ep + 6);
					}
					n_vg = total_vg;
				}
				else
				{
					pidx[3] = pidx[1];
					nidx[3] = nidx[1];
					cidx[3] = cidx[1];
					uidx[3] = uidx[1];
					n_vg = 4;
				}
			}

			if (ptype == 2)
			{
				HSF_EMIT (0);
				HSF_EMIT (1);
				HSF_EMIT (2);
			}
			else if (ptype == 3)
			{
				HSF_EMIT (0);
				HSF_EMIT (1);
				HSF_EMIT (2);
				HSF_EMIT (1);
				HSF_EMIT (3);
				HSF_EMIT (2);
			}
			else // ptype == 4: triangulate the [v0,v1,v2,v1,ext...] strip
			{
				for (int c = 2; c < n_vg; c++)
				{
					int i0 = (c & 1) ? c - 1 : c - 2;
					int i1 = (c & 1) ? c - 2 : c - 1;
					int i2 = c;
					int p0 = pidx[i0] >= 0 && (u32)pidx[i0] < mesh->num_positions ? pidx[i0] : 0;
					int p1 = pidx[i1] >= 0 && (u32)pidx[i1] < mesh->num_positions ? pidx[i1] : 0;
					int p2 = pidx[i2] >= 0 && (u32)pidx[i2] < mesh->num_positions ? pidx[i2] : 0;
					if (p0 != p1 && p1 != p2 && p0 != p2)
					{
						HSF_EMIT (i0);
						HSF_EMIT (i1);
						HSF_EMIT (i2);
					}
				}
			}
			if (allocated)
				FREE (allocated);
		}
#undef HSF_EMIT

		if (bad)
		{
			FREE (tris);
			FREE (vertex_mats);
			break;
		}

		mesh->vertices = tris;
		mesh->num_vertices = tri_n;
		mesh->triangle_materials = MALLOC ((tri_n / 3) * sizeof (*mesh->triangle_materials));
		for (uint t = 0; t < tri_n / 3; t++)
			mesh->triangle_materials[t] = vertex_mats[t * 3];
		FREE (vertex_mats);

		out_n++;
	}

	// Mesh objects carry a symbol-list of alternate position-buffer indices.
	// Hudson's ShapeLoad/DispObject resolves these through NSymIndex and the
	// vertex table; glTF instead stores a delta from the mesh's base position.
	if (!bad && entry_off[HSF_IDX_SYMBOLS] && sym_cnt)
	{
		for (u32 m = 0; m < out_n; m++)
		{
			int oi = -1;
			for (u32 i = 0; i < node_cnt; i++)
			{
				u64 o = (u64)entry_off[HSF_IDX_NODES] + (u64)i * 0x144;
				if (o + 0x144 > size)
					break;
				if (hsf_be32 (data + o + 4) == 2
					&& !strcmp (hsf_str (data, size, str_off, hsf_be32 (data + o)), meshes[m].name))
				{
					oi = i;
					break;
				}
			}
			if (oi < 0)
				continue;
			const u64 o = (u64)entry_off[HSF_IDX_NODES] + (u64)oi * 0x144;
			const u32 nt = hsf_be32 (data + o + 292), si = hsf_be32 (data + o + 296);
			if (!nt || nt > 64 || si > sym_cnt || nt > sym_cnt - si)
				continue;
			meshes[m].morph_targets = CALLOC (nt, sizeof (*meshes[m].morph_targets));
			meshes[m].morph_weights = CALLOC (nt, sizeof (*meshes[m].morph_weights));
			if (!meshes[m].morph_targets || !meshes[m].morph_weights)
			{
				bad = true;
				break;
			}
			meshes[m].num_morph_targets = nt;
			for (u32 t = 0; t < nt; t++)
			{
				u32 pi = hsf_be32 (data + entry_off[HSF_IDX_SYMBOLS] + (u64)(si + t) * 4);
				morph_target_t *mt = meshes[m].morph_targets + t;
				mt->source_kind = 1;
				if (pi >= pos_cnt || pos_hdr[pi].count != meshes[m].num_positions)
					continue;
				hsf_safe_name (mt->name, sizeof (mt->name),
					hsf_str (data, size, str_off, pos_hdr[pi].name_off), "shape");
				mt->num_positions = meshes[m].num_positions;
				mt->position_deltas = CALLOC (mt->num_positions, sizeof (*mt->position_deltas));
				if (!mt->position_deltas)
				{
					bad = true;
					break;
				}
				u64 po = (u64)pos_base + pos_hdr[pi].data_off;
				if (po + (u64)mt->num_positions * 12 > size)
				{
					FREE (mt->position_deltas);
					mt->num_positions = 0;
					continue;
				}
				for (size_t v = 0; v < mt->num_positions; v++)
				{
					vec3_t q = { hsf_bef32 (data + po + v * 12), hsf_bef32 (data + po + v * 12 + 4),
						hsf_bef32 (data + po + v * 12 + 8) };
					mt->position_deltas[v] = (vec3_t) { q.x - meshes[m].positions[v].x,
						q.y - meshes[m].positions[v].y, q.z - meshes[m].positions[v].z };
				}
				if (t < 33)
				{
					float w = hsf_bef32 (data + o + 128 + t * 4);
					if (isfinite (w))
						meshes[m].morph_weights[t] = w;
				}
			}
			if (bad)
				break;
		}
		// Clusters deform only the vertex indices listed by an HSFPART. Expand
		// each referenced partial vertex buffer into a sparse, full-mesh glTF
		// morph target; this exactly represents index interpolation and preserves
		// the individual weighted alternatives for cluster type 2.
		for (u32 c = 0; c < entry_cnt[HSF_IDX_CLUSTERS] && !bad; c++)
		{
			u64 co = (u64)entry_off[HSF_IDX_CLUSTERS] + (u64)c * 0xa0;
			if (co + 0xa0 > size)
				break;
			ccp target = hsf_str (data, size, str_off, hsf_be32 (data + co + 8));
			int mi = -1;
			for (u32 m = 0; m < out_n; m++)
				if (!strcmp (meshes[m].name, target))
				{
					mi = m;
					break;
				}
			if (mi < 0)
				continue;
			s32 part = (s32)hsf_be32 (data + co + 12);
			u32 vn = hsf_be32 (data + co + 0x98), vsi = hsf_be32 (data + co + 0x9c);
			if (part < 0 || (u32)part >= entry_cnt[HSF_IDX_PARTS] || !vn || vn > 32 || vsi > sym_cnt
				|| vn > sym_cnt - vsi)
				continue;
			u64 po = (u64)entry_off[HSF_IDX_PARTS] + (u64)part * 12;
			if (po + 12 > size)
				continue;
			u32 pn = hsf_be32 (data + po + 4), pio = hsf_be32 (data + po + 8);
			u64 pib
				= (u64)entry_off[HSF_IDX_PARTS] + (u64)entry_cnt[HSF_IDX_PARTS] * 12 + (u64)pio * 2;
			if (!pn || pib + (u64)pn * 2 > size)
				continue;
			mesh_t *mesh = meshes + mi;
			size_t old = mesh->num_morph_targets, total = old + vn;
			morph_target_t *mts = REALLOC (mesh->morph_targets, total * sizeof (*mts));
			if (!mts)
			{
				bad = true;
				break;
			}
			mesh->morph_targets = mts;
			float *mws = REALLOC (mesh->morph_weights, total * sizeof (*mws));
			if (!mws)
			{
				bad = true;
				break;
			}
			mesh->morph_weights = mws;
			memset (mts + old, 0, vn * sizeof (*mts));
			memset (mws + old, 0, vn * sizeof (*mws));
			mesh->num_morph_targets = total;
			float index = hsf_bef32 (data + co + 16);
			u16 type = (u16)hsf_be16s (data + co + 0x96);
			for (u32 t = 0; t < vn; t++)
			{
				u32 ai = hsf_be32 (data + entry_off[HSF_IDX_SYMBOLS] + (u64)(vsi + t) * 4);
				morph_target_t *mt = mts + old + t;
				mt->source_kind = 2;
				if (ai >= pos_cnt || pos_hdr[ai].count != pn)
					continue;
				hsf_safe_name (mt->name, sizeof (mt->name),
					hsf_str (data, size, str_off, pos_hdr[ai].name_off), "cluster");
				mt->num_positions = mesh->num_positions;
				mt->position_deltas = CALLOC (mt->num_positions, sizeof (*mt->position_deltas));
				if (!mt->position_deltas)
				{
					bad = true;
					break;
				}
				u64 ao = (u64)pos_base + pos_hdr[ai].data_off;
				if (ao + (u64)pn * 12 > size)
					continue;
				for (u32 v = 0; v < pn; v++)
				{
					u32 bi = (u16)hsf_be16s (data + pib + v * 2);
					if (bi >= mesh->num_positions)
						continue;
					vec3_t q = { hsf_bef32 (data + ao + v * 12), hsf_bef32 (data + ao + v * 12 + 4),
						hsf_bef32 (data + ao + v * 12 + 8) };
					mt->position_deltas[bi] = (vec3_t) { q.x - mesh->positions[bi].x,
						q.y - mesh->positions[bi].y, q.z - mesh->positions[bi].z };
				}
				if (type == 2)
				{
					float w = hsf_bef32 (data + co + 20 + t * 4);
					if (isfinite (w) && w > 0)
						mws[old + t] = w;
				}
				else
				{
					int lo = (int)floorf (index);
					float f = index - lo;
					if ((int)t == lo)
						mws[old + t] = 1 - f;
					if ((int)t == lo + 1)
						mws[old + t] = f;
				}
			}
			if (type == 2)
			{
				float raw[32], sum = 0, carry = 1;
				for (u32 t = 0; t < vn; t++)
				{
					raw[t] = hsf_bef32 (data + co + 20 + t * 4);
					if (!isfinite (raw[t]))
						raw[t] = 0;
					sum += raw[t];
				}
				for (int t = vn - 1; t >= 1; t--)
				{
					float w = raw[t] < 0 ? 0 : sum > 1 ? raw[t] / sum : raw[t];
					mws[old + t] = w * carry;
					carry *= 1 - w;
				}
				mws[old] = carry;
			}
		}
	}

	FREE (pos_hdr);
	if (col_hdr)
		FREE (col_hdr);
	if (nrm_hdr)
		FREE (nrm_hdr);
	if (uv_hdr)
		FREE (uv_hdr);
	FREE (prim_hdr);

	u32 *rig_map = 0;
	if (!bad && out_n)
	{
		enumError split_err = hsf_split_material_meshes (&meshes, &out_n, &rig_map);
		if (split_err)
			bad = true;
	}

	enumError rc;
	if (bad || !out_n)
	{
		rc = ERR_NOTHING_TO_DO;
	}
	else
	{
		model_t model;
		memset (&model, 0, sizeof (model));
		model.meshes = meshes;
		model.num_meshes = out_n;
		// Filter the raw node table down to true skeleton joints and remap
		// parent_idx references (which are raw node-table indices) to the
		// compacted joint index space -- see the `ntype` comment above.
		// Only replica/instance nodes (type 1), mesh-definition nodes
		// (type 2), and camera/light nodes (mapped to ntype 7) are written
		// back separately by EncodeModelToHSF (as instances, meshes, and
		// cameras/lights respectively); every other raw node type (0, and
		// the various non-mesh/replica helper/effector kinds 3-6 some HSF
		// exporters emit for a plain skeleton) is a genuine joint used by
		// vertex skinning and must stay in model.joints, or bone indices
		// recorded by hsf_set_influences() (which reference the raw
		// node-table index space) and skin joint counts fall out of range.
#define HSF_IS_JOINT(t) ((t) != 1 && (t) != 2 && (t) < 7)
		u32 *jmap = node_cnt ? CALLOC (node_cnt, sizeof (*jmap)) : 0;
		u32 true_joint_cnt = 0;
		for (u32 i = 0; i < node_cnt; i++)
			jmap[i] = HSF_IS_JOINT (ntype[i]) ? true_joint_cnt++ : (u32)-1;
		joint_t *real_joints = true_joint_cnt ? CALLOC (true_joint_cnt, sizeof (*real_joints)) : 0;
		for (u32 i = 0, j = 0; i < node_cnt; i++)
			if (HSF_IS_JOINT (ntype[i]))
			{
				real_joints[j] = joints[i];
				s32 pi = joints[i].parent_idx;
				real_joints[j].parent_idx
					= pi >= 0 && (u32)pi < node_cnt && jmap[pi] != (u32)-1 ? (s32)jmap[pi] : -1;
				j++;
			}
		model.joints = real_joints;
		model.num_joints = true_joint_cnt;
		FREE (joints);
		joints = 0; // avoid double-free at the shared cleanup below
		FREE (ntype);
#undef HSF_IS_JOINT
		model.materials = materials;
		model.num_materials = mat_cnt;
		if (node_cnt && (u64)entry_off[HSF_IDX_NODES] + (u64)node_cnt * 0x144 <= size)
		{
			for (u32 i = 0; i < node_cnt; i++)
			{
				u32 t = hsf_be32 (data + entry_off[HSF_IDX_NODES] + (u64)i * 0x144 + 4);
				model.num_cameras += t == 7;
				model.num_lights += t == 8;
			}
			model.cameras
				= model.num_cameras ? CALLOC (model.num_cameras, sizeof (*model.cameras)) : 0;
			model.lights = model.num_lights ? CALLOC (model.num_lights, sizeof (*model.lights)) : 0;
			size_t ci = 0, li = 0;
			for (u32 i = 0; i < node_cnt; i++)
			{
				const u8 *p = data + entry_off[HSF_IDX_NODES] + (u64)i * 0x144;
				u32 t = hsf_be32 (p + 4);
				if (t == 7)
				{
					model_camera_t *c = model.cameras + ci++;
					hsf_safe_name (c->name, sizeof (c->name),
						hsf_str (data, size, str_off, hsf_be32 (p)), "camera");
					hsf_look_at_matrix (c->matrix, p + 16, hsf_bef32 (p + 40));
					c->yfov = hsf_bef32 (p + 44) * (float)M_PI / 180.0f;
					c->znear = hsf_bef32 (p + 48);
					c->zfar = hsf_bef32 (p + 52);
					if (!(c->yfov > 0 && c->yfov < (float)M_PI))
						c->yfov = (float)M_PI / 4;
					if (c->znear <= 0)
						c->znear = .1f;
				}
				else if (t == 8)
				{
					model_light_t *l = model.lights + li++;
					hsf_safe_name (l->name, sizeof (l->name),
						hsf_str (data, size, str_off, hsf_be32 (p)), "light");
					hsf_look_at_matrix (l->matrix, p + 16, 0);
					const u8 lt = p[40];
					l->kind = lt == 0 ? MODEL_LIGHT_DIRECTIONAL
						: lt == 1	  ? MODEL_LIGHT_POINT
									  : MODEL_LIGHT_SPOT;
					l->color[0] = p[41] / 255.0f;
					l->color[1] = p[42] / 255.0f;
					l->color[2] = p[43] / 255.0f;
					l->range = hsf_bef32 (p + 48);
					l->intensity = hsf_bef32 (p + 52);
					if (l->intensity <= 0)
						l->intensity = 1;
					float cutoff = hsf_bef32 (p + 56);
					if (cutoff > 0 && cutoff < 90)
						l->outer_cone = cutoff * (float)M_PI / 180.0f;
					else
						l->outer_cone = (float)M_PI / 4;
					l->inner_cone = 0;
				}
			}
		}
		if (node_cnt && (u64)entry_off[HSF_IDX_NODES] + (u64)node_cnt * 0x144 <= size)
		{
			size_t cap = 0;
			u8 *active = CALLOC (node_cnt, 1);
			for (u32 i = 0; i < node_cnt; i++)
			{
				const u8 *p = data + entry_off[HSF_IDX_NODES] + (u64)i * 0x144;
				if (hsf_be32 (p + 4) != 1)
					continue;
				float base[16];
				hsf_node_matrix (base, p);
				if (active
					&& !hsf_expand_replica (&model, &cap, data, size, entry_off, str_off, node_cnt,
						i, base, (s32)hsf_be32 (p + 16), active, 0))
					break;
			}
			FREE (active);
		}
		// hsf_expand_replica() set model.instances[i].parent_idx to the
		// replica node's raw node-table parent index; remap it into the
		// compacted joint index space, same as the joints above.
		for (u32 i = 0; i < model.num_instances; i++)
		{
			s32 pi = model.instances[i].parent_idx;
			model.instances[i].parent_idx
				= pi >= 0 && (u32)pi < node_cnt && jmap[pi] != (u32)-1 ? (s32)jmap[pi] : -1;
		}
		FREE (jmap);
		ComputeModelTRSBinds (&model);

		// CENV envelope data. Each output position receives a private influence
		// node so HSF's ranged binds can be represented without altering shared
		// source indices. Header order follows the primitive/mesh order.
		const uint rig_cnt = entry_cnt[12];
		if (rig_cnt && rig_cnt <= out_n && (u64)entry_off[12] + (u64)rig_cnt * 36 <= size)
		{
			size_t total_pos = 0;
			for (uint m = 0; m < out_n; m++)
				total_pos += meshes[m].num_positions;
			model.node_influences = CALLOC (total_pos, sizeof (*model.node_influences));
			model.num_node_influences = total_pos;
			size_t next_node = 0;
			for (uint m = 0; m < out_n; m++)
			{
				meshes[m].position_node
					= MALLOC (meshes[m].num_positions * sizeof (*meshes[m].position_node));
				for (size_t p = 0; p < meshes[m].num_positions; p++)
					meshes[m].position_node[p] = (int)next_node++;
			}
			const u64 bind_base = (u64)entry_off[12] + (u64)rig_cnt * 36;
			u64 bind_end = bind_base;
			for (uint r = 0; r < rig_cnt; r++)
			{
				const u8 *h = data + entry_off[12] + r * 36;
				const uint so = hsf_be32 (h + 4), doff = hsf_be32 (h + 8), mo = hsf_be32 (h + 12);
				const uint sn = hsf_be32 (h + 16), dn = hsf_be32 (h + 20), mn = hsf_be32 (h + 24);
				u64 e = bind_base + so + (u64)sn * 12;
				if (e > bind_end)
					bind_end = e;
				e = bind_base + doff + (u64)dn * 16;
				if (e > bind_end)
					bind_end = e;
				e = bind_base + mo + (u64)mn * 16;
				if (e > bind_end)
					bind_end = e;
			}
			u64 double_end = bind_end;
			for (uint r = 0; r < rig_cnt; r++)
			{
				const u8 *h = data + entry_off[12] + r * 36;
				const uint doff = hsf_be32 (h + 8), dn = hsf_be32 (h + 20);
				for (uint k = 0; k < dn; k++)
				{
					u64 o = bind_base + doff + (u64)k * 16;
					if (o + 16 > size)
						continue;
					u64 e = bind_end + hsf_be32 (data + o + 12) + (u64)hsf_be32 (data + o + 8) * 12;
					if (e > double_end)
						double_end = e;
				}
			}
			const u64 multi_weight_base = double_end;
			for (uint m = 0; m < out_n; m++)
			{
				const uint r = rig_map ? rig_map[m] : m;
				if (r >= rig_cnt)
					continue;
				mesh_t *mesh = meshes + m;
				const u8 *h = data + entry_off[12] + r * 36;
				const uint so = hsf_be32 (h + 4), doff = hsf_be32 (h + 8), mo = hsf_be32 (h + 12);
				const uint sn = hsf_be32 (h + 16), dn = hsf_be32 (h + 20), mn = hsf_be32 (h + 24);
				const s32 whole = (s32)hsf_be32 (h + 32);
				if (whole >= 0 && (u32)whole < node_cnt)
				{
					int b = whole;
					float w = 1;
					hsf_set_influences (&model, mesh, 0, mesh->num_positions, &b, &w, 1);
				}
				for (uint k = 0; k < sn; k++)
				{
					u64 o = bind_base + so + (u64)k * 12;
					if (o + 12 > size)
						continue;
					int b = (s32)hsf_be32 (data + o);
					float w = 1;
					const s16 first = hsf_be16s (data + o + 4), count = hsf_be16s (data + o + 6);
					if (first >= 0 && count > 0)
						hsf_set_influences (&model, mesh, first, count, &b, &w, 1);
				}
				for (uint k = 0; k < dn; k++)
				{
					u64 o = bind_base + doff + (u64)k * 16;
					if (o + 16 > size)
						continue;
					int b[2] = { (s32)hsf_be32 (data + o), (s32)hsf_be32 (data + o + 4) };
					const uint wn = hsf_be32 (data + o + 8), wo = hsf_be32 (data + o + 12);
					for (uint q = 0; q < wn; q++)
					{
						u64 x = bind_end + wo + (u64)q * 12;
						if (x + 12 > size)
							continue;
						float w0 = hsf_bef32 (data + x), w[2] = { w0, 1 - w0 };
						s16 first = hsf_be16s (data + x + 4), count = hsf_be16s (data + x + 6);
						if (first >= 0 && count > 0)
							hsf_set_influences (&model, mesh, first, count, b, w, 2);
					}
				}
				for (uint k = 0; k < mn; k++)
				{
					u64 o = bind_base + mo + (u64)k * 16;
					if (o + 16 > size)
						continue;
					uint wn = hsf_be32 (data + o);
					s16 first = hsf_be16s (data + o + 4), count = hsf_be16s (data + o + 6);
					u32 wo = hsf_be32 (data + o + 12);
					if (!wn || wn > 8 || first < 0 || count <= 0
						|| multi_weight_base + wo + (u64)wn * 8 > size)
						continue;
					int b[8];
					float w[8];
					for (uint q = 0; q < wn; q++)
					{
						const u8 *x = data + multi_weight_base + wo + q * 8;
						b[q] = (s32)hsf_be32 (x);
						w[q] = hsf_bef32 (x + 4);
					}
					hsf_set_influences (&model, mesh, first, count, b, w, wn);
				}
			}
		}

		ComputeModelTRSBinds (&model);

		hsf_build_animations (&model, data, size, entry_off, entry_cnt, str_off);
		const uint path_len = strlen (out_path);
		const bool is_dae = path_len > 4 && !strcasecmp (out_path + path_len - 4, ".dae");
		rc = (ExportModelToGLB (&model, out_path))
				== 0
			? ERR_OK
			: ERR_CANT_CREATE;
		for (size_t i = 0; i < model.num_node_influences; i++)
			FREE (model.node_influences[i].weights);
		FREE (model.node_influences);
		FREE (model.joints);
		FREE (model.instances);
		FREE (model.cameras);
		FREE (model.lights);
		for (size_t a = 0; a < model.num_animations; a++)
		{
			for (size_t c = 0; c < model.animations[a].num_channels; c++)
			{
				FREE (model.animations[a].channels[c].times);
				FREE (model.animations[a].channels[c].values);
			}
			FREE (model.animations[a].channels);
		}
		FREE (model.animations);
	}

	for (u32 i = 0; i < out_n; i++)
		hsf_free_mesh_content (meshes + i);
	FREE (meshes);
	FREE (joints);
	FREE (materials);
	FREE (tex_names);
	FREE (rig_map);
	return rc;
}

static void hsf_put32 (u8 *p, u32 v)
{
	p[0] = v >> 24;
	p[1] = v >> 16;
	p[2] = v >> 8;
	p[3] = v;
}
static void hsf_put16 (u8 *p, u16 v)
{
	p[0] = v >> 8;
	p[1] = v;
}
static void hsf_putf (u8 *p, float v)
{
	u32 x;
	memcpy (&x, &v, 4);
	hsf_put32 (p, x);
}
static u32 hsf_align32 (u32 x)
{
	return (x + 31) & ~31u;
}

static u8 hsf_float_to_u8 (float v)
{
	return v <= 0 ? 0 : v >= 1 ? 255 : (u8)(v * 255 + .5f);
}

enumError EncodeModelToHSF (const model_t *model, ccp out_path)
{
	if (!model || !out_path || !model->num_meshes || model->num_meshes > HSF_MAX_PARTS)
		return ERR_NOTHING_TO_DO;
	const u32 nm = model->num_meshes, nmat = model->num_materials;
	const u32 nnode
		= model->num_joints + nm + model->num_instances + model->num_cameras + model->num_lights;
	Image_t *tex_img = CALLOC (nmat * 8 + 1, sizeof (*tex_img));
	int *tex_map = CALLOC (nmat * 8 + 1, sizeof (*tex_map));
	u32 ntex = 0;
	u64 tex_bytes = 0;
	if (!tex_img || !tex_map)
	{
		FREE (tex_img);
		FREE (tex_map);
		return ERR_CANT_CREATE;
	}
	for (u32 i = 0; i < nmat * 8 + 1; i++)
		tex_map[i] = -1;
	char out_dir[PATH_MAX] = "";
	ccp slash = strrchr (out_path, '/');
	if (slash)
		snprintf (out_dir, sizeof (out_dir), "%.*s", (int)(slash - out_path + 1), out_path);
	for (u32 m = 0; m < nmat; m++)
		for (int l = 0; l < model->materials[m].num_textures && l < 8; l++)
		{
			ccp name = model->materials[m].textures[l];
			if (!*name)
				continue;
			Image_t src;
			InitializeIMG (&src);
			enumError e = LoadPNG (&src, false, false, name, 0);
			if (e > ERR_WARNING && *out_dir)
				e = LoadPNG (&src, false, false, out_dir, name);
			if (e <= ERR_WARNING)
			{
				Image_t dst;
				InitializeIMG (&dst);
				if (ConvertIMG (&dst, false, &src, IMG_RGBA32, PAL_INVALID) <= ERR_WARNING)
				{
					tex_img[ntex] = dst;
					tex_map[m * 8 + l] = ntex++;
					tex_bytes += dst.data_size;
				}
				else
					ResetIMG (&dst);
			}
			ResetIMG (&src);
		}
	u64 total_pos = 0, total_shape_pos = 0, total_nrm = 0, total_uv = 0, total_col = 0,
		total_tri = 0, nshape = 0, ncluster = 0, cluster_indices = 0, str_size = 1, cenv_single = 0,
		cenv_multi = 0, cenv_weights = 0, ntracks = 0, nkeys = 0;
	for (u32 m = 0; m < nm; m++)
	{
		const mesh_t *x = model->meshes + m;
		total_pos += x->num_positions;
		total_nrm += x->num_normals;
		total_uv += x->num_texcoords;
		total_col += x->num_colors[0];
		total_tri += x->num_vertices / 3;
		str_size += strlen (x->name) + 1;
		for (size_t t = 0; t < x->num_morph_targets; t++)
		{
			nshape++;
			total_shape_pos += x->num_positions;
			str_size += strlen (x->morph_targets[t].name) + 1;
			if (x->morph_targets[t].source_kind == 2)
			{
				ncluster++;
				cluster_indices += x->num_positions;
			}
		}
	}
	for (u32 i = 0; i < nmat; i++)
		str_size += strlen (model->materials[i].name) + 1;
	for (u32 i = 0; i < model->num_joints; i++)
		str_size += strlen (model->joints[i].name) + 1;
	for (u32 i = 0; i < model->num_instances; i++)
	{
		// Match the "_<meshName>" suffix strip performed below when the
		// scene/replica name is actually written -- str_size must reflect
		// the stripped length or the string pool is over-allocated and the
		// leftover bytes are written as trailing NULs, growing the file by
		// the difference on every decode->encode cycle.
		ccp full = model->instances[i].name;
		ccp mesh_nm = model->instances[i].mesh_idx >= 0 && (u32)model->instances[i].mesh_idx < nm
			? model->meshes[model->instances[i].mesh_idx].name : "";
		size_t full_len = strlen (full), mesh_len = strlen (mesh_nm);
		if (mesh_len && full_len > mesh_len + 1 && full[full_len - mesh_len - 1] == '_'
			&& !strcmp (full + full_len - mesh_len, mesh_nm))
			str_size += full_len - mesh_len - 1 + 1;
		else
			str_size += full_len + 1;
	}
	for (u32 i = 0; i < model->num_cameras; i++)
		str_size += strlen (model->cameras[i].name) + 1;
	for (u32 i = 0; i < model->num_lights; i++)
		str_size += strlen (model->lights[i].name) + 1;
	for (u32 m = 0; m < nmat; m++)
		for (int l = 0; l < 8; l++)
			if (tex_map[m * 8 + l] >= 0)
				str_size += strlen (model->materials[m].textures[l]) + 1;
	for (u32 a = 0; a < model->num_animations; a++)
	{
		str_size += strlen (model->animations[a].name) + 1;
		for (size_t c = 0; c < model->animations[a].num_channels; c++)
		{
			const model_anim_channel_t *ch = model->animations[a].channels + c;
			if (!ch->values || !ch->times || !ch->count || ch->count > 0xffff)
				continue;
			if (ch->path == MODEL_ANIM_WEIGHTS)
			{
				int mi = ch->node_idx - (int)model->num_joints;
				if (mi < 0 || (u32)mi >= nm)
					continue;
			}
			else if (ch->node_idx < 0 || (u32)ch->node_idx >= model->num_joints)
				continue;
			const u32 nc = ch->path == MODEL_ANIM_WEIGHTS ? ch->components : 3;
			ntracks += nc;
			nkeys += (u64)nc * ch->count;
		}
	}
	for (u32 m = 0; m < nm; m++)
	{
		const mesh_t *x = model->meshes + m;
		if (!x->position_node)
			continue;
		bool whole = true;
		int wb = -1;
		for (size_t p = 0; p < x->num_positions; p++)
		{
			int ni = x->position_node[p];
			const node_influence_t *in = ni >= 0 && (size_t)ni < model->num_node_influences
				? model->node_influences + ni
				: 0;
			if (!in || in->num_weights != 1)
			{
				whole = false;
				break;
			}
			if (p == 0)
				wb = in->weights[0].bone_idx;
			else if (wb != in->weights[0].bone_idx)
			{
				whole = false;
				break;
			}
		}
		if (whole)
			continue;
		for (size_t p = 0; p < x->num_positions; p++)
		{
			int ni = x->position_node[p];
			const node_influence_t *in = ni >= 0 && (size_t)ni < model->num_node_influences
				? model->node_influences + ni
				: 0;
			if (in && in->num_weights == 1)
				cenv_single++;
			else if (in && in->num_weights > 1)
			{
				cenv_multi++;
				cenv_weights += in->num_weights > 8 ? 8 : in->num_weights;
			}
		}
	}
	if (total_pos > UINT_MAX / 12 || total_tri > UINT_MAX / 48 || str_size > UINT_MAX
		|| (model->num_animations && str_size > 0x7fff))
	{
		for (u32 i = 0; i < ntex; i++)
			ResetIMG (tex_img + i);
		FREE (tex_img);
		FREE (tex_map);
		return ERR_CANT_CREATE;
	}
	u32 off[HSF_NUM_ENTRIES] = { 0 }, cnt[HSF_NUM_ENTRIES] = { 0 }, cur = 0xb0;
#define HSF_SEC(idx, count, bytes)                                                                 \
	do                                                                                             \
	{                                                                                              \
		if (count)                                                                                 \
		{                                                                                          \
			cur = hsf_align32 (cur);                                                               \
			off[idx] = cur;                                                                        \
			cnt[idx] = (count);                                                                    \
			cur += (bytes);                                                                        \
		}                                                                                          \
	} while (0)
	HSF_SEC (HSF_IDX_MATERIALS, nmat, (u64)nmat * 60);
	HSF_SEC (HSF_IDX_ATTRIBUTES, ntex, (u64)ntex * 132);
	HSF_SEC (HSF_IDX_COLORS, total_col ? nm : 0, (u64)nm * 12 + total_col * 4);
	const u32 npos = nm + nshape;
	HSF_SEC (HSF_IDX_POSITIONS, npos, (u64)npos * 12 + (total_pos + total_shape_pos) * 12);
	HSF_SEC (HSF_IDX_NORMALS, total_nrm ? nm : 0, (u64)nm * 12 + total_nrm * 12);
	HSF_SEC (HSF_IDX_UVS, total_uv ? nm : 0, (u64)nm * 12 + total_uv * 8);
	HSF_SEC (HSF_IDX_FACES, nm, (u64)nm * 12 + total_tri * 48);
	HSF_SEC (HSF_IDX_NODES, nnode, (u64)nnode * 0x144);
	HSF_SEC (HSF_IDX_TEXTURES, ntex, (u64)ntex * 32 + tex_bytes);
	HSF_SEC (HSF_IDX_MOTIONS, model->num_animations,
		(u64)model->num_animations * 16 + ntracks * 16 + nkeys * 8);
	if (model->num_node_influences)
		HSF_SEC (HSF_IDX_ENVELOPES, nm,
			(u64)nm * 36 + cenv_single * 12 + cenv_multi * 16 + cenv_weights * 8);
	HSF_SEC (HSF_IDX_PARTS, ncluster, (u64)ncluster * 12 + cluster_indices * 2);
	HSF_SEC (HSF_IDX_CLUSTERS, ncluster, (u64)ncluster * 0xa0);
	HSF_SEC (HSF_IDX_SHAPES, nshape - ncluster, (u64)(nshape - ncluster) * 12);
	cur = hsf_align32 (cur);
	off[HSF_IDX_SYMBOLS] = cur;
	cnt[HSF_IDX_SYMBOLS] = nshape + ntex;
	cur += (nshape + ntex) * 4;
	const u32 str_off = cur;
	cur += (u32)str_size;
	if ((u64)cur > UINT_MAX)
	{
		for (u32 i = 0; i < ntex; i++)
			ResetIMG (tex_img + i);
		FREE (tex_img);
		FREE (tex_map);
		return ERR_CANT_CREATE;
	}
	u8 *buf = CALLOC (cur, 1);
	if (!buf)
	{
		for (u32 i = 0; i < ntex; i++)
			ResetIMG (tex_img + i);
		FREE (tex_img);
		FREE (tex_map);
		return ERR_CANT_CREATE;
	}
	memcpy (buf, HSF_MAGIC, 8);
	for (int i = 0; i < HSF_NUM_ENTRIES; i++)
	{
		hsf_put32 (buf + 8 + i * 8, off[i]);
		hsf_put32 (buf + 12 + i * 8, cnt[i]);
	}
	hsf_put32 (buf + 0xa8, str_off);
	hsf_put32 (buf + 0xac, (u32)str_size);
	u32 sp = 1;
	buf[str_off] = 0;
	u32 *mesh_name = CALLOC (nm, sizeof (*mesh_name)),
		*mat_name = CALLOC (nmat ? nmat : 1, sizeof (*mat_name)),
		*joint_name = CALLOC (model->num_joints ? model->num_joints : 1, sizeof (*joint_name)),
		*shape_name = CALLOC (nshape ? nshape : 1, sizeof (*shape_name)),
		*scene_name = CALLOC (model->num_instances + model->num_cameras + model->num_lights + 1,
			sizeof (*scene_name)),
		*anim_name = CALLOC (model->num_animations + 1, sizeof (*anim_name)),
		*tex_name = CALLOC (ntex + 1, sizeof (*tex_name));
	if (!mesh_name || !mat_name || !joint_name || !shape_name || !scene_name || !anim_name
		|| !tex_name)
	{
		FREE (buf);
		FREE (mesh_name);
		FREE (mat_name);
		FREE (joint_name);
		FREE (shape_name);
		FREE (scene_name);
		FREE (anim_name);
		FREE (tex_name);
		for (u32 i = 0; i < ntex; i++)
			ResetIMG (tex_img + i);
		FREE (tex_img);
		FREE (tex_map);
		return ERR_CANT_CREATE;
	}
#define HSF_NAME(dst, s, fallback)                                                                 \
	do                                                                                             \
	{                                                                                              \
		ccp _s = *(s) ? (s) : (fallback);                                                          \
		dst = sp;                                                                                  \
		size_t _n = strlen (_s) + 1;                                                               \
		memcpy (buf + str_off + sp, _s, _n);                                                       \
		sp += _n;                                                                                  \
	} while (0)
	for (u32 i = 0; i < nm; i++)
		HSF_NAME (mesh_name[i], model->meshes[i].name, "mesh");
	for (u32 i = 0; i < nmat; i++)
		HSF_NAME (mat_name[i], model->materials[i].name, "material");
	for (u32 i = 0; i < model->num_joints; i++)
		HSF_NAME (joint_name[i], model->joints[i].name, "joint");
	u32 scene_idx = 0;
	for (u32 i = 0; i < model->num_instances; i++)
	{
		// hsf_expand_replica() (decode side) composes the instance name as
		// "<replicaNodeName>_<meshName>" so it is unique in the flattened
		// glTF scene. Strip that synthetic "_<meshName>" suffix back off
		// here so the HSF node name we write matches what was originally
		// decoded -- otherwise every decode -> encode cycle grows the name
		// by another "_<meshName>", breaking the canonical fixed point.
		char rn[64];
		ccp full = model->instances[i].name;
		ccp mesh_nm = model->instances[i].mesh_idx >= 0 && (u32)model->instances[i].mesh_idx < nm
			? model->meshes[model->instances[i].mesh_idx].name : "";
		size_t full_len = strlen (full), mesh_len = strlen (mesh_nm);
		if (mesh_len && full_len > mesh_len + 1 && full[full_len - mesh_len - 1] == '_'
			&& !strcmp (full + full_len - mesh_len, mesh_nm))
		{
			size_t rn_len = full_len - mesh_len - 1;
			if (rn_len >= sizeof (rn))
				rn_len = sizeof (rn) - 1;
			memcpy (rn, full, rn_len);
			rn[rn_len] = 0;
			HSF_NAME (scene_name[scene_idx++], rn, "replica");
		}
		else
			HSF_NAME (scene_name[scene_idx++], full, "replica");
	}
	for (u32 i = 0; i < model->num_cameras; i++)
		HSF_NAME (scene_name[scene_idx++], model->cameras[i].name, "camera");
	for (u32 i = 0; i < model->num_lights; i++)
		HSF_NAME (scene_name[scene_idx++], model->lights[i].name, "light");
	u32 shape_idx = 0;
	for (u32 m = 0; m < nm; m++)
		for (size_t t = 0; t < model->meshes[m].num_morph_targets; t++)
			HSF_NAME (shape_name[shape_idx++], model->meshes[m].morph_targets[t].name, "shape");
	for (u32 i = 0; i < model->num_animations; i++)
		HSF_NAME (anim_name[i], model->animations[i].name, "motion");
	for (u32 m = 0; m < nmat; m++)
		for (int l = 0; l < 8; l++)
		{
			int ti = tex_map[m * 8 + l];
			if (ti >= 0)
				HSF_NAME (tex_name[ti], model->materials[m].textures[l], "texture");
		}
	u32 sym_tex = nshape;
	for (u32 i = 0; i < nmat; i++)
	{
		const material_t *mat = model->materials + i;
		u8 *p = buf + off[HSF_IDX_MATERIALS] + i * 60;
		hsf_put32 (p, mat_name[i]);
		// HsfMaterial_s stores lit (ambient), diffuse, shadow, highlight
		// scale and inverse alpha in the fields consumed by DecodeHSF().
		// These used to remain zero, making an encode/decode cycle turn every
		// material black and fully transparent and discard its shininess.
		p[0x0b] = hsf_float_to_u8 (mat->ambient[0]);
		p[0x0c] = hsf_float_to_u8 (mat->ambient[1]);
		p[0x0d] = hsf_float_to_u8 (mat->ambient[2]);
		p[0x0e] = hsf_float_to_u8 (mat->diffuse[0]);
		p[0x0f] = hsf_float_to_u8 (mat->diffuse[1]);
		p[0x10] = hsf_float_to_u8 (mat->diffuse[2]);
		p[0x11] = hsf_float_to_u8 (mat->specular[0]);
		p[0x12] = hsf_float_to_u8 (mat->specular[1]);
		p[0x13] = hsf_float_to_u8 (mat->specular[2]);
		hsf_putf (p + 0x14, isfinite (mat->shininess) ? mat->shininess : 0);
		float alpha = isfinite (mat->diffuse[3]) ? mat->diffuse[3] : 1;
		if (alpha < 0)
			alpha = 0;
		else if (alpha > 1)
			alpha = 1;
		hsf_putf (p + 0x1c, 1 - alpha);
		u32 first = sym_tex, count = 0;
		for (int l = 0; l < 8; l++)
		{
			int ti = tex_map[i * 8 + l];
			if (ti < 0)
				continue;
			u8 *a = buf + off[HSF_IDX_ATTRIBUTES] + ti * 132;
			hsf_put32 (a, tex_name[ti]);
			const bool transformed = mat->has_tex_transform[l];
			hsf_putf (a + 0x28, transformed ? mat->tex_scale_s[l] : 1);
			hsf_putf (a + 0x2c, transformed ? mat->tex_scale_t[l] : 1);
			hsf_putf (a + 0x30, transformed ? mat->tex_translate_s[l] : 0);
			hsf_putf (a + 0x34, transformed ? mat->tex_translate_t[l] : 0);
			hsf_put32 (a + 100, model->materials[i].wrap_s[l]);
			hsf_put32 (a + 104, model->materials[i].wrap_t[l]);
			hsf_put32 (a + 128, ti);
			hsf_put32 (buf + off[HSF_IDX_SYMBOLS] + sym_tex++ * 4, ti);
			count++;
		}
		hsf_put32 (p + 52, count);
		hsf_put32 (p + 56, first);
	}
	u32 tex_data = 0;
	for (u32 i = 0; i < ntex; i++)
	{
		u8 *p = buf + off[HSF_IDX_TEXTURES] + i * 32;
		hsf_put32 (p, tex_name[i]);
		p[8] = 6;
		p[9] = 32;
		hsf_put16 (p + 10, tex_img[i].width);
		hsf_put16 (p + 12, tex_img[i].height);
		hsf_put32 (p + 20, 0xffffffff);
		hsf_put32 (p + 28, tex_data);
		memcpy (buf + off[HSF_IDX_TEXTURES] + ntex * 32 + tex_data, tex_img[i].data,
			tex_img[i].data_size);
		tex_data += tex_img[i].data_size;
	}
	// AttributeHeader arrays followed by tightly packed payloads.
	u32 pd = 0, nd = 0, ud = 0, cd = 0, fd = 0;
	for (u32 m = 0; m < nm; m++)
	{
		const mesh_t *x = model->meshes + m;
		u8 *h;
		h = buf + off[HSF_IDX_POSITIONS] + m * 12;
		hsf_put32 (h, mesh_name[m]);
		hsf_put32 (h + 4, x->num_positions);
		hsf_put32 (h + 8, pd);
		u8 *p = buf + off[HSF_IDX_POSITIONS] + npos * 12 + pd;
		for (size_t i = 0; i < x->num_positions; i++, p += 12)
		{
			hsf_putf (p, x->positions[i].x);
			hsf_putf (p + 4, x->positions[i].y);
			hsf_putf (p + 8, x->positions[i].z);
		}
		pd += x->num_positions * 12;
		if (total_nrm)
		{
			h = buf + off[HSF_IDX_NORMALS] + m * 12;
			hsf_put32 (h, mesh_name[m]);
			hsf_put32 (h + 4, x->num_normals);
			hsf_put32 (h + 8, nd);
			p = buf + off[HSF_IDX_NORMALS] + nm * 12 + nd;
			for (size_t i = 0; i < x->num_normals; i++, p += 12)
			{
				hsf_putf (p, x->normals[i].x);
				hsf_putf (p + 4, x->normals[i].y);
				hsf_putf (p + 8, x->normals[i].z);
			}
			nd += x->num_normals * 12;
		}
		if (total_uv)
		{
			h = buf + off[HSF_IDX_UVS] + m * 12;
			hsf_put32 (h, mesh_name[m]);
			hsf_put32 (h + 4, x->num_texcoords);
			hsf_put32 (h + 8, ud);
			p = buf + off[HSF_IDX_UVS] + nm * 12 + ud;
			for (size_t i = 0; i < x->num_texcoords; i++, p += 8)
			{
				hsf_putf (p, x->texcoords[i].u);
				hsf_putf (p + 4, 1.0f - x->texcoords[i].v);
			}
			ud += x->num_texcoords * 8;
		}
		if (total_col)
		{
			h = buf + off[HSF_IDX_COLORS] + m * 12;
			hsf_put32 (h, mesh_name[m]);
			hsf_put32 (h + 4, x->num_colors[0]);
			hsf_put32 (h + 8, cd);
			p = buf + off[HSF_IDX_COLORS] + nm * 12 + cd;
			for (size_t i = 0; i < x->num_colors[0]; i++, p += 4)
			{
				color4_t c = x->colors[0][i];
				p[0] = c.r <= 0 ? 0 : c.r >= 1 ? 255 : c.r * 255 + .5f;
				p[1] = c.g <= 0 ? 0 : c.g >= 1 ? 255 : c.g * 255 + .5f;
				p[2] = c.b <= 0 ? 0 : c.b >= 1 ? 255 : c.b * 255 + .5f;
				p[3] = c.a <= 0 ? 0 : c.a >= 1 ? 255 : c.a * 255 + .5f;
			}
			cd += x->num_colors[0] * 4;
		}
		h = buf + off[HSF_IDX_FACES] + m * 12;
		const u32 nt = x->num_vertices / 3;
		hsf_put32 (h, mesh_name[m]);
		hsf_put32 (h + 4, nt);
		hsf_put32 (h + 8, fd);
		p = buf + off[HSF_IDX_FACES] + nm * 12 + fd;
		for (u32 t = 0; t < nt; t++, p += 48)
		{
			hsf_put16 (p, 2);
			int mat = x->triangle_materials ? x->triangle_materials[t] : x->material_idx;
			if (mat < 0 || mat > 0xfff)
				mat = 0;
			hsf_put16 (p + 2, mat);
			for (int k = 0; k < 4; k++)
			{
				u8 *g = p + 4 + k * 8;
				if (k == 3)
				{
					memset (g, 0xff, 8);
					continue;
				}
				const vertex_t *v = x->vertices + t * 3 + k;
				hsf_put16 (g, v->position_idx);
				hsf_put16 (g + 2, v->normal_idx);
				hsf_put16 (g + 4, v->color_idx[0]);
				hsf_put16 (g + 6, v->texcoord_idx);
			}
		}
		fd += nt * 48;
	}
	for (u32 i = 0; i < model->num_joints; i++)
	{
		const joint_t *j = model->joints + i;
		u8 *p = buf + off[HSF_IDX_NODES] + (u64)i * 0x144;
		hsf_put32 (p, joint_name[i]);
		hsf_put32 (p + 4, 0);
		hsf_put32 (p + 16, j->parent_idx);
		hsf_putf (p + 28, j->translate.x);
		hsf_putf (p + 32, j->translate.y);
		hsf_putf (p + 36, j->translate.z);
		hsf_putf (p + 40, j->rotate.x);
		hsf_putf (p + 44, j->rotate.y);
		hsf_putf (p + 48, j->rotate.z);
		hsf_putf (p + 52, j->scale.x);
		hsf_putf (p + 56, j->scale.y);
		hsf_putf (p + 60, j->scale.z);
	}
	if (off[HSF_IDX_ENVELOPES])
	{
		const u32 rec_base = off[HSF_IDX_ENVELOPES] + nm * 36,
				  multi_base = rec_base + cenv_single * 12,
				  weight_base = multi_base + cenv_multi * 16;
		u32 si = 0, mi = 0, wi = 0;
		for (u32 m = 0; m < nm; m++)
		{
			const mesh_t *x = model->meshes + m;
			u8 *h = buf + off[HSF_IDX_ENVELOPES] + m * 36;
			hsf_put32 (h, mesh_name[m]);
			hsf_put32 (h + 32, 0xffffffff);
			if (!x->position_node)
				continue;
			bool whole = true;
			int wb = -1;
			for (size_t p = 0; p < x->num_positions; p++)
			{
				int ni = x->position_node[p];
				const node_influence_t *in = ni >= 0 && (size_t)ni < model->num_node_influences
					? model->node_influences + ni
					: 0;
				if (!in || in->num_weights != 1)
				{
					whole = false;
					break;
				}
				if (!p)
					wb = in->weights[0].bone_idx;
				else if (wb != in->weights[0].bone_idx)
				{
					whole = false;
					break;
				}
			}
			if (whole)
			{
				hsf_put32 (h + 32, wb);
				continue;
			}
			u32 first_si = si, first_mi = mi;
			for (size_t p = 0; p < x->num_positions; p++)
			{
				int ni = x->position_node[p];
				const node_influence_t *in = ni >= 0 && (size_t)ni < model->num_node_influences
					? model->node_influences + ni
					: 0;
				if (!in || !in->num_weights)
					continue;
				if (in->num_weights == 1)
				{
					u8 *r = buf + rec_base + si++ * 12;
					hsf_put32 (r, in->weights[0].bone_idx);
					hsf_put16 (r + 4, p);
					hsf_put16 (r + 6, 1);
				}
				else
				{
					u32 nw = in->num_weights > 8 ? 8 : in->num_weights;
					u8 *r = buf + multi_base + mi++ * 16;
					hsf_put32 (r, nw);
					hsf_put16 (r + 4, p);
					hsf_put16 (r + 6, 1);
					hsf_put32 (r + 12, wi * 8);
					for (u32 k = 0; k < nw; k++)
					{
						u8 *w = buf + weight_base + (wi++) * 8;
						hsf_put32 (w, in->weights[k].bone_idx);
						hsf_putf (w + 4, in->weights[k].weight);
					}
				}
			}
			hsf_put32 (h + 4, first_si * 12);
			hsf_put32 (h + 12, first_mi * 16 + cenv_single * 12);
			hsf_put32 (h + 16, si - first_si);
			hsf_put32 (h + 24, mi - first_mi);
		}
	}
	shape_idx = 0;
	u32 shape_data = pd, shape_rec = 0, cluster_rec = 0, part_index_off = 0;
	for (u32 m = 0; m < nm; m++)
	{
		const mesh_t *x = model->meshes + m;
		u8 *p = buf + off[HSF_IDX_NODES] + (u64)(model->num_joints + m) * 0x144;
		hsf_put32 (p, mesh_name[m]);
		hsf_put32 (p + 4, 2);
		hsf_put32 (p + 16, 0xffffffff);
		hsf_putf (p + 52, 1);
		hsf_putf (p + 56, 1);
		hsf_putf (p + 60, 1);
		u32 ordinary = 0;
		while (ordinary < x->num_morph_targets && x->morph_targets[ordinary].source_kind != 2)
			ordinary++;
		hsf_put32 (p + 292, ordinary);
		hsf_put32 (p + 296, shape_idx);
		for (size_t t = 0; t < x->num_morph_targets; t++, shape_idx++)
		{
			const morph_target_t *mt = x->morph_targets + t;
			u8 *h = buf + off[HSF_IDX_POSITIONS] + (nm + shape_idx) * 12;
			hsf_put32 (h, shape_name[shape_idx]);
			hsf_put32 (h + 4, x->num_positions);
			hsf_put32 (h + 8, shape_data);
			u8 *q = buf + off[HSF_IDX_POSITIONS] + npos * 12 + shape_data;
			for (size_t v = 0; v < x->num_positions; v++, q += 12)
			{
				vec3_t d = { 0, 0, 0 };
				if (mt->position_deltas && v < mt->num_positions)
					d = mt->position_deltas[v];
				hsf_putf (q, x->positions[v].x + d.x);
				hsf_putf (q + 4, x->positions[v].y + d.y);
				hsf_putf (q + 8, x->positions[v].z + d.z);
			}
			shape_data += x->num_positions * 12;
			hsf_put32 (buf + off[HSF_IDX_SYMBOLS] + shape_idx * 4, nm + shape_idx);
			if (mt->source_kind == 2)
			{
				u8 *part = buf + off[HSF_IDX_PARTS] + cluster_rec * 12;
				hsf_put32 (part, shape_name[shape_idx]);
				hsf_put32 (part + 4, x->num_positions);
				hsf_put32 (part + 8, part_index_off);
				u8 *ix = buf + off[HSF_IDX_PARTS] + ncluster * 12 + (u64)part_index_off * 2;
				for (u32 v = 0; v < x->num_positions; v++)
					hsf_put16 (ix + v * 2, v);
				part_index_off += x->num_positions;
				u8 *cl = buf + off[HSF_IDX_CLUSTERS] + cluster_rec * 0xa0;
				hsf_put32 (cl, shape_name[shape_idx]);
				hsf_put32 (cl + 4, shape_name[shape_idx]);
				hsf_put32 (cl + 8, mesh_name[m]);
				hsf_put32 (cl + 12, cluster_rec);
				hsf_putf (cl + 16, 0);
				hsf_put16 (cl + 0x96, 0);
				hsf_put32 (cl + 0x98, 1);
				hsf_put32 (cl + 0x9c, shape_idx);
				cluster_rec++;
			}
			else
			{
				u8 *s = buf + off[HSF_IDX_SHAPES] + shape_rec++ * 12;
				hsf_put32 (s, shape_name[shape_idx]);
				hsf_put16 (s + 4, 2);
				hsf_put16 (s + 6, 1);
				hsf_put32 (s + 8, shape_idx);
			}
			if (t < 33 && x->morph_weights)
				hsf_putf (p + 128 + t * 4, x->morph_weights[t]);
		}
	}
	u32 no = model->num_joints + nm;
	scene_idx = 0;
	for (u32 i = 0; i < model->num_instances; i++, no++)
	{
		const model_instance_t *in = model->instances + i;
		u8 *p = buf + off[HSF_IDX_NODES] + (u64)no * 0x144;
		hsf_put32 (p, scene_name[scene_idx++]);
		hsf_put32 (p + 4, 1);
		hsf_put32 (p + 16, in->parent_idx);
		hsf_put32 (p + 0x64,
			in->mesh_idx >= 0 && (u32)in->mesh_idx < nm ? model->num_joints + in->mesh_idx
														: 0xffffffff);
		if (in->has_matrix)
		{
			float sx = hypotf (hypotf (in->matrix[0], in->matrix[1]), in->matrix[2]),
				  sy = hypotf (hypotf (in->matrix[4], in->matrix[5]), in->matrix[6]),
				  sz = hypotf (hypotf (in->matrix[8], in->matrix[9]), in->matrix[10]);
			if (sx < 1e-8f)
				sx = 1;
			if (sy < 1e-8f)
				sy = 1;
			if (sz < 1e-8f)
				sz = 1;
			float r20 = in->matrix[2] / sx, ry = asinf (fmaxf (-1, fminf (1, -r20))),
				  rx = atan2f (in->matrix[6] / sy, in->matrix[10] / sz),
				  rz = atan2f (in->matrix[1] / sx, in->matrix[0] / sx);
			hsf_putf (p + 28, in->matrix[12]);
			hsf_putf (p + 32, in->matrix[13]);
			hsf_putf (p + 36, in->matrix[14]);
			hsf_putf (p + 40, rx * 180 / M_PI);
			hsf_putf (p + 44, ry * 180 / M_PI);
			hsf_putf (p + 48, rz * 180 / M_PI);
			hsf_putf (p + 52, sx);
			hsf_putf (p + 56, sy);
			hsf_putf (p + 60, sz);
		}
		else
		{
			hsf_putf (p + 28, in->translate.x);
			hsf_putf (p + 32, in->translate.y);
			hsf_putf (p + 36, in->translate.z);
			hsf_putf (p + 40, in->rotate.x);
			hsf_putf (p + 44, in->rotate.y);
			hsf_putf (p + 48, in->rotate.z);
			hsf_putf (p + 52, in->scale.x);
			hsf_putf (p + 56, in->scale.y);
			hsf_putf (p + 60, in->scale.z);
		}
	}
	for (u32 i = 0; i < model->num_cameras; i++, no++)
	{
		const model_camera_t *c = model->cameras + i;
		u8 *p = buf + off[HSF_IDX_NODES] + (u64)no * 0x144;
		hsf_put32 (p, scene_name[scene_idx++]);
		hsf_put32 (p + 4, 7);
		for (int k = 0; k < 3; k++)
		{
			hsf_putf (p + 16 + k * 4, c->matrix[12 + k]);
			hsf_putf (p + 28 + k * 4, c->matrix[12 + k] - c->matrix[8 + k]);
		}
		hsf_putf (p + 44, c->yfov * 180 / M_PI);
		hsf_putf (p + 48, c->znear);
		hsf_putf (p + 52, c->zfar);
	}
	for (u32 i = 0; i < model->num_lights; i++, no++)
	{
		const model_light_t *l = model->lights + i;
		u8 *p = buf + off[HSF_IDX_NODES] + (u64)no * 0x144;
		hsf_put32 (p, scene_name[scene_idx++]);
		hsf_put32 (p + 4, 8);
		for (int k = 0; k < 3; k++)
		{
			hsf_putf (p + 16 + k * 4, l->matrix[12 + k]);
			hsf_putf (p + 28 + k * 4, l->matrix[12 + k] - l->matrix[8 + k]);
		}
		p[40] = l->kind == MODEL_LIGHT_DIRECTIONAL ? 0 : l->kind == MODEL_LIGHT_POINT ? 1 : 2;
		p[41] = l->color[0] * 255 + .5f;
		p[42] = l->color[1] * 255 + .5f;
		p[43] = l->color[2] * 255 + .5f;
		hsf_putf (p + 48, l->range);
		hsf_putf (p + 52, l->intensity);
		hsf_putf (p + 56, l->outer_cone * 180 / M_PI);
	}
	if (off[HSF_IDX_MOTIONS])
	{
		const u32 track_base = off[HSF_IDX_MOTIONS] + model->num_animations * 16,
				  key_base = track_base + ntracks * 16;
		u32 ti = 0, ki = 0;
		for (u32 a = 0; a < model->num_animations; a++)
		{
			const model_animation_t *an = model->animations + a;
			u8 *mh = buf + off[HSF_IDX_MOTIONS] + a * 16;
			u32 first = ti, max_track = 0;
			float max_frame = 0;
			hsf_put32 (mh, anim_name[a]);
			for (size_t c = 0; c < an->num_channels; c++)
			{
				const model_anim_channel_t *ch = an->channels + c;
				if (!ch->values || !ch->times || !ch->count || ch->count > 0xffff)
					continue;
				u32 nc = ch->path == MODEL_ANIM_WEIGHTS ? ch->components : 3;
				u32 target = 0;
				int mode = 2;
				if (ch->path == MODEL_ANIM_WEIGHTS)
				{
					mode = 3;
					int mi = ch->node_idx - (int)model->num_joints;
					if (mi < 0 || (u32)mi >= nm)
						continue;
					target = mesh_name[mi];
				}
				else
				{
					if (ch->node_idx < 0 || (u32)ch->node_idx >= model->num_joints)
						continue;
					target = joint_name[ch->node_idx];
				}
				if (target > 0x7fff)
					continue;
				for (u32 comp = 0; comp < nc; comp++)
				{
					u8 *tr = buf + track_base + ti++ * 16;
					tr[0] = mode;
					hsf_put16 (tr + 2, target);
					hsf_put16 (tr + 4, 0);
					int effect = ch->path == MODEL_ANIM_TRANSLATION ? 8 + comp
						: ch->path == MODEL_ANIM_ROTATION			? 28 + comp
						: ch->path == MODEL_ANIM_SCALE				? 31 + comp
																	: comp;
					hsf_put16 (tr + 6, effect);
					hsf_put16 (tr + 8, 1);
					hsf_put16 (tr + 10, ch->count);
					hsf_put32 (tr + 12, ki * 8);
					for (size_t k = 0; k < ch->count; k++)
					{
						float frame = ch->times[k] * 60;
						if (frame > max_frame)
							max_frame = frame;
						float val = 0;
						if (ch->path == MODEL_ANIM_ROTATION)
						{
							const float *q = ch->values + k * 4;
							float sinr = 2 * (q[3] * q[0] + q[1] * q[2]),
								  cosr = 1 - 2 * (q[0] * q[0] + q[1] * q[1]);
							float e[3];
							e[0] = atan2f (sinr, cosr);
							float sinp = 2 * (q[3] * q[1] - q[2] * q[0]);
							e[1] = fabsf (sinp) >= 1 ? copysignf (M_PI / 2, sinp) : asinf (sinp);
							e[2] = atan2f (2 * (q[3] * q[2] + q[0] * q[1]),
								1 - 2 * (q[1] * q[1] + q[2] * q[2]));
							val = e[comp] * 180 / M_PI;
						}
						else
							val = ch->values[k * ch->components + comp];
						u8 *key = buf + key_base + (ki++) * 8;
						hsf_putf (key, frame);
						hsf_putf (key + 4, val);
					}
				}
			}
			max_track = ti - first;
			hsf_put32 (mh + 4, max_track);
			hsf_put32 (mh + 8, first * 16);
			hsf_putf (mh + 12, max_frame);
		}
	}
	enumError rc = SaveFILE (out_path, 0, true, buf, cur, 0);
	FREE (buf);
	FREE (mesh_name);
	FREE (mat_name);
	FREE (joint_name);
	FREE (shape_name);
	FREE (scene_name);
	FREE (anim_name);
	FREE (tex_name);
	for (u32 i = 0; i < ntex; i++)
		ResetIMG (tex_img + i);
	FREE (tex_img);
	FREE (tex_map);
#undef HSF_NAME
#undef HSF_SEC
	return rc;
}
