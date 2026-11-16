// Chunked LZF containers of the Monkey-Bar / Alien Syndrome / Despicable Me engine (Wii; "Maps/*.map", "Regions/*.rgn", "Regions/<lang>/*.loc", "EntityTemplates/*.et",
// "Cinematics/*.cin", ...; files named by a 64-bit hash): the 8 byte file type hash, u32 BE unpacked size, u32 BE offset count n, n offsets (the first is 0x10 + 4 * n, bit 31 marks a chunk that is stored as is, the last is
// the packed size), then n - 1 chunks of liblzf data (control byte < 0x20 = literal run of ctrl + 1 bytes, otherwise a back reference of length (ctrl >> 5) + 2, 7 = one more
// length byte, distance ((ctrl & 0x1f) << 8 | next) + 1), each unpacking to 0x8000 bytes except the last. The result is the object container that the same games also store
// uncompressed: "FA AF FA AF", u32 version (3 / 7), then BBBBBBBB-tagged blocks and a closing "FE EF FE EF". It is written as "<file>.d/<name>.faaf" (the container itself is not parsed).
static bool faafz_lzf (const u8 *in, size_t n, u8 *out, size_t *olen, size_t ocap)
{
	size_t i = 0, o = 0;
	while (i < n)
	{
		const uint c = in[i++];
		if (c < 32)
		{
			const size_t l = c + 1;
			if (i + l > n || o + l > ocap)
				return false;
			memcpy (out + o, in + i, l);
			i += l, o += l;
		}
		else
		{
			size_t l = c >> 5;
			if (l == 7)
			{
				if (i >= n)
					return false;
				l += in[i++];
			}
			if (i >= n)
				return false;
			const size_t d = ((size_t)(c & 31) << 8) + in[i++] + 1;
			l += 2;
			if (d > o || o + l > ocap)
				return false;
			for (size_t k = 0; k < l; k++, o++)
				out[o] = out[o - d];
		}
	}
	*olen = o;
	return true;
}

static enumError extract_faafz_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	u8 *raw = 0, *out = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
		return ERR_NOTHING_TO_DO;
	enumError err = ERR_NOTHING_TO_DO;
	if (sz < 0x20 || (raw[0] == 0xfa && raw[1] == 0xaf))
		goto done;
	{
		const u32 usz = rd_be32 (raw + 8), n = rd_be32 (raw + 12);
		if (n < 2 || n > 0x10000 || usz < 8 || usz > 0x10000000 || 0x10 + 4 * (u64)n > sz || (rd_be32 (raw + 16) & 0x7fffffff) != 0x10 + 4 * n || rd_be32 (raw + 16 + 4 * (size_t)(n - 1)) != sz)
			goto done;
		for (u32 i = 1; i < n; i++)
			if ((rd_be32 (raw + 16 + 4 * (size_t)i) & 0x7fffffff) < (rd_be32 (raw + 12 + 4 * (size_t)i) & 0x7fffffff))
				goto done;
		out = MALLOC (usz + 0x10000);
		if (!out)
			goto done;
		size_t total = 0;
		for (u32 i = 0; i + 1 < n; i++)
		{
			const u32 fa = rd_be32 (raw + 16 + 4 * (size_t)i), a = fa & 0x7fffffff, b = rd_be32 (raw + 16 + 4 * (size_t)(i + 1)) & 0x7fffffff;
			size_t got = 0;
			if (fa & 0x80000000u)	// stored chunk
			{
				if (total + (b - a) > usz + 0x10000)
					goto done;
				memcpy (out + total, raw + a, b - a);
				got = b - a;
			}
			else if (!faafz_lzf (raw + a, b - a, out + total, &got, usz + 0x10000 - total))
				goto done;
			total += got;
		}
		if (total != usz || memcmp (out, "\xfa\xaf\xfa\xaf", 4))
			goto done;
		char dest[PATH_MAX], name[PATH_MAX];
		beside_source_dest (dest, sizeof (dest), arg);
		const char *sl = strrchr (arg, '/');
		snprintf (name, sizeof (name), "%s.faaf", sl ? sl + 1 : arg);
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT FAAFZ:%s (%u bytes) -> %s/%s\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, usz, dest, name);
		err = ERR_OK;
		if (!testmode)
			err = shade_write (arg, dest, basedir, name, out, usz);
	}
done:
	FREE (out);
	FREE (raw);
	return err;
}
