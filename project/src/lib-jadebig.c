// SPDX-License-Identifier: GPL-2.0+
//-----------------------------------------------------------------------------
// Ubisoft Jade BigFile index scanner; see lib-jadebig.h for the layout.
//-----------------------------------------------------------------------------
#include "lib-std.h"
#include "lib-jadebig.h"
#include "lib-nintendo.h"
#include <string.h>

#define JB_MAX_FAT_SLOTS 0x100000
#define JB_MAX_FATS 4096
#define JB_NAME_LEN 0x40
#define JB_DIR_INFO_SIZE (0x14 + JB_NAME_LEN)
#define JB_FAT_HEADER 0x18
#define JB_MAX_DEPTH 64

static const u8 jb_xor_key[4] = {0xb3, 0x98, 0xcc, 0x66};

typedef struct jb_ctx_t
{
	bool be;
	bool xor_;
} jb_ctx_t;

static u32 jb_rd32 (const jb_ctx_t *c, const u8 *p)
{
	return c->be ? (u32)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]
		     : p[0] | p[1] << 8 | p[2] << 16 | (u32)p[3] << 24;
}

// Read [off, off+len) and undo the BUG XOR for it when asked.
static bool jb_read (FILE *f, u64 file_size, const jb_ctx_t *c, bool unxor, u64 off, u8 *buf, size_t len)
{
	if (off > file_size || len > file_size - off || fseeko (f, (off_t)off, SEEK_SET)
		|| fread (buf, 1, len, f) != len)
		return false;
	if (unxor && c->xor_)
		for (size_t i = 0; i < len; i++)
			buf[i] ^= jb_xor_key[(off + i) & 3];
	return true;
}

static bool jb_version_ok (u32 v)
{
	return v >= 30 && v <= 80;
}

bool IsJadeBigHeader (const u8 *data, size_t size)
{
	if (!data || size < 0x30 || (memcmp (data, "BIG\0", 4) && memcmp (data, "BUG\0", 4)))
		return false;
	const bool xor_ = data[1] == 'U';
	u8 h[0x24];
	memcpy (h, data + 4, sizeof (h));
	if (xor_)
		for (uint i = 0; i < sizeof (h); i++)
			h[i] ^= jb_xor_key[(4 + i) & 3];
	for (int be = 0; be < 2; be++)
	{
		const jb_ctx_t c = {be, xor_};
		const u32 size_of_fat = jb_rd32 (&c, h + 28), num_fat = jb_rd32 (&c, h + 32);
		if (jb_version_ok (jb_rd32 (&c, h)) && size_of_fat && size_of_fat <= JB_MAX_FAT_SLOTS
			&& num_fat && num_fat <= JB_MAX_FATS)
			return true;
	}
	return false;
}

typedef struct jb_dir_t
{
	int parent;
	char name[JB_NAME_LEN + 1];
} jb_dir_t;

static void jb_copy_name (char *dst, const u8 *src)
{
	memcpy (dst, src, JB_NAME_LEN);
	dst[JB_NAME_LEN] = 0;
	for (char *p = dst; *p; p++)
		if (*p == '\\' || *p == ':' || (u8)*p < 0x20)
			*p = '_';
}

static bool jb_dir_path (const jb_dir_t *dirs, uint n_dirs, int idx, char *out, size_t out_size)
{
	int chain[JB_MAX_DEPTH];
	uint depth = 0;
	while (idx >= 0)
	{
		if ((uint)idx >= n_dirs || depth >= JB_MAX_DEPTH)
			return false;
		chain[depth++] = idx;
		idx = dirs[idx].parent;
	}
	out[0] = 0;
	// The topmost directory is the archive root ("ROOT"); leave it out.
	for (int i = (int)depth - 2; i >= 0; i--)
	{
		const char *nm = dirs[chain[i]].name;
		if (!*nm || !strcmp (nm, ".") || !strcmp (nm, ".."))
			return false;
		if (strlen (out) + strlen (nm) + 2 >= out_size)
			return false;
		strcat (out, nm);
		strcat (out, "/");
	}
	return true;
}

