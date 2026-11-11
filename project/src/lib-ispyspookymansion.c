// SPDX-License-Identifier: GPL-2.0+
// "I Spy Spooky Mansion" (Wii) formats -- see lib-ispyspookymansion.h for
// exactly what is and is not understood about each of them.

#include "lib-ispyspookymansion.h"
#include "lib-nintendo.h"
#include <string.h>
#include <ctype.h>
#include <math.h>

//-----------------------------------------------------------------------------
// shared helpers

static int is_printable_name (const u8 *data, size_t size, u32 ofs)
{
	if (ofs >= size)
		return 0;
	size_t j = ofs;
	int len = 0;
	while (j < size && data[j])
	{
		if (data[j] < 0x20 || data[j] >= 0x7f)
			return 0;
		j++;
		len++;
		if (len > 4096)
			return 0;
	}
	return j < size; // must be NUL-terminated in-bounds
}

static void print_cstr (FILE *f, const u8 *data, size_t size, u32 ofs)
{
	if (ofs >= size)
		return;
	size_t j = ofs;
	while (j < size && data[j])
		j++;
	fwrite (data + ofs, 1, j - ofs, f);
}

//-----------------------------------------------------------------------------
// (1) ".eid" sound-event/effect table -- shared "VARS" field-record helper

enum
{
	EID_EFF_ENTRY_SIZE = 20,
	EID_VARS_HEADER_SIZE = 12,
	EID_FIELD_RECORD_SIZE = 16,
};

// Decodes one "VARS"-tagged field-record table at absolute offset [vars_ofs].
// Shared between .eid effect blocks and .sdf TYPE-record payloads, since
// both use the exact same layout (confirmed byte-for-byte identical).
static enumError decode_vars_block (FILE *f, const u8 *data, size_t size, u32 vars_ofs, int indent)
{
	if ((u64)vars_ofs + EID_VARS_HEADER_SIZE > size || memcmp (data + vars_ofs, "VARS", 4))
	{
		fprintf (f, "%*s# 'VARS' block at 0x%x invalid/out-of-bounds -- not decoded\n", indent, "",
			vars_ofs);
		return ERR_OK;
	}

	u32 field_count = rd_be32 (data + vars_ofs + 4);
	u32 ro = vars_ofs + EID_VARS_HEADER_SIZE;

	fprintf (f, "%*sfield_count = %u\n", indent, "", field_count);

	for (u32 i = 0; i < field_count; i++)
	{
		if ((u64)ro + EID_FIELD_RECORD_SIZE > size)
		{
			fprintf (f, "%*s# field table truncated -- not decoded further\n", indent, "");
			break;
		}

		u32 name_ofs = rd_be32 (data + ro);
		u32 type = rd_be32 (data + ro + 4);
		const u8 *tag = data + ro + 8;
		u32 value = rd_be32 (data + ro + 12);
		float valuef;
		memcpy (&valuef, &value, 4);

		fprintf (f, "%*sfield[%u]: name=\"", indent, "", i);
		if (is_printable_name (data, size, name_ofs))
			print_cstr (f, data, size, name_ofs);
		else
			fprintf (f, "?");
		fprintf (f, "\" type=0x%x tag=\"", type);
		for (int k = 0; k < 4; k++)
		{
			u8 c = tag[k];
			if (c >= 0x20 && c < 0x7f)
				fputc (c, f);
		}
		fprintf (f, "\" value=0x%x (as_float=%g)", value, valuef);

		// OGGFILE-style fields store a second string offset in `value`
		// instead of a number; print it when it resolves to one.
		if (is_printable_name (data, size, value) && value != 0)
		{
			fprintf (f, " (as_string=\"");
			print_cstr (f, data, size, value);
			fprintf (f, "\")");
		}
		fprintf (f, "\n");

		ro += EID_FIELD_RECORD_SIZE;
	}

	return ERR_OK;
}

//-----------------------------------------------------------------------------
// (1) ".eid" sound-event/effect table

