// SPDX-License-Identifier: GPL-2.0+
// Metroid Prime: Federation Force support -- see lib-fedforce.h.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <limits.h>
#ifdef __cplusplus
extern "C"
{
#endif
#include "types.h"
#include "lib-std.h"
#include "lib-nintendo.h"
#include "lib-model-glb.h"
#include "lib-ctpk.h"
#include "lib-fedforce.h"
#ifdef __cplusplus
}
#endif

#define FED_MAX_CHUNKS 100000
#define FED_MAX_MESHES 65536
#define FED_MAX_VERTS (16u << 20)
#define FED_MAX_INDICES (64u << 20)
#define FED_MAX_JOINTS 4096
#define FED_MAX_OUTPUT (512u << 20)

static float fed_f32 (const u8 *p)
{
	float v;
	memcpy (&v, p, 4);
	return v;
}

//-----------------------------------------------------------------------------
// NLG hash
//-----------------------------------------------------------------------------

u32 FedForceHash (const char *name, bool case_sensitive)
{
	int h = -1;
	for (const u8 *p = (const u8 *)name; *p; p++)
	{
		int c = *p;
		if (case_sensitive && c >= 65 && c <= 90)
			c |= 0x20;
		h = (int)((h * 33 + c) & 0xFFFFFFFF);
	}
	return (u32)h;
}

// Known names for reverse lookup. Layout presets drive vertex decoding;
// languages + fixed hashes come from the dumper's Hashing.cs.
static const char *fed_known_layouts[] = {
	"enemyrigidskin",
	"chameleonrigidskin",
	"rigidskinuvsliding",
	"rigidskindiffuseconstcolor",
	"rigidskinconstcolor",
	"uvsliding",
	"diffuselightmap",
	"chameleondiffuseconstcolor",
	"chameleonconstcolor",
	"characterrigidskin",
	"transparent",
	"decalrigidskin",
	"chameleonmapgeo",
	"chameleondiffuseconstcolorrimlight",
	"weaponrigidskin",
	"rigidskindiffusevertcolor",
	"cockpitrigidskin",
	"cockpitenvironmentmaprigidskin",
	"chameleondiffuselit",
	"diffuselightmaprimlight",
	"SkyboxMaterial",
	"skyboxmaterial",
	"luigieyematerial",
	"diffuseskin",
	"luigimaterial",
	"pestmaterial",
	"morphghostmaterial",
	"morphluigimaterial",
	"morphpestmaterial",
	"windowrigidskin",
	"skyboxmaterial",
	"ghostnonskinmaterial",
	"diffusevertcolor",
	"vertcolor",
	"windowmaterial",
	"propsmaterial",
	"environmentspheremap",
	"environmentspheremaprigidskin",
	"moolahmaterial",
	"uvslidingrigidskin",
	"environmentmaterial",
	"environmentrigidskin",
	"environmentspecularrigidskin",
	"environmentspecularmaterial",
	"uvslidingmaterial",
	"diffuseconstcolor",
	"clothmaterial",
	"uvslidingmaterialgs",
};

static const char *fed_known_fixed[] = {
	"french",
	"german",
	"japanese",
	"korean",
	"nafrench",
	"naspanish",
	"portuguese",
	"russian",
	"spanish",
	"italian",
	"dutch",
	"naportuguese",
	"english",
	"ukenglish",
	"cnsimplified",
	"cntraditional",
	"frenchhw",
	"germanhw",
	"japanesehw",
	"koreanhw",
	"englishhw",
	"debughw",
	"debugenglish",
	"debugenglishhw",
	"shader",
	"feloc",
	"material",
	"lightfield",
	"audiobank",
	"bank",
	"effecttemplate",
	"collision",
	"localization",
	"gui",
	"materialparams",
	"shaderconstants",
};

ccp FedForceHashName (u32 hash, char out[16])
{
	static struct
	{
		u32 hash;
		const char *name;
	} cache[256];
	static int n_cache = 0;
	static bool built = false;
	if (!built)
	{
		built = true;
		for (uint i = 0; i < sizeof (fed_known_layouts) / sizeof (fed_known_layouts[0]); i++)
		{
			u32 h = FedForceHash (fed_known_layouts[i], false);
			if (n_cache < 256)
			{
				cache[n_cache].hash = h;
				cache[n_cache].name = fed_known_layouts[i];
				n_cache++;
			}
		}
		for (uint i = 0; i < sizeof (fed_known_fixed) / sizeof (fed_known_fixed[0]); i++)
		{
			u32 h = FedForceHash (fed_known_fixed[i], false);
			bool dup = false;
			for (int k = 0; k < n_cache; k++)
				if (cache[k].hash == h)
				{
					dup = true;
					break;
				}
			if (!dup && n_cache < 256)
			{
				cache[n_cache].hash = h;
				cache[n_cache].name = fed_known_fixed[i];
				n_cache++;
			}
		}
	}
	for (int i = 0; i < n_cache; i++)
		if (cache[i].hash == hash)
			return cache[i].name;
	if (out)
		snprintf (out, 16, "%X", hash);
	return 0;
}

//-----------------------------------------------------------------------------
// Chunk flag helpers
//-----------------------------------------------------------------------------

bool FedChunkHasChildren (u16 flags)
{
	return (flags >> 15) & 1;
}

uint FedChunkBlockIndex (u16 flags)
{
	return (flags >> 12) & 7; // FedForce uses the LM2 3-bit variant
}

uint FedChunkAlignment (u16 flags)
{
	return (flags >> 1) & 0x3FF;
}

enumError ScanFedForceChunks (fed_chunk_t **chunks, uint *n, const u8 *data, uint size)
{
	if (!chunks || !n || !data)
		return EINVAL;
	*chunks = 0;
	*n = 0;
	if (size < 12 || size % 12 != 0)
		return EINVAL;
	uint count = size / 12;
	if (count == 0 || count > FED_MAX_CHUNKS)
		return EINVAL;
	fed_chunk_t *out = CALLOC (count, sizeof (*out));
	if (!out)
		return ERR_CANT_CREATE;
	for (uint i = 0; i < count; i++)
	{
		const u8 *e = data + (size_t)i * 12;
		u16 type = rd_le16 (e);
		u16 flags = rd_le16 (e + 2);
		u32 sz = rd_le32 (e + 4);
		u32 off = rd_le32 (e + 8);
		out[i].type = type;
		out[i].flags = flags;
		out[i].size = sz;
		out[i].offset = off;
		out[i].is_file = (type == FED_CHUNK_FILEHEADER);
	}
	*chunks = out;
	*n = count;
	return ERR_OK;
}

//-----------------------------------------------------------------------------
// FedForce .dict header scan
//-----------------------------------------------------------------------------

