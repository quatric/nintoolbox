// BFRES ("FRES") -- Nintendo's NintendoWare resource container.
//
// This handles the Wii U flavour (version 3.x, big endian), whose offsets
// are all *self-relative*: the stored value is added to the address of the
// field holding it. Geometry lives in FMDL -> FVTX (vertex buffers, laid out
// as interleaved GX2 attributes) and FSHP (shapes, each with a LOD carrying
// an index buffer).
//
// The Switch flavour reuses the "FRES" magic but is little endian with a
// completely different layout and keeps its textures in a separate BNTX; it
// is detected and rejected here rather than misparsed.

#include "lib-bfres.h"
#include "lib-brres-model.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

extern int DecodeFZIP (
	uint8_t **dest, unsigned int *dest_size, const uint8_t *src, unsigned int src_size);

typedef unsigned int uint;

static uint16_t rb16 (const uint8_t *p)
{
	return (uint16_t)p[0] << 8 | p[1];
}
static uint32_t rb32 (const uint8_t *p)
{
	return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}
static int32_t rbs32 (const uint8_t *p)
{
	return (int32_t)rb32 (p);
}

static inline float read_be32f (const uint8_t *p)
{
	union
	{
		uint32_t u;
		float f;
	} c;
	c.u = rb32 (p);
	return c.f;
}

// Half-precision float, as used by the 16-bit vertex attribute formats.
static float half_to_float (uint16_t h)
{
	const int sign = (h >> 15) & 1;
	int exp = (h >> 10) & 0x1F;
	int man = h & 0x3FF;
	float v;
	if (!exp)
		v = man ? (float)man / 16384.0f / 64.0f : 0.0f; // subnormal
	else if (exp == 31)
		v = man ? 0.0f : 1e30f; // NaN/Inf -> tame value
	else
	{
		float m = 1.0f + (float)man / 1024.0f;
		int e = exp - 15;
		v = m;
		while (e > 0)
		{
			v *= 2.0f;
			e--;
		}
		while (e < 0)
		{
			v /= 2.0f;
			e++;
		}
	}
	return sign ? -v : v;
}

// Resolves a self-relative offset stored at ADDR.
#define REL(base, addr) ((size_t)(addr) + (size_t)rbs32 ((base) + (addr)))

static const char *rel_string (const uint8_t *d, size_t size, size_t at)
{
	if (at + 4 > size)
		return NULL;
	const size_t p = REL (d, at);
	if (p >= size)
		return NULL;
	for (size_t q = p; q < size; q++)
		if (!d[q])
			return (const char *)(d + p);
	return NULL;
}

//-----------------------------------------------------------------------------
// GX2 vertex attribute formats. Only the ones that actually carry geometry
// are handled; anything else leaves the component at zero.
//-----------------------------------------------------------------------------

static int attr_read (const uint8_t *p, size_t avail, uint32_t fmt, float out[4])
{
	out[0] = out[1] = out[2] = 0.0f;
	out[3] = 1.0f;
	switch (fmt)
	{
		case 0x00000004: // 8_8 unorm (alias)
		case 0x00000005: // 8_8 unorm
			if (avail < 2)
				return 0;
			out[0] = p[0] / 255.0f;
			out[1] = p[1] / 255.0f;
			return 1;
		case 0x00000204: // 8_8 snorm (alias)
		case 0x00000205: // 8_8 snorm
			if (avail < 2)
				return 0;
			out[0] = (int8_t)p[0] / 127.0f;
			out[1] = (int8_t)p[1] / 127.0f;
			return 1;
		case 0x00000007: // 16_16 unorm (alias)
		case 0x00000008: // 16_16 unorm
			if (avail < 4)
				return 0;
			out[0] = rb16 (p) / 65535.0f;
			out[1] = rb16 (p + 2) / 65535.0f;
			return 1;
		case 0x00000207: // 16_16 snorm (alias)
		case 0x00000208: // 16_16 snorm
			if (avail < 4)
				return 0;
			out[0] = (int16_t)rb16 (p) / 32767.0f;
			out[1] = (int16_t)rb16 (p + 2) / 32767.0f;
			return 1;
		case 0x0000000A: // 8_8_8_8 unorm
			if (avail < 4)
				return 0;
			for (int i = 0; i < 4; i++)
				out[i] = p[i] / 255.0f;
			return 1;
		case 0x0000020A: // 8_8_8_8 snorm
			if (avail < 4)
				return 0;
			for (int i = 0; i < 4; i++)
				out[i] = (int8_t)p[i] / 127.0f;
			return 1;
		case 0x0000000B: // 10_10_10_2 unorm
		case 0x0000000C: // 10_10_10_2 unorm
		{
			if (avail < 4)
				return 0;
			const uint32_t v = rb32 (p);
			for (int i = 0; i < 3; i++)
				out[i] = (float)((v >> (i * 10)) & 0x3FF) / 1023.0f;
			out[3] = (float)((v >> 30) & 3) / 3.0f;
			return 1;
		}
		case 0x0000020B: // 10_10_10_2 snorm
		case 0x0000020C: // 10_10_10_2 snorm
		{
			if (avail < 4)
				return 0;
			const uint32_t v = rb32 (p);
			for (int i = 0; i < 3; i++)
			{
				int c = (v >> (i * 10)) & 0x3FF;
				if (c & 0x200)
					c -= 0x400;
				out[i] = (float)c / 511.0f;
			}
			int w = (v >> 30) & 3;
			if (w & 2)
				w -= 4;
			out[3] = (float)w;
			return 1;
		}
		case 0x00000806: // 16_16 float
		case 0x00000809: // 16_16 float
			if (avail < 4)
				return 0;
			out[0] = half_to_float (rb16 (p));
			out[1] = half_to_float (rb16 (p + 2));
			return 1;
		case 0x00000807: // 32 float
		case 0x00000808: // 32 float
		{
			if (avail < 4)
				return 0;
			union
			{
				uint32_t u;
				float f;
			} c;
			c.u = rb32 (p);
			out[0] = c.f;
			return 1;
		}
		case 0x0000080A: // 16_16_16_16 float
		case 0x0000080E: // 16_16_16_16 float
			if (avail < 8)
				return 0;
			for (int i = 0; i < 4; i++)
				out[i] = half_to_float (rb16 (p + i * 2));
			return 1;
		case 0x0000080D: // 32_32 float
		case 0x0000080F: // 32_32 float
		{
			if (avail < 8)
				return 0;
			for (int i = 0; i < 2; i++)
			{
				union
				{
					uint32_t u;
					float f;
				} c;
				c.u = rb32 (p + i * 4);
				out[i] = c.f;
			}
			return 1;
		}
		case 0x00000811: // 32_32_32 float
		case 0x00000813: // 32_32_32 float
		{
			if (avail < 12)
				return 0;
			for (int i = 0; i < 3; i++)
			{
				union
				{
					uint32_t u;
					float f;
				} c;
				c.u = rb32 (p + i * 4);
				out[i] = c.f;
			}
			return 1;
		}
		case 0x00000814: // 32_32_32_32 float
		{
			if (avail < 16)
				return 0;
			for (int i = 0; i < 4; i++)
			{
				union
				{
					uint32_t u;
					float f;
				} c;
				c.u = rb32 (p + i * 4);
				out[i] = c.f;
			}
			return 1;
		}
	}
	return 0;
}

//-----------------------------------------------------------------------------

typedef struct
{
	const uint8_t *pos, *nrm, *uv, *clr, *bone, *wt;
	uint32_t fmt_pos, fmt_nrm, fmt_uv, fmt_clr, fmt_bone, fmt_wt;
	uint stride_pos, stride_nrm, stride_uv, stride_clr, stride_bone, stride_wt;
	size_t avail_pos, avail_nrm, avail_uv, avail_clr, avail_bone, avail_wt;
	// Extra UV channels (_u1.._u6) and secondary color (_c1).
	const uint8_t *extra_uv[6];
	uint32_t fmt_extra_uv[6];
	uint stride_extra_uv[6];
	size_t avail_extra_uv[6];
	const uint8_t *clr1;
	uint32_t fmt_clr1;
	uint stride_clr1;
	size_t avail_clr1;
	// Tangent stream (_t0) for models that carry explicit tangent data.
	const uint8_t *tan;
	uint32_t fmt_tan;
	uint stride_tan;
	size_t avail_tan;
	uint count;
} fvtx_t;

// Reads one FVTX and locates its position/normal/uv attribute streams.
static int read_fvtx (const uint8_t *d, size_t size, size_t fv, fvtx_t *out)
{
	if (fv + 0x20 > size || memcmp (d + fv, "FVTX", 4))
		return 0;
	memset (out, 0, sizeof (*out));

	const uint n_attr = d[fv + 4], n_buf = d[fv + 5];
	out->count = rb32 (d + fv + 8);
	if (!out->count || !n_attr || !n_buf)
		return 0;

	const size_t attrs = REL (d, fv + 0x10);
	const size_t bufs = REL (d, fv + 0x18);
	if (attrs + (size_t)n_attr * 12 > size || bufs + (size_t)n_buf * 0x18 > size)
		return 0;

	for (uint i = 0; i < n_attr; i++)
	{
		const size_t a = attrs + i * 12;
		const char *name = rel_string (d, size, a);
		if (!name)
			continue;
		const uint bi = d[a + 4];
		const uint boff = rb16 (d + a + 6);
		const uint32_t fmt = rb32 (d + a + 8);
		if (bi >= n_buf)
			continue;

		const size_t b = bufs + (size_t)bi * 0x18;
		const uint32_t bsize = rb32 (d + b + 4);
		const uint stride = rb16 (d + b + 0x0c);
		const size_t data = REL (d, b + 0x14);
		if (!stride || data >= size || data + bsize > size)
			continue;
		if (boff >= stride)
			continue;

		const uint8_t *p = d + data + boff;
		const size_t avail = size - (data + boff);
		// Attribute names follow the "_p0"/"_n0"/"_u0"/"_c0"/"_t0" convention.
		if (!strncmp (name, "_p", 2) && !out->pos)
		{
			out->pos = p;
			out->fmt_pos = fmt;
			out->stride_pos = stride;
			out->avail_pos = avail;
		}
		else if (!strncmp (name, "_n", 2) && !out->nrm)
		{
			out->nrm = p;
			out->fmt_nrm = fmt;
			out->stride_nrm = stride;
			out->avail_nrm = avail;
		}
		else if (!strncmp (name, "_t", 2) && !out->tan)
		{
			out->tan = p;
			out->fmt_tan = fmt;
			out->stride_tan = stride;
			out->avail_tan = avail;
		}
		else if (name[0] == '_' && name[1] == 'u' && name[2] >= '0' && name[2] <= '6')
		{
			const uint ch = name[2] - '0';
			if (ch == 0 && !out->uv)
			{
				out->uv = p;
				out->fmt_uv = fmt;
				out->stride_uv = stride;
				out->avail_uv = avail;
			}
			else if (ch > 0 && !out->extra_uv[ch - 1])
			{
				out->extra_uv[ch - 1] = p;
				out->fmt_extra_uv[ch - 1] = fmt;
				out->stride_extra_uv[ch - 1] = stride;
				out->avail_extra_uv[ch - 1] = avail;
			}
		}
		else if (name[0] == '_' && name[1] == 'c' && name[2] >= '0' && name[2] <= '1')
		{
			const uint ch = name[2] - '0';
			if (ch == 0 && !out->clr)
			{
				out->clr = p;
				out->fmt_clr = fmt;
				out->stride_clr = stride;
				out->avail_clr = avail;
			}
			else if (ch == 1 && !out->clr1)
			{
				out->clr1 = p;
				out->fmt_clr1 = fmt;
				out->stride_clr1 = stride;
				out->avail_clr1 = avail;
			}
		}
	}
	return out->pos != NULL;
}

//-----------------------------------------------------------------------------
// FSKA (skeletal animation) parsing for Wii U models.
//
// Layouts and key semantics were reverse-engineered from KillzXGaming's
// BfresLibrary (AnimCurve.cs, SkeletalAnim.cs, BoneAnim.cs) and verified
// byte-for-byte against in-game data (YWW AMBBASE00). Curves are resampled at
// every integer frame -- conveniently, exactly the points the exporter's
// linear channel interpolation needs, with none of the file's Hermite-style
// input coefficients leaking into glTF.  Un-animated TRS components of an
// animated joint are left at the animation's base values where present, else
// at the skeleton's bind value.
//-----------------------------------------------------------------------------

static int bfres_find_joint (const model_t *model, const char *name)
{
	for (size_t i = 0; i < model->num_joints; i++)
		if (name && !strcmp (model->joints[i].name, name))
			return (int)i;
	return -1;
}

