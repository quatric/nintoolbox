// SPDX-License-Identifier: GPL-2.0+
#include "lib-bea.h"
#include "lib-archive-util.h"
#include "lib-zstd.h"
#include <string.h>
#include <stdio.h>
#include <dirent.h>
#include <sys/stat.h>

//-----------------------------------------------------------------------------

static inline void wr_le64 (u8 *p, u64 v)
{
	wr_le32 (p, (u32)v);
	wr_le32 (p + 4, (u32)(v >> 32));
}

//-----------------------------------------------------------------------------
///////////////			IsBEA / ScanBEA			///////////////
//-----------------------------------------------------------------------------

ccp BEA_SETUP_FILE = "bea-setup.txt";

bool IsBEA (const u8 *data, uint size)
{
	// "SCNE" has no companion checksum like IsNDSBanner()'s CRC16, but its
	// header is fully self-describing and cross-checkable: every offset it
	// stores (dictionary, file-info array, name string, each ASST pointer)
	// must land in-bounds and every ASST block must start with its own
	// "ASST" signature. That is concrete structural proof, not a guess from
	// a few bytes -- see the RFL-RES fix this codec's detection mirrors.
	if (!data || size < 44 || memcmp (data, "SCNE", 4))
		return false;

	const u32 version = rd_le32 (data + 8);
	const u8 vmaj2 = (u8)(version >> 16);
	const u32 file_count = rd_le32 (data + 32);
	if (file_count > 0x10000)
		return false;

	const uint header_tail = vmaj2 >= 5 ? 40 + 8 + 8 : 40 + 8;
	if (header_tail + 8 > size)
		return false;
	const u64 file_info_offset = rd_le64 (data + (vmaj2 >= 5 ? 40 + 8 : 40));
	if (file_info_offset + (u64)file_count * 8 > size)
		return false;

	for (u32 i = 0; i < file_count; i++)
	{
		const u64 asst_off = rd_le64 (data + file_info_offset + i * 8);
		if (asst_off + 4 > size || memcmp (data + asst_off, "ASST", 4))
			return false;
	}
	return true;
}

// Read a string stored as an 8-byte absolute offset to a u16 length-prefixed
// UTF-8 blob (see FileLoader.LoadString()). Returns a fresh STRDUP'd copy,
// or NULL if the on-disk offset is 0 ("no string").
static ccp load_bea_string (const u8 *data, uint size, u64 off)
{
	if (!off || off + 2 > size)
		return 0;
	const u16 len = rd_le16 (data + off);
	if (off + 2 + len > size)
		return 0;
	char *str = MALLOC (len + 1);
	memcpy (str, data + off + 2, len);
	str[len] = 0;
	return str;
}

static enumError scan_bea_file (bea_file_t *f, const u8 *data, uint size, u64 asst_off, u8 vmaj2)
{
	memset (f, 0, sizeof (*f));
	if (asst_off + 16 > size || memcmp (data + asst_off, "ASST", 4))
		return ERR_INVALID_DATA;

	u64 p = asst_off + 16; // skip "ASST" + block header (offset u32, size u64)
	if (p + 16 > size)
		return ERR_INVALID_DATA;
	f->unk1 = rd_le16 (data + p);
	p += 2;
	f->unk2 = rd_le16 (data + p);
	p += 2;
	const u32 file_size = rd_le32 (data + p);
	p += 4;
	const u32 uncompressed_size = rd_le32 (data + p);
	p += 4;

	if (vmaj2 >= 5)
	{
		if (p + 8 > size)
			return ERR_INVALID_DATA;
		memcpy (f->file_type, data + p, 8);
		f->file_type[8] = 0;
		p += 8;
	}

	if (p + 4 > size)
		return ERR_INVALID_DATA;
	f->unknown3 = rd_le32 (data + p);
	p += 4;

	if (vmaj2 >= 6)
	{
		if (p + 16 > size)
			return ERR_INVALID_DATA;
		f->file_id1 = rd_le64 (data + p);
		p += 8;
		f->file_id2 = rd_le64 (data + p);
		p += 8;
	}

	if (p + 16 > size)
		return ERR_INVALID_DATA;
	const u64 file_offset = rd_le64 (data + p);
	p += 8;
	const u64 name_str_off = p; // LoadString() reads its own 8-byte offset here
	ccp name = load_bea_string (data, size, rd_le64 (data + p));
	p += 8;
	(void)name_str_off;
	if (!name || file_offset + file_size > size)
	{
		FREE ((void *)name);
		return ERR_INVALID_DATA;
	}

	f->name = name;
	f->size = uncompressed_size;
	f->is_compressed = uncompressed_size != file_size;

	if (f->is_compressed)
	{
		u8 *dec = 0;
		uint dec_size = 0;
		const enumError derr
			= DecodeZSTD (&dec, &dec_size, data + file_offset, file_size);
		if (derr || !dec || dec_size != uncompressed_size)
		{
			FREE (dec);
			FREE ((void *)f->name);
			return ERR_INVALID_DATA;
		}
		f->data = dec;
	}
	else
	{
		f->data = MALLOC (file_size ? file_size : 1);
		memcpy (f->data, data + file_offset, file_size);
	}
	return ERR_OK;
}

