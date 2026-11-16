// Bleach: Shattered Blade (Wii, Polygon Magic / Sega GVR textures) "*.sms" containers: "SMST", u32 body size, u32 entry count, 0x20-byte header, then
//   tagged entries ("GCIX", "NGIF" models, "ANMX" animations, "KAZ", "LPS" lip sync, nested "SMST"...). The texture packs hold GCIX entries
//   {"GCIX", u32 8, 8 bytes 0, then a plain Sega "GVRT" texture {"GVRT", u32 LE size, ...}}, padded to 16 bytes between entries.
// Every GCIX/GVRT texture is written as "<NNN>.gvr" (a complete GCIX+GVRT file, which the GVR decoder then turns into PNG); the model, animation and lip-sync
// entries are not decoded.
static enumError extract_smsgvr_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".sms"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	enumError err = ERR_NOTHING_TO_DO;
	if (sz < 0x60 || memcmp (raw, "SMST", 4))
		goto done;
	{
		enum { MAXT = 4096 };
		size_t *pos = CALLOC (MAXT, 2 * sizeof (size_t));
		uint n = 0;
		if (!pos)
			goto done;
		for (size_t p = 0x20; p + 0x20 <= sz && n < MAXT;)
		{
			if (!memcmp (raw + p, "GCIX", 4) && rd_le32 (raw + p + 4) == 8 && !memcmp (raw + p + 0x10, "GVRT", 4))
			{
				const size_t l = (size_t)rd_le32 (raw + p + 0x14) + 8;
				if (l >= 0x10 && p + 0x10 + l <= sz)
				{
					pos[n * 2] = p;
					pos[n * 2 + 1] = l + 0x10;
					n++;
					p += 0x10 + l;
					continue;
				}
			}
			p += 0x10;
		}
		if (n)
		{
			char dest[PATH_MAX];
			beside_source_dest (dest, sizeof (dest), arg);
			if (verbose >= 0 || testmode)
				fprintf (stdlog, "%s%sEXTRACT SMS:%s (%u textures) -> %s/\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, n, dest);
			err = ERR_OK;
			for (uint i = 0; i < n && !err && !testmode; i++)
			{
				char path[PATH_MAX];
				snprintf (path, sizeof (path), "%s/%s%03u.gvr", dest, basedir ? basedir : "", i);
				File_t F;
				err = CreateFileOpt (&F, true, path, false, arg);
				if (F.f && fwrite (raw + pos[i * 2], 1, pos[i * 2 + 1], F.f) != pos[i * 2 + 1])
					err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", (uint)pos[i * 2 + 1], path);
				ResetFile (&F, opt_preserve);
			}
			if (!err && !testmode)
			{
				const enumError sub = extract_tree_complete (dest, depth + 1);
				if (err < sub)
					err = sub;
			}
		}
		FREE (pos);
	}
done:
	FREE (raw);
	return err;
}
