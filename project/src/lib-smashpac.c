// SPDX-License-Identifier: GPL-2.0+
// Super Smash Bros. 4 animation container (.pac, magic "PACK").
//
// Reference: KillzXGaming/Smash-Forge, "Smash Forge/Filetypes/PAC.cs"
// (MIT licensed; clean-room C port of the binary layout).

#include "lib-smashpac.h"
#include "lib-std.h"
#include "lib-archive-util.h"
#include <string.h>

static u32 smashpac_rd32 (const u8 *p, bool be)
{
	return be ? rd_be32 (p) : rd_le32 (p);
}

static bool smashpac_layout_ok (const u8 *data, size_t size, u32 *count, bool *be)
{
	if (!data || size < 16)
		return false;
	if (!memcmp (data, "PACK", 4))
		*be = false;
	else if (!memcmp (data, "KCAP", 4))
		*be = true;
	else
		return false;

	const u32 n = smashpac_rd32 (data + 8, *be);
	if (n > 100000)
		return false;
	if (16 + (size_t)n * 12 > size)
		return false;
	*count = n;

	for (u32 i = 0; i < n; i++)
	{
		const u32 soff = smashpac_rd32 (data + 16 + (size_t)i * 4, *be);
		const u32 doff = smashpac_rd32 (data + 16 + (size_t)n * 4 + (size_t)i * 4, *be);
		const u32 sz = smashpac_rd32 (data + 16 + (size_t)n * 8 + (size_t)i * 4, *be);
		if (soff >= size || !memchr (data + soff, 0, size - soff))
			return false;
		if ((size_t)doff + sz > size)
			return false;
	}
	return true;
}

bool IsSmashPac (const u8 *data, size_t size)
{
	u32 n;
	bool be;
	return smashpac_layout_ok (data, size, &n, &be);
}

enumError ScanSmashPac (
	nintendo_sarc_entry_t **entries, uint *n_entries, const u8 *data, size_t size)
{
	u32 n;
	bool be;
	if (!entries || !n_entries || !smashpac_layout_ok (data, size, &n, &be))
		return EINVAL;

	nintendo_sarc_entry_t *res = CALLOC (n ? n : 1, sizeof (*res));
	if (!res)
		return ERR_CANT_CREATE;

	for (u32 i = 0; i < n; i++)
	{
		const u32 soff = smashpac_rd32 (data + 16 + (size_t)i * 4, be);
		const u32 doff = smashpac_rd32 (data + 16 + (size_t)n * 4 + (size_t)i * 4, be);
		const u32 sz = smashpac_rd32 (data + 16 + (size_t)n * 8 + (size_t)i * 4, be);
		res[i].name = STRDUP ((const char *)(data + soff));
		if (!res[i].name)
		{
			for (u32 j = 0; j < i; j++)
				FREE ((void *)res[j].name);
			FREE (res);
			return ERR_CANT_CREATE;
		}
		res[i].data = data + doff;
		res[i].size = sz;
	}
	*entries = res;
	*n_entries = n;
	return ERR_OK;
}

enumError CreateSmashPac (
	u8 **out, uint *out_size, const nintendo_sarc_entry_t *entries, uint n_entries, bool big_endian)
{
	if (!out || !out_size || !entries || !n_entries || n_entries > 100000)
		return EINVAL;

	size_t names_len = 0;
	for (uint i = 0; i < n_entries; i++)
	{
		if (!entries[i].name || !OwnedNameOk (entries[i].name))
			return EINVAL;
		names_len += strlen (entries[i].name) + 1;
	}

	size_t hdr = 16 + (size_t)n_entries * 12;
	size_t total = hdr + names_len;
	for (uint i = 0; i < n_entries; i++)
	{
		total = (total + 15) & ~(size_t)15;
		total += entries[i].size;
	}
	if (total > UINT_MAX)
		return EINVAL;

	u8 *buf = CALLOC (1, total ? total : 1);
	if (!buf)
		return ERR_CANT_CREATE;

	memcpy (buf, big_endian ? "KCAP" : "PACK", 4);
	if (big_endian)
		wr_be32 (buf + 8, n_entries);
	else
		wr_le32 (buf + 8, n_entries);

	size_t spos = hdr;
	size_t dpos = hdr + names_len;
	u32 *soffs = CALLOC (n_entries, sizeof (*soffs));
	u32 *doffs = CALLOC (n_entries, sizeof (*doffs));
	if (!soffs || !doffs)
	{
		FREE (soffs);
		FREE (doffs);
		FREE (buf);
		return ERR_CANT_CREATE;
	}

	for (uint i = 0; i < n_entries; i++)
	{
		ccp name = entries[i].name ? entries[i].name : "";
		soffs[i] = (u32)spos;
		size_t nl = strlen (name) + 1;
		memcpy (buf + spos, name, nl);
		spos += nl;

		dpos = (dpos + 15) & ~(size_t)15;
		doffs[i] = (u32)dpos;
		if (entries[i].data && entries[i].size)
			memcpy (buf + dpos, entries[i].data, entries[i].size);
		dpos += entries[i].size;
	}

	for (uint i = 0; i < n_entries; i++)
	{
		if (big_endian)
		{
			wr_be32 (buf + 16 + (size_t)i * 4, soffs[i]);
			wr_be32 (buf + 16 + (size_t)n_entries * 4 + (size_t)i * 4, doffs[i]);
			wr_be32 (buf + 16 + (size_t)n_entries * 8 + (size_t)i * 4, entries[i].size);
		}
		else
		{
			wr_le32 (buf + 16 + (size_t)i * 4, soffs[i]);
			wr_le32 (buf + 16 + (size_t)n_entries * 4 + (size_t)i * 4, doffs[i]);
			wr_le32 (buf + 16 + (size_t)n_entries * 8 + (size_t)i * 4, entries[i].size);
		}
	}

	FREE (soffs);
	FREE (doffs);
	*out = buf;
	*out_size = (uint)dpos;
	return ERR_OK;
}

