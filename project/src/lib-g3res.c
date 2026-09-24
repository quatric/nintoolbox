// SPDX-License-Identifier: GPL-2.0+
// Sakura Wars: So Long, My Love "G3" resource-chunk family -- see
// lib-g3res.h for exactly what is and is not understood.

#include "lib-g3res.h"
#include "lib-nintendo.h"
#include <stdio.h>
#include <string.h>

#define G3_CHUNK_HEADER_SIZE 16

// Tags confirmed (by repeated cross-file observation) to be containers that
// follow the generic 16-byte chunk header and whose 'size'/'child_offset'
// fields may be trusted to walk into and past them. Any tag not in this
// list is treated as an opaque leaf: reported by tag+offset only, never
// recursed into.
static const char *const g3_container_tags[] = {
	"GRO3", "GDEN", "G3TX", "G3MD", "HTEX", "HTSF", "HMDL",
	"UCOG", "UCOA", "UCUR", "UGR3", "ARMS", "ABDA", "UMDL",
	// Extended cross-file (2026 survey of .abd/.abr/.amf/.arm/.asd/.bs1/
	// .dia/.bf1/.gtp/.msb/.urs): the SAME generic 16-byte tag/size/
	// child_offset/flags chunk header is reused as the root of a much
	// wider family of asset containers than just the G3/GDEN model tree.
	// Each tag below was confirmed structurally (its child_offset, when
	// non-zero, lands on another printable/valid chunk header) across
	// multiple independent sample files before being added here.
	// Root/branch containers confirmed with real (non-garbage) children:
	"ABRS", // .abr - root of "arrows"/attack-range data, wraps ABDA image data
	"ABST", // .asd - root, event/cutscene still-image sequence data
	"URES", // .urs - root, "unit resource" (SLG battle unit), wraps GRO3+ABDA
	"AMMP", // .amf - root variant ("AMxx" family; motion/animation table)
	"HSPR", // sprite-table branch seen under AMCT/AMIN roots (.amf)
	0
};

// Root magics that are recognized here but whose payload -- while it does
// start with a valid 16-byte tag/size/child_offset/flags header -- was
// confirmed NOT to have a further nested chunk at its child_offset (the
// bytes there are raw binary, e.g. a table of offsets or timestamp-shaped
// constants, not another tag). They are still reported by DecodeG3Res_Text()
// as a single opaque top-level leaf (tag + fields only), which is enough to
// positively identify/dispatch the file even though its body is not decoded.
static const char *const g3_root_only_tags[] = {
	"ABDT", // seen as an ABDA/URES sub-chunk AND as some files' own root
	"MSCR", // .msb - dialogue/script container (body not decoded: no
	        // readable Shift-JIS/ASCII strings found directly after the
	        // header; likely tokenized/compiled script bytecode)
	"GTPA", // .gtp, and an alternate .arm root; child_offset points at a
	        // u32 count + u32[] offset table, not a nested chunk
	"MPLN", // .grd - ground/plane geometry data
	"BS1 ", // .bs1 - battle-stage layout (tag is "BS1" + one space pad)
	"AMCT", "AMIN", "AMPP", "AMWN", // .amf root variants (AM = "anim
	                                // motion"?); HSPR/KSPR sub-chunks
	0
};

static int g3_is_container_tag (const char *tag4)
{
	for (int i = 0; g3_container_tags[i]; i++)
		if (!memcmp (tag4, g3_container_tags[i], 4))
			return 1;
	return 0;
}

static int g3_tag_printable (const u8 *tag)
{
	for (int i = 0; i < 4; i++)
		if (tag[i] < 0x20 || tag[i] > 0x7e)
			return 0;
	return 1;
}

static int g3_is_root_only_tag (const u8 *tag4)
{
	for (int i = 0; g3_root_only_tags[i]; i++)
		if (!memcmp (tag4, g3_root_only_tags[i], 4))
			return 1;
	return 0;
}

int IsG3Res (const u8 *data, size_t size)
{
	if (!data || size < G3_CHUNK_HEADER_SIZE)
		return 0;
	if (!memcmp (data, "GRO3", 4) || !memcmp (data, "GDEN", 4))
		return 1;
	if (g3_is_container_tag ((const char *) data))
		return 1;
	if (g3_is_root_only_tag (data))
		return 1;
	return 0;
}

