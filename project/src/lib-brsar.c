#include "lib-brsar.h"
#include "lib-sequence.h"
#include "lib-sound-archive-util.h"
#include <assert.h>
#include <dirent.h>
#include <sys/stat.h>

// SYMB (string table) builder.
//
// Layout traced from vgmtrans RSAR::ParseSymbBlock():
//   content+0x00: u32 offset (relative to content base) of the string-offset table
//   at that offset: u32 count, then 'count' * u32 string offsets (relative to
//   content base), each pointing to a NUL-terminated string later in the block.
// The name-lookup tree that real RSAR SYMB blocks also contain is never read
// by the vendored parser, so it is intentionally omitted here.

typedef struct symb_builder_t
{
	membuf_t content;
	ccp *names;
	u32 n_names;
	u32 cap_names;
} symb_builder_t;

static void symb_init (symb_builder_t *sb)
{
	mb_init (&sb->content);
	sb->names = 0;
	sb->n_names = sb->cap_names = 0;
}

// Returns the string-table index for 'name', adding it if not already present.
static u32 symb_intern (symb_builder_t *sb, ccp name)
{
	for (u32 i = 0; i < sb->n_names; i++)
		if (!strcmp (sb->names[i], name))
			return i;

	if (sb->n_names == sb->cap_names)
	{
		sb->cap_names = sb->cap_names ? sb->cap_names * 2 : 16;
		sb->names = REALLOC (sb->names, sb->cap_names * sizeof (ccp));
	}
	sb->names[sb->n_names] = name;
	return sb->n_names++;
}

// Finalizes the SYMB block (magic + size + content) into 'out'.
static void symb_finish (symb_builder_t *sb, membuf_t *out)
{
	membuf_t c;
	mb_init (&c);

	// content+0x00: placeholder for the string-offset-table offset
	mb_append_u32 (&c, 0);

	size_t str_table_offs = c.size; // == 0x04, right after the header word
	mb_put_u32 (&c, 0x00, (u32)str_table_offs);

	mb_append_u32 (&c, sb->n_names);
	size_t offs_slot0 = c.size;
	for (u32 i = 0; i < sb->n_names; i++)
		mb_append_u32 (&c, 0); // filled in below once string bytes are placed

	for (u32 i = 0; i < sb->n_names; i++)
	{
		size_t str_offs = mb_append (&c, sb->names[i], strlen (sb->names[i]) + 1);
		mb_put_u32 (&c, offs_slot0 + i * 4, (u32)str_offs);
	}

	mb_align (&c, 4);

	mb_append (out, "SYMB", 4);
	mb_append_u32 (out, (u32)(8 + c.size));
	mb_append (out, c.data, c.size);
	mb_free (&c);

	if (sb->names)
		FREE (sb->names);
}

// -----------------------------------------------------------------------------
// Generic "reference table" writer used by INFO: a table is
//   u32 count; u32 pad/type(=0); { u32 item_offset; u32 pad(=0); } * count
// with item_offset relative to the INFO content base. This is the layout
// vgmtrans reads for the sound/bank/file/group root tables (stride 8,
// first word of each 8-byte entry is the offset -- see ReadSoundTable etc).

static size_t reftab_begin (membuf_t *info, u32 expected_n)
{
	size_t base = mb_append_u32 (info, expected_n);
	mb_append_u32 (info, 0); // pad/type word, unused by the reader
	return base;
}

// Appends one entry to a reference table whose entries have already been
// pre-sized; 'item_offs' is the INFO-relative offset of the referenced item.
static void reftab_entry (membuf_t *info, u32 item_offs)
{
	mb_append_u32 (info, item_offs);
	mb_append_u32 (info, 0);
}

// -----------------------------------------------------------------------------
// Directory scanning / sniffing helpers.

static brsar_asset_type_t classify_asset (ccp name)
{
	if (has_suffix (name, ".rseq") || has_suffix (name, ".brseq") || has_suffix (name, ".txt"))
		return BRSAR_ASSET_RSEQ;
	if (has_suffix (name, ".rbnk") || has_suffix (name, ".brbnk"))
		return BRSAR_ASSET_RBNK;
	if (has_suffix (name, ".rwar") || has_suffix (name, ".brwar"))
		return BRSAR_ASSET_RWAR;
	if (has_suffix (name, ".rwsd") || has_suffix (name, ".brwsd"))
		return BRSAR_ASSET_RWSD;
	return (brsar_asset_type_t)-1;
}

// Sniff an asset's own type from its 4-byte container magic (RSEQ/RBNK/
// RWAR/RWSD all start with their tag, per the same convention this file's
// writer follows for embedded RSEQ containers).
static ccp sniff_extension (const u8 *data, size_t size)
{
	if (size >= 4)
	{
		if (!memcmp (data, "RSEQ", 4))
			return ".brseq";
		if (!memcmp (data, "RBNK", 4))
			return ".brbnk";
		if (!memcmp (data, "RWAR", 4))
			return ".brwar";
		if (!memcmp (data, "RWSD", 4))
			return ".brwsd";
	}
	return ".bin";
}

// -----------------------------------------------------------------------------
// Shared SYMB/INFO/FILE content builder. Produces three self-contained,
// tag+size-prefixed, 0x20-aligned blocks; the caller (envelope writer)
// places them at container-specific absolute offsets and patches
// group.data.offset (recorded here as an offset relative to the INFO
// block's *content* base, i.e. 8 bytes past its own tag+size).

static u32 find_asset (const brsar_asset_t *assets, uint n_assets, ccp name, brsar_asset_type_t type)
{
	for (uint i = 0; i < n_assets; i++)
		if (assets[i].type == type && name && !strcmp (assets[i].name, name))
			return i;
	return 0xFFFFFFFF;
}

