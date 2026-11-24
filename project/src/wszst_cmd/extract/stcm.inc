// "STCM2L" visual novel scripts (DS ".DAT": Uwasa no Midori-kun!! and about 5 titles; little endian): "STCM2L" + build date (0x20 bytes), u32 export table offset, u32 export
// count, ...; "GLOBAL_DATA", then "CODE_START_" (12 bytes) and a chain of actions {u32 global call, u32 opcode, u32 parameter count, u32 action size, 12 byte parameters,
// data} up to the export table. Strings sit in the data as {u32 0, u32 dword count, u32 1, u32 byte size = 4 * count, Shift-JIS text, NUL padded}. Decoded to "<file>.txt":
// one line per action that carries strings, "<opcode>: <string> | <string> ..." (in Midori-kun 210 is a text line, 212 the speaker); the opcodes are not interpreted.
static enumError extract_stcm_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir, (void)depth;
	if (!file_has_magic (arg, "STCM2L", 6))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
		return ERR_NOTHING_TO_DO;
	enumError err = ERR_NOTHING_TO_DO;
	const size_t eo = sz >= 0x30 ? rd_le32 (raw + 0x20) : 0;
	const u8 *cs = sz >= 0x30 ? memmem (raw, sz, "CODE_START_", 12) : 0;
	if (!cs || eo > sz)
		goto done;
	const size_t start = (size_t)(cs - raw) + 12;
	size_t o = start;
	uint nact = 0;
	while (o + 16 <= eo)
	{
		const size_t L = rd_le32 (raw + o + 12), np = rd_le32 (raw + o + 8);
		if (L < 16 || L > eo - o || 16 + 12 * np > L)
			break;
		o += L, nact++;
	}
	if (o + 16 > eo + 16 || eo - o >= 16 || !nact) // the chain must run up to the export table
		goto done;
	char out[PATH_MAX];
	snprintf (out, sizeof (out), "%s.txt", arg);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sDECODE STCM2L:%s (%u actions) -> %s\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, nact, out);
	err = ERR_OK;
	if (testmode)
		goto done;
	File_t F;
	err = CreateFileOpt (&F, true, out, false, arg);
	if (!F.f)
		goto done;
	fprintf (F.f, "# STCM2L script, %u actions\n", nact);
	for (o = start; nact--; o += rd_le32 (raw + o + 12))
	{
		const size_t L = rd_le32 (raw + o + 12), end = o + L;
		size_t d = o + 16 + 12 * (size_t)rd_le32 (raw + o + 8);
		bool any = false;
		while (d + 16 <= end && !rd_le32 (raw + d) && rd_le32 (raw + d + 8) == 1 && rd_le32 (raw + d + 12) == 4 * (size_t)rd_le32 (raw + d + 4)
		       && rd_le32 (raw + d + 12) <= end - d - 16)
		{
			const size_t n = rd_le32 (raw + d + 12);
			fprintf (F.f, any ? " | " : "%u: ", rd_le32 (raw + o + 4));
			flw0_print_sjis (F.f, raw + d + 16, strnlen ((ccp)raw + d + 16, n));
			any = true;
			d += 16 + n;
		}
		if (any)
			fputc ('\n', F.f);
	}
	err = ResetFile (&F, opt_preserve);
done:
	FREE (raw);
	return err;
}
