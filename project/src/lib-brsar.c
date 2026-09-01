#include "lib-brsar.h"
#include "lib-sequence.h"
#include <assert.h>
#include <dirent.h>
#include <sys/stat.h>

// -----------------------------------------------------------------------------
// A tiny growable byte buffer, local to this file.

typedef struct membuf_t
{
	u8 *data;
	size_t size;
	size_t capacity;
} membuf_t;

static void mb_init (membuf_t *mb)
{
	mb->data = 0;
	mb->size = 0;
	mb->capacity = 0;
}

static void mb_reserve (membuf_t *mb, size_t need)
{
	if (mb->size + need <= mb->capacity)
		return;
	size_t new_cap = mb->capacity ? mb->capacity * 2 : 0x1000;
	while (new_cap < mb->size + need)
		new_cap *= 2;
	mb->data = REALLOC (mb->data, new_cap);
	mb->capacity = new_cap;
}

static size_t mb_append (membuf_t *mb, const void *src, size_t len)
{
	mb_reserve (mb, len);
	size_t offs = mb->size;
	if (len)
		memcpy (mb->data + offs, src, len);
	mb->size += len;
	return offs;
}

static size_t mb_append_u32 (membuf_t *mb, u32 val)
{
	u8 be[4] = { (u8)(val >> 24), (u8)(val >> 16), (u8)(val >> 8), (u8)val };
	return mb_append (mb, be, 4);
}

static size_t mb_append_u16 (membuf_t *mb, u16 val)
{
	u8 be[2] = { (u8)(val >> 8), (u8)val };
	return mb_append (mb, be, 2);
}

static void mb_put_u32 (membuf_t *mb, size_t offs, u32 val)
{
	assert (offs + 4 <= mb->size);
	mb->data[offs + 0] = (u8)(val >> 24);
	mb->data[offs + 1] = (u8)(val >> 16);
	mb->data[offs + 2] = (u8)(val >> 8);
	mb->data[offs + 3] = (u8)val;
}

static void mb_put_u16 (membuf_t *mb, size_t offs, u16 val)
{
	assert (offs + 2 <= mb->size);
	mb->data[offs + 0] = (u8)(val >> 8);
	mb->data[offs + 1] = (u8)val;
}

static size_t mb_append_u16e (membuf_t *mb, u16 val, bool le)
{
	u8 b[2];
	if (le)
	{
		b[0] = (u8)val;
		b[1] = (u8)(val >> 8);
	}
	else
	{
		b[0] = (u8)(val >> 8);
		b[1] = (u8)val;
	}
	return mb_append (mb, b, 2);
}

static size_t mb_append_u32e (membuf_t *mb, u32 val, bool le)
{
	u8 b[4];
	if (le)
	{
		b[0] = (u8)val;
		b[1] = (u8)(val >> 8);
		b[2] = (u8)(val >> 16);
		b[3] = (u8)(val >> 24);
	}
	else
	{
		b[0] = (u8)(val >> 24);
		b[1] = (u8)(val >> 16);
		b[2] = (u8)(val >> 8);
		b[3] = (u8)val;
	}
	return mb_append (mb, b, 4);
}

static void mb_put_u32e (membuf_t *mb, size_t offs, u32 val, bool le)
{
	assert (offs + 4 <= mb->size);
	if (le)
	{
		mb->data[offs] = (u8)val;
		mb->data[offs + 1] = (u8)(val >> 8);
		mb->data[offs + 2] = (u8)(val >> 16);
		mb->data[offs + 3] = (u8)(val >> 24);
	}
	else
	{
		mb->data[offs] = (u8)(val >> 24);
		mb->data[offs + 1] = (u8)(val >> 16);
		mb->data[offs + 2] = (u8)(val >> 8);
		mb->data[offs + 3] = (u8)val;
	}
}

