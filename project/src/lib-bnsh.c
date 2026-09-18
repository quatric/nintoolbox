#include "lib-bnsh.h"
#include "lib-std.h"

// BNSH, little-endian, verified against Switch-Toolbox
// File_Format_Library/FileFormats/Shader/BNSH.cs Header.Read()/ShaderVariation.Read()/
// ShaderProgram.Read()/ShaderData.Read(), plus KillzXGaming/BinaryShaderLibrary
// (BNSH/BnshFile.cs, ShaderVariation/, ShaderModels/, GFX/Enums.cs) for the parts
// Switch-Toolbox leaves implicit:
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
//   s64  source_program_offset;  // BinaryShaderLibrary: SourceProgram
//   s64  unk2;
//   s64  shader_program_offset;  // BinaryShaderLibrary: BinaryProgram
//   s64  grsc_offset;            // back-pointer (BinaryShaderLibrary: parentOffset)
//   -- 32 bytes reserved --
// Switch-Toolbox sums shader_program_offset + source_program_offset to find a single
// ShaderProgram record; BinaryShaderLibrary instead loads SourceProgram and BinaryProgram
// as two independent programs at those two offsets. Real files in the wild match either
// convention depending on producer/version, so both are decoded here: when both offsets
// are non-zero and each points at a plausible program header they are listed separately
// as source_program / binary_program, otherwise the summed base is used as before.
//
// ShaderProgram record (absolute base), cf. BinaryShaderLibrary ShaderProgram.cs /
// ShaderInfoData.cs:
//   u8   type;                 // BinaryShaderLibrary ShaderInfoData.Type
//   u8   format;               // 0 == binary, 3 == source-text (ShaderSourceData)
//   -- 2 bytes padding --
//   u32  compression;          // 0 == none, 1 == zlib (BinaryShaderLibrary CompressionType)
//   s64  stage_offset[6];      // vertex, tess-ctrl, tess-eval, geometry,
//                               // fragment, compute -- each an absolute file offset, 0 if absent
//   -- 40 bytes padding --     // (end of ShaderInfoData, total 96 bytes)
//   u32  memory_size;          // BinaryShaderLibrary ShaderProgram.MemoryData length
//   -- 4 bytes padding --
//   s64  memory_offset;        // absolute offset of MemoryData
//   s64  parent_offset;        // back-pointer to owning variation
//   s64  reflection_offset;    // absolute offset of 6x stage reflection offsets
//   -- 32 bytes reserved --
//
// Per-stage payload selection mirrors BinaryShaderLibrary ShaderInfoData.ReadShaderCode():
// if compression == 1 the stage is ShaderCodeDataCompressed, else if format == 3 it is
// ShaderSourceData, else ShaderData (compiled binary):
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
// compression == 1 (ShaderCodeDataCompressed) per stage:
//   u32  compressed_size;
//   u32  decompressed_size;
//   s64  code_offset;          // absolute offset of the zlib blob
//
// otherwise (ShaderData, compiled binary) per stage:
//   -- 8 bytes padding --
//   s64  shader_offset;
//   s64  shader_offset2;
//   s32  shader_size;
//   s32  shader_size2;
//   -- 32 bytes padding --
//   -- two blobs: (shader_offset, shader_size2) and (shader_offset2, shader_size) --
//
// Reflection (at reflection_offset, 6x s64 stage offsets) follows Switch-Toolbox's
// slot-based layout; BinaryShaderLibrary's older ShaderReflectionData layout instead
// carries 5 ResDicts (inputs/outputs/samplers/constants/unknown) plus 11 s32 unknowns.
// Both are reported: the slot tables when present, plus the raw unknown words so the
// older layout's constants/unknown dict indices are not silently dropped.

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

	// BinaryShaderLibrary's older ShaderReflectionData layout carries the same 5 dicts
	// (there named inputs/outputs/samplers/constants/unknown) followed by 11 s32 unknowns
	// and 8 bytes padding (total 92 bytes). The slot-based layout above only names the
	// first 4 index words + slot table; dump the full 11-word tail so the older layout's
	// constants/unknown indices are visible instead of silently dropped. When the stage
	// is really slot-based these extra words are just the slot offset/count plus padding
	// and print harmlessly as small integers.
	if (refl_stage_off + 92 <= size)
	{
		bool any_unknown = false;
		for (uint k = 5; k < 11; k++)
		{
			const s32 w = (s32)rd_le32 (data + refl_stage_off + 0x28 + (u64)k * 4);
			if (w)
			{
				any_unknown = true;
				break;
			}
		}
		if (any_unknown)
		{
			fprintf (out, "%s  unknowns =", sub_indent);
			for (uint k = 0; k < 11; k++)
				fprintf (out, " %d", (s32)rd_le32 (data + refl_stage_off + 0x28 + (u64)k * 4));
			fprintf (out, "\n");
		}
	}

	FREE (slots);
}