enumError ScanBEA (bea_archive_t *bea, const u8 *data, uint size)
{
	memset (bea, 0, sizeof (*bea));
	if (!IsBEA (data, size))
		return ERR_INVALID_DATA;

	const u32 version = rd_le32 (data + 8);
	bea->version_major = (u8)(version >> 24);
	bea->version_major2 = (u8)(version >> 16);
	bea->version_minor = (u8)(version >> 8);
	bea->version_minor2 = (u8)version;
	bea->byte_order = rd_le16 (data + 12);
	bea->alignment = data[14];
	bea->target_address_size = data[15];

	const u32 file_count = rd_le32 (data + 32);
	const u32 ref_count = rd_le32 (data + 36);

	u64 p = 40;
	u64 file_info_offset;
	u64 dic_off;

	if (bea->version_major2 >= 5)
	{
		p += 8; // AssetOffset -- unused for reading
		file_info_offset = rd_le64 (data + p);
		p += 8;
		dic_off = rd_le64 (data + p);
		p += 8;
		bea->name = load_bea_string (data, size, rd_le64 (data + p));
		p += 8;
		bea->compression_name = load_bea_string (data, size, rd_le64 (data + p));
		p += 8;
		const u64 ref_off = rd_le64 (data + p);
		p += 8;
		if (ref_count && ref_off)
		{
			bea->reference_list = CALLOC (ref_count, sizeof (ccp));
			u64 rp = ref_off;
			for (u32 i = 0; i < ref_count && rp + 8 <= size; i++)
			{
				bea->reference_list[i] = load_bea_string (data, size, rp);
				rp += 8;
			}
			bea->n_references = ref_count;
		}
	}
	else
	{
		file_info_offset = rd_le64 (data + p);
		p += 8;
		dic_off = rd_le64 (data + p);
		p += 8;
		p += 8; // unused u64
		bea->name = load_bea_string (data, size, rd_le64 (data + p));
		p += 8;
	}

	// Parse the on-disk dictionary's node table (see bea_dic_entry_t):
	// CreateBEA() reuses the reference/idx_left/idx_right triplets verbatim
	// whenever the member set on resave is unchanged, re-registering only
	// the key strings with the new file's string pool.
	if (dic_off && dic_off + 8 <= size)
	{
		const u32 n_nodes = rd_le32 (data + dic_off + 4); // excludes root
		const u64 table_off = dic_off + 8;
		const u64 n_total = (u64)n_nodes + 1; // widen before the multiply below:
			// n_nodes==UINT32_MAX would otherwise wrap n_nodes+1 to 0 in 32-bit
			// math, passing the bounds check with a 0-size table while the loop
			// below still iterates 2^32 times into it.
		if (table_off + n_total * 16 <= size)
		{
			bea->n_dict_nodes = (uint)n_total;
			bea->dict = CALLOC (bea->n_dict_nodes, sizeof (bea_dic_entry_t));
			for (u32 i = 0; i <= n_nodes; i++)
			{
				const u64 e = table_off + (u64)i * 16;
				bea->dict[i].reference = rd_le32 (data + e);
				bea->dict[i].idx_left = rd_le16 (data + e + 4);
				bea->dict[i].idx_right = rd_le16 (data + e + 6);
				ccp key = load_bea_string (data, size, rd_le64 (data + e + 8));
				bea->dict[i].key = key ? key : STRDUP ("");
			}
		}
	}

	bea->n_files = file_count;
	bea->files = CALLOC (file_count, sizeof (bea_file_t));
	for (u32 i = 0; i < file_count; i++)
	{
		if (file_info_offset + (u64)i * 8 + 8 > size)
		{
			ResetBEA (bea);
			return ERR_INVALID_DATA;
		}
		const u64 asst_off = rd_le64 (data + file_info_offset + i * 8);
		const enumError err
			= scan_bea_file (&bea->files[i], data, size, asst_off, bea->version_major2);
		if (err)
		{
			ResetBEA (bea);
			return err;
		}
	}
	return ERR_OK;
}

void ResetBEA (bea_archive_t *bea)
{
	if (!bea)
		return;
	for (uint i = 0; i < bea->n_files; i++)
	{
		FREE ((void *)bea->files[i].name);
		FREE (bea->files[i].data);
	}
	FREE (bea->files);
	FREE ((void *)bea->name);
	FREE ((void *)bea->compression_name);
	for (uint i = 0; i < bea->n_references; i++)
		FREE ((void *)bea->reference_list[i]);
	FREE (bea->reference_list);
	for (uint i = 0; i < bea->n_dict_nodes; i++)
		FREE ((void *)bea->dict[i].key);
	FREE (bea->dict);
	memset (bea, 0, sizeof (*bea));
}

//-----------------------------------------------------------------------------
///////////////		Patricia trie ("_DIC") construction		///////////////
//-----------------------------------------------------------------------------
//
// Faithful port of BezelEngineArchive_Lib's ResDict.UpdateNodes()/Tree/Node
// (Switch-Toolbox, File_Format_Library/FileFormats/Archives/BEA/DIC/ResDict.cs).
// Only exercised by CreateBEA() when the member set actually changed --
// otherwise the archive's own already-parsed dict[] (see ScanBEA()) is
// reused verbatim (same node count, same names -> the algorithm below would
// reproduce the exact same reference/idx_left/idx_right values anyway, so
// this is purely a shortcut around re-deriving them). This is the one part
// of the codec validated against only a single real sample (two files, one
// branch): treat with extra care.
//
// Each node's C# ".Data" field is always a full copy of one of the ORIGINAL
// inserted key strings (never a synthesized value -- every branch created
// during Insert() sets Data = the key currently being inserted), so this
// port stores a plain (ccp,len) pair instead of a BigInteger: no bignum
// arithmetic needed, just bit-indexed reads into an existing string.

typedef struct bea_dic_node_t
{
	ccp  data; // borrowed pointer into the caller's 'names' array, or "" for the tree's own root
	int  data_len;
	int  bit_idx; // C#'s bitInx; -1 for the tree's own root
	struct bea_dic_node_t *parent;
	struct bea_dic_node_t *child[2];
} bea_dic_node_t;

