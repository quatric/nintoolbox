// Heatseeker (Wii) "Sound/Wii/*.ffs" archives: little-endian {"FFS ", u32 count} + count * {u32 name length, name, u32 file offset, u32 size}; the payloads (FMOD "FSB3" banks and small ".DAT" tables) are stored
// back to back after the directory and are written to "<file>.d/<name>"; FSB banks are then extracted by the existing FSB handler.
static enumError extract_ffsarc_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	u8 *raw = 0;
	size_t sz = 0;
	if (!is_ext (arg, ".ffs") || LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
		return ERR_NOTHING_TO_DO;
	enumError err = ERR_NOTHING_TO_DO;
	if (sz < 0x20 || memcmp (raw, "FFS ", 4))
		goto done;
	{
		const u32 count = rd_le32 (raw + 4);
		if (!count || count > 10000)
			goto done;
		size_t o = 8;
		uint ok = 0;
		for (uint i = 0; i < count; i++, ok++)
		{
			if (o + 4 > sz)
				break;
			const u32 nl = rd_le32 (raw + o);
			if (!nl || nl > 200 || o + 4 + nl + 8 > sz)
				break;
			const u32 off = rd_le32 (raw + o + 4 + nl), len = rd_le32 (raw + o + 8 + nl);
			if ((u64)off + len > sz)
				break;
			o += 12 + nl;
		}
		if (ok != count)
			goto done;
		char dest[PATH_MAX];
		beside_source_dest (dest, sizeof (dest), arg);
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT FFS:%s (%u files) -> %s/\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, count, dest);
		err = ERR_OK;
		o = 8;
		for (uint i = 0; i < count && !err && !testmode; i++)
		{
			const u32 nl = rd_le32 (raw + o);
			char name[256];
			for (uint k = 0; k < nl; k++)
			{
				const u8 c = raw[o + 4 + k];
				name[k] = c == '/' || c == '\\' || c < 0x20 ? '_' : c;
			}
			name[nl] = 0;
			if (!strcmp (name, ".") || !strcmp (name, ".."))
				name[0] = '_';
			const u32 off = rd_le32 (raw + o + 4 + nl), len = rd_le32 (raw + o + 8 + nl);
			o += 12 + nl;
			char path[PATH_MAX];
			snprintf (path, sizeof (path), "%s/%s", dest, name);
			File_t F;
			err = CreateFileOpt (&F, true, path, false, arg);
			if (F.f && fwrite (raw + off, 1, len, F.f) != len)
				err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", len, path);
			ResetFile (&F, opt_preserve);
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
