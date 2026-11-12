// VoiceText TTS databases (".vtdb2", e.g. Tomodachi Life: Living the Dream
// VoiceText/tts_single_db(*).vtdb2), little endian: {u32 count, u32 name size (0x200), u32 key
// (10), char info[name size] ("3 12 3 0 0 <voice> <voice>-D32-FileIO")}, then `count` records {char
// name[name size], char source[name size], u32 offset, u32 size}; members follow the table back to
// back up to the end of the file. Names (build machine paths) and ".txt2" word lists are stored
// with every byte + key. Members are written as "<file>.d/<base name>" (".txt2" members un-shifted
// as "<name>.txt"); the voice data (dictionaries, hash tables, decision trees, unit database
// "p.bin") stays in the engine's own formats.

static bool vtdb2_check (const u8 *d, size_t sz, u32 *count, u32 *nl, u32 *key)
{
	if (sz < 12)
		return false;
	*count = le32 (d), *nl = le32 (d + 4), *key = le32 (d + 8);
	if (!*count || *count > 1000 || *nl < 16 || *nl > 0x1000 || !*key || *key > 0xff)
		return false;
	const u64 rs = 2ull * *nl + 8, table = 12 + (u64)*nl;
	if (table + rs * *count > sz)
		return false;
	u64 next = table + rs * *count;
	for (u32 k = 0; k < *count; k++)
	{
		const u8 *r = d + table + rs * k + 2ull * *nl;
		if (le32 (r) != next || le32 (r + 4) > sz - next)
			return false;
		next += le32 (r + 4);
	}
	return next == sz;
}

static enumError extract_vtdb2_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	if (!is_ext (arg, ".vtdb2") || (export_count == 0 && !opt_decode))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	u32 n, nl, key;
	enumError err = ERR_NOTHING_TO_DO;
	if (vtdb2_check (raw, sz, &n, &nl, &key))
	{
		char dest[PATH_MAX];
		beside_source_dest (dest, sizeof (dest), arg);
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT VTDB2:%s (%u members) -> %s/\n", verbose > 0 ? "\n" : "",
				testmode ? "WOULD " : "", arg, n, dest);
		err = ERR_OK;
		const u64 rs = 2ull * nl + 8, table = 12 + (u64)nl;
		for (u32 k = 0; k < n && !err; k++)
		{
			const u8 *r = raw + table + rs * k;
			char path[0x1001], name[0x1010];
			uint len = 0;
			while (len < nl && r[len] != (u8)key)
				len++;
			for (uint i = 0; i < len; i++)
				path[i] = (char)(r[i] - key);
			path[len] = 0;
			ccp base = strrchr (path, '/');
			base = base ? base + 1 : path;
			if (!*base || strstr (base, "..") || strchr (base, '\\'))
				snprintf (name, sizeof (name), "member%03u.bin", k);
			else
				snprintf (name, sizeof (name), "%s", base);
			const u32 off = le32 (r + 2 * nl), size = le32 (r + 2 * nl + 4);
			if (is_ext (name, ".txt2"))
			{
				u8 *t = MALLOC (size + 1);
				for (u32 i = 0; i < size; i++)
					t[i] = (u8)(raw[off + i] - key);
				name[strlen (name) - 1] = 0;
				err = shade_write (arg, dest, basedir, name, t, size);
				FREE (t);
			}
			else
				err = shade_write (arg, dest, basedir, name, raw + off, size);
		}
	}
	FREE (raw);
	return err;
}