// One ShaderProgram's stages + memory + reflection, shared by the summed-layout and the
// BinaryShaderLibrary dual-program paths. 'label' is "shader_program", "source_program"
// or "binary_program" and only affects the manifest's section headers.
static void decode_bnsh_program (FILE *out, const u8 *data, size_t size, u64 base,
	ccp label)
{
	if (base + 8 + 6 * 8 > size)
	{
		fprintf (out, "    %s: <shader program offset out of bounds>\n", label);
		return;
	}
	const u8 shader_type = data[base];
	const u8 format = data[base + 1];
	const u32 compression = (base + 8 <= size) ? rd_le32 (data + base + 4) : 0;
	fprintf (out, "    %s: shader_type = %u, format = %u, compression = %u%s\n",
		label, shader_type, format, compression,
		compression == 1 ? " (zlib)" : compression ? " (unknown)" : "");

	for (uint s = 0; s < 6; s++)
	{
		const s64 stage_off_s = (s64)rd_le64 (data + base + 8 + (u64)s * 8);
		if (!stage_off_s)
			continue;
		if (stage_off_s < 0)
		{
			fprintf (out, "      %s: <negative offset>\n", bnsh_stage_name[s]);
			continue;
		}
		const u64 stage_off = (u64)stage_off_s;

		if (compression == 1)
		{
			// ShaderCodeDataCompressed: zlib blob with its decompressed size inline.
			if (stage_off + 16 > size)
			{
				fprintf (out, "      %s: <compressed header out of bounds>\n", bnsh_stage_name[s]);
				continue;
			}
			const u32 comp_size = rd_le32 (data + stage_off);
			const u32 decomp_size = rd_le32 (data + stage_off + 4);
			const s64 code_ptr_s = (s64)rd_le64 (data + stage_off + 8);
			fprintf (out, "      %s: compressed, size = %u, decompressed_size = %u\n",
				bnsh_stage_name[s], comp_size, decomp_size);
			if (code_ptr_s < 0 || (u64)code_ptr_s + comp_size > size)
				fprintf (out, "        blob: <out of bounds>\n");
			else
				fprintf (out, "        blob: offset = %lld, size = %u\n",
					(long long)code_ptr_s, comp_size);
		}
		else if (format == 3)
		{
			// ShaderSourceData: an array of (offset,size) pairs pointing at text.
			if (stage_off + 32 > size)
			{
				fprintf (out, "      %s: <source header out of bounds>\n", bnsh_stage_name[s]);
				continue;
			}
			const u16 code_count = rd_le16 (data + stage_off);
			const u64 size_array = rd_le64 (data + stage_off + 8);
			const u64 offset_array = rd_le64 (data + stage_off + 16);
			fprintf (out, "      %s: source, code_count = %u\n", bnsh_stage_name[s], code_count);

			const uint n = code_count > BNSH_MAX_CODE_ENTRIES ? BNSH_MAX_CODE_ENTRIES : code_count;
			if (n < code_count)
				fprintf (out, "        <code_count truncated for listing>\n");

			if (offset_array + (u64)n * 8 > size || size_array + (u64)n * 4 > size)
			{
				fprintf (out, "        <code offset/size array out of bounds>\n");
				continue;
			}
			for (uint i = 0; i < n; i++)
			{
				const s64 code_off = (s64)rd_le64 (data + offset_array + (u64)i * 8);
				const u32 code_len = rd_le32 (data + size_array + (u64)i * 4);
				if (code_off < 0 || (u64)code_off + code_len > size)
					fprintf (out, "        [%u] <out of bounds>\n", i);
				else
					fprintf (out, "        [%u] offset = %lld, size = %u\n", i,
						(long long)code_off, code_len);
			}
		}
		else
		{
			// ShaderData: two raw GPU-binary blobs.
			if (stage_off + 64 > size)
			{
				fprintf (out, "      %s: <binary header out of bounds>\n", bnsh_stage_name[s]);
				continue;
			}
			const s64 shader_offset = (s64)rd_le64 (data + stage_off + 8);
			const s64 shader_offset2 = (s64)rd_le64 (data + stage_off + 16);
			const u32 shader_size = rd_le32 (data + stage_off + 24);
			const u32 shader_size2 = rd_le32 (data + stage_off + 28);

			fprintf (out, "      %s: binary\n", bnsh_stage_name[s]);
			if (shader_offset < 0 || (u64)shader_offset + shader_size2 > size)
				fprintf (out, "        blob0: <out of bounds>\n");
			else
				fprintf (out, "        blob0: offset = %lld, size = %u\n",
					(long long)shader_offset, shader_size2);
			if (shader_offset2 < 0 || (u64)shader_offset2 + shader_size > size)
				fprintf (out, "        blob1: <out of bounds>\n");
			else
				fprintf (out, "        blob1: offset = %lld, size = %u\n",
					(long long)shader_offset2, shader_size);
		}
	}

	// ShaderProgram.MemoryData (BinaryShaderLibrary): a variable-length scratch buffer
	// whose size/offset live right after the 96-byte ShaderInfoData.
	if (base + 112 <= size)
	{
		const u32 mem_size = rd_le32 (data + base + 96);
		const s64 mem_off_s = (s64)rd_le64 (data + base + 104);
		if (mem_size && mem_off_s)
		{
			if (mem_off_s < 0 || (u64)mem_off_s + mem_size > size)
				fprintf (out, "      memory: size = %u <out of bounds>\n", mem_size);
			else
				fprintf (out, "      memory: offset = %lld, size = %u\n",
					(long long)mem_off_s, mem_size);
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
					fprintf (out, "      %s ", bnsh_stage_name[s]);
					decode_bnsh_reflection (out, data, size, stage_refl_off,
						bnsh_stage_name[s], "        ");
				}
			}
		}
	}
}

