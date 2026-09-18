#ifndef SZS_LIB_MSBT_H
#define SZS_LIB_MSBT_H 1

#include "lib-std.h"
#include "file-type.h"

// Message Studio formats
// MSBT: MsgStdBn (Text)
// MSBP: MsgPrjBn (Project)
// MSBF: MsgFlwBn (Flowchart)

typedef enum msbt_encoding_t
{
	MSBT_ENC_UTF8 = 0,
	MSBT_ENC_UTF16 = 1,
	MSBT_ENC_UTF32 = 2
} msbt_encoding_t;

typedef struct msbt_entry_t
{
	char *label; // Label name (e.g. "T_Mario_00") or NULL/empty (LBL1 mode)
	char *text; // Decoded UTF-8 text string (with escape tags like [tag:0,1,0001])
	u32 index; // Index in message table
	u8 *attrib; // Attribute bytes (or NULL, excluding trailing string offset)
	u32 attrib_size; // Size of attribute bytes (excluding trailing string offset)
	u32 style_index; // Style index (or 0)
	char *attr_str; // Optional attribute string (ATR1 trailing table) or NULL
	u32 msg_id; // Numeric message ID (NLI1 mode)
} msbt_entry_t;

typedef struct msbt_file_t
{
	char *fname;
	bool is_big_endian;
	msbt_encoding_t encoding;
	u8 version;

	msbt_entry_t *entries;
	uint num_entries;
	uint alloc_entries;

	// Optional raw attribute global info
	u32 attr_item_size;

	// CLMS/MSBTConverter parity (all optional, backwards compatible)
	bool uses_nli1; // true: NLI1 numeric IDs instead of LBL1 labels
	u32 label_slot_count; // LBL1 hash bucket count (official: 101, 0 = auto)
	bool has_ato1; // ATO1 section present (opaque)
	u8 *ato1_data; // Raw ATO1 payload or NULL
	u32 ato1_size; // Size of ATO1 payload
	bool uses_attr_strings; // ATR1 has trailing string table
	bool has_tsy1; // TSY1 style section present
	bool is_wmbt; // WMBT variant (TXTW instead of TXT2, same magic)
} msbt_file_t;

// Project (MSBP) structures (full LMS spec: CLR1/CLB1, ATI2/ALB1/ALI2,
// TGG2/TAG2/TGP2/TGL2, SYL3/SLB1, CTI1)
typedef struct msbp_color_t
{
	char *name;
	u8 r, g, b, a;
} msbp_color_t;

typedef struct msbp_attribute_t
{
	char *name;
	u8 type; // 9 = list, else scalar (2 = u32, 8 = u8, etc.)
	u16 list_index; // ALI2 list slot (raw, preserved)
	u32 offset;
	char **list_items;
	uint num_list_items;
} msbp_attribute_t;

typedef struct msbp_tag_param_t
{
	char *name;
	u8 type; // 9 = list into TGL2
	char **list_items; // TGL2 strings when type == 9
	uint num_list_items;
} msbp_tag_param_t;

typedef struct msbp_tag_t
{
	char *name;
	u16 tag_id; // index within TAG2 (preserved order)
	msbp_tag_param_t *params;
	uint num_params;
} msbp_tag_t;

typedef struct msbp_tag_group_t
{
	char *name;
	u16 group_id; // TGG2 group id (v4 explicit, v3 = positional)
	msbp_tag_t *tags;
	uint num_tags;
} msbp_tag_group_t;

typedef struct msbp_style_t
{
	char *name; // SLB1 label or NULL
	int region_width;
	int line_number;
	int font_index;
	int base_color_index;
} msbp_style_t;

typedef struct msbp_file_t
{
	char *fname;
	bool is_big_endian;
	msbt_encoding_t encoding;
	u8 version;
	u32 label_slot_count; // shared LBL hash buckets (official: 29, 0 = auto)

	msbp_color_t *colors;
	uint num_colors;
	bool has_colors;

	msbp_attribute_t *attributes;
	uint num_attributes;
	bool has_attributes;

	msbp_tag_group_t *tag_groups;
	uint num_tag_groups;
	bool has_tags;

	msbp_style_t *styles;
	uint num_styles;
	bool has_styles;

	char **source_files;
	uint num_source_files;
	bool has_source_files;
} msbp_file_t;