int IsSpookyEid (const u8 *data, size_t size, size_t file_size)
{
	(void)file_size;
	if (!data || size < 0x14)
		return 0;
	if (memcmp (data, "EID\0", 4))
		return 0;
	if (rd_be32 (data + 4) != 1 || rd_be32 (data + 8) != 0 || rd_be32 (data + 0xc) != 0)
		return 0;

	u32 count = rd_be32 (data + 0x10);
	if ((u64)0x14 + (u64)count * EID_EFF_ENTRY_SIZE > size)
		return 0;

	// Validate the effect-table entries structurally so magic-only false
	// positives (and the one known byte-swapped outlier sample) are
	// rejected rather than mis-decoded.
	u32 o = 0x14;
	for (u32 i = 0; i < count; i++)
	{
		if (memcmp (data + o, "EFF\0", 4) || memcmp (data + o + 8, "OGG\0", 4))
			return 0;
		u32 vars_ofs = rd_be32 (data + o + 12);
		if ((u64)vars_ofs + EID_VARS_HEADER_SIZE > size || memcmp (data + vars_ofs, "VARS", 4))
			return 0;
		o += EID_EFF_ENTRY_SIZE;
	}
	return 1;
}

enumError DecodeSpookyEid_Text (FILE *f, const u8 *data, size_t size, size_t file_size)
{
	if (!f || !data || !IsSpookyEid (data, size, file_size))
		return EINVAL;

	u32 count = rd_be32 (data + 0x10);
	fprintf (f, "# I Spy Spooky Mansion sound-event/effect table (.eid)\n");
	fprintf (f, "effect_count = %u\n", count);

	u32 o = 0x14;
	for (u32 i = 0; i < count; i++)
	{
		u32 string_tab_ofs = rd_be32 (data + o + 4);
		u32 vars_ofs = rd_be32 (data + o + 12);
		fprintf (
			f, "\neffect[%u]: string_table_ofs=0x%x vars_ofs=0x%x\n", i, string_tab_ofs, vars_ofs);
		decode_vars_block (f, data, size, vars_ofs, 2);
		o += EID_EFF_ENTRY_SIZE;
	}
	return ERR_OK;
}

//-----------------------------------------------------------------------------
// (2) ".ast" "SDASSETF" asset-bundle container

enum
{
	AST_HEADER_SIZE = 16,
	AST_CHUNK_HEADER_SIZE = 16
};

// Returns 1 and sets *swapped if the 8-byte magic matches "SDASSETF"
// either directly (big-endian samples) or word-swapped (little-endian
// samples exported by the "Proteus" authoring tool). See lib-ispyspooky
// mansion.h note (2).
static int check_ast_magic (const u8 *data, size_t size, int *swapped)
{
	if (!data || size < AST_HEADER_SIZE)
		return 0;
	if (!memcmp (data, "SDASSETF", 8))
	{
		*swapped = 0;
		return 1;
	}
	u8 rev[8];
	for (int i = 0; i < 4; i++)
		rev[i] = data[3 - i];
	for (int i = 0; i < 4; i++)
		rev[4 + i] = data[7 - i];
	if (!memcmp (rev, "SDASSETF", 8))
	{
		*swapped = 1;
		return 1;
	}
	return 0;
}

static u32 ast_u32 (const u8 *data, u32 ofs, int swapped)
{
	return swapped ? rd_le32 (data + ofs) : rd_be32 (data + ofs);
}

static void ast_tag (const u8 *data, u32 ofs, int swapped, char out[5])
{
	if (swapped)
	{
		out[0] = data[ofs + 3];
		out[1] = data[ofs + 2];
		out[2] = data[ofs + 1];
		out[3] = data[ofs + 0];
	}
	else
		memcpy (out, data + ofs, 4);
	out[4] = 0;
}

static int is_ascii_tag (const char tag[5])
{
	int any = 0;
	for (int i = 0; i < 4; i++)
	{
		u8 c = (u8)tag[i];
		if (c && (c < 0x20 || c >= 0x7f))
			return 0;
		if (c)
			any = 1;
	}
	return any;
}

int IsSpookyAst (const u8 *data, size_t size, size_t file_size)
{
	(void)file_size;
	int swapped = 0;
	if (!check_ast_magic (data, size, &swapped))
		return 0;

	// Require the first top-level chunk header to look structurally valid
	// (printable tag, in-bounds size, zero reserved field) so arbitrary
	// data that happens to start with the magic isn't accepted.
	if (size < AST_HEADER_SIZE + AST_CHUNK_HEADER_SIZE)
		return 0;
	char tag[5];
	ast_tag (data, AST_HEADER_SIZE, swapped, tag);
	if (!is_ascii_tag (tag))
		return 0;
	u32 chunk_size = ast_u32 (data, AST_HEADER_SIZE + 8, swapped);
	if ((u64)AST_HEADER_SIZE + AST_CHUNK_HEADER_SIZE + chunk_size > size)
		return 0;
	return 1;
}