// FSKA's Euler rotations are actual radians (unlike CHR0's degrees), so this
// uses half-angle sine/cosine of the raw value.
static void bfres_euler_rad_to_quat (float rx, float ry, float rz, float q[4])
{
	const double hx = (double)rx / 2.0;
	const double hy = (double)ry / 2.0;
	const double hz = (double)rz / 2.0;
	const double cx = cos (hx), sx = sin (hx);
	const double cy = cos (hy), sy = sin (hy);
	const double cz = cos (hz), sz = sin (hz);
	q[0] = (float)(sx * cy * cz - cx * sy * sz);
	q[1] = (float)(cx * sy * cz + sx * cy * sz);
	q[2] = (float)(cx * cy * sz - sx * sy * cz);
	q[3] = (float)(cx * cy * cz + sx * sy * sz);
}

// Inverse of the above, for converting an FSKL bone whose rotation is stored
// as a quaternion into the half-angle-Euler convention joint_t::rotate uses.
static void bfres_quat_to_euler (float qx, float qy, float qz, float qw,
	float *rx, float *ry, float *rz)
{
	const double x = qx, y = qy, z = qz, w = qw;
	const double xx = x * x, yy = y * y, zz = z * z;
	const double m02 = 2.0 * (x * z + y * w);
	const double m12 = 2.0 * (y * z - x * w);
	const double s = m02 >= 0.0 ? 1.0 : -1.0;
	*ry = (float)(s * M_PI / 2.0);
	*rx = (float)atan2 (-m12, 1.0 - 2.0 * (xx + yy));
	const double m10 = 2.0 * (x * y + z * w);
	const double m11 = 1.0 - 2.0 * (xx + zz);
	if (fabs (m02) < 1e-4) // roll-pitch degenerate near the poles: fold Z in
		*rz = (float)atan2 (m10, m11);
	else
		*rz = 0.0f;
}

static void bfres_anim_add_channel (model_animation_t *anim, int node_idx, model_anim_path_t path,
	float *times, float *values, size_t count, size_t components)
{
	model_anim_channel_t *ch = realloc (anim->channels, sizeof (*ch) * (anim->num_channels + 1));
	if (!ch)
	{
		free (times);
		free (values);
		return;
	}
	anim->channels = ch;
	model_anim_channel_t *c = &anim->channels[anim->num_channels++];
	c->node_idx = node_idx;
	c->path = path;
	c->times = times;
	c->values = values;
	c->count = count;
	c->components = components;
}

typedef struct
{
	float *frames;   // decoded keyframe times, num_keys entries
	float **keys;    // num_keys * elems_per_key decoded key coefficients
	int num_keys;
	int elems_per_key;
	int frame_type;  // 0=Single, 1=Decimal10x5, 2=Byte
	int key_type;    // 0=Single, 1=Int16, 2=SByte
	int curve_type;  // 0=Cubic, 1=Linear, 2=BakedFloat, 4=StepInt, 5=BakedInt, 6=StepBool
	float scale, offset;
	uint32_t target; // AnimDataOffset this curve animates
} bfres_curve_t;

static void bfres_curve_free (bfres_curve_t *c)
{
	if (c->keys)
		for (int i = 0; i < c->num_keys; i++)
			free (c->keys[i]);
	free (c->keys);
	free (c->frames);
	memset (c, 0, sizeof (*c));
}

// Reads one AnimCurve record. Wii U curve records are 0x24 bytes for
// FRES >= 0x03040000 (explicit Delta), 0x20 before that.
static int bfres_curve_read (const uint8_t *d, size_t size, size_t c,
	int new_layout, bfres_curve_t *out)
{
	const size_t rec = new_layout ? 0x24 : 0x20;
	memset (out, 0, sizeof (*out));
	if (c + rec > size)
		return 0;

	const uint16_t flags = rb16 (d + c);
	const uint16_t num_key = rb16 (d + c + 2);
	if (!num_key)
		return 0;

	out->frame_type = flags & 0x3;
	out->key_type = (flags >> 2) & 0x3;
	out->curve_type = (flags >> 4) & 0x7;
	if (out->frame_type > 2 || out->key_type > 2)
		return 0;
	out->target = rb32 (d + c + 4);
	out->scale = read_be32f (d + c + 0x10);
	out->offset = read_be32f (d + c + 0x14);
	out->elems_per_key = out->curve_type == 0 ? 4 : (out->curve_type == 1 ? 2 : 1);
	out->num_keys = num_key;

	// Skip 0x1C/0x18 frame pointer into the frame array, then the key array.
	const size_t fa = REL (d, c + (new_layout ? 0x1C : 0x18));
	const size_t ka = REL (d, c + (new_layout ? 0x20 : 0x1C));
	if (fa < c || ka < c || fa > size || ka > size)
		return 0;

	const size_t fw = out->frame_type == 0 ? 4 : (out->frame_type == 1 ? 2 : 1);
	const size_t kw = out->key_type == 0 ? 4 : (out->key_type == 1 ? 2 : 1);

	out->frames = malloc (sizeof (float) * num_key);
	out->keys = malloc (sizeof (float *) * num_key);
	if (!out->frames || !out->keys)
	{
		bfres_curve_free (out);
		return 0;
	}

	for (uint16_t i = 0; i < num_key; i++)
	{
		const size_t p = fa + (size_t)i * fw;
		if (p + fw > size)
		{
			bfres_curve_free (out);
			return 0;
		}
		if (out->frame_type == 0)
			out->frames[i] = read_be32f (d + p);
		else if (out->frame_type == 1)
			out->frames[i] = (float)(int16_t)rb16 (d + p) / 32.0f; // Decimal10x5
		else
			out->frames[i] = (float)d[p];
	}

	for (uint16_t i = 0; i < num_key; i++)
	{
		out->keys[i] = calloc (out->elems_per_key, sizeof (float));
		if (!out->keys[i])
		{
			bfres_curve_free (out);
			return 0;
		}
		for (int j = 0; j < out->elems_per_key; j++)
		{
			const size_t p = ka + ((size_t)i * out->elems_per_key + (size_t)j) * kw;
			if (p + kw > size)
			{
				bfres_curve_free (out);
				return 0;
			}
			if (out->key_type == 0)
			{
				// StepInt / StepBool store UInt32 bit patterns; everything
				// else stores raw Single. Keep the 32-bit pattern either way
				// and only interpret it as float for the float curve types.
				out->keys[i][j] = read_be32f (d + p);
			}
			else if (out->key_type == 1)
				out->keys[i][j] = (float)(int16_t)rb16 (d + p);
			else
				out->keys[i][j] = (float)(int8_t)d[p];
		}
	}

	// Key coefficients are already in value units: KeyCount fixes the whole
	// Delta, so a quadratic/step curve needs no fixed-point rescaling. Keep
	// the raw stored int16/sbyte numbers, multiplied by scale at evaluation.
	return 1;
}

static int32_t bfres_int_key (const bfres_curve_t *c, float stored)
{
	if (c->key_type == 0)
	{
		union
		{
			uint32_t u;
			float f;
		} uf;
		uf.f = stored;
		return (int32_t)uf.u; // UInt32-pattern keys (StepInt / StepBool)
	}
	return (int32_t)stored;
}

static int32_t bfres_int_offset (const bfres_curve_t *c)
{
	union
	{
		uint32_t u;
		float f;
	} uf;
	uf.f = c->offset;
	return (int32_t)uf.u; // Offset is a raw DWord for integer curve types
}

static float bfres_key_value (const bfres_curve_t *c, float stored)
{
	switch (c->curve_type)
	{
		case 4:
		case 5:
		case 6: // StepInt / BakedInt / StepBool: integer semantics, no scale
			return (float)(bfres_int_key (c, stored) + bfres_int_offset (c));
		case 2: // BakedFloat
		default:
			return stored * c->scale + c->offset;
	}
}

static float bfres_eval_curve (const bfres_curve_t *c, float t)
{
	const int hi = c->num_keys - 1;
	if (t <= c->frames[0])
		return bfres_key_value (c, c->keys[0][0]);
	if (t >= c->frames[hi])
		return bfres_key_value (c, c->keys[hi][0]);

	int i = 0;
	while (i < hi && t >= c->frames[i + 1])
		i++;

	const float f0 = c->frames[i];
	const float dt = c->frames[i + 1] - f0;
	if (dt <= 0.0f)
		return bfres_key_value (c, c->keys[i][0]);

	switch (c->curve_type)
	{
		case 0: // cubic: P(u) = c0 + c1*u + c2*u^2 + c3*u^3, u in [0,1)
		{
			const float u = (t - f0) / dt;
			const float c0 = c->keys[i][0] * c->scale + c->offset;
			return c0 + u * (c->keys[i][1] * c->scale
				+ u * (c->keys[i][2] * c->scale + u * (c->keys[i][3] * c->scale)));
		}
		case 1: // linear: v = K0*scale + offset + (K1*scale) * (t - frame)
			return c->keys[i][0] * c->scale + c->offset
				+ c->keys[i][1] * c->scale * (t - f0);
		default: // step / baked: constant per segment
			return bfres_key_value (c, c->keys[i][0]);
	}
}

