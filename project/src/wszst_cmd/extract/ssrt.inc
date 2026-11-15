// "SSRT" parameter database (Sonic & Sega All-Stars Racing, Wii: Resource/*.dat), big-endian, self-describing:
//   {"SSRT", u32 version, u32 H (offset of the table block: 0x10 .. 0x80), ... , at H: u32 table hash, char name[0x40],
//    u32 item count, count * u32 item offsets}; every item is {u32 field count, u32 item hash, char name[0x40],
//    count * {u32 field name hash, u32 type hash, u32 value}} and the items follow each other; behind the last item
//   comes a NUL separated string table and the end marker 0xdeadbeef. Type hashes seen: 0xa45c088e = float,
//   0xfbe46712 = int, 0xb7459825 = string (value = absolute file offset of the string), 0x1e2a64fb = hash/id.
// Field names are stored as hashes only, so they are written as hex. Written as "<file>.json".
static void ssrt_str (pcr_ctx_t *c, const u8 *raw, size_t sz, size_t o)
{
	size_t e = o;
	while (e < sz && raw[e])
		e++;
	parm_emit_str (c, raw + o, (uint)(e - o));
}

static enumError extract_ssrt_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	(void)depth;
	if (!is_ext (arg, ".dat"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	bool good = sz >= 0x60 && !memcmp (raw, "SSRT", 4);
	const size_t hs = good ? rd_be32 (raw + 8) : 0;
	const size_t ni = good && hs >= 0x10 && hs <= 0x100 && !(hs & 15) && hs + 0x48 <= sz ? rd_be32 (raw + hs + 0x44) : 0;
	good = good && ni && ni < 100000 && hs + 0x48 + ni * 4 <= sz && !memcmp (raw + sz - 4, "\xde\xad\xbe\xef", 4);
	size_t tab = 0;
	for (size_t i = 0; good && i < ni; i++)
	{
		const size_t o = rd_be32 (raw + hs + 0x48 + 4 * i);
		if (o + 0x48 > sz)
		{
			good = false;
			break;
		}
		const size_t fc = rd_be32 (raw + o);
		const size_t e = o + 0x48 + fc * 12;
		if (fc > 100000 || e > sz || (i + 1 < ni ? rd_be32 (raw + hs + 0x4c + 4 * i) != e : false))
			good = false;
		tab = e;
	}
	if (!good)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	pcr_ctx_t c;
	memset (&c, 0, sizeof (c));
	c.d = raw;
	c.sz = sz;
	c.ok = true;
	pcr_emit (&c, "{\n  \"version\": %u,\n  \"hash\": \"%08x\",\n  \"name\": ", rd_be32 (raw + 4), rd_be32 (raw + hs));
	parm_emit_str (&c, raw + hs + 4, (uint)strnlen ((const char *)raw + hs + 4, 0x40));
	pcr_emit (&c, ",\n  \"items\": [");
	for (size_t i = 0; i < ni; i++)
	{
		const size_t o = rd_be32 (raw + hs + 0x48 + 4 * i), fc = rd_be32 (raw + o);
		pcr_emit (&c, "%s\n    {\"hash\": \"%08x\", \"name\": ", i ? "," : "", rd_be32 (raw + o + 4));
		parm_emit_str (&c, raw + o + 8, (uint)strnlen ((const char *)raw + o + 8, 0x40));
		pcr_emit (&c, ", \"fields\": [");
		for (size_t k = 0; k < fc; k++)
		{
			const u8 *p = raw + o + 0x48 + 12 * k;
			const u32 nh = rd_be32 (p), th = rd_be32 (p + 4), v = rd_be32 (p + 8);
			pcr_emit (&c, "%s\n      {\"id\": \"%08x\", ", k ? "," : "", nh);
			if (th == 0xa45c088eu)
			{
				float f;
				memcpy (&f, &v, 4);
				pcr_emit (&c, "\"type\": \"float\", \"value\": %.9g}", (double)f);
			}
			else if (th == 0xfbe46712u)
				pcr_emit (&c, "\"type\": \"int\", \"value\": %d}", (int)v);
			else if (th == 0xb7459825u && v >= tab && v < sz)
			{
				pcr_emit (&c, "\"type\": \"string\", \"value\": ");
				ssrt_str (&c, raw, sz, v);
				pcr_emit (&c, "}");
			}
			else
				pcr_emit (&c, "\"type\": \"%08x\", \"value\": %u}", th, v);
		}
		pcr_emit (&c, "\n    ]}");
	}
	pcr_emit (&c, "\n  ]\n}\n");
	enumError err = ERR_NOTHING_TO_DO;
	if (c.ok)
	{
		char dest[PATH_MAX];
		snprintf (dest, sizeof (dest), "%s.json", arg);
		if (testmode)
		{
			fprintf (stdlog, "WOULD EXTRACT SSRT:%s -> JSON:%s\n", arg, dest);
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
				fprintf (stdlog, "%sEXTRACT SSRT:%s -> JSON:%s\n", verbose > 0 ? "\n" : "", arg, dest);
		}
	}
	FREE (c.out);
	FREE (raw);
	return err;
}
#pragma pop_macro("free")
#pragma pop_macro("calloc")
