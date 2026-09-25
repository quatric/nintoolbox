// Call of Duty: Black Ops / MW3 (Wii) sound archive (PAK0) -- directory-tree
// CREATE glue.

#include "lib-std.h"
#include "lib-archive-util.h"
#include "lib-cod-pak.h"
#include "lib-nintendo.h"

// Members carry no names in the format, only a CRC (see
// extract_cod_pak0_file()); pairs a parsed member with its collected entry
// for CRC-order sorting.
typedef struct
{
	uint crc;
	nintendo_sarc_entry_t *e;
} cod_pak_member_t;

// Extract the member CRC out of an extracted member's basename. Returns
// false when the name is not "<anything>_0x%08x.dsp".
bool parse_cod_pak_crc (ccp name, uint *crc)
{
	const size_t len = strlen (name);
	if (len < 15 || memcmp (name + len - 15, "_0x", 3)
		|| strcasecmp (name + len - 4, ".dsp"))
		return false;
	uint v = 0;
	for (uint i = 0; i < 8; i++)
	{
		const char c = name[len - 12 + i];
		uint d;
		if (c >= '0' && c <= '9')
			d = (uint)(c - '0');
		else if (c >= 'a' && c <= 'f')
			d = (uint)(c - 'a' + 10);
		else if (c >= 'A' && c <= 'F')
			d = (uint)(c - 'A' + 10);
		else
			return false;
		v = (v << 4) | d;
	}
	if (crc)
		*crc = v;
	return true;
}


// Heuristic used to route CREATE *.pak.d trees between the PAK0 and GPAK
// builders (".pak" is shared by both formats; see extract_gpak_file()).
bool looks_like_cod_pak_dir (ccp source)
{
	DIR *dir = opendir (source);
	if (!dir)
		return false;
	bool found = false;
	struct dirent *de;
	while ((de = readdir (dir)))
	{
		if (*de->d_name == '.' || !strcmp (de->d_name, SZS_SETUP_FILE)
			|| !strcmp (de->d_name, ".wszst-cache.txt"))
			continue;
		if (parse_cod_pak_crc (de->d_name, 0))
		{
			found = true;
			break;
		}
	}
	closedir (dir);
	return found;
}


int cmp_cod_pak (const void *a, const void *b)
{
	const cod_pak_member_t *const ma = a, *const mb = b;
	return ma->crc < mb->crc ? -1 : ma->crc > mb->crc ? 1 : 0;
}