static void mb_align (membuf_t *mb, size_t align)
{
	size_t pad = (align - (mb->size % align)) % align;
	if (pad)
	{
		u8 zero[32] = { 0 };
		while (pad)
		{
			size_t n = pad < sizeof (zero) ? pad : sizeof (zero);
			mb_append (mb, zero, n);
			pad -= n;
		}
	}
}

static void mb_free (membuf_t *mb)
{
	if (mb->data)
		FREE (mb->data);
	mb->data = 0;
	mb->size = mb->capacity = 0;
}

static u32 rd_u32 (const u8 *p)
{
	return (u32)p[0] << 24 | (u32)p[1] << 16 | (u32)p[2] << 8 | p[3];
}
static u16 rd_u16 (const u8 *p)
{
	return (u16)((u16)p[0] << 8 | p[1]);
}
static u32 rd_u32e (const u8 *p, bool le)
{
	return le ? ((u32)p[3] << 24 | (u32)p[2] << 16 | (u32)p[1] << 8 | p[0]) : rd_u32 (p);
}
static u16 rd_u16e (const u8 *p, bool le)
{
	return le ? (u16)((u16)p[1] << 8 | p[0]) : rd_u16 (p);
}

static void mb_put_u16e (membuf_t *mb, size_t offs, u16 val, bool le)
{
	assert (offs + 2 <= mb->size);
	if (le) { mb->data[offs] = val; mb->data[offs+1] = val >> 8; }
	else { mb->data[offs] = val >> 8; mb->data[offs+1] = val; }
}

// -----------------------------------------------------------------------------
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

static bool has_suffix (ccp name, ccp suffix)
{
	size_t nl = strlen (name), sl = strlen (suffix);
	return nl >= sl && !strcasecmp (name + nl - sl, suffix);
}

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

static char *strip_ext_dup (ccp name)
{
	ccp dot = strrchr (name, '.');
	size_t len = dot ? (size_t)(dot - name) : strlen (name);
	char *out = MALLOC (len + 1);
	memcpy (out, name, len);
	out[len] = 0;
	return out;
}

// Sniff an asset's own type from its 4-byte container magic (RSEQ/RBNK/
// RWAR/RWSD all start with their tag, per the same convention this file's
// writer follows for embedded RSEQ containers).
static ccp sniff_extension (const u8 *data, size_t size)
{
	if (size >= 4)
	{
		if (!memcmp (data, "RSEQ", 4))
			return ".rseq";
		if (!memcmp (data, "RBNK", 4))
			return ".rbnk";
		if (!memcmp (data, "RWAR", 4))
			return ".rwar";
		if (!memcmp (data, "RWSD", 4))
			return ".rwsd";
	}
	return ".bin";
}

// -----------------------------------------------------------------------------
// Shared SYMB/INFO/FILE content builder. Produces three self-contained,
// tag+size-prefixed, 0x20-aligned blocks; the caller (envelope writer)
// places them at container-specific absolute offsets and patches
// group.data.offset (recorded here as an offset relative to the INFO
// block's *content* base, i.e. 8 bytes past its own tag+size).

