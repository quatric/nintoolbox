// Electronic Arts PathFinder ".mus" stream banks (The Godfather: Blackhand Edition "audiostreams/*.mus", Boom Blox,
// Skate It, ...): a zero-filled header (non-zero checksum word at 0) followed by many "SCHl" ... "SCEl" EA streams
// back to back (little-endian block sizes, found at their natural 0x100 aligned positions; the sibling ".mpf" only
// holds the interactive playback graph). Every stream is written as "NNNN.exa" when the EA-XA decoder (codec 0x0a)
// takes it, else "NNNN.ngc", and the usual EA decoders turn them into WAV.
static enumError extract_eamus_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".mus"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	size_t p = 0;
	while (p < 0x1000 && p + 8 <= sz && memcmp (raw + p, "SCHl", 4))
		p += 4;
	if (p >= 0x1000 || p + 0x30 > sz)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	char dest[PATH_MAX];
	beside_source_dest (dest, sizeof (dest), arg);
	enumError err = ERR_OK;
	uint n = 0;
	while (!err && p + 0x30 <= sz && !memcmp (raw + p, "SCHl", 4))
	{
		size_t q = p;
		bool found = false;
		while (q + 8 <= sz)
		{
			const size_t bs = rd_le32 (raw + q + 4);
			if (bs < 8 || bs > sz - q)
				break;
			const bool end = !memcmp (raw + q, "SCEl", 4);
			q += bs;
			if (end)
			{
				found = true;
				break;
			}
		}
		if (!found)
			break;
		eaxa_hdr_t h;
		const size_t hsz = rd_le32 (raw + p + 4);
		const bool xa = hsz >= 12 && hsz <= q - p && eaxa_parse (raw + p, hsz, &h) && h.codec2 == 0x0a;
		char name[32];
		snprintf (name, sizeof (name), "%04u.%s", n, xa ? "exa" : "ngc");
		if (n == 0 && (verbose >= 0 || testmode))
			fprintf (stdlog, "%s%sEXTRACT EA-MUS:%s -> %s/\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, dest);
		err = shade_write (arg, dest, basedir, name, raw + p, (uint)(q - p));
		n++;
		p = q;
		while (p + 4 <= sz && p < q + 0x1000 && memcmp (raw + p, "SCHl", 4))
			p += 4;
	}
	FREE (raw);
	if (!n)
		return ERR_NOTHING_TO_DO;
	if (!err && !testmode && (opt_recurse < 0 || depth < (uint)opt_recurse))
		err = extract_tree_complete (dest, depth + 1);
	return err;
}
