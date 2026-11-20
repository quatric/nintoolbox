// IO Interactive "IOPacked v0.1" property streams (Mini Ninjas scene ".prp" next to the scene ".gms"; big endian; the engine class is ZPackedStream, the tag names come from its debug strings).
// Layout: "IOPacked v0.1\0", at 0x17 {u32 name count, u32 name table size}, the name table (NUL terminated strings) from 0x1f, then after 4 bytes the tag stream. Tags: 0x01 Array {u32 count}
// ... 0x7c EndArray, 0x02 BeginObject ... 0x7e EndObject, 0x03 Reference, 0x04 Container {u32 count} ... 0x7e, 0x05 Char / 0x06 Bool / 0x07 Int8 {1 byte}, 0x08 Int16, 0x09 Int32, 0x0a Float32, 0x0b
// Float64, 0x0c String {u32 name table index}, 0x0d RawData {u32 size, bytes}, 0x0e {u32 name table index} (key), 0x0f Bitfield {u32 count, count x u32 name table index}, 0x7d SkipMark, 0x7f end of stream
// (the Named* variants 0x81-0x8f carry a name and are not used in the Mini Ninjas files). Written as an indented text dump "<file>.txt" (the 531,312 tags of C15_FireCastle_Main.prp are consumed
// exactly up to the end tag).
static enumError extract_glacier_prp_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	if (!is_ext (arg, ".prp"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw || sz < 0x40 || memcmp (raw, "IOPacked v0.1", 14))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	const u32 nn = rd_be32 (raw + 0x17), nsz = rd_be32 (raw + 0x1b);
	const size_t sstart = 0x1f, send = sstart + (size_t)nsz;
	if (nn > 10000000 || send + 8 > sz)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	// name table
	u32 *noff = MALLOC (sizeof (u32) * (nn + 8));
	u32 cnt = 0;
	for (size_t p = sstart; p < send && cnt < nn + 4;)
	{
		noff[cnt++] = (u32)p;
		while (p < send && raw[p])
			p++;
		p++;
	}
	sumo_buf_t b = { 0, 0, 0, true };
	sumo_printf (&b, "# Glacier IOPacked property stream, decoded from %s (%u names)\n", arg, cnt);
	size_t p = send + 4;
	uint ind = 0;
	bool done = false;
#define PRP_NAME(i) ((i) < cnt ? (ccp)raw + noff[i] : "?")
#define PRP_IND sumo_printf (&b, "%.*s", (int)(ind > 40 ? 80 : 2 * ind), "                                                                                ")
	while (p < sz && b.ok && !done)
	{
		const u8 op = raw[p++];
		u32 a = 0;
		switch (op)
		{
		case 0x01: case 0x04: case 0x09: case 0x0c: case 0x0e:
			if (p + 4 > sz)
				goto prp_bad;
			a = rd_be32 (raw + p);
			p += 4;
			break;
		case 0x0a:
			if (p + 4 > sz)
				goto prp_bad;
			a = rd_be32 (raw + p);
			p += 4;
			break;
		default:
			break;
		}
		switch (op)
		{
		case 0x01: PRP_IND; sumo_printf (&b, "array %u [\n", a); ind++; break;
		case 0x02: PRP_IND; sumo_printf (&b, "object {\n"); ind++; break;
		case 0x03: PRP_IND; sumo_printf (&b, "reference\n"); break;
		case 0x04: PRP_IND; sumo_printf (&b, "container %u\n", a); break;
		case 0x05: case 0x06: case 0x07:
			if (p + 1 > sz)
				goto prp_bad;
			PRP_IND;
			sumo_printf (&b, "%s %u\n", op == 5 ? "char" : op == 6 ? "bool" : "int8", raw[p++]);
			break;
		case 0x08:
			if (p + 2 > sz)
				goto prp_bad;
			PRP_IND;
			sumo_printf (&b, "int16 %d\n", (s16)rd_be16 (raw + p));
			p += 2;
			break;
		case 0x09: PRP_IND; sumo_printf (&b, "int32 %d\n", (s32)a); break;
		case 0x0a:
		{
			float f;
			memcpy (&f, &a, 4);
			PRP_IND;
			sumo_printf (&b, "float %.9g\n", (double)f);
			break;
		}
		case 0x0b:
		{
			if (p + 8 > sz)
				goto prp_bad;
			const u64 u = ((u64)rd_be32 (raw + p) << 32) | rd_be32 (raw + p + 4);
			double dv;
			memcpy (&dv, &u, 8);
			p += 8;
			PRP_IND;
			sumo_printf (&b, "double %.17g\n", dv);
			break;
		}
		case 0x0c: PRP_IND; sumo_printf (&b, "string %u \"%s\"\n", a, PRP_NAME (a)); break;
		case 0x0d:
		{
			if (p + 4 > sz)
				goto prp_bad;
			const u32 l = rd_be32 (raw + p);
			p += 4;
			if ((u64)p + l > sz)
				goto prp_bad;
			PRP_IND;
			sumo_printf (&b, "raw %u", l);
			for (u32 k = 0; k < l && k < 64; k++)
				sumo_printf (&b, "%s%02x", k ? "" : " ", raw[p + k]);
			sumo_printf (&b, "\n");
			p += l;
			break;
		}
		case 0x0e: PRP_IND; sumo_printf (&b, "key %u \"%s\"\n", a, PRP_NAME (a)); break;
		case 0x0f:
		{
			if (p + 4 > sz)
				goto prp_bad;
			const u32 c = rd_be32 (raw + p);
			p += 4;
			if ((u64)p + 4ull * c > sz)
				goto prp_bad;
			PRP_IND;
			sumo_printf (&b, "bitfield %u", c);
			for (u32 k = 0; k < c; k++)
				sumo_printf (&b, "%s%s", k ? "|" : " ", PRP_NAME (rd_be32 (raw + p + 4 * k)));
			sumo_printf (&b, "\n");
			p += 4 * (size_t)c;
			break;
		}
		case 0x7c: if (ind) ind--; PRP_IND; sumo_printf (&b, "]\n"); break;
		case 0x7d: PRP_IND; sumo_printf (&b, "skip\n"); break;
		case 0x7e: if (ind) ind--; PRP_IND; sumo_printf (&b, "}\n"); break;
		case 0x7f: done = true; break;
		default:
			goto prp_bad;
		}
	}
	if (0)
	{
prp_bad:
		done = false;
	}
	enumError err = ERR_NOTHING_TO_DO;
	if (done && b.ok && b.n < 0xffffffffu)
	{
		char dest[PATH_MAX], name[PATH_MAX];
		beside_source_dest (dest, sizeof (dest), arg);
		const char *sl = strrchr (arg, '/');
		snprintf (name, sizeof (name), "%s", sl ? sl + 1 : arg);
		char *dot = strrchr (name, '.');
		if (dot)
			strcpy (dot, ".txt");
		else
			strcat (name, ".txt");
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT GLACIER-PRP:%s (%u names) -> %s/%s\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, cnt, dest, name);
		err = shade_write (arg, dest, basedir, name, (u8 *)b.p, (uint)b.n);
	}
	FREE (b.p);
	FREE (noff);
	FREE (raw);
	return err;
}
