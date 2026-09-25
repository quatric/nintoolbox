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

	fprintf (out, "#RSTB\n"
		"# Resource Size Table -- decoded by " "wszst" "\n"
		"# crc_count=%u name_count=%u\n\n", crc_count, name_count);

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
