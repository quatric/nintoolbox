// SPDX-License-Identifier: GPL-2.0+
// See lib-ideimg.h for the layout and what is/isn't recovered.
#include "lib-ideimg.h"
#include "lib-archive-util.h"
#include <string.h>
#include <sys/stat.h>

#define IDEIMG_REC_SIZE 32
#define IDEIMG_SECTOR_SIZE 2048
#define IDEIMG_MAX_ENTRIES 100000

// This decoder only fires on the ".dir" half of the pair -- it needs the
// sibling ".img" to actually have anything to extract.
static bool find_sibling_img (ccp dir_path, char *out, size_t out_size)
{
	const size_t len = strlen (dir_path);
	if (len < 4 || strcasecmp (dir_path + len - 4, ".dir"))
		return false;

	struct stat st;
	snprintf (out, out_size, "%.*s.img", (int)(len - 4), dir_path);
	if (!stat (out, &st) && S_ISREG (st.st_mode))
		return true;
	snprintf (out, out_size, "%.*s.IMG", (int)(len - 4), dir_path);
	return !stat (out, &st) && S_ISREG (st.st_mode);
}

enumError ExtractIdeImgArchive (ccp arg, ccp basedir, uint depth)
{
	char img_path[PATH_MAX];
	if (!find_sibling_img (arg, img_path, sizeof (img_path)))
		return ERR_NOTHING_TO_DO;

	u8 *dir_data = 0;
	size_t dir_size = 0;
	if (LoadFileAlloc (arg, 0, 0, &dir_data, &dir_size, 0, 0, 0, false))
		return ERR_NOTHING_TO_DO;

	const uint n = (uint)(dir_size / IDEIMG_REC_SIZE);
	if (!n || dir_size % IDEIMG_REC_SIZE || n > IDEIMG_MAX_ENTRIES)
	{
		FREE (dir_data);
		return ERR_NOTHING_TO_DO;
	}

	// Gate on the one invariant this layout was recovered from: entries
	// are contiguous sector ranges in ascending order, entry 0 starting at
	// sector 0. A garbled or unrelated ".dir" file is extremely unlikely
	// to satisfy this by chance for every one of its entries.
	u32 expect_sector = 0;
	for (uint i = 0; i < n; i++)
	{
		const u8 *rec = dir_data + i * IDEIMG_REC_SIZE;
		const u32 start_sector = rd_be32 (rec);
		const u32 num_sectors = rd_be32 (rec + 4);
		if (start_sector != expect_sector || !num_sectors)
		{
			FREE (dir_data);
			return ERR_NOTHING_TO_DO;
		}
		if (!memchr (rec + 8, 0, IDEIMG_REC_SIZE - 8))
		{
			FREE (dir_data);
			return ERR_NOTHING_TO_DO;
		}
		expect_sector += num_sectors;
	}

	u8 *img_data = 0;
	size_t img_size = 0;
	if (LoadFileAlloc (img_path, 0, 0, &img_data, &img_size, 0, 0, 0, false))
	{
		FREE (dir_data);
		return ERR_NOTHING_TO_DO;
	}
	if ((u64)expect_sector * IDEIMG_SECTOR_SIZE != img_size)
	{
		// The table's total doesn't reconcile against the real sibling
		// file's size -- almost certainly not actually a matching pair.
		FREE (dir_data);
		FREE (img_data);
		return ERR_NOTHING_TO_DO;
	}

	char dest[PATH_MAX];
	get_dest_dir (dest, sizeof (dest), arg, basedir);
	CreatePath (dest, true);

	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT IDE.dir/img:%s (%u member(s)) -> %s/\n",
			verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, n, dest);

	enumError err = ERR_OK;
	if (!testmode)
	{
		for (uint i = 0; i < n; i++)
		{
			const u8 *rec = dir_data + i * IDEIMG_REC_SIZE;
			const u32 start_sector = rd_be32 (rec);
			const u32 num_sectors = rd_be32 (rec + 4);
			const u64 off = (u64)start_sector * IDEIMG_SECTOR_SIZE;
			const u64 len = (u64)num_sectors * IDEIMG_SECTOR_SIZE;

			char name[32];
			StringCopyS (name, sizeof (name), (ccp)(rec + 8));
			for (char *p = name; *p; p++)
				if (*p == '/' || *p == '\\' || (u8)*p < 0x20)
					*p = '_';
			if (!*name)
				snprintf (name, sizeof (name), "member%04u", i);

			char out_path[PATH_MAX];
			snprintf (out_path, sizeof (out_path), "%s/%s", dest, name);
			if (SaveFile (out_path, 0, 0, img_data + off, (uint)len, 0))
				err = ERR_CANT_CREATE;
		}
	}

	FREE (dir_data);
	FREE (img_data);
	(void)depth;
	return err;
}