// Parses one FSKA object and appends one model_animation_t. Returns 1 when
// at least one channel was produced.
static int parse_fska_into_model (model_t *model, const uint8_t *d, size_t size,
	uint32_t fr_version, size_t fs, const char *clip_name)
{
	if (fr_version >= 0x03040000)
	{
		if (fs + 0x30 > size)
			return 0;
	}
	else if (fs + 0x24 > size)
		return 0;

	const uint32_t flags = rb32 (d + fs + 0x0C);
	const int euler_rot = (flags & 0x1000) != 0; // bit 0x1000 = EulerXYZ mode
	const int num_frames_hdr = fr_version >= 0x03040000
		? rbs32 (d + fs + 0x10) : (int)rb16 (d + fs + 0x10);
	const uint16_t num_bone_anim = fr_version >= 0x03040000
		? rb16 (d + fs + 0x14) : rb16 (d + fs + 0x12);
	const int num_frames = num_frames_hdr + (((flags & 0x2) != 0) ? 1 : 0); // loop pad
	if (num_bone_anim == 0 || num_frames <= 0 || num_frames > 100000)
		return 0;

	const size_t bone_anims = fs + 0x24 <= size ? REL (d, fs + 0x20) : 0;
	if (!bone_anims || bone_anims + (size_t)num_bone_anim * 0x18 > size)
		return 0;

	model_animation_t anim = { 0 };
	snprintf (anim.name, sizeof (anim.name), "%s",
		clip_name && *clip_name ? clip_name : "fska");

	const int new_curve_layout = fr_version >= 0x03040000;
	int any = 0;

	for (uint16_t bi = 0; bi < num_bone_anim; bi++)
	{
		const size_t ba = bone_anims + (size_t)bi * 0x18;
		const uint32_t bflags = rb32 (d + ba);
		const char *bname = rel_string (d, size, ba + 4);
		const uint8_t num_curve = d[ba + 0xA];
		if (num_curve == 0)
			continue;
		const int joint_idx = bfres_find_joint (model, bname);
		if (joint_idx < 0)
			continue;
		const joint_t *joint = &model->joints[joint_idx];

		// Curves are laid out contiguously after the BoneAnim record, one
		// record per bone (BfresLibrary: loader.LoadList<AnimCurve>(numCurve)).
		const size_t curve_base = REL (d, ba + 0x10);
		if (!curve_base || curve_base + (size_t)num_curve * (new_curve_layout ? 0x24 : 0x20) > size)
			continue;

		bfres_curve_t curves[9];
		memset (curves, 0, sizeof (curves));
		int nread = 0;
		for (uint8_t k = 0; k < num_curve && k < 9; k++)
		{
			if (bfres_curve_read (d, size, curve_base + (size_t)k * (new_curve_layout ? 0x24 : 0x20),
					new_curve_layout, &curves[k]))
				nread++;
		}
		if (!nread)
			continue;

		// Base (bind-affect) values present in the BoneAnim; the game applies
		// these to the whole TRS set before curves override single components.
		const uint32_t base_flags = bflags & 0x38; // 0x8=scale 0x10=rotate 0x20=translate
		float base[3][4] = { { 1, 1, 1, 1 }, { 0, 0, 0, 1 }, { 0, 0, 0, 1 } };
		const int base_present[3] = { (base_flags & 0x8) != 0, (base_flags & 0x10) != 0, (base_flags & 0x20) != 0 };
		const size_t base_off = REL (d, ba + 0x14);
		if (base_off && base_off + 0x28 <= size)
		{
			size_t p = base_off;
			if (base_present[0])
				for (int e = 0; e < 3; e++)
					base[0][e] = read_be32f (d + p + (size_t)e * 4);
			p += 3 * 4;
			if (base_present[1])
				for (int e = 0; e < 4; e++)
					base[1][e] = read_be32f (d + p + (size_t)e * 4);
			p += 4 * 4;
			if (base_present[2])
				for (int e = 0; e < 3; e++)
					base[2][e] = read_be32f (d + p + (size_t)e * 4);
		}

		// Gather which curve targets each TRS component.
		//   scale    0x04 0x08 0x0C
		//   rotate   0x20 0x24 0x28 (+0x2C W for quaternion mode)
		//   translate 0x10 0x14 0x18
		int curve_of[3][4] =
		{
			{ -1, -1, -1, -1 },
			{ -1, -1, -1, -1 },
			{ -1, -1, -1, -1 }
		};
		for (int k = 0; k < nread; k++)
		{
			const uint32_t tgt = curves[k].target & 0xFFFFFFFC;
			switch (tgt & 0xF0)
			{
				case 0x00: if (tgt >= 0x04 && tgt <= 0x0C) curve_of[0][(tgt - 4) / 4] = k; break;
				case 0x10: if (tgt >= 0x10 && tgt <= 0x18) curve_of[2][(tgt - 0x10) / 4] = k; break;
				case 0x20: if (tgt >= 0x20 && tgt <= 0x2C) curve_of[1][(tgt - 0x20) / 4] = k; break;
			}
		}

		const float fps = 60.0f;
		const model_anim_path_t paths[3] = { MODEL_ANIM_SCALE, MODEL_ANIM_ROTATION, MODEL_ANIM_TRANSLATION };

		// Scale (3 components) and translation (3 components): a plane channel.
		for (int set = 0; set < 3; set += 2)
		{
			if (curve_of[set][0] < 0 && curve_of[set][1] < 0 && curve_of[set][2] < 0)
				continue;
			const int comps = 3;
			float *times = malloc (sizeof (float) * num_frames);
			float *values = malloc (sizeof (float) * num_frames * comps);
			if (!times || !values)
			{
				free (times);
				free (values);
				continue;
			}
			for (int f = 0; f < num_frames; f++)
			{
				const float t = (float)f;
				times[f] = t / fps;
				for (int e = 0; e < comps; e++)
				{
					float v;
					if (curve_of[set][e] >= 0)
						v = bfres_eval_curve (&curves[curve_of[set][e]], t);
					else if (base_present[set])
						v = base[set][e];
					else
					{
						const vec3_t *b = set == 0 ? &joint->scale : &joint->translate;
						v = e == 0 ? b->x : (e == 1 ? b->y : b->z);
					}
					values[f * comps + e] = v;
				}
			}
			bfres_anim_add_channel (&anim, joint_idx, paths[set], times, values, num_frames, comps);
			any = 1;
		}

		// Rotation. Euler (bit 0x1000): assemble XYZ then convert to a
		// quaternion channel. Quaternion mode: use the base/curve components.
		if (curve_of[1][0] >= 0 || curve_of[1][1] >= 0 || curve_of[1][2] >= 0 || curve_of[1][3] >= 0)
		{
			const int comps = 4;
			float *times = malloc (sizeof (float) * num_frames);
			float *values = malloc (sizeof (float) * num_frames * comps);
			if (times && values)
			{
				for (int f = 0; f < num_frames; f++)
				{
					const float t = (float)f;
					times[f] = t / fps;
					float v[4];
					if (euler_rot)
					{
						const float *base_r = base[1];
						const vec3_t *jb = &joint->rotate;
						float e[3];
						for (int e2 = 0; e2 < 3; e2++)
						{
							if (curve_of[1][e2] >= 0)
								e[e2] = bfres_eval_curve (&curves[curve_of[1][e2]], t);
							else if (base_present[1])
								e[e2] = base_r[e2];
							else
								e[e2] = e2 == 0 ? jb->x : (e2 == 1 ? jb->y : jb->z);
						}
						bfres_euler_rad_to_quat (e[0], e[1], e[2], v);
					}
					else
					{
						for (int e2 = 0; e2 < 4; e2++)
						{
							if (curve_of[1][e2] >= 0)
								v[e2] = bfres_eval_curve (&curves[curve_of[1][e2]], t);
							else if (base_present[1])
								v[e2] = base[1][e2];
							else
								v[e2] = e2 == 3 ? 1.0f : 0.0f;
						}
					}
					memcpy (values + f * comps, v, sizeof (v));
				}
				bfres_anim_add_channel (&anim, joint_idx, MODEL_ANIM_ROTATION, times, values, num_frames, comps);
				any = 1;
			}
			else
			{
				free (times);
				free (values);
			}
		}

		for (int k = 0; k < nread; k++)
			bfres_curve_free (&curves[k]);
	}

	if (!any)
	{
		free (anim.channels);
		return 0;
	}

	model_animation_t *na = realloc (model->animations, sizeof (*na) * (model->num_animations + 1));
	if (!na)
	{
		for (size_t c = 0; c < anim.num_channels; c++)
		{
			free (anim.channels[c].times);
			free (anim.channels[c].values);
		}
		free (anim.channels);
		return 0;
	}
	model->animations = na;
	model->animations[model->num_animations++] = anim;
	return 1;
}

