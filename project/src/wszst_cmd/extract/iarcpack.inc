// "IARC" archive (*.iaa, *.arc, *.wad; Benjamin Blumchen: Törööö im Zoo, Wii, and sibling Tierpension-engine titles): big-endian {"IARC", u32 file size, u32 count, u32 0x80, u32 data base, u32 1, u32 0x3400, u32 ?} followed by
// a Nintendo LZ11 stream (type byte 0x11 + 24-bit LE size) at 0x20 holding the directory: 0x48-byte records {u32 offset (from the data base), u32 compressed size, u32 unpacked size, name[60]}. Every
// member is itself an LZ11 stream at base + offset; members are written decompressed to "<file>.d/<name>" and extracted recursively (".wax" = RIFX WAVE, ".ogg", ...).
static enumError extract_iarcpack_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".iaa") && !is_ext (arg, ".arc") && !is_ext (arg, ".wad"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0, *dir = 0;
	size_t sz = 0;
	uint dsz = 0;
	enumError err = ERR_NOTHING_TO_DO;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
		return ERR_NOTHING_TO_DO;
	if (sz < 0x40 || memcmp (raw, "IARC", 4) || rd_be32 (raw + 4) != sz || raw[0x20] != 0x11)
		goto done;
	{
		const u32 count = rd_be32 (raw + 8), base = rd_be32 (raw + 0x10);
		if (!count || count > 100000 || base >= sz || DecodeLZ10LZ11 (&dir, &dsz, raw + 0x20, (uint)(sz - 0x20)) || !dir || dsz < (u64)count * 0x48)
			goto done;
		char dest[PATH_MAX];
		beside_source_dest (dest, sizeof (dest), arg);
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT IARC:%s (%u files) -> %s/\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, count, dest);
		err = ERR_OK;
		for (uint i = 0; i < count && !err && !testmode; i++)
		{
			const u8 *r = dir + (size_t)i * 0x48;
			const u32 off = rd_be32 (r), csz = rd_be32 (r + 4), rsz = rd_be32 (r + 8);
			char name[64];
			memcpy (name, r + 12, 60);
			name[60] = 0;
			if (!name[0] || strstr (name, "..") || name[0] == '/' || (u64)base + off + csz > sz || !csz)
				continue;
			u8 *m = 0;
			uint msz = 0;
			if (raw[base + off] == 0x11 || raw[base + off] == 0x10)
			{
				if (DecodeLZ10LZ11 (&m, &msz, raw + base + off, csz) || !m)
				{
					FREE (m);
					continue;
				}
			}
			else
			{
				m = MALLOC (csz);
				if (!m)
					continue;
				memcpy (m, raw + base + off, csz);
				msz = csz;
			}
			if (msz > rsz && rsz)
				msz = rsz;
			char path[PATH_MAX];
			snprintf (path, sizeof (path), "%s/%s%s", dest, basedir ? basedir : "", name);
			File_t F;
			err = CreateFileOpt (&F, true, path, false, arg);
			if (F.f && msz && fwrite (m, 1, msz, F.f) != msz)
				err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", msz, path);
			ResetFile (&F, opt_preserve);
			FREE (m);
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
	FREE (dir);
	return err;
}
