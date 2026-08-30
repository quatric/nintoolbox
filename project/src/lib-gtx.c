// GTX/GSH (Wii U "Gfx2" texture/shader container) -- see lib-gtx.h.
//
// The tiled-surface addressing here is a scoped port of AMD's addrlib as
// used by GX2 on Wii U hardware (fixed 2 pipes / 4 banks -- Wii U's Latte
// GPU never uses any other pipe/bank config, unlike desktop AMD parts, so
// the general parametrized addrlib is overkill; the bit shifts below bake
// those constants in directly, matching the reference implementation
// (aboood40091/GTX-Extractor's addrlib.py, itself derived from the real
// AMD/Cemu addrlib) exactly).
//
// The RGBA decoder supports linear modes 0/1, micro-tiled modes 2/3, and
// all 2D/2B macro modes 4-11. The latter include aspect ratios 1/2/4,
// bank-swapped addressing, and GX2 pipe/bank swizzle bits. The math is
// differential-tested against GTX-Extractor's independent addrlib port.
// The legacy image API remains level-0 and 2D-compatible. Its extended
// sibling covers 3D tile modes 12-15, thick slices, arrays/cube faces,
// samples and depth ordering without flattening subresources together.
//
// One real simplification found while porting: the reference's own
// deswizzle() is called with the FILE's own stored `pitch` field, not a
// freshly recomputed/aligned one -- and for every real BC-format sample
// checked, the stored pitch already equals what a full addrlib recompute
// would produce. So this port trusts the file's stored pitch directly
// rather than re-deriving alignment from scratch (which would have pulled
// in a much larger slice of addrlib's macro-tile-alignment maze for no
// verified benefit).

#include "lib-std.h"
#include "lib-gtx.h"
#include "lib-bntx.h"
#include "latte-decaf/latte_bridge.h"

static inline u32 grd32 (const u8 *p)
{
	return (u32)p[0] << 24 | (u32)p[1] << 16 | (u32)p[2] << 8 | (u32)p[3];
}

static inline void gwr32 (u8 *p, u32 v)
{
	p[0] = (u8)(v >> 24);
	p[1] = (u8)(v >> 16);
	p[2] = (u8)(v >> 8);
	p[3] = (u8)v;
}

static inline u32 glr32 (const u8 *p)
{
	return (u32)p[0] | (u32)p[1] << 8 | (u32)p[2] << 16 | (u32)p[3] << 24;
}

static inline void glw32 (u8 *p, u32 v)
{
	p[0] = (u8)v;
	p[1] = (u8)(v >> 8);
	p[2] = (u8)(v >> 16);
	p[3] = (u8)(v >> 24);
}

static inline uint div_round_up (uint n, uint d)
{
	return d ? (n + d - 1) / d : 0;
}

#define GTX_MAX_OUTPUT (256u << 20)

//-----------------------------------------------------------------------------
///////////////       Latte shader control flow       ///////////////
//-----------------------------------------------------------------------------

static ccp latte_cf_name (uint op)
{
	static const ccp name[] = { "NOP", "TEX", "VTX", "VTX_TC", "LOOP_START", "LOOP_END",
		"LOOP_START_DX10", "LOOP_START_NO_AL", "LOOP_CONTINUE", "LOOP_BREAK", "JUMP", "PUSH",
		"PUSH_ELSE", "ELSE", "POP", "POP_JUMP", "POP_PUSH", "POP_PUSH_ELSE", "CALL", "CALL_FS",
		"RETURN", "EMIT_VERTEX", "EMIT_CUT_VERTEX", "CUT_VERTEX", "KILL", "END_PROGRAM", "WAIT_ACK",
		"TEX_ACK", "VTX_ACK", "VTX_TC_ACK", "TC", "VC", "GDS", "TC_ACK", "VC_ACK", "JUMPTABLE",
		"GLOBAL_WAVE_SYNC", "HALT", "END", "LDS_DEALLOC", "PUSH_WQM", "POP_WQM", "ELSE_WQM",
		"JUMP_ANY", "REACTIVATE", "REACTIVATE_WQM", "INTERRUPT", "INTERRUPT_AND_SLEEP",
		"SET_PRIORITY" };
	return op < sizeof (name) / sizeof (*name) ? name[op] : "UNKNOWN";
}

enumError DisassembleLatteCF (char **text, const u8 *program, uint size)
{
	if (!text || !program || !size || size % 8)
		return EINVAL;
	char *semantic = szs_latte_disassemble (program, size);
	const uint semantic_len = semantic ? strlen (semantic) : 0;
	size_t rebuilt_size = 0;
	u8 *rebuilt = semantic ? szs_latte_assemble (semantic, semantic_len, &rebuilt_size) : 0;
	const bool semantic_mode = rebuilt && rebuilt_size == size && !memcmp (rebuilt, program, size);
	szs_latte_free (rebuilt);
	const u64 cap = (u64)(size / 8) * 176 + semantic_len + 192;
	if (cap > GTX_MAX_OUTPUT)
		return EFBIG;
	char *out = MALLOC ((size_t)cap);
	if (!out)
		return ERR_CANT_CREATE;
	uint used = 0;
	used += snprintf (out + used, (size_t)cap - used,
		"; Latte ISA disassembly (Decaf GPL-3.0)\n; assembly-mode: %s\n%s%s; decoded control-flow "
		"words\n",
		semantic_mode ? "semantic" : "raw",
		semantic ? semantic : "; semantic decoder rejected this program\n",
		semantic && semantic_len && semantic[semantic_len - 1] != '\n' ? "\n" : "");
	szs_latte_free (semantic);
	for (uint off = 0; off < size; off += 8)
	{
		const u32 w0 = glr32 (program + off), w1 = glr32 (program + off + 4);
		const uint type = (w1 >> 28) & 3, op = type < 2 ? (w1 >> 23) & 0x7f : (w1 >> 26) & 15;
		used += snprintf (out + used, (size_t)cap - used,
			"CF[%04x] %-20s type=%u addr=%#x word0=%#010x word1=%#010x\n", off / 8,
			type == 0		? latte_cf_name (op)
				: type == 1 ? "EXPORT"
				: type == 2 ? "ALU"
							: "ALU_EXT",
			type, w0, w0, w1);
		if (type < 2 && (w1 & (1u << 21)))
			break;
	}
	used += snprintf (
		out + used, (size_t)cap - used, "; lossless full-program words (assembler input)\n");
	for (uint off = 0; off < size; off += 8)
	{
		const u32 w0 = glr32 (program + off), w1 = glr32 (program + off + 4);
		used += snprintf (out + used, (size_t)cap - used, "RAW[%04x] word0=%#010x word1=%#010x\n",
			off / 8, w0, w1);
	}
	*text = out;
	return ERR_OK;
}

enumError AssembleLatteCF (u8 **program, uint *size, const char *text)
{
	if (!program || !size || !text)
		return EINVAL;
	if (strstr (text, "; assembly-mode: semantic"))
	{
		ccp end = strstr (text, "; decoded control-flow words");
		if (!end)
			return EINVAL;
		size_t assembled_size = 0;
		u8 *assembled = szs_latte_assemble (text, (size_t)(end - text), &assembled_size);
		if (!assembled || !assembled_size || assembled_size > UINT_MAX
			|| assembled_size > GTX_MAX_OUTPUT)
		{
			szs_latte_free (assembled);
			return ERR_INVALID_DATA;
		}
		u8 *out = MALLOC (assembled_size);
		if (!out)
		{
			szs_latte_free (assembled);
			return ERR_CANT_CREATE;
		}
		memcpy (out, assembled, assembled_size);
		szs_latte_free (assembled);
		*program = out;
		*size = (uint)assembled_size;
		return ERR_OK;
	}

	// Semantic lines are intentionally human-readable.  RAW lines retain every
	// instruction and clause word, allowing the emitted file to be recompiled
	// byte-for-byte even when it contains opcodes unknown to this decoder.
	uint count = 0;
	for (ccp p = text; (p = strstr (p, "RAW[")); p += 4)
		count++;
	if (!count || (u64)count * 8 > GTX_MAX_OUTPUT)
		return EINVAL;
	u8 *out = MALLOC ((size_t)count * 8);
	if (!out)
		return ERR_CANT_CREATE;
	uint n = 0;
	for (ccp p = text; (p = strstr (p, "RAW[")); p += 4)
	{
		ccp words = strstr (p, "word0=");
		if (!words)
		{
			FREE (out);
			return EINVAL;
		}
		char *end;
		const u32 w0 = (u32)strtoul (words + 6, &end, 0);
		if (end == words + 6)
		{
			FREE (out);
			return EINVAL;
		}
		ccp q = strstr (end, "word1=");
		if (!q)
		{
			FREE (out);
			return EINVAL;
		}
		const u32 w1 = (u32)strtoul (q + 6, &end, 0);
		if (end == q + 6)
		{
			FREE (out);
			return EINVAL;
		}
		glw32 (out + 8 * n, w0);
		glw32 (out + 8 * n + 4, w1);
		n++;
	}
	*program = out;
	*size = n * 8;
	return ERR_OK;
}