model_t *ParseBFRES (const uint8_t *data, size_t size)
{
	if (!data || size < 8)
		return NULL;

	if (!memcmp (data, "FZIP", 4))
	{
		uint8_t *dec = NULL;
		unsigned int dec_sz = 0;
		if (DecodeFZIP (&dec, &dec_sz, data, (unsigned int)size) == 0 && dec)
		{
			model_t *m = ParseBFRES (dec, dec_sz);
			if (!m)
				m = ParseBFRESSwitch (dec, dec_sz);
			free (dec);
			return m;
		}
	}

	if (size < 0x60 || memcmp (data, "FRES", 4))
		return NULL;

	// Wii U BFRES is big endian and version 3.x; Switch BFRES reuses the
	// magic with a different layout entirely.
	if (rb16 (data + 8) != 0xFEFF)
		return NULL;
	if (data[4] != 3)
		return NULL;
	// The version lives at +4 (e.g. 3.5.0.3 = 0x03050003); +8 is the
	// byte-order marker (0xFEFF) plus platform flags, so reading the layout
	// version from +8 would always look "new".
	const uint32_t bfr_version = rb32 (data + 4);

	const uint8_t *d = data;

	// Index group 0 is FMDL.
	const uint16_t n_fmdl = rb16 (d + 0x50);
	if (!n_fmdl)
		return NULL;
	const size_t grp = REL (d, 0x20);
	if (grp + 8 > size)
		return NULL;
	const uint32_t entries = rb32 (d + grp + 4);
	if (!entries || entries > 0x10000 || grp + 8 + (size_t)(entries + 1) * 16 > size)
		return NULL;

	// First model only: DAE has no multi-model concept here.
	const size_t e = grp + 8 + 16;
	const size_t m = REL (d, e + 12);
	if (m + 0x30 > size || memcmp (d + m, "FMDL", 4))
		return NULL;

	const uint16_t n_fvtx = rb16 (d + m + 0x20);
	const uint16_t n_fshp = rb16 (d + m + 0x22);
	if (!n_fvtx || !n_fshp)
		return NULL;

	const size_t fvtx_arr = REL (d, m + 0x10);
	const size_t fshp_grp = REL (d, m + 0x14);
	if (fshp_grp + 8 > size)
		return NULL;

	model_t *out = calloc (1, sizeof (model_t));
	if (!out)
		return NULL;
	out->meshes = calloc (n_fshp, sizeof (mesh_t));
	if (!out->meshes)
	{
		free (out);
		return NULL;
	}

	// FMAT materials (FMDL+0x18 index group; header layout verified against
	// mk8.tockdom.com's FMDL doc page byte-for-byte, plus KillzXGaming/
	// BfresLibrary's TextureRef.cs for the texture-ref array's [nameOffset,
	// ftexOffset] pair -- only the first texture ref per material is bound
	// (diffuse-slot heuristic; real files commonly have several ref'd
	// textures -- e.g. normal/specular -- that this fork's DAE export has
	// no material-model slot for yet). Texture *names* only, not pixel data
	// -- the actual FTEX decode-to-PNG happens in wszst.c's extraction
	// pass, so this only needs to match the names those PNGs get written
	// under (see extract_bfres_textures() in wszst.c).
	const uint16_t n_fmat = rb16 (d + m + 0x24);
	const size_t fmat_grp = REL (d, m + 0x18);
	if (n_fmat && fmat_grp + 8 <= size)
	{
		out->materials = calloc (n_fmat, sizeof (material_t));
		if (out->materials)
		{
			const uint32_t mat_entries = rb32 (d + fmat_grp + 4);
			for (uint32_t i = 0; i < mat_entries && i < n_fmat; i++)
			{
				const size_t me = fmat_grp + 8 + (size_t)(i + 1) * 16;
				if (me + 16 > size)
					break;
				const size_t fm = REL (d, me + 12);
				if (fm + 0x4C > size || memcmp (d + fm, "FMAT", 4))
					continue;

				material_t *mat = out->materials + out->num_materials++;
				const char *mname = rel_string (d, size, fm + 4);
				snprintf (
					mat->name, sizeof (mat->name), "%s", mname && *mname ? mname : "material");

				const uint8_t n_texref = d[fm + 0x11];
				if (n_texref)
				{
					const size_t texrefs = REL (d, fm + 0x28);
					// TextureRef: 8 bytes, [nameOffset:4][ftexOffset:4].
					if (texrefs + 8 <= size)
					{
						const char *tname = rel_string (d, size, texrefs);
						if (tname)
						{
							snprintf (mat->textures[0], sizeof (mat->textures[0]), "%s", tname);
							mat->texture_coord[0] = 0; // uv0
							mat->num_textures = 1;
						}
					}
				}
			}
		}
	}

	const uint32_t sh_entries = rb32 (d + fshp_grp + 4);
	for (uint32_t i = 0; i < sh_entries && i < n_fshp; i++)
	{
		const size_t se = fshp_grp + 8 + (size_t)(i + 1) * 16;
		if (se + 16 > size)
			break;
		const size_t sh = REL (d, se + 12);
		if (sh + 0x30 > size || memcmp (d + sh, "FSHP", 4))
			continue;

		const char *name = rel_string (d, size, sh + 4);
		const uint16_t vtx_index = rb16 (d + sh + 0x12); // FSHP+0x12: FVTX index
		if (vtx_index >= n_fvtx)
			continue;

		// Each FVTX is 0x20 bytes of header in the array.
		fvtx_t fvtx;
		if (!read_fvtx (d, size, fvtx_arr + (size_t)vtx_index * 0x20, &fvtx))
			continue;

		// LOD model: primitive type, index format, count, then the buffer.
		const size_t lod = REL (d, sh + 0x24);
		if (lod + 0x18 > size)
			continue;
		const uint32_t prim = rb32 (d + lod);
		const uint32_t ifmt = rb32 (d + lod + 4);
		const uint32_t icount = rb32 (d + lod + 8);
		if (prim != 4 || !icount || icount > 0x1000000)
			continue; // triangles only

		const size_t ibo = REL (d, lod + 0x14);
		if (ibo + 0x18 > size)
			continue;
		const size_t idata = REL (d, ibo + 0x14);
		// Index format 4 is 16-bit, 9 is 32-bit.
		const uint isz = ifmt == 9 ? 4 : 2;
		if (idata + (size_t)icount * isz > size)
			continue;

		mesh_t *mesh = out->meshes + out->num_meshes;
		snprintf (mesh->name, sizeof (mesh->name), "%s", name && *name ? name : "shape");
		const uint16_t fmat_idx = rb16 (d + sh + 0x0E); // FSHP+0x0E: FMAT index
		mesh->material_idx = fmat_idx < out->num_materials ? (int)fmat_idx : -1;

		mesh->positions = calloc (icount, sizeof (vec3_t));
		mesh->normals = calloc (icount, sizeof (vec3_t));
		mesh->texcoords = calloc (icount, sizeof (vec2_t));
		mesh->tangents = fvtx.tan ? calloc (icount, sizeof (vec3_t)) : NULL;
		mesh->colors[0] = fvtx.clr ? calloc (icount, sizeof (color4_t)) : NULL;
		mesh->colors[1] = fvtx.clr1 ? calloc (icount, sizeof (color4_t)) : NULL;
		mesh->vertices = calloc (icount, sizeof (vertex_t));
		{
			uint nuv = 0;
			for (uint k = 0; k < 6; k++)
				if (fvtx.extra_uv[k])
					nuv++;
			if (nuv)
				for (uint k = 0; k < 6; k++)
					if (fvtx.extra_uv[k])
						mesh->extra_texcoords[k] = calloc (icount, sizeof (vec2_t));
		}
		if (!mesh->positions || !mesh->normals || !mesh->texcoords || !mesh->vertices)
		{
			free (mesh->positions);
			free (mesh->normals);
			free (mesh->texcoords);
			free (mesh->tangents);
			free (mesh->colors[0]);
			free (mesh->colors[1]);
			for (uint k = 0; k < 6; k++)
				free (mesh->extra_texcoords[k]);
			memset (mesh, 0, sizeof (*mesh));
			continue;
		}

		uint n = 0;
		for (uint32_t k = 0; k < icount; k++)
		{
			const uint8_t *ip = d + idata + (size_t)k * isz;
			const uint32_t vi = isz == 4 ? rb32 (ip) : rb16 (ip);
			if (vi >= fvtx.count)
				continue;

			float v[4];
			if (attr_read (fvtx.pos + (size_t)vi * fvtx.stride_pos,
					fvtx.avail_pos - (size_t)vi * fvtx.stride_pos, fvtx.fmt_pos, v))
			{
				mesh->positions[n].x = v[0];
				mesh->positions[n].y = v[1];
				mesh->positions[n].z = v[2];
			}
			if (fvtx.nrm
				&& attr_read (fvtx.nrm + (size_t)vi * fvtx.stride_nrm,
					fvtx.avail_nrm - (size_t)vi * fvtx.stride_nrm, fvtx.fmt_nrm, v))
			{
				mesh->normals[n].x = v[0];
				mesh->normals[n].y = v[1];
				mesh->normals[n].z = v[2];
			}
			if (fvtx.uv
				&& attr_read (fvtx.uv + (size_t)vi * fvtx.stride_uv,
					fvtx.avail_uv - (size_t)vi * fvtx.stride_uv, fvtx.fmt_uv, v))
			{
				mesh->texcoords[n].u = v[0];
				mesh->texcoords[n].v = v[1];
			}
			if (fvtx.tan
				&& attr_read (fvtx.tan + (size_t)vi * fvtx.stride_tan,
					fvtx.avail_tan - (size_t)vi * fvtx.stride_tan, fvtx.fmt_tan, v))
			{
				mesh->tangents[n].x = v[0];
				mesh->tangents[n].y = v[1];
				mesh->tangents[n].z = v[2];
			}
			if (fvtx.clr
				&& attr_read (fvtx.clr + (size_t)vi * fvtx.stride_clr,
					fvtx.avail_clr - (size_t)vi * fvtx.stride_clr, fvtx.fmt_clr, v))
			{
				mesh->colors[0][n].r = v[0];
				mesh->colors[0][n].g = v[1];
				mesh->colors[0][n].b = v[2];
				mesh->colors[0][n].a = v[3];
			}
			if (fvtx.clr1
				&& attr_read (fvtx.clr1 + (size_t)vi * fvtx.stride_clr1,
					fvtx.avail_clr1 - (size_t)vi * fvtx.stride_clr1, fvtx.fmt_clr1, v))
			{
				mesh->colors[1][n].r = v[0];
				mesh->colors[1][n].g = v[1];
				mesh->colors[1][n].b = v[2];
				mesh->colors[1][n].a = v[3];
			}
			for (uint e = 0; e < 6; e++)
			{
				if (fvtx.extra_uv[e]
					&& attr_read (fvtx.extra_uv[e] + (size_t)vi * fvtx.stride_extra_uv[e],
						fvtx.avail_extra_uv[e] - (size_t)vi * fvtx.stride_extra_uv[e],
						fvtx.fmt_extra_uv[e], v))
				{
					mesh->extra_texcoords[e][n].u = v[0];
					mesh->extra_texcoords[e][n].v = v[1];
				}
			}

			mesh->vertices[n].position_idx = (int)n;
			mesh->vertices[n].normal_idx = fvtx.nrm ? (int)n : -1;
			mesh->vertices[n].texcoord_idx = fvtx.uv ? (int)n : -1;
			mesh->vertices[n].tangent_idx = fvtx.tan ? (int)n : -1;
			mesh->vertices[n].color_idx[0] = fvtx.clr ? (int)n : -1;
			mesh->vertices[n].color_idx[1] = fvtx.clr1 ? (int)n : -1;
			for (uint e = 0; e < 6; e++)
				mesh->vertices[n].extra_texcoord_idx[e] = fvtx.extra_uv[e] ? (int)n : -1;
			n++;
		}
		if (!n)
		{
			free (mesh->positions);
			free (mesh->normals);
			free (mesh->texcoords);
			free (mesh->tangents);
			free (mesh->colors[0]);
			free (mesh->colors[1]);
			for (uint k = 0; k < 6; k++)
				free (mesh->extra_texcoords[k]);
			free (mesh->vertices);
			memset (mesh, 0, sizeof (*mesh));
			continue;
		}
		mesh->num_positions = mesh->num_normals = mesh->num_texcoords = n;
		mesh->num_vertices = n;
		if (fvtx.tan)
			mesh->num_tangents = n;
		if (fvtx.clr)
			mesh->num_colors[0] = n;
		if (fvtx.clr1)
			mesh->num_colors[1] = n;
		for (uint e = 0; e < 6; e++)
			if (fvtx.extra_uv[e])
				mesh->num_extra_texcoords[e] = n;
		out->num_meshes++;
	}

	// Skeleton (FSKL): the FMDL references it via a self-relative pointer at
	// FMDL+0x0C. Bones are 0x40-byte records: name, index, parent, smooth /
	// rigid / billboard indices, flags and bind TRS. A rotation stored as a
	// quaternion (bone flag bit 0x1000 clear) is converted to the
	// half-angle-Euler convention used by the exporter; everything else is
	// already XYZ Euler in radians.
	{
		const size_t sk = REL (d, m + 0x0C);
		if (sk + 8 <= size && !memcmp (d + sk, "FSKL", 4))
		{
			const uint16_t n_bones = rb16 (d + sk + 8);
			const size_t bone_arr = sk + 0x18 <= size ? REL (d, sk + 0x14) : 0;
			if (n_bones && n_bones < 4096 && bone_arr
				&& bone_arr + (size_t)n_bones * 0x40 <= size)
			{
				out->num_joints = n_bones;
				out->joints = calloc (n_bones, sizeof (joint_t));
				if (out->joints)
				{
					for (uint b = 0; b < n_bones; b++)
					{
						const size_t boff = bone_arr + (size_t)b * 0x40;
						joint_t *j = out->joints + b;
						j->parent_idx = -1;

						const char *bname = rel_string (d, size, boff);
						if (bname && *bname)
							snprintf (j->name, sizeof (j->name), "%s", bname);

						const int16_t parent = (int16_t)rb16 (d + boff + 6);
						if (parent >= 0 && (uint16_t)parent < n_bones)
							j->parent_idx = (int)parent;

						j->scale.x = read_be32f (d + boff + 0x14);
						j->scale.y = read_be32f (d + boff + 0x18);
						j->scale.z = read_be32f (d + boff + 0x1C);
						if (rb32 (d + boff + 0x10) & 0x1000)
						{
							j->rotate.x = read_be32f (d + boff + 0x20);
							j->rotate.y = read_be32f (d + boff + 0x24);
							j->rotate.z = read_be32f (d + boff + 0x28);
						}
						else
						{
							float rx, ry, rz;
							bfres_quat_to_euler (
								read_be32f (d + boff + 0x20), read_be32f (d + boff + 0x24),
								read_be32f (d + boff + 0x28), read_be32f (d + boff + 0x2C),
								&rx, &ry, &rz);
							j->rotate.x = rx;
							j->rotate.y = ry;
							j->rotate.z = rz;
						}
						j->translate.x = read_be32f (d + boff + 0x30);
						j->translate.y = read_be32f (d + boff + 0x34);
						j->translate.z = read_be32f (d + boff + 0x38);
					}
				}
			}
		}
	}

	// Skeletal animations (FSKA): index group 2 at d+0x28. Every FSKA object
	// in the group is turned into one animation clip; each animation targets
	// joints by name, so animations referencing a joint that isn't in the
	// (single) exported skeleton are skipped.
	{
		const size_t fska_grp = REL (d, 0x28);
		const uint32_t fska_entries = fska_grp + 8 <= size ? rb32 (d + fska_grp + 4) : 0;
		if (fska_entries && fska_entries <= 0x10000
			&& fska_grp + 8 + (size_t)(fska_entries + 1) * 16 <= size)
		{
			for (uint32_t fi = 1; fi <= fska_entries; fi++)
			{
				const size_t fe = fska_grp + 8 + (size_t)fi * 16;
				const size_t fs = REL (d, fe + 12);
				if (fs + 0x30 > size || memcmp (d + fs, "FSKA", 4))
					continue;
				const char *clip = rel_string (d, size, fe + 8);
				parse_fska_into_model (out, d, size, bfr_version, fs, clip);
			}
		}
	}

	if (!out->num_meshes)
	{
		FreeModel (out);
		return NULL;
	}
	return out;
}

//-----------------------------------------------------------------------------
// Switch BFRES ("FRES", little endian, version-major-gated header layout).
//
// Header field offsets (name-offset, model-array, material-array, per-
// FMDL/FSHP/FVTX/FMAT prologue width) are ported from KillzXGaming's
// BfresLibrary (MIT) -- ResFileParser.Load()/ModelParser.Read()/
// ShapeParser.Read()/VertexBufferParser.Load()/Mesh.Load(), mirroring the
// already-verified name-resolution code in wszst.c's
// extract_bfres_switch_manifest(). What that code did NOT solve -- the
// vertex/index *data* location -- is solved here:
//
// - ResFileParser.Load()'s exact field sequence (hand-counted byte-by-byte
//   against the C# source, not guessed) places the `BufferInfo` pointer at
//   absolute header offset 0x90 (144) for version-major<9/<10 files (every
//   real sample seen so far). Cross-checked against the already-verified
//   `numModel` field position (0xBC for v8): counting forward from 0x90
//   through ExternalFiles/padding/StringTable/StringPoolSize lands exactly
//   on 0xBC, confirming the byte count is right, not just plausible.
// - BufferInfo's own struct is `u32 unk, u32 Size, s64 BufferOffset, u8[16]
//   padding` -- verified on a real file (AirBubble.bfres, Super Mario
//   Odyssey): `unk` read back exactly 34, BfresLibrary's own hardcoded
//   default for that field, and the bytes at `BufferOffset` decode as a
//   clean u16 triangle index list (0,1,2, 3,0,2, 0,4,1, ...).
// - Index and vertex buffer data all live in ONE pool starting at
//   `BufferOffset`: the index buffer for each Mesh at
//   `BufferOffset + FaceBufferOffset` (a local s32 in the Mesh struct), and
//   each FVTX's buffers (one buffer per attribute is common -- FVTX
//   attributes are NOT necessarily interleaved on Switch, unlike Wii U)
//   starting at `BufferOffset + (the FVTX's own local s32 offset)`,
//   8-byte-aligned, one after another, size/stride given by separate
//   per-buffer arrays. Verified end to end on the same real file: index
//   buffer (1560 * 2 bytes = 3120, matching its separately-stored Size
//   field exactly) is immediately followed with zero padding by vertex
//   buffer 0 (275 vertices * stride 12 = 3300 bytes, again matching its
//   separately-stored Size field exactly).
// - Per-attribute Format (and Mesh's PrimitiveType/IndexFormat) are stored
//   as their raw enum value but the reader temporarily swaps to
//   BIG-endian just for that one field (see VertexAttrib.Load() setting
//   `loader.ByteOrder = ByteOrder.BigEndian` around the Format read) even
//   though the rest of the file is little-endian -- confirmed by matching
//   real attribute bytes (0x05,0x18 -> big-endian 0x0518) against
//   BfresLibrary's own `SwitchAttribFormat` enum (0x0518 =
//   Format_32_32_32_Single, exactly a 3-float position attribute) rather
//   than assuming a plain little-endian read (which would give a
//   nonexistent format code).
//-----------------------------------------------------------------------------

static uint16_t le16 (const uint8_t *p)
{
	return (uint16_t)p[1] << 8 | p[0];
}
static uint32_t le32 (const uint8_t *p)
{
	return (uint32_t)p[3] << 24 | (uint32_t)p[2] << 16 | (uint32_t)p[1] << 8 | p[0];
}
static int32_t les32 (const uint8_t *p)
{
	return (int32_t)le32 (p);
}
static uint64_t le64 (const uint8_t *p)
{
	return (uint64_t)le32 (p + 4) << 32 | le32 (p);
}
static int64_t les64 (const uint8_t *p)
{
	return (int64_t)le64 (p);
}

// A handful of enum values are stored byte-order-swapped relative to the
// rest of the (little-endian) file -- see the comment above. Only the low
// 16 bits are ever non-zero on any real sample seen, so this just swaps
// the first two bytes rather than fully byte-reversing a 32-bit read.
static inline uint32_t swz16 (const uint8_t *p)
{
	return (uint32_t)p[0] << 8 | p[1];
}

