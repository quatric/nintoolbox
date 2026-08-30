#include "lib-bfsar.h"
#include <string.h>

static u32 rd_u32e (const u8 *p, bool le)
{
	return le ? (u32)p[0] | (u32)p[1] << 8 | (u32)p[2] << 16 | (u32)p[3] << 24
			  : (u32)p[0] << 24 | (u32)p[1] << 16 | (u32)p[2] << 8 | p[3];
}
static u16 rd_u16e (const u8 *p, bool le)
{
	return le ? (u16)(p[0] | p[1] << 8) : (u16)((u16)p[0] << 8 | p[1]);
}
static s32 rd_s32e (const u8 *p, bool le)
{
	return (s32)rd_u32e (p, le);
}

#define NULL_PTR ((s32) - 1)

// -----------------------------------------------------------------------------
///////////////		    STRG (string table + name lookup)	///////////////
// -----------------------------------------------------------------------------
// See lib-bfsar.h: layout from Citric Composer's SoundArchive.cs StrgBlock,
// exact offset arithmetic confirmed against real bytes (not trusted from
// the C# alone -- e.g. string data sits at block_abs + entry.offset + 24,
// a "+24" the source doesn't explain and this project isn't guessing at
// either, just reproducing what real data showed).

typedef struct leaf_t
{
	u32 id;
	u32 string_index;
} leaf_t;

static enumError scan_strg (const u8 *data, uint size, const u8 *block, bool le,
	char ***out_strings, uint *out_n_strings, leaf_t **out_leaves, uint *out_n_leaves)
{
	const u8 *base = data; // for bounds checks against the whole file
	if ((u64)(block - base) + 0x18 > size || memcmp (block, "STRG", 4))
		return ERROR0 (ERR_INVALID_DATA, "ScanBFSAR: missing STRG block\n");

	s32 string_table_off = rd_s32e (block + 0xC, le);
	s32 lookup_table_off = rd_s32e (block + 0x14, le);
	if (string_table_off == NULL_PTR)
		return ERROR0 (ERR_INVALID_DATA, "ScanBFSAR: STRG has no string table\n");

	const u8 *strtab = block + string_table_off + 8;
	if ((u64)(strtab - base) + 4 > size)
		return ERROR0 (ERR_INVALID_DATA, "ScanBFSAR: STRG string table out of range\n");
	u32 n_strings = rd_u32e (strtab, le);
	if ((u64)(strtab - base) + 4 + 12ull * n_strings > size)
		return ERROR0 (ERR_INVALID_DATA,
			"ScanBFSAR: STRG string table exceeds buffer (%u entries)\n", n_strings);

	char **strings = n_strings ? CALLOC (n_strings, sizeof (*strings)) : 0;
	for (u32 i = 0; i < n_strings; i++)
	{
		const u8 *r = strtab + 4 + 12 * i;
		s32 off = rd_s32e (r + 4, le);
		u32 sz = rd_u32e (r + 8, le);
		const u8 *s = block + off + 24;
		if (!sz || (u64)(s - base) + sz > size)
		{
			strings[i] = STRDUP ("");
			continue;
		}
		char *str = MALLOC (sz);
		memcpy (str, s, sz - 1);
		str[sz - 1] = 0;
		strings[i] = str;
	}

	leaf_t *leaves = 0;
	uint n_leaves = 0;
	if (lookup_table_off != NULL_PTR)
	{
		const u8 *lookup = block + lookup_table_off + 8;
		if ((u64)(lookup - base) + 8 > size)
			return ERROR0 (ERR_INVALID_DATA, "ScanBFSAR: STRG lookup table out of range\n");
		u32 n_nodes = rd_u32e (lookup + 4, le);
		const u8 *recs = lookup + 8;
		if ((u64)(recs - base) + 20ull * n_nodes > size)
			return ERROR0 (ERR_INVALID_DATA,
				"ScanBFSAR: STRG lookup table exceeds buffer (%u nodes)\n", n_nodes);

		leaves = n_nodes ? MALLOC (n_nodes * sizeof (*leaves)) : 0;
		for (u32 i = 0; i < n_nodes; i++)
		{
			const u8 *r = recs + 20 * i;
			u16 leaf_flag = rd_u16e (r, le);
			if (!leaf_flag)
				continue;
			leaves[n_leaves].string_index = rd_u32e (r + 0xC, le);
			leaves[n_leaves].id = rd_u32e (r + 0x10, le);
			n_leaves++;
		}
	}

	*out_strings = strings;
	*out_n_strings = n_strings;
	*out_leaves = leaves;
	*out_n_leaves = n_leaves;
	return ERR_OK;
}