enumError create_cod_pak_dir (ccp source, ccp dest)
{
	sarc_build_list_t list = { 0 };
	enumError err = collect_sarc_dir (&list, source, "");
	if (!err && !list.used)
		err = ERR_NOTHING_TO_DO;

	// Only the CRC-named .dsp files are PAK0 members; drop any non-member
	// litter the tree happens to carry.
	uint used = 0;
	for (uint i = 0; !err && i < list.used; i++)
	{
		ccp base = list.entry[i].name;
		ccp slash = base ? strrchr (base, '/') : 0;
		ccp nm = slash ? slash + 1 : base;
		if (!parse_cod_pak_crc (nm, 0))
		{
			FREE ((void *)list.entry[i].name);
			FREE ((void *)list.entry[i].data);
			continue;
		}
		if (used != i)
			list.entry[used] = list.entry[i];
		used++;
	}
	list.used = used;
	if (!err && !used)
		err = ERR_NOTHING_TO_DO;

	cod_pak_member_t *m = 0;
	if (!err)
	{
		m = CALLOC (used, sizeof (*m));
		if (!m)
			err = ERR_CANT_CREATE;
		else
		{
			for (uint i = 0; i < used; i++)
			{
				ccp base = list.entry[i].name;
				ccp slash = base ? strrchr (base, '/') : 0;
				m[i].crc = 0;
				parse_cod_pak_crc (slash ? slash + 1 : base, &m[i].crc);
				m[i].e = &list.entry[i];
			}
			qsort (m, used, sizeof (*m), cmp_cod_pak);
			for (uint i = 1; !err && i < used; i++)
				if (m[i].crc == m[i - 1].crc)
					err = ERROR0 (ERR_INVALID_DATA,
						"PAK0 input has duplicate member CRC 0x%08x: %s\n", m[i].crc, source);
		}
	}

	u8 *out = 0;
	size_t out_len = 0;
	if (!err)
	{
		// Reuse the existing target's layout for a byte-exact round trip when
		// it is a valid PAK0 with exactly our members and unchanged sizes.
		u8 *raw = 0;
		size_t raw_size = 0;
		if (!LoadFileAlloc (dest, 0, 0, &raw, &raw_size, 0, 0, 0, false)
			&& raw_size >= 20 && !memcmp (raw, "PAK0", 4))
		{
			const uint n = rd_le32 (raw + 8);
			const uint mult = rd_le32 (raw + 12);
			const uint dstart = rd_le32 (raw + 16);
			const size_t table_end = 20 + (size_t)n * 12;
			bool reusable = n && mult && n == used && n <= (raw_size - 20) / 12
				&& dstart >= table_end && dstart <= raw_size;
			for (uint i = 0; reusable && i < n; i++)
			{
				const uint e_crc = rd_le32 (raw + 20 + i * 12 + 0);
				const uint e_off = rd_le32 (raw + 20 + i * 12 + 4);
				const uint e_size = rd_le32 (raw + 20 + i * 12 + 8);
				const uint64_t pos = (uint64_t)e_off * mult + dstart;
				if (pos > raw_size || e_size > raw_size - pos)
					reusable = false;
				cod_pak_member_t *match = 0;
				for (uint k = 0; !match && k < used; k++)
					if (m[k].crc == e_crc)
						match = &m[k];
				if (!match || match->e->size != e_size)
					reusable = false;
			}
			if (reusable)
			{
				out = MALLOC (raw_size);
				if (!out)
					err = ERR_CANT_CREATE;
				else
				{
					memcpy (out, raw, raw_size);
					for (uint i = 0; i < n; i++)
					{
						const uint e_crc = rd_le32 (raw + 20 + i * 12 + 0);
						const uint e_off = rd_le32 (raw + 20 + i * 12 + 4);
						const uint e_size = rd_le32 (raw + 20 + i * 12 + 8);
						const uint64_t pos = (uint64_t)e_off * mult + dstart;
						for (uint k = 0; k < used; k++)
							if (m[k].crc == e_crc)
							{
								memcpy (out + pos, m[k].e->data, (size_t)e_size);
								break;
							}
					}
					out_len = raw_size;
				}
			}
		}
		FREE (raw);
	}

	if (!err && !out)
	{
		const uint n = used;
		uint64_t total = (20 + (uint64_t)n * 12 + 15) & ~(uint64_t)15;
		const uint dstart = (uint)total;
		for (uint i = 0; i < n; i++)
			total += m[i].e->size;
		if (total > UINT_MAX)
			err = ERR_FILE_TOO_BIG;
		else
		{
			out = CALLOC (1, (size_t)total);
			if (!out)
				err = ERR_CANT_CREATE;
			else
			{
				out_len = (size_t)total;
				memcpy (out, "PAK0", 4);
				wr_le32 (out + 4, 0); // no known salt to carry for a new archive
				wr_le32 (out + 8, n);
				wr_le32 (out + 12, 1); // multiplier
				wr_le32 (out + 16, dstart);
				for (uint i = 0, off = 0; i < n; i++)
				{
					// With multiplier 1 the entry offset is the member's byte
					// distance from DATASTART; pack the payloads back to back.
					wr_le32 (out + 20 + i * 12 + 0, m[i].crc);
					wr_le32 (out + 20 + i * 12 + 4, off);
					wr_le32 (out + 20 + i * 12 + 8, m[i].e->size);
					memcpy (out + dstart + off, m[i].e->data, m[i].e->size);
					off += m[i].e->size;
				}
			}
		}
	}

	if (!err && !testmode)
	{
		File_t F;
		err = CreateFileOpt (&F, true, dest, false, dest);
		if (F.f && fwrite (out, 1, out_len, F.f) != out_len)
			err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %llu bytes failed: %s\n", (u64)out_len, dest);
		ResetFile (&F, opt_preserve);
	}

	FREE (out);
	FREE (m);
	reset_sarc_build_list (&list);
	return err;
}

