/***************************************************************************
 *                         _______ _______ _______                         *
 *                        |  ___  |____   |  ___  |                        *
 *                        | |   |_|    / /| |   |_|                        *
 *                        | |_____    / / | |_____                         *
 *                        |_____  |  / /  |_____  |                        *
 *                         _    | | / /    _    | |                        *
 *                        | |___| |/ /____| |___| |                        *
 *                        |_______|_______|_______|                        *
 *                                                                         *
 *                            Wiimms SZS Tools                             *
 *                          https://szs.wiimm.de/                          *
 *                                                                         *
 ***************************************************************************
 *                                                                         *
 *   This file is part of the SZS project.                                 *
 *   Visit https://szs.wiimm.de/ for project details and sources.          *
 *                                                                         *
 ***************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "lib-scn.h"
#include "lib-brres.h"
#include "lib-brres-anim.h"
#include "lib-szs.h"

///////////////////////////////////////////////////////////////////////////////
///////////////			byte order helper		///////////////
///////////////////////////////////////////////////////////////////////////////
// BRRES/SCN0 data is always big endian (Wii).

static inline u16 scn_rd16 (const u8 *p) { return (u16)p[0] << 8 | p[1]; }

static inline u32 scn_rd32 (const u8 *p)
{
	return (u32)p[0] << 24 | (u32)p[1] << 16 | (u32)p[2] << 8 | p[3];
}

static inline s32 scn_rds32 (const u8 *p) { return (s32)scn_rd32 (p); }

static inline float scn_rdf (const u8 *p)
{
	const u32 v = scn_rd32 (p);
	float f;
	memcpy (&f, &v, 4);
	return f;
}

static inline void scn_w16 (u8 *p, u16 v)
{
	p[0] = v >> 8;
	p[1] = (u8)v;
}

static inline void scn_w32 (u8 *p, u32 v)
{
	p[0] = v >> 24;
	p[1] = v >> 16;
	p[2] = v >> 8;
	p[3] = (u8)v;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			section tables			///////////////
///////////////////////////////////////////////////////////////////////////////

const char *const scn0_sect_name[SCN0_N_SECT]
	= { "LightSet(NW4R)", "AmbLights(NW4R)", "Lights(NW4R)", "Fogs(NW4R)", "Cameras(NW4R)" };

const u8 scn0_node_size[SCN0_N_SECT] = { 0x4c, 0x1c, 0x5c, 0x28, 0x5c };

// short keys used by the text format
static const char *const scn0_sect_key[SCN0_N_SECT]
	= { "lightset", "amblight", "light", "fog", "camera" };

//-----------------------------------------------------------------------------
// [[scn0_slot_t]]

// One animatable slot of a node. The slots are listed in flag-BIT numeric
// order, which is the order retail writes their payloads in -- see lib-scn.h.

typedef struct scn0_slot_t
{
	u16 off; // offset of the slot word inside the node struct
	u16 bit; // flag bit; when set the slot holds a fixed value
	char kind; // 'k'=keyframe set, 'c'=colour array, 'v'=visibility bits

} scn0_slot_t;

static const scn0_slot_t scn0_slot_lightset[] = { { 0, 0, 0 } };

static const scn0_slot_t scn0_slot_amblight[] = {
	{ 0x18, 0x80, 'c' }, // lighting colour
	{ 0, 0, 0 }
};

static const scn0_slot_t scn0_slot_light[] = {
	{ 0x24, 0x0008, 'k' }, // start.x
	{ 0x28, 0x0010, 'k' }, // start.y
	{ 0x2c, 0x0020, 'k' }, // start.z
	{ 0x30, 0x0040, 'c' }, // light colour
	{ 0x20, 0x0080, 'v' }, // per frame enable bits
	{ 0x34, 0x0100, 'k' }, // end.x
	{ 0x38, 0x0200, 'k' }, // end.y
	{ 0x3c, 0x0400, 'k' }, // end.z
	{ 0x50, 0x0800, 'k' }, // cutoff
	{ 0x44, 0x1000, 'k' }, // reference distance
	{ 0x48, 0x2000, 'k' }, // reference brightness
	{ 0x54, 0x4000, 'c' }, // specular colour
	{ 0x58, 0x8000, 'k' }, // shininess
	{ 0, 0, 0 }
};

static const scn0_slot_t scn0_slot_fog[] = {
	{ 0x1c, 0x20, 'k' }, // start
	{ 0x20, 0x40, 'k' }, // end
	{ 0x24, 0x80, 'c' }, // colour
	{ 0, 0, 0 }
};

static const scn0_slot_t scn0_slot_camera[] = {
	{ 0x20, 0x0002, 'k' }, // position.x
	{ 0x24, 0x0004, 'k' }, // position.y
	{ 0x28, 0x0008, 'k' }, // position.z
	{ 0x2c, 0x0010, 'k' }, // aspect
	{ 0x30, 0x0020, 'k' }, // near z
	{ 0x34, 0x0040, 'k' }, // far z
	{ 0x54, 0x0080, 'k' }, // perspective fov y
	{ 0x58, 0x0100, 'k' }, // ortho height
	{ 0x44, 0x0200, 'k' }, // aim.x
	{ 0x48, 0x0400, 'k' }, // aim.y
	{ 0x4c, 0x0800, 'k' }, // aim.z
	{ 0x50, 0x1000, 'k' }, // twist
	{ 0x38, 0x2000, 'k' }, // rotate.x
	{ 0x3c, 0x4000, 'k' }, // rotate.y
	{ 0x40, 0x8000, 'k' }, // rotate.z
	{ 0, 0, 0 }
};

static const scn0_slot_t *const scn0_slots[SCN0_N_SECT] = { scn0_slot_lightset,
	scn0_slot_amblight, scn0_slot_light, scn0_slot_fog, scn0_slot_camera };

//-----------------------------------------------------------------------------

// The "is fixed" flag word of a node. Ambient lights and fog keep it in a
// single byte, lights and cameras in a big endian u16.

static u32 GetFixedFlagsSCN0 (scn0_sect_t sect, const u8 *raw)
{
	switch (sect)
	{
	case SCN0_AMBLIGHT:
	case SCN0_FOG:
		return raw[0x14];
	case SCN0_LIGHT:
		return scn_rd16 (raw + 0x1c);
	case SCN0_CAMERA:
		return scn_rd16 (raw + 0x18);
	default:
		return 0;
	}
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			setup & teardown		///////////////
///////////////////////////////////////////////////////////////////////////////

void InitializeSCN0 (scn0_t *scn)
{
	DASSERT (scn);
	memset (scn, 0, sizeof (*scn));
	scn->version = SCN0_DEFAULT_VERSION;
}

///////////////////////////////////////////////////////////////////////////////

void ResetSCN0 (scn0_t *scn)
{
	DASSERT (scn);

	for (uint s = 0; s < SCN0_N_SECT; s++)
	{
		for (uint i = 0; i < scn->n_node[s]; i++)
		{
			scn0_node_t *nd = scn->node[s] + i;
			FreeString (nd->name);
			for (uint b = 0; b < nd->n_blob; b++)
				FREE (nd->blob[b].data);
			FREE (nd->blob);
		}
		FREE (scn->node[s]);
	}

	FreeString (scn->fname);
	FreeString (scn->name);
	FreeString (scn->orig_path);

	InitializeSCN0 (scn);
}

///////////////////////////////////////////////////////////////////////////////

scn0_node_t *AppendNodeSCN0 (scn0_t *scn, scn0_sect_t sect, ccp name)
{
	DASSERT (scn);
	DASSERT ((uint)sect < SCN0_N_SECT);

	const uint n = scn->n_node[sect] + 1;
	scn->node[sect] = REALLOC (scn->node[sect], n * sizeof (*scn->node[sect]));
	scn0_node_t *nd = scn->node[sect] + scn->n_node[sect]++;
	memset (nd, 0, sizeof (*nd));
	nd->name = STRDUP (name ? name : "");
	return nd;
}

///////////////////////////////////////////////////////////////////////////////

static scn0_blob_t *AppendBlobSCN0 (scn0_node_t *nd)
{
	nd->blob = REALLOC (nd->blob, (nd->n_blob + 1) * sizeof (*nd->blob));
	scn0_blob_t *bl = nd->blob + nd->n_blob++;
	memset (bl, 0, sizeof (*bl));
	return bl;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			ScanRawSCN0()			///////////////
///////////////////////////////////////////////////////////////////////////////

enumError ScanRawSCN0 (scn0_t *scn, bool init_scn, const void *data, uint data_size)
{
	DASSERT (scn);
	DASSERT (data);

	if (init_scn)
		InitializeSCN0 (scn);
	else
		ResetSCN0 (scn);

	const u8 *base = data;

	if (data_size < 0x44 || memcmp (base, SCN_MAGIC, 4))
		return ERROR0 (ERR_INVALID_DATA, "Not a SCN0 file.\n");

	const u32 version = scn_rd32 (base + 8);
	if (version < SCN0_MIN_VERSION || version > SCN0_MAX_VERSION)
		return ERROR0 (ERR_INVALID_DATA, "SCN0: unsupported version %u.\n", version);
	scn->version = version;

	const uint head_size = version == 5 ? 0x48 : 0x44;
	if (data_size < head_size)
		return ERROR0 (ERR_INVALID_DATA, "SCN0: file too short.\n");

	const u32 group_off = scn_rd32 (base + 0x10);

	// version 5 inserts a user data offset ahead of the string fields
	const uint off = version == 5 ? 0x2c : 0x28;
	const u32 name_off = scn_rd32 (base + off);
	const u32 orig_off = scn_rd32 (base + off + 4);
	scn->n_frames = scn_rd16 (base + off + 8);
	scn->spec_light = scn_rd16 (base + off + 10);
	scn->loop = scn_rd32 (base + off + 12);

	if (name_off && name_off < data_size)
		scn->name = STRDUP ((ccp)(base + name_off));
	if (orig_off && orig_off < data_size)
		scn->orig_path = STRDUP ((ccp)(base + orig_off));

	//--- the outer resource group names the sections

	if ((u64)group_off + 8 > data_size)
		return ERROR0 (ERR_INVALID_DATA, "SCN0: bad group offset.\n");
	const uint n_sect = scn_rd32 (base + group_off + 4);
	if (n_sect > SCN0_N_SECT)
		return ERROR0 (ERR_INVALID_DATA, "SCN0: %u sections, at most %u expected.\n", n_sect,
			SCN0_N_SECT);

	for (uint g = 0; g < n_sect; g++)
	{
		const uint rec = group_off + 8 + 16 * (g + 1);
		if ((u64)rec + 16 > data_size)
			return ERROR0 (ERR_INVALID_DATA, "SCN0: section record %u out of range.\n", g);

		const u64 nabs = (u64)group_off + scn_rd32 (base + rec + 8);
		const u64 sabs = (u64)group_off + scn_rd32 (base + rec + 12);
		if (nabs >= data_size || sabs + 8 > data_size)
			return ERROR0 (ERR_INVALID_DATA, "SCN0: section %u out of range.\n", g);

		ccp sname = (ccp)(base + nabs);
		scn0_sect_t sect = SCN0_N_SECT;
		for (uint s = 0; s < SCN0_N_SECT; s++)
			if (!strcmp (sname, scn0_sect_name[s]))
			{
				sect = s;
				break;
			}
		if (sect == SCN0_N_SECT)
			return ERROR0 (ERR_INVALID_DATA, "SCN0: unknown section '%s'.\n", sname);
		if (scn->n_node[sect])
			return ERROR0 (ERR_INVALID_DATA, "SCN0: section '%s' listed twice.\n", sname);

		const uint nsize = scn0_node_size[sect];
		const scn0_slot_t *slots = scn0_slots[sect];
		const uint n_node = scn_rd32 (base + sabs + 4);

		for (uint i = 0; i < n_node; i++)
		{
			const u64 nrec = sabs + 8 + 16 * (i + 1);
			if (nrec + 16 > data_size)
				return ERROR0 (ERR_INVALID_DATA, "SCN0: node record out of range.\n");

			const u64 n2 = sabs + scn_rd32 (base + nrec + 8);
			const u64 dabs = sabs + scn_rd32 (base + nrec + 12);
			if (n2 >= data_size || dabs + nsize > data_size)
				return ERROR0 (ERR_INVALID_DATA, "SCN0: node data out of range.\n");

			scn0_node_t *nd = AppendNodeSCN0 (scn, sect, (ccp)(base + n2));
			memcpy (nd->raw, base + dabs, nsize);

			const u32 fixed = GetFixedFlagsSCN0 (sect, nd->raw);
			for (const scn0_slot_t *sl = slots; sl->bit; sl++)
			{
				if (fixed & sl->bit)
					continue;

				// the slot word holds a self relative offset
				const u64 slot = dabs + sl->off;
				const s64 payload = (s64)slot + scn_rds32 (base + slot);
				if (payload < 0 || (u64)payload + 4 > data_size)
					return ERROR0 (ERR_INVALID_DATA, "SCN0: slot payload out of range.\n");

				uint size;
				switch (sl->kind)
				{
				case 'k':
					size = 8 + 12 * (uint)scn_rd16 (base + payload);
					break;
				case 'c':
					size = 4 * (scn->n_frames + 1);
					break;
				default: // 'v'
					size = 4 + scn_rd32 (base + payload);
					break;
				}
				if ((u64)payload + size > data_size)
					return ERROR0 (ERR_INVALID_DATA, "SCN0: slot payload truncated.\n");

				scn0_blob_t *bl = AppendBlobSCN0 (nd);
				bl->slot_off = sl->off;
				bl->kind = sl->kind;
				bl->size = size;
				bl->data = MEMDUP (base + payload, size);
			}
		}
	}

	return ERR_OK;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			SaveRawSCN0()			///////////////
///////////////////////////////////////////////////////////////////////////////

enumError SaveRawSCN0 (scn0_t *scn, ccp fname, bool set_time)
{
	DASSERT (scn);
	DASSERT (fname);

	const uint head_size = scn->version == 5 ? 0x48 : 0x44;
	const uint group_off = head_size;

	//--- which sections are present, in canonical order

	scn0_sect_t sect_list[SCN0_N_SECT];
	uint n_sect = 0;
	for (uint s = 0; s < SCN0_N_SECT; s++)
		if (scn->n_node[s])
			sect_list[n_sect++] = s;

	uint n_node_total = 0;
	for (uint s = 0; s < SCN0_N_SECT; s++)
		n_node_total += scn->n_node[s];

	//--- layout: outer group, per section groups, all nodes, then all payloads

	uint pos = group_off + 8 + 16 * (n_sect + 1);

	uint *sect_rel = CALLOC (n_sect ? n_sect : 1, sizeof (*sect_rel));
	for (uint g = 0; g < n_sect; g++)
	{
		sect_rel[g] = pos;
		pos += 8 + 16 * (scn->n_node[sect_list[g]] + 1);
	}

	uint *node_abs = CALLOC (n_node_total ? n_node_total : 1, sizeof (*node_abs));
	uint idx = 0;
	for (uint g = 0; g < n_sect; g++)
	{
		const uint nsize = scn0_node_size[sect_list[g]];
		for (uint i = 0; i < scn->n_node[sect_list[g]]; i++)
		{
			node_abs[idx++] = pos;
			pos += nsize;
		}
	}

	// the typed offsets in the header point at the first node of each section
	uint typed_off[SCN0_N_SECT];
	memset (typed_off, 0, sizeof (typed_off));
	idx = 0;
	for (uint g = 0; g < n_sect; g++)
	{
		typed_off[sect_list[g]] = node_abs[idx];
		idx += scn->n_node[sect_list[g]];
	}

	// payloads follow every node, in node order and then in flag-bit order
	const uint payload_start = pos;
	for (uint g = 0; g < n_sect; g++)
		for (uint i = 0; i < scn->n_node[sect_list[g]]; i++)
		{
			const scn0_node_t *nd = scn->node[sect_list[g]] + i;
			for (uint b = 0; b < nd->n_blob; b++)
				pos += nd->blob[b].size;
		}

	const uint data_end = pos;

	//--- string pool: animation name, orig path, section names, node names

	uint n_names = 1 + (scn->orig_path ? 1 : 0) + n_sect + n_node_total;
	ccp *names = CALLOC (n_names, sizeof (ccp));
	uint *name_off = CALLOC (n_names, sizeof (uint));

	uint ni = 0;
	const uint name_slot = ni;
	names[ni++] = scn->name ? scn->name : "";
	const uint orig_slot = ni;
	if (scn->orig_path)
		names[ni++] = scn->orig_path;
	const uint sect_slot = ni;
	for (uint g = 0; g < n_sect; g++)
		names[ni++] = scn0_sect_name[sect_list[g]];
	const uint node_slot = ni;
	for (uint g = 0; g < n_sect; g++)
		for (uint i = 0; i < scn->n_node[sect_list[g]]; i++)
			names[ni++] = scn->node[sect_list[g]][i].name;

	// Retail lays the pool out in ordinal name order rather than in record
	// order, so sort, place, then scatter the offsets back onto the slots.
	uint *ord = CALLOC (n_names, sizeof (uint));
	for (uint i = 0; i < n_names; i++)
		ord[i] = i;
	for (uint i = 1; i < n_names; i++)
	{
		const uint cur = ord[i];
		int j = i - 1;
		while (j >= 0 && strcmp (names[ord[j]], names[cur]) > 0)
		{
			ord[j + 1] = ord[j];
			j--;
		}
		ord[j + 1] = cur;
	}

	ccp *sorted = CALLOC (n_names, sizeof (ccp));
	uint *sorted_off = CALLOC (n_names, sizeof (uint));
	for (uint i = 0; i < n_names; i++)
		sorted[i] = names[ord[i]];

	const uint pool_size = CalcPoolBANIM (sorted, n_names, data_end, sorted_off);
	const uint total_size = data_end + pool_size;
	for (uint i = 0; i < n_names; i++)
		name_off[ord[i]] = sorted_off[i];

	u8 *buf = CALLOC (total_size, 1);
	WritePoolBANIM (buf, sorted, n_names, data_end);
	FREE (sorted);
	FREE (sorted_off);
	FREE (ord);

	//--- header

	memcpy (buf, SCN_MAGIC, 4);
	// version 4 declares the whole file, version 5 stops at the data section
	scn_w32 (buf + 4, scn->version == 5 ? data_end : total_size);
	scn_w32 (buf + 8, scn->version);
	scn_w32 (buf + 0xc, 0); // brres_offset, patched by the container

	scn_w32 (buf + 0x10, group_off);
	for (uint s = 0; s < SCN0_N_SECT; s++)
		scn_w32 (buf + 0x14 + 4 * s, typed_off[s]);
	if (scn->version == 5)
		scn_w32 (buf + 0x28, 0); // user_data_offset: none

	const uint hoff = scn->version == 5 ? 0x2c : 0x28;
	scn_w32 (buf + hoff, name_off[name_slot]);
	scn_w32 (buf + hoff + 4, scn->orig_path ? name_off[orig_slot] : 0);
	scn_w16 (buf + hoff + 8, (u16)scn->n_frames);
	scn_w16 (buf + hoff + 10, (u16)scn->spec_light);
	scn_w32 (buf + hoff + 12, scn->loop);
	for (uint s = 0; s < SCN0_N_SECT; s++)
		scn_w16 (buf + hoff + 16 + 2 * s, (u16)scn->n_node[s]);
	scn_w16 (buf + hoff + 26, 0); // pad

	//--- outer group

	u8 *group = buf + group_off;
	scn_w32 (group, 8 + 16 * (n_sect + 1));
	scn_w32 (group + 4, n_sect);

	brres_info_t *info = CALLOC (n_sect + 1, sizeof (*info));
	info[0].id = 0xffff;
	info[0].name = "";
	info[0].nlen = 0;
	for (uint g = 0; g < n_sect; g++)
	{
		info[g + 1].name = scn0_sect_name[sect_list[g]];
		info[g + 1].nlen = strlen (info[g + 1].name);
		CalcEntryBRRES (info, g + 1);
	}
	for (uint g = 0; g <= n_sect; g++)
	{
		u8 *rec = group + 8 + g * 16;
		scn_w16 (rec, info[g].id);
		scn_w16 (rec + 2, 0);
		scn_w16 (rec + 4, info[g].left_idx);
		scn_w16 (rec + 6, info[g].right_idx);
		if (g)
		{
			scn_w32 (rec + 8, name_off[sect_slot + g - 1] - group_off);
			scn_w32 (rec + 12, sect_rel[g - 1] - group_off);
		}
	}
	FREE (info);

	//--- per section groups and their nodes

	uint node_i = 0;
	uint payload_pos = payload_start;
	uint name_i = node_slot;
	for (uint g = 0; g < n_sect; g++)
	{
		const scn0_sect_t sect = sect_list[g];
		const uint n_node = scn->n_node[sect];
		const uint nsize = scn0_node_size[sect];
		const uint sabs = sect_rel[g];

		u8 *sgroup = buf + sabs;
		scn_w32 (sgroup, 8 + 16 * (n_node + 1));
		scn_w32 (sgroup + 4, n_node);

		brres_info_t *ni2 = CALLOC (n_node + 1, sizeof (*ni2));
		ni2[0].id = 0xffff;
		ni2[0].name = "";
		ni2[0].nlen = 0;
		for (uint i = 0; i < n_node; i++)
		{
			ni2[i + 1].name = scn->node[sect][i].name;
			ni2[i + 1].nlen = strlen (ni2[i + 1].name);
			CalcEntryBRRES (ni2, i + 1);
		}

		for (uint i = 0; i <= n_node; i++)
		{
			u8 *rec = sgroup + 8 + i * 16;
			scn_w16 (rec, ni2[i].id);
			scn_w16 (rec + 2, 0);
			scn_w16 (rec + 4, ni2[i].left_idx);
			scn_w16 (rec + 6, ni2[i].right_idx);
			if (i)
			{
				scn_w32 (rec + 8, name_off[name_i + i - 1] - sabs);
				scn_w32 (rec + 12, node_abs[node_i + i - 1] - sabs);
			}
		}
		FREE (ni2);

		for (uint i = 0; i < n_node; i++)
		{
			const scn0_node_t *nd = scn->node[sect] + i;
			const uint nabs = node_abs[node_i + i];
			u8 *out = buf + nabs;
			memcpy (out, nd->raw, nsize);

			scn_w32 (out, nsize); // _length
			scn_w32 (out + 4, (u32)(-(s32)nabs)); // _scn0Offset

			// The node's own _stringOffset (and, for a light set, its ambient
			// name and entry names) point into the *shared* BRRES string pool,
			// so they cannot be recomputed standalone and are kept verbatim.

			for (uint b = 0; b < nd->n_blob; b++)
			{
				const scn0_blob_t *bl = nd->blob + b;
				const uint slot = nabs + bl->slot_off;
				scn_w32 (buf + slot, (u32)((s32)payload_pos - (s32)slot));
				memcpy (buf + payload_pos, bl->data, bl->size);
				payload_pos += bl->size;
			}
		}

		node_i += n_node;
		name_i += n_node;
	}

	FREE (sect_rel);
	FREE (node_abs);
	FREE (names);
	FREE (name_off);

	//--- write it out

	File_t F;
	enumError err = CreateFileOpt (&F, true, fname, testmode, scn->fname);
	if (err > ERR_WARNING || !F.f)
	{
		FREE (buf);
		ResetFile (&F, set_time ? opt_preserve : 0);
		return err;
	}

	if (fwrite (buf, 1, total_size, F.f) != total_size)
		err = ERROR1 (ERR_WRITE_FAILED, "Write to SCN0 file failed: %s\n", fname);

	FREE (buf);
	ResetFile (&F, set_time ? opt_preserve : 0);
	return err;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			text helpers			///////////////
///////////////////////////////////////////////////////////////////////////////

static void PrintQuotedSCN0 (FILE *f, ccp text)
{
	fputc ('"', f);
	for (ccp p = text; p && *p; p++)
	{
		if (*p == '"' || *p == '\\')
			fputc ('\\', f);
		fputc (*p, f);
	}
	fputc ('"', f);
}

///////////////////////////////////////////////////////////////////////////////

static ccp ScanQuotedSCN0 (ccp src, ccp *res)
{
	while (*src == ' ' || *src == '\t')
		src++;
	char buf[1000];
	uint len = 0;
	if (*src == '"')
	{
		src++;
		while (*src && *src != '"' && len < sizeof (buf) - 1)
		{
			if (*src == '\\' && src[1])
				src++;
			buf[len++] = *src++;
		}
		if (*src == '"')
			src++;
	}
	else
		while (*src && *src != ' ' && *src != '\t' && *src != '\n' && len < sizeof (buf) - 1)
			buf[len++] = *src++;
	buf[len] = 0;
	if (res)
		*res = STRDUP (buf);
	return src;
}

///////////////////////////////////////////////////////////////////////////////

static void PrintHexSCN0 (FILE *f, ccp key, const u8 *data, uint size)
{
	for (uint p = 0; p < size; p += 24)
	{
		const uint n = size - p < 24 ? size - p : 24;
		fprintf (f, "  %s ", key);
		for (uint i = 0; i < n; i++)
			fprintf (f, "%02x", data[p + i]);
		fputc ('\n', f);
	}
}

///////////////////////////////////////////////////////////////////////////////

static uint ScanHexSCN0 (ccp src, u8 **dest, uint *fill, uint *alloced)
{
	uint added = 0;
	while (*src)
	{
		while (*src == ' ' || *src == '\t')
			src++;
		if (!isxdigit ((int)(uchar)src[0]) || !isxdigit ((int)(uchar)src[1]))
			break;
		char hex[3] = { src[0], src[1], 0 };
		if (*fill == *alloced)
		{
			*alloced = *alloced ? 2 * *alloced : 64;
			*dest = REALLOC (*dest, *alloced);
		}
		(*dest)[(*fill)++] = (u8)strtoul (hex, 0, 16);
		src += 2;
		added++;
	}
	return added;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			SaveTextSCN0()			///////////////
///////////////////////////////////////////////////////////////////////////////

enumError SaveTextSCN0 (scn0_t *scn, ccp fname, bool set_time)
{
	DASSERT (scn);
	DASSERT (fname);

	File_t F;
	enumError err = CreateFileOpt (&F, true, fname, testmode, scn->fname);
	if (err > ERR_WARNING || !F.f)
	{
		ResetFile (&F, set_time ? opt_preserve : 0);
		return err;
	}

	fprintf (F.f, "#SCN0\n# Wiimms SZS Tools Plus -- SCN0 scene animation\n");
	if (scn->fname)
		fprintf (F.f, "# decoded from %s\n", scn->fname);
	fprintf (F.f, "\nversion    = %u\n", scn->version);
	fprintf (F.f, "name       = ");
	PrintQuotedSCN0 (F.f, scn->name ? scn->name : "");
	fputc ('\n', F.f);
	if (scn->orig_path)
	{
		fprintf (F.f, "orig-path  = ");
		PrintQuotedSCN0 (F.f, scn->orig_path);
		fputc ('\n', F.f);
	}
	fprintf (F.f, "n-frames   = %u\n", scn->n_frames);
	fprintf (F.f, "spec-light = %u\n", scn->spec_light);
	fprintf (F.f, "loop       = %u\n", scn->loop);

	fprintf (F.f,
		"\n# One block per scene node. 'raw' is the node struct verbatim: its\n"
		"# offset words are recomputed on encode, every other field is kept as\n"
		"# found. A node's own name offset points into the shared BRRES string\n"
		"# pool, so it cannot be resolved standalone and is preserved as is.\n"
		"#\n"
		"# 'blob' gives an animated slot: the byte offset of its slot word inside\n"
		"# the node, the payload kind (k=keyframe set, c=colour array, v=enable\n"
		"# bits) and the payload itself. Slots are listed in flag-bit order,\n"
		"# which is the order retail writes them in.\n");

	for (uint s = 0; s < SCN0_N_SECT; s++)
	{
		if (!scn->n_node[s])
			continue;
		fprintf (F.f, "\nsection %s\n", scn0_sect_key[s]);

		for (uint i = 0; i < scn->n_node[s]; i++)
		{
			const scn0_node_t *nd = scn->node[s] + i;
			fprintf (F.f, "\nnode ");
			PrintQuotedSCN0 (F.f, nd->name);
			fputc ('\n', F.f);
			PrintHexSCN0 (F.f, "raw", nd->raw, scn0_node_size[s]);

			for (uint b = 0; b < nd->n_blob; b++)
			{
				const scn0_blob_t *bl = nd->blob + b;
				fprintf (F.f, "  blob 0x%02x %c %u\n", bl->slot_off, bl->kind, bl->size);
				if (bl->kind == 'k')
				{
					const uint n_key = scn_rd16 (bl->data);
					fprintf (F.f, "  # %u key%s (tangent, frame, value):\n", n_key,
						n_key == 1 ? "" : "s");
					for (uint k = 0; k < n_key; k++)
					{
						const u8 *p = bl->data + 4 + 12 * k;
						fprintf (F.f, "  #   %.9g %.9g %.9g\n", scn_rdf (p), scn_rdf (p + 4),
							scn_rdf (p + 8));
					}
				}
				PrintHexSCN0 (F.f, "data", bl->data, bl->size);
			}
		}
	}

	ResetFile (&F, set_time ? opt_preserve : 0);
	return err;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			ScanTextSCN0()			///////////////
///////////////////////////////////////////////////////////////////////////////

enumError ScanTextSCN0 (scn0_t *scn, bool init_scn, ccp src_fname)
{
	DASSERT (scn);
	DASSERT (src_fname);

	FILE *f = fopen (src_fname, "r");
	if (!f)
		return ERROR0 (ERR_CANT_OPEN, "Can't open SCN0 text file: %s\n", src_fname);

	if (init_scn)
		InitializeSCN0 (scn);
	else
		ResetSCN0 (scn);
	scn->fname = STRDUP (src_fname);

	char line[8000];
	bool have_magic = false;
	scn0_sect_t cur_sect = SCN0_N_SECT;
	scn0_node_t *cur = 0;
	uint raw_fill = 0;

	// the blob currently being filled by 'data' lines
	u8 *bl_data = 0;
	uint bl_fill = 0, bl_alloced = 0, bl_size = 0, bl_off = 0;
	char bl_kind = 0;

	#define FLUSH_BLOB()                                          \
		if (bl_kind && cur)                                       \
		{                                                         \
			scn0_blob_t *bl = AppendBlobSCN0 (cur);               \
			bl->slot_off = (u16)bl_off;                           \
			bl->kind = bl_kind;                                   \
			bl->size = bl_fill < bl_size ? bl_fill : bl_size;     \
			bl->data = CALLOC (bl->size ? bl->size : 1, 1);       \
			if (bl_data && bl->size)                              \
				memcpy (bl->data, bl_data, bl->size);             \
		}                                                         \
		FREE (bl_data);                                           \
		bl_data = 0;                                              \
		bl_fill = bl_alloced = bl_size = 0;                       \
		bl_kind = 0;

	while (fgets (line, sizeof (line), f))
	{
		ccp s = line;
		while (*s == ' ' || *s == '\t')
			s++;

		if (!have_magic)
		{
			if (!strncmp (s, "#SCN0", 5))
			{
				have_magic = true;
				continue;
			}
			if (*s == '#' || *s == '\n' || !*s)
				continue;
			fclose (f);
			FREE (bl_data);
			return ERROR0 (ERR_WRONG_FILE_TYPE, "Not a SCN0 text file: %s\n", src_fname);
		}

		if (*s == '#' || *s == '\n' || !*s)
			continue;

		if (!strncmp (s, "raw", 3) && cur)
		{
			u8 *dest = cur->raw;
			ccp p = s + 3;
			while (*p)
			{
				while (*p == ' ' || *p == '\t')
					p++;
				if (!isxdigit ((int)(uchar)p[0]) || !isxdigit ((int)(uchar)p[1]))
					break;
				char hex[3] = { p[0], p[1], 0 };
				if (raw_fill < SCN0_MAX_NODE_SIZE)
					dest[raw_fill++] = (u8)strtoul (hex, 0, 16);
				p += 2;
			}
		}
		else if (!strncmp (s, "data", 4) && bl_kind)
			ScanHexSCN0 (s + 4, &bl_data, &bl_fill, &bl_alloced);
		else if (!strncmp (s, "blob", 4) && cur)
		{
			FLUSH_BLOB ();
			char *end;
			bl_off = (uint)strtoul (s + 4, &end, 0);
			while (*end == ' ' || *end == '\t')
				end++;
			bl_kind = *end ? *end++ : 'k';
			bl_size = (uint)strtoul (end, 0, 10);
		}
		else if (!strncmp (s, "node", 4))
		{
			FLUSH_BLOB ();
			if (cur_sect == SCN0_N_SECT)
			{
				fclose (f);
				FREE (bl_data);
				return ERROR0 (ERR_INVALID_DATA, "SCN0: 'node' before any 'section'.\n");
			}
			ccp name = 0;
			ScanQuotedSCN0 (s + 4, &name);
			cur = AppendNodeSCN0 (scn, cur_sect, name);
			FreeString (name);
			raw_fill = 0;
		}
		else if (!strncmp (s, "section", 7))
		{
			FLUSH_BLOB ();
			cur = 0;
			ccp key = 0;
			ScanQuotedSCN0 (s + 7, &key);
			cur_sect = SCN0_N_SECT;
			for (uint i = 0; i < SCN0_N_SECT; i++)
				if (!strcmp (key, scn0_sect_key[i]))
				{
					cur_sect = i;
					break;
				}
			const bool bad = cur_sect == SCN0_N_SECT;
			FreeString (key);
			if (bad)
			{
				fclose (f);
				FREE (bl_data);
				return ERROR0 (ERR_INVALID_DATA, "SCN0: unknown section in %s.\n", src_fname);
			}
		}
		else if (!strncmp (s, "version", 7))
		{
			ccp v = strchr (s, '=');
			if (v)
				scn->version = strtoul (v + 1, 0, 10);
		}
		else if (!strncmp (s, "name", 4) && (s[4] == ' ' || s[4] == '\t' || s[4] == '='))
		{
			ccp v = strchr (s, '=');
			if (v)
			{
				FreeString (scn->name);
				ScanQuotedSCN0 (v + 1, &scn->name);
			}
		}
		else if (!strncmp (s, "orig-path", 9))
		{
			ccp v = strchr (s, '=');
			if (v)
			{
				FreeString (scn->orig_path);
				ScanQuotedSCN0 (v + 1, &scn->orig_path);
			}
		}
		else if (!strncmp (s, "n-frames", 8))
		{
			ccp v = strchr (s, '=');
			if (v)
				scn->n_frames = strtoul (v + 1, 0, 10);
		}
		else if (!strncmp (s, "spec-light", 10))
		{
			ccp v = strchr (s, '=');
			if (v)
				scn->spec_light = strtoul (v + 1, 0, 10);
		}
		else if (!strncmp (s, "loop", 4))
		{
			ccp v = strchr (s, '=');
			if (v)
				scn->loop = (u32)strtoul (v + 1, 0, 0);
		}
	}

	FLUSH_BLOB ();
	#undef FLUSH_BLOB

	fclose (f);
	return ERR_OK;
}

///////////////////////////////////////////////////////////////////////////////
