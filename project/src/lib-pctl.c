#include "lib-pctl.h"
#include "lib-std.h"
#include "lib-archive-util.h"
#include <sys/stat.h>

// VFXB container, little-endian, matching Switch-Toolbox
// File_Format_Library/FileFormats/Effects/PCTL.cs PTCL.Header.Read()/SectionBase.Read()
// and KillzXGaming/EffectLibrary.

#define PCTL_HDR_SIZE 32
#define PCTL_SECTION_HDR_SIZE 32
#define PCTL_NULL_OFFSET 0xFFFFFFFFu
#define PCTL_MAX_DEPTH 32
#define PCTL_MAX_SECTIONS 200000u
#define PCTL_MAX_GTNT_ENTRIES 4096

bool IsPCTL (const u8 *data, size_t size)
{
	return data && size >= 4 && !memcmp (data, "VFXB", 4);
}

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
		return false;

	uint n = len < destsz - 1 ? (uint)len : destsz - 1;
	memcpy (dest, data + off, n);
	dest[n] = 0;
	return true;
}

typedef struct pctl_walk_result
{
	bool ok;
	bool has_next;
	u64 next_pos;
} pctl_walk_result;

static void print_indent (FILE *out, int indent)
{
	for (int i = 0; i < indent; i++)
		fputc ('\t', out);
}

