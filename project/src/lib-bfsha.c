#include "lib-bfsha.h"
#include "lib-std.h"

// BFSHA, little-endian Switch shader archive. BFSHA.cs in Switch-Toolbox only wraps a stream
// with the closed-source-looking "BfshaLibrary" -- that library is itself open source at
// KillzXGaming/BfshaLibrary and is the actual reference used here:
//   ShaderLibrary/IO/BinaryDataReader.cs   (ReadOffset/LoadString/dictionary helpers)
//   ShaderLibrary/Switch/BfshaLoader.cs    Read()/ReadShaderModel()/ReadBfshaShaderProgram()
//   ShaderLibrary/Structs.cs               BinaryHeader / ShaderProgramHeaderV4/V5/V7/V8
//   ShaderLibrary/Dict/ResDict.cs          Read() (the "ResDict" node-table format)
//
// Archive layout (absolute offsets, little-endian):
//   BinaryHeader (0x20 bytes, shared with BNSH/BNTX):
//     char magic[4] "FSHA"; u32 magic_pad;
//     u8 version_micro; u8 version_minor; u16 version_major;
//     u16 byte_order_mark; u8 alignment; u8 target_addr_size;
//     u32 name_offset; u16 flag; u16 block_offset;
//     u32 relocation_table_offset; u32 file_size;
//   -- then, sequentially from 0x20 (BfshaLoader.Read) --
//     u64 unk0; u64 string_pool_offset; u64 shader_model_offset (unused);
//     u64 name_ptr; u64 path_ptr;               // both u32-length-prefixed strings (LoadString)
//     u64 models_array_offset; u64 models_dict_offset;
//     u32 unk1[4]; u64 unk2;
//     [version_major >= 7: u64 padding, must be 0]
//     u16 model_count; u16 flag; u16 unused;
//
// A "ResDict" node table (used for the shader-model dictionary) is:
//     u32 magic; s32 num_nodes;                 // excludes the root node
//     (num_nodes+1) x { u32 ref; u16 idx_left; u16 idx_right; u64 key_ptr }
// and its matching value array holds num_nodes entries (one per non-root node, same order)
// at a separately-stored "array offset".
//
// Each ShaderModel value is a *fixed-size* record (all its own sub-tables are referenced by
// pointer, never inlined) whose exact size depends on version_major -- 0xc0 bytes below major
// 7, 0xe8 at major 7, 0x100 from major 8 up -- mirrored field-by-field from ReadShaderModel().
// Its ShaderProgram array entries are ShaderProgramHeaderV4/V5/V7/V8 (0x30/0x38/0x38/0x40
// bytes), and its key table is `program_count * (static_key_len+dynamic_key_len)` s32 values.

#define BFSHA_BIN_HDR_SIZE 0x20
#define BFSHA_MAX_MODELS   4096
#define BFSHA_MAX_PROGRAMS 4096
#define BFSHA_MAX_KEYS     1024

bool IsBFSHA (const u8 *data, size_t size)
{
	return data && size >= 4 && !memcmp (data, "FSHA", 4);
}

bool IsBFSHA_WiiU (const u8 *data, size_t size)
{
	return data && size >= 16 && !memcmp (data, "FSHA", 4) && memcmp (data + 4, "    ", 4) != 0
		&& rd_be16 (data + 8) == 0xfeff;
}

static bool bfsha_wiiu_read_string (char *dest, uint destsz, const u8 *data, size_t size, u64 ptr_loc, s32 rel_off)
{
	dest[0] = 0;
	if (!rel_off)
		return false;
	const s64 str_off_s = (s64)ptr_loc + rel_off;
	if (str_off_s < 0 || (u64)str_off_s >= size)
		return false;
	const u64 str_off = (u64)str_off_s;
	uint n = 0;
	while (str_off + n < size && n < destsz - 1 && data[str_off + n])
		n++;
	memcpy (dest, data + str_off, n);
	dest[n] = 0;
	return true;
}

// Reads a Switch-BFRES-family string: 'ptr' points at a u16 length field immediately
// preceding the NUL-terminated text (LoadString()'s "shift = 2" for non-WiiU targets).
// Bounds-checked and NUL-safe; leaves 'dest' empty on any out-of-range pointer.
static bool bfsha_read_string (char *dest, uint destsz, const u8 *data, size_t size, u64 ptr)
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

// One ShaderModel's fixed header fields, decoded version-aware. Returns the record's total
// byte size (the model array's per-entry stride) via *out_stride, or 0 if the header ran past
// 'size' (caller reports and stops walking the array).
typedef struct
{
	u64 program_array_off, key_table_off, parent_off, symbol_info_off, shader_file_off;
	u32 uniform_count, storage_count;
	s32 default_program_idx;
	u16 static_opt_count, dynamic_opt_count, program_count;
	u8 static_key_len, dynamic_key_len, attrib_count, sampler_count, image_count, ublock_count;

	// ResDict (array_off, dict_off) pairs -- LoadDictionary() reads valuesOffset then dictOffset,
	// in that order, for each of these; kept so DecodeBFSHA_Text can decode their names/values
	// instead of just skipping past them.
	u64 static_opts_arr, static_opts_dict, dynamic_opts_arr, dynamic_opts_dict;
	u64 attribs_arr, attribs_dict, samplers_arr, samplers_dict;
	u64 images_arr, images_dict;       // version_major >= 8 only
	u64 ublocks_arr, ublocks_dict;
} bfsha_model_hdr_t;

