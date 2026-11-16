// The Oregon Trail (Wii) "strings/shared_<lang>.stp" string packs, little-endian: u16 2, u16 2, u32 size, u32 hash, then tagged sections ("PSC", "PSD" = a sorted table of u32 string hashes, "PSB" = trailer);
// the texts themselves form a pool of {u32 length, <length> ASCII bytes} records that runs up to the "PSB" tag. The pool start is found by trying the chain from each offset of the head and taking the longest
// valid run. Written as "<file>.tsv" (index, text; \n, \t, \\ escaped). The hash table is not paired with the texts (its order is sorted by hash, not by pool order).
static enumError extract_stptext_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	(void)depth;
	if (!is_ext (arg, ".stp"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
		return ERR_NOTHING_TO_DO;
	enumError err = ERR_NOTHING_TO_DO;
	pcr_ctx_t c;
	memset (&c, 0, sizeof (c));
	if (sz < 0x100 || rd_le16 (raw) != 2 || rd_le16 (raw + 2) != 2 || memcmp (raw + 12, "PSC", 3))
		goto done;
	{
		size_t best = 0, best_n = 0;
		for (size_t s = 0x20; s < sz && s < 0x20000; s += 1)
		{
			size_t p = s, n = 0;
			while (p + 5 <= sz)
			{
				const u32 l = rd_le32 (raw + p);
				if (!l || l > 0x2000 || p + 4 + l > sz)
					break;
				bool ok = true;
				for (u32 k = 0; k < l && ok; k++)
					ok = raw[p + 4 + k] >= 0x20 && raw[p + 4 + k] < 0x7f || raw[p + 4 + k] == '\n' || raw[p + 4 + k] == '\r' || raw[p + 4 + k] == '\t';
				if (!ok)
					break;
				p += 4 + l;
				n++;
			}
			if (n > best_n)
			{
				best_n = n;
				best = s;
			}
			if (n > 50)
				break;
		}
		if (best_n < 5)
			goto done;
		c.d = raw;
		c.sz = sz;
		c.ok = true;
		pcr_emit (&c, "index\ttext\n");
		size_t p = best;
		for (size_t i = 0; i < best_n; i++)
		{
			const u32 l = rd_le32 (raw + p);
			pcr_emit (&c, "%zu\t", i);
			for (u32 k = 0; k < l; k++)
			{
				const u8 ch = raw[p + 4 + k];
				if (ch == '\n')
					pcr_emit (&c, "\\n");
				else if (ch == '\t')
					pcr_emit (&c, "\\t");
				else if (ch == '\\')
					pcr_emit (&c, "\\\\");
				else if (ch != '\r')
					pcr_emit (&c, "%c", ch);
			}
			pcr_emit (&c, "\n");
			p += 4 + l;
		}
		if (c.ok)
		{
			char dest[PATH_MAX];
			snprintf (dest, sizeof (dest), "%s.tsv", arg);
			if (testmode)
			{
				fprintf (stdlog, "WOULD EXTRACT STP:%s -> TSV:%s\n", arg, dest);
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
					fprintf (stdlog, "%sEXTRACT STP:%s (%zu strings) -> TSV:%s\n", verbose > 0 ? "\n" : "", arg, best_n, dest);
			}
		}
	}
done:
	FREE (c.out);
	FREE (raw);
	return err;
}
