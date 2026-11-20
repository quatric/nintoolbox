// Batman: The Brave and the Bold (Wii) lip sync tracks (".lipb", big endian). Layout: u32 0, 60 byte NUL padded name of the sound file ("f_1_012_batman.ogg"), u32 0, u32 key count N, u32 offset of
// the keys (0x4c), then N keys {f32 time in seconds, u32 viseme index 0-7}; the file ends with the last key. Written as "<file>.json" (sound name and the keys).
static enumError extract_lipb_sync_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	if (!is_ext (arg, ".lipb"))
		return ERR_NOTHING_TO_DO;
	u8 *c = 0;
	size_t cs = 0;
	if (LoadFileAlloc (arg, 0, 0, &c, &cs, 0, 0, 0, false) || !c || cs < 0x4c || rd_be32 (c) || rd_be32 (c + 0x48) != 0x4c)
	{
		FREE (c);
		return ERR_NOTHING_TO_DO;
	}
	const u32 n = rd_be32 (c + 0x44);
	if (n > 0x100000 || cs != 0x4c + 8ull * n || !memchr (c + 4, 0, 60))
	{
		FREE (c);
		return ERR_NOTHING_TO_DO;
	}
	sumo_buf_t b = { 0, 0, 0, true };
	sumo_printf (&b, "{\n\t\"format\": \"Batman BATB LIPB\",\n\t\"sound\": \"%s\",\n\t\"keys\": [", (ccp)c + 4);
	for (u32 i = 0; i < n; i++)
	{
		union
		{
			u32 u;
			float f;
		} t = { rd_be32 (c + 0x4c + 8 * i) };
		sumo_printf (&b, "%s[%.6g, %u]", i ? ", " : "", t.f, rd_be32 (c + 0x50 + 8 * i));
	}
	sumo_printf (&b, "]\n}\n");
	FREE (c);
	enumError err = ERR_NOTHING_TO_DO;
	if (b.ok && b.n && b.n < 0xffffffffu)
	{
		char dest[PATH_MAX], name[PATH_MAX];
		beside_source_dest (dest, sizeof (dest), arg);
		const char *sl = strrchr (arg, '/');
		snprintf (name, sizeof (name), "%s", sl ? sl + 1 : arg);
		char *dot = strrchr (name, '.');
		if (dot)
			*dot = 0;
		strncat (name, ".json", sizeof (name) - strlen (name) - 1);
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT LIPB:%s (%u keys) -> %s/%s\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, n, dest, name);
		err = shade_write (arg, dest, basedir, name, (u8 *)b.p, (uint)b.n);
	}
	FREE (b.p);
	return err;
}
