#ifndef LIB_AAMP_H
#define LIB_AAMP_H

#include <stdio.h>
#include "types.h"
#include "file-type.h"
#include "lib-nintendo.h"

// Nintendo Parameter Archive (.aamp / AAMP) format definitions.
// Supports both V1 (Wii U / early) and V2 (Switch / BOTW / TOTK / etc.)
// with full binary reading/writing and YAML / JSON conversion.

typedef enum aamp_param_type_t
{
	AAMP_TYPE_BOOL         = 0,
	AAMP_TYPE_FLOAT        = 1,
	AAMP_TYPE_INT          = 2,
	AAMP_TYPE_VEC2         = 3,
	AAMP_TYPE_VEC3         = 4,
	AAMP_TYPE_VEC4         = 5,
	AAMP_TYPE_COLOR        = 6,
	AAMP_TYPE_STRING32     = 7,
	AAMP_TYPE_STRING64     = 8,
	AAMP_TYPE_CURVE1       = 9,
	AAMP_TYPE_CURVE2       = 10,
	AAMP_TYPE_CURVE3       = 11,
	AAMP_TYPE_CURVE4       = 12,
	AAMP_TYPE_BUFFER_INT   = 13,
	AAMP_TYPE_BUFFER_FLOAT = 14,
	AAMP_TYPE_STRING256    = 15,
	AAMP_TYPE_QUAT         = 16,
	AAMP_TYPE_UINT         = 17,
	AAMP_TYPE_BUFFER_UINT  = 18,
	AAMP_TYPE_BUFFER_BIN   = 19,
	AAMP_TYPE_STRING_REF   = 20,
} aamp_param_type_t;

typedef struct aamp_curve_t
{
	u32 uints[2];
	float floats[30];
} aamp_curve_t;

typedef struct aamp_param_entry_t
{
	u32 hash;
	aamp_param_type_t type;
	union
	{
		bool b;
		float f;
		s32 i;
		u32 u;
		float vec[4]; // vec2, vec3, vec4, color, quat
		char *str;    // str32, str64, str256, strRef
		struct
		{
			u32 count;
			void *data; // buffer_int (s32*), buffer_float (float*), buffer_uint (u32*), buffer_bin (u8*)
		} buf;
		struct
		{
			u32 count; // 1..4
			aamp_curve_t *curves;
		} curve;
	};
} aamp_param_entry_t;

typedef struct aamp_param_object_t
{
	u32 hash;
	u32 group_hash; // V1 only
	u32 entry_count;
	u32 entry_alloc;
	aamp_param_entry_t *entries;
} aamp_param_object_t;

typedef struct aamp_param_list_t
{
	u32 hash;
	u32 list_count;
	u32 list_alloc;
	struct aamp_param_list_t *lists;
	u32 object_count;
	u32 object_alloc;
	aamp_param_object_t *objects;
} aamp_param_list_t;

typedef struct aamp_file_t
{
	u32 version;        // 1 or 2
	u32 flags;          // bit0: LE (1)
	bool is_le;
	u32 pio_version;
	char pio_type[256];
	aamp_param_list_t root;
} aamp_file_t;

void InitializeAAMP (aamp_file_t *aamp);
void ResetAAMP (aamp_file_t *aamp);

// Hash name dictionary
void AAMP_InitHashDB (void);
const char *AAMP_HashToName (u32 hash, char *buf, size_t buf_size);
u32 AAMP_NameToHash (const char *name);

// Detection & binary scan
bool IsAAMP (const u8 *data, size_t size);
enumError ScanAAMP (aamp_file_t *aamp, const u8 *data, size_t size);

// Binary writing (V1 & V2)
enumError WriteAAMP (const aamp_file_t *aamp, u8 **dest, size_t *dest_size, u32 version, bool is_le);

// YAML decoding (AAMP binary -> YAML text)
enumError DecodeAAMP_YAML (FILE *out, const u8 *data, size_t size);

// YAML encoding (YAML text -> AAMP binary)
enumError EncodeAAMP_Text (
	u8 **dest, uint *dest_size, const char *text, uint text_len, u32 target_version, bool is_le);

// JSON decoding (AAMP binary -> JSON text)
enumError DecodeAAMP_JSON (FILE *out, const u8 *data, size_t size);

// High-level CLI helpers
enumError decode_aamp_file (ccp source, ccp dest);
enumError encode_aamp_file (ccp source, ccp dest);
enumError decode_aamp_if_possible (ccp arg);
void DumpStructureAAMP (FILE *out, const aamp_file_t *aamp, int indent);

#endif // LIB_AAMP_H
