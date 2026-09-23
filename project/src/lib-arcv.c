// Namco / Tose Wii archive format (ARCV) -- directory-tree CREATE glue.

#include "lib-std.h"
#include "lib-archive-util.h"
#include "lib-arcv.h"
#include "lib-nintendo.h"
#include <zlib.h>

// Repack a directory extracted by extract_arcv_file() back into a Namco/Tose
// ARCV archive. Members carry no on-disk name, only an ordinal encoded into
// the extractor's "file_%04u<ext>" filename, so recover that ordinal from the
// filename instead of relying on directory scan order (which is not
// guaranteed to match numerically for 4+ digit indices on all filesystems).

// A bare "file_%04u%n" scan also matches decoded side-products of a member
// that extract_tree_complete() expanded further (e.g. "file_0002.rseq.mid",
// "file_0002.rseq.txt", or a "file_0000.brres.d" preview directory), which
// would otherwise look like duplicate or extra members. Require the name to
// end exactly at one of arcv_member_ext()'s known single extensions.
bool arcv_member_name_ok (ccp nm, uint *idx)
{
	int consumed = 0;
	if (sscanf (nm, "file_%u%n", idx, &consumed) != 1 || !consumed)
		return false;
	ccp rest = nm + consumed;
	static const char *known_ext[]
		= { ".bin", ".brres", ".rseq", ".rstm", ".rwav", ".rarc", ".pac", 0 };
	for (uint i = 0; known_ext[i]; i++)
		if (!strcmp (rest, known_ext[i]))
			return true;
	return false;
}


// ARCV shares the .arc extension with several other archive formats (RARC,
// Brawl PAC, etc.), so disambiguate at CREATE time the same way looks_like_
// sfzdat_dir()/looks_like_rflres_dir() do for the equally overloaded .dat
// extension: treat a directory as ARCV input once it holds at least one of
// extract_arcv_file()'s unnamed "file_%04u<ext>" members. Anything else in
// the directory is ignored rather than rejected -- extract_tree_complete()
// may have expanded a member further into "file_0000.brres.d/" or emitted
// derived side-products like "file_0002.rseq.mid" alongside the original.
bool looks_like_arcv_dir (ccp source)
{
	DIR *dir = opendir (source);
	if (!dir)
		return false;
	uint n_members = 0;
	for (const struct dirent *de; (de = readdir (dir));)
	{
		uint idx;
		if (arcv_member_name_ok (de->d_name, &idx))
			n_members++;
	}
	closedir (dir);
	return n_members > 0;
}


int cmp_arcv_member (const void *a, const void *b)
{
	const arcv_member_t *ma = a, *mb = b;
	return ma->index < mb->index ? -1 : ma->index > mb->index ? 1 : 0;
}


enumError create_arcv_dir (ccp source, ccp dest)
{
	DIR *dir = opendir (source);
	if (!dir)
		return ERROR0 (ERR_NOT_EXISTS, "Can't open ARCV input directory: %s\n", source);

	arcv_member_t *list = 0;
	uint used = 0, size = 0;
	enumError err = ERR_OK;
	struct dirent *de;
	while (!err && (de = readdir (dir)))
	{
		uint idx;
		if (!arcv_member_name_ok (de->d_name, &idx))
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
			ERROR0 (err, "Can't load ARCV input: %s\n", path);
			break;
		}
		if (fsize > UINT_MAX)
		{
			FREE (data);
			err = ERR_FILE_TOO_BIG;
			break;
		}
		list[used].index = idx;
		list[used].data = data;
		list[used].size = fsize;
		used++;
		if (used > 1000000)
		{
			err = ERR_FILE_TOO_BIG;
			break;
		}
	}
	closedir (dir);

	if (!err && !used)
		err = ERR_NOTHING_TO_DO;
	if (!err)
		qsort (list, used, sizeof (*list), cmp_arcv_member);
	// The extractor names members by ordinal position (0, 1, 2, ...), so a
	// gap or duplicate here means the directory wasn't produced by it.
	for (uint i = 0; !err && i < used; i++)
		if (list[i].index != i)
			err = ERROR0 (ERR_INVALID_DATA,
				"ARCV input directory has a non-contiguous member index: %s/file_%04u*\n",
				source, list[i].index);

	if (!err && !testmode)
	{
		const uint table_end = 12 + used * 12;
		u64 total_size = table_end;
		for (uint i = 0; i < used; i++)
			total_size += list[i].size;
		if (total_size > UINT_MAX)
			err = ERR_FILE_TOO_BIG;
		else
		{
			u8 *out = CALLOC (1, total_size);
			if (!out)
				err = ERR_CANT_CREATE;
			else
			{
				memcpy (out, "ARCV", 4);
				wr_le32 (out + 4, used);
				wr_le32 (out + 8, (u32)total_size);

				u64 off = table_end;
				for (uint i = 0; i < used; i++)
				{
					u8 *entry = out + 12 + i * 12;
					wr_le32 (entry, (u32)off);
					wr_le32 (entry + 4, (u32)list[i].size);
					wr_le32 (entry + 8, (u32)crc32 (0, list[i].data, list[i].size));
					memcpy (out + off, list[i].data, list[i].size);
					off += list[i].size;
				}

				File_t F;
				err = CreateFileOpt (&F, true, dest, false, dest);
				if (F.f && fwrite (out, 1, total_size, F.f) != total_size)
					err = FILEERROR1 (&F, ERR_WRITE_FAILED,
						"Writing %llu bytes failed: %s\n", (unsigned long long)total_size, dest);
				ResetFile (&F, opt_preserve);
				FREE (out);
			}
		}
	}

	for (uint i = 0; i < used; i++)
		FREE (list[i].data);
	FREE (list);
	return err;
}