static void BuildBrsarContent (const brsar_asset_t *assets, uint n_assets, membuf_t *symb_block,
	membuf_t *info_block, membuf_t *file_block, size_t *group_data_offs_rel)
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
	mb_append_u32 (&info, (u32)file_content.size); // +0x14 data.size (final, no patch needed)
	mb_append_u32 (&info, 0); // +0x18 waveData.offset (unused: assets share the data region)
	mb_append_u32 (&info, 0); // +0x1C waveData.size
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
		mb_append_u32 (&info, 0); // waveData.offset
		mb_append_u32 (&info, 0); // waveData.size
	}

	// ---- sound entries (SEQ only) ----
	uint seq_idx = 0;
	for (uint i = 0; i < n_assets; i++)
	{
		if (assets[i].type != BRSAR_ASSET_RSEQ)
			continue;

		mb_put_u32 (&info, sound_item_offs_base + seq_idx * 8, (u32)info.size);
		seq_idx++;

		mb_append_u32 (&info, name_id[i]); // +0x00 stringID
		mb_append_u32 (&info, i); // +0x04 fileID
		mb_append_u32 (&info, 0); // +0x08 player
		mb_append_u32 (&info, 0); // +0x0C param3DRef
		mb_append_u32 (&info, 0); // +0x10
		u8 tail[8] = { 127, 64, (u8)1 /*Sound::Type::SEQ*/, 0, 0, 0, 0, 0 };
		mb_append (&info, tail,
			8); // +0x14 volume, +0x15 prio, +0x16 type, +0x17 remoteFilter, then pad to +0x1C
		size_t seq_info_ref_slot = info.size - 4; // last 4 bytes of 'tail' land at +0x1C
		assert (seq_info_ref_slot % 4 == 0);

		size_t seq_info_offs = info.size;
		mb_put_u32 (&info, seq_info_ref_slot, (u32)seq_info_offs);
		mb_append_u32 (&info,
			0); // dataOffset: start-of-track offset within the RSEQ bytecode (0 = from the top)
		mb_append_u32 (&info, assets[i].bank_id); // bankID
		mb_append_u32 (&info, 0xFFFF); // allocTrack: all tracks
	}

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
	FREE (name_id);
	FREE (asset_data_offs);
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

	size_t file_content_abs = file_offs + 8; // past FILE's own tag+size
	mb_put_u32 (out, info_offs + group_data_offs_rel, (u32)file_content_abs);
}

// -----------------------------------------------------------------------------
// FSAR/CSAR envelope: EXTRAPOLATED, see lib-brsar.h. Section-table layout
// modeled directly on the verified BFSTM/BCSTM one (brstm_write_fstm() in
// mobipeg): a fixed 0x40-byte header, sections flagged 0x4000 (SYMB),
// 0x4001 (INFO), 0x4002 (FILE), big-endian for FSAR / little-endian by
// default for CSAR. No independent reader confirms this for the archive
// format specifically.

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

	u16 flags[3] = { 0x4000, 0x4001, 0x4002 };
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
	size_t file_content_abs = file_offs + 8;
	mb_put_u32 (out, info_offs + group_data_offs_rel, (u32)file_content_abs);
}

// -----------------------------------------------------------------------------

