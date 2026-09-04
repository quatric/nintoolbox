#ifndef SZS_LIB_BFLYT_H
#define SZS_LIB_BFLYT_H 1

#include "types.h"

// File magics
#define BFLYT_MAGIC_FLYT 0x464C5954 // 'FLYT'
#define BFLYT_MAGIC_FLAN 0x464C414E // 'FLAN'
#define BCLYT_MAGIC_CLYT 0x434C5954 // 'CLYT'
#define BCLYT_MAGIC_CLAN 0x434C414E // 'CLAN'
#define BRLYT_MAGIC_RLYT 0x524C5954 // 'RLYT' (Wii)
#define BRLYT_MAGIC_RLAN 0x524C414E // 'RLAN' (Wii)

// Text file magics (first 4 bytes of the '#FLYT' comment line)
#define BFLYT_TEXT_MAGIC_FLYT 0x23464C59 // '#FLY'
#define BFLYT_TEXT_MAGIC_FLAN 0x23464C41 // '#FLA'
#define BRLYT_TEXT_MAGIC_RLYT 0x23524C59 // '#RLY'
#define BRLYT_TEXT_MAGIC_RLAN 0x23524C41 // '#RLA'
#define BCLYT_TEXT_MAGIC_CLYT 0x23434C59 // '#CLY'
#define BCLYT_TEXT_MAGIC_CLAN 0x23434C41 // '#CLA'

// Endian marks
#define BFLYT_BOM_BE 0xFEFF
#define BFLYT_BOM_LE 0xFFFE

// Chunk magics
#define BFLYT_CHUNK_lyt1 0x6C797431 // 'lyt1'
#define BFLYT_CHUNK_txl1 0x74786C31 // 'txl1'
#define BFLYT_CHUNK_fnl1 0x666E6C31 // 'fnl1'
#define BFLYT_CHUNK_mat1 0x6D617431 // 'mat1'
#define BFLYT_CHUNK_pan1 0x70616E31 // 'pan1'
#define BFLYT_CHUNK_pic1 0x70696331 // 'pic1'
#define BFLYT_CHUNK_txt1 0x74787431 // 'txt1'
#define BFLYT_CHUNK_wnd1 0x776E6431 // 'wnd1'
#define BFLYT_CHUNK_bnd1 0x626E6431 // 'bnd1'
#define BFLYT_CHUNK_grp1 0x67727031 // 'grp1'
#define BFLYT_CHUNK_grs1 0x67727331 // 'grs1'
#define BFLYT_CHUNK_gre1 0x67726531 // 'gre1'
#define BFLYT_CHUNK_pas1 0x70617331 // 'pas1'
#define BFLYT_CHUNK_pae1 0x70616531 // 'pae1'
#define BFLYT_CHUNK_usd1 0x75736431 // 'usd1'
#define BFLYT_CHUNK_prt1 0x70727431 // 'prt1'
#define BFLYT_CHUNK_cnt1 0x636E7431 // 'cnt1'

//
///////////////////////////////////////////////////////////////////////////////
// Generic ordered tree used as the lossless layout model.
// This is a C port of the OrderedDict tree that benzin (bflyt.py) builds,
// so the text (.tflyt) representation round-trips through the reference tool.
///////////////////////////////////////////////////////////////////////////////

typedef struct bf_node_t bf_node_t;
typedef struct bf_list_t bf_list_t;

typedef enum bf_val_type_t
{
	BF_T_NONE, // Python None
	BF_T_BOOL,
	BF_T_INT,
	BF_T_UINT, // same 32-bit bit pattern as BF_T_INT, but printed/encoded
	           // unsigned (BYML type 0xD2 instead of 0xD1)
	BF_T_FLOAT,
	BF_T_STR, // UTF-8 string, malloc owned, never NULL
	BF_T_BYTES, // raw bytes (Python bytes), malloc owned
	BF_T_NODE, // an ordered dict (Python OrderedDict)
	BF_T_LIST // an ordered list
} bf_val_type_t;