enumError ScanFedForceDict (const u8 *dict, uint dict_size, bool *is_fed, fed_dict_block_t **blocks,
	uint *n_blocks, fed_dict_ref_t *ref, const char ***strings, uint *n_strings)
{
	if (is_fed)
		*is_fed = false;
	if (!dict || dict_size < 20)
		return EINVAL;
	// Identifier aliases 0x5824F3A9 (LM2/LM3 family); FedForce is the
	// 0x297B947A-at-offset-16 variant (either endianness).
	u32 m_be = rd_be32 (dict), m_le = rd_le32 (dict);
	if (m_be != 0x5824F3A9 && m_le != 0x5824F3A9 && m_be != 0xA9F32458 && m_le != 0xA9F32458)
		return ERR_NOTHING_TO_DO;
	if (dict_size < 20)
		return EINVAL;
	bool fed = (dict_size >= 20
		&& (rd_be32 (dict + 16) == 0x297B947A || rd_le32 (dict + 16) == 0x297B947A));
	if (is_fed)
		*is_fed = fed;
	if (!fed)
		return ERR_NOTHING_TO_DO;
	// LE layout: id u32, flags u16, compressed u8, pad, largest u32,
	// numBlocks u8, numRefs u8, numStrings u8, pad, ref(s), blocks, strings.
	uint num_blocks = dict[12];
	uint num_refs = dict[13];
	uint num_strings = dict[14];
	if (num_refs == 0 || num_blocks == 0 || num_blocks > 100000 || num_strings > 10000)
		return EINVAL;
	size_t p = 16;
	if (p + num_refs * 16 > dict_size)
		return EINVAL;
	fed_dict_ref_t first;
	memset (&first, 0, sizeof (first));
	{
		const u8 *r = dict + p;
		first.name_hash = rd_le32 (r);
		first.file_section_count = rd_le16 (r + 4);
		first.file_count = rd_le16 (r + 6);
		memcpy (first.block_indices, r + 8, 8);
	}
	p += (size_t)num_refs * 16;
	if (p + (size_t)num_blocks * 16 > dict_size)
		return EINVAL;
	fed_dict_block_t *bl = CALLOC (num_blocks, sizeof (*bl));
	if (!bl)
		return ERR_CANT_CREATE;
	for (uint i = 0; i < num_blocks; i++)
	{
		const u8 *b = dict + p + (size_t)i * 16;
		bl[i].offset = rd_le32 (b);
		bl[i].decomp_size = rd_le32 (b + 4);
		bl[i].comp_size = rd_le32 (b + 8);
		bl[i].flags = rd_le32 (b + 12);
		bl[i].source_index = (u8)((bl[i].flags >> 16) & 0xFF);
	}
	p += (size_t)num_blocks * 16;
	const char **strs = 0;
	if (num_strings)
	{
		strs = CALLOC (num_strings, sizeof (*strs));
		if (!strs)
		{
			FREE (bl);
			return ERR_CANT_CREATE;
		}
		for (uint i = 0; i < num_strings; i++)
		{
			if (p >= dict_size)
			{
				for (uint k = 0; k < i; k++)
					FREE ((void *)strs[k]);
				FREE (strs);
				FREE (bl);
				return EINVAL;
			}
			const u8 *s = dict + p;
			const u8 *nul = memchr (s, 0, dict_size - p);
			if (!nul)
			{
				for (uint k = 0; k < i; k++)
					FREE ((void *)strs[k]);
				FREE (strs);
				FREE (bl);
				return EINVAL;
			}
			size_t len = nul - s;
			char *cp = MALLOC (len + 1);
			if (!cp)
			{
				for (uint k = 0; k < i; k++)
					FREE ((void *)strs[k]);
				FREE (strs);
				FREE (bl);
				return ERR_CANT_CREATE;
			}
			memcpy (cp, s, len);
			cp[len] = 0;
			strs[i] = cp;
			p += len + 1;
		}
	}
	if (blocks)
		*blocks = bl;
	else
		FREE (bl);
	if (n_blocks)
		*n_blocks = num_blocks;
	if (ref)
		*ref = first;
	if (strings)
		*strings = strs;
	else if (strs)
	{
		for (uint i = 0; i < num_strings; i++)
			FREE ((void *)strs[i]);
		FREE (strs);
	}
	if (n_strings)
		*n_strings = num_strings;
	return ERR_OK;
}

void FreeFedForceDict (fed_dict_block_t *blocks, const char **strings, uint n_strings)
{
	if (blocks)
		FREE (blocks);
	if (strings)
	{
		for (uint i = 0; i < n_strings; i++)
			FREE ((void *)strings[i]);
		FREE (strings);
	}
}

//-----------------------------------------------------------------------------
// Vertex layouts (dumper VertexLoaderExtension: 47 presets, stride table)
//-----------------------------------------------------------------------------

typedef enum
{
	L_SKINNING = 0,
	L_SKINNING_COLOR,
	L_SKINNING_MORPH,
	L_SKINNING_MORPH_COLOR,
	L_POS_UV2,
	L_POS_UV2_COLOR,
	L_POS_COLOR_ONLY,
	L_POS_NRM_UV1,
	L_RIGID_POS_NRM_UV1,
	L_RIGID_POS_NRM_UV1_COLOR,
	L_RIGID_POS_NRM_UV1_COLOR_UV2,
	L_POS_NRM_UV1_COLOR_EXTRA,
	L_POS_NRM_UV3_COLOR,
	L_POS_NRM_UV2,
	L_POS_NRM_UV2_COLOR,
	L_RIGID_POS_NRM,
	L_POS_ONLY,
	L_POS_NRM_UV2_COLOR_COMP,
} fed_layout_t;

typedef struct
{
	const char *name;
	fed_layout_t layout;
	int stride;
} fed_preset_t;

// Strides per dumper StrideTable.
static const fed_preset_t fed_presets[] = {
	{ "enemyrigidskin", L_RIGID_POS_NRM_UV1_COLOR, 24 },
	{ "chameleonrigidskin", L_RIGID_POS_NRM_UV1_COLOR, 24 },
	{ "rigidskinuvsliding", L_RIGID_POS_NRM_UV1_COLOR_UV2, 28 },
	{ "rigidskindiffuseconstcolor", L_RIGID_POS_NRM_UV1, 20 },
	{ "rigidskinconstcolor", L_RIGID_POS_NRM, 16 },
	{ "uvsliding", L_POS_NRM_UV2_COLOR, 28 },
	{ "diffuselightmap", L_POS_NRM_UV1_COLOR_EXTRA, 28 },
	{ "chameleondiffuseconstcolor", L_POS_NRM_UV1, 20 },
	{ "chameleonconstcolor", L_POS_ONLY, 12 },
	{ "characterrigidskin", L_RIGID_POS_NRM_UV1_COLOR, 24 },
	{ "transparent", L_RIGID_POS_NRM_UV1_COLOR, 24 },
	{ "decalrigidskin", L_RIGID_POS_NRM_UV1_COLOR, 24 },
	{ "chameleonmapgeo", L_POS_ONLY, 12 },
	{ "chameleondiffuseconstcolorrimlight", L_POS_NRM_UV1, 20 },
	{ "weaponrigidskin", L_RIGID_POS_NRM_UV1_COLOR, 24 },
	{ "rigidskindiffusevertcolor", L_RIGID_POS_NRM_UV1_COLOR, 24 },
	{ "cockpitrigidskin", L_RIGID_POS_NRM_UV1_COLOR, 24 },
	{ "cockpitenvironmentmaprigidskin", L_POS_NRM_UV2_COLOR, 28 },
	{ "chameleondiffuselit", L_POS_NRM_UV1, 20 },
	{ "diffuselightmaprimlight", L_POS_NRM_UV2_COLOR, 28 },
	{ "SkyboxMaterial", L_POS_NRM_UV1, 20 },
	{ "luigieyematerial", L_SKINNING, 22 },
	{ "diffuseskin", L_SKINNING, 22 },
	{ "luigimaterial", L_SKINNING, 22 },
	{ "pestmaterial", L_SKINNING_COLOR, 26 },
	{ "morphghostmaterial", L_SKINNING_MORPH, 70 },
	{ "morphluigimaterial", L_SKINNING_MORPH, 70 },
	{ "morphpestmaterial", L_SKINNING_MORPH_COLOR, 74 },
	{ "windowrigidskin", L_POS_UV2, 20 },
	{ "skyboxmaterial", L_POS_UV2, 20 },
	{ "ghostnonskinmaterial", L_POS_UV2, 20 },
	{ "diffusevertcolor", L_POS_UV2_COLOR, 24 },
	{ "vertcolor", L_POS_COLOR_ONLY, 16 },
	{ "windowmaterial", L_POS_NRM_UV1, 20 },
	{ "propsmaterial", L_POS_NRM_UV2, 24 },
	{ "environmentspheremap", L_POS_NRM_UV2, 24 },
	{ "environmentspheremaprigidskin", L_POS_NRM_UV2, 24 },
	{ "moolahmaterial", L_POS_NRM_UV2, 24 },
	{ "uvslidingrigidskin", L_POS_NRM_UV2_COLOR, 28 },
	{ "environmentmaterial", L_POS_NRM_UV2_COLOR, 28 },
	{ "environmentrigidskin", L_POS_NRM_UV2_COLOR, 28 },
	{ "environmentspecularrigidskin", L_POS_NRM_UV2_COLOR, 28 },
	{ "environmentspecularmaterial", L_POS_NRM_UV2_COLOR, 28 },
	{ "uvslidingmaterial", L_POS_NRM_UV2_COLOR, 28 },
	{ "diffuseconstcolor", L_RIGID_POS_NRM, 16 },
	{ "clothmaterial", L_POS_NRM_UV2_COLOR_COMP, 36 },
	{ "uvslidingmaterialgs", L_POS_ONLY, 12 },
};
#define FED_N_PRESETS (sizeof (fed_presets) / sizeof (fed_presets[0]))

#define FED_SHORT_POS_1 (1.0f / 4096.0f)
#define FED_SHORT_POS_2 (1.0f / 8192.0f)
#define FED_MORPH_SCALE (1.0f / 256.0f)
#define FED_NORMAL_SCALE (1.0f / 128.0f)
#define FED_COLOR_SCALE (1.0f / 255.0f)
#define FED_WEIGHT_SCALE (1.0f / 16384.0f)
#define FED_UV_SCALE (1.0f / 1024.0f)

static const fed_preset_t *fed_preset_for_hash (u32 hash)
{
	char hex[16];
	ccp name = FedForceHashName (hash, hex);
	if (!name)
		name = hex;
	// Case-sensitive like the dumper's layout dictionary
	// ("SkyboxMaterial" and "skyboxmaterial" differ).
	for (uint i = 0; i < FED_N_PRESETS; i++)
		if (!strcmp (name, fed_presets[i].name))
			return &fed_presets[i];
	return 0;
}