//-----------------------------------------------------------------------------
///////////////		container parsing			///////////////
//-----------------------------------------------------------------------------

void ResetGTX (gtx_t *gtx)
{
	if (!gtx)
		return;
	FREE (gtx->blocks);
	FREE (gtx->textures);
	FREE (gtx->shaders);
	memset (gtx, 0, sizeof (*gtx));
}

// Block types: v6.0 uses 0x0A for the texture header (surfBlkType), v6.1/
// v7.x use 0x0B -- confirmed against the reference tool's own version gate.
enumError ScanGTX (gtx_t *gtx, const u8 *data, uint size)
{
	if (!gtx || !data || size < 32 || memcmp (data, "Gfx2", 4))
		return EINVAL;

	const u32 hdr_size = grd32 (data + 4);
	const u32 vmajor = grd32 (data + 8);
	const u32 vminor = grd32 (data + 12);
	const u32 gpu_ver = grd32 (data + 16);
	if (hdr_size != 32 || gpu_ver != 2)
		return EINVAL;

	uint surf_type;
	if (vmajor == 6 && vminor == 0)
		surf_type = 0x0A;
	else if (vmajor == 6 || vmajor == 7)
		surf_type = 0x0B;
	else
		return EINVAL;

	memset (gtx, 0, sizeof (*gtx));
	gtx->data = data;
	gtx->size = size;
	gtx->version_major = vmajor;
	gtx->version_minor = vminor;
	gtx->gpu_version = gpu_ver;
	gtx->alignment = grd32 (data + 20);

	gtx_texture_t *list = 0;
	uint n = 0, cap = 0;
	gtx_block_t *blocks = 0;
	uint nb = 0, bcap = 0;

	uint pos = hdr_size;
	while (pos + 32 <= size)
	{
		if (memcmp (data + pos, "BLK{", 4))
			goto invalid;
		const u32 blk_hdrsize = grd32 (data + pos + 4);
		const u32 blk_type = grd32 (data + pos + 16);
		const u32 blk_dsize = grd32 (data + pos + 20);
		if (blk_hdrsize < 32 || (u64)pos + blk_hdrsize + blk_dsize > size)
			goto invalid;
		const uint data_pos = pos + blk_hdrsize;
		if (nb >= bcap)
		{
			bcap = bcap ? bcap * 2 : 16;
			gtx_block_t *grown = REALLOC (blocks, bcap * sizeof (*blocks));
			if (!grown)
				goto nomem;
			blocks = grown;
		}
		blocks[nb++] = (gtx_block_t) { blk_type, pos, blk_hdrsize, blk_dsize, data + data_pos };

		if (blk_type == surf_type)
		{
			// GX2Texture: GX2Surface (16 u32) + 13 mip-offset entries +
			// viewFirstMip/NumMips/FirstSlice/NumSlices (4 u32) + compSel
			// (4 bytes) + texRegs (5 u32) = 156 bytes total.
			if (blk_dsize < 136)
				goto invalid;
			const u32 dim = grd32 (data + data_pos + 0);
			const u32 width = grd32 (data + data_pos + 4);
			const u32 height = grd32 (data + data_pos + 8);
			const u32 depth = grd32 (data + data_pos + 12);
			const u32 num_mips = grd32 (data + data_pos + 16);
			const u32 format = grd32 (data + data_pos + 20);
			const u32 aa = grd32 (data + data_pos + 24);
			const u32 use = grd32 (data + data_pos + 28);
			const u32 tile_mode = grd32 (data + data_pos + 48);
			const u32 swizzle = grd32 (data + data_pos + 52);
			const u32 pitch = grd32 (data + data_pos + 60);

			if (n >= cap)
			{
				cap = cap ? cap * 2 : 4;
				gtx_texture_t *grown = REALLOC (list, cap * sizeof (*list));
				if (!grown)
				{
					FREE (list);
					return ERR_CANT_CREATE;
				}
				list = grown;
			}
			memset (list + n, 0, sizeof (*list));
			list[n].dim = dim;
			list[n].width = width;
			list[n].height = height;
			list[n].depth = depth ? depth : 1;
			list[n].num_mips = num_mips;
			list[n].format = format;
			list[n].aa = aa;
			list[n].use = use;
			list[n].tile_mode = tile_mode;
			list[n].swizzle = swizzle;
			list[n].pitch = pitch;
			for (uint i = 0; i < 13; i++)
				list[n].mip_offsets[i] = grd32 (data + data_pos + 64 + 4 * i);
			list[n].view_first_mip = grd32 (data + data_pos + 116);
			list[n].view_num_mips = grd32 (data + data_pos + 120);
			list[n].view_first_slice = grd32 (data + data_pos + 124);
			list[n].view_num_slices = grd32 (data + data_pos + 128);
			memcpy (list[n].comp_sel, data + data_pos + 132, 4);
			n++;
		}
		else if (blk_type == surf_type + 1 && n > 0)
		{
			// image data block, immediately follows its texture header block
			list[n - 1].data = data + data_pos;
			list[n - 1].data_size = blk_dsize;
		}
		else if (blk_type == surf_type + 2 && n > 0)
		{
			list[n - 1].mip_data = data + data_pos;
			list[n - 1].mip_data_size = blk_dsize;
		}

		pos = data_pos + blk_dsize;
		if (blk_type == 1)
		{
			if (blk_dsize)
				goto invalid;
			break;
		}
	}

	if (pos != size && (pos + 32 != size || memcmp (data + pos, "BLK{", 4)))
		goto invalid;
	// Associate shader header/program blocks without interpreting the
	// version-dependent GX2 shader structs. The original bytes remain
	// available through blocks[] for lossless extraction/re-emission.
	uint ns = 0;
	for (uint i = 0; i < nb; i++)
		if (blocks[i].type == 3 || blocks[i].type == 6 || blocks[i].type == 8
			|| blocks[i].type == 14)
			ns++;
	gtx_shader_t *shaders = ns ? CALLOC (ns, sizeof (*shaders)) : 0;
	if (ns && !shaders)
		goto nomem;
	uint si = 0;
	for (uint i = 0; i < nb; i++)
	{
		gtx_shader_stage_t stage;
		uint program_type, copy_type = 0;
		switch (blocks[i].type)
		{
			case 3:
				stage = GTX_SHADER_VERTEX;
				program_type = 5;
				break;
			case 6:
				stage = GTX_SHADER_PIXEL;
				program_type = 7;
				break;
			case 8:
				stage = GTX_SHADER_GEOMETRY;
				program_type = 9;
				copy_type = 10;
				break;
			case 14:
				stage = GTX_SHADER_COMPUTE;
				program_type = 15;
				break;
			default:
				continue;
		}
		gtx_shader_t *s = shaders + si++;
		s->stage = stage;
		s->header = blocks + i;
		if (blocks[i].data_size >= 0x28)
		{
			const u8 *ri = blocks[i].data + blocks[i].data_size - 0x28;
			if (!memcmp (ri, "}BLK", 4) && grd32 (ri + 4) == 0x28)
			{
				const uint str_size = grd32 (ri + 0x14), str_tag = grd32 (ri + 0x18);
				const uint reloc_count = grd32 (ri + 0x20), reloc_tag = grd32 (ri + 0x24);
				const uint str_off = str_tag & 0xfffff, reloc_off = reloc_tag & 0xfffff;
				if ((str_tag >> 20) == 0xd06 && (u64)str_off + str_size <= blocks[i].data_size)
					s->string_table = blocks[i].data + str_off, s->string_table_size = str_size;
				if ((reloc_tag >> 20) == 0xd06
					&& (u64)reloc_off + 4ull * reloc_count <= blocks[i].data_size)
					s->relocations = blocks[i].data + reloc_off, s->n_relocations = reloc_count;
			}
		}
		for (uint j = i + 1; j < nb; j++)
		{
			if (blocks[j].type == program_type && !s->program)
				s->program = blocks + j;
			if (copy_type && blocks[j].type == copy_type && !s->copy_program)
				s->copy_program = blocks + j;
			if (blocks[j].type == 3 || blocks[j].type == 6 || blocks[j].type == 8
				|| blocks[j].type == 14)
				break;
		}
	}
	gtx->blocks = blocks;
	gtx->n_blocks = nb;
	gtx->textures = list;
	gtx->n_textures = n;
	gtx->shaders = shaders;
	gtx->n_shaders = ns;
	return ERR_OK;

nomem:
	FREE (blocks);
	FREE (list);
	return ERR_CANT_CREATE;
invalid:
	FREE (blocks);
	FREE (list);
	return EINVAL;
}