// 'out' may be NULL for a silent scan (ScanBFSHA_ModelRefs): no truncation diagnostics needed
// there since the caller only wants the BNSH offset/size, not a manifest.
static u64 decode_shader_model_header (FILE *out, const u8 *data, size_t size, u64 base,
	u16 vmajor, bfsha_model_hdr_t *m)
{
	memset (m, 0, sizeof (*m));
	u64 p = base;

#define NEED(n) do { if ((u64)p + (n) > size) { \
		if (out) fprintf (out, "    <model header truncated at +0x%llx>\n", (unsigned long long)(p - base)); \
		return 0; } } while (0)

	NEED (8);  p += 8;                              // name_ptr (unused: dict already gave us it)
	NEED (16); m->static_opts_arr = rd_le64 (data + p); m->static_opts_dict = rd_le64 (data + p + 8); p += 16;
	NEED (16); m->dynamic_opts_arr = rd_le64 (data + p); m->dynamic_opts_dict = rd_le64 (data + p + 8); p += 16;
	NEED (16); m->attribs_arr = rd_le64 (data + p); m->attribs_dict = rd_le64 (data + p + 8); p += 16;
	NEED (16); m->samplers_arr = rd_le64 (data + p); m->samplers_dict = rd_le64 (data + p + 8); p += 16;
	if (vmajor >= 8)
	{
		NEED (16); m->images_arr = rd_le64 (data + p); m->images_dict = rd_le64 (data + p + 8); p += 16;
	}
	NEED (16); m->ublocks_arr = rd_le64 (data + p); m->ublocks_dict = rd_le64 (data + p + 8); p += 16;
	NEED (8);  p += 8;                               // uniform array offset (not a dict pair)
	if (vmajor >= 7)
	{
		NEED (16); p += 16;                          // storage buffers dict
		NEED (8);  p += 8;                           // unused offset
	}
	NEED (8); m->program_array_off = rd_le64 (data + p); p += 8;
	NEED (8); m->key_table_off     = rd_le64 (data + p); p += 8;
	NEED (8); m->parent_off        = rd_le64 (data + p); p += 8;
	NEED (8); m->symbol_info_off   = rd_le64 (data + p); p += 8;
	NEED (8); m->shader_file_off   = rd_le64 (data + p); p += 8;
	NEED (24); p += 24;                              // 3x reserved s64
	if (vmajor >= 7) { NEED (16); p += 16; }         // 2x padding s64
	NEED (4); m->uniform_count = rd_le32 (data + p); p += 4;
	if (vmajor >= 7) { NEED (4); m->storage_count = rd_le32 (data + p); p += 4; }
	NEED (4); m->default_program_idx = (s32) rd_le32 (data + p); p += 4;
	NEED (2); m->static_opt_count  = rd_le16 (data + p); p += 2;
	NEED (2); m->dynamic_opt_count = rd_le16 (data + p); p += 2;
	NEED (2); m->program_count     = rd_le16 (data + p); p += 2;
	if (vmajor < 7) { NEED (2); p += 2; }            // unknown
	NEED (1); m->static_key_len  = data[p]; p += 1;
	NEED (1); m->dynamic_key_len = data[p]; p += 1;
	NEED (1); m->attrib_count    = data[p]; p += 1;
	NEED (1); m->sampler_count   = data[p]; p += 1;
	if (vmajor >= 8) { NEED (1); m->image_count = data[p]; p += 1; }
	NEED (1); m->ublock_count = data[p]; p += 1;
	NEED (1); p += 1;                                // unknown2
	NEED (4); p += 4;                                // block indices[4]
	if (vmajor >= 8)      { NEED (11); p += 11; }
	else if (vmajor >= 7) { NEED (4);  p += 4;  }
	else                  { NEED (6);  p += 6;  }

#undef NEED
	return p - base;
}

// ShaderProgramHeaderV4/V5/V7/V8 field offsets differ per version; sizes are 0x30/0x38/0x38/0x40.
static void decode_shader_program (FILE *out, const u8 *data, size_t size, u64 base, u16 vmajor,
	bfsha_model_hdr_t *m, uint idx)
{
	u64 stride;
	u64 variation_off; u32 attr_flags; u16 flags, n_samplers, n_blocks, n_storage = 0, n_images = 0;

	if (vmajor >= 8)
	{
		stride = 0x40;
		if (base + stride > size) goto truncated;
		attr_flags  = rd_le32 (data + base + 48);
		flags       = rd_le16 (data + base + 52);
		n_samplers  = rd_le16 (data + base + 54);
		n_images    = rd_le16 (data + base + 56);
		n_blocks    = rd_le16 (data + base + 58);
		n_storage   = rd_le16 (data + base + 60);
		variation_off = rd_le64 (data + base + 32);
	}
	else if (vmajor >= 7)
	{
		stride = 0x38;
		if (base + stride > size) goto truncated;
		variation_off = rd_le64 (data + base + 24);
		attr_flags  = rd_le32 (data + base + 40);
		flags       = rd_le16 (data + base + 44);
		n_samplers  = rd_le16 (data + base + 46);
		n_blocks    = rd_le16 (data + base + 48);
		n_storage   = rd_le16 (data + base + 50);
	}
	else if (vmajor >= 5)
	{
		stride = 0x38;
		if (base + stride > size) goto truncated;
		variation_off = rd_le64 (data + base + 16);
		attr_flags  = rd_le32 (data + base + 32);
		flags       = rd_le16 (data + base + 36);
		n_samplers  = rd_le16 (data + base + 38);
		n_blocks    = rd_le16 (data + base + 40);
	}
	else
	{
		stride = 0x30;
		if (base + stride > size) goto truncated;
		variation_off = rd_le64 (data + base + 16);
		attr_flags  = rd_le32 (data + base + 32);
		flags       = rd_le16 (data + base + 36);
		n_samplers  = rd_le16 (data + base + 38);
		n_blocks    = rd_le16 (data + base + 40);
	}

	fprintf (out, "    program[%u]: variation_offset = %llu, attr_flags = 0x%08x, flags = 0x%04x,"
		" n_samplers = %u, n_blocks = %u",
		idx, (unsigned long long) variation_off, attr_flags, flags, n_samplers, n_blocks);
	if (vmajor >= 7)
		fprintf (out, ", n_storage = %u", n_storage);
	if (vmajor >= 8)
		fprintf (out, ", n_images = %u", n_images);
	fprintf (out, "\n");

	// Per-program key row: static+dynamic option key indices, program_count rows total.
	const uint n_keys = (uint) m->static_key_len + m->dynamic_key_len;
	if (n_keys && n_keys <= BFSHA_MAX_KEYS && m->key_table_off)
	{
		const u64 row = m->key_table_off + (u64) idx * n_keys * 4;
		if (row + (u64) n_keys * 4 <= size)
		{
			fprintf (out, "      keys =");
			for (uint k = 0; k < n_keys; k++)
				fprintf (out, " %d", (s32) rd_le32 (data + row + (u64) k * 4));
			fprintf (out, "\n");
		}
		else
			fprintf (out, "      keys = <out of bounds>\n");
	}
	return;

truncated:
	fprintf (out, "    program[%u]: <header out of bounds>\n", idx);
}

#define BFSHA_MAX_DICT_NODES 4096

// Which fixed-size value struct (if any) sits in the ResDict's parallel array, per
// BfshaLoader.cs's ReadShaderOption/ReadAttribute/ReadSampler/ReadBfshaUniformBlock/
// ReadBfshaUniform -- ReadImage's struct is empty (no array read needed, names only).
typedef enum
{
	BFSHA_DICT_OPTION,   // ShaderOption:      stride 32
	BFSHA_DICT_ATTRIB,   // BfshaAttribute:    u8 index, s8 location                    (stride 2)
	BFSHA_DICT_SAMPLER,  // BfshaSampler:      u64 name_ptr, u8 index, 7 pad            (stride 16)
	BFSHA_DICT_IMAGE,    // BfshaImageBuffer:  empty                                    (stride 0)
	BFSHA_DICT_UBLOCK,   // BfshaUniformBlock: u64 uarr, u64 udict, u64 default_off,
	                     //                    u8 index, u8 type, u16 size, u16 numu    (stride 32)
	BFSHA_DICT_UNIFORM,  // BfshaUniform:      u64 name_ptr, s32 index, u16 data_off,
	                     //                    u8 block_index, 1 pad                    (stride 16)
	BFSHA_DICT_CHOICE,   // Choice:            name only                                (stride 0)
} bfsha_dict_kind_t;