static int attr_read_switch (const uint8_t *p, size_t avail, uint32_t fmt, float out[4])
{
	out[0] = out[1] = out[2] = 0.0f;
	out[3] = 1.0f;
	switch (fmt)
	{
		case 0x0112: // 16_16 unorm
			if (avail < 4)
				return 0;
			out[0] = le16 (p) / 65535.0f;
			out[1] = le16 (p + 2) / 65535.0f;
			return 1;
		case 0x0212: // 16_16 snorm
			if (avail < 4)
				return 0;
			out[0] = (int16_t)le16 (p) / 32767.0f;
			out[1] = (int16_t)le16 (p + 2) / 32767.0f;
			return 1;
		case 0x010b: // 8_8_8_8 unorm
			if (avail < 4)
				return 0;
			for (int i = 0; i < 4; i++)
				out[i] = p[i] / 255.0f;
			return 1;
		case 0x020b: // 8_8_8_8 snorm
			if (avail < 4)
				return 0;
			for (int i = 0; i < 4; i++)
				out[i] = (int8_t)p[i] / 127.0f;
			return 1;
		case 0x020e: // 10_10_10_2 snorm (verified: real normal attribute)
		{
			if (avail < 4)
				return 0;
			const uint32_t v = le32 (p);
			for (int i = 0; i < 3; i++)
			{
				int c = (v >> (i * 10)) & 0x3FF;
				if (c & 0x200)
					c -= 0x400;
				out[i] = (float)c / 511.0f;
			}
			return 1;
		}
		case 0x050a: // 16 float (single half, e.g. some scalar attribs)
			if (avail < 2)
				return 0;
			out[0] = half_to_float (le16 (p));
			return 1;
		case 0x0512: // 16_16 float (verified: real uv attribute)
			if (avail < 4)
				return 0;
			out[0] = half_to_float (le16 (p));
			out[1] = half_to_float (le16 (p + 2));
			return 1;
		case 0x0515: // 16_16_16_16 float
			if (avail < 8)
				return 0;
			for (int i = 0; i < 4; i++)
				out[i] = half_to_float (le16 (p + i * 2));
			return 1;
		case 0x0516: // 32 float
		{
			if (avail < 4)
				return 0;
			union
			{
				uint32_t u;
				float f;
			} c;
			c.u = le32 (p);
			out[0] = c.f;
			return 1;
		}
		case 0x0517: // 32_32 float
		{
			if (avail < 8)
				return 0;
			for (int i = 0; i < 2; i++)
			{
				union
				{
					uint32_t u;
					float f;
				} c;
				c.u = le32 (p + i * 4);
				out[i] = c.f;
			}
			return 1;
		}
		case 0x0518: // 32_32_32 float (verified: real position attribute)
		{
			if (avail < 12)
				return 0;
			for (int i = 0; i < 3; i++)
			{
				union
				{
					uint32_t u;
					float f;
				} c;
				c.u = le32 (p + i * 4);
				out[i] = c.f;
			}
			return 1;
		}
		case 0x0519: // 32_32_32_32 float
		{
			if (avail < 16)
				return 0;
			for (int i = 0; i < 4; i++)
			{
				union
				{
					uint32_t u;
					float f;
				} c;
				c.u = le32 (p + i * 4);
				out[i] = c.f;
			}
			return 1;
		}
	}
	return 0;
}

// Bytes consumed by the version-gated prologue before the first LoadString()
// in FMDL/FSHP/FMAT/FVTX sections -- same convention documented in wszst.c's
// bfres_switch_hdr_extra(), duplicated here to keep this file's Switch path
// self-contained.
static inline uint bfres_switch_hdr_extra (uint vmajor)
{
	return vmajor >= 9 ? 4 : 12;
}

static const char *rel_string_switch (const uint8_t *d, size_t size, int64_t off)
{
	if (off < 2 || (size_t)off + 2 > size)
		return NULL;
	const uint len = le16 (d + off);
	if ((size_t)off + 2 + len > size)
		return NULL;
	return (const char *)(d + off + 2);
}

static inline float read_le32f (const uint8_t *p)
{
	union
	{
		uint32_t u;
		float f;
	} c;
	c.u = le32 (p);
	return c.f;
}

typedef struct
{
	const uint8_t *pos, *nrm, *uv, *clr, *bone, *wt;
	uint32_t fmt_pos, fmt_nrm, fmt_uv, fmt_clr, fmt_bone, fmt_wt;
	uint stride_pos, stride_nrm, stride_uv, stride_clr, stride_bone, stride_wt;
	size_t avail_pos, avail_nrm, avail_uv, avail_clr, avail_bone, avail_wt;
	// Extra UV channels (_u1.._u6) and secondary color (_c1).
	const uint8_t *extra_uv[6];
	uint32_t fmt_extra_uv[6];
	uint stride_extra_uv[6];
	size_t avail_extra_uv[6];
	const uint8_t *clr1;
	uint32_t fmt_clr1;
	uint stride_clr1;
	size_t avail_clr1;
	const uint8_t *tan;
	uint32_t fmt_tan;
	uint stride_tan;
	size_t avail_tan;
	uint count;
} fvtx_switch_t;

// Reads unsigned 8-bit components from a Switch vertex attribute.
// Returns count of components read (1..4).
static int attr_read_uint8_switch (const uint8_t *p, size_t avail, uint32_t fmt, uint8_t out[4])
{
	out[0] = out[1] = out[2] = out[3] = 0;
	switch (fmt)
	{
		case 0x000b: // 8_8_8_8 uint
		case 0x030b: // 8_8_8_8 uint (per-vertex bone indices; same layout)
			if (avail < 4)
				return 0;
			for (int i = 0; i < 4; i++)
				out[i] = p[i];
			return 4;
		case 0x000a: // 8_8 uint
			if (avail < 2)
				return 0;
			out[0] = p[0];
			out[1] = p[1];
			return 2;
		case 0x0009: // 8 uint (scalar)
			if (avail < 1)
				return 0;
			out[0] = p[0];
			return 1;
	}
	return 0;
}

// Reads one FVTX (Switch): attribute list + one buffer per attribute
// (commonly non-interleaved, unlike Wii U), located via the shared
// BufferInfo pool base plus this FVTX's own local buffer offset.
static int read_fvtx_switch (
	const uint8_t *d, size_t size, size_t fv, uint vmajor, int64_t pool_base, fvtx_switch_t *out)
{
	memset (out, 0, sizeof (*out));
	if (fv + 0x60 > size || memcmp (d + fv, "FVTX", 4))
		return 0;

	const uint vhdr = bfres_switch_hdr_extra (vmajor);
	const int64_t attr_arr = les64 (d + fv + 4 + vhdr);
	const int64_t counts_off = fv + 4 + vhdr + 0x40;
	if ((size_t)counts_off + 16 > size)
		return 0;

	const int32_t vb_local_off = les32 (d + counts_off);
	const uint n_attr = d[counts_off + 4];
	const uint n_buf = d[counts_off + 5];
	// VertexBufferSizeOffset then VertexStrideSizeOffset are the two
	// ReadOffset() calls right before an 8-byte padding field that ends
	// exactly at counts_off (VertexBufferParser.Load()) -- so counting
	// backward from counts_off: padding(8) at counts_off-8,
	// VertexStrideSizeOffset(8) at counts_off-16, VertexBufferSizeOffset(8)
	// at counts_off-24. Verified against a real file (AirBubble.bfres):
	// these land on 2432/2480 respectively, and the values they point to
	// (stride 12/4/4, size 3300/1100/1100) match the real vertex count
	// (275) and attribute formats exactly, zero slack.
	const int64_t vtx_bufsize_off = les64 (d + counts_off - 24);
	const int64_t vtx_stride_off2 = les64 (d + counts_off - 16);
	const uint32_t vertex_count = (size_t)counts_off + 12 <= size ? le32 (d + counts_off + 8) : 0;

	if (!n_attr || !n_buf || n_buf > 8 || !vertex_count)
		return 0;
	if (attr_arr <= 0 || (size_t)attr_arr + (size_t)n_attr * 16 > size)
		return 0;
	if (vtx_bufsize_off <= 0 || vtx_stride_off2 <= 0
		|| (size_t)vtx_bufsize_off + (size_t)n_buf * 16 > size
		|| (size_t)vtx_stride_off2 + (size_t)n_buf * 16 > size)
		return 0;

	// Walk the buffer pool sequentially, 8-byte aligned, same order the
	// buffers are declared in (verified: on a real file this lands with
	// zero gap directly after the preceding index buffer).
	uint64_t bufpos[8];
	uint64_t cur = (uint64_t)pool_base + (uint32_t)vb_local_off;
	for (uint i = 0; i < n_buf; i++)
	{
		cur = (cur + 7) & ~(uint64_t)7;
		bufpos[i] = cur;
		const uint32_t bsize = le32 (d + vtx_bufsize_off + (size_t)i * 16);
		cur += bsize;
	}

	out->count = vertex_count;
	for (uint i = 0; i < n_attr; i++)
	{
		const size_t a = (size_t)attr_arr + (size_t)i * 16;
		const char *name = rel_string_switch (d, size, les64 (d + a));
		if (!name)
			continue;
		const uint32_t fmt = swz16 (d + a + 8);
		const uint boff = le16 (d + a + 12);
		const uint bi = le16 (d + a + 14);
		if (bi >= n_buf)
			continue;

		const uint stride = le32 (d + vtx_stride_off2 + (size_t)bi * 16);
		if (!stride || bufpos[bi] >= size)
			continue;
		const size_t data = (size_t)bufpos[bi];
		if (boff >= stride || data + boff >= size)
			continue;

		const uint8_t *p = d + data + boff;
		const size_t avail = size - (data + boff);
		if (!strncmp (name, "_p", 2) && !out->pos)
		{
			out->pos = p;
			out->fmt_pos = fmt;
			out->stride_pos = stride;
			out->avail_pos = avail;
		}
		else if (!strncmp (name, "_n", 2) && !out->nrm)
		{
			out->nrm = p;
			out->fmt_nrm = fmt;
			out->stride_nrm = stride;
			out->avail_nrm = avail;
		}
		else if (!strncmp (name, "_t", 2) && !out->tan)
		{
			out->tan = p;
			out->fmt_tan = fmt;
			out->stride_tan = stride;
			out->avail_tan = avail;
		}
		else if (name[0] == '_' && name[1] == 'u' && name[2] >= '0' && name[2] <= '6')
		{
			const uint ch = name[2] - '0';
			if (ch == 0 && !out->uv)
			{
				out->uv = p;
				out->fmt_uv = fmt;
				out->stride_uv = stride;
				out->avail_uv = avail;
			}
			else if (ch > 0 && !out->extra_uv[ch - 1])
			{
				out->extra_uv[ch - 1] = p;
				out->fmt_extra_uv[ch - 1] = fmt;
				out->stride_extra_uv[ch - 1] = stride;
				out->avail_extra_uv[ch - 1] = avail;
			}
		}
		else if (name[0] == '_' && name[1] == 'c' && name[2] >= '0' && name[2] <= '1')
		{
			const uint ch = name[2] - '0';
			if (ch == 0 && !out->clr)
			{
				out->clr = p;
				out->fmt_clr = fmt;
				out->stride_clr = stride;
				out->avail_clr = avail;
			}
			else if (ch == 1 && !out->clr1)
			{
				out->clr1 = p;
				out->fmt_clr1 = fmt;
				out->stride_clr1 = stride;
				out->avail_clr1 = avail;
			}
		}
		else if (!strncmp (name, "_b", 2) && !out->bone)
		{
			out->bone = p;
			out->fmt_bone = fmt;
			out->stride_bone = stride;
			out->avail_bone = avail;
		}
		else if (!strncmp (name, "_i", 2) && !out->bone)
		{
			// v10 files store the 4 bone indices per vertex in _i0
			// (Format_8_8_8_8_UInt) instead of _b0; same handling.
			out->bone = p;
			out->fmt_bone = fmt;
			out->stride_bone = stride;
			out->avail_bone = avail;
		}
		else if (!strncmp (name, "_w", 2) && !out->wt)
		{
			out->wt = p;
			out->fmt_wt = fmt;
			out->stride_wt = stride;
			out->avail_wt = avail;
		}
	}
	return out->pos != NULL;
}

