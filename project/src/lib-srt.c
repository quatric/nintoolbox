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
#include "lib-srt.h"
#include "lib-szs.h"
#include "lib-brres.h"

///////////////////////////////////////////////////////////////////////////////
///////////////			byte order helper		///////////////
///////////////////////////////////////////////////////////////////////////////

static inline u16 srt_rd16 (const u8 *p) { return (u16)p[0] << 8 | p[1]; }

static inline u32 srt_rd32 (const u8 *p)
{
	return (u32)p[0] << 24 | (u32)p[1] << 16 | (u32)p[2] << 8 | p[3];
}

static inline void srt_w16 (u8 *p, u16 v)
{
	p[0] = v >> 8;
	p[1] = (u8)v;
}

static inline void srt_w32 (u8 *p, u32 v)
{
	p[0] = v >> 24;
	p[1] = v >> 16;
	p[2] = v >> 8;
	p[3] = (u8)v;
}

static inline float srt_rdf (const u8 *p)
{
	const u32 raw = srt_rd32 (p);
	float f;
	memcpy (&f, &raw, 4);
	return f;
}

static inline void srt_wf (u8 *p, float f)
{
	u32 raw;
	memcpy (&raw, &f, 4);
	srt_w32 (p, raw);
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			code word layout		///////////////
///////////////////////////////////////////////////////////////////////////////
// Per texture entry, from BrawlLib's SRT0Code:
//
//   bit 0   always set
//   bit 1   scale is one           (no scale data at all)
//   bit 2   rotation is zero       (no rotation data at all)
//   bit 3   translation is zero    (no translation data at all)
//   bit 4   scale is isotropic     (one shared value for X and Y)
//   bit 5   scale X is fixed
//   bit 6   scale Y is fixed
//   bit 7   rotation is fixed
//   bit 8   translation X is fixed
//   bit 9   translation Y is fixed
//
// Note the inverted sense of bits 1..3 compared with CHR0: here a set bit
// means the group is ABSENT.

#define SRT0_BIT_NO_SCALE 1
#define SRT0_BIT_NO_ROTATION 2
#define SRT0_BIT_NO_TRANSLATION 3
#define SRT0_BIT_SCALE_ISOTROPIC 4
#define SRT0_BIT_FIXED 5 // +0 scale X, +1 scale Y, +2 rot, +3 trans X, +4 trans Y

// channel indices
enum
{
	SRT0_SCALE_X = 0,
	SRT0_SCALE_Y,
	SRT0_ROTATION,
	SRT0_TRANS_X,
	SRT0_TRANS_Y
};

//
///////////////////////////////////////////////////////////////////////////////
///////////////			init/reset			///////////////
///////////////////////////////////////////////////////////////////////////////

void InitializeSRT0 (srt0_t *srt)
{
	DASSERT (srt);
	memset (srt, 0, sizeof (*srt));
	srt->version = SRT0_DEFAULT_VERSION;
}

///////////////////////////////////////////////////////////////////////////////

void ResetSRT0 (srt0_t *srt)
{
	if (!srt)
		return;

	FreeString (srt->fname);
	FreeString (srt->name);
	FreeString (srt->orig_path);

	for (uint i = 0; i < srt->n_entry; i++)
	{
		srt0_entry_t *e = srt->entry + i;
		FreeString (e->name);
		for (uint t = 0; t < e->n_texture; t++)
			for (uint c = 0; c < SRT0_N_CHANNEL; c++)
				ResetTrackBANIM (&e->texture[t].channel[c].track);
		FREE (e->texture);
	}
	FREE (srt->entry);

	InitializeSRT0 (srt);
}

///////////////////////////////////////////////////////////////////////////////

srt0_entry_t *AppendEntrySRT0 (srt0_t *srt, ccp name)
{
	DASSERT (srt);
	if (srt->n_entry == srt->n_entry_alloced)
	{
		srt->n_entry_alloced = srt->n_entry_alloced ? srt->n_entry_alloced * 2 : 8;
		srt->entry = REALLOC (srt->entry, srt->n_entry_alloced * sizeof (*srt->entry));
	}

	srt0_entry_t *e = srt->entry + srt->n_entry++;
	memset (e, 0, sizeof (*e));
	e->name = STRDUP (name ? name : "");
	return e;
}

///////////////////////////////////////////////////////////////////////////////

srt0_texture_t *AppendTextureSRT0 (srt0_entry_t *entry, bool indirect, uint layer)
{
	DASSERT (entry);

	// the number of layers per entry is bounded by the two masks, so a plain
	// exact-size reallocation is cheap enough here
	entry->texture
		= REALLOC (entry->texture, (entry->n_texture + 1) * sizeof (*entry->texture));

	srt0_texture_t *t = entry->texture + entry->n_texture++;
	memset (t, 0, sizeof (*t));
	t->indirect = indirect;
	t->layer = layer;
	for (uint c = 0; c < SRT0_N_CHANNEL; c++)
	{
		InitializeTrackBANIM (&t->channel[c].track);
		t->channel[c].is_fixed = true;
	}
	// identity texture matrix: unit scale, no rotation, no translation
	t->channel[SRT0_SCALE_X].value = 1.0f;
	t->channel[SRT0_SCALE_Y].value = 1.0f;
	return t;
}

///////////////////////////////////////////////////////////////////////////////

uint GetFrameLimitSRT0 (const srt0_t *srt)
{
	DASSERT (srt);
	return srt->n_frames + (srt->loop ? 1 : 0);
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			ScanRawSRT0			///////////////
///////////////////////////////////////////////////////////////////////////////
// Layout, big endian:
//
//  common BRRES sub-file header (0x10 bytes): magic "SRT0", size, version,
//                                             brres_offset
//  0x10  u32 data_offset       // -> resource group, relative to file start
//        u32 user_data_offset  // version 5 only
//        u32 string_offset
//        u32 orig_path_offset
//        u16 n_frames
//        u16 n_entries
//        u32 matrix_mode
//        u32 loop
//
// each SRT0 entry, at group + record.data_off:
//  0x00  u32 string_offset, relative to the entry
//  0x04  u32 texture_indices   // bitmask of animated ordinary layers
//  0x08  u32 indirect_indices  // bitmask of animated indirect layers
//  0x0c  one u32 offset per set mask bit, relative to the entry
//
// each texture entry, at entry + offset:
//  0x00  u32 code
//  0x04  one 4-byte slot per stored channel, in order scale X, scale Y,
//        rotation, translation X, translation Y. A slot holds either the
//        channel's constant float or an offset to an I12 track. Unlike CHR0,
//        a track offset here is relative to the slot's own address.

enumError ScanRawSRT0 (srt0_t *srt, bool init_srt, const void *data, uint data_size)
{
	DASSERT (srt);
	DASSERT (data);

	if (init_srt)
		InitializeSRT0 (srt);
	else
		ResetSRT0 (srt);

	const u8 *base = data;
	if (data_size < 0x28 || memcmp (base, "SRT0", 4))
		return ERR_WRONG_FILE_TYPE;

	const u32 version = srt_rd32 (base + 8);
	if (version < SRT0_MIN_VERSION || version > SRT0_MAX_VERSION)
		return ERROR0 (ERR_INVALID_DATA, "SRT0: unsupported sub-version %u\n", version);
	srt->version = version;

	const uint head_size = version == 5 ? 0x2c : 0x28;
	if (data_size < head_size)
		return ERROR0 (ERR_INVALID_DATA, "SRT0: file too small\n");

	const u32 data_off = srt_rd32 (base + 0x10);
	uint off = 0x14;
	if (version == 5)
		off += 4; // user_data_offset
	const u32 string_off = srt_rd32 (base + off);
	off += 4;
	const u32 orig_path_off = srt_rd32 (base + off);
	off += 4;
	srt->n_frames = srt_rd16 (base + off);
	off += 2;
	const uint n_entries_hdr = srt_rd16 (base + off);
	off += 2;
	srt->matrix_mode = srt_rd32 (base + off);
	off += 4;
	srt->loop = srt_rd32 (base + off) != 0;

	if (string_off && string_off < data_size)
		srt->name = STRDUP ((ccp)(base + string_off));
	if (orig_path_off && orig_path_off < data_size)
		srt->orig_path = STRDUP ((ccp)(base + orig_path_off));

	if (!data_off || (u64)data_off + 8 > data_size)
		return ERROR0 (ERR_INVALID_DATA, "SRT0: invalid group offset\n");

	const u8 *group = base + data_off;
	const u32 n_entries = srt_rd32 (group + 4);
	if (n_entries != n_entries_hdr)
		; // the group's own count is authoritative
	if ((u64)data_off + 8 + (u64)(n_entries + 1) * 16 > data_size)
		return ERROR0 (ERR_INVALID_DATA, "SRT0: group entry table exceeds file size\n");

	const uint frame_limit = GetFrameLimitSRT0 (srt);

	for (u32 i = 1; i <= n_entries; i++)
	{
		const u8 *rec = group + 8 + (size_t)i * 16;
		const u32 name_off = srt_rd32 (rec + 8);
		const u32 entry_off = srt_rd32 (rec + 12);

		if (!entry_off || (u64)data_off + entry_off + 12 > data_size)
			return ERROR0 (ERR_INVALID_DATA, "SRT0: invalid entry offset\n");
		const uint entry_pos = data_off + entry_off;

		ccp name = name_off && (u64)data_off + name_off < data_size
			? (ccp)(base + data_off + name_off)
			: "";
		srt0_entry_t *e = AppendEntrySRT0 (srt, name);
		e->tex_mask = srt_rd32 (base + entry_pos + 4);
		e->ind_mask = srt_rd32 (base + entry_pos + 8);

		uint slot = entry_pos + 12; // absolute position of the next layer offset

		for (uint kind = 0; kind < 2; kind++)
		{
			const u32 mask = kind ? e->ind_mask : e->tex_mask;
			const uint n_layer = kind ? SRT0_N_INDIRECT : SRT0_N_TEXTURE;

			for (uint layer = 0; layer < n_layer; layer++)
			{
				if (!(mask >> layer & 1))
					continue;
				if (slot + 4 > data_size)
					return ERROR0 (ERR_INVALID_DATA,
						"SRT0: entry '%s' layer table exceeds file size\n", e->name);

				const u32 rel = srt_rd32 (base + slot);
				const u64 tex_pos = (u64)entry_pos + rel;
				slot += 4;
				if (tex_pos + 4 > data_size)
					return ERROR0 (ERR_INVALID_DATA,
						"SRT0: entry '%s' layer offset out of range\n", e->name);

				srt0_texture_t *t = AppendTextureSRT0 (e, kind != 0, layer);
				t->code = srt_rd32 (base + tex_pos);

				// walk the five channels in their fixed storage order
				uint chan_slot = (uint)tex_pos + 4;
				static const uint group_of[SRT0_N_CHANNEL]
					= { 0, 0, 1, 2, 2 }; // scale, scale, rot, trans, trans
				static const uint no_bit[3] = { SRT0_BIT_NO_SCALE, SRT0_BIT_NO_ROTATION,
					SRT0_BIT_NO_TRANSLATION };

				for (uint c = 0; c < SRT0_N_CHANNEL; c++)
				{
					// a group flagged "no data" stores nothing at all
					if (t->code >> no_bit[group_of[c]] & 1)
						continue;

					// an isotropic scale stores only the X channel, which
					// then also supplies Y
					if (c == SRT0_SCALE_Y && (t->code >> SRT0_BIT_SCALE_ISOTROPIC & 1))
					{
						t->channel[c] = t->channel[SRT0_SCALE_X];
						InitializeTrackBANIM (&t->channel[c].track);
						continue;
					}

					if (chan_slot + 4 > data_size)
						return ERROR0 (ERR_INVALID_DATA,
							"SRT0: entry '%s' channel slot exceeds file size\n", e->name);

					srt0_channel_t *ch = t->channel + c;
					if (t->code >> (SRT0_BIT_FIXED + c) & 1)
					{
						ch->is_fixed = true;
						ch->value = srt_rdf (base + chan_slot);
					}
					else
					{
						ch->is_fixed = false;
						// SRT0 track offsets are relative to the slot itself
						const u64 track_pos
							= (u64)chan_slot + srt_rd32 (base + chan_slot);
						if (track_pos + 8 > data_size)
							return ERROR0 (ERR_INVALID_DATA,
								"SRT0: entry '%s' track offset out of range\n", e->name);

						const enumError err = DecodeTrackBANIM (&ch->track,
							base + track_pos, data_size - (uint)track_pos, BANIM_I12,
							frame_limit);
						if (err)
							return err;
					}
					chan_slot += 4;
				}
			}
		}
	}

	return ERR_OK;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			SaveRawSRT0			///////////////
///////////////////////////////////////////////////////////////////////////////

// Number of 4-byte channel slots a texture entry stores.
static uint srt_count_slots (const srt0_texture_t *t)
{
	static const uint group_of[SRT0_N_CHANNEL] = { 0, 0, 1, 2, 2 };
	static const uint no_bit[3]
		= { SRT0_BIT_NO_SCALE, SRT0_BIT_NO_ROTATION, SRT0_BIT_NO_TRANSLATION };

	uint n = 0;
	for (uint c = 0; c < SRT0_N_CHANNEL; c++)
	{
		if (t->code >> no_bit[group_of[c]] & 1)
			continue;
		if (c == SRT0_SCALE_Y && (t->code >> SRT0_BIT_SCALE_ISOTROPIC & 1))
			continue;
		n++;
	}
	return n;
}

///////////////////////////////////////////////////////////////////////////////

enumError SaveRawSRT0 (srt0_t *srt, ccp fname, bool set_time)
{
	DASSERT (srt);
	DASSERT (fname);

	const uint head_size = srt->version == 5 ? 0x2c : 0x28;
	const uint frame_limit = GetFrameLimitSRT0 (srt);

	//--- data section layout: group header, entry headers, texture entries,
	//--- track data. All offsets below are relative to the group start.

	const uint group_off = head_size;
	const uint group_head_size = 8 + (srt->n_entry + 1) * 16;

	// Retail interleaves each entry's texture entries directly behind that
	// entry's own header -- header, its layers, next header, its layers -- and
	// only then emits one shared block holding every track. Laying the texture
	// entries out as one block behind all headers produces a file of exactly
	// the same size that is not byte identical to retail.
	const uint n_alloc = srt->n_entry ? srt->n_entry : 1;
	uint *entry_rel = CALLOC (n_alloc, sizeof (uint));
	uint *entry_tex_rel = CALLOC (n_alloc, sizeof (uint));

	uint data_size = group_head_size;
	for (uint i = 0; i < srt->n_entry; i++)
	{
		entry_rel[i] = data_size;
		data_size += 12 + srt->entry[i].n_texture * 4;

		entry_tex_rel[i] = data_size;
		for (uint t = 0; t < srt->entry[i].n_texture; t++)
			data_size += 4 + srt_count_slots (srt->entry[i].texture + t) * 4;
	}

	const uint track_start = data_size;

	// upper bound: retail files share byte identical track data between
	// channels, so the final size may be smaller -- see the dedup below
	uint track_size = 0;
	uint n_track = 0;
	for (uint i = 0; i < srt->n_entry; i++)
		for (uint t = 0; t < srt->entry[i].n_texture; t++)
		{
			const srt0_texture_t *tx = srt->entry[i].texture + t;
			for (uint c = 0; c < SRT0_N_CHANNEL; c++)
				if (!tx->channel[c].is_fixed)
				{
					track_size += GetEncodedSizeBANIM (&tx->channel[c].track, frame_limit);
					n_track++;
				}
		}

	//--- pre-encode all tracks into a scratch blob, sharing byte identical
	//--- ones exactly as retail files do

	u8 *blob = MALLOC (track_size ? track_size : 1);
	uint blob_used = 0;
	uint *track_at = CALLOC (n_track ? n_track : 1, sizeof (uint)); // rel to track_start
	uint *track_uid = CALLOC (n_track ? n_track : 1, sizeof (uint));

	// the unique blobs, in encounter order
	uint *uni_at = CALLOC (n_track ? n_track : 1, sizeof (uint)); // rel to 'blob'
	uint *uni_len = CALLOC (n_track ? n_track : 1, sizeof (uint));
	uint n_uni = 0;
	uint track_idx = 0;

	// where each texture entry's channel tracks start, in encounter order
	uint n_tex_total = 0;
	for (uint i = 0; i < srt->n_entry; i++)
		n_tex_total += srt->entry[i].n_texture;
	uint *tex_first = CALLOC (n_tex_total ? n_tex_total : 1, sizeof (uint));
	uint *tex_ntrack = CALLOC (n_tex_total ? n_tex_total : 1, sizeof (uint));
	uint tex_idx = 0;

	for (uint i = 0; i < srt->n_entry; i++)
		for (uint t = 0; t < srt->entry[i].n_texture; t++)
		{
			const srt0_texture_t *tx = srt->entry[i].texture + t;
			tex_first[tex_idx] = track_idx;
			for (uint c = 0; c < SRT0_N_CHANNEL; c++)
			{
				if (tx->channel[c].is_fixed)
					continue;
				const uint len
					= EncodeTrackBANIM (&tx->channel[c].track, blob + blob_used, frame_limit);

				uint found = ~0u;
				for (uint p = 0; p < n_uni; p++)
					if (uni_len[p] == len && !memcmp (blob + uni_at[p], blob + blob_used, len))
					{
						found = p;
						break;
					}

				if (found == ~0u)
				{
					found = n_uni++;
					uni_at[found] = blob_used;
					uni_len[found] = len;
					blob_used += len;
				}
				track_uid[track_idx++] = found;
			}
			tex_ntrack[tex_idx] = track_idx - tex_first[tex_idx];
			tex_idx++;
		}
	track_size = blob_used;

	// Retail does not emit the shared track blobs in the order the channels
	// first reference them. It walks the texture entries in *reverse* order --
	// last layer of the last material first -- while keeping the channels
	// inside one texture entry in their normal order, and then stably sorts
	// the resulting list by descending encoded size. All three parts were
	// needed: files whose tracks differ in size fail without the sort, files
	// whose equal-size tracks live in different layers fail without the
	// reversal, and files whose equal-size tracks share one layer fail if the
	// reversal is applied per track instead of per texture entry.
	uint *order = CALLOC (n_uni ? n_uni : 1, sizeof (uint));
	u8 *seen = CALLOC (n_uni ? n_uni : 1, 1);
	uint n_order = 0;
	for (int x = (int)n_tex_total - 1; x >= 0; x--)
		for (uint k = 0; k < tex_ntrack[x]; k++)
		{
			const uint u = track_uid[tex_first[x] + k];
			if (!seen[u])
			{
				seen[u] = 1;
				order[n_order++] = u;
			}
		}
	DASSERT (n_order == n_uni);
	FREE (seen);
	FREE (tex_first);
	FREE (tex_ntrack);
	for (uint i = 1; i < n_uni; i++)
	{
		const uint cur = order[i];
		int j = i - 1;
		while (j >= 0 && uni_len[order[j]] < uni_len[cur])
		{
			order[j + 1] = order[j];
			j--;
		}
		order[j + 1] = cur;
	}

	// lay the unique blobs out in that order and repack the scratch buffer
	uint *uni_final = CALLOC (n_uni ? n_uni : 1, sizeof (uint));
	u8 *packed = MALLOC (track_size ? track_size : 1);
	uint packed_used = 0;
	for (uint i = 0; i < n_uni; i++)
	{
		const uint u = order[i];
		uni_final[u] = packed_used;
		memcpy (packed + packed_used, blob + uni_at[u], uni_len[u]);
		packed_used += uni_len[u];
	}
	FREE (blob);
	blob = packed;

	for (uint i = 0; i < track_idx; i++)
		track_at[i] = uni_final[track_uid[i]];

	FREE (order);
	FREE (uni_final);
	FREE (uni_at);
	FREE (uni_len);
	FREE (track_uid);

	// the sub-file's declared size stops at the end of the data section; the
	// string pool that follows belongs to the enclosing BRRES
	const uint file_size = group_off + track_start + track_size;

	//--- string pool, appended behind the data section

	const uint n_names = srt->n_entry + 2;
	ccp *names = CALLOC (n_names, sizeof (ccp));
	uint *name_off = CALLOC (n_names, sizeof (uint));
	names[0] = srt->name ? srt->name : "";
	names[1] = srt->orig_path ? srt->orig_path : "";
	for (uint i = 0; i < srt->n_entry; i++)
		names[i + 2] = srt->entry[i].name;

	const uint pool_size = CalcPoolSortedBANIM (names, n_names, file_size, name_off);
	const uint total_size = file_size + pool_size;

	u8 *buf = CALLOC (total_size, 1);
	WritePoolSortedBANIM (buf, names, n_names, file_size);

	//--- header

	memcpy (buf, "SRT0", 4);
	srt_w32 (buf + 4, file_size);
	srt_w32 (buf + 8, srt->version);
	srt_w32 (buf + 0xc, 0); // brres_offset, patched by the container

	srt_w32 (buf + 0x10, group_off);
	uint off = 0x14;
	if (srt->version == 5)
	{
		srt_w32 (buf + off, 0); // user_data_offset: none
		off += 4;
	}
	srt_w32 (buf + off, name_off[0]);
	off += 4;
	srt_w32 (buf + off, name_off[1]);
	off += 4;
	srt_w16 (buf + off, (u16)srt->n_frames);
	off += 2;
	srt_w16 (buf + off, (u16)srt->n_entry);
	off += 2;
	srt_w32 (buf + off, srt->matrix_mode);
	off += 4;
	srt_w32 (buf + off, srt->loop ? 1 : 0);

	//--- resource group, with the NW4R lookup tree

	u8 *group = buf + group_off;
	srt_w32 (group, group_head_size);
	srt_w32 (group + 4, srt->n_entry);

	brres_info_t *info = CALLOC (srt->n_entry + 1, sizeof (*info));
	info[0].id = 0xffff;
	info[0].name = "";
	info[0].nlen = 0;
	for (uint i = 0; i < srt->n_entry; i++)
	{
		info[i + 1].name = srt->entry[i].name;
		info[i + 1].nlen = strlen (srt->entry[i].name);
		CalcEntryBRRES (info, i + 1);
	}

	for (uint i = 0; i <= srt->n_entry; i++)
	{
		u8 *rec = group + 8 + i * 16;
		srt_w16 (rec, info[i].id);
		srt_w16 (rec + 2, 0);
		srt_w16 (rec + 4, info[i].left_idx);
		srt_w16 (rec + 6, info[i].right_idx);
		if (i)
		{
			srt_w32 (rec + 8, name_off[i + 1] - group_off);
			srt_w32 (rec + 12, entry_rel[i - 1]);
		}
	}
	FREE (info);

	memcpy (buf + group_off + track_start, blob, track_size);
	FREE (blob);

	uint tex_pos = 0;
	track_idx = 0;

	for (uint i = 0; i < srt->n_entry; i++)
	{
		srt0_entry_t *e = srt->entry + i;

		u8 *entry = group + entry_rel[i];
		srt_w32 (entry, name_off[i + 2] - group_off - entry_rel[i]);
		tex_pos = entry_tex_rel[i];

		// rebuild the masks from the layer list so that an edited text file
		// stays self consistent
		u32 tex_mask = 0, ind_mask = 0;
		for (uint t = 0; t < e->n_texture; t++)
		{
			const srt0_texture_t *tx = e->texture + t;
			if (tx->indirect)
				ind_mask |= 1u << tx->layer;
			else
				tex_mask |= 1u << tx->layer;
		}
		e->tex_mask = tex_mask;
		e->ind_mask = ind_mask;
		srt_w32 (entry + 4, tex_mask);
		srt_w32 (entry + 8, ind_mask);

		for (uint t = 0; t < e->n_texture; t++)
		{
			srt0_texture_t *tx = e->texture + t;
			srt_w32 (entry + 12 + t * 4, tex_pos - entry_rel[i]);

			u8 *tex = group + tex_pos;
			srt_w32 (tex, tx->code);

			static const uint group_of[SRT0_N_CHANNEL] = { 0, 0, 1, 2, 2 };
			static const uint no_bit[3] = { SRT0_BIT_NO_SCALE, SRT0_BIT_NO_ROTATION,
				SRT0_BIT_NO_TRANSLATION };

			uint chan_slot = tex_pos + 4;
			for (uint c = 0; c < SRT0_N_CHANNEL; c++)
			{
				if (tx->code >> no_bit[group_of[c]] & 1)
					continue;
				if (c == SRT0_SCALE_Y && (tx->code >> SRT0_BIT_SCALE_ISOTROPIC & 1))
					continue;

				u8 *slot = group + chan_slot;
				if (tx->channel[c].is_fixed)
					srt_wf (slot, tx->channel[c].value);
				else
					srt_w32 (slot, track_start + track_at[track_idx++] - chan_slot);
				chan_slot += 4;
			}
			tex_pos = chan_slot;
		}
	}

	FREE (entry_rel);
	FREE (entry_tex_rel);
	FREE (track_at);
	FREE (names);
	FREE (name_off);

	//--- write file

	File_t F;
	enumError err = CreateFileOpt (&F, true, fname, testmode, srt->fname);
	if (err <= ERR_WARNING && F.f)
	{
		SetFileAttrib (&F.fatt, &srt->fatt, 0);
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
//
//   #SRT0
//   version     = 4|5
//   name        = "<string>"
//   orig-path   = "<string>"      (omitted when empty)
//   n-frames    = <uint>
//   loop        = 0|1
//   matrix-mode = <uint>
//
//   material "<name>"
//     layer tex|ind <index> code 0x<hex>
//       <channel> fixed <float>
//       <channel> track <frame-scale>
//         <frame> <value> <tangent>
//
// where <channel> is one of scale-x, scale-y, rot, trans-x, trans-y.

static ccp srt_channel_name[SRT0_N_CHANNEL]
	= { "scale-x", "scale-y", "rot", "trans-x", "trans-y" };

///////////////////////////////////////////////////////////////////////////////

static void PrintQuotedSRT0 (FILE *f, ccp s)
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

static ccp ScanQuotedSRT0 (ccp src, ccp *result)
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

enumError SaveTextSRT0 (srt0_t *srt, ccp fname, bool set_time)
{
	DASSERT (srt);
	DASSERT (fname);

	File_t F;
	enumError err = CreateFileOpt (&F, true, fname, testmode, srt->fname);
	if (err > ERR_WARNING || !F.f)
	{
		ResetFile (&F, 0);
		return err;
	}
	SetFileAttrib (&F.fatt, &srt->fatt, 0);

	fprintf (F.f, "#SRT0\n");
	fprintf (F.f, "# Wiimms SZS Tools Plus -- SRT0 texture SRT animation\n");
	if (srt->fname)
		fprintf (F.f, "# decoded from %s\n", srt->fname);
	fprintf (F.f, "\n");
	fprintf (F.f, "version     = %u\n", srt->version);
	fprintf (F.f, "name        = ");
	PrintQuotedSRT0 (F.f, srt->name ? srt->name : "");
	fprintf (F.f, "\n");
	if (srt->orig_path && *srt->orig_path)
	{
		fprintf (F.f, "orig-path   = ");
		PrintQuotedSRT0 (F.f, srt->orig_path);
		fprintf (F.f, "\n");
	}
	fprintf (F.f, "n-frames    = %u\n", srt->n_frames);
	fprintf (F.f, "loop        = %u\n", srt->loop ? 1 : 0);
	fprintf (F.f, "matrix-mode = %u\n", srt->matrix_mode);
	fprintf (F.f, "n-entries   = %u\n\n", srt->n_entry);

	for (uint i = 0; i < srt->n_entry; i++)
	{
		const srt0_entry_t *e = srt->entry + i;
		fprintf (F.f, "material ");
		PrintQuotedSRT0 (F.f, e->name);
		fprintf (F.f, "\n");

		for (uint t = 0; t < e->n_texture; t++)
		{
			const srt0_texture_t *tx = e->texture + t;
			fprintf (F.f, "  layer %s %u code 0x%08x\n", tx->indirect ? "ind" : "tex",
				tx->layer, tx->code);

			for (uint c = 0; c < SRT0_N_CHANNEL; c++)
			{
				const srt0_channel_t *ch = tx->channel + c;
				if (ch->is_fixed)
					fprintf (F.f, "    %-7s fixed %.9g\n", srt_channel_name[c], ch->value);
				else
				{
					fprintf (F.f, "    %-7s track %.9g\n", srt_channel_name[c],
						ch->track.frame_scale);
					for (uint k = 0; k < ch->track.n_key; k++)
						fprintf (F.f, "      %.9g %.9g %.9g\n", ch->track.key[k].frame,
							ch->track.key[k].value, ch->track.key[k].tangent);
				}
			}
		}
		fprintf (F.f, "\n");
	}

	ResetFile (&F, set_time ? opt_preserve : 0);
	return ERR_OK;
}

///////////////////////////////////////////////////////////////////////////////

enumError ScanTextSRT0 (srt0_t *srt, bool init_srt, ccp src_fname)
{
	DASSERT (srt);
	DASSERT (src_fname);

	FILE *f = fopen (src_fname, "r");
	if (!f)
		return ERROR0 (ERR_CANT_OPEN, "Can't open SRT0 text file: %s\n", src_fname);

	if (init_srt)
		InitializeSRT0 (srt);
	else
		ResetSRT0 (srt);
	srt->fname = STRDUP (src_fname);

	char line[4096];
	bool first = true;
	srt0_entry_t *cur_entry = 0;
	srt0_texture_t *cur_tex = 0;
	banim_track_t *cur_track = 0;

	while (fgets (line, sizeof (line), f))
	{
		ccp s = line;
		while (*s == ' ' || *s == '\t')
			s++;

		if (first)
		{
			first = false;
			if (strncmp (s, "#SRT0", 5))
			{
				fclose (f);
				ResetSRT0 (srt);
				return ERROR0 (ERR_WRONG_FILE_TYPE, "Not an SRT0 text file: %s\n",
					src_fname);
			}
			continue;
		}

		if (!*s || *s == '\r' || *s == '\n' || *s == '#')
			continue;

		uint uval;
		if (sscanf (s, "version = %u", &uval) == 1)
			srt->version = uval;
		else if (sscanf (s, "n-frames = %u", &uval) == 1)
			srt->n_frames = uval;
		else if (sscanf (s, "loop = %u", &uval) == 1)
			srt->loop = uval != 0;
		else if (sscanf (s, "matrix-mode = %u", &uval) == 1)
			srt->matrix_mode = uval;
		else if (sscanf (s, "n-entries = %u", &uval) == 1)
			; // informational
		else if (!strncmp (s, "name", 4) && strchr (s, '='))
		{
			FreeString (srt->name);
			ScanQuotedSRT0 (strchr (s, '=') + 1, &srt->name);
		}
		else if (!strncmp (s, "orig-path", 9) && strchr (s, '='))
		{
			FreeString (srt->orig_path);
			ScanQuotedSRT0 (strchr (s, '=') + 1, &srt->orig_path);
		}
		else if (!strncmp (s, "material", 8))
		{
			ccp mname;
			if (!ScanQuotedSRT0 (s + 8, &mname))
				continue;
			cur_entry = AppendEntrySRT0 (srt, mname);
			FreeString (mname);
			cur_tex = 0;
			cur_track = 0;
		}
		else if (!strncmp (s, "layer", 5) && cur_entry)
		{
			char kind[16];
			uint layer = 0;
			u32 code = 0;
			if (sscanf (s + 5, "%15s %u", kind, &layer) != 2)
				continue;
			ccp c = strstr (s, "code");
			if (c)
				sscanf (c + 4, " 0x%x", &code);

			cur_tex = AppendTextureSRT0 (cur_entry, !strcmp (kind, "ind"), layer);
			cur_tex->code = code;
			cur_track = 0;
		}
		else if (cur_tex)
		{
			char cname[32], kind[16];
			double v;
			if (sscanf (s, "%31s %15s", cname, kind) == 2 && !strcmp (kind, "fixed"))
			{
				for (uint c = 0; c < SRT0_N_CHANNEL; c++)
					if (!strcmp (cname, srt_channel_name[c]))
					{
						ccp p = strstr (s, "fixed");
						if (p && sscanf (p + 5, "%lf", &v) == 1)
						{
							cur_tex->channel[c].is_fixed = true;
							cur_tex->channel[c].value = (float)v;
						}
						break;
					}
				cur_track = 0;
			}
			else if (sscanf (s, "%31s %15s", cname, kind) == 2 && !strcmp (kind, "track"))
			{
				cur_track = 0;
				for (uint c = 0; c < SRT0_N_CHANNEL; c++)
					if (!strcmp (cname, srt_channel_name[c]))
					{
						srt0_channel_t *ch = cur_tex->channel + c;
						ch->is_fixed = false;
						ResetTrackBANIM (&ch->track);
						ch->track.format = BANIM_I12;

						ccp p = strstr (s, "track");
						double fs = 0;
						if (p && sscanf (p + 5, "%lf", &fs) == 1)
							ch->track.frame_scale = (float)fs;
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

	if (!srt->version)
		srt->version = SRT0_DEFAULT_VERSION;

	return ERR_OK;
}
