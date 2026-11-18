// Dead Rising: Chop Till You Drop (Wii) "*.efa" effect archives: be32 0x00414645 ("\0AFE"), be32 0x40 (first group), be32 end of the first group, ... Each group at P is be32 count, 12 zero bytes and count 24-byte entries {u32 offset, u32 size, char name[16]}; the data follows the entry list (0x40 aligned) and follows in order (gaps below 0x40 bytes) up to the group end; the next group starts there (aligned to 0x10). Members are written to "<file>.d/GG_NNN_<name>.<ext>" (bres -> ".brres" and recursed, "KTPP" -> ".ktpp").
static enumError extract_efa_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	if (!is_ext (arg, ".efa"))
		return ERR_NOTHING_TO_DO;
	FILE *f = fopen (arg, "rb");
	if (!f)
		return ERR_NOTHING_TO_DO;
	enumError err = ERR_NOTHING_TO_DO;
	u8 *d = 0;
	if (fseeko (f, 0, SEEK_END))
		goto done;
	{
		const long sz = ftell (f);
		if (sz < 0x100 || sz > 0x40000000 || fseeko (f, 0, SEEK_SET))
			goto done;
		d = MALLOC ((size_t)sz);
		if (fread (d, 1, (size_t)sz, f) != (size_t)sz)
			goto done;
		if (be32 (d) != 0x00414645 || be32 (d + 4) != 0x40)
			goto done;
		// validate and count all groups first
		uint groups = 0, total = 0;
		size_t P = 0x40;
		u32 gstart[16], gcount[16];
		while (P + 0x10 < (size_t)sz && groups < 16)
		{
			const u32 n = be32 (d + P);
			if (!n || n > 4096 || P + 0x10 + 24 * (size_t)n > (size_t)sz)
				break;
			u64 next = 0, end = 0;
			bool ok = true;
			for (uint i = 0; i < n; i++)
			{
				const u8 *e = d + P + 0x10 + 24 * i;
				const u32 o = be32 (e), s = be32 (e + 4);
				if (i == 0)
					next = o;
				if (o < next || o > next + 0x3f || (u64)o + s > (u64)sz || o < P + 0x10 + 24 * (size_t)n)
				{
					ok = false;
					break;
				}
				next = (u64)o + s;
				end = next;
			}
			if (!ok)
				break;
			gstart[groups] = (u32)P;
			gcount[groups++] = n;
			total += n;
			P = (size_t)((end + 0xf) & ~(u64)0xf);
		}
		if (!groups || P + 0x20 < (size_t)sz)
			goto done;
		char dest[PATH_MAX];
		beside_source_dest (dest, sizeof (dest), arg);
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT EFA:%s (%u groups, %u files) -> %s/\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, groups, total, dest);
		err = ERR_OK;
		for (uint g = 0; g < groups && !err && !testmode; g++)
			for (uint i = 0; i < gcount[g] && !err; i++)
			{
				const u8 *e = d + gstart[g] + 0x10 + 24 * i;
				const u32 o = be32 (e), s = be32 (e + 4);
				char nm[17];
				memcpy (nm, e + 8, 16);
				nm[16] = 0;
				for (char *c = nm; *c; c++)
					if (*c == '/' || *c == '\\' || *c < 0x20)
						*c = '_';
				ccp ext = s >= 4 && !memcmp (d + o, "bres", 4) ? "brres" : s >= 4 && !memcmp (d + o, "KTPP", 4) ? "ktpp" : "bin";
				char path[PATH_MAX];
				snprintf (path, sizeof (path), "%s/%02u_%03u_%s.%s", dest, g, i, nm, ext);
				File_t F;
				err = CreateFileOpt (&F, true, path, false, arg);
				if (F.f && s && fwrite (d + o, 1, s, F.f) != s)
					err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", s, path);
				ResetFile (&F, opt_preserve);
				if (!err && !strcmp (ext, "brres"))
					extract_one_file (path, 0, depth + 1);
			}
	}
done:
	FREE (d);
	fclose (f);
	return err;
}
