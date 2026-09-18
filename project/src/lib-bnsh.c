#include "lib-bnsh.h"
#include "lib-std.h"

// BNSH, little-endian, verified against Switch-Toolbox
// File_Format_Library/FileFormats/Shader/BNSH.cs Header.Read()/ShaderVariation.Read()/
// ShaderProgram.Read()/ShaderData.Read():
//
//   char magic[4];             // "BNSH"
//   u32  padding;
//   u32  version;              // byte-packed major.major2.minor.minor2
//   u16  bom;
//   u8   alignment;
//   u8   target;
//   u32  filename_ptr;         // offset of a u32-length-prefixed string
//   u32  path_offset;
//   u32  reloc_table_offset;
//   u32  file_size;
//   -- seek to absolute 0x60 (fixed padding) --
//   char grsc_magic[4];        // "grsc"
//   u32  block_offset;
//   u64  block_size;
//   -- 12 bytes reserved --
//   u32  variation_count;
//   u32  variation_offset;     // absolute
//
// Each variation entry (64 bytes, at variation_offset + i*64):
//   s64  source_program_offset;
//   s64  unk2;
//   s64  shader_program_offset;
//   s64  grsc_offset;          // back-pointer, unused here
//   -- 32 bytes reserved --
// The reference sums shader_program_offset + source_program_offset to find the
// ShaderProgram record; we mirror that literally rather than guessing a "fix".
//
// ShaderProgram record (absolute base):
//   u8   shader_type;
//   u8   format;               // 3 == source-text variant (ShaderSourceData)
//   -- 6 bytes padding --
//   s64  stage_offset[6];      // vertex, unk(tess-ctrl), unk2(tess-eval), geometry,
//                               // fragment, compute -- each an absolute file offset, 0 if absent
//
// format == 3 (ShaderSourceData) per stage:
//   u16  code_count;
//   -- 6 bytes padding --
//   s64  size_array_offset;
//   s64  offset_array_offset;
//   -- 8 bytes padding --
//   then code_count x { s64 offset } at offset_array_offset,
//        code_count x { u32 size }   at size_array_offset
//
// otherwise (ShaderData, compiled binary) per stage:
//   -- 8 bytes padding --
//   s64  shader_offset;
//   s64  shader_offset2;
//   s32  shader_size;
//   s32  shader_size2;
//   -- 32 bytes padding --
//   -- two blobs: (shader_offset, shader_size2) and (shader_offset2, shader_size) --

#define BNSH_HDR_SIZE 0x84
#define BNSH_VARIATION_SIZE 0x40
#define BNSH_MAX_CODE_ENTRIES 4096

static const ccp bnsh_stage_name[6] = {
	"vertex", "tess_control(unk)", "tess_eval(unk2)", "geometry", "fragment", "compute"
};

bool IsBNSH (const u8 *data, size_t size)
{
	return data && size >= 4 && !memcmp (data, "BNSH", 4);
}

// Reads a u32-length-prefixed string at 'off', bounds-checked against 'size'. Returns false
// (leaving 'dest' empty) if the pointer or length is out of range instead of aborting the
// whole decode -- the filename is informational only.
static bool read_pstring (char *dest, uint destsz, const u8 *data, size_t size, u64 off)
{
	dest[0] = 0;
	if (!off || (u64)off + 4 > size)
		return false;
	const u32 len = rd_le32 (data + off);
	if ((u64)off + 4 + len > size)
		return false;
	uint n = len < destsz - 1 ? len : destsz - 1;
	memcpy (dest, data + off + 4, n);
	dest[n] = 0;
	return true;
}

// Reads a Switch-family ResDict string (u16 length + characters)
static bool bnsh_read_string (char *dest, uint destsz, const u8 *data, size_t size, u64 ptr)
{
	dest[0] = 0;
	if (!ptr || (u64)ptr + 2 > size)
		return false;
	const u64 str_off = ptr + 2;
	uint n = 0;
	while (str_off + n < size && n < destsz - 1 && data[str_off + n])
		n++;
	memcpy (dest, data + str_off, n);
	dest[n] = 0;
	return true;
}

#define BNSH_MAX_DICT_NODES 4096

static void decode_bnsh_resdict (FILE *out, const u8 *data, size_t size, u64 dict_off,
	const s32 *slots, uint slot_count, uint slot_start_idx, ccp dict_name, ccp indent)
{
	if (!dict_off || (u64)dict_off + 8 > size)
		return;

	const s32 num_nodes_signed = (s32)rd_le32 (data + dict_off + 4);
	if (num_nodes_signed <= 0 || num_nodes_signed > BNSH_MAX_DICT_NODES)
		return;

	const u64 num_nodes = (u64)num_nodes_signed;
	const u64 nodes_base = dict_off + 8;
	if (nodes_base + (num_nodes + 1) * 16 > size)
		return;

	fprintf (out, "%s%s:\n", indent, dict_name);
	for (u64 i = 0; i < num_nodes; i++)
	{
		const u64 node_off = nodes_base + (i + 1) * 16;
		const u64 key_ptr = rd_le64 (data + node_off + 8);
		char name[256];
		bnsh_read_string (name, sizeof (name), data, size, key_ptr);
		fprintf (out, "%s  [%llu] %s", indent, (unsigned long long)i, name[0] ? name : "<unnamed>");
		if (slots && (slot_start_idx + i) < slot_count)
		{
			fprintf (out, " (slot %d)", slots[slot_start_idx + i]);
		}
		fprintf (out, "\n");
	}
}