model_t *ParseBFRESSwitch (const uint8_t *data, size_t size)
{
	if (!data || size < 0x100 || memcmp (data, "FRES", 4))
		return NULL;
	if (le16 (data + 0x0C) != 0xFEFF)
		return NULL; // Switch BOM position/endianness

	const uint32_t version = le32 (data + 8);
	const uint vmajor = (version >> 16) & 0xFFFF;
	const uint8_t *d = data;

	const int64_t fmdl_arr = les64 (d + 0x28);
	if (fmdl_arr <= 0 || (size_t)fmdl_arr + 0x60 > size || memcmp (d + fmdl_arr, "FMDL", 4))
		return NULL;

	// BufferInfo pointer: version-gated. v8 keeps it at +0x90 (matches the
	// v8 files CreateSwitchBFRES writes); v9+ moves it to +0xB0 -- the
	// 32-byte reserved block BfresLibrary documents for version>=9 sits in
	// front of it. Verified on real data: Male.bfres (v9) has 0 at +0x90
	// and a valid {unk=36, size=40960, pool=122880} triple at +0xB0, with
	// header+0xA8 == pool+size as an independent cross-check; both shapes'
	// vertex bboxes are human-scale and both index buffers max out at
	// vcount-1 with full vertex coverage. Either slot is accepted when its
	// struct validates (bounds-checked triple), so unknown versions fall
	// back gracefully instead of failing outright.
	int64_t bufinfo = -1, pool_base = -1;
	{
		const int64_t cands[2] = { vmajor >= 9 ? 0xB0 : 0x90, vmajor >= 9 ? 0x90 : 0xB0 };
		for (int ci = 0; ci < 2 && pool_base <= 0; ci++)
		{
			const size_t pf = (size_t)cands[ci];
			if (pf + 8 > size)
				continue;
			const int64_t bi = les64 (d + pf);
			if (bi <= 0 || (size_t)bi + 16 > size)
				continue;
			const uint32_t pool_size = le32 (d + bi + 4);
			const int64_t pb = les64 (d + bi + 8);
			if (pb <= 0 || pool_size == 0 || (uint64_t)pb + pool_size > size)
				continue;
			bufinfo = bi;
			pool_base = pb;
		}
	}
	if (pool_base <= 0 || (size_t)pool_base >= size)
		return NULL;
	(void)bufinfo;

	const uint fhdr = bfres_switch_hdr_extra (vmajor);
	// name(8) + path(8) + skeleton(8) + vertex-buffer array(8) precede the
	// shapes-array field; materials/userdata/etc that follow it aren't
	// needed here since shapes are located by scanning for "FSHP" magics
	// below, not via a numShape count field.
	const int64_t shapes_val_field = fmdl_arr + 4 + fhdr + 32;

	if ((size_t)shapes_val_field + 8 > size)
		return NULL;
	const int64_t shapes_val = les64 (d + shapes_val_field);
	if (shapes_val <= 0 || (size_t)shapes_val + 0x60 > size || memcmp (d + shapes_val, "FSHP", 4))
		return NULL;

	// Count shapes by scanning for "FSHP" magics from the first one (same
	// approach the already-verified name-resolution manifest code uses --
	// FSHP entries aren't fixed-stride, so there's no clean array stride to
	// step through instead).
	uint n_fshp = 0;
	{
		const uint8_t *s = d + shapes_val;
		while (s && (size_t)(s - d) < size)
		{
			n_fshp++;
			s = memmem (s + 4, size - (s + 4 - d), "FSHP", 4);
		}
	}
	if (!n_fshp)
		return NULL;

	model_t *out = calloc (1, sizeof (model_t));
	if (!out)
		return NULL;
	out->meshes = calloc (n_fshp, sizeof (mesh_t));
	if (!out->meshes)
	{
		free (out);
		return NULL;
	}

	// Parse FSKL skeleton -- FSKL pointer at FMDL+0x18 (v>=9) or
	// FMDL+0x20 (v<9). Field layout per BfresLibrary's Skeleton.cs
	// (BoneDict@+8, BoneArray@+16, MatrixToBoneList@+24,
	// InverseModelMatrices@+32, userPtr@+40, mirror@+48,
	// numBone/smooth/rigid u16s@+56), verified byte-for-byte on real
	// v9 (Male.bfres: 27-bone TopL/EffectL/all_root/... hierarchy)
	// and v10 (Tomodachi PenguinBaby: 8-bone Root/Skl_Root/Body/...
	// hierarchy with bilateral symmetry) files. Bones are 0x60 stride
	// on v8/v9 but 0x58 on v10+ (Bone.cs seeks 8, not 16, after the
	// two UserData offsets), with parent/TRS shifted accordingly;
	// rotation is euler XYZ (3 floats) or quaternion (4 floats) per
	// the FSKL flags mode bits.
	{
		const int64_t fskl_off = les64 (d + fmdl_arr + 4 + fhdr + 16);
		if (fskl_off > 0 && (size_t)fskl_off + 0x40 <= size && !memcmp (d + fskl_off, "FSKL", 4))
		{
			const uint skdr = bfres_switch_hdr_extra (vmajor);
			const int64_t sk_base = fskl_off + 4 + skdr;
			int64_t bone_arr = 0, matrix_off = 0;
			uint16_t n_bones = 0, n_smooth = 0;
			size_t bone_stride = 0x60, parent_off = 0x2A, trs_off = 0x38;
			int rot_quat = 0;
			if (vmajor >= 9)
			{
				bone_arr = les64 (d + fskl_off + 0x10);
				matrix_off = les64 (d + fskl_off + 0x20);
				n_bones = le16 (d + fskl_off + 0x38);
				n_smooth = le16 (d + fskl_off + 0x3A);
				rot_quat = ((le32 (d + fskl_off + 4) >> 12) & 7) == 0;
				if (vmajor >= 10)
				{
					bone_stride = 0x58;
					parent_off = 0x22;
					trs_off = 0x30;
				}
			}
			else
			{
				// For v<9:  sk_base+0x10=bone array, sk_base+0x28=matrix, sk_base+0x4C=num bones
				bone_arr = (size_t)sk_base + 0x18 <= size ? les64 (d + sk_base + 0x10) : 0;
				matrix_off
					= (size_t)sk_base + 0x28 <= size ? les64 (d + sk_base + 0x20) : 0;
				n_bones = (size_t)sk_base + 0x4E <= size ? le16 (d + sk_base + 0x4C) : 0;
				n_smooth = n_bones;
			}
			// For v>=9: sk_base+0x10=bone array, sk_base+0x20=matrix, sk_base+0x38=num bones
			// (superseded by the BfresLibrary layout above; kept as comment
			// for provenance -- the old guess read every field 8 bytes late
			// and never matched retail).

			if (bone_arr > 0 && n_bones > 0 && n_bones < 4096)
			{
				if ((size_t)bone_arr + n_bones * bone_stride > size)
				{ /* skip skeleton */
				}
				else
				{
					out->num_joints = n_bones;
					out->joints = calloc (n_bones, sizeof (joint_t));
					if (out->joints)
					{
						for (uint b = 0; b < n_bones; b++)
						{
							const size_t boff = (size_t)bone_arr + b * bone_stride;
							joint_t *j = out->joints + b;
							j->parent_idx = -1;

							const char *bname = rel_string_switch (d, size, les64 (d + boff));
							if (bname && *bname)
								snprintf (j->name, sizeof (j->name), "%s", bname);

							// Parent index (s16)
							if (boff + parent_off + 2 <= size)
								j->parent_idx = (int)(int16_t)le16 (d + boff + parent_off);

							// TRS: scale 3f at base, rotation (euler 3f or
							// quaternion 4f) 12 bytes later, position 16
							// after that. Stored as euler radians, matching
							// the Wii U path (quaternions converted).
							if (boff + trs_off + 40 <= size)
							{
								j->scale.x = read_le32f (d + boff + trs_off);
								j->scale.y = read_le32f (d + boff + trs_off + 4);
								j->scale.z = read_le32f (d + boff + trs_off + 8);
								if (rot_quat)
								{
									float qx = read_le32f (d + boff + trs_off + 12);
									float qy = read_le32f (d + boff + trs_off + 16);
									float qz = read_le32f (d + boff + trs_off + 20);
									float qw = read_le32f (d + boff + trs_off + 24);
									bfres_quat_to_euler (qx, qy, qz, qw, &j->rotate.x,
										&j->rotate.y, &j->rotate.z);
								}
								else
								{
									j->rotate.x = read_le32f (d + boff + trs_off + 12);
									j->rotate.y = read_le32f (d + boff + trs_off + 16);
									j->rotate.z = read_le32f (d + boff + trs_off + 20);
								}
								j->translate.x = read_le32f (d + boff + trs_off + 28);
								j->translate.y = read_le32f (d + boff + trs_off + 32);
								j->translate.z = read_le32f (d + boff + trs_off + 36);
							}

							// Inverse bind matrix: the array holds NumSmooth
							// entries; each bone names its own via its
							// smooth-matrix index (s16 right after parent).
							if (matrix_off > 0 && n_smooth > 0)
							{
								const size_t sidx_off = boff + parent_off + 2;
								if (sidx_off + 2 <= size)
								{
									int sidx = (int)(int16_t)le16 (d + sidx_off);
									if (sidx >= 0)
									{
										const size_t moff
											= (size_t)matrix_off + (size_t)sidx * 48;
										if (moff + 48 <= size)
										{
											for (int k = 0; k < 12; k++)
												j->inverse_bind[k]
													= read_le32f (d + moff + k * 4);
											j->has_inverse_bind = 1;
										}
									}
								}
							}
						}
					}
				}
			}
		}
	}

	// Parse FMAT materials if present so DAE materials and texture bindings resolve
	const int64_t mat_val_field = shapes_val_field + 16;
	const int64_t mat_val = (size_t)mat_val_field + 8 <= size ? les64 (d + mat_val_field) : -1;
	uint n_fmat = 0;
	if (mat_val > 0 && (size_t)mat_val + 0x20 <= size && !memcmp (d + mat_val, "FMAT", 4))
	{
		const uint8_t *m = d + mat_val;
		while (m && (size_t)(m - d) < size)
		{
			n_fmat++;
			m = memmem (m + 4, size - (m + 4 - d), "FMAT", 4);
		}
	}

	if (n_fmat)
	{
		out->materials = calloc (n_fmat, sizeof (material_t));
		if (out->materials)
		{
			int64_t mp = mat_val;
			for (uint mi = 0; mi < n_fmat && mp > 0 && (size_t)mp + 0x20 <= size; mi++)
			{
				if (memcmp (d + mp, "FMAT", 4))
					break;
				const uint mhdr = bfres_switch_hdr_extra (vmajor);
				const int64_t mp_base = mp + 4 + mhdr;
				const char *matname = rel_string_switch (d, size, les64 (d + mp_base));
				material_t *mat = out->materials + out->num_materials++;
				snprintf (mat->name, sizeof (mat->name), "%s",
					matname && *matname ? matname : "material");

				// Read all textures from this material's texture name array.
				// Per Wexos's Wiki: texture name array at mp+0x30 (v>=9) or
				// mp+0x38 (v<9); num textures at mp+0x9D (v>=9) or mp+0xAD (v<9).
				const int64_t tex_name_arr = vmajor >= 9
					? ((size_t)mp + 0x38 <= size ? les64 (d + mp + 0x30) : 0)
					: ((size_t)mp + 0x40 <= size ? les64 (d + mp + 0x38) : 0);
				const uint8_t n_tex = vmajor >= 9 ? ((size_t)mp + 0x9D < size ? d[mp + 0x9D] : 0)
												  : ((size_t)mp + 0xAD < size ? d[mp + 0xAD] : 0);
				if (tex_name_arr > 0 && n_tex > 0)
				{
					for (uint t = 0; t < n_tex && t < 8; t++)
					{
						const size_t off = (size_t)tex_name_arr + t * 8;
						if (off + 8 > size)
							break;
						const char *tname = rel_string_switch (d, size, les64 (d + off));
						if (tname && *tname)
						{
							snprintf (mat->textures[t], sizeof (mat->textures[t]), "%s", tname);
							mat->texture_coord[t] = (int)t;
							mat->num_textures = (int)(t + 1);
						}
					}
				}

				const uint8_t *next_m
					= mi + 1 < n_fmat ? memmem (d + mp + 4, size - (mp + 4), "FMAT", 4) : NULL;
				mp = next_m ? next_m - d : -1;
			}
		}
	}

	int64_t sh = shapes_val;

	// Dynamic node_influence accumulator -- one entry per unique bone-weight
	// combination across all shapes. position_node[] per mesh indexes into this.
	node_influence_t *node_inf = NULL;
	size_t n_node_inf = 0, cap_node_inf = 0;

	for (uint si = 0; si < n_fshp && sh > 0 && (size_t)sh + 0x60 <= size; si++)
	{
		if (memcmp (d + sh, "FSHP", 4))
			break;
		const uint shdr = bfres_switch_hdr_extra (vmajor);
		const int64_t sname_off = sh + 4 + shdr;
		const char *sname = rel_string_switch (d, size, les64 (d + sname_off));
		const int64_t fvtx = les64 (d + sname_off + 8);
		const int64_t mesh_arr_off_field = sname_off + 16;
		const int64_t mesh_arr = les64 (d + mesh_arr_off_field);
		// Per Wexos's Wiki: FMAT index at FSHP+0x52 (v>=9) / 0x5E (v<9).
		// Num LOD meshes at FSHP+0x5B (v>=9) / 0x67 (v<9).
		const uint8_t num_mesh = vmajor >= 9
			? ((size_t)sname_off + 0x54 <= size ? d[sname_off + 0x53] : 0)
			: ((size_t)sname_off + 0x58 <= size ? d[sname_off + 0x57] : 0);
		const uint16_t fmat_idx = vmajor >= 9
			? ((size_t)sname_off + 0x4C <= size ? le16 (d + sname_off + 0x4A) : 0)
			: ((size_t)sname_off + 0x50 <= size ? le16 (d + sname_off + 0x4E) : 0);

		// Per Wexos's Wiki: skin bone index array at FSHP+0x20 (v>=9)
		// or FSHP+0x28 (v<9) → both map to sname_off+0x18.
		// Count: FSHP+0x58 (v>=9) or FSHP+0x60 (v<9).
		const int64_t skin_bone_arr
			= (size_t)sname_off + 0x20 <= size ? les64 (d + sname_off + 0x18) : 0;
		const uint16_t n_skin_bones = vmajor >= 9
			? ((size_t)sname_off + 0x58 <= size ? le16 (d + sname_off + 0x50) : 0)
			: ((size_t)sname_off + 0x68 <= size ? le16 (d + sname_off + 0x60) : 0);

		do
		{
			if (fvtx <= 0 || !num_mesh || mesh_arr <= 0)
				break;
			fvtx_switch_t fv;
			if (!read_fvtx_switch (d, size, (size_t)fvtx, vmajor, pool_base, &fv))
				break;

			// First mesh only (LOD 0) -- same "no multi-LOD concept in a
			// plain DAE" scope ParseBFRES() already uses for Wii U.
			const int64_t mesh = mesh_arr;
			if ((size_t)mesh + 56 > size)
				break;
			const uint32_t face_off = le32 (d + mesh + 32);
			const uint32_t prim_raw = le32 (d + mesh + 36);
			const uint32_t ifmt_raw = le32 (d + mesh + 40);
			const uint32_t idx_count = le32 (d + mesh + 44);
			if (prim_raw != 3 || !idx_count || idx_count > 0x1000000)
				break; // triangles only
			const uint isz = ifmt_raw == 2 ? 4 : 2;

			const uint64_t idata = (uint64_t)pool_base + face_off;
			if (idata + (uint64_t)idx_count * isz > size)
				break;

			mesh_t *ms = out->meshes + out->num_meshes;
			snprintf (ms->name, sizeof (ms->name), "%s", sname && *sname ? sname : "shape");
			ms->material_idx = fmat_idx < out->num_materials ? (int)fmat_idx : -1;

			const int has_skin = fv.bone && fv.wt && n_skin_bones > 0 && skin_bone_arr > 0;

			ms->positions = calloc (idx_count, sizeof (vec3_t));
			ms->normals = calloc (idx_count, sizeof (vec3_t));
			ms->texcoords = calloc (idx_count, sizeof (vec2_t));
			ms->tangents = fv.tan ? calloc (idx_count, sizeof (vec3_t)) : NULL;
			ms->vertices = calloc (idx_count, sizeof (vertex_t));
			if (fv.clr)
			{
				ms->colors[0] = calloc (idx_count, sizeof (color4_t));
				ms->num_colors[0] = idx_count;
			}
			if (fv.clr1)
			{
				ms->colors[1] = calloc (idx_count, sizeof (color4_t));
				ms->num_colors[1] = idx_count;
			}
			{
				uint nuv = 0;
				for (uint kk = 0; kk < 6; kk++)
					if (fv.extra_uv[kk])
						nuv++;
				if (nuv)
					for (uint kk = 0; kk < 6; kk++)
						if (fv.extra_uv[kk])
							ms->extra_texcoords[kk] = calloc (idx_count, sizeof (vec2_t));
			}
			if (has_skin)
				ms->position_node = calloc (idx_count, sizeof (int));
			if (!ms->positions || !ms->normals || !ms->texcoords || !ms->vertices)
			{
				free (ms->positions);
				free (ms->normals);
				free (ms->texcoords);
				free (ms->tangents);
				free (ms->colors[0]);
				free (ms->colors[1]);
				for (uint kk = 0; kk < 6; kk++)
					free (ms->extra_texcoords[kk]);
				free (ms->vertices);
				free (ms->position_node);
				memset (ms, 0, sizeof (*ms));
				break;
			}

			uint n = 0;
			for (uint32_t k = 0; k < idx_count; k++)
			{
				const uint8_t *ip = d + idata + (size_t)k * isz;
				const uint32_t vi = isz == 4 ? le32 (ip) : le16 (ip);
				if (vi >= fv.count)
					continue;

				float v[4];
				if (attr_read_switch (fv.pos + (size_t)vi * fv.stride_pos,
						fv.avail_pos - (size_t)vi * fv.stride_pos, fv.fmt_pos, v))
				{
					ms->positions[n].x = v[0];
					ms->positions[n].y = v[1];
					ms->positions[n].z = v[2];
				}
				if (fv.nrm
					&& attr_read_switch (fv.nrm + (size_t)vi * fv.stride_nrm,
						fv.avail_nrm - (size_t)vi * fv.stride_nrm, fv.fmt_nrm, v))
				{
					ms->normals[n].x = v[0];
					ms->normals[n].y = v[1];
					ms->normals[n].z = v[2];
				}
				if (fv.uv
					&& attr_read_switch (fv.uv + (size_t)vi * fv.stride_uv,
						fv.avail_uv - (size_t)vi * fv.stride_uv, fv.fmt_uv, v))
				{
					ms->texcoords[n].u = v[0];
					ms->texcoords[n].v = v[1];
				}
				if (fv.tan && ms->tangents
					&& attr_read_switch (fv.tan + (size_t)vi * fv.stride_tan,
						fv.avail_tan - (size_t)vi * fv.stride_tan, fv.fmt_tan, v))
				{
					ms->tangents[n].x = v[0];
					ms->tangents[n].y = v[1];
					ms->tangents[n].z = v[2];
				}

				if (fv.clr && ms->colors[0]
					&& attr_read_switch (fv.clr + (size_t)vi * fv.stride_clr,
						fv.avail_clr - (size_t)vi * fv.stride_clr, fv.fmt_clr, v))
				{
					ms->colors[0][n].r = v[0];
					ms->colors[0][n].g = v[1];
					ms->colors[0][n].b = v[2];
					ms->colors[0][n].a = v[3];
				}
				if (fv.clr1 && ms->colors[1]
					&& attr_read_switch (fv.clr1 + (size_t)vi * fv.stride_clr1,
						fv.avail_clr1 - (size_t)vi * fv.stride_clr1, fv.fmt_clr1, v))
				{
					ms->colors[1][n].r = v[0];
					ms->colors[1][n].g = v[1];
					ms->colors[1][n].b = v[2];
					ms->colors[1][n].a = v[3];
				}
				for (uint e = 0; e < 6; e++)
				{
					if (fv.extra_uv[e] && ms->extra_texcoords[e]
						&& attr_read_switch (fv.extra_uv[e] + (size_t)vi * fv.stride_extra_uv[e],
							fv.avail_extra_uv[e] - (size_t)vi * fv.stride_extra_uv[e],
							fv.fmt_extra_uv[e], v))
					{
						ms->extra_texcoords[e][n].u = v[0];
						ms->extra_texcoords[e][n].v = v[1];
					}
				}

				ms->vertices[n].position_idx = (int)n;
				ms->vertices[n].normal_idx = fv.nrm ? (int)n : -1;
				ms->vertices[n].texcoord_idx = fv.uv ? (int)n : -1;
				ms->vertices[n].tangent_idx = fv.tan ? (int)n : -1;
				ms->vertices[n].color_idx[0] = fv.clr ? (int)n : -1;
				ms->vertices[n].color_idx[1] = fv.clr1 ? (int)n : -1;
				for (uint e = 0; e < 6; e++)
					ms->vertices[n].extra_texcoord_idx[e] = fv.extra_uv[e] ? (int)n : -1;

				// Skin bone data: read per-vertex bone indices + weights,
				// remap through skin_bone_idx table, accumulate unique
				// weight combinations as node_influence entries.
				if (has_skin && ms->position_node)
				{
					uint8_t bi[4] = { 0, 0, 0, 0 };
					float bw[4] = { 0, 0, 0, 0 };
					attr_read_uint8_switch (fv.bone + (size_t)vi * fv.stride_bone,
						fv.avail_bone - (size_t)vi * fv.stride_bone, fv.fmt_bone, bi);
					{
						float wb[4];
						if (attr_read_switch (fv.wt + (size_t)vi * fv.stride_wt,
								fv.avail_wt - (size_t)vi * fv.stride_wt, fv.fmt_wt, wb))
						{
							bw[0] = wb[0];
							bw[1] = wb[1];
							bw[2] = wb[2];
							bw[3] = wb[3];
						}
					}

					// Remap local bone indices through skin_bone_idx table
					// and normalize weights.
					influence_t weights[4];
					uint nw = 0;
					float wsum = 0;
					for (int b = 0; b < 4; b++)
					{
						if (bw[b] <= 0.0f)
							continue;
						if (bi[b] >= n_skin_bones)
							continue;
						const size_t idx_off = (size_t)skin_bone_arr + bi[b] * 2;
						if (idx_off + 2 > size)
							continue;
						const uint16_t fskl_bone = le16 (d + idx_off);
						if (fskl_bone >= out->num_joints)
							continue;
						weights[nw].bone_idx = (int)fskl_bone;
						weights[nw].weight = bw[b];
						wsum += bw[b];
						nw++;
					}
					// Normalize weights to sum to 1
					if (nw > 0 && wsum > 0.0f && wsum != 1.0f)
						for (uint i = 0; i < nw; i++)
							weights[i].weight /= wsum;

					// Find or create matching node_influence
					int ni_idx = -1;
					if (nw > 0)
					{
						// Linear scan for matching existing entry
						for (size_t ii = 0; ii < n_node_inf; ii++)
						{
							node_influence_t *ex = &node_inf[ii];
							if (ex->num_weights != nw)
								continue;
							int match = 1;
							for (uint w = 0; w < nw; w++)
							{
								if (ex->weights[w].bone_idx != weights[w].bone_idx
									|| ex->weights[w].weight != weights[w].weight)
								{
									match = 0;
									break;
								}
							}
							if (match)
							{
								ni_idx = (int)ii;
								break;
							}
						}
						// Create new entry if not found
						if (ni_idx < 0)
						{
							if (n_node_inf == cap_node_inf)
							{
								cap_node_inf = cap_node_inf ? cap_node_inf * 2 : 256;
								node_inf = realloc (node_inf, cap_node_inf * sizeof (*node_inf));
							}
							influence_t *wl = calloc (nw, sizeof (*wl));
							if (wl)
							{
								memcpy (wl, weights, nw * sizeof (*wl));
								node_inf[n_node_inf].weights = wl;
								node_inf[n_node_inf].num_weights = nw;
								ni_idx = (int)n_node_inf++;
							}
						}
					}
					else if (out->num_joints > 0)
					{
						// No usable influence (all weights zero, e.g. the
						// format's 0xFF-unbound marker): bind rigidly to
						// joint 0 so the mesh still exports skinned instead
						// of dropping every other vertex's skin data. This
						// matches the GLB exporter's own default for
						// unbound vertices.
						influence_t one;
						one.bone_idx = 0;
						one.weight = 1.0f;
						int found = -1;
						for (size_t ii = 0; ii < n_node_inf; ii++)
							if (node_inf[ii].num_weights == 1
								&& node_inf[ii].weights[0].bone_idx == 0
								&& node_inf[ii].weights[0].weight == 1.0f)
							{
								found = (int)ii;
								break;
							}
						if (found < 0)
						{
							if (n_node_inf == cap_node_inf)
							{
								cap_node_inf = cap_node_inf ? cap_node_inf * 2 : 256;
								node_inf = realloc (node_inf, cap_node_inf * sizeof (*node_inf));
							}
							influence_t *wl = calloc (1, sizeof (*wl));
							if (wl)
							{
								wl[0] = one;
								node_inf[n_node_inf].weights = wl;
								node_inf[n_node_inf].num_weights = 1;
								found = (int)n_node_inf++;
							}
						}
						ni_idx = found;
					}
					ms->position_node[n] = ni_idx;
				}

				n++;
			}
			if (n)
			{
				ms->num_positions = ms->num_normals = ms->num_texcoords = n;
				ms->num_vertices = n;
				if (fv.tan)
					ms->num_tangents = n;
				if (fv.clr1)
					ms->num_colors[1] = n;
				for (uint e = 0; e < 6; e++)
					if (fv.extra_uv[e])
						ms->num_extra_texcoords[e] = n;
				out->num_meshes++;
			}
			else
			{
				free (ms->positions);
				free (ms->normals);
				free (ms->texcoords);
				free (ms->tangents);
				free (ms->vertices);
				free (ms->colors[0]);
				free (ms->colors[1]);
				for (uint kk = 0; kk < 6; kk++)
					free (ms->extra_texcoords[kk]);
				free (ms->position_node);
				memset (ms, 0, sizeof (*ms));
			}
		} while (0);

		const uint8_t *next
			= si + 1 < n_fshp ? memmem (d + sh + 4, size - (sh + 4), "FSHP", 4) : NULL;
		sh = next ? next - d : -1;
	}

	if (!out->num_meshes)
	{
		for (size_t i = 0; i < n_node_inf; i++)
			free (node_inf[i].weights);
		free (node_inf);
		FreeModel (out);
		return NULL;
	}

	// Transfer accumulated node_influences to model
	if (n_node_inf > 0 && node_inf)
	{
		out->node_influences = node_inf;
		out->num_node_influences = n_node_inf;
	}
	else
		free (node_inf);

	return out;
}

