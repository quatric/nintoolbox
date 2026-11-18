// Ubisoft "BIG" bigfiles ("*.bf", version 42; Prince of Persia: The Forgotten Sands "djinn_bin_wii.bf", Avatar, Shaun White Skateboarding): little-endian {"BIG\0", u32 42, u32 file count, u32 4, ...} with a table of {u32 key, u32 offset} pairs at 0x38 (the first pair is a 0xffffffff/0 sentinel).
// Records are stored back to back: {u32 size, size bytes of payload, 4 trailer bytes} so a record runs to the next offset. The names are hashes; payloads are written to "<file>.d/NNNN_<key>.bin" (file streamed from disk, not held in memory).
typedef struct jadebf_ent_t { u32 key, off; } jadebf_ent_t;

static int jadebf_cmp (const void *a, const void *b)
{
	const u32 x = ((const jadebf_ent_t *)a)->off, y = ((const jadebf_ent_t *)b)->off;
	return x < y ? -1 : x > y;
}

static enumError extract_jadebf_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	if (!is_ext (arg, ".bf"))
		return ERR_NOTHING_TO_DO;
	FILE *f = fopen (arg, "rb");
	if (!f)
		return ERR_NOTHING_TO_DO;
	enumError err = ERR_NOTHING_TO_DO;
	jadebf_ent_t *ents = 0;
	u8 hdr[0x40];
	if (fread (hdr, 1, sizeof (hdr), f) != sizeof (hdr) || memcmp (hdr, "BIG\0", 4) || rd_le32 (hdr + 4) != 42)
		goto done;
	{
		const uint n = rd_le32 (hdr + 8);
		if (n < 2 || n > 1000000 || fseeko (f, 0, SEEK_END))
			goto done;
		const u64 fsz = (u64)ftello (f);
		u8 *tab = MALLOC ((size_t)n * 8);
		if (fseeko (f, 0x38, SEEK_SET) || fread (tab, 1, (size_t)n * 8, f) != (size_t)n * 8)
		{
			FREE (tab);
			goto done;
		}
		ents = MALLOC ((size_t)n * sizeof (*ents));
		uint m = 0;
		for (uint i = 0; i < n; i++)
		{
			const u32 key = rd_le32 (tab + i * 8), off = rd_le32 (tab + i * 8 + 4);
			if (off && off + 8 <= fsz)
			{
				ents[m].key = key;
				ents[m].off = off;
				m++;
			}
		}
		FREE (tab);
		if (m < 2)
			goto done;
		qsort (ents, m, sizeof (*ents), jadebf_cmp);
		char dest[PATH_MAX];
		beside_source_dest (dest, sizeof (dest), arg);
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT BIGFILE:%s (%u files) -> %s/\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, m, dest);
		err = ERR_OK;
		u8 *buf = 0;
		size_t cap = 0;
		for (uint i = 0; i < m && !err && !testmode; i++)
		{
			u8 sb[4];
			if (fseeko (f, (off_t)ents[i].off, SEEK_SET) || fread (sb, 1, 4, f) != 4)
				continue;
			const u64 size = rd_le32 (sb);
			if (!size || ents[i].off + 4 + size > fsz || size > 0x20000000)
				continue;
			if (size > cap)
			{
				FREE (buf);
				cap = (size_t)size;
				buf = MALLOC (cap);
			}
			if (fread (buf, 1, (size_t)size, f) != (size_t)size)
				continue;
			char path[PATH_MAX];
			snprintf (path, sizeof (path), "%s/%04u_%08x.bin", dest, i, ents[i].key);
			File_t F;
			err = CreateFileOpt (&F, true, path, false, arg);
			if (F.f && fwrite (buf, 1, (size_t)size, F.f) != (size_t)size)
				err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %llu bytes failed: %s\n", (unsigned long long)size, path);
			ResetFile (&F, opt_preserve);
		}
		FREE (buf);
		(void)depth;
	}
done:
	FREE (ents);
	fclose (f);
	return err;
}
