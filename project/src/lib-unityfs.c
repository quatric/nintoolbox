// SPDX-License-Identifier: GPL-2.0+
//-----------------------------------------------------------------------------
// Unity "UnityFS" bundle scanner; see lib-unityfs.h for the layout.
//-----------------------------------------------------------------------------
#include "lib-std.h"
#include "lib-unityfs.h"
#include "lib-lzma.h"
#include "lz4.h"
#include <string.h>

#define UFS_MAX_BLOCKS 0x100000
#define UFS_MAX_NODES 0x100000
#define UFS_MAX_TOTAL (3ull << 30)

static u32 ufs_rd32 (const u8 *p)
{
	return (u32)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3];
}

static u64 ufs_rd64 (const u8 *p)
{
	return (u64)ufs_rd32 (p) << 32 | ufs_rd32 (p + 4);
}

bool IsUnityFS (const u8 *data, size_t size)
{
	return data && size >= 0x30 && !memcmp (data, "UnityFS\0", 8);
}

// Unity LZMA streams: 5 property bytes followed by raw data, no size field.
static bool ufs_unpack (u8 *dest, uint dest_size, const u8 *src, uint src_size, uint mode)
{
	switch (mode)
	{
		case 0:
			if (src_size != dest_size)
				return false;
			memcpy (dest, src, dest_size);
			return true;

		case 1:
		{
			if (src_size <= 5)
				return false;
			u8 *tmp = MALLOC (src_size + 8);
			if (!tmp)
				return false;
			memcpy (tmp, src, 5);
			for (int i = 0; i < 8; i++)
				tmp[5 + i] = (u8)(((u64)dest_size >> (8 * i)) & 0xff);
			memcpy (tmp + 13, src + 5, src_size - 5);
			uint written = 0;
			enumError err = DecodeLZMApart (dest, dest_size, &written, tmp, src_size + 8);
			FREE (tmp);
			return err != ERR_NOTHING_TO_DO && written == dest_size;
		}

		case 2:
		case 3:
			return LZ4_decompress_safe ((const char *)src, (char *)dest, (int)src_size,
					       (int)dest_size)
			       == (int)dest_size;
	}
	return false;
}

enumError ScanUnityFS (nintendo_sarc_entry_t **entries, uint *n_entries, const u8 *data, size_t size)
{
	if (!entries || !n_entries || !IsUnityFS (data, size))
		return EINVAL;
	*entries = 0;
	*n_entries = 0;

	const u8 *end = data + size;
	const u8 *p = data + 8;
	const u32 version = ufs_rd32 (p);
	p += 4;
	for (int i = 0; i < 2; i++) // player version, engine version
	{
		const u8 *z = memchr (p, 0, end - p);
		if (!z)
			return EINVAL;
		p = z + 1;
	}
	if (end - p < 20)
		return EINVAL;
	p += 8; // total size, not trusted
	const u32 bi_comp = ufs_rd32 (p);
	const u32 bi_uncomp = ufs_rd32 (p + 4);
	const u32 flags = ufs_rd32 (p + 8);
	p += 12;

	if (flags & 0x400 || (flags & 0x3f) > 3 || bi_uncomp > (64u << 20))
		return EINVAL;

	size_t pos = p - data;
	if (version >= 7 || (flags & 0x200))
		pos = (pos + 15) & ~(size_t)15;

	size_t bi_pos = pos;
	if (flags & 0x80)
	{
		if (bi_comp > size)
			return EINVAL;
		bi_pos = size - bi_comp;
	}
	else
		pos += bi_comp;
	if (bi_pos > size || bi_comp > size - bi_pos)
		return EINVAL;

	u8 *bi = MALLOC (bi_uncomp + 1);
	if (!bi)
		return ERR_CANT_CREATE;
	if (!ufs_unpack (bi, bi_uncomp, data + bi_pos, bi_comp, flags & 0x3f))
	{
		FREE (bi);
		return EINVAL;
	}

	enumError result = EINVAL;
	u8 *blob = 0;
	nintendo_sarc_entry_t *out = 0;
	uint n = 0;

	if (bi_uncomp < 24)
		goto done;
	const u32 n_blocks = ufs_rd32 (bi + 16);
	if (!n_blocks || n_blocks > UFS_MAX_BLOCKS || 20 + (u64)n_blocks * 10 + 4 > bi_uncomp)
		goto done;

	u64 total = 0;
	for (u32 i = 0; i < n_blocks; i++)
		total += ufs_rd32 (bi + 20 + i * 10);
	if (!total || total > UFS_MAX_TOTAL)
		goto done;

	blob = MALLOC (total);
	if (!blob)
	{
		result = ERR_CANT_CREATE;
		goto done;
	}

	u64 dpos = 0;
	for (u32 i = 0; i < n_blocks; i++)
	{
		const u8 *b = bi + 20 + i * 10;
		const u32 usz = ufs_rd32 (b), csz = ufs_rd32 (b + 4);
		const uint bflags = (b[8] << 8) | b[9];
		if (bflags & 0x100 || (bflags & 0x3f) > 3 || pos > size || csz > size - pos)
			goto done;
		if (!ufs_unpack (blob + dpos, usz, data + pos, csz, bflags & 0x3f))
			goto done;
		pos += csz;
		dpos += usz;
	}

	const u8 *np = bi + 20 + n_blocks * 10;
	const u8 *bend = bi + bi_uncomp;
	const u32 n_nodes = ufs_rd32 (np);
	np += 4;
	if (!n_nodes || n_nodes > UFS_MAX_NODES)
		goto done;

	out = CALLOC (n_nodes, sizeof (*out));
	if (!out)
	{
		result = ERR_CANT_CREATE;
		goto done;
	}
	for (u32 i = 0; i < n_nodes; i++)
	{
		if (bend - np < 21)
			break;
		const u64 off = ufs_rd64 (np), sz = ufs_rd64 (np + 8);
		np += 20;
		const u8 *z = memchr (np, 0, bend - np);
		if (!z)
			break;
		ccp path = (ccp)np;
		np = z + 1;
		if (off > total || sz > total - off || sz > 0xffffffffu)
			continue;

		// Bundle paths are like "CAB-<hash>" or "archive:/CAB-<hash>/x.resS";
		// keep the last component, falling back to an index on odd names.
		ccp base = strrchr (path, '/');
		base = base ? base + 1 : path;
		char name[512];
		if (!*base || strlen (base) >= sizeof (name) || !OwnedNameOk (base))
			snprintf (name, sizeof (name), "%05u.bin", i);
		else
			strcpy (name, base);
		if (!OwnedEntryAdd (out, n, name, blob + off, (uint)sz))
		{
			ResetOwnedEntries (out, n);
			out = 0;
			result = ERR_CANT_CREATE;
			goto done;
		}
		n++;
	}
	if (n)
	{
		*entries = out;
		*n_entries = n;
		out = 0;
		result = ERR_OK;
	}

done:
	if (out)
	{
		ResetOwnedEntries (out, n);
	}
	FREE (blob);
	FREE (bi);
	return result;
}