static int fed_stride_for_hash (u32 hash)
{
	const fed_preset_t *p = fed_preset_for_hash (hash);
	return p ? p->stride : 0;
}

static float fed_short_pos_scale (u32 hash)
{
	char hex[16];
	ccp name = FedForceHashName (hash, hex);
	if (!name)
		name = hex;
	if (!strcmp (name, "luigimaterial") || !strcmp (name, "morphluigimaterial"))
		return FED_SHORT_POS_2;
	return FED_SHORT_POS_1;
}

static s16 fed_rd_s16 (const u8 *p)
{
	return (s16)rd_le16 (p);
}

// Decoded vertex (subset of dumper Vertex needed for model_t).
typedef struct fed_vertex_t
{
	float px, py, pz;
	float nx, ny, nz;
	float u0, v0, u2, v2, u3, v3;
	float r, g, b, a;
	bool has_normal, has_uv0, has_uv2, has_uv3, has_color;
} fed_vertex_t;

// Returns bytes consumed (0 = unknown layout).
static uint fed_decode_vertex (fed_vertex_t *v, const u8 *p, uint avail, u32 mat_hash)
{
	const fed_preset_t *pr = fed_preset_for_hash (mat_hash);
	if (!pr || !v || !p)
		return 0;
	memset (v, 0, sizeof (*v));
	v->r = v->g = v->b = v->a = 1.0f;
	uint need = (uint)pr->stride;
	// COMP consumes 24 of its 36 stride bytes (remainder reserved).
	if (pr->layout == L_POS_NRM_UV2_COLOR_COMP)
		need = 24;
	if (avail < need)
		return 0;
	switch (pr->layout)
	{
		case L_SKINNING:
		case L_SKINNING_COLOR:
		case L_SKINNING_MORPH:
		case L_SKINNING_MORPH_COLOR:
		{
			float s = fed_short_pos_scale (mat_hash);
			v->px = fed_rd_s16 (p) * s;
			v->py = fed_rd_s16 (p + 2) * s;
			v->pz = fed_rd_s16 (p + 4) * s;
			v->nx = (s8)p[6] * FED_NORMAL_SCALE;
			v->ny = (s8)p[7] * FED_NORMAL_SCALE;
			v->nz = (s8)p[8] * FED_NORMAL_SCALE;
			v->has_normal = true;
			v->u0 = fed_rd_s16 (p + 10) * FED_UV_SCALE;
			v->v0 = fed_rd_s16 (p + 12) * FED_UV_SCALE;
			v->has_uv0 = true;
			// weights decoded for completeness (skinning export is future work)
			if (pr->layout == L_SKINNING_COLOR || pr->layout == L_SKINNING_MORPH_COLOR)
			{
				uint base = (pr->layout == L_SKINNING_COLOR) ? 22 : 70;
				v->r = p[base] * FED_COLOR_SCALE;
				v->g = p[base + 1] * FED_COLOR_SCALE;
				v->b = p[base + 2] * FED_COLOR_SCALE;
				v->a = p[base + 3] * FED_COLOR_SCALE;
				v->has_color = true;
			}
			return (uint)pr->stride;
		}
		case L_POS_UV2:
		case L_POS_UV2_COLOR:
		case L_POS_ONLY:
			v->px = fed_f32 (p);
			v->py = fed_f32 (p + 4);
			v->pz = fed_f32 (p + 8);
			if (pr->layout == L_POS_ONLY)
				return 12;
			v->u0 = fed_rd_s16 (p + 12) * FED_UV_SCALE;
			v->v0 = fed_rd_s16 (p + 14) * FED_UV_SCALE;
			v->u2 = fed_rd_s16 (p + 16) * FED_UV_SCALE;
			v->v2 = fed_rd_s16 (p + 18) * FED_UV_SCALE;
			v->has_uv0 = v->has_uv2 = true;
			if (pr->layout == L_POS_UV2_COLOR)
			{
				v->r = p[20] * FED_COLOR_SCALE;
				v->g = p[21] * FED_COLOR_SCALE;
				v->b = p[22] * FED_COLOR_SCALE;
				v->a = p[23] * FED_COLOR_SCALE;
				v->has_color = true;
			}
			return (uint)pr->stride;
		case L_POS_COLOR_ONLY:
			v->px = fed_f32 (p);
			v->py = fed_f32 (p + 4);
			v->pz = fed_f32 (p + 8);
			v->r = p[12] * FED_COLOR_SCALE;
			v->g = p[13] * FED_COLOR_SCALE;
			v->b = p[14] * FED_COLOR_SCALE;
			v->a = p[15] * FED_COLOR_SCALE;
			v->has_color = true;
			return 16;
		default:
			break;
	}
	// Normal layouts share the 16-byte position+normal+pad prefix.
	v->px = fed_f32 (p);
	v->py = fed_f32 (p + 4);
	v->pz = fed_f32 (p + 8);
	v->nx = (s8)p[12] * FED_NORMAL_SCALE;
	v->ny = (s8)p[13] * FED_NORMAL_SCALE;
	v->nz = (s8)p[14] * FED_NORMAL_SCALE;
	v->has_normal = true;
	if (pr->layout == L_RIGID_POS_NRM)
		return 16;
	v->u0 = fed_rd_s16 (p + 16) * FED_UV_SCALE;
	v->v0 = fed_rd_s16 (p + 18) * FED_UV_SCALE;
	v->has_uv0 = true;
	if (pr->layout == L_POS_NRM_UV1 || pr->layout == L_RIGID_POS_NRM_UV1)
		return 20;
	if (pr->layout == L_RIGID_POS_NRM_UV1_COLOR)
	{
		v->r = p[20] * FED_COLOR_SCALE;
		v->g = p[21] * FED_COLOR_SCALE;
		v->b = p[22] * FED_COLOR_SCALE;
		v->a = p[23] * FED_COLOR_SCALE;
		v->has_color = true;
		return 24;
	}
	if (pr->layout == L_POS_NRM_UV1_COLOR_EXTRA)
	{
		v->r = p[20] * FED_COLOR_SCALE;
		v->g = p[21] * FED_COLOR_SCALE;
		v->b = p[22] * FED_COLOR_SCALE;
		v->a = p[23] * FED_COLOR_SCALE;
		v->has_color = true;
		v->u2 = fed_rd_s16 (p + 24) * FED_UV_SCALE;
		v->v2 = fed_rd_s16 (p + 26) * FED_UV_SCALE;
		v->has_uv2 = true;
		return 28;
	}
	if (pr->layout == L_POS_NRM_UV3_COLOR)
	{
		v->u2 = fed_rd_s16 (p + 20) * FED_UV_SCALE;
		v->v2 = fed_rd_s16 (p + 22) * FED_UV_SCALE;
		v->u3 = fed_rd_s16 (p + 24) * FED_UV_SCALE;
		v->v3 = fed_rd_s16 (p + 26) * FED_UV_SCALE;
		v->has_uv2 = v->has_uv3 = true;
		v->r = p[28] * FED_COLOR_SCALE;
		v->g = p[29] * FED_COLOR_SCALE;
		v->b = p[30] * FED_COLOR_SCALE;
		v->a = p[31] * FED_COLOR_SCALE;
		v->has_color = true;
		return 32;
	}
	if (pr->layout == L_RIGID_POS_NRM_UV1_COLOR_UV2)
	{
		v->u2 = fed_rd_s16 (p + 20) * FED_UV_SCALE;
		v->v2 = fed_rd_s16 (p + 22) * FED_UV_SCALE;
		v->has_uv2 = true;
		v->r = p[24] * FED_COLOR_SCALE;
		v->g = p[25] * FED_COLOR_SCALE;
		v->b = p[26] * FED_COLOR_SCALE;
		v->a = p[27] * FED_COLOR_SCALE;
		v->has_color = true;
		return 28;
	}
	v->u2 = fed_rd_s16 (p + 20) * FED_UV_SCALE;
	v->v2 = fed_rd_s16 (p + 22) * FED_UV_SCALE;
	v->has_uv2 = true;
	if (pr->layout == L_POS_NRM_UV2_COLOR)
	{
		v->r = p[24] * FED_COLOR_SCALE;
		v->g = p[25] * FED_COLOR_SCALE;
		v->b = p[26] * FED_COLOR_SCALE;
		v->a = p[27] * FED_COLOR_SCALE;
		v->has_color = true;
	}
	return (uint)pr->stride;
}