static pctl_walk_result decode_section (
	FILE *out, const u8 *data, size_t size, u64 pos, int indent, int depth, u32 *budget)
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

	const u32 section_size = rd_le32 (data + pos + 4);
	const u32 subsection_offset = rd_le32 (data + pos + 8);
	const u32 next_section_offset = rd_le32 (data + pos + 12);
	const u32 binary_data_offset = rd_le32 (data + pos + 20);
	const u32 subsection_count = rd_le32 (data + pos + 28);

	print_indent (out, indent);
	fprintf (out, "[%s] offset=%llu size=%u binary_data_offset=%s subsection_count=%u\n", sig,
		(unsigned long long)pos, section_size, binary_data_offset == PCTL_NULL_OFFSET ? "-" : "set",
		subsection_count);

	const u64 bin_pos = (u64)pos + binary_data_offset;
	const bool has_binary = binary_data_offset != PCTL_NULL_OFFSET && bin_pos >= pos;

	if (!memcmp (sig, "TEXR", 4))
	{
		if (has_binary && bin_pos + 48 <= size)
		{
			const u16 width = rd_le16 (data + bin_pos);
			const u16 height = rd_le16 (data + bin_pos + 2);
			const u32 image_size = rd_le32 (data + bin_pos + 28);
			const u32 texture_id = rd_le32 (data + bin_pos + 36);
			const u8 surf_format = data[bin_pos + 40];
			print_indent (out, indent);
			fprintf (out, "  texture: %ux%u format=%u image_size=%u texture_id=0x%x\n", width,
				height, surf_format, image_size, texture_id);
		}
		else
		{
			print_indent (out, indent);
			fprintf (out, "  <texture data out of bounds>\n");
		}
	}
	else if (!memcmp (sig, "TEXA", 4))
	{
		print_indent (out, indent);
		fprintf (out, "  Wii U GX2 texture array (%u textures)\n", subsection_count);
	}
	else if (!memcmp (sig, "GX2B", 4))
	{
		print_indent (out, indent);
		fprintf (out, "  Wii U GX2 raw surface payload (%u bytes)\n", section_size);
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
			fprintf (out, "  %u bytes of emitter parameter data at file offset %llu\n",
				section_size, (unsigned long long)bin_pos);
		else
			fprintf (out, "  <emitter parameter block out of bounds>\n");
	}
	else if (!memcmp (sig, "ESET", 4))
	{
		char name[256];
		bool named = false;
		if (has_binary)
			named = read_cstr (name, sizeof (name), data, size, bin_pos + 16, 64);
		else if (pos + PCTL_SECTION_HDR_SIZE + 16 <= size)
			named
				= read_cstr (name, sizeof (name), data, size, pos + PCTL_SECTION_HDR_SIZE + 16, 64);
		if (named)
		{
			print_indent (out, indent);
			fprintf (out, "  emitter set name = %s\n", name);
		}
	}
	else if (!memcmp (sig, "ESTA", 4))
	{
		print_indent (out, indent);
		fprintf (out, "  emitter list (%u emitter sets)\n", subsection_count);
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
					break;
				const u64 tex_id = rd_le64 (data + p);
				const u32 next_off = rd_le32 (data + p + 8);
				char name[256];
				bool named = read_cstr (name, sizeof (name), data, size, p + 16, 128);

				print_indent (out, indent);
				fprintf (out, "  [%u] texture_id=0x%llx name=%s\n", i, (unsigned long long)tex_id,
					named ? name : "<out of bounds>");

				if (!next_off)
					break;
				const u64 next_p = p + next_off;
				if (next_p <= p || next_p >= size)
					break;
				p = next_p;
			}
		}
	}
	else if (!memcmp (sig, "G3NT", 4))
	{
		if (has_binary)
		{
			u64 p = bin_pos;
			for (uint i = 0; i < PCTL_MAX_GTNT_ENTRIES && p < size; i++)
			{
				if (p + 16 > size)
					break;
				const u64 prim_id = rd_le64 (data + p);
				const u32 next_off = rd_le32 (data + p + 8);
				char name[256];
				bool named = read_cstr (name, sizeof (name), data, size, p + 16, 128);

				print_indent (out, indent);
				fprintf (out, "  [%u] primitive_id=0x%llx name=%s\n", i,
					(unsigned long long)prim_id, named ? name : "<out of bounds>");

				if (!next_off)
					break;
				const u64 next_p = p + next_off;
				if (next_p <= p || next_p >= size)
					break;
				p = next_p;
			}
		}
	}
	else if (!memcmp (sig, "GRTF", 4))
	{
		print_indent (out, indent);
		if (has_binary && bin_pos + 4 <= size && !memcmp (data + bin_pos, "BNTX", 4))
			fprintf (out, "  embedded BNTX texture archive at offset %llu (%u bytes)\n",
				(unsigned long long)bin_pos, section_size);
		else if (has_binary && bin_pos + section_size <= size)
			fprintf (out, "  embedded texture blob at offset %llu (%u bytes)\n",
				(unsigned long long)bin_pos, section_size);
	}
	else if (!memcmp (sig, "G3PR", 4))
	{
		print_indent (out, indent);
		if (has_binary && bin_pos + 4 <= size && !memcmp (data + bin_pos, "FRES", 4))
			fprintf (out, "  embedded BFRES model archive at offset %llu (%u bytes)\n",
				(unsigned long long)bin_pos, section_size);
		else if (has_binary && bin_pos + section_size <= size)
			fprintf (out, "  embedded primitive model blob at offset %llu (%u bytes)\n",
				(unsigned long long)bin_pos, section_size);
	}
	else if (!memcmp (sig, "GRSN", 4))
	{
		print_indent (out, indent);
		if (has_binary && bin_pos + 4 <= size)
		{
			char shmag[5] = { 0 };
			memcpy (shmag, data + bin_pos, 4);
			fprintf (out, "  embedded shader archive (%s) at offset %llu (%u bytes)\n", shmag,
				(unsigned long long)bin_pos, section_size);
		}
	}
	else if (!memcmp (sig, "PRMA", 4))
	{
		print_indent (out, indent);
		fprintf (out, "  primitive list (%u primitives)\n", subsection_count);
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
			pctl_walk_result child
				= decode_section (out, data, size, cur, indent + 1, depth + 1, budget);
			if (!child.ok || !child.has_next)
				break;
			if (child.next_pos <= cur)
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
	const u8 alignment = data[14];
	const u8 target_offset = data[15];
	const u32 header_size = rd_le32 (data + 16);
	const u16 flag = rd_le16 (data + 20);
	const u16 block_offset = rd_le16 (data + 22);
	const u32 file_size = rd_le32 (data + 28);

	if ((u64)block_offset > size)
		return ERROR0 (ERR_INVALID_DATA, "VFXB: block_offset out of bounds\n");
	if (file_size && (u64)file_size > size)
		return ERROR0 (ERR_INVALID_DATA, "VFXB: header file_size (%u) exceeds actual size (%llu)\n",
			file_size, (u64)size);

	char name[33] = "";
	if (size >= 64)
	{
		memcpy (name, data + 32, 32);
		name[32] = 0;
	}

	fprintf (out,
		"#VFXB\n"
		"# NintendoWare particle-effect archive -- section manifest.\n\n"
		"graphics_api_version = %u\n"
		"vfx_version = %u\n"
		"byte_order_mark = 0x%04x\n"
		"alignment = %u\n"
		"target_offset = %u\n"
		"header_size = %u\n"
		"flag = 0x%x\n"
		"block_offset = %u\n"
		"file_size = %u\n",
		graphics_api_version, vfx_version, byte_order_mark, alignment, target_offset, header_size,
		flag, block_offset, file_size);

	if (*name)
		fprintf (out, "name = %s\n", name);

	fprintf (out, "\n[sections]\n");

	u32 budget = PCTL_MAX_SECTIONS;
	u64 pos = block_offset;
	while (pos < size && budget)
	{
		pctl_walk_result r = decode_section (out, data, size, pos, 0, 0, &budget);
		if (!r.ok || !r.has_next)
			break;
		if (r.next_pos <= pos)
			break;
		pos = r.next_pos;
	}

	return ERR_OK;
}

