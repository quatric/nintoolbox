#include "lib-effn.h"
#include "lib-pctl.h"
#include "lib-std.h"
#include "lib-archive-util.h"
#include <ctype.h>
#include <sys/stat.h>

// Bandai Namco Effect File (.eff / .effn, magic "EFFN").
// Used in Super Smash Bros 4 (Wii U / 3DS) and Super Smash Bros Ultimate (Switch).
// Reference: KillzXGaming/EffectLibrary FileData/EFFN/NamcoEffectFile.cs.

#define EFFN_MAGIC "EFFN"
#define EFFN_HDR_SIZE 16
#define EFFN_ENTRY_SIZE 16
#define EFFN_VARIANT_SIZE 4

bool IsEFFN (const u8 *data, size_t size)
{
	return data && size >= EFFN_HDR_SIZE && !memcmp (data, EFFN_MAGIC, 4);
}

typedef struct effn_entry_raw_t
{
	u16 kind;
	u16 unknown;
	u32 emitter_set_id;
	u32 external_model_idx; // 1-based index (0 = none)
	u16 variant_start_idx;  // 1-based index (0 = none)
	u16 variant_count;
} effn_entry_raw_t;

typedef struct effn_variant_raw_t
{
	u16 start_frame;
	u16 emitter_set_id;
} effn_variant_raw_t;

// Reads a null-terminated UTF-8 string starting at *offset.
static char *read_effn_string (const u8 *data, size_t size, size_t *offset)
{
	if (*offset >= size)
		return NULL;
	size_t start = *offset;
	size_t len = 0;
	while (start + len < size && data[start + len] != 0)
		len++;
	if (start + len >= size)
		return NULL;

	char *str = MALLOC (len + 1);
	if (!str)
		return NULL;
	memcpy (str, data + start, len);
	str[len] = '\0';
	*offset = start + len + 1;
	return str;
}