// C#'s BigInteger bit 'b' (0 = LSB) of the value built from STR's bytes in
// order (STR's LAST byte holds the value's low-order bits). Implicit
// leading zero bits (b beyond the string's own bit length) read as 0,
// exactly like a real arbitrary-precision integer.
static int bea_bit (ccp str, int len, int b)
{
	if (b < 0)
		return 0;
	const int byte_from_end = b / 8;
	if (byte_from_end >= len)
		return 0;
	const u8 byte = (u8)str[len - 1 - byte_from_end];
	return (byte >> (b % 8)) & 1;
}

// C#'s BitLength(): position of the highest set bit, plus one; 1 for value 0
// (an empty string). Matches the recursive-halving definition exactly for
// nonzero values (halving a base-2 integer until 0 counts its bit length).
static int bea_bit_length (ccp str, int len)
{
	for (int i = 0; i < len; i++)
	{
		if ((u8)str[i])
		{
			int hi = 7;
			while (!(((u8)str[i] >> hi) & 1))
				hi--;
			return (len - i - 1) * 8 + hi + 1;
		}
	}
	return 1;
}

// C#'s first_1bit(): index of the lowest set bit (scanning up from bit 0).
static int bea_first_1bit (ccp str, int len)
{
	const int bl = bea_bit_length (str, len);
	for (int i = 0; i < bl; i++)
		if (bea_bit (str, len, i))
			return i;
	return 0; // unreachable for a real (non-empty) key; C# throws here instead
}

// C#'s bit_mismatch(): index of the first bit (from LSB) where A and B
// differ, or -1 if they're identical over both operands' bit lengths.
static int bea_bit_mismatch (ccp a, int alen, ccp b, int blen)
{
	const int bla = bea_bit_length (a, alen);
	const int blb = bea_bit_length (b, blen);
	const int hi = bla > blb ? bla : blb;
	for (int i = 0; i < hi; i++)
		if (bea_bit (a, alen, i) != bea_bit (b, blen, i))
			return i;
	return -1;
}

// tree.entries: an append-only (value -> node) map. C#'s Dictionary
// indexer assignment either adds a new key at the next sequential index, or
// -- for an already-seen value -- just repoints that SAME index at the new
// node (Count doesn't grow, so entries.Count at that moment, used as the
// index, is unchanged): a plain "linear-scan, update in place or append"
// array reproduces this exactly.
typedef struct bea_dic_tree_t
{
	bea_dic_node_t **entries; // entries[i]->{data,data_len} is entry i's key
	uint n_entries, cap_entries;
	bea_dic_node_t *root;
	bea_dic_node_t **all_nodes; // every node ever allocated, for cleanup
	uint n_all, cap_all;
} bea_dic_tree_t;

static bea_dic_node_t *bea_dic_new_node (
	bea_dic_tree_t *tree, ccp data, int data_len, int bit_idx, bea_dic_node_t *parent)
{
	bea_dic_node_t *n = CALLOC (1, sizeof (*n));
	n->data = data;
	n->data_len = data_len;
	n->bit_idx = bit_idx;
	n->parent = parent ? parent : n;
	n->child[0] = n->child[1] = n;
	if (tree->n_all == tree->cap_all)
	{
		tree->cap_all = tree->cap_all ? tree->cap_all * 2 : 16;
		tree->all_nodes = REALLOC (tree->all_nodes, tree->cap_all * sizeof (*tree->all_nodes));
	}
	tree->all_nodes[tree->n_all++] = n;
	return n;
}

static uint bea_dic_insert_entry (bea_dic_tree_t *tree, ccp data, int data_len, bea_dic_node_t *node)
{
	for (uint i = 0; i < tree->n_entries; i++)
	{
		bea_dic_node_t *e = tree->entries[i];
		if (e->data_len == data_len && !memcmp (e->data, data, data_len))
		{
			tree->entries[i] = node;
			return i;
		}
	}
	if (tree->n_entries == tree->cap_entries)
	{
		tree->cap_entries = tree->cap_entries ? tree->cap_entries * 2 : 16;
		tree->entries = REALLOC (tree->entries, tree->cap_entries * sizeof (*tree->entries));
	}
	tree->entries[tree->n_entries] = node;
	return tree->n_entries++;
}

static uint bea_dic_index_of (bea_dic_tree_t *tree, ccp data, int data_len)
{
	for (uint i = 0; i < tree->n_entries; i++)
		if (tree->entries[i]->data_len == data_len && !memcmp (tree->entries[i]->data, data, data_len))
			return i;
	return 0; // unreachable: every child pointer was itself inserted earlier
}

static bea_dic_node_t *bea_dic_search (bea_dic_tree_t *tree, ccp data, int data_len, bool prev)
{
	if (tree->root->child[0] == tree->root)
		return tree->root;
	bea_dic_node_t *node = tree->root->child[0];
	bea_dic_node_t *prev_node = node;
	for (;;)
	{
		prev_node = node;
		node = node->child[bea_bit (data, data_len, node->bit_idx)];
		if (node->bit_idx <= prev_node->bit_idx)
			break;
	}
	return prev ? prev_node : node;
}

