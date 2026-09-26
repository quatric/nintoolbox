// SPDX-License-Identifier: GPL-2.0+
//-----------------------------------------------------------------------------
// Heavy Iron .ho package scanner; see lib-ho.h for the layout.
//-----------------------------------------------------------------------------
#include "lib-std.h"
#include "lib-ho.h"
#include <string.h>
#include <stdlib.h>
#include "lib-excite.h"

#define HO_SECTOR 0x800
#define HO_MAX_LAYERS 0x10000
#define HO_MAX_ASSETS 0x400000

static u32 ho_rd32 (const u8 *p)
{
	return (u32)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3];
}
static u64 ho_rd64 (const u8 *p)
{
	return (u64)ho_rd32 (p) << 32 | ho_rd32 (p + 4);
}

typedef struct
{
	u64 id;
	ccp name;
} ho_name_t;

static int ho_name_cmp (const void *a, const void *b)
{
	const u64 x = ((const ho_name_t *)a)->id, y = ((const ho_name_t *)b)->id;
	return x < y ? -1 : x > y;
}

static nintendo_sarc_entry_t *ho_sort_entries;

static int ho_entry_cmp (const void *a, const void *b)
{
	const uint x = *(const uint *)a, y = *(const uint *)b;
	const int r = strcasecmp (ho_sort_entries[x].name, ho_sort_entries[y].name);
	return r ? r : x < y ? -1 : x > y;
}

typedef struct
{
	uint tag; // 0 none, 1 PSL, 2 PSLD
	u32 lang;
	u64 start;
	u64 size;
	u64 meta;
} ho_layer_t;

