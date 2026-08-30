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
	char *label; // Label name (e.g. "T_Mario_00") or NULL/empty
	char *text; // Decoded UTF-8 text string (with escape tags like [tag:0,1,0001])
	u32 index; // Index in message table
	u8 *attrib; // Attribute bytes (or NULL)
	u32 attrib_size; // Size of attribute bytes
	u32 style_index; // Style index (or 0)
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
} msbt_file_t;

// Project (MSBP) structures
typedef struct msbp_color_t
{
	char *name;
	u8 r, g, b, a;
} msbp_color_t;

typedef struct msbp_attribute_t
{
	char *name;
	u8 type;
	u32 offset;
	char **list_items;
	uint num_list_items;
} msbp_attribute_t;

typedef struct msbp_tag_param_t
{
	char *name;
	u8 type;
} msbp_tag_param_t;

typedef struct msbp_tag_t
{
	char *name;
	u16 tag_id;
	msbp_tag_param_t *params;
	uint num_params;
} msbp_tag_t;

typedef struct msbp_tag_group_t
{
	char *name;
	u16 group_id;
	msbp_tag_t *tags;
	uint num_tags;
} msbp_tag_group_t;

typedef struct msbp_file_t
{
	char *fname;
	bool is_big_endian;
	msbt_encoding_t encoding;
	u8 version;

	msbp_color_t *colors;
	uint num_colors;

	msbp_attribute_t *attributes;
	uint num_attributes;

	msbp_tag_group_t *tag_groups;
	uint num_tag_groups;
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
	u16 next_node; // Next node ID (or 0xFFFF)
	char *label; // Label if entry point or named node

	// Message node fields
	u16 msg_index;
	char *msg_label;

	// Branch node fields
	u16 condition_id;
	u16 *branches;
	uint num_branches;

	// Event node fields
	u16 event_id;
	u32 event_param;
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