typedef struct bf_val_t
{
	bf_val_type_t type;
	union
	{
		bool b;
		int i;
		double f;
		char *s; // UTF-8 string, malloc owned
		struct
		{
			u8 *d;
			uint n;
		} by; // raw bytes, malloc owned
		bf_node_t *node;
		bf_list_t *list;
	} u;
} bf_val_t;

struct bf_list_t
{
	bf_val_t *items;
	uint n, cap;
};

struct bf_node_t
{
	struct bf_kv_t
	{
		char *key; // malloc owned, never NULL
		bf_val_t val;
	} *kv;
	uint n, cap;
};

void BFNodeInit (bf_node_t *node);
void BFNodeFree (bf_node_t *node);
void BFListInit (bf_list_t *list);
void BFListFree (bf_list_t *list);
void BFValClear (bf_val_t *val);

// set functions return ERR_OK or ERR_OUT_OF_MEMORY
// (they take ownership of the passed string on success)
enumError BFNodeSetStr (bf_node_t *node, ccp key, ccp s);
enumError BFNodeSetInt (bf_node_t *node, ccp key, int i);
enumError BFNodeSetFloat (bf_node_t *node, ccp key, double f);
enumError BFNodeSetBool (bf_node_t *node, ccp key, bool b);
enumError BFNodeSetNone (bf_node_t *node, ccp key);
enumError BFNodeSetBytes (bf_node_t *node, ccp key, const void *data, uint n);
bf_node_t *BFNodeSetNode (bf_node_t *node, ccp key); // returns the new child, NULL on OOM
bf_list_t *BFNodeSetList (bf_node_t *node, ccp key); // returns the new list, NULL on OOM
bf_val_t *BFNodeGet (bf_node_t *node, ccp key); // NULL if absent

enumError BFListAddStr (bf_list_t *list, ccp s);
enumError BFListAddInt (bf_list_t *list, int i);
enumError BFListAddFloat (bf_list_t *list, double f);
enumError BFListAddBool (bf_list_t *list, bool b);
enumError BFListAddBytes (bf_list_t *list, const void *data, uint n);
bf_node_t *BFListAddNode (bf_list_t *list); // returns the new element, NULL on OOM
bf_list_t *BFListAddList (bf_list_t *list); // returns the new element, NULL on OOM

//
///////////////////////////////////////////////////////////////////////////////
// txtree text format (port of benzin txtree.py) - lossless tree dump/load.
///////////////////////////////////////////////////////////////////////////////

// Returns malloc'd NUL-terminated text (Python repr based). NULL on OOM.
char *BFTreeDump (const bf_node_t *root);

// Parse txtree text into ROOT. On error the tree is left partially filled.
enumError BFTreeLoad (const char *text, uint len, bf_node_t *root);

//
///////////////////////////////////////////////////////////////////////////////
// BFLYT/BCLYT/BFLAN/BCLAN model + scan/save API.
// The scan accepts both binary layout data and txtree text.
///////////////////////////////////////////////////////////////////////////////

typedef struct bflyt_t
{
	u32 magic; // FLYT/CLYT/FLAN/CLAN, or 0 if the input was text
	u32 data_size; // size of the scanned binary input (0 for text)
	bf_node_t tree; // root tree: byte-order, version, BFLYT, magic
} bflyt_t;

void InitializeBFLYT (bflyt_t *bflyt);
void ResetBFLYT (bflyt_t *bflyt);

// Detect magic (FLYT/CLYT/FLAN/CLAN) or the txtree text format.
enumError ScanBFLYT (bflyt_t *bflyt, bool init, const u8 *data, uint data_size);

// Build a complete binary layout from the model tree. *DEST is malloc owned.
enumError BuildBFLYT (const bflyt_t *bflyt, u8 **dest, uint *dest_size);

enumError SaveRawBFLYT (const bflyt_t *bflyt, ccp fname, bool set_time);
enumError SaveTextBFLYT (const bflyt_t *bflyt, ccp fname, bool set_time);

#endif // SZS_LIB_BFLYT_H
