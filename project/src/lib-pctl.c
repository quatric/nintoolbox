#include "lib-pctl.h"
#include "lib-std.h"

// VFXB container, little-endian, matching Switch-Toolbox
// File_Format_Library/FileFormats/Effects/PCTL.cs PTCL.Header.Read()/SectionBase.Read():
//
//   char magic[4];        // "VFXB"
//   u32  padding;
//   u16  graphics_api_version;
//   u16  vfx_version;
//   u16  byte_order_mark;
//   u8   alignment;
//   u8   target_offset;
//   u32  header_size;     // observed constant 32
//   u16  flag;
//   u16  block_offset;    // absolute offset of the first top-level section
//   u32  padding2;
//   u32  file_size;
//
// From block_offset, a linked list of "SectionBase" nodes follows, each 32 bytes:
//   char signature[4];
//   u32  section_size;
//   u32  subsection_offset;   // relative to this node's own position, 0xFFFFFFFF = none
//   u32  next_section_offset; // relative to this node's own position, 0xFFFFFFFF = none/last
//   u32  unknown;             // observed 0xFFFFFFFF
//   u32  binary_data_offset;  // relative to this node's own position, 0xFFFFFFFF = none
//   u32  unknown3;            // observed 0
//   u32  subsection_count;
//
// A node with subsection_offset set recurses into a chain of child nodes (walked the same way,
// following each child's own next_section_offset) before its own next_section_offset resumes the
// parent's chain. The reference's Cafe/EFTB variant uses a slightly different, big-endian layout
// with a u16 subsection_count; that variant is out of scope here (FF_VFXB only matches "VFXB").
//
// A handful of section signatures carry a small fixed-layout payload in the reference and are
// decoded below (TEXR/ESET/ESFT/GTNT, plus the EMTR name string). The bulk of EMTR's payload --
// the actual particle emitter parameters (lifetime, emission rate, color/size curves, samplers)
// -- is read in the reference at VFXVersion-dependent fixed offsets (differs at VFXVersion 22 and
// 37) deep inside a ~2500-byte struct; porting that exactly without a corpus of real files to
// cross-check against would be guessing, so it's reported as a byte range only, per this task's
// explicit "don't rush a shaky decode" guidance.

#define PCTL_HDR_SIZE 32
#define PCTL_SECTION_HDR_SIZE 32
#define PCTL_NULL_OFFSET 0xFFFFFFFFu
#define PCTL_MAX_DEPTH 32
#define PCTL_MAX_SECTIONS 200000u	// shared work budget: bounds recursion *and* sibling-chain length
#define PCTL_MAX_GTNT_ENTRIES 4096

bool IsPCTL (const u8 *data, size_t size)
{
	return data && size >= 4 && !memcmp (data, "VFXB", 4);
}

// Reads a zero-terminated string at 'off', bounds-checked against 'size' and capped at
// 'maxscan' bytes of search so a missing terminator can't run us off the end of the buffer.
// Returns false (leaving 'dest' empty) on any out-of-bounds condition -- informational only.
static bool read_cstr (char *dest, uint destsz, const u8 *data, size_t size, u64 off, u64 maxscan)
{
	dest[0] = 0;
	if (off > size)
		return false;
	u64 limit = size - off;
	if (limit > maxscan)
		limit = maxscan;

	u64 len = 0;
	while (len < limit && data[off + len])
		len++;
	if (len == limit)
		// no zero terminator found within the scan/buffer limit -- treat as invalid rather
		// than silently truncating an unbounded string
		return false;

	uint n = len < destsz - 1 ? (uint)len : destsz - 1;
	memcpy (dest, data + off, n);
	dest[n] = 0;
	return true;
}

typedef struct pctl_walk_result
{
	bool ok;		// header/bounds were valid enough to read this node
	bool has_next;		// next_section_offset != NULL
	u64  next_pos;		// absolute offset of the next sibling, if has_next
}
pctl_walk_result;

static void print_indent (FILE *out, int indent)
{
	for (int i = 0; i < indent; i++)
		fputc ('\t', out);
}

