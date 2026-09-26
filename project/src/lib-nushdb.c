#include "lib-nushdb.h"
#include "lib-std.h"

// SSBH SHDR (.nushdb), little-endian. Reference: ultimate-research/ssbh_lib
// ssbh_lib/src/{lib.rs,arrays.rs,strings.rs,formats/shdr.rs}, verified against
// the same container conventions this codebase already uses for NUMSHB
// (IsSSBH() in lib-numsh.c): every pointer is a 64-bit offset relative to the
// field that holds it.
//
//   0x00  "HBSS" (or "SSBH"), then a u64 at 0x04 (0x40 on every retail file)
//   0x10  "RDHS" ("SHDR" byte-reversed, or literal "SHDR"), u16 major, u16 minor
//   0x18  shaders: SsbhArray<Shader> -- one relative offset (u64) + count (u64)
//
// A Shader entry is 0x38 bytes, relative to the array's own element base:
//
//   0x00  name:          SsbhString (u64 relative offset to a NUL-terminated,
//                        4-byte-aligned string)
//   0x08  shader_stage:  u32 (0 vertex, 3 geometry, 4 fragment, 5 compute)
//   0x0c  unk3:          u32 (always 2 per the reference)
//   0x10  shader_binary: SsbhByteBuffer (u64 relative offset + u64 count) --
//                        the compiled NVN GPU binary plus its own metadata
//                        (uniform/buffer/texture/attribute bindings). This is
//                        proprietary machine code with no public spec, so --
//                        same scope as DecodeBNSH_Text -- it isn't decoded
//                        further, only exposed as an offset+size.
//   0x20  binary_size:   u64 (duplicate of the SsbhByteBuffer's own count)
//   0x28  -- 16 bytes padding --

#define NUSHDB_SUBHDR_OFF 0x10
#define NUSHDB_SHADER_SIZE 0x38
#define NUSHDB_MAX_SHADERS 65536

static const ccp nushdb_stage_name[6]
	= { "vertex", "unk1", "unk2", "geometry", "fragment", "compute" };

bool IsNUSHDB (const u8 *data, size_t size)
{
	if (!data || size < NUSHDB_SUBHDR_OFF + 8)
		return false;
	if (memcmp (data, "HBSS", 4) && memcmp (data, "SSBH", 4))
		return false;
	return !memcmp (data + NUSHDB_SUBHDR_OFF, "RDHS", 4)
		|| !memcmp (data + NUSHDB_SUBHDR_OFF, "SHDR", 4);
}

// Reads a NUL-terminated string at the relative offset stored at 'field_off' (an
// SsbhString field), bounds-checked against 'size'. Leaves 'dest' empty and returns
// false on a null or out-of-range offset instead of aborting the whole decode.
static bool read_ssbh_string (char *dest, uint destsz, const u8 *data, size_t size, u64 field_off)
{
	dest[0] = 0;
	if (field_off + 8 > size)
		return false;
	const u64 rel = rd_le64 (data + field_off);
	if (!rel)
		return false;
	const u64 str_off = field_off + rel;
	if (str_off < field_off || str_off >= size) // 64-bit wrap guard + bounds
		return false;

	const u8 *p = data + str_off;
	const size_t max_len = size - str_off;
	size_t len = 0;
	while (len < max_len && p[len])
		len++;
	const uint n = len < destsz - 1 ? (uint)len : destsz - 1;
	memcpy (dest, p, n);
	dest[n] = 0;
	return true;
}

enumError DecodeNUSHDB_Text (FILE *out, const u8 *data, size_t size)
{
	if (!out || !IsNUSHDB (data, size))
		return ERR_INVALID_DATA;

	const u16 major = rd_le16 (data + NUSHDB_SUBHDR_OFF + 4);
	const u16 minor = rd_le16 (data + NUSHDB_SUBHDR_OFF + 6);

	const u64 array_field_off = NUSHDB_SUBHDR_OFF + 8;
	if (array_field_off + 16 > size)
		return ERROR0 (ERR_INVALID_DATA, "NUSHDB: file shorter than the fixed header\n");

	const u64 array_rel = rd_le64 (data + array_field_off);
	const u64 shader_count = rd_le64 (data + array_field_off + 8);

	fprintf (out,
		"#NUSHDB\n"
		"version = %u.%u\n"
		"shader_count = %llu\n\n"
		"[shaders]\n",
		major, minor, (unsigned long long)shader_count);

	if (!array_rel || shader_count > NUSHDB_MAX_SHADERS)
	{
		fprintf (out, "  <no shader array>\n");
		return ERR_OK;
	}

	const u64 array_base = array_field_off + array_rel;
	if (array_base < array_field_off) // 64-bit wrap guard
		return ERROR0 (ERR_INVALID_DATA, "NUSHDB: shader array offset overflow\n");

	for (u64 i = 0; i < shader_count; i++)
	{
		const u64 entry_off = array_base + i * NUSHDB_SHADER_SIZE;
		if (entry_off < array_base || entry_off + NUSHDB_SHADER_SIZE > size)
		{
			fprintf (out, "  [%llu] <entry out of bounds>\n", (unsigned long long)i);
			break;
		}

		char name[256];
		read_ssbh_string (name, sizeof (name), data, size, entry_off);
		const u32 stage = rd_le32 (data + entry_off + 8);
		const u64 blob_rel = rd_le64 (data + entry_off + 0x10);
		const u64 blob_count = rd_le64 (data + entry_off + 0x18);
		const u64 binary_size = rd_le64 (data + entry_off + 0x20);

		fprintf (out,
			"  [%llu] %s\n"
			"    stage = %u (%s)\n"
			"    binary_size = %llu\n",
			(unsigned long long)i, name[0] ? name : "<unnamed>", stage,
			stage < 6 ? nushdb_stage_name[stage] : "?", (unsigned long long)binary_size);

		if (!blob_rel)
		{
			fprintf (out, "    binary: <none>\n");
			continue;
		}
		const u64 blob_field_off = entry_off + 0x10;
		const u64 blob_off = blob_field_off + blob_rel;
		if (blob_off < blob_field_off || blob_off + blob_count > size)
			fprintf (out, "    binary: <out of bounds>\n");
		else
			fprintf (out, "    binary: offset = %llu, size = %llu\n", (unsigned long long)blob_off,
				(unsigned long long)blob_count);
	}

	return ERR_OK;
}
