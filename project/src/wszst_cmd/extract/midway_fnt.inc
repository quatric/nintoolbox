// Midway bitmap font metrics (".fnt", members of TNA Impact! ".WAD" packs; big endian): u16 @0x0c cell height, @0x0e line height, @0x14 name offset (0x120), @0x18 glyph table
// offset (0x134); a 256-byte character -> glyph index map @0x20 (index 1 for unmapped), the font name, then 20-byte glyph records {float u0, v0, u1, v1 (normalised atlas
// coordinates), u8 advance, pad}. The atlas is the sibling ".tim". Written as "<name>.txt" with one "char glyph u0 v0 u1 v1 advance" line per mapped character.
static enumError extract_midway_fnt_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	if (!is_ext (arg, ".fnt"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	enumError err = ERR_NOTHING_TO_DO;
	const size_t noff = sz > 0x140 ? rd_be32 (raw + 0x14) : 0, goff = sz > 0x140 ? rd_be32 (raw + 0x18) : 0;
	if (noff == 0x120 && goff == 0x134 && sz > goff + 20)
	{
		const size_t n = (sz - goff) / 20;
		char *txt = CALLOC (n * 128 + 4096, 1);
		if (txt)
		{
			size_t o = (size_t)sprintf (txt, "# font %.20s, cell height %u, line height %u, %zu glyphs\n# char glyph u0 v0 u1 v1 advance\n", (ccp)raw + noff, rd_be16 (raw + 0x0c), rd_be16 (raw + 0x0e), n);
			for (uint c = 0; c < 256; c++)
			{
				const uint gi = raw[0x20 + c];
				if (gi >= n || (gi == 1 && c < 0x20))
					continue;
				const u8 *g = raw + goff + (size_t)gi * 20;
				float f[4];
				for (uint k = 0; k < 4; k++)
				{
					const uint v = rd_be32 (g + 4 * k);
					memcpy (f + k, &v, 4);
				}
				o += (size_t)sprintf (txt + o, "%u %u %.5f %.5f %.5f %.5f %u\n", c, gi, f[0], f[1], f[2], f[3], g[16]);
			}
			char dest[PATH_MAX], name[PATH_MAX];
			beside_source_dest (dest, sizeof (dest), arg);
			const char *sl = strrchr (arg, '/');
			snprintf (name, sizeof (name), "%s", sl ? sl + 1 : arg);
			char *dot = strrchr (name, '.');
			if (dot) strcpy (dot, ".txt");
			if (verbose >= 0 || testmode)
				fprintf (stdlog, "%s%sEXTRACT MIDWAY-FNT:%s (%zu glyphs) -> %s/%s\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, n, dest, name);
			err = shade_write (arg, dest, basedir, name, (u8 *)txt, (uint)o);
			FREE (txt);
		}
	}
	FREE (raw);
	return err;
}