// Top-level chunk layout, verified on all 1598 .ast files of Goosebumps HorrorLand (832 big-endian, 766 little-endian; every file walks to EOF):
//   +0  tag (4 chars, word-swapped in the little-endian files; 3 character tags such as "MDL" and "BND" are NUL padded)
//   +4  version, +8 size of the fixed payload, +12 size of the child area
//   +16 NUL terminated resource name padded to 4 bytes, then the fixed payload (size bytes), then the child area (children bytes).
// Child chunks have a 16 byte header {tag, version, field (0 or 1), size} followed by size bytes of payload and no name.
// The top-level chunks are BMAP (children IMAG, CLUT), MTRL (EFCT), MDL (BND, INFO, LOD with MESH/WDGE/DATA), SKEL, SPLM (SPCH), LGHT, PRSP, FRCE, PLNE.
static int ast_walk_name (const u8 *data, size_t size, u32 o, u32 *name_len)
{
	u32 p = o + AST_CHUNK_HEADER_SIZE;
	while (p < size && data[p])
		p++;
	if (p >= size)
		return 0;
	*name_len = ((p - (o + AST_CHUNK_HEADER_SIZE) + 1) + 3) & ~3u;
	return 1;
}

static void ast_put_hex (FILE *f, const u8 *p, u32 n)
{
	for (u32 i = 0; i < n; i++)
		fprintf (f, "%s%02x", i ? " " : "", p[i]);
}

enumError DecodeSpookyAst_Text (FILE *f, const u8 *data, size_t size, size_t file_size)
{
	if (!f || !data || !IsSpookyAst (data, size, file_size))
		return EINVAL;

	int swapped = 0;
	check_ast_magic (data, size, &swapped);
	u32 version = ast_u32 (data, 8, swapped);
	u32 count = ast_u32 (data, 12, swapped);

	fprintf (f, "# I Spy Spooky Mansion \"SDASSETF\" asset-bundle container (.ast)\n");
	fprintf (f, "byte_order = %s\n", swapped ? "little-endian (word-swapped)" : "big-endian");
	fprintf (f, "version = %u\n", version);
	fprintf (f, "chunk_count = %u  # top-level chunk count (declared)\n", count);

	u32 o = AST_HEADER_SIZE;
	u32 n = 0;
	for (; n < count && o + AST_CHUNK_HEADER_SIZE <= size; n++)
	{
		char tag[5];
		ast_tag (data, o, swapped, tag);
		u32 name_len;
		if (!is_ascii_tag (tag) || !ast_walk_name (data, size, o, &name_len))
			break;
		const u32 cver = ast_u32 (data, o + 4, swapped);
		const u32 fixed = ast_u32 (data, o + 8, swapped);
		const u32 kids = ast_u32 (data, o + 12, swapped);
		const u64 pay = (u64)o + AST_CHUNK_HEADER_SIZE + name_len;
		const u64 end = pay + fixed + kids;
		if (end > size)
		{
			fprintf (f, "\n# chunk table stops at offset 0x%x (out-of-bounds size fields)\n", o);
			break;
		}

		fprintf (f, "\nchunk[%u]: tag=\"%s\" version=0x%x name=\"%s\" fixed_size=%u children_size=%u offset=0x%x\n", n, tag, cver, (const char *)data + o + AST_CHUNK_HEADER_SIZE, fixed, kids, o);
		if (fixed)
		{
			fprintf (f, "  fixed: ");
			ast_put_hex (f, data + pay, fixed > 64 ? 64 : fixed);
			fprintf (f, "%s\n", fixed > 64 ? " ..." : "");
		}
		u64 c = pay + fixed;
		for (u32 j = 0; c < end; j++)
		{
			char ctag[5];
			if (c + AST_CHUNK_HEADER_SIZE > end)
			{
				fprintf (f, "  # truncated child header at 0x%llx\n", (unsigned long long)c);
				break;
			}
			ast_tag (data, (u32)c, swapped, ctag);
			const u32 ccver = ast_u32 (data, (u32)c + 4, swapped);
			const u32 cfield = ast_u32 (data, (u32)c + 8, swapped);
			const u32 csize = ast_u32 (data, (u32)c + 12, swapped);
			if (!is_ascii_tag (ctag) || c + AST_CHUNK_HEADER_SIZE + csize > end)
			{
				fprintf (f, "  # child chunk at 0x%llx not understood\n", (unsigned long long)c);
				break;
			}
			fprintf (f, "  child[%u]: tag=\"%s\" version=0x%x field=%u size=%u offset=0x%llx\n", j, ctag, ccver, cfield, csize, (unsigned long long)c);
			c += AST_CHUNK_HEADER_SIZE + csize;
		}
		o = (u32)end;
	}

	if (n < count)
		fprintf (f, "\n# only %u of %u declared top-level chunks could be walked\n", n, count);
	else if (o != size)
		fprintf (f, "\n# %zu trailing bytes after the last chunk\n", size - o);

	return ERR_OK;
}