static const u64 bfsha_dict_stride[] =
{
	[BFSHA_DICT_OPTION]  = 32,
	[BFSHA_DICT_ATTRIB]  = 2,
	[BFSHA_DICT_SAMPLER] = 16,
	[BFSHA_DICT_IMAGE]   = 0,
	[BFSHA_DICT_UBLOCK]  = 32,
	[BFSHA_DICT_UNIFORM] = 16,
	[BFSHA_DICT_CHOICE]  = 0,
};

// Decodes one ResDict node table's key names (LoadDictionary()'s Node.Key), plus -- for the
// kinds with a known fixed-size value struct -- a couple of the value's own fields, straight
// out of the parallel values array at 'array_off'. Recurses one level for uniform blocks, whose
// own value struct embeds a second ResDict of per-block uniform names. All offsets/lengths are
// bounds-checked at 64-bit width before use, same discipline as the rest of this decoder.
static void decode_resdict (FILE *out, const u8 *data, size_t size, u64 dict_off, u64 array_off,
	bfsha_dict_kind_t kind, ccp indent)
{
	if (!dict_off || (u64)dict_off + 8 > size)
		return;

	const s32 num_nodes_signed = (s32) rd_le32 (data + dict_off + 4);
	if (num_nodes_signed <= 0 || num_nodes_signed > BFSHA_MAX_DICT_NODES)
	{
		if (num_nodes_signed)
			fprintf (out, "%s<dictionary node count implausible: %d>\n", indent, num_nodes_signed);
		return;
	}
	const u64 num_nodes = (u64) num_nodes_signed;
	const u64 nodes_base = dict_off + 8;
	if (nodes_base + (num_nodes + 1) * 16 > size)
	{
		fprintf (out, "%s<dictionary node table out of bounds>\n", indent);
		return;
	}

	const u64 stride = bfsha_dict_stride[kind];
	char sub_indent[288];
	snprintf (sub_indent, sizeof (sub_indent), "%s    ", indent);

	for (u64 i = 0; i < num_nodes; i++)
	{
		// Node 0 is the dictionary root and carries no key/value; entries start at node 1.
		const u64 node_off = nodes_base + (i + 1) * 16;
		const u64 key_ptr = rd_le64 (data + node_off + 8);
		char name[256];
		bfsha_read_string (name, sizeof (name), data, size, key_ptr);
		fprintf (out, "%s[%llu] %s", indent, (unsigned long long) i, name[0] ? name : "<unnamed>");

		u64 ublock_udict = 0, ublock_uarr = 0;
		if (array_off && stride)
		{
			const u64 elem = array_off + i * stride;
			if (elem < array_off || elem + stride > size) // 64-bit wrap guard + bounds
			{
				fprintf (out, " <value out of bounds>");
			}
			else switch (kind)
			{
			case BFSHA_DICT_OPTION:
				if (elem + 32 <= size)
				{
					u16 choice_count;
					s16 default_choice;
					u16 block_offset;
					u8 key_offset, bit_idx, bit_shift;
					u32 mask;
					const u16 vmaj = rd_le16 (data + 10);
					if (vmaj >= 9)
					{
						choice_count = rd_le16 (data + elem + 16);
						default_choice = (s16) rd_le16 (data + elem + 18);
						block_offset = data[elem + 22];
						key_offset = data[elem + 23];
						mask = rd_le32 (data + elem + 24);
						bit_idx = data[elem + 28];
						bit_shift = data[elem + 29];
					}
					else
					{
						choice_count = data[elem + 16];
						default_choice = (s8) data[elem + 17];
						block_offset = rd_le16 (data + elem + 19);
						key_offset = data[elem + 21];
						bit_idx = data[elem + 22];
						bit_shift = data[elem + 23];
						mask = rd_le32 (data + elem + 24);
					}
					fprintf (out, " (choices=%u, default=%d, block_offset=%u, key_offset=%u, bit_idx=%u, bit_shift=%u, mask=0x%08x)",
						choice_count, default_choice, block_offset, key_offset, bit_idx, bit_shift, mask);
					ublock_udict = rd_le64 (data + elem); // choice_dict
					ublock_uarr = 0;
				}
				break;
			case BFSHA_DICT_ATTRIB:
				fprintf (out, " (index=%u, location=%d)", data[elem], (int)(s8) data[elem + 1]);
				break;
			case BFSHA_DICT_SAMPLER:
				fprintf (out, " (index=%u)", data[elem + 8]);
				break;
			case BFSHA_DICT_UBLOCK:
				fprintf (out, " (index=%u, type=%u, size=%u, uniforms=%u)",
					data[elem + 24], data[elem + 25],
					rd_le16 (data + elem + 26), rd_le16 (data + elem + 28));
				ublock_uarr  = rd_le64 (data + elem);
				ublock_udict = rd_le64 (data + elem + 8);
				break;
			case BFSHA_DICT_UNIFORM:
				fprintf (out, " (index=%d, data_offset=%u, block_index=%u)",
					(s32) rd_le32 (data + elem + 8), rd_le16 (data + elem + 12), data[elem + 14]);
				break;
			default:
				break;
			}
		}
		fprintf (out, "\n");

		if (ublock_udict)
			decode_resdict (out, data, size, ublock_udict, ublock_uarr,
				kind == BFSHA_DICT_OPTION ? BFSHA_DICT_CHOICE : BFSHA_DICT_UNIFORM, sub_indent);
	}
}

static void decode_switch_symbol_table (FILE *out, const u8 *data, size_t size, u64 sym_base,
	u16 vmajor, const bfsha_model_hdr_t *m)
{
	if (!sym_base || sym_base + 32 > size)
		return;

	fprintf (out, "    symbol_table:\n");
	const u64 samplers_arr = rd_le64 (data + sym_base + 0);
	const u64 images_arr = (vmajor >= 8) ? rd_le64 (data + sym_base + 8) : 0;
	const u64 ublocks_arr = (vmajor >= 8) ? rd_le64 (data + sym_base + 16) : rd_le64 (data + sym_base + 8);
	const u64 storage_arr = (vmajor >= 8) ? rd_le64 (data + sym_base + 24) : (vmajor >= 7) ? rd_le64 (data + sym_base + 16) : 0;

	const u64 sym_stride = (vmajor >= 9) ? 16 : (vmajor == 8) ? 48 : 32;

	if (samplers_arr && m->sampler_count)
	{
		fprintf (out, "      samplers:\n");
		for (uint i = 0; i < m->sampler_count && samplers_arr + (u64)i * sym_stride + sym_stride <= size; i++)
		{
			const u64 e = samplers_arr + (u64)i * sym_stride;
			char n1[128] = "", v1[128] = "";
			bfsha_read_string (n1, sizeof (n1), data, size, rd_le64 (data + e));
			bfsha_read_string (v1, sizeof (v1), data, size, rd_le64 (data + e + 8));
			fprintf (out, "        [%u] %s%s%s\n", i, n1[0] ? n1 : "<unnamed>", v1[0] ? " = " : "", v1);
		}
	}
	if (images_arr && m->image_count)
	{
		fprintf (out, "      images:\n");
		for (uint i = 0; i < m->image_count && images_arr + (u64)i * sym_stride + sym_stride <= size; i++)
		{
			const u64 e = images_arr + (u64)i * sym_stride;
			char n1[128] = "", v1[128] = "";
			bfsha_read_string (n1, sizeof (n1), data, size, rd_le64 (data + e));
			bfsha_read_string (v1, sizeof (v1), data, size, rd_le64 (data + e + 8));
			fprintf (out, "        [%u] %s%s%s\n", i, n1[0] ? n1 : "<unnamed>", v1[0] ? " = " : "", v1);
		}
	}
	if (ublocks_arr && m->ublock_count)
	{
		fprintf (out, "      uniform_blocks:\n");
		for (uint i = 0; i < m->ublock_count && ublocks_arr + (u64)i * sym_stride + sym_stride <= size; i++)
		{
			const u64 e = ublocks_arr + (u64)i * sym_stride;
			char n1[128] = "", v1[128] = "";
			bfsha_read_string (n1, sizeof (n1), data, size, rd_le64 (data + e));
			bfsha_read_string (v1, sizeof (v1), data, size, rd_le64 (data + e + 8));
			fprintf (out, "        [%u] %s%s%s\n", i, n1[0] ? n1 : "<unnamed>", v1[0] ? " = " : "", v1);
		}
	}
	if (storage_arr && m->storage_count)
	{
		fprintf (out, "      storage_buffers:\n");
		for (uint i = 0; i < m->storage_count && storage_arr + (u64)i * sym_stride + sym_stride <= size; i++)
		{
			const u64 e = storage_arr + (u64)i * sym_stride;
			char n1[128] = "", v1[128] = "";
			bfsha_read_string (n1, sizeof (n1), data, size, rd_le64 (data + e));
			bfsha_read_string (v1, sizeof (v1), data, size, rd_le64 (data + e + 8));
			fprintf (out, "        [%u] %s%s%s\n", i, n1[0] ? n1 : "<unnamed>", v1[0] ? " = " : "", v1);
		}
	}
}