//-----------------------------------------------------------------------------
///////////////		GX2 tiled-surface addressing		///////////////
//-----------------------------------------------------------------------------

static uint gx2_surface_thickness (uint tile_mode)
{
	if (tile_mode == 3 || tile_mode == 7 || tile_mode == 11 || tile_mode == 13 || tile_mode == 15)
		return 4;
	if (tile_mode == 16 || tile_mode == 17)
		return 8;
	return 1;
}

static bool gx2_is_thick_macro (uint tile_mode)
{
	return tile_mode == 7 || tile_mode == 11 || tile_mode == 13 || tile_mode == 15;
}

static bool gx2_is_bank_swapped (uint tile_mode)
{
	return tile_mode == 8 || tile_mode == 9 || tile_mode == 10 || tile_mode == 11 || tile_mode == 14
		|| tile_mode == 15;
}

// Macro-tile aspect ratio: 2D/2B_TILED_THIN2 (5/9) halve the tile width and
// double the height (ratio 2); THIN4 (6/10) do it by a factor of 4; every
// other macro tile mode (4/7/8/11, the THIN1/THICK pair) is aspect ratio 1.
// Port of aboood40091/BFRES-Tool's addrlib.py computeMacroTileAspectRatio()
// (itself a from-scratch Python reimplementation of the real AMD/GX2
// addrlib algorithm, GPL-3.0, github.com/aboood40091/BFRES-Tool,
// addrlib/addrlib.py) -- see the file header for the existing prior art
// this file already ports the same way.
static uint gx2_macro_tile_aspect (uint tile_mode)
{
	if (tile_mode == 5 || tile_mode == 9)
		return 2;
	if (tile_mode == 6 || tile_mode == 10)
		return 4;
	return 1;
}

// Bank-swap order lookup used below, straight from the same reference.
static const uint gx2_bank_swap_order[8] = { 0, 1, 3, 2, 6, 7, 5, 4 };

// Width (in macro tiles) at which the bank-swap XOR pattern below repeats,
// for the bank-swapped tile modes (8/9/10/11/14/15) only -- 0 for anything
// else (matches the reference's own "not bank swapped -> 0" early return).
// numSamples is hardcoded to 1: this decoder has no MSAA/depth-surface
// support, matching the rest of this file's single-sample-2D-texture scope.
// Port of the same addrlib.py's computeSurfaceBankSwappedWidth().
static uint gx2_bank_swapped_width (uint tile_mode, uint bpp, uint pitch, uint num_samples)
{
	if (!gx2_is_bank_swapped (tile_mode))
		return 0;

	uint bytes_per_sample = 8 * bpp;
	uint slices_per_tile = 1;
	if (bytes_per_sample)
	{
		const uint samples_per_tile = 2048 / bytes_per_sample;
		slices_per_tile = samples_per_tile ? num_samples / samples_per_tile : 0;
		if (!slices_per_tile)
			slices_per_tile = 1;
	}

	uint eff_samples = num_samples;
	if (gx2_is_thick_macro (tile_mode))
		eff_samples = 4;

	const uint bytes_per_tile_slice = eff_samples * bytes_per_sample / slices_per_tile;
	const uint factor = gx2_macro_tile_aspect (tile_mode);
	const uint swap_tiles = bpp ? (128 / bpp > 1 ? 128 / bpp : 1) : 1;
	const uint swap_width = swap_tiles * 32;
	const uint height_bytes = eff_samples * factor * bpp * 2 / slices_per_tile;
	const uint swap_max = height_bytes ? 0x4000 / height_bytes : 0;
	const uint swap_min = bytes_per_tile_slice ? 256 / bytes_per_tile_slice : 0;

	uint inner = swap_width > swap_min ? swap_width : swap_min;
	uint bank_swap_width = inner < swap_max ? inner : swap_max;
	while (pitch && bank_swap_width >= 2 * pitch)
		bank_swap_width >>= 1;
	return bank_swap_width;
}

// Bit-permutation table for the pixel's position within its 8x8 micro-tile,
// keyed by bpp (bits per element -- 8/16/32/64/128, matching the reference's
// own bpp-keyed branch table exactly).
static uint gx2_pixel_index_in_microtile_ex (
	uint x, uint y, uint z, uint bpp, uint tile_mode, bool is_depth)
{
	uint b0, b1, b2, b3, b4, b5;
	if (is_depth)
	{
		b0 = x & 1;
		b1 = y & 1;
		b2 = (x >> 1) & 1;
		b3 = (y >> 1) & 1;
		b4 = (x >> 2) & 1;
		b5 = (y >> 2) & 1;
	}
	else
		switch (bpp)
		{
			case 8:
				b0 = x & 1;
				b1 = (x >> 1) & 1;
				b2 = (x >> 2) & 1;
				b3 = (y >> 1) & 1;
				b4 = y & 1;
				b5 = (y >> 2) & 1;
				break;
			case 0x10:
				b0 = x & 1;
				b1 = (x >> 1) & 1;
				b2 = (x >> 2) & 1;
				b3 = y & 1;
				b4 = (y >> 1) & 1;
				b5 = (y >> 2) & 1;
				break;
			case 0x20:
			case 0x60:
				b0 = x & 1;
				b1 = (x >> 1) & 1;
				b2 = y & 1;
				b3 = (x >> 2) & 1;
				b4 = (y >> 1) & 1;
				b5 = (y >> 2) & 1;
				break;
			case 0x40:
				b0 = x & 1;
				b1 = y & 1;
				b2 = (x >> 1) & 1;
				b3 = (x >> 2) & 1;
				b4 = (y >> 1) & 1;
				b5 = (y >> 2) & 1;
				break;
			case 0x80:
				b0 = y & 1;
				b1 = x & 1;
				b2 = (x >> 1) & 1;
				b3 = (x >> 2) & 1;
				b4 = (y >> 1) & 1;
				b5 = (y >> 2) & 1;
				break;
			default:
				b0 = x & 1;
				b1 = (x >> 1) & 1;
				b2 = y & 1;
				b3 = (x >> 2) & 1;
				b4 = (y >> 1) & 1;
				b5 = (y >> 2) & 1;
				break;
		}
	uint result = 32 * b5 | 16 * b4 | 8 * b3 | 4 * b2 | b0 | 2 * b1;
	const uint thickness = gx2_surface_thickness (tile_mode);
	if (thickness > 1)
		result |= (z & 3) << 6;
	if (thickness == 8)
		result |= (z & 4) << 6;
	return result;
}

static uint gx2_pixel_index_in_microtile (uint x, uint y, uint bpp)
{
	return gx2_pixel_index_in_microtile_ex (x, y, 0, bpp, 0, false);
}

static inline uint gx2_pipe_from_coord (uint x, uint y)
{
	return ((y >> 3) ^ (x >> 3)) & 1;
}
static inline uint gx2_bank_from_coord (uint x, uint y)
{
	return (((y >> 5) ^ (x >> 3)) & 1) | 2 * (((y >> 4) ^ (x >> 4)) & 1);
}

// Address (in bytes) of element (x,y) within a micro-tiled (tileMode 2/3)
// surface. bpp is bits per element.
static uint gx2_addr_micro_tiled (uint x, uint y, uint bpp, uint pitch, uint height, uint tile_mode)
{
	const uint thickness = tile_mode == 3 ? 4 : 1;
	const uint micro_tile_bytes = (64 * thickness * bpp + 7) / 8;
	const uint tiles_per_row = pitch >> 3;
	const uint tile_x = x >> 3, tile_y = y >> 3;
	const uint micro_off = micro_tile_bytes * (tile_x + tile_y * tiles_per_row);
	(void)height;
	const uint pixel_idx = gx2_pixel_index_in_microtile (x, y, bpp);
	const uint pixel_off = (bpp * pixel_idx) >> 3;
	return pixel_off + micro_off;
}