enumError PackBRSAR (u8 **out_data, size_t *out_size, const brsar_asset_t *assets, uint n_assets,
	brsar_variant_t variant)
{
	if (!assets || !n_assets)
		return ERROR0 (ERR_INVALID_DATA, "PackBRSAR: no assets given\n");

	membuf_t symb_block, info_block, file_block;
	mb_init (&symb_block);
	mb_init (&info_block);
	mb_init (&file_block);
	size_t group_data_offs_rel = 0;

	BuildBrsarContent (
		assets, n_assets, &symb_block, &info_block, &file_block, &group_data_offs_rel);

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

enumError PackBRSARDir (u8 **out_data, size_t *out_size, ccp input_dir, brsar_variant_t variant)
{
	DIR *dir = opendir (input_dir);
	if (!dir)
		return ERROR0 (ERR_CANT_OPEN, "PackBRSARDir: can't open directory '%s'\n", input_dir);

	brsar_asset_t *assets = 0;
	uint n_assets = 0, cap_assets = 0;
	char **owned_names = 0;
	u8 **owned_data = 0;

	struct dirent *ent;
	enumError err = ERR_OK;
	while (!err && (ent = readdir (dir)) != 0)
	{
		if (ent->d_name[0] == '.')
			continue;

		brsar_asset_type_t type = classify_asset (ent->d_name);
		if (type == (brsar_asset_type_t)-1)
			continue;

		char path[PATH_MAX];
		snprintf (path, sizeof (path), "%s/%s", input_dir, ent->d_name);

		struct stat st;
		if (stat (path, &st) != 0 || !S_ISREG (st.st_mode))
			continue;

		u8 *raw = 0;
		size_t raw_size = 0;
		err = LoadFileAlloc (path, 0, 0, &raw, &raw_size, 0, 0, 0, false);
		if (err)
			break;

		u8 *asset_data = raw;
		size_t asset_size = raw_size;

		if (type == BRSAR_ASSET_RSEQ && has_suffix (ent->d_name, ".txt"))
		{
			u8 *bin = 0;
			size_t bin_size = 0;
			err = AssembleSequence (&bin, &bin_size, (const char *)raw, SEQ_FMT_RSEQ);
			FREE (raw);
			if (err)
				break;
			asset_data = bin;
			asset_size = bin_size;
		}

		if (n_assets == cap_assets)
		{
			cap_assets = cap_assets ? cap_assets * 2 : 16;
			assets = REALLOC (assets, cap_assets * sizeof (brsar_asset_t));
			owned_names = REALLOC (owned_names, cap_assets * sizeof (char *));
			owned_data = REALLOC (owned_data, cap_assets * sizeof (u8 *));
		}

		owned_names[n_assets] = strip_ext_dup (ent->d_name);
		owned_data[n_assets] = asset_data;

		brsar_asset_t *a = &assets[n_assets];
		a->name = owned_names[n_assets];
		a->type = type;
		a->data = asset_data;
		a->size = asset_size;
		a->bank_id = 0;
		n_assets++;
	}
	closedir (dir);

	if (!err && !n_assets)
		err = ERROR0 (ERR_INVALID_DATA,
			"PackBRSARDir: no RSEQ/RBNK/RWAR/RWSD assets found in '%s'\n", input_dir);

	if (!err)
		err = PackBRSAR (out_data, out_size, assets, n_assets, variant);

	for (uint i = 0; i < n_assets; i++)
	{
		FREE (owned_names[i]);
		FREE (owned_data[i]);
	}
	if (owned_names)
		FREE (owned_names);
	if (owned_data)
		FREE (owned_data);
	if (assets)
		FREE (assets);

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
	u32 count;
	const u8 *table;
} symb_reader_t;

static void symb_read (
	symb_reader_t *sr, const u8 *block) // block = SYMB content base (past tag+size)
{
	sr->base = block;
	u32 str_table_offs = rd_u32 (block);
	sr->table = block + str_table_offs;
	sr->count = rd_u32 (sr->table);
}

static ccp symb_name (symb_reader_t *sr, u32 idx)
{
	if (idx == 0xFFFFFFFF || idx >= sr->count)
		return 0;
	u32 str_offs = rd_u32 (sr->table + 4 + idx * 4);
	return (ccp)(sr->base + str_offs);
}

// 'file_base'/'file_size' are the whole archive buffer -- group.data.offset
// (per the writer) is an absolute offset into it, not into the FILE block.
static enumError UnpackBrsarContent (
	const u8 *symb, const u8 *info, const u8 *file_base, size_t file_size, ccp out_dir)
{
	symb_reader_t sr;
	symb_read (&sr, symb);

	u32 sound_tab_offs = rd_u32 (info + 0x04);
	u32 sound_count = rd_u32 (info + sound_tab_offs);
	u32 bank_tab_offs = rd_u32 (info + 0x0C);
	u32 bank_count = rd_u32 (info + bank_tab_offs);
	u32 file_tab_offs = rd_u32 (info + 0x1C);
	u32 file_count = rd_u32 (info + file_tab_offs);
	u32 group_tab_offs = rd_u32 (info + 0x24);
	u32 group_count = rd_u32 (info + group_tab_offs);
	if (group_count < 1)
		return ERROR0 (ERR_INVALID_DATA, "UnpackBRSAR: no groups in archive\n");

	// name maps: fileID -> name, for the two asset kinds that have one
	ccp *file_name = MALLOC (file_count * sizeof (ccp));
	memset (file_name, 0, file_count * sizeof (ccp));

	for (u32 i = 0; i < sound_count; i++)
	{
		u32 entry_offs = rd_u32 (info + sound_tab_offs + 8 + i * 8);
		const u8 *sound = info + entry_offs;
		u32 str_id = rd_u32 (sound);
		u32 fid = rd_u32 (sound + 4);
		if (fid < file_count)
			file_name[fid] = symb_name (&sr, str_id);
	}
	for (u32 i = 0; i < bank_count; i++)
	{
		u32 entry_offs = rd_u32 (info + bank_tab_offs + 8 + i * 8);
		const u8 *bank = info + entry_offs;
		u32 str_id = rd_u32 (bank);
		u32 fid = rd_u32 (bank + 4);
		if (fid < file_count)
			file_name[fid] = symb_name (&sr, str_id);
	}
	// PackBRSAR's marked file-entry extension supplies names for RWSD/RWAR,
	// which otherwise have no name-bearing INFO table. Unknown retail file
	// entry fields are ignored unless the explicit marker is present.
	for (u32 fid = 0; fid < file_count; fid++)
	{
		u32 entry_offs = rd_u32 (info + file_tab_offs + 8 + fid * 8);
		const u8 *entry = info + entry_offs;
		if (!file_name[fid] && rd_u32 (entry + 4) == 0x574e414d)
			file_name[fid] = symb_name (&sr, rd_u32 (entry));
	}

	// group 0 (the only group PackBRSAR ever produces; a real multi-group
	// archive would need per-file groupID lookup via the file/position
	// tables, not attempted here)
	u32 group_entry_offs = rd_u32 (info + group_tab_offs + 8);
	const u8 *group = info + group_entry_offs;
	u32 group_data_offs = rd_u32 (group + 0x10); // absolute, per the writer
	u32 item_tab_offs = rd_u32 (group + 0x24);
	u32 item_count = rd_u32 (info + item_tab_offs);

	struct stat st;
	if (stat (out_dir, &st) != 0)
		mkdir (out_dir, 0755);

	uint extracted = 0;
	for (u32 i = 0; i < item_count; i++)
	{
		u32 item_entry_offs = rd_u32 (info + item_tab_offs + 8 + i * 8);
		const u8 *item = info + item_entry_offs;
		u32 fid = rd_u32 (item + 0x00);
		u32 data_offs = rd_u32 (item + 0x04);
		u32 data_size = rd_u32 (item + 0x08);

		if ((size_t)group_data_offs + data_offs + data_size > file_size)
			continue;
		const u8 *bytes = file_base + group_data_offs + data_offs;

		ccp name = fid < file_count ? file_name[fid] : 0;
		char path[PATH_MAX];
		if (name)
			snprintf (
				path, sizeof (path), "%s/%s%s", out_dir, name, sniff_extension (bytes, data_size));
		else
			snprintf (path, sizeof (path), "%s/file_%03u%s", out_dir, fid,
				sniff_extension (bytes, data_size));

		File_t F;
		enumError ferr = CreateFileOpt (&F, true, path, false, out_dir);
		if (F.f && fwrite (bytes, 1, data_size, F.f) != data_size)
			ferr = FILEERROR1 (
				&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", data_size, path);
		ResetFile (&F, 0);
		if (!ferr)
			extracted++;
	}

	FREE (file_name);
	return extracted ? ERR_OK : ERROR0 (ERR_INVALID_DATA, "UnpackBRSAR: no assets extracted\n");
}

enumError UnpackBRSAR (const u8 *data, size_t size, ccp out_dir)
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
			out_dir); // file_content base = whole buffer; group.data.offset is absolute
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
			if (flag == 0x4000)
				symb_offs = offs;
			else if (flag == 0x4001)
				info_offs = offs;
			else if (flag == 0x4002)
				file_offs = offs;
		}
		if (!symb_offs || memcmp (data + symb_offs, "SYMB", 4))
			return ERROR0 (ERR_INVALID_DATA, "UnpackBRSAR: missing SYMB section\n");
		if (!info_offs || memcmp (data + info_offs, "INFO", 4))
			return ERROR0 (ERR_INVALID_DATA, "UnpackBRSAR: missing INFO section\n");
		if (!file_offs || memcmp (data + file_offs, "FILE", 4))
			return ERROR0 (ERR_INVALID_DATA, "UnpackBRSAR: missing FILE section\n");

		return UnpackBrsarContent (data + symb_offs + 8, data + info_offs + 8, data, size, out_dir);
	}

	return ERROR0 (ERR_INVALID_DATA, "UnpackBRSAR: not an RSAR/FSAR/CSAR file\n");
}