// Flowchart (MSBF) structures
typedef enum msbf_node_type_t
{
	MSBF_NODE_MESSAGE = 1,
	MSBF_NODE_BRANCH = 2,
	MSBF_NODE_EVENT = 3,
	MSBF_NODE_ENTRY = 4
} msbf_node_type_t;

typedef struct msbf_node_t
{
	u16 node_id;
	u8 type; // msbf_node_type_t
	u8 param_type; // FLW3 parameter type (0-6), preserved
	u16 next_node; // Next node ID (or 0xFFFF)
	char *label; // Label if entry point or named node

	// Message node fields (FLW3: msbt file index + msg index)
	u16 msbt_index;
	u16 msg_index;
	char *msg_label;

	// Branch node fields
	u16 condition_id;
	u16 *branches;
	uint num_branches;

	// Event node fields
	u16 event_id;
	u32 event_param;

	// Raw preservation for unknown/future fields
	u8 raw[16];
	bool has_raw;
} msbf_node_t;

typedef struct msbf_file_t
{
	char *fname;
	bool is_big_endian;
	msbt_encoding_t encoding;
	u8 version;

	msbf_node_t *nodes;
	uint num_nodes;
	uint alloc_nodes;

	// Variant preservation (FLW3/LBL1 vs legacy FLW2/FEN1)
	bool is_legacy_flw2; // true: FLW2 section instead of FLW3
	bool has_fen1; // true: FEN1 labels instead of (or in addition to) LBL1
	u32 fen1_slot_count; // FEN1 hash buckets (0 = auto)
	bool has_lbl1;
	bool has_string_table; // FLW3 trailing string table present
	u8 *flow_string_table;
	u32 flow_string_table_size;
} msbf_file_t;

// MSBT API
void InitMSBT (msbt_file_t *msbt);
void ResetMSBT (msbt_file_t *msbt);
enumError ScanMSBT (msbt_file_t *msbt, const u8 *data, uint data_size, ccp fname);
enumError SaveTextMSBT (const msbt_file_t *msbt, ccp dest_fname);
enumError SaveJSONMSBT (const msbt_file_t *msbt, ccp dest_fname);
enumError CreateMSBT (u8 **out_data, uint *out_size, const msbt_file_t *msbt);
enumError LoadTextMSBT (msbt_file_t *msbt, ccp src_fname);

// MSBP API
void InitMSBP (msbp_file_t *msbp);
void ResetMSBP (msbp_file_t *msbp);
enumError ScanMSBP (msbp_file_t *msbp, const u8 *data, uint data_size, ccp fname);
enumError SaveTextMSBP (const msbp_file_t *msbp, ccp dest_fname);
enumError SaveJSONMSBP (const msbp_file_t *msbp, ccp dest_fname);
enumError CreateMSBP (u8 **out_data, uint *out_size, const msbp_file_t *msbp);
enumError LoadTextMSBP (msbp_file_t *msbp, ccp src_fname);

// MSBF API
void InitMSBF (msbf_file_t *msbf);
void ResetMSBF (msbf_file_t *msbf);
enumError ScanMSBF (msbf_file_t *msbf, const u8 *data, uint data_size, ccp fname);
enumError SaveTextMSBF (const msbf_file_t *msbf, ccp dest_fname);
enumError SaveJSONMSBF (const msbf_file_t *msbf, ccp dest_fname);
enumError CreateMSBF (u8 **out_data, uint *out_size, const msbf_file_t *msbf);
enumError LoadTextMSBF (msbf_file_t *msbf, ccp src_fname);

// Helper / Detection
bool IsMSBT (const u8 *data, uint size);
bool IsMSBP (const u8 *data, uint size);
bool IsMSBF (const u8 *data, uint size);

#endif // SZS_LIB_MSBT_H
