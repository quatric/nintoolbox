#include "lib-rstb.h"
#include "lib-std.h"

// Layout (zeldamods.org/wiki/ResourceSizeTable.product.rsizetable, verified against
// KillzXGaming/Switch-Toolbox SizeTables/RSTB.cs):
//   char magic[4];       // "RSTB"
//   u32  crc_count;      // number of (crc32,size) entries
//   u32  name_count;      // number of (name[128],size) entries
//   { u32 crc32; u32 size; } crc_count times
//   { char name[128]; u32 size; } name_count times
// No relocation pointers -- every field is either fixed-size or a flat repeated record, so the
// whole file length is known up front from the two counts.

#define RSTB_HDR_SIZE 12
#define RSTB_CRC_ENTRY_SIZE 8
#define RSTB_NAME_ENTRY_SIZE 132

bool IsRSTB (const u8 *data, size_t size)
{
	return data && size >= RSTB_HDR_SIZE && !memcmp (data, "RSTB", 4);
}

enumError DecodeRSTB_Text (FILE *out, const u8 *data, size_t size)
{
	if (!out || !IsRSTB (data, size))
		return ERR_INVALID_DATA;

	// real files are little-endian (Switch); the field values themselves are endian agnostic
	// here because we only ever saw sane, small counts in the wild -- read via helper anyway
	// so a future big-endian variant is a one-line change.
	const u32 crc_count = rd_le32 (data + 4);
	const u32 name_count = rd_le32 (data + 8);

	// bound-check the whole table before trusting either count; do the addition at 64 bit
	// width so a hostile/corrupt count can't wrap a 32 bit sum back under 'size'.
	const u64 need = (u64)RSTB_HDR_SIZE + (u64)crc_count * RSTB_CRC_ENTRY_SIZE
		+ (u64)name_count * RSTB_NAME_ENTRY_SIZE;
	if (need > size)
		return ERROR0 (ERR_INVALID_DATA, "RSTB: truncated file (need %llu, have %llu bytes)\n",
			(unsigned long long)need, (u64)size);

	fprintf (out,
		"#RSTB\n"
		"# Resource Size Table -- decoded by "
		"wszst"
		"\n"
		"# crc_count=%u name_count=%u\n\n",
		crc_count, name_count);

	const u8 *p = data + RSTB_HDR_SIZE;
	fprintf (out, "[hash]\n");
	for (u32 i = 0; i < crc_count; i++, p += RSTB_CRC_ENTRY_SIZE)
	{
		const u32 crc = rd_le32 (p);
		const u32 sz = rd_le32 (p + 4);
		fprintf (out, "0x%08x = %u\n", crc, sz);
	}

	fprintf (out, "\n[name]\n");
	for (u32 i = 0; i < name_count; i++, p += RSTB_NAME_ENTRY_SIZE)
	{
		char name[129];
		memcpy (name, p, 128);
		name[128] = 0;
		const u32 sz = rd_le32 (p + 128);
		fprintf (out, "%s = %u\n", name, sz);
	}

	return ERR_OK;
}

// Parses one 'key = value' text line, trimming surrounding whitespace. Returns false on blank
// lines, comments and section headers so the caller can just skip those.
static bool rstb_parse_line (const char *line, char *key, uint keysz, u32 *val)
{
	while (*line == ' ' || *line == '\t')
		line++;
	if (!*line || *line == '#' || *line == '[' || *line == '\n' || *line == '\r')
		return false;

	const char *eq = strchr (line, '=');
	if (!eq)
		return false;

	uint len = (uint)(eq - line);
	while (len > 0 && (line[len - 1] == ' ' || line[len - 1] == '\t'))
		len--;
	if (!len || len >= keysz)
		return false;
	memcpy (key, line, len);
	key[len] = 0;

	*val = (u32)strtoul (eq + 1, 0, 0);
	return true;
}

