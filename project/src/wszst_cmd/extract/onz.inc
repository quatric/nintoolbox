// Sonic Team "ONE" archives (Sonic Unleashed, Wii: "one_wii*/.../*.onz"; the whole file is one Nintendo LZ11 stream, the nested "*.one" archives are the same layout uncompressed). The data is
// an 8-byte header {"one.", u8 entry count, 3 zero bytes} and 0x40-byte records {char name[0x38] (NUL padded), u32 LE offset, u32 LE size} from offset 8 (the first
// payload starts right after the table; payloads are 0x20 aligned), holding GVR textures, "gncp" scene data and similar members.
// Members are written under their name ("~<n>" before the extension for repeated names) and recursed into.
static enumError extract_onz_file (ccp arg, ccp basedir, uint depth)
{
	const bool packed = is_ext (arg, ".onz");
	if (!packed && !is_ext (arg, ".one"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0, *dec = 0;
	size_t sz = 0;
	uint dsz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
		return ERR_NOTHING_TO_DO;
	enumError err = ERR_NOTHING_TO_DO;
	if (!packed)
	{
		dec = raw;
		dsz = (uint)sz;
		raw = 0;
	}
	else if (sz >= 8 && raw[0] == 0x11)
		DecodeLZ10LZ11 (&dec, &dsz, raw, (uint)sz);
	if (!dec || dsz < 0x8 || memcmp (dec, "one.", 4) || dec[5] || dec[6] || dec[7] || !dec[4])
		goto done;
	{
		const uint n = dec[4];
		if (8 + 0x40 * (u64)n > dsz)
			goto done;
		for (uint i = 0; i < n; i++)
		{
			const u8 *e = dec + 8 + 0x40 * (size_t)i;
			const u64 o = rd_le32 (e + 0x38), s = rd_le32 (e + 0x3c);
			if (!e[0] || (s && o < 8 + 0x40 * (u64)n) || o + s > dsz)
				goto done;
			uint k = 0;
			while (k < 0x38 && e[k])
				k++;
			if (k == 0x38)
				goto done;
			for (uint j = 0; j < k; j++)
				if (e[j] < 0x20 || e[j] > 0x7e || e[j] == '/' || e[j] == '\\')
					goto done;
		}
		char dest[PATH_MAX];
		beside_source_dest (dest, sizeof (dest), arg);
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT ONZ:%s (%u entries) -> %s/\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, n, dest);
		err = ERR_OK;
		for (uint i = 0; i < n && !err && !testmode; i++)
		{
			const u8 *e = dec + 8 + 0x40 * (size_t)i;
			const u32 o = rd_le32 (e + 0x38), s = rd_le32 (e + 0x3c);
			char path[PATH_MAX];
			uint dup = 0;
			for (uint j = 0; j < i; j++)
				if (!strcmp ((ccp)(dec + 8 + 0x40 * (size_t)j), (ccp)e))
					dup++;
			if (dup)
			{
				// repeated names get a "~<n>" before the extension
				ccp dot = strrchr ((ccp)e, '.');
				const int stem = dot ? (int)(dot - (ccp)e) : (int)strlen ((ccp)e);
				snprintf (path, sizeof (path), "%s/%s%.*s~%u%s", dest, basedir ? basedir : "", stem, (ccp)e, dup, dot ? dot : "");
			}
			else
				snprintf (path, sizeof (path), "%s/%s%s", dest, basedir ? basedir : "", (ccp)e);
			File_t F;
			err = CreateFileOpt (&F, true, path, false, arg);
			if (F.f && s && fwrite (dec + o, 1, s, F.f) != s)
				err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", s, path);
			ResetFile (&F, opt_preserve);
		}
		if (!err && !testmode)
		{
			enumError sub = extract_tree_complete (dest, depth + 1);
			if (err < sub)
				err = sub;
		}
	}
done:
	FREE (dec);
	FREE (raw);
	return err;
}