enumError ScanHO (nintendo_sarc_entry_t **entries, uint *n_entries, const u8 *data, size_t size)
{
	if (!entries || !n_entries || !data || size < 0x900 || memcmp (data, "HE", 2)
		|| (data[2] != 'L' && data[2] != 'B') || data[3] != 0x1a)
		return EINVAL;
	if (data[2] == 'B')
		return EINVAL; // little-endian PC packages are not handled
	*entries = 0;
	*n_entries = 0;
	if (memcmp (data + HO_SECTOR, "MAST", 4))
		return EINVAL;
	const u64 sect = (u64)ho_rd32 (data + HO_SECTOR + 0x20 + 0x1c) * HO_SECTOR;
	if (sect + 0x20 > size || memcmp (data + sect, "SECT", 4))
		return EINVAL;
	const u32 nl = ho_rd32 (data + sect + 4);
	if (!nl || nl > HO_MAX_LAYERS || sect + 0x20 + 0x40ull * nl > size)
		return EINVAL;

	ho_layer_t *lay = CALLOC (nl, sizeof (*lay));
	if (!lay)
		return ERR_CANT_CREATE;
	uint n_names = 0, n_assets = 0;
	for (uint i = 0; i < nl; i++)
	{
		const u8 *e = data + sect + 0x20 + 0x40ull * i;
		const u64 meta = sect + ho_rd32 (e + 0x38);
		lay[i].lang = ho_rd32 (e + 4) >> 16;
		lay[i].start = (u64)ho_rd32 (e + 0x1c) * HO_SECTOR;
		lay[i].size = ho_rd32 (e + 0x20);
		lay[i].meta = meta;
		if (meta + 0x20 > size || lay[i].start + lay[i].size > size)
			continue;
		if (!memcmp (data + meta, "PSLD", 4))
		{
			lay[i].tag = 2;
			const u32 nm_cnt = ho_rd32 (data + meta + 8);
			if (n_names + nm_cnt < n_names || n_names + nm_cnt > 0x100000)
			{
				FREE (lay);
				return EINVAL;
			}
			n_names += nm_cnt;
		}
		else if (!memcmp (data + meta, "PSL\0", 4))
			lay[i].tag = 1;
	}

	// Debug names, looked up by asset id.
	ho_name_t *names = CALLOC (n_names ? n_names : 1, sizeof (*names));
	uint nn = 0;
	for (uint i = 0; names && i < nl; i++)
	{
		if (lay[i].tag != 2)
			continue;
		const u32 cnt = ho_rd32 (data + lay[i].meta + 8), off = ho_rd32 (data + lay[i].meta + 0xc);
		if ((u64)cnt * 4 > lay[i].size)
			continue;
		u64 o = lay[i].start + off;
		for (uint k = 0; k < cnt; k++)
		{
			const u32 esz = ho_rd32 (data + lay[i].start + 4ull * k);
			if (!esz || o + 0x24 > lay[i].start + lay[i].size || o + esz > size)
				break;
			const u32 no = ho_rd32 (data + o + 8);
			if (no < 0x10 || no >= esz || !memchr (data + o + no, 0, esz - no))
			{
				o += esz;
				continue;
			}
			names[nn].id = ho_rd64 (data + o);
			names[nn].name = (ccp)data + o + no;
			nn++;
			o += esz;
		}
	}
	if (nn)
		qsort (names, nn, sizeof (*names), ho_name_cmp);

	for (uint i = 0; i < nl; i++)
	{
		if (lay[i].tag != 1)
			continue;
		const u32 ns = ho_rd32 (data + lay[i].meta + 8);
		for (uint k = 0; k < ns && k < 16; k++)
		{
			const u8 *s = data + lay[i].meta + 0x10 + 16ull * k;
			if (s + 16 > data + size || ho_rd32 (s))
				continue;
			const u64 tab = lay[i].start + ho_rd32 (s + 4);
			if (tab + 0x20 <= size)
			{
				const u32 acnt = ho_rd32 (data + tab);
				if (acnt <= HO_MAX_ASSETS)
				{
					if (n_assets + acnt > HO_MAX_ASSETS || n_assets + acnt < n_assets)
						n_assets = HO_MAX_ASSETS;
					else
						n_assets += acnt;
				}
			}
		}
	}
	nintendo_sarc_entry_t *out = n_assets ? CALLOC (n_assets, sizeof (*out)) : 0;
	u64 *ids = n_assets ? CALLOC (n_assets, sizeof (*ids)) : 0;
	if (!out || !ids || !names)
	{
		FREE (lay);
		FREE (names);
		FREE (out);
		FREE (ids);
		return n_assets ? ERR_CANT_CREATE : EINVAL;
	}

	uint n = 0;
	for (uint i = 0; i < nl; i++)
	{
		if (lay[i].tag != 1)
			continue;
		const u32 ns = ho_rd32 (data + lay[i].meta + 8);
		for (uint k = 0; k < ns && k < 16; k++)
		{
			const u8 *s = data + lay[i].meta + 0x10 + 16ull * k;
			if (s + 16 > data + size || ho_rd32 (s))
				continue;
			const u64 tab = lay[i].start + ho_rd32 (s + 4);
			if (tab + 0x20 > size)
				continue;
			const u32 cnt = ho_rd32 (data + tab);
			if (cnt > HO_MAX_ASSETS || tab + 0x20 + 0x20ull * cnt > size)
				continue;
			for (uint a = 0; a < cnt && n < n_assets; a++)
			{
				const u8 *e = data + tab + 0x20 + 0x20ull * a;
				const u64 off = lay[i].start + ho_rd32 (e + 4);
				const u32 asz = ho_rd32 (e + 8);
				if (off + asz > size)
					continue;
				const u64 id = ho_rd64 (e + 0x10);
				const u32 typ = ho_rd32 (e + 0x18);
				ho_name_t key = { id, 0 },
						  *hit = nn ? bsearch (&key, names, nn, sizeof (*names), ho_name_cmp) : 0;
				char nm[256], path[768];
				if (hit)
					snprintf (nm, sizeof (nm), "%s", hit->name);
				else
					snprintf (nm, sizeof (nm), "%016llx", (unsigned long long)id);
				if (lay[i].lang)
					snprintf (path, sizeof (path), "lang%02x/%s.%08x", lay[i].lang, nm, typ);
				else
					snprintf (path, sizeof (path), "%s.%08x", nm, typ);
				if (!OwnedNameOk (path))
					snprintf (path, sizeof (path), "%016llx.%08x", (unsigned long long)id, typ);
				if (!OwnedEntryAdd (out, n, path, data + off, asz))
				{
					ResetOwnedEntries (out, n);
					FREE (lay);
					FREE (names);
					FREE (ids);
					return ERR_CANT_CREATE;
				}
				ids[n++] = id;
			}
		}
	}
	FREE (lay);
	FREE (names);
	if (!n)
	{
		FREE (out);
		FREE (ids);
		return EINVAL;
	}

	// The same name can occur twice in one package (e.g. per-layer copies):
	// suffix the later ones with their asset id and index so nothing is overwritten.
	uint *order = MALLOC (n * sizeof (*order));
	if (order)
	{
		for (uint i = 0; i < n; i++)
			order[i] = i;
		ho_sort_entries = out;
		qsort (order, n, sizeof (*order), ho_entry_cmp);
		uint base = 0;
		for (uint i = 1; i < n; i++)
		{
			if (strcasecmp (out[order[i]].name, out[order[base]].name))
			{
				base = i;
				continue;
			}
			const uint k = order[i];
			char path[800];
			ccp cur = out[k].name, dot = strrchr (cur, '.');
			const int stem = dot ? (int)(dot - cur) : (int)strlen (cur);
			snprintf (path, sizeof (path), "%.*s_%016llx_%u%s", stem, cur,
				(unsigned long long)ids[k], k, dot ? dot : "");
			char *nm = MALLOC (strlen (path) + 1);
			if (!nm)
				break;
			strcpy (nm, path);
			FREE ((char *)out[k].name);
			out[k].name = nm;
		}
		FREE (order);
	}
	FREE (ids);
	*entries = out;
	*n_entries = n;
	return ERR_OK;
}

bool IsHoTexture (const u8 *d, size_t size)
{
	if (size < 0x80 || ho_rd32 (d + 0x20) != 0x0020af30)
		return false;
	const u32 ver = ho_rd32 (d + 0x24), hs = ho_rd32 (d + 0x2c);
	if ((ver != 1 && ver != 2) || hs < 0x14 || hs > 0x40)
		return false;
	const uint h = d[0x20 + hs] << 8 | d[0x21 + hs], w = d[0x22 + hs] << 8 | d[0x23 + hs];
	const u32 fmt = ho_rd32 (d + 0x24 + hs);
	if (!w || !h || w > 4096 || h > 4096 || (fmt != 5 && fmt != 6 && fmt != 14))
		return false;
	return true;
}

enumError DecodeHoTexture (u8 **rgba, uint *width, uint *height, const u8 *d, size_t size)
{
	if (!IsHoTexture (d, size))
		return ERR_NOTHING_TO_DO;
	const u32 hs = ho_rd32 (d + 0x2c);
	const uint h = d[0x20 + hs] << 8 | d[0x21 + hs], w = d[0x22 + hs] << 8 | d[0x23 + hs];
	const enumError err = DecodeGXTexture_RGBA (
		rgba, w, h, ho_rd32 (d + 0x24 + hs), d + 0x60, size - 0x60, 0, 0, 0);
	if (!err)
	{
		*width = w;
		*height = h;
	}
	return err;
}
