// RAD Game Tools / Miles Sound System Wii sound banks ("RADBFile", "*.mssbnk", Fit in Six): a database file that holds sound resources behind a string/metadata directory. Embedded "RIFF/WAVE" sound effects
// (Microsoft IMA ADPCM, format tag 0x11) are carved out by their RIFF size and written to "<file>.d/<offset>.wav" (decoded to PCM by the IMA handler); the large music streams use RAD Audio ("RADA") which is not decoded.
static enumError extract_mssbnk_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	if (!is_ext (arg, ".mssbnk"))
		return ERR_NOTHING_TO_DO;
	FILE *f = fopen (arg, "rb");
	if (!f)
		return ERR_NOTHING_TO_DO;
	enumError err = ERR_NOTHING_TO_DO;
	u8 h[16];
	struct stat st;
	if (fstat (fileno (f), &st) || fread (h, 1, 16, f) != 16 || memcmp (h, "RADBFile", 8))
		goto done;
	{
		const u64 fsz = (u64)st.st_size;
		char dest[PATH_MAX];
		beside_source_dest (dest, sizeof (dest), arg);
		uint n = 0;
		enum { BUF = 1 << 20 };
		u8 *buf = MALLOC (BUF + 64);
		if (!buf)
			goto done;
		for (u64 base = 0; base < fsz && (!err || err == ERR_NOTHING_TO_DO); base += BUF)
		{
			if (fseeko (f, (off_t)base, SEEK_SET))
				break;
			const size_t got = fread (buf, 1, BUF + 64 < fsz - base ? BUF + 64 : fsz - base, f);
			for (size_t i = 0; i + 44 <= got && i < BUF; i++)
			{
				if (buf[i] != 'R' || memcmp (buf + i, "RIFF", 4) || memcmp (buf + i + 8, "WAVEfmt ", 8) || rd_le16 (buf + i + 20) != 0x11)
					continue;
				const u32 rl = rd_le32 (buf + i + 4);
				const u64 at = base + i;
				if (rl < 36 || rl > 0x4000000 || at + 8 + rl > fsz)
					continue;
				if (!n)
				{
					if (verbose >= 0 || testmode)
						fprintf (stdlog, "%s%sEXTRACT MSSBNK:%s -> %s/\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, dest);
					err = ERR_OK;
				}
				n++;
				if (testmode)
					continue;
				u8 *w = MALLOC (rl + 8);
				if (!w || fseeko (f, (off_t)at, SEEK_SET) || fread (w, 1, rl + 8, f) != rl + 8)
				{
					FREE (w);
					break;
				}
				char path[PATH_MAX];
				snprintf (path, sizeof (path), "%s/%010llx.wav", dest, (unsigned long long)at);
				File_t F;
				err = CreateFileOpt (&F, true, path, false, arg);
				if (F.f && fwrite (w, 1, rl + 8, F.f) != rl + 8)
					err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", rl + 8, path);
				ResetFile (&F, opt_preserve);
				FREE (w);
				if (err && err != ERR_NOTHING_TO_DO)
					break;
			}
		}
		FREE (buf);
		if (n && !err && !testmode)
		{
			const enumError sub = extract_tree_complete (dest, depth + 1);
			if (err < sub)
				err = sub;
		}
	}
done:
	fclose (f);
	return err;
}