static void bea_dic_insert (bea_dic_tree_t *tree, ccp key, int key_len)
{
	bea_dic_node_t *current = bea_dic_search (tree, key, key_len, true);
	int bit_idx = bea_bit_mismatch (current->data, current->data_len, key, key_len);
	while (bit_idx < current->parent->bit_idx)
		current = current->parent;

	if (bit_idx < current->bit_idx)
	{
		bea_dic_node_t *nn = bea_dic_new_node (tree, key, key_len, bit_idx, current->parent);
		nn->child[bea_bit (key, key_len, bit_idx) ^ 1] = current;
		current->parent->child[bea_bit (key, key_len, current->parent->bit_idx)] = nn;
		current->parent = nn;
		bea_dic_insert_entry (tree, key, key_len, nn);
	}
	else if (bit_idx > current->bit_idx)
	{
		bea_dic_node_t *nn = bea_dic_new_node (tree, key, key_len, bit_idx, current);
		const int b = bea_bit (key, key_len, bit_idx) ^ 1;
		nn->child[b] = bea_bit (current->data, current->data_len, bit_idx) == b ? current : tree->root;
		current->child[bea_bit (key, key_len, current->bit_idx)] = nn;
		bea_dic_insert_entry (tree, key, key_len, nn);
	}
	else
	{
		int new_bit_idx = bea_first_1bit (key, key_len);
		bea_dic_node_t *branch = current->child[bea_bit (key, key_len, bit_idx)];
		if (branch != tree->root)
			new_bit_idx = bea_bit_mismatch (branch->data, branch->data_len, key, key_len);
		bea_dic_node_t *nn = bea_dic_new_node (tree, key, key_len, new_bit_idx, current);
		nn->child[bea_bit (key, key_len, new_bit_idx) ^ 1] = branch;
		current->child[bea_bit (key, key_len, bit_idx)] = nn;
		bea_dic_insert_entry (tree, key, key_len, nn);
	}
}

// Build a fresh dictionary node table (root + one node per NAMES[0..n-1],
// in the same append order UpdateNodes() would produce) from scratch.
// Returned array is CALLOC-owned by the caller (FREE the array itself and
// each .key string); its length is n+1.
static bea_dic_entry_t *BuildBeaDict (ccp const *names, uint n)
{
	bea_dic_tree_t tree = { 0 };
	tree.root = bea_dic_new_node (&tree, "", 0, -1, 0);
	tree.root->parent = tree.root;
	tree.root->child[0] = tree.root->child[1] = tree.root;
	bea_dic_insert_entry (&tree, "", 0, tree.root);

	for (uint i = 0; i < n; i++)
		bea_dic_insert (&tree, names[i], (int)strlen (names[i]));

	bea_dic_entry_t *out = CALLOC (tree.n_entries, sizeof (*out));
	for (uint i = 0; i < tree.n_entries; i++)
	{
		bea_dic_node_t *node = tree.entries[i];
		out[i].reference = (u32)node->bit_idx;
		out[i].idx_left = (u16)bea_dic_index_of (&tree, node->child[0]->data, node->child[0]->data_len);
		out[i].idx_right = (u16)bea_dic_index_of (&tree, node->child[1]->data, node->child[1]->data_len);
		if (i == 0)
			out[i].key = STRDUP ("");
		else
		{
			char *key = MALLOC (node->data_len + 1);
			memcpy (key, node->data, node->data_len);
			key[node->data_len] = 0;
			out[i].key = key;
		}
	}

	for (uint i = 0; i < tree.n_all; i++)
		FREE (tree.all_nodes[i]);
	FREE (tree.all_nodes);
	FREE (tree.entries);
	return out;
}

//-----------------------------------------------------------------------------
///////////////			CreateBEA()				///////////////
//-----------------------------------------------------------------------------
//
// Byte layout faithfully mirrors BinaryDataWriter.FileSaver.Execute() (and
// BevelEngineArchive/ASST/ResDict .Save()), cross-checked field-by-field
// against a real retail sample (WarioWare: Get It Together!'s
// Bonus~BonusCulture.nx.bea) via a from-scratch parse -- see ScanBEA().

typedef struct bea_w_t
{
	u8 *data;
	uint pos, size, cap;
} bea_w_t;

static void bw_reserve (bea_w_t *w, uint extra)
{
	if (w->pos + extra <= w->cap)
		return;
	uint ncap = w->cap ? w->cap * 2 : 4096;
	while (ncap < w->pos + extra)
		ncap *= 2;
	w->data = REALLOC (w->data, ncap);
	memset (w->data + w->cap, 0, ncap - w->cap);
	w->cap = ncap;
}
static void bw_touch (bea_w_t *w)
{
	if (w->pos > w->size)
		w->size = w->pos;
}
static void bw_u8 (bea_w_t *w, u8 v)
{
	bw_reserve (w, 1);
	w->data[w->pos++] = v;
	bw_touch (w);
}
static void bw_u16 (bea_w_t *w, u16 v)
{
	bw_reserve (w, 2);
	wr_le16 (w->data + w->pos, v);
	w->pos += 2;
	bw_touch (w);
}
static void bw_u32 (bea_w_t *w, u32 v)
{
	bw_reserve (w, 4);
	wr_le32 (w->data + w->pos, v);
	w->pos += 4;
	bw_touch (w);
}
static void bw_u64 (bea_w_t *w, u64 v)
{
	bw_reserve (w, 8);
	wr_le64 (w->data + w->pos, v);
	w->pos += 8;
	bw_touch (w);
}
static void bw_bytes (bea_w_t *w, const void *p, uint n)
{
	bw_reserve (w, n);
	if (n)
		memcpy (w->data + w->pos, p, n);
	w->pos += n;
	bw_touch (w);
}
static void bw_align (bea_w_t *w, uint a)
{
	if (a < 2)
		return;
	const uint pad = (a - w->pos % a) % a;
	for (uint i = 0; i < pad; i++)
		bw_u8 (w, 0);
}
static void bw_patch_u16 (bea_w_t *w, uint pos, u16 v)
{
	wr_le16 (w->data + pos, v);
}
static void bw_patch_u32 (bea_w_t *w, uint pos, u32 v)
{
	wr_le32 (w->data + pos, v);
}
static void bw_patch_u64 (bea_w_t *w, uint pos, u64 v)
{
	wr_le64 (w->data + pos, v);
}

// One reserved 8-byte pointer slot needing an entry in the "_RLT" runtime
// relocation table (see SaveRelocateEntryToSection()). BEA only ever uses
// struct_count=1/padding_count=0; offset_count is 1 for a lone pointer or
// N for a contiguous run of N pointers (the ReferenceList array).
typedef struct bea_reloc_t
{
	u32 pos;
	u16 struct_count;
	u8  offset_count, padding_count;
} bea_reloc_t;