//-----------------------------------------------------------------------------
// FEDM / FEDS / FEDT containers
//
// FEDM: "FEDM" u16le ver=1 u16le nchunks, then nchunks x
//   (u16le type u16le 0 u32le size), then chunk payloads concatenated.
// Known chunk types are the FED_CHUNK_* values above plus 0xBFF0, a
// private material map: u32 n_meshes, then per mesh u32 diffuse_hash.
// FEDS: same with magic "FEDS" (skeleton chunks 0x7101..0x7105 only).
// FEDT: "FEDT" u16le ver=1 u16le pica_format u16le w u16le h u16le mip
//   u16le reserved u32le tex_hash u32le data_size, then pixel bytes.
//-----------------------------------------------------------------------------

typedef struct
{
	u16 type;
	const u8 *data;
	u32 size;
} fed_part_t;

static enumError fed_parse_container (
	const u8 *data, size_t size, const char magic[4], fed_part_t **parts, uint *n_parts)
{
	if (parts)
		*parts = 0;
	if (n_parts)
		*n_parts = 0;
	if (!data || size < 8 || memcmp (data, magic, 4))
		return EINVAL;
	if (rd_le16 (data + 4) != 1)
		return EINVAL;
	uint n = rd_le16 (data + 6);
	// FEDT uses a fixed header instead of a chunk table; it never reaches here.
	if (n == 0 || n > 4096)
		return EINVAL;
	if (8 + (size_t)n * 8 > size)
		return EINVAL;
	fed_part_t *out = CALLOC (n, sizeof (*out));
	if (!out)
		return ERR_CANT_CREATE;
	size_t base = 8 + (size_t)n * 8;
	size_t p = base;
	for (uint i = 0; i < n; i++)
	{
		u16 ty = rd_le16 (data + 8 + (size_t)i * 8);
		u32 sz = rd_le32 (data + 8 + (size_t)i * 8 + 4);
		if ((u64)p + sz > size)
		{
			FREE (out);
			return EINVAL;
		}
		out[i].type = ty;
		out[i].data = data + p;
		out[i].size = sz;
		p += sz;
	}
	if (parts)
		*parts = out;
	else
		FREE (out);
	if (n_parts)
		*n_parts = n;
	return ERR_OK;
}

static const fed_part_t *fed_find_part (const fed_part_t *parts, uint n, u16 type)
{
	for (uint i = 0; i < n; i++)
		if (parts[i].type == type)
			return &parts[i];
	return 0;
}

bool IsFedForceModel (const u8 *data, size_t size)
{
	return fed_parse_container (data, size, "FEDM", 0, 0) == ERR_OK;
}

bool IsFedForceSkeleton (const u8 *data, size_t size)
{
	if (fed_parse_container (data, size, "FEDS", 0, 0) != ERR_OK)
		return false;
	return true;
}

bool IsFedForceTexture (const u8 *data, size_t size)
{
	if (!data || size < 24 || memcmp (data, "FEDT", 4))
		return false;
	if (rd_le16 (data + 4) != 1)
		return false;
	uint w = rd_le16 (data + 8), h = rd_le16 (data + 10);
	uint ds = rd_le32 (data + 20);
	if (!w || !h || w > 16384 || h > 16384)
		return false;
	if (24 + (size_t)ds > size)
		return false;
	return true;
}

enumError BuildFedForceTexture (u8 **dest, uint *dest_size, uint width, uint height,
	uint pica_format, const u8 *pixels, uint pixels_size, uint tex_hash)
{
	if (!dest || !dest_size || !pixels || !pixels_size || !width || !height)
		return EINVAL;
	if (width > 16384 || height > 16384)
		return EINVAL;
	// Header (24 bytes): FEDT ver fmt w h mip res hash datasize data...
	u8 *out = MALLOC (24 + pixels_size);
	if (!out)
		return ERR_CANT_CREATE;
	memcpy (out, "FEDT", 4);
	wr_le16 (out + 4, 1);
	wr_le16 (out + 6, (u16)pica_format);
	wr_le16 (out + 8, (u16)width);
	wr_le16 (out + 10, (u16)height);
	wr_le16 (out + 12, 1);
	wr_le16 (out + 14, 0);
	wr_le32 (out + 16, tex_hash);
	wr_le32 (out + 20, pixels_size);
	memcpy (out + 24, pixels, pixels_size);
	*dest = out;
	*dest_size = 24 + pixels_size;
	return ERR_OK;
}

// FEDT header is 24 bytes (see builder above).
enumError DecodeFedForceTexture (u8 **dest, uint *width, uint *height, const u8 *data, size_t size)
{
	if (!dest || !width || !height || !data || size < 24 || memcmp (data, "FEDT", 4))
		return EINVAL;
	uint fmt = rd_le16 (data + 6);
	uint w = rd_le16 (data + 8), h = rd_le16 (data + 10);
	uint ds = rd_le32 (data + 20);
	if (!w || !h || w > 16384 || h > 16384)
		return EINVAL;
	if ((u64)24 + ds > size)
		return EINVAL;
	return DecodePicaTexture (dest, width, height, data + 24, w, h, fmt, ds);
}

enumError BuildFedForceModel (u8 **dest, uint *dest_size, const u8 *b008, uint b008_size,
	const u8 *b009, uint b009_size, const u8 *b001, uint b001_size, const u8 *b003, uint b003_size,
	const u8 *b004, uint b004_size, const u8 *b005, uint b005_size, const u8 *b006, uint b006_size,
	const u8 *b007, uint b007_size, const u8 *b002, uint b002_size, const u8 *s101, uint s101_size,
	const u8 *s102, uint s102_size, const u8 *s103, uint s103_size, const u8 *s104, uint s104_size,
	const u8 *s105, uint s105_size)
{
	struct
	{
		u16 type;
		const u8 *data;
		uint size;
	} tab[] = {
		{ FED_CHUNK_BOUND_RADIUS, b008, b008_size },
		{ FED_CHUNK_BOUND_BOX, b009, b009_size },
		{ FED_CHUNK_MODEL_TRANSFORM, b001, b001_size },
		{ FED_CHUNK_MESH_INFO, b003, b003_size },
		{ FED_CHUNK_VERT_START, b004, b004_size },
		{ FED_CHUNK_MESH_BUFFERS, b005, b005_size },
		{ FED_CHUNK_MATERIAL_DATA, b006, b006_size },
		{ FED_CHUNK_MATERIAL_LUT, b007, b007_size },
		{ FED_CHUNK_MODEL_INFO, b002, b002_size },
		{ FED_CHUNK_SKEL_HEADER, s101, s101_size },
		{ FED_CHUNK_SKEL_BONEINFO, s102, s102_size },
		{ FED_CHUNK_SKEL_BONEXTM, s103, s103_size },
		{ FED_CHUNK_SKEL_BONEIDX, s104, s104_size },
		{ FED_CHUNK_SKEL_BONEHASH, s105, s105_size },
	};
	uint n = 0;
	size_t total = 0;
	for (uint i = 0; i < sizeof (tab) / sizeof (tab[0]); i++)
		if (tab[i].data && tab[i].size)
		{
			n++;
			total += tab[i].size;
		}
	if (!dest || !dest_size || n == 0)
		return EINVAL;
	size_t hdr = 8 + (size_t)n * 8;
	if (hdr + total > FED_MAX_OUTPUT)
		return EFBIG;
	u8 *out = MALLOC (hdr + total);
	if (!out)
		return ERR_CANT_CREATE;
	memcpy (out, "FEDM", 4);
	wr_le16 (out + 4, 1);
	wr_le16 (out + 6, (u16)n);
	size_t tp = 8, dp = hdr;
	uint k = 0;
	for (uint i = 0; i < sizeof (tab) / sizeof (tab[0]); i++)
	{
		if (!tab[i].data || !tab[i].size)
			continue;
		wr_le16 (out + tp + (size_t)k * 8, tab[i].type);
		wr_le16 (out + tp + (size_t)k * 8 + 2, 0);
		wr_le32 (out + tp + (size_t)k * 8 + 4, tab[i].size);
		memcpy (out + dp, tab[i].data, tab[i].size);
		dp += tab[i].size;
		k++;
	}
	*dest = out;
	*dest_size = (uint)(hdr + total);
	return ERR_OK;
}

//-----------------------------------------------------------------------------
// Skeleton: 0x7101 header, 0x7102 bone infos, 0x7103 transforms,
// 0x7104 index list (ignored), 0x7105 hash list.
//-----------------------------------------------------------------------------

static void fed_quat_to_euler (
	float qx, float qy, float qz, float qw, float *rx, float *ry, float *rz)
{
	float sinr = 2.0f * (qw * qx + qy * qz);
	float cosr = 1.0f - 2.0f * (qx * qx + qy * qy);
	float roll = atan2f (sinr, cosr);
	float sinp = 2.0f * (qw * qy - qz * qx);
	float pitch = (fabsf (sinp) >= 1.0f) ? copysignf (1.57079632679f, sinp) : asinf (sinp);
	float siny = 2.0f * (qw * qz + qx * qy);
	float cosy = 1.0f - 2.0f * (qy * qy + qz * qz);
	float yaw = atan2f (siny, cosy);
	if (rx)
		*rx = roll;
	if (ry)
		*ry = pitch;
	if (rz)
		*rz = yaw;
}