// Decodes the well-known PVR texture sub-header that follows a "PVRT" tag:
//   +4  u32 data_size  (LE, bytes of texel data following this header)
//   +8  u8  pixel_format
//   +9  u8  data_flags
//   +10 u16 reserved
//   +12 u16 width  (LE)
//   +14 u16 height (LE)
static void g3_dump_pvrt (FILE *f, const char *indent, const u8 *data, size_t size, size_t off)
{
	if (off + 16 > size)
		return;
	const u8 *p = data + off;
	u32 data_size = rd_le32 (p + 4);
	u8 pixel_format = p[8];
	u8 data_flags = p[9];
	u16 width = p[12] | p[13] << 8;
	u16 height = p[14] | p[15] << 8;
	fprintf (f, "%s  PVRT header: pixel_format=0x%02x data_flags=0x%02x "
		"width=%u height=%u texel_data_size=0x%x\n",
		indent, pixel_format, data_flags, width, height, data_size);
}

static void g3_walk (FILE *f, const u8 *data, size_t size,
	size_t start, size_t end, int depth)
{
	if (depth > 16) // sanity backstop, never hit on real files
	{
		fprintf (f, "%*s<recursion limit reached, stopping>\n", depth * 2, "");
		return;
	}

	char indent[40];
	int ind_len = depth * 2;
	if (ind_len > (int) sizeof (indent) - 1)
		ind_len = sizeof (indent) - 1;
	memset (indent, ' ', ind_len);
	indent[ind_len] = 0;

	size_t pos = start;
	while (pos + G3_CHUNK_HEADER_SIZE <= end && pos + G3_CHUNK_HEADER_SIZE <= size)
	{
		const u8 *hdr = data + pos;
		if (!g3_tag_printable (hdr))
		{
			fprintf (f, "%s<non-chunk data at 0x%zx, %zu bytes remaining, not decoded>\n",
				indent, pos, end - pos);
			return;
		}

		char tag[5];
		memcpy (tag, hdr, 4);
		tag[4] = 0;
		u32 csize = rd_le32 (hdr + 4);
		u32 coff = rd_le32 (hdr + 8);
		u32 flags = rd_le32 (hdr + 12);

		fprintf (f, "%s@0x%08zx %-4s size=0x%x child_off=0x%x flags=0x%x\n",
			indent, pos, tag, csize, coff, flags);

		if (!strcmp (tag, "PVRT"))
			g3_dump_pvrt (f, indent, data, size, pos);

		if (!g3_is_container_tag (tag))
		{
			// Opaque leaf: its 'size'/'child_offset' fields are not
			// container-shaped and cannot be trusted to find the next
			// sibling, so stop descending this branch here rather than
			// guess. The remaining bytes at this level belong to it.
			fprintf (f, "%s  (opaque leaf, not decoded further)\n", indent);
			return;
		}

		size_t chunk_end = pos + G3_CHUNK_HEADER_SIZE + csize;
		if (chunk_end > end || chunk_end < pos) // overflow / out of range
		{
			fprintf (f, "%s  <chunk size runs past parent, stopping>\n", indent);
			return;
		}

		if (coff && (u64)pos + coff + G3_CHUNK_HEADER_SIZE <= chunk_end
			&& (u64)pos + coff >= (u64)pos + G3_CHUNK_HEADER_SIZE)
			g3_walk (f, data, size, pos + coff, chunk_end, depth + 1);

		if (csize == 0 && !strcmp (tag, "EOFC"))
			return; // end-of-file marker chunk

		pos = chunk_end;
	}
}

enumError DecodeG3Res_Text (FILE *f, const u8 *data, size_t size)
{
	if (!f)
		return EINVAL;
	if (!IsG3Res (data, size))
		return EINVAL;

	fprintf (f, "# Sakura Wars: So Long, My Love \"G3\" resource-chunk tree\n");
	fprintf (f, "# reverse-engineered container/tag/size/offset structure only;\n");
	fprintf (f, "# mesh/animation/texel payloads are NOT decoded (see header)\n");

	g3_walk (f, data, size, 0, size, 0);
	return ERR_OK;
}
