#ifndef SZS_LIB_CNUT_H
#define SZS_LIB_CNUT_H 1

#include "types.h"
#include "file-type.h"

// Compiled Squirrel script archive format (.cnut / .nut) used in Wii Party
// and Nd Cube games.

#define SQ_BYTECODE_STREAM_TAG 0xFAFA

// Squirrel object type tags
#define SQ_RT_NULL        0x0001
#define SQ_RT_INTEGER     0x0002
#define SQ_RT_FLOAT       0x0004
#define SQ_RT_BOOL        0x0008
#define SQ_RT_STRING      0x0010
#define SQ_RT_TABLE       0x0020
#define SQ_RT_ARRAY       0x0040
#define SQ_RT_USERDATA    0x0080
#define SQ_RT_CLOSURE     0x0100
#define SQ_RT_NATCLOSURE  0x0200
#define SQ_RT_GENERATOR   0x0400
#define SQ_RT_USERPOINTER 0x0800
#define SQ_RT_THREAD      0x1000
#define SQ_RT_FUNCPROTO   0x2000
#define SQ_RT_CLASS       0x4000
#define SQ_RT_INSTANCE    0x8000
#define SQ_RT_WEAKREF     0x00010000

typedef struct cnut_object_t
{
	u32 raw_type;
	u16 type;
	u32 len;
	char *str;
	s32 ival;
	float fval;
	bool bval;
} cnut_object_t;

typedef struct cnut_instruction_t
{
	s32 arg1;
	u8 op;
	u8 arg0;
	u8 arg2;
	u8 arg3;
} cnut_instruction_t;

typedef struct cnut_localvar_t
{
	char name[128];
	u32 pos;
	u32 start_op;
	u32 end_op;
} cnut_localvar_t;

typedef struct cnut_lineinfo_t
{
	u32 line;
	u32 op;
} cnut_lineinfo_t;

typedef struct cnut_outerval_t
{
	u32 type;
	cnut_object_t src;
	cnut_object_t name;
} cnut_outerval_t;

typedef struct cnut_funcproto_t
{
	char source_name[128];
	char func_name[128];
	uint n_literals;
	cnut_object_t *literals;
	uint n_parameters;
	char **parameters;
	uint n_outervalues;
	cnut_outerval_t *outervalues;
	uint n_localvars;
	cnut_localvar_t *localvars;
	uint n_lineinfos;
	cnut_lineinfo_t *lineinfos;
	uint n_defaultparams;
	u32 *defaultparams;
	uint n_instructions;
	cnut_instruction_t *instructions;
	uint n_functions;
	struct cnut_funcproto_t *functions;
	u32 stacksize;
	u8 bgenerator;
	u8 varparams;
} cnut_funcproto_t;

typedef struct cnut_t
{
	const u8 *data;
	size_t size;
	u16 stream_tag;
	char magic[5];
	u32 char_size;
	cnut_funcproto_t root;
} cnut_t;

// Returns true if 'data' is a valid compiled Squirrel script (.cnut / SQIR).
bool IsCNUT (const u8 *data, size_t size);

// Scans and parses the CNUT bytecode container.
enumError ScanCNUT (cnut_t *cnut, const u8 *data, size_t size);

// Frees all memory associated with cnut.
void ResetCNUT (cnut_t *cnut);

// Disassembles the CNUT bytecode into readable Squirrel assembly / text representation.
enumError DisassembleCNUT (const cnut_t *cnut, char **out_text, size_t *out_size);

// Extracts string constants and messages from the CNUT bytecode into a text / message table.
enumError ExtractCNUTStrings (const cnut_t *cnut, char **out_text, size_t *out_size);

// Creates a basic compiled CNUT (SQIR) script containing string table and instructions.
enumError CreateCNUT (u8 **dest, size_t *dest_size, const char *source_name, const char *func_name,
	uint n_strings, const char *const *strings, uint n_instructions, const cnut_instruction_t *instructions);

#endif // SZS_LIB_CNUT_H