int ParseBFRESArchive (const uint8_t *data, size_t size, bfres_archive_t *out)
{
	if (!data || !out || size < 0x70 || memcmp (data, "FRES", 4))
		return 0;
	if (rb16 (data + 8) != 0xFEFF || data[4] != 3)
		return 0;

	memset (out, 0, sizeof (*out));

	const char *aname = rel_string (data, size, 0x14);
	snprintf (out->name, sizeof (out->name), "%s", aname && *aname ? aname : "archive");

	for (uint8_t slot = 0; slot < 12; slot++)
	{
		const uint32_t dict_off = rb32 (data + 0x20 + 4 * (size_t)slot);
		const uint16_t n_meta = rb16 (data + 0x50 + 2 * (size_t)slot);
		if (!dict_off || !n_meta)
			continue;

		const size_t dict = REL (data, 0x20 + 4 * (size_t)slot);
		if (dict + 8 > size)
			continue;
		const uint32_t n_obj = rb32 (data + dict + 4);
		if (!n_obj || n_obj > 0x10000 || dict + 8 + (size_t)(n_obj + 1) * 16 > size)
			continue;

		bfres_slot_census_t *s = out->slots + out->n_slots++;
		s->slot = slot;
		s->count_meta = n_meta;
		s->count_dict = (uint16_t)n_obj;
		out->n_objects += n_obj;

		// First object (dict node 1; node 0 is the -1 sentinel root).
		const size_t nd = dict + 8 + 16;
		if (nd + 12 <= size)
		{
			const size_t obj = REL (data, nd + 12);
			if (obj + 4 <= size)
				memcpy (s->magic, data + obj, 4);
		}
	}

	return 1;
}

