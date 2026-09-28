// SPDX-License-Identifier: GPL-2.0+
// Illvelo (Wii) Sega "Ninja" chunk model/motion family -- see lib-ninja.h
// for exactly what is and is not decoded.

#include "lib-ninja.h"
#include "lib-nintendo.h"
#include <string.h>

#define NJ_CHUNK_HEADER_SIZE 8

static int nj_tag_printable (const u8 *tag)
{
	for (int i = 0; i < 4; i++)
		if (tag[i] < 0x20 || tag[i] > 0x7e)
			return 0;
	return 1;
}

int IsNinjaChunk (const u8 *data, size_t size)
{
	if (!data || size < NJ_CHUNK_HEADER_SIZE)
		return 0;
	return !memcmp (data, "LTJN", 4) // NJTL
		|| !memcmp (data, "MCJN", 4) // NJCM
		|| !memcmp (data, "MDMN", 4) // NMDM
		|| !memcmp (data, "MACN", 4); // NCAM (camera motion; seen as a lone .njm root)
}

// Decodes the confirmed NJTL (texture list) body: see lib-ninja.h for the
// byte-exact layout this was verified against.
static void nj_dump_njtl (FILE *f, const u8 *body, u32 body_size)
{
	if (body_size < 8)
	{
		fprintf (f, "  <NJTL body too small to hold header fields>\n");
		return;
	}
	const u32 texlist_off = rd_be32 (body);
	const u32 tex_count = rd_be32 (body + 4);
	fprintf (f, "  texlist_rel_off=0x%x tex_count=%u\n", texlist_off, tex_count);

	if ((u64)texlist_off + (u64)tex_count * 12 > body_size)
	{
		fprintf (f, "  <texture list entry array runs past chunk body, stopping>\n");
		return;
	}

	for (u32 i = 0; i < tex_count; i++)
	{
		const u8 *entry = body + texlist_off + (size_t)i * 12;
		const u32 name_off = rd_be32 (entry);
		const u32 global_index = rd_be32 (entry + 4);
		const u32 flags = rd_be32 (entry + 8);

		fprintf (f, "  [%u] global_index=0x%x flags=0x%x name=", i, global_index, flags);
		if (name_off < body_size)
		{
			const u8 *name = body + name_off;
			const u8 *end = body + body_size;
			const u8 *p = name;
			while (p < end && *p)
				p++;
			fprintf (f, "\"%.*s\"\n", (int)(p - name), name);
		}
		else
			fprintf (f, "<name_rel_off 0x%x out of range>\n", name_off);
	}
}

enumError DecodeNinjaChunk_Text (FILE *f, const u8 *data, size_t size)
{
	if (!f || !IsNinjaChunk (data, size))
		return EINVAL;

	fprintf (f, "# Illvelo (Wii) Sega \"Ninja\" chunk model/motion file\n");
	fprintf (f,
		"# reverse-engineered top-level chunk tree only: tags are\n"
		"# byte-reversed ASCII (\"LTJN\"=NJTL, \"MCJN\"=NJCM, \"MDMN\"=NMDM,\n"
		"# \"0FOP\"=POF0), all numeric fields are big-endian; only the NJTL\n"
		"# texture list is decoded, NJCM/NMDM mesh/motion payloads are not\n"
		"# (see lib-ninja.h)\n");

	size_t pos = 0;
	while (pos + NJ_CHUNK_HEADER_SIZE <= size)
	{
		const u8 *hdr = data + pos;
		if (!nj_tag_printable (hdr))
		{
			fprintf (f, "@0x%08zx <non-chunk data, %zu bytes remaining, not decoded>\n", pos,
				size - pos);
			break;
		}

		char tag[5];
		memcpy (tag, hdr, 4);
		tag[4] = 0;
		const u32 body_size = rd_be32 (hdr + 4);
		const u64 chunk_end = (u64)pos + NJ_CHUNK_HEADER_SIZE + body_size;

		fprintf (f, "@0x%08zx %-4s size=0x%x\n", pos, tag, body_size);
		if (chunk_end > size)
		{
			fprintf (f, "  <chunk size runs past end of file, stopping>\n");
			break;
		}

		if (!strcmp (tag, "LTJN"))
			nj_dump_njtl (f, hdr + NJ_CHUNK_HEADER_SIZE, body_size);
		else
			fprintf (f, "  (opaque chunk, not decoded further)\n");

		pos = (size_t)chunk_end;
	}

	return ERR_OK;
}