typedef struct bea_reloc_list_t
{
	bea_reloc_t *entry;
	uint n, cap;
} bea_reloc_list_t;

static void reloc_add (bea_reloc_list_t *list, u32 pos, u8 offset_count)
{
	if (list->n == list->cap)
	{
		list->cap = list->cap ? list->cap * 2 : 16;
		list->entry = REALLOC (list->entry, list->cap * sizeof (*list->entry));
	}
	list->entry[list->n].pos = pos;
	list->entry[list->n].struct_count = 1;
	list->entry[list->n].offset_count = offset_count;
	list->entry[list->n].padding_count = 0;
	list->n++;
}
static int cmp_reloc_pos (const void *a, const void *b)
{
	const u32 pa = ((const bea_reloc_t *)a)->pos, pb = ((const bea_reloc_t *)b)->pos;
	return pa < pb ? -1 : pa > pb ? 1 : 0;
}

// String pool ("_STR"): each distinct string is written once; every SaveString()
// call site registers a placeholder position against it, patched to the
// string's final offset once the whole pool is emitted (see WriteStringPool()).
typedef struct bea_str_entry_t
{
	ccp str;
	uint *patch, n_patch, cap_patch;
} bea_str_entry_t;
typedef struct bea_strpool_t
{
	bea_str_entry_t *entry;
	uint n, cap;
} bea_strpool_t;

static bea_str_entry_t *strpool_get (bea_strpool_t *pool, ccp str)
{
	for (uint i = 0; i < pool->n; i++)
		if (!strcmp (pool->entry[i].str, str))
			return &pool->entry[i];
	if (pool->n == pool->cap)
	{
		pool->cap = pool->cap ? pool->cap * 2 : 16;
		pool->entry = REALLOC (pool->entry, pool->cap * sizeof (*pool->entry));
	}
	bea_str_entry_t *e = &pool->entry[pool->n++];
	memset (e, 0, sizeof (*e));
	e->str = str;
	return e;
}
static void strpool_patch_add (bea_str_entry_t *e, uint pos)
{
	if (e->n_patch == e->cap_patch)
	{
		e->cap_patch = e->cap_patch ? e->cap_patch * 2 : 4;
		e->patch = REALLOC (e->patch, e->cap_patch * sizeof (*e->patch));
	}
	e->patch[e->n_patch++] = pos;
}

// Reserves the 8-byte pointer slot for STR (or writes a literal 0 for NULL,
// same as SaveString(null)) and registers it with the pool for later
// patching. Does not itself add a relocation entry -- callers add one via
// reloc_add() first, exactly where the reference code does (see CreateBEA()).
static void bw_save_string (bea_w_t *w, bea_strpool_t *pool, ccp str)
{
	if (!str)
	{
		bw_u64 (w, 0);
		return;
	}
	bea_str_entry_t *e = strpool_get (pool, str);
	strpool_patch_add (e, w->pos);
	bw_u64 (w, UINT64_MAX); // placeholder, matches FileSaver.SaveString()
}

// One pending raw data block (a member's compressed bytes), written by
// write_bea_blocks() once the string pool and relocation table are behind
// it (see FileSaver.WriteBlocks() -- data always comes last in the file).
typedef struct bea_block_t
{
	uint patch_pos;
	const u8 *data;
	uint size, alignment;
} bea_block_t;

