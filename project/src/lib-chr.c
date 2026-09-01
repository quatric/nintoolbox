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
#include "lib-chr.h"
#include "lib-szs.h"
#include "lib-brres.h"

///////////////////////////////////////////////////////////////////////////////
///////////////			byte order helper		///////////////
///////////////////////////////////////////////////////////////////////////////

static inline u16 chr_rd16 (const u8 *p) { return (u16)p[0] << 8 | p[1]; }

static inline u32 chr_rd32 (const u8 *p)
{
	return (u32)p[0] << 24 | (u32)p[1] << 16 | (u32)p[2] << 8 | p[3];
}

static inline void chr_w16 (u8 *p, u16 v)
{
	p[0] = v >> 8;
	p[1] = (u8)v;
}

static inline void chr_w32 (u8 *p, u32 v)
{
	p[0] = v >> 24;
	p[1] = v >> 16;
	p[2] = v >> 8;
	p[3] = (u8)v;
}

static inline float chr_rdf (const u8 *p)
{
	const u32 raw = chr_rd32 (p);
	float f;
	memcpy (&f, &raw, 4);
	return f;
}

static inline void chr_wf (u8 *p, float f)
{
	u32 raw;
	memcpy (&raw, &f, 4);
	chr_w32 (p, raw);
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			code word layout		///////////////
///////////////////////////////////////////////////////////////////////////////
// The per-entry code word, from BrawlLib's AnimationCode.cs:
//
//   bit  0        always set
//   bit  1        identity (scale 1, rot 0, trans 0)
//   bit  2        rotation and translation are zero
//   bit  3        scale is one
//   bits 4,5,6    scale / rotation / translation is isotropic
//   bits 7,8,9    use the model's own scale / rotation / translation
//   bits 10,11    Maya scale-compensation flags
//   bit  12       SoftImage "classic scale off"
//   bits 13,14,15 scale X/Y/Z is fixed
//   bits 16,17,18 rotation X/Y/Z is fixed
//   bits 19,20,21 translation X/Y/Z is fixed
//   bits 22,23,24 scale / rotation / translation group exists
//   ...           track format fields, see below
//
// The format fields are the one part of this word that does NOT match
// BrawlLib for every file. BrawlLib documents them at bits 25..26 (scale),
// 27..29 (rotation) and 30..31 (translation), which is only valid for
// version 4 and 5. In retail Mario Kart Wii the version 3 sub-files place
// the same block two bits lower. Verified against every CHR0 in Mario Kart
// Wii (USA):
//
//   - version 5 (the 9 "*_trophy" animations, code 0x08bde059 / 0x09bde019):
//     bits 27..29 read 1 = I4, and the track data is unambiguously I4
//     (2 keys, frame 0 -> 1.5 and frame 239 -> 359.9, a full turn over the
//     240 frame loop). BrawlLib's documented position is correct here.
//
//   - version 3 (all remaining animations): bits 27..29 read 0 = None for
//     every single rotation track, which cannot be right because those
//     tracks demonstrably hold data. Bits 25..27 instead read 2 = I6 and
//     3 = I12, and those predictions match the measured track sizes for
//     70 of 75 rotation tracks (the other 5 are the last track of their
//     sub-file, where no following offset exists to measure a size against).
//
// One further version 3 deviation: its I6 tracks use an 8 byte header
// (key count + frame scale) rather than BrawlLib's 16 byte I6Header. This is
// handled by CHR0_I6_HEADER_8 below.
//
// Scale and translation formats could not be confirmed from retail data:
// every scale and translation track in Mario Kart Wii is I12, in both
// version 3 and version 5. We therefore read BrawlLib's documented field and
// fall back to I12 when it reads None, which reproduces all 25 observed
// scale/translation tracks exactly.

#define CHR0_BIT_ISOTROPIC 4 // +0 scale, +1 rotation, +2 translation
#define CHR0_BIT_FIXED 13 // +0..2 scale XYZ, +3..5 rotation, +6..8 translation
#define CHR0_BIT_EXISTS 22 // +0 scale, +1 rotation, +2 translation

// bit position of the rotation format field; scale sits 2 bits below it and
// translation 3 bits above it
static inline uint chr_rot_format_shift (uint version) { return version < 4 ? 25 : 27; }

///////////////////////////////////////////////////////////////////////////////

// Extract the track format for channel group 'grp' (0=scale, 1=rot, 2=trans).
static banim_format_t chr_get_format (u32 code, uint version, uint grp)
{
	const uint rot = chr_rot_format_shift (version);
	uint fmt;
	switch (grp)
	{
		case 0: fmt = code >> (rot - 2) & 3; break;
		case 1: fmt = code >> rot & 7; break;
		default: fmt = code >> (rot + 3) & 3; break;
	}

	// scale and translation only ever use I12 in the retail data we could
	// verify against; treat an empty field as I12 rather than as "no data",
	// which would desynchronise the whole entry walk
	if (grp != 1 && fmt == BANIM_NONE)
		fmt = BANIM_I12;

	return (banim_format_t)fmt;
}

///////////////////////////////////////////////////////////////////////////////

// true: this version stores I6 tracks with an 8 byte header instead of the
// 16 byte header BrawlLib documents
static inline bool chr_i6_header_8 (uint version) { return version < 4; }

//
///////////////////////////////////////////////////////////////////////////////
///////////////			init/reset			///////////////
///////////////////////////////////////////////////////////////////////////////

void InitializeCHR0 (chr0_t *chr)
{
	DASSERT (chr);
	memset (chr, 0, sizeof (*chr));
	chr->version = CHR0_DEFAULT_VERSION;
}

///////////////////////////////////////////////////////////////////////////////

void ResetCHR0 (chr0_t *chr)
{
	if (!chr)
		return;

	FreeString (chr->fname);
	FreeString (chr->name);
	FreeString (chr->orig_path);

	for (uint i = 0; i < chr->n_entry; i++)
	{
		FreeString (chr->entry[i].name);
		for (uint c = 0; c < CHR0_N_CHANNEL; c++)
			ResetTrackBANIM (&chr->entry[i].channel[c].track);
	}
	FREE (chr->entry);

	InitializeCHR0 (chr);
}

///////////////////////////////////////////////////////////////////////////////

chr0_entry_t *AppendEntryCHR0 (chr0_t *chr, ccp name)
{
	DASSERT (chr);
	if (chr->n_entry == chr->n_entry_alloced)
	{
		chr->n_entry_alloced = chr->n_entry_alloced ? chr->n_entry_alloced * 2 : 8;
		chr->entry = REALLOC (chr->entry, chr->n_entry_alloced * sizeof (*chr->entry));
	}

	chr0_entry_t *e = chr->entry + chr->n_entry++;
	memset (e, 0, sizeof (*e));
	e->name = STRDUP (name ? name : "");
	for (uint c = 0; c < CHR0_N_CHANNEL; c++)
	{
		InitializeTrackBANIM (&e->channel[c].track);
		e->channel[c].is_fixed = true;
	}
	return e;
}

///////////////////////////////////////////////////////////////////////////////

uint GetFrameLimitCHR0 (const chr0_t *chr)
{
	DASSERT (chr);
	return chr->n_frames + (chr->loop ? 1 : 0);
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			ScanRawCHR0			///////////////
///////////////////////////////////////////////////////////////////////////////
// Layout, big endian:
//
//  common BRRES sub-file header (0x10 bytes): magic "CHR0", size, version,
//                                             brres_offset
//  0x10  u32 data_offset       // -> resource group, relative to file start
//        u32 user_data_offset  // version 5 only
//        u32 string_offset     // -> animation name
//        u32 orig_path_offset  // -> original path, or 0
//        u16 n_frames
//        u16 n_entries
//        u32 loop
//        u32 scaling_rule
//
// resource group @ data_offset: standard BRRES group (u32 size, u32 count,
// then count+1 16-byte records; record #0 is the dummy root).
//
// each CHR0 entry, at group + record.data_off:
//  0x00  u32 string_offset, relative to the entry
//  0x04  u32 code
//  0x08  one 4-byte slot per stored channel, in group order scale, rotation,
//        translation and within a group in axis order X, Y, Z (an isotropic
//        group stores a single slot). A slot holds either the channel's
//        constant float, or -- when the group's "fixed" bit for that axis is
//        clear -- an offset to the channel's track data, relative to the
//        start of the entry.

// Walk one entry's slot array. 'cb_fixed' and 'cb_track' style handling is
// inlined here because decode and encode need mirror-image traversals.
enumError ScanRawCHR0 (chr0_t *chr, bool init_chr, const void *data, uint data_size)
{
	DASSERT (chr);
	DASSERT (data);

	if (init_chr)
		InitializeCHR0 (chr);
	else
		ResetCHR0 (chr);

	const u8 *base = data;
	if (data_size < 0x28 || memcmp (base, "CHR0", 4))
		return ERR_WRONG_FILE_TYPE;

	const u32 version = chr_rd32 (base + 8);
	if (version < CHR0_MIN_VERSION || version > CHR0_MAX_VERSION)
		return ERROR0 (ERR_INVALID_DATA, "CHR0: unsupported sub-version %u\n", version);
	chr->version = version;

	const uint head_size = version == 5 ? 0x2c : 0x28;
	if (data_size < head_size)
		return ERROR0 (ERR_INVALID_DATA, "CHR0: file too small\n");

	const u32 data_off = chr_rd32 (base + 0x10);
	uint off = 0x14;
	if (version == 5)
		off += 4; // user_data_offset, not needed for a faithful re-encode
	const u32 string_off = chr_rd32 (base + off);
	off += 4;
	const u32 orig_path_off = chr_rd32 (base + off);
	off += 4;
	chr->n_frames = chr_rd16 (base + off);
	off += 2;
	const uint n_entries_hdr = chr_rd16 (base + off);
	off += 2;
	chr->loop = chr_rd32 (base + off) != 0;
	off += 4;
	chr->scaling_rule = chr_rd32 (base + off);

	if (string_off && string_off < data_size)
		chr->name = STRDUP ((ccp)(base + string_off));
	if (orig_path_off && orig_path_off < data_size)
		chr->orig_path = STRDUP ((ccp)(base + orig_path_off));

	if (!data_off || (u64)data_off + 8 > data_size)
		return ERROR0 (ERR_INVALID_DATA, "CHR0: invalid group offset\n");

	const u8 *group = base + data_off;
	const u32 n_entries = chr_rd32 (group + 4);
	if (n_entries != n_entries_hdr)
		; // the group's own count is authoritative
	if ((u64)data_off + 8 + (u64)(n_entries + 1) * 16 > data_size)
		return ERROR0 (ERR_INVALID_DATA, "CHR0: group entry table exceeds file size\n");

	const uint frame_limit = GetFrameLimitCHR0 (chr);

	for (u32 i = 1; i <= n_entries; i++)
	{
		const u8 *rec = group + 8 + (size_t)i * 16;
		const u32 name_off = chr_rd32 (rec + 8);
		const u32 entry_off = chr_rd32 (rec + 12);

		if (!entry_off || (u64)data_off + entry_off + 8 > data_size)
			return ERROR0 (ERR_INVALID_DATA, "CHR0: invalid entry offset\n");
		const u8 *entry = group + entry_off;
		const uint entry_pos = data_off + entry_off; // absolute file position

		ccp name = name_off && (u64)data_off + name_off < data_size
			? (ccp)(base + data_off + name_off)
			: "";
		chr0_entry_t *e = AppendEntryCHR0 (chr, name);
		e->code = chr_rd32 (entry + 4);

		uint slot = entry_pos + 8; // absolute position of the next slot
		for (uint grp = 0; grp < CHR0_N_GROUP; grp++)
		{
			chr0_group_t *g = e->group + grp;
			g->exists = (e->code >> (CHR0_BIT_EXISTS + grp) & 1) != 0;
			g->isotropic = (e->code >> (CHR0_BIT_ISOTROPIC + grp) & 1) != 0;
			g->format = chr_get_format (e->code, version, grp);
			if (!g->exists)
				continue;

			// an isotropic group stores exactly one slot, whose fixed flag is
			// the group's Z bit; otherwise one slot per axis
			const uint n_slot = g->isotropic ? 1 : 3;
			for (uint s = 0; s < n_slot; s++)
			{
				const uint axis = g->isotropic ? 2 : s;
				const uint chan = grp * 3 + (g->isotropic ? 0 : s);
				const bool fixed
					= (e->code >> (CHR0_BIT_FIXED + grp * 3 + axis) & 1) != 0;

				if (slot + 4 > data_size)
					return ERROR0 (ERR_INVALID_DATA,
						"CHR0: entry '%s' slot exceeds file size\n", e->name);

				chr0_channel_t *ch = e->channel + chan;
				if (fixed)
				{
					ch->is_fixed = true;
					ch->value = chr_rdf (base + slot);
				}
				else
				{
					ch->is_fixed = false;
					const u32 rel = chr_rd32 (base + slot);
					const u64 track_pos = (u64)entry_pos + rel;
					if (track_pos + 8 > data_size)
						return ERROR0 (ERR_INVALID_DATA,
							"CHR0: entry '%s' track offset out of range\n", e->name);

					const banim_format_t fmt = g->format;

					// Version 3 stores its I6 tracks with an 8 byte header
					// (key count + frame scale) instead of BrawlLib's 16 byte
					// header, and therefore carries no quantization base/step
					// pair. The 11-bit frame indices decode cleanly, but we
					// could not establish how the 16 bit magnitude maps to a
					// value: neither a raw reading, a binary-angle measure,
					// nor any simple fixed-point divisor reproduces the
					// stored Hermite tangents across the 62 retail tracks we
					// tested. Rather than emit plausible-looking but wrong
					// numbers, refuse this one variant explicitly.
					if (fmt == BANIM_I6 && chr_i6_header_8 (version))
						return ERROR0 (ERR_NOT_IMPLEMENTED,
							"CHR0: entry '%s' uses the version 3 short-header I6 track"
							" encoding, whose value scaling is not yet known\n",
							e->name);

					const enumError err = DecodeTrackBANIM (&ch->track,
						base + track_pos, data_size - (uint)track_pos, fmt, frame_limit);
					if (err)
						return err;
				}
				slot += 4;
			}
		}
	}

	return ERR_OK;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			SaveRawCHR0			///////////////
///////////////////////////////////////////////////////////////////////////////

// Number of 4-byte slots an entry stores.
static uint chr_count_slots (const chr0_entry_t *e)
{
	uint n = 0;
	for (uint grp = 0; grp < CHR0_N_GROUP; grp++)
		if (e->group[grp].exists)
			n += e->group[grp].isotropic ? 1 : 3;
	return n;
}

///////////////////////////////////////////////////////////////////////////////

enumError SaveRawCHR0 (chr0_t *chr, ccp fname, bool set_time)
{
	DASSERT (chr);
	DASSERT (fname);

	const uint head_size = chr->version == 5 ? 0x2c : 0x28;
	const uint frame_limit = GetFrameLimitCHR0 (chr);

	//--- layout: group header, all entry slot arrays, then all track data.
	//--- The name pool is appended behind the data section, see below.

	const uint group_off = head_size;
	const uint group_head_size = 8 + (chr->n_entry + 1) * 16;

	uint *entry_rel = CALLOC (chr->n_entry ? chr->n_entry : 1, sizeof (uint));
	uint data_size = group_head_size;
	for (uint i = 0; i < chr->n_entry; i++)
	{
		entry_rel[i] = data_size;
		data_size += 8 + chr_count_slots (chr->entry + i) * 4;
	}

	// track data follows all entries, in entry then slot order
	const uint track_start = data_size;
	uint track_size = 0, n_track = 0;
	for (uint i = 0; i < chr->n_entry; i++)
	{
		const chr0_entry_t *e = chr->entry + i;
		for (uint grp = 0; grp < CHR0_N_GROUP; grp++)
		{
			if (!e->group[grp].exists)
				continue;
			const uint n_slot = e->group[grp].isotropic ? 1 : 3;
			for (uint s = 0; s < n_slot; s++)
			{
				const uint chan = grp * 3 + (e->group[grp].isotropic ? 0 : s);
				if (!e->channel[chan].is_fixed)
				{
					track_size += GetEncodedSizeBANIM (&e->channel[chan].track, frame_limit);
					n_track++;
				}
			}
		}
	}

	//--- pre-encode all tracks, sharing byte identical ones as retail does

	u8 *blob = MALLOC (track_size ? track_size : 1);
	uint blob_used = 0;
	uint *track_at = CALLOC (n_track ? n_track : 1, sizeof (uint));
	uint *track_len = CALLOC (n_track ? n_track : 1, sizeof (uint));
	uint track_idx = 0;

	for (uint i = 0; i < chr->n_entry; i++)
	{
		const chr0_entry_t *e = chr->entry + i;
		for (uint grp = 0; grp < CHR0_N_GROUP; grp++)
		{
			if (!e->group[grp].exists)
				continue;
			const uint n_slot = e->group[grp].isotropic ? 1 : 3;
			for (uint s = 0; s < n_slot; s++)
			{
				const uint chan = grp * 3 + (e->group[grp].isotropic ? 0 : s);
				if (e->channel[chan].is_fixed)
					continue;
				const uint len
					= EncodeTrackBANIM (&e->channel[chan].track, blob + blob_used, frame_limit);

				uint found = ~0u;
				for (uint p = 0; p < track_idx; p++)
					if (track_len[p] == len
						&& !memcmp (blob + track_at[p], blob + blob_used, len))
					{
						found = track_at[p];
						break;
					}

				track_len[track_idx] = len;
				if (found == ~0u)
				{
					track_at[track_idx] = blob_used;
					blob_used += len;
				}
				else
					track_at[track_idx] = found;
				track_idx++;
			}
		}
	}
	track_size = blob_used;

	// the declared size stops at the end of the data section; the name pool
	// behind it belongs to the enclosing BRRES
	const uint file_size = group_off + track_start + track_size;

	const uint n_names = chr->n_entry + 2;
	ccp *names = CALLOC (n_names, sizeof (ccp));
	uint *name_off = CALLOC (n_names, sizeof (uint));
	names[0] = chr->name ? chr->name : "";
	names[1] = chr->orig_path ? chr->orig_path : "";
	for (uint i = 0; i < chr->n_entry; i++)
		names[i + 2] = chr->entry[i].name;

	const uint pool_size = CalcPoolBANIM (names, n_names, file_size, name_off);
	const uint total_size = file_size + pool_size;

	u8 *buf = CALLOC (total_size, 1);
	WritePoolBANIM (buf, names, n_names, file_size);
	memcpy (buf + group_off + track_start, blob, track_size);
	FREE (blob);

	//--- header

	memcpy (buf, "CHR0", 4);
	chr_w32 (buf + 4, file_size);
	chr_w32 (buf + 8, chr->version);
	chr_w32 (buf + 0xc, 0); // brres_offset, patched by the container

	chr_w32 (buf + 0x10, group_off);
	uint off = 0x14;
	if (chr->version == 5)
	{
		chr_w32 (buf + off, 0); // user_data_offset: none
		off += 4;
	}
	chr_w32 (buf + off, name_off[0]); // string_offset
	off += 4;
	chr_w32 (buf + off, name_off[1]); // orig_path_offset
	off += 4;
	chr_w16 (buf + off, (u16)chr->n_frames);
	off += 2;
	chr_w16 (buf + off, (u16)chr->n_entry);
	off += 2;
	chr_w32 (buf + off, chr->loop ? 1 : 0);
	off += 4;
	chr_w32 (buf + off, chr->scaling_rule);

	//--- resource group, with the NW4R lookup tree

	u8 *group = buf + group_off;
	chr_w32 (group, group_head_size);
	chr_w32 (group + 4, chr->n_entry);

	brres_info_t *info = CALLOC (chr->n_entry + 1, sizeof (*info));
	info[0].id = 0xffff;
	info[0].name = "";
	info[0].nlen = 0;
	for (uint i = 0; i < chr->n_entry; i++)
	{
		info[i + 1].name = chr->entry[i].name;
		info[i + 1].nlen = strlen (chr->entry[i].name);
		CalcEntryBRRES (info, i + 1);
	}

	for (uint i = 0; i <= chr->n_entry; i++)
	{
		u8 *rec = group + 8 + i * 16;
		chr_w16 (rec, info[i].id);
		chr_w16 (rec + 2, 0);
		chr_w16 (rec + 4, info[i].left_idx);
		chr_w16 (rec + 6, info[i].right_idx);
		if (i)
		{
			chr_w32 (rec + 8, name_off[i + 1] - group_off);
			chr_w32 (rec + 12, entry_rel[i - 1]);
		}
	}
	FREE (info);

	track_idx = 0;
	for (uint i = 0; i < chr->n_entry; i++)
	{
		chr0_entry_t *e = chr->entry + i;

		u8 *entry = group + entry_rel[i];
		chr_w32 (entry, name_off[i + 2] - group_off - entry_rel[i]);
		chr_w32 (entry + 4, e->code);

		u8 *slot = entry + 8;
		for (uint grp = 0; grp < CHR0_N_GROUP; grp++)
		{
			if (!e->group[grp].exists)
				continue;
			const uint n_slot = e->group[grp].isotropic ? 1 : 3;
			for (uint s = 0; s < n_slot; s++)
			{
				const uint chan = grp * 3 + (e->group[grp].isotropic ? 0 : s);
				const chr0_channel_t *ch = e->channel + chan;
				if (ch->is_fixed)
					chr_wf (slot, ch->value);
				else
					chr_w32 (slot, track_start + track_at[track_idx++] - entry_rel[i]);
				slot += 4;
			}
		}
	}

	FREE (entry_rel);
	FREE (track_at);
	FREE (track_len);
	FREE (names);
	FREE (name_off);

	//--- write file

	File_t F;
	enumError err = CreateFileOpt (&F, true, fname, testmode, chr->fname);
	if (err <= ERR_WARNING && F.f)
	{
		SetFileAttrib (&F.fatt, &chr->fatt, 0);
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
// Line based, mirroring the shape used by lib-vis0.c:
//
//   #CHR0
//   version      = 3|4|5
//   name         = "<string>"
//   orig-path    = "<string>"     (omitted when empty)
//   n-frames     = <uint>
//   loop         = 0|1
//   scaling-rule = <uint>
//
//   bone "<name>" code 0x<hex>
//     <channel> fixed <float>
//     <channel> track <format> <frame-scale> <step> <base> <unknown>
//       <frame> <value> <tangent>
//       ...
//
// where <channel> is one of scale-x .. trans-z. The code word is written
// verbatim so that a decode/encode round trip reproduces the original
// grouping and format selection exactly.

static ccp chr_channel_name[CHR0_N_CHANNEL]
	= { "scale-x", "scale-y", "scale-z", "rot-x", "rot-y", "rot-z", "trans-x", "trans-y",
		"trans-z" };

///////////////////////////////////////////////////////////////////////////////

static void PrintQuotedCHR0 (FILE *f, ccp s)
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

///////////////////////////////////////////////////////////////////////////////

static ccp ScanQuotedCHR0 (ccp src, ccp *result)
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

enumError SaveTextCHR0 (chr0_t *chr, ccp fname, bool set_time)
{
	DASSERT (chr);
	DASSERT (fname);

	File_t F;
	enumError err = CreateFileOpt (&F, true, fname, testmode, chr->fname);
	if (err > ERR_WARNING || !F.f)
	{
		ResetFile (&F, 0);
		return err;
	}
	SetFileAttrib (&F.fatt, &chr->fatt, 0);

	fprintf (F.f, "#CHR0\n");
	fprintf (F.f, "# Wiimms SZS Tools Plus -- CHR0 bone animation\n");
	if (chr->fname)
		fprintf (F.f, "# decoded from %s\n", chr->fname);
	fprintf (F.f, "\n");
	fprintf (F.f, "version      = %u\n", chr->version);
	fprintf (F.f, "name         = ");
	PrintQuotedCHR0 (F.f, chr->name ? chr->name : "");
	fprintf (F.f, "\n");
	if (chr->orig_path && *chr->orig_path)
	{
		fprintf (F.f, "orig-path    = ");
		PrintQuotedCHR0 (F.f, chr->orig_path);
		fprintf (F.f, "\n");
	}
	fprintf (F.f, "n-frames     = %u\n", chr->n_frames);
	fprintf (F.f, "loop         = %u\n", chr->loop ? 1 : 0);
	fprintf (F.f, "scaling-rule = %u\n", chr->scaling_rule);
	fprintf (F.f, "n-entries    = %u\n\n", chr->n_entry);

	for (uint i = 0; i < chr->n_entry; i++)
	{
		const chr0_entry_t *e = chr->entry + i;
		fprintf (F.f, "bone ");
		PrintQuotedCHR0 (F.f, e->name);
		fprintf (F.f, " code 0x%08x\n", e->code);

		for (uint grp = 0; grp < CHR0_N_GROUP; grp++)
		{
			if (!e->group[grp].exists)
				continue;
			const uint n_slot = e->group[grp].isotropic ? 1 : 3;
			for (uint s = 0; s < n_slot; s++)
			{
				const uint chan = grp * 3 + (e->group[grp].isotropic ? 0 : s);
				const chr0_channel_t *ch = e->channel + chan;
				if (ch->is_fixed)
					fprintf (F.f, "  %-7s fixed %.9g\n", chr_channel_name[chan], ch->value);
				else
				{
					const banim_track_t *tr = &ch->track;
					fprintf (F.f, "  %-7s track %s %.9g %.9g %.9g %u\n",
						chr_channel_name[chan], GetFormatNameBANIM (tr->format),
						tr->frame_scale, tr->step, tr->base, tr->unknown);
					for (uint k = 0; k < tr->n_key; k++)
						fprintf (F.f, "    %.9g %.9g %.9g\n", tr->key[k].frame,
							tr->key[k].value, tr->key[k].tangent);
				}
			}
		}
		fprintf (F.f, "\n");
	}

	ResetFile (&F, set_time ? opt_preserve : 0);
	return ERR_OK;
}

///////////////////////////////////////////////////////////////////////////////

enumError ScanTextCHR0 (chr0_t *chr, bool init_chr, ccp src_fname)
{
	DASSERT (chr);
	DASSERT (src_fname);

	FILE *f = fopen (src_fname, "r");
	if (!f)
		return ERROR0 (ERR_CANT_OPEN, "Can't open CHR0 text file: %s\n", src_fname);

	if (init_chr)
		InitializeCHR0 (chr);
	else
		ResetCHR0 (chr);
	chr->fname = STRDUP (src_fname);

	char line[4096];
	bool first = true;
	chr0_entry_t *cur = 0;
	banim_track_t *cur_track = 0;

	while (fgets (line, sizeof (line), f))
	{
		ccp s = line;
		while (*s == ' ' || *s == '\t')
			s++;

		if (first)
		{
			first = false;
			if (strncmp (s, "#CHR0", 5))
			{
				fclose (f);
				ResetCHR0 (chr);
				return ERROR0 (ERR_WRONG_FILE_TYPE, "Not a CHR0 text file: %s\n", src_fname);
			}
			continue;
		}

		if (!*s || *s == '\r' || *s == '\n' || *s == '#')
			continue;

		uint uval;
		if (sscanf (s, "version = %u", &uval) == 1)
			chr->version = uval;
		else if (sscanf (s, "n-frames = %u", &uval) == 1)
			chr->n_frames = uval;
		else if (sscanf (s, "loop = %u", &uval) == 1)
			chr->loop = uval != 0;
		else if (sscanf (s, "scaling-rule = %u", &uval) == 1)
			chr->scaling_rule = uval;
		else if (sscanf (s, "n-entries = %u", &uval) == 1)
			; // informational
		else if (!strncmp (s, "name", 4) && strchr (s, '='))
		{
			FreeString (chr->name);
			ScanQuotedCHR0 (strchr (s, '=') + 1, &chr->name);
		}
		else if (!strncmp (s, "orig-path", 9) && strchr (s, '='))
		{
			FreeString (chr->orig_path);
			ScanQuotedCHR0 (strchr (s, '=') + 1, &chr->orig_path);
		}
		else if (!strncmp (s, "bone", 4))
		{
			ccp bname;
			ccp rest = ScanQuotedCHR0 (s + 4, &bname);
			if (!rest)
				continue;
			cur = AppendEntryCHR0 (chr, bname);
			FreeString (bname);
			cur_track = 0;

			ccp c = strstr (rest, "code");
			u32 code = 0;
			if (c && sscanf (c + 4, " 0x%x", &code) == 1)
				cur->code = code;

			// rebuild the group flags from the code word
			for (uint grp = 0; grp < CHR0_N_GROUP; grp++)
			{
				chr0_group_t *g = cur->group + grp;
				g->exists = (cur->code >> (CHR0_BIT_EXISTS + grp) & 1) != 0;
				g->isotropic = (cur->code >> (CHR0_BIT_ISOTROPIC + grp) & 1) != 0;
				g->format = chr_get_format (cur->code, chr->version, grp);
			}
		}
		else if (cur)
		{
			// channel line, or a keyframe of the current track
			char cname[32], kind[16], fmtname[16];
			double v;
			if (sscanf (s, "%31s %15s", cname, kind) == 2 && !strcmp (kind, "fixed"))
			{
				for (uint c = 0; c < CHR0_N_CHANNEL; c++)
					if (!strcmp (cname, chr_channel_name[c]))
					{
						ccp p = strstr (s, "fixed");
						if (p && sscanf (p + 5, "%lf", &v) == 1)
						{
							cur->channel[c].is_fixed = true;
							cur->channel[c].value = (float)v;
						}
						break;
					}
				cur_track = 0;
			}
			else if (sscanf (s, "%31s %15s %15s", cname, kind, fmtname) == 3
				&& !strcmp (kind, "track"))
			{
				cur_track = 0;
				for (uint c = 0; c < CHR0_N_CHANNEL; c++)
					if (!strcmp (cname, chr_channel_name[c]))
					{
						chr0_channel_t *ch = cur->channel + c;
						ch->is_fixed = false;
						ResetTrackBANIM (&ch->track);
						ch->track.format = ScanFormatNameBANIM (fmtname);

						double fs = 0, st = 0, bs = 0;
						uint unk = 0;
						ccp p = strstr (s, fmtname);
						if (p && sscanf (p + strlen (fmtname), "%lf %lf %lf %u", &fs, &st,
								&bs, &unk)
								>= 3)
						{
							ch->track.frame_scale = (float)fs;
							ch->track.step = (float)st;
							ch->track.base = (float)bs;
							ch->track.unknown = (u16)unk;
						}
						cur_track = &ch->track;
						break;
					}
			}
			else if (cur_track)
			{
				double fr, val, tan;
				if (sscanf (s, "%lf %lf %lf", &fr, &val, &tan) == 3)
					AppendKeyBANIM (cur_track, (float)fr, (float)val, (float)tan);
			}
		}
	}

	fclose (f);

	if (!chr->version)
		chr->version = CHR0_DEFAULT_VERSION;

	return ERR_OK;
}
