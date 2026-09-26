#include "lib-std.h"
#include "lib-archive-util.h"
#include "lib-pac.h"
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <zlib.h>

void ResetPAC (pac_t *pac)
{
	if (!pac)
		return;
	FREE (pac->entries);
	memset (pac, 0, sizeof (*pac));
}

enumError ScanPAC (pac_t *pac, const u8 *data, uint size)
{
	if (!pac || !data || size < 0x40 || memcmp (data, "ARC\0", 4))
		return EINVAL;
	if (data[4] != 1 || data[5] != 1)
		return EINVAL;

	const uint n = rd_be16 (data + 6);
	if (!n || n > 0x10000)
		return EINVAL;

	memset (pac, 0, sizeof (*pac));
	pac->data = data;
	pac->size = size;
	memcpy (pac->name, data + 0x10, sizeof (pac->name) - 1);
	pac->name[sizeof (pac->name) - 1] = 0;

	pac_entry_t *entries = CALLOC (n, sizeof (*entries));
	if (!entries)
		return ERR_CANT_CREATE;

	uint off = 0x40, i;
	for (i = 0; i < n; i++)
	{
		if (off + 0x20 > size)
			break;
		const u8 *h = data + off;
		const u16 type = rd_be16 (h);
		const u16 index = rd_be16 (h + 2);
		const u32 fsize = rd_be32 (h + 4);
		const u8 group = h[8];
		const s16 redirect = (s16)rd_be16 (h + 10);

		const u32 data_off = off + 0x20;
		if ((u64)data_off + fsize > size)
			break;

		entries[i].type = type;
		entries[i].index = index;
		entries[i].group_index = group;
		entries[i].redirect_index = redirect;

		const u8 *np = h + 0x10;
		uint nl = 0;
		while (nl < sizeof (entries[i].name) && np[nl] >= 0x20 && np[nl] <= 0x7E && np[nl] != '/')
			nl++;
		if (nl && nl < sizeof (entries[i].name) && !np[nl])
		{
			memcpy (entries[i].name, np, nl);
			entries[i].name[nl] = 0;
		}
		entries[i].size = fsize;
		entries[i].data = data + data_off;

		const u64 next = ((u64)data_off + fsize + 0x1f) & ~(u64)0x1f;
		if (next <= off || next > size)
		{
			i++;
			break;
		}
		off = (uint)next;
	}

	if (!i)
	{
		FREE (entries);
		return EINVAL;
	}
	pac->entries = entries;
	pac->n_entries = i;
	return ERR_OK;
}

enumError CreatePAC (
	u8 **dest, uint *dest_size, const nintendo_sarc_entry_t *entries, uint n_entries)
{
	if (!dest || !dest_size || !entries || !n_entries || n_entries > 0xFFFF)
		return EINVAL;

	uint cur_size = 0x40;
	for (uint i = 0; i < n_entries; i++)
	{
		cur_size += 0x20;
		cur_size += entries[i].size;
		cur_size = (cur_size + 0x1F) & ~0x1Fu;
	}

	u8 *out = CALLOC (1, cur_size);
	if (!out)
		return ERR_CANT_CREATE;

	memcpy (out, "ARC\0", 4);
	out[4] = 1;
	out[5] = 1;
	wr_be16 (out + 6, (u16)n_entries);

	uint off = 0x40;
	for (uint i = 0; i < n_entries; i++)
	{
		ccp full_name = entries[i].name ? entries[i].name : "misc";
		ccp slash = strrchr (full_name, '/');
		ccp fname = slash ? slash + 1 : full_name;

		u16 type = 1;
		if (entries[i].data && entries[i].size >= 4)
		{
			if (!memcmp (entries[i].data, "bres", 4) || !memcmp (entries[i].data, "MDL0", 4))
				type = 2;
			else if (!memcmp (entries[i].data, "TEX0", 4))
				type = 3;
			else if (!memcmp (entries[i].data, "ANIM", 4) || !memcmp (entries[i].data, "CHR0", 4)
				|| !memcmp (entries[i].data, "CLR0", 4) || !memcmp (entries[i].data, "PAT0", 4)
				|| !memcmp (entries[i].data, "SHP0", 4) || !memcmp (entries[i].data, "VIS0", 4)
				|| !memcmp (entries[i].data, "SCN0", 4))
				type = 5;
		}

		u8 *h = out + off;
		wr_be16 (h + 0, type);
		wr_be16 (h + 2, (u16)i);
		wr_be32 (h + 4, entries[i].size);
		h[8] = 0;
		h[9] = 0;
		wr_be16 (h + 10, 0xFFFF);

		size_t nlen = strlen (fname);
		if (nlen > 15)
			nlen = 15;
		memcpy (h + 0x10, fname, nlen);
		h[0x10 + nlen] = 0;

		if (entries[i].size && entries[i].data)
			memcpy (out + off + 0x20, entries[i].data, entries[i].size);

		off += 0x20 + entries[i].size;
		off = (off + 0x1F) & ~0x1Fu;
	}

	*dest = out;
	*dest_size = cur_size;
	return ERR_OK;
}