//-----------------------------------------------------------------------------
// Wii U BFRES animation entry bodies.
//
// Field layouts per the NintendoWare G3D SDK resource headers
// (nw/g3d/res/g3d_ResSkeletalAnim.h, g3d_ResShaderParamAnim.h,
// g3d_ResTexPatternAnim.h, g3d_ResVisibilityAnim.h, g3d_ResShapeAnim.h,
// g3d_ResSceneAnim.h): every entry opens with its 4-byte block magic and
// BinString name/path, then class-specific counts; curves everywhere share
// the 36/32-byte ResAnimCurve record bfres_curve_read() already decodes.
// Verified entry-by-entry against real data: effectDemoCar.bfres (SDK,
// FSKA "Car": 440 frames, 11 bones, 7/7 curves) and Yoshi's Woolly World
// retail (BS02.bfres: 10 FSHU incl. "BS02_0910" with 2/2 U8-frame cubic
// curves via 2 ParamInfos, 34+1 FVIS incl. 92 named targets + base bit
// array; EN075.bfres: 11 constant-only FTXP, 5 empty FSCN stubs;
// ENV303.bfres: 2 FSHA incl. 32/32 validated curves).
//-----------------------------------------------------------------------------

static int bfres_anim_count_curves (const uint8_t *d, size_t size,
	size_t arr, uint32_t n, int new_layout, uint32_t *ok)
{
	uint32_t good = 0;
	const size_t rec = new_layout ? 0x24 : 0x20;
	for (uint32_t k = 0; k < n; k++)
	{
		if (arr + (size_t)(k + 1) * rec > size)
			break;
		bfres_curve_t c;
		if (bfres_curve_read (d, size, arr + (size_t)k * rec, new_layout, &c))
		{
			good++;
			bfres_curve_free (&c);
		}
	}
	if (ok)
		*ok = good;
	return 1;
}

int ParseBFRESAnims (const uint8_t *data, size_t size, bfres_anim_entry_t **out_entries)
{
	if (out_entries)
		*out_entries = NULL;
	if (!data || !out_entries || size < 0x70 || memcmp (data, "FRES", 4))
		return 0;
	if (rb16 (data + 8) != 0xFEFF || data[4] != 3)
		return 0;
	const int new_layout = rb32 (data + 4) >= 0x03040000;
	const uint8_t *d = data;

	size_t cap = 64, n = 0;
	bfres_anim_entry_t *list = calloc (cap, sizeof (*list));
	if (!list)
		return 0;

	for (uint8_t slot = 2; slot <= 10; slot++)
	{
		const uint32_t dict_off = rb32 (d + 0x20 + 4 * (size_t)slot);
		if (!dict_off)
			continue;
		const size_t dict = REL (d, 0x20 + 4 * (size_t)slot);
		if (dict + 8 > size)
			continue;
		const uint32_t n_obj = rb32 (d + dict + 4);
		if (!n_obj || n_obj > 0x10000 || dict + 8 + (size_t)(n_obj + 1) * 16 > size)
			continue;

		for (uint32_t fi = 1; fi <= n_obj; fi++)
		{
			const size_t fe = dict + 8 + (size_t)fi * 16;
			const size_t fs = REL (d, fe + 12);
			if (fs + 8 > size)
				continue;
			char magic[5];
			memcpy (magic, d + fs, 4);
			magic[4] = 0;
			if (memcmp (magic, "FSKA", 4) && memcmp (magic, "FSHU", 4)
				&& memcmp (magic, "FTXP", 4) && memcmp (magic, "FVIS", 4)
				&& memcmp (magic, "FSHA", 4) && memcmp (magic, "FSCN", 4))
				continue;

			if (n >= cap)
			{
				size_t ncap = cap * 2;
				bfres_anim_entry_t *nl = realloc (list, ncap * sizeof (*nl));
				if (!nl)
					break;
				memset (nl + cap, 0, (ncap - cap) * sizeof (*nl));
				list = nl;
				cap = ncap;
			}
			bfres_anim_entry_t *e = list + n;
			memcpy (e->cls, magic, 5);
			const char *nm = rel_string (d, size, fe + 8);
			snprintf (e->name, sizeof (e->name), "%s", nm && *nm ? nm : "?");
			e->frames = -1;

			if (!memcmp (magic, "FSKA", 4))
			{
				// ResSkeletalAnimData: flag@12 numFrame@16 nBone@20
				// nUser@22 nCurve@24 baked@28 + 4 offsets@32.
				if (fs + 48 > size)
					continue;
				e->frames = rbs32 (d + fs + 16);
				const uint16_t nba = rb16 (d + fs + 20);
				e->n_sub = nba;
				e->n_curve = (uint32_t)rbs32 (d + fs + 24);
				if (!nba || nba > 4096)
					continue;
				// ResBoneAnimData is 0x18 bytes; curve array at +0x10,
				// count (u8) at +0x0A -- same walk parse_fska_into_model().
				const size_t ba = REL (d, fs + 32);
				if (!ba || ba + (size_t)nba * 0x18 > size)
					continue;
				uint32_t tot = 0, ok = 0;
				for (uint16_t b = 0; b < nba; b++)
				{
					const size_t bo = ba + (size_t)b * 0x18;
					const uint8_t nk = d[bo + 0x0A];
					const size_t cb = REL (d, bo + 0x10);
					if (!nk || !cb)
						continue;
					tot += nk;
					uint32_t g = 0;
					bfres_anim_count_curves (d, size, cb, nk, new_layout, &g);
					ok += g;
				}
				if (!e->n_curve)
					e->n_curve = tot;
				e->n_curve_ok = ok;
			}
			else if (!memcmp (magic, "FSHU", 4))
			{
				// ResShaderParamAnimData (52B): flag@12 numFrame@16
				// nMat@20 nUser@22 nParam@24 nCurve@28 baked@32
				// + 4 offsets@36 (model, index, matArray, userDic).
				// ResShaderParamMatAnimData (32B): counts@0 begins@8
				// name@16 paramInfo@20 curve@24 const@28.
				if (fs + 52 > size)
					continue;
				e->frames = rbs32 (d + fs + 16);
				const uint16_t nmat = rb16 (d + fs + 20);
				e->n_sub = nmat;
				e->n_curve = (uint32_t)rbs32 (d + fs + 28);
				if (!nmat || nmat > 4096)
					continue;
				const size_t ma = REL (d, fs + 44);
				if (!ma || ma + (size_t)nmat * 32 > size)
					continue;
				uint32_t tot = 0, ok = 0;
				for (uint16_t m = 0; m < nmat; m++)
				{
					const size_t mo = ma + (size_t)m * 32;
					const uint16_t nc = rb16 (d + mo + 2);
					const size_t ca = REL (d, mo + 24);
					if (!nc || !ca)
						continue;
					tot += nc;
					uint32_t g = 0;
					bfres_anim_count_curves (d, size, ca, nc, new_layout, &g);
					ok += g;
				}
				if (!e->n_curve)
					e->n_curve = tot;
				e->n_curve_ok = ok;
			}
			else if (!memcmp (magic, "FTXP", 4))
			{
				// ResTexPatternAnimData (60B): flag@12 nUser@14
				// numFrame@16 nTexRef@20 nMat@22 nPat@24 nCurve@28
				// baked@32 + 5 offsets@36. ResTexPatternMatAnimData
				// (28B): counts@0 begins@4 name@12 pat@16 curve@20
				// base@24.
				if (fs + 60 > size)
					continue;
				e->frames = rbs32 (d + fs + 16);
				const uint16_t nmat = rb16 (d + fs + 22);
				e->n_sub = nmat;
				e->n_curve = (uint32_t)rbs32 (d + fs + 28);
				if (!nmat || nmat > 4096)
					continue;
				const size_t ma = REL (d, fs + 44);
				if (!ma || ma + (size_t)nmat * 28 > size)
					continue;
				uint32_t tot = 0, ok = 0;
				for (uint16_t m = 0; m < nmat; m++)
				{
					const size_t mo = ma + (size_t)m * 28;
					const uint16_t nc = rb16 (d + mo + 2);
					const size_t ca = REL (d, mo + 20);
					if (!nc || !ca)
						continue;
					tot += nc;
					uint32_t g = 0;
					bfres_anim_count_curves (d, size, ca, nc, new_layout, &g);
					ok += g;
				}
				if (!e->n_curve)
					e->n_curve = tot;
				e->n_curve_ok = ok;
			}
			else if (!memcmp (magic, "FVIS", 4))
			{
				// ResVisibilityAnimData (60B): flag@12 nUser@14
				// numFrame@16 nAnim@20 nCurve@22 baked@24
				// + 6 offsets@28. Curves (if any) at ofs[3].
				if (fs + 60 > size)
					continue;
				e->frames = rbs32 (d + fs + 16);
				const uint16_t nan = rb16 (d + fs + 20);
				e->n_sub = nan;
				e->n_curve = rb16 (d + fs + 22);
				const size_t ca = REL (d, fs + 40);
				uint32_t ok = 0;
				if (e->n_curve && ca)
					bfres_anim_count_curves (d, size, ca, e->n_curve, new_layout, &ok);
				e->n_curve_ok = ok;
			}
			else if (!memcmp (magic, "FSHA", 4))
			{
				// ResShapeAnimData (56B): flag@12 nUser@14 numFrame@16
				// nVShape@20 nKey@22 nCurve@24 rsvd@26 baked@28
				// + 4 offsets@32. ResVertexShapeAnimData (28B):
				// counts@0 begins@4 name@12 keyInfo@16 curve@20
				// base@24.
				if (fs + 56 > size)
					continue;
				e->frames = rbs32 (d + fs + 16);
				const uint16_t nvs = rb16 (d + fs + 20);
				e->n_sub = nvs;
				e->n_curve = rb16 (d + fs + 24);
				if (!nvs || nvs > 4096)
					continue;
				const size_t va = REL (d, fs + 40);
				if (!va || va + (size_t)nvs * 28 > size)
					continue;
				uint32_t tot = 0, ok = 0;
				for (uint16_t v = 0; v < nvs; v++)
				{
					const size_t vo = va + (size_t)v * 28;
					const uint16_t nc = rb16 (d + vo);
					const size_t ca = REL (d, vo + 20);
					if (!nc || !ca)
						continue;
					tot += nc;
					uint32_t g = 0;
					bfres_anim_count_curves (d, size, ca, nc, new_layout, &g);
					ok += g;
				}
				if (!e->n_curve)
					e->n_curve = tot;
				e->n_curve_ok = ok;
			}
			else /* FSCN */
			{
				// ResSceneAnimData (44B): nUser@12 nCam@14 nLight@16
				// nFog@18 + 4 dict offsets@20. No frame count.
				if (fs + 44 > size)
					continue;
				e->n_sub = (uint32_t)rb16 (d + fs + 14)
					+ (uint32_t)rb16 (d + fs + 16) + (uint32_t)rb16 (d + fs + 18);
			}
			n++;
		}
	}

	*out_entries = list;
	return (int)n;
}
