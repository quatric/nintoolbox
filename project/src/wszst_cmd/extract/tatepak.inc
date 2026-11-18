// Titeuf - Le Film "*.PAKz" / "*.PAK": a PAKz is "zst1", 8 more header bytes and a zlib stream (from +12) holding a PAK. The PAK is "TATE", le32 total size, le32 item count, le32 0, name[0xf0] at +0x10 (up to 0x100), then items every 0x80-aligned: {"item", le32 size, le32 index, le32 0, char name[0x70], data[size]} (header 0x80). Members are written to "<file>.d/<name>".
static enumError extract_tatepak_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	(void)depth;
	const bool z = is_ext (arg, ".pakz");
	if (!z && !is_ext (arg, ".pak"))
		return ERR_NOTHING_TO_DO;
	FILE *f = fopen (arg, "rb");
	if (!f)
		return ERR_NOTHING_TO_DO;
	enumError err = ERR_NOTHING_TO_DO;
	u8 *raw = 0, *d = 0;
	if (fseeko (f, 0, SEEK_END))
		goto done;
	{
		const long sz = ftell (f);
		if (sz < 0x100 || sz > 0x40000000 || fseeko (f, 0, SEEK_SET))
			goto done;
		raw = MALLOC ((size_t)sz);
		if (fread (raw, 1, (size_t)sz, f) != (size_t)sz)
			goto done;
		size_t dsz = (size_t)sz;
		if (z)
		{
			if (memcmp (raw, "zst1", 4) || sz < 20)
				goto done;
			size_t cap = (size_t)sz * 4;
			d = MALLOC (cap);
			z_stream zs;
			memset (&zs, 0, sizeof (zs));
			if (inflateInit (&zs) != Z_OK)
				goto done;
			zs.next_in = raw + 12;
			zs.avail_in = (uInt)(sz - 12);
			int zr;
			for (;;)
			{
				zs.next_out = d + zs.total_out;
				zs.avail_out = (uInt)(cap - zs.total_out);
				zr = inflate (&zs, Z_NO_FLUSH);
				if (zr != Z_OK || zs.avail_out)
					break;
				if (cap > 0x40000000)
					break;
				cap *= 2;
				d = REALLOC (d, cap);
			}
			dsz = zs.total_out;
			inflateEnd (&zs);
			if (zr != Z_STREAM_END)
				goto done;
		}
		else
		{
			d = raw;
			raw = 0;
		}
		if (dsz < 0x100 || memcmp (d, "TATE", 4))
			goto done;
		const u32 cnt = rd_le32 (d + 8);
		if (!cnt || cnt > 100000)
			goto done;
		size_t pos = 0x100;
		for (uint i = 0; i < cnt; i++)
		{
			if (pos + 0x80 > dsz || memcmp (d + pos, "item", 4))
				goto done;
			const u32 s = rd_le32 (d + pos + 4);
			if (pos + 0x80 + s > dsz || !memchr (d + pos + 0x10, 0, 0x70))
				goto done;
			pos = (pos + 0x80 + s + 0x7f) & ~(size_t)0x7f;
		}
		char dest[PATH_MAX];
		beside_source_dest (dest, sizeof (dest), arg);
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT TATEPAK:%s (%u items) -> %s/\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, cnt, dest);
		err = ERR_OK;
		pos = 0x100;
		for (uint i = 0; i < cnt && !err; i++)
		{
			const u32 s = rd_le32 (d + pos + 4);
			const char *nm = (const char *)d + pos + 0x10;
			const u8 *data = d + pos + 0x80;
			pos = (pos + 0x80 + s + 0x7f) & ~(size_t)0x7f;
			if (testmode)
				continue;
			char rel[0x80];
			uint k = 0;
			for (const char *c = nm; *c == '\\' || *c == '/'; c++)
				nm = c + 1;
			for (const char *c = nm; *c && k < sizeof (rel) - 1; c++)
				rel[k++] = *c == '\\' ? '/' : *c;
			rel[k] = 0;
			if (!k || rel[0] == '/' || strstr (rel, ".."))
				snprintf (rel, sizeof (rel), "item%04u", i);
			char path[PATH_MAX];
			snprintf (path, sizeof (path), "%s/%s", dest, rel);
			File_t F;
			err = CreateFileOpt (&F, true, path, false, arg);
			if (F.f && s && fwrite (data, 1, s, F.f) != s)
				err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", s, path);
			ResetFile (&F, opt_preserve);
		}
	}
done:
	FREE (raw);
	FREE (d);
	fclose (f);
	return err;
}
