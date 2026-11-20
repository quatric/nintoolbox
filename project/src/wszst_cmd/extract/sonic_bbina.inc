// Sonic Team "BBINA" data files (Sonic Colors, Wii: object placement ".orc", ".anm", some ".bin"; 877 files of the retail disc, big endian). Layout: a 0x20 byte header {u32 file size, u32 data size, u32
// pointer count (not always exact), 0, 0, u16 0, u16 0x3142 (version "1B"), "BINA"... at 0x18, 0} followed by the data (from 0x20, data size bytes) and a pointer relocation stream: one variable length
// delta per pointer counted in 4 byte words from the previous pointer slot (first byte: top 2 bits 1 = 6 bit, 2 = 14 bit, 3 = 30 bit delta, 0 = end). Every pointer slot holds an offset relative to the data
// start. Written as "<file>.json": the header words, all data words, the pointer slots (word indices) and the text strings the pointers lead to.
static enumError extract_sonic_bbina_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	if (!is_ext (arg, ".orc") && !is_ext (arg, ".anm") && !is_ext (arg, ".bin"))
		return ERR_NOTHING_TO_DO;
	u8 *c = 0;
	size_t cs = 0;
	if (LoadFileAlloc (arg, 0, 0, &c, &cs, 0, 0, 0, false) || !c || cs < 0x28 || memcmp (c + 0x18, "BINA", 4) || rd_be32 (c) != cs)
	{
		FREE (c);
		return ERR_NOTHING_TO_DO;
	}
	const size_t dsz = rd_be32 (c + 4), rs = 0x20 + dsz;
	if (dsz < 4 || (dsz & 3) || rs > cs)
	{
		FREE (c);
		return ERR_NOTHING_TO_DO;
	}
	u32 *slot = MALLOC (sizeof (u32) * (cs - rs + 1));
	uint np = 0;
	bool ok = slot != 0;
	size_t i = rs, pos = 0;
	while (ok && i < cs)
	{
		const uint t = c[i] >> 6;
		size_t d = c[i] & 0x3f;
		if (!t)
			break;
		if (t == 1)
			i++;
		else if (t == 2 && i + 2 <= cs)
		{
			d = (d << 8) | c[i + 1];
			i += 2;
		}
		else if (t == 3 && i + 4 <= cs)
		{
			d = (d << 24) | ((size_t)c[i + 1] << 16) | ((size_t)c[i + 2] << 8) | c[i + 3];
			i += 4;
		}
		else
			ok = false;
		pos += d;
		ok = ok && 4 * pos + 4 <= dsz && rd_be32 (c + 0x20 + 4 * pos) <= dsz;
		if (ok)
			slot[np++] = (u32)pos;
	}
	if (!ok)
	{
		FREE (slot);
		FREE (c);
		return ERR_NOTHING_TO_DO;
	}
	sumo_buf_t b = { 0, 0, 0, true };
	sumo_printf (&b, "{\n\t\"format\": \"Sonic Team BBINA\",\n\t\"header\": [");
	for (uint k = 0; k < 8; k++)
		sumo_printf (&b, "%s%u", k ? ", " : "", rd_be32 (c + 4 * k));
	sumo_printf (&b, "],\n\t\"words\": [");
	for (size_t k = 0; k < dsz / 4; k++)
		sumo_printf (&b, "%s%u", k ? ", " : "", rd_be32 (c + 0x20 + 4 * k));
	sumo_printf (&b, "],\n\t\"pointers\": [");
	for (uint k = 0; k < np; k++)
		sumo_printf (&b, "%s%u", k ? ", " : "", slot[k]);
	sumo_printf (&b, "],\n\t\"strings\": [");
	uint ns = 0;
	for (uint k = 0; k < np; k++)
	{
		const size_t to = rd_be32 (c + 0x20 + 4 * (size_t)slot[k]);
		size_t n = 0;
		while (to + n < dsz && n < 200 && c[0x20 + to + n] >= 0x20 && c[0x20 + to + n] < 0x7f)
			n++;
		if (n && to + n < dsz && !c[0x20 + to + n])
			sumo_printf (&b, "%s{\"slot\": %u, \"offset\": %zu, \"text\": \"%.*s\"}", ns++ ? ", " : "", slot[k], to, (int)n, (ccp)c + 0x20 + to);
	}
	sumo_printf (&b, "]\n}\n");
	FREE (slot);
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
			strcpy (dot, ".json");
		else
			strcat (name, ".json");
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT SONIC-BBINA:%s (%u pointers, %u strings) -> %s/%s\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, np, ns, dest, name);
		err = shade_write (arg, dest, basedir, name, (u8 *)b.p, (uint)b.n);
	}
	FREE (b.p);
	return err;
}