enumError DecodeEFFN_Text (FILE *out, const u8 *data, size_t size)
{
	if (!out || !IsEFFN (data, size))
		return ERR_INVALID_DATA;

	const u32 version             = rd_le32 (data + 4);
	const u16 num_effects         = rd_le16 (data + 8);
	const u16 num_external_models = rd_le16 (data + 10);
	const u16 multi_part_effects  = rd_le16 (data + 12);
	const u16 header_chunk_align  = rd_le16 (data + 14);

	fprintf (out, "#EFFN\n"
		"# Bandai Namco Effect File (Super Smash Bros 4 / Ultimate)\n\n"
		"version = %u (0x%08x)\n"
		"num_effects = %u\n"
		"num_external_models = %u\n"
		"multi_part_effects = %u\n"
		"header_chunk_align = %u\n\n",
		version, version, num_effects, num_external_models,
		multi_part_effects, header_chunk_align);

	size_t cur = EFFN_HDR_SIZE;
	const size_t entries_size = (size_t)num_effects * EFFN_ENTRY_SIZE;
	const size_t variants_size = (size_t)multi_part_effects * EFFN_VARIANT_SIZE;
	const size_t models_size = (size_t)num_external_models;

	if (cur + entries_size + variants_size + models_size > size)
		return ERROR0 (ERR_INVALID_DATA, "EFFN: header offsets exceed file size\n");

	const u8 *entries_ptr = data + cur;
	cur += entries_size;

	const u8 *variants_ptr = data + cur;
	cur += variants_size;

	const u8 *models_ptr = data + cur;
	cur += models_size;

	// Read string tables
	char **entry_names = CALLOC (num_effects ? num_effects : 1, sizeof (char *));
	char **model_names = CALLOC (num_external_models ? num_external_models : 1, sizeof (char *));
	char **bone_names = CALLOC (multi_part_effects ? multi_part_effects : 1, sizeof (char *));
	if (!entry_names || !model_names || !bone_names)
	{
		FREE (entry_names);
		FREE (model_names);
		FREE (bone_names);
		return ERR_OUT_OF_MEMORY;
	}

	for (uint i = 0; i < num_effects; i++)
		entry_names[i] = read_effn_string (data, size, &cur);

	for (uint i = 0; i < num_external_models; i++)
		model_names[i] = read_effn_string (data, size, &cur);

	for (uint i = 0; i < multi_part_effects; i++)
		bone_names[i] = read_effn_string (data, size, &cur);

	// Locate embedded VFXB
	size_t align_bytes = (header_chunk_align == 2) ? 8192 : 4096;
	size_t vfxb_offset = (cur + align_bytes) & ~(align_bytes - 1);
	if (vfxb_offset >= size || memcmp (data + vfxb_offset, "VFXB", 4) != 0)
	{
		// Fallback probe for VFXB if alignment differed
		for (size_t p = (cur + 15) & ~15; p + 4 <= size && p < cur + 16384; p += 16)
		{
			if (!memcmp (data + p, "VFXB", 4))
			{
				vfxb_offset = p;
				break;
			}
		}
	}

	fprintf (out, "[effects]\n");
	for (uint i = 0; i < num_effects; i++)
	{
		const size_t ep = i * EFFN_ENTRY_SIZE;
		const u16 kind = rd_le16 (entries_ptr + ep);
		const u16 unknown = rd_le16 (entries_ptr + ep + 2);
		const u32 emitter_set_id = rd_le32 (entries_ptr + ep + 4);
		const u32 model_idx = rd_le32 (entries_ptr + ep + 8);
		const u16 var_start = rd_le16 (entries_ptr + ep + 12);
		const u16 var_count = rd_le16 (entries_ptr + ep + 14);

		ccp name = entry_names[i] ? entry_names[i] : "<unnamed>";
		fprintf (out, "  [%u] name=%s kind=%u unknown=%u emitter_set_id=%u\n",
			i, name, kind, unknown, emitter_set_id);

		if (model_idx > 0 && model_idx <= num_external_models)
		{
			uint midx = model_idx - 1;
			u8 flag = models_ptr[midx];
			ccp mname = model_names[midx] ? model_names[midx] : "<unknown>";
			fprintf (out, "      model: name=%s flag=0x%02x\n", mname, flag);
		}

		if (var_count > 0 && var_start > 0)
		{
			uint sidx = var_start - 1;
			for (uint j = 0; j < var_count && sidx + j < multi_part_effects; j++)
			{
				const size_t vp = (sidx + j) * EFFN_VARIANT_SIZE;
				const u16 start_frame = rd_le16 (variants_ptr + vp);
				const u16 eset_id = rd_le16 (variants_ptr + vp + 2);
				ccp bname = bone_names[sidx + j] ? bone_names[sidx + j] : "<none>";
				fprintf (out, "      variant [%u]: start_frame=%u emitter_set_id=%u bone=%s\n",
					j, start_frame, eset_id, bname);
			}
		}
	}

	if (vfxb_offset < size && !memcmp (data + vfxb_offset, "VFXB", 4))
	{
		fprintf (out, "\n[embedded_vfxb]\n"
			"offset = %zu\n"
			"size = %zu\n\n",
			vfxb_offset, size - vfxb_offset);
		DecodePCTL_Text (out, data + vfxb_offset, size - vfxb_offset);
	}
	else
	{
		fprintf (out, "\n[embedded_vfxb]\n"
			"<no valid VFXB found at aligned offset %zu>\n", vfxb_offset);
	}

	// Free strings
	for (uint i = 0; i < num_effects; i++)
		FREE (entry_names[i]);
	for (uint i = 0; i < num_external_models; i++)
		FREE (model_names[i]);
	for (uint i = 0; i < multi_part_effects; i++)
		FREE (bone_names[i]);
	FREE (entry_names);
	FREE (model_names);
	FREE (bone_names);

	return ERR_OK;
}