static void BuildBrsarContent (const brsar_asset_t *assets, uint n_assets,
	const brsar_sound_t *sounds, uint n_sounds, membuf_t *symb_block, membuf_t *info_block,
	membuf_t *file_block, size_t *group_data_offs_rel)
{
	symb_builder_t symb;
	symb_init (&symb);

	u32 *name_id = MALLOC (n_assets * sizeof (u32));
	for (uint i = 0; i < n_assets; i++)
		name_id[i] = symb_intern (&symb, assets[i].name);

	// --- FILE block content: the concatenated raw asset bytes ---------------
	membuf_t file_content;
	mb_init (&file_content);

	u32 *asset_data_offs = MALLOC (n_assets * sizeof (u32));
	for (uint i = 0; i < n_assets; i++)
	{
		mb_align (&file_content, 0x20);
		asset_data_offs[i] = (u32)file_content.size;
		mb_append (&file_content, assets[i].data, assets[i].size);
	}
	mb_align (&file_content, 0x20);

	// Wave data (RWAR paired with an RBNK/RWSD) lives in the group's separate
	// wave region, which follows the header region in the FILE block.
	membuf_t wave_content;
	mb_init (&wave_content);
	u32 *asset_wave_offs = MALLOC (n_assets * sizeof (u32));
	for (uint i = 0; i < n_assets; i++)
	{
		asset_wave_offs[i] = 0;
		if (!assets[i].wave_size)
			continue;
		mb_align (&wave_content, 0x20);
		asset_wave_offs[i] = (u32)wave_content.size;
		mb_append (&wave_content, assets[i].wave_data, assets[i].wave_size);
	}
	mb_align (&wave_content, 0x20);
	const size_t wave_region_offs = file_content.size;
	mb_append (&file_content, wave_content.data, wave_content.size);

	// --- INFO block content ---------------------------------------------------
	// Root reference table order, per RSAR::Parse():
	//   +0x04 sound table ref, +0x0C bank table ref,
	//   +0x1C file table ref,  +0x24 group table ref.
	// (+0x00, +0x08, +0x14 belong to fields the vendored reader never reads
	// -- player table / 3D-sound table roots -- and are left zero.)
	membuf_t info;
	mb_init (&info);
	for (int w = 0; w < 10; w++)
		mb_append_u32 (&info, 0); // reserves +0x00..+0x24
	size_t sound_ref_slot = 0x04;
	size_t bank_ref_slot = 0x0C;
	size_t file_ref_slot = 0x1C;
	size_t group_ref_slot = 0x24;

	// ---- sound table (RSEQ assets) ----
	uint n_seq = 0, n_bnk = 0;
	for (uint i = 0; i < n_assets; i++)
	{
		if (assets[i].type == BRSAR_ASSET_RSEQ)
			n_seq++;
		else if (assets[i].type == BRSAR_ASSET_RBNK)
			n_bnk++;
	}
	if (n_sounds)
		n_seq = n_sounds;

	size_t sound_tab_offs = info.size;
	mb_put_u32 (&info, sound_ref_slot, (u32)sound_tab_offs);
	reftab_begin (&info, n_seq);
	size_t sound_item_offs_base = info.size;
	for (uint i = 0; i < n_seq; i++)
		reftab_entry (&info, 0);

	// ---- bank table (RBNK assets: {stringID, fileID}, per ReadBankTable) ----
	size_t bank_tab_offs = info.size;
	mb_put_u32 (&info, bank_ref_slot, (u32)bank_tab_offs);
	reftab_begin (&info, n_bnk);
	size_t bank_item_offs_base = info.size;
	for (uint i = 0; i < n_bnk; i++)
		reftab_entry (&info, 0);

	uint bnk_idx = 0;
	for (uint i = 0; i < n_assets; i++)
	{
		if (assets[i].type != BRSAR_ASSET_RBNK)
			continue;
		mb_put_u32 (&info, bank_item_offs_base + bnk_idx * 8, (u32)info.size);
		bnk_idx++;
		mb_append_u32 (&info, name_id[i]); // +0x00 stringID
		mb_append_u32 (&info, i); // +0x04 fileID
	}

	// ---- file table: one entry per asset, each pointing at a 1-entry
	// file-position table -> {groupID=0, index=asset index} ----
	size_t file_tab_offs = info.size;
	mb_put_u32 (&info, file_ref_slot, (u32)file_tab_offs);
	reftab_begin (&info, n_assets);
	size_t file_item_offs_base = info.size;
	for (uint i = 0; i < n_assets; i++)
		reftab_entry (&info, 0);

	for (uint i = 0; i < n_assets; i++)
	{
		mb_put_u32 (&info, (size_t)(file_item_offs_base + i * 8), (u32)info.size);

		size_t file_base = info.size;
		// Retail readers ignore these first two file-entry words. Preserve a
		// name association for asset kinds (RWSD/RWAR) that have no sound or
		// bank table entry; the marker prevents interpreting retail metadata
		// as this private extension during unpack.
		mb_append_u32 (&info, name_id[i]);
		mb_append_u32 (&info, 0x574e414d); // "WNAM"
		for (int w = 0; w < 4; w++)
			mb_append_u32 (&info, 0); // +0x08..+0x14
		size_t pos_tab_offs_slot = info.size; // this becomes +0x18
		mb_append_u32 (&info, 0); // filePosTableOffs, patched below
		assert (pos_tab_offs_slot - file_base == 0x18);

		size_t pos_tab_offs = info.size;
		mb_put_u32 (&info, pos_tab_offs_slot, (u32)pos_tab_offs);
		mb_append_u32 (&info, 1); // count = 1
		mb_append_u32 (&info, 0); // +0x04 pad/type, unused by reader
		size_t pos_entry_offs = info.size;
		mb_append_u32 (
			&info, (u32)(pos_entry_offs + 4)); // +0x08 slot is read directly as filePosBase

		mb_append_u32 (&info, 0); // groupID = 0 (single group)
		mb_append_u32 (&info, i); // index into the one group's item list
	}

	// ---- group table: a single group containing every asset ----
	size_t group_tab_offs = info.size;
	mb_put_u32 (&info, group_ref_slot, (u32)group_tab_offs);
	reftab_begin (&info, 1);
	size_t group_item_offs_base = info.size;
	reftab_entry (&info, 0);

	mb_put_u32 (&info, group_item_offs_base, (u32)info.size);
	size_t group_base = info.size;
	mb_append_u32 (&info, 0xFFFFFFFF); // +0x00 stringID (unnamed group)
	mb_append_u32 (&info, 0); // +0x04
	mb_append_u32 (&info, 0); // +0x08
	mb_append_u32 (&info, 0); // +0x0C
	size_t group_data_offs_slot = info.size;
	mb_append_u32 (&info, 0); // +0x10 data.offset, patched by envelope writer
	mb_append_u32 (&info, (u32)wave_region_offs); // +0x14 data.size (final, no patch needed)
	mb_append_u32 (&info, (u32)wave_region_offs); // +0x18 waveData.offset, rebased by envelope
	mb_append_u32 (&info, (u32)wave_content.size); // +0x1C waveData.size
	while (info.size - group_base < 0x24)
		mb_append_u32 (&info, 0);
	size_t item_tab_offs_slot = info.size;
	mb_append_u32 (&info, 0);
	assert (item_tab_offs_slot - group_base == 0x24);

	size_t item_tab_offs = info.size;
	mb_put_u32 (&info, item_tab_offs_slot, (u32)item_tab_offs);
	reftab_begin (&info, n_assets);
	size_t item_offs_base = info.size;
	for (uint i = 0; i < n_assets; i++)
		reftab_entry (&info, 0);

	for (uint i = 0; i < n_assets; i++)
	{
		mb_put_u32 (&info, item_offs_base + i * 8, (u32)info.size);
		mb_append_u32 (&info, i); // fileID
		mb_append_u32 (&info, asset_data_offs[i]); // data.offset (relative to group data region)
		mb_append_u32 (&info, (u32)assets[i].size); // data.size
		mb_append_u32 (&info, asset_wave_offs[i]); // waveData.offset (relative to wave region)
		mb_append_u32 (&info, (u32)assets[i].wave_size); // waveData.size
	}

	// ---- sound entries (SEQ only) ----
	// Bank ids index the bank table, which is in RBNK asset order.
	u32 *bank_index = MALLOC ((n_assets + 1) * sizeof (u32));
	{
		u32 bi = 0;
		for (uint i = 0; i < n_assets; i++)
			bank_index[i] = assets[i].type == BRSAR_ASSET_RBNK ? bi++ : 0xFFFFFFFF;
	}

	for (uint k = 0; k < n_seq; k++)
	{
		uint i = 0;
		u32 name_sid, data_offset, bank_id, alloc_track;
		if (n_sounds)
		{
			u32 ai = find_asset (assets, n_assets, sounds[k].seq_name, BRSAR_ASSET_RSEQ);
			if (ai == 0xFFFFFFFF)
				ai = 0;
			i = ai;
			name_sid = symb_intern (&symb, sounds[k].name);
			data_offset = sounds[k].data_offset;
			u32 bai = find_asset (assets, n_assets, sounds[k].bank_name, BRSAR_ASSET_RBNK);
			bank_id = bai == 0xFFFFFFFF ? 0 : bank_index[bai];
			alloc_track = sounds[k].alloc_track;
		}
		else
		{
			// k-th RSEQ asset
			uint seen = 0;
			for (i = 0; i < n_assets; i++)
				if (assets[i].type == BRSAR_ASSET_RSEQ && seen++ == k)
					break;
			name_sid = name_id[i];
			data_offset = 0;
			bank_id = assets[i].bank_id;
			alloc_track = 0xFFFF;
		}

		mb_put_u32 (&info, sound_item_offs_base + k * 8, (u32)info.size);

		mb_append_u32 (&info, name_sid); // +0x00 stringID
		mb_append_u32 (&info, i); // +0x04 fileID
		mb_append_u32 (&info, 0); // +0x08 player
		mb_append_u32 (&info, 0); // +0x0C param3DRef
		mb_append_u32 (&info, 0); // +0x10
		u8 tail[8] = { 127, 64, (u8)1 /*Sound::Type::SEQ*/, 0, 0, 0, 0, 0 };
		mb_append (&info, tail,
			8); // +0x14 volume, +0x15 prio, +0x16 type, +0x17 remoteFilter, +0x18 pad
		size_t seq_info_ref_slot = mb_append_u32 (&info, 0); // +0x1C seqInfo offset, patched below

		size_t seq_info_offs = info.size;
		mb_put_u32 (&info, seq_info_ref_slot, (u32)seq_info_offs);
		mb_append_u32 (&info, data_offset); // label offset within the RSEQ DATA block
		mb_append_u32 (&info, bank_id); // bankID
		mb_append_u32 (&info, alloc_track); // allocTrack
	}
	FREE (bank_index);

	mb_align (&info, 4);

	mb_append (info_block, "INFO", 4);
	mb_append_u32 (info_block, (u32)(8 + info.size));
	mb_append (info_block, info.data, info.size);
	mb_align (info_block, 0x20);

	symb_finish (&symb, symb_block);

	mb_append (file_block, "FILE", 4);
	mb_append_u32 (file_block, (u32)(8 + file_content.size));
	mb_append (file_block, file_content.data, file_content.size);
	mb_align (file_block, 0x20);

	*group_data_offs_rel
		= 8 + group_data_offs_slot; // relative to info_block's own start (incl. tag+size)

	mb_free (&info);
	mb_free (&file_content);
	mb_free (&wave_content);
	FREE (asset_wave_offs);
	FREE (name_id);
	FREE (asset_data_offs);
}

