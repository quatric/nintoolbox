// SPDX-License-Identifier: GPL-2.0+
// Dependency-free probes for Next Level Games containers -- see
// lib-nlg-probe.h. Shape checks only; parsing lives in lib-nlg-lm.c.

#include "types.h"
#include "lib-nintendo.h"
#include "lib-nlg-probe.h"
#include <string.h>

#define NLG_PROBE_MAX (512u << 20)

static bool nlg_probe_container (const u8 *data, size_t size, const char magic[4])
{
	if (!data || size < 8 || size > NLG_PROBE_MAX || memcmp (data, magic, 4))
		return false;
	u16 v = rd_le16 (data + 4);
	if (v < 1 || v > 3)
		return false;
	uint n = rd_le16 (data + 6);
	if (n == 0 || n > 4096 || 8 + (size_t)n * 8 > size)
		return false;
	size_t p = 8 + (size_t)n * 8;
	for (uint i = 0; i < n; i++)
	{
		u32 sz = rd_le32 (data + 8 + (size_t)i * 8 + 4);
		if ((u64)p + sz > size)
			return false;
		p += sz;
	}
	return true;
}

bool IsNLGModel (const u8 *data, size_t size)
{
	return nlg_probe_container (data, size, "FEDM");
}

bool IsNLGSkeleton (const u8 *data, size_t size)
{
	return nlg_probe_container (data, size, "FEDS");
}

bool IsNLGTexture (const u8 *data, size_t size)
{
	if (!data || size < 24 || size > NLG_PROBE_MAX || memcmp (data, "FEDT", 4))
		return false;
	u16 v = rd_le16 (data + 4);
	if (v < 1 || v > 3)
		return false;
	uint w = rd_le16 (data + 8), h = rd_le16 (data + 10);
	uint ds = rd_le32 (data + 20);
	if (!w || !h || w > 16384 || h > 16384 || 24 + (size_t)ds > size)
		return false;
	return true;
}

static bool nlg_sanim_known (u16 magic)
{
	switch (magic)
	{
	case 0x7000:
	case 0x7001:
	case 0x7002:
	case 0x7003:
	case 0x7004:
	case 0x7005:
	case 0x7006:
	case 0x7007:
	case 0x7008:
	case 0x7100:
	case 0x7101:
	case 0x7102:
	case 0x7103:
		return true;
	default:
		return false;
	}
}

// Strict walk: true only if the whole buffer is well-formed chunks with at
// least one animation header. Depth-capped against hostile nesting.
static bool nlg_sanim_walk (const u8 *d, uint size, int depth, bool *found_hdr)
{
	if (depth > 32 || !d || size < 8)
		return false;
	uint p = 0;
	bool any = false;
	while (p + 8 <= size)
	{
		u16 magic = rd_be16 (d + p + 2);
		u32 sz = rd_be32 (d + p + 4);
		if (!nlg_sanim_known (magic))
			return false;
		if ((u64)p + 8 + sz > size)
			return false;
		any = true;
		if (magic == 0x7001)
		{
			if (sz < 16 || rd_be32 (d + p + 8) != 0)
				return false;
			if (found_hdr)
				*found_hdr = true;
		}
		if ((magic == 0x7000 || magic == 0x7100) && sz)
		{
			if (!nlg_sanim_walk (d + p + 8, sz, depth + 1, found_hdr))
				return false;
		}
		p += 8 + sz;
		p = (p + 3) & ~3u; // Wii .rlg-style 4-byte section padding
	}
	return any && p <= size;
}

bool IsSANIM (const u8 *data, size_t size)
{
	if (!data || size < 8 || size > NLG_PROBE_MAX)
		return false;
	bool found = false;
	if (!nlg_sanim_walk (data, (uint)size, 0, &found))
		return false;
	return found;
}