// Address (packed pipe/bank/offset word, matching the reference's own
// packed return value -- see gtx_detile()'s use of it) of element (x,y)
// within a macro-tiled surface, for the full non-3D macro tile family:
// tile modes 4/5/6/7 (2D_TILED THIN1/THIN2/THIN4/THICK) and 8/9/10/11
// (2B_TILED, same four, bank-swapped). pipe_swizzle (0/1) and bank_swizzle
// (0-3) come from the GX2Surface's `swizzle` field, bits 8 and 9-10
// respectively (see ScanGTX / the `swizzle_` split in the reference's own
// swizzleSurf()).
//
// Port of aboood40091/BFRES-Tool's addrlib.py
// computeSurfaceAddrFromCoordMacroTiled() (GPL-3.0,
// github.com/aboood40091/BFRES-Tool, addrlib/addrlib.py -- itself a
// from-scratch Python reimplementation of the real AMD/GX2 addrlib
// algorithm; same lineage this file already credits for the tile-mode-4
// case). Scoped to numSamples=1, sampleSlice=0 (single-sample, single-slice
// 2D textures only, matching this decoder's overall scope) -- the
// reference's own sample-split branch for microTileBytes>2048 divides by
// zero for every input in that range (2048 // 0), so it's dead code in the
// reference too; the caller (gtx_detile) declines rather than hit it.
static uint gx2_addr_macro_tiled (uint x, uint y, uint z, uint sample, uint bpp, uint pitch,
	uint height, uint num_samples, uint tile_mode, bool is_depth, uint pipe_swizzle,
	uint bank_swizzle)
{
	const uint thickness = gx2_surface_thickness (tile_mode);
	const uint pixel_idx = gx2_pixel_index_in_microtile_ex (x, y, z, bpp, tile_mode, is_depth);
	const u64 micro_bits = (u64)num_samples * bpp * thickness * 64;
	const uint micro_bytes = (uint)((micro_bits + 7) / 8);
	const uint bytes_per_sample = micro_bytes / num_samples;
	u64 elem_bits = is_depth ? (u64)num_samples * bpp * pixel_idx + bpp * sample
							 : (u64)bpp * pixel_idx + sample * (micro_bits / num_samples);
	uint samples_per_slice = num_samples, sample_splits = 1, sample_slice = 0;
	if (num_samples > 1 && micro_bytes > 2048)
	{
		samples_per_slice = 2048 / bytes_per_sample;
		if (!samples_per_slice)
			return ~0u;
		sample_splits = num_samples / samples_per_slice;
		num_samples = samples_per_slice;
		const u64 tile_slice_bits = micro_bits / sample_splits;
		sample_slice = (uint)(elem_bits / tile_slice_bits);
		elem_bits %= tile_slice_bits;
	}
	const uint elem_off = (uint)((elem_bits + 7) / 8);

	uint pipe = gx2_pipe_from_coord (x, y);
	uint bank = gx2_bank_from_coord (x, y);
	const uint swizzle_ = pipe_swizzle + 2 * bank_swizzle;
	const uint rotation = tile_mode <= 11 ? 2 : 1;
	const uint slice_in = gx2_is_thick_macro (tile_mode) ? z >> 2 : z;
	uint bank_pipe = (pipe + 2 * bank) ^ (6 * sample_slice) ^ (swizzle_ + slice_in * rotation);
	bank_pipe %= 8;
	pipe = bank_pipe % 2;
	bank = bank_pipe / 2;
	const u64 slice_bytes = ((u64)height * pitch * thickness * bpp * num_samples + 7) / 8;
	const u64 slice_off = slice_bytes * ((sample_slice + sample_splits * z) / thickness);

	uint macro_tile_pitch = 32, macro_tile_height = 16;
	if (tile_mode == 5 || tile_mode == 9)
	{
		macro_tile_pitch >>= 1;
		macro_tile_height <<= 1;
	}
	else if (tile_mode == 6 || tile_mode == 10)
	{
		macro_tile_pitch >>= 2;
		macro_tile_height <<= 2;
	}

	const uint macro_tiles_per_row = pitch / macro_tile_pitch;
	const uint macro_tile_bytes
		= (num_samples * thickness * bpp * macro_tile_height * macro_tile_pitch + 7) / 8;
	const uint mx = x / macro_tile_pitch, my = y / macro_tile_height;
	uint macro_off = (mx + macro_tiles_per_row * my) * macro_tile_bytes;

	if (gx2_is_bank_swapped (tile_mode))
	{
		const uint bsw = gx2_bank_swapped_width (tile_mode, bpp, pitch, num_samples);
		if (bsw)
		{
			const uint swap_index = (macro_tile_pitch * mx / bsw) & 3;
			bank ^= gx2_bank_swap_order[swap_index];
		}
	}

	const u64 total = elem_off + ((macro_off + slice_off) >> 3);
	return (uint)(bank << 9 | pipe << 8 | (total & 255) | ((total & ~255ull) << 3));
}

u64 GetGX2SurfaceOffset (
	uint x, uint y, uint bpp, uint width, uint height, uint tile_mode, uint pitch, uint swizzle)
{
	return GetGX2SurfaceOffsetEx (
		x, y, 0, 0, bpp, width, height, 1, 1, tile_mode, pitch, swizzle, false);
}

u64 GetGX2SurfaceOffsetEx (uint x, uint y, uint slice, uint sample, uint bpp, uint width,
	uint height, uint num_slices, uint num_samples, uint tile_mode, uint pitch, uint swizzle,
	bool is_depth)
{
	if (!bpp || bpp % 8 || !width || !height || !pitch || !num_slices
		|| (num_samples != 1 && num_samples != 2 && num_samples != 4 && num_samples != 8)
		|| x >= width || y >= height || slice >= num_slices || sample >= num_samples)
		return ~(u64)0;
	if (tile_mode == 0 || tile_mode == 1)
		return ((u64)y * pitch + x + (u64)pitch * height * (slice + sample * num_slices)) * bpp / 8;
	if (tile_mode == 2 || tile_mode == 3)
	{
		if (num_samples != 1)
			return ~(u64)0;
		const uint thickness = gx2_surface_thickness (tile_mode);
		const u64 micro_bytes = ((u64)64 * thickness * bpp + 7) / 8;
		const u64 micro_off = micro_bytes * ((x >> 3) + (u64)(y >> 3) * (pitch >> 3));
		const u64 slice_bytes = ((u64)pitch * height * thickness * bpp + 7) / 8;
		const u64 slice_off = (slice / thickness) * slice_bytes;
		const uint pixel = gx2_pixel_index_in_microtile_ex (x, y, slice, bpp, tile_mode, is_depth);
		return slice_off + micro_off + (u64)bpp * pixel / 8;
	}
	if (tile_mode >= 4 && tile_mode <= 15)
		return gx2_addr_macro_tiled (x, y, slice, sample, bpp, pitch, height, num_samples,
			tile_mode, is_depth, (swizzle >> 8) & 1, (swizzle >> 9) & 3);
	return ~(u64)0;
}

// Storage size (bits per element, or bits per 4x4 block for BC/CTX1) for the
// byte-addressable Latte data formats.  Numeric interpretation is encoded in
// the upper GX2SurfaceFormat bits and does not change the storage footprint.
static bool gx2_format_bpp (uint format, uint *bpp, bool *is_bc, uint *bc_variant)
{
	*is_bc = false;
	switch (format & 0x3F)
	{
		case 0x01:
			*bpp = 8;
			return true; // R8_UNORM
		case 0x02:
			*bpp = 8;
			return true; // R4_G4_UNORM
		case 0x05:
		case 0x06:
			*bpp = 16;
			return true; // R16 / R16_FLOAT
		case 0x07:
			*bpp = 16;
			return true; // R8_G8_UNORM
		case 0x08:
			*bpp = 16;
			return true; // R5_G6_B5_UNORM
		case 0x0a:
			*bpp = 16;
			return true; // R5_G5_B5_A1_UNORM
		case 0x0b:
			*bpp = 16;
			return true; // R4_G4_B4_A4_UNORM
		case 0x0c:
			*bpp = 16;
			return true; // A1_B5_G5_R5
		case 0x0d:
		case 0x0e:
			*bpp = 32;
			return true; // R32 / R32_FLOAT
		case 0x0f:
		case 0x10:
			*bpp = 32;
			return true; // RG16 / RG16_FLOAT
		case 0x11:
			*bpp = 32;
			return true; // D24S8 / R24X8
		case 0x16:
			*bpp = 32;
			return true; // R11G11B10_FLOAT
		case 0x19:
			*bpp = 32;
			return true; // R10_G10_B10_A2_UNORM
		case 0x1a:
			*bpp = 32;
			return true; // R8_G8_B8_A8_UNORM(/SRGB)
		case 0x1b:
			*bpp = 32;
			return true; // A2_B10_G10_R10
		case 0x1c:
			*bpp = 64;
			return true; // X24_8_32_FLOAT
		case 0x1d:
		case 0x1e:
			*bpp = 64;
			return true; // RG32 / RG32_FLOAT
		case 0x1f:
		case 0x20:
			*bpp = 64;
			return true; // RGBA16 / RGBA16_FLOAT
		case 0x22:
		case 0x23:
			*bpp = 128;
			return true; // RGBA32 / RGBA32_FLOAT
		case 0x31:
			*bpp = 64;
			*is_bc = true;
			*bc_variant = 1;
			return true; // BC1
		case 0x32:
			*bpp = 128;
			*is_bc = true;
			*bc_variant = 2;
			return true; // BC2
		case 0x33:
			*bpp = 128;
			*is_bc = true;
			*bc_variant = 3;
			return true; // BC3
		case 0x34:
			*bpp = 64;
			*is_bc = true;
			*bc_variant = 4;
			return true; // BC4
		case 0x35:
			*bpp = 128;
			*is_bc = true;
			*bc_variant = 5;
			return true; // BC5
		default:
			return false;
	}
}