// group.data.offset (slot, written as 0) and group.waveData.offset (slot+8,
// written relative to the FILE content) become absolute once the FILE block's
// position is known.
static void patch_group_offsets (membuf_t *out, size_t slot, size_t file_content_abs)
{
	u32 wave_rel = rd_u32 (out->data + slot + 8);
	mb_put_u32 (out, slot, (u32)file_content_abs);
	mb_put_u32 (out, slot + 8, (u32)(file_content_abs + wave_rel));
}

// -----------------------------------------------------------------------------
// RSAR envelope: fixed 3-slot block table (SYMB/INFO/FILE offset+size pairs
// at header+0x10/0x18/0x20), verified against vgmtrans' RSAR::Parse().

static void WriteRsarEnvelope (membuf_t *out, membuf_t *symb_block, membuf_t *info_block,
	membuf_t *file_block, size_t group_data_offs_rel)
{
	mb_append (out, "RSAR", 4);
	mb_append (out, "\xFE\xFF", 2); // byte-order mark
	mb_append (
		out, "\x01\x04", 2); // version 1.4 (matches the "\x01" major byte MatchBytes checks for)
	mb_append_u32 (out, 0); // file size, patched below
	u8 hdrsz_blocks[4] = { 0x00, 0x40, 0x00, 0x03 }; // header size 0x40, block count 3
	mb_append (out, hdrsz_blocks, 4);
	mb_align (out, 0x40);

	size_t symb_offs = out->size;
	mb_append (out, symb_block->data, symb_block->size);
	mb_align (out, 0x20);

	size_t info_offs = out->size;
	mb_append (out, info_block->data, info_block->size);
	mb_align (out, 0x20);

	size_t file_offs = out->size;
	mb_append (out, file_block->data, file_block->size);
	mb_align (out, 0x20);

	mb_put_u32 (out, 0x10, (u32)symb_offs);
	mb_put_u32 (out, 0x14, (u32)symb_block->size);
	mb_put_u32 (out, 0x18, (u32)info_offs);
	mb_put_u32 (out, 0x1C, (u32)info_block->size);
	mb_put_u32 (out, 0x20, (u32)file_offs);
	mb_put_u32 (out, 0x24, (u32)file_block->size);
	mb_put_u32 (out, 0x08, (u32)out->size);

	patch_group_offsets (out, info_offs + group_data_offs_rel, file_offs + 8);
}

// -----------------------------------------------------------------------------
// FSAR/CSAR envelope. Section-table layout modeled on the verified
// BFSTM/BCSTM one (brstm_write_fstm() in mobipeg): a fixed 0x40-byte
// header, big-endian for FSAR / little-endian by default for CSAR.
// Section flags are 0x2000 (STRG), 0x2001 (INFO), 0x2002 (FILE) --
// confirmed against real retail Wii U BFSAR archives (Splatoon,
// content/Sound/DummySound.bfsar); the previously-assumed 0x4000-series
// (borrowed from RSAR's own block-table convention) never matched real
// files. CSAR (3DS) flags are not independently verified but are kept
// symmetric with the confirmed FSAR scheme.

