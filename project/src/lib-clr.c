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
#include "lib-clr.h"
#include "lib-brres.h"
#include "lib-brres-anim.h"
#include "lib-szs.h"

///////////////////////////////////////////////////////////////////////////////
///////////////			byte order helper		///////////////
///////////////////////////////////////////////////////////////////////////////
// BRRES/CLR0 data is always big endian (Wii).

static inline u16 clr_rd16 (const u8 *p) { return (u16)p[0] << 8 | p[1]; }

static inline u32 clr_rd32 (const u8 *p)
{
	return (u32)p[0] << 24 | (u32)p[1] << 16 | (u32)p[2] << 8 | p[3];
}

static inline void clr_w16 (u8 *p, u16 v)
{
	p[0] = v >> 8;
	p[1] = (u8)v;
}

static inline void clr_w32 (u8 *p, u32 v)
{
	p[0] = v >> 24;
	p[1] = v >> 16;
	p[2] = v >> 8;
	p[3] = (u8)v;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			target names			///////////////
///////////////////////////////////////////////////////////////////////////////
// Order matters: it is the order of the flag bit pairs in the material flags
// word, and therefore also the order the per-target records are stored in.

static const ccp clr0_target_name[CLR0_N_TARGET] = {
	"matcolor0", // LightChannel0MaterialColor (GX_COLOR0A0)
	"matcolor1", // LightChannel1MaterialColor (GX_COLOR1A1)
	"ambient0", // LightChannel0AmbientColor  (GX_COLOR0A0)
	"ambient1", // LightChannel1AmbientColor  (GX_COLOR1A1)
	"tevreg0", // ColorRegister0             (GX_TEVREG0)
	"tevreg1", // ColorRegister1             (GX_TEVREG1)
	"tevreg2", // ColorRegister2             (GX_TEVREG2)
	"konst0", // ConstantColorRegister0     (GX_KCOLOR0)
	"konst1", // ConstantColorRegister1     (GX_KCOLOR1)
	"konst2", // ConstantColorRegister2     (GX_KCOLOR2)
	"konst3", // ConstantColorRegister3     (GX_KCOLOR3)
};

///////////////////////////////////////////////////////////////////////////////

ccp GetTargetNameCLR0 (uint target)
{
	return target < CLR0_N_TARGET ? clr0_target_name[target] : 0;
}

///////////////////////////////////////////////////////////////////////////////

int ScanTargetNameCLR0 (ccp name)
{
	if (name)
		for (uint i = 0; i < CLR0_N_TARGET; i++)
			if (!strcasecmp (name, clr0_target_name[i]))
				return i;
	return -1;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			init/reset			///////////////
///////////////////////////////////////////////////////////////////////////////

void InitializeCLR0 (clr0_t *clr)
{
	DASSERT (clr);
	memset (clr, 0, sizeof (*clr));
	clr->version = CLR0_DEFAULT_VERSION;
}

///////////////////////////////////////////////////////////////////////////////

void ResetCLR0 (clr0_t *clr)
{
	if (!clr)
		return;

	FreeString (clr->fname);
	FreeString (clr->name);
	FreeString (clr->orig_path);

	for (uint i = 0; i < clr->n_entry; i++)
	{
		FreeString (clr->entry[i].name);
		for (uint t = 0; t < CLR0_N_TARGET; t++)
			FREE (clr->entry[i].target[t].color_list);
	}
	FREE (clr->entry);

	InitializeCLR0 (clr);
}

///////////////////////////////////////////////////////////////////////////////

clr0_entry_t *AppendEntryCLR0 (clr0_t *clr, ccp name)
{
	DASSERT (clr);
	if (clr->n_entry == clr->n_entry_alloced)
	{
		clr->n_entry_alloced = clr->n_entry_alloced ? clr->n_entry_alloced * 2 : 8;
		clr->entry = REALLOC (clr->entry, clr->n_entry_alloced * sizeof (*clr->entry));
	}

	clr0_entry_t *e = clr->entry + clr->n_entry++;
	memset (e, 0, sizeof (*e));
	e->name = STRDUP (name ? name : "");
	return e;
}

///////////////////////////////////////////////////////////////////////////////

uint GetColorCountCLR0 (const clr0_t *clr)
{
	DASSERT (clr);
	return clr->n_frames + 1;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			ScanRawCLR0			///////////////
///////////////////////////////////////////////////////////////////////////////
// Layout, big endian, from BrawlLib (BrawlLib/SSBB/Types/Animations/CLR0.cs):
//
//  common BRRES sub-file header (0x10): magic "CLR0", size, version, brres_off
//  0x10  u32 data_offset       // -> resource group, relative to file start
//  0x14  u32 user_data_offset  // version 4 only
//  0x14 / 0x18  u32 string_offset
//  0x18 / 0x1c  u32 orig_path_offset
//  0x1c / 0x20  u16 n_frames
//  0x1e / 0x22  u16 n_entries
//  0x20 / 0x24  u32 loop
//
// resource group @ data_offset: standard BRRES group, entry #0 is the dummy
// root. Each group entry's data offset points at a CLR0Material:
//
//  0x00  u32 string_offset  // relative to the material record itself
//  0x04  u32 flags          // 11 target pairs: bit 2t "exists", 2t+1 "constant"
//
// followed, in target order, by one 8-byte record per existing target:
//
//  0x00  u32 color mask (RGBA)
//  0x04  u32 data: the solid RGBA if "constant", else an offset such that the
//        color array lives at <record address> + data + 4
//
// A non-constant target stores n_frames+1 RGBA values (the extra terminating
// frame that CHR0/SRT0 also carry).

enumError ScanRawCLR0 (clr0_t *clr, bool init_clr, const void *data, uint data_size)
{
	DASSERT (clr);
	DASSERT (data);

	if (init_clr)
		InitializeCLR0 (clr);
	else
		ResetCLR0 (clr);

	const u8 *base = data;
	if (data_size < 0x24 || memcmp (base, "CLR0", 4))
		return ERR_WRONG_FILE_TYPE;

	const u32 version = clr_rd32 (base + 8);
	if (version < CLR0_MIN_VERSION || version > CLR0_MAX_VERSION)
		return ERROR0 (ERR_INVALID_DATA, "CLR0: unsupported sub-version %u\n", version);
	clr->version = version;

	const uint head_size = version == 4 ? 0x28 : 0x24;
	if (data_size < head_size)
		return ERROR0 (ERR_INVALID_DATA, "CLR0: file too small\n");

	const u32 data_off = clr_rd32 (base + 0x10);
	uint off = 0x14;
	if (version == 4)
		off += 4; // user_data_offset, not needed for a faithful re-encode
	const u32 string_off = clr_rd32 (base + off);
	off += 4;
	const u32 orig_path_off = clr_rd32 (base + off);
	off += 4;
	const u32 n_frames = clr_rd16 (base + off);
	off += 2;
	const u32 n_entries = clr_rd16 (base + off);
	off += 2;
	const u32 loop = clr_rd32 (base + off);

	if (string_off && string_off < data_size)
		clr->name = STRDUP ((ccp)(base + string_off));
	if (orig_path_off && orig_path_off < data_size)
		clr->orig_path = STRDUP ((ccp)(base + orig_path_off));
	clr->n_frames = n_frames;
	clr->loop = loop != 0;

	if (!data_off || (u64)data_off + 8 > data_size)
		return ERROR0 (ERR_INVALID_DATA, "CLR0: invalid group offset\n");
	const u8 *group = base + data_off;
	const u32 grp_n_entries = clr_rd32 (group + 4);
	if (grp_n_entries != n_entries)
		; // the group's own count is authoritative
	if ((u64)data_off + 8 + (u64)(grp_n_entries + 1) * 16 > data_size)
		return ERROR0 (ERR_INVALID_DATA, "CLR0: group entry table exceeds file size\n");

	const uint n_color = n_frames + 1;

	for (u32 i = 1; i <= grp_n_entries; i++)
	{
		const u8 *rec = group + 8 + (size_t)i * 16;
		const u32 name_off = clr_rd32 (rec + 8);
		const u32 entry_off = clr_rd32 (rec + 12);
		if (!entry_off || (u64)data_off + entry_off + 8 > data_size)
			return ERROR0 (ERR_INVALID_DATA, "CLR0: invalid entry offset\n");
		const u8 *mat = group + entry_off;

		ccp name = name_off && (u64)data_off + name_off < data_size
			? (ccp)(base + data_off + name_off)
			: "";
		clr0_entry_t *e = AppendEntryCLR0 (clr, name);
		e->flags = clr_rd32 (mat + 4);

		const u8 *trec = mat + 8;
		for (uint t = 0; t < CLR0_N_TARGET; t++)
		{
			if (!(e->flags & CLR0_BIT_EXISTS (t)))
				continue;

			const size_t trec_off = (size_t)(trec - base);
			if (trec_off + 8 > data_size)
				return ERROR0 (ERR_INVALID_DATA,
					"CLR0: target record of entry #%u exceeds file size\n", i);

			clr0_target_t *tg = e->target + t;
			tg->exists = true;
			tg->is_constant = (e->flags & CLR0_BIT_CONSTANT (t)) != 0;
			tg->mask = clr_rd32 (trec);

			const u32 tdata = clr_rd32 (trec + 4);
			if (tg->is_constant)
				tg->color = tdata;
			else
			{
				const u64 color_at = (u64)trec_off + tdata + 4;
				if (color_at + (u64)n_color * 4 > data_size)
					return ERROR0 (ERR_INVALID_DATA,
						"CLR0: color array of entry #%u target %s exceeds file size\n",
						i, clr0_target_name[t]);
				tg->n_color = n_color;
				tg->color_list = MALLOC (n_color * sizeof (*tg->color_list));
				for (uint c = 0; c < n_color; c++)
					tg->color_list[c] = clr_rd32 (base + color_at + c * 4);
			}
			trec += 8;
		}
	}

	return ERR_OK;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			SaveRawCLR0			///////////////
///////////////////////////////////////////////////////////////////////////////

enumError SaveRawCLR0 (clr0_t *clr, ccp fname, bool set_time)
{
	DASSERT (clr);
	DASSERT (fname);

	const uint head_size = clr->version == 4 ? 0x28 : 0x24;
	const uint n_color = GetColorCountCLR0 (clr);
	const uint color_bytes = n_color * 4;

	const uint group_off = head_size;
	const uint group_head_size = 8 + (clr->n_entry + 1) * 16;

	//--- material records: header + one 8-byte record per existing target

	uint *entry_rel = CALLOC (clr->n_entry ? clr->n_entry : 1, sizeof (*entry_rel));
	uint data_size = group_head_size;
	for (uint i = 0; i < clr->n_entry; i++)
	{
		entry_rel[i] = data_size;
		uint n_exist = 0;
		for (uint t = 0; t < CLR0_N_TARGET; t++)
			if (clr->entry[i].target[t].exists)
				n_exist++;
		data_size += 8 + n_exist * 8;
	}

	//--- color arrays, appended behind all material records. Retail shares a
	// single array between targets whose colors are byte-identical, so do the
	// same: it is what makes an unedited file re-encode to its original size.

	const uint color_start = data_size;
	uint n_slot = 0;
	for (uint i = 0; i < clr->n_entry; i++)
		for (uint t = 0; t < CLR0_N_TARGET; t++)
		{
			const clr0_target_t *tg = clr->entry[i].target + t;
			if (tg->exists && !tg->is_constant)
				n_slot++;
		}

	uint *slot_rel = CALLOC (n_slot ? n_slot : 1, sizeof (*slot_rel));
	u8 *blob = CALLOC (n_slot ? n_slot * color_bytes : 1, 1);
	uint blob_used = 0, slot_idx = 0;

	for (uint i = 0; i < clr->n_entry; i++)
		for (uint t = 0; t < CLR0_N_TARGET; t++)
		{
			const clr0_target_t *tg = clr->entry[i].target + t;
			if (!tg->exists || tg->is_constant)
				continue;

			u8 *cur = blob + blob_used;
			for (uint c = 0; c < n_color; c++)
				clr_w32 (cur + c * 4, c < tg->n_color ? tg->color_list[c] : 0);

			uint found = ~0u;
			for (uint p = 0; p < blob_used; p += color_bytes)
				if (!memcmp (blob + p, cur, color_bytes))
				{
					found = p;
					break;
				}

			if (found == ~0u)
			{
				slot_rel[slot_idx] = blob_used;
				blob_used += color_bytes;
			}
			else
				slot_rel[slot_idx] = found;
			slot_idx++;
		}

	data_size = color_start + blob_used;

	// the declared size stops at the end of the data section; the name pool
	// behind it belongs to the enclosing BRRES
	const uint file_size = group_off + data_size;

	const uint n_names = clr->n_entry + 2;
	ccp *names = CALLOC (n_names, sizeof (ccp));
	uint *name_off = CALLOC (n_names, sizeof (uint));
	names[0] = clr->name ? clr->name : "";
	names[1] = clr->orig_path ? clr->orig_path : "";
	for (uint i = 0; i < clr->n_entry; i++)
		names[i + 2] = clr->entry[i].name;

	// Retail CLR0 files lay the string pool out in ordinal name order, not in
	// entry order (BrawlLib's shared string table sorts before writing).
	// Emit the pool sorted so a re-encode is byte-identical, then scatter the
	// resulting offsets back onto the logical slots.
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
	memcpy (buf + group_off + color_start, blob, blob_used);
	FREE (blob);

	//--- header

	memcpy (buf, "CLR0", 4);
	clr_w32 (buf + 4, file_size);
	clr_w32 (buf + 8, clr->version);
	clr_w32 (buf + 0xc, 0); // brres_offset, patched by the container

	clr_w32 (buf + 0x10, group_off);
	uint off = 0x14;
	if (clr->version == 4)
	{
		clr_w32 (buf + off, 0); // user_data_offset: none
		off += 4;
	}
	clr_w32 (buf + off, name_off[0]); // string_offset
	off += 4;
	clr_w32 (buf + off, clr->orig_path ? name_off[1] : 0);
	off += 4;
	clr_w16 (buf + off, (u16)clr->n_frames);
	off += 2;
	clr_w16 (buf + off, (u16)clr->n_entry);
	off += 2;
	clr_w32 (buf + off, clr->loop ? 1 : 0);

	//--- resource group, with the NW4R lookup tree

	u8 *group = buf + group_off;
	clr_w32 (group, group_head_size);
	clr_w32 (group + 4, clr->n_entry);

	brres_info_t *info = CALLOC (clr->n_entry + 1, sizeof (*info));
	info[0].id = 0xffff;
	info[0].name = "";
	info[0].nlen = 0;
	for (uint i = 0; i < clr->n_entry; i++)
	{
		info[i + 1].name = clr->entry[i].name;
		info[i + 1].nlen = strlen (clr->entry[i].name);
		CalcEntryBRRES (info, i + 1);
	}

	for (uint i = 0; i <= clr->n_entry; i++)
	{
		u8 *rec = group + 8 + i * 16;
		clr_w16 (rec, info[i].id);
		clr_w16 (rec + 2, 0);
		clr_w16 (rec + 4, info[i].left_idx);
		clr_w16 (rec + 6, info[i].right_idx);
		if (i)
		{
			clr_w32 (rec + 8, name_off[i + 1] - group_off);
			clr_w32 (rec + 12, entry_rel[i - 1]);
		}
	}
	FREE (info);

	//--- material records

	slot_idx = 0;
	for (uint i = 0; i < clr->n_entry; i++)
	{
		const clr0_entry_t *e = clr->entry + i;
		u8 *mat = group + entry_rel[i];
		clr_w32 (mat, name_off[i + 2] - group_off - entry_rel[i]);
		clr_w32 (mat + 4, e->flags);

		u8 *trec = mat + 8;
		for (uint t = 0; t < CLR0_N_TARGET; t++)
		{
			const clr0_target_t *tg = e->target + t;
			if (!tg->exists)
				continue;

			clr_w32 (trec, tg->mask);
			if (tg->is_constant)
				clr_w32 (trec + 4, tg->color);
			else
			{
				// colors live at <record address> + data + 4
				const uint trec_abs = (uint)(trec - buf);
				const uint color_abs = group_off + color_start + slot_rel[slot_idx];
				clr_w32 (trec + 4, color_abs - trec_abs - 4);
				slot_idx++;
			}
			trec += 8;
		}
	}

	FREE (entry_rel);
	FREE (slot_rel);
	FREE (names);
	FREE (name_off);

	//--- write file

	File_t F;
	enumError err = CreateFileOpt (&F, true, fname, testmode, clr->fname);
	if (err <= ERR_WARNING && F.f)
	{
		SetFileAttrib (&F.fatt, &clr->fatt, 0);
		fwrite (buf, 1, total_size, F.f);
	}
	ResetFile (&F, set_time ? opt_preserve : 0);
	FREE (buf);

	return err;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			text format			///////////////
///////////////////////////////////////////////////////////////////////////////
// Line based, like the VIS0 text form: CLR0's data shape is a flat list of
// named materials each carrying up to 11 color targets, so a small dedicated
// format is clearer than the shared scripting parser.
//
//   version   = 4
//   name      = "<string>"
//   n-frames  = <n>
//   loop      = 0|1
//   n-entries = <n>
//
//   material "<name>" flags 0x<hex>
//     <target> const <mask> <color>
//     <target> anim  <mask> <color> <color> ...
//
// Colors are 8 hex digits, RGBA in file order.

static void PrintQuotedCLR0 (FILE *f, ccp text)
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

static ccp ScanQuotedCLR0 (ccp src, ccp *res)
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
		while (*src && *src != ' ' && *src != '\t' && len < sizeof (buf) - 1)
			buf[len++] = *src++;

	buf[len] = 0;
	*res = STRDUP (buf);
	return src;
}

///////////////////////////////////////////////////////////////////////////////

enumError SaveTextCLR0 (clr0_t *clr, ccp fname, bool set_time)
{
	DASSERT (clr);
	DASSERT (fname);

	File_t F;
	enumError err = CreateFileOpt (&F, true, fname, testmode, clr->fname);
	if (err > ERR_WARNING || !F.f)
	{
		ResetFile (&F, set_time ? opt_preserve : 0);
		return err;
	}

	fprintf (F.f, "#CLR0\n# Wiimms SZS Tools Plus -- CLR0 material color animation\n");
	if (clr->fname)
		fprintf (F.f, "# decoded from %s\n", clr->fname);
	fprintf (F.f, "\nversion   = %u\n", clr->version);
	fprintf (F.f, "name      = ");
	PrintQuotedCLR0 (F.f, clr->name ? clr->name : "");
	fputc ('\n', F.f);
	if (clr->orig_path)
	{
		fprintf (F.f, "orig-path = ");
		PrintQuotedCLR0 (F.f, clr->orig_path);
		fputc ('\n', F.f);
	}
	fprintf (F.f, "n-frames  = %u\n", clr->n_frames);
	fprintf (F.f, "loop      = %u\n", clr->loop);
	fprintf (F.f, "n-entries = %u\n", clr->n_entry);

	fprintf (F.f,
		"\n# One block per material. 'flags' is the raw NW4R flags word and is\n"
		"# preserved verbatim. Colors are RGBA hex in file order; an animated\n"
		"# target stores n-frames+1 of them.\n");

	for (uint i = 0; i < clr->n_entry; i++)
	{
		const clr0_entry_t *e = clr->entry + i;
		fprintf (F.f, "\nmaterial ");
		PrintQuotedCLR0 (F.f, e->name);
		fprintf (F.f, " flags 0x%08x\n", e->flags);

		for (uint t = 0; t < CLR0_N_TARGET; t++)
		{
			const clr0_target_t *tg = e->target + t;
			if (!tg->exists)
				continue;
			fprintf (F.f, "  %-9s %s %08x", clr0_target_name[t],
				tg->is_constant ? "const" : "anim ", tg->mask);
			if (tg->is_constant)
				fprintf (F.f, " %08x\n", tg->color);
			else
			{
				for (uint c = 0; c < tg->n_color; c++)
					fprintf (F.f, "%s%08x", c % 8 ? " " : "\n   ", tg->color_list[c]);
				fputc ('\n', F.f);
			}
		}
	}

	SetFileAttrib (&F.fatt, &clr->fatt, 0);
	ResetFile (&F, set_time ? opt_preserve : 0);
	return err;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			ScanTextCLR0			///////////////
///////////////////////////////////////////////////////////////////////////////

enumError ScanTextCLR0 (clr0_t *clr, bool init_clr, ccp src_fname)
{
	DASSERT (clr);
	DASSERT (src_fname);

	FILE *f = fopen (src_fname, "r");
	if (!f)
		return ERROR0 (ERR_CANT_OPEN, "Can't open CLR0 text file: %s\n", src_fname);

	if (init_clr)
		InitializeCLR0 (clr);
	else
		ResetCLR0 (clr);
	clr->fname = STRDUP (src_fname);

	char line[8000];
	bool have_magic = false;
	clr0_entry_t *cur = 0;
	clr0_target_t *cur_target = 0;
	uint cur_fill = 0;

	while (fgets (line, sizeof (line), f))
	{
		ccp s = line;
		while (*s == ' ' || *s == '\t')
			s++;

		if (!have_magic)
		{
			if (!strncmp (s, "#CLR0", 5))
			{
				have_magic = true;
				continue;
			}
			if (*s == '#' || *s == '\n' || !*s)
				continue;
			fclose (f);
			return ERROR0 (ERR_WRONG_FILE_TYPE, "Not a CLR0 text file: %s\n", src_fname);
		}

		// a continuation line of the current animated target: bare hex colors
		if (cur_target && isxdigit ((int)(uchar)*s))
		{
			while (*s)
			{
				while (*s == ' ' || *s == '\t' || *s == '\n')
					s++;
				if (!isxdigit ((int)(uchar)*s))
					break;
				char *end;
				const u32 val = (u32)strtoul (s, &end, 16);
				if (cur_fill < cur_target->n_color)
					cur_target->color_list[cur_fill++] = val;
				s = end;
			}
			continue;
		}
		cur_target = 0;

		if (*s == '#' || *s == '\n' || !*s)
			continue;

		if (!strncmp (s, "version", 7))
		{
			ccp v = strchr (s, '=');
			if (v)
				clr->version = strtoul (v + 1, 0, 10);
		}
		else if (!strncmp (s, "name", 4) && (s[4] == ' ' || s[4] == '\t' || s[4] == '='))
		{
			ccp v = strchr (s, '=');
			if (v)
			{
				FreeString (clr->name);
				ScanQuotedCLR0 (v + 1, &clr->name);
			}
		}
		else if (!strncmp (s, "orig-path", 9))
		{
			ccp v = strchr (s, '=');
			if (v)
			{
				FreeString (clr->orig_path);
				ScanQuotedCLR0 (v + 1, &clr->orig_path);
			}
		}
		else if (!strncmp (s, "n-frames", 8))
		{
			ccp v = strchr (s, '=');
			if (v)
				clr->n_frames = strtoul (v + 1, 0, 10);
		}
		else if (!strncmp (s, "loop", 4))
		{
			ccp v = strchr (s, '=');
			if (v)
				clr->loop = strtoul (v + 1, 0, 10) != 0;
		}
		else if (!strncmp (s, "n-entries", 9))
			; // informational, the material blocks are authoritative
		else if (!strncmp (s, "material", 8))
		{
			ccp name;
			ccp rest = ScanQuotedCLR0 (s + 8, &name);
			cur = AppendEntryCLR0 (clr, name);
			FreeString (name);
			ccp fl = strstr (rest, "flags");
			if (fl)
				cur->flags = (u32)strtoul (fl + 5, 0, 0);
		}
		else if (cur)
		{
			// "<target> const|anim <mask> [<color> ...]"
			char tname[64];
			uint len = 0;
			while (*s && *s != ' ' && *s != '\t' && len < sizeof (tname) - 1)
				tname[len++] = *s++;
			tname[len] = 0;

			const int t = ScanTargetNameCLR0 (tname);
			if (t < 0)
				continue;

			while (*s == ' ' || *s == '\t')
				s++;
			const bool is_const = !strncmp (s, "const", 5);
			while (*s && *s != ' ' && *s != '\t')
				s++;

			clr0_target_t *tg = cur->target + t;
			tg->exists = true;
			tg->is_constant = is_const;

			char *end;
			tg->mask = (u32)strtoul (s, &end, 16);
			s = end;

			if (is_const)
				tg->color = (u32)strtoul (s, &end, 16);
			else
			{
				const uint n_color = GetColorCountCLR0 (clr);
				tg->n_color = n_color;
				tg->color_list = CALLOC (n_color ? n_color : 1, sizeof (*tg->color_list));
				cur_target = tg;
				cur_fill = 0;
				// any colors already on this line
				while (*s)
				{
					while (*s == ' ' || *s == '\t' || *s == '\n')
						s++;
					if (!isxdigit ((int)(uchar)*s))
						break;
					const u32 val = (u32)strtoul (s, &end, 16);
					if (cur_fill < n_color)
						tg->color_list[cur_fill++] = val;
					s = end;
				}
			}
		}
	}

	fclose (f);
	return ERR_OK;
}

///////////////////////////////////////////////////////////////////////////////