typedef enum
{
	WIIU_DICT_OPTION,
	WIIU_DICT_ATTRIB,
	WIIU_DICT_SAMPLER,
	WIIU_DICT_UBLOCK,
	WIIU_DICT_UNIFORM,
} wiiu_dict_kind_t;

static void decode_wiiu_resdict (FILE *out, const u8 *data, size_t size, u64 ptr_loc, s32 rel_off,
	wiiu_dict_kind_t kind, ccp indent)
{
	if (!rel_off)
		return;
	const s64 dict_off_s = (s64)ptr_loc + rel_off;
	if (dict_off_s < 0 || (u64)dict_off_s + 8 > size)
		return;
	const u64 dict_off = (u64)dict_off_s;
	const s32 num_nodes_signed = (s32) rd_be32 (data + dict_off + 4);
	if (num_nodes_signed <= 0 || num_nodes_signed > BFSHA_MAX_DICT_NODES)
		return;
	const u64 num_nodes = (u64) num_nodes_signed;
	const u64 nodes_base = dict_off + 8;
	if (nodes_base + (num_nodes + 1) * 16 > size)
		return;

	char sub_indent[288];
	snprintf (sub_indent, sizeof (sub_indent), "%s    ", indent);

	for (u64 i = 0; i < num_nodes; i++)
	{
		const u64 node_off = nodes_base + (i + 1) * 16;
		const s32 key_rel = (s32) rd_be32 (data + node_off + 8);
		const s32 val_rel = (s32) rd_be32 (data + node_off + 12);

		char name[256];
		bfsha_wiiu_read_string (name, sizeof (name), data, size, node_off + 8, key_rel);
		fprintf (out, "%s[%llu] %s", indent, (unsigned long long) i, name[0] ? name : "<unnamed>");

		s32 inner_dict_rel = 0;
		u64 inner_ptr_loc = 0;

		if (val_rel)
		{
			const s64 val_off_s = (s64)(node_off + 12) + val_rel;
			if (val_off_s >= 0 && (u64)val_off_s < size)
			{
				const u64 val_off = (u64)val_off_s;
				switch (kind)
				{
				case WIIU_DICT_OPTION:
					if (val_off + 24 <= size)
					{
						const u8 choice_count = data[val_off];
						const u8 default_choice = data[val_off + 1];
						const u16 block_offset = rd_be16 (data + val_off + 2);
						const u8 flags = data[val_off + 4];
						const u8 key_offset = data[val_off + 5];
						const u8 bit32_index = data[val_off + 6];
						const u8 bit32_shift = data[val_off + 7];
						const u32 bit32_mask = rd_be32 (data + val_off + 8);
						fprintf (out, " (choices=%u, default=%u, block_offset=%u, flags=0x%02x, key_offset=%u, bit_idx=%u, bit_shift=%u, mask=0x%08x)",
							choice_count, default_choice, block_offset, flags, key_offset, bit32_index, bit32_shift, bit32_mask);
						inner_dict_rel = (s32) rd_be32 (data + val_off + 16);
						inner_ptr_loc = val_off + 16;
					}
					break;
				case WIIU_DICT_ATTRIB:
					if (val_off + 4 <= size)
					{
						fprintf (out, " (index=%u, type=%u, count=%u, location=%d)",
							data[val_off], data[val_off + 1], data[val_off + 2], (int)(s8)data[val_off + 3]);
					}
					break;
				case WIIU_DICT_SAMPLER:
					if (val_off + 4 <= size)
					{
						fprintf (out, " (index=%u, type=%u, count=%u)",
							data[val_off], data[val_off + 1], data[val_off + 2]);
					}
					break;
				case WIIU_DICT_UBLOCK:
					if (val_off + 16 <= size)
					{
						const u8 index = data[val_off];
						const u8 type = data[val_off + 1];
						const u16 bsize = rd_be16 (data + val_off + 2);
						const u16 num_u = rd_be16 (data + val_off + 4);
						fprintf (out, " (index=%u, type=%u, size=%u, uniforms=%u)",
							index, type, bsize, num_u);
						inner_dict_rel = (s32) rd_be32 (data + val_off + 8);
						inner_ptr_loc = val_off + 8;
					}
					break;
				case WIIU_DICT_UNIFORM:
					if (val_off + 16 <= size)
					{
						const s32 idx = (s32) rd_be32 (data + val_off);
						const u16 gcount = rd_be16 (data + val_off + 4);
						const u8 gtype = data[val_off + 6];
						const u8 bidx = data[val_off + 7];
						const u16 doff = rd_be16 (data + val_off + 8);
						const u8 ptype = data[val_off + 10];
						fprintf (out, " (index=%d, count=%u, type=%u, block_index=%u, data_offset=%u, param_type=%u)",
							idx, gcount, gtype, bidx, doff, ptype);
					}
					break;
				}
			}
		}
		fprintf (out, "\n");

		if (inner_dict_rel)
		{
			decode_wiiu_resdict (out, data, size, inner_ptr_loc, inner_dict_rel,
				kind == WIIU_DICT_UBLOCK ? WIIU_DICT_UNIFORM : WIIU_DICT_OPTION, sub_indent);
		}
	}
}