static ccp lookup_name (const leaf_t *leaves, uint n_leaves, char **strings, uint n_strings, u32 id)
{
	for (uint i = 0; i < n_leaves; i++)
		if (leaves[i].id == id)
			return leaves[i].string_index < n_strings ? strings[leaves[i].string_index] : 0;
	return 0;
}

// -----------------------------------------------------------------------------
///////////////		    INFO (the real directory)		///////////////
// -----------------------------------------------------------------------------

static const struct
{
	u16 ref_type;
	bfsar_sound_type_t type;
	ccp name;
} info_sections[] = {
	{ 0x2100, BFSAR_TYPE_SOUND, "Sound" },
	{ 0x2101, BFSAR_TYPE_BANK, "Bank" },
	{ 0x2102, BFSAR_TYPE_PLAYER, "Player" },
	{ 0x2103, BFSAR_TYPE_WAVEARCHIVE, "WaveArchive" },
	{ 0x2104, BFSAR_TYPE_SOUNDGROUP, "SoundGroup" },
	{ 0x2105, BFSAR_TYPE_GROUP, "Group" },
	{ 0x2106, BFSAR_TYPE_NONE, "File" }, // no Id/name -- not in the Sound Types enum
};

static enumError scan_info (const u8 *data, uint size, const u8 *block, bool le,
	const leaf_t *leaves, uint n_leaves, char **strings, uint n_strings, bfsar_table_t *out_table,
	uint *out_n_table)
{
	if ((u64)(block - data) + 0x48 > size || memcmp (block, "INFO", 4))
		return ERROR0 (ERR_INVALID_DATA, "ScanBFSAR: missing INFO block\n");

	const u8 *begin = block + 8; // References below are relative to here
	uint n_table = 0;

	for (uint s = 0; s < sizeof (info_sections) / sizeof (*info_sections); s++)
	{
		const u8 *ref = begin + 8 * s;
		if ((u64)(ref - data) + 8 > size)
			return ERROR0 (ERR_INVALID_DATA, "ScanBFSAR: INFO section table truncated\n");
		s32 off = rd_s32e (ref + 4, le);
		if (off == NULL_PTR)
			continue;

		const u8 *table = begin + off;
		if ((u64)(table - data) + 4 > size)
			return ERROR0 (ERR_INVALID_DATA, "ScanBFSAR: INFO section '%s' out of range\n",
				info_sections[s].name);
		u32 count = rd_u32e (table, le);
		if ((u64)(table - data) + 4 + 8ull * count > size)
			return ERROR0 (ERR_INVALID_DATA,
				"ScanBFSAR: INFO section '%s' exceeds buffer (%u entries)\n", info_sections[s].name,
				count);

		bfsar_table_t *t = out_table + n_table++;
		t->type = info_sections[s].type;
		t->type_name = info_sections[s].name;
		t->n_entry = count;
		t->entry = count ? CALLOC (count, sizeof (*t->entry)) : 0;

		for (u32 i = 0; i < count; i++)
		{
			const u8 *er = table + 4 + 8 * i;
			s32 eoff = rd_s32e (er + 4, le);
			bfsar_entry_t *e = t->entry + i;
			e->present = eoff != NULL_PTR;
			e->id = t->type != BFSAR_TYPE_NONE ? ((u32)t->type << 24 | i) : 0;
			e->name = t->type != BFSAR_TYPE_NONE
				? lookup_name (leaves, n_leaves, strings, n_strings, e->id)
				: 0;
		}
	}

	*out_n_table = n_table;
	return ERR_OK;
}

// -----------------------------------------------------------------------------
///////////////		    top-level scan			///////////////
// -----------------------------------------------------------------------------