static void WriteFsarEnvelope (membuf_t *out, membuf_t *symb_block, membuf_t *info_block,
	membuf_t *file_block, size_t group_data_offs_rel, bool cstm)
{
	bool le
		= cstm; // CSAR defaults little-endian, mirroring BCSTM's "c->little_endian = variant==CSTM"

	mb_append (out, cstm ? "CSAR" : "FSAR", 4);
	mb_append_u16e (out, 0xFEFF, le);
	mb_append_u16e (out, 0x40, le); // header size
	mb_append_u32e (out, cstm ? 0x00000200 : 0x00030000, le); // version
	size_t file_size_slot = mb_append_u32e (out, 0, le); // file size, patched below
	mb_append_u16e (out, 3, le); // section count
	mb_append_u16e (out, 0, le); // pad

	size_t sect_table = out->size;
	for (int i = 0; i < 3; i++)
	{
		mb_append_u16e (out, 0, le);
		mb_append_u16e (out, 0, le); // flag, pad
		mb_append_u32e (out, 0, le);
		mb_append_u32e (out, 0, le); // offset, size (patched below)
	}
	mb_align (out, 0x40);

	size_t symb_offs = out->size;
	mb_append (out, symb_block->data, symb_block->size);
	mb_align (out, 0x20);

	size_t info_offs = out->size;
	mb_append (out, info_block->data, info_block->size);
	mb_align (out, 0x20);

	size_t file_offs = out->size;
	mb_append (out, file_block->data, file_block->size);
	mb_align (out, 0x20);

	u16 flags[3] = { 0x2000, 0x2001, 0x2002 };
	u32 offs[3] = { (u32)symb_offs, (u32)info_offs, (u32)file_offs };
	u32 sizes[3] = { (u32)symb_block->size, (u32)info_block->size, (u32)file_block->size };
	for (int i = 0; i < 3; i++)
	{
		size_t e = sect_table + i * 12;
		mb_put_u16 (out, e,
			le ? (u16)((flags[i] >> 8) | (flags[i] << 8))
			   : flags[i]); // mb_put_u16 always writes BE; feed it pre-swapped bytes for LE
		mb_put_u32e (out, e + 4, offs[i], le);
		mb_put_u32e (out, e + 8, sizes[i], le);
	}

	mb_put_u32e (out, file_size_slot, (u32)out->size, le);

	// group.data.offset lives inside the SYMB/INFO/FILE content, which (like
	// RSTM's HEAD3 coefficient tables) is treated as a fixed big-endian
	// payload regardless of the envelope's own endianness -- consistent
	// with how the content builder produces it once, shared by all variants.
	patch_group_offsets (out, info_offs + group_data_offs_rel, file_offs + 8);
}

// -----------------------------------------------------------------------------

enumError PackBRSAR (u8 **out_data, size_t *out_size, const brsar_asset_t *assets, uint n_assets,
	brsar_variant_t variant)
{
	return PackBRSAREx (out_data, out_size, assets, n_assets, 0, 0, variant);
}

enumError PackBRSAREx (u8 **out_data, size_t *out_size, const brsar_asset_t *assets, uint n_assets,
	const brsar_sound_t *sounds, uint n_sounds, brsar_variant_t variant)
{
	if (!assets || !n_assets)
		return ERROR0 (ERR_INVALID_DATA, "PackBRSAR: no assets given\n");

	membuf_t symb_block, info_block, file_block;
	mb_init (&symb_block);
	mb_init (&info_block);
	mb_init (&file_block);
	size_t group_data_offs_rel = 0;

	BuildBrsarContent (assets, n_assets, sounds, n_sounds, &symb_block, &info_block, &file_block,
		&group_data_offs_rel);

	membuf_t out;
	mb_init (&out);

	if (variant == BRSAR_VARIANT_RSAR)
		WriteRsarEnvelope (&out, &symb_block, &info_block, &file_block, group_data_offs_rel);
	else
		WriteFsarEnvelope (&out, &symb_block, &info_block, &file_block, group_data_offs_rel,
			variant == BRSAR_VARIANT_CSAR);

	*out_data = out.data;
	*out_size = out.size;

	mb_free (&symb_block);
	mb_free (&info_block);
	mb_free (&file_block);

	return ERR_OK;
}

// -----------------------------------------------------------------------------
// RWAR wave archive: header(0x20) + TABL{count, {kind,offset,size}*} + DATA
// holding the RWAV files. Entry offsets are relative to the DATA block start.

enumError UnpackRWAR (const u8 *data, size_t size, ccp out_dir)
{
	if (size < 0x40 || memcmp (data, "RWAR", 4))
		return ERROR0 (ERR_INVALID_DATA, "UnpackRWAR: not an RWAR\n");
	u32 tabl = rd_u32 (data + 0x10), dat = rd_u32 (data + 0x18);
	if ((size_t)tabl + 12 > size || (size_t)dat + 8 > size || memcmp (data + tabl, "TABL", 4))
		return ERROR0 (ERR_INVALID_DATA, "UnpackRWAR: bad table/data offsets\n");
	u32 count = rd_u32 (data + tabl + 8);
	if ((size_t)count * 12 + tabl + 12 > size)
		return ERROR0 (ERR_INVALID_DATA, "UnpackRWAR: implausible entry count\n");

	struct stat st;
	if (stat (out_dir, &st) != 0)
		mkdir (out_dir, 0755);
	for (u32 i = 0; i < count; i++)
	{
		const u8 *e = data + tabl + 12 + (size_t)i * 12;
		u32 offs = rd_u32 (e + 4), sz = rd_u32 (e + 8);
		if ((size_t)dat + offs + sz > size)
			return ERROR0 (ERR_INVALID_DATA, "UnpackRWAR: entry %u out of bounds\n", i);
		char path[PATH_MAX];
		snprintf (path, sizeof (path), "%s/%05u.brwav", out_dir, i);
		File_t F;
		enumError err = CreateFileOpt (&F, true, path, false, out_dir);
		if (F.f && fwrite (data + dat + offs, 1, sz, F.f) != sz)
			err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", sz, path);
		ResetFile (&F, 0);
		if (err)
			return err;
	}
	return ERR_OK;
}

static int cmp_strp (const void *a, const void *b)
{
	return strcmp (*(char *const *)a, *(char *const *)b);
}

