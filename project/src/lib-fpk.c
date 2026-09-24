// SPDX-License-Identifier: GPL-2.0+
#include "lib-std.h"
#include "lib-nintendo.h"
#include "lib-fpk.h"
#include "lib-archive-util.h"
#include <string.h>

#define FPK_MAGIC	0x1234567au
#define FPK_ENTRY_SIZE	28

enumError ScanFPK (fpk_t *fpk, const u8 *data, size_t size)
{
	if (!fpk || !data)
		return EINVAL;
	memset (fpk, 0, sizeof (*fpk));

	if (size < 0x18 || rd_le32 (data) != FPK_MAGIC)
		return EINVAL;

	const u32 n_entries = rd_le32 (data + 4);
	if (!n_entries || n_entries > 1000000)
		return ERR_INVALID_DATA;

	const u64 table_end = 0x18 + (u64)n_entries * FPK_ENTRY_SIZE;
	if (table_end > size)
		return ERR_INVALID_DATA;

	fpk->raw = data;
	fpk->raw_size = size;
	fpk->entries = CALLOC (n_entries, sizeof (fpk_entry_t));
	if (!fpk->entries)
		return ERR_CANT_CREATE;
	fpk->n_entries = n_entries;

	for (uint i = 0; i < n_entries; i++)
	{
		const u8 *p = data + 0x18 + (u64)i * FPK_ENTRY_SIZE;
		const u32 name_off = rd_le32 (p);
		const u32 data_off = rd_le32 (p + 4);
		const u32 data_size = rd_le32 (p + 8);
		const u32 type = rd_le32 (p + 12);

		if ( name_off >= size || (u64)data_off + data_size > size )
		{
			ResetFPK (fpk);
			return ERR_INVALID_DATA;
		}

		fpk_entry_t *e = fpk->entries + i;
		e->name_offset = name_off;
		e->data_offset = data_off;
		e->data_size   = data_size;
		e->type        = type;

		// name is a NUL-terminated string somewhere before the end of file;
		// bound the copy defensively in case of a corrupt/truncated table.
		const size_t max_len = size - name_off < sizeof (e->name) - 1
					? size - name_off : sizeof (e->name) - 1;
		size_t len = 0;
		while (len < max_len && data[name_off + len])
			len++;
		memcpy (e->name, data + name_off, len);
		e->name[len] = 0;
	}

	return ERR_OK;
}

void ResetFPK (fpk_t *fpk)
{
	if (!fpk)
		return;
	if (fpk->entries)
		FREE (fpk->entries);
	memset (fpk, 0, sizeof (*fpk));
}

enumError ExtractFPKArchive (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext_match (arg, ".fpk"))
		return ERR_NOTHING_TO_DO;

	u8 *raw = 0;
	size_t raw_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false);
	if (err)
		return ERR_NOTHING_TO_DO;

	fpk_t fpk;
	err = ScanFPK (&fpk, raw, raw_size);
	if (err)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	char dest[PATH_MAX];
	get_dest_dir (dest, sizeof (dest), arg, basedir);
	CreatePath (dest, true);

	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT FPK:%s (%u files) -> %s/\n",
			verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, fpk.n_entries, dest);

	for (uint i = 0; i < fpk.n_entries; i++)
	{
		const fpk_entry_t *e = fpk.entries + i;

		// Stored names use the original TT tool path with backslashes and
		// a leading "audio\..." prefix that is not unique across packages
		// and not a valid subdirectory separator here, so flatten it to a
		// leaf filename and always prefix with the entry index (entries
		// are not guaranteed unique even as leaf names).
		ccp leaf = strrchr (e->name, '\\');
		leaf = leaf ? leaf + 1 : e->name;

		char out_path[PATH_MAX];
		if (leaf[0] && OwnedNameOk (leaf))
			snprintf (out_path, sizeof (out_path), "%s/%04u_%s", dest, i, leaf);
		else
			snprintf (out_path, sizeof (out_path), "%s/%04u.bin", dest, i);

		if (!testmode)
			SaveFile (out_path, 0, 0, raw + e->data_offset, e->data_size, 0);
	}

	ResetFPK (&fpk);
	FREE (raw);
	return ERR_OK;
}
