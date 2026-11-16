// "vaps" page sets (Disney-Pixar Toy Story Mania!, Disney Princess: Enchanted Journey, George of the Jungle,
// Trivial Pursuit: Bet You Know It; Wii ".wii" files, Papaya Studio engine), big-endian:
//   header words: [0] "vaps", [1] 0x00010003, [2] entry count, [7] data base (page size, 0x800 or 0x2000),
//   [10] size of the bucket array, [11] its offset; the {hash, size, offset} entries (12 bytes each) start right
//   after the bucket array, offsets are relative to the data base and every member sits on a 0x800 boundary.
// Members carry no names. About a fifth start with a 0x00010000 header {version, class code, hash, class code, 0, 0}
// (classes 58008910 / 58008911 / 58008912 / 58003910 ...); the rest are raw data. They are written as
// "<hash>.<class>.bin" (headered) or "<hash>.bin"; repeated hashes get "~<n>". Not decoded further.
static enumError extract_vaps_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	if (!is_ext (arg, ".wii"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	bool good = sz >= 0x100 && !memcmp (raw, "vaps", 4) && rd_be32 (raw + 4) == 0x10003;
	const u32 n = good ? rd_be32 (raw + 8) : 0;
	const u64 base = good ? rd_be32 (raw + 28) : 0;
	const u64 eo = good ? (u64)rd_be32 (raw + 44) + rd_be32 (raw + 40) : 0;
	if (!n || n > 0x100000 || !base || eo + (u64)n * 12 > sz)
		good = false;
	nintendo_sarc_entry_t *entries = 0;
	uint cnt = 0;
	if (good)
	{
		entries = CALLOC ((size_t)n + 1, sizeof (*entries));
		good = entries != 0;
	}
	for (u32 i = 0; good && i < n; i++)
	{
		const u8 *e = raw + eo + (size_t)i * 12;
		const u32 hash = rd_be32 (e), len = rd_be32 (e + 4);
		const u64 off = base + rd_be32 (e + 8);
		if (off + len > sz)
		{
			good = false;
			break;
		}
		char name[0x40];
		if (len >= 0x18 && rd_be32 (raw + off) == 0x10000)
			snprintf (name, sizeof (name), "%08x.%08x.bin", hash, rd_be32 (raw + off + 4));
		else
			snprintf (name, sizeof (name), "%08x.bin", hash);
		for (uint k = 0; k < cnt; k++)
			if (!strcmp (entries[k].name, name))
			{
				char *dot = strchr (name, '.');
				char ext[0x30] = "";
				if (dot)
				{
					snprintf (ext, sizeof (ext), "%s", dot);
					*dot = 0;
				}
				snprintf (name + strlen (name), sizeof (name) - strlen (name), "~%u%s", i, ext);
				break;
			}
		good = OwnedEntryAdd (entries, cnt, name, raw + off, len);
		if (good)
			cnt++;
	}
	enumError err = ERR_NOTHING_TO_DO;
	if (good && cnt)
	{
		// the members are opaque (undecoded) data: write them flat and do not let the generic sniffers recurse into
		// them (they would run RL/LZ "decompression" on raw blobs and leave junk files behind)
		char dest[PATH_MAX];
		beside_source_dest (dest, sizeof (dest), arg);
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT VAPS:%s (%u entries) -> %s/\n", verbose > 0 ? "\n" : "",
				testmode ? "WOULD " : "", arg, cnt, dest);
		err = ERR_OK;
		for (uint i = 0; !err && !testmode && i < cnt; i++)
		{
			char path[PATH_MAX];
			snprintf (path, sizeof (path), "%s/%s%s", dest, basedir ? basedir : "", entries[i].name);
			File_t F;
			err = CreateFileOpt (&F, true, path, false, arg);
			if (F.f && entries[i].size && fwrite (entries[i].data, 1, entries[i].size, F.f) != entries[i].size)
				err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", entries[i].size, path);
			ResetFile (&F, opt_preserve);
		}
		ResetOwnedEntries (entries, cnt);
	}
	else
		FREE (entries);
	FREE (raw);
	return err;
}