static void decode_wiiu_programs (FILE *out, const u8 *data, size_t size, u64 progs_base,
	uint program_count, u8 vmajor, u8 vmicro, u8 static_key_len, u8 dynamic_key_len, s64 key_table_off)
{
	const bool is_new_layout = (vmajor >= 4 || (vmajor == 3 && vmicro >= 5));
	const u64 stride = is_new_layout ? 100 : 56;
	const uint n_keys = (uint)static_key_len + dynamic_key_len;

	const uint n_prog = program_count > BFSHA_MAX_PROGRAMS ? BFSHA_MAX_PROGRAMS : program_count;
	for (uint pi = 0; pi < n_prog; pi++)
	{
		const u64 p = progs_base + (u64)pi * stride;
		if (p + stride > size)
		{
			fprintf (out, "    program[%u]: <out of bounds>\n", pi);
			break;
		}

		const u16 flags = rd_be16 (data + p);
		const u8 sampler_count = data[p + 2];
		const u8 block_count = data[p + 3];
		const u32 used_attrs = rd_be32 (data + p + 4);

		s32 vx_data_rel = 0, px_data_rel = 0, geom_data_rel = 0;
		u64 vx_ptr_loc = 0, px_ptr_loc = 0, geom_ptr_loc = 0;

		if (is_new_layout)
		{
			vx_data_rel = (s32) rd_be32 (data + p + 76);
			vx_ptr_loc = p + 76;
			geom_data_rel = (s32) rd_be32 (data + p + 80);
			geom_ptr_loc = p + 80;
			px_data_rel = (s32) rd_be32 (data + p + 88);
			px_ptr_loc = p + 88;
		}
		else
		{
			vx_data_rel = (s32) rd_be32 (data + p + 40);
			vx_ptr_loc = p + 40;
			geom_data_rel = (s32) rd_be32 (data + p + 44);
			geom_ptr_loc = p + 44;
			px_data_rel = (s32) rd_be32 (data + p + 48);
			px_ptr_loc = p + 48;
		}

		fprintf (out, "    program[%u]: flags = 0x%04x, attr_flags = 0x%08x, n_samplers = %u, n_blocks = %u\n",
			pi, flags, used_attrs, sampler_count, block_count);

		if (vx_data_rel)
		{
			const s64 vx_off_s = (s64)vx_ptr_loc + vx_data_rel;
			if (vx_off_s >= 0 && (u64)vx_off_s + 216 <= size)
			{
				const u64 vx = (u64)vx_off_s;
				const u32 dsize = rd_be32 (data + vx + 208);
				const s32 doff = (s32) rd_be32 (data + vx + 212);
				const u32 mode = rd_be32 (data + vx + 216);
				const s64 bin_off = doff ? (s64)(vx + 212) + doff : 0;
				fprintf (out, "      vertex: size = %u, data_offset = %lld, mode = %u%s\n",
					dsize, (long long)bin_off, mode,
					bin_off < 0 || (u64)bin_off + dsize > size ? " <out of bounds>" : "");
			}
		}
		if (px_data_rel)
		{
			const s64 px_off_s = (s64)px_ptr_loc + px_data_rel;
			if (px_off_s >= 0 && (u64)px_off_s + 172 <= size)
			{
				const u64 px = (u64)px_off_s;
				const u32 dsize = rd_be32 (data + px + 164);
				const s32 doff = (s32) rd_be32 (data + px + 168);
				const u32 mode = rd_be32 (data + px + 172);
				const s64 bin_off = doff ? (s64)(px + 168) + doff : 0;
				fprintf (out, "      pixel: size = %u, data_offset = %lld, mode = %u%s\n",
					dsize, (long long)bin_off, mode,
					bin_off < 0 || (u64)bin_off + dsize > size ? " <out of bounds>" : "");
			}
		}
		if (geom_data_rel)
		{
			const s64 gx_off_s = (s64)geom_ptr_loc + geom_data_rel;
			if (gx_off_s >= 0 && (u64)gx_off_s + 172 <= size)
			{
				const u64 gx = (u64)gx_off_s;
				const u32 dsize = rd_be32 (data + gx + 164);
				const s32 doff = (s32) rd_be32 (data + gx + 168);
				const u32 mode = rd_be32 (data + gx + 172);
				const s64 bin_off = doff ? (s64)(gx + 168) + doff : 0;
				fprintf (out, "      geometry: size = %u, data_offset = %lld, mode = %u%s\n",
					dsize, (long long)bin_off, mode,
					bin_off < 0 || (u64)bin_off + dsize > size ? " <out of bounds>" : "");
			}
		}

		if (n_keys && n_keys <= BFSHA_MAX_KEYS && key_table_off > 0)
		{
			const u64 row = (u64)key_table_off + (u64)pi * n_keys * 4;
			if (row + (u64)n_keys * 4 <= size)
			{
				fprintf (out, "      keys =");
				for (uint k = 0; k < n_keys; k++)
					fprintf (out, " %d", (s32) rd_be32 (data + row + (u64)k * 4));
				fprintf (out, "\n");
			}
		}
	}
}

