// SPDX-License-Identifier: GPL-2.0+
// Small shared helpers for the per-format archive extractors/creators that
// were split out of lib-nintendo-archives.c. Header-only (static inline) so
// no extra translation unit is needed.
#ifndef LIB_ARCHIVE_UTIL_H
#define LIB_ARCHIVE_UTIL_H 1

#include "lib-std.h"
#include "lib-nintendo.h"
#include "lib-zdat.h"
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>

static inline bool is_ext_match (ccp path, ccp ext)
{
	if (!path || !ext)
		return false;
	const size_t plen = strlen (path);
	const size_t elen = strlen (ext);
	if (plen < elen)
		return false;
	return !strcasecmp (path + plen - elen, ext);
}

static inline void get_dest_dir (char *dest, size_t dest_size, ccp arg, ccp basedir)
{
	if (opt_dest && *opt_dest)
		snprintf (dest, dest_size, "%s", opt_dest);
	else if (basedir && *basedir)
		snprintf (dest, dest_size, "%s", basedir);
	else
	{
		snprintf (dest, dest_size, "%s.d", arg);
	}
}

static inline int compare_archive_entries (const void *a, const void *b)
{
	const nintendo_sarc_entry_t *ea = (const nintendo_sarc_entry_t *)a;
	const nintendo_sarc_entry_t *eb = (const nintendo_sarc_entry_t *)b;
	ccp na = ea->name ? ea->name : "";
	ccp nb = eb->name ? eb->name : "";
	return strcmp (na, nb);
}

static inline u32 align_up (u32 value, u32 align)
{
	return align > 1 ? (value + align - 1) & ~(align - 1) : value;
}

// Strip any directory part: these formats store leaf names only.
static inline ccp leaf_name (ccp name)
{
	if (!name)
		return "";
	ccp slash = strrchr (name, '/');
	return slash ? slash + 1 : name;
}

//-----------------------------------------------------------------------------
// Generic "collect a directory tree into a flat named-entry list" walker,
// shared by every archive format whose CREATE side just packs a flat member
// list (SARC and most everything built on the same nintendo_sarc_entry_t
// shape).

typedef struct sarc_build_list_t
{
	nintendo_sarc_entry_t *entry;
	uint used, size;
} sarc_build_list_t;

static inline void reset_sarc_build_list (sarc_build_list_t *list)
{
	for (uint i = 0; i < list->used; i++)
	{
		FREE ((void *)list->entry[i].name);
		FREE ((void *)list->entry[i].data);
	}
	FREE (list->entry);
	memset (list, 0, sizeof (*list));
}

static inline int cmp_strptr_sarc (const void *a, const void *b)
{
	return strcmp (*(char *const *)a, *(char *const *)b);
}

static inline enumError collect_sarc_dir (sarc_build_list_t *list, ccp root, ccp rel)
{
	char path[PATH_MAX];
	snprintf (path, sizeof (path), "%s%s%s", root, *rel ? "/" : "", rel);
	DIR *dir = opendir (path);
	if (!dir)
		return ERROR0 (ERR_NOT_EXISTS, "Can't open SARC input directory: %s\n", path);

	// readdir() order is filesystem-dependent and not reproducible (see the
	// analogous comment in lib-szs-create.c's scan_data()): sort the names
	// first so CREATE -> EXTRACT -> CREATE is a pure function of the tree's
	// content, never of directory/inode history.
	uint n_names = 0, cap_names = 0;
	char **names = 0;
	struct dirent *de;
	while ((de = readdir (dir)))
	{
		if (n_names == cap_names)
		{
			cap_names = cap_names ? cap_names * 2 : 16;
			names = REALLOC (names, cap_names * sizeof (*names));
		}
		names[n_names++] = STRDUP (de->d_name);
	}
	closedir (dir);
	dir = 0;
	if (n_names)
		qsort (names, n_names, sizeof (*names), cmp_strptr_sarc);

	enumError err = ERR_OK;
	for (uint name_idx = 0; !err && name_idx < n_names; name_idx++)
	{
		ccp name = names[name_idx];
		if (!strcmp (name, ".") || !strcmp (name, ".."))
			continue;
		// wszst-setup.txt (and its cache sibling) is metadata written by
		// EXTRACT for the generic SZS tree format, not archive payload --
		// including it here as a member is why re-CREATE-ing an
		// EXTRACT-ed rarc/nccarc/etc. tree used to balloon or corrupt the
		// second-generation archive.
		if (!strcmp (name, SZS_SETUP_FILE) || !strcmp (name, ".wszst-cache.txt")
			|| !strcmp (name, SZS_MTIME_BASELINE_FILE) || !strcmp (name, ZDAT_CACHE_FILE))
			continue;
		const uint dlen = strlen (name);
		if (dlen >= 2 && name[dlen - 1] == 'd' && name[dlen - 2] == '.')
			continue;
		char child_rel[PATH_MAX], child_path[PATH_MAX];
		snprintf (child_rel, sizeof (child_rel), "%s%s%s", rel, *rel ? "/" : "", name);
		snprintf (child_path, sizeof (child_path), "%s/%s", root, child_rel);
		struct stat st;
		if (lstat (child_path, &st))
		{
			err = ERROR0 (ERR_NOT_EXISTS, "Can't stat SARC input: %s\n", child_path);
			break;
		}
		if (S_ISDIR (st.st_mode))
			err = collect_sarc_dir (list, root, child_rel);
		else if (S_ISREG (st.st_mode))
		{
			if (list->used == list->size)
			{
				const uint nsize = list->size ? 2 * list->size : 32;
				void *ptr = REALLOC (list->entry, nsize * sizeof (*list->entry));
				if (!ptr)
				{
					err = ERR_CANT_CREATE;
					break;
				}
				list->entry = ptr;
				list->size = nsize;
			}
			u8 *data = 0;
			size_t size = 0;
			err = LoadFileAlloc (child_path, 0, 0, &data, &size, 0, 0, 0, false);
			if (err || size > UINT_MAX)
			{
				FREE (data);
				if (!err)
					err = ERR_FILE_TOO_BIG;
				if (err)
					ERROR0 (err, "Can't load SARC input: %s\n", child_path);
				break;
			}
			nintendo_sarc_entry_t *entry = list->entry + list->used++;
			entry->name = STRDUP (child_rel);
			entry->data = data;
			entry->size = size;
		}
	}
	for (uint i = 0; i < n_names; i++)
		FREE (names[i]);
	FREE (names);
	return err;
}

#endif // LIB_ARCHIVE_UTIL_H