// Nintendo DS SDAT. Block-relative SYMB/INFO offsets and absolute FAT
// offsets match the conventions consumed by the vendored NDSScanner.cpp.
typedef struct sdat_item_t { char *name; u8 *data; size_t size; int kind; } sdat_item_t;

static int cmp_sdat_item (const void *a, const void *b)
{
	const sdat_item_t *aa=a, *bb=b;
	return aa->kind != bb->kind ? aa->kind-bb->kind : strcmp(aa->name,bb->name);
}

static void sdat_block_header (membuf_t *b, ccp magic)
{ mb_append(b,magic,4); mb_append_u32e(b,0,true); }

static size_t sdat_name_list (membuf_t *b, sdat_item_t *it, uint n, int kind)
{
	size_t list=b->size; uint count=0,j=0;
	for(uint i=0;i<n;i++) if(it[i].kind==kind) count++;
	mb_append_u32e(b,count,true); size_t ptrs=b->size;
	for(uint i=0;i<count;i++) mb_append_u32e(b,0,true);
	for(uint i=0;i<n;i++) if(it[i].kind==kind) {
		mb_put_u32e(b,ptrs+4*j++,b->size,true);
		mb_append(b,it[i].name,strlen(it[i].name)+1);
	}
	mb_align(b,4); return list;
}

