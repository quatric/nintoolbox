// Atlus "FLW0" flow scripts (Trauma Team, Wii; ".bf"), big-endian: u32 0, u32 file size, "FLW0", u32 0, u32 section
// count, u32 0x10000, then count 16-byte section headers at 0x20 {u32 type, u32 element size, u32 element count,
// u32 offset}. Type 0 are procedure labels and type 1 jump labels (0x38-byte elements: char name[0x20], ..., u32
// instruction index at +0x30), type 2 the instruction stream (u32 elements), type 3 the embedded "MSG2"
// message block and type 4 a string pool. The MSG2 block has a 0x30-byte header (record count u32 @0x28 of the block,
// record table offset @0x24) followed by 0x40-byte records {char name[0x20], u32 0, u32 text size, u32 text offset,
// u32 0xffff0000, ...}; the text blobs are a byte-coded stream carrying Shift-JIS strings. Decoded to
// "<file>.txt": the label tables, the instruction count and every Shift-JIS string per message (the instruction
// stream and the control codes in the message blobs are not interpreted).
static void flw0_print_sjis (FILE *f, const u8 *s, size_t n)
{
	cucp src = s, end = s + n;
	while (src < end)
	{
		cucp before = src;
		const int code = ScanShiftJISCharE (&src, end);
		if (code <= 0)
		{
			fprintf (f, "\\x%02x", *before);
			src = before + 1;
			continue;
		}
		char buf[8], *e = PrintUTF8Char (buf, code);
		fwrite (buf, 1, (size_t)(e - buf), f);
	}
}

// Writes the messages of an Atlus "MSG2" block (see above): every record name with its Shift-JIS strings.
static void msg2_write_text (FILE *f, const u8 *m, size_t msglen)
{
	const size_t tab = rd_be32 (m + 0x24), rs = 0x40, cnt = rd_be32 (m + 0x28);
	for (size_t i = 0; i < cnt && tab + rs * (i + 1) <= msglen; i++)
	{
		const u8 *r = m + tab + rs * i;
		const size_t tsz = rd_be32 (r + 0x24), toff = rd_be32 (r + 0x28);
		if (toff > msglen || tsz > msglen - toff)
			break;
		fprintf (f, "\nmessage %.32s\n", (ccp)r);
		const u8 *t = m + toff;
		for (size_t p = 0, q; p < tsz; p = q + 1)
		{
			for (q = p; q < tsz && t[q] >= 0x20 && t[q] != 0xff; q++)
				;
			if (q - p >= 2)
			{
				fputs ("  \"", f);
				flw0_print_sjis (f, t + p, q - p);
				fputs ("\"\n", f);
			}
		}
	}
}

static enumError extract_flw0_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	(void)depth;
	if (!is_ext (arg, ".bf") || (export_count <= 0 && !opt_decode))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	enumError err = ERR_NOTHING_TO_DO;
	if (sz < 0x30 || memcmp (raw + 8, "FLW0", 4))
		goto done;
	const uint nsec = rd_be32 (raw + 0x10);
	if (!nsec || nsec > 16 || 0x20 + 16 * (size_t)nsec > sz)
		goto done;
	const u8 *lab[2] = {0, 0};
	uint nlab[2] = {0, 0}, ninstr = 0;
	size_t msg = 0, msglen = 0;
	for (uint i = 0; i < nsec; i++)
	{
		const u8 *h = raw + 0x20 + 16 * i;
		const uint type = rd_be32 (h), es = rd_be32 (h + 4), cnt = rd_be32 (h + 8);
		const size_t off = rd_be32 (h + 12);
		if (off > sz || (u64)es * cnt > sz - off)
			goto done;
		if (type < 2 && es == 0x38)
			lab[type] = raw + off, nlab[type] = cnt;
		else if (type == 2)
			ninstr = cnt;
		else if (type == 3)
			msg = off, msglen = cnt * es;
	}
	char out[PATH_MAX];
	snprintf (out, sizeof (out), "%s.txt", arg);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sDECODE FLW0:%s -> %s\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, out);
	if (testmode)
	{
		err = ERR_OK;
		goto done;
	}
	File_t F;
	err = CreateFileOpt (&F, true, out, false, arg);
	if (!F.f)
		goto done;
	fprintf (F.f, "# Atlus FLW0 flow script\n# instructions: %u\n", ninstr);
	for (uint t = 0; t < 2; t++)
		for (uint i = 0; i < nlab[t]; i++)
		{
			const u8 *e = lab[t] + 0x38 * (size_t)i;
			fprintf (F.f, "%s %.32s @ %u\n", t ? "jump" : "proc", (ccp)e, rd_be32 (e + 0x30));
		}
	if (msglen >= 0x30 && !memcmp (raw + msg + 4, "MSG2", 4))
		msg2_write_text (F.f, raw + msg, msglen);
	err = ResetFile (&F, opt_preserve);
done:
	FREE (raw);
	return err;
}

// Standalone "MSG2" message files (Trauma Team ".bm2"): the same block as inside the flow scripts, decoded to
// "<file>.txt".
static enumError extract_bm2_file (ccp arg)
{
	if (!is_ext (arg, ".bm2") || (export_count <= 0 && !opt_decode))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	enumError err = ERR_NOTHING_TO_DO;
	if (sz < 0x30 || memcmp (raw + 4, "MSG2", 4) || rd_be32 (raw + 12) != sz)
		goto done;
	char out[PATH_MAX];
	snprintf (out, sizeof (out), "%s.txt", arg);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sDECODE MSG2:%s -> %s\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, out);
	if (testmode)
	{
		err = ERR_OK;
		goto done;
	}
	File_t F;
	err = CreateFileOpt (&F, true, out, false, arg);
	if (!F.f)
		goto done;
	fputs ("# Atlus MSG2 message file\n", F.f);
	msg2_write_text (F.f, raw, sz);
	err = ResetFile (&F, opt_preserve);
done:
	FREE (raw);
	return err;
}