// Fill joints from skeleton chunks; returns count (0 = none/invalid).
static uint fed_parse_joints (const fed_part_t *parts, uint n, model_t *model)
{
	const fed_part_t *h101 = fed_find_part (parts, n, FED_CHUNK_SKEL_HEADER);
	const fed_part_t *h102 = fed_find_part (parts, n, FED_CHUNK_SKEL_BONEINFO);
	const fed_part_t *h103 = fed_find_part (parts, n, FED_CHUNK_SKEL_BONEXTM);
	const fed_part_t *h105 = fed_find_part (parts, n, FED_CHUNK_SKEL_BONEHASH);
	if (!h101 || !h102 || !h103 || h101->size < 28)
		return 0;
	u32 bone_count = rd_le32 (h101->data);
	if (!bone_count || bone_count > FED_MAX_JOINTS)
		return 0;
	if (h102->size < (size_t)bone_count * 12 || h103->size < (size_t)bone_count * 28)
		return 0;
	model->joints = CALLOC (bone_count, sizeof (joint_t));
	if (!model->joints)
		return 0;
	model->num_joints = bone_count;
	for (uint i = 0; i < bone_count; i++)
	{
		joint_t *j = &model->joints[i];
		const u8 *bi = h102->data + (size_t)i * 12;
		u32 hash = rd_le32 (bi);
		s16 parent = (s16)rd_le16 (bi + 4);
		char hex[16];
		ccp nm = FedForceHashName (hash, hex);
		snprintf (j->name, sizeof (j->name), "%s", nm ? nm : hex);
		j->parent_idx
			= (parent < 0 || (uint)parent >= bone_count || (uint)parent == i) ? -1 : (int)parent;
		const u8 *bt = h103->data + (size_t)i * 28;
		float qx = fed_f32 (bt), qy = fed_f32 (bt + 4), qz = fed_f32 (bt + 8),
			  qw = fed_f32 (bt + 12);
		float ql = sqrtf (qx * qx + qy * qy + qz * qz + qw * qw);
		if (ql > 1e-6f && isfinite (ql))
		{
			qx /= ql;
			qy /= ql;
			qz /= ql;
			qw /= ql;
		}
		else
		{
			qx = qy = qz = 0.0f;
			qw = 1.0f;
		}
		fed_quat_to_euler (qx, qy, qz, qw, &j->rotate.x, &j->rotate.y, &j->rotate.z);
		j->translate.x = fed_f32 (bt + 16);
		j->translate.y = fed_f32 (bt + 20);
		j->translate.z = fed_f32 (bt + 24);
		if (!isfinite (j->translate.x) || !isfinite (j->translate.y) || !isfinite (j->translate.z))
		{
			j->translate.x = j->translate.y = j->translate.z = 0.0f;
		}
		j->scale.x = j->scale.y = j->scale.z = 1.0f;
		(void)h105;
	}
	return bone_count;
}

model_t *ParseFedForceSkeleton (const u8 *data, size_t size)
{
	fed_part_t *parts = 0;
	uint n = 0;
	if (fed_parse_container (data, size, "FEDS", &parts, &n) != ERR_OK)
		return 0;
	model_t *model = CALLOC (1, sizeof (*model));
	if (!model)
	{
		FREE (parts);
		return 0;
	}
	if (!fed_parse_joints (parts, n, model))
	{
		FREE (parts);
		FreeModel (model);
		return 0;
	}
	FREE (parts);
	return model;
}

//-----------------------------------------------------------------------------
// Model: B000 family -> model_t (unskinned geometry + joints when present,
// mirroring the MPR SMDL "no skeleton oracle" discipline).
//-----------------------------------------------------------------------------

typedef struct
{
	u32 hash;
	u16 index_count;
	u16 index_format; // 0x8000 = u8, else u16
	u32 vert_ptr_off; // byte offset into B004 table
	u32 mat_hash;
	u32 tm_index;
	u32 variant_hash;
	u16 vert_count;
	u8 header_size;
	const u32 *mat_pointers;
	uint n_mat_pointers;
	u32 vert_start; // resolved from B004
} fed_mesh_hdr_t;