static size_t sdat_info_list (membuf_t *b, sdat_item_t *it, uint n, int kind,
	uint n_bank, uint n_wave)
{
	size_t list=b->size; uint count=0,j=0,file_id=0;
	for(uint i=0;i<n;i++) if(it[i].kind==kind) count++;
	mb_append_u32e(b,count,true); size_t ptrs=b->size;
	for(uint i=0;i<count;i++) mb_append_u32e(b,0,true);
	for(uint i=0;i<n;i++) {
		if(it[i].kind==kind) {
			mb_put_u32e(b,ptrs+4*j++,b->size,true);
			mb_append_u16e(b,file_id,true); mb_append_u16e(b,0,true);
			if(kind==0) { mb_append_u16e(b,n_bank?0:0xffff,true); const u8 v[6]={127,64,64,0,0,0}; mb_append(b,v,6); }
			else if(kind==1) for(uint w=0;w<4;w++) mb_append_u16e(b,w<n_wave?w:0xffff,true);
		}
		file_id++;
	}
	return list;
}

enumError PackSDATDir (u8 **out_data, size_t *out_size, ccp input_dir)
{
	if(!out_data||!out_size)
		return ERR_INVALID_DATA;
	*out_data=0; *out_size=0;
	DIR *dir=opendir(input_dir);
	if(!dir) return ERROR0(ERR_CANT_OPEN,"PackSDATDir: can't open directory '%s'\n",input_dir);
	sdat_item_t *it=0; uint n=0,cap=0; enumError err=ERR_OK; struct dirent *ent;
	while(!err&&(ent=readdir(dir))) {
		if(ent->d_name[0]=='.') continue;
		int kind=has_suffix(ent->d_name,".sseq")?0:has_suffix(ent->d_name,".sbnk")?1
			:has_suffix(ent->d_name,".swar")?2:has_suffix(ent->d_name,".txt")?0:-1;
		if(kind<0) continue;
		char path[PATH_MAX]; snprintf(path,sizeof(path),"%s/%s",input_dir,ent->d_name);
		struct stat st; if(stat(path,&st)||!S_ISREG(st.st_mode)) continue;
		u8 *raw=0; size_t raw_size=0;
		err=LoadFileAlloc(path,0,0,&raw,&raw_size,0,0,0,false); if(err) break;
		if(has_suffix(ent->d_name,".txt")) {
			u8 *bin=0; size_t bin_size=0; err=AssembleSequence(&bin,&bin_size,(ccp)raw,SEQ_FMT_SSEQ);
			FREE(raw); raw=bin; raw_size=bin_size; if(err) break;
		}
		if(n==cap) { cap=cap?cap*2:16; it=REALLOC(it,cap*sizeof(*it)); }
		it[n]=(sdat_item_t){strip_ext_dup(ent->d_name),raw,raw_size,kind}; n++;
	}
	closedir(dir);
	if(!err&&!n) err=ERROR0(ERR_INVALID_DATA,"PackSDATDir: no SSEQ/SBNK/SWAR assets found in '%s'\n",input_dir);
	if(!err) {
		qsort(it,n,sizeof(*it),cmp_sdat_item); uint counts[3]={0};
		for(uint i=0;i<n;i++) counts[it[i].kind]++;
		membuf_t symb,info,fat,file,out; mb_init(&symb);mb_init(&info);mb_init(&fat);mb_init(&file);mb_init(&out);
		sdat_block_header(&symb,"SYMB"); size_t sp[8]; for(int i=0;i<8;i++) sp[i]=mb_append_u32e(&symb,0,true);
		for(int k=0;k<8;k++) { int kind=k==0?0:k==2?1:k==3?2:99; size_t p=sdat_name_list(&symb,it,n,kind); mb_put_u32e(&symb,sp[k],p,true); }
		mb_put_u32e(&symb,4,symb.size,true);
		sdat_block_header(&info,"INFO"); size_t ip[8]; for(int i=0;i<8;i++) ip[i]=mb_append_u32e(&info,0,true);
		for(int k=0;k<8;k++) { int kind=k==0?0:k==2?1:k==3?2:99; size_t p=sdat_info_list(&info,it,n,kind,counts[1],counts[2]); mb_put_u32e(&info,ip[k],p,true); }
		mb_align(&info,4); mb_put_u32e(&info,4,info.size,true);
		sdat_block_header(&fat,"FAT "); mb_append_u32e(&fat,n,true); size_t fe=fat.size;
		for(uint i=0;i<n;i++){mb_append_u32e(&fat,0,true);mb_append_u32e(&fat,it[i].size,true);mb_append_u32e(&fat,0,true);mb_append_u32e(&fat,0,true);} mb_put_u32e(&fat,4,fat.size,true);
		sdat_block_header(&file,"FILE");mb_append_u32e(&file,n,true);mb_append_u32e(&file,0,true);mb_align(&file,32);
		u8 zero[0x40]={0};mb_append(&out,zero,sizeof(zero));membuf_t*blocks[4]={&symb,&info,&fat,&file};size_t offs[4];
		for(int b=0;b<4;b++){mb_align(&out,32);offs[b]=out.size;mb_append(&out,blocks[b]->data,blocks[b]->size);}
		for(uint i=0;i<n;i++){mb_align(&out,32);mb_put_u32e(&out,offs[2]+fe+16*i,out.size,true);mb_append(&out,it[i].data,it[i].size);}
		mb_put_u32e(&out,offs[3]+4,out.size-offs[3],true);memcpy(out.data,"SDAT",4);
		mb_put_u16e(&out,4,0xfeff,true);mb_put_u16e(&out,6,0x0100,true);mb_put_u32e(&out,8,out.size,true);mb_put_u16e(&out,12,0x40,true);mb_put_u16e(&out,14,4,true);
		for(int b=0;b<4;b++){mb_put_u32e(&out,0x10+8*b,offs[b],true);mb_put_u32e(&out,0x14+8*b,b==3?out.size-offs[b]:blocks[b]->size,true);}
		*out_data=out.data;*out_size=out.size;mb_free(&symb);mb_free(&info);mb_free(&fat);mb_free(&file);
	}
	for(uint i=0;i<n;i++){FREE(it[i].name);FREE(it[i].data);}if(it)FREE(it);return err;
}

