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
#include "lib-shp.h"
#include "lib-brres.h"
#include "lib-brres-anim.h"
#include "lib-szs.h"

///////////////////////////////////////////////////////////////////////////////
///////////////			byte order helper		///////////////
///////////////////////////////////////////////////////////////////////////////
// BRRES/SHP0 data is always big endian (Wii).

static inline u16 shp_rd16 (const u8 *p) { return (u16)p[0] << 8 | p[1]; }

static inline s16 shp_rds16 (const u8 *p) { return (s16)(u16)((u16)p[0] << 8 | p[1]); }

static inline u32 shp_rd32 (const u8 *p)
{
	return (u32)p[0] << 24 | (u32)p[1] << 16 | (u32)p[2] << 8 | p[3];
}

static inline float shp_rdf (const u8 *p)
{
	const u32 v = shp_rd32 (p);
	float f;
	memcpy (&f, &v, 4);
	return f;
}

static inline void shp_w16 (u8 *p, u16 v)
{
	p[0] = v >> 8;
	p[1] = (u8)v;
}

static inline void shp_w32 (u8 *p, u32 v)
{
	p[0] = v >> 24;
	p[1] = v >> 16;
	p[2] = v >> 8;
	p[3] = (u8)v;
}

static inline void shp_wf (u8 *p, float f)
{
	u32 v;
	memcpy (&v, &f, 4);
	shp_w32 (p, v);
}

static inline uint shp_align (uint v, uint a) { return v + a - 1 & ~(a - 1); }

//
///////////////////////////////////////////////////////////////////////////////
///////////////			setup & teardown		///////////////
///////////////////////////////////////////////////////////////////////////////

void InitializeSHP0 (shp0_t *shp)
{
	DASSERT (shp);
	memset (shp, 0, sizeof (*shp));
	shp->version = SHP0_DEFAULT_VERSION;
}

///////////////////////////////////////////////////////////////////////////////

void ResetSHP0 (shp0_t *shp)
{
	DASSERT (shp);

	for (uint i = 0; i < shp->n_entry; i++)
	{
		shp0_entry_t *e = shp->entry + i;
		FreeString (e->name);
		for (uint t = 0; t < e->n_track; t++)
			FREE (e->track[t].key);
		FREE (e->track);
	}
	FREE (shp->entry);

	for (uint i = 0; i < shp->n_str; i++)
		FreeString (shp->str[i]);
	FREE (shp->str);
	FREE (shp->str_off);

	FreeString (shp->fname);
	FreeString (shp->name);
	FreeString (shp->orig_path);

	InitializeSHP0 (shp);
}

///////////////////////////////////////////////////////////////////////////////

shp0_entry_t *AppendEntrySHP0 (shp0_t *shp, ccp name)
{
	DASSERT (shp);

	if (shp->n_entry == shp->n_entry_alloced)
	{
		shp->n_entry_alloced = shp->n_entry_alloced ? 2 * shp->n_entry_alloced : 8;
		shp->entry = REALLOC (shp->entry, shp->n_entry_alloced * sizeof (*shp->entry));
	}

	shp0_entry_t *e = shp->entry + shp->n_entry++;
	memset (e, 0, sizeof (*e));
	e->name = STRDUP (name ? name : "");
	return e;
}

///////////////////////////////////////////////////////////////////////////////

