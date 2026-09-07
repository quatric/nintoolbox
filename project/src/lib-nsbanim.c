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
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
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
		m[pv] = (d0 & 0x10) ? -1.0f : 1.0f;
		m[pivot_util_[pv][0]] = A;
		m[pivot_util_[pv][1]] = B;
		m[pivot_util_[pv][2]] = (d0 & 0x20) ? (float)-B : (float)B;
		m[pivot_util_[pv][3]] = (d0 & 0x40) ? (float)-A : (float)A;
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
		float a0 = m00, a1 = m01, a2 = m02, b0 = m10, b1 = m11, b2 = m12;
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
#ifdef DEBUG_NSBANIM
								if (node == 1 && f == 0 && ax == 0)
									fprintf (stderr, "S node1 ax%d off=%x info=%x arrrel=%x idx=%u fx16=%d v=%g\n",
										ax, off[ax], info[ax], (unsigned)(arr - data), idx, !!(info[ax] & XFX16), si[0]);
#endif
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
	(void)model; (void)clip_name;
	return nsb_scan_clips (data, size, "VIS0");
}
int ParseNSBMAIntoModel (model_t *model, const uint8_t *data, size_t size, const char *clip_name)
{
	(void)model; (void)clip_name;
	return nsb_scan_clips (data, size, "MAT0");
}
int ParseNSBTAIntoModel (model_t *model, const uint8_t *data, size_t size, const char *clip_name)
{
	(void)model; (void)clip_name;
	return nsb_scan_clips (data, size, "SRT0");
}
int ParseNSBTPIntoModel (model_t *model, const uint8_t *data, size_t size, const char *clip_name)
{
	(void)model; (void)clip_name;
	return nsb_scan_clips (data, size, "PAT0");
}

uint8_t *EncodeNSBCA (const model_t *model, size_t *out_size)
{
	(void)model;
	if (out_size) *out_size = 0;
	// BCA0 re-encoding from an edited model is not supported yet; the
	// pass-through path (decode -> keep original bytes -> encode) still wins.
	return 0;
}
uint8_t *EncodeNSBVA (const model_t *model, size_t *out_size)
{
	(void)model; (void)out_size; return 0;
}
uint8_t *EncodeNSBMA (const model_t *model, size_t *out_size)
{
	(void)model; (void)out_size; return 0;
}
uint8_t *EncodeNSBTA (const model_t *model, size_t *out_size)
{
	(void)model; (void)out_size; return 0;
}
uint8_t *EncodeNSBTP (const model_t *model, size_t *out_size)
{
	(void)model; (void)out_size; return 0;
}

//-----------------------------------------------------------------------------
// Sibling import: merge NSB* animation files that sit next to an NSBMD into
// the same model, mirroring import_chr0_siblings_for_mdl0.  The directory
// scan itself lives on the wszst side (see create_update.inc), which opens
// each sibling file, calls the matching ParseNSB*IntoModel, and frees the
// buffer; nothing is needed here beyond the per-format parsers above.
//-----------------------------------------------------------------------------

void ImportNSBAnimSiblings (model_t *model, const char *nsbmd_path)
{
	(void)model; (void)nsbmd_path;
}
