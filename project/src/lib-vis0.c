
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "lib-vis0.h"
#include "lib-szs.h"
#include "lib-brres.h"

///////////////////////////////////////////////////////////////////////////////
///////////////			byte order helper			///////////////
///////////////////////////////////////////////////////////////////////////////
// BRRES/VIS0 data is always big endian (Wii).

static inline u16 vis0_rd16 (const u8 *p) { return (u16)p[0] << 8 | p[1]; }

static inline u32 vis0_rd32 (const u8 *p)
{
	return (u32)p[0] << 24 | (u32)p[1] << 16 | (u32)p[2] << 8 | p[3];
}

static inline void vis0_w16 (u8 *p, u16 v)
{
	p[0] = v >> 8;
	p[1] = (u8)v;
}

static inline void vis0_w32 (u8 *p, u32 v)
{
	p[0] = v >> 24;
	p[1] = v >> 16;
	p[2] = v >> 8;
	p[3] = (u8)v;
}

// align 'val' up to a multiple of 'align' (align must be a power of 2)
static inline uint vis0_align (uint val, uint align) { return (val + align - 1) & ~(align - 1); }

//
///////////////////////////////////////////////////////////////////////////////
///////////////			init/reset			///////////////
///////////////////////////////////////////////////////////////////////////////

void InitializeVIS0 (vis0_t *vis)
{
	DASSERT (vis);
	memset (vis, 0, sizeof (*vis));
	vis->version = VIS0_DEFAULT_VERSION;
}

///////////////////////////////////////////////////////////////////////////////

void ResetVIS0 (vis0_t *vis)
{
	if (!vis)
		return;

	FreeString (vis->fname);
	FreeString (vis->name);
	FreeString (vis->orig_path);

	for (uint i = 0; i < vis->n_entry; i++)
	{
		FreeString (vis->entry[i].name);
		FREE (vis->entry[i].bits);
	}
	FREE (vis->entry);

	InitializeVIS0 (vis);
}

///////////////////////////////////////////////////////////////////////////////

