// Manhunt 2 (Wii) world geometry (".bsp", the unpacked "WRLD" of an MH2Z file, big endian), vertices only. The world references vertex buffers through 24-byte records {u16 id, u16 users,
// u32 size, u32 data offset, 2 x u32 0}; a buffer is an array of 16-byte entries {f32 x, y, z, 0}. Every buffer found this way is written as one "o" group of "v" lines to
// "<name>.obj". The polygon and index tables of the world are not decoded, so the OBJ holds points and no faces.
static enumError extract_mh2_wrld_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".bsp"))
		return ERR_NOTHING_TO_DO;
	u8 *c = 0;
	size_t cs = 0;
	if (LoadFileAlloc (arg, 0, 0, &c, &cs, 0, 0, 0, false) || !c || cs < 0x100 || memcmp (c, "WRLD", 4))
	{
		FREE (c);
		return ERR_NOTHING_TO_DO;
	}
	sumo_buf_t b = { 0, 0, 0, true };
	uint groups = 0;
	size_t lim = rd_be32 (c + 8);
	if (lim > cs || lim < 0x100)
		lim = cs;
	for (size_t o = 0x40; o + 24 <= lim; o += 4)
	{
		const u32 size = rd_be32 (c + o + 4), off = rd_be32 (c + o + 8);
		if (!size || size % 16 || size > 0x400000 || off < 0x40 || (size_t)off + size > cs || rd_be32 (c + o + 12) || rd_be32 (c + o + 16))
			continue;
		bool good = true;
		for (u32 i = 0; i < size && good; i += 16)
		{
			const u8 *v = c + off + i;
			if (rd_be32 (v + 12))
				good = false;
			for (int k = 0; k < 3 && good; k++)
			{
				const float f = mh2inst_f (v + 4 * k, true);
				if (!(f > -1e5f && f < 1e5f))
					good = false;
			}
		}
		if (!good)
			continue;
		sumo_printf (&b, "o buffer%04u_%08x\n", groups++, off);
		for (u32 i = 0; i < size; i += 16)
			sumo_printf (&b, "v %g %g %g\n", mh2inst_f (c + off + i, true), mh2inst_f (c + off + i + 4, true), mh2inst_f (c + off + i + 8, true));
	}
	FREE (c);
	enumError err = ERR_NOTHING_TO_DO;
	if (b.ok && groups && b.n < 0xffffffffu)
	{
		char name[PATH_MAX];
		const char *sl = strrchr (arg, '/');
		snprintf (name, sizeof (name), "%s", sl ? sl + 1 : arg);
		char *dot = strrchr (name, '.');
		if (dot)
			*dot = 0;
		strcat (name, ".obj");
		nintendo_sarc_entry_t *entries = CALLOC (1, sizeof (*entries));
		if (entries && OwnedEntryAdd (entries, 0, name, (u8 *)b.p, (uint)b.n))
			err = write_owned_entries (arg, basedir, depth, "MH2-WRLD", entries, 1);
		else
			FREE (entries);
	}
	else
		FREE (b.p);
	return err;
}
