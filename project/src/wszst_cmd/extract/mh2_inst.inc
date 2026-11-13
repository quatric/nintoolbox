// Rockstar Leeds entity placement lists (Manhunt 2 ".inst"; big endian on the Wii disc, little endian in the PC copy): u32 entity count, u32 record size per entity, then the records
// back to back (the sizes add up to the file). A record starts with two NUL-terminated strings (entity type, instance name), each padded with 'p' bytes to 4 bytes, followed by the
// position (3 x f32) and the rotation quaternion (4 x f32); property tables follow (not decoded). Written as "<name>.csv" (type, name, position, quaternion).
static float mh2inst_f (const u8 *p, bool be)
{
	const uint v = be ? rd_be32 (p) : rd_le32 (p);
	float f;
	memcpy (&f, &v, 4);
	return f;
}

static enumError extract_mh2_inst_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	if (!is_ext (arg, ".inst"))
		return ERR_NOTHING_TO_DO;
	u8 *c = 0;
	size_t cs = 0;
	if (LoadFileAlloc (arg, 0, 0, &c, &cs, 0, 0, 0, false) || !c || cs < 16)
	{
		FREE (c);
		return ERR_NOTHING_TO_DO;
	}
	bool be = true;
	uint n = rd_be32 (c);
	if (!n || n > 100000 || 4 + 4 * (size_t)n > cs)
	{
		be = false;
		n = rd_le32 (c);
	}
	if (!n || n > 100000 || 4 + 4 * (size_t)n > cs)
	{
		FREE (c);
		return ERR_NOTHING_TO_DO;
	}
	size_t total = 4 + 4 * (size_t)n;
	for (uint i = 0; i < n; i++)
		total += be ? rd_be32 (c + 4 + 4 * i) : rd_le32 (c + 4 + 4 * i);
	if (total != cs)
	{
		FREE (c);
		return ERR_NOTHING_TO_DO;
	}
	sumo_buf_t b = { 0, 0, 0, true };
	sumo_printf (&b, "type,name,x,y,z,qx,qy,qz,qw\n");
	size_t p = 4 + 4 * (size_t)n;
	uint ok = 0;
	for (uint i = 0; i < n; i++)
	{
		const size_t rs = be ? rd_be32 (c + 4 + 4 * i) : rd_le32 (c + 4 + 4 * i);
		const u8 *r = c + p;
		p += rs;
		size_t o = 0;
		const char *s[2];
		bool good = true;
		for (uint k = 0; k < 2 && good; k++)
		{
			size_t l = 0;
			while (o + l < rs && r[o + l])
				l++;
			if (o + l >= rs)
			{
				good = false;
				break;
			}
			s[k] = (ccp)r + o;
			o += (l + 1 + 3) & ~(size_t)3;
		}
		if (!good || o + 28 > rs)
			continue;
		sumo_printf (&b, "%.60s,%.60s,%g,%g,%g,%g,%g,%g,%g\n", s[0], s[1], mh2inst_f (r + o, be), mh2inst_f (r + o + 4, be), mh2inst_f (r + o + 8, be), mh2inst_f (r + o + 12, be), mh2inst_f (r + o + 16, be), mh2inst_f (r + o + 20, be), mh2inst_f (r + o + 24, be));
		ok++;
	}
	FREE (c);
	enumError err = ERR_NOTHING_TO_DO;
	if (b.ok && ok && b.n < 0xffffffffu)
	{
		char dest[PATH_MAX], name[PATH_MAX];
		beside_source_dest (dest, sizeof (dest), arg);
		const char *sl = strrchr (arg, '/');
		snprintf (name, sizeof (name), "%s", sl ? sl + 1 : arg);
		char *dot = strrchr (name, '.');
		if (dot)
			strcpy (dot, ".csv");
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT MH2-INST:%s (%u entities) -> %s/%s\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, ok, dest, name);
		err = shade_write (arg, dest, basedir, name, (u8 *)b.p, (uint)b.n);
	}
	FREE (b.p);
	return err;
}