model_t *ParseFedForceModel (const u8 *data, size_t size)
{
	fed_part_t *parts = 0;
	uint n = 0;
	if (fed_parse_container (data, size, "FEDM", &parts, &n) != ERR_OK)
		return 0;
	const fed_part_t *b003 = fed_find_part (parts, n, FED_CHUNK_MESH_INFO);
	const fed_part_t *b004 = fed_find_part (parts, n, FED_CHUNK_VERT_START);
	const fed_part_t *b005 = fed_find_part (parts, n, FED_CHUNK_MESH_BUFFERS);
	if (!b003 || !b004 || !b005 || !b003->size || !b005->size)
	{
		FREE (parts);
		return 0;
	}
	// Walk B003: (Header hash/u16 count/u16 size/u32 pad)+ per mesh.
	typedef struct
	{
		u32 hash;
		u32 mesh_start, mesh_end; // indices into meshes[]
	} fed_model_hdr_t;
	fed_model_hdr_t *models = 0;
	uint n_models = 0, cap_models = 0;
	fed_mesh_hdr_t *meshes = 0;
	uint n_meshes = 0, cap_meshes = 0;
	size_t p = 0;
	while (p + 12 <= b003->size)
	{
		u32 mhash = rd_le32 (b003->data + p);
		u16 mcount = rd_le16 (b003->data + p + 4);
		u16 msize = rd_le16 (b003->data + p + 6);
		if (!mcount || !msize || p + msize > b003->size)
			break;
		if (n_models >= cap_models)
		{
			uint nc = cap_models ? cap_models * 2 : 4;
			fed_model_hdr_t *nn = REALLOC (models, nc * sizeof (*nn));
			if (!nn)
				goto fail;
			models = nn;
			cap_models = nc;
		}
		models[n_models].hash = mhash;
		models[n_models].mesh_start = n_meshes;
		size_t q = p + 12;
		for (uint m = 0; m < mcount; m++)
		{
			if (q + 40 > p + msize)
				goto fail;
			const u8 *mh = b003->data + q;
			u8 hsize = mh[39]; // HeaderSize trails Flag at mh[38]
			if (hsize < 40 || q + hsize > p + msize)
				goto fail;
			if (n_meshes >= cap_meshes)
			{
				uint nc = cap_meshes ? cap_meshes * 2 : 8;
				if (nc > FED_MAX_MESHES)
					goto fail;
				fed_mesh_hdr_t *nn = REALLOC (meshes, nc * sizeof (*nn));
				if (!nn)
					goto fail;
				meshes = nn;
				cap_meshes = nc;
			}
			fed_mesh_hdr_t *d = &meshes[n_meshes++];
			memset (d, 0, sizeof (*d));
			{
				// Mesh struct field order per dumper Model_B000.Mesh.
				// HeaderSize trails Flag at mh[38].
				d->hash = rd_le32 (mh + 4);
				uint index_off = rd_le32 (mh + 8);
				d->index_count = rd_le16 (mh + 12);
				d->index_format = rd_le16 (mh + 14);
				d->vert_ptr_off = rd_le32 (mh + 16);
				d->mat_hash = rd_le32 (mh + 20);
				d->tm_index = rd_le32 (mh + 24);
				d->variant_hash = rd_le32 (mh + 32);
				d->vert_count = rd_le16 (mh + 36);
				d->header_size = mh[39];
				d->vert_start = index_off; // temp hold; resolved below
				uint nptr = (hsize - 40) / 4;
				if (nptr)
				{
					if (q + 40 + nptr * 4 > p + msize)
						goto fail;
					d->mat_pointers = (const u32 *)(mh + 40);
					d->n_mat_pointers = nptr;
				}
			}
			q += hsize;
		}
		models[n_models].mesh_end = n_meshes;
		n_models++;
		p += msize ? msize : 12;
	}
	if (!n_models || !n_meshes)
		goto fail;
	// Resolve vertex starts via B004 table.
	for (uint i = 0; i < n_meshes; i++)
	{
		uint tab_off = meshes[i].vert_ptr_off;
		uint index_off = meshes[i].vert_start;
		meshes[i].vert_start = 0;
		if (tab_off + 4 > b004->size)
			goto fail;
		u32 vs = rd_le32 (b004->data + tab_off);
		if (vs >= b005->size)
			goto fail;
		meshes[i].vert_start = vs;
		// restore index offset into a side table
		meshes[i].vert_ptr_off = index_off;
	}
	// Material map 0xBFF0 (optional): per-mesh diffuse hashes.
	const fed_part_t *mmap = fed_find_part (parts, n, 0xBFF0);
	const u8 *b006 = 0;
	uint b006_size = 0;
	{
		const fed_part_t *b6 = fed_find_part (parts, n, FED_CHUNK_MATERIAL_DATA);
		if (b6)
		{
			b006 = b6->data;
			b006_size = b6->size;
		}
	}
	// Build model_t: one mesh per FedForce mesh, one material per mesh.
	model_t *model = CALLOC (1, sizeof (*model));
	if (!model)
		goto fail;
	model->meshes = CALLOC (n_meshes, sizeof (mesh_t));
	model->materials = CALLOC (n_meshes, sizeof (material_t));
	if (!model->meshes || !model->materials)
	{
		if (model->meshes)
			FREE (model->meshes);
		if (model->materials)
			FREE (model->materials);
		FREE (model);
		goto fail;
	}
	model->num_meshes = n_meshes;
	model->num_materials = n_meshes;
	uint out_mesh = 0;
	for (uint mi = 0; mi < n_models; mi++)
	{
		for (uint i = models[mi].mesh_start; i < models[mi].mesh_end; i++)
		{
			fed_mesh_hdr_t *s = &meshes[i];
			// Stride from material hash, fallback to variant hash.
			int stride = fed_stride_for_hash (s->mat_hash);
			u32 stride_hash = s->mat_hash;
			if (!stride && s->variant_hash)
			{
				stride = fed_stride_for_hash (s->variant_hash);
				stride_hash = s->variant_hash;
			}
			if (!stride || !s->vert_count || !s->index_count)
			{
				// Unknown material layout: skip the mesh entirely (and its
				// material slot stays unfilled -- counts are compacted via
				// out_mesh below), mirroring the MPR CMDL discipline of
				// emitting no geometry rather than wrong geometry.
				continue;
			}
			if ((u64)s->vert_start + (u64)s->vert_count * (u64)stride > b005->size)
				goto fail_model;
			uint index_off = s->vert_ptr_off;
			uint idx_size = (s->index_format == 0x8000) ? 1 : 2;
			if ((u64)index_off + (u64)s->index_count * idx_size > b005->size)
				goto fail_model;
			mesh_t *dm = &model->meshes[out_mesh];
			material_t *mt = &model->materials[out_mesh];
			char mhex[16], vhex[16];
			ccp mn = FedForceHashName (s->mat_hash, mhex);
			snprintf (dm->name, sizeof (dm->name), "mesh_%u_%s", out_mesh, mn ? mn : mhex);
			ccp vn = FedForceHashName (s->variant_hash, vhex);
			snprintf (mt->name, sizeof (mt->name), "%s", vn ? vn : (mn ? mn : mhex));
			mt->num_textures = 0;
			// Diffuse hash: material map first, else B006 pointer heuristic
			// (first pointer whose u32 target is nonzero), else none.
			u32 dif = 0;
			if (mmap && mmap->size >= 4)
			{
				uint nm = rd_le32 (mmap->data);
				if (4 + (size_t)i * 4 + 4 <= mmap->size && i < nm)
					dif = rd_le32 (mmap->data + 4 + (size_t)i * 4);
			}
			if (!dif && b006 && s->mat_pointers)
			{
				for (uint k = 0; k < s->n_mat_pointers; k++)
				{
					u32 ptr = rd_le32 ((const u8 *)&s->mat_pointers[k]);
					if (ptr == 0 || ptr == 0xFFFFFFFF || ptr + 4 > b006_size)
						continue;
					u32 cand = rd_le32 (b006 + ptr);
					if (cand && cand != 0xFFFFFFFF)
					{
						dif = cand;
						break;
					}
				}
			}
			if (dif)
			{
				char thex[16];
				ccp tn = FedForceHashName (dif, thex);
				snprintf (
					mt->textures[0], sizeof (mt->textures[0]), "%s.fedtex.png", tn ? tn : thex);
				mt->num_textures = 1;
				mt->texture_coord[0] = 0;
				mt->wrap_s[0] = mt->wrap_t[0] = 1;
				mt->min_filter[0] = mt->mag_filter[0] = 1;
			}
			dm->material_idx = (int)out_mesh;
			// Vertices.
			dm->num_positions = s->vert_count;
			dm->positions = CALLOC (s->vert_count, sizeof (vec3_t));
			dm->position_node = CALLOC (s->vert_count, sizeof (int));
			if (!dm->positions || !dm->position_node)
				goto fail_model;
			bool have_n = false, have_uv = false, have_c = false, have_uv2 = false;
			// First pass: detect channels.
			for (uint vxi = 0; vxi < s->vert_count; vxi++)
			{
				fed_vertex_t fv;
				const u8 *vp = b005->data + s->vert_start + (size_t)vxi * (uint)stride;
				if (!fed_decode_vertex (&fv, vp, b005->size - (vp - b005->data), stride_hash))
					goto fail_model;
				if (fv.has_normal)
					have_n = true;
				if (fv.has_uv0)
					have_uv = true;
				if (fv.has_uv2)
					have_uv2 = true;
				if (fv.has_color)
					have_c = true;
			}
			if (have_n)
			{
				dm->normals = CALLOC (s->vert_count, sizeof (vec3_t));
				dm->num_normals = s->vert_count;
			}
			if (have_uv)
			{
				dm->texcoords = CALLOC (s->vert_count, sizeof (vec2_t));
				dm->num_texcoords = s->vert_count;
			}
			if (have_uv2)
			{
				dm->extra_texcoords[0] = CALLOC (s->vert_count, sizeof (vec2_t));
				dm->num_extra_texcoords[0] = s->vert_count;
			}
			if (have_c)
			{
				dm->colors[0] = CALLOC (s->vert_count, sizeof (color4_t));
				dm->num_colors[0] = s->vert_count;
			}
			dm->vertices = CALLOC (s->vert_count, sizeof (vertex_t));
			dm->num_vertices = s->vert_count;
			if (!dm->vertices)
				goto fail_model;
			if ((have_n && !dm->normals) || (have_uv && !dm->texcoords)
				|| (have_uv2 && !dm->extra_texcoords[0]) || (have_c && !dm->colors[0]))
				goto fail_model;
			for (uint vxi = 0; vxi < s->vert_count; vxi++)
			{
				fed_vertex_t fv;
				const u8 *vp = b005->data + s->vert_start + (size_t)vxi * (uint)stride;
				fed_decode_vertex (&fv, vp, b005->size - (vp - b005->data), stride_hash);
				if (!isfinite (fv.px) || !isfinite (fv.py) || !isfinite (fv.pz))
				{
					fv.px = fv.py = fv.pz = 0.0f;
				}
				dm->positions[vxi].x = fv.px;
				dm->positions[vxi].y = fv.py;
				dm->positions[vxi].z = fv.pz;
				dm->position_node[vxi] = -1;
				vertex_t *vx = &dm->vertices[vxi];
				vx->position_idx = (int)vxi;
				vx->matrix_idx = -1;
				vx->texcoord_idx = -1;
				vx->normal_idx = -1;
				vx->tangent_idx = -1;
				vx->color_idx[0] = vx->color_idx[1] = -1;
				for (int e = 0; e < 7; e++)
					vx->extra_texcoord_idx[e] = -1;
				if (have_n)
				{
					float nl = sqrtf (fv.nx * fv.nx + fv.ny * fv.ny + fv.nz * fv.nz);
					if (nl > 1e-6f && isfinite (nl))
					{
						dm->normals[vxi].x = fv.nx / nl;
						dm->normals[vxi].y = fv.ny / nl;
						dm->normals[vxi].z = fv.nz / nl;
					}
					else
					{
						dm->normals[vxi].x = 0.0f;
						dm->normals[vxi].y = 1.0f;
						dm->normals[vxi].z = 0.0f;
					}
					vx->normal_idx = (int)vxi;
				}
				if (have_uv)
				{
					dm->texcoords[vxi].u = fv.u0;
					dm->texcoords[vxi].v = fv.v0;
					vx->texcoord_idx = (int)vxi;
				}
				if (have_uv2)
				{
					dm->extra_texcoords[0][vxi].u = fv.u2;
					dm->extra_texcoords[0][vxi].v = fv.v2;
					vx->extra_texcoord_idx[0] = (int)vxi;
				}
				if (have_c)
				{
					dm->colors[0][vxi].r = fv.r;
					dm->colors[0][vxi].g = fv.g;
					dm->colors[0][vxi].b = fv.b;
					dm->colors[0][vxi].a = fv.a;
					vx->color_idx[0] = (int)vxi;
				}
			}
			// Indices (triangles assumed; exporter handles non-multiple).
			{
				uint nic = s->index_count;
				uint ntri = nic / 3 * 3;
				size_t nv = (size_t)ntri;
				// Reuse vertices array as triangle soup: append index redirection
				// by rewriting vertex entries per index.
				vertex_t *tri = CALLOC (nv ? nv : 1, sizeof (*tri));
				if (!tri)
					goto fail_model;
				for (uint k = 0; k < ntri; k++)
				{
					uint idx = (s->index_format == 0x8000)
						? b005->data[index_off + k]
						: rd_le16 (b005->data + index_off + (size_t)k * 2);
					if (idx >= s->vert_count)
						idx %= s->vert_count ? s->vert_count : 1;
					tri[k] = dm->vertices[idx];
				}
				FREE (dm->vertices);
				dm->vertices = tri;
				dm->num_vertices = nv;
			}
			out_mesh++;
			continue;
		fail_model:
			FREE (parts);
			FREE (models);
			FREE (meshes);
			FreeModel (model);
			return 0;
		}
	}
	FREE (models);
	FREE (meshes);
	// Joints when skeleton chunks ride along (same-hash skeleton file).
	fed_parse_joints (parts, n, model);
	FREE (parts);
	if (!out_mesh)
	{
		FreeModel (model);
		return 0;
	}
	// Skipped meshes (unknown layouts) leave trailing slots unfilled.
	model->num_meshes = out_mesh;
	model->num_materials = out_mesh;
	return model;
fail:
	FREE (parts);
	FREE (models);
	FREE (meshes);
	return 0;
}