int SpookyAstForEachTexture (const u8 *data, size_t size, int (*cb) (const SpookyAstTexture_t *tex, void *ctx), void *ctx)
{
	int swapped = 0;
	if (!data || !check_ast_magic (data, size, &swapped))
		return 0;
	// The little-endian ("SADSFTES", 766 of 1598 files) bundles keep the same chunk tree but store the BMAP header fields (the format byte is not at +15 / +16) and
	// pixel data differently; decoding them as GX produces noise, so only the big-endian bundles are exported (all verified visually: CMPR, RGBA8, C4, C8).
	if (swapped)
		return 0;
	const u32 count = ast_u32 (data, 12, swapped);
	u32 o = AST_HEADER_SIZE;
	int found = 0;
	for (u32 n = 0; n < count && o + AST_CHUNK_HEADER_SIZE <= size; n++)
	{
		char tag[5];
		ast_tag (data, o, swapped, tag);
		u32 name_len;
		if (!is_ascii_tag (tag) || !ast_walk_name (data, size, o, &name_len))
			break;
		const u32 fixed = ast_u32 (data, o + 8, swapped), kids = ast_u32 (data, o + 12, swapped);
		const u64 pay = (u64)o + AST_CHUNK_HEADER_SIZE + name_len, end = pay + fixed + kids;
		if (end > size)
			break;
		if (!strcmp (tag, "BMAP") && fixed >= 20)
		{
			const u8 *fx = data + pay;
			SpookyAstTexture_t t = { (const char *)data + o + AST_CHUNK_HEADER_SIZE, swapped ? rd_le16 (fx) : rd_be16 (fx), swapped ? rd_le16 (fx + 2) : rd_be16 (fx + 2), 0, 0, 0, 0, 0 };
			const uint bits = fx[16];
			for (u64 c = pay + fixed; c + AST_CHUNK_HEADER_SIZE <= end;)
			{
				char ctag[5];
				ast_tag (data, (u32)c, swapped, ctag);
				const u32 csize = ast_u32 (data, (u32)c + 12, swapped);
				if (!is_ascii_tag (ctag) || c + AST_CHUNK_HEADER_SIZE + csize > end)
					break;
				if (csize >= 4 && !strcmp (ctag, "IMAG"))
				{
					t.pixels = data + c + AST_CHUNK_HEADER_SIZE + 4; // skips the "Rev\0" tag
					t.pixels_size = csize - 4;
				}
				else if (csize >= 4 && !strcmp (ctag, "CLUT"))
				{
					t.clut = data + c + AST_CHUNK_HEADER_SIZE + 4;
					t.clut_size = csize - 4;
				}
				c += AST_CHUNK_HEADER_SIZE + csize;
			}
			if (fx[15] == 7 && bits == 32)
				t.kind = 0x0e;
			else if (!fx[15] && bits == 32)
				t.kind = 0x06;
			else if (!fx[15] && bits == 4 && t.clut)
				t.kind = 0x08;
			else if (!fx[15] && bits == 8 && t.clut)
				t.kind = 0x09;
			size_t need = (size_t)t.width * t.height;
			need = t.kind == 0x0e || t.kind == 0x08 ? need / 2 : t.kind == 0x06 ? need * 4 : need;
			if (t.kind && t.pixels && t.width && t.height && !(t.width & 7) && !(t.height & 7) && t.width <= 4096 && t.height <= 4096 && t.pixels_size >= need)
			{
				found++;
				const int r = cb (&t, ctx);
				if (r)
					return -r;
			}
		}
		o = (u32)end;
	}
	return found;
}

//-----------------------------------------------------------------------------
// Proteus MDL models -> model_t

static float ast_f32 (const u8 *data, u32 ofs, int swapped)
{
	const u32 v = ast_u32 (data, ofs, swapped);
	float f;
	memcpy (&f, &v, 4);
	return isfinite (f) ? f : 0.0f;
}

typedef struct ast_child_t
{
	char tag[5];
	u32 field, size;
	u64 pay; // offset of the child payload
} ast_child_t;

