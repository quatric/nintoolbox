#include "lib-std.h"
#include "lib-nintendo.h"
#include "lib-afs.h"
#include "lib-archive-util.h"
#include <string.h>

enumError ScanAFS (afs_t *afs, const u8 *data, size_t size)
{
	if (!afs || !data)
		return EINVAL;
	memset (afs, 0, sizeof (*afs));

	if (size < 8 || memcmp (data, "AFS\0", 4))
		return EINVAL;

	afs->raw = data;
	afs->raw_size = size;

	const u32 n_files = rd_le32 (data + 4);
	if (!n_files || n_files > 100000)
		return ERR_INVALID_DATA;

	// entry table (n_files pairs) + one extra footer pair
	const u64 table_end = 8 + (u64)(n_files + 1) * 8;
	if (table_end > size)
		return ERR_INVALID_DATA;

	afs->entries = CALLOC (n_files, sizeof (afs_entry_t));
	if (!afs->entries)
		return ERR_CANT_CREATE;
	afs->n_entries = n_files;

	for (uint i = 0; i < n_files; i++)
	{
		const u8 *p = data + 8 + (u64)i * 8;
		const u32 off = rd_le32 (p);
		const u32 len = rd_le32 (p + 4);
		if (off > size || (u64)off + len > size)
		{
			ResetAFS (afs);
			return ERR_INVALID_DATA;
		}
		afs->entries[i].offset = off;
		afs->entries[i].size = len;
	}

	// footer descriptor: one more { offset, size } pair right after the
	// last real entry, pointing at the name/date metadata table.
	const u8 *foot = data + 8 + (u64)n_files * 8;
	const u32 meta_off = rd_le32 (foot);
	const u32 meta_size = rd_le32 (foot + 4);
	if (meta_off && (u64)meta_off + meta_size <= size && (u64)meta_size >= (u64)n_files * 48)
	{
		afs->meta_offset = meta_off;
		afs->meta_size = meta_size;
		for (uint i = 0; i < n_files; i++)
		{
			const u8 *rec = data + meta_off + (u64)i * 48;
			memcpy (afs->entries[i].name, rec, 32);
			afs->entries[i].name[32] = 0;
			afs->entries[i].year   = rd_le16 (rec + 32);
			afs->entries[i].month  = rd_le16 (rec + 34);
			afs->entries[i].day    = rd_le16 (rec + 36);
			afs->entries[i].hour   = rd_le16 (rec + 38);
			afs->entries[i].minute = rd_le16 (rec + 40);
			afs->entries[i].second = rd_le16 (rec + 42);
			// bytes 44..47 are an unverified/reserved trailing u32;
			// intentionally not exposed -- see lib-afs.h.
		}
	}

	return ERR_OK;
}

void ResetAFS (afs_t *afs)
{
	if (!afs)
		return;
	if (afs->entries)
		FREE (afs->entries);
	memset (afs, 0, sizeof (*afs));
}

enumError ExtractAFSArchive (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext_match (arg, ".afs"))
		return ERR_NOTHING_TO_DO;

	u8 *raw = 0;
	size_t raw_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false);
	if (err)
		return ERR_NOTHING_TO_DO;

	afs_t afs;
	err = ScanAFS (&afs, raw, raw_size);
	if (err)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	char dest[PATH_MAX];
	get_dest_dir (dest, sizeof (dest), arg, basedir);
	CreatePath (dest, true);

	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT AFS:%s (%u files) -> %s/\n",
			verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, afs.n_entries, dest);

	for (uint i = 0; i < afs.n_entries; i++)
	{
		const afs_entry_t *e = afs.entries + i;

		// Retail names are truncated to fixed-width records and are not
		// unique (e.g. several "loa..." or "bt_..." entries in the same
		// archive), so always prefix with the entry index.
		char out_path[PATH_MAX];
		if (e->name[0] && OwnedNameOk (e->name))
			snprintf (out_path, sizeof (out_path), "%s/%04u_%s", dest, i, e->name);
		else
			snprintf (out_path, sizeof (out_path), "%s/%04u.bin", dest, i);

		if (!testmode)
			SaveFile (out_path, 0, 0, raw + e->offset, e->size, 0);
	}

	ResetAFS (&afs);
	FREE (raw);
	return ERR_OK;
}
