#ifndef SZS_LIB_CHANS_H
#define SZS_LIB_CHANS_H 1

#include "types.h"
#include "file-type.h"

//
///////////////////////////////////////////////////////////////////////////////
/////////        Nintendo Wii ChannelScript (.cs / RCHE) Format      //////////
///////////////////////////////////////////////////////////////////////////////
//
//  ChannelScript is a compiled ECMAScript bytecode format used by the Wii
//  System Menu and WiiConnect24 channels (Forecast Channel, News Channel,
//  Photo Channel, Wii Shop Channel) to run dynamic banner scripts (.cs).
//
//  Header (0x20 / 32 bytes):
//    u32 magic        – 0x52434845 ("RCHE")
//    u32 version      – version number (v3)
//    u32 file_size    – total file size
//    u8  reserved[20] – reserved / unk
//
//  All section offsets in the subheader are relative to 0x20:
//    0x20: field_0
//    0x24: field_4
//    0x28: field_8
//    0x2C: fds_size      – bytecode (First Data Section) size
//    0x30: fds_offset    – bytecode offset (from 0x20)
//    0x34: table4_count  – exported symbols count
//    0x38: field_18
//    0x3C: field_1C
//    0x40: table1_count  – local methods count
//    0x44: table1_offset – local methods offset (from 0x20)
//    0x48: table2_count  – imported symbols count
//    0x4C: table2_offset – imported symbols offset (from 0x20)
//    0x50: table3_count  – string literals count
//    0x54: table3_offset – string literals offset (from 0x20)
//    0x58: field_38
//    0x5C: field_3C
//    0x60: table4_offset – exported symbols offset (from 0x20)
//    0x64: table5_offset – line start bitmasks offset (from 0x20)
//
///////////////////////////////////////////////////////////////////////////////

#define CHANS_MAGIC "RCHE"
#define CHANS_MAGIC_NUM 0x52434845

typedef struct chans_method_t
{
	u32 offset;         // offset in FDS bytecode
	u16 symbol_id;      // index into exported symbols (Table 4)
	u8  param_count;
	u8  temp_count;
} chans_method_t;

typedef struct chans_symbol_t
{
	u8 length;
	u8 padding;
	u16 offset;
	char *name;
} chans_symbol_t;

typedef struct chans_string_t
{
	u16 byte_len;
	char *utf8;
} chans_string_t;

typedef struct chans_line_block_t
{
	u32 offset;
	u8 data[0x20];
} chans_line_block_t;

typedef struct chans_script_t
{
	u32 version;
	u32 file_size;

	// Bytecode
	u8 *bytecode;
	u32 bytecode_size;

	// Table 1: Local methods
	u32 method_count;
	chans_method_t *methods;

	// Table 2: Imported symbols
	u32 imported_count;
	chans_symbol_t *imported;

	// Table 3: String literals
	u32 string_count;
	chans_string_t *strings;

	// Table 4: Exported symbols
	u32 exported_count;
	chans_symbol_t *exported;

	// Table 5: Line start bitmasks
	u32 block_count;
	chans_line_block_t *blocks;
} chans_script_t;

// API
bool IsChannelScript (const void *data, size_t size);
enumError ScanChannelScript (chans_script_t *cs, const void *data, size_t size);
void ResetChannelScript (chans_script_t *cs);

// Disassembly & decompilation dumps
enumError DumpChannelScriptDisasm (const chans_script_t *cs, char **out_buf, size_t *out_len);
enumError DumpChannelScriptDecompiled (const chans_script_t *cs, char **out_buf, size_t *out_len);
enumError DumpChannelScriptAll (const chans_script_t *cs, char **out_buf, size_t *out_len);

#endif // SZS_LIB_CHANS_H