static pctl_walk_result decode_section
	( FILE *out, const u8 *data, size_t size, u64 pos, int indent, int depth, u32 *budget )
{
	pctl_walk_result res = { false, false, 0 };

	if (depth > PCTL_MAX_DEPTH)
	{
		print_indent (out, indent);
		fprintf (out, "<max recursion depth exceeded>\n");
		return res;
	}
	if (!*budget)
	{
		print_indent (out, indent);
		fprintf (out, "<section budget exhausted, listing truncated>\n");
		return res;
	}
	(*budget)--;

	if (pos + PCTL_SECTION_HDR_SIZE > size)
	{
		print_indent (out, indent);
		fprintf (out, "<section header out of bounds at offset %llu>\n", (unsigned long long)pos);
		return res;
	}

	char sig[5];
	memcpy (sig, data + pos, 4);
	sig[4] = 0;
	for (int i = 0; i < 4; i++)
		if (sig[i] < 0x20 || sig[i] > 0x7e)
			sig[i] = '.';

	const u32 section_size       = rd_le32 (data + pos + 4);
	const u32 subsection_offset  = rd_le32 (data + pos + 8);
	const u32 next_section_offset= rd_le32 (data + pos + 12);
	const u32 binary_data_offset = rd_le32 (data + pos + 20);
	const u32 subsection_count   = rd_le32 (data + pos + 28);

	print_indent (out, indent);
	fprintf (out, "[%s] offset=%llu size=%u binary_data_offset=%s subsection_count=%u\n",
		sig, (unsigned long long)pos, section_size,
		binary_data_offset == PCTL_NULL_OFFSET ? "-" : "set", subsection_count);

	const u64 bin_pos = (u64)pos + binary_data_offset;	// only meaningful if != NULL
	const bool has_binary = binary_data_offset != PCTL_NULL_OFFSET && bin_pos >= pos;

	if (!memcmp (sig, "TEXR", 4))
	{
		if (has_binary && bin_pos + 48 <= size)
		{
			const u16 width  = rd_le16 (data + bin_pos);
			const u16 height = rd_le16 (data + bin_pos + 2);
			const u32 image_size = rd_le32 (data + bin_pos + 28);
			const u32 texture_id = rd_le32 (data + bin_pos + 36);
			const u8  surf_format = data[bin_pos + 40];
			print_indent (out, indent);
			fprintf (out, "  texture: %ux%u format=%u image_size=%u texture_id=0x%x\n",
				width, height, surf_format, image_size, texture_id);
		}
		else
		{
			print_indent (out, indent);
			fprintf (out, "  <texture data out of bounds>\n");
		}
	}
	else if (!memcmp (sig, "EMTR", 4))
	{
		char name[256];
		bool named = false;
		if (has_binary)
			named = read_cstr (name, sizeof (name), data, size, bin_pos + 16, 64);
		print_indent (out, indent);
		if (named)
			fprintf (out, "  emitter name = %s\n", name);
		else
			fprintf (out, "  emitter name = <out of bounds>\n");

		print_indent (out, indent);
		if (has_binary)
			fprintf (out, "  %u bytes of type-tagged emitter parameter data at file offset %llu, "
				"not decoded (version-dependent layout)\n",
				section_size, (unsigned long long)(bin_pos + 16 + 64));
		else
			fprintf (out, "  <emitter parameter block out of bounds>\n");
	}
	else if (!memcmp (sig, "ESET", 4))
	{
		char name[256];
		if (pos + PCTL_SECTION_HDR_SIZE + 16 <= size &&
			read_cstr (name, sizeof (name), data, size, pos + PCTL_SECTION_HDR_SIZE + 16, 64))
		{
			print_indent (out, indent);
			fprintf (out, "  emitter set name = %s\n", name);
		}
	}
	else if (!memcmp (sig, "ESFT", 4))
	{
		const u64 len_off = pos + PCTL_SECTION_HDR_SIZE + 28;
		if (len_off + 4 <= size)
		{
			const u32 str_len = rd_le32 (data + len_off);
			const u64 str_off = len_off + 4;
			if (str_len <= 4096 && str_off + str_len <= size)
			{
				char name[4097];
				memcpy (name, data + str_off, str_len);
				name[str_len] = 0;
				print_indent (out, indent);
				fprintf (out, "  font/effect name = %s\n", name);
			}
			else
			{
				print_indent (out, indent);
				fprintf (out, "  <name string out of bounds>\n");
			}
		}
	}
	else if (!memcmp (sig, "GTNT", 4))
	{
		if (has_binary)
		{
			u64 p = bin_pos;
			for (uint i = 0; i < PCTL_MAX_GTNT_ENTRIES && p < size; i++)
			{
				if (p + 16 > size)
				{
					print_indent (out, indent);
					fprintf (out, "  [%u] <descriptor header out of bounds>\n", i);
					break;
				}
				const u64 tex_id = rd_le64 (data + p);
				const u32 next_off = rd_le32 (data + p + 8);
				char name[256];
				bool named = read_cstr (name, sizeof (name), data, size, p + 16, 128);

				print_indent (out, indent);
				fprintf (out, "  [%u] texture_id=0x%llx name=%s\n", i,
					(unsigned long long)tex_id, named ? name : "<out of bounds>");

				if (!next_off)
					break;
				const u64 next_p = p + next_off;
				if (next_p <= p || next_p >= size)
					break;
				p = next_p;
			}
		}
	}
	else if (!memcmp (sig, "GRTF", 4) || !memcmp (sig, "G3PR", 4) ||
		!memcmp (sig, "GRSN", 4) || !memcmp (sig, "GRSC", 4))
	{
		print_indent (out, indent);
		if (has_binary && bin_pos + section_size <= size)
			fprintf (out, "  embedded blob at offset %llu, %u bytes, not decoded\n",
				(unsigned long long)bin_pos, section_size);
		else
			fprintf (out, "  <embedded blob out of bounds>\n");
	}

	res.ok = true;

	if (subsection_offset != PCTL_NULL_OFFSET)
	{
		const u64 child_pos = pos + subsection_offset;
		const u32 count = subsection_count ? subsection_count : 1;
		u64 cur = child_pos;
		for (u32 i = 0; i < count && *budget; i++)
		{
			if (cur >= size)
			{
				print_indent (out, indent + 1);
				fprintf (out, "<subsection chain out of bounds>\n");
				break;
			}
			pctl_walk_result child = decode_section (out, data, size, cur, indent + 1, depth + 1, budget);
			if (!child.ok || !child.has_next)
				break;
			if (child.next_pos <= cur)	// guard against a cycle/non-advancing chain
				break;
			cur = child.next_pos;
		}
	}

	if (next_section_offset != PCTL_NULL_OFFSET)
	{
		res.has_next = true;
		res.next_pos = pos + next_section_offset;
	}

	return res;
}