static bool gx2_format_rgba_supported (uint format)
{
	switch (format & 0x3f)
	{
		case 0x01:
		case 0x02:
		case 0x05:
		case 0x06:
		case 0x07:
		case 0x08:
		case 0x0a:
		case 0x0b:
		case 0x0c:
		case 0x11:
		case 0x19:
		case 0x1a:
		case 0x0d:
		case 0x0e:
		case 0x0f:
		case 0x10:
		case 0x16:
		case 0x1b:
		case 0x1c:
		case 0x1d:
		case 0x1e:
		case 0x1f:
		case 0x20:
		case 0x22:
		case 0x23:
		case 0x31:
		case 0x32:
		case 0x33:
		case 0x34:
		case 0x35:
			return true;
		default:
			return false;
	}
}

static float gx2_half_float (u16 h)
{
	const uint sign = h >> 15, exp = (h >> 10) & 31, mant = h & 1023;
	float v;
	if (!exp)
		v = mant / 16777216.0f;
	else if (exp == 31)
		v = mant ? 0.0f : 65504.0f;
	else
	{
		v = 1.0f + mant / 1024.0f;
		int e = (int)exp - 15;
		while (e > 0)
		{
			v *= 2;
			e--;
		}
		while (e < 0)
		{
			v *= .5f;
			e++;
		}
	}
	return sign ? -v : v;
}

static float gx2_ufloat (uint raw, uint mant_bits)
{
	const uint mant = raw & ((1u << mant_bits) - 1), exp = raw >> mant_bits;
	if (exp == 31)
		return mant ? 0.0f : 65504.0f;
	float v = exp ? 1.0f + mant / (float)(1u << mant_bits) : mant / (float)(1u << mant_bits);
	int e = exp ? (int)exp - 15 : -14;
	while (e > 0)
	{
		v *= 2;
		e--;
	}
	while (e < 0)
	{
		v *= .5f;
		e++;
	}
	return v;
}

static u8 gx2_float_byte (float v)
{
	return v <= 0 ? 0 : v >= 1 ? 255 : (u8)(v * 255.0f + .5f);
}

static u8 gx2_int_byte (u32 raw, uint bits, uint kind)
{
	const u32 mask = bits == 32 ? ~0u : (1u << bits) - 1;
	raw &= mask;
	if (kind == 2 || kind == 3) // SNORM or SINT: visualize [-max,+max] as [0,255]
	{
		const s64 sign = 1ull << (bits - 1);
		const s64 val = (raw & sign) ? (s64)raw - (1ull << bits) : raw;
		const s64 max = sign - 1;
		if (val <= -max)
			return 0;
		if (val >= max)
			return 255;
		return (u8)((val + max) * 255 / (2 * max));
	}
	return bits == 32 ? (u8)(((u64)raw * 255) / 0xffffffffu) : (u8)(raw * 255 / mask);
}

static uint gx2_min_pitch (uint width, uint tile_mode);

static uint gx2_mip_pitch (uint base_pitch, uint width, uint tile_mode, uint mip)
{
	uint pitch = base_pitch >> mip;
	const uint minimum = gx2_min_pitch (width, tile_mode);
	return pitch > minimum ? pitch : minimum;
}

// Deswizzles one level-0 surface into a tightly packed, row-major buffer of
// DIV_ROUND_UP(width,blk)*DIV_ROUND_UP(height,blk)*(bpp/8) bytes (blk=4 for
// BCn formats, 1 otherwise) -- i.e. still block-compressed for BC formats,
// exactly like BntxDeswizzle's contract.
static enumError gtx_detile (u8 **dest, const u8 *src, uint src_size, uint width, uint height,
	uint bpp, bool is_bc, uint tile_mode, uint pitch, uint swizzle, uint slice, uint num_slices,
	uint sample, uint num_samples, bool is_depth)
{
	const uint bw = is_bc ? div_round_up (width, 4) : width;
	const uint bh = is_bc ? div_round_up (height, 4) : height;
	const uint bytes = bpp / 8;
	const u64 out_size = (u64)bw * bh * bytes;
	if (!bw || !bh || out_size > GTX_MAX_OUTPUT)
		return EINVAL;

	u8 *out = MALLOC (out_size);
	if (!out)
		return ERR_CANT_CREATE;
	memset (out, 0, out_size);

	for (uint y = 0; y < bh; y++)
		for (uint x = 0; x < bw; x++)
		{
			const u64 addr = GetGX2SurfaceOffsetEx (x, y, slice, sample, bpp, bw, bh, num_slices,
				num_samples, tile_mode, pitch, swizzle, is_depth);
			if (addr == ~(u64)0)
			{
				FREE (out);
				return EINVAL;
			}

			if ((u64)addr + bytes > src_size)
				continue; // leave zero-filled, matches reference's own bounds behaviour
			memcpy (out + (y * bw + x) * bytes, src + (size_t)addr, bytes);
		}

	*dest = out;
	return ERR_OK;
}

//-----------------------------------------------------------------------------
///////////////		RGBA8 decode				///////////////
//-----------------------------------------------------------------------------

