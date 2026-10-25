// Scribblenauts Unlimited (Wii U / PC) "index.bin" + "*.p" packages (big-endian
// on Wii U; see GMWare ScribbleUnpacker for the little-endian layout).
//   u32 count; count * { u8 pack; u8 zlevel; u32 offset; u32 stored; u32 size; }
//   u32 npacks; npacks * { u8 len; char name[len]; }
// pack 0xff means no data. zlevel 0 is raw, otherwise a zlib stream (the pack
// stores one extra trailer byte after each stream, which stored already counts).
// The table carries no names, so members are written as <index>.<ext>.
static enumError extract_scribble_file (ccp arg, ccp basedir, uint depth)
{
	if (strcmp (strrchr (arg, '/') ? strrchr (arg, '/') + 1 : arg, "index.bin"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t raw_size = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	const uint count = raw_size >= 8 ? rd_be32 (raw) : 0;
	if (!count || (raw_size - 8) / 14 < count)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	size_t p = 4 + 14ull * count;
	const uint npacks = rd_be32 (raw + p);
	p += 4;
	if (!npacks || npacks > 64)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	char dir[PATH_MAX];
	snprintf (dir, sizeof (dir), "%s", arg);
	char *sl = strrchr (dir, '/');
	if (sl)
		sl[1] = 0;
	else
		dir[0] = 0;
	FILE *pk[64] = {0};
	bool ok = true;
	for (uint i = 0; i < npacks && ok; i++)
	{
		if (p >= raw_size || p + 1 + raw[p] > raw_size)
		{
			ok = false;
			break;
		}
		char path[PATH_MAX];
		snprintf (path, sizeof (path), "%s%.*s", dir, (int)raw[p], raw + p + 1);
		p += 1 + raw[p];
		pk[i] = fopen (path, "rb");
	}
	if (!ok)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	char dest[PATH_MAX];
	beside_source_dest (dest, sizeof (dest), arg);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT SCRIBBLE:%s (%u entries) -> %s/\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", arg, count, dest);

	enumError err = ERR_OK;
	uint n_files = 0;
	for (uint i = 0; i < count && !err; i++)
	{
		const u8 *e = raw + 4 + 14ull * i;
		const uint pack = e[0], zlevel = e[1];
		const u32 off = rd_be32 (e + 2), stored = rd_be32 (e + 6), size = rd_be32 (e + 10);
		if (pack >= npacks || !pk[pack] || size > (1u << 30) || stored > (1u << 30))
			continue;
		u8 *in = MALLOC (stored ? stored : 1);
		u8 *out = zlevel ? MALLOC (size ? size : 1) : 0;
		if (!in || (zlevel && !out))
		{
			FREE (in);
			FREE (out);
			err = ERR_OUT_OF_MEMORY;
			break;
		}
		const u8 *data = in;
		if (fseeko (pk[pack], off, SEEK_SET) || fread (in, 1, stored, pk[pack]) != stored)
		{
			FREE (in);
			FREE (out);
			continue;
		}
		if (zlevel)
		{
			uLongf dl = size;
			if (uncompress (out, &dl, in, stored) != Z_OK || dl != size)
			{
				FREE (in);
				FREE (out);
				continue;
			}
			data = out;
		}
		else if (stored < size)
		{
			FREE (in);
			continue;
		}
		if (!testmode)
		{
			const char *ext = size >= 4 && !memcmp (data, "Gfx2", 4) ? "gtx" : "bin";
			char path[PATH_MAX];
			snprintf (path, sizeof (path), "%s/%s%05u.%s", dest, basedir ? basedir : "", i, ext);
			File_t F;
			err = CreateFileOpt (&F, true, path, false, arg);
			if (F.f && size && fwrite (data, 1, size, F.f) != size)
				err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", size, path);
			ResetFile (&F, opt_preserve);
		}
		n_files++;
		FREE (in);
		FREE (out);
	}
	for (uint i = 0; i < npacks; i++)
		if (pk[i])
			fclose (pk[i]);
	FREE (raw);
	if (!err && !n_files)
		return ERR_NOTHING_TO_DO;
	if (!err && !testmode)
	{
		const enumError sub = extract_tree_complete (dest, depth + 1);
		if (err < sub)
			err = sub;
	}
	return err;
}