enumError DecodePCTL_Text (FILE *out, const u8 *data, size_t size)
{
	if (!out || !IsPCTL (data, size))
		return ERR_INVALID_DATA;
	if (size < PCTL_HDR_SIZE)
		return ERROR0 (ERR_INVALID_DATA, "VFXB: file shorter than the fixed header\n");

	const u16 graphics_api_version = rd_le16 (data + 8);
	const u16 vfx_version = rd_le16 (data + 10);
	const u16 byte_order_mark = rd_le16 (data + 12);
	const u8  alignment = data[14];
	const u8  target_offset = data[15];
	const u32 header_size = rd_le32 (data + 16);
	const u16 flag = rd_le16 (data + 20);
	const u16 block_offset = rd_le16 (data + 22);
	const u32 file_size = rd_le32 (data + 28);

	if ((u64)block_offset > size)
		return ERROR0 (ERR_INVALID_DATA, "VFXB: block_offset out of bounds\n");
	if (file_size && (u64)file_size > size)
		return ERROR0 (ERR_INVALID_DATA, "VFXB: header file_size (%u) exceeds actual size (%zu)\n",
			file_size, size);

	fprintf (out, "#VFXB\n"
		"# NintendoWare particle-effect archive -- section manifest.\n\n"
		"graphics_api_version = %u\n"
		"vfx_version = %u\n"
		"byte_order_mark = 0x%04x\n"
		"alignment = %u\n"
		"target_offset = %u\n"
		"header_size = %u\n"
		"flag = 0x%x\n"
		"block_offset = %u\n"
		"file_size = %u\n\n"
		"[sections]\n",
		graphics_api_version, vfx_version, byte_order_mark, alignment, target_offset,
		header_size, flag, block_offset, file_size);

	u32 budget = PCTL_MAX_SECTIONS;
	u64 pos = block_offset;
	while (pos < size && budget)
	{
		pctl_walk_result r = decode_section (out, data, size, pos, 0, 0, &budget);
		if (!r.ok || !r.has_next)
			break;
		if (r.next_pos <= pos)		// guard against a cycle/non-advancing chain
			break;
		pos = r.next_pos;
	}

	return ERR_OK;
}