enumError DecodeGX2SurfaceSlice_RGBA (u8 **dest, uint *width, uint *height, uint dim, uint w,
	uint h, uint depth, uint format, uint aa, uint tile_mode, uint pitch, uint swizzle, uint slice,
	uint sample, const u8 *data, uint data_size)
{
	const uint num_samples = aa < 4 ? 1u << aa : 0;
	const uint num_slices = depth ? depth : 1;
	if (!dest || !width || !height || !data || !w || !h || dim > 7 || !num_samples
		|| slice >= num_slices || sample >= num_samples)
		return EINVAL;

	uint bpp, bc_variant = 0;
	bool is_bc;
	if (!gx2_format_bpp (format, &bpp, &is_bc, &bc_variant))
		return EINVAL;
	if (!gx2_format_rgba_supported (format))
		return EINVAL;

	u8 *tiled = 0;
	const bool is_depth = (format & 0x3f) == 0x11;
	enumError err = gtx_detile (&tiled, data, data_size, w, h, bpp, is_bc, tile_mode, pitch,
		swizzle, slice, num_slices, sample, num_samples, is_depth);
	if (err)
		return err;

	const u64 out_size = (u64)w * h * 4;
	if (out_size > GTX_MAX_OUTPUT)
	{
		FREE (tiled);
		return ERR_INVALID_DATA;
	}
	u8 *rgba = MALLOC (out_size);
	if (!rgba)
	{
		FREE (tiled);
		return ERR_CANT_CREATE;
	}

	if (is_bc)
	{
		const uint bw = div_round_up (w, 4), bh = div_round_up (h, 4);
		const uint block_bytes = bpp / 8;
		for (uint by = 0; by < bh; by++)
			for (uint bx = 0; bx < bw; bx++)
			{
				u8 px[64];
				const u8 *blk = tiled + (by * bw + bx) * block_bytes;
				switch (bc_variant)
				{
					case 1:
						decode_bc1_block (blk, px, true);
						break;
					case 2:
						decode_bc2_block (blk, px);
						break;
					case 3:
						decode_bc3_block (blk, px);
						break;
					case 4:
						format & 0x200 ? decode_bc4_signed_block (blk, px)
									   : decode_bc4_block (blk, px);
						break;
					case 5:
						format & 0x200 ? decode_bc5_signed_block (blk, px)
									   : decode_bc5_block (blk, px);
						break;
				}
				for (uint py = 0; py < 4; py++)
				{
					const uint dy = by * 4 + py;
					if (dy >= h)
						break;
					for (uint pxi = 0; pxi < 4; pxi++)
					{
						const uint dx = bx * 4 + pxi;
						if (dx >= w)
							break;
						memcpy (rgba + (dy * w + dx) * 4, px + (py * 4 + pxi) * 4, 4);
					}
				}
			}
	}
	else
	{
		for (uint y = 0; y < h; y++)
			for (uint x = 0; x < w; x++)
			{
				const u8 *s = tiled + (y * w + x) * (bpp / 8);
				u8 *d = rgba + (y * w + x) * 4;
				const uint kind = (format >> 8) & 15;
				switch (format & 0x3F)
				{
					case 0x01:
						d[0] = d[1] = d[2] = s[0];
						d[3] = 255;
						break; // R8
					case 0x02:
						d[0] = (s[0] >> 4) * 17;
						d[1] = (s[0] & 15) * 17;
						d[2] = 0;
						d[3] = 255;
						break; // R4_G4
					case 0x05:
					case 0x06:
					{
						const u16 v = (u16)s[0] << 8 | s[1];
						d[0] = d[1] = d[2] = kind == 8 || ((format & 0x3f) == 6)
							? gx2_float_byte (gx2_half_float (v))
							: gx2_int_byte (v, 16, kind);
						d[3] = 255;
						break;
					}
					case 0x07:
						d[0] = s[0];
						d[1] = s[1];
						d[2] = 0;
						d[3] = 255;
						break; // R8_G8_UNORM: 2-channel RG, no alpha channel
					case 0x08: // R5G6B5
					{
						const u16 v = (u16)s[0] << 8 | s[1];
						d[0] = (u8)(((v >> 11) & 0x1F) * 255 / 31);
						d[1] = (u8)(((v >> 5) & 0x3F) * 255 / 63);
						d[2] = (u8)((v & 0x1F) * 255 / 31);
						d[3] = 255;
						break;
					}
					case 0x0a: // R5G5B5A1
					{
						const u16 v = (u16)s[0] << 8 | s[1];
						d[0] = (u8)(((v >> 11) & 0x1F) * 255 / 31);
						d[1] = (u8)(((v >> 6) & 0x1F) * 255 / 31);
						d[2] = (u8)(((v >> 1) & 0x1F) * 255 / 31);
						d[3] = (v & 1) ? 255 : 0;
						break;
					}
					case 0x0b: // R4G4B4A4
					{
						const u16 v = (u16)s[0] << 8 | s[1];
						d[0] = (u8)(((v >> 12) & 0xF) * 17);
						d[1] = (u8)(((v >> 8) & 0xF) * 17);
						d[2] = (u8)(((v >> 4) & 0xF) * 17);
						d[3] = (u8)((v & 0xF) * 17);
						break;
					}
					case 0x0c: // A1B5G5R5
					{
						const u16 v = (u16)s[0] << 8 | s[1];
						d[0] = (u8)((v & 31) * 255 / 31);
						d[1] = (u8)(((v >> 5) & 31) * 255 / 31);
						d[2] = (u8)(((v >> 10) & 31) * 255 / 31);
						d[3] = (v & 0x8000) ? 255 : 0;
						break;
					}
					case 0x0d:
					case 0x0e:
					{
						const u32 v = grd32 (s);
						float f;
						memcpy (&f, &v, 4);
						d[0] = d[1] = d[2] = kind == 8 || ((format & 0x3f) == 0x0e)
							? gx2_float_byte (f)
							: gx2_int_byte (v, 32, kind);
						d[3] = 255;
						break;
					}
					case 0x0f:
					case 0x10:
					{
						for (uint c = 0; c < 2; c++)
						{
							u16 v = (u16)s[2 * c] << 8 | s[2 * c + 1];
							d[c] = kind == 8 || ((format & 0x3f) == 0x10)
								? gx2_float_byte (gx2_half_float (v))
								: gx2_int_byte (v, 16, kind);
						}
						d[2] = 0;
						d[3] = 255;
						break;
					}
					case 0x11: // D24_S8: normalized depth visualization + stencil alpha
					{
						const u32 v = (u32)s[0] << 24 | (u32)s[1] << 16 | (u32)s[2] << 8 | s[3];
						const u8 z = (u8)(((v >> 8) & 0xffffff) * 255 / 0xffffff);
						d[0] = d[1] = d[2] = z;
						d[3] = (u8)v;
						break;
					}
					case 0x19: // R10G10B10A2
					{
						const u32 v = (u32)s[0] << 24 | (u32)s[1] << 16 | (u32)s[2] << 8 | s[3];
						d[0] = (u8)(((v >> 20) & 1023) * 255 / 1023);
						d[1] = (u8)(((v >> 10) & 1023) * 255 / 1023);
						d[2] = (u8)((v & 1023) * 255 / 1023);
						d[3] = (u8)(((v >> 30) & 3) * 85);
						break;
					}
					case 0x16:
					{
						const u32 v = grd32 (s);
						d[0] = gx2_float_byte (gx2_ufloat (v >> 21, 6));
						d[1] = gx2_float_byte (gx2_ufloat ((v >> 10) & 0x7ff, 6));
						d[2] = gx2_float_byte (gx2_ufloat (v & 0x3ff, 5));
						d[3] = 255;
						break;
					}
					case 0x1a: // R8G8B8A8
						for (uint c = 0; c < 4; c++)
							d[c] = gx2_int_byte (s[c], 8, kind);
						break;
					case 0x1b:
					{
						const u32 v = grd32 (s);
						d[0] = gx2_int_byte (v & 1023, 10, kind);
						d[1] = gx2_int_byte ((v >> 10) & 1023, 10, kind);
						d[2] = gx2_int_byte ((v >> 20) & 1023, 10, kind);
						d[3] = gx2_int_byte (v >> 30, 2, kind);
						break;
					}
					case 0x1c:
					{
						u32 v = grd32 (s);
						float f;
						memcpy (&f, &v, 4);
						d[0] = d[1] = d[2] = gx2_float_byte (f);
						d[3] = s[7];
						break;
					}
					case 0x1d:
					case 0x1e:
					{
						for (uint c = 0; c < 2; c++)
						{
							u32 v = grd32 (s + 4 * c);
							float f;
							memcpy (&f, &v, 4);
							d[c] = kind == 8 || ((format & 0x3f) == 0x1e)
								? gx2_float_byte (f)
								: gx2_int_byte (v, 32, kind);
						}
						d[2] = 0;
						d[3] = 255;
						break;
					}
					case 0x1f:
					case 0x20:
					{
						for (uint c = 0; c < 4; c++)
						{
							u16 v = (u16)s[2 * c] << 8 | s[2 * c + 1];
							d[c] = kind == 8 || ((format & 0x3f) == 0x20)
								? gx2_float_byte (gx2_half_float (v))
								: gx2_int_byte (v, 16, kind);
						}
						break;
					}
					case 0x22:
					case 0x23:
					{
						for (uint c = 0; c < 4; c++)
						{
							u32 v = grd32 (s + 4 * c);
							float f;
							memcpy (&f, &v, 4);
							d[c] = kind == 8 || ((format & 0x3f) == 0x23)
								? gx2_float_byte (f)
								: gx2_int_byte (v, 32, kind);
						}
						break;
					}
				}
			}
	}

	FREE (tiled);
	*dest = rgba;
	*width = w;
	*height = h;
	return ERR_OK;
}

enumError DecodeGX2Surface_RGBA (u8 **dest, uint *width, uint *height, uint dim, uint w, uint h,
	uint format, uint tile_mode, uint pitch, uint swizzle, const u8 *data, uint data_size)
{
	return DecodeGX2SurfaceSlice_RGBA (dest, width, height, dim, w, h, 1, format, 0, tile_mode,
		pitch, swizzle, 0, 0, data, data_size);
}

enumError DecodeGTX_RGBA (u8 **dest, uint *width, uint *height, const gtx_t *gtx, uint index)
{
	if (!gtx || index >= gtx->n_textures)
		return EINVAL;
	const gtx_texture_t *t = gtx->textures + index;
	if (!t->data)
		return EINVAL;
	enumError err = DecodeGX2SurfaceSlice_RGBA (dest, width, height, t->dim, t->width, t->height,
		t->depth, t->format, t->aa, t->tile_mode, t->pitch, t->swizzle, t->view_first_slice, 0,
		t->data, t->data_size);
	if (err)
		return err;

	// GX2 component selectors: X/Y/Z/W, constant 0, constant 1. Apply
	// after format expansion so the rule is identical for BCn and plain
	// formats. Invalid selectors retain identity for damaged old files.
	u8 *rgba = *dest;
	for (u64 i = 0, count = (u64)*width * *height; i < count; i++)
	{
		u8 in[4], out[4];
		memcpy (in, rgba + 4 * i, 4);
		for (uint c = 0; c < 4; c++)
		{
			const uint sel = t->comp_sel[c];
			out[c] = sel < 4 ? in[sel] : sel == 4 ? 0 : sel == 5 ? 255 : in[c];
		}
		memcpy (rgba + 4 * i, out, 4);
	}
	return ERR_OK;
}