static void decode_bnsh_reflection (FILE *out, const u8 *data, size_t size, u64 refl_stage_off,
	ccp stage_name, ccp indent)
{
	if (!refl_stage_off || refl_stage_off + 0x48 > size)
		return;

	const u64 in_dict   = rd_le64 (data + refl_stage_off + 0x00);
	const u64 out_dict  = rd_le64 (data + refl_stage_off + 0x08);
	const u64 samp_dict = rd_le64 (data + refl_stage_off + 0x10);
	const u64 ubo_dict  = rd_le64 (data + refl_stage_off + 0x18);
	const u64 ssbo_dict = rd_le64 (data + refl_stage_off + 0x20);

	const s32 out_idx   = (s32)rd_le32 (data + refl_stage_off + 0x28);
	const s32 samp_idx  = (s32)rd_le32 (data + refl_stage_off + 0x2c);
	const s32 ubo_idx   = (s32)rd_le32 (data + refl_stage_off + 0x30);
	const s32 ssbo_idx  = (s32)rd_le32 (data + refl_stage_off + 0x34);

	const u32 slot_off   = rd_le32 (data + refl_stage_off + 0x38);
	const s32 slot_count = (s32)rd_le32 (data + refl_stage_off + 0x48);

	s32 *slots = NULL;
	if (slot_count > 0 && slot_count <= 8192 && (u64)slot_off + (u64)slot_count * 4 <= size)
	{
		slots = CALLOC (slot_count, sizeof (s32));
		if (slots)
		{
			for (int k = 0; k < slot_count; k++)
				slots[k] = (s32)rd_le32 (data + slot_off + (u64)k * 4);
		}
	}

	fprintf (out, "%sreflection:\n", indent);
	char sub_indent[128];
	snprintf (sub_indent, sizeof (sub_indent), "%s  ", indent);

	if (in_dict)
		decode_bnsh_resdict (out, data, size, in_dict, slots, (uint)(slots ? slot_count : 0),
			0, "inputs", sub_indent);
	if (out_dict)
		decode_bnsh_resdict (out, data, size, out_dict, slots, (uint)(slots ? slot_count : 0),
			(uint)(out_idx >= 0 ? out_idx : 0), "outputs", sub_indent);
	if (samp_dict)
		decode_bnsh_resdict (out, data, size, samp_dict, slots, (uint)(slots ? slot_count : 0),
			(uint)(samp_idx >= 0 ? samp_idx : 0), "samplers", sub_indent);
	if (ubo_dict)
		decode_bnsh_resdict (out, data, size, ubo_dict, slots, (uint)(slots ? slot_count : 0),
			(uint)(ubo_idx >= 0 ? ubo_idx : 0), "uniform_buffers", sub_indent);
	if (ssbo_dict)
		decode_bnsh_resdict (out, data, size, ssbo_dict, slots, (uint)(slots ? slot_count : 0),
			(uint)(ssbo_idx >= 0 ? ssbo_idx : 0), "storage_buffers", sub_indent);

	FREE (slots);
}