//-----------------------------------------------------------------------------
// NLOC localization. BE form has an "NLOC" magic and a 20-byte header;
// the FedForce LE form has no magic (version2): the first u32 is the
// language id, then encoding/language/count/unknown, with entries at
// file offset 0x14 -- ported exactly from the dumper's NlocFile.Read,
// with bounds checks the reference lacks.
//-----------------------------------------------------------------------------

bool IsFedForceNLOC (const u8 *data, size_t size)
{
	if (!data || size < 0x14)
		return false;
	if (!memcmp (data, "NLOC", 4))
	{
		u32 n = rd_be32 (data + 12);
		return n > 0 && n < 100000 && 0x14 + (size_t)n * 8 <= size;
	}
	// LE version2 probe: lang/enc/lang/count/unk plausible + entries fit.
	u32 enc = rd_le32 (data + 4);
	u32 n = rd_le32 (data + 8);
	if ((enc != 1 && enc != 2 && enc != 0x01000000 && enc != 0x02000000) || n == 0 || n > 100000)
		return false;
	if (0x14 + (size_t)n * 8 > size)
		return false;
	return true;
}

static u16 fed_rd_u16e (const u8 *p, bool be)
{
	return be ? rd_be16 (p) : rd_le16 (p);
}

static u32 fed_rd_u32e (const u8 *p, bool be)
{
	return be ? rd_be32 (p) : rd_le32 (p);
}