// Extraction helpers
enumError ExtractPCTLArchive (ccp source_file, ccp dest_dir)
{
	u8 *raw = 0;
	size_t raw_size = 0;
	enumError err = LoadFileAlloc (source_file, 0, 0, &raw, &raw_size, 0, 0, 0, false);
	if (err)
		return err;

	if (!IsPCTL (raw, raw_size) || raw_size < PCTL_HDR_SIZE)
	{
		FREE (raw);
		return ERR_INVALID_DATA;
	}

	CreatePath (dest_dir, true);

	// 1. Save PtclHeader.txt (JSON format matching EffectLibrary)
	const u16 graphics_api_version = rd_le16 (raw + 8);
	const u16 vfx_version = rd_le16 (raw + 10);
	const u16 byte_order_mark = rd_le16 (raw + 12);
	const u8 alignment = raw[14];
	const u8 target_offset = raw[15];
	const u16 flag = rd_le16 (raw + 20);
	const u16 block_offset = rd_le16 (raw + 22);
	const u32 file_size = rd_le32 (raw + 28);

	char name[33] = "";
	if (raw_size >= 64)
	{
		memcpy (name, raw + 32, 32);
		name[32] = 0;
	}

	char hdr_path[PATH_MAX];
	snprintf (hdr_path, sizeof (hdr_path), "%s/PtclHeader.txt", dest_dir);
	FILE *hf = fopen (hdr_path, "w");
	if (hf)
	{
		fprintf (hf,
			"{\n"
			"  \"Header\": {\n"
			"    \"Magic\": 1112884822,\n" // "VFXB"
			"    \"GraphicsAPIVersion\": %u,\n"
			"    \"VFXVersion\": %u,\n"
			"    \"ByteOrder\": %u,\n"
			"    \"Alignment\": %u,\n"
			"    \"TargetAddressSize\": %u,\n"
			"    \"NameOffset\": 32,\n"
			"    \"Flag\": %u,\n"
			"    \"BlockOffset\": %u,\n"
			"    \"RelocationTableOffset\": 0,\n"
			"    \"FileSize\": %u\n"
			"  },\n"
			"  \"Name\": \"%s\"\n"
			"}\n",
			graphics_api_version, vfx_version, byte_order_mark, alignment, target_offset, flag,
			block_offset, file_size, name);
		fclose (hf);
	}

	// 3. Walk sections and dump textures.bntx, models.bfres, shaders.bnsh, emitters, etc.
	u64 pos = block_offset;
	u32 budget = PCTL_MAX_SECTIONS;

	while (pos + PCTL_SECTION_HDR_SIZE <= raw_size && budget--)
	{
		char sig[5] = { 0 };
		memcpy (sig, raw + pos, 4);

		const u32 section_size = rd_le32 (raw + pos + 4);
		const u32 subsection_offset = rd_le32 (raw + pos + 8);
		const u32 next_section_offset = rd_le32 (raw + pos + 12);
		const u32 binary_data_offset = rd_le32 (raw + pos + 20);
		const u32 subsection_count = rd_le32 (raw + pos + 28);

		const u64 bin_pos = (u64)pos + binary_data_offset;
		const bool has_bin = binary_data_offset != PCTL_NULL_OFFSET && bin_pos < raw_size;

		if (!memcmp (sig, "GRTF", 4) && has_bin)
		{
			// Extract embedded BNTX texture archive
			size_t bsize = section_size;
			if (bin_pos + bsize > raw_size)
				bsize = raw_size - bin_pos;
			char tex_path[PATH_MAX];
			snprintf (tex_path, sizeof (tex_path), "%s/textures.bntx", dest_dir);
			FILE *tf = fopen (tex_path, "wb");
			if (tf)
			{
				fwrite (raw + bin_pos, 1, bsize, tf);
				fclose (tf);
			}
		}
		else if (!memcmp (sig, "G3PR", 4) && has_bin)
		{
			// Extract embedded BFRES model archive
			size_t msize = section_size;
			if (bin_pos + msize > raw_size)
				msize = raw_size - bin_pos;
			char mdl_path[PATH_MAX];
			snprintf (mdl_path, sizeof (mdl_path), "%s/models.bfres", dest_dir);
			FILE *mf = fopen (mdl_path, "wb");
			if (mf)
			{
				fwrite (raw + bin_pos, 1, msize, mf);
				fclose (mf);
			}
		}
		else if (!memcmp (sig, "GRSN", 4) && has_bin)
		{
			// Extract embedded BNSH / BFSHA shader archive
			size_t s_size = section_size;
			if (bin_pos + s_size > raw_size)
				s_size = raw_size - bin_pos;
			char sh_path[PATH_MAX];
			bool is_bfsha = bin_pos + 4 <= raw_size && !memcmp (raw + bin_pos, "FSHA", 4);
			snprintf (
				sh_path, sizeof (sh_path), "%s/shaders.%s", dest_dir, is_bfsha ? "bfsha" : "bnsh");
			FILE *sf = fopen (sh_path, "wb");
			if (sf)
			{
				fwrite (raw + bin_pos, 1, s_size, sf);
				fclose (sf);
			}
		}
		else if (!memcmp (sig, "PRMA", 4) && has_bin)
		{
			size_t psize = section_size;
			if (bin_pos + psize > raw_size)
				psize = raw_size - bin_pos;
			char prm_path[PATH_MAX];
			snprintf (prm_path, sizeof (prm_path), "%s/primitives.bin", dest_dir);
			FILE *pf = fopen (prm_path, "wb");
			if (pf)
			{
				fwrite (raw + bin_pos, 1, psize, pf);
				fclose (pf);
			}
		}
		else if (!memcmp (sig, "ESTA", 4) && subsection_offset != PCTL_NULL_OFFSET)
		{
			// Extract Emitter Sets
			char eset_info_path[PATH_MAX];
			snprintf (eset_info_path, sizeof (eset_info_path), "%s/EmitterSetInfo.txt", dest_dir);
			FILE *einf = fopen (eset_info_path, "w");
			if (einf)
				fprintf (einf, "{\n  \"Order\": [\n");

			u64 eset_pos = pos + subsection_offset;
			const u32 n_esets = subsection_count ? subsection_count : 1;
			for (u32 ei = 0; ei < n_esets && eset_pos + PCTL_SECTION_HDR_SIZE <= raw_size; ei++)
			{
				const u32 eset_sub_off = rd_le32 (raw + eset_pos + 8);
				const u32 eset_next_off = rd_le32 (raw + eset_pos + 12);
				const u32 eset_bin_off = rd_le32 (raw + eset_pos + 20);
				const u32 eset_sub_count = rd_le32 (raw + eset_pos + 28);

				char eset_name[128] = "";
				if (eset_bin_off != PCTL_NULL_OFFSET)
					read_cstr (eset_name, sizeof (eset_name), raw, raw_size,
						eset_pos + eset_bin_off + 16, 64);
				if (!*eset_name || !OwnedNameOk (eset_name))
					snprintf (eset_name, sizeof (eset_name), "EmitterSet_%03u", ei);

				if (einf)
					fprintf (einf, "    \"%s\"%s\n", eset_name, (ei + 1 < n_esets) ? "," : "");

				char eset_dir[PATH_MAX];
				snprintf (eset_dir, sizeof (eset_dir), "%s/%s", dest_dir, eset_name);
				CreatePath (eset_dir, true);

				char eorder_path[PATH_MAX];
				snprintf (eorder_path, sizeof (eorder_path), "%s/EmitterOrder.txt", eset_dir);
				FILE *eordf = fopen (eorder_path, "w");
				if (eordf)
					fprintf (eordf, "{\n  \"Order\": [\n");

				if (eset_sub_off != PCTL_NULL_OFFSET)
				{
					u64 emtr_pos = eset_pos + eset_sub_off;
					const u32 n_emtrs = eset_sub_count ? eset_sub_count : 1;
					for (u32 mi = 0; mi < n_emtrs && emtr_pos + PCTL_SECTION_HDR_SIZE <= raw_size;
						mi++)
					{
						const u32 emtr_sec_size = rd_le32 (raw + emtr_pos + 4);
						const u32 emtr_next_off = rd_le32 (raw + emtr_pos + 12);
						const u32 emtr_bin_off = rd_le32 (raw + emtr_pos + 20);

						char emtr_name[128] = "";
						if (emtr_bin_off != PCTL_NULL_OFFSET)
							read_cstr (emtr_name, sizeof (emtr_name), raw, raw_size,
								emtr_pos + emtr_bin_off + 16, 64);
						if (!*emtr_name || !OwnedNameOk (emtr_name))
							snprintf (emtr_name, sizeof (emtr_name), "Emitter_%03u", mi);

						if (eordf)
							fprintf (
								eordf, "    \"%s\"%s\n", emtr_name, (mi + 1 < n_emtrs) ? "," : "");

						char emtr_dir[PATH_MAX];
						snprintf (emtr_dir, sizeof (emtr_dir), "%s/%s", eset_dir, emtr_name);
						CreatePath (emtr_dir, true);

						if (emtr_bin_off != PCTL_NULL_OFFSET)
						{
							u64 ebin_pos = emtr_pos + emtr_bin_off;
							size_t ebin_size = emtr_sec_size;
							if (ebin_pos + ebin_size > raw_size)
								ebin_size = raw_size - ebin_pos;

							char emtr_bin_path[PATH_MAX];
							snprintf (emtr_bin_path, sizeof (emtr_bin_path), "%s/EmitterData.bin",
								emtr_dir);
							FILE *ebf = fopen (emtr_bin_path, "wb");
							if (ebf)
							{
								fwrite (raw + ebin_pos, 1, ebin_size, ebf);
								fclose (ebf);
							}
						}

						if (emtr_next_off == PCTL_NULL_OFFSET || emtr_next_off == 0)
							break;
						emtr_pos += emtr_next_off;
					}
				}

				if (eordf)
				{
					fprintf (eordf, "  ]\n}\n");
					fclose (eordf);
				}

				if (eset_next_off == PCTL_NULL_OFFSET || eset_next_off == 0)
					break;
				eset_pos += eset_next_off;
			}

			if (einf)
			{
				fprintf (einf, "  ]\n}\n");
				fclose (einf);
			}
		}

		if (next_section_offset == PCTL_NULL_OFFSET || next_section_offset == 0)
			break;
		pos += next_section_offset;
	}

	FREE (raw);
	return ERR_OK;
}