vis0_entry_t *AppendEntryVIS0 (vis0_t *vis, ccp name)
{
	DASSERT (vis);
	if (vis->n_entry == vis->n_entry_alloced)
	{
		vis->n_entry_alloced = vis->n_entry_alloced ? vis->n_entry_alloced * 2 : 8;
		vis->entry = REALLOC (vis->entry, vis->n_entry_alloced * sizeof (*vis->entry));
	}

	vis0_entry_t *e = vis->entry + vis->n_entry++;
	memset (e, 0, sizeof (*e));
	e->name = STRDUP (name ? name : "");
	e->is_constant = true;
	e->enabled = true;
	return e;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			ScanRawVIS0			///////////////
///////////////////////////////////////////////////////////////////////////////
// Layout, big endian, reverse engineered from BrawlLib
// (BrawlLib/SSBB/Types/Animations/VIS0.cs):
//
//  common BRRES sub-file header (0x10 bytes): magic "VIS0", size, version, brres_offset
//  0x10  u32 data_offset        // -> resource group (relative to file start)
//  0x14  u32 user_data_offset   // version 4 only
//  0x14 / 0x18  u32 string_offset      // -> object name
//  0x18 / 0x1c  u32 orig_path_offset   // -> original path, or 0
//  0x1c / 0x20  u16 n_frames
//  0x1e / 0x22  u16 n_entries
//  0x20 / 0x24  u32 loop
//
// resource group @ data_offset: standard BRRES group (u32 size, u32
// n_entries, then n_entries+1 x 16-byte brres_entry_t; entry #0 is a dummy
// root with id 0xffff).
//
// each VIS0 entry, addressed via brres_entry_t.data_off (relative to the
// group):
//  0x00  u32 string_offset (relative to the entry itself)
//  0x04  u32 flags: bit0 Enabled, bit1 Constant
//  0x08  [only if !Constant] ceil(n_frames,32)/8 bytes, 1 bit/frame, MSB
//        of first byte == frame 0

enumError ScanRawVIS0 (vis0_t *vis, bool init_vis, const void *data, uint data_size)
{
	DASSERT (vis);
	DASSERT (data);

	if (init_vis)
		InitializeVIS0 (vis);
	else
		ResetVIS0 (vis);

	const u8 *base = data;
	if (data_size < 0x24 || memcmp (base, "VIS0", 4))
		return ERR_WRONG_FILE_TYPE;

	const u32 version = vis0_rd32 (base + 8);
	if (version < VIS0_MIN_VERSION || version > VIS0_MAX_VERSION)
		return ERROR0 (ERR_INVALID_DATA, "VIS0: unsupported sub-version %u\n", version);
	vis->version = version;

	const uint head_size = version == 4 ? 0x28 : 0x24;
	if (data_size < head_size)
		return ERROR0 (ERR_INVALID_DATA, "VIS0: file too small\n");

	const u32 data_off = vis0_rd32 (base + 0x10);
	uint off = 0x14;
	if (version == 4)
		off += 4; // skip user_data_offset, not needed for a faithful re-encode
	const u32 string_off = vis0_rd32 (base + off);
	off += 4;
	const u32 orig_path_off = vis0_rd32 (base + off);
	off += 4;
	const u32 n_frames = vis0_rd16 (base + off);
	off += 2;
	const u32 n_entries = vis0_rd16 (base + off);
	off += 2;
	const u32 loop = vis0_rd32 (base + off);

	if (string_off && string_off < data_size)
		vis->name = STRDUP ((ccp)(base + string_off));
	if (orig_path_off && orig_path_off < data_size)
		vis->orig_path = STRDUP ((ccp)(base + orig_path_off));
	vis->n_frames = n_frames;
	vis->loop = loop != 0;

	if (!data_off || data_off + 8 > data_size)
		return ERROR0 (ERR_INVALID_DATA, "VIS0: invalid group offset\n");
	const u8 *group = base + data_off;
	const u32 grp_n_entries = vis0_rd32 (group + 4);
	if (grp_n_entries != n_entries)
		; // trust the group's own count, it's authoritative
	if (data_off + 8 + (u64)(grp_n_entries + 1) * 16 > data_size)
		return ERROR0 (ERR_INVALID_DATA, "VIS0: group entry table exceeds file size\n");

	const uint n_bits_byte = vis0_align (n_frames, 32) / 8;
	uint n_unresolved = 0;

	for (u32 i = 1; i <= grp_n_entries; i++)
	{
		const u8 *rec = group + 8 + (size_t)i * 16;
		const u32 name_off = vis0_rd32 (rec + 8);
		const u32 entry_off = vis0_rd32 (rec + 12);
		const u8 *entry = group + entry_off;
		if (!entry_off || entry_off + 8 > data_size - data_off)
			return ERROR0 (ERR_INVALID_DATA, "VIS0: invalid entry offset\n");

		// Unlike CHR0/SRT0, retail VIS0 entries usually do not carry their own
		// names: the entry name offset points into the *shared* BRRES string
		// pool, because the names duplicate the sibling MDL0's bone names. A
		// VIS0 extracted on its own therefore has no bytes to resolve them
		// from. Emit an explicit "?<offset>" marker in that case rather than
		// an empty string, so the loss is visible in the text form (and in a
		// re-encoded file) instead of silently producing unnamed entries.
		char namebuf[24];
		ccp name;
		if (name_off && data_off + name_off < data_size)
			name = (ccp)(base + data_off + name_off);
		else
		{
			snprintf (namebuf, sizeof (namebuf), "?0x%x", name_off);
			name = namebuf;
			n_unresolved++;
		}
		vis0_entry_t *e = AppendEntryVIS0 (vis, name);

		const u32 flags = vis0_rd32 (entry + 4);
		e->enabled = (flags & 1) != 0;
		e->is_constant = (flags & 2) != 0;

		if (!e->is_constant)
		{
			if (entry_off + 8 + n_bits_byte > data_size - data_off)
				return ERROR0 (ERR_INVALID_DATA, "VIS0: entry bit array exceeds file size\n");
			e->n_bits_byte = n_bits_byte;
			e->bits = MALLOC (n_bits_byte);
			memcpy (e->bits, entry + 8, n_bits_byte);
		}
	}

	if (n_unresolved)
		ERROR0 (ERR_WARNING,
			"VIS0: %u of %u entry name%s point outside this file, into the"
			" shared BRRES string pool, and were replaced by \"?<offset>\""
			" markers. Decode the VIS0 from within its BRRES to get the real"
			" names.\n",
			n_unresolved, grp_n_entries, grp_n_entries == 1 ? "" : "s");

	return ERR_OK;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			SaveRawVIS0			///////////////
///////////////////////////////////////////////////////////////////////////////

enumError SaveRawVIS0 (vis0_t *vis, ccp fname, bool set_time)
{
	DASSERT (vis);
	DASSERT (fname);

	const uint head_size = vis->version == 4 ? 0x28 : 0x24;
	const uint n_bits_byte = vis0_align (vis->n_frames, 32) / 8;

	//--- layout, mirroring what retail VIS0 files do: header, resource group
	// (with its entry data inline), then the string pool last. The pool has to
	// come last because the group's entry name fields are stored relative to
	// the group; a pool placed before the group would make every one of those
	// offsets negative.

	ccp name = vis->name ? vis->name : "";
	ccp orig_path = vis->orig_path ? vis->orig_path : "";

	const uint group_off = head_size;
	const uint group_head_size = 8 + (vis->n_entry + 1) * 16;

	uint data_size = group_head_size;
	uint *entry_rel = CALLOC (vis->n_entry, sizeof (*entry_rel));
	for (uint i = 0; i < vis->n_entry; i++)
	{
		entry_rel[i] = data_size;
		data_size += vis->entry[i].is_constant ? 8 : 8 + n_bits_byte;
	}

	// absolute pool offsets: [0] name, [1] orig_path (0 if none), [2+i] entries
	const uint pool_off = vis0_align (group_off + data_size, 4);
	uint *name_off = CALLOC (vis->n_entry + 2, sizeof (*name_off));
	uint pool_size = 0;

	name_off[0] = pool_off + pool_size;
	pool_size += strlen (name) + 1;
	if (orig_path[0])
	{
		name_off[1] = pool_off + pool_size;
		pool_size += strlen (orig_path) + 1;
	}
	for (uint i = 0; i < vis->n_entry; i++)
	{
		name_off[i + 2] = pool_off + pool_size;
		pool_size += strlen (vis->entry[i].name) + 1;
	}

	const uint total_size = pool_off + pool_size;
	u8 *buf = CALLOC (total_size, 1);

	//--- header

	memcpy (buf, "VIS0", 4);
	vis0_w32 (buf + 4, total_size);
	vis0_w32 (buf + 8, vis->version);
	vis0_w32 (buf + 0xc, 0); // brres_offset, patched by the container on injection

	vis0_w32 (buf + 0x10, group_off);
	uint off = 0x14;
	if (vis->version == 4)
	{
		vis0_w32 (buf + off, 0); // user_data_offset: none
		off += 4;
	}
	vis0_w32 (buf + off, name_off[0]); // string_offset
	off += 4;
	vis0_w32 (buf + off, name_off[1]); // orig_path_offset, 0 if absent
	off += 4;
	vis0_w16 (buf + off, vis->n_frames);
	off += 2;
	vis0_w16 (buf + off, vis->n_entry);
	off += 2;
	vis0_w32 (buf + off, vis->loop ? 1 : 0);

	//--- resource group, with the NW4R lookup tree

	u8 *group = buf + group_off;
	vis0_w32 (group, group_head_size);
	vis0_w32 (group + 4, vis->n_entry);

	brres_info_t *info = CALLOC (vis->n_entry + 1, sizeof (*info));
	info[0].id = 0xffff;
	info[0].name = "";
	info[0].nlen = 0;
	for (uint i = 0; i < vis->n_entry; i++)
	{
		info[i + 1].name = vis->entry[i].name;
		info[i + 1].nlen = strlen (vis->entry[i].name);
		CalcEntryBRRES (info, i + 1);
	}

	for (uint i = 0; i <= vis->n_entry; i++)
	{
		u8 *rec = group + 8 + i * 16;
		vis0_w16 (rec, info[i].id);
		vis0_w16 (rec + 2, 0);
		vis0_w16 (rec + 4, info[i].left_idx);
		vis0_w16 (rec + 6, info[i].right_idx);
		if (i)
		{
			vis0_w32 (rec + 8, name_off[i + 1] - group_off);
			vis0_w32 (rec + 12, entry_rel[i - 1]);
		}
	}
	FREE (info);

	for (uint i = 0; i < vis->n_entry; i++)
	{
		const vis0_entry_t *e = vis->entry + i;
		u8 *entry = group + entry_rel[i];
		vis0_w32 (entry, name_off[i + 2] - group_off - entry_rel[i]);
		vis0_w32 (entry + 4, (e->enabled ? 1u : 0) | (e->is_constant ? 2u : 0));
		if (!e->is_constant && e->bits)
			memcpy (entry + 8, e->bits,
				e->n_bits_byte < n_bits_byte ? e->n_bits_byte : n_bits_byte);
	}

	//--- string pool

	memcpy (buf + name_off[0], name, strlen (name) + 1);
	if (orig_path[0])
		memcpy (buf + name_off[1], orig_path, strlen (orig_path) + 1);
	for (uint i = 0; i < vis->n_entry; i++)
		memcpy (buf + name_off[i + 2], vis->entry[i].name,
			strlen (vis->entry[i].name) + 1);

	FREE (name_off);
	FREE (entry_rel);

	//--- write file

	File_t F;
	enumError err = CreateFileOpt (&F, true, fname, testmode, vis->fname);
	if (err <= ERR_WARNING && F.f)
	{
		SetFileAttrib (&F.fatt, &vis->fatt, 0);
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
// Simple, line based, hand written format (not the shared ScanInfo_t
// scripting parser used by e.g. PAT-TXT/KMP-TXT -- VIS0's data shape is a
// flat list of named bit arrays, so a minimal dedicated format is a better
// match and much smaller/lower risk than wiring up the full grammar):
//
//   #VIS0
//   version   = 3|4
//   name      = "<string>"
//   orig-path = "<string>"      (line omitted on save if empty)
//   n-frames  = <uint>
//   loop      = 0|1
//
//   "<entry name>" const 0|1
//   "<entry name>" anim <n-frames whitespace separated 0|1 values>

static void PrintQuotedVIS0 (FILE *f, ccp s)
{
	fputc ('"', f);
	for (; s && *s; s++)
	{
		if (*s == '"' || *s == '\\')
			fputc ('\\', f);
		fputc (*s, f);
	}
	fputc ('"', f);
}

// Extract a "..." string starting at 'src' (leading whitespace already
// skipped by the caller isn't required). Returns pointer behind the closing
// quote, or NULL if 'src' doesn't start with '"' or is unterminated.
// '*result' receives an alloced, unescaped copy.
static ccp ScanQuotedVIS0 (ccp src, ccp *result)
{
	while (*src == ' ' || *src == '\t')
		src++;
	if (*src != '"')
		return 0;
	src++;

	char buf[1024];
	uint n = 0;
	while (*src && *src != '"' && n + 1 < sizeof (buf))
	{
		if (*src == '\\' && src[1])
			src++;
		buf[n++] = *src++;
	}
	if (*src != '"')
		return 0;
	src++;

	buf[n] = 0;
	*result = STRDUP (buf);
	return src;
}

///////////////////////////////////////////////////////////////////////////////

enumError SaveTextVIS0 (vis0_t *vis, ccp fname, bool set_time)
{
	DASSERT (vis);
	DASSERT (fname);

	File_t F;
	enumError err = CreateFileOpt (&F, true, fname, testmode, vis->fname);
	if (err > ERR_WARNING || !F.f)
	{
		ResetFile (&F, 0);
		return err;
	}
	SetFileAttrib (&F.fatt, &vis->fatt, 0);

	fprintf (F.f, "#VIS0\n");
	fprintf (F.f, "# Wiimms SZS Tools Plus -- VIS0 bone/node visibility animation\n");
	if (vis->fname)
		fprintf (F.f, "# decoded from %s\n", vis->fname);
	fprintf (F.f, "\n");
	fprintf (F.f, "version   = %u\n", vis->version);
	fprintf (F.f, "name      = ");
	PrintQuotedVIS0 (F.f, vis->name ? vis->name : "");
	fprintf (F.f, "\n");
	if (vis->orig_path && *vis->orig_path)
	{
		fprintf (F.f, "orig-path = ");
		PrintQuotedVIS0 (F.f, vis->orig_path);
		fprintf (F.f, "\n");
	}
	fprintf (F.f, "n-frames  = %u\n", vis->n_frames);
	fprintf (F.f, "loop      = %u\n", vis->loop ? 1 : 0);
	fprintf (F.f, "n-entries = %u\n", vis->n_entry);
	fprintf (F.f, "\n");
	fprintf (F.f,
		"# <name> const <0|1>            -- always hidden/visible, no keyframes\n"
		"# <name> anim  <n-frames x 0|1> -- one visibility flag per frame\n\n");

	for (uint i = 0; i < vis->n_entry; i++)
	{
		const vis0_entry_t *e = vis->entry + i;
		PrintQuotedVIS0 (F.f, e->name);
		if (e->is_constant)
			fprintf (F.f, " const %u\n", e->enabled ? 1 : 0);
		else
		{
			fprintf (F.f, " anim ");
			for (uint fr = 0; fr < vis->n_frames; fr++)
			{
				const uint byte = fr >> 3, bit = 7 - (fr & 7);
				const int val = byte < e->n_bits_byte ? (e->bits[byte] >> bit & 1) : 0;
				fputc (val ? '1' : '0', F.f);
				fputc (fr + 1 < vis->n_frames ? ' ' : '\n', F.f);
			}
			if (!vis->n_frames)
				fputc ('\n', F.f);
		}
	}

	ResetFile (&F, set_time ? opt_preserve : 0);
	return ERR_OK;
}

///////////////////////////////////////////////////////////////////////////////

enumError ScanTextVIS0 (vis0_t *vis, bool init_vis, ccp src_fname)
{
	DASSERT (vis);
	DASSERT (src_fname);

	FILE *f = fopen (src_fname, "r");
	if (!f)
		return ERROR0 (ERR_CANT_OPEN, "Can't open VIS0 text file: %s\n", src_fname);

	if (init_vis)
		InitializeVIS0 (vis);
	else
		ResetVIS0 (vis);
	vis->fname = STRDUP (src_fname);

	char line[65536];
	uint n_frames = 0;
	bool first = true;

	while (fgets (line, sizeof (line), f))
	{
		ccp s = line;
		while (*s == ' ' || *s == '\t')
			s++;

		if (first)
		{
			first = false;
			if (strncmp (s, "#VIS0", 5))
			{
				fclose (f);
				ResetVIS0 (vis);
				return ERROR0 (ERR_WRONG_FILE_TYPE, "Not a VIS0 text file: %s\n", src_fname);
			}
			continue;
		}

		if (!*s || *s == '\r' || *s == '\n' || *s == '#')
			continue;

		uint uval;
		if (sscanf (s, "version = %u", &uval) == 1)
			vis->version = uval;
		else if (sscanf (s, "n-frames = %u", &uval) == 1)
			n_frames = vis->n_frames = uval;
		else if (sscanf (s, "loop = %u", &uval) == 1)
			vis->loop = uval != 0;
		else if (sscanf (s, "n-entries = %u", &uval) == 1)
			; // informational only, entries are appended as found below
		else if (!strncmp (s, "name", 4) && (s[4] == ' ' || s[4] == '\t' || s[4] == '='))
		{
			ccp val = strchr (s, '=');
			if (val)
			{
				FreeString (vis->name);
				ScanQuotedVIS0 (val + 1, &vis->name);
			}
		}
		else if (!strncmp (s, "orig-path", 9))
		{
			ccp val = strchr (s, '=');
			if (val)
			{
				FreeString (vis->orig_path);
				ScanQuotedVIS0 (val + 1, &vis->orig_path);
			}
		}
		else if (*s == '"')
		{
			ccp name;
			ccp rest = ScanQuotedVIS0 (s, &name);
			if (!rest)
				continue;

			vis0_entry_t *e = AppendEntryVIS0 (vis, name);
			FreeString (name);

			while (*rest == ' ' || *rest == '\t')
				rest++;

			if (!strncmp (rest, "const", 5))
			{
				e->is_constant = true;
				int cv = 0;
				sscanf (rest + 5, "%d", &cv);
				e->enabled = cv != 0;
			}
			else if (!strncmp (rest, "anim", 4))
			{
				e->is_constant = false;
				rest += 4;

				const uint n_bits_byte = vis0_align (n_frames, 32) / 8;
				e->n_bits_byte = n_bits_byte;
				e->bits = CALLOC (n_bits_byte ? n_bits_byte : 1, 1);

				uint fr = 0;
				while (*rest && fr < n_frames)
				{
					while (*rest == ' ' || *rest == '\t')
						rest++;
					if (*rest == '0' || *rest == '1')
					{
						if (*rest == '1')
							e->bits[fr >> 3] |= 1 << (7 - (fr & 7));
						fr++;
						rest++;
					}
					else if (*rest == '\r' || *rest == '\n' || !*rest)
						break;
					else
						rest++;
				}
			}
		}
	}

	fclose (f);

	if (!vis->version)
		vis->version = VIS0_DEFAULT_VERSION;

	return ERR_OK;
}