// Reads the child chunks of the area [c, end); returns the count (0 when the area is malformed).
static uint ast_children (const u8 *data, int swapped, u64 c, u64 end, ast_child_t *out, uint cap)
{
	uint n = 0;
	while (c + AST_CHUNK_HEADER_SIZE <= end && n < cap)
	{
		ast_child_t *k = out + n;
		ast_tag (data, (u32)c, swapped, k->tag);
		k->field = ast_u32 (data, (u32)c + 8, swapped);
		k->size = ast_u32 (data, (u32)c + 12, swapped);
		if (!is_ascii_tag (k->tag) || c + AST_CHUNK_HEADER_SIZE + k->size > end)
			return 0;
		k->pay = c + AST_CHUNK_HEADER_SIZE;
		c = k->pay + k->size;
		n++;
	}
	return c == end ? n : 0;
}

static const ast_child_t *ast_find (const ast_child_t *k, uint n, const char *tag, u32 field)
{
	for (uint i = 0; i < n; i++)
		if (!strcmp (k[i].tag, tag) && k[i].field == field)
			return k + i;
	return 0;
}

typedef struct ast_material_t
{
	u32 field;
	char name[64], texture[64];
	float diffuse[4];
} ast_material_t;

#define AST_MAX_CHILDREN 4096
#define AST_MAX_MATERIALS 512

static uint ast_collect_materials (const u8 *data, size_t size, int swapped, ast_material_t *mats)
{
	const u32 count = ast_u32 (data, 12, swapped);
	u32 o = AST_HEADER_SIZE;
	uint nm = 0;
	for (u32 n = 0; n < count && o + AST_CHUNK_HEADER_SIZE <= size; n++)
	{
		char tag[5];
		ast_tag (data, o, swapped, tag);
		u32 name_len;
		if (!is_ascii_tag (tag) || !ast_walk_name (data, size, o, &name_len))
			break;
		const u32 fixed = ast_u32 (data, o + 8, swapped), kids = ast_u32 (data, o + 12, swapped);
		const u64 pay = (u64)o + AST_CHUNK_HEADER_SIZE + name_len, end = pay + fixed + kids;
		if (end > size)
			break;
		if (!strcmp (tag, "MTRL") && nm < AST_MAX_MATERIALS)
		{
			ast_material_t *m = mats + nm;
			memset (m, 0, sizeof (*m));
			snprintf (m->name, sizeof (m->name), "%s", (const char *)data + o + AST_CHUNK_HEADER_SIZE);
			for (int i = 0; i < 4; i++)
				m->diffuse[i] = fixed >= 16 ? data[pay + 8 + i] / 255.0f : 1.0f;
			ast_child_t kid[8];
			const uint nk = ast_children (data, swapped, pay + fixed, end, kid, 8);
			for (uint i = 0; i < nk; i++)
				if (!strcmp (kid[i].tag, "EFCT") && kid[i].size >= 56)
				{
					m->field = kid[i].field;
					const u8 *e = data + kid[i].pay;
					if (ast_u32 (e, 20, swapped))
						snprintf (m->texture, sizeof (m->texture), "%.31s", (const char *)e + 24);
				}
			nm++;
		}
		o = (u32)end;
	}
	return nm;
}

static uint ast_vertex_size (u32 flags)
{
	uint s = 0;
	for (int i = 0; i < 4; i++)
		switch ((flags >> (8 * i)) & 255)
		{
			case 1: case 2: s += 12; break;
			case 3: s += 4; break;
			case 4: s += 8; break;
			default: break;
		}
	return s;
}

