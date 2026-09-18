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

// ---- SHARCFB "NX" variant --------------------------------------------------------------
//
// Verified against Switch-Toolbox File_Format_Library/FileFormats/Shader/SHARC/SHARCFBNX.cs
// (SHARCFBNX.Header.Read()/ShaderVariation.Read()/ShaderProgram.Read()) plus the
// VariationSymbolData/VariationMacroData/ShaderSymbolData helper classes it reuses from
// SHARCFB.cs. Every offset in that source is either relative to a fixed 'pos' recorded right
// before it, or an absolute byte offset from the fixed string-table base (336); we track both
// as plain u64 and bounds-check every derived address against 'size' before touching memory --
// this offset arithmetic is exactly the overflow class this codebase has had to fix repeatedly,
// so nothing here is done as 32-bit or as unchecked pointer math.
//
// A small cursor, rather than raw pointers, keeps every read from silently running past 'size'.
typedef struct nxr_t
{
	const u8 *data;
	u64 size;
	u64 pos;
	bool is_le;
	bool bad; // sticky: once true, every further read is a no-op that keeps returning false
} nxr_t;

static bool nxr_u32 (nxr_t *r, u32 *out)
{
	if (r->bad || r->pos + 4 > r->size)
		return r->bad = true, false;
	*out = r->is_le ? rd_le32 (r->data + r->pos) : rd_be32 (r->data + r->pos);
	r->pos += 4;
	return true;
}

static bool nxr_u64 (nxr_t *r, u64 *out)
{
	if (r->bad || r->pos + 8 > r->size)
		return r->bad = true, false;
	*out = r->is_le ? rd_le64 (r->data + r->pos) : rd_be64 (r->data + r->pos);
	r->pos += 8;
	return true;
}

static bool nxr_seek (nxr_t *r, u64 pos)
{
	if (r->bad || pos > r->size)
		return r->bad = true, false;
	r->pos = pos;
	return true;
}

// Reads 'len' bytes starting at the cursor as a capped, NUL-terminated string (VariationMacro
// / VariationSymbol / ShaderProgram names, which are stored inline rather than as a string-table
// reference) and advances the cursor past them.
static void nxr_string_inline (nxr_t *r, char *dest, uint destsz, u32 len)
{
	dest[0] = 0;
	if (r->bad || r->pos + len > r->size)
	{
		r->bad = true;
		return;
	}
	uint copy = len < destsz ? len : destsz - 1;
	memcpy (dest, r->data + r->pos, copy);
	dest[copy] = 0;
	r->pos += len;
}

// ShaderVariation's symbol tables (Attributes/Samplers/Buffers/Uniforms/UniformBlocks) instead
// store an 8-byte offset from STRING_TABLE_BASE (the fixed value 336) to a NUL-terminated name;
// SHARCFBNX.cs's ReadString(). Read the offset off the cursor, then fetch the string without
// otherwise disturbing the cursor position.
#define SHARCFBNX_STRTAB_BASE 336

static void nxr_string_ref (nxr_t *r, char *dest, uint destsz)
{
	dest[0] = 0;
	u64 off;
	if (!nxr_u64 (r, &off))
		return;
	if (off > r->size || (u64)SHARCFBNX_STRTAB_BASE + off > r->size)
		return;
	const u64 start = (u64)SHARCFBNX_STRTAB_BASE + off;
	uint i = 0;
	while (i + 1 < destsz && start + i < r->size && r->data[start + i])
	{
		dest[i] = r->data[start + i];
		i++;
	}
	dest[i] = 0;
}