static shp0_track_t *AppendTrackSHP0 (shp0_entry_t *e)
{
	e->track = REALLOC (e->track, (e->n_track + 1) * sizeof (*e->track));
	shp0_track_t *t = e->track + e->n_track++;
	memset (t, 0, sizeof (*t));
	t->vertex_idx = -1;
	return t;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			ScanRawSHP0()			///////////////
///////////////////////////////////////////////////////////////////////////////

enumError ScanRawSHP0 (shp0_t *shp, bool init_shp, const void *data, uint data_size)
{
	DASSERT (shp);
	DASSERT (data);

	if (init_shp)
		InitializeSHP0 (shp);
	else
		ResetSHP0 (shp);

	const u8 *base = data;

	if (data_size < 0x2c || memcmp (base, SHP_MAGIC, 4))
		return ERROR0 (ERR_INVALID_DATA, "Not a SHP0 file.\n");

	const u32 version = shp_rd32 (base + 8);
	if (version < SHP0_MIN_VERSION || version > SHP0_MAX_VERSION)
		return ERROR0 (ERR_INVALID_DATA, "SHP0: unsupported version %u.\n", version);
	shp->version = version;

	const uint head_size = version == 4 ? 0x2c : 0x28;
	if (data_size < head_size)
		return ERROR0 (ERR_INVALID_DATA, "SHP0: file too short.\n");

	const u32 group_off = shp_rd32 (base + 0x10);
	const u32 strlist_off = shp_rd32 (base + 0x14);

	uint off = 0x18;
	if (version == 4)
		off += 4; // user_data_offset, not represented
	const u32 name_off = shp_rd32 (base + off);
	off += 4;
	const u32 orig_off = shp_rd32 (base + off);
	off += 4;
	shp->n_frames = shp_rd16 (base + off);
	off += 2;
	const uint n_str = shp_rd16 (base + off);
	off += 2;
	shp->loop = shp_rd32 (base + off) != 0;

	if (name_off && name_off < data_size)
		shp->name = STRDUP ((ccp)(base + name_off));
	if (orig_off && orig_off < data_size)
		shp->orig_path = STRDUP ((ccp)(base + orig_off));

	//--- the vertex-set string list

	shp->n_str = n_str;
	shp->str = CALLOC (n_str ? n_str : 1, sizeof (ccp));
	shp->str_off = CALLOC (n_str ? n_str : 1, sizeof (u32));

	uint n_unresolved = 0;
	for (uint i = 0; i < n_str; i++)
	{
		if (strlist_off + 4 * i + 4 > data_size)
			return ERROR0 (ERR_INVALID_DATA, "SHP0: string list runs past end of file.\n");
		const u32 rel = shp_rd32 (base + strlist_off + 4 * i);
		shp->str_off[i] = rel;
		// BrawlLib resolves these as (string_list_base + offset)
		const u64 abs = (u64)strlist_off + rel;
		if (abs < data_size)
			shp->str[i] = STRDUP ((ccp)(base + abs));
		else
			n_unresolved++;
	}

	//--- the resource group

	if (group_off + 8 > data_size)
		return ERROR0 (ERR_INVALID_DATA, "SHP0: bad group offset.\n");
	const uint n_entry = shp_rd32 (base + group_off + 4);

	for (uint i = 0; i < n_entry; i++)
	{
		const uint rec = group_off + 8 + 16 * (i + 1);
		if (rec + 16 > data_size)
			return ERROR0 (ERR_INVALID_DATA, "SHP0: group record %u out of range.\n", i);

		const u32 nrel = shp_rd32 (base + rec + 8);
		const u32 drel = shp_rd32 (base + rec + 12);
		const u64 nabs = (u64)group_off + nrel;
		const u64 dabs = (u64)group_off + drel;
		if (dabs + 0x14 > data_size)
			return ERROR0 (ERR_INVALID_DATA, "SHP0: entry %u out of range.\n", i);

		shp0_entry_t *e = AppendEntrySHP0 (shp, nabs < data_size ? (ccp)(base + nabs) : "");
		const u8 *ent = base + dabs;
		e->flags = shp_rd32 (ent);
		e->name_idx = shp_rds16 (ent + 8);
		const uint n_track = (u16)shp_rd16 (ent + 10);
		const u32 fixed_flags = shp_rd32 (ent + 0xc);
		const u32 idx_off = shp_rd32 (ent + 0x10);

		// The u16 index array sits at ent+idx_off; the bint offset array of
		// the same length sits immediately before it.
		const u64 idx_abs = dabs + idx_off;
		const u64 ofs_abs = idx_abs - 4 * (u64)n_track;
		if (idx_abs + 2 * (u64)n_track > data_size || ofs_abs > data_size)
			return ERROR0 (ERR_INVALID_DATA, "SHP0: entry %u track table out of range.\n", i);

		for (uint t = 0; t < n_track; t++)
		{
			shp0_track_t *tr = AppendTrackSHP0 (e);
			tr->vertex_idx = shp_rds16 (base + idx_abs + 2 * t);

			const u64 slot = ofs_abs + 4 * (u64)t;
			const u32 raw = shp_rd32 (base + slot);
			if (fixed_flags & 1u << t)
			{
				tr->is_fixed = true;
				memcpy (&tr->fixed, &raw, 4);
				{
					const float f = shp_rdf (base + slot);
					tr->fixed = f;
				}
				continue;
			}

			const u64 kabs = slot + raw;
			if (kabs + 8 > data_size)
				return ERROR0 (ERR_INVALID_DATA, "SHP0: keyframe set out of range.\n");
			const uint n_key = shp_rd16 (base + kabs);
			tr->recip = shp_rdf (base + kabs + 4);
			if (kabs + 8 + 12 * (u64)n_key > data_size)
				return ERROR0 (ERR_INVALID_DATA, "SHP0: keyframe data out of range.\n");

			tr->n_key = n_key;
			tr->key = CALLOC (n_key ? n_key : 1, sizeof (*tr->key));
			for (uint k = 0; k < n_key; k++)
			{
				const u8 *p = base + kabs + 8 + 12 * k;
				tr->key[k].frame = shp_rdf (p);
				tr->key[k].value = shp_rdf (p + 4);
				tr->key[k].tangent = shp_rdf (p + 8);
			}
		}
	}

	if (n_unresolved)
		ERROR0 (ERR_WARNING,
			"SHP0: %u of %u vertex set name%s point outside this file. Retail SHP0"
			" files name their morph targets through the *shared* BRRES string pool,"
			" so a SHP0 extracted on its own cannot resolve them. The raw offsets are"
			" preserved, so re-encoding still reproduces the original bytes.\n",
			n_unresolved, n_str, n_unresolved == 1 ? "" : "s");

	return ERR_OK;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			SaveRawSHP0()			///////////////
///////////////////////////////////////////////////////////////////////////////

enumError SaveRawSHP0 (shp0_t *shp, ccp fname, bool set_time)
{
	DASSERT (shp);
	DASSERT (fname);

	const uint head_size = shp->version == 4 ? 0x2c : 0x28;
	const uint group_off = head_size;
	const uint group_head_size = 8 + (shp->n_entry + 1) * 16;

	//--- entry structs, each followed by its offset array and index array

	uint *entry_rel = CALLOC (shp->n_entry ? shp->n_entry : 1, sizeof (*entry_rel));
	uint *table_rel = CALLOC (shp->n_entry ? shp->n_entry : 1, sizeof (*table_rel));
	uint data_size = group_head_size;
	for (uint i = 0; i < shp->n_entry; i++)
	{
		const uint n = shp->entry[i].n_track;
		entry_rel[i] = data_size;
		table_rel[i] = data_size + 0x14; // the bint offset array
		// 0x14 entry + n*4 offsets + n*2 indices, padded to 4
		data_size = shp_align (data_size + 0x14 + 4 * n + 2 * n, 4);
	}

	//--- keyframe sets, appended behind all entries

	const uint key_start = data_size;
	uint n_slot = 0;
	for (uint i = 0; i < shp->n_entry; i++)
		for (uint t = 0; t < shp->entry[i].n_track; t++)
			if (!shp->entry[i].track[t].is_fixed)
				n_slot++;

	uint *slot_rel = CALLOC (n_slot ? n_slot : 1, sizeof (*slot_rel));
	uint slot_idx = 0;
	for (uint i = 0; i < shp->n_entry; i++)
		for (uint t = 0; t < shp->entry[i].n_track; t++)
		{
			const shp0_track_t *tr = shp->entry[i].track + t;
			if (tr->is_fixed)
				continue;
			slot_rel[slot_idx++] = data_size;
			data_size += 8 + 12 * tr->n_key;
		}
	(void)key_start;

	//--- the vertex-set string list closes the declared data section

	const uint strlist_rel = data_size;
	data_size += 4 * shp->n_str;

	// the declared size stops at the end of the data section; the name pool
	// behind it belongs to the enclosing BRRES
	const uint file_size = group_off + data_size;

	//--- string pool: animation name, orig path, and one name per entry

	const uint n_names = shp->n_entry + 2;
	ccp *names = CALLOC (n_names, sizeof (ccp));
	uint *name_off = CALLOC (n_names, sizeof (uint));
	names[0] = shp->name ? shp->name : "";
	names[1] = shp->orig_path ? shp->orig_path : "";
	for (uint i = 0; i < shp->n_entry; i++)
		names[i + 2] = shp->entry[i].name;

	// Retail lays the pool out in ordinal name order rather than entry order
	// (BrawlLib's shared string table sorts before writing), so sort, then
	// scatter the offsets back onto the logical slots.
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

	const uint pool_size = CalcPoolBANIM (sorted, n_names, file_size, sorted_off);
	const uint total_size = file_size + pool_size;
	for (uint i = 0; i < n_names; i++)
		name_off[ord[i]] = sorted_off[i];

	u8 *buf = CALLOC (total_size, 1);
	WritePoolBANIM (buf, sorted, n_names, file_size);
	FREE (sorted);
	FREE (sorted_off);
	FREE (ord);

	//--- header

	memcpy (buf, SHP_MAGIC, 4);
	shp_w32 (buf + 4, file_size);
	shp_w32 (buf + 8, shp->version);
	shp_w32 (buf + 0xc, 0); // brres_offset, patched by the container

	shp_w32 (buf + 0x10, group_off);
	shp_w32 (buf + 0x14, group_off + strlist_rel);
	uint off = 0x18;
	if (shp->version == 4)
	{
		shp_w32 (buf + off, 0); // user_data_offset: none
		off += 4;
	}
	shp_w32 (buf + off, name_off[0]);
	off += 4;
	shp_w32 (buf + off, shp->orig_path ? name_off[1] : 0);
	off += 4;
	shp_w16 (buf + off, (u16)shp->n_frames);
	off += 2;
	shp_w16 (buf + off, (u16)shp->n_str);
	off += 2;
	shp_w32 (buf + off, shp->loop ? 1 : 0);

	//--- resource group, with the NW4R lookup tree

	u8 *group = buf + group_off;
	shp_w32 (group, group_head_size);
	shp_w32 (group + 4, shp->n_entry);

	brres_info_t *info = CALLOC (shp->n_entry + 1, sizeof (*info));
	info[0].id = 0xffff;
	info[0].name = "";
	info[0].nlen = 0;
	for (uint i = 0; i < shp->n_entry; i++)
	{
		info[i + 1].name = shp->entry[i].name;
		info[i + 1].nlen = strlen (shp->entry[i].name);
		CalcEntryBRRES (info, i + 1);
	}

	for (uint i = 0; i <= shp->n_entry; i++)
	{
		u8 *rec = group + 8 + i * 16;
		shp_w16 (rec, info[i].id);
		shp_w16 (rec + 2, 0);
		shp_w16 (rec + 4, info[i].left_idx);
		shp_w16 (rec + 6, info[i].right_idx);
		if (i)
		{
			shp_w32 (rec + 8, name_off[i + 1] - group_off);
			shp_w32 (rec + 12, entry_rel[i - 1]);
		}
	}
	FREE (info);

	//--- entries

	slot_idx = 0;
	for (uint i = 0; i < shp->n_entry; i++)
	{
		const shp0_entry_t *e = shp->entry + i;
		u8 *ent = group + entry_rel[i];
		const uint ent_abs = group_off + entry_rel[i];

		u32 fixed_flags = 0;
		for (uint t = 0; t < e->n_track; t++)
			if (e->track[t].is_fixed)
				fixed_flags |= 1u << t;

		shp_w32 (ent, e->flags);
		shp_w32 (ent + 4, name_off[i + 2] - ent_abs);
		shp_w16 (ent + 8, (u16)e->name_idx);
		shp_w16 (ent + 10, (u16)e->n_track);
		shp_w32 (ent + 0xc, fixed_flags);
		// _indiciesOffset points at the u16 index array, which follows the
		// bint offset array of the same length
		shp_w32 (ent + 0x10, table_rel[i] + 4 * e->n_track - entry_rel[i]);

		u8 *ofs = group + table_rel[i];
		u8 *idx = ofs + 4 * e->n_track;
		for (uint t = 0; t < e->n_track; t++)
		{
			const shp0_track_t *tr = e->track + t;
			shp_w16 (idx + 2 * t, (u16)tr->vertex_idx);

			if (tr->is_fixed)
			{
				shp_wf (ofs + 4 * t, tr->fixed);
				continue;
			}

			const uint krel = slot_rel[slot_idx++];
			const uint slot_abs = group_off + table_rel[i] + 4 * t;
			shp_w32 (ofs + 4 * t, group_off + krel - slot_abs);

			u8 *kf = group + krel;
			shp_w16 (kf, (u16)tr->n_key);
			shp_w16 (kf + 2, 0);
			shp_wf (kf + 4, tr->recip);
			for (uint k = 0; k < tr->n_key; k++)
			{
				u8 *p = kf + 8 + 12 * k;
				shp_wf (p, tr->key[k].frame);
				shp_wf (p + 4, tr->key[k].value);
				shp_wf (p + 8, tr->key[k].tangent);
			}
		}
	}

	//--- the string list: raw offsets are preserved verbatim, so entries that
	// pointed into the shared BRRES pool survive a round trip unchanged

	u8 *sl = group + strlist_rel;
	for (uint i = 0; i < shp->n_str; i++)
		shp_w32 (sl + 4 * i, shp->str_off[i]);

	FREE (entry_rel);
	FREE (table_rel);
	FREE (slot_rel);
	FREE (names);
	FREE (name_off);

	//--- write it out

	File_t F;
	enumError err = CreateFileOpt (&F, true, fname, testmode, shp->fname);
	if (err > ERR_WARNING || !F.f)
	{
		FREE (buf);
		ResetFile (&F, set_time ? opt_preserve : 0);
		return err;
	}

	if (fwrite (buf, 1, total_size, F.f) != total_size)
		err = ERROR1 (ERR_WRITE_FAILED, "Write to SHP0 file failed: %s\n", fname);

	FREE (buf);
	ResetFile (&F, set_time ? opt_preserve : 0);
	return err;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			text helpers			///////////////
///////////////////////////////////////////////////////////////////////////////

static void PrintQuotedSHP0 (FILE *f, ccp text)
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

static ccp ScanQuotedSHP0 (ccp src, ccp *res)
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

//
///////////////////////////////////////////////////////////////////////////////
///////////////			SaveTextSHP0()			///////////////
///////////////////////////////////////////////////////////////////////////////

enumError SaveTextSHP0 (shp0_t *shp, ccp fname, bool set_time)
{
	DASSERT (shp);
	DASSERT (fname);

	File_t F;
	enumError err = CreateFileOpt (&F, true, fname, testmode, shp->fname);
	if (err > ERR_WARNING || !F.f)
	{
		ResetFile (&F, set_time ? opt_preserve : 0);
		return err;
	}

	fprintf (F.f, "#SHP0\n# Wiimms SZS Tools Plus -- SHP0 vertex morph animation\n");
	if (shp->fname)
		fprintf (F.f, "# decoded from %s\n", shp->fname);
	fprintf (F.f, "\nversion   = %u\n", shp->version);
	fprintf (F.f, "name      = ");
	PrintQuotedSHP0 (F.f, shp->name ? shp->name : "");
	fputc ('\n', F.f);
	if (shp->orig_path)
	{
		fprintf (F.f, "orig-path = ");
		PrintQuotedSHP0 (F.f, shp->orig_path);
		fputc ('\n', F.f);
	}
	fprintf (F.f, "n-frames  = %u\n", shp->n_frames);
	fprintf (F.f, "loop      = %u\n", shp->loop);

	fprintf (F.f,
		"\n# The vertex-set string list. Retail files point these at the shared\n"
		"# BRRES string pool, so the name is usually unresolvable standalone; the\n"
		"# raw offset is what the encoder writes back, so it must be kept.\n");
	for (uint i = 0; i < shp->n_str; i++)
	{
		fprintf (F.f, "vertex-set %u 0x%08x ", i, shp->str_off[i]);
		if (shp->str[i])
			PrintQuotedSHP0 (F.f, shp->str[i]);
		else
			fputs ("-", F.f);
		fputc ('\n', F.f);
	}

	fprintf (F.f,
		"\n# One block per polygon. 'flags' and 'name-index' are raw NW4R fields\n"
		"# and are preserved verbatim. Each track is either a fixed value or a\n"
		"# keyframe list of 'frame value tangent' triples.\n");

	for (uint i = 0; i < shp->n_entry; i++)
	{
		const shp0_entry_t *e = shp->entry + i;
		fprintf (F.f, "\npolygon ");
		PrintQuotedSHP0 (F.f, e->name);
		fprintf (F.f, " flags 0x%08x name-index %d\n", e->flags, e->name_idx);

		for (uint t = 0; t < e->n_track; t++)
		{
			const shp0_track_t *tr = e->track + t;
			if (tr->is_fixed)
			{
				fprintf (F.f, "  track %d fixed %.9g\n", tr->vertex_idx, tr->fixed);
				continue;
			}
			fprintf (F.f, "  track %d keys %u recip %.9g\n", tr->vertex_idx, tr->n_key,
				tr->recip);
			for (uint k = 0; k < tr->n_key; k++)
				fprintf (F.f, "    %.9g %.9g %.9g\n", tr->key[k].frame, tr->key[k].value,
					tr->key[k].tangent);
		}
	}

	ResetFile (&F, set_time ? opt_preserve : 0);
	return err;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			ScanTextSHP0()			///////////////
///////////////////////////////////////////////////////////////////////////////

enumError ScanTextSHP0 (shp0_t *shp, bool init_shp, ccp src_fname)
{
	DASSERT (shp);
	DASSERT (src_fname);

	FILE *f = fopen (src_fname, "r");
	if (!f)
		return ERROR0 (ERR_CANT_OPEN, "Can't open SHP0 text file: %s\n", src_fname);

	if (init_shp)
		InitializeSHP0 (shp);
	else
		ResetSHP0 (shp);
	shp->fname = STRDUP (src_fname);

	// the string list is collected first and installed once its size is known
	uint n_str_alloced = 0;

	char line[8000];
	bool have_magic = false;
	shp0_entry_t *cur = 0;
	shp0_track_t *cur_track = 0;
	uint cur_fill = 0;

	while (fgets (line, sizeof (line), f))
	{
		ccp s = line;
		while (*s == ' ' || *s == '\t')
			s++;

		if (!have_magic)
		{
			if (!strncmp (s, "#SHP0", 5))
			{
				have_magic = true;
				continue;
			}
			if (*s == '#' || *s == '\n' || !*s)
				continue;
			fclose (f);
			return ERROR0 (ERR_WRONG_FILE_TYPE, "Not a SHP0 text file: %s\n", src_fname);
		}

		// a keyframe triple of the track currently being filled
		if (cur_track && (isdigit ((int)(uchar)*s) || *s == '-' || *s == '+' || *s == '.'))
		{
			if (cur_fill < cur_track->n_key)
			{
				char *end;
				shp0_key_t *k = cur_track->key + cur_fill++;
				k->frame = strtof (s, &end);
				k->value = strtof (end, &end);
				k->tangent = strtof (end, &end);
			}
			continue;
		}
		cur_track = 0;

		if (*s == '#' || *s == '\n' || !*s)
			continue;

		if (!strncmp (s, "version", 7))
		{
			ccp v = strchr (s, '=');
			if (v)
				shp->version = strtoul (v + 1, 0, 10);
		}
		else if (!strncmp (s, "name", 4) && (s[4] == ' ' || s[4] == '\t' || s[4] == '='))
		{
			ccp v = strchr (s, '=');
			if (v)
			{
				FreeString (shp->name);
				ScanQuotedSHP0 (v + 1, &shp->name);
			}
		}
		else if (!strncmp (s, "orig-path", 9))
		{
			ccp v = strchr (s, '=');
			if (v)
			{
				FreeString (shp->orig_path);
				ScanQuotedSHP0 (v + 1, &shp->orig_path);
			}
		}
		else if (!strncmp (s, "n-frames", 8))
		{
			ccp v = strchr (s, '=');
			if (v)
				shp->n_frames = strtoul (v + 1, 0, 10);
		}
		else if (!strncmp (s, "loop", 4))
		{
			ccp v = strchr (s, '=');
			if (v)
				shp->loop = strtoul (v + 1, 0, 10) != 0;
		}
		else if (!strncmp (s, "vertex-set", 10))
		{
			char *end;
			const uint idx = strtoul (s + 10, &end, 10);
			const u32 raw = (u32)strtoul (end, &end, 0);
			if (idx >= n_str_alloced)
			{
				const uint want = idx + 8;
				shp->str = REALLOC (shp->str, want * sizeof (ccp));
				shp->str_off = REALLOC (shp->str_off, want * sizeof (u32));
				memset (shp->str + n_str_alloced, 0, (want - n_str_alloced) * sizeof (ccp));
				memset (shp->str_off + n_str_alloced, 0, (want - n_str_alloced) * sizeof (u32));
				n_str_alloced = want;
			}
			shp->str_off[idx] = raw;
			while (*end == ' ' || *end == '\t')
				end++;
			if (*end == '"')
				ScanQuotedSHP0 (end, shp->str + idx);
			if (idx + 1 > shp->n_str)
				shp->n_str = idx + 1;
		}
		else if (!strncmp (s, "polygon", 7))
		{
			ccp name = 0;
			ccp p = ScanQuotedSHP0 (s + 7, &name);
			cur = AppendEntrySHP0 (shp, name);
			FreeString (name);
			ccp fl = strstr (p, "flags");
			if (fl)
				cur->flags = (u32)strtoul (fl + 5, 0, 0);
			ccp ni = strstr (p, "name-index");
			if (ni)
				cur->name_idx = (int)strtol (ni + 10, 0, 10);
		}
		else if (!strncmp (s, "track", 5) && cur)
		{
			char *end;
			const int vidx = (int)strtol (s + 5, &end, 10);
			shp0_track_t *tr = AppendTrackSHP0 (cur);
			tr->vertex_idx = vidx;

			while (*end == ' ' || *end == '\t')
				end++;
			if (!strncmp (end, "fixed", 5))
			{
				tr->is_fixed = true;
				tr->fixed = strtof (end + 5, 0);
			}
			else if (!strncmp (end, "keys", 4))
			{
				tr->n_key = strtoul (end + 4, &end, 10);
				ccp rc = strstr (end, "recip");
				if (rc)
					tr->recip = strtof (rc + 5, 0);
				tr->key = CALLOC (tr->n_key ? tr->n_key : 1, sizeof (*tr->key));
				cur_track = tr;
				cur_fill = 0;
			}
		}
	}

	fclose (f);
	return ERR_OK;
}

///////////////////////////////////////////////////////////////////////////////
