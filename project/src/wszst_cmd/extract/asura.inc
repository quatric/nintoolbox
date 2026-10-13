// Rebellion "Asura" engine resource files (PDC World Championship Darts, Sniper Elite, The Simpsons Game, Rogue
// Trooper on Wii; ".wii", ".asrBE", ".guiBE", ".enBE", ...). A file is either plain ("Asura   ") or wrapped as
// "AsuraZlb": 8-byte magic, u32 version (1), u32 compressed size, u32 unpacked size (both big-endian), then one
// zlib stream. The body is "Asura   " followed by big-endian chunks {char tag[4] (spelled backwards on disc, e.g.
// "FCSR" is RSCF), u32 size including this 16-byte header, u32 version, u32 instance, data}. An RSCF chunk embeds a
// complete file: {u32, u32, u32 file size, NUL-terminated backslash path padded to 4 bytes, file}. Those files are
// written under their paths in "<file>.d" (".tga" files that really hold a TPL are renamed ".tpl"); the other chunk
// types (models, entities, materials, ...) are not decoded.
static enumError extract_asura_file (ccp arg, ccp basedir, uint depth)
{
	u8 magic[8];
	FILE *f = fopen (arg, "rb");
	const bool hit = f && fread (magic, 1, 8, f) == 8 && (!memcmp (magic, "AsuraZlb", 8) || !memcmp (magic, "Asura   ", 8));
	if (f)
		fclose (f);
	if (!hit)
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0, *body = 0;
	size_t sz = 0, bsz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	nintendo_sarc_entry_t *entries = 0;
	uint n = 0, cap = 0;
	enumError err = ERR_NOTHING_TO_DO;
	if (!memcmp (raw, "AsuraZlb", 8))
	{
		const size_t zs = sz >= 20 ? rd_be32 (raw + 12) : 0, us = sz >= 20 ? rd_be32 (raw + 16) : 0;
		if (!us || us > 0x40000000 || zs > sz - 20)
			goto done;
		body = MALLOC (us);
		uLongf dl = us;
		if (!body || uncompress (body, &dl, raw + 20, zs) != Z_OK || dl != us)
			goto done;
		bsz = us;
	}
	else
	{
		body = raw;
		bsz = sz;
	}
	if (bsz < 8 || memcmp (body, "Asura   ", 8))
		goto done;
	for (size_t p = 8; p + 16 <= bsz;)
	{
		const size_t cs = rd_be32 (body + p + 4);
		if (cs < 16 || cs > bsz - p)
			break;
		if (!memcmp (body + p, "FCSR", 4) && cs >= 16 + 13)
		{
			const u8 *d = body + p + 16;
			const size_t dl = cs - 16, fsz = rd_be32 (d + 8);
			size_t nl = 0;
			while (12 + nl < dl && d[12 + nl])
				nl++;
			const size_t off = 12 + ((nl + 1 + 3) & ~(size_t)3);
			if (12 + nl < dl && nl && off <= dl && dl - off == fsz)
			{
				char name[PATH_MAX], *q = name;
				for (size_t i = 0; i < nl && q < name + sizeof (name) - 8; i++)
				{
					const char c = (char)d[12 + i];
					if (c == '\\')
					{
						if (q != name && q[-1] != '/')
							*q++ = '/';
					}
					else
						*q++ = c;
				}
				*q = 0;
				if (fsz >= 4 && rd_be32 (d + off) == 0x0020af30 && (q = strrchr (name, '.')) && !strcasecmp (q, ".tga"))
					strcpy (q, ".tpl");
				if (valid_sarc_path (name))
				{
					if (n == cap)
					{
						cap = cap ? 2 * cap : 64;
						entries = REALLOC (entries, cap * sizeof (*entries));
						if (!entries)
						{
							err = ERR_OUT_OF_MEMORY;
							goto done;
						}
						memset (entries + n, 0, (cap - n) * sizeof (*entries));
					}
					if (!OwnedEntryAdd (entries, n, name, d + off, (uint)fsz))
						goto done;
					n++;
				}
			}
		}
		p += cs;
	}
	if (n)
	{
		err = write_owned_entries (arg, basedir, depth, "ASURA", entries, n);
		entries = 0;
	}
done:
	if (entries)
		ResetOwnedEntries (entries, n);
	if (body != raw)
		FREE (body);
	FREE (raw);
	return err;
}
