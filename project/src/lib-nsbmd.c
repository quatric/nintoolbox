// NSBMD ("BMD0") -- Nintendo DS model container.
//
// A BMD0 file holds an MDL0 block, which holds one or more models. Each
// model has a bone dictionary and a set of shapes ("polygons"), and each
// shape's geometry is a DS geometry-engine command list: the same packed
// FIFO command stream the hardware consumes, four command bytes per word
// followed by their parameters. Decoding that stream is what actually
// produces vertices.

#include "lib-nsbmd.h"
#include "lib-brres-model.h"
#include "lib-nintendo.h"
#include "lib-image.h"
#include "lib-std.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

typedef unsigned int uint;

//-----------------------------------------------------------------------------

static uint16_t rd16 (const uint8_t *p)
{
	return (uint16_t)p[0] | (uint16_t)p[1] << 8;
}
static uint32_t rd32 (const uint8_t *p)
{
	return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static int16_t rds16 (const uint8_t *p)
{
	return (int16_t)rd16 (p);
}
static inline int32_t rds32 (const uint8_t *p)
{
	return (int32_t)rd32 (p);
}

// DS fixed-point: positions are 1:3:12, texcoords 1:11:4.
static float fx12 (int v)
{
	return (float)v / 4096.0f;
}
static float fx4 (int v)
{
	return (float)v / 16.0f;
}
// Normals are 1:0:9 in a packed 10-bit-per-axis word.
static float fx9 (int v)
{
	if (v & 0x200)
		v -= 0x400; // sign-extend 10 bits
	return (float)v / 512.0f;
}

//-----------------------------------------------------------------------------
// Nitro "3D info list" dictionary: a fixed header, a per-entry data array and
// a table of 16-byte names.
//-----------------------------------------------------------------------------

typedef struct
{
	uint n; // entry count
	uint data_off; // offset of the first entry's data
	uint data_size; // bytes per entry
	uint names_off; // offset of the 16-byte name table
} nitro_dict_t;

static int read_dict (nitro_dict_t *d, const uint8_t *base, size_t avail)
{
	if (avail < 0x10)
		return 0;
	const uint n = base[1];
	if (!n)
		return 0;

	// header(8) + unknown block(n*4 + 4) then the info block header
	const uint unk_off = 8;
	const uint unk_size = 4 + n * 4;
	const uint info_hdr = unk_off + unk_size;
	if (info_hdr + 4 > avail)
		return 0;

	// Info block header: u16 bytes-per-entry, u16 offset from this header to
	// the name table (which is 4 + n*item_size -- the entries sit directly
	// after the 4-byte header).
	const uint item_size = rd16 (base + info_hdr);
	const uint item_off = rd16 (base + info_hdr + 2);
	if (!item_size || item_size > 0x1000)
		return 0;

	d->n = n;
	d->data_size = item_size;
	d->data_off = info_hdr + 4;
	d->names_off = info_hdr + item_off;
	if ((size_t)d->names_off + (size_t)n * 16 > avail)
		return 0;
	return 1;
}

static void dict_name (const nitro_dict_t *d, const uint8_t *base, uint i, char *out, size_t outsz)
{
	const uint8_t *p = base + d->names_off + i * 16;
	size_t n = 0;
	while (n < 16 && n + 1 < outsz && p[n])
	{
		out[n] = (char)p[n];
		n++;
	}
	out[n] = 0;
}

//-----------------------------------------------------------------------------
// DS geometry command list
//-----------------------------------------------------------------------------

typedef struct
{
	float tx, ty, tz;
	float sx, sy, sz;
} ds_bone_xf_t;

typedef struct
{
	vec3_t *pos;
	size_t n_pos, cap_pos;
	vec3_t *nrm;
	size_t n_nrm, cap_nrm;
	vec2_t *uv;
	size_t n_uv, cap_uv;
	vertex_t *vtx;
	size_t n_vtx, cap_vtx;
} geom_t;

#define GROW(arr, n, cap, type)                                                                    \
	do                                                                                             \
	{                                                                                              \
		if ((n) >= (cap))                                                                          \
		{                                                                                          \
			size_t nc = (cap) ? (cap) * 2 : 256;                                                   \
			type *np = REALLOC ((arr), nc * sizeof (type));                                        \
			if (!np)                                                                               \
				return 0;                                                                          \
			(arr) = np;                                                                            \
			(cap) = nc;                                                                            \
		}                                                                                          \
	} while (0)

static int push_vertex (geom_t *g, vec3_t p, vec3_t n, vec2_t t, int has_n, int has_t)
{
	GROW (g->pos, g->n_pos, g->cap_pos, vec3_t);
	g->pos[g->n_pos] = p;
	GROW (g->nrm, g->n_nrm, g->cap_nrm, vec3_t);
	g->nrm[g->n_nrm] = has_n ? n : (vec3_t) { 0.0f, 1.0f, 0.0f };
	GROW (g->uv, g->n_uv, g->cap_uv, vec2_t);
	g->uv[g->n_uv] = t;
	GROW (g->vtx, g->n_vtx, g->cap_vtx, vertex_t);
	g->vtx[g->n_vtx].position_idx = (int)g->n_pos;
	g->vtx[g->n_vtx].normal_idx = (int)g->n_nrm;
	g->vtx[g->n_vtx].texcoord_idx = has_t ? (int)g->n_uv : -1;
	g->n_pos++;
	g->n_nrm++;
	g->n_uv++;
	g->n_vtx++;
	return 1;
}

// Emits the triangles of one primitive run, converting quads and strips to
// triangles so the DAE writer only ever sees a triangle list.
static int emit_primitive (geom_t *g, uint prim, const vec3_t *P, const vec3_t *N, const vec2_t *T,
	const int *hasN, const int *hasT, uint count)
{
#define V(i)                                                                                       \
	do                                                                                             \
	{                                                                                              \
		if (!push_vertex (g, P[i], N[i], T[i], hasN[i], hasT[i]))                                  \
			return 0;                                                                              \
	} while (0)
	switch (prim)
	{
		case 0: // triangles
			for (uint i = 0; i + 2 < count; i += 3)
			{
				V (i);
				V (i + 1);
				V (i + 2);
			}
			break;
		case 1: // quads
			for (uint i = 0; i + 3 < count; i += 4)
			{
				V (i);
				V (i + 1);
				V (i + 2);
				V (i);
				V (i + 2);
				V (i + 3);
			}
			break;
		case 2: // triangle strip
			for (uint i = 0; i + 2 < count; i++)
			{
				if (i & 1)
				{
					V (i + 1);
					V (i);
					V (i + 2);
				}
				else
				{
					V (i);
					V (i + 1);
					V (i + 2);
				}
			}
			break;
		case 3: // quad strip
			for (uint i = 0; i + 3 < count; i += 2)
			{
				V (i);
				V (i + 1);
				V (i + 3);
				V (i);
				V (i + 3);
				V (i + 2);
			}
			break;
	}
#undef V
	return 1;
}

// Runs one display list. The stream packs four command bytes into each word;
// each command's parameters follow, in order, after the packed word.
static int run_display_list (geom_t *g, const uint8_t *d, size_t size, const ds_bone_xf_t *bones,
	uint num_bones, uint tex_w, uint tex_h)
{
	static const uint8_t nparams[0x100] = {
		[0x00] = 0,
		[0x10] = 1,
		[0x11] = 0,
		[0x12] = 1,
		[0x13] = 1,
		[0x14] = 1,
		[0x15] = 0,
		[0x16] = 16,
		[0x17] = 12,
		[0x18] = 16,
		[0x19] = 12,
		[0x1a] = 9,
		[0x1b] = 3,
		[0x1c] = 3,
		[0x20] = 1,
		[0x21] = 1,
		[0x22] = 1,
		[0x23] = 2,
		[0x24] = 1,
		[0x25] = 1,
		[0x26] = 1,
		[0x27] = 1,
		[0x28] = 1,
		[0x29] = 1,
		[0x2a] = 1,
		[0x2b] = 1,
		[0x30] = 1,
		[0x31] = 1,
		[0x32] = 1,
		[0x33] = 1,
		[0x34] = 32,
		[0x40] = 1,
		[0x41] = 0,
		[0x50] = 1,
		[0x60] = 1,
		[0x70] = 3,
		[0x71] = 2,
		[0x72] = 1,
	};

	// Current vertex state, latched between commands like the hardware does.
	vec3_t cur_p = { 0, 0, 0 }, cur_n = { 0, 0, 0 };
	vec2_t cur_t = { 0, 0 };
	int has_n = 0, has_t = 0;
	uint active_bone = 0;

#define MAXRUN 8192
	static vec3_t P[MAXRUN], N[MAXRUN];
	static vec2_t T[MAXRUN];
	static int HN[MAXRUN], HT[MAXRUN];
	uint run = 0, prim = 0, in_prim = 0;

	size_t pos = 0;
	while (pos + 4 <= size)
	{
		const uint8_t cmd[4] = { d[pos], d[pos + 1], d[pos + 2], d[pos + 3] };
		pos += 4;

		for (int c = 0; c < 4; c++)
		{
			const uint8_t op = cmd[c];
			const uint np = nparams[op];
			if (pos + (size_t)np * 4 > size)
				return 1; // truncated stream: keep what we already decoded
			const uint8_t *p = d + pos;
			pos += (size_t)np * 4;

			switch (op)
			{
				case 0x14: // MTX_RESTORE
					active_bone = rd32 (p);
					break;

				case 0x40: // BEGIN_VTXS
					prim = rd32 (p) & 3;
					run = 0;
					in_prim = 1;
					break;

				case 0x41: // END_VTXS
					if (in_prim)
						emit_primitive (g, prim, P, N, T, HN, HT, run);
					in_prim = 0;
					run = 0;
					break;

				case 0x21: // NORMAL
				{
					const uint32_t v = rd32 (p);
					cur_n.x = fx9 (v & 0x3FF);
					cur_n.y = fx9 ((v >> 10) & 0x3FF);
					cur_n.z = fx9 ((v >> 20) & 0x3FF);
					has_n = 1;
					break;
				}

				case 0x22: // TEXCOORD
					if (tex_w > 0 && tex_h > 0)
					{
						cur_t.u = (rds16 (p) / 16.0f) / (float)tex_w;
						cur_t.v = 1.0f - ((rds16 (p + 2) / 16.0f) / (float)tex_h);
					}
					else
					{
						cur_t.u = fx4 (rds16 (p)) / 32.0f;
						cur_t.v = 1.0f - (fx4 (rds16 (p + 2)) / 32.0f);
					}
					has_t = 1;
					break;

				case 0x23: // VTX_16
					cur_p.x = fx12 (rds16 (p));
					cur_p.y = fx12 (rds16 (p + 2));
					cur_p.z = fx12 (rds16 (p + 4));
					goto emit;

				case 0x24: // VTX_10
				{
					const uint32_t v = rd32 (p);
#define S10(x) (((x) & 0x200) ? (int)(x) - 0x400 : (int)(x))
					cur_p.x = (float)S10 (v & 0x3FF) / 64.0f;
					cur_p.y = (float)S10 ((v >> 10) & 0x3FF) / 64.0f;
					cur_p.z = (float)S10 ((v >> 20) & 0x3FF) / 64.0f;
#undef S10
					goto emit;
				}

				case 0x25: // VTX_XY
					cur_p.x = fx12 (rds16 (p));
					cur_p.y = fx12 (rds16 (p + 2));
					goto emit;

				case 0x26: // VTX_XZ
					cur_p.x = fx12 (rds16 (p));
					cur_p.z = fx12 (rds16 (p + 2));
					goto emit;

				case 0x27: // VTX_YZ
					cur_p.y = fx12 (rds16 (p));
					cur_p.z = fx12 (rds16 (p + 2));
					goto emit;

				case 0x28: // VTX_DIFF -- a signed 10-bit delta on each axis
				{
					const uint32_t v = rd32 (p);
#define D10(x) ((((x) & 0x200) ? (int)(x) - 0x400 : (int)(x)) / 4096.0f / 8.0f)
					cur_p.x += D10 (v & 0x3FF);
					cur_p.y += D10 ((v >> 10) & 0x3FF);
					cur_p.z += D10 ((v >> 20) & 0x3FF);
#undef D10
					goto emit;
				}

				emit:
					if (in_prim && run < MAXRUN)
					{
						vec3_t p_world = cur_p;
						if (bones && active_bone < num_bones)
						{
							p_world.x = cur_p.x * bones[active_bone].sx + bones[active_bone].tx;
							p_world.y = cur_p.y * bones[active_bone].sy + bones[active_bone].ty;
							p_world.z = cur_p.z * bones[active_bone].sz + bones[active_bone].tz;
						}
						P[run] = p_world;
						N[run] = cur_n;
						T[run] = cur_t;
						HN[run] = has_n;
						HT[run] = has_t;
						run++;
					}
					break;

				default:
					break; // matrix/material/lighting commands do not add geometry
			}
		}
	}
	if (in_prim)
		emit_primitive (g, prim, P, N, T, HN, HT, run);
	return 1;
}

//-----------------------------------------------------------------------------

// Walks a Model's RenderCommandList to recover bone parent/child hierarchy
// -- NSBMD has no direct parent-index field in the bone dictionary itself
// (unlike most other formats this project parses); the relationship is
// only recorded as a side effect of the "Multiply Current Matrix with Bone
// Matrix" render command's own parameters. Layout from
// github.com/scurest/nsbmd_docs (nsbmd_docs.txt), read directly rather
// than guessed: opcode low 5 bits select the operation, high 3 bits (0x40
// load-from-stack, 0x20 store-to-stack) add extra parameter bytes on top
// of the base set for a handful of opcodes.
static void parse_bone_hierarchy (model_t *out, const uint8_t *cmds, size_t len)
{
	if (!out || !out->joints || !out->num_joints || !cmds || !len)
		return;

	// A small stack tracking matrix IDs pushed onto the hardware stack
	// (RenderCommand 0x20 / 0x40 flag).
	uint stack[32];
	int sp = 0;
	int cur_parent = 0; // matrix 0 is root

	const uint8_t *p = cmds;
	const uint8_t *end = cmds + len;

	while (p < end)
	{
		const uint8_t op = *p++;
		const uint8_t code = op & 0x1f;

		if (op & 0x20) // Push current matrix onto stack
		{
			if (sp < 31)
				stack[sp++] = cur_parent;
		}
		if (op & 0x40) // Pop matrix from stack
		{
			const uint8_t sidx = (p < end) ? *p++ : 0;
			if (sidx < (uint)sp)
				cur_parent = stack[sidx];
		}

		switch (code)
		{
			case 0x00:
				break; // NOP
			case 0x01:
				break; // Return
			case 0x02:
				p += 1;
				break; // Node: node_id (1 byte)
			case 0x03: // MTX Mult: node_id, parent_id, flag
			{
				if (p + 3 <= end)
				{
					const uint node_id = p[0];
					const uint parent_id = p[1];
					p += 3;
					if (node_id < out->num_joints)
					{
						out->joints[node_id].parent_idx = (int)parent_id;
						cur_parent = node_id;
					}
				}
				break;
			}
			case 0x04:
				p += 2;
				break; // Material: 2 params
			case 0x05:
				p += 2;
				break; // Shape: 2 params
			case 0x08:
				break; // Scale Down: 0 params
			case 0x09:
				break; // Scale Restore: 0 params
			case 0x0b:
				break; // Scale Up: 0 params
			case 0x0c:
				p += 2;
				break;
			case 0x0d:
				p += 2;
				break;
			default:
				// Unrecognized opcode -- stop rather than risk misreading the
				// rest of the stream as garbage parameters.
				return;
		}
	}
}

//-----------------------------------------------------------------------------

model_t *ParseEarlyDSBMD (const uint8_t *data, size_t size)
{
	if (!data || size < 60)
		return NULL;

	uint8_t *allocated_data = NULL;
	if (size >= 5 && !memcmp (data, "LZ77\x10", 5))
	{
		uint dec_sz = 0;
		u8 *dec_buf = 0;
		if (DecodeLZ10LZ11 (&dec_buf, &dec_sz, data + 4, (uint)size - 4) == ERR_OK && dec_buf)
		{
			allocated_data = dec_buf;
			data = allocated_data;
			size = dec_sz;
		}
	}
	else if (size >= 4 && data[0] == 0x10)
	{
		const uint uncomp_len = (uint)data[1] | (uint)data[2] << 8 | (uint)data[3] << 16;
		if (uncomp_len > size && uncomp_len < 0x2000000)
		{
			uint dec_sz = 0;
			u8 *dec_buf = 0;
			if (DecodeLZ10LZ11 (&dec_buf, &dec_sz, data, (uint)size) == ERR_OK && dec_buf)
			{
				allocated_data = dec_buf;
				data = allocated_data;
				size = dec_sz;
			}
		}
	}

	if (size < 60)
	{
		if (allocated_data)
			FREE (allocated_data);
		return NULL;
	}

	const uint32_t bone_count = rd32 (data + 4);
	const uint32_t bone_off = rd32 (data + 8);
	const uint32_t shapes_base = rd32 (data + 16);
	const uint32_t mat_count = rd32 (data + 20);
	const uint32_t mat_off = rd32 (data + 24);

	if (shapes_base != 0x3c || bone_off <= shapes_base || bone_off > size)
	{
		if (allocated_data)
			FREE (allocated_data);
		return NULL;
	}

	model_t *out = CALLOC (1, sizeof (model_t));
	if (!out)
	{
		if (allocated_data)
			FREE (allocated_data);
		return NULL;
	}

	// --- bones -------------------------------------------------------------
	ds_bone_xf_t *bone_xfs = NULL;
	if (bone_count > 0 && bone_off < size)
	{
		out->joints = CALLOC (bone_count, sizeof (joint_t));
		bone_xfs = CALLOC (bone_count, sizeof (ds_bone_xf_t));
		if (out->joints && bone_xfs)
		{
			out->num_joints = bone_count;
			for (uint b = 0; b < bone_count; b++)
			{
				joint_t *j = out->joints + b;
				const uint32_t boff = bone_off + b * 64;
				if (boff + 64 <= size)
				{
					uint32_t name_ptr = rd32 (data + boff + 4);
					if (name_ptr > 0 && name_ptr < size)
					{
						size_t slen = 0;
						while (name_ptr + slen < size && slen + 1 < sizeof (j->name)
							&& data[name_ptr + slen])
						{
							j->name[slen] = (char)data[name_ptr + slen];
							slen++;
						}
						j->name[slen] = 0;
					}

					const uint16_t parent_idx = rd16 (data + boff + 8);
					j->parent_idx = (parent_idx >= 0x8000) ? -1 : (int)parent_idx;

					const int32_t sx_raw = rds32 (data + boff + 16);
					const int32_t sy_raw = rds32 (data + boff + 20);
					const int32_t sz_raw = rds32 (data + boff + 24);
					const int32_t tx_raw = rds32 (data + boff + 36);
					const int32_t ty_raw = rds32 (data + boff + 40);
					const int32_t tz_raw = rds32 (data + boff + 44);

					j->scale.x = sx_raw ? fx12 (sx_raw) : 1.0f;
					j->scale.y = sy_raw ? fx12 (sy_raw) : 1.0f;
					j->scale.z = sz_raw ? fx12 (sz_raw) : 1.0f;
					j->translate.x = fx12 (tx_raw);
					j->translate.y = fx12 (ty_raw);
					j->translate.z = fx12 (tz_raw);

					bone_xfs[b].sx = j->scale.x;
					bone_xfs[b].sy = j->scale.y;
					bone_xfs[b].sz = j->scale.z;
					bone_xfs[b].tx = j->translate.x;
					bone_xfs[b].ty = j->translate.y;
					bone_xfs[b].tz = j->translate.z;
				}
				if (!j->name[0])
					snprintf (j->name, sizeof (j->name), "bone_%u", b);
			}
		}
	}

	// --- materials ---------------------------------------------------------
	uint tex_w = 32, tex_h = 32;
	if (mat_count > 0 && mat_off > 0 && mat_off < size)
	{
		out->materials = CALLOC (mat_count, sizeof (material_t));
		if (out->materials)
		{
			out->num_materials = mat_count;
			for (uint m = 0; m < mat_count; m++)
			{
				material_t *mat = out->materials + m;
				const uint32_t moff = mat_off + m * 20;
				if (moff + 20 <= size)
				{
					const uint32_t name_ptr = rd32 (data + moff);
					if (name_ptr > 0 && name_ptr < size && data[name_ptr])
					{
						size_t slen = 0;
						while (name_ptr + slen < size && slen + 1 < sizeof (mat->name)
							&& data[name_ptr + slen])
						{
							mat->name[slen] = (char)data[name_ptr + slen];
							slen++;
						}
						mat->name[slen] = 0;
					}
					const uint16_t mw = rd16 (data + moff + 12);
					const uint16_t mh = rd16 (data + moff + 14);
					if (mw > 0 && mw <= 1024)
						tex_w = mw;
					if (mh > 0 && mh <= 1024)
						tex_h = mh;
				}
				if (!mat->name[0])
					snprintf (mat->name, sizeof (mat->name), "mat_%u", m);

				snprintf (mat->textures[0], sizeof (mat->textures[0]), "%s.png", mat->name);
				mat->num_textures = 1;
			}
		}
	}

	// --- display lists -----------------------------------------------------
	typedef struct
	{
		uint32_t sz;
		uint32_t off;
	} dl_entry_t;
	dl_entry_t dls[512];
	uint n_dls = 0;

	for (uint32_t off = shapes_base; off + 8 <= (bone_off ? bone_off : size) && n_dls < 512;
		off += 4)
	{
		uint32_t sz = rd32 (data + off);
		uint32_t dloff = rd32 (data + off + 4);
		if (dloff >= shapes_base && sz >= 16 && sz <= size && dloff <= size - sz && dloff + 4 <= size)
		{
			const uint8_t *w = data + dloff;
			if ((w[0] == 0x40 || w[1] == 0x40 || w[2] == 0x40 || w[3] == 0x40)
				&& (w[0] >= 0x14 || w[1] >= 0x14 || w[2] >= 0x14 || w[3] >= 0x14))
			{
				bool overlap = false;
				for (uint k = 0; k < n_dls; k++)
				{
					if (dloff >= dls[k].off && dloff - dls[k].off < dls[k].sz)
					{
						overlap = true;
						break;
					}
				}
				if (!overlap)
				{
					dls[n_dls].sz = sz;
					dls[n_dls].off = dloff;
					n_dls++;
				}
			}
		}
	}

	if (n_dls > 0)
	{
		out->meshes = CALLOC (n_dls, sizeof (mesh_t));
		if (out->meshes)
		{
			for (uint i = 0; i < n_dls; i++)
			{
				geom_t g;
				memset (&g, 0, sizeof (g));
				run_display_list (
					&g, data + dls[i].off, dls[i].sz, bone_xfs, out->num_joints, tex_w, tex_h);
				if (!g.n_vtx)
				{
					FREE (g.pos);
					FREE (g.nrm);
					FREE (g.uv);
					FREE (g.vtx);
					continue;
				}
				mesh_t *mesh = out->meshes + out->num_meshes;
				snprintf (mesh->name, sizeof (mesh->name), "mesh_%u", (uint)out->num_meshes);
				mesh->positions = g.pos;
				mesh->num_positions = g.n_pos;
				mesh->normals = g.nrm;
				mesh->num_normals = g.n_nrm;
				mesh->texcoords = g.uv;
				mesh->num_texcoords = g.n_uv;
				mesh->vertices = g.vtx;
				mesh->num_vertices = g.n_vtx;
				mesh->material_idx = (out->num_materials > 0) ? (int)(i % out->num_materials) : 0;
				out->num_meshes++;
			}
		}
	}

	if (bone_xfs)
		FREE (bone_xfs);
	if (allocated_data)
		FREE (allocated_data);

	if (!out->num_meshes && !out->num_joints)
	{
		FreeModel (out);
		return NULL;
	}
	return out;
}

//-----------------------------------------------------------------------------

static inline bool is_ext (ccp src, ccp ext)
{
	if (!src || !ext)
		return false;
	const size_t slen = strlen (src);
	const size_t elen = strlen (ext);
	return slen >= elen && !strcasecmp (src + slen - elen, ext);
}

enumError ExportEarlyDSBMDTextures (const uint8_t *data, size_t size, const char *dest_path_or_dir)
{
	if (!data || size < 60 || !dest_path_or_dir)
		return ERR_OK;

	uint8_t *allocated_data = NULL;
	if (size >= 5 && !memcmp (data, "LZ77\x10", 5))
	{
		uint dec_sz = 0;
		u8 *dec_buf = 0;
		if (DecodeLZ10LZ11 (&dec_buf, &dec_sz, data + 4, (uint)size - 4) == ERR_OK && dec_buf)
		{
			allocated_data = dec_buf;
			data = allocated_data;
			size = dec_sz;
		}
	}
	else if (size >= 4 && data[0] == 0x10)
	{
		const uint uncomp_len = (uint)data[1] | (uint)data[2] << 8 | (uint)data[3] << 16;
		if (uncomp_len > size && uncomp_len < 0x2000000)
		{
			uint dec_sz = 0;
			u8 *dec_buf = 0;
			if (DecodeLZ10LZ11 (&dec_buf, &dec_sz, data, (uint)size) == ERR_OK && dec_buf)
			{
				allocated_data = dec_buf;
				data = allocated_data;
				size = dec_sz;
			}
		}
	}

	if (size < 60)
	{
		if (allocated_data)
			FREE (allocated_data);
		return ERR_OK;
	}

	const uint32_t bone_off = rd32 (data + 8);
	const uint32_t shapes_base = rd32 (data + 16);
	const uint32_t mat_count = rd32 (data + 20);
	const uint32_t mat_off = rd32 (data + 24);
	const uint32_t plt_count = rd32 (data + 28);
	const uint32_t plt_off = rd32 (data + 32);

	if (shapes_base != 0x3c || bone_off <= shapes_base || bone_off > size || !mat_count || !mat_off
		|| mat_off >= size)
	{
		if (allocated_data)
			FREE (allocated_data);
		return ERR_OK;
	}

	char dir[PATH_MAX];
	snprintf (dir, sizeof (dir), "%s", dest_path_or_dir);
	if (is_ext (dir, ".dae") || is_ext (dir, ".glb"))
	{
		char *slash = strrchr (dir, '/');
		if (slash)
			*slash = 0;
		else
			snprintf (dir, sizeof (dir), ".");
	}
	CreatePath (dir, true);

	for (uint m = 0; m < mat_count; m++)
	{
		const uint32_t moff = mat_off + m * 20;
		if (moff + 20 > size)
			break;

		const uint32_t name_ptr = rd32 (data + moff);
		const uint32_t data_ptr = rd32 (data + moff + 4);
		const uint16_t w = rd16 (data + moff + 12);
		const uint16_t h = rd16 (data + moff + 14);

		if (!w || !h || w > 1024 || h > 1024 || data_ptr >= size)
			continue;

		char clean_name[128];
		if (name_ptr > 0 && name_ptr < size && data[name_ptr])
		{
			size_t slen = 0;
			while (
				name_ptr + slen < size && slen + 1 < sizeof (clean_name) && data[name_ptr + slen])
			{
				clean_name[slen] = (char)data[name_ptr + slen];
				slen++;
			}
			clean_name[slen] = 0;
		}
		else
		{
			snprintf (clean_name, sizeof (clean_name), "mat_%03u", m);
		}

		// Match palette by name (e.g. <clean_name>_pl or <clean_name>) or fallback to index m
		uint32_t pl_ptr = 0;
		if (plt_count > 0 && plt_off > 0 && plt_off < size)
		{
			for (uint pl = 0; pl < plt_count; pl++)
			{
				const uint32_t ploff = plt_off + pl * 16;
				if (ploff + 16 > size)
					break;
				const uint32_t pl_name_ptr = rd32 (data + ploff);
				const uint32_t candidate_ptr = rd32 (data + ploff + 4);
				if (pl_name_ptr > 0 && pl_name_ptr < size)
				{
					const char *pname = (const char *)(data + pl_name_ptr);
					if (!strncmp (pname, clean_name, strlen (clean_name)))
					{
						pl_ptr = candidate_ptr;
						break;
					}
				}
			}
			if (!pl_ptr && m < plt_count && plt_off + m * 16 + 16 <= size)
				pl_ptr = rd32 (data + plt_off + m * 16 + 4);
		}

		// Calculate available data size
		size_t next_off = size;
		for (uint om = 0; om < mat_count; om++)
		{
			const uint32_t optr = rd32 (data + mat_off + om * 20 + 4);
			if (optr > data_ptr && optr < next_off)
				next_off = optr;
		}
		if (plt_count > 0 && plt_off > 0 && plt_off < size)
		{
			for (uint opl = 0; opl < plt_count; opl++)
			{
				const uint32_t optr = rd32 (data + plt_off + opl * 16 + 4);
				if (optr > data_ptr && optr < next_off)
					next_off = optr;
			}
		}
		const size_t avail = (next_off > data_ptr) ? (next_off - data_ptr) : (size - data_ptr);

		u8 *rgba = CALLOC (1, (size_t)w * h * 4);
		if (!rgba)
			continue;

		if (avail == ((size_t)w * h * 6) / 16 && w >= 4 && h >= 4 && pl_ptr + 8 <= size)
		{
			// CMP4 (Nitro 4x4 texel compression)
			const uint bw = w / 4, bh = h / 4;
			const uint32_t texel_base = data_ptr;
			const uint32_t info_base = data_ptr + bw * bh * 4;
			for (uint by = 0; by < bh; by++)
			{
				for (uint bx = 0; bx < bw; bx++)
				{
					const uint bidx = by * bw + bx;
					if (texel_base + bidx * 4 + 4 > size || info_base + bidx * 2 + 2 > size)
						break;
					const uint32_t tdata = rd32 (data + texel_base + bidx * 4);
					const uint16_t info = rd16 (data + info_base + bidx * 2);
					const uint32_t pal_offset = (info & 0x3fff) << 1;
					const uint mode = info >> 14;

					if (pl_ptr + pal_offset + 4 > size)
						continue;
					const uint16_t c0 = rd16 (data + pl_ptr + pal_offset);
					const uint16_t c1 = rd16 (data + pl_ptr + pal_offset + 2);
					u8 pal[4][4];
					pal[0][0] = (c0 & 0x1f) << 3;
					pal[0][1] = ((c0 >> 5) & 0x1f) << 3;
					pal[0][2] = ((c0 >> 10) & 0x1f) << 3;
					pal[0][3] = 255;
					pal[1][0] = (c1 & 0x1f) << 3;
					pal[1][1] = ((c1 >> 5) & 0x1f) << 3;
					pal[1][2] = ((c1 >> 10) & 0x1f) << 3;
					pal[1][3] = 255;
					if (mode == 0)
					{
						const uint16_t c2 = (pl_ptr + pal_offset + 6 <= size)
							? rd16 (data + pl_ptr + pal_offset + 4)
							: 0;
						pal[2][0] = (c2 & 0x1f) << 3;
						pal[2][1] = ((c2 >> 5) & 0x1f) << 3;
						pal[2][2] = ((c2 >> 10) & 0x1f) << 3;
						pal[2][3] = 255;
						pal[3][0] = pal[3][1] = pal[3][2] = pal[3][3] = 0;
					}
					else if (mode == 1)
					{
						pal[2][0] = (pal[0][0] + pal[1][0]) / 2;
						pal[2][1] = (pal[0][1] + pal[1][1]) / 2;
						pal[2][2] = (pal[0][2] + pal[1][2]) / 2;
						pal[2][3] = 255;
						pal[3][0] = pal[3][1] = pal[3][2] = pal[3][3] = 0;
					}
					else if (mode == 2)
					{
						const uint16_t c2 = (pl_ptr + pal_offset + 6 <= size)
							? rd16 (data + pl_ptr + pal_offset + 4)
							: 0;
						const uint16_t c3 = (pl_ptr + pal_offset + 8 <= size)
							? rd16 (data + pl_ptr + pal_offset + 6)
							: 0;
						pal[2][0] = (c2 & 0x1f) << 3;
						pal[2][1] = ((c2 >> 5) & 0x1f) << 3;
						pal[2][2] = ((c2 >> 10) & 0x1f) << 3;
						pal[2][3] = 255;
						pal[3][0] = (c3 & 0x1f) << 3;
						pal[3][1] = ((c3 >> 5) & 0x1f) << 3;
						pal[3][2] = ((c3 >> 10) & 0x1f) << 3;
						pal[3][3] = 255;
					}
					else
					{
						pal[2][0] = (2 * pal[0][0] + pal[1][0]) / 3;
						pal[2][1] = (2 * pal[0][1] + pal[1][1]) / 3;
						pal[2][2] = (2 * pal[0][2] + pal[1][2]) / 3;
						pal[2][3] = 255;
						pal[3][0] = (pal[0][0] + 2 * pal[1][0]) / 3;
						pal[3][1] = (pal[0][1] + 2 * pal[1][1]) / 3;
						pal[3][2] = (pal[0][2] + 2 * pal[1][2]) / 3;
						pal[3][3] = 255;
					}

					for (uint py = 0; py < 4; py++)
					{
						for (uint px = 0; px < 4; px++)
						{
							const uint shift = (py * 4 + px) * 2;
							const uint cidx = (tdata >> shift) & 3;
							const size_t out_idx = ((by * 4 + py) * (size_t)w + (bx * 4 + px)) * 4;
							rgba[out_idx + 0] = pal[cidx][0];
							rgba[out_idx + 1] = pal[cidx][1];
							rgba[out_idx + 2] = pal[cidx][2];
							rgba[out_idx + 3] = pal[cidx][3];
						}
					}
				}
			}
		}
		else if (avail == ((size_t)w * h) / 2 && pl_ptr + 32 <= size)
		{
			// 4-bit / 16-color paletted
			for (size_t i = 0; i < (size_t)w * h; i++)
			{
				if (data_ptr + i / 2 >= size)
					break;
				const uint8_t byte = data[data_ptr + i / 2];
				const uint8_t cidx = (i & 1) ? (byte >> 4) : (byte & 0x0f);
				if (pl_ptr + cidx * 2 + 2 <= size)
				{
					const uint16_t col = rd16 (data + pl_ptr + cidx * 2);
					rgba[i * 4 + 0] = (col & 0x1f) << 3;
					rgba[i * 4 + 1] = ((col >> 5) & 0x1f) << 3;
					rgba[i * 4 + 2] = ((col >> 10) & 0x1f) << 3;
					rgba[i * 4 + 3] = (cidx == 0 && col == 0) ? 0 : 255;
				}
			}
		}
		else
		{
			// 8-bit / 256-color paletted
			for (size_t i = 0; i < (size_t)w * h; i++)
			{
				if (data_ptr + i >= size)
					break;
				const uint8_t cidx = data[data_ptr + i];
				if (pl_ptr + cidx * 2 + 2 <= size)
				{
					const uint16_t col = rd16 (data + pl_ptr + cidx * 2);
					rgba[i * 4 + 0] = (col & 0x1f) << 3;
					rgba[i * 4 + 1] = ((col >> 5) & 0x1f) << 3;
					rgba[i * 4 + 2] = ((col >> 10) & 0x1f) << 3;
					rgba[i * 4 + 3] = (cidx == 0 && col == 0) ? 0 : 255;
				}
			}
		}

		char out_path[PATH_MAX];
		snprintf (out_path, sizeof (out_path), "%s/%s.png", dir, clean_name);

		Image_t img;
		InitializeIMG (&img);
		const uint xw = EXPAND8 (w), xh = EXPAND8 (h);
		u8 *padded = xw == w && xh == h ? rgba : CALLOC (1, xw * xh * 4);
		if (padded != rgba)
		{
			for (uint y = 0; y < h; y++)
				memcpy (padded + (size_t)y * xw * 4, rgba + (size_t)y * w * 4, (size_t)w * 4);
			FREE (rgba);
		}
		img.data = padded;
		img.data_alloced = true;
		img.data_size = xw * xh * 4;
		img.width = w;
		img.xwidth = xw;
		img.height = h;
		img.xheight = xh;
		img.iform = img.info_iform = IMG_X_RGB;
		img.info_fform = FF_PNG;
		img.info_n_image = 1;
		img.endian = &le_func;

		SavePNG (&img, false, 0, out_path, 0, 0, true, 0);
		ResetIMG (&img);
	}

	if (allocated_data)
		FREE (allocated_data);
	return ERR_OK;
}

model_t *ParseNSBMD (const uint8_t *data, size_t size)
{
	if (!data || size < 0x20)
		return NULL;
	if (memcmp (data, "BMD0", 4))
		return ParseEarlyDSBMD (data, size);
	if (rd16 (data + 4) != 0xFEFF)
		return NULL;

	const uint n_blocks = rd16 (data + 0x0e);
	if (!n_blocks || (size_t)0x10 + n_blocks * 4 > size)
		return NULL;

	// Find the MDL0 block.
	const uint8_t *mdl = NULL;
	size_t mdl_size = 0;
	for (uint i = 0; i < n_blocks; i++)
	{
		const uint32_t off = rd32 (data + 0x10 + i * 4);
		if ((size_t)off + 8 > size)
			continue;
		if (memcmp (data + off, "MDL0", 4))
			continue;
		mdl = data + off;
		mdl_size = rd32 (data + off + 4);
		if (mdl_size > size - off)
			mdl_size = size - off;
		break;
	}
	if (!mdl || mdl_size < 0x10)
		return NULL;

	// Model dictionary starts right after the MDL0 header.
	nitro_dict_t models;
	if (!read_dict (&models, mdl + 8, mdl_size - 8))
		return NULL;

	// Only the first model is exported; DAE has no multi-model concept here.
	const uint32_t model_off = rd32 (mdl + 8 + models.data_off);
	if ((size_t)model_off + 0x40 > mdl_size)
		return NULL;
	const uint8_t *m = mdl + model_off;
	const size_t m_avail = mdl_size - model_off;

	model_t *out = CALLOC (1, sizeof (model_t));
	if (!out)
		return NULL;

	// --- bones -------------------------------------------------------------
	// The bone dictionary sits at a fixed offset in the model header.
	const uint32_t bones_off = 0x40;
	nitro_dict_t bones;
	if (bones_off < m_avail && read_dict (&bones, m + bones_off, m_avail - bones_off))
	{
		out->joints = CALLOC (bones.n, sizeof (joint_t));
		if (out->joints)
		{
			out->num_joints = bones.n;
			for (uint i = 0; i < bones.n; i++)
			{
				joint_t *j = out->joints + i;
				dict_name (&bones, m + bones_off, i, j->name, sizeof (j->name));
				j->parent_idx = -1; // recovered below from the render commands, if present
				j->scale.x = j->scale.y = j->scale.z = 1.0f;
			}

			const uint32_t render_cmds_off = rd32 (m + 0x04);
			if (render_cmds_off && (size_t)render_cmds_off < m_avail)
				parse_bone_hierarchy (out, m + render_cmds_off, m_avail - render_cmds_off);
		}
	}

	// --- shapes ------------------------------------------------------------
	const uint32_t shapes_off = rd32 (m + 0x0c);
	nitro_dict_t shapes;
	if (shapes_off < m_avail && read_dict (&shapes, m + shapes_off, m_avail - shapes_off))
	{
		out->meshes = CALLOC (shapes.n, sizeof (mesh_t));
		if (out->meshes)
		{
			const uint8_t *sbase = m + shapes_off;
			for (uint i = 0; i < shapes.n; i++)
			{
				const uint8_t *rec = sbase + shapes.data_off + i * shapes.data_size;
				if ((size_t)(rec - m) + shapes.data_size > m_avail)
					continue;
				// The dictionary entry is a bare u32 offset when itemSize is 4;
				// wider records keep it in the second word.
				const uint32_t sh_off = shapes.data_size >= 8 ? rd32 (rec + 4) : rd32 (rec);
				if ((size_t)shapes_off + sh_off + 0x10 > m_avail)
					continue;
				const uint8_t *sh = sbase + sh_off;

				// shape header: a constant tag (0x00100000), the shape size, then
				// the display list's size and its offset from the shape start.
				const uint32_t dl_size = rd32 (sh + 8);
				const uint32_t dl_off = rd32 (sh + 12);
				const size_t dl_pos = (size_t)(sh - m) + dl_off;
				if (!dl_size || dl_pos + dl_size > m_avail)
					continue;

				geom_t g;
				memset (&g, 0, sizeof (g));
				run_display_list (&g, m + dl_pos, dl_size, NULL, 0, 32, 32);
				if (!g.n_vtx)
				{
					FREE (g.pos);
					FREE (g.nrm);
					FREE (g.uv);
					FREE (g.vtx);
					continue;
				}

				mesh_t *mesh = out->meshes + out->num_meshes;
				dict_name (&shapes, sbase, i, mesh->name, sizeof (mesh->name));
				if (!mesh->name[0])
					snprintf (mesh->name, sizeof (mesh->name), "shape%u", i);
				mesh->positions = g.pos;
				mesh->num_positions = g.n_pos;
				mesh->normals = g.nrm;
				mesh->num_normals = g.n_nrm;
				mesh->texcoords = g.uv;
				mesh->num_texcoords = g.n_uv;
				mesh->vertices = g.vtx;
				mesh->num_vertices = g.n_vtx;
				mesh->material_idx = -1;
				out->num_meshes++;
			}
		}
	}

	if (!out->num_meshes && !out->num_joints)
	{
		FreeModel (out);
		return NULL;
	}
	return out;
}
