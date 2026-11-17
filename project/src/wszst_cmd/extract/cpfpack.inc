// Cabela's (Fun Labs / Activision, Wii) "streams/*.crs" packages: {"!CPF", u32 0x180100, u32 count} (LE) followed by count x 292-byte entries {u32 1, u32 time, u32 date,
// u32 memory size, u32 unpacked size, u32 data offset, u32 block count, u32 name hash, char name[260] (backslash paths)}. The data is a run of blocks {u32 size | 0x80000000 when
// LZO1X-compressed (stored otherwise), payload}. ".wis" sounds are DSP-ADPCM files and are renamed ".dsp" so they decode to WAV. Members are written under "<file>.d/<path>" and the usual sniffers (textures, sounds, text) run on them afterwards.
static enumError extract_cpfpack_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".crs"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
		return ERR_NOTHING_TO_DO;
	enumError err = ERR_NOTHING_TO_DO;
	if (sz < 0x130 || memcmp (raw, "!CPF", 4))
		goto done;
	{
		const u32 n = rd_le32 (raw + 8);
		if (!n || n > 1000000 || 0x0c + (u64)n * 292 > sz)
			goto done;
		char dest[PATH_MAX];
		beside_source_dest (dest, sizeof (dest), arg);
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT CPF:%s (%u files) -> %s/\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, n, dest);
		err = ERR_OK;
		for (uint i = 0; i < n && !err && !testmode; i++)
		{
			const u8 *e = raw + 0x0c + (u64)i * 292;
			const u32 usize = rd_le32 (e + 16), nblk = rd_le32 (e + 24);
			u64 off = rd_le32 (e + 20);
			char name[261];
			memcpy (name, e + 32, 260);
			name[260] = 0;
			if (!*name || off > sz || usize > 0x40000000 || !nblk || nblk > 100000)
				continue;
			for (char *p = name; *p; p++)
				if (*p == '\\')
					*p = '/';
			if (name[0] == '/' || strstr (name, ".."))
				continue;
			u8 *out = MALLOC (usize + 1);
			u64 used = 0;
			bool ok = true;
			for (uint b = 0; b < nblk && ok; b++)
			{
				if (off + 4 > sz)
				{
					ok = false;
					break;
				}
				const u32 hdr = rd_le32 (raw + off), csz = hdr & 0x7fffffff;
				off += 4;
				if (off + csz > sz)
				{
					ok = false;
					break;
				}
				if (hdr & 0x80000000)
				{
					u8 *dec = 0;
					uint dsz = 0;
					if (DecodeLZO1XGrow (&dec, &dsz, raw + off, csz) || !dec || used + dsz > usize)
					{
						FREE (dec);
						ok = false;
						break;
					}
					memcpy (out + used, dec, dsz);
					used += dsz;
					FREE (dec);
				}
				else
				{
					if (used + csz > usize)
					{
						ok = false;
						break;
					}
					memcpy (out + used, raw + off, csz);
					used += csz;
				}
				off += csz;
			}
			if (!ok || used != usize)
			{
				if (verbose >= 0)
					fprintf (stdlog, "CPF: skipping %s (%llu of %u bytes)\n", name, (unsigned long long)used, usize);
				FREE (out);
				continue;
			}
			{
				const size_t nl = strlen (name);
				if (nl > 4 && !strcasecmp (name + nl - 4, ".wis") && usize > 0x60 && rd_be32 (out + 0x10) == 2) // DSP-ADPCM file with the engine's extension
					strcpy (name + nl - 4, ".dsp");
			}
			char path[PATH_MAX];
			snprintf (path, sizeof (path), "%s/%s%s", dest, basedir ? basedir : "", name);
			File_t F;
			err = CreateFileOpt (&F, true, path, false, arg);
			if (F.f && usize && fwrite (out, 1, usize, F.f) != usize)
				err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", usize, path);
			ResetFile (&F, opt_preserve);
			FREE (out);
		}
		if (!err && !testmode)
		{
			const enumError sub = extract_tree_complete (dest, depth + 1);
			if (err < sub)
				err = sub;
		}
	}
done:
	FREE (raw);
	return err;
}