// A program header is plausible if its type/format/compression fields are in range and
// at least its stage-offset table lies inside the file. Used to decide whether a
// variation's two offsets are independent programs (BinaryShaderLibrary) or addends of
// a single summed base (Switch-Toolbox).
static bool bnsh_program_plausible (const u8 *data, size_t size, s64 off)
{
	if (off <= 0 || (u64)off + 8 + 6 * 8 > size)
		return false;
	const u8 fmt = data[(u64)off + 1];
	const u32 comp = rd_le32 (data + (u64)off + 4);
	if (fmt > 3 || comp > 1)
		return false;
	return true;
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
		const s64 unk2 = (s64)rd_le64 (e + 8);
		const s64 shader_program_offset = (s64)rd_le64 (e + 16);
		const s64 parent_offset = (s64)rd_le64 (e + 24);

		fprintf (out, "  [%u] source_program_offset = %lld, binary_program_offset = %lld",
			v, (long long)source_program_offset, (long long)shader_program_offset);
		if (unk2 || parent_offset)
			fprintf (out, ", unk2 = %lld, parent_offset = %lld",
				(long long)unk2, (long long)parent_offset);
		fprintf (out, "\n");

		// BinaryShaderLibrary loads SourceProgram and BinaryProgram independently;
		// Switch-Toolbox uses their sum as a single base. Decode independently when
		// both offsets are plausible program headers, otherwise fall back to the sum.
		const bool src_ok = bnsh_program_plausible (data, size, source_program_offset);
		const bool bin_ok = bnsh_program_plausible (data, size, shader_program_offset);
		if (src_ok && bin_ok
			&& source_program_offset != shader_program_offset
			&& source_program_offset + shader_program_offset != source_program_offset
			&& source_program_offset + shader_program_offset != shader_program_offset)
		{
			decode_bnsh_program (out, data, size, (u64)source_program_offset, "source_program");
			decode_bnsh_program (out, data, size, (u64)shader_program_offset, "binary_program");
			continue;
		}
		if (src_ok && !bin_ok && shader_program_offset == 0)
		{
			decode_bnsh_program (out, data, size, (u64)source_program_offset, "shader_program");
			continue;
		}
		if (bin_ok && !src_ok && source_program_offset == 0)
		{
			decode_bnsh_program (out, data, size, (u64)shader_program_offset, "shader_program");
			continue;
		}

		// Mirrors Switch-Toolbox's TemporarySeek(ShaderProgramOffset + SourceProgramOffset).
		const s64 base_s = shader_program_offset + source_program_offset;
		if (base_s < 0 || (u64)base_s + 8 + 6 * 8 > size)
		{
			fprintf (out, "    <shader program offset out of bounds>\n");
			continue;
		}
		decode_bnsh_program (out, data, size, (u64)base_s, "shader_program");
	}

	return ERR_OK;
}