enumError CreateBEA (u8 **dest, uint *dest_size, bea_archive_t *bea)
{
	bea_w_t w = { 0 };
	bea_reloc_list_t reloc = { 0 };
	bea_strpool_t pool = { 0 };
	bea_block_t *block = CALLOC (bea->n_files ? bea->n_files : 1, sizeof (*block));
	uint *block_header_pos = CALLOC (bea->n_files + 1, sizeof (*block_header_pos));

	const bool v5 = bea->version_major2 >= 5;
	const u32 raw_align = 1u << bea->alignment;

	//--- header

	bw_bytes (&w, "SCNE", 4);
	bw_u32 (&w, 0); // padding
	bw_u32 (&w,
		(u32)bea->version_major << 24 | (u32)bea->version_major2 << 16
			| (u32)bea->version_minor << 8 | bea->version_minor2);
	bw_u16 (&w, bea->byte_order);
	bw_u8 (&w, bea->alignment);
	bw_u8 (&w, bea->target_address_size);
	bw_u32 (&w, 0); // padding
	bw_u16 (&w, 0); // padding2
	const uint ofs_first_block = w.pos;
	bw_u16 (&w, 0); // BlockOffset placeholder
	const uint ofs_reloc_table = w.pos;
	bw_u32 (&w, 0); // RelocationTableOffset placeholder
	const uint ofs_file_size = w.pos;
	bw_u32 (&w, 0); // DataOffset/total-size placeholder
	bw_u32 (&w, bea->n_files);
	bw_u32 (&w, v5 ? bea->n_references : 0);

	uint ofs_asset_block = 0, ofs_asst_ref_array = 0;
	uint ofs_asst_array, ofs_file_dictionary;
	if (v5)
	{
		ofs_asset_block = w.pos;
		bw_u64 (&w, 0); // AssetOffset placeholder (unused by this codec's readers)
		reloc_add (&reloc, w.pos, 1);
		ofs_asst_array = w.pos;
		bw_u64 (&w, 0);
		reloc_add (&reloc, w.pos, 1);
		ofs_file_dictionary = w.pos;
		bw_u64 (&w, 0);
		reloc_add (&reloc, w.pos, 1);
		bw_save_string (&w, &pool, bea->name);
		reloc_add (&reloc, w.pos, 1);
		bw_save_string (&w, &pool, bea->compression_name);
		ofs_asst_ref_array = w.pos;
		bw_u64 (&w, 0);
	}
	else
	{
		reloc_add (&reloc, w.pos, 1);
		ofs_asst_array = w.pos;
		bw_u64 (&w, 0);
		reloc_add (&reloc, w.pos, 1);
		ofs_file_dictionary = w.pos;
		bw_u64 (&w, 0);
		bw_u64 (&w, 0); // unused
		reloc_add (&reloc, w.pos, 1);
		bw_save_string (&w, &pool, bea->name);
	}

	//--- reference list (v5+ only)

	const uint ref_offset = w.pos;
	if (v5 && bea->n_references)
	{
		reloc_add (&reloc, w.pos, (u8)(bea->n_references > 255 ? 255 : bea->n_references));
		for (uint i = 0; i < bea->n_references; i++)
			bw_save_string (&w, &pool, bea->reference_list[i]);
	}

	//--- ASST offset-pointer array, then 40 bytes/file of zero padding
	// (matches wit's own fixed reserved gap -- see FileSaver.Execute()).

	const uint offset_array_asst = w.pos;
	uint *asst_ptr_pos = CALLOC (bea->n_files ? bea->n_files : 1, sizeof (*asst_ptr_pos));
	for (uint i = 0; i < bea->n_files; i++)
	{
		reloc_add (&reloc, w.pos, 1);
		asst_ptr_pos[i] = w.pos;
		bw_u64 (&w, 0);
	}
	for (uint i = 0; i < bea->n_files * 40; i++)
		bw_u8 (&w, 0);

	//--- dictionary ("_DIC"): reuse the archive's own parsed nodes when the
	// member set is unchanged (same count/names, any order), else rebuild.

	bool reuse_dict = bea->dict && bea->n_dict_nodes == bea->n_files + 1;
	if (reuse_dict)
		for (uint i = 1; reuse_dict && i < bea->n_dict_nodes; i++)
		{
			bool found = false;
			for (uint j = 0; !found && j < bea->n_files; j++)
				found = !strcmp (bea->dict[i].key, bea->files[j].name);
			reuse_dict = found;
		}

	bea_dic_entry_t *dict = bea->dict;
	uint n_dict = bea->n_dict_nodes;
	ccp *built_names = 0;
	bea_dic_entry_t *built_dict = 0;
	if (!reuse_dict)
	{
		built_names = CALLOC (bea->n_files ? bea->n_files : 1, sizeof (*built_names));
		for (uint i = 0; i < bea->n_files; i++)
			built_names[i] = bea->files[i].name;
		built_dict = BuildBeaDict (built_names, bea->n_files);
		dict = built_dict;
		n_dict = bea->n_files + 1;
	}

	const uint dictionary_offset = w.pos;
	bw_bytes (&w, "_DIC", 4);
	bw_u32 (&w, n_dict ? n_dict - 1 : 0);
	for (uint i = 0; i < n_dict; i++)
	{
		bw_u32 (&w, dict[i].reference);
		bw_u16 (&w, dict[i].idx_left);
		bw_u16 (&w, dict[i].idx_right);
		reloc_add (&reloc, w.pos, 1);
		bw_save_string (&w, &pool, i == 0 ? "" : dict[i].key);
	}

	//--- ASST structs, in the SAME order the dictionary's keys were just
	// written (dict[1..n_dict-1], matching FileDictionary.GetKey(i)).

	const uint block_offset = w.pos;
	for (uint i = 1; i < n_dict; i++)
	{
		ccp name = i == 0 ? "" : dict[i].key;
		uint fi = 0;
		for (; fi < bea->n_files; fi++)
			if (!strcmp (bea->files[fi].name, name))
				break;
		bea_file_t *f = &bea->files[fi < bea->n_files ? fi : 0];

		bw_patch_u64 (&w, asst_ptr_pos[i - 1], w.pos);

		bw_bytes (&w, "ASST", 4);
		block_header_pos[i - 1] = w.pos; // recorded right after the signature, matching SaveBlockHeader()
		bw_u32 (&w, 0); // block header offset placeholder
		bw_u64 (&w, 0); // block header size placeholder
		bw_u16 (&w, f->unk1);
		bw_u16 (&w, f->unk2);

		u8 *comp = 0;
		uint comp_size = 0;
		if (f->is_compressed)
		{
			const enumError cerr
				= EncodeZSTD (&comp, &comp_size, f->data, f->size, ZSTD_DEFAULT_COMPR);
			if (cerr || !comp)
			{
				FREE (comp);
				comp = MALLOC (f->size ? f->size : 1);
				memcpy (comp, f->data, f->size);
				comp_size = f->size;
			}
		}
		else
		{
			comp = MALLOC (f->size ? f->size : 1);
			memcpy (comp, f->data, f->size);
			comp_size = f->size;
		}
		block[i - 1].data = comp;
		block[i - 1].size = comp_size;
		block[i - 1].alignment = raw_align;

		bw_u32 (&w, comp_size);
		bw_u32 (&w, f->size);

		if (v5)
			bw_bytes (&w, f->file_type, 8);
		bw_u32 (&w, f->unknown3);
		if (bea->version_major2 >= 6)
		{
			bw_u64 (&w, f->file_id1);
			bw_u64 (&w, f->file_id2);
		}

		block[i - 1].patch_pos = w.pos;
		bw_u64 (&w, 0); // data-offset placeholder, patched by write_bea_blocks()

		reloc_add (&reloc, w.pos, 1);
		bw_save_string (&w, &pool, f->name);
	}

	//--- string pool ("_STR")

	bw_bytes (&w, "_STR", 4);
	block_header_pos[bea->n_files] = w.pos; // recorded right after the signature, matching SaveBlockHeader()
	bw_u32 (&w, 0); // block header offset placeholder
	bw_u64 (&w, 0); // block header size placeholder
	bw_u32 (&w, pool.n ? pool.n - 1 : 0);
	for (uint i = 0; i < pool.n; i++)
	{
		bea_str_entry_t *e = &pool.entry[i];
		const uint here = w.pos;
		for (uint k = 0; k < e->n_patch; k++)
			bw_patch_u64 (&w, e->patch[k], here);
		bw_u16 (&w, (u16)strlen (e->str));
		bw_bytes (&w, e->str, (uint)strlen (e->str));
		bw_align (&w, 2);
	}
	const uint section1_size = w.pos;

	//--- relocation table ("_RLT")

	bw_align (&w, raw_align);
	qsort (reloc.entry, reloc.n, sizeof (*reloc.entry), cmp_reloc_pos);

	const uint reloc_table_offset = w.pos;
	bw_bytes (&w, "_RLT", 4);
	const uint ofs_end_of_block = w.pos;
	bw_u32 (&w, reloc_table_offset);
	bw_u32 (&w, 1); // section count
	bw_u32 (&w, 0); // padding
	bw_u64 (&w, 0); // section padding
	bw_u32 (&w, 0); // section position
	bw_u32 (&w, section1_size);
	bw_u32 (&w, 0); // entry index
	bw_u32 (&w, reloc.n);
	for (uint i = 0; i < reloc.n; i++)
	{
		bw_u32 (&w, reloc.entry[i].pos);
		bw_u16 (&w, reloc.entry[i].struct_count);
		bw_u8 (&w, reloc.entry[i].offset_count);
		bw_u8 (&w, reloc.entry[i].padding_count);
	}
	const u32 bea_size = w.pos;
	bw_patch_u32 (&w, ofs_reloc_table, reloc_table_offset);

	//--- raw data blocks, appended last (see FileSaver.WriteBlocks())

	for (uint i = 0; i < bea->n_files; i++)
	{
		bw_align (&w, block[i].alignment);
		bw_patch_u64 (&w, block[i].patch_pos, w.pos);
		bw_bytes (&w, block[i].data, block[i].size);
		FREE ((void *)block[i].data);
	}

	//--- patch each ASST/string-pool block header's "distance to next
	// sibling block" pair (see FileSaver.Execute()'s post-WriteBlocks loop)

	for (uint i = 0; i <= bea->n_files; i++)
	{
		if (i == bea->n_files)
		{
			bw_patch_u32 (&w, block_header_pos[i], 0);
			bw_patch_u64 (&w, block_header_pos[i] + 4, ofs_end_of_block - block_header_pos[i]);
		}
		else
		{
			const u32 block_size = block_header_pos[i + 1] - block_header_pos[i];
			bw_patch_u32 (&w, block_header_pos[i], block_size);
			bw_patch_u64 (&w, block_header_pos[i] + 4, block_size);
		}
	}

	//--- patch remaining header pointers

	bw_patch_u64 (&w, ofs_asst_array, offset_array_asst);
	if (v5 && ofs_asset_block)
		bw_patch_u64 (&w, ofs_asset_block, block_offset);
	bw_patch_u16 (&w, ofs_first_block, (u16)block_offset);
	bw_patch_u64 (&w, ofs_file_dictionary, dictionary_offset);
	if (v5 && ofs_asst_ref_array)
		bw_patch_u64 (&w, ofs_asst_ref_array, ref_offset);
	bw_patch_u32 (&w, ofs_file_size, bea_size);

	//--- cleanup

	FREE (asst_ptr_pos);
	FREE (block);
	FREE (block_header_pos);
	FREE (built_names);
	if (built_dict)
	{
		for (uint i = 0; i < n_dict; i++)
			FREE ((void *)built_dict[i].key);
		FREE (built_dict);
	}
	for (uint i = 0; i < pool.n; i++)
		FREE (pool.entry[i].patch);
	FREE (pool.entry);
	FREE (reloc.entry);

	*dest = w.data;
	*dest_size = w.size;
	return ERR_OK;
}