static enumError DecodeBFSHA_WiiU_Text (FILE *out, const u8 *data, size_t size)
{
	if (size < 48)
		return ERROR0 (ERR_INVALID_DATA, "BFSHA (Wii U): file shorter than header\n");

	const u8 version_major = data[4];
	const u8 version_micro = data[5];
	const u8 version_minor = data[7];
	const u16 bom = rd_be16 (data + 8);
	const u16 header_size = rd_be16 (data + 10);
	const u32 file_size = rd_be32 (data + 12);
	const u32 alignment = rd_be32 (data + 16);
	const s32 name_rel_off = (s32) rd_be32 (data + 20);
	const s32 path_rel_off = (s32) rd_be32 (data + 32);
	const u16 model_count = rd_be16 (data + 36);
	const u16 flag = rd_be16 (data + 38);
	const s32 models_dict_rel = (s32) rd_be32 (data + 44);

	char name[256], path[256];
	bfsha_wiiu_read_string (name, sizeof (name), data, size, 20, name_rel_off);
	bfsha_wiiu_read_string (path, sizeof (path), data, size, 32, path_rel_off);

	fprintf (out, "#BFSHA\n"
		"platform = Wii U\n"
		"version = %u.%u.%u\n"
		"byte_order_mark = 0x%04x\n"
		"header_size = %u\n"
		"alignment = %u\n"
		"file_size = %u\n"
		"name = %s\n"
		"path = %s\n"
		"flag = 0x%04x\n"
		"model_count = %u\n\n"
		"[shader_models]\n",
		version_major, version_minor, version_micro, bom, header_size,
		alignment, file_size, name, path, flag, model_count);

	if (!models_dict_rel)
	{
		fprintf (out, "  <no shader model dictionary>\n");
		return ERR_OK;
	}

	const s64 dict_off_s = 44 + (s64) models_dict_rel;
	if (dict_off_s < 0 || (u64)dict_off_s + 8 > size)
		return ERROR0 (ERR_INVALID_DATA, "BFSHA (Wii U): shader model dictionary offset out of bounds\n");

	const u64 dict_off = (u64) dict_off_s;
	const s32 num_nodes_signed = (s32) rd_be32 (data + dict_off + 4);
	if (num_nodes_signed < 0 || num_nodes_signed > BFSHA_MAX_MODELS)
	{
		fprintf (out, "  <shader model dictionary node count implausible: %d>\n", num_nodes_signed);
		return ERR_OK;
	}
	const u64 num_nodes = (u64) num_nodes_signed;
	const u64 nodes_base = dict_off + 8;
	if (nodes_base + (num_nodes + 1) * 16 > size)
		return ERROR0 (ERR_INVALID_DATA, "BFSHA (Wii U): shader model dictionary node table out of bounds\n");

	for (u64 i = 0; i < num_nodes; i++)
	{
		const u64 node_off = nodes_base + (i + 1) * 16;
		const s32 key_rel = (s32) rd_be32 (data + node_off + 8);
		const s32 val_rel = (s32) rd_be32 (data + node_off + 12);

		char model_name[256];
		bfsha_wiiu_read_string (model_name, sizeof (model_name), data, size, node_off + 8, key_rel);
		fprintf (out, "  [%llu] %s\n", (unsigned long long) i,
			model_name[0] ? model_name : "<unnamed>");

		if (!val_rel)
			continue;

		const s64 model_off_s = (s64)(node_off + 12) + val_rel;
		if (model_off_s < 0 || (u64)model_off_s + 112 > size)
		{
			fprintf (out, "    <model header out of bounds>\n");
			continue;
		}
		const u64 m_pos = (u64) model_off_s;

		const u8 static_key_len = data[m_pos + 0];
		const u8 dynamic_key_len = data[m_pos + 1];
		const u16 static_opt_count = rd_be16 (data + m_pos + 2);
		const u16 dynamic_opt_count = rd_be16 (data + m_pos + 4);
		const u16 program_count = rd_be16 (data + m_pos + 6);
		const u8 attrib_count = data[m_pos + 8];
		const u8 sampler_count = data[m_pos + 9];
		const u8 ublock_count = data[m_pos + 10];
		const u32 uniform_count = rd_be32 (data + m_pos + 20);
		const s32 default_prog_idx = (s32) rd_be32 (data + m_pos + 28);

		const s32 static_opts_dict_rel = (s32) rd_be32 (data + m_pos + 48);
		const s32 dynamic_opts_dict_rel = (s32) rd_be32 (data + m_pos + 56);
		const s32 attribs_dict_rel = (s32) rd_be32 (data + m_pos + 64);
		const s32 samplers_dict_rel = (s32) rd_be32 (data + m_pos + 72);
		const s32 ublocks_dict_rel = (s32) rd_be32 (data + m_pos + 80);
		const s32 programs_rel = (s32) rd_be32 (data + m_pos + 88);
		const s32 key_table_rel = (s32) rd_be32 (data + m_pos + 92);
		const s32 shader_archive_rel = (s32) rd_be32 (data + m_pos + 96);

		fprintf (out, "    static_opts = %u, dynamic_opts = %u, attribs = %u, samplers = %u,"
			" uniform_blocks = %u, uniforms = %u, programs = %u, default_program = %d\n",
			static_opt_count, dynamic_opt_count, attrib_count, sampler_count,
			ublock_count, uniform_count, program_count, default_prog_idx);
		fprintf (out, "    static_key_len = %u, dynamic_key_len = %u\n",
			static_key_len, dynamic_key_len);

		if (static_opts_dict_rel)
		{
			fprintf (out, "    static_options:\n");
			decode_wiiu_resdict (out, data, size, m_pos + 48, static_opts_dict_rel, WIIU_DICT_OPTION, "      ");
		}
		if (dynamic_opts_dict_rel)
		{
			fprintf (out, "    dynamic_options:\n");
			decode_wiiu_resdict (out, data, size, m_pos + 56, dynamic_opts_dict_rel, WIIU_DICT_OPTION, "      ");
		}
		if (attribs_dict_rel)
		{
			fprintf (out, "    attributes:\n");
			decode_wiiu_resdict (out, data, size, m_pos + 64, attribs_dict_rel, WIIU_DICT_ATTRIB, "      ");
		}
		if (samplers_dict_rel)
		{
			fprintf (out, "    samplers:\n");
			decode_wiiu_resdict (out, data, size, m_pos + 72, samplers_dict_rel, WIIU_DICT_SAMPLER, "      ");
		}
		if (ublocks_dict_rel)
		{
			fprintf (out, "    uniform_blocks:\n");
			decode_wiiu_resdict (out, data, size, m_pos + 80, ublocks_dict_rel, WIIU_DICT_UBLOCK, "      ");
		}

		if (shader_archive_rel)
		{
			const s64 sa_off = (s64)(m_pos + 96) + shader_archive_rel;
			if (sa_off >= 0 && (u64)sa_off < size)
				fprintf (out, "    shader_archive_offset = %llu\n", (unsigned long long) sa_off);
		}

		if (programs_rel && program_count)
		{
			const s64 progs_off_s = (s64)(m_pos + 88) + programs_rel;
			if (progs_off_s >= 0 && (u64)progs_off_s < size)
			{
				decode_wiiu_programs (out, data, size, (u64)progs_off_s, program_count,
					version_major, version_micro, static_key_len, dynamic_key_len,
					key_table_rel ? (s64)(m_pos + 92) + key_table_rel : 0);
			}
		}
	}

	return ERR_OK;
}