// One extractable shader blob inside a BNSH file, for the EXTRACT sidecar writer.
static void bnsh_collect_program_blobs (bnsh_blobs_t *blobs, const u8 *data, size_t size,
	u64 base, uint variation, ccp program_kind)
{
	if (!blobs || base + 8 + 6 * 8 > size)
		return;
	const u8 format = data[base + 1];
	const u32 compression = (base + 8 <= size) ? rd_le32 (data + base + 4) : 0;

	for (uint s = 0; s < 6; s++)
	{
		const s64 stage_off_s = (s64)rd_le64 (data + base + 8 + (u64)s * 8);
		if (stage_off_s <= 0)
			continue;
		const u64 stage_off = (u64)stage_off_s;

		if (compression == 1)
		{
			if (stage_off + 16 > size)
				continue;
			const u32 comp_size = rd_le32 (data + stage_off);
			const u32 decomp_size = rd_le32 (data + stage_off + 4);
			const s64 code_ptr_s = (s64)rd_le64 (data + stage_off + 8);
			if (code_ptr_s <= 0 || (u64)code_ptr_s + comp_size > size || !comp_size)
				continue;
			bnsh_blob_t *b = REALLOC (blobs->blobs,
				(blobs->n_blobs + 1) * sizeof (*b));
			if (!b)
				return;
			blobs->blobs = b;
			b = blobs->blobs + blobs->n_blobs++;
			memset (b, 0, sizeof (*b));
			b->variation = variation;
			snprintf (b->program_kind, sizeof (b->program_kind), "%s", program_kind);
			snprintf (b->stage, sizeof (b->stage), "%s", bnsh_stage_name[s]);
			b->blob_index = 0;
			b->offset = (u64)code_ptr_s;
			b->size = comp_size;
			b->decompressed_size = decomp_size;
			b->is_compressed = true;
		}
		else if (format == 3)
		{
			if (stage_off + 32 > size)
				continue;
			const u16 code_count = rd_le16 (data + stage_off);
			const u64 size_array = rd_le64 (data + stage_off + 8);
			const u64 offset_array = rd_le64 (data + stage_off + 16);
			const uint n = code_count > BNSH_MAX_CODE_ENTRIES ? BNSH_MAX_CODE_ENTRIES : code_count;
			if (offset_array + (u64)n * 8 > size || size_array + (u64)n * 4 > size)
				continue;
			for (uint i = 0; i < n; i++)
			{
				const s64 code_off = (s64)rd_le64 (data + offset_array + (u64)i * 8);
				const u32 code_len = rd_le32 (data + size_array + (u64)i * 4);
				if (code_off <= 0 || (u64)code_off + code_len > size || !code_len)
					continue;
				bnsh_blob_t *b = REALLOC (blobs->blobs,
					(blobs->n_blobs + 1) * sizeof (*b));
				if (!b)
					return;
				blobs->blobs = b;
				b = blobs->blobs + blobs->n_blobs++;
				memset (b, 0, sizeof (*b));
				b->variation = variation;
				snprintf (b->program_kind, sizeof (b->program_kind), "%s", program_kind);
				snprintf (b->stage, sizeof (b->stage), "%s", bnsh_stage_name[s]);
				b->blob_index = i;
				b->offset = (u64)code_off;
				b->size = code_len;
				b->is_source = true;
			}
		}
		else
		{
			if (stage_off + 64 > size)
				continue;
			const s64 off0 = (s64)rd_le64 (data + stage_off + 8);
			const s64 off1 = (s64)rd_le64 (data + stage_off + 16);
			const u32 sz0 = rd_le32 (data + stage_off + 24);
			const u32 sz1 = rd_le32 (data + stage_off + 28);
			if (off0 > 0 && sz1 && (u64)off0 + sz1 <= size)
			{
				bnsh_blob_t *b = REALLOC (blobs->blobs,
					(blobs->n_blobs + 1) * sizeof (*b));
				if (!b)
					return;
				blobs->blobs = b;
				b = blobs->blobs + blobs->n_blobs++;
				memset (b, 0, sizeof (*b));
				b->variation = variation;
				snprintf (b->program_kind, sizeof (b->program_kind), "%s", program_kind);
				snprintf (b->stage, sizeof (b->stage), "%s", bnsh_stage_name[s]);
				b->blob_index = 0;
				b->offset = (u64)off0;
				b->size = sz1;
			}
			if (off1 > 0 && sz0 && (u64)off1 + sz0 <= size)
			{
				bnsh_blob_t *b = REALLOC (blobs->blobs,
					(blobs->n_blobs + 1) * sizeof (*b));
				if (!b)
					return;
				blobs->blobs = b;
				b = blobs->blobs + blobs->n_blobs++;
				memset (b, 0, sizeof (*b));
				b->variation = variation;
				snprintf (b->program_kind, sizeof (b->program_kind), "%s", program_kind);
				snprintf (b->stage, sizeof (b->stage), "%s", bnsh_stage_name[s]);
				b->blob_index = 1;
				b->offset = (u64)off1;
				b->size = sz0;
			}
		}
	}
}

