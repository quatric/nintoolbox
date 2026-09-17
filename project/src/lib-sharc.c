#include "lib-sharc.h"
#include "lib-std.h"

// SHARC (source archive), little-endian, verified against Switch-Toolbox
// File_Format_Library/FileFormats/Shader/SHARC/SHARC.cs Header.Read():
//   char magic[4];        // "AAHS"
//   u32  version;
//   u32  file_size;
//   u32  bom;
//   u32  name_length;
//   char name[name_length];
//   -- pos = here --
//   u32  source_array_offset;  // relative to 'pos'
//   u32  program_count;
//   { ShaderProgram }  program_count times
//   -- seek to pos + source_array_offset --
//   u32  source_section_size;
//   u32  source_file_count;
//   { SourceData }  source_file_count times
//
// Every ShaderProgram/SourceData entry starts with its own u32 section_size (byte count
// including itself, measured from the entry's own start), so we can skip over the
// version-dependent, undocumented shader-variation payload entirely and just walk entry to
// entry -- we only need enough of each entry's head to pull out its name.

#define SHARC_HDR_MIN 20

bool IsSHARC (const u8 *data, size_t size)
{
	return data && size >= 4 && !memcmp (data, "AAHS", 4);
}

bool IsSHARCFB (const u8 *data, size_t size)
{
	return data && size >= 4 && !memcmp (data, "BAHS", 4);
}

// Copies up to 'max' bytes from 'p' as a NUL-terminated string, bounds-checked against 'limit'.
static void copy_name (char *dest, uint destsz, const u8 *p, u32 len, const u8 *limit)
{
	if (len >= destsz)
		len = destsz - 1;
	if (p + len > limit)
		len = p <= limit ? (u32)(limit - p) : 0;
	memcpy (dest, p, len);
	dest[len] = 0;
}

enumError DecodeSHARC_Text (FILE *out, const u8 *data, size_t size)
{
	if (!out || !IsSHARC (data, size))
		return ERR_INVALID_DATA;
	if (size < SHARC_HDR_MIN)
		return ERROR0 (ERR_INVALID_DATA, "SHARC: file shorter than the fixed header\n");

	const u8 *end = data + size;
	const u32 version = rd_le32 (data + 4);
	const u32 file_size = rd_le32 (data + 8);
	// data+12 is the byte-order-mark field, unused for the little-endian NX variant we target
	const u32 name_length = rd_le32 (data + 16);

	if ((u64)20 + name_length + 8 > size)
		return ERROR0 (ERR_INVALID_DATA, "SHARC: truncated archive name\n");

	char name[256];
	copy_name (name, sizeof (name), data + 20, name_length, end);

	const u8 *p = data + 20 + name_length;
	const u8 *pos = p; // offsets below are relative to here, per the reference implementation
	const u32 source_array_offset = rd_le32 (p);
	const u32 program_count = rd_le32 (p + 4);
	p += 8;

	fprintf (out, "#SHARC\n"
		"version = %u\n"
		"file_size = %u\n"
		"name = %s\n"
		"program_count = %u\n\n"
		"[programs]\n",
		version, file_size, name, program_count);

	// header size before the 'Name' field of a ShaderProgram entry differs by version: pre-13
	// has 3 x s32 (vertex/fragment/geometry shader index), 13+ adds 5 x u16 on top.
	const uint program_prefix = version >= 13 ? 12 + 10 : 12;

	for (u32 i = 0; i < program_count; i++)
	{
		if (p + 8 > end)
			return ERROR0 (ERR_INVALID_DATA, "SHARC: program table runs past end of file\n");

		const u8 *entry = p;
		const u32 section_size = rd_le32 (entry);
		const u32 nlen = rd_le32 (entry + 4);

		if (!section_size || (u64)(entry - data) + section_size > size)
			return ERROR0 (ERR_INVALID_DATA, "SHARC: program %u has an invalid section size\n", i);

		const u8 *name_p = entry + 8 + program_prefix;
		char pname[256];
		copy_name (pname, sizeof (pname), name_p, nlen, entry + section_size);
		fprintf (out, "  [%u] %s\n", i, pname);

		p = entry + section_size;
	}

	if ((u64)(pos - data) + source_array_offset + 8 > size)
		return ERROR0 (ERR_INVALID_DATA, "SHARC: source table offset out of bounds\n");

	p = pos + source_array_offset;
	const u32 source_file_count = rd_le32 (p + 4);
	p += 8;

	fprintf (out, "\nsource_file_count = %u\n\n[sources]\n", source_file_count);

	for (u32 i = 0; i < source_file_count; i++)
	{
		if (p + 16 > end)
			return ERROR0 (ERR_INVALID_DATA, "SHARC: source table runs past end of file\n");

		const u8 *entry = p;
		const u32 section_size = rd_le32 (entry);
		const u32 fname_len = rd_le32 (entry + 4);
		const u32 code_len = rd_le32 (entry + 8);

		if (!section_size || (u64)(entry - data) + section_size > size)
			return ERROR0 (ERR_INVALID_DATA, "SHARC: source %u has an invalid section size\n", i);

		char fname[256];
		copy_name (fname, sizeof (fname), entry + 16, fname_len, entry + section_size);
		fprintf (out, "  [%u] %s (%u bytes)\n", i, fname, code_len);

		p = entry + section_size;
	}

	return ERR_OK;
}

enumError DecodeSHARCFB_Text (FILE *out, const u8 *data, size_t size)
{
	if (!out || !IsSHARCFB (data, size))
		return ERR_INVALID_DATA;
	if (size < 24)
		return ERROR0 (ERR_INVALID_DATA, "SHARCFB: file shorter than the fixed header\n");

	// SHARCFB is big-endian on the Wii U variant; verified header fields against
	// Switch-Toolbox Shader/SHARC/SHARCFB.cs Header.Read() up to the byte-order-mark switch.
	const u32 version = rd_be32 (data + 4);
	const u32 file_size = rd_be32 (data + 8);
	const u32 bom = rd_be32 (data + 12);
	const bool is_le = bom != 1;
	const u32 name_length = is_le ? rd_le32 (data + 20) : rd_be32 (data + 20);

	fprintf (out, "#SHARCFB\n"
		"version = %u\n"
		"file_size = %u\n"
		"byte_order = %s\n",
		version, file_size, is_le ? "little" : "big");

	// name_length == 4096/8192 flags the Switch (NX) variant, which replaces the rest of the
	// header with a different, undocumented-from-source layout (SHARCFBNX.cs uses a private
	// closed-source parser); we stop here rather than misreading it as a name length.
	if (name_length == 4096 || name_length == 8192)
	{
		fprintf (out, "variant = NX (compiled-binary layout not decoded)\n");
		return ERR_OK;
	}

	if ((u64)24 + name_length > size)
		return ERROR0 (ERR_INVALID_DATA, "SHARCFB: truncated archive name\n");

	char name[256];
	copy_name (name, sizeof (name), data + 24, name_length, data + size);
	fprintf (out, "name = %s\n", name);

	return ERR_OK;
}
