// SPDX-License-Identifier: GPL-2.0+
// Nintendo EAD "Bezel Engine Archive" (.bea / .nx.bea, "SCNE") -- seen in
// WarioWare: Get It Together! and other Nintendo EAD Tokyo Switch titles.
// Reverse-engineered format, no public spec; this codec follows the layout
// implemented by KillzXGaming/Switch-Toolbox's BezelEngineArchive_Lib
// (File_Format_Library/FileFormats/Archives/BEA), cross-checked byte-for-byte
// against a real retail sample (Bonus~BonusCulture.nx.bea).
#ifndef LIB_BEA_H
#define LIB_BEA_H 1

#include "lib-nintendo.h"

//-----------------------------------------------------------------------------

typedef struct bea_file_t
{
	ccp  name; // relative member path, e.g. "Bonus/BonusCulture/Layout.lyt" (owned)
	u8  *data; // decompressed content (owned)
	uint size; // decompressed size

	u16  unk1, unk2; // always present; unk2 varies per member (looks like a type tag)
	u32  unknown3;

	// Only meaningful when the owning archive's version_major2 >= 6
	u64  file_id1, file_id2;

	// Only meaningful when the owning archive's version_major2 >= 5
	char file_type[9]; // up to 8 ASCII bytes + NUL

	bool is_compressed; // true: 'data' is stored zstd-compressed on disk
} bea_file_t;

// One "_DIC" Patricia-trie node, parsed from the archive's on-disk
// dictionary (see ScanBEA()/BuildBeaDict() in lib-bea.c). Node 0 is always
// the trie root (its own on-disk key is always the empty string, never a
// real member name); nodes 1..n_dict_nodes-1 are the real entries, one per
// archive member, though not necessarily in the same order as 'files'.
typedef struct bea_dic_entry_t
{
	u32 reference;
	u16 idx_left, idx_right;
	ccp key; // owned; "" for node 0
} bea_dic_entry_t;

typedef struct bea_archive_t
{
	u8  version_major, version_major2, version_minor, version_minor2;
	u16 byte_order;
	u8  alignment;
	u8  target_address_size;

	ccp name; // owned, may be NULL

	// Only meaningful when version_major2 >= 5
	ccp  compression_name; // owned, may be NULL
	ccp *reference_list; // owned array of owned strings, may be NULL
	uint n_references;

	bea_file_t *files;
	uint n_files;

	// Parsed "_DIC" dictionary nodes (see bea_dic_entry_t), kept so a resave
	// that neither adds nor removes a member can reuse their
	// reference/idx_left/idx_right verbatim (only the key strings need
	// re-registering with the new file's string pool) instead of
	// re-deriving the Patricia trie from scratch -- the highest-risk part
	// of this codec (see BuildBeaDict() in lib-bea.c), only exercised when
	// the member set actually changes.
	bea_dic_entry_t *dict;
	uint n_dict_nodes; // includes the root (node 0)
} bea_archive_t;

//-----------------------------------------------------------------------------

bool IsBEA (const u8 *data, uint size);
enumError ScanBEA (bea_archive_t *bea, const u8 *data, uint size);
void ResetBEA (bea_archive_t *bea);

// Build archive bytes. If 'reuse_dict' is true and the archive's own
// 'dic_raw' matches the current file set 1:1 (same names, same order,
// same count), the original dictionary bytes are reused verbatim, matching
// the on-disk member order they were built for. Otherwise a fresh
// dictionary is derived with BuildBeaDict() (see lib-bea.c) -- correct for
// the reference algorithm as tested, but with far fewer real-world samples
// to validate against than the "member content edited in place" path.
enumError CreateBEA (u8 **dest, uint *dest_size, bea_archive_t *bea);

//-----------------------------------------------------------------------------

extern ccp BEA_SETUP_FILE;
bool looks_like_bea_dir (ccp dir);
enumError create_bea_dir (ccp source, ccp dest);

#endif // LIB_BEA_H