// Nd Cube Wii U PAC ("PAC\0"): does the *existing destination* hold this
// container? Then rebuild it -- a byte-exact round trip when every entry
// still matches a tree member 1:1 by name and decompressed content (the
// archive is copied as-is), otherwise a fresh valid PAC that compresses
// the members with zlib again. A destination with any other magic (or none
// at all) falls through to the ARC "CreatePAC" Brawl builder below; the two
// formats only share the ".pac"/".pcs" name here.
bool ndcube_pac_dest (ccp dest)
{
	FILE *f = fopen (dest, "rb");
	bool is = false;
	if (f)
	{
		char m[4] = { 0 };
		is = fread (m, 1, 4, f) == 4 && !memcmp (m, "PAC\0", 4);
		fclose (f);
	}
	return is;
}

// Fresh Nd Cube "PAC\0": header + entries table + name strings + zlib
// deflate payloads. LANGUAGECOUNT is left at 0 (the extractor keys the
// member count off FILETOTAL, never the language blocks), and every
// payload gets a real 0x78.x zlib stream -- even empty members -- so each
// one decodes on the way back out.
enumError create_ndcube_pac (u8 **data, uint *size, sarc_build_list_t *list)
{
	const uint n = list->used;
	if (!n || n > 200000)
		return EINVAL;

	u8 **comp = CALLOC (n, sizeof (*comp));
	u32 *csize = CALLOC (n, sizeof (*csize));
	u32 *usize = CALLOC (n, sizeof (*usize));
	u32 *fstart = CALLOC (n, sizeof (*fstart));
	if (!comp || !csize || !usize || !fstart)
	{
		FREE (comp);
		FREE (csize);
		FREE (usize);
		FREE (fstart);
		return ERR_CANT_CREATE;
	}

	uint names_len = 0;
	enumError err = ERR_OK;
	for (uint i = 0; i < n; i++)
	{
		usize[i] = list->entry[i].size;
		names_len += strlen (list->entry[i].name) + 1;
		uLongf bound = compressBound (usize[i]);
		comp[i] = CALLOC (1, bound);
		if (!comp[i])
		{
			err = ERR_CANT_CREATE;
			break;
		}
		uLongf out_len = bound;
		if (compress2 (comp[i], &out_len, list->entry[i].data, usize[i], 9) != Z_OK)
		{
			err = ERR_CANT_CREATE;
			break;
		}
		csize[i] = (u32)out_len;
	}
	if (!err)
	{
		const u32 entries_off = 0x44;
		u32 strings_off = entries_off + n * 0x30;
		strings_off = (strings_off + 3) & ~3u;
		uint data_off = strings_off + names_len;
		data_off = (data_off + 15) & ~15u;

		uint total = data_off;
		for (uint i = 0; i < n; i++)
		{
			fstart[i] = total;
			total += csize[i];
			total = (total + 0x1f) & ~0x1fu;
		}

		u8 *out = CALLOC (1, total);
		if (!out)
			err = ERR_CANT_CREATE;
		else
		{
			memcpy (out, "PAC\0", 4);
			wr_be32 (out + 0x04, 0x44); // HEADERLENGTH
			wr_be32 (out + 0x0c, data_off); // OVERALLFILESTART
			wr_be32 (out + 0x10, total); // PACSIZE
			wr_be32 (out + 0x14, 0); // LANGUAGECOUNT
			wr_be32 (out + 0x20, n); // FILETOTAL
			wr_be32 (out + 0x34, entries_off); // LANGUAGESTART
			wr_be32 (out + 0x38, entries_off); // FILEHEADERSTART
			wr_be32 (out + 0x3c, strings_off); // STRINGSTART
			wr_be32 (out + 0x40, data_off); // OVERALLFILESTART2

			uint name_off = strings_off;
			for (uint i = 0; i < n; i++)
			{
				u8 *e = out + entries_off + i * 0x30;
				wr_be32 (e + 0x00, name_off); // FILENAMESTART
				wr_be32 (e + 0x10, fstart[i]); // FILESTART
				wr_be32 (e + 0x14, usize[i]); // SIZE
				wr_be32 (e + 0x18, csize[i]); // ZSIZE
				wr_be32 (e + 0x1c, csize[i]); // ZSIZE2
				ccp name = list->entry[i].name;
				const uint nlen = strlen (name) + 1;
				memcpy (out + name_off, name, nlen);
				name_off += nlen;
				if (csize[i])
					memcpy (out + fstart[i], comp[i], csize[i]);
			}

			*data = out;
			*size = total;
		}
	}

	for (uint i = 0; i < n; i++)
		FREE (comp[i]);
	FREE (comp);
	FREE (csize);
	FREE (usize);
	FREE (fstart);
	return err;
}

