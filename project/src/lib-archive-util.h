// SPDX-License-Identifier: GPL-2.0+
// Small shared helpers for the per-format archive extractors/creators that
// were split out of lib-nintendo-archives.c. Header-only (static inline) so
// no extra translation unit is needed.
#ifndef LIB_ARCHIVE_UTIL_H
#define LIB_ARCHIVE_UTIL_H 1

#include "lib-std.h"
#include "lib-nintendo.h"
#include <string.h>

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

#endif // LIB_ARCHIVE_UTIL_H