enumError CreatePCTLArchive (ccp source_dir, ccp dest_file)
{
	char base_path[PATH_MAX];
	snprintf (base_path, sizeof (base_path), "%s/Base.ptcl", source_dir);
	struct stat st;

	u8 *ptcl_raw = 0;
	size_t ptcl_size = 0;

	if (stat (base_path, &st) != 0)
	{
		snprintf (base_path, sizeof (base_path), "%s/particle.ptcl", source_dir);
	}

	if (stat (base_path, &st) == 0)
	{
		enumError err = LoadFileAlloc (base_path, 0, 0, &ptcl_raw, &ptcl_size, 0, 0, 0, false);
		if (err || !ptcl_raw)
			return err ? err : ERR_CANT_OPEN;

		// Check for updated textures.bntx, models.bfres, shaders.bnsh
		char bntx_path[PATH_MAX];
		snprintf (bntx_path, sizeof (bntx_path), "%s/textures.bntx", source_dir);
		struct stat bntx_st;
		if (stat (bntx_path, &bntx_st) == 0)
		{
			u8 *bntx_data = 0;
			size_t bntx_size = 0;
			if (!LoadFileAlloc (bntx_path, 0, 0, &bntx_data, &bntx_size, 0, 0, 0, false)
				&& bntx_data)
			{
				// Locate GRTF section in ptcl_raw
				const u16 block_offset = rd_le16 (ptcl_raw + 22);
				u64 pos = block_offset;
				while (pos + PCTL_SECTION_HDR_SIZE <= ptcl_size)
				{
					if (!memcmp (ptcl_raw + pos, "GRTF", 4))
					{
						const u32 bin_off = rd_le32 (ptcl_raw + pos + 20);
						if (bin_off != PCTL_NULL_OFFSET && pos + bin_off < ptcl_size)
						{
							u64 bpos = pos + bin_off;
							const u32 old_sz = rd_le32 (ptcl_raw + pos + 4);
							if (bntx_size <= old_sz)
							{
								memcpy (ptcl_raw + bpos, bntx_data, bntx_size);
								if (bntx_size < old_sz)
									memset (ptcl_raw + bpos + bntx_size, 0, old_sz - bntx_size);
							}
						}
						break;
					}
					const u32 next_off = rd_le32 (ptcl_raw + pos + 12);
					if (next_off == PCTL_NULL_OFFSET || next_off == 0)
						break;
					pos += next_off;
				}
				FREE (bntx_data);
			}
		}

		FILE *out = fopen (dest_file, "wb");
		if (!out)
		{
			FREE (ptcl_raw);
			return ERR_CANT_CREATE;
		}
		fwrite (ptcl_raw, 1, ptcl_size, out);
		fclose (out);
		FREE (ptcl_raw);
		return ERR_OK;
	}

	return ERROR0 (ERR_NOT_EXISTS, "Base.ptcl not found in %s\n", source_dir);
}