// Writes a string escaping quotes and backslashes for JSON.
static void write_json_str (FILE *f, ccp str)
{
	fputc ('"', f);
	if (str)
	{
		for (const char *p = str; *p; p++)
		{
			if (*p == '"' || *p == '\\')
				fputc ('\\', f);
			fputc (*p, f);
		}
	}
	fputc ('"', f);
}

enumError ExtractEFFNArchive (ccp source_file, ccp dest_dir)
{
	u8 *raw = 0;
	size_t raw_size = 0;
	enumError err = LoadFileAlloc (source_file, 0, 0, &raw, &raw_size, 0, 0, 0, false);
	if (err)
		return err;

	if (!IsEFFN (raw, raw_size))
	{
		FREE (raw);
		return ERR_INVALID_DATA;
	}

	const u16 num_effects         = rd_le16 (raw + 8);
	const u16 num_external_models = rd_le16 (raw + 10);
	const u16 multi_part_effects  = rd_le16 (raw + 12);
	const u16 header_chunk_align  = rd_le16 (raw + 14);

	size_t cur = EFFN_HDR_SIZE;
	const size_t entries_size = (size_t)num_effects * EFFN_ENTRY_SIZE;
	const size_t variants_size = (size_t)multi_part_effects * EFFN_VARIANT_SIZE;
	const size_t models_size = (size_t)num_external_models;

	if (cur + entries_size + variants_size + models_size > raw_size)
	{
		FREE (raw);
		return ERROR0 (ERR_INVALID_DATA, "EFFN: file truncated\n");
	}

	const u8 *entries_ptr = raw + cur;
	cur += entries_size;

	const u8 *variants_ptr = raw + cur;
	cur += variants_size;

	const u8 *models_ptr = raw + cur;
	cur += models_size;

	char **entry_names = CALLOC (num_effects ? num_effects : 1, sizeof (char *));
	char **model_names = CALLOC (num_external_models ? num_external_models : 1, sizeof (char *));
	char **bone_names = CALLOC (multi_part_effects ? multi_part_effects : 1, sizeof (char *));
	if (!entry_names || !model_names || !bone_names)
	{
		FREE (entry_names);
		FREE (model_names);
		FREE (bone_names);
		FREE (raw);
		return ERR_OUT_OF_MEMORY;
	}

	for (uint i = 0; i < num_effects; i++)
		entry_names[i] = read_effn_string (raw, raw_size, &cur);

	for (uint i = 0; i < num_external_models; i++)
		model_names[i] = read_effn_string (raw, raw_size, &cur);

	for (uint i = 0; i < multi_part_effects; i++)
		bone_names[i] = read_effn_string (raw, raw_size, &cur);

	// Find VFXB offset
	size_t align_bytes = (header_chunk_align == 2) ? 8192 : 4096;
	size_t vfxb_offset = (cur + align_bytes) & ~(align_bytes - 1);
	if (vfxb_offset >= raw_size || memcmp (raw + vfxb_offset, "VFXB", 4) != 0)
	{
		for (size_t p = (cur + 15) & ~15; p + 4 <= raw_size && p < cur + 16384; p += 16)
		{
			if (!memcmp (raw + p, "VFXB", 4))
			{
				vfxb_offset = p;
				break;
			}
		}
	}

	CreatePath (dest_dir, true);

	// 1. Export NamcoFile.json (EffectLibrary compatible format)
	char json_path[PATH_MAX];
	snprintf (json_path, sizeof (json_path), "%s/NamcoFile.json", dest_dir);
	FILE *jf = fopen (json_path, "w");
	if (jf)
	{
		fprintf (jf, "[\n");
		for (uint i = 0; i < num_effects; i++)
		{
			const size_t ep = i * EFFN_ENTRY_SIZE;
			const u16 kind = rd_le16 (entries_ptr + ep);
			const u16 unknown = rd_le16 (entries_ptr + ep + 2);
			const u32 emitter_set_id = rd_le32 (entries_ptr + ep + 4);
			const u32 model_idx = rd_le32 (entries_ptr + ep + 8);
			const u16 var_start = rd_le16 (entries_ptr + ep + 12);
			const u16 var_count = rd_le16 (entries_ptr + ep + 14);

			fprintf (jf, "  {\n");
			fprintf (jf, "    \"Name\": ");
			write_json_str (jf, entry_names[i] ? entry_names[i] : "");
			fprintf (jf, ",\n    \"Kind\": %u,\n    \"Unknown\": %u,\n    \"EmitterSet_ID\": %u",
				kind, unknown, emitter_set_id);

			if (model_idx > 0 && model_idx <= num_external_models)
			{
				uint midx = model_idx - 1;
				u8 flag = models_ptr[midx];
				ccp mname = model_names[midx] ? model_names[midx] : "";
				fprintf (jf, ",\n    \"ExternalModelFlag\": %u,\n    \"ExternalModelString\": ", flag);
				write_json_str (jf, mname);
			}

			fprintf (jf, ",\n    \"Variants\": [");
			if (var_count > 0 && var_start > 0)
			{
				uint sidx = var_start - 1;
				fprintf (jf, "\n");
				for (uint j = 0; j < var_count && sidx + j < multi_part_effects; j++)
				{
					const size_t vp = (sidx + j) * EFFN_VARIANT_SIZE;
					const u16 start_frame = rd_le16 (variants_ptr + vp);
					const u16 eset_id = rd_le16 (variants_ptr + vp + 2);
					ccp bname = bone_names[sidx + j] ? bone_names[sidx + j] : "";

					fprintf (jf, "      {\n");
					fprintf (jf, "        \"BoneName\": ");
					write_json_str (jf, bname);
					fprintf (jf, ",\n        \"StartFrame\": %u,\n        \"EmitterSetID\": %u\n",
						start_frame, eset_id);
					fprintf (jf, "      }%s\n", (j + 1 < var_count) ? "," : "");
				}
				fprintf (jf, "    ");
			}
			fprintf (jf, "]\n  }%s\n", (i + 1 < num_effects) ? "," : "");
		}
		fprintf (jf, "]\n");
		fclose (jf);
	}

	// 2. Export embedded particle.ptcl and Base.ptcl
	if (vfxb_offset < raw_size && !memcmp (raw + vfxb_offset, "VFXB", 4))
	{
		const u8 *vfxb_data = raw + vfxb_offset;
		const size_t vfxb_size = raw_size - vfxb_offset;

		char ptcl_path[PATH_MAX];
		snprintf (ptcl_path, sizeof (ptcl_path), "%s/particle.ptcl", dest_dir);
		FILE *pf = fopen (ptcl_path, "wb");
		if (pf)
		{
			fwrite (vfxb_data, 1, vfxb_size, pf);
			fclose (pf);
		}

		char base_path[PATH_MAX];
		snprintf (base_path, sizeof (base_path), "%s/Base.ptcl", dest_dir);
		FILE *bf = fopen (base_path, "wb");
		if (bf)
		{
			fwrite (vfxb_data, 1, vfxb_size, bf);
			fclose (bf);
		}
	}

	// Clean up
	for (uint i = 0; i < num_effects; i++)
		FREE (entry_names[i]);
	for (uint i = 0; i < num_external_models; i++)
		FREE (model_names[i]);
	for (uint i = 0; i < multi_part_effects; i++)
		FREE (bone_names[i]);
	FREE (entry_names);
	FREE (model_names);
	FREE (bone_names);
	FREE (raw);

	return ERR_OK;
}