enumError DecodeGTXMip_RGBA (
	u8 **dest, uint *width, uint *height, const gtx_t *gtx, uint index, uint mip_level)
{
	if (!mip_level)
		return DecodeGTX_RGBA (dest, width, height, gtx, index);
	if (!gtx || index >= gtx->n_textures)
		return EINVAL;
	const gtx_texture_t *t = gtx->textures + index;
	if (mip_level >= t->num_mips || mip_level > 13 || !t->mip_data)
		return EINVAL;
	const uint mip_base = t->mip_offsets[0];
	if (t->mip_offsets[mip_level - 1] < mip_base)
		return EINVAL;
	const uint start = t->mip_offsets[mip_level - 1] - mip_base;
	const uint end = mip_level + 1 < t->num_mips && mip_level < 13
		? t->mip_offsets[mip_level] - mip_base
		: t->mip_data_size;
	if (start >= end || end > t->mip_data_size)
		return EINVAL;
	const uint w = t->width >> mip_level ? t->width >> mip_level : 1;
	const uint h = t->height >> mip_level ? t->height >> mip_level : 1;
	uint bpp = 0, bcv = 0;
	bool is_bc = false;
	if (!gx2_format_bpp (t->format, &bpp, &is_bc, &bcv))
		return EINVAL;
	const uint pitch
		= gx2_mip_pitch (t->pitch, is_bc ? div_round_up (w, 4) : w, t->tile_mode, mip_level);
	const uint depth = t->dim == 2 ? (t->depth >> mip_level ? t->depth >> mip_level : 1) : t->depth;
	enumError err = DecodeGX2SurfaceSlice_RGBA (dest, width, height, t->dim, w, h, depth, t->format,
		t->aa, t->tile_mode, pitch, t->swizzle, t->view_first_slice, 0, t->mip_data + start,
		end - start);
	if (err)
		return err;
	u8 *rgba = *dest;
	for (u64 i = 0, count = (u64)*width * *height; i < count; i++)
	{
		u8 in[4], out[4];
		memcpy (in, rgba + 4 * i, 4);
		for (uint c = 0; c < 4; c++)
		{
			uint s = t->comp_sel[c];
			out[c] = s < 4 ? in[s] : s == 4 ? 0 : s == 5 ? 255 : in[c];
		}
		memcpy (rgba + 4 * i, out, 4);
	}
	return ERR_OK;
}

enumError DecodeGTXSubresource_RGBA (u8 **dest, uint *width, uint *height, const gtx_t *gtx,
	uint index, uint mip_level, uint slice, uint sample)
{
	if (!gtx || index >= gtx->n_textures)
		return EINVAL;
	const gtx_texture_t *t = gtx->textures + index;
	const u8 *src = t->data;
	uint src_size = t->data_size;
	uint w = t->width, h = t->height, pitch = t->pitch;
	if (mip_level)
	{
		if (mip_level >= t->num_mips || mip_level > 13 || !t->mip_data)
			return EINVAL;
		const uint mip_base = t->mip_offsets[0];
		if (t->mip_offsets[mip_level - 1] < mip_base)
			return EINVAL;
		const uint start = t->mip_offsets[mip_level - 1] - mip_base;
		const uint end = mip_level + 1 < t->num_mips && mip_level < 13
			? t->mip_offsets[mip_level] - mip_base
			: t->mip_data_size;
		if (start >= end || end > t->mip_data_size)
			return EINVAL;
		src = t->mip_data + start;
		src_size = end - start;
		w = w >> mip_level ? w >> mip_level : 1;
		h = h >> mip_level ? h >> mip_level : 1;
		uint bpp = 0, bcv = 0;
		bool bc = false;
		if (!gx2_format_bpp (t->format, &bpp, &bc, &bcv))
			return EINVAL;
		pitch = gx2_mip_pitch (t->pitch, bc ? div_round_up (w, 4) : w, t->tile_mode, mip_level);
	}
	const uint depth = t->dim == 2 ? (t->depth >> mip_level ? t->depth >> mip_level : 1) : t->depth;
	enumError err = DecodeGX2SurfaceSlice_RGBA (dest, width, height, t->dim, w, h, depth, t->format,
		t->aa, t->tile_mode, pitch, t->swizzle, slice, sample, src, src_size);
	if (err)
		return err;
	u8 *rgba = *dest;
	for (u64 i = 0, count = (u64)*width * *height; i < count; i++)
	{
		u8 in[4], out[4];
		memcpy (in, rgba + 4 * i, 4);
		for (uint c = 0; c < 4; c++)
		{
			uint s = t->comp_sel[c];
			out[c] = s < 4 ? in[s] : s == 4 ? 0 : s == 5 ? 255 : in[c];
		}
		memcpy (rgba + 4 * i, out, 4);
	}
	return ERR_OK;
}

//-----------------------------------------------------------------------------
///////////////			format encoding			///////////////
//-----------------------------------------------------------------------------

// GX2Texture block payload size: GX2Surface (16 u32) + 13 mip-offset u32
// (unused, level 0 only) + viewFirstMip/NumMips/FirstSlice/NumSlices (4
// u32) + compSel (4 bytes) + texRegs (5 u32) -- matches ScanGTX's own
// comment on the layout it reads.
#define GTX_TEX_BLOCK_SIZE (64 + 13 * 4 + 4 * 4 + 4 + 5 * 4)
#define GTX_BLOCK_HDR_SIZE 32
#define GTX_FILE_HDR_SIZE 32

static uint gx2_min_pitch (uint width, uint tile_mode)
{
	uint align = 1;
	if (tile_mode == 2 || tile_mode == 3)
		align = 8;
	else if (tile_mode >= 4 && tile_mode <= 15)
	{
		align = 32;
		if (tile_mode == 5 || tile_mode == 9)
			align = 16;
		else if (tile_mode == 6 || tile_mode == 10)
			align = 8;
	}
	return (width + align - 1) & ~(align - 1);
}

static enumError gtx_tile_level (u8 **dest, uint *dest_size, const u8 *linear, uint linear_size,
	uint width, uint height, uint slices, uint samples, uint bpp, bool is_bc, bool is_depth,
	uint tile_mode, uint pitch, uint swizzle)
{
	const uint ew = is_bc ? div_round_up (width, 4) : width;
	const uint eh = is_bc ? div_round_up (height, 4) : height;
	const uint bytes = bpp / 8;
	const u64 expected = (u64)ew * eh * slices * samples * bytes;
	if (!linear || expected != linear_size)
		return EINVAL;
	u64 size = 0;
	for (uint sample = 0; sample < samples; sample++)
		for (uint slice = 0; slice < slices; slice++)
			for (uint y = 0; y < eh; y++)
				for (uint x = 0; x < ew; x++)
				{
					const u64 off = GetGX2SurfaceOffsetEx (x, y, slice, sample, bpp, ew, eh, slices,
						samples, tile_mode, pitch, swizzle, is_depth);
					if (off == ~(u64)0 || off + bytes > GTX_MAX_OUTPUT)
						return EINVAL;
					if (off + bytes > size)
						size = off + bytes;
				}
	// GX2 data blocks conventionally have at least surface alignment. Keeping
	// each level independently aligned also makes mip offsets unambiguous.
	size = (size + 0xfff) & ~0xfffull;
	if (!size || size > GTX_MAX_OUTPUT)
		return EFBIG;
	u8 *out = CALLOC (1, (size_t)size);
	if (!out)
		return ERR_CANT_CREATE;
	for (uint sample = 0; sample < samples; sample++)
		for (uint slice = 0; slice < slices; slice++)
			for (uint y = 0; y < eh; y++)
				for (uint x = 0; x < ew; x++)
				{
					const u64 src = ((((u64)sample * slices + slice) * eh + y) * ew + x) * bytes;
					const u64 off = GetGX2SurfaceOffsetEx (x, y, slice, sample, bpp, ew, eh, slices,
						samples, tile_mode, pitch, swizzle, is_depth);
					memcpy (out + off, linear + src, bytes);
				}
	*dest = out;
	*dest_size = (uint)size;
	return ERR_OK;
}

static void gtx_write_block_header (u8 *p, uint type, uint size)
{
	memcpy (p, "BLK{", 4);
	gwr32 (p + 4, GTX_BLOCK_HDR_SIZE);
	gwr32 (p + 16, type);
	gwr32 (p + 20, size);
}

