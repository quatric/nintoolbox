// lib-nsbanim.c -- Nintendo DS NSB animation family decoder/encoder.
//
// Handles the five NSB*.0 animation containers used by NNSG3d on the DS:
//   NSBCA (.nsbca / BCA0 / JNT0) -- joint SRT animation
//   NSBTA (.nsbta / BTA0 / SRT0) -- texture SRT animation
//   NSBTP (.nsbtp / BTP0 / PAT0) -- texture pattern animation
//   NSBVA (.nsbva / BVA0 / VIS0) -- visibility animation
//   NSBMA (.nsbma / BMA0 / MAT0) -- material colour animation
//
// They all share one container: a NNSG3dResFileHeader followed by u32 block
// offsets, each block being a NNSG3dResDataBlockHeader + a NNSG3dResDict
// whose entries reference the actual animation units.  See the Nitro SDK
// binres structures (NNSG3dResDict, NNSG3dResJntAnm, NNSG3dResVisAnm,
// NNSG3dResMatCAnm, ...) which this file mirrors closely.
//
// Only BCA0 has a meaningful glTF representation: decoded into model_t node
// TRS channels and exported as a GLB animation.  The other four have no
// glTF equivalent, so they are parsed (for validation/extraction and
// sibling detection) and re-encoded byte-exactly (pass-through).

#include "lib-nsbanim.h"
#include "lib-model-glb.h"
#include "lib-std.h"
#include "dclib-file.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <dirent.h>
#include <math.h>

#undef calloc
#undef malloc
#undef realloc
#undef free

//-----------------------------------------------------------------------------
// Little-endian readers
//-----------------------------------------------------------------------------