// Sorted list of non-hidden names in 'dir'; caller frees each and the array.
static char **list_dir_sorted (ccp dir, uint *n_out)
{
	*n_out = 0;
	DIR *d = opendir (dir);
	if (!d)
		return 0;
	char **names = 0;
	uint n = 0, cap = 0;
	struct dirent *ent;
	while ((ent = readdir (d)) != 0)
	{
		if (ent->d_name[0] == '.')
			continue;
		if (n == cap)
		{
			cap = cap ? cap * 2 : 64;
			names = REALLOC (names, cap * sizeof (char *));
		}
		names[n++] = STRDUP (ent->d_name);
	}
	closedir (d);
	if (n)
		qsort (names, n, sizeof (char *), cmp_strp);
	*n_out = n;
	return names;
}

enumError PackRWARDir (u8 **out_data, size_t *out_size, ccp in_dir)
{
	uint n = 0;
	char **names = list_dir_sorted (in_dir, &n);
	if (!names)
		return ERROR0 (ERR_CANT_OPEN, "PackRWARDir: can't open '%s'\n", in_dir);

	u8 **blobs = CALLOC (n + 1, sizeof (u8 *));
	size_t *sizes = CALLOC (n + 1, sizeof (size_t));
	uint cnt = 0;
	enumError err = ERR_OK;
	for (uint i = 0; i < n && !err; i++)
	{
		if (!has_suffix (names[i], ".brwav") && !has_suffix (names[i], ".rwav"))
			continue;
		char path[PATH_MAX];
		snprintf (path, sizeof (path), "%s/%s", in_dir, names[i]);
		err = LoadFileAlloc (path, 0, 0, &blobs[cnt], &sizes[cnt], 0, 0, 0, false);
		if (!err)
			cnt++;
	}

	if (!err)
	{
		membuf_t tabl, dat, out;
		mb_init (&tabl);
		mb_init (&dat);
		mb_init (&out);
		mb_append (&tabl, "TABL", 4);
		mb_append_u32 (&tabl, 0);
		mb_append_u32 (&tabl, cnt);
		mb_append (&dat, "DATA", 4);
		mb_append_u32 (&dat, 0);
		mb_align (&dat, 0x20);
		for (uint i = 0; i < cnt; i++)
		{
			mb_align (&dat, 0x20);
			mb_append_u32 (&tabl, 0x01000000);
			mb_append_u32 (&tabl, (u32)dat.size);
			mb_append_u32 (&tabl, (u32)sizes[i]);
			mb_append (&dat, blobs[i], sizes[i]);
		}
		mb_align (&tabl, 0x20);
		mb_align (&dat, 0x20);
		mb_put_u32 (&tabl, 4, (u32)tabl.size);
		mb_put_u32 (&dat, 4, (u32)dat.size);

		mb_append (&out, "RWAR", 4);
		mb_append (&out, "\xFE\xFF\x01\x00", 4);
		mb_append_u32 (&out, (u32)(0x20 + tabl.size + dat.size));
		mb_append (&out, "\x00\x20\x00\x02", 4);
		mb_append_u32 (&out, 0x20);
		mb_append_u32 (&out, (u32)tabl.size);
		mb_append_u32 (&out, (u32)(0x20 + tabl.size));
		mb_append_u32 (&out, (u32)dat.size);
		mb_append (&out, tabl.data, tabl.size);
		mb_append (&out, dat.data, dat.size);
		*out_data = out.data;
		*out_size = out.size;
		mb_free (&tabl);
		mb_free (&dat);
	}

	for (uint i = 0; i < cnt; i++)
		FREE (blobs[i]);
	FREE (blobs);
	FREE (sizes);
	for (uint i = 0; i < n; i++)
		FREE (names[i]);
	FREE (names);
	return err;
}

// -----------------------------------------------------------------------------
// Directory packing.

typedef struct dir_asset_t
{
	char *name; // stem
	brsar_asset_type_t type;
	u8 *data;
	size_t size;
	u8 *wave;
	size_t wave_size;
	bool wave_is_asset; // wave claimed by a bank/wsd (not a standalone RWAR)
} dir_asset_t;

static char *strip_one_ext_dup (ccp name, ccp ext)
{
	size_t len = strlen (name) - strlen (ext);
	char *o = MALLOC (len + 1);
	memcpy (o, name, len);
	o[len] = 0;
	return o;
}

// Parse sounds.tsv (name, seq stem, label offset, bank stem, track mask).
static brsar_sound_t *load_sounds_tsv (ccp input_dir, uint *n_out, char **owned_text)
{
	*n_out = 0;
	*owned_text = 0;
	char path[PATH_MAX];
	snprintf (path, sizeof (path), "%s/sounds.tsv", input_dir);
	u8 *raw = 0;
	size_t raw_size = 0;
	if (access (path, R_OK) || LoadFileAlloc (path, 0, 0, &raw, &raw_size, 0, 0, 0, false))
		return 0;
	char *text = MALLOC (raw_size + 1);
	memcpy (text, raw, raw_size);
	text[raw_size] = 0;
	FREE (raw);

	brsar_sound_t *v = 0;
	uint n = 0, cap = 0;
	char *save = 0;
	for (char *line = strtok_r (text, "\n", &save); line; line = strtok_r (0, "\n", &save))
	{
		size_t ll = strlen (line);
		if (ll && line[ll - 1] == '\r')
			line[ll - 1] = 0;
		if (line[0] == '#' || !line[0])
			continue;
		char *f[5] = { 0 };
		int nf = 0;
		char *p = line;
		while (nf < 5)
		{
			f[nf++] = p;
			char *t = strchr (p, '\t');
			if (!t)
				break;
			*t = 0;
			p = t + 1;
		}
		if (nf < 5)
			continue;
		if (n == cap)
		{
			cap = cap ? cap * 2 : 64;
			v = REALLOC (v, cap * sizeof (*v));
		}
		v[n].name = f[0];
		v[n].seq_name = f[1];
		v[n].data_offset = (u32)strtoul (f[2], 0, 0);
		v[n].bank_name = f[3][0] ? f[3] : 0;
		v[n].alloc_track = (u32)strtoul (f[4], 0, 0);
		n++;
	}
	*n_out = n;
	*owned_text = text;
	return v;
}

