// SPDX-License-Identifier: GPL-2.0+
// Ubisoft "Magma" bigfile format -- see lib-magma.h for exactly what is
// and is not understood about it.

#include "lib-magma.h"
#include "lib-nintendo.h"
#include "lib-archive-util.h"
#include <stdio.h>
#include <string.h>

#define MAGMA_FAT_RECORD_MIN 21 // 20-byte fixed part + at least a NUL
#define MAGMA_FAT_MAX_RECORDS 200000 // sanity cap
#define MAGMA_FAT_MAX_PATH 4096 // sanity cap on a single path_len

#define MAGMA_BF_HEADER_SIZE 16

//-----------------------------------------------------------------------------

static int magma_path_ok (const u8 *path, u32 path_len)
{
	if (!path_len || path_len > MAGMA_FAT_MAX_PATH)
		return 0;
	if (path[path_len - 1] != 0)
		return 0;
	for (u32 i = 0; i < path_len - 1; i++)
	{
		u8 c = path[i];
		// Real paths seen are plain ASCII with '/' separators; reject
		// control bytes and high-bit bytes so random binary data can't
		// masquerade as a record run.
		if (c < 0x20 || c >= 0x7f)
			return 0;
	}
	return 1;
}

int IsMagmaFat (const u8 *data, size_t size, size_t file_size)
{
	if (!data || size < MAGMA_FAT_RECORD_MIN || file_size < MAGMA_FAT_RECORD_MIN)
		return 0;

	size_t off = 0;
	uint n = 0;
	int saw_full_record = 0;

	while (off + 20 <= size)
	{
		const u8 *e = data + off;
		u32 self_off = rd_le32 (e);
		u32 path_len = rd_le32 (e + 16);

		if (self_off != off)
			return 0;
		if (!path_len || path_len > MAGMA_FAT_MAX_PATH)
			return 0;
		if (off + 20 + path_len > size)
		{
			// Record announces a path we can't fully see. That's fine
			// for a short FILETYPE probe buffer (size < file_size) as
			// long as the probe wasn't obviously bogus (i.e. it is
			// actually short, not just an internally inconsistent
			// record in a fully loaded buffer).
			return size < file_size;
		}

		if (!magma_path_ok (e + 20, path_len))
			return 0;

		off += 20 + path_len;
		saw_full_record = 1;
		if (++n > MAGMA_FAT_MAX_RECORDS)
			return 0;
	}

	// Whole buffer consumed exactly, or trailing bytes too short to be
	// another 20-byte header -- either way, only accept if we saw the
	// entire real file and it parsed cleanly end to end.
	return saw_full_record && size == file_size && off == size;
}

enumError DecodeMagmaFat_Text (FILE *f, const u8 *data, size_t size, size_t file_size)
{
	if (!f || !data)
		return EINVAL;
	if (!IsMagmaFat (data, size, file_size))
		return EINVAL;

	fprintf (f, "# Ubisoft Magma \".fat\" bigfile index\n");
	fprintf (f, "# self_offset is cross-checked against each record's real position\n");
	fprintf (f, "# 'zero' is an unexplained field always observed as 0\n");
	fprintf (f, "\n");
	fprintf (f, "# idx   self-off   data-off   data-size  zero  path\n");

	size_t off = 0;
	uint idx = 0;
	while (off + 20 <= size)
	{
		const u8 *e = data + off;
		u32 self_off = rd_le32 (e);
		u32 data_off = rd_le32 (e + 4);
		u32 data_size = rd_le32 (e + 8);
		u32 zero = rd_le32 (e + 12);
		u32 path_len = rd_le32 (e + 16);
		if (off + 20 + path_len > size)
			break;

		fprintf (f, "%5u  0x%08x 0x%08x 0x%08x %5u  %.*s\n", idx, self_off, data_off, data_size,
			zero, (int)(path_len ? path_len - 1 : 0), e + 20);

		off += 20 + path_len;
		idx++;
	}

	fprintf (f, "\n# %u records total\n", idx);
	return ERR_OK;
}

//-----------------------------------------------------------------------------

int IsMagmaBigfile (const u8 *data, size_t size, size_t file_size)
{
	if (!data || size < MAGMA_BF_HEADER_SIZE || memcmp (data, "BIG\0", 4))
		return 0;

	// field_a/field_b/field_c (bytes 4..15) vary per title and are not
	// understood (see lib-magma.h); only sanity-bound field_c, which was
	// observed as a small single-digit count (5/5/6) on every sample
	// file seen, to avoid matching unrelated data that merely starts
	// with the four bytes "BIG\0".
	u32 field_c = rd_le32 (data + 12);
	if (!field_c || field_c > 4096)
		return 0;

	(void)file_size;
	return 1;
}

//-----------------------------------------------------------------------------

enumError ExtractMagmaFat (ccp fat_path, ccp data_path, ccp dest_dir)
{
	if (!fat_path || !data_path || !dest_dir)
		return EINVAL;

	u8 *fat = 0;
	size_t fat_size = 0;
	enumError err = LoadFileAlloc (fat_path, 0, 0, &fat, &fat_size, 0, 0, 0, false);
	if (err)
		return err;

	if (!IsMagmaFat (fat, fat_size, fat_size))
	{
		FREE (fat);
		return ERR_INVALID_DATA;
	}

	FILE *df = fopen (data_path, "rb");
	if (!df)
	{
		FREE (fat);
		return ERR_CANT_OPEN;
	}

	fseek (df, 0, SEEK_END);
	long data_total = ftell (df);

	size_t off = 0;
	enumError result = ERR_OK;
	while (off + 20 <= fat_size)
	{
		const u8 *e = fat + off;
		u32 data_off = rd_le32 (e + 4);
		u32 data_size = rd_le32 (e + 8);
		u32 path_len = rd_le32 (e + 16);
		if (off + 20 + path_len > fat_size)
			break;
		ccp path = (ccp)(e + 20);

		if (!OwnedNameOk (path))
		{
			fprintf (stderr, "WARNING: Magma entry '%s' has unsafe path -- skipped\n", path);
			off += 20 + path_len;
			result = ERR_WARNING;
			continue;
		}

		if (data_total < 0 || (u64)data_off + data_size > (u64)data_total)
		{
			fprintf (stderr,
				"WARNING: Magma entry '%s' (offset 0x%x, size 0x%x) exceeds '%s' (%ld bytes) -- "
				"skipped\n",
				path, data_off, data_size, data_path, data_total);
			off += 20 + path_len;
			result = ERR_WARNING;
			continue;
		}

		char out_path[PATH_MAX];
		snprintf (out_path, sizeof (out_path), "%s/%s", dest_dir, path);
		CreatePath (out_path, true);

		u8 *buf = MALLOC (data_size ? data_size : 1);
		if (!buf)
		{
			fprintf (stderr, "WARNING: out of memory for Magma entry '%s' -- skipped\n", path);
			off += 20 + path_len;
			result = ERR_WARNING;
			continue;
		}
		if (fseek (df, data_off, SEEK_SET) || fread (buf, 1, data_size, df) != data_size)
		{
			fprintf (
				stderr, "WARNING: failed to read Magma entry '%s' from '%s'\n", path, data_path);
			FREE (buf);
			off += 20 + path_len;
			result = ERR_WARNING;
			continue;
		}

		SaveFile (out_path, 0, 0, buf, data_size, 0);
		FREE (buf);

		off += 20 + path_len;
	}

	fclose (df);
	FREE (fat);
	return result;
}
