// Pikmin 1 ".key" animation tables (the per-model "*.bin" animation lists use the same layout and are accepted too) (tekikeys/*.key; zen AnimMgr::read / AnimInfo::doread in the decompilation):
// {s32 version, s32 count}, a tagged parameter list ({u24 id, u8 size} + value, -1 ends), then per animation a
// length-prefixed name, its tagged parameters ("p00" flags, "spd" speed), the anim key frames, the info keys
// (version >= 1) and the event keys. Written as "<name>.key.json".
static enumError extract_pikmin_key_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	(void)depth;
	if (!is_ext (arg, ".key") && !is_ext (arg, ".bin"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	pcr_ctx_t c;
	memset (&c, 0, sizeof (c));
	c.d = raw;
	c.sz = sz;
	c.ok = sz >= 16;
	c.first = true;
	const int ver = c.ok ? (int)rd_be32 (raw) : -1;
	const uint count = c.ok ? rd_be32 (raw + 4) : 0;
	if (!c.ok || ver < 0 || ver > 4 || !count || count > 4096)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	c.p = 8;
	pcr_emit (&c, "{\n  \"version\": %d,\n  \"params\": {", ver);
	// tagged parameter list; "a01" is a length-prefixed string, everything else is a 4 byte value
	for (int level = 0; level < 1 + (int)count && c.ok; level++)
	{
		if (level)
		{
			if (!pcr_need (&c, 4))
				break;
			const uint nl = rd_be32 (raw + c.p);
			if (nl > 256 || !pcr_need (&c, 4 + (size_t)nl))
				break;
			pcr_emit (&c, "%s\n    {\"name\": \"%.*s\", \"params\": {", level > 1 ? "," : "", (int)nl, (ccp)raw + c.p + 4);
			c.p += 4 + nl;
		}
		bool first = true;
		for (;;)
		{
			if (!pcr_need (&c, 4))
				break;
			const u32 h = rd_be32 (raw + c.p);
			c.p += 4;
			if (h == 0xffffffffu)
				break;
			char id[4] = { (char)(h >> 24), (char)(h >> 16), (char)(h >> 8), 0 };
			const uint vs = h & 0xff;
			pcr_emit (&c, "%s\"%s\": ", first ? "" : ", ", id);
			first = false;
			if (!memcmp (id, "a01", 4))
			{
				if (!pcr_need (&c, 4))
					break;
				const uint sl = rd_be32 (raw + c.p);
				if (sl > 1024 || !pcr_need (&c, 4 + (size_t)sl))
					break;
				size_t n = sl;
				while (n && !raw[c.p + 4 + n - 1])
					n--;
				pcr_emit (&c, "\"%.*s\"", (int)n, (ccp)raw + c.p + 4);
				c.p += 4 + sl;
			}
			else if (vs == 4 && pcr_need (&c, 4))
			{
				const u32 u = rd_be32 (raw + c.p);
				if (!memcmp (id, "spd", 4))
				{
					float f;
					memcpy (&f, &u, 4);
					pcr_emit (&c, "%.9g", (double)f);
				}
				else
					pcr_emit (&c, "%d", (int)u);
				c.p += 4;
			}
			else
			{
				c.ok = false;
				break;
			}
		}
		if (!c.ok)
			break;
		pcr_emit (&c, "}");
		if (!level)
		{
			pcr_emit (&c, ",\n  \"animations\": [");
			continue;
		}
		// key lists
		for (int kind = 0; kind < 3 && c.ok; kind++)
		{
			if (kind == 1 && ver < 1)
				continue;
			if (!pcr_need (&c, 4))
				break;
			const uint n = rd_be32 (raw + c.p);
			c.p += 4;
			const uint ks = kind == 0 ? 4 : 8;
			if (n > 100000 || !pcr_need (&c, (size_t)n * ks))
			{
				c.ok = false;
				break;
			}
			pcr_emit (&c, ", \"%s\": [", kind == 0 ? "animKeys" : kind == 1 ? "infoKeys" : "eventKeys");
			for (uint i = 0; i < n; i++)
			{
				const u8 *k = raw + c.p;
				if (kind == 0)
					pcr_emit (&c, "%s%u", i ? ", " : "", (uint)rd_be32 (k));
				else
					pcr_emit (&c, "%s{\"frame\": %u, \"type\": %u, \"cmd\": %u, \"keyType\": %u}", i ? ", " : "",
						(uint)rd_be32 (k), (uint)rd_be16 (k + 4), k[6], k[7]);
				c.p += ks;
			}
			pcr_emit (&c, "]");
		}
		pcr_emit (&c, "}");
	}
	if (c.ok && c.p == sz)
		pcr_emit (&c, "\n  ]\n}\n");
	else
		c.ok = false;
	enumError err = ERR_NOTHING_TO_DO;
	if (c.ok)
	{
		char dest[PATH_MAX];
		snprintf (dest, sizeof (dest), "%s.json", arg);
		if (testmode)
		{
			fprintf (stdlog, "WOULD EXTRACT KEY:%s -> JSON:%s\n", arg, dest);
			err = ERR_OK;
		}
		else
		{
			File_t F;
			err = CreateFileOpt (&F, true, dest, false, arg);
			if (F.f && fwrite (c.out, 1, c.len, F.f) != c.len)
				err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", (uint)c.len, dest);
			ResetFile (&F, opt_preserve);
			if (verbose >= 0 && !err)
				fprintf (stdlog, "%sEXTRACT KEY:%s -> JSON:%s\n", verbose > 0 ? "\n" : "", arg, dest);
		}
	}
	FREE (c.out);
	FREE (raw);
	return err;
}