enumError ScanJadeBig (FILE *f, u64 file_size, stream_entry_t **entries, uint *n_entries)
{
	if (!f || !entries || !n_entries || file_size < 0x30)
		return EINVAL;
	*entries = 0;
	*n_entries = 0;

	u8 magic[4];
	if (!jb_read (f, file_size, &(jb_ctx_t){0}, false, 0, magic, 4)
		|| (memcmp (magic, "BIG\0", 4) && memcmp (magic, "BUG\0", 4)))
		return EINVAL;

	u8 h[0x24 + 4];
	jb_ctx_t c = {false, magic[1] == 'U'};
	if (!jb_read (f, file_size, &c, true, 4, h, sizeof (h)))
		return EINVAL;
	// The XOR only covers the nine header words; the universe key (+0x28) is plain.
	if (c.xor_)
		for (uint i = 0x24; i < sizeof (h); i++)
			h[i] ^= jb_xor_key[(4 + i) & 3];
	if (!jb_version_ok (jb_rd32 (&c, h)))
	{
		c.be = true;
		if (!jb_version_ok (jb_rd32 (&c, h)))
			return EINVAL;
	}
	const u32 version = jb_rd32 (&c, h);
	const u32 max_file = jb_rd32 (&c, h + 4);
	const u32 size_of_fat = jb_rd32 (&c, h + 28);
	const u32 num_fat = jb_rd32 (&c, h + 32);
	if (!size_of_fat || size_of_fat > JB_MAX_FAT_SLOTS || !num_fat || num_fat > JB_MAX_FATS
		|| max_file > (u64)size_of_fat * num_fat)
		return EINVAL;

	u32 info_size;
	if (version == 34 || version == 37 || version == 38)
		info_size = 0x54;
	else if (version >= 42)
		info_size = 0x3C + JB_NAME_LEN;
	else
		info_size = 0x58;
	const u64 fat_stride = JB_FAT_HEADER + (u64)size_of_fat * (8 + info_size + JB_DIR_INFO_SIZE);
	const u64 fat_base = 0x2c + (version >= 43 ? 44 : 0);
	if (fat_base + fat_stride * num_fat > file_size)
		return EINVAL;

	// Pass 1: all directories, so file paths can be resolved afterwards.
	jb_dir_t *dirs = CALLOC ((size_t)size_of_fat * num_fat, sizeof (*dirs));
	if (!dirs)
		return ERR_CANT_CREATE;
	u8 *tab = MALLOC ((size_t)size_of_fat * (8 + info_size + JB_DIR_INFO_SIZE));
	if (!tab)
	{
		FREE (dirs);
		return ERR_CANT_CREATE;
	}
	u32 n_dirs = 0;
	enumError res = ERR_OK;
	for (u32 k = 0; k < num_fat && !res; k++)
	{
		const u64 base = fat_base + fat_stride * k;
		u8 fh[JB_FAT_HEADER];
		if (!jb_read (f, file_size, &c, true, base, fh, sizeof (fh)))
		{
			res = EINVAL;
			break;
		}
		const u32 fat_dirs = jb_rd32 (&c, fh + 4);
		if (fat_dirs > size_of_fat)
		{
			res = EINVAL;
			break;
		}
		const u64 doff = base + JB_FAT_HEADER + (u64)size_of_fat * (8 + info_size);
		if (fat_dirs && !jb_read (f, file_size, &c, true, doff, tab, (size_t)fat_dirs * JB_DIR_INFO_SIZE))
		{
			res = EINVAL;
			break;
		}
		for (u32 i = 0; i < fat_dirs; i++)
		{
			const u8 *d = tab + (size_t)i * JB_DIR_INFO_SIZE;
			jb_dir_t *o = dirs + (size_t)k * size_of_fat + i;
			o->parent = (int)jb_rd32 (&c, d + 16);
			jb_copy_name (o->name, d + 20);
			if ((size_t)k * size_of_fat + i + 1 > n_dirs)
				n_dirs = (uint)((size_t)k * size_of_fat + i + 1);
		}
	}
	if (res)
	{
		FREE (tab);
		FREE (dirs);
		return res;
	}

	stream_entry_t *out = CALLOC (max_file ? max_file : 1, sizeof (*out));
	u8 *refs = MALLOC ((size_t)size_of_fat * 8);
	u8 *infos = MALLOC ((size_t)size_of_fat * info_size);
	if (!out || !refs || !infos)
		res = ERR_CANT_CREATE;
	uint n = 0;
	for (u32 k = 0; k < num_fat && !res; k++)
	{
		const u64 base = fat_base + fat_stride * k;
		u8 fh[JB_FAT_HEADER];
		if (!jb_read (f, file_size, &c, true, base, fh, sizeof (fh)))
		{
			res = EINVAL;
			break;
		}
		const u32 fat_files = jb_rd32 (&c, fh);
		if (fat_files > size_of_fat || n + fat_files > max_file)
		{
			res = EINVAL;
			break;
		}
		if (!fat_files)
			continue;
		const u64 roff = base + JB_FAT_HEADER;
		const u64 ioff = roff + (u64)size_of_fat * 8;
		if (!jb_read (f, file_size, &c, true, roff, refs, (size_t)fat_files * 8))
		{
			res = EINVAL;
			break;
		}
		// Slots are info_size apart but only the first fat_files are filled.
		if (!jb_read (f, file_size, &c, true, ioff, infos, (size_t)fat_files * info_size))
		{
			res = EINVAL;
			break;
		}
		for (u32 i = 0; i < fat_files; i++)
		{
			const u8 *r = refs + (size_t)i * 8;
			const u8 *inf = infos + (size_t)i * info_size;
			const u32 off = jb_rd32 (&c, r), key = jb_rd32 (&c, r + 4);
			if (!off || off == 0xffffffffu)
				continue;
			u8 sw[4];
			if (!jb_read (f, file_size, &c, false, off, sw, 4))
				continue; // dangling reference
			const u32 size = jb_rd32 (&c, sw) & 0x7fffffffu;
			if ((u64)off + 4 + size > file_size)
				continue;

			// A BUG file zeroes names that were stripped; the raw word tells.
			char leaf[JB_NAME_LEN + 1] = "";
			const int parent = (int)jb_rd32 (&c, inf + 12);
			if (jb_rd32 (&c, inf) || !c.xor_)
				jb_copy_name (leaf, inf + 20);

			char dir[512] = "";
			char path[640];
			const bool dir_ok = jb_dir_path (dirs, n_dirs, parent, dir, sizeof (dir));
			if (!*leaf)
				snprintf (leaf, sizeof (leaf), "%08x.bin", key);
			snprintf (path, sizeof (path), "%s%s", dir_ok ? dir : "", leaf);
			if (!OwnedNameOk (path))
				snprintf (path, sizeof (path), "%08x_%u.bin", key, n);

			// Same name twice (several versions of a file): keep both.
			for (uint j = 0; j < n; j++)
				if (!strcmp (out[j].name, path))
				{
					char alt[700];
					snprintf (alt, sizeof (alt), "%s.%08x", path, key);
					snprintf (path, sizeof (path), "%s", alt);
					break;
				}
			if (!StreamEntryAdd (out, n, path, (u64)off + 4, size))
			{
				res = ERR_CANT_CREATE;
				break;
			}
			n++;
		}
	}
	FREE (refs);
	FREE (infos);
	FREE (tab);
	FREE (dirs);
	if (res || !n)
	{
		FreeStreamEntries (out, n);
		return res ? res : EINVAL;
	}
	*entries = out;
	*n_entries = n;
	return ERR_OK;
}