enumError EncodeGTXTextures (
	u8 **dest, uint *dest_size, const gtx_encode_texture_t *textures, uint n_textures)
{
	if (!dest || !dest_size || !textures || !n_textures)
		return EINVAL;
	typedef struct
	{
		u8 *level[14];
		uint size[14], pitch[14];
	} work_t;
	work_t *work = CALLOC (n_textures, sizeof (*work));
	if (!work)
		return ERR_CANT_CREATE;
	u64 total = GTX_FILE_HDR_SIZE + GTX_BLOCK_HDR_SIZE;
	enumError err = ERR_OK;
	for (uint ti = 0; ti < n_textures; ti++)
	{
		const gtx_encode_texture_t *t = textures + ti;
		uint bpp = 0, bcv = 0;
		bool bc = false;
		const uint samples = t->aa < 4 ? 1u << t->aa : 0;
		const bool known_format = gx2_format_bpp (t->format, &bpp, &bc, &bcv);
		if (!known_format)
		{
			bpp = t->element_bpp;
			bc = t->block_width > 1 || t->block_height > 1;
		}
		if (!t->levels || !t->width || !t->height || !t->depth || !t->num_mips || t->num_mips > 14
			|| t->dim > 7 || t->tile_mode > 15 || !samples || !bpp || bpp % 8)
		{
			err = EINVAL;
			goto fail;
		}
		for (uint mip = 0; mip < t->num_mips; mip++)
		{
			const uint w = t->width >> mip ? t->width >> mip : 1;
			const uint h = t->height >> mip ? t->height >> mip : 1;
			const uint block_w = known_format && bc ? 4 : t->block_width ? t->block_width : 1;
			const uint block_h = known_format && bc ? 4 : t->block_height ? t->block_height : 1;
			const uint ew = div_round_up (w, block_w), eh = div_round_up (h, block_h);
			const uint default_slices
				= t->dim == 2 ? (t->depth >> mip ? t->depth >> mip : 1) : t->depth;
			const uint slices = t->levels[mip].slices ? t->levels[mip].slices : default_slices;
			uint pitch = t->pitch ? gx2_mip_pitch (t->pitch, ew, t->tile_mode, mip)
								  : gx2_min_pitch (ew, t->tile_mode);
			const uint min_pitch = gx2_min_pitch (ew, t->tile_mode);
			if (pitch < min_pitch)
				pitch = min_pitch;
			work[ti].pitch[mip] = pitch;
			err = gtx_tile_level (work[ti].level + mip, work[ti].size + mip, t->levels[mip].data,
				t->levels[mip].size, ew, eh, slices, samples, bpp, false,
				t->depth_order || (known_format && (t->format & 0x3f) == 0x11), t->tile_mode, pitch,
				t->swizzle);
			if (err)
				goto fail;
		}
		total += GTX_BLOCK_HDR_SIZE + GTX_TEX_BLOCK_SIZE;
		total += GTX_BLOCK_HDR_SIZE + work[ti].size[0];
		if (t->num_mips > 1)
		{
			total += GTX_BLOCK_HDR_SIZE;
			for (uint m = 1; m < t->num_mips; m++)
				total += work[ti].size[m];
		}
		if (total > GTX_MAX_OUTPUT)
		{
			err = EFBIG;
			goto fail;
		}
	}
	u8 *buf = CALLOC (1, (size_t)total);
	if (!buf)
	{
		err = ERR_CANT_CREATE;
		goto fail;
	}
	memcpy (buf, "Gfx2", 4);
	gwr32 (buf + 4, 32);
	gwr32 (buf + 8, 7);
	gwr32 (buf + 12, 3);
	gwr32 (buf + 16, 2);
	gwr32 (buf + 20, 0x1000);
	u8 *p = buf + GTX_FILE_HDR_SIZE;
	for (uint ti = 0; ti < n_textures; ti++)
	{
		const gtx_encode_texture_t *t = textures + ti;
		gtx_write_block_header (p, 0x0b, GTX_TEX_BLOCK_SIZE);
		u8 *s = p + 32;
		gwr32 (s, t->dim);
		gwr32 (s + 4, t->width);
		gwr32 (s + 8, t->height);
		gwr32 (s + 12, t->depth);
		gwr32 (s + 16, t->num_mips);
		gwr32 (s + 20, t->format);
		gwr32 (s + 24, t->aa);
		gwr32 (s + 28, t->use ? t->use : 1);
		gwr32 (s + 32, work[ti].size[0]);
		uint mip_size = 0;
		for (uint m = 1; m < t->num_mips; m++)
			mip_size += work[ti].size[m];
		gwr32 (s + 40, mip_size);
		gwr32 (s + 48, t->tile_mode);
		gwr32 (s + 52, t->swizzle);
		gwr32 (s + 60, work[ti].pitch[0]);
		uint moff = work[ti].size[0];
		for (uint m = 1; m < t->num_mips; m++)
		{
			gwr32 (s + 64 + 4 * (m - 1), moff);
			moff += work[ti].size[m];
		}
		gwr32 (s + 116, t->view_first_mip);
		gwr32 (s + 120, t->view_num_mips ? t->view_num_mips : t->num_mips);
		gwr32 (s + 124, t->view_first_slice);
		gwr32 (s + 128, t->view_num_slices ? t->view_num_slices : t->depth);
		if (t->comp_sel[0] || t->comp_sel[1] || t->comp_sel[2] || t->comp_sel[3])
			memcpy (s + 132, t->comp_sel, 4);
		else
		{
			s[132] = 0;
			s[133] = 1;
			s[134] = 2;
			s[135] = 3;
		}
		p += 32 + GTX_TEX_BLOCK_SIZE;
		gtx_write_block_header (p, 0x0c, work[ti].size[0]);
		memcpy (p + 32, work[ti].level[0], work[ti].size[0]);
		p += 32 + work[ti].size[0];
		if (t->num_mips > 1)
		{
			gtx_write_block_header (p, 0x0d, mip_size);
			p += 32;
			for (uint m = 1; m < t->num_mips; m++)
			{
				memcpy (p, work[ti].level[m], work[ti].size[m]);
				p += work[ti].size[m];
			}
		}
	}
	gtx_write_block_header (p, 1, 0);
	for (uint ti = 0; ti < n_textures; ti++)
		for (uint m = 0; m < 14; m++)
			FREE (work[ti].level[m]);
	FREE (work);
	*dest = buf;
	*dest_size = (uint)total;
	return ERR_OK;
fail:
	for (uint ti = 0; ti < n_textures; ti++)
		for (uint m = 0; m < 14; m++)
			FREE (work[ti].level[m]);
	FREE (work);
	return err;
}

enumError EncodeGTX_RGBA_Format (u8 **dest, uint *dest_size, const u8 *rgba, uint width,
	uint height, uint format, uint tile_mode)
{
	if (!rgba || !width || !height)
		return EINVAL;
	uint bpp = 0, bcv = 0;
	bool bc = false;
	if (!gx2_format_bpp (format, &bpp, &bc, &bcv) || bc || !gx2_format_rgba_supported (format))
		return EINVAL;
	const u64 size = (u64)width * height * (bpp / 8);
	if (size > GTX_MAX_OUTPUT)
		return EFBIG;
	u8 *raw = MALLOC ((size_t)size);
	if (!raw)
		return ERR_CANT_CREATE;
	for (u64 i = 0, n = (u64)width * height; i < n; i++)
	{
		const u8 *q = rgba + 4 * i;
		u8 *d = raw + i * (bpp / 8);
		switch (format & 0x3f)
		{
			case 0x01:
				d[0] = q[0];
				break;
			case 0x02:
				d[0] = (q[0] & 0xf0) | (q[1] >> 4);
				break;
			case 0x07:
				d[0] = q[0];
				d[1] = q[1];
				break;
			case 0x08:
			{
				u16 v = (q[0] * 31 / 255) << 11 | (q[1] * 63 / 255) << 5 | q[2] * 31 / 255;
				d[0] = v >> 8;
				d[1] = v;
				break;
			}
			case 0x0a:
			{
				u16 v = (q[0] * 31 / 255) << 11 | (q[1] * 31 / 255) << 6 | (q[2] * 31 / 255) << 1
					| (q[3] >= 128);
				d[0] = v >> 8;
				d[1] = v;
				break;
			}
			case 0x0b:
			{
				u16 v = (q[0] >> 4) << 12 | (q[1] >> 4) << 8 | (q[2] >> 4) << 4 | (q[3] >> 4);
				d[0] = v >> 8;
				d[1] = v;
				break;
			}
			case 0x0c:
			{
				u16 v = (q[3] >= 128) << 15 | (q[2] * 31 / 255) << 10 | (q[1] * 31 / 255) << 5
					| q[0] * 31 / 255;
				d[0] = v >> 8;
				d[1] = v;
				break;
			}
			case 0x11:
			{
				u32 z = ((u32)q[0] << 16) | ((u32)q[0] << 8) | q[0];
				d[0] = z >> 16;
				d[1] = z >> 8;
				d[2] = z;
				d[3] = q[3];
				break;
			}
			case 0x19:
			{
				u32 v = (q[3] / 85u) << 30 | (q[0] * 1023 / 255) << 20 | (q[1] * 1023 / 255) << 10
					| q[2] * 1023 / 255;
				d[0] = v >> 24;
				d[1] = v >> 16;
				d[2] = v >> 8;
				d[3] = v;
				break;
			}
			case 0x1a:
				memcpy (d, q, 4);
				break;
			default:
				FREE (raw);
				return EINVAL;
		}
	}
	gtx_encode_level_t level = { raw, (uint)size, 1 };
	gtx_encode_texture_t t = { 1, width, height, 1, 1, format, 0, 1, tile_mode, 0, 0, 0, 0, 0,
		false, 0, 1, 0, 1, { 0, 1, 2, 3 }, &level };
	enumError err = EncodeGTXTextures (dest, dest_size, &t, 1);
	FREE (raw);
	return err;
}

enumError EncodeGTX_RGBA (u8 **dest, uint *dest_size, const u8 *rgba, uint width, uint height)
{
	return EncodeGTX_RGBA_Format (dest, dest_size, rgba, width, height, 0x1a, 4);
}