// Minimal json string parser helper
static char *json_skip_ws (char *p)
{
	while (*p && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n'))
		p++;
	return p;
}

static char *json_parse_str_val (char *p, char *out, size_t out_max)
{
	p = json_skip_ws (p);
	if (*p != '"')
		return NULL;
	p++;
	size_t len = 0;
	while (*p && *p != '"')
	{
		if (*p == '\\' && p[1])
			p++;
		if (len + 1 < out_max)
			out[len++] = *p;
		p++;
	}
	out[len] = '\0';
	if (*p == '"')
		p++;
	return p;
}

static char *json_parse_uint_val (char *p, uint *val)
{
	p = json_skip_ws (p);
	char *end = 0;
	*val = (uint)strtoul (p, &end, 0);
	return end ? end : p;
}

typedef struct parsed_variant_t
{
	char bone_name[128];
	u16 start_frame;
	u16 emitter_set_id;
} parsed_variant_t;

typedef struct parsed_entry_t
{
	char name[128];
	u16 kind;
	u16 unknown;
	u32 emitter_set_id;
	u8 model_flag;
	char model_name[128];
	parsed_variant_t variants[64];
	uint variant_count;
} parsed_entry_t;

enumError CreateEFFNArchive (ccp source_dir, ccp dest_file)
{
	// Find NamcoFile.json or namco.json
	char json_path[PATH_MAX];
	snprintf (json_path, sizeof (json_path), "%s/NamcoFile.json", source_dir);
	struct stat st;
	if (stat (json_path, &st) != 0)
	{
		snprintf (json_path, sizeof (json_path), "%s/namco.json", source_dir);
		if (stat (json_path, &st) != 0)
			return ERROR0 (ERR_NOT_EXISTS, "EFFN: NamcoFile.json not found in %s\n", source_dir);
	}

	// Find particle.ptcl or Base.ptcl
	char ptcl_path[PATH_MAX];
	snprintf (ptcl_path, sizeof (ptcl_path), "%s/particle.ptcl", source_dir);
	if (stat (ptcl_path, &st) != 0)
	{
		snprintf (ptcl_path, sizeof (ptcl_path), "%s/Base.ptcl", source_dir);
		if (stat (ptcl_path, &st) != 0)
			return ERROR0 (ERR_NOT_EXISTS, "EFFN: particle.ptcl not found in %s\n", source_dir);
	}

	u8 *ptcl_raw = 0;
	size_t ptcl_size = 0;
	enumError err = LoadFileAlloc (ptcl_path, 0, 0, &ptcl_raw, &ptcl_size, 0, 0, 0, false);
	if (err || !ptcl_raw)
		return err ? err : ERR_CANT_OPEN;

	char *jdata = 0;
	size_t jsize = 0;
	err = LoadFileAlloc (json_path, 0, 0, (u8 **)&jdata, &jsize, 0, 0, 0, false);
	if (err || !jdata)
	{
		FREE (ptcl_raw);
		return err ? err : ERR_CANT_OPEN;
	}

	// Parse JSON entries
	parsed_entry_t *entries = CALLOC (2048, sizeof (parsed_entry_t));
	uint entry_count = 0;

	char *p = jdata;
	while (*p && entry_count < 2048)
	{
		p = strchr (p, '{');
		if (!p)
			break;
		p++;

		parsed_entry_t *cur_ent = &entries[entry_count];
		cur_ent->name[0] = '\0';
		cur_ent->model_name[0] = '\0';
		cur_ent->variant_count = 0;

		while (*p && *p != '}')
		{
			p = json_skip_ws (p);
			if (*p == '"')
			{
				char key[64] = "";
				p = json_parse_str_val (p, key, sizeof (key));
				p = json_skip_ws (p);
				if (*p == ':')
					p++;
				p = json_skip_ws (p);

				if (!strcmp (key, "Name"))
				{
					p = json_parse_str_val (p, cur_ent->name, sizeof (cur_ent->name));
				}
				else if (!strcmp (key, "Kind"))
				{
					uint v = 0;
					p = json_parse_uint_val (p, &v);
					cur_ent->kind = (u16)v;
				}
				else if (!strcmp (key, "Unknown"))
				{
					uint v = 0;
					p = json_parse_uint_val (p, &v);
					cur_ent->unknown = (u16)v;
				}
				else if (!strcmp (key, "EmitterSet_ID"))
				{
					uint v = 0;
					p = json_parse_uint_val (p, &v);
					cur_ent->emitter_set_id = v;
				}
				else if (!strcmp (key, "ExternalModelFlag"))
				{
					uint v = 0;
					p = json_parse_uint_val (p, &v);
					cur_ent->model_flag = (u8)v;
				}
				else if (!strcmp (key, "ExternalModelString"))
				{
					p = json_parse_str_val (p, cur_ent->model_name, sizeof (cur_ent->model_name));
				}
				else if (!strcmp (key, "Variants"))
				{
					p = json_skip_ws (p);
					if (*p == '[')
					{
						p++;
						while (*p && *p != ']' && cur_ent->variant_count < 64)
						{
							p = strchr (p, '{');
							if (!p)
								break;
							p++;
							parsed_variant_t *var = &cur_ent->variants[cur_ent->variant_count];
							var->bone_name[0] = '\0';
							var->start_frame = 0;
							var->emitter_set_id = 0;

							while (*p && *p != '}')
							{
								p = json_skip_ws (p);
								if (*p == '"')
								{
									char vkey[64] = "";
									p = json_parse_str_val (p, vkey, sizeof (vkey));
									p = json_skip_ws (p);
									if (*p == ':')
										p++;
									p = json_skip_ws (p);

									if (!strcmp (vkey, "BoneName"))
										p = json_parse_str_val (p, var->bone_name, sizeof (var->bone_name));
									else if (!strcmp (vkey, "StartFrame"))
									{
										uint v = 0;
										p = json_parse_uint_val (p, &v);
										var->start_frame = (u16)v;
									}
									else if (!strcmp (vkey, "EmitterSetID"))
									{
										uint v = 0;
										p = json_parse_uint_val (p, &v);
										var->emitter_set_id = (u16)v;
									}
									else
									{
										while (*p && *p != ',' && *p != '}')
											p++;
									}
								}
								if (*p == ',')
									p++;
							}
							if (*p == '}')
								p++;
							cur_ent->variant_count++;
							p = json_skip_ws (p);
							if (*p == ',')
								p++;
						}
						if (*p == ']')
							p++;
					}
				}
				else
				{
					while (*p && *p != ',' && *p != '}')
						p++;
				}
			}
			if (*p == ',')
				p++;
		}
		if (*p == '}')
			p++;
		if (cur_ent->name[0] != '\0')
			entry_count++;
	}

	FREE (jdata);

	// Count variants and external models
	uint total_variants = 0;
	uint total_models = 0;
	for (uint i = 0; i < entry_count; i++)
	{
		total_variants += entries[i].variant_count;
		if (entries[i].model_name[0] != '\0')
			total_models++;
	}

	// Calculate unaligned header size
	size_t raw_hdr_size = EFFN_HDR_SIZE + entry_count * EFFN_ENTRY_SIZE
		+ total_variants * EFFN_VARIANT_SIZE + total_models;

	for (uint i = 0; i < entry_count; i++)
		raw_hdr_size += strlen (entries[i].name) + 1;
	for (uint i = 0; i < entry_count; i++)
		if (entries[i].model_name[0] != '\0')
			raw_hdr_size += strlen (entries[i].model_name) + 1;
	for (uint i = 0; i < entry_count; i++)
		for (uint j = 0; j < entries[i].variant_count; j++)
			raw_hdr_size += strlen (entries[i].variants[j].bone_name) + 1;

	// Determine required chunk alignment (4096 or 8192)
	size_t aligned_hdr_size = (raw_hdr_size + 0x1000) & ~0xFFFu;
	u16 chunk_align_flag = 1;
	if (aligned_hdr_size > 4096)
	{
		chunk_align_flag = 2;
		aligned_hdr_size = (raw_hdr_size + 0x2000) & ~0x1FFFu;
	}

	u8 *out_buf = CALLOC (aligned_hdr_size + ptcl_size, 1);
	if (!out_buf)
	{
		FREE (entries);
		FREE (ptcl_raw);
		return ERR_OUT_OF_MEMORY;
	}

	// 1. Write Header
	memcpy (out_buf, EFFN_MAGIC, 4);
	wr_le32 (out_buf + 4, 131072); // version 0x00020000
	wr_le16 (out_buf + 8, (u16)entry_count);
	wr_le16 (out_buf + 10, (u16)total_models);
	wr_le16 (out_buf + 12, (u16)total_variants);
	wr_le16 (out_buf + 14, chunk_align_flag);

	u8 *p_entries = out_buf + EFFN_HDR_SIZE;
	u8 *p_variants = p_entries + entry_count * EFFN_ENTRY_SIZE;
	u8 *p_models = p_variants + total_variants * EFFN_VARIANT_SIZE;
	u8 *p_strings = p_models + total_models;

	uint cur_var_idx = 0;
	uint cur_model_idx = 0;

	// Fill entries, variants, and model flags
	for (uint i = 0; i < entry_count; i++)
	{
		const size_t ep = i * EFFN_ENTRY_SIZE;
		wr_le16 (p_entries + ep, entries[i].kind);
		wr_le16 (p_entries + ep + 2, entries[i].unknown);
		wr_le32 (p_entries + ep + 4, entries[i].emitter_set_id);

		if (entries[i].model_name[0] != '\0')
		{
			cur_model_idx++;
			wr_le32 (p_entries + ep + 8, cur_model_idx); // 1-based
			p_models[cur_model_idx - 1] = entries[i].model_flag;
		}
		else
			wr_le32 (p_entries + ep + 8, 0);

		if (entries[i].variant_count > 0)
		{
			wr_le16 (p_entries + ep + 12, (u16)(cur_var_idx + 1)); // 1-based
			wr_le16 (p_entries + ep + 14, (u16)entries[i].variant_count);

			for (uint j = 0; j < entries[i].variant_count; j++)
			{
				const size_t vp = cur_var_idx * EFFN_VARIANT_SIZE;
				wr_le16 (p_variants + vp, entries[i].variants[j].start_frame);
				wr_le16 (p_variants + vp + 2, entries[i].variants[j].emitter_set_id);
				cur_var_idx++;
			}
		}
		else
		{
			wr_le16 (p_entries + ep + 12, 0);
			wr_le16 (p_entries + ep + 14, 0);
		}
	}

	// Write string tables: entry names, model names, bone names
	for (uint i = 0; i < entry_count; i++)
	{
		const size_t slen = strlen (entries[i].name) + 1;
		memcpy (p_strings, entries[i].name, slen);
		p_strings += slen;
	}
	for (uint i = 0; i < entry_count; i++)
	{
		if (entries[i].model_name[0] != '\0')
		{
			const size_t slen = strlen (entries[i].model_name) + 1;
			memcpy (p_strings, entries[i].model_name, slen);
			p_strings += slen;
		}
	}
	for (uint i = 0; i < entry_count; i++)
	{
		for (uint j = 0; j < entries[i].variant_count; j++)
		{
			const size_t slen = strlen (entries[i].variants[j].bone_name) + 1;
			memcpy (p_strings, entries[i].variants[j].bone_name, slen);
			p_strings += slen;
		}
	}

	// Append embedded VFXB at aligned offset
	memcpy (out_buf + aligned_hdr_size, ptcl_raw, ptcl_size);

	FILE *out_f = fopen (dest_file, "wb");
	if (!out_f)
	{
		FREE (out_buf);
		FREE (entries);
		FREE (ptcl_raw);
		return ERR_CANT_CREATE;
	}
	fwrite (out_buf, 1, aligned_hdr_size + ptcl_size, out_f);
	fclose (out_f);

	FREE (out_buf);
	FREE (entries);
	FREE (ptcl_raw);
	return ERR_OK;
}