static enumError ScanBFSHA_WiiU_ModelRefs (bfsha_models_t *out, const u8 *data, size_t size)
{
	if (size < 48)
		return ERR_OK;

	const u8 version_major = data[4];
	const u8 version_micro = data[5];
	const s32 models_dict_rel = (s32) rd_be32 (data + 44);
	if (!models_dict_rel)
		return ERR_OK;

	const s64 dict_off_s = 44 + (s64) models_dict_rel;
	if (dict_off_s < 0 || (u64)dict_off_s + 8 > size)
		return ERR_OK;

	const u64 dict_off = (u64) dict_off_s;
	const s32 num_nodes_signed = (s32) rd_be32 (data + dict_off + 4);
	if (num_nodes_signed <= 0 || num_nodes_signed > BFSHA_MAX_MODELS)
		return ERR_OK;

	const u64 num_nodes = (u64) num_nodes_signed;
	const u64 nodes_base = dict_off + 8;
	if (nodes_base + (num_nodes + 1) * 16 > size)
		return ERR_OK;

	const bool is_new_layout = (version_major >= 4 || (version_major == 3 && version_micro >= 5));
	const u64 prog_stride = is_new_layout ? 100 : 56;

	bfsha_model_ref_t *models = CALLOC (num_nodes, sizeof (*models));

	for (u64 i = 0; i < num_nodes; i++)
	{
		const u64 node_off = nodes_base + (i + 1) * 16;
		const s32 key_rel = (s32) rd_be32 (data + node_off + 8);
		const s32 val_rel = (s32) rd_be32 (data + node_off + 12);
		bfsha_model_ref_t *ref = models + i;

		bfsha_wiiu_read_string (ref->name, sizeof (ref->name), data, size, node_off + 8, key_rel);
		if (!ref->name[0])
			snprintf (ref->name, sizeof (ref->name), "<unnamed>");

		if (!val_rel)
			continue;

		const s64 model_off_s = (s64)(node_off + 12) + val_rel;
		if (model_off_s < 0 || (u64)model_off_s + 112 > size)
			continue;
		const u64 m_pos = (u64) model_off_s;

		const u16 program_count = rd_be16 (data + m_pos + 6);
		const s32 programs_rel = (s32) rd_be32 (data + m_pos + 88);
		const s32 shader_archive_rel = (s32) rd_be32 (data + m_pos + 96);

		if (shader_archive_rel)
		{
			const s64 sa_off = (s64)(m_pos + 96) + shader_archive_rel;
			if (sa_off >= 0 && (u64)sa_off < size)
			{
				ref->bnsh_offset = (u64) sa_off;
				ref->bnsh_size = (u32)(size - (u64)sa_off);
				continue;
			}
		}

		if (programs_rel && program_count)
		{
			const s64 progs_off_s = (s64)(m_pos + 88) + programs_rel;
			if (progs_off_s >= 0 && (u64)progs_off_s + prog_stride <= size)
			{
				const u64 p0 = (u64) progs_off_s;
				const s32 vx_data_rel = (s32) rd_be32 (data + p0 + (is_new_layout ? 76 : 40));
				const u64 vx_ptr_loc = p0 + (is_new_layout ? 76 : 40);
				if (vx_data_rel)
				{
					const s64 vx_off_s = (s64)vx_ptr_loc + vx_data_rel;
					if (vx_off_s >= 0 && (u64)vx_off_s + 216 <= size)
					{
						const u64 vx = (u64) vx_off_s;
						const u32 dsize = rd_be32 (data + vx + 208);
						const s32 doff = (s32) rd_be32 (data + vx + 212);
						const s64 bin_off = doff ? (s64)(vx + 212) + doff : 0;
						if (bin_off > 0 && (u64)bin_off + dsize <= size)
						{
							ref->bnsh_offset = (u64) bin_off;
							ref->bnsh_size = dsize;
						}
					}
				}
			}
		}
	}

	out->n_models = num_nodes;
	out->models = models;
	return ERR_OK;
}

enumError DecodeBFSHA_Text (FILE *out, const u8 *data, size_t size)
{
	if (!out || !IsBFSHA (data, size))
		return ERR_INVALID_DATA;
	if (IsBFSHA_WiiU (data, size))
		return DecodeBFSHA_WiiU_Text (out, data, size);
	if (size < BFSHA_BIN_HDR_SIZE)
		return ERROR0 (ERR_INVALID_DATA, "BFSHA: file shorter than the fixed BinaryHeader\n");

	const u8 version_micro = data[8];
	const u8 version_minor = data[9];
	const u16 version_major = rd_le16 (data + 10);
	const u16 bom = rd_le16 (data + 12);
	const u8 alignment = data[14];
	const u8 target = data[15];
	const u32 reloc_table_offset = rd_le32 (data + 0x18);
	const u32 file_size = rd_le32 (data + 0x1c);

	u64 p = BFSHA_BIN_HDR_SIZE;
#define NEED(n) do { if ((u64)p + (n) > size) \
		return ERROR0 (ERR_INVALID_DATA, "BFSHA: archive header truncated at +0x%llx\n", \
			(unsigned long long)p); } while (0)

	NEED (8); p += 8;                                // unk0
	NEED (8); p += 8;                                // string_pool_offset
	NEED (8); p += 8;                                // shader_model_offset (unused by reference)
	NEED (8); const u64 name_ptr = rd_le64 (data + p); p += 8;
	NEED (8); const u64 path_ptr = rd_le64 (data + p); p += 8;
	NEED (8); const u64 models_array_off = rd_le64 (data + p); p += 8;
	NEED (8); const u64 models_dict_off  = rd_le64 (data + p); p += 8;
	NEED (16); p += 16;                              // 4x u32 unknown
	NEED (8); p += 8;                                // unknown u64
	if (version_major >= 7) { NEED (8); p += 8; }    // padding, must be 0
	NEED (2); const u16 model_count = rd_le16 (data + p); p += 2;
	NEED (2); const u16 flag = rd_le16 (data + p); p += 2;