enumError DecodeBNSH_Text (FILE *out, const u8 *data, size_t size)
{
	if (!out || !IsBNSH (data, size))
		return ERR_INVALID_DATA;
	if (size < BNSH_HDR_SIZE)
		return ERROR0 (ERR_INVALID_DATA, "BNSH: file shorter than the fixed header\n");

	const u32 version = rd_le32 (data + 8);
	const u16 bom = rd_le16 (data + 12);
	const u8 alignment = data[14];
	const u8 target = data[15];
	const u32 filename_ptr = rd_le32 (data + 16);
	const u32 path_offset = rd_le32 (data + 20);
	const u32 file_size = rd_le32 (data + 28);

	if (memcmp (data + 0x60, "grsc", 4))
		return ERROR0 (ERR_INVALID_DATA, "BNSH: missing 'grsc' block signature\n");
	const u32 block_offset = rd_le32 (data + 0x64);
	const u64 block_size = rd_le64 (data + 0x68);
	const u32 variation_count = rd_le32 (data + 0x7c);
	const u32 variation_offset = rd_le32 (data + 0x80);

	char filename[256];
	read_pstring (filename, sizeof (filename), data, size, filename_ptr);

	fprintf (out, "#BNSH\n"
		"version = %u.%u.%u.%u\n"
		"byte_order_mark = 0x%04x\n"
		"alignment = %u\n"
		"target = %u\n"
		"filename = %s\n"
		"path_offset = %u\n"
		"file_size = %u\n"
		"grsc.block_offset = %u\n"
		"grsc.block_size = %llu\n"
		"variation_count = %u\n\n"
		"[variations]\n",
		version >> 24, version >> 16 & 0xff, version >> 8 & 0xff, version & 0xff,
		bom, alignment, target, filename, path_offset, file_size,
		block_offset, (unsigned long long)block_size, variation_count);

	for (u32 v = 0; v < variation_count; v++)
	{
		const u64 entry_off = (u64)variation_offset + (u64)v * BNSH_VARIATION_SIZE;
		if (entry_off + BNSH_VARIATION_SIZE > size)
		{
			fprintf (out, "  [%u] <variation table entry out of bounds>\n", v);
			break;
		}
		const u8 *e = data + entry_off;
		const s64 source_program_offset = (s64)rd_le64 (e);
		const s64 shader_program_offset = (s64)rd_le64 (e + 16);

		fprintf (out, "  [%u]\n", v);

		// Mirrors the reference's TemporarySeek(ShaderProgramOffset + SourceProgramOffset, ...).
		const s64 base_s = shader_program_offset + source_program_offset;
		if (base_s < 0 || (u64)base_s + 8 + 6 * 8 > size)
		{
			fprintf (out, "    <shader program offset out of bounds>\n");
			continue;
		}
		const u64 base = (u64)base_s;
		const u8 shader_type = data[base];
		const u8 format = data[base + 1];
		fprintf (out, "    shader_type = %u\n    format = %u\n", shader_type, format);

		for (uint s = 0; s < 6; s++)
		{
			const s64 stage_off_s = (s64)rd_le64 (data + base + 8 + (u64)s * 8);
			if (!stage_off_s)
				continue;
			if (stage_off_s < 0)
			{
				fprintf (out, "    %s: <negative offset>\n", bnsh_stage_name[s]);
				continue;
			}
			const u64 stage_off = (u64)stage_off_s;

			if (format == 3)
			{
				// ShaderSourceData: an array of (offset,size) pairs pointing at shift-jis text.
				if (stage_off + 32 > size)
				{
					fprintf (out, "    %s: <source header out of bounds>\n", bnsh_stage_name[s]);
					continue;
				}
				const u16 code_count = rd_le16 (data + stage_off);
				const u64 size_array = rd_le64 (data + stage_off + 8);
				const u64 offset_array = rd_le64 (data + stage_off + 16);
				fprintf (out, "    %s: source, code_count = %u\n", bnsh_stage_name[s], code_count);

				const uint n = code_count > BNSH_MAX_CODE_ENTRIES ? BNSH_MAX_CODE_ENTRIES : code_count;
				if (n < code_count)
					fprintf (out, "      <code_count truncated for listing>\n");

				if (offset_array + (u64)n * 8 > size || size_array + (u64)n * 4 > size)
				{
					fprintf (out, "      <code offset/size array out of bounds>\n");
					continue;
				}
				for (uint i = 0; i < n; i++)
				{
					const s64 code_off = (s64)rd_le64 (data + offset_array + (u64)i * 8);
					const u32 code_len = rd_le32 (data + size_array + (u64)i * 4);
					if (code_off < 0 || (u64)code_off + code_len > size)
						fprintf (out, "      [%u] <out of bounds>\n", i);
					else
						fprintf (out, "      [%u] offset = %lld, size = %u\n", i,
							(long long)code_off, code_len);
				}
			}
			else
			{
				// ShaderData: two raw GPU-binary blobs.
				if (stage_off + 64 > size)
				{
					fprintf (out, "    %s: <binary header out of bounds>\n", bnsh_stage_name[s]);
					continue;
				}
				const s64 shader_offset = (s64)rd_le64 (data + stage_off + 8);
				const s64 shader_offset2 = (s64)rd_le64 (data + stage_off + 16);
				const u32 shader_size = rd_le32 (data + stage_off + 24);
				const u32 shader_size2 = rd_le32 (data + stage_off + 28);

				fprintf (out, "    %s: binary\n", bnsh_stage_name[s]);
				if (shader_offset < 0 || (u64)shader_offset + shader_size2 > size)
					fprintf (out, "      blob0: <out of bounds>\n");
				else
					fprintf (out, "      blob0: offset = %lld, size = %u\n",
						(long long)shader_offset, shader_size2);
				if (shader_offset2 < 0 || (u64)shader_offset2 + shader_size > size)
					fprintf (out, "      blob1: <out of bounds>\n");
				else
					fprintf (out, "      blob1: offset = %lld, size = %u\n",
						(long long)shader_offset2, shader_size);
			}
		}

		// ShaderReflectionOffset at base + 120 (0x78)
		if (base + 128 <= size)
		{
			const u64 refl_off = rd_le64 (data + base + 120);
			if (refl_off && refl_off + 6 * 8 <= size)
			{
				for (uint s = 0; s < 6; s++)
				{
					const u64 stage_refl_off = rd_le64 (data + refl_off + (u64)s * 8);
					if (stage_refl_off)
					{
						fprintf (out, "    %s ", bnsh_stage_name[s]);
						decode_bnsh_reflection (out, data, size, stage_refl_off,
							bnsh_stage_name[s], "      ");
					}
				}
			}
		}
	}

	return ERR_OK;
}