enumError PackBRSARDir (u8 **out_data, size_t *out_size, ccp input_dir, brsar_variant_t variant)
{
	uint n_names = 0;
	char **names = list_dir_sorted (input_dir, &n_names);
	if (!names)
		return ERROR0 (ERR_CANT_OPEN, "PackBRSARDir: can't open directory '%s'\n", input_dir);

	dir_asset_t *da = CALLOC (n_names + 1, sizeof (dir_asset_t));
	uint n = 0;
	enumError err = ERR_OK;

	// Pass 1: header assets (sequences, banks, wave sets) and standalone RWARs.
	for (uint i = 0; i < n_names && !err; i++)
	{
		char path[PATH_MAX];
		snprintf (path, sizeof (path), "%s/%s", input_dir, names[i]);
		struct stat st;
		if (stat (path, &st) != 0)
			continue;

		if (S_ISDIR (st.st_mode))
		{
			if (!has_suffix (names[i], ".brwar.d") && !has_suffix (names[i], ".rwar.d"))
				continue;
			err = PackRWARDir (&da[n].data, &da[n].size, path);
			if (err)
				break;
			da[n].name = strip_one_ext_dup (
				names[i], has_suffix (names[i], ".brwar.d") ? ".brwar.d" : ".rwar.d");
			da[n].type = BRSAR_ASSET_RWAR;
			n++;
			continue;
		}
		if (!S_ISREG (st.st_mode))
			continue;
		brsar_asset_type_t type = classify_asset (names[i]);
		if (type == (brsar_asset_type_t)-1)
			continue;

		u8 *raw = 0;
		size_t raw_size = 0;
		err = LoadFileAlloc (path, 0, 0, &raw, &raw_size, 0, 0, 0, false);
		if (err)
			break;
		if (type == BRSAR_ASSET_RSEQ && has_suffix (names[i], ".txt"))
		{
			u8 *bin = 0;
			size_t bin_size = 0;
			err = AssembleSequence (&bin, &bin_size, (const char *)raw, SEQ_FMT_RSEQ);
			FREE (raw);
			if (err)
				break;
			raw = bin;
			raw_size = bin_size;
		}
		da[n].name = strip_ext_dup (names[i]);
		da[n].type = type;
		da[n].data = raw;
		da[n].size = raw_size;
		n++;
	}

	// Pass 2: an RWAR whose stem matches a bank/wave-set is that asset's wave
	// data (group wave region) rather than an archive member of its own.
	for (uint i = 0; i < n && !err; i++)
	{
		if (da[i].type != BRSAR_ASSET_RWAR)
			continue;
		for (uint j = 0; j < n; j++)
		{
			if ((da[j].type == BRSAR_ASSET_RBNK || da[j].type == BRSAR_ASSET_RWSD)
				&& !da[j].wave && !strcmp (da[i].name, da[j].name))
			{
				da[j].wave = da[i].data;
				da[j].wave_size = da[i].size;
				da[i].wave_is_asset = true;
				break;
			}
		}
	}

	brsar_asset_t *assets = CALLOC (n + 1, sizeof (brsar_asset_t));
	uint n_assets = 0;
	for (uint i = 0; i < n; i++)
	{
		if (da[i].wave_is_asset)
			continue;
		brsar_asset_t *a = &assets[n_assets++];
		a->name = da[i].name;
		a->type = da[i].type;
		a->data = da[i].data;
		a->size = da[i].size;
		a->wave_data = da[i].wave;
		a->wave_size = da[i].wave_size;
	}

	if (!err && !n_assets)
		err = ERROR0 (ERR_INVALID_DATA,
			"PackBRSARDir: no RSEQ/RBNK/RWAR/RWSD assets found in '%s'\n", input_dir);

	uint n_sounds = 0;
	char *tsv_text = 0;
	brsar_sound_t *sounds = err ? 0 : load_sounds_tsv (input_dir, &n_sounds, &tsv_text);
	if (!err)
		err = PackBRSAREx (out_data, out_size, assets, n_assets, sounds, n_sounds, variant);

	if (sounds)
		FREE (sounds);
	if (tsv_text)
		FREE (tsv_text);
	FREE (assets);
	for (uint i = 0; i < n; i++)
	{
		FREE (da[i].name);
		FREE (da[i].data);
		if (da[i].wave && !da[i].wave_is_asset)
			; // owned by its RWAR entry
	}
	FREE (da);
	for (uint i = 0; i < n_names; i++)
		FREE (names[i]);
	FREE (names);
	return err;
}

// -----------------------------------------------------------------------------
// UnpackBRSAR(): reverse of PackBRSAR(). Reads back the SYMB string table,
// bank/sound name<->fileID maps, and the file/group tables' data offsets +
// sizes, then dumps every file-table entry's bytes to 'out_dir'. Same
// variant-detection + section-table-vs-fixed-header split as the writer.

typedef struct symb_reader_t
{
	const u8 *base;
	size_t limit;
	u32 count;
	const u8 *table;
	u32 table_offs;
} symb_reader_t;

// Bounds-checked u32 read relative to a [base, base+limit) window -- used
// throughout this file, since a degenerate/minimal retail archive (e.g.
// Splatoon's content/Sound/DummySound.bfsar: real INFO entries whose
// group/item tables carry bogus or zero offsets and huge derived counts)
// can otherwise walk table offsets and counts read straight from file data
// past the mapped buffer and crash (SIGSEGV) rather than failing cleanly.
// Returns 0 for any read that doesn't fit inside the window.
static inline u32 rd_u32_bound (const u8 *base, size_t limit, size_t off)
{
	if (off + 4 > limit || off + 4 < off)
		return 0;
	return rd_u32 (base + off);
}

static void symb_read (
	symb_reader_t *sr, const u8 *block, size_t block_limit) // block = SYMB content base
{
	sr->base = block;
	sr->limit = block_limit;
	u32 str_table_offs = rd_u32_bound (block, block_limit, 0);
	sr->table = block + str_table_offs;
	sr->table_offs = str_table_offs;
	sr->count = str_table_offs <= block_limit
		? rd_u32_bound (block, block_limit, str_table_offs)
		: 0;
}

static ccp symb_name (symb_reader_t *sr, u32 idx)
{
	if (idx == 0xFFFFFFFF || idx >= sr->count)
		return 0;
	size_t entry_off = (size_t)sr->table_offs + 4 + (size_t)idx * 4;
	u32 str_offs = rd_u32_bound (sr->base, sr->limit, entry_off);
	if (str_offs >= sr->limit)
		return 0;
	// The referenced string itself isn't NUL-terminated-checked here (as
	// before); it's trusted to lie within the SYMB block like every other
	// well-formed archive's string table does.
	return (ccp)(sr->base + str_offs);
}

