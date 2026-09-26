#include "lib-std.h"
#include "lib-gpak.h"
#include "lib-arcv.h"
#include "lib-jarc.h"
#include <string.h>
#include <dirent.h>

void ResetGPAK (gpak_t *pak)
{
	if (!pak)
		return;
	FREE (pak->entries);
	memset (pak, 0, sizeof (*pak));
}

enumError ScanGPAK (gpak_t *pak, const u8 *data, uint size)
{
	if (!pak || !data || size < 16)
		return ERR_NOTHING_TO_DO;

	memset (pak, 0, sizeof (*pak));

	const u32 n = rd_be32 (data);
	const u32 zero = rd_be32 (data + 8);
	if (zero || !n || n > 0x1000000)
		return ERR_NOTHING_TO_DO;

	const u64 table_size = (u64)(n + 1) * 16;
	if (table_size > size)
		return ERR_NOTHING_TO_DO;

	gpak_entry_t *entries = CALLOC (n, sizeof (*entries));
	if (!entries)
		return ERR_CANT_CREATE;

	for (uint i = 0; i < n; i++)
	{
		const u8 *h = data + (i + 1) * 16;
		const u32 off = rd_be32 (h);
		const u32 esz = rd_be32 (h + 4);
		if ((u64)off + esz > size)
			continue;
		entries[i].data = data + off;
		entries[i].size = esz;
	}

	for (uint i = 0; i < n; i++)
		if (!entries[i].data)
		{
			FREE (entries);
			return ERR_NOTHING_TO_DO;
		}

	pak->data = data;
	pak->size = size;
	pak->entries = entries;
	pak->n_entries = n;
	return ERR_OK;
}

// Writer for the same nameless .pak: a 16-byte big-endian header holding the
// entry count, then one 16-byte (offset, size) record per member starting at
// 0x10, then the member data. The reader names members by ordinal
// ("file_%04u.bin"), so the writer takes them back in that order.
enumError CreateGPAK (
	u8 **dest, uint *dest_size, const nintendo_sarc_entry_t *entries, uint n_entries)
{
	if (!dest || !dest_size || !entries || !n_entries)
		return ERR_INVALID_DATA;

	const u32 data_start = (n_entries + 1) * 16;
	u32 total = data_start;
	for (uint i = 0; i < n_entries; i++)
		total = (total + entries[i].size + 15) & ~15u;

	u8 *buf = CALLOC (total, 1);
	if (!buf)
		return ERR_OUT_OF_MEMORY;

	wr_be32 (buf, n_entries);

	u32 off = data_start;
	for (uint i = 0; i < n_entries; i++)
	{
		u8 *h = buf + (i + 1) * 16;
		wr_be32 (h, off);
		wr_be32 (h + 4, entries[i].size);
		if (entries[i].data && entries[i].size)
			memcpy (buf + off, entries[i].data, entries[i].size);
		off = (off + entries[i].size + 15) & ~15u;
	}

	*dest = buf;
	*dest_size = total;
	return ERR_OK;
}

// True when the directory has ordinal "file_%04u*" members (GPAK).
bool looks_like_gpak_dir (ccp source)
{
	DIR *dir = opendir (source);
	if (!dir)
		return false;
	bool found = false;
	struct dirent *de;
	while ((de = readdir (dir)))
	{
		uint idx;
		if (jarc_member_name_ok (de->d_name, &idx))
		{
			found = true;
			break;
		}
	}
	closedir (dir);
	return found;
}

// Repack a directory extracted by extract_gpak_file() back into a nameless
// .pak. Members are ordinal-named, exactly like ARCV and jARC.
enumError create_gpak_dir (ccp source, ccp dest)
{
	DIR *dir = opendir (source);
	if (!dir)
		return ERROR0 (ERR_NOT_EXISTS, "Can't open GPAK input directory: %s\n", source);

	arcv_member_t *list = 0;
	uint used = 0, size = 0;
	enumError err = ERR_OK;
	struct dirent *de;
	while (!err && (de = readdir (dir)))
	{
		uint idx;
		if (!jarc_member_name_ok (de->d_name, &idx))
			continue;

		char path[PATH_MAX];
		snprintf (path, sizeof (path), "%s/%s", source, de->d_name);
		struct stat st;
		if (stat (path, &st) || !S_ISREG (st.st_mode))
			continue;

		if (used == size)
		{
			const uint nsize = size ? 2 * size : 32;
			void *ptr = REALLOC (list, nsize * sizeof (*list));
			if (!ptr)
			{
				err = ERR_CANT_CREATE;
				break;
			}
			list = ptr;
			size = nsize;
		}
		u8 *data = 0;
		size_t fsize = 0;
		err = LoadFileAlloc (path, 0, 0, &data, &fsize, 0, 0, 0, false);
		if (err)
		{
			ERROR0 (err, "Can't load GPAK input: %s\n", path);
			break;
		}
		list[used].index = idx;
		list[used].data = data;
		list[used].size = fsize;
		used++;
	}
	closedir (dir);

	if (!err && !used)
		err = ERR_NOTHING_TO_DO;
	if (!err)
		qsort (list, used, sizeof (*list), cmp_arcv_member);
	for (uint i = 0; !err && i < used; i++)
		if (list[i].index != i)
			err = ERROR0 (ERR_INVALID_DATA,
				"GPAK input directory has a non-contiguous member index: %s/file_%04u*\n", source,
				list[i].index);

	if (!err && !testmode)
	{
		nintendo_sarc_entry_t *ent = CALLOC (used, sizeof (*ent));
		if (!ent)
			err = ERR_CANT_CREATE;
		else
		{
			for (uint i = 0; i < used; i++)
			{
				ent[i].data = list[i].data;
				ent[i].size = (uint)list[i].size;
			}
			u8 *out = 0;
			uint out_size = 0;
			err = CreateGPAK (&out, &out_size, ent, used);
			if (!err)
			{
				File_t F;
				err = CreateFileOpt (&F, true, dest, false, dest);
				if (F.f && fwrite (out, 1, out_size, F.f) != out_size)
					err = FILEERROR1 (
						&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", out_size, dest);
				ResetFile (&F, opt_preserve);
			}
			FREE (out);
			FREE (ent);
		}
	}

	for (uint i = 0; i < used; i++)
		FREE (list[i].data);
	FREE (list);
	return err;
}