// VariationSymbolData: a list of {Name, Values[] (a run of NUL-terminated strings packed into
// valueLength bytes), SymbolName}, all inline. We only report the symbol names and how many
// value strings each one carries -- the individual values are per-shader-variation macro
// settings, not needed for a manifest listing.
static void nxr_skip_variation_symbol_table (nxr_t *r, FILE *out, const char *label)
{
	u64 pos = r->pos;
	u32 section_size, count;
	if (!nxr_u32 (r, &section_size) || !nxr_u32 (r, &count) || !section_size)
	{
		r->bad = true;
		return;
	}
	fprintf (out, "      %s_count = %u\n", label, count);
	for (u32 i = 0; i < count && !r->bad; i++)
	{
		const u64 epos = r->pos;
		u32 esize, name_len, value_len, symbol_len;
		if (!nxr_u32 (r, &esize) || !nxr_u32 (r, &name_len)
			|| !nxr_u32 (r, &value_len) || !nxr_u32 (r, &symbol_len) || !esize)
		{
			r->bad = true;
			break;
		}
		char name[256];
		nxr_string_inline (r, name, sizeof (name), name_len);
		// Values[] and SymbolName follow but aren't needed for the manifest; skip straight to
		// the next entry via its own self-reported size instead of decoding them.
		fprintf (out, "        [%u] %s\n", i, name);
		nxr_seek (r, epos + esize);
	}
	nxr_seek (r, pos + section_size);
}

// VariationMacroData: a list of inline {Name, Value} pairs.
static void nxr_skip_macro_table (nxr_t *r, FILE *out)
{
	u64 pos = r->pos;
	u32 section_size, count;
	if (!nxr_u32 (r, &section_size) || !nxr_u32 (r, &count) || !section_size)
	{
		r->bad = true;
		return;
	}
	fprintf (out, "      macro_count = %u\n", count);
	for (u32 i = 0; i < count && !r->bad; i++)
	{
		const u64 epos = r->pos;
		u32 esize, name_len, value_len;
		if (!nxr_u32 (r, &esize) || !nxr_u32 (r, &name_len) || !nxr_u32 (r, &value_len) || !esize)
		{
			r->bad = true;
			break;
		}
		char name[256];
		nxr_string_inline (r, name, sizeof (name), name_len);
		fprintf (out, "        [%u] %s\n", i, name);
		nxr_seek (r, epos + esize);
	}
	nxr_seek (r, pos + section_size);
}

// ShaderSymbolData (used for ShaderProgram.UniformVariables): a list of ShaderSymbol, each of
// which -- for the version>=13 layout the NX variant actually uses -- carries its own nested
// "value table" of SharcNXValue{Name} entries. This is the "Value tables" the task refers to.
static void nxr_skip_uniform_table (nxr_t *r, FILE *out)
{
	u64 pos = r->pos;
	u32 section_size, count;
	if (!nxr_u32 (r, &section_size) || !nxr_u32 (r, &count) || !section_size)
	{
		r->bad = true;
		return;
	}
	fprintf (out, "      uniform_count = %u\n", count);
	for (u32 i = 0; i < count && !r->bad; i++)
	{
		const u64 epos = r->pos;
		u32 esize, var_size, name_len, value_section_size, value_count;
		if (!nxr_u32 (r, &esize) || !nxr_u32 (r, &var_size) || !nxr_u32 (r, &name_len)
			|| !esize)
		{
			r->bad = true;
			break;
		}
		char name[256];
		nxr_string_inline (r, name, sizeof (name), name_len);
		fprintf (out, "        [%u] %s\n", i, name);

		if (nxr_u32 (r, &value_section_size) && nxr_u32 (r, &value_count))
		{
			fprintf (out, "          value_count = %u\n", value_count);
			for (u32 v = 0; v < value_count && !r->bad; v++)
			{
				const u64 vpos = r->pos;
				u32 vsize, unk, str_len;
				if (!nxr_u32 (r, &vsize) || !nxr_u32 (r, &unk) || !nxr_u32 (r, &str_len)
					|| !vsize)
				{
					r->bad = true;
					break;
				}
				char vname[256];
				nxr_string_inline (r, vname, sizeof (vname), str_len);
				fprintf (out, "            [%u] %s\n", v, vname);
				nxr_seek (r, vpos + vsize);
			}
		}
		nxr_seek (r, epos + esize);
	}
	nxr_seek (r, pos + section_size);
}