// 'file_base'/'file_size' are the whole archive buffer -- group.data.offset
// (per the writer) is an absolute offset into it, not into the FILE block.
static enumError UnpackBrsarContent (const u8 *symb, const u8 *info, const u8 *file_base,
	size_t file_size, ccp out_dir, bool recursive)
{
	if (info < file_base || (size_t)(info - file_base) > file_size)
		return ERROR0 (ERR_INVALID_DATA, "UnpackBRSAR: INFO block outside archive\n");
	size_t info_limit = file_size - (size_t)(info - file_base);

	if (symb < file_base || (size_t)(symb - file_base) > file_size)
		return ERROR0 (ERR_INVALID_DATA, "UnpackBRSAR: SYMB block outside archive\n");
	size_t symb_limit = file_size - (size_t)(symb - file_base);

	symb_reader_t sr;
	symb_read (&sr, symb, symb_limit);

	u32 sound_tab_offs = rd_u32_bound (info, info_limit, 0x04);
	u32 sound_count = rd_u32_bound (info, info_limit, sound_tab_offs);
	u32 bank_tab_offs = rd_u32_bound (info, info_limit, 0x0C);
	u32 bank_count = rd_u32_bound (info, info_limit, bank_tab_offs);
	u32 file_tab_offs = rd_u32_bound (info, info_limit, 0x1C);
	u32 file_count = rd_u32_bound (info, info_limit, file_tab_offs);
	u32 group_tab_offs = rd_u32_bound (info, info_limit, 0x24);
	u32 group_count = rd_u32_bound (info, info_limit, group_tab_offs);
	if (group_count < 1)
		return ERROR0 (ERR_INVALID_DATA, "UnpackBRSAR: no groups in archive\n");

	// Sanity-cap counts derived from file data before using them to drive
	// MALLOC/loops below: each table entry is at least 8 bytes, so a count
	// that can't possibly fit inside the INFO block is bogus.
	if ((size_t)sound_count * 8 > info_limit || (size_t)bank_count * 8 > info_limit
		|| (size_t)file_count * 8 > info_limit || (size_t)group_count * 8 > info_limit)
		return ERROR0 (ERR_INVALID_DATA, "UnpackBRSAR: implausible table count\n");

	// name maps: fileID -> name, for the two asset kinds that have one
	ccp *file_name = MALLOC (file_count * sizeof (ccp));
	memset (file_name, 0, file_count * sizeof (ccp));

	// Banks first, so a bank-table index (a sound's bankID) resolves to a name.
	ccp *bank_name = CALLOC (bank_count + 1, sizeof (ccp));
	for (u32 i = 0; i < bank_count; i++)
	{
		u32 entry_offs = rd_u32_bound (info, info_limit, bank_tab_offs + 8 + (size_t)i * 8);
		u32 str_id = rd_u32_bound (info, info_limit, entry_offs);
		u32 fid = rd_u32_bound (info, info_limit, entry_offs + 4);
		bank_name[i] = symb_name (&sr, str_id);
		if (fid < file_count && !file_name[fid])
			file_name[fid] = bank_name[i];
	}
	// A RSEQ file holds many labeled songs; each SEQ sound points at one label.
	// The file takes the name of the first sound that references it.
	typedef struct sound_rec_t
	{
		ccp name;
		u32 fid, data_offset, bank_id, alloc_track;
	} sound_rec_t;
	sound_rec_t *snd = CALLOC (sound_count + 1, sizeof (sound_rec_t));
	u32 n_snd = 0;
	for (u32 i = 0; i < sound_count; i++)
	{
		u32 entry_offs = rd_u32_bound (info, info_limit, sound_tab_offs + 8 + (size_t)i * 8);
		u32 str_id = rd_u32_bound (info, info_limit, entry_offs);
		u32 fid = rd_u32_bound (info, info_limit, entry_offs + 4);
		ccp nm = symb_name (&sr, str_id);
		if (fid < file_count && !file_name[fid])
			file_name[fid] = nm;
		if (nm && fid < file_count && entry_offs + 0x1C + 4 <= info_limit
			&& info[entry_offs + 0x16] == 1)
		{
			u32 seq_offs = rd_u32_bound (info, info_limit, entry_offs + 0x1C);
			snd[n_snd].name = nm;
			snd[n_snd].fid = fid;
			snd[n_snd].data_offset = rd_u32_bound (info, info_limit, seq_offs);
			snd[n_snd].bank_id = rd_u32_bound (info, info_limit, seq_offs + 4);
			snd[n_snd].alloc_track = rd_u32_bound (info, info_limit, seq_offs + 8);
			n_snd++;
		}
	}
	// PackBRSAR's marked file-entry extension supplies names for RWSD/RWAR,
	// which otherwise have no name-bearing INFO table. Unknown retail file
	// entry fields are ignored unless the explicit marker is present.
	for (u32 fid = 0; fid < file_count; fid++)
	{
		u32 entry_offs = rd_u32_bound (info, info_limit, file_tab_offs + 8 + (size_t)fid * 8);
		if (!file_name[fid] && rd_u32_bound (info, info_limit, entry_offs + 4) == 0x574e414d)
			file_name[fid] = symb_name (&sr, rd_u32_bound (info, info_limit, entry_offs));
	}

	struct stat st;
	if (stat (out_dir, &st) != 0)
		mkdir (out_dir, 0755);

	bool *extracted_fid = CALLOC (file_count + 1, sizeof (bool));
	uint extracted = 0;

	for (u32 g = 0; g < group_count; g++)
	{
		u32 group_entry_offs = rd_u32_bound (info, info_limit, group_tab_offs + 8 + (size_t)g * 8);
		u32 group_data_offs = rd_u32_bound (info, info_limit, group_entry_offs + 0x10);
		u32 item_tab_offs = rd_u32_bound (info, info_limit, group_entry_offs + 0x24);
		u32 item_count = rd_u32_bound (info, info_limit, item_tab_offs);
		if ((size_t)item_count * 8 > info_limit)
			continue; // implausible -- skip this group rather than walk OOB

		for (u32 i = 0; i < item_count; i++)
		{
			u32 item_entry_offs
				= rd_u32_bound (info, info_limit, item_tab_offs + 8 + (size_t)i * 8);
			u32 fid = rd_u32_bound (info, info_limit, item_entry_offs + 0x00);
			u32 data_offs = rd_u32_bound (info, info_limit, item_entry_offs + 0x04);
			u32 data_size = rd_u32_bound (info, info_limit, item_entry_offs + 0x08);

			if (fid < file_count && extracted_fid[fid])
				continue;

			// A zero-size item is an empty/reserved file slot with no real
			// payload (seen in retail archives, e.g. MotionPlusMovieSound.brsar
			// fid 3) -- there's nothing to preserve, so don't fabricate a
			// placeholder file for it; PackBRSAR wouldn't recreate one anyway.
			if (!data_size)
				continue;

			if ((size_t)group_data_offs + data_offs + data_size > file_size)
				continue;
			const u8 *bytes = file_base + group_data_offs + data_offs;

			ccp name = fid < file_count ? file_name[fid] : 0;
			ccp ext = sniff_extension (bytes, data_size);
			char stem[PATH_MAX];
			if (name)
				snprintf (stem, sizeof (stem), "%s", name);
			else
				snprintf (stem, sizeof (stem), "file_%03u", fid);
			if (has_suffix (stem, ext))
				stem[strlen (stem) - strlen (ext)] = 0;
			char path[PATH_MAX];
			snprintf (path, sizeof (path), "%s/%s%s", out_dir, stem, ext);

			File_t F;
			enumError ferr = CreateFileOpt (&F, true, path, false, out_dir);
			if (F.f && fwrite (bytes, 1, data_size, F.f) != data_size)
				ferr = FILEERROR1 (
					&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", data_size, path);
			ResetFile (&F, 0);
			if (!ferr)
			{
				extracted++;
				if (fid < file_count)
					extracted_fid[fid] = true;

				// The group's wave region slice for this item (RWAR).
				u32 wave_base = rd_u32_bound (info, info_limit, group_entry_offs + 0x18);
				u32 wave_offs = rd_u32_bound (info, info_limit, item_entry_offs + 0x0C);
				u32 wave_size = rd_u32_bound (info, info_limit, item_entry_offs + 0x10);
				if (wave_size >= 4 && (size_t)wave_base + wave_offs + wave_size <= file_size)
				{
					const u8 *wb = file_base + wave_base + wave_offs;
					if (!memcmp (wb, "RWAR", 4))
					{
						enumError werr = ERR_OK;
						if (recursive)
						{
							char dpath[PATH_MAX];
							snprintf (dpath, sizeof (dpath), "%s/%s.brwar.d", out_dir, stem);
							werr = UnpackRWAR (wb, wave_size, dpath);
							if (!werr)
								continue;
						}
						snprintf (path, sizeof (path), "%s/%s.brwar", out_dir, stem);
						File_t W;
						werr = CreateFileOpt (&W, true, path, false, out_dir);
						if (W.f && fwrite (wb, 1, wave_size, W.f) != wave_size)
							werr = FILEERROR1 (&W, ERR_WRITE_FAILED,
								"Writing %u bytes failed: %s\n", wave_size, path);
						ResetFile (&W, 0);
						if (werr)
							return werr;
					}
				}
			}
		}
	}

	// sounds.tsv: sound name, sequence file, label offset, bank, track mask.
	if (n_snd)
	{
		char path[PATH_MAX];
		snprintf (path, sizeof (path), "%s/sounds.tsv", out_dir);
		FILE *tf = fopen (path, "wb");
		if (tf)
		{
			for (u32 i = 0; i < n_snd; i++)
			{
				ccp seq = file_name[snd[i].fid];
				char seqbuf[64];
				if (!seq)
				{
					snprintf (seqbuf, sizeof (seqbuf), "file_%03u", snd[i].fid);
					seq = seqbuf;
				}
				char *seq_stem = strip_ext_dup (seq);
				ccp bank = snd[i].bank_id < bank_count ? bank_name[snd[i].bank_id] : 0;
				char *bank_stem = bank ? strip_ext_dup (bank) : 0;
				fprintf (tf, "%s\t%s\t%u\t%s\t%u\n", snd[i].name, seq_stem, snd[i].data_offset,
					bank_stem ? bank_stem : "", snd[i].alloc_track);
				FREE (seq_stem);
				if (bank_stem)
					FREE (bank_stem);
			}
			fclose (tf);
		}
	}
	FREE (snd);
	FREE (bank_name);
	FREE (extracted_fid);
	FREE (file_name);
	return extracted ? ERR_OK : ERROR0 (ERR_INVALID_DATA, "UnpackBRSAR: no assets extracted\n");
}

enumError UnpackBRSAR (const u8 *data, size_t size, ccp out_dir)
{
	return UnpackBRSAREx (data, size, out_dir, false);
}

enumError UnpackBRSAREx (const u8 *data, size_t size, ccp out_dir, bool recursive)
{
	if (size < 0x40)
		return ERROR0 (ERR_INVALID_DATA, "UnpackBRSAR: file too small\n");

	if (!memcmp (data, "RSAR", 4))
	{
		u32 symb_offs = rd_u32 (data + 0x10);
		u32 info_offs = rd_u32 (data + 0x18);
		u32 file_offs = rd_u32 (data + 0x20);
		if ((size_t)symb_offs + 8 > size || memcmp (data + symb_offs, "SYMB", 4))
			return ERROR0 (ERR_INVALID_DATA, "UnpackBRSAR: missing SYMB block\n");
		if ((size_t)info_offs + 8 > size || memcmp (data + info_offs, "INFO", 4))
			return ERROR0 (ERR_INVALID_DATA, "UnpackBRSAR: missing INFO block\n");
		if ((size_t)file_offs + 8 > size || memcmp (data + file_offs, "FILE", 4))
			return ERROR0 (ERR_INVALID_DATA, "UnpackBRSAR: missing FILE block\n");

		return UnpackBrsarContent (data + symb_offs + 8, data + info_offs + 8, data, size,
			out_dir, recursive); // file_content base = whole buffer; group.data.offset is absolute
	}

	if (!memcmp (data, "FSAR", 4) || !memcmp (data, "CSAR", 4))
	{
		bool le = data[0] == 'C';
		u16 sections = rd_u16e (data + 16, le);
		u32 symb_offs = 0, info_offs = 0, file_offs = 0;
		u32 pos = 20;
		for (int i = 0; i < sections && pos + 12 <= size; i++, pos += 12)
		{
			u16 flag = rd_u16e (data + pos, le);
			u32 offs = rd_u32e (data + pos + 4, le);
			// Real retail FSAR section flags are 0x2000 (STRG), 0x2001 (INFO),
			// 0x2002 (FILE); the 0x4000-series was this codebase's own,
			// unverified encoder convention (borrowed from RSAR) before real
			// retail samples confirmed otherwise -- still accepted here so
			// previously-encoded/round-tripped archives keep reading back.
			if (flag == 0x2000 || flag == 0x4000)
				symb_offs = offs;
			else if (flag == 0x2001 || flag == 0x4001)
				info_offs = offs;
			else if (flag == 0x2002 || flag == 0x4002)
				file_offs = offs;
		}
		// Real retail FSAR string-table blocks are tagged "STRG", not RSAR's
		// "SYMB" -- confirmed against real Wii U BFSAR archives (Splatoon).
		// Accept either tag; the block content layout read by
		// UnpackBrsarContent() is unaffected by which 4-byte tag precedes it.
		if (!symb_offs
			|| (memcmp (data + symb_offs, "SYMB", 4) && memcmp (data + symb_offs, "STRG", 4)))
			return ERROR0 (ERR_INVALID_DATA, "UnpackBRSAR: missing SYMB/STRG section\n");
		if (!info_offs || memcmp (data + info_offs, "INFO", 4))
			return ERROR0 (ERR_INVALID_DATA, "UnpackBRSAR: missing INFO section\n");
		if (!file_offs || memcmp (data + file_offs, "FILE", 4))
			return ERROR0 (ERR_INVALID_DATA, "UnpackBRSAR: missing FILE section\n");

		return UnpackBrsarContent (data + symb_offs + 8, data + info_offs + 8, data, size, out_dir,
			recursive);
	}

	return ERROR0 (ERR_INVALID_DATA, "UnpackBRSAR: not an RSAR/FSAR/CSAR file\n");
}
