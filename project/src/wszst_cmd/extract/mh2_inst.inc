// Rockstar Leeds entity placement lists (Manhunt 2 ".inst"; big endian on the Wii disc, little endian in the PC copy): u32 entity count, u32 record size per entity, then the records
// back to back (the sizes add up to the file). A record starts with two NUL-terminated strings (entity type, instance name), each padded with 'p' bytes to 4 bytes, followed by the
// position (3 x f32), the rotation quaternion (4 x f32), the entity class string and a list of typed parameters {u32 name hash, 4-byte type "flo"/"boo"/"int"/"str", value (4 bytes, or a
// padded string)} up to the end of the record (layout as documented by the Manhunt modding community). Written into "<name>.inst.d": "<name>.csv" (type, name, position, quaternion, class)
// and "<name>_params.csv" (entity index, parameter hash, type, value; the hash-to-name table is not included).
static float mh2inst_f (const u8 *p, bool be)
{
	const uint v = be ? rd_be32 (p) : rd_le32 (p);
	float f;
	memcpy (&f, &v, 4);
	return f;
}

static enumError extract_mh2_inst_file (ccp arg, ccp basedir, uint depth)
{
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
	sumo_printf (&b, "type,name,x,y,z,qx,qy,qz,qw,class\n");
	sumo_buf_t pb = { 0, 0, 0, true };
	sumo_printf (&pb, "entity,hash,type,value\n");
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
		size_t po = o + 28;
		char cls[64] = "";
		{
			size_t l = 0;
			while (po + l < rs && r[po + l])
				l++;
			if (po + l < rs)
			{
				snprintf (cls, sizeof cls, "%.60s", (ccp)r + po);
				po += (l + 1 + 3) & ~(size_t)3;
			}
		}
		sumo_printf (&b, "%.60s,%.60s,%g,%g,%g,%g,%g,%g,%g,%s\n", s[0], s[1], mh2inst_f (r + o, be), mh2inst_f (r + o + 4, be), mh2inst_f (r + o + 8, be), mh2inst_f (r + o + 12, be), mh2inst_f (r + o + 16, be), mh2inst_f (r + o + 20, be), mh2inst_f (r + o + 24, be), cls);
		while (po + 12 <= rs)
		{
			const uint hash = be ? rd_be32 (r + po) : rd_le32 (r + po);
			const u8 *ty = r + po + 4;
			if (!memcmp (ty, "flo", 3) && po + 12 <= rs)
				sumo_printf (&pb, "%u,%08x,float,%g\n", i, hash, mh2inst_f (r + po + 8, be));
			else if (!memcmp (ty, "boo", 3) && po + 12 <= rs)
				sumo_printf (&pb, "%u,%08x,bool,%d\n", i, hash, (int)(be ? rd_be32 (r + po + 8) : rd_le32 (r + po + 8)));
			else if (!memcmp (ty, "int", 3) && po + 12 <= rs)
				sumo_printf (&pb, "%u,%08x,int,%d\n", i, hash, (int)(be ? rd_be32 (r + po + 8) : rd_le32 (r + po + 8)));
			else if (!memcmp (ty, "str", 3))
			{
				size_t sp = po + 8, l = 0;
				while (sp + l < rs && r[sp + l])
					l++;
				if (sp + l >= rs)
					break;
				sumo_printf (&pb, "%u,%08x,str,%.100s\n", i, hash, (ccp)r + sp);
				po = sp + ((l + 1 + 3) & ~(size_t)3);
				continue;
			}
			else
				break;
			po += 12;
		}
		ok++;
	}
	FREE (c);
	enumError err = ERR_NOTHING_TO_DO;
	if (b.ok && pb.ok && ok && b.n < 0xffffffffu && pb.n < 0xffffffffu)
	{
		char name[PATH_MAX], pname[PATH_MAX];
		const char *sl = strrchr (arg, '/');
		snprintf (name, sizeof (name), "%s", sl ? sl + 1 : arg);
		char *dot = strrchr (name, '.');
		if (dot)
			*dot = 0;
		snprintf (pname, sizeof (pname), "%s_params.csv", name);
		strcat (name, ".csv");
		nintendo_sarc_entry_t *entries = CALLOC (2, sizeof (*entries));
		if (entries && OwnedEntryAdd (entries, 0, name, (u8 *)b.p, (uint)b.n) && OwnedEntryAdd (entries, 1, pname, (u8 *)pb.p, (uint)pb.n))
			err = write_owned_entries (arg, basedir, depth, "MH2-INST", entries, 2);
		else
			FREE (entries);
	}
	FREE (b.p);
	FREE (pb.p);
	return err;
}
