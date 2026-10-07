// SPDX-License-Identifier: GPL-2.0+
//-----------------------------------------------------------------------------
// Sega ARCB archive / AVLZ decoder; see lib-arcb.h for the layout.
//-----------------------------------------------------------------------------
#include "lib-std.h"
#include "lib-arcb.h"
#include <string.h>

#define ARCB_MAX_UNPACKED (512u << 20)
#define ARCB_MAX_NODES 0x10000

static u32 ab_rd32 (const u8 *p)
{
	return (u32)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3];
}

enumError DecodeAvlz (const u8 *src, size_t size, u8 **out, uint *out_size)
{
	if (!src || !out || !out_size || size < 12 || memcmp (src, "AVLZ", 4))
		return EINVAL;
	const u32 usz = ab_rd32 (src + 4), csz = ab_rd32 (src + 8);
	if (!usz || usz > ARCB_MAX_UNPACKED || csz < 12 || csz > size)
		return EINVAL;
	u8 *dst = CALLOC (usz, 1);
	if (!dst)
		return ERR_CANT_CREATE;

	static const u8 zero_ring[4096];
	u8 ring[4096];
	memcpy (ring, zero_ring, sizeof (ring));
	uint r = 0xfee, ip = 12, op = 0;
	while (op < usz && ip < csz)
	{
		const u8 flags = src[ip++];
		for (uint b = 0; b < 8 && op < usz; b++)
		{
			if (flags >> b & 1)
			{
				if (ip >= csz)
					goto bad;
				const u8 c = src[ip++];
				dst[op++] = ring[r] = c;
				r = (r + 1) & 4095;
			}
			else
			{
				if (ip + 2 > csz)
					goto bad;
				const uint pos = src[ip] | (src[ip + 1] & 0xf0) << 4,
					   len = (src[ip + 1] & 0x0f) + 3;
				ip += 2;
				for (uint k = 0; k < len && op < usz; k++)
				{
					const u8 c = ring[(pos + k) & 4095];
					dst[op++] = ring[r] = c;
					r = (r + 1) & 4095;
				}
			}
		}
	}
	if (op != usz)
		goto bad;
	*out = dst;
	*out_size = usz;
	return ERR_OK;
bad:
	FREE (dst);
	return EINVAL;
}

enumError ScanArcb (nintendo_sarc_entry_t **entries, uint *n_entries, const u8 *data, size_t size)
{
	if (!entries || !n_entries || !data || size < 0x80 || memcmp (data, "ARCB", 4)
		|| memcmp (data + 0x20, "U\xaa" "8-", 4))
		return EINVAL;
	*entries = 0;
	*n_entries = 0;

	const u8 *u8a = data + 0x20;
	const u32 root = ab_rd32 (u8a + 4), hsize = ab_rd32 (u8a + 8), dofs = ab_rd32 (u8a + 12);
	if (root < 0x20 || (u64)0x20 + root + 12 > size || (u64)0x20 + dofs + 12 > size)
		return EINVAL;
	const u8 *nodes = u8a + root;
	const u32 n_nodes = ab_rd32 (nodes + 8);
	if (n_nodes < 1 || n_nodes > ARCB_MAX_NODES || (u64)0x20 + root + 12ull * n_nodes > size
		|| 12ull * n_nodes > hsize)
		return EINVAL;
	const u8 *names = nodes + 12 * n_nodes;
	const u32 names_size = hsize - 12 * n_nodes;

	u8 *blob = 0;
	uint blob_size = 0;
	enumError err = DecodeAvlz (data + 0x20 + dofs, size - 0x20 - dofs, &blob, &blob_size);
	if (err)
		return err;

	nintendo_sarc_entry_t *out = CALLOC (n_nodes, sizeof (*out));
	if (!out)
	{
		FREE (blob);
		return ERR_CANT_CREATE;
	}
	// Directory stack: path prefix lengths with the node index each one ends at.
	char path[1024];
	size_t pfx[64];
	u32 pend[64];
	uint depth = 0, n = 0;
	path[0] = 0;
	for (u32 i = 1; i < n_nodes; i++)
	{
		while (depth && i >= pend[depth - 1])
		{
			depth--;
			path[depth ? pfx[depth - 1] : 0] = 0;
		}
		const u8 *nd = nodes + 12 * i;
		const u32 name_off = ab_rd32 (nd) & 0xffffff, a = ab_rd32 (nd + 4) & 0x7fffffff,
			  b = ab_rd32 (nd + 8); // bit 31 of the offset flags a packed member
		if (name_off >= names_size)
			continue;
		char name[256];
		const size_t nl = strnlen ((const char *)names + name_off, names_size - name_off);
		if (nl == 0 || nl >= sizeof (name) || name_off + nl >= names_size)
			continue;
		memcpy (name, names + name_off, nl);
		name[nl] = 0;
		if (nd[0] == 1)
		{
			if (depth >= 64 || strlen (path) + nl + 2 >= sizeof (path) || b > n_nodes)
				continue;
			strcat (path, name);
			strcat (path, "/");
			pfx[depth] = strlen (path);
			pend[depth] = b;
			depth++;
			continue;
		}
		char full[1300];
		snprintf (full, sizeof (full), "%s%s", path, name);
		if (!OwnedNameOk (full) || (u64)a + b > blob_size)
			continue;
		if (!OwnedEntryAdd (out, n, full, blob + a, b))
		{
			ResetOwnedEntries (out, n);
			FREE (blob);
			return ERR_CANT_CREATE;
		}
		n++;
	}
	FREE (blob);
	if (!n)
	{
		FREE (out);
		return EINVAL;
	}
	*entries = out;
	*n_entries = n;
	return ERR_OK;
}