enumError EncodeRSTB_Text (u8 **dest, uint *dest_size, const char *text, uint text_len, bool is_le)
{
	if (!dest || !dest_size || !text)
		return ERR_INVALID_DATA;

	// pass 1: count entries so we can size the output buffer up front
	uint crc_count = 0, name_count = 0;
	bool in_name_section = false;
	{
		const char *p = text, *end = text + text_len;
		while (p < end)
		{
			const char *nl = memchr (p, '\n', end - p);
			uint linelen = nl ? (uint)(nl - p) : (uint)(end - p);
			char line[256];
			uint copy = linelen < sizeof (line) - 1 ? linelen : sizeof (line) - 1;
			memcpy (line, p, copy);
			line[copy] = 0;

			if (!strncmp (line, "[name]", 6))
				in_name_section = true;
			else if (!strncmp (line, "[hash]", 6))
				in_name_section = false;
			else
			{
				char key[132];
				u32 val;
				if (rstb_parse_line (line, key, sizeof (key), &val))
				{
					if (in_name_section)
						name_count++;
					else
						crc_count++;
				}
			}
			p = nl ? nl + 1 : end;
		}
	}

	const u64 out_size = (u64)RSTB_HDR_SIZE + (u64)crc_count * RSTB_CRC_ENTRY_SIZE
		+ (u64)name_count * RSTB_NAME_ENTRY_SIZE;
	if (out_size > NFMT_MAX_OUTPUT)
		return ERROR0 (ERR_INVALID_DATA, "RSTB: encoded size too large (%llu bytes)\n",
			(unsigned long long)out_size);

	u8 *buf = MALLOC ((uint)out_size);
	if (!buf)
		return ERR_OUT_OF_MEMORY;
	memset (buf, 0, (uint)out_size);
	memcpy (buf, "RSTB", 4);

	u8 *crc_p = buf + RSTB_HDR_SIZE;
	u8 *name_p = crc_p + (u64)crc_count * RSTB_CRC_ENTRY_SIZE;
	uint crc_written = 0, name_written = 0;
	in_name_section = false;

	const char *p = text, *end = text + text_len;
	while (p < end)
	{
		const char *nl = memchr (p, '\n', end - p);
		uint linelen = nl ? (uint)(nl - p) : (uint)(end - p);
		char line[256];
		uint copy = linelen < sizeof (line) - 1 ? linelen : sizeof (line) - 1;
		memcpy (line, p, copy);
		line[copy] = 0;

		if (!strncmp (line, "[name]", 6))
			in_name_section = true;
		else if (!strncmp (line, "[hash]", 6))
			in_name_section = false;
		else
		{
			char key[132];
			u32 val;
			if (rstb_parse_line (line, key, sizeof (key), &val))
			{
				if (in_name_section)
				{
					u8 *e = name_p + (u64)name_written++ * RSTB_NAME_ENTRY_SIZE;
					uint klen = (uint)strlen (key);
					if (klen > 128)
						klen = 128;
					memcpy (e, key, klen);
					if (is_le)
						wr_le32 (e + 128, val);
					else
						wr_be32 (e + 128, val);
				}
				else
				{
					u8 *e = crc_p + (u64)crc_written++ * RSTB_CRC_ENTRY_SIZE;
					const u32 crc = (u32)strtoul (key, 0, 0);
					if (is_le)
					{
						wr_le32 (e, crc);
						wr_le32 (e + 4, val);
					}
					else
					{
						wr_be32 (e, crc);
						wr_be32 (e + 4, val);
					}
				}
			}
		}
		p = nl ? nl + 1 : end;
	}

	if (is_le)
	{
		wr_le32 (buf + 4, crc_written);
		wr_le32 (buf + 8, name_written);
	}
	else
	{
		wr_be32 (buf + 4, crc_written);
		wr_be32 (buf + 8, name_written);
	}

	*dest = buf;
	*dest_size = (uint)out_size;
	return ERR_OK;
}

//-----------------------------------------------------------------------------
// RESTBL (Switch 2, Tears of the Kingdom)

#include <dirent.h>
#include <sys/stat.h>
#include <zlib.h>

#define RESTBL_HDR_SIZE 22

bool IsRESTBL (const u8 *data, size_t size)
{
	return data && size >= RESTBL_HDR_SIZE && !memcmp (data, "RESTBL", 6);
}

typedef struct restbl_name_t
{
	u32 crc;
	char *path;
} restbl_name_t;

typedef struct restbl_names_t
{
	restbl_name_t *list;
	size_t n, cap;
} restbl_names_t;

static void restbl_add_name (restbl_names_t *nl, const char *rel)
{
	char key[PATH_MAX];
	snprintf (key, sizeof (key), "%s", rel);
	const size_t len = strlen (key);
	if (len > 3 && !strcmp (key + len - 3, ".zs"))
		key[len - 3] = 0;
	if (nl->n == nl->cap)
	{
		const size_t cap = nl->cap ? nl->cap * 2 : 4096;
		restbl_name_t *l = REALLOC (nl->list, cap * sizeof (*l));
		if (!l)
			return;
		nl->list = l, nl->cap = cap;
	}
	nl->list[nl->n].crc = (u32)crc32 (0, (const Bytef *)key, (uInt)strlen (key));
	nl->list[nl->n++].path = STRDUP (key);
}

