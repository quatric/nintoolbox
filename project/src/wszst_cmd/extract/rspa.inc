// Dragon Quest Monster Battle Road Victory (Wii) "RSPA/*.RSPA" resource packs: little-endian {"RSPA", u32 count, u32 size (about the file size, may be a few bytes more)} + count * {u32 tag, u32 offset} (the member length is the distance to the next offset, the last member ends at
// the file end). Members are BRRES models/textures ("bres"), "RSM"/"RST"/"RSA" effect resources and "rss" sets; each is written to "<file>.d/<tag>.<ext>" (".brres" members are then extracted by the existing BRRES handler).
static enumError extract_rspa_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
		return ERR_NOTHING_TO_DO;
	enumError err = ERR_NOTHING_TO_DO;
	if (sz < 0x20 || memcmp (raw, "RSPA", 4) || rd_le32 (raw + 8) > sz + 0x100 || rd_le32 (raw + 8) + 0x100 < sz)
		goto done;
	{
		const u32 count = rd_le32 (raw + 4);
		if (!count || count > 8192 || 12 + (u64)count * 8 > sz)
			goto done;
		const u32 first = 12 + count * 8;
		u32 *offs = (u32 *)CALLOC (count + 1, sizeof (u32));
		if (!offs)
			goto done;
		bool ok = true;
		for (uint i = 0; i < count && ok; i++)
		{
			offs[i] = rd_le32 (raw + 12 + i * 8 + 4);
			ok = offs[i] >= first && offs[i] < sz;
		}
		if (ok)
		{
			char dest[PATH_MAX];
			beside_source_dest (dest, sizeof (dest), arg);
			if (verbose >= 0 || testmode)
				fprintf (stdlog, "%s%sEXTRACT RSPA:%s (%u members) -> %s/\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, count, dest);
			err = ERR_OK;
			for (uint i = 0; i < count && !err && !testmode; i++)
			{
				const u32 tag = rd_le32 (raw + 12 + i * 8), off = offs[i];
				u32 end = sz;
				for (uint k = 0; k < count; k++)
					if (offs[k] > off && offs[k] < end)
						end = offs[k];
				const u8 *p = raw + off;
				ccp ext = "bin";
				if (end - off >= 4)
				{
					if (!memcmp (p, "bres", 4))
						ext = "brres";
					else if (!memcmp (p, "RSM\0", 4))
						ext = "rsm";
					else if (!memcmp (p, "RST\0", 4))
						ext = "rst";
					else if (!memcmp (p, "RSA\0", 4))
						ext = "rsa";
					else if (!memcmp (p, "rss\0", 4))
						ext = "rss";
				}
				char path[PATH_MAX];
				snprintf (path, sizeof (path), "%s/%08x.%s", dest, tag, ext);
				File_t F;
				err = CreateFileOpt (&F, true, path, false, arg);
				if (F.f && fwrite (p, 1, end - off, F.f) != end - off)
					err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", end - off, path);
				ResetFile (&F, opt_preserve);
			}
			if (!err && !testmode)
			{
				const enumError sub = extract_tree_complete (dest, depth + 1);
				if (err < sub)
					err = sub;
			}
		}
		FREE (offs);
	}
done:
	FREE (raw);
	return err;
}