enumError ExtractSmashPacArchive (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext_match (arg, ".pac"))
		return ERR_NOTHING_TO_DO;

	u8 *raw = 0;
	size_t raw_size = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false))
		return ERR_NOTHING_TO_DO;

	nintendo_sarc_entry_t *list = 0;
	uint n_list = 0;
	if (ScanSmashPac (&list, &n_list, raw, raw_size))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	char dest[PATH_MAX];
	get_dest_dir (dest, sizeof (dest), arg, basedir);
	CreatePath (dest, true);

	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT SMPAC:%s (%u members) -> %s/\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", arg, n_list, dest);

	if (!testmode)
	{
		char man_path[PATH_MAX];
		snprintf (man_path, sizeof (man_path), "%s/SmashPac.txt", dest);
		FILE *man = fopen (man_path, "w");
		if (man)
		{
			fprintf (man,
				"#SMPAC\n# Super Smash Bros. 4 animation container\n\nmembers = %u\n\n[members]\n",
				n_list);
			for (uint i = 0; i < n_list; i++)
				fprintf (man, "%s (%u bytes)\n", list[i].name ? list[i].name : "", list[i].size);
			fclose (man);
		}
		for (uint i = 0; i < n_list; i++)
		{
			char fallback[64];
			ccp name = list[i].name;
			if (!name || !OwnedNameOk (name))
			{
				snprintf (fallback, sizeof (fallback), "member_%04u.bin", i);
				name = fallback;
			}
			char out_path[PATH_MAX];
			snprintf (out_path, sizeof (out_path), "%s/%s", dest, name);
			if (list[i].size)
				SaveFile (out_path, 0, 0, list[i].data, list[i].size, 0);
		}
	}

	for (uint i = 0; i < n_list; i++)
		FREE ((void *)list[i].name);
	FREE (list);
	FREE (raw);
	return ERR_OK;
}

enumError create_smashpac_dir (ccp source, ccp dest)
{
	sarc_build_list_t list = { 0 };
	enumError err = collect_sarc_dir (&list, source, "");
	if (!err && !list.used)
		err = ERR_NOTHING_TO_DO;

	// The extractor's SmashPac.txt manifest is metadata, not payload.
	for (uint i = 0; !err && i < list.used;)
	{
		if (list.entry[i].name && !strcmp (leaf_name (list.entry[i].name), "SmashPac.txt"))
		{
			FREE ((void *)list.entry[i].name);
			FREE ((void *)list.entry[i].data);
			memmove (
				&list.entry[i], &list.entry[i + 1], (list.used - i - 1) * sizeof (list.entry[i]));
			list.used--;
		}
		else
			i++;
	}
	if (!err && !list.used)
		err = ERR_NOTHING_TO_DO;

	u8 *data = 0;
	uint size = 0;
	if (!err)
		err = CreateSmashPac (&data, &size, list.entry, list.used, false);
	if (!err && !testmode)
	{
		File_t F;
		err = CreateFileOpt (&F, true, dest, false, dest);
		if (F.f && size && fwrite (data, 1, size, F.f) != size)
			err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", size, dest);
		ResetFile (&F, opt_preserve);
	}
	FREE (data);
	reset_sarc_build_list (&list);
	return err;
}