enumError create_pac_dir (ccp source, ccp dest)
{
	sarc_build_list_t list = { 0 };
	enumError err = collect_sarc_dir (&list, source, "");
	if (!err && !list.used)
		err = ERR_NOTHING_TO_DO;

	u8 *data = 0;
	uint size = 0;

	if (!err && ndcube_pac_dest (dest))
	{
		// Byte-exact round trip: every entry of the existing PAC still has a
		// tree member with the same name whose bytes inflate to the same
		// content, so the original archive is reproduced unchanged (header,
		// language blocks, entry metadata, name table and payloads alike).
		u8 *raw = 0;
		size_t raw_size = 0;
		if (!LoadFileAlloc (dest, 0, 0, &raw, &raw_size, 0, 0, 0, false) && raw_size >= 0x44
			&& raw_size <= UINT_MAX && !memcmp (raw, "PAC\0", 4))
		{
			const u32 n = rd_be32 (raw + 0x20);
			const u32 fhs = rd_be32 (raw + 0x38);
			bool reusable = n && n <= 200000 && n == list.used && fhs <= raw_size - (u64)n * 0x30;
			bool *used_member = reusable ? CALLOC (list.used, 1) : 0;
			if (reusable && !used_member)
				reusable = false;
			if (reusable)
			{
				for (uint i = 0; reusable && i < n; i++)
				{
					const u8 *e = raw + fhs + i * 0x30;
					const u32 noff = rd_be32 (e);
					const u32 fstart = rd_be32 (e + 0x10);
					const u32 zsize = rd_be32 (e + 0x18);
					char name[PATH_MAX];
					reusable = noff < raw_size && zsize && fstart <= raw_size - zsize;
					if (reusable)
					{
						size_t max_len = sizeof (name) - 1;
						if (max_len > raw_size - noff)
							max_len = raw_size - noff;
						const size_t nlen = strnlen ((const char *)(raw + noff), max_len);
						memcpy (name, raw + noff, nlen);
						name[nlen] = 0;
					}
					int hit = -1;
					for (uint k = 0; reusable && hit < 0 && k < list.used; k++)
						if (!used_member[k] && !strcmp (list.entry[k].name, name))
							hit = (int)k;
					if (reusable && hit < 0)
						reusable = false;
					if (reusable)
					{
						u8 *decomp = 0;
						uint ds = 0;
						if (DecodeZlibGrow (&decomp, &ds, raw + fstart, zsize) != ERR_OK)
							reusable = false;
						else if (ds != list.entry[hit].size
							|| (ds && memcmp (decomp, list.entry[hit].data, ds)))
							reusable = false;
						FREE (decomp);
					}
					if (reusable)
						used_member[hit] = true;
				}
			}
			FREE (used_member);
			if (reusable)
			{
				u8 *out = MALLOC (raw_size);
				if (!out)
					err = ERR_CANT_CREATE;
				else
				{
					memcpy (out, raw, raw_size);
					data = out;
					size = (uint)raw_size;
				}
			}
		}
		FREE (raw);
		if (err)
		{
			FREE (data);
			data = 0;
			size = 0;
		}
		if (!data)
			err = create_ndcube_pac (&data, &size, &list);
	}
	else if (!err)
		err = CreatePAC (&data, &size, list.entry, list.used);

	if (!err && !testmode)
	{
		File_t F;
		err = CreateFileOpt (&F, true, dest, false, dest);
		if (F.f && fwrite (data, 1, size, F.f) != size)
			err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", size, dest);
		ResetFile (&F, opt_preserve);
	}
	FREE (data);
	reset_sarc_build_list (&list);
	return err;
}