#undef NEED

	char name[256], path[256];
	bfsha_read_string (name, sizeof (name), data, size, name_ptr);
	bfsha_read_string (path, sizeof (path), data, size, path_ptr);

	fprintf (out, "#BFSHA\n"
		"version = %u.%u.%u\n"
		"byte_order_mark = 0x%04x\n"
		"alignment = %u\n"
		"target = %u\n"
		"relocation_table_offset = %u\n"
		"file_size = %u\n"
		"name = %s\n"
		"path = %s\n"
		"flag = 0x%04x\n"
		"model_count = %u\n\n"
		"[shader_models]\n",
		version_major, version_minor, version_micro, bom, alignment, target,
		reloc_table_offset, file_size, name, path, flag, model_count);

	if (!models_dict_off)
	{
		fprintf (out, "  <no shader model dictionary>\n");
		return ERR_OK;
	}
	if ((u64)models_dict_off + 8 > size)
		return ERROR0 (ERR_INVALID_DATA, "BFSHA: shader model dictionary offset out of bounds\n");

	const s32 num_nodes_signed = (s32) rd_le32 (data + models_dict_off + 4);
	if (num_nodes_signed < 0 || num_nodes_signed > BFSHA_MAX_MODELS)
	{
		fprintf (out, "  <shader model dictionary node count implausible: %d>\n", num_nodes_signed);
		return ERR_OK;
	}
	const u64 num_nodes = (u64) num_nodes_signed;
	const u64 nodes_base = models_dict_off + 8;
	if (nodes_base + (num_nodes + 1) * 16 > size)
		return ERROR0 (ERR_INVALID_DATA, "BFSHA: shader model dictionary node table out of bounds\n");

	// Model records are fixed-size per version, laid out sequentially -- probe entry 0 once to
	// learn that stride, then every other entry's base is a plain multiple of it.
	u64 model_stride = 0;
	bfsha_model_hdr_t mh0;
	if (models_array_off)
		model_stride = decode_shader_model_header (out, data, size, models_array_off, version_major, &mh0);

	for (u64 i = 0; i < num_nodes; i++)
	{
		// Node 0 is the dictionary root and carries no key/value; models start at node 1.
		const u64 node_off = nodes_base + (i + 1) * 16;
		const u64 key_ptr = rd_le64 (data + node_off + 8);
		char model_name[256];
		bfsha_read_string (model_name, sizeof (model_name), data, size, key_ptr);

		fprintf (out, "  [%llu] %s\n", (unsigned long long) i,
			model_name[0] ? model_name : "<unnamed>");

		if (!models_array_off || !model_stride)
			continue;

		bfsha_model_hdr_t mh;
		const u64 model_off = models_array_off + i * model_stride;
		if (i == 0)
			mh = mh0;
		else if (!decode_shader_model_header (out, data, size, model_off, version_major, &mh))
			continue;

		fprintf (out, "    static_opts = %u, dynamic_opts = %u, attribs = %u, samplers = %u,"
			" uniform_blocks = %u, programs = %u, default_program = %d\n",
			mh.static_opt_count, mh.dynamic_opt_count, mh.attrib_count, mh.sampler_count,
			mh.ublock_count, mh.program_count, mh.default_program_idx);
		fprintf (out, "    static_key_len = %u, dynamic_key_len = %u,"
			" key_table_offset = %llu, program_array_offset = %llu\n",
			mh.static_key_len, mh.dynamic_key_len,
			(unsigned long long) mh.key_table_off, (unsigned long long) mh.program_array_off);

		if (mh.static_opts_dict)
		{
			fprintf (out, "    static_options:\n");
			decode_resdict (out, data, size, mh.static_opts_dict, mh.static_opts_arr,
				BFSHA_DICT_OPTION, "      ");
		}
		if (mh.dynamic_opts_dict)
		{
			fprintf (out, "    dynamic_options:\n");
			decode_resdict (out, data, size, mh.dynamic_opts_dict, mh.dynamic_opts_arr,
				BFSHA_DICT_OPTION, "      ");
		}
		if (mh.attribs_dict)
		{
			fprintf (out, "    attributes:\n");
			decode_resdict (out, data, size, mh.attribs_dict, mh.attribs_arr,
				BFSHA_DICT_ATTRIB, "      ");
		}
		if (mh.samplers_dict)
		{
			fprintf (out, "    samplers:\n");
			decode_resdict (out, data, size, mh.samplers_dict, mh.samplers_arr,
				BFSHA_DICT_SAMPLER, "      ");
		}
		if (version_major >= 8 && mh.images_dict)
		{
			fprintf (out, "    images:\n");
			decode_resdict (out, data, size, mh.images_dict, mh.images_arr,
				BFSHA_DICT_IMAGE, "      ");
		}
		if (mh.ublocks_dict)
		{
			fprintf (out, "    uniform_blocks:\n");
			decode_resdict (out, data, size, mh.ublocks_dict, mh.ublocks_arr,
				BFSHA_DICT_UBLOCK, "      ");
		}

		if (mh.symbol_info_off)
			decode_switch_symbol_table (out, data, size, mh.symbol_info_off, version_major, &mh);

		if (mh.shader_file_off && (u64)mh.shader_file_off + 0x20 <= size)
		{
			const u32 bnsh_size = rd_le32 (data + mh.shader_file_off + 0x1c);
			fprintf (out, "    bnsh: offset = %llu, size = %u%s\n",
				(unsigned long long) mh.shader_file_off, bnsh_size,
				(u64)mh.shader_file_off + bnsh_size > size ? " <out of bounds>" : "");
		}

		if (!mh.program_array_off || !mh.program_count)
			continue;

		const uint n_prog = mh.program_count > BFSHA_MAX_PROGRAMS ? BFSHA_MAX_PROGRAMS : mh.program_count;
		if (n_prog < mh.program_count)
			fprintf (out, "    <program_count truncated for listing>\n");

		const u64 prog_stride = version_major >= 8 ? 0x40 : version_major >= 5 ? 0x38 : 0x30;
		for (uint pi = 0; pi < n_prog; pi++)
		{
			const u64 prog_base = mh.program_array_off + (u64) pi * prog_stride;
			if (prog_base < mh.program_array_off) // 64-bit wrap guard
			{
				fprintf (out, "    program[%u]: <offset overflow>\n", pi);
				break;
			}
			decode_shader_program (out, data, size, prog_base, version_major, &mh, pi);
		}
	}

	return ERR_OK;
}

enumError ScanBFSHA_ModelRefs (bfsha_models_t *out, const u8 *data, size_t size)
{
	if (!out)
		return ERR_INVALID_DATA;
	out->n_models = 0;
	out->models = NULL;

	if (!IsBFSHA (data, size))
		return ERR_INVALID_DATA;
	if (IsBFSHA_WiiU (data, size))
		return ScanBFSHA_WiiU_ModelRefs (out, data, size);
	if (size < BFSHA_BIN_HDR_SIZE)
		return ERR_INVALID_DATA;

	const u16 version_major = rd_le16 (data + 10);

	u64 p = BFSHA_BIN_HDR_SIZE;
#define NEED(n) do { if ((u64)p + (n) > size) return ERR_OK; } while (0)
	NEED (8); p += 8;                                // unk0
	NEED (8); p += 8;                                // string_pool_offset
	NEED (8); p += 8;                                // shader_model_offset (unused by reference)
	NEED (8); p += 8;                                // name_ptr
	NEED (8); p += 8;                                // path_ptr
	NEED (8); const u64 models_array_off = rd_le64 (data + p); p += 8;
	NEED (8); const u64 models_dict_off  = rd_le64 (data + p); p += 8;
#undef NEED

	if (!models_array_off || !models_dict_off || (u64)models_dict_off + 8 > size)
		return ERR_OK;

	const s32 num_nodes_signed = (s32) rd_le32 (data + models_dict_off + 4);
	if (num_nodes_signed <= 0 || num_nodes_signed > BFSHA_MAX_MODELS)
		return ERR_OK;
	const u64 num_nodes = (u64) num_nodes_signed;
	const u64 nodes_base = models_dict_off + 8;
	if (nodes_base + (num_nodes + 1) * 16 > size)
		return ERR_OK;

	bfsha_model_hdr_t mh0;
	const u64 model_stride = decode_shader_model_header (0, data, size, models_array_off,
		version_major, &mh0);
	if (!model_stride)
		return ERR_OK;

	bfsha_model_ref_t *models = CALLOC (num_nodes, sizeof (*models));

	for (u64 i = 0; i < num_nodes; i++)
	{
		// Node 0 is the dictionary root and carries no key/value; models start at node 1.
		const u64 node_off = nodes_base + (i + 1) * 16;
		const u64 key_ptr = rd_le64 (data + node_off + 8);
		bfsha_model_ref_t *ref = models + i;

		bfsha_read_string (ref->name, sizeof (ref->name), data, size, key_ptr);
		if (!ref->name[0])
			snprintf (ref->name, sizeof (ref->name), "<unnamed>");

		bfsha_model_hdr_t mh;
		const u64 model_off = models_array_off + i * model_stride;
		if (i == 0)
			mh = mh0;
		else if (!decode_shader_model_header (0, data, size, model_off, version_major, &mh))
			continue;

		if (mh.shader_file_off && (u64)mh.shader_file_off + 0x20 <= size)
		{
			const u32 bnsh_size = rd_le32 (data + mh.shader_file_off + 0x1c);
			if ((u64)mh.shader_file_off + bnsh_size <= size)
			{
				ref->bnsh_offset = mh.shader_file_off;
				ref->bnsh_size   = bnsh_size;
			}
		}
	}

	out->n_models = num_nodes;
	out->models   = models;
	return ERR_OK;
}

void ResetBFSHA_ModelRefs (bfsha_models_t *out)
{
	if (out)
	{
		FREE (out->models);
		out->models   = NULL;
		out->n_models = 0;
	}
}