static void restbl_walk (restbl_names_t *nl, const char *root, const char *rel, uint depth)
{
	char dir[PATH_MAX];
	snprintf (dir, sizeof (dir), "%s%s%s", root, *rel ? "/" : "", rel);
	DIR *d = depth < 32 ? opendir (dir) : 0;
	if (!d)
		return;
	struct dirent *de;
	while ((de = readdir (d)))
	{
		if (de->d_name[0] == '.')
			continue;
		char sub[PATH_MAX], full[PATH_MAX];
		snprintf (sub, sizeof (sub), "%s%s%s", rel, *rel ? "/" : "", de->d_name);
		snprintf (full, sizeof (full), "%s/%s", root, sub);
		struct stat st;
		if (stat (full, &st))
			continue;
		if (S_ISDIR (st.st_mode))
		{
			const size_t n = strlen (de->d_name);
			if (n < 2 || strcmp (de->d_name + n - 2, ".d")) // skip extraction output folders
				restbl_walk (nl, root, sub, depth + 1);
		}
		else if (S_ISREG (st.st_mode))
			restbl_add_name (nl, sub);
	}
	closedir (d);
}

static int restbl_cmp (const void *a, const void *b)
{
	const u32 x = ((const restbl_name_t *)a)->crc, y = ((const restbl_name_t *)b)->crc;
	return x < y ? -1 : x > y;
}

enumError DecodeRESTBL_Text (FILE *out, const u8 *data, size_t size, const char *romfs_root)
{
	if (!out || !IsRESTBL (data, size))
		return ERR_INVALID_DATA;
	const u32 version = rd_le32 (data + 6), name_size = rd_le32 (data + 10);
	const u32 crc_count = rd_le32 (data + 14), name_count = rd_le32 (data + 18);
	const u64 need = (u64)RESTBL_HDR_SIZE + (u64)crc_count * 8 + (u64)name_count * ((u64)name_size + 4);
	if (!name_size || name_size > 0x1000 || need > size)
		return ERROR0 (ERR_INVALID_DATA, "RESTBL: truncated file (need %llu, have %llu bytes)\n",
			(unsigned long long)need, (unsigned long long)size);

	restbl_names_t nl = { 0 };
	if (romfs_root && *romfs_root)
	{
		restbl_walk (&nl, romfs_root, "", 0);
		if (nl.n)
			qsort (nl.list, nl.n, sizeof (*nl.list), restbl_cmp);
	}

	fprintf (out,
		"#RESTBL\n"
		"# Resource Size Table (Switch 2 / Tears of the Kingdom) -- decoded by wszst\n"
		"# version=%u name_size=%u crc_count=%u name_count=%u\n",
		version, name_size, crc_count, name_count);
	if (nl.n)
		fprintf (out, "# paths resolved from the %zu files below %s\n", nl.n, romfs_root);

	const u8 *p = data + RESTBL_HDR_SIZE;
	uint resolved = 0;
	fprintf (out, "\n[hash]\n");
	for (u32 i = 0; i < crc_count; i++, p += 8)
	{
		const u32 crc = rd_le32 (p), sz = rd_le32 (p + 4);
		const restbl_name_t key = { crc, 0 };
		const restbl_name_t *hit = nl.n ? bsearch (&key, nl.list, nl.n, sizeof (*nl.list), restbl_cmp) : 0;
		if (hit)
		{
			resolved++;
			fprintf (out, "0x%08x = %u  # %s\n", crc, sz, hit->path);
		}
		else
			fprintf (out, "0x%08x = %u\n", crc, sz);
	}

	fprintf (out, "\n[name]\n");
	for (u32 i = 0; i < name_count; i++, p += name_size + 4)
	{
		const size_t len = strnlen ((const char *)p, name_size);
		fprintf (out, "%.*s = %u\n", (int)len, (const char *)p, rd_le32 (p + name_size));
	}
	if (nl.n)
		fprintf (out, "\n# %u of %u hashes resolved\n", resolved, crc_count);

	for (size_t i = 0; i < nl.n; i++)
		FREE (nl.list[i].path);
	FREE (nl.list);
	return ERR_OK;
}
