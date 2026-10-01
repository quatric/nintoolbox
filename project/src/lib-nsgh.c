// SPDX-License-Identifier: GPL-2.0+
// Neversoft Guitar Hero (Wii) .pak.ngc / .img.ngc; see lib-nsgh.h.
#include "lib-nsgh.h"
#include "lib-image.h"

#define NSPAK_MAX_ENTRIES 200000

static u32 ns_be32 (const u8 *p)
{
	return (u32)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3];
}

static uint nspak_walk (const u8 *d, size_t size, nintendo_sarc_entry_t *v)
{
	uint n = 0;
	u64 e = 0;
	for (;;)
	{
		if (n >= NSPAK_MAX_ENTRIES || e + 0x20 > size)
			return 0;
		const u32 type = ns_be32 (d + e), off = ns_be32 (d + e + 4), sz = ns_be32 (d + e + 8);
		const u32 flags = ns_be32 (d + e + 0x1c);
		if (!type && !off && !sz)
			return n;
		if (flags & ~0x24u || off < e || off > size || sz > size - off)
			return 0;
		const bool named = (flags & 0x20) != 0;
		if (named && e + 0x20 + 160 > size)
			return 0;
		if (v)
		{
			char name[200];
			if (named)
			{
				memcpy (name, d + e + 0x20, 160);
				name[160] = 0;
				for (char *c = name; *c; c++)
					if (*c == '\\')
						*c = '/';
				// no absolute paths, no parent escapes
				if (name[0] == '/' || strstr (name, "..") || (unsigned char)name[0] < 0x20)
					name[0] = 0;
			}
			else
				name[0] = 0;
			char full[256];
			if (name[0])
				snprintf (full, sizeof (full), "%04u_%s", n, strrchr (name, '/') ? strrchr (name, '/') + 1 : name);
			else
				snprintf (full, sizeof (full), "%04u_%08x.%08x", n, ns_be32 (d + e + 0x14) ? ns_be32 (d + e + 0x14) : ns_be32 (d + e + 0x0c), type);
			u8 *out = MALLOC (sz ? sz : 1);
			if (!out)
				return 0;
			memcpy (out, d + off, sz);
			v[n].name = STRDUP (full);
			v[n].data = out;
			v[n].size = sz;
		}
		n++;
		e += 0x20 + (named ? 160 : 0);
	}
}

enumError ScanNsPak (nintendo_sarc_entry_t **entries, uint *n_entries, const u8 *d, size_t size)
{
	if (!entries || !n_entries || !d || size < 0x40)
		return EINVAL;
	const uint n = nspak_walk (d, size, 0);
	if (!n)
		return EINVAL;
	nintendo_sarc_entry_t *v = CALLOC (n, sizeof (*v));
	if (!v)
		return ERR_OUT_OF_MEMORY;
	if (nspak_walk (d, size, v) != n)
	{
		FREE (v);
		return ERR_WARNING;
	}
	*entries = v;
	*n_entries = n;
	return ERR_OK;
}

enumError ScanNsImg (nintendo_sarc_entry_t **entries, uint *n_entries, const u8 *d, size_t size, ccp name)
{
	if (!entries || !n_entries || !d || size < 0x20 || d[0] != 0x04 || d[1] != 0x20)
		return EINVAL;
	const uint lw = d[10], lh = d[11];
	const uint fmt = d[13];
	const u32 dsz = ns_be32 (d + 0x10), doff = ns_be32 (d + 0x14);
	if (fmt != 14 || lw > 11 || lh > 11 || doff != 0x20 || dsz > size - 0x20)
		return EINVAL;
	const u32 w = 1u << lw, h = 1u << lh;
	// CMPR: 8x8 tiles of 32 bytes
	const u32 want = ((w + 7) / 8) * ((h + 7) / 8) * 32;
	if (dsz < want)
		return EINVAL;

	const u32 tpl_hdr = sizeof (tpl_header_t);
	const u32 tpl_tab = tpl_hdr + sizeof (tpl_imgtab_t);
	const u32 tpl_data = tpl_tab + sizeof (tpl_img_header_t);
	u8 *tpl = CALLOC (tpl_data + want, 1);
	if (!tpl)
		return ERR_OUT_OF_MEMORY;
	write_be32 (tpl, TPL_MAGIC_NUM);
	write_be32 (tpl + 4, 1);
	write_be32 (tpl + 8, tpl_hdr);
	write_be32 (tpl + tpl_hdr, tpl_tab);
	write_be32 (tpl + tpl_hdr + 4, 0);
	write_be16 (tpl + tpl_tab, (u16)h);
	write_be16 (tpl + tpl_tab + 2, (u16)w);
	write_be32 (tpl + tpl_tab + 4, IMG_CMPR);
	write_be32 (tpl + tpl_tab + 8, tpl_data);
	write_be32 (tpl + tpl_tab + 20, 1);
	write_be32 (tpl + tpl_tab + 24, 1);
	memcpy (tpl + tpl_data, d + doff, want);

	nintendo_sarc_entry_t *v = CALLOC (1, sizeof (*v));
	if (!v)
	{
		FREE (tpl);
		return ERR_OUT_OF_MEMORY;
	}
	char base[256];
	snprintf (base, sizeof (base), "%s", name && *name ? name : "image");
	char *dot = strstr (base, ".img.ngc");
	if (dot)
		*dot = 0;
	char full[300];
	snprintf (full, sizeof (full), "%s.tpl", base);
	v->name = STRDUP (full);
	v->data = tpl;
	v->size = tpl_data + want;
	*entries = v;
	*n_entries = 1;
	return ERR_OK;
}