enumError UnpackSDAT (const u8 *data, size_t size, ccp out_dir)
{
	if(size<0x40||memcmp(data,"SDAT",4)) return ERROR0(ERR_INVALID_DATA,"UnpackSDAT: invalid header\n");
	u32 fat=rd_u32e(data+0x20,true);if(fat+12>size||memcmp(data+fat,"FAT ",4))return ERROR0(ERR_INVALID_DATA,"UnpackSDAT: missing FAT block\n");
	u32 n=rd_u32e(data+fat+8,true);if(n>(size-fat-12)/16)return ERROR0(ERR_INVALID_DATA,"UnpackSDAT: invalid FAT count\n");
	if(mkdir(out_dir,0755)&&errno!=EEXIST)return ERROR0(ERR_CANT_CREATE,"UnpackSDAT: can't create '%s'\n",out_dir);
	char **names=CALLOC(n,sizeof(*names));
	u32 symb=rd_u32e(data+0x10,true),info=rd_u32e(data+0x18,true);
	if(symb+0x28<=size&&info+0x28<=size&&!memcmp(data+symb,"SYMB",4)&&!memcmp(data+info,"INFO",4)) {
		const int cats[3]={0,2,3};
		for(int c=0;c<3;c++) {
			u32 sl=rd_u32e(data+symb+8+4*cats[c],true), il=rd_u32e(data+info+8+4*cats[c],true);
			if(sl>size-symb-4||il>size-info-4) continue;
			u32 sc=rd_u32e(data+symb+sl,true),ic=rd_u32e(data+info+il,true),count=sc<ic?sc:ic;
			if(count>(size-symb-sl-4)/4||count>(size-info-il-4)/4) continue;
			for(u32 j=0;j<count;j++) {
				u32 so=rd_u32e(data+symb+sl+4+4*j,true),io=rd_u32e(data+info+il+4+4*j,true);
				if(!so||!io||so>=size-symb||io+2>size-info) continue;
				u16 id=rd_u16e(data+info+io,true); if(id>=n) continue;
				size_t max=size-(symb+so),len=strnlen((ccp)data+symb+so,max); if(len==max) continue;
				char *name=MALLOC(len+1); memcpy(name,data+symb+so,len); name[len]=0;
				for(char*p=name;*p;p++) if(*p=='/'||*p=='\\'||(unsigned char)*p<32) *p='_';
				names[id]=name;
			}
		}
	}
	for(u32 i=0;i<n;i++){
		u32 off=rd_u32e(data+fat+12+16*i,true),len=rd_u32e(data+fat+16+16*i,true);if(off>size||len>size-off)return ERROR0(ERR_INVALID_DATA,"UnpackSDAT: invalid file %u bounds\n",i);
		ccp ext=len>=4&&!memcmp(data+off,"SSEQ",4)?"sseq":len>=4&&!memcmp(data+off,"SBNK",4)?"sbnk":len>=4&&!memcmp(data+off,"SWAR",4)?"swar":"bin";
		char fallback[32];snprintf(fallback,sizeof(fallback),"file_%04u",i);char path[PATH_MAX];snprintf(path,sizeof(path),"%s/%s.%s",out_dir,names[i]?names[i]:fallback,ext);FILE*f=fopen(path,"wb");if(!f)return ERROR0(ERR_CANT_CREATE,"UnpackSDAT: can't create '%s'\n",path);bool bad=fwrite(data+off,1,len,f)!=len;fclose(f);if(bad)return ERR_WRITE_FAILED;
	}
	for(u32 i=0;i<n;i++)
		if(names[i]) FREE(names[i]);
	FREE(names);
	return ERR_OK;
}