// Builds one mesh from its DATA / WDGE / MESH children; returns false when the chunks do not fit together.
static bool ast_build_mesh (const u8 *data, size_t size, int swapped, const ast_child_t *dc, const ast_child_t *wc, const ast_child_t *mc, const ast_material_t *mats, uint nmat, mesh_t *mesh)
{
	if (dc->size < 32 || wc->size < 8 || mc->size < 4)
		return false;
	const u32 stride = ast_u32 (data, (u32)dc->pay + 8, swapped), flags = ast_u32 (data, (u32)dc->pay + 16, swapped);
	const u32 nv = ast_u32 (data, (u32)wc->pay, swapped), ni = ast_u32 (data, (u32)wc->pay + 4, swapped);
	if (!nv || !ni || nv > 0x100000 || ni > 0x400000 || ast_vertex_size (flags) > stride || (u64)32 + (u64)nv * stride > dc->size || 8 + 4 * (u64)ni > wc->size)
		return false;

	// MESH: name padded to 4, bounds (6 floats), then {material field, index count, 1, 0, 0, triangle count}
	const u8 *mp = data + mc->pay;
	u32 nl = 0;
	while (nl < mc->size && mp[nl])
		nl++;
	nl = (nl + 1 + 3) & ~3u;
	if ((u64)nl + 24 + 24 > mc->size)
		return false;
	const u32 mat_field = ast_u32 (mp, nl + 24, swapped), ntri_decl = ast_u32 (mp, nl + 24 + 20, swapped);

	vec3_t *pos = CALLOC (nv, sizeof (*pos));
	vec3_t *nrm = 0;
	vec2_t *uv = 0;
	color4_t *col = 0;
	vertex_t *vt = CALLOC ((size_t)ni * 3, sizeof (*vt)); // upper bound: a strip yields ni - 2 triangles
	if (!pos || !vt)
	{
		FREE (pos);
		FREE (vt);
		return false;
	}
	bool has_n = false, has_uv = false, has_c = false;
	for (int i = 0; i < 4; i++)
		switch ((flags >> (8 * i)) & 255)
		{
			case 2: has_n = true; break;
			case 3: has_c = true; break;
			case 4: has_uv = true; break;
		}
	if (has_n)
		nrm = CALLOC (nv, sizeof (*nrm));
	if (has_uv)
		uv = CALLOC (nv, sizeof (*uv));
	if (has_c)
		col = CALLOC (nv, sizeof (*col));
	if ((has_n && !nrm) || (has_uv && !uv) || (has_c && !col))
	{
		FREE (pos); FREE (nrm); FREE (uv); FREE (col); FREE (vt);
		return false;
	}
	for (u32 i = 0; i < nv; i++)
	{
		const u32 v = (u32)dc->pay + 32 + i * stride;
		u32 o = v;
		for (int k = 0; k < 4; k++)
			switch ((flags >> (8 * k)) & 255)
			{
				case 1:
					pos[i].x = ast_f32 (data, o, swapped); pos[i].y = ast_f32 (data, o + 4, swapped); pos[i].z = ast_f32 (data, o + 8, swapped);
					o += 12;
					break;
				case 2:
					nrm[i].x = ast_f32 (data, o, swapped); nrm[i].y = ast_f32 (data, o + 4, swapped); nrm[i].z = ast_f32 (data, o + 8, swapped);
					o += 12;
					break;
				case 3:
					col[i].r = data[o] / 255.0f; col[i].g = data[o + 1] / 255.0f; col[i].b = data[o + 2] / 255.0f; col[i].a = data[o + 3] / 255.0f;
					o += 4;
					break;
				case 4:
					uv[i].u = ast_f32 (data, o, swapped); uv[i].v = ast_f32 (data, o + 4, swapped);
					o += 8;
					break;
				default: break;
			}
	}

	// the declared triangle count tells a triangle list (3 * count == index count) from a strip
	const bool list = ntri_decl * 3 == ni;
	const u8 *ip = data + wc->pay + 8;
	size_t num = 0;
	bool ok = true;
	for (u32 t = 0; ok && (list ? t + 3 <= ni : t + 3 <= ni); t += list ? 3 : 1)
	{
		u32 i3[3] = { ast_u32 (ip, 4 * t, swapped), ast_u32 (ip, 4 * (t + 1), swapped), ast_u32 (ip, 4 * (t + 2), swapped) };
		if (!list && (t & 1))
		{
			const u32 x = i3[0];
			i3[0] = i3[1];
			i3[1] = x;
		}
		if (i3[0] >= nv || i3[1] >= nv || i3[2] >= nv)
		{
			ok = false;
			break;
		}
		if (i3[0] == i3[1] || i3[1] == i3[2] || i3[0] == i3[2])
			continue; // degenerate strip joint
		for (int j = 0; j < 3; j++)
		{
			vertex_t *v = vt + num++;
			v->position_idx = (int)i3[j];
			v->normal_idx = has_n ? (int)i3[j] : -1;
			v->tangent_idx = -1;
			v->texcoord_idx = has_uv ? (int)i3[j] : -1;
			v->matrix_idx = -1;
			v->color_idx[0] = has_c ? (int)i3[j] : -1;
			v->color_idx[1] = -1;
			for (int x = 0; x < 7; x++)
				v->extra_texcoord_idx[x] = -1;
		}
	}
	if (!ok || !num)
	{
		FREE (pos); FREE (nrm); FREE (uv); FREE (col); FREE (vt);
		return false;
	}
	snprintf (mesh->name, sizeof (mesh->name), "%s", (const char *)mp);
	mesh->positions = pos;
	mesh->num_positions = nv;
	mesh->normals = nrm;
	mesh->num_normals = nrm ? nv : 0;
	mesh->texcoords = uv;
	mesh->num_texcoords = uv ? nv : 0;
	mesh->colors[0] = col;
	mesh->num_colors[0] = col ? nv : 0;
	mesh->vertices = vt;
	mesh->num_vertices = num;
	mesh->material_idx = 0;
	for (uint i = 0; i < nmat; i++)
		if (mats[i].field == mat_field)
		{
			mesh->material_idx = (int)i;
			break;
		}
	(void)size;
	return true;
}