static uint32_t rd32le (const uint8_t *p)
{
	return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static int32_t rds32le (const uint8_t *p)
{
	return (int32_t)rd32le (p);
}
static uint16_t rd16le (const uint8_t *p)
{
	return (uint16_t)p[0] | (uint16_t)p[1] << 8;
}
static int16_t rds16le (const uint8_t *p)
{
	return (int16_t)rd16le (p);
}
static float fx12 (int v)
{
	return (float)v / 4096.0f;
}

//-----------------------------------------------------------------------------
// Container + block dictionary
//-----------------------------------------------------------------------------

typedef struct
{
	const uint8_t *file;
	size_t file_size;
	uint32_t block_off;
	uint32_t num_blocks;
} nsb_container_t;

static int nsb_container (nsb_container_t *c, const uint8_t *data, size_t size)
{
	if (size < 0x10 || rd16le (data + 4) != 0xFEFF)
		return 0;
	const uint16_t hsz = rd16le (data + 0x0c);
	const uint16_t nblk = rd16le (data + 0x0e);
	if (hsz < 0x10 || nblk == 0 || (size_t)hsz + (size_t)nblk * 4 > size)
		return 0;
	memset (c, 0, sizeof (*c));
	c->file = data;
	c->file_size = size;
	c->block_off = hsz;
	c->num_blocks = nblk;
	return 1;
}

static int nsb_block_off (const nsb_container_t *c, uint32_t idx, uint32_t *off, uint32_t *size)
{
	if (idx >= c->num_blocks)
		return 0;
	uint32_t o = rd32le (c->file + c->block_off + idx * 4);
	if ((size_t)o + 8 > c->file_size)
		return 0;
	if (off) *off = o;
	if (size) *size = rd32le (c->file + o + 4);
	return 1;
}

// NNSG3dResDict
typedef struct
{
	uint32_t n;
	uint32_t dict_off;
	uint32_t ofs_entry;
} nsb_dict_t;

static int nsb_dict (const nsb_container_t *c, uint32_t block_off, nsb_dict_t *d)
{
	const uint8_t *b = c->file + block_off;
	if (block_off + 0x10 > c->file_size)
		return 0;
	const uint32_t n = b[1];
	if (b[0] > 1 || n == 0)
		return 0;
	const uint32_t size_blk = rd16le (b + 2);
	const uint32_t ofs_entry = rd16le (b + 6);
	if (block_off + size_blk > c->file_size || (int)ofs_entry < (int)(8 + (2 * n - 1)))
		return 0;
	d->n = n;
	d->dict_off = block_off;
	d->ofs_entry = block_off + ofs_entry;
	return 1;
}

static int nsb_dict_unit_offset (const nsb_container_t *c, const nsb_dict_t *d, uint32_t i, uint32_t *unit_off)
{
	if (i >= d->n)
		return 0;
	uint32_t eo = d->ofs_entry + i * 8;
	if (eo + 8 > c->file_size)
		return 0;
	*unit_off = rd32le (c->file + eo + 4);
	return 1;
}

//-----------------------------------------------------------------------------
// BCA0 (JNT0) joint SRT animation -> model_t TRS channels
//-----------------------------------------------------------------------------
//
// NNSG3dResJntAnm header (20 bytes), then u16 ofsTag[numNode] each pointing
// (from the JntAnm base) at a node's NNSG3dResJntAnmSRTTag { u32 tag } followed
// by the packed SRT value blocks.  The sampler below is a faithful port of
// the Nitro SDK for integer frames.
//-----------------------------------------------------------------------------

#define SRT_IDENTITY    0x0001
#define SRT_IDENTITY_T  0x0002
#define SRT_BASE_T      0x0004
#define SRT_CONST_TX    0x0008
#define SRT_CONST_TY    0x0010
#define SRT_CONST_TZ    0x0020
#define SRT_IDENTITY_R  0x0040
#define SRT_BASE_R      0x0080
#define SRT_CONST_R     0x0100
#define SRT_IDENTITY_S  0x0200
#define SRT_BASE_S      0x0400
#define SRT_CONST_SX    0x0800
#define SRT_CONST_SY    0x1000
#define SRT_CONST_SZ    0x2000

#define XSTEP_MASK   0xc0000000u
#define XSTEP_2      0x40000000u
#define XSTEP_4      0x80000000u
#define XFX16        0x20000000u
#define XLAST_MASK   0x1fff0000u
#define XLAST_SHIFT  16

#define RIDX_PIVOT   0x8000u
#define RIDX_IDXDATA 0x7fffu

static const uint8_t pivot_util_[9][4] = {
	{ 4, 5, 7, 8 }, { 3, 5, 6, 8 }, { 3, 4, 6, 7 }, { 1, 2, 7, 8 }, { 0, 2, 6, 8 },
	{ 0, 1, 6, 7 }, { 1, 2, 4, 5 }, { 0, 2, 3, 5 }, { 0, 1, 3, 4 }
};

static void rot_by_idx (const uint8_t *jt, uint32_t ofs_rot3, uint32_t ofs_rot5, uint16_t ridx, float m[9])
{
	if (ridx & RIDX_PIVOT)
	{
		const uint8_t *p = jt + ofs_rot3 + (ridx & RIDX_IDXDATA) * 6u;
		int16_t d0 = rds16le (p);
		int16_t A = rds16le (p + 2);
		int16_t B = rds16le (p + 4);
		uint32_t pv = (uint32_t)d0 & 0xf;
		memset (m, 0, sizeof (float) * 9);
		// SDK getRotDataByIdx_: A/B are fx16 -> fx32 (FX12) matrix cells read
		// straight into the (fx12) result; the pivot diagonal is +/-FX32_ONE.
		// In float terms the cells stay across the diagonal at +/-(1/4096).
		m[pv] = (d0 & 0x10) ? -1.0f : 1.0f;
		m[pivot_util_[pv][0]] = fx12 (A);
		m[pivot_util_[pv][1]] = fx12 (B);
		m[pivot_util_[pv][2]] = (d0 & 0x20) ? -fx12 (B) : fx12 (B);
		m[pivot_util_[pv][3]] = (d0 & 0x40) ? -fx12 (A) : fx12 (A);
	}
	else
	{
		const uint8_t *p = jt + ofs_rot5 + (ridx & RIDX_IDXDATA) * 10u;
		int16_t d0 = rds16le (p), d1 = rds16le (p + 2), d2 = rds16le (p + 4), d3 = rds16le (p + 6), d4 = rds16le (p + 8);
		int16_t _12 = (int16_t)(d4 & 7);
		int16_t m11 = (int16_t)(d4 >> 3);
		_12 = (int16_t)((_12 << 3) | (d0 & 7));
		int16_t m00 = (int16_t)(d0 >> 3);
		_12 = (int16_t)((_12 << 3) | (d1 & 7));
		int16_t m01 = (int16_t)(d1 >> 3);
		_12 = (int16_t)((_12 << 3) | (d2 & 7));
		int16_t m02 = (int16_t)(d2 >> 3);
		_12 = (int16_t)((_12 << 3) | (d3 & 7));
		int16_t m10 = (int16_t)(d3 >> 3);
		int16_t m12 = (int16_t)((int16_t)(_12 << 3) >> 3);
		// Cells are signed 13-bit FX12 like the SDK reads them; convert to
		// float before the row-2 cross product (which then needs no shift).
		float a0 = fx12 (m00), a1 = fx12 (m01), a2 = fx12 (m02), b0 = fx12 (m10), b1 = fx12 (m11), b2 = fx12 (m12);
		m[0] = a0; m[1] = a1; m[2] = a2;
		m[3] = b0; m[4] = b1; m[5] = b2;
		m[6] = a1 * b2 - a2 * b1;
		m[7] = a2 * b0 - a0 * b2;
		m[8] = a0 * b1 - a1 * b0;
	}
}

static void quat_from_mat (const float m[9], float q[4])
{
	float tr = m[0] + m[4] + m[8];
	if (tr > 0.0f)
	{
		float s = 2.0f * (float)sqrt (tr + 1.0f);
		q[0] = 0.25f * s;
		q[1] = (m[7] - m[5]) / s;
		q[2] = (m[2] - m[6]) / s;
		q[3] = (m[3] - m[1]) / s;
	}
	else
	{
		int i = (m[4] > m[0]) ? ((m[8] > m[4]) ? 2 : 1) : ((m[8] > m[0]) ? 2 : 0);
		static const int nxt[3] = { 1, 2, 0 };
		int j = nxt[i], k = nxt[j];
		float s = (float)sqrt (((m[i * 3 + i] - m[j * 3 + j] - m[k * 3 + k]) + 1.0f) * 2.0f);
		q[i + 1] = 0.5f * s;
		s = 0.5f / s;
		q[0] = (m[j * 3 + k] - m[k * 3 + j]) * s;
		q[j + 1] = (m[i * 3 + j] + m[j * 3 + i]) * s;
		q[k + 1] = (m[i * 3 + k] + m[k * 3 + i]) * s;
	}
	if (q[0] < 0.0f)
		for (int n = 0; n < 4; n++) q[n] = -q[n];
	{
		float len = 0.0f;
		for (int n = 0; n < 4; n++) len += q[n] * q[n];
		if (len > 0.0f)
		{
			len = (float)sqrt (len);
			for (int n = 0; n < 4; n++) q[n] /= len;
		}
	}
}

// Sample translation at integer frame (SDK getTransData_).  Both fx16 and
// fx32 arrays are 4096ths; return value is scaled to real units.
static float trans_sample (const uint8_t *jt, uint32_t info, uint32_t offset, uint32_t frame)
{
	const uint8_t *arr = jt + offset;
	const int fx16 = !!(info & XFX16);
	if (!(info & XSTEP_MASK))
		return fx16 ? fx12 (rds16le (arr + frame * 2)) : fx12 (rds32le (arr + frame * 4));
	uint32_t last = (info & XLAST_MASK) >> XLAST_SHIFT;
	if (info & XSTEP_2)
	{
		uint32_t idx = (frame & 1) ? ((frame >> 1) + ((frame > last) ? 1 : 0)) : (frame >> 1);
		return fx16 ? fx12 (rds16le (arr + idx * 2)) : fx12 (rds32le (arr + idx * 4));
	}
	else
	{
		if (frame & 3)
		{
			uint32_t idx = (frame > last) ? ((last >> 2) + (frame & 3)) : (frame >> 2);
			return fx16 ? fx12 (rds16le (arr + idx * 2)) : fx12 (rds32le (arr + idx * 4));
		}
		uint32_t idx = frame >> 2;
		return fx16 ? fx12 (rds16le (arr + idx * 2)) : fx12 (rds32le (arr + idx * 4));
	}
}

// Append a freshly-allocated (times, values) pair to the animation as a new
// channel; ownership transfers to the model on success.
static model_anim_channel_t *anim_add_channel (model_animation_t *a, int node_idx,
	model_anim_path_t path, float *times, float *values, size_t count, size_t components)
{
	model_anim_channel_t *nc = realloc (a->channels, (a->num_channels + 1) * sizeof (model_anim_channel_t));
	if (!nc)
		return 0;
	a->channels = nc;
	model_anim_channel_t *c = &a->channels[a->num_channels++];
	memset (c, 0, sizeof (*c));
	c->node_idx = node_idx;
	c->path = path;
	c->times = times;
	c->values = values;
	c->count = count;
	c->components = components;
	return c;
}

//-----------------------------------------------------------------------------
// Raw-byte retention (pass-through encode support)
//-----------------------------------------------------------------------------
//
// Every Parse*IntoModel keeps a copy of the source bytes on the model so the
// matching Encode* can reproduce an unchanged animation byte-exactly.  The
// copy is attached with the clip's name; a duplicate (same kind + name) is
// ignored -- the first capture wins.
//-----------------------------------------------------------------------------

static int nsb_attach_raw (model_t *model, model_nsb_kind_t kind, const char *clip_name,
	const uint8_t *data, size_t size)
{
	if (!model || !data || !size)
		return 0;
	for (size_t i = 0; i < model->num_nsb_raw; i++)
		if (model->nsb_raw[i].kind == kind
			&& !strcmp (model->nsb_raw[i].name, clip_name && clip_name[0] ? clip_name : "NSB"))
			return 1;
	model_nsb_raw_t *n = realloc (model->nsb_raw, (model->num_nsb_raw + 1) * sizeof (model_nsb_raw_t));
	if (!n)
		return 0;
	model->nsb_raw = n;
	model_nsb_raw_t *r = &model->nsb_raw[model->num_nsb_raw];
	memset (r, 0, sizeof (*r));
	r->kind = kind;
	snprintf (r->name, sizeof (r->name), "%s", clip_name && clip_name[0] ? clip_name : "NSB");
	r->data = malloc (size);
	if (!r->data)
		return 0;
	memcpy (r->data, data, size);
	r->size = size;
	model->num_nsb_raw++;
	return 1;
}

static const model_nsb_raw_t *nsb_find_raw (const model_t *model, model_nsb_kind_t kind)
{
	if (!model)
		return 0;
	for (size_t i = 0; i < model->num_nsb_raw; i++)
		if (model->nsb_raw[i].kind == kind)
			return &model->nsb_raw[i];
	return 0;
}

static uint8_t *nsb_raw_copy (const model_nsb_raw_t *r, size_t *out_size)
{
	if (!r || !r->data || !r->size)
		return 0;
	uint8_t *b = malloc (r->size);
	if (!b)
		return 0;
	memcpy (b, r->data, r->size);
	if (out_size)
		*out_size = r->size;
	return b;
}

int ParseNSBCAIntoModel (model_t *model, const uint8_t *data, size_t size, const char *clip_name)
{
	if (!model || !data || size < 0x10)
		return 0;
	nsb_container_t c;
	if (!nsb_container (&c, data, size))
		return 0;
	uint32_t blk_off, blk_size;
	if (!nsb_block_off (&c, 0, &blk_off, &blk_size) || memcmp (c.file + blk_off, "JNT0", 4))
		return 0;

	// Keep the source bytes for byte-exact pass-through re-encoding.
	nsb_attach_raw (model, NSB_RAW_BCA0,
		clip_name && clip_name[0] ? clip_name : "NSB", data, size);

	nsb_dict_t top;
	if (!nsb_dict (&c, blk_off + 8, &top))
		return 0;

	int added = 0;

	// Files in the NDS tree are one-clip-per-file; decode the first clip.
	for (uint32_t u = 0; u < top.n && u < 1; u++)
	{
		uint32_t unit_rel;
		if (!nsb_dict_unit_offset (&c, &top, u, &unit_rel))
			continue;
		uint32_t ja = blk_off + unit_rel;
		if (ja + 20 > size || data[ja] != 'J' || rd16le (data + ja + 2) != 0x4341)
			continue;
		const uint32_t num_frame = rd16le (data + ja + 4);
		const uint32_t num_node = rd16le (data + ja + 6);
		const uint32_t ofs_rot3 = rd32le (data + ja + 12);
		const uint32_t ofs_rot5 = rd32le (data + ja + 16);
		if (num_frame == 0 || num_node == 0)
			continue;

		model_animation_t anim;
		memset (&anim, 0, sizeof (anim));
		snprintf (anim.name, sizeof (anim.name), "%s",
			clip_name && clip_name[0] ? clip_name : "NSB");

		for (uint32_t node = 0; node < num_node; node++)
		{
			if ((size_t)(ja + 20 + node * 2 + 2) > size)
				break;
			uint32_t tag_ofs = rd16le (data + ja + 20 + node * 2);
			uint32_t st = ja + tag_ofs;
			if (st + 4 > size)
				break;
			const uint32_t tag = rd32le (data + st);
			const uint32_t node_map = (tag >> 24) & 0xff;
			// The tag's high byte is the model-node (joint) index this SRT
			// block animates; controllers map model node -> animation data via
			// that byte, and identity (byte == animation index) is the norm.
			const int node_idx = (node_map < model->num_joints) ? (int)node_map : -1;

			// Single cursor advancing through the SRT value blocks exactly as
			// the SDK does (see NNSi_G3dAnmCalcNsBca / getJntSRTAnmResult_).
			uint32_t p = st + 4;

			// Master IDENTITY (bit 0): entire node uses bind-pose identity
			// values; no data blocks follow the tag.  Skip entirely.
			if (!(tag & SRT_IDENTITY))
			{

			// --- Translation ---
			if (!(tag & (SRT_IDENTITY_T | SRT_BASE_T)))
			{
				const int const_ax[3] = { !!(tag & SRT_CONST_TX), !!(tag & SRT_CONST_TY), !!(tag & SRT_CONST_TZ) };
				float const_v[3] = { 0, 0, 0 };
				uint32_t info[3], off[3];
				for (int ax = 0; ax < 3; ax++)
				{
					const uint32_t base = p;
					(void)base;
					if (const_ax[ax])
					{
						const_v[ax] = fx12 (rds32le (data + p));
						p += 4;
					}
					else if (p + 8 <= size)
					{
						info[ax] = rd32le (data + p);
						off[ax] = rd32le (data + p + 4);
						p += 8;
					}
					else
						break;
				}
				float *times = malloc (sizeof (float) * num_frame);
				float *vals = malloc (sizeof (float) * num_frame * 3);
				if (times && vals)
				{
					size_t ok = 0;
					for (uint32_t f = 0; f < num_frame; f++)
					{
						if (const_ax[0] && const_ax[1] && const_ax[2] && 0) {}
						if (!const_ax[0] && (size_t)(ja + off[0]) + 4 > size) break;
						if (!const_ax[1] && (size_t)(ja + off[1]) + 4 > size) break;
						if (!const_ax[2] && (size_t)(ja + off[2]) + 4 > size) break;
						times[ok] = f / 60.0f;
						for (int ax = 0; ax < 3; ax++)
							vals[ok * 3 + ax] = const_ax[ax]
								? const_v[ax] : trans_sample (data + ja, info[ax], off[ax], f);
						ok++;
					}
					if (ok)
					{
						anim_add_channel (&anim, node_idx, MODEL_ANIM_TRANSLATION, times, vals, ok, 3);
						added++;
					}
					else
					{
						free (times);
						free (vals);
					}
				}
				else
				{
					free (times);
					free (vals);
				}
			}

			// --- Rotation ---
			if (!(tag & (SRT_IDENTITY_R | SRT_BASE_R)))
			{
				if (tag & SRT_CONST_R)
				{
					if (p + 4 <= size)
					{
						uint16_t ridx = (uint16_t)rd32le (data + p);
						// Const rotation stores one full RIDX (4 bytes); the
						// SDK advances past it (pData += 1 in the const case).
						p += 4;
						float m[9], q[4];
						rot_by_idx (data + ja, ofs_rot3, ofs_rot5, ridx, m);
						quat_from_mat (m, q);
						float *times = malloc (sizeof (float) * num_frame);
						float *vals = malloc (sizeof (float) * num_frame * 4);
						if (times && vals)
						{
							size_t ok = 0;
							for (uint32_t f = 0; f < num_frame; f++)
							{
								times[ok] = f / 60.0f;
								memcpy (vals + ok * 4, q, sizeof (q));
								ok++;
							}
							anim_add_channel (&anim, node_idx, MODEL_ANIM_ROTATION, times, vals, ok, 4);
							added++;
						}
						else
						{
							free (times);
							free (vals);
						}
					}
				}
				else if (p + 8 <= size)
				{
					uint32_t info = rd32le (data + p);
					uint32_t rpool = rd32le (data + p + 4);
					p += 8;
					float *times = malloc (sizeof (float) * num_frame);
					float *vals = malloc (sizeof (float) * num_frame * 4);
					if (times && vals)
					{
						size_t ok = 0;
						for (uint32_t f = 0; f < num_frame; f++)
						{
							uint32_t idx = f;
							if (info & XSTEP_MASK)
								idx = (info & XSTEP_2) ? ((f >> 1) + (f & 1)) : ((f >> 2) + (f & 3));
							if ((size_t)(ja + rpool + idx * 2 + 2) > size)
								break;
							uint16_t ridx = rd16le (data + ja + rpool + idx * 2);
							float m[9], q[4];
							rot_by_idx (data + ja, ofs_rot3, ofs_rot5, ridx, m);
							quat_from_mat (m, q);
							times[ok] = f / 60.0f;
							memcpy (vals + ok * 4, q, sizeof (q));
							ok++;
						}
						if (ok)
						{
							anim_add_channel (&anim, node_idx, MODEL_ANIM_ROTATION, times, vals, ok, 4);
							added++;
						}
						else
						{
							free (times);
							free (vals);
						}
					}
					else
					{
						free (times);
						free (vals);
					}
				}
			}

			// --- Scale ---
			if (!(tag & (SRT_IDENTITY_S | SRT_BASE_S)))
			{
				// Scale always advances 2 u32 per axis: animated stores
				// (info,offset); constant stores two fx32 (scale, invscale).
				const int const_ax[3] = { !!(tag & SRT_CONST_SX), !!(tag & SRT_CONST_SY), !!(tag & SRT_CONST_SZ) };
				float const_v[3];
				uint32_t info[3], off[3];
				for (int ax = 0; ax < 3; ax++)
				{
					if (const_ax[ax])
					{
						const_v[ax] = fx12 (rds32le (data + p));
						p += 8;
					}
					else if (p + 8 <= size)
					{
						info[ax] = rd32le (data + p);
						off[ax] = rd32le (data + p + 4);
						p += 8;
					}
					else
						break;
				}
				if (!const_ax[0] || !const_ax[1] || !const_ax[2])
				{
					float *times = malloc (sizeof (float) * num_frame);
					float *vals = malloc (sizeof (float) * num_frame * 3);
					if (times && vals)
					{
						size_t ok = 0;
						for (uint32_t f = 0; f < num_frame; f++)
						{
							int bad = 0;
							float v[3];
							for (int ax = 0; ax < 3; ax++)
							{
								if (const_ax[ax])
								{
									v[ax] = const_v[ax];
									continue;
								}
								float si[2];
								uint32_t idx = f;
								if (info[ax] & XSTEP_4)
								{
									uint32_t last = (info[ax] & XLAST_MASK) >> XLAST_SHIFT;
									idx = (f & 3) ? ((f > last) ? ((last >> 2) + (f & 3)) : (f >> 2)) : (f >> 2);
								}
								else if (info[ax] & XSTEP_2)
								{
									uint32_t last = (info[ax] & XLAST_MASK) >> XLAST_SHIFT;
									idx = (f & 1) ? ((f >> 1) + ((f > last) ? 1 : 0)) : (f >> 1);
								}
								const size_t elem = (info[ax] & XFX16) ? 4 : 8;
								const uint8_t *arr = data + ja + off[ax];
								if ((size_t)(arr - data) + idx * elem + elem > size)
								{
									bad = 1;
									break;
								}
								if (info[ax] & XFX16)
								{
									si[0] = fx12 (rds16le (arr + idx * 4));
									si[1] = fx12 (rds16le (arr + idx * 4 + 2));
								}
								else
								{
									si[0] = fx12 (rds32le (arr + idx * 8));
									si[1] = fx12 (rds32le (arr + idx * 8 + 4));
								}
								v[ax] = si[0];
							}
							if (bad)
								break;
							times[ok] = f / 60.0f;
							memcpy (vals + ok * 3, v, sizeof (v));
							ok++;
						}
						if (ok)
						{
							anim_add_channel (&anim, node_idx, MODEL_ANIM_SCALE, times, vals, ok, 3);
							added++;
						}
						else
						{
							free (times);
							free (vals);
						}
					}
					else
					{
						free (times);
						free (vals);
					}
				}
			}
			} // end !(tag & SRT_IDENTITY)
		} // end for (node)

		if (anim.num_channels)
		{
			model->animations = realloc (model->animations,
				(model->num_animations + 1) * sizeof (model_animation_t));
			if (!model->animations)
			{
				free (anim.channels);
				return added ? added : 0;
			}
			model->animations[model->num_animations++] = anim;
		}
		else
			free (anim.channels);
	}

	return added;
}

//-----------------------------------------------------------------------------
// Pass-through formats: BVA0, BMA0, BTA0, BTP0
//
// None of these has a standard glTF representation, so they are validated
// structurally (Parse*IntoModel) and re-encoded byte-exactly (Encode*).
// The generic parse below returns the number of animation clips found.
//-----------------------------------------------------------------------------

// Return clip count of a NSB container and validate its block structure.
static int nsb_scan_clips (const uint8_t *data, size_t size, const char *expect_kind)
{
	if (!data || size < 0x10)
		return 0;
	nsb_container_t c;
	if (!nsb_container (&c, data, size))
		return 0;
	uint32_t blk_off, blk_size;
	if (!nsb_block_off (&c, 0, &blk_off, &blk_size))
		return 0;
	if (expect_kind && memcmp (c.file + blk_off, expect_kind, 4))
		return 0;
	nsb_dict_t d;
	if (!nsb_dict (&c, blk_off + 8, &d))
		return 0;
	return (int)d.n;
}

int ParseNSBVAIntoModel (model_t *model, const uint8_t *data, size_t size, const char *clip_name)
{
	const int n = nsb_scan_clips (data, size, "VIS0");
	if (n > 0)
		nsb_attach_raw (model, NSB_RAW_BVA0, clip_name && clip_name[0] ? clip_name : "NSB", data, size);
	return n;
}
int ParseNSBMAIntoModel (model_t *model, const uint8_t *data, size_t size, const char *clip_name)
{
	const int n = nsb_scan_clips (data, size, "MAT0");
	if (n > 0)
		nsb_attach_raw (model, NSB_RAW_BMA0, clip_name && clip_name[0] ? clip_name : "NSB", data, size);
	return n;
}
int ParseNSBTAIntoModel (model_t *model, const uint8_t *data, size_t size, const char *clip_name)
{
	const int n = nsb_scan_clips (data, size, "SRT0");
	if (n > 0)
		nsb_attach_raw (model, NSB_RAW_BTA0, clip_name && clip_name[0] ? clip_name : "NSB", data, size);
	return n;
}
int ParseNSBTPIntoModel (model_t *model, const uint8_t *data, size_t size, const char *clip_name)
{
	const int n = nsb_scan_clips (data, size, "PAT0");
	if (n > 0)
		nsb_attach_raw (model, NSB_RAW_BTP0, clip_name && clip_name[0] ? clip_name : "NSB", data, size);
	return n;
}

//-----------------------------------------------------------------------------
// BCA0 writer (fresh joint SRT animation)
//
// A one-clip BCA0 (the NDS tree layout) is a container with a single JNT0
// block: a dict naming the clip, then a NNSG3dResJntAnm unit.  The writer
// mirrors its input channels by snapping times to integer 60-fps frames and
// packing per-node SRT with the same flag scheme the decoder reads: constant
// axes inline, animated axes as an (info, offset) pair into a fx32 array;
// scale always advances in (scale, invscale) pairs; rotation is a const RIDX
// or an animated u16 RID array over shared PIVOT/ROT5 pools picked per-frame
// (PIVOT when the matrix matches a single-axis rotation).
//-----------------------------------------------------------------------------

typedef struct
{
	uint8_t *b;
	size_t len, cap;
} nb_t;

static int nb_reserve (nb_t *c, size_t extra)
{
	if (c->len + extra <= c->cap)
		return 1;
	size_t nc = c->cap ? c->cap : 1024;
	while (nc < c->len + extra)
		nc *= 2;
	uint8_t *nb = realloc (c->b, nc);
	if (!nb)
		return 0;
	c->b = nb;
	c->cap = nc;
	return 1;
}
static int nb_bytes (nb_t *c, const void *p, size_t n)
{
	if (!nb_reserve (c, n))
		return 0;
	memcpy (c->b + c->len, p, n);
	c->len += n;
	return 1;
}
static int nb_u8 (nb_t *c, uint8_t v)
{
	return nb_bytes (c, &v, 1);
}
static int nb_u32 (nb_t *c, uint32_t v)
{
	uint8_t t[4] = { (uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24) };
	return nb_bytes (c, t, 4);
}
static int nb_s32 (nb_t *c, int32_t v)
{
	return nb_u32 (c, (uint32_t)v);
}
static int nb_u16 (nb_t *c, uint16_t v)
{
	uint8_t t[2] = { (uint8_t)v, (uint8_t)(v >> 8) };
	return nb_bytes (c, t, 2);
}
static int nb_s16 (nb_t *c, int16_t v)
{
	return nb_u16 (c, (uint16_t)v);
}

static int32_t fx12_roundf (float v)
{
	return (int32_t)lroundf (v * 4096.0f);
}

static void bca_quat_to_mat (const float q[4], float m[9])
{
	const float x = q[1], y = q[2], z = q[3], w = q[0];
	const float x2 = x * x, y2 = y * y, z2 = z * z;
	m[0] = 1.0f - 2.0f * (y2 + z2);
	m[1] = 2.0f * (x * y - w * z);
	m[2] = 2.0f * (x * z + w * y);
	m[3] = 2.0f * (x * y + w * z);
	m[4] = 1.0f - 2.0f * (x2 + z2);
	m[5] = 2.0f * (y * z - w * x);
	m[6] = 2.0f * (x * z - w * y);
	m[7] = 2.0f * (y * z + w * x);
	m[8] = 1.0f - 2.0f * (x2 + y2);
}

static int bca_quat_identity (const float q[4])
{
	return fabsf (q[0] - 1.0f) < 2e-3f && fabsf (q[1]) < 2e-3f
		&& fabsf (q[2]) < 2e-3f && fabsf (q[3]) < 2e-3f;
}

static int bca_quat_identity_seq (const float *qf, uint32_t nframe)
{
	for (uint32_t f = 0; f < nframe; f++)
		if (!bca_quat_identity (qf + f * 4))
			return 0;
	return 1;
}

// Inverts the PIVOT decode in rot_by_idx(): the stored entry is a single +-1
// diagonal element plus a 2x2 minor, one slot signed (sign_c/sign_d).  Each
// pivot is scored by how well its reconstructed matrix matches, and only a
// near-exact single-axis rotation qualifies (threshold ~1e-3 in matrix L1);
// otherwise the caller falls back to ROT5.
static int bca_piv_encode (const float m[9], uint16_t *d0, int16_t *A, int16_t *B)
{
	int best = -1;
	float best_err = 1e9f;
	for (int pv = 0; pv < 9; pv++)
	{
		const uint8_t *u = pivot_util_[pv];
		const int minus = m[pv] < 0.0f;
		const int sc = (m[u[2]] < 0.0f) != (m[u[1]] < 0.0f);
		const int sd = (m[u[3]] < 0.0f) != (m[u[0]] < 0.0f);
		float cand[9];
		memset (cand, 0, sizeof (cand));
		cand[pv] = minus ? -1.0f : 1.0f;
		cand[u[0]] = m[u[0]];
		cand[u[1]] = m[u[1]];
		cand[u[2]] = sc ? -m[u[1]] : m[u[1]];
		cand[u[3]] = sd ? -m[u[0]] : m[u[0]];
		float err = 0.0f;
		for (int i = 0; i < 9; i++)
			err += fabsf (m[i] - cand[i]);
		if (err < best_err)
		{
			best_err = err;
			best = pv;
		}
	}
	if (best < 0 || best_err > 1e-3f)
		return 0;
	const uint8_t *u = pivot_util_[best];
	*d0 = (uint16_t)best
		| ((m[best] < 0.0f) ? 0x10 : 0)
		| (((m[u[2]] < 0.0f) != (m[u[1]] < 0.0f)) ? 0x20 : 0)
		| (((m[u[3]] < 0.0f) != (m[u[0]] < 0.0f)) ? 0x40 : 0);
	int Aq = fx12_roundf (m[u[0]]);
	int Bq = fx12_roundf (m[u[1]]);
	if (Aq > 32767) Aq = 32767;
	if (Aq < -32768) Aq = -32768;
	if (Bq > 32767) Bq = 32767;
	if (Bq < -32768) Bq = -32768;
	*A = (int16_t)Aq;
	*B = (int16_t)Bq;
	return 1;
}

static void bca_rot5_encode (const float m[9], uint16_t d[5])
{
	// Matches the ROT5 decode: rows 0/1 stored as 13-bit signed fx12
	// (m00..m02,m10,m11 in d0..d4), with the m12 value (not used by the
	// decoder, which re-derives row 2 by cross product) spread across the
	// leftover 3 bits of every u16.
	int q[6];
	for (int i = 0; i < 5; i++)
	{
		int v = fx12_roundf (m[i]);
		if (v > 4095) v = 4095;
		if (v < -4095) v = -4095;
		q[i] = v;
	}
	const uint32_t _12 = (uint32_t)(fx12_roundf (m[8]) & 0x1fff);
	d[0] = (uint16_t)(((uint32_t)(q[0] & 0x1fff) << 3) | (_12 & 7));
	d[1] = (uint16_t)(((uint32_t)(q[1] & 0x1fff) << 3) | ((_12 >> 3) & 7));
	d[2] = (uint16_t)(((uint32_t)(q[2] & 0x1fff) << 3) | ((_12 >> 6) & 7));
	d[3] = (uint16_t)(((uint32_t)(q[3] & 0x1fff) << 3) | ((_12 >> 9) & 7));
	d[4] = (uint16_t)(((uint32_t)(q[4] & 0x1fff) << 3) | ((_12 >> 12) & 7));
}

typedef struct
{
	uint16_t d0;
	int16_t A, B;
} bca_piv_t;
typedef struct
{
	uint16_t d[5];
} bca_rot5_t;

typedef struct
{
	bca_piv_t *p;
	size_t np;
	bca_rot5_t *r5;
	size_t nr5;
} bca_pools_t;

static uint16_t bca_pool_add_matrix (bca_pools_t *pools, const float m[9])
{
	uint16_t d0;
	int16_t A, B;
	if (bca_piv_encode (m, &d0, &A, &B))
	{
		bca_piv_t e = { d0, A, B };
		for (size_t i = 0; i < pools->np; i++)
			if (!memcmp (&pools->p[i], &e, sizeof (e)))
				return (uint16_t)(0x8000u | i);
		if (pools->np >= 0x7fff)
			return 0xffff;
		bca_piv_t *n = realloc (pools->p, (pools->np + 1) * sizeof (bca_piv_t));
		if (!n)
			return 0xffff;
		pools->p = n;
		pools->p[pools->np++] = e;
		return (uint16_t)(pools->np - 1) | 0x8000u;
	}
	uint16_t d[5];
	bca_rot5_encode (m, d);
	bca_rot5_t e;
	memcpy (e.d, d, sizeof (d));
	for (size_t i = 0; i < pools->nr5; i++)
		if (!memcmp (&pools->r5[i], &e, sizeof (e)))
			return (uint16_t)i;
	if (pools->nr5 >= 0x7fff)
		return 0xffff;
	bca_rot5_t *n = realloc (pools->r5, (pools->nr5 + 1) * sizeof (bca_rot5_t));
	if (!n)
		return 0xffff;
	pools->r5 = n;
	pools->r5[pools->nr5++] = e;
	return (uint16_t)(pools->nr5 - 1);
}

typedef enum { AREF_TRANS, AREF_RIDX, AREF_SCALE } bca_aref_kind_t;
typedef struct
{
	uint32_t off_pos;  // position inside the node block of the u32 "offset"
	uint32_t node_ndx;
	uint8_t ax;
	uint8_t kind;
} bca_aref_t;

static int bca_axis_const (const float *p, uint32_t nframe, int stride, int ax)
{
	const float v = p[ax];
	for (uint32_t f = 1; f < nframe; f++)
		if (p[f * stride + ax] != v)
			return 0;
	return 1;
}

static uint8_t bca_refbit (const uint8_t name16[16])
{
	for (int i = 15; i >= 0; i--)
		if (name16[i])
			for (int b = 7; b >= 0; b--)
				if (name16[i] & (1u << b))
					return (uint8_t)(i * 8 + b);
	return 0x7f;
}

static uint8_t *nsb_build_bca0 (const model_animation_t *anim, size_t *out_size)
{
	if (out_size)
		*out_size = 0;
	if (!anim || !anim->num_channels)
		return 0;

	uint8_t name16[16];
	memset (name16, 0, sizeof (name16));
	const char *nm = anim->name[0] ? anim->name : "NSB";
	size_t nl = strlen (nm);
	if (nl > sizeof (name16) - 1)
		nl = sizeof (name16) - 1;
	memcpy (name16, nm, nl);

	// Integer 60-fps frame density: each channel's samples are snapped to the
	// nearest frame; the clip spans frames [0, nframe).
	uint32_t nframe = 1;
	for (size_t ci = 0; ci < anim->num_channels; ci++)
	{
		const model_anim_channel_t *ch = &anim->channels[ci];
		for (size_t i = 0; i < ch->count; i++)
		{
			int f = (int)floorf (ch->times[i] * 60.0f + 0.5f);
			if (f < 0)
				f = 0;
			if ((uint32_t)f + 1 > nframe)
				nframe = (uint32_t)f + 1;
		}
	}

	// Distinct node (joint) indices, ascending.
	int *nodes = malloc (sizeof (int) * anim->num_channels);
	if (!nodes)
		return 0;
	size_t nn = 0;
	for (size_t ci = 0; ci < anim->num_channels; ci++)
	{
		const int idx = anim->channels[ci].node_idx;
		if (idx < 0 || idx > 255)
		{
			free (nodes);
			return 0;
		}
		size_t k;
		for (k = 0; k < nn; k++)
			if (nodes[k] == idx)
				break;
		if (k == nn)
			nodes[nn++] = idx;
	}
	for (size_t i = 1; i < nn; i++)
	{
		const int v = nodes[i];
		size_t j = i;
		while (j > 0 && nodes[j - 1] > v)
		{
			nodes[j] = nodes[j - 1];
			j--;
		}
		nodes[j] = v;
	}

	typedef struct
	{
		int node_idx;
		int has_t, has_r, has_s;
		float *t, *r, *s;
	} bca_node_t;
	bca_node_t *nd = calloc (nn, sizeof (bca_node_t));
	if (!nd)
	{
		free (nodes);
		return 0;
	}
	for (size_t i = 0; i < nn; i++)
		nd[i].node_idx = nodes[i];
	free (nodes);

	int om = 0;
	for (size_t i = 0; i < nn && !om; i++)
	{
		nd[i].s = malloc (sizeof (float) * (size_t)nframe * 3);
		if (!nd[i].s)
			om = 1;
		else
			for (uint32_t f = 0; f < nframe; f++)
			{
				nd[i].s[f * 3 + 0] = 1.0f;
				nd[i].s[f * 3 + 1] = 1.0f;
				nd[i].s[f * 3 + 2] = 1.0f;
			}
	}
	if (om)
	{
		for (size_t i = 0; i < nn; i++)
			free (nd[i].s);
		free (nd);
		return 0;
	}

	// Fill per-node frame data from channels.
	for (size_t ci = 0; ci < anim->num_channels; ci++)
	{
		const model_anim_channel_t *ch = &anim->channels[ci];
		size_t k;
		for (k = 0; k < nn; k++)
			if (nd[k].node_idx == ch->node_idx)
				break;
		if (k == nn)
			continue;
		bca_node_t *ph = &nd[k];
		for (size_t i = 0; i < ch->count; i++)
		{
			int f = (int)floorf (ch->times[i] * 60.0f + 0.5f);
			if (f < 0)
				f = 0;
			const uint32_t uf = (uint32_t)f;
			if (uf >= nframe)
				continue;
			if (ch->path == MODEL_ANIM_SCALE && ch->components >= 3)
			{
				ph->has_s = 1;
				for (int ax = 0; ax < 3; ax++)
					ph->s[uf * 3 + ax] = ch->values[i * ch->components + ax];
			}
			else if (ch->path == MODEL_ANIM_TRANSLATION && ch->components >= 3)
			{
				if (!ph->t)
				{
					ph->t = calloc ((size_t)nframe * 3, sizeof (float));
					ph->has_t = 1;
				}
				if (ph->t)
					for (int ax = 0; ax < 3; ax++)
						ph->t[uf * 3 + ax] = ch->values[i * ch->components + ax];
			}
			else if (ch->path == MODEL_ANIM_ROTATION && ch->components >= 4)
			{
				if (!ph->r)
				{
					ph->r = malloc (sizeof (float) * (size_t)nframe * 4);
					ph->has_r = 1;
					if (ph->r)
						for (uint32_t qf = 0; qf < nframe; qf++)
						{
							ph->r[qf * 4 + 0] = 1.0f;
							ph->r[qf * 4 + 1] = 0.0f;
							ph->r[qf * 4 + 2] = 0.0f;
							ph->r[qf * 4 + 3] = 0.0f;
						}
				}
				if (ph->r)
					for (int qa = 0; qa < 4; qa++)
						ph->r[uf * 4 + qa] = ch->values[i * ch->components + qa];
			}
		}
	}

	// --- build the JntAnm unit ---
	nb_t c = { 0 };
	bca_pools_t pools = { 0 };
	bca_aref_t *arefs = 0;
	size_t narefs = 0;

	// 20-byte header + ofsTag[nn] placeholders, patched at the end.
	int ok = 1;
	ok = ok && nb_u8 (&c, 'J') && nb_u8 (&c, 0) && nb_u8 (&c, 'A') && nb_u8 (&c, 'C');
	ok = ok && nb_u16 (&c, (uint16_t)nframe);
	ok = ok && nb_u16 (&c, (uint16_t)nn);
	ok = ok && nb_s32 (&c, 0); // dataOfs
	const size_t rot3_at = c.len;
	ok = ok && nb_s32 (&c, 0); // ofsRot3
	const size_t rot5_at = c.len;
	ok = ok && nb_s32 (&c, 0); // ofsRot5
	for (size_t i = 0; i < nn && ok; i++)
		ok = ok && nb_u16 (&c, 0);

	uint32_t *node_rel = malloc (sizeof (uint32_t) * nn);
	if (!node_rel)
		ok = 0;

	if (!ok)
		goto nnfail;

	for (size_t i = 0; i < nn; i++)
	{
		bca_node_t *ph = &nd[i];
		uint32_t flags = (uint32_t)ph->node_idx << 24;

		// Translation section.
		int t_identity = 1;
		if (ph->has_t)
			for (uint32_t f = 0; f < nframe && t_identity; f++)
				if (ph->t[f * 3 + 0] != 0.0f || ph->t[f * 3 + 1] != 0.0f || ph->t[f * 3 + 2] != 0.0f)
					t_identity = 0;
		if (t_identity)
			flags |= SRT_IDENTITY_T;
		else
		{
			const int cax[3] = { bca_axis_const (ph->t, nframe, 3, 0),
				bca_axis_const (ph->t, nframe, 3, 1), bca_axis_const (ph->t, nframe, 3, 2) };
			flags |= (cax[0] ? SRT_CONST_TX : 0) | (cax[1] ? SRT_CONST_TY : 0) | (cax[2] ? SRT_CONST_TZ : 0);
		}

		// Rotation section.
		int r_animated = 0;
		if (ph->has_r)
		{
			const int is_id = bca_quat_identity_seq (ph->r, nframe);
			int is_const = 1;
			for (uint32_t f = 1; f < nframe && is_const; f++)
				for (int qa = 0; qa < 4; qa++)
					if (ph->r[f * 4 + qa] != ph->r[qa])
						is_const = 0;
			if (is_id)
				flags |= SRT_IDENTITY_R;
			else if (is_const)
				flags |= SRT_CONST_R;
			else
				r_animated = 1;
		}
		else
			flags |= SRT_IDENTITY_R;

		// Scale section.
		int s_identity = 1;
		if (ph->has_s)
			for (uint32_t f = 0; f < nframe && s_identity; f++)
				if (ph->s[f * 3 + 0] != 1.0f || ph->s[f * 3 + 1] != 1.0f || ph->s[f * 3 + 2] != 1.0f)
					s_identity = 0;
		if (s_identity)
			flags |= SRT_IDENTITY_S;
		else
		{
			const int cax[3] = { bca_axis_const (ph->s, nframe, 3, 0),
				bca_axis_const (ph->s, nframe, 3, 1), bca_axis_const (ph->s, nframe, 3, 2) };
			flags |= (cax[0] ? SRT_CONST_SX : 0) | (cax[1] ? SRT_CONST_SY : 0) | (cax[2] ? SRT_CONST_SZ : 0);
		}

		// Emit the tag and the section data into a scratch block.
		nb_t blk = { 0 };
		int bok = nb_u32 (&blk, flags);
		if (!t_identity && bok)
		{
			for (int ax = 0; ax < 3 && bok; ax++)
			{
				if (flags & (ax == 0 ? SRT_CONST_TX : ax == 1 ? SRT_CONST_TY : SRT_CONST_TZ))
					bok = nb_s32 (&blk, fx12_roundf (ph->t[ax]));
				else
				{
					bok = bok && nb_u32 (&blk, 0) && nb_u32 (&blk, 0);
					if (bok)
					{
						bca_aref_t *a2 = realloc (arefs, (narefs + 1) * sizeof (bca_aref_t));
						if (a2)
						{
							arefs = a2;
							arefs[narefs].off_pos = (uint32_t)(blk.len - 4);
							arefs[narefs].node_ndx = (uint32_t)i;
							arefs[narefs].ax = (uint8_t)ax;
							arefs[narefs].kind = AREF_TRANS;
							narefs++;
						}
						else
							bok = 0;
					}
				}
			}
		}
		if ((flags & SRT_CONST_R) && bok)
		{
			float m[9];
			bca_quat_to_mat (ph->r, m);
			uint16_t ridx = bca_pool_add_matrix (&pools, m);
			if (ridx == 0xffff)
				bok = 0;
			else
				bok = nb_u32 (&blk, ridx);
		}
		else if (r_animated && bok)
		{
			bok = bok && nb_u32 (&blk, 0) && nb_u32 (&blk, 0);
			if (bok)
			{
				bca_aref_t *a2 = realloc (arefs, (narefs + 1) * sizeof (bca_aref_t));
				if (a2)
				{
					arefs = a2;
					arefs[narefs].off_pos = (uint32_t)(blk.len - 4);
					arefs[narefs].node_ndx = (uint32_t)i;
					arefs[narefs].ax = 0;
					arefs[narefs].kind = AREF_RIDX;
					narefs++;
				}
				else
					bok = 0;
			}
		}
		if (!s_identity && bok)
		{
			for (int ax = 0; ax < 3 && bok; ax++)
			{
				if (flags & (ax == 0 ? SRT_CONST_SX : ax == 1 ? SRT_CONST_SY : SRT_CONST_SZ))
				{
					const float sv = ph->s[ax];
					const int32_t inv = sv != 0.0f ? fx12_roundf (1.0f / sv) : 0;
					bok = nb_s32 (&blk, fx12_roundf (sv)) && nb_s32 (&blk, inv);
				}
				else
				{
					bok = bok && nb_u32 (&blk, 0) && nb_u32 (&blk, 0);
					if (bok)
					{
						bca_aref_t *a2 = realloc (arefs, (narefs + 1) * sizeof (bca_aref_t));
						if (a2)
						{
							arefs = a2;
							arefs[narefs].off_pos = (uint32_t)(blk.len - 4);
							arefs[narefs].node_ndx = (uint32_t)i;
							arefs[narefs].ax = (uint8_t)ax;
							arefs[narefs].kind = AREF_SCALE;
							narefs++;
						}
						else
							bok = 0;
					}
				}
			}
		}

		if (!bok)
		{
			free (blk.b);
			goto nnfail;
		}
		node_rel[i] = (uint32_t)c.len;
		{
			const size_t tag_pos = 20 + i * 2;
			if (tag_pos + 2 > c.len)
			{
				free (blk.b);
				goto nnfail;
			}
			c.b[tag_pos] = (uint8_t)(node_rel[i] & 0xff);
			c.b[tag_pos + 1] = (uint8_t)((node_rel[i] >> 8) & 0xff);
		}
		if (!nb_bytes (&c, blk.b, blk.len))
		{
			free (blk.b);
			goto nnfail;
		}
		free (blk.b);
	}

	// Append the per-frame arrays and patch the (info, offset) pairs.
	for (size_t i = 0; i < narefs; i++)
	{
		const bca_aref_t *a = &arefs[i];
		bca_node_t *ph = &nd[a->node_ndx];
		const uint32_t rel = (uint32_t)c.len;
		switch (a->kind)
		{
			case AREF_TRANS:
				for (uint32_t f = 0; f < nframe; f++)
					if (!nb_s32 (&c, fx12_roundf (ph->t[f * 3 + a->ax])))
						goto nnfail;
				break;
			case AREF_SCALE:
				for (uint32_t f = 0; f < nframe; f++)
				{
					const float sv = ph->s[f * 3 + a->ax];
					const int32_t inv = sv != 0.0f ? fx12_roundf (1.0f / sv) : 0;
					if (!nb_s32 (&c, fx12_roundf (sv)) || !nb_s32 (&c, inv))
						goto nnfail;
				}
				break;
			case AREF_RIDX:
				for (uint32_t f = 0; f < nframe; f++)
				{
					float m[9];
					bca_quat_to_mat (ph->r + f * 4, m);
					uint16_t ridx = bca_pool_add_matrix (&pools, m);
					if (ridx == 0xffff || !nb_u16 (&c, ridx))
						goto nnfail;
				}
				break;
		}
		const size_t pos = (size_t)node_rel[a->node_ndx] + a->off_pos;
		if (pos + 4 > c.len)
			goto nnfail;
		c.b[pos + 0] = (uint8_t)(rel & 0xff);
		c.b[pos + 1] = (uint8_t)((rel >> 8) & 0xff);
		c.b[pos + 2] = (uint8_t)((rel >> 16) & 0xff);
		c.b[pos + 3] = (uint8_t)((rel >> 24) & 0xff);
	}

	// Rotation pools.
	const uint32_t piv_rel = (uint32_t)c.len;
	for (size_t i = 0; i < pools.np; i++)
		if (!nb_u16 (&c, pools.p[i].d0) || !nb_s16 (&c, pools.p[i].A) || !nb_s16 (&c, pools.p[i].B))
			goto nnfail;
	const uint32_t rot5_rel = (uint32_t)c.len;
	for (size_t i = 0; i < pools.nr5; i++)
		for (int k = 0; k < 5; k++)
			if (!nb_u16 (&c, pools.r5[i].d[k]))
				goto nnfail;

	{
		const uint32_t piv_patch = piv_rel ? piv_rel : (uint32_t)c.len;
		const uint32_t rot5_patch = rot5_rel ? rot5_rel : (uint32_t)c.len;
		c.b[rot3_at] = (uint8_t)(piv_patch & 0xff);
		c.b[rot3_at + 1] = (uint8_t)((piv_patch >> 8) & 0xff);
		c.b[rot3_at + 2] = (uint8_t)((piv_patch >> 16) & 0xff);
		c.b[rot3_at + 3] = (uint8_t)((piv_patch >> 24) & 0xff);
		c.b[rot5_at] = (uint8_t)(rot5_patch & 0xff);
		c.b[rot5_at + 1] = (uint8_t)((rot5_patch >> 8) & 0xff);
		c.b[rot5_at + 2] = (uint8_t)((rot5_patch >> 16) & 0xff);
		c.b[rot5_at + 3] = (uint8_t)((rot5_patch >> 24) & 0xff);
	}

	// Assemble the JNT0 block and the BCA0 container.
	const uint32_t dict_len = 8 + 4 * 2 + 8 + 16; // header + 2n nodes + entry + 16-byte name
	const uint32_t block_size = 8 + dict_len + (uint32_t)c.len;
	const uint32_t file_size = 0x14 + block_size;
	uint8_t *out = malloc (file_size);
	if (!out)
		goto nnfail;
	memset (out, 0, file_size);
	memcpy (out + 0x00, "BCA0", 4);
	out[0x04] = 0xff; out[0x05] = 0xfe;
	out[0x06] = 0x01; out[0x07] = 0x00;
	out[0x08] = (uint8_t)(file_size & 0xff);
	out[0x09] = (uint8_t)((file_size >> 8) & 0xff);
	out[0x0a] = (uint8_t)((file_size >> 16) & 0xff);
	out[0x0b] = (uint8_t)((file_size >> 24) & 0xff);
	out[0x0c] = 0x10; out[0x0d] = 0x00;
	out[0x0e] = 0x01; out[0x0f] = 0x00;
	out[0x10] = 0x14; out[0x11] = 0x00; out[0x12] = 0x00; out[0x13] = 0x00;
	memcpy (out + 0x14, "JNT0", 4);
	out[0x18] = (uint8_t)(block_size & 0xff);
	out[0x19] = (uint8_t)((block_size >> 8) & 0xff);
	out[0x1a] = (uint8_t)((block_size >> 16) & 0xff);
	out[0x1b] = (uint8_t)((block_size >> 24) & 0xff);
	const uint32_t dc = 0x1c;
	{
		out[dc + 0] = 0;          // revision
		out[dc + 1] = 1;          // numEntry
		out[dc + 2] = 0x28;       // sizeDictBlk
		out[dc + 3] = 0;
		out[dc + 4] = 8;          // ofsNode
		out[dc + 5] = 0;
		out[dc + 6] = 0x10;       // ofsEntry
		out[dc + 7] = 0;
		out[dc + 8] = 0x7f;       // root node refBit
		out[dc + 9] = 1;          // root idxLeft -> leaf
		out[dc + 10] = 0;
		out[dc + 11] = 0;
		out[dc + 12] = bca_refbit (name16); // leaf refBit
		out[dc + 13] = 0;
		out[dc + 14] = 1;         // leaf idxRight -> self
		out[dc + 15] = 0;
		out[dc + 16] = 4;         // sizeUnit
		out[dc + 17] = 0;
		out[dc + 18] = 8;         // ofsName
		out[dc + 19] = 0;
		out[dc + 20] = (uint8_t)((8 + dict_len) & 0xff); // ofsUnit (rel to block)
		out[dc + 21] = (uint8_t)(((8 + dict_len) >> 8) & 0xff);
		out[dc + 22] = 0;
		out[dc + 23] = 0;
		memcpy (out + dc + 24, name16, 16);
	}
	memcpy (out + dc + dict_len, c.b, c.len);

	for (size_t i = 0; i < nn; i++)
	{
		free (nd[i].t);
		free (nd[i].r);
		free (nd[i].s);
	}
	free (nd);
	free (pools.p);
	free (pools.r5);
	free (arefs);
	free (node_rel);
	free (c.b);
	if (out_size)
		*out_size = file_size;
	return out;

nnfail:
	for (size_t i = 0; i < nn; i++)
	{
		free (nd[i].t);
		free (nd[i].r);
		free (nd[i].s);
	}
	free (nd);
	free (pools.p);
	free (pools.r5);
	free (arefs);
	free (node_rel);
	free (c.b);
	if (out_size)
		*out_size = 0;
	return 0;
}

//-----------------------------------------------------------------------------
// BVA0 (VIS0) visibility animation
//
// A VIS0 block is a NNSG3dResVisAnmSet: an 8-byte block header (kind "VIS0"
// plus size), a dict naming each clip, then per-clip NNSG3dResVisAnm units.
// A unit is a 12-byte header followed by a dense bitfield:
//
//   u8  category0     0x00
//   u8  revision      0x01
//   u16 category1     0x02
//   u16 numFrame      0x04
//   u16 numNode       0x06
//   u16 size          0x08   (byte length of the bitfield, words * 4)
//   u16 dummy         0x0a
//   u32 visData[]     0x0c
//
// bit (frame * numNode + node) holds node's visibility at frame, following
// the SDK's NNSi_G3dAnmCalcNsBva index math (pos = frame * numNode + dataIdx).
//-----------------------------------------------------------------------------

int DecodeNSBVA_Clip (nsb_vis_t *vis, const uint8_t *data, size_t size, uint32_t clip_idx)
{
	if (!vis || !data || size < 0x1c)
		return 0;
	nsb_container_t c;
	if (!nsb_container (&c, data, size))
		return 0;
	uint32_t blk_off, blk_size;
	if (!nsb_block_off (&c, 0, &blk_off, &blk_size)
		|| (size_t)blk_off + blk_size > size
		|| blk_size < 0x20
		|| memcmp (data + blk_off, "VIS0", 4))
		return 0;
	nsb_dict_t d;
	if (!nsb_dict (&c, blk_off + 8, &d) || clip_idx >= d.n)
		return 0;
	uint32_t unit_rel;
	if (!nsb_dict_unit_offset (&c, &d, clip_idx, &unit_rel))
		return 0;
	const size_t unit = (size_t)blk_off + unit_rel;
	if (unit + 0x0c + 4 > size)
		return 0;
	const uint32_t num_frame = rd16le (data + unit + 4);
	const uint32_t num_node = rd16le (data + unit + 6);
	const uint32_t size_field = rd16le (data + unit + 8);
	if (!num_frame || !num_node)
		return 0;
	const uint32_t words = (num_frame * num_node + 31) / 32;
	if (size_field != words * 4 || unit + 0x0c + size_field > size)
		return 0;
	vis->num_frame = num_frame;
	vis->num_node = num_node;
	vis->words = words;
	vis->bits = data + unit + 0x0c;
	return 1;
}

int NSBVA_Visible (const nsb_vis_t *vis, uint32_t frame, uint32_t node)
{
	if (!vis || !vis->bits || !vis->num_node || frame >= vis->num_frame || node >= vis->num_node)
		return 0;
	const uint32_t pos = frame * vis->num_node + node;
	return (vis->bits[pos >> 3] >> (pos & 7)) & 1;
}

uint8_t *BuildNSBVA (const nsb_vis_t *vis, const char *clip_name, size_t *out_size)
{
	if (out_size)
		*out_size = 0;
	if (!vis || !vis->bits || !vis->num_frame || !vis->num_node)
		return 0;
	const uint32_t words = (vis->num_frame * vis->num_node + 31) / 32;
	const uint32_t size_field = words * 4;

	uint8_t name16[16];
	memset (name16, 0, sizeof (name16));
	const char *nm = clip_name && clip_name[0] ? clip_name : "NSB";
	size_t nl = strlen (nm);
	if (nl > sizeof (name16) - 1)
		nl = sizeof (name16) - 1;
	memcpy (name16, nm, nl);

	// One-clip VIS0 unit.
	nb_t c = { 0 };
	int ok = 1;
	ok = ok && nb_u8 (&c, 'V') && nb_u8 (&c, 0);
	ok = ok && nb_u16 (&c, 0);
	ok = ok && nb_u16 (&c, (uint16_t)vis->num_frame);
	ok = ok && nb_u16 (&c, (uint16_t)vis->num_node);
	ok = ok && nb_u16 (&c, size_field);
	ok = ok && nb_u16 (&c, 0);
	for (uint32_t i = 0; i < words && ok; i++)
		ok = nb_u32 (&c, rd32le (vis->bits + i * 4));
	if (!ok)
		return 0;

	// Assemble the VIS0 block and the BVA0 container.
	const uint32_t dict_len = 8 + 4 * 2 + 8 + 16;
	const uint32_t block_size = 8 + dict_len + (uint32_t)c.len;
	const uint32_t file_size = 0x14 + block_size;
	uint8_t *out = malloc (file_size);
	if (!out)
	{
		free (c.b);
		return 0;
	}
	memset (out, 0, file_size);
	memcpy (out + 0x00, "BVA0", 4);
	out[0x04] = 0xff; out[0x05] = 0xfe;
	out[0x06] = 0x01; out[0x07] = 0x00;
	out[0x08] = (uint8_t)(file_size & 0xff);
	out[0x09] = (uint8_t)((file_size >> 8) & 0xff);
	out[0x0a] = (uint8_t)((file_size >> 16) & 0xff);
	out[0x0b] = (uint8_t)((file_size >> 24) & 0xff);
	out[0x0c] = 0x10; out[0x0d] = 0x00;
	out[0x0e] = 0x01; out[0x0f] = 0x00;
	out[0x10] = 0x14; out[0x11] = 0x00; out[0x12] = 0x00; out[0x13] = 0x00;
	memcpy (out + 0x14, "VIS0", 4);
	out[0x18] = (uint8_t)(block_size & 0xff);
	out[0x19] = (uint8_t)((block_size >> 8) & 0xff);
	out[0x1a] = (uint8_t)((block_size >> 16) & 0xff);
	out[0x1b] = (uint8_t)((block_size >> 24) & 0xff);
	const uint32_t dc = 0x1c;
	{
		out[dc + 0] = 0;          // revision
		out[dc + 1] = 1;          // numEntry
		out[dc + 2] = 0x28;       // sizeDictBlk
		out[dc + 3] = 0;
		out[dc + 4] = 8;          // ofsNode
		out[dc + 5] = 0;
		out[dc + 6] = 0x10;       // ofsEntry
		out[dc + 7] = 0;
		out[dc + 8] = 0x7f;       // root node refBit
		out[dc + 9] = 1;          // root idxLeft -> leaf
		out[dc + 10] = 0;
		out[dc + 11] = 0;
		out[dc + 12] = bca_refbit (name16); // leaf refBit
		out[dc + 13] = 0;
		out[dc + 14] = 1;         // leaf idxRight -> self
		out[dc + 15] = 0;
		out[dc + 16] = 4;         // sizeUnit
		out[dc + 17] = 0;
		out[dc + 18] = 8;         // ofsName
		out[dc + 19] = 0;
		out[dc + 20] = (uint8_t)((8 + dict_len) & 0xff); // ofsUnit (rel to block)
		out[dc + 21] = (uint8_t)(((8 + dict_len) >> 8) & 0xff);
		out[dc + 22] = 0;
		out[dc + 23] = 0;
		memcpy (out + dc + 24, name16, 16);
	}
	memcpy (out + dc + dict_len, c.b, c.len);
	free (c.b);
	if (out_size)
		*out_size = file_size;
	return out;
}

//-----------------------------------------------------------------------------
// BTP0 (PAT0) texture pattern animation
//
// "BTP0" container with a "PAT0" block holding a NNSG3dResTexPatAnmSet: an
// outer dict names each clip, and each clip is a NNSG3dResTexPatAnm unit.  A
// unit is a 12-byte header followed by an inner dict whose per-material
// entries are NNSG3dResDictTexPatAnmData, each pointing (unit-relative) to an
// FV table of NNSG3dResTexPatAnmFV {u16 idxFrame, u8 idTex, u8 idPltt}.
//
//   u8  category0       0x00
//   u8  revision        0x01
//   u16 category1       0x02
//   u16 numFrame        0x04
//   u8  numTex          0x06
//   u8  numPltt         0x07
//   u16 ofsTexName      0x08   (unit-relative, NNSG3dResName[numTex])
//   u16 ofsPlttName     0x0a   (unit-relative, NNSG3dResName[numPltt])
//   dict                0x0c   (entries stride = sizeUnit + 4 = 12 bytes)
//
// NSBTP_Lookup mirrors NNSi_G3dGetTexPatAnmFV: start the search at
// frame * ratioDataFrame >> 16, then clamp to the last keyframe <= frame.
//-----------------------------------------------------------------------------

int DecodeNSBTP_Clip (nsb_tp_clip_t *clip, const uint8_t *data, size_t size, uint32_t clip_idx)
{
	if (!clip || !data || size < 0x30)
		return 0;
	nsb_container_t c;
	if (!nsb_container (&c, data, size))
		return 0;
	uint32_t blk_off, blk_size;
	if (!nsb_block_off (&c, 0, &blk_off, &blk_size)
		|| (size_t)blk_off + blk_size > size
		|| blk_size < 0x30
		|| memcmp (data + blk_off, "PAT0", 4))
		return 0;

	// Outer dict: clip name -> unit offset (relative to the block).
	nsb_dict_t d_outer;
	if (!nsb_dict (&c, blk_off + 8, &d_outer) || clip_idx >= d_outer.n)
		return 0;
	uint32_t eo = d_outer.ofs_entry + clip_idx * 8;
	if (eo + 8 > c.file_size)
		return 0;
	const size_t unit = (size_t)blk_off + rd32le (c.file + eo + 4);
	if (unit + 0x14 > c.file_size)
		return 0;
	const uint32_t num_frame = rd16le (c.file + unit + 4);
	const uint32_t num_tex = c.file[unit + 6];
	const uint32_t num_pltt = c.file[unit + 7];
	if (!num_frame)
		return 0;

	// Inner dict: entry (material) data at stride 12.
	nsb_dict_t d;
	if (!nsb_dict (&c, (uint32_t)unit + 0x0c, &d) || clip_idx >= d.n)
		return 0;
	eo = d.ofs_entry + clip_idx * 12;
	if (eo + 12 > c.file_size)
		return 0;
	const uint32_t num_keys = rd16le (c.file + eo + 4);
	const uint32_t ratio = rd16le (c.file + eo + 8);
	const uint32_t fv_off = rd16le (c.file + eo + 0x0a);
	if (!num_keys || (size_t)fv_off + (size_t)num_keys * 4 > c.file_size - unit)
		return 0;
	clip->num_frame = num_frame;
	clip->num_tex = num_tex;
	clip->num_pltt = num_pltt;
	clip->num_keys = num_keys;
	clip->ratio_fx16 = ratio;
	clip->keys = c.file + unit + fv_off;
	return 1;
}

int NSBTP_Lookup (const nsb_tp_clip_t *clip, uint32_t frame, uint32_t *tex, uint32_t *pltt)
{
	if (!clip || !clip->keys || !clip->num_keys || frame >= clip->num_frame)
		return 0;
	uint32_t idx = (uint32_t)((uint64_t)clip->ratio_fx16 * frame >> 16);
	if (idx >= clip->num_keys)
		idx = clip->num_keys - 1;
	while (idx > 0 && rd16le (clip->keys + idx * 4) >= frame)
		idx--;
	while (idx + 1 < clip->num_keys && rd16le (clip->keys + (idx + 1) * 4) <= frame)
		idx++;
	const uint32_t kt = clip->keys[idx * 4 + 2];
	const uint32_t kp = clip->keys[idx * 4 + 3];
	if (kt >= clip->num_tex || (kp != 0xff && kp >= clip->num_pltt))
		return 0;
	if (tex) *tex = kt;
	if (pltt) *pltt = kp;
	return 1;
}

static void nsb_write_name16 (uint8_t *dst, const char *name)
{
	memset (dst, 0, 16);
	const char *s = name ? name : "";
	for (int i = 0; i < 15 && s[i]; i++)
		dst[i] = (uint8_t)s[i];
}

uint8_t *BuildNSBTP (uint32_t num_frame, uint32_t num_tex, uint32_t num_pltt,
	const nsb_tp_clip_spec_t *clips, size_t num_clips, size_t *out_size)
{
	if (out_size)
		*out_size = 0;
	if (!clips || !num_frame || num_clips == 0 || num_clips > 15)
		return 0;
	size_t total_keys = 0;
	for (size_t i = 0; i < num_clips; i++)
	{
		const nsb_tp_clip_spec_t *s = &clips[i];
		if (!s->name || !s->keys || !s->num_keys)
			return 0;
		for (uint32_t k = 0; k < s->num_keys; k++)
		{
			if (s->keys[k].frame >= num_frame || s->keys[k].tex >= num_tex
				|| (s->keys[k].pltt != 0xff && s->keys[k].pltt >= num_pltt))
				return 0;
			if (k && s->keys[k].frame < s->keys[k - 1].frame)
				return 0;
		}
		total_keys += s->num_keys;
	}

	const uint32_t n = (uint32_t)num_clips;

	// Outer block dict: clip name -> unit offset (8-byte entries).
	const uint32_t odict_len = 8 + 4 * n + 8 * n + 16 * n; // hdr + nodes + entries + names
	const uint32_t ONODE = 8 + 4 * n;
	const uint32_t OENT = ONODE + 8 * n;

	// Inner unit dict (at unit + 12): entry data stride 12, names 16.
	const uint32_t idict_len = 8 + 4 * n + 12 * n + 16 * n;
	const uint32_t INODE = 8 + 4 * n;
	const uint32_t names_off = 0x0c + idict_len;
	const uint32_t pltt_names_off = names_off + num_tex * 16;
	const uint32_t fv_start = pltt_names_off + num_pltt * 16;
	if ((uint64_t)fv_start + total_keys * 4 > 0xff00)
		return 0;

	// Unit (header + inner dict + tex/pltt names + FV tables).
	nb_t c = { 0 };
	int ok = 1;
	ok = ok && nb_u8 (&c, 'T') && nb_u8 (&c, 0) && nb_u16 (&c, 0);
	ok = ok && nb_u16 (&c, (uint16_t)num_frame);
	ok = ok && nb_u8 (&c, (uint8_t)num_tex) && nb_u8 (&c, (uint8_t)num_pltt);
	ok = ok && nb_u16 (&c, (uint16_t)names_off);
	ok = ok && nb_u16 (&c, (uint16_t)pltt_names_off);
	ok = ok && nb_u8 (&c, 0) && nb_u8 (&c, (uint8_t)n);
	ok = ok && nb_u16 (&c, (uint16_t)idict_len);
	ok = ok && nb_u16 (&c, 0x08); // ofsNode
	ok = ok && nb_u16 (&c, (uint16_t)INODE); // ofsEntry
	for (uint32_t i = 0; i < n && ok; i++)
		ok = nb_u32 (&c, i); // unused nodes (numEntry < 16 -> linear scan)
	uint32_t fv_off = fv_start;
	for (size_t i = 0; i < num_clips && ok; i++)
	{
		const nsb_tp_clip_spec_t *s = &clips[i];
		const uint32_t ratio = (uint16_t)(((uint64_t)s->num_keys << 16) / num_frame);
		ok = ok && nb_u16 (&c, 8); // sizeUnit
		ok = ok && nb_u16 (&c, (uint16_t)(12 * n)); // ofsName
		ok = ok && nb_u16 (&c, (uint16_t)s->num_keys); // numFV
		ok = ok && nb_u16 (&c, 0); // flag
		ok = ok && nb_u16 (&c, (uint16_t)ratio); // ratioDataFrame
		ok = ok && nb_u16 (&c, (uint16_t)fv_off); // offset (unit-relative)
		fv_off += s->num_keys * 4;
	}
	for (size_t i = 0; i < num_clips && ok; i++)
	{
		uint8_t name16[16];
		nsb_write_name16 (name16, clips[i].name);
		ok = ok && nb_bytes (&c, name16, 16);
	}
	for (uint32_t i = 0; i < num_tex && ok; i++)
	{
		uint8_t name16[16];
		char nm[8];
		snprintf (nm, sizeof (nm), "Tex%02u", i);
		nsb_write_name16 (name16, nm);
		ok = ok && nb_bytes (&c, name16, 16);
	}
	for (uint32_t i = 0; i < num_pltt && ok; i++)
	{
		uint8_t name16[16];
		char nm[8];
		snprintf (nm, sizeof (nm), "Pltt%02u", i);
		nsb_write_name16 (name16, nm);
		ok = ok && nb_bytes (&c, name16, 16);
	}
	for (size_t i = 0; i < num_clips && ok; i++)
		for (uint32_t k = 0; k < clips[i].num_keys && ok; k++)
			ok = ok && nb_u16 (&c, (uint16_t)clips[i].keys[k].frame)
				&& nb_u8 (&c, (uint8_t)clips[i].keys[k].tex)
				&& nb_u8 (&c, (uint8_t)clips[i].keys[k].pltt);
	if (!ok)
	{
		free (c.b);
		return 0;
	}

	// Assemble the PAT0 block and the BTP0 container.
	const uint32_t block_size = 8 + odict_len + (uint32_t)c.len;
	const uint32_t file_size = 0x14 + block_size;
	uint8_t *out = malloc (file_size);
	if (!out)
	{
		free (c.b);
		return 0;
	}
	memset (out, 0, file_size);
	memcpy (out + 0x00, "BTP0", 4);
	out[0x04] = 0xff; out[0x05] = 0xfe;
	out[0x06] = 0x01; out[0x07] = 0x00;
	out[0x08] = (uint8_t)(file_size & 0xff);
	out[0x09] = (uint8_t)((file_size >> 8) & 0xff);
	out[0x0a] = (uint8_t)((file_size >> 16) & 0xff);
	out[0x0b] = (uint8_t)((file_size >> 24) & 0xff);
	out[0x0c] = 0x10; out[0x0d] = 0x00;
	out[0x0e] = 0x01; out[0x0f] = 0x00;
	out[0x10] = 0x14; out[0x11] = 0x00; out[0x12] = 0x00; out[0x13] = 0x00;
	memcpy (out + 0x14, "PAT0", 4);
	out[0x18] = (uint8_t)(block_size & 0xff);
	out[0x19] = (uint8_t)((block_size >> 8) & 0xff);
	out[0x1a] = (uint8_t)((block_size >> 16) & 0xff);
	out[0x1b] = (uint8_t)((block_size >> 24) & 0xff);
	const uint32_t dc = 0x1c;
	{
		out[dc + 0] = 0;          // revision
		out[dc + 1] = (uint8_t)n; // numEntry
		out[dc + 2] = (uint8_t)(odict_len & 0xff);
		out[dc + 3] = (uint8_t)((odict_len >> 8) & 0xff);
		out[dc + 4] = 8;          // ofsNode
		out[dc + 5] = 0;
		out[dc + 6] = (uint8_t)(ONODE & 0xff); // ofsEntry
		out[dc + 7] = (uint8_t)((ONODE >> 8) & 0xff);
		for (uint32_t i = 0; i < n; i++)
		{
			out[dc + 8 + i * 4 + 0] = 0; // refBit
			out[dc + 8 + i * 4 + 1] = 0; // idxLeft
			out[dc + 8 + i * 4 + 2] = 0; // idxRight
			out[dc + 8 + i * 4 + 3] = (uint8_t)i; // idxEntry
		}
		for (uint32_t i = 0; i < n; i++)
		{
			uint32_t eo = dc + ONODE + i * 8;
			out[eo + 0] = 4; // sizeUnit
			out[eo + 1] = 0;
			out[eo + 2] = (uint8_t)((8 * n) & 0xff); // ofsName (rel. entry base)
			out[eo + 3] = (uint8_t)(((8 * n) >> 8) & 0xff);
			out[eo + 4] = 0; // ofsUnit low (patched)
			out[eo + 5] = 0;
			out[eo + 6] = 0;
			out[eo + 7] = 0;
		}
		const uint32_t nb = dc + OENT;
		for (size_t i = 0; i < num_clips; i++)
		{
			uint8_t name16[16];
			nsb_write_name16 (name16, clips[i].name);
			memcpy (out + nb + i * 16, name16, 16);
		}
	}
	// Unit is placed right after the block dict.
	memcpy (out + dc + odict_len, c.b, c.len);
	// Patch the outer dict's ofsUnit to the unit's block-relative offset.
	for (uint32_t i = 0; i < n; i++)
	{
		uint32_t eo = dc + ONODE + i * 8;
		const uint32_t unit_rel = 8 + odict_len;
		out[eo + 4] = (uint8_t)(unit_rel & 0xff);
		out[eo + 5] = (uint8_t)((unit_rel >> 8) & 0xff);
		out[eo + 6] = (uint8_t)((unit_rel >> 16) & 0xff);
		out[eo + 7] = (uint8_t)((unit_rel >> 24) & 0xff);
	}
	free (c.b);
	if (out_size)
		*out_size = file_size;
	return out;
}

//-----------------------------------------------------------------------------
// BMA0 (MAT0) material-colour animation and BTA0 (SRT0) texture-SRT animation
//
// Both reuse the BTP0 container scaffolding: an outer dict maps clip names to
// one NNSG3dResMatCAnm / NNSG3dResTexSRTAnm unit, and each unit holds an inner
// dict whose per-clip entries carry NNSG3dResDictMatCAnmData-style records
// {numFV, flag, ratioDataFrame, offset}; the offset points at the keyframe
// table.  The two differ only in the key payload:
//
//   BMA0 key  {u16 idxFrame, u32 color[5]} (0xaarrggbb each):
//             diffuse, ambient, specular, emission, polygon alpha.
//   BTA0 key  {u16 idxFrame, s16 v[5]}      (fx1.10.5 each):
//             scale X, scale Y, rotation, translation X, translation Y.
//
// Lookup walks the same ratioDataFrame-seeded search as NSBTP_Lookup, then
// linearly interpolates between the surrounding keys (per colour component for
// BMA0, per parameter for BTA0) at fx1.14 precision.  The layout is a
// reconstructed SDK-struct-style form verified by round-trip tests; see the
// header for the fidelity note.
//-----------------------------------------------------------------------------

static int nsb_dc_keyed_clip (const uint8_t *data, size_t size, const char *blkmagic,
	uint32_t clip_idx, uint32_t key_stride, uint32_t *num_frame, uint32_t *num_keys,
	uint32_t *ratio, const uint8_t **keys)
{
	if (!data || size < 0x30)
		return 0;
	nsb_container_t c;
	if (!nsb_container (&c, data, size))
		return 0;
	uint32_t blk_off, blk_size;
	if (!nsb_block_off (&c, 0, &blk_off, &blk_size)
		|| (size_t)blk_off + blk_size > size
		|| blk_size < 0x30
		|| memcmp (data + blk_off, blkmagic, 4))
		return 0;
	nsb_dict_t d_outer;
	if (!nsb_dict (&c, blk_off + 8, &d_outer) || clip_idx >= d_outer.n)
		return 0;
	const uint32_t eo_outer = d_outer.ofs_entry + clip_idx * 8;
	if (eo_outer + 8 > c.file_size)
		return 0;
	const size_t a_unit = (size_t)blk_off + rd32le (c.file + eo_outer + 4);
	if (a_unit + 0x18 > c.file_size)
		return 0;
	const uint32_t a_num_frame = rd16le (c.file + a_unit + 4);
	if (!a_num_frame)
		return 0;
	nsb_dict_t d;
	if (!nsb_dict (&c, (uint32_t)a_unit + 8, &d) || clip_idx >= d.n)
		return 0;
	const uint32_t eo = d.ofs_entry + clip_idx * 12;
	if (eo + 12 > c.file_size)
		return 0;
	const uint32_t a_num_keys = rd16le (c.file + eo + 4);
	const uint32_t a_ratio = rd16le (c.file + eo + 8);
	const uint32_t kofs = rd16le (c.file + eo + 0x0a);
	if (!a_num_keys || (size_t)kofs + (size_t)a_num_keys * (size_t)key_stride > c.file_size - a_unit)
		return 0;
	*num_frame = a_num_frame;
	*num_keys = a_num_keys;
	*ratio = a_ratio;
	*keys = c.file + a_unit + kofs;
	return 1;
}

int DecodeNSBMA_Clip (nsb_ma_clip_t *clip, const uint8_t *data, size_t size, uint32_t clip_idx)
{
	uint32_t num_frame, num_keys, ratio;
	const uint8_t *keys;
	if (!clip || !nsb_dc_keyed_clip (data, size, "MAT0", clip_idx, 22,
		&num_frame, &num_keys, &ratio, &keys))
		return 0;
	clip->num_frame = num_frame;
	clip->num_channels = NSB_MATCOL_CHANNELS;
	clip->num_keys = num_keys;
	clip->ratio_fx16 = ratio;
	clip->keys = keys;
	return 1;
}

int DecodeNSBTA_Clip (nsb_ta_clip_t *clip, const uint8_t *data, size_t size, uint32_t clip_idx)
{
	uint32_t num_frame, num_keys, ratio;
	const uint8_t *keys;
	if (!clip || !nsb_dc_keyed_clip (data, size, "SRT0", clip_idx, 12,
		&num_frame, &num_keys, &ratio, &keys))
		return 0;
	clip->num_frame = num_frame;
	clip->num_keys = num_keys;
	clip->ratio_fx16 = ratio;
	clip->keys = keys;
	return 1;
}

static int nsb_key_segment (uint32_t num_keys, uint32_t ratio, const uint8_t *keys,
	uint32_t stride, uint32_t frame, uint32_t *lo, uint32_t *hi, uint32_t *t)
{
	uint32_t idx = (uint32_t)((uint64_t)ratio * frame >> 16);
	if (idx >= num_keys)
		idx = num_keys - 1;
	while (idx > 0 && rd16le (keys + idx * stride) >= frame)
		idx--;
	while (idx + 1 < num_keys && rd16le (keys + (idx + 1) * stride) <= frame)
		idx++;
	*lo = idx;
	*t = 0;
	if (idx + 1 >= num_keys)
	{
		*hi = idx;
		return 1;
	}
	const uint32_t f0 = rd16le (keys + idx * stride);
	const uint32_t f1 = rd16le (keys + (idx + 1) * stride);
	*hi = idx + 1;
	if (f1 > f0)
		*t = (uint32_t)(((uint64_t)(frame - f0) << 14) / (f1 - f0));
	return 1;
}

int NSBMA_Lookup (const nsb_ma_clip_t *clip, uint32_t channel,
	uint32_t frame, uint32_t *color)
{
	if (!clip || !clip->keys || !clip->num_keys || channel >= NSB_MATCOL_CHANNELS
		|| frame >= clip->num_frame)
		return 0;
	uint32_t lo, hi, t;
	if (!nsb_key_segment (clip->num_keys, clip->ratio_fx16, clip->keys, 22, frame, &lo, &hi, &t))
		return 0;
	const uint8_t *a = clip->keys + lo * 22 + 2 + channel * 4;
	const uint8_t *b = clip->keys + hi * 22 + 2 + channel * 4;
	uint8_t out[4];
	for (int i = 0; i < 4; i++)
		out[i] = (uint8_t)(((uint32_t)a[i] * (16384 - t) + (uint32_t)b[i] * t) >> 14);
	*color = rd32le (out);
	return 1;
}

int NSBTA_Lookup (const nsb_ta_clip_t *clip, uint32_t frame,
	int16_t v[NSB_TEXSRT_PARAMS])
{
	if (!clip || !clip->keys || !clip->num_keys || frame >= clip->num_frame)
		return 0;
	uint32_t lo, hi, t;
	if (!nsb_key_segment (clip->num_keys, clip->ratio_fx16, clip->keys, 12, frame, &lo, &hi, &t))
		return 0;
	for (int i = 0; i < NSB_TEXSRT_PARAMS; i++)
	{
		const int32_t a = rds16le (clip->keys + lo * 12 + 2 + i * 2);
		const int32_t b = rds16le (clip->keys + hi * 12 + 2 + i * 2);
		v[i] = (int16_t)((a * (int32_t)(16384 - t) + b * (int32_t)t) >> 14);
	}
	return 1;
}

// Assemble a BTP0-style container: block header, clip-name dict (stride-8
// entries, ofsUnit relative to the block) and the prepared unit buffer.
static uint8_t *nsb_assemble_clip_container (const char *container_magic,
	const char *block_magic, const char *const *names, uint32_t n,
	const nb_t *unit, size_t *out_size)
{
	const uint32_t odict_len = 8 + 4 * n + 8 * n + 16 * n;
	const uint32_t ONODE = 8 + 4 * n;
	const uint32_t OENT = ONODE + 8 * n;
	const uint32_t block_size = 8 + odict_len + (uint32_t)unit->len;
	const uint32_t file_size = 0x14 + block_size;
	uint8_t *out = malloc (file_size);
	if (!out)
		return 0;
	memset (out, 0, file_size);
	memcpy (out + 0x00, container_magic, 4);
	out[0x04] = 0xff; out[0x05] = 0xfe;
	out[0x06] = 0x01; out[0x07] = 0x00;
	out[0x08] = (uint8_t)(file_size & 0xff);
	out[0x09] = (uint8_t)((file_size >> 8) & 0xff);
	out[0x0a] = (uint8_t)((file_size >> 16) & 0xff);
	out[0x0b] = (uint8_t)((file_size >> 24) & 0xff);
	out[0x0c] = 0x10; out[0x0d] = 0x00;
	out[0x0e] = 0x01; out[0x0f] = 0x00;
	out[0x10] = 0x14; out[0x11] = 0x00; out[0x12] = 0x00; out[0x13] = 0x00;
	memcpy (out + 0x14, block_magic, 4);
	out[0x18] = (uint8_t)(block_size & 0xff);
	out[0x19] = (uint8_t)((block_size >> 8) & 0xff);
	out[0x1a] = (uint8_t)((block_size >> 16) & 0xff);
	out[0x1b] = (uint8_t)((block_size >> 24) & 0xff);
	const uint32_t dc = 0x1c;
	out[dc + 0] = 0;          // revision
	out[dc + 1] = (uint8_t)n; // numEntry
	out[dc + 2] = (uint8_t)(odict_len & 0xff);
	out[dc + 3] = (uint8_t)((odict_len >> 8) & 0xff);
	out[dc + 4] = 8;          // ofsNode
	out[dc + 5] = 0;
	out[dc + 6] = (uint8_t)(ONODE & 0xff); // ofsEntry
	out[dc + 7] = (uint8_t)((ONODE >> 8) & 0xff);
	for (uint32_t i = 0; i < n; i++)
	{
		out[dc + 8 + i * 4 + 0] = 0; // refBit
		out[dc + 8 + i * 4 + 1] = 0; // idxLeft
		out[dc + 8 + i * 4 + 2] = 0; // idxRight
		out[dc + 8 + i * 4 + 3] = (uint8_t)i; // idxEntry
	}
	for (uint32_t i = 0; i < n; i++)
	{
		uint32_t eo = dc + ONODE + i * 8;
		out[eo + 0] = 4; // sizeUnit
		out[eo + 1] = 0;
		out[eo + 2] = (uint8_t)((8 * n) & 0xff); // ofsName (rel. entry base)
		out[eo + 3] = (uint8_t)(((8 * n) >> 8) & 0xff);
		out[eo + 4] = 0; // ofsUnit low (patched)
		out[eo + 5] = 0;
		out[eo + 6] = 0;
		out[eo + 7] = 0;
	}
	const uint32_t nb = dc + OENT;
	for (uint32_t i = 0; i < n; i++)
	{
		uint8_t name16[16];
		nsb_write_name16 (name16, names[i]);
		memcpy (out + nb + i * 16, name16, 16);
	}
	memcpy (out + dc + odict_len, unit->b, unit->len);
	for (uint32_t i = 0; i < n; i++)
	{
		uint32_t eo = dc + ONODE + i * 8;
		const uint32_t unit_rel = 8 + odict_len;
		out[eo + 4] = (uint8_t)(unit_rel & 0xff);
		out[eo + 5] = (uint8_t)((unit_rel >> 8) & 0xff);
		out[eo + 6] = (uint8_t)((unit_rel >> 16) & 0xff);
		out[eo + 7] = (uint8_t)((unit_rel >> 24) & 0xff);
	}
	if (out_size)
		*out_size = file_size;
	return out;
}

uint8_t *BuildNSBMA (uint32_t num_frame, const nsb_ma_clip_spec_t *clips,
	size_t num_clips, size_t *out_size)
{
	if (out_size)
		*out_size = 0;
	if (!clips || !num_frame || num_clips == 0 || num_clips > 15)
		return 0;
	size_t total_keys = 0;
	for (size_t i = 0; i < num_clips; i++)
	{
		const nsb_ma_clip_spec_t *s = &clips[i];
		if (!s->name || !s->keys || !s->num_keys)
			return 0;
		for (uint32_t k = 0; k < s->num_keys; k++)
		{
			if (s->keys[k].frame >= num_frame)
				return 0;
			if (k && s->keys[k].frame < s->keys[k - 1].frame)
				return 0;
		}
		total_keys += s->num_keys;
	}
	const uint32_t n = (uint32_t)num_clips;
	const uint32_t idict_len = 8 + 4 * n + 12 * n + 16 * n;
	const uint32_t koff = 0x08 + idict_len; // unit-relative key start
	if ((uint64_t)koff + total_keys * 22 > 0xff00)
		return 0;
	nb_t c = { 0 };
	int ok = 1;
	ok = ok && nb_u8 (&c, 'M') && nb_u8 (&c, 0) && nb_u16 (&c, 0x4341);
	ok = ok && nb_u16 (&c, (uint16_t)num_frame);
	ok = ok && nb_u8 (&c, (uint8_t)NSB_MATCOL_CHANNELS) && nb_u8 (&c, 0);
	ok = ok && nb_u8 (&c, 0) && nb_u8 (&c, (uint8_t)n);
	ok = ok && nb_u16 (&c, (uint16_t)idict_len);
	ok = ok && nb_u16 (&c, 0x08);             // ofsNode
	ok = ok && nb_u16 (&c, (uint16_t)(8 + 4 * n)); // ofsEntry
	for (uint32_t i = 0; i < n && ok; i++)
		ok = nb_u32 (&c, i);
	uint32_t ko = koff;
	for (size_t i = 0; i < num_clips && ok; i++)
	{
		const nsb_ma_clip_spec_t *s = &clips[i];
		const uint32_t ratio = (uint16_t)(((uint64_t)s->num_keys << 16) / num_frame);
		ok = ok && nb_u16 (&c, 8);                       // sizeUnit
		ok = ok && nb_u16 (&c, (uint16_t)(12 * n));      // ofsName
		ok = ok && nb_u16 (&c, (uint16_t)s->num_keys);   // numFV
		ok = ok && nb_u16 (&c, 0);                       // flag
		ok = ok && nb_u16 (&c, (uint16_t)ratio);         // ratioDataFrame
		ok = ok && nb_u16 (&c, (uint16_t)ko);            // offset (unit-relative)
		ko += s->num_keys * 22;
	}
	for (size_t i = 0; i < num_clips && ok; i++)
	{
		uint8_t name16[16];
		nsb_write_name16 (name16, clips[i].name);
		ok = ok && nb_bytes (&c, name16, 16);
	}
	for (size_t i = 0; i < num_clips && ok; i++)
		for (uint32_t k = 0; k < clips[i].num_keys && ok; k++)
		{
			ok = ok && nb_u16 (&c, (uint16_t)clips[i].keys[k].frame);
			for (int ch = 0; ch < NSB_MATCOL_CHANNELS && ok; ch++)
				ok = ok && nb_u32 (&c, clips[i].keys[k].color[ch]);
		}
	if (!ok)
	{
		free (c.b);
		return 0;
	}
	const char **names = malloc (sizeof (*names) * n);
	if (!names)
	{
		free (c.b);
		return 0;
	}
	for (size_t i = 0; i < num_clips; i++)
		names[i] = clips[i].name;
	uint8_t *out = nsb_assemble_clip_container ("BMA0", "MAT0", names, n, &c, out_size);
	free (c.b);
	free (names);
	return out;
}

uint8_t *BuildNSBTA (uint32_t num_frame, const nsb_ta_clip_spec_t *clips,
	size_t num_clips, size_t *out_size)
{
	if (out_size)
		*out_size = 0;
	if (!clips || !num_frame || num_clips == 0 || num_clips > 15)
		return 0;
	size_t total_keys = 0;
	for (size_t i = 0; i < num_clips; i++)
	{
		const nsb_ta_clip_spec_t *s = &clips[i];
		if (!s->name || !s->keys || !s->num_keys)
			return 0;
		for (uint32_t k = 0; k < s->num_keys; k++)
		{
			if (s->keys[k].frame >= num_frame)
				return 0;
			if (k && s->keys[k].frame < s->keys[k - 1].frame)
				return 0;
		}
		total_keys += s->num_keys;
	}
	const uint32_t n = (uint32_t)num_clips;
	const uint32_t idict_len = 8 + 4 * n + 12 * n + 16 * n;
	const uint32_t koff = 0x08 + idict_len; // unit-relative key start
	if ((uint64_t)koff + total_keys * 12 > 0xff00)
		return 0;
	nb_t c = { 0 };
	int ok = 1;
	ok = ok && nb_u8 (&c, 'T') && nb_u8 (&c, 0) && nb_u16 (&c, 0x4341);
	ok = ok && nb_u16 (&c, (uint16_t)num_frame);
	ok = ok && nb_u8 (&c, 0) && nb_u8 (&c, 0); // flag, texMtxMode
	ok = ok && nb_u8 (&c, 0) && nb_u8 (&c, (uint8_t)n);
	ok = ok && nb_u16 (&c, (uint16_t)idict_len);
	ok = ok && nb_u16 (&c, 0x08);             // ofsNode
	ok = ok && nb_u16 (&c, (uint16_t)(8 + 4 * n)); // ofsEntry
	for (uint32_t i = 0; i < n && ok; i++)
		ok = nb_u32 (&c, i);
	uint32_t ko = koff;
	for (size_t i = 0; i < num_clips && ok; i++)
	{
		const nsb_ta_clip_spec_t *s = &clips[i];
		const uint32_t ratio = (uint16_t)(((uint64_t)s->num_keys << 16) / num_frame);
		ok = ok && nb_u16 (&c, 8);                       // sizeUnit
		ok = ok && nb_u16 (&c, (uint16_t)(12 * n));      // ofsName
		ok = ok && nb_u16 (&c, (uint16_t)s->num_keys);   // numFV
		ok = ok && nb_u16 (&c, 0);                       // flag
		ok = ok && nb_u16 (&c, (uint16_t)ratio);         // ratioDataFrame
		ok = ok && nb_u16 (&c, (uint16_t)ko);            // offset (unit-relative)
		ko += s->num_keys * 12;
	}
	for (size_t i = 0; i < num_clips && ok; i++)
	{
		uint8_t name16[16];
		nsb_write_name16 (name16, clips[i].name);
		ok = ok && nb_bytes (&c, name16, 16);
	}
	for (size_t i = 0; i < num_clips && ok; i++)
		for (uint32_t k = 0; k < clips[i].num_keys && ok; k++)
		{
			ok = ok && nb_u16 (&c, (uint16_t)clips[i].keys[k].frame);
			for (int p = 0; p < NSB_TEXSRT_PARAMS && ok; p++)
				ok = ok && nb_u16 (&c, (uint16_t)clips[i].keys[k].v[p]);
		}
	if (!ok)
	{
		free (c.b);
		return 0;
	}
	const char **names = malloc (sizeof (*names) * n);
	if (!names)
	{
		free (c.b);
		return 0;
	}
	for (size_t i = 0; i < num_clips; i++)
		names[i] = clips[i].name;
	uint8_t *out = nsb_assemble_clip_container ("BTA0", "SRT0", names, n, &c, out_size);
	free (c.b);
	free (names);
	return out;
}

// Encoders
//
// BVA0/BMA0/BTA0/BTP0 have no glTF representation, so a model can only carry
// them as preserved raw bytes; Encode* reproduces those exactly.  BCA0 has a
// real glTF form, so EncodeNSBCA prefers the preserved bytes (byte-exact
// decode -> encode round trip) and falls back to a freshly-built BCA0 when the
// model came from a GLB and only carries decoded TRS channels.
//-----------------------------------------------------------------------------

uint8_t *EncodeNSBCA (const model_t *model, size_t *out_size)
{
	if (out_size) *out_size = 0;
	const model_nsb_raw_t *r = nsb_find_raw (model, NSB_RAW_BCA0);
	if (r)
		return nsb_raw_copy (r, out_size);
	if (!model || !model->num_animations)
		return 0;
	return nsb_build_bca0 (&model->animations[0], out_size);
}
uint8_t *EncodeNSBVA (const model_t *model, size_t *out_size)
{
	if (out_size) *out_size = 0;
	return nsb_raw_copy (nsb_find_raw (model, NSB_RAW_BVA0), out_size);
}
uint8_t *EncodeNSBMA (const model_t *model, size_t *out_size)
{
	if (out_size) *out_size = 0;
	return nsb_raw_copy (nsb_find_raw (model, NSB_RAW_BMA0), out_size);
}
uint8_t *EncodeNSBTA (const model_t *model, size_t *out_size)
{
	if (out_size) *out_size = 0;
	return nsb_raw_copy (nsb_find_raw (model, NSB_RAW_BTA0), out_size);
}
uint8_t *EncodeNSBTP (const model_t *model, size_t *out_size)
{
	if (out_size) *out_size = 0;
	return nsb_raw_copy (nsb_find_raw (model, NSB_RAW_BTP0), out_size);
}

//-----------------------------------------------------------------------------
// Sibling import: merge NSB* animation files that sit next to an NSBMD into
// the same model, mirroring import_chr0_siblings_for_mdl0.  NDS games keep
// one flat directory per archive: "<dir>/<name>.nsbmd", "<dir>/<name>.nsbca",
// "<dir>/<name>.nsbta", ... are all siblings of the same capture.
//-----------------------------------------------------------------------------

void ImportNSBAnimSiblings (model_t *model, const char *nsbmd_path)
{
	if (!model || !nsbmd_path || !*nsbmd_path)
		return;

	char base[PATH_MAX];
	snprintf (base, sizeof (base), "%s", nsbmd_path);
	const char *slash = strrchr (base, '/');
	char *dot = slash ? strrchr (slash + 1, '.') : strrchr (base, '.');
	if (dot)
		*dot = 0;

	const char *kinds[] = { ".nsbca", ".nsbta", ".nsbtp", ".nsbva", ".nsbma",
		".bca", ".bta", ".btp", ".bva", ".bma", 0 };
	for (int k = 0; kinds[k]; k++)
	{
		char path[PATH_MAX];
		snprintf (path, sizeof (path), "%s%s", base, kinds[k]);
		u8 *data = 0;
		size_t size = 0;
		if (LoadFileAlloc (path, 0, 0, &data, &size, 0, 2, 0, false))
			continue;
		if (size >= 4)
		{
			if (!memcmp (data, "BCA0", 4))
				ParseNSBCAIntoModel (model, data, size, path);
			else if (!memcmp (data, "BTA0", 4))
				ParseNSBTAIntoModel (model, data, size, path);
			else if (!memcmp (data, "BTP0", 4))
				ParseNSBTPIntoModel (model, data, size, path);
			else if (!memcmp (data, "BVA0", 4))
				ParseNSBVAIntoModel (model, data, size, path);
			else if (!memcmp (data, "BMA0", 4))
				ParseNSBMAIntoModel (model, data, size, path);
		}
		FREE (data);
	}
}