//-----------------------------------------------------------------------------
///////////////		filesystem glue (extract sidecar / CREATE)	///////////////
//-----------------------------------------------------------------------------

bool looks_like_bea_dir (ccp dir)
{
	char path[PATH_MAX];
	snprintf (path, sizeof (path), "%s/%s", dir, BEA_SETUP_FILE);
	struct stat st;
	return stat (path, &st) == 0 && S_ISREG (st.st_mode);
}

// Per-member metadata parsed from the "file" lines of BEA_SETUP_FILE (see
// write_bea_setup() in create_update.inc), looked up by name while building
// the archive back up in create_bea_dir().
typedef struct bea_setup_file_t
{
	ccp name;
	u16 unk1, unk2;
	u32 unknown3;
	u64 file_id1, file_id2;
	char file_type[9];
	bool is_compressed;
} bea_setup_file_t;

enumError create_bea_dir (ccp source, ccp dest)
{
	char setup_path[PATH_MAX];
	snprintf (setup_path, sizeof (setup_path), "%s/%s", source, BEA_SETUP_FILE);
	FILE *sf = fopen (setup_path, "rb");
	if (!sf)
		return ERROR0 (ERR_NOT_EXISTS, "Missing %s in: %s\n", BEA_SETUP_FILE, source);

	bea_archive_t bea = { 0 };
	bea.alignment = 4;
	bea.version_major2 = 1;

	bea_setup_file_t *setup = 0;
	uint n_setup = 0, cap_setup = 0;

	bea_dic_entry_t *dict = 0;
	uint n_dict = 0, cap_dict = 0;

	char *line = 0;
	size_t line_cap = 0;
	ssize_t len;
	while ((len = getline (&line, &line_cap, sf)) >= 0)
	{
		while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r'))
			line[--len] = 0;
		if (!strncmp (line, "BEA-SETUP", 9))
			continue;

		char *tab = strchr (line, '\t');
		if (!tab)
			continue;
		*tab = 0;
		ccp key = line;
		char *rest = tab + 1;

		if (!strcmp (key, "version"))
			sscanf (rest, "%hhu\t%hhu\t%hhu\t%hhu", &bea.version_major, &bea.version_major2,
				&bea.version_minor, &bea.version_minor2);
		else if (!strcmp (key, "byte_order"))
			bea.byte_order = (u16)strtoul (rest, 0, 10);
		else if (!strcmp (key, "alignment"))
			bea.alignment = (u8)strtoul (rest, 0, 10);
		else if (!strcmp (key, "target_address_size"))
			bea.target_address_size = (u8)strtoul (rest, 0, 10);
		else if (!strcmp (key, "name"))
			bea.name = STRDUP (rest);
		else if (!strcmp (key, "compression_name"))
			bea.compression_name = STRDUP (rest);
		else if (!strcmp (key, "reference"))
		{
			bea.reference_list = REALLOC (bea.reference_list, (bea.n_references + 1) * sizeof (ccp));
			bea.reference_list[bea.n_references++] = STRDUP (rest);
		}
		else if (!strcmp (key, "file"))
		{
			if (n_setup == cap_setup)
			{
				cap_setup = cap_setup ? cap_setup * 2 : 16;
				setup = REALLOC (setup, cap_setup * sizeof (*setup));
			}
			bea_setup_file_t *sfe = &setup[n_setup];
			memset (sfe, 0, sizeof (*sfe));
			char name_buf[PATH_MAX], type_buf[16];
			unsigned unk1 = 2, unk2 = 12, unknown3 = 0, is_compr = 1;
			unsigned long long fid1 = 0, fid2 = 0;
			type_buf[0] = 0;
			sscanf (rest, "%[^\t]\t%u\t%u\t%u\t%llx\t%llx\t%15[^\t]\t%u", name_buf, &unk1, &unk2,
				&unknown3, &fid1, &fid2, type_buf, &is_compr);
			sfe->name = STRDUP (name_buf);
			sfe->unk1 = (u16)unk1;
			sfe->unk2 = (u16)unk2;
			sfe->unknown3 = unknown3;
			sfe->file_id1 = fid1;
			sfe->file_id2 = fid2;
			sfe->is_compressed = is_compr != 0;
			if (strcmp (type_buf, "-"))
				snprintf (sfe->file_type, sizeof (sfe->file_type), "%s", type_buf);
			n_setup++;
		}
		else if (!strcmp (key, "dict"))
		{
			if (n_dict == cap_dict)
			{
				cap_dict = cap_dict ? cap_dict * 2 : 16;
				dict = REALLOC (dict, cap_dict * sizeof (*dict));
			}
			unsigned reference = 0, idx_left = 0, idx_right = 0;
			char key_buf[PATH_MAX];
			key_buf[0] = 0;
			sscanf (rest, "%u\t%u\t%u\t%[^\t]", &reference, &idx_left, &idx_right, key_buf);
			dict[n_dict].reference = reference;
			dict[n_dict].idx_left = (u16)idx_left;
			dict[n_dict].idx_right = (u16)idx_right;
			dict[n_dict].key = STRDUP (key_buf);
			n_dict++;
		}
	}
	FREE (line);
	fclose (sf);

	sarc_build_list_t list = { 0 };
	enumError err = collect_sarc_dir (&list, source, "");

	// collect_sarc_dir() only skips the GENERIC wszst-setup.txt family (see
	// lib-archive-util.h); this codec's own sidecar needs the same
	// treatment or it becomes a bogus member of every repacked archive.
	uint kept = 0;
	for (uint i = 0; i < list.used; i++)
	{
		if (!strcmp (list.entry[i].name, BEA_SETUP_FILE))
		{
			FREE ((void *)list.entry[i].name);
			FREE ((void *)list.entry[i].data);
			continue;
		}
		if (kept != i)
			list.entry[kept] = list.entry[i];
		kept++;
	}
	list.used = kept;

	if (!err && !list.used)
		err = ERR_NOTHING_TO_DO;

	if (!err)
	{
		bea.n_files = list.used;
		bea.files = CALLOC (list.used, sizeof (bea_file_t));
		for (uint i = 0; i < list.used; i++)
		{
			bea_file_t *bf = &bea.files[i];
			bf->name = list.entry[i].name;
			bf->data = (u8 *)list.entry[i].data;
			bf->size = list.entry[i].size;
			list.entry[i].name = 0; // ownership moved into bf->name/bf->data
			list.entry[i].data = 0;

			bf->unk1 = 2;
			bf->unk2 = 12;
			bf->is_compressed = true;
			for (uint s = 0; s < n_setup; s++)
				if (!strcmp (setup[s].name, bf->name))
				{
					bf->unk1 = setup[s].unk1;
					bf->unk2 = setup[s].unk2;
					bf->unknown3 = setup[s].unknown3;
					bf->file_id1 = setup[s].file_id1;
					bf->file_id2 = setup[s].file_id2;
					bf->is_compressed = setup[s].is_compressed;
					memcpy (bf->file_type, setup[s].file_type, sizeof (bf->file_type));
					break;
				}
		}
	}
	reset_sarc_build_list (&list);

	bea.dict = dict;
	bea.n_dict_nodes = n_dict;

	u8 *data = 0;
	uint size = 0;
	if (!err)
		err = CreateBEA (&data, &size, &bea);

	if (!err && !testmode)
	{
		File_t F;
		err = CreateFileOpt (&F, true, dest, false, dest);
		if (F.f && fwrite (data, 1, size, F.f) != size)
			err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", size, dest);
		ResetFile (&F, opt_preserve);
	}

	FREE (data);
	ResetBEA (&bea);
	for (uint i = 0; i < n_setup; i++)
		FREE ((void *)setup[i].name);
	FREE (setup);
	return err;
}