// Header/Variations/ShaderPrograms, following SHARCFBNX.cs's Header.Read() exactly. ALIGNMENT
// is the value already read from the outer SHARCFB header at offset 0x14 (the field the non-NX
// path calls 'name_length', but that's this variant's alignment for the fixed string table).
static enumError DecodeSHARCFBNX_Text (FILE *out, const u8 *data, size_t size,
	bool is_le, u32 alignment)
{
	nxr_t r = { data, size, 0, is_le, false };

	// The fixed-size region [0x00, 0x20) is the common SHARCFB header already parsed by the
	// caller; SHARCFBNX.cs re-parses it from scratch and then adds BinaryArraySize/Offset.
	if (!nxr_seek (&r, 0x14))
		return ERROR0 (ERR_INVALID_DATA, "SHARCFB-NX: file shorter than the fixed header\n");
	u32 hdr_alignment, binary_array_size, binary_array_offset;
	if (!nxr_u32 (&r, &hdr_alignment) || !nxr_u32 (&r, &binary_array_size)
		|| !nxr_u32 (&r, &binary_array_offset))
		return ERROR0 (ERR_INVALID_DATA, "SHARCFB-NX: file shorter than the fixed header\n");

	fprintf (out, "variant = NX\n"
		"alignment = %u\n"
		"binary_array_size = %u\n"
		"binary_array_offset = %u\n",
		alignment, binary_array_size, binary_array_offset);

	// The string table always starts at byte 336; the fixed-header fields end well before it,
	// but only if 'alignment' (originally sized to fit the string table) doesn't overflow past
	// the file -- checked here since everything after is offset relative to this position.
	if ((u64)SHARCFBNX_STRTAB_BASE + alignment > size)
		return ERROR0 (ERR_INVALID_DATA, "SHARCFB-NX: string table runs past end of file\n");
	const u64 base_pos = (u64)SHARCFBNX_STRTAB_BASE + alignment;
	if (!nxr_seek (&r, base_pos))
		return ERROR0 (ERR_INVALID_DATA, "SHARCFB-NX: string table runs past end of file\n");

	u32 program_array_offset, variation_count;
	if (!nxr_u32 (&r, &program_array_offset) || !nxr_u32 (&r, &variation_count))
		return ERROR0 (ERR_INVALID_DATA, "SHARCFB-NX: truncated variation array header\n");

	fprintf (out, "variation_count = %u\n\n[variations]\n", variation_count);

	static const char *type_name[] = { "Vertex", "Pixel" };

	for (u32 i = 0; i < variation_count && !r.bad; i++)
	{
		const u64 vpos = r.pos;
		u32 section_size, type, pad, section_size2;
		if (!nxr_u32 (&r, &section_size) || !nxr_u32 (&r, &type)
			|| !nxr_u32 (&r, &pad) || !nxr_u32 (&r, &section_size2) || !section_size)
			return ERROR0 (ERR_INVALID_DATA, "SHARCFB-NX: variation %u has an invalid section size\n", i);

		// every offset below (attribute/uniform/sampler/buffer tables) is relative to r.pos
		// here, per ShaderVariation.Read() -- unused since those tables are only string-table
		// references we don't need for the manifest and are skipped via section_size instead.
		u64 binary_data_offset;
		u32 shader_a_size, shader_a_offset, pad2, num_uniform_blocks;
		u64 uniform_block_offset;
		if (!nxr_u64 (&r, &binary_data_offset) || !nxr_u32 (&r, &shader_a_size)
			|| !nxr_u32 (&r, &shader_a_offset) || !nxr_u32 (&r, &pad2)
			|| !nxr_u32 (&r, &num_uniform_blocks) || !nxr_u64 (&r, &uniform_block_offset))
			return ERROR0 (ERR_INVALID_DATA, "SHARCFB-NX: variation %u header runs past end of file\n", i);

		// Same "dumb hack" the reference implementation uses: the version field alone doesn't
		// distinguish the layout that inserts a Buffers table, so it's inferred from field
		// values that are otherwise nonsensical for the older layout.
		const bool is_new_version = shader_a_offset == 96 || uniform_block_offset == 92;
		u32 num_buffers = 0;
		u64 buffer_offset = 0;
		if (is_new_version && (!nxr_u32 (&r, &num_buffers) || !nxr_u64 (&r, &buffer_offset)))
			return ERROR0 (ERR_INVALID_DATA, "SHARCFB-NX: variation %u header runs past end of file\n", i);

		u32 num_attributes, num_uniforms, num_samplers;
		u64 attribute_offset, uniform_offset, sampler_offset;
		if (!nxr_u32 (&r, &num_attributes) || !nxr_u64 (&r, &attribute_offset)
			|| !nxr_u32 (&r, &num_uniforms) || !nxr_u64 (&r, &uniform_offset)
			|| !nxr_u32 (&r, &num_samplers) || !nxr_u64 (&r, &sampler_offset))
			return ERROR0 (ERR_INVALID_DATA, "SHARCFB-NX: variation %u header runs past end of file\n", i);

		fprintf (out, "  [%u] type = %s, attributes = %u, uniforms = %u, samplers = %u, "
			"uniform_blocks = %u, buffers = %u, shader_binary_size = %u\n",
			i, type < 2 ? type_name[type] : "?", num_attributes, num_uniforms, num_samplers,
			num_uniform_blocks, num_buffers, shader_a_size);

		// The symbol tables themselves are only string-table references (name + a small fixed
		// record); listing their names isn't essential for the manifest and each one requires
		// its own seek, so we only print the counts above and move on to the next variation via
		// its self-reported section size, exactly like DecodeSHARC_Text does for its entries.
		nxr_seek (&r, vpos + section_size);
	}

	if (r.bad)
		return ERROR0 (ERR_INVALID_DATA, "SHARCFB-NX: variation table runs past end of file\n");

	if ((u64)base_pos + program_array_offset + 8 > size)
		return ERROR0 (ERR_INVALID_DATA, "SHARCFB-NX: program array offset out of bounds\n");
	if (!nxr_seek (&r, base_pos + program_array_offset))
		return ERROR0 (ERR_INVALID_DATA, "SHARCFB-NX: program array offset out of bounds\n");

	u32 program_array_size, program_count;
	if (!nxr_u32 (&r, &program_array_size) || !nxr_u32 (&r, &program_count))
		return ERROR0 (ERR_INVALID_DATA, "SHARCFB-NX: truncated program array header\n");

	fprintf (out, "\nprogram_count = %u\n\n[programs]\n", program_count);

	for (u32 i = 0; i < program_count && !r.bad; i++)
	{
		const u64 pos = r.pos;
		u32 section_size, name_len, section_count;
		int32_t base_index;
		if (!nxr_u32 (&r, &section_size) || !nxr_u32 (&r, &name_len)
			|| !nxr_u32 (&r, &section_count) || !nxr_u32 (&r, (u32 *)&base_index) || !section_size)
			return ERROR0 (ERR_INVALID_DATA, "SHARCFB-NX: program %u has an invalid section size\n", i);

		char name[256];
		nxr_string_inline (&r, name, sizeof (name), name_len);
		if (r.bad)
			return ERROR0 (ERR_INVALID_DATA, "SHARCFB-NX: program %u name runs past end of file\n", i);

		fprintf (out, "  [%u] %s (base_index = %d)\n", i, name, base_index);

		nxr_skip_variation_symbol_table (&r, out, "variation_symbol");
		nxr_skip_macro_table (&r, out);
		nxr_skip_uniform_table (&r, out);

		if (r.bad)
			return ERROR0 (ERR_INVALID_DATA, "SHARCFB-NX: program %u's value tables run past end of file\n", i);

		nxr_seek (&r, pos + section_size);
	}

	if (r.bad)
		return ERROR0 (ERR_INVALID_DATA, "SHARCFB-NX: program table runs past end of file\n");

	return ERR_OK;
}


