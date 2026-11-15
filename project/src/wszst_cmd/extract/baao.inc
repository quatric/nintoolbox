#pragma push_macro("calloc")
#pragma push_macro("free")
#undef calloc
#undef free
// "BAAO" attack attribute tables (One Piece Unlimited Adventure / Cruise, Wii: battle/*atk_attr*.bin), big-endian:
//   {"BAAO", u32 group count, u32 version, group count * {u32 n, u32 ids_offset, u32 records_offset}} then
//   per group n u32 ids at ids_offset and n u32 offsets at records_offset; the offsets point to variable length
//   records that run to the next offset (or the end of the file). Words are written unnamed (floats where the
//   value looks like one) because the field meanings are not known. Written as "<file>.json".
static int baao_cmp (const void *a, const void *b)
{
	const u32 x = *(const u32 *)a, y = *(const u32 *)b;
	return x < y ? -1 : x > y;
}

static void baao_words (pcr_ctx_t *c, const u8 *p, size_t n)
{
	for (size_t i = 0; i + 4 <= n; i += 4)
	{
		const u32 w = rd_be32 (p + i);
		float f;
		memcpy (&f, &w, 4);
		if (gff_float_like (w))
			pcr_emit (c, "%s%.9g", i ? ", " : "", (double)f);
		else
			pcr_emit (c, "%s%u", i ? ", " : "", w);
	}
}

static enumError extract_baao_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	(void)depth;
	if (!is_ext (arg, ".bin"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	const u32 ng = sz >= 12 ? rd_be32 (raw + 4) : 0;
	bool good = sz >= 12 && !memcmp (raw, "BAAO", 4) && ng && ng < 4096 && 8 + (size_t)ng * 12 <= sz && !(sz & 3);
	u32 *offs = 0;
	size_t no = 0;
	if (good)
		offs = (u32 *)calloc (sz / 4 + 1, 4);
	for (u32 g = 0; good && g < ng; g++)
	{
		const u32 n = rd_be32 (raw + 8 + g * 12), a = rd_be32 (raw + 12 + g * 12), b = rd_be32 (raw + 16 + g * 12);
		if (n > 0x10000 || (a & 3) || (b & 3) || (size_t)a + (size_t)n * 4 > sz || (size_t)b + (size_t)n * 4 > sz)
		{
			good = false;
			break;
		}
		for (u32 k = 0; k < n; k++)
		{
			const u32 o = rd_be32 (raw + b + k * 4);
			if ((o & 3) || o >= sz)
			{
				good = false;
				break;
			}
			offs[no++] = o;
		}
	}
	if (!good)
	{
		free (offs);
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	qsort (offs, no, 4, baao_cmp);
	size_t nu = 0;
	for (size_t i = 0; i < no; i++)
		if (!nu || offs[nu - 1] != offs[i])
			offs[nu++] = offs[i];
	pcr_ctx_t c;
	memset (&c, 0, sizeof (c));
	c.d = raw;
	c.sz = sz;
	c.ok = true;
	pcr_emit (&c, "{\n  \"format\": \"BAAO\",\n  \"version\": %u,\n  \"groups\": [", rd_be32 (raw + 8));
	for (u32 g = 0; g < ng; g++)
	{
		const u32 n = rd_be32 (raw + 8 + g * 12), a = rd_be32 (raw + 12 + g * 12), b = rd_be32 (raw + 16 + g * 12);
		pcr_emit (&c, "%s\n    {\"ids\": [", g ? "," : "");
		for (u32 k = 0; k < n; k++)
			pcr_emit (&c, "%s%u", k ? ", " : "", rd_be32 (raw + a + k * 4));
		pcr_emit (&c, "], \"records\": [");
		for (u32 k = 0; k < n; k++)
		{
			const u32 o = rd_be32 (raw + b + k * 4);
			size_t e = sz;
			for (size_t i = 0; i < nu; i++)
				if (offs[i] > o)
				{
					e = offs[i];
					break;
				}
			pcr_emit (&c, "%s\n      [", k ? "," : "");
			baao_words (&c, raw + o, e - o);
			pcr_emit (&c, "]");
		}
		pcr_emit (&c, "\n    ]}");
	}
	pcr_emit (&c, "\n  ]\n}\n");
	free (offs);
	enumError err = ERR_NOTHING_TO_DO;
	if (c.ok)
	{
		char dest[PATH_MAX];
		snprintf (dest, sizeof (dest), "%s.json", arg);
		if (testmode)
		{
			fprintf (stdlog, "WOULD EXTRACT BAAO:%s -> JSON:%s\n", arg, dest);
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
				fprintf (stdlog, "%sEXTRACT BAAO:%s -> JSON:%s\n", verbose > 0 ? "\n" : "", arg, dest);
		}
	}
	FREE (c.out);
	FREE (raw);
	return err;
}
#pragma pop_macro("free")
#pragma pop_macro("calloc")