enumError ScanFedForceNLOC (
	const u8 *data, size_t size, fed_nloc_msg_t **msgs, uint *n_msgs, u32 *language_id)
{
	if (msgs)
		*msgs = 0;
	if (n_msgs)
		*n_msgs = 0;
	if (!data || size < 0x14)
		return EINVAL;
	bool be = true, v2 = false;
	uint enc = 1, lang = 0, n = 0;
	size_t entries_at = 0x14, data_start = 0;
	if (!memcmp (data, "NLOC", 4))
	{
		enc = rd_be32 (data + 4);
		if (enc == 0x01000000)
		{
			enc = 1;
			be = false;
		}
		else if (enc != 1 && enc != 2)
			return EINVAL;
		lang = fed_rd_u32e (data + 8, be);
		n = fed_rd_u32e (data + 12, be);
		entries_at = 0x14;
		data_start = 0x14 + (size_t)n * 8;
	}
	else
	{
		// version2: u32 lang, u32 enc, u32 lang, u32 count, u32 unk.
		v2 = true;
		enc = rd_be32 (data + 4);
		if (enc == 0x01000000)
		{
			enc = 1;
			be = false;
		}
		else if (enc == 0x02000000)
		{
			enc = 2;
			be = false;
		}
		else if (enc != 1 && enc != 2)
			return EINVAL;
		lang = fed_rd_u32e (data + 8, be);
		n = fed_rd_u32e (data + 12, be);
		entries_at = 0x14;
		data_start = 0x0C + (size_t)n * 8;
	}
	if (!n || n > 100000 || entries_at + (size_t)n * 8 > size)
		return EINVAL;
	(void)v2;
	fed_nloc_msg_t *out = CALLOC (n, sizeof (*out));
	if (!out)
		return ERR_CANT_CREATE;
	for (uint i = 0; i < n; i++)
	{
		const u8 *e = data + entries_at + (size_t)i * 8;
		u32 id = fed_rd_u32e (e, be);
		u32 off = fed_rd_u32e (e + 4, be);
		out[i].id = id;
		size_t sp = data_start + (size_t)off * 2;
		if (sp >= size)
		{
			for (uint k = 0; k < i; k++)
				FREE (out[k].text);
			FREE (out);
			return EINVAL;
		}
		// Decode UTF-16 (enc 1) or UTF-32 (enc 2) to UTF-8.
		char *buf = 0;
		size_t cap = 0, len = 0;
		size_t q = sp;
		for (uint guard = 0; guard < 1000000; guard++)
		{
			u32 cp;
			if (enc == 2)
			{
				if (q + 4 > size)
					break;
				cp = fed_rd_u32e (data + q, be);
				q += 4;
			}
			else
			{
				if (q + 2 > size)
					break;
				cp = fed_rd_u16e (data + q, be);
				q += 2;
				if (cp >= 0xD800 && cp <= 0xDBFF && q + 2 <= size)
				{
					u32 lo = fed_rd_u16e (data + q, be);
					if (lo >= 0xDC00 && lo <= 0xDFFF)
					{
						cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
						q += 2;
					}
				}
			}
			if (!cp)
				break;
			if (cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF))
				cp = 0xFFFD;
			char tmp[4];
			int tl = 0;
			if (cp < 0x80)
			{
				tmp[0] = (char)cp;
				tl = 1;
			}
			else if (cp < 0x800)
			{
				tmp[0] = (char)(0xC0 | (cp >> 6));
				tmp[1] = (char)(0x80 | (cp & 0x3F));
				tl = 2;
			}
			else if (cp < 0x10000)
			{
				tmp[0] = (char)(0xE0 | (cp >> 12));
				tmp[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
				tmp[2] = (char)(0x80 | (cp & 0x3F));
				tl = 3;
			}
			else
			{
				tmp[0] = (char)(0xF0 | (cp >> 18));
				tmp[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
				tmp[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
				tmp[3] = (char)(0x80 | (cp & 0x3F));
				tl = 4;
			}
			if (len + (size_t)tl + 1 > cap)
			{
				size_t nc = cap ? cap * 2 : 64;
				while (nc < len + (size_t)tl + 1)
					nc *= 2;
				char *nb = REALLOC (buf, nc);
				if (!nb)
				{
					FREE (buf);
					for (uint k = 0; k < i; k++)
						FREE (out[k].text);
					FREE (out);
					return ERR_CANT_CREATE;
				}
				buf = nb;
				cap = nc;
			}
			memcpy (buf + len, tmp, tl);
			len += tl;
		}
		if (!buf)
		{
			buf = MALLOC (1);
			if (!buf)
			{
				for (uint k = 0; k < i; k++)
					FREE (out[k].text);
				FREE (out);
				return ERR_CANT_CREATE;
			}
			buf[0] = 0;
		}
		else
			buf[len] = 0;
		out[i].text = buf;
	}
	if (msgs)
		*msgs = out;
	else
		FreeFedForceNLOC (out, n);
	if (n_msgs)
		*n_msgs = n;
	if (language_id)
		*language_id = lang;
	return ERR_OK;
}

void FreeFedForceNLOC (fed_nloc_msg_t *msgs, uint n)
{
	if (!msgs)
		return;
	for (uint i = 0; i < n; i++)
		FREE (msgs[i].text);
	FREE (msgs);
}

enumError FedForceNLOCToText (u8 **dest, uint *dest_size, const fed_nloc_msg_t *msgs, uint n)
{
	if (!dest || !dest_size)
		return EINVAL;
	size_t total = 0;
	for (uint i = 0; i < n; i++)
		total += 9 + 1 + (msgs[i].text ? strlen (msgs[i].text) : 0) + 2;
	u8 *out = MALLOC (total + 1);
	if (!out)
		return ERR_CANT_CREATE;
	size_t p = 0;
	for (uint i = 0; i < n; i++)
	{
		char hex[16];
		ccp nm = FedForceHashName (msgs[i].id, hex);
		p += snprintf ((char *)out + p, total + 1 - p, "%08X %s\n%s\n\n", msgs[i].id, nm ? nm : hex,
			msgs[i].text ? msgs[i].text : "");
	}
	*dest = out;
	*dest_size = (uint)p;
	return ERR_OK;
}

enumError FedForceTextToNLOC (
	u8 **dest, uint *dest_size, const u8 *text, uint text_size, u32 language_id, bool big_endian)
{
	if (!dest || !dest_size)
		return EINVAL;
	// Parse "XXXXXXXX label\\ntext\\n\\n..." records (label ignored).
	typedef struct
	{
		u32 id;
		char *txt;
	} rec_t;
	rec_t *recs = 0;
	uint n = 0, cap = 0;
	const u8 *p = text ? text : (const u8 *)"";
	const u8 *end = p + text_size;
	while (p < end)
	{
		while (p < end && (*p == '\n' || *p == '\r'))
			p++;
		if (p >= end)
			break;
		if (end - p < 8)
			break;
		char idb[9];
		memcpy (idb, p, 8);
		idb[8] = 0;
		char *e = 0;
		u32 id = (u32)strtoul (idb, &e, 16);
		if (!e || e != idb + 8)
			break;
		while (p < end && *p != '\n')
			p++;
		if (p < end)
			p++;
		const u8 *ts = p;
		// Text runs to a blank line or end.
		const u8 *te = ts;
		while (te < end)
		{
			const u8 *nl = memchr (te, '\n', end - te);
			if (!nl)
			{
				te = end;
				break;
			}
			const u8 *nl2 = nl + 1;
			while (nl2 < end && (*nl2 == '\r'))
				nl2++;
			if (nl2 < end && (*nl2 == '\n' || (nl2 + 1 < end && *nl2 == '\r' && nl2[1] == '\n')))
			{
				te = nl;
				break;
			}
			// Blank line = line with only whitespace.
			bool blank = true;
			for (const u8 *q = nl + 1; q < end && *q != '\n'; q++)
				if (*q != '\r' && *q != ' ' && *q != '\t')
				{
					blank = false;
					break;
				}
			if (blank && nl + 1 < end)
			{
				te = nl;
				break;
			}
			te = nl + 1;
		}
		while (te > ts && (te[-1] == '\n' || te[-1] == '\r'))
			te--;
		if (n >= cap)
		{
			uint nc = cap ? cap * 2 : 8;
			rec_t *nn = REALLOC (recs, nc * sizeof (*nn));
			if (!nn)
			{
				for (uint k = 0; k < n; k++)
					FREE (recs[k].txt);
				FREE (recs);
				return ERR_CANT_CREATE;
			}
			recs = nn;
			cap = nc;
		}
		size_t tl = te - ts;
		char *cp = MALLOC (tl + 1);
		if (!cp)
		{
			for (uint k = 0; k < n; k++)
				FREE (recs[k].txt);
			FREE (recs);
			return ERR_CANT_CREATE;
		}
		memcpy (cp, ts, tl);
		cp[tl] = 0;
		recs[n].id = id;
		recs[n].txt = cp;
		n++;
		p = te;
	}
	// Encode UTF-8 -> UTF-16 units (no surrogates beyond BMP+BMP pairs).
	size_t units = 0;
	u16 **enc = CALLOC (n ? n : 1, sizeof (*enc));
	size_t *ulen = CALLOC (n ? n : 1, sizeof (*ulen));
	if ((n && (!enc || !ulen)))
	{
		FREE (enc);
		FREE (ulen);
		for (uint k = 0; k < n; k++)
			FREE (recs[k].txt);
		FREE (recs);
		return ERR_CANT_CREATE;
	}
	for (uint i = 0; i < n; i++)
	{
		const u8 *s = (const u8 *)recs[i].txt;
		size_t sl = strlen (recs[i].txt);
		u16 *u = MALLOC ((sl + 1) * 2 * sizeof (u16));
		if (!u)
		{
			for (uint k = 0; k < n; k++)
			{
				FREE (recs[k].txt);
				if (enc[k])
					FREE (enc[k]);
			}
			FREE (recs);
			FREE (enc);
			FREE (ulen);
			return ERR_CANT_CREATE;
		}
		size_t ul = 0;
		for (size_t k = 0; k < sl;)
		{
			u32 cp;
			u8 c = s[k];
			if (c < 0x80)
			{
				cp = c;
				k += 1;
			}
			else if ((c & 0xE0) == 0xC0 && k + 1 < sl)
			{
				cp = ((u32)(c & 0x1F) << 6) | (s[k + 1] & 0x3F);
				k += 2;
			}
			else if ((c & 0xF0) == 0xE0 && k + 2 < sl)
			{
				cp = ((u32)(c & 0x0F) << 12) | ((u32)(s[k + 1] & 0x3F) << 6) | (s[k + 2] & 0x3F);
				k += 3;
			}
			else if ((c & 0xF8) == 0xF0 && k + 3 < sl)
			{
				cp = ((u32)(c & 0x07) << 18) | ((u32)(s[k + 1] & 0x3F) << 12)
					| ((u32)(s[k + 2] & 0x3F) << 6) | (s[k + 3] & 0x3F);
				k += 4;
			}
			else
			{
				cp = 0xFFFD;
				k += 1;
			}
			if (cp >= 0x10000)
			{
				cp -= 0x10000;
				u[ul++] = (u16)(0xD800 + (cp >> 10));
				u[ul++] = (u16)(0xDC00 + (cp & 0x3FF));
			}
			else
				u[ul++] = (u16)cp;
		}
		u[ul++] = 0;
		enc[i] = u;
		ulen[i] = ul;
		units += ul;
	}
	size_t hdr = 0x14;
	size_t total = hdr + (size_t)n * 8 + units * 2;
	if (total > FED_MAX_OUTPUT)
	{
		for (uint k = 0; k < n; k++)
		{
			FREE (recs[k].txt);
			FREE (enc[k]);
		}
		FREE (recs);
		FREE (enc);
		FREE (ulen);
		return EFBIG;
	}
	u8 *out = CALLOC (1, total);
	if (!out)
	{
		for (uint k = 0; k < n; k++)
		{
			FREE (recs[k].txt);
			FREE (enc[k]);
		}
		FREE (recs);
		FREE (enc);
		FREE (ulen);
		return ERR_CANT_CREATE;
	}
	if (big_endian)
	{
		memcpy (out, "NLOC", 4);
		wr_be32 (out + 4, 1);
		wr_be32 (out + 8, language_id);
		wr_be32 (out + 12, n);
		wr_be32 (out + 16, 0);
	}
	else
	{
		wr_le32 (out, language_id);
		wr_le32 (out + 4, 1);
		wr_le32 (out + 8, language_id);
		wr_le32 (out + 12, n);
		wr_le32 (out + 16, 0);
	}
	size_t sp = hdr + (size_t)n * 8;
	for (uint i = 0; i < n; i++)
	{
		u32 off = (u32)((sp - hdr - (size_t)n * 8) / 2);
		if (big_endian)
		{
			wr_be32 (out + hdr + (size_t)i * 8, recs[i].id);
			wr_be32 (out + hdr + (size_t)i * 8 + 4, off);
		}
		else
		{
			wr_le32 (out + hdr + (size_t)i * 8, recs[i].id);
			wr_le32 (out + hdr + (size_t)i * 8 + 4, off);
		}
		for (size_t k = 0; k < ulen[i]; k++)
		{
			if (big_endian)
				wr_be16 (out + sp + k * 2, enc[i][k]);
			else
			{
				out[sp + k * 2] = (u8)enc[i][k];
				out[sp + k * 2 + 1] = (u8)(enc[i][k] >> 8);
			}
		}
		sp += ulen[i] * 2;
	}
	for (uint k = 0; k < n; k++)
	{
		FREE (recs[k].txt);
		FREE (enc[k]);
	}
	FREE (recs);
	FREE (enc);
	FREE (ulen);
	*dest = out;
	*dest_size = (uint)total;
	return ERR_OK;
}

//-----------------------------------------------------------------------------
// NLG font description text.
//-----------------------------------------------------------------------------

bool IsFedForceFont (const u8 *data, size_t size)
{
	if (!data || !size)
		return false;
	if (size >= 18 && !memcmp (data, "NLG Font Description", 20 - 2))
		return true;
	// Scan the first 512 bytes for a "Glyph " line.
	size_t scan = size < 512 ? size : 512;
	for (size_t i = 0; i + 6 < scan; i++)
		if (!memcmp (data + i, "\nGlyph ", 7))
			return true;
	if (scan >= 6 && !memcmp (data, "Glyph ", 6))
		return true;
	return false;
}