static void print_sharcfb_symbol_list (FILE *out, const u8 *data, size_t size, u64 *cur_pos,
	bool is_le, ccp list_name, ccp indent)
{
	u64 p = *cur_pos;
	if (p + 8 > size)
		return;
	const u32 sec_size = is_le ? rd_le32 (data + p) : rd_be32 (data + p);
	const u32 count = is_le ? rd_le32 (data + p + 4) : rd_be32 (data + p + 4);
	if (!sec_size || p + sec_size > size)
	{
		*cur_pos = p + (sec_size ? sec_size : 8);
		return;
	}

	if (count)
		fprintf (out, "%s%s:\n", indent, list_name);

	u64 item_p = p + 8;
	const u64 sec_end = p + sec_size;

	for (u32 i = 0; i < count && item_p + 4 <= sec_end; i++)
	{
		const u32 item_sec_size = is_le ? rd_le32 (data + item_p) : rd_be32 (data + item_p);
		if (!item_sec_size || item_p + item_sec_size > sec_end)
			break;

		if (item_p + 24 <= item_p + item_sec_size)
		{
			const u32 sym_size = is_le ? rd_le32 (data + item_p + 4) : rd_be32 (data + item_p + 4);
			const u32 var_name_len = is_le ? rd_le32 (data + item_p + 8) : rd_be32 (data + item_p + 8);
			const u32 sym_name_len = is_le ? rd_le32 (data + item_p + 12) : rd_be32 (data + item_p + 12);
			const u64 str_p = item_p + 24;

			char var_name[128] = "", sym_name[128] = "";
			if (str_p + var_name_len <= item_p + item_sec_size)
				copy_name (var_name, sizeof (var_name), data + str_p, var_name_len, data + size);
			if (str_p + var_name_len + sym_name_len <= item_p + item_sec_size)
				copy_name (sym_name, sizeof (sym_name), data + str_p + var_name_len, sym_name_len, data + size);

			fprintf (out, "%s  [%u] %s (symbol = %s, size = %u)\n", indent, i,
				var_name[0] ? var_name : "<unnamed>", sym_name, sym_size);
		}
		item_p += item_sec_size;
	}
	*cur_pos = p + sec_size;
}