static void ast_free_mesh (mesh_t *m)
{
	FREE (m->positions);
	FREE (m->normals);
	FREE (m->texcoords);
	FREE (m->colors[0]);
	FREE (m->vertices);
	FREE (m->triangle_materials);
}

int SpookyAstForEachModel (const u8 *data, size_t size, int (*cb) (const char *name, model_t *model, void *ctx), void *ctx)
{
	int swapped = 0;
	if (!data || !check_ast_magic (data, size, &swapped))
		return 0;
	ast_material_t *mats = CALLOC (AST_MAX_MATERIALS, sizeof (*mats));
	ast_child_t *kid = CALLOC (AST_MAX_CHILDREN, sizeof (*kid));
	if (!mats || !kid)
	{
		FREE (mats);
		FREE (kid);
		return 0;
	}
	const uint nmat = ast_collect_materials (data, size, swapped, mats);
	const u32 count = ast_u32 (data, 12, swapped);
	u32 o = AST_HEADER_SIZE;
	int found = 0;
	for (u32 n = 0; n < count && o + AST_CHUNK_HEADER_SIZE <= size; n++)
	{
		char tag[5];
		ast_tag (data, o, swapped, tag);
		u32 name_len;
		if (!is_ascii_tag (tag) || !ast_walk_name (data, size, o, &name_len))
			break;
		const u32 fixed = ast_u32 (data, o + 8, swapped), kids = ast_u32 (data, o + 12, swapped);
		const u64 pay = (u64)o + AST_CHUNK_HEADER_SIZE + name_len, end = pay + fixed + kids;
		if (end > size)
			break;
		if (!strcmp (tag, "MDL"))
		{
			const uint nk = ast_children (data, swapped, pay + fixed, end, kid, AST_MAX_CHILDREN);
			uint nmesh = 0;
			for (uint i = 0; i < nk; i++)
				nmesh += !strcmp (kid[i].tag, "MESH");
			model_t *model = CALLOC (1, sizeof (*model));
			mesh_t *meshes = CALLOC (nmesh ? nmesh : 1, sizeof (*meshes));
			material_t *mt = CALLOC (nmat ? nmat : 1, sizeof (*mt));
			if (model && meshes && mt)
			{
				uint made = 0;
				for (uint i = 0; i < nk; i++)
				{
					if (strcmp (kid[i].tag, "MESH"))
						continue;
					const ast_child_t *d = ast_find (kid, nk, "DATA", kid[i].field), *w = ast_find (kid, nk, "WDGE", kid[i].field);
					if (d && w && ast_build_mesh (data, size, swapped, d, w, kid + i, mats, nmat, meshes + made))
						made++;
				}
				for (uint i = 0; i < nmat; i++)
				{
					material_t *m = mt + i;
					snprintf (m->name, sizeof (m->name), "%s", mats[i].name);
					memcpy (m->diffuse, mats[i].diffuse, sizeof (m->diffuse));
					if (mats[i].texture[0])
					{
						snprintf (m->textures[0], sizeof (m->textures[0]), "%s", mats[i].texture);
						m->num_textures = 1;
						m->wrap_s[0] = m->wrap_t[0] = 1;
						m->min_filter[0] = m->mag_filter[0] = 1;
					}
				}
				model->meshes = meshes;
				model->num_meshes = made;
				model->materials = mt;
				model->num_materials = nmat;
				if (made)
				{
					found++;
					const int r = cb ((const char *)data + o + AST_CHUNK_HEADER_SIZE, model, ctx);
					for (uint i = 0; i < made; i++)
						ast_free_mesh (meshes + i);
					FREE (meshes);
					FREE (mt);
					FREE (model);
					if (r)
					{
						FREE (mats);
						FREE (kid);
						return -r;
					}
					o = (u32)end;
					continue;
				}
			}
			FREE (meshes);
			FREE (mt);
			FREE (model);
		}
		o = (u32)end;
	}
	FREE (mats);
	FREE (kid);
	return found;
}

