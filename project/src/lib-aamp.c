#include "lib-aamp.h"
#include "lib-std.h"

// v2 header, little-endian (zeldamods.org/wiki/AAMP; matches every real BOTW/TOTK .aamp file we
// checked -- Switch-Toolbox's own AAMP.cs only reads magic+version itself and defers the rest to
// an external closed-source library, so we don't have a from-source cross-check for the fields
// past those two):
//   char magic[4];             // "AAMP"
//   u32  version;              // 2
//   u32  flags;                // bit0: little endian strings; always 0 so far in practice
//   u32  file_size;            // total file size
//   u32  pio_version;          // parameter IO version of the game that wrote it
//   u32  data_offset;          // offset of the root node, relative to end of this header (0x30)
//   u32  string_table_offset;  // relative to end of header
//   u32  data_size;
//   u32  string_size;
#define AAMP_HDR_SIZE 0x30

bool IsAAMP (const u8 *data, size_t size)
{
	return data && size >= 4 && !memcmp (data, "AAMP", 4);
}

enumError DecodeAAMP_Text (FILE *out, const u8 *data, size_t size)
{
	if (!out || !IsAAMP (data, size))
		return ERR_INVALID_DATA;

	if (size < AAMP_HDR_SIZE)
		return ERROR0 (ERR_INVALID_DATA, "AAMP: file shorter than the fixed header (%zu bytes)\n",
			size);

	const u32 version = rd_le32 (data + 4);
	const u32 flags = rd_le32 (data + 8);
	const u32 file_size = rd_le32 (data + 0xc);
	const u32 pio_version = rd_le32 (data + 0x10);
	const u32 data_offset = rd_le32 (data + 0x14);
	const u32 string_table_offset = rd_le32 (data + 0x18);
	const u32 data_size = rd_le32 (data + 0x1c);
	const u32 string_size = rd_le32 (data + 0x20);

	// every offset/size pair is relative to the end of the fixed header; check the addition at
	// 64 bit width before trusting them against the real buffer length.
	if ((u64)AAMP_HDR_SIZE + (u64)data_offset + (u64)data_size > size)
		return ERROR0 (ERR_INVALID_DATA, "AAMP: data section out of bounds\n");
	if ((u64)AAMP_HDR_SIZE + (u64)string_table_offset + (u64)string_size > size)
		return ERROR0 (ERR_INVALID_DATA, "AAMP: string table out of bounds\n");
	if (file_size && file_size > size)
		return ERROR0 (ERR_INVALID_DATA, "AAMP: header file_size (%u) exceeds actual size (%zu)\n",
			file_size, size);

	fprintf (out, "#AAMP\n"
		"# Nintendo Parameter Archive -- header summary only.\n"
		"# The node tree uses hashed hierarchical offsets not publicly documented in a\n"
		"# from-source form; decode is limited to the fixed v2 header for now.\n\n"
		"version = %u\n"
		"flags = 0x%x\n"
		"file_size = %u\n"
		"pio_version = %u\n"
		"data_offset = 0x%x\n"
		"data_size = %u\n"
		"string_table_offset = 0x%x\n"
		"string_size = %u\n",
		version, flags, file_size, pio_version, data_offset, data_size, string_table_offset,
		string_size);

	return ERR_OK;
}