static void print_sharcfb_macro_list (FILE *out, const u8 *data, size_t size, u64 *cur_pos,
	bool is_le, ccp list_name, ccp indent)
{
	u64 p = *cur_pos;
	if (p + 8 > size)
		return;
	const u32 sec_size = is_le ? rd_le32 (data + p) : rd_be32 (data + p);
	const u32 count = is_le ? rd_le32 (data + p + 4) : rd_be32 (data + p + 4);
	if (!sec_size || p + sec_size > size)
	{
		*cur_pos = p + (sec_size ? sec_size : 8);
		return;
	}

	if (count)
		fprintf (out, "%s%s:\n", indent, list_name);

	u64 item_p = p + 8;
	const u64 sec_end = p + sec_size;

	for (u32 i = 0; i < count && item_p + 4 <= sec_end; i++)
	{
		const u32 item_sec_size = is_le ? rd_le32 (data + item_p) : rd_be32 (data + item_p);
		if (!item_sec_size || item_p + item_sec_size > sec_end)
			break;

		if (item_p + 16 <= item_p + item_sec_size)
		{
			const u32 name_len = is_le ? rd_le32 (data + item_p + 4) : rd_be32 (data + item_p + 4);
			const u32 val_cnt = is_le ? rd_le32 (data + item_p + 8) : rd_be32 (data + item_p + 8);
			char mname[128] = "";
			copy_name (mname, sizeof (mname), data + item_p + 16, name_len, data + size);
			fprintf (out, "%s  [%u] %s (%u values)\n", indent, i, mname[0] ? mname : "<unnamed>", val_cnt);
		}
		item_p += item_sec_size;
	}
	*cur_pos = p + sec_size;
}