enumError ScanBNSH_Blobs (bnsh_blobs_t *out, const u8 *data, size_t size)
{
	if (!out)
		return ERR_INVALID_DATA;
	out->n_blobs = 0;
	out->blobs = NULL;
	if (!IsBNSH (data, size) || size < BNSH_HDR_SIZE)
		return ERR_INVALID_DATA;
	if (memcmp (data + 0x60, "grsc", 4))
		return ERR_OK;
	const u32 variation_count = rd_le32 (data + 0x7c);
	const u32 variation_offset = rd_le32 (data + 0x80);
	if (!variation_count || variation_count > 4096 || !variation_offset)
		return ERR_OK;

	for (u32 v = 0; v < variation_count; v++)
	{
		const u64 entry_off = (u64)variation_offset + (u64)v * BNSH_VARIATION_SIZE;
		if (entry_off + BNSH_VARIATION_SIZE > size)
			break;
		const s64 src = (s64)rd_le64 (data + entry_off);
		const s64 bin = (s64)rd_le64 (data + entry_off + 16);
		const bool src_ok = bnsh_program_plausible (data, size, src);
		const bool bin_ok = bnsh_program_plausible (data, size, bin);
		if (src_ok && bin_ok && src != bin)
		{
			bnsh_collect_program_blobs (out, data, size, (u64)src, v, "source");
			bnsh_collect_program_blobs (out, data, size, (u64)bin, v, "binary");
			continue;
		}
		if (src_ok && !bin_ok && bin == 0)
		{
			bnsh_collect_program_blobs (out, data, size, (u64)src, v, "program");
			continue;
		}
		if (bin_ok && !src_ok && src == 0)
		{
			bnsh_collect_program_blobs (out, data, size, (u64)bin, v, "program");
			continue;
		}
		const s64 base = src + bin;
		if (base > 0 && (u64)base + 8 + 6 * 8 <= size)
			bnsh_collect_program_blobs (out, data, size, (u64)base, v, "program");
	}
	return ERR_OK;
}

void ResetBNSH_Blobs (bnsh_blobs_t *out)
{
	if (out)
	{
		FREE (out->blobs);
		out->blobs = NULL;
		out->n_blobs = 0;
	}
}