enumError ScanBFSAR (bfsar_t *bfsar, const u8 *data, uint size)
{
	memset (bfsar, 0, sizeof (*bfsar));

	if (size < 0x20)
		return ERROR0 (ERR_INVALID_DATA, "ScanBFSAR: too small\n");

	bool is_ctr;
	if (!memcmp (data, "FSAR", 4))
		is_ctr = false;
	else if (!memcmp (data, "CSAR", 4))
		is_ctr = true;
	else
		return ERROR0 (ERR_INVALID_DATA, "ScanBFSAR: not a FSAR/CSAR file\n");

	u16 bom = (u16)(data[4] << 8 | data[5]);
	bool le;
	if (bom == 0xFEFF)
		le = false;
	else if (bom == 0xFFFE)
		le = true;
	else
		return ERROR0 (ERR_INVALID_DATA, "ScanBFSAR: bad byte-order mark\n");

	u32 version = rd_u32e (data + 8, le);
	// 'F' type: 0x00MMIIRR; 'C' type: 0xMMIIRR00 (see lib-bfsar.h provenance).
	u8 vmaj, vmin, vrev;
	if (is_ctr)
	{
		vmaj = (version >> 24) & 0xff;
		vmin = (version >> 16) & 0xff;
		vrev = (version >> 8) & 0xff;
	}
	else
	{
		vmaj = (version >> 16) & 0xff;
		vmin = (version >> 8) & 0xff;
		vrev = version & 0xff;
	}

	u16 n_blocks = rd_u16e (data + 0x10, le);
	if ((u64)0x14 + 12ull * n_blocks > size)
		return ERROR0 (
			ERR_INVALID_DATA, "ScanBFSAR: block table exceeds buffer (%u blocks)\n", n_blocks);

	const u8 *strg = 0, *info = 0;
	for (uint i = 0; i < n_blocks; i++)
	{
		const u8 *r = data + 0x14 + 12 * i;
		u32 off = rd_u32e (r + 4, le); // block-table offsets are absolute file offsets
		if ((u64)off + 8 > size)
			return ERROR0 (ERR_INVALID_DATA, "ScanBFSAR: block %u out of range\n", i);
		const u8 *blk = data + off;
		if (!memcmp (blk, "STRG", 4))
			strg = blk;
		else if (!memcmp (blk, "INFO", 4))
			info = blk;
		// FILE block (raw sample-data pool) and any unknown block types are
		// intentionally not parsed here -- see lib-bfsar.h scope note.
	}

	if (!strg)
		return ERROR0 (ERR_INVALID_DATA, "ScanBFSAR: missing STRG block\n");
	if (!info)
		return ERROR0 (ERR_INVALID_DATA, "ScanBFSAR: missing INFO block\n");

	leaf_t *leaves = 0;
	uint n_leaves = 0;
	enumError err
		= scan_strg (data, size, strg, le, &bfsar->strings, &bfsar->n_strings, &leaves, &n_leaves);
	if (!err)
		err = scan_info (data, size, info, le, leaves, n_leaves, bfsar->strings, bfsar->n_strings,
			bfsar->table, &bfsar->n_table);
	FREE (leaves);

	if (err)
	{
		ResetBFSAR (bfsar);
		return err;
	}

	bfsar->is_ctr = is_ctr;
	bfsar->little_endian = le;
	bfsar->version_major = vmaj;
	bfsar->version_minor = vmin;
	bfsar->version_revision = vrev;
	return ERR_OK;
}

void ResetBFSAR (bfsar_t *bfsar)
{
	if (!bfsar)
		return;
	for (uint t = 0; t < bfsar->n_table; t++)
		FREE (bfsar->table[t].entry);
	for (uint i = 0; i < bfsar->n_strings; i++)
		FREE (bfsar->strings[i]);
	FREE (bfsar->strings);
	memset (bfsar, 0, sizeof (*bfsar));
}

// -----------------------------------------------------------------------------
///////////////		    XML dump				///////////////
// -----------------------------------------------------------------------------

static void xml_escape (FILE *f, ccp s)
{
	if (!s)
		return;
	for (; *s; s++)
	{
		switch (*s)
		{
			case '&':
				fputs ("&amp;", f);
				break;
			case '<':
				fputs ("&lt;", f);
				break;
			case '>':
				fputs ("&gt;", f);
				break;
			case '"':
				fputs ("&quot;", f);
				break;
			default:
				fputc (*s, f);
				break;
		}
	}
}

enumError DumpBFSAR_XML (const bfsar_t *bfsar, FILE *f, ccp source_name)
{
	fprintf (f, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
	fprintf (f, "<bfsar source=\"%s\" magic=\"%s\" version=\"%u.%u.%u\" endian=\"%s\">\n",
		source_name ? source_name : "", bfsar->is_ctr ? "CSAR" : "FSAR", bfsar->version_major,
		bfsar->version_minor, bfsar->version_revision, bfsar->little_endian ? "little" : "big");

	for (uint t = 0; t < bfsar->n_table; t++)
	{
		const bfsar_table_t *tab = bfsar->table + t;
		fprintf (f, "  <table type=\"%s\" n=\"%u\">\n", tab->type_name, tab->n_entry);
		for (uint i = 0; i < tab->n_entry; i++)
		{
			const bfsar_entry_t *e = tab->entry + i;
			fprintf (f, "    <entry index=\"%u\" id=\"0x%08x\" present=\"%s\"", i, e->id,
				e->present ? "yes" : "no");
			if (e->name)
			{
				fprintf (f, " name=\"");
				xml_escape (f, e->name);
				fprintf (f, "\"");
			}
			fprintf (f, "/>\n");
		}
		fprintf (f, "  </table>\n");
	}

	fprintf (f, "</bfsar>\n");
	return ERR_OK;
}