//-----------------------------------------------------------------------------
// (3) ".sdf" asset-type registry table

enum
{
	SDF_HEADER_SIZE = 16,
	SDF_TYPE_RECORD_SIZE = 12
};

int IsSpookySdf (const u8 *data, size_t size, size_t file_size)
{
	(void)file_size;
	if (!data || size < SDF_HEADER_SIZE)
		return 0;
	if (memcmp (data, "SDF\0", 4))
		return 0;
	if (rd_be32 (data + 4) != 1 || rd_be32 (data + 8) != 0)
		return 0;

	u32 count = rd_be32 (data + 0xc);
	if ((u64)SDF_HEADER_SIZE + (u64)count * SDF_TYPE_RECORD_SIZE > size)
		return 0;

	u32 o = SDF_HEADER_SIZE;
	for (u32 i = 0; i < count; i++)
	{
		if (memcmp (data + o, "TYPE", 4))
			return 0;
		o += SDF_TYPE_RECORD_SIZE;
	}
	return 1;
}

enumError DecodeSpookySdf_Text (FILE *f, const u8 *data, size_t size, size_t file_size)
{
	if (!f || !data || !IsSpookySdf (data, size, file_size))
		return EINVAL;

	u32 count = rd_be32 (data + 0xc);
	fprintf (f, "# I Spy Spooky Mansion asset-type registry table (.sdf)\n");
	fprintf (f, "type_count = %u\n", count);

	u32 o = SDF_HEADER_SIZE;
	u32 body_ofs = SDF_HEADER_SIZE + count * SDF_TYPE_RECORD_SIZE;
	for (u32 i = 0; i < count; i++)
	{
		char type_tag[5];
		memcpy (type_tag, data + o + 4, 4);
		type_tag[4] = 0;
		u32 len = rd_be32 (data + o + 8);

		fprintf (f, "\ntype[%u]: tag=\"%s\" length=%u body_ofs=0x%x\n", i, type_tag, len, body_ofs);
		if ((u64)body_ofs + EID_VARS_HEADER_SIZE <= size && !memcmp (data + body_ofs, "VARS", 4))
			decode_vars_block (f, data, size, body_ofs, 2);
		else
			fprintf (f, "  # type body is not a 'VARS' record table -- not decoded\n");

		if ((u64)body_ofs + len > size)
			break;
		body_ofs += len;
		o += SDF_TYPE_RECORD_SIZE;
	}
	return ERR_OK;
}

//-----------------------------------------------------------------------------
// (4) ".ges" Wiimote gesture recording

enum
{
	GES_SIZE = 136,
	GES_POINT_COUNT = 10
};

int IsSpookyGes (const u8 *data, size_t size, size_t file_size)
{
	(void)file_size;
	if (!data || size != GES_SIZE)
		return 0;
	if (rd_be32 (data) != GES_POINT_COUNT)
		return 0;
	if (rd_be32 (data + 4) != 1 || rd_be32 (data + 8) != 1 || rd_be32 (data + 0xc) != 1)
		return 0;
	return 1;
}

enumError DecodeSpookyGes_Text (FILE *f, const u8 *data, size_t size, size_t file_size)
{
	if (!f || !data || !IsSpookyGes (data, size, file_size))
		return EINVAL;

	fprintf (f, "# I Spy Spooky Mansion Wiimote gesture recording (.ges)\n");
	fprintf (f, "point_count = %u\n", GES_POINT_COUNT);
	fprintf (f, "dim_x = 1\ndim_y = 1\ndim_z = 1\n\n");

	for (u32 i = 0; i < GES_POINT_COUNT; i++)
	{
		u32 ofs = 0x10 + i * 12;
		u32 xb = rd_be32 (data + ofs), yb = rd_be32 (data + ofs + 4), zb = rd_be32 (data + ofs + 8);
		float x, y, z;
		memcpy (&x, &xb, 4);
		memcpy (&y, &yb, 4);
		memcpy (&z, &zb, 4);
		fprintf (f, "point[%u] = (%g, %g, %g)\n", i, x, y, z);
	}
	return ERR_OK;
}