static enumError DecodeSHARCFBWiiU_Text (FILE *out, const u8 *data, size_t size, u32 version,
	bool is_le, u32 name_length)
{
	char name[256];
	copy_name (name, sizeof (name), data + 24, name_length, data + size);
	fprintf (out, "name = %s\n\n", name);

	u64 p = 24 + (u64)name_length;

	// Section 1: Binaries
	if (p + 8 <= size)
	{
		const u32 sec1_size = is_le ? rd_le32 (data + p) : rd_be32 (data + p);
		const u32 binary_count = is_le ? rd_le32 (data + p + 4) : rd_be32 (data + p + 4);
		const u64 sec1_end = p + sec1_size;

		fprintf (out, "[binaries]\nbinary_count = %u\n", binary_count);

		u64 bp = p + 8;
		static const ccp gx2_types[4] = { "Vertex", "Pixel", "Geometry", "Unknown" };

		for (u32 i = 0; i < binary_count && bp + 4 <= sec1_end && bp + 4 <= size; i++)
		{
			const u32 b_sec_size = is_le ? rd_le32 (data + bp) : rd_be32 (data + bp);
			if (!b_sec_size || bp + b_sec_size > size)
				break;

			if (bp + 16 <= size)
			{
				const u32 b_type = is_le ? rd_le32 (data + bp + 4) : rd_be32 (data + bp + 4);
				const u32 b_size = is_le ? rd_le32 (data + bp + 12) : rd_be32 (data + bp + 12);
				ccp tname = b_type < 3 ? gx2_types[b_type] : gx2_types[3];
				fprintf (out, "  [%u] type = %s, size = %u\n", i, tname, b_size);
			}
			bp += b_sec_size;
		}

		p = sec1_end;
	}

	// Section 2: Programs
	if (p + 8 <= size)
	{
		const u32 sec2_size = is_le ? rd_le32 (data + p) : rd_be32 (data + p);
		const u32 program_count = is_le ? rd_le32 (data + p + 4) : rd_be32 (data + p + 4);
		const u64 sec2_end = p + sec2_size;

		fprintf (out, "\n[programs]\nprogram_count = %u\n", program_count);

		u64 pp = p + 8;

		for (u32 i = 0; i < program_count && pp + 4 <= sec2_end && pp + 4 <= size; i++)
		{
			const u32 p_sec_size = is_le ? rd_le32 (data + pp) : rd_be32 (data + pp);
			if (!p_sec_size || pp + p_sec_size > size)
				break;

			if (pp + 16 <= size)
			{
				const u32 p_name_len = is_le ? rd_le32 (data + pp + 4) : rd_be32 (data + pp + 4);
				const u32 kind = is_le ? rd_le32 (data + pp + 8) : rd_be32 (data + pp + 8);
				const s32 base_idx = (s32)(is_le ? rd_le32 (data + pp + 12) : rd_be32 (data + pp + 12));

				char prog_name[256];
				copy_name (prog_name, sizeof (prog_name), data + pp + 16, p_name_len, data + size);
				fprintf (out, "  [%u] %s (kind = 0x%x, base_index = %d)\n",
					i, prog_name[0] ? prog_name : "<unnamed>", kind, base_idx);

				u64 cp = pp + 16 + p_name_len;
				const u64 pp_end = pp + p_sec_size;

				if (cp < pp_end)
					print_sharcfb_macro_list (out, data, size, &cp, is_le, "variation_macros", "    ");
				if (cp < pp_end)
					print_sharcfb_macro_list (out, data, size, &cp, is_le, "variation_defaults", "    ");
				if (cp < pp_end)
					print_sharcfb_symbol_list (out, data, size, &cp, is_le, "uniforms", "    ");

				if (version >= 9 && cp + 4 <= pp_end)
				{
					const u32 skip_sz = is_le ? rd_le32 (data + cp) : rd_be32 (data + cp);
					cp += 4 + skip_sz;
				}

				if (cp < pp_end)
					print_sharcfb_symbol_list (out, data, size, &cp, is_le, "uniform_blocks", "    ");
				if (cp < pp_end)
					print_sharcfb_symbol_list (out, data, size, &cp, is_le, "samplers", "    ");
				if (cp < pp_end)
					print_sharcfb_symbol_list (out, data, size, &cp, is_le, "attributes", "    ");
			}
			pp += p_sec_size;
		}
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

	// name_length == 4096/8192 flags the Switch (NX) variant, which repurposes this same field
	// as a string-table alignment (SHARCFBNX.cs's Header.Read()) instead of a name length --
	// decoded separately below rather than misread as one.
	if (name_length == 4096 || name_length == 8192)
		return DecodeSHARCFBNX_Text (out, data, size, is_le, name_length);

	if ((u64)24 + name_length > size)
		return ERROR0 (ERR_INVALID_DATA, "SHARCFB: truncated archive name\n");

	return DecodeSHARCFBWiiU_Text (out, data, size, version, is_le, name_length);
}
