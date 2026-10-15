// Lost in Blue: Shipwrecked (Wii) formats.
//
// "WII\0" scene archives (".bin"), big-endian: "WII\0", float 1.0, u32 file count @8 (2 or 3), u32 2, count
// {u32 offset, u32 size} pairs at 0x10, then count 0x10-byte NUL-padded member names ("0000Model.bin",
// "0000Snd.bin", ...); the member data follows 0x20 aligned. Members are written under their names. Variant with
// u32 0 @0xc (animal, Stage, Effect, Text, ...): no name table, just count {offset, size} pairs (empty ones skipped);
// members are written as "NNN.ext" with the extension guessed from their magic (ZMB, TEB, ZMS, nested WII).
static enumError extract_liblue_bin_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".bin"))
		return ERR_NOTHING_TO_DO;
	u8 hdr[8];
	FILE *f = fopen (arg, "rb");
	const bool magic = f && fread (hdr, 1, 8, f) == 8 && !memcmp (hdr, "WII", 4) && rd_be32 (hdr + 4) == 0x3f800000;
	if (f)
		fclose (f);
	if (!magic)
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	const uint count = sz >= 0x20 ? rd_be32 (raw + 8) : 0;
	const bool unnamed = sz >= 0x20 && rd_be32 (raw + 0xc) == 0;
	const size_t names = 0x10 + 8 * (size_t)count;
	bool ok = count && count <= (unnamed ? 4096u : 64u) && names + (unnamed ? 0 : 0x10 * (size_t)count) <= sz;
	uint nonempty = 0;
	if (unnamed && IsZMB (raw, (uint)sz))
		ok = false; // single ZMB model container: handled by the ZMB extractor
	for (uint i = 0; ok && i < count; i++)
	{
		const u8 *e = raw + 0x10 + 8 * (size_t)i, *nm = raw + names + 0x10 * (size_t)i;
		const size_t o = rd_be32 (e), n = rd_be32 (e + 4);
		if (unnamed)
		{
			ok = n ? o >= names && o <= sz && n <= sz - o : true;
			nonempty += n != 0;
		}
		else
			ok = o >= names + 0x10 * (size_t)count && o <= sz && n <= sz - o && nm[0] && memchr (nm, 0, 0x10)
				&& valid_sarc_path ((ccp)nm);
	}
	ok = ok && (!unnamed || nonempty);
	nintendo_sarc_entry_t *entries = ok ? CALLOC (count, sizeof (*entries)) : 0;
	if (!entries)
	{
		FREE (raw);
		return ok ? ERR_OUT_OF_MEMORY : ERR_NOTHING_TO_DO;
	}
	uint n = 0;
	for (uint i = 0; i < count && ok; i++)
	{
		const u8 *e = raw + 0x10 + 8 * (size_t)i;
		char uname[32];
		ccp name = (ccp)raw + names + 0x10 * (size_t)i;
		if (unnamed)
		{
			if (!rd_be32 (e + 4))
				continue;
			const u8 *m = raw + rd_be32 (e);
			const bool big = rd_be32 (e + 4) >= 4;
			ccp ext = !big ? "bin" : !memcmp (m, "ZMB ", 4) ? "zmb" : !memcmp (m, "TEB", 4) ? "teb" : !memcmp (m, "ZMS", 4) ? "zms" : "bin";
			snprintf (uname, sizeof (uname), "%03u.%s", i, ext);
			name = uname;
		}
		ok = OwnedEntryAdd (entries, n, name, raw + rd_be32 (e), rd_be32 (e + 4));
		n += ok;
	}
	enumError err = ERR_NOTHING_TO_DO;
	if (ok && n)
		err = write_owned_entries (arg, basedir, depth, "LIB-WII", entries, n);
	else
		ResetOwnedEntries (entries, n);
	FREE (raw);
	return err;
}

// ".zms" message tables, big-endian: "ZMS\0", "WII\0", float 1.0, ..., u8 slot count @0x20, u32 0x20 @0x24 (base
// of all offsets), u32 message count @0x28, then at 0x40 one {u32 offset, u32 0} per slot pointing at that slot's
// array of count {u32 offset, u32 0} string entries; strings are NUL-terminated Shift-JIS with "#c.." colour codes
// and 0x0a newlines. Decoded to "<file>.txt" as "slot<TAB>index<TAB>text" lines.
static enumError extract_liblue_zms_file (ccp arg)
{
	if (!is_ext (arg, ".zms") || (export_count <= 0 && !opt_decode))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	const uint slots = sz >= 0x40 ? raw[0x20] : 0, count = sz >= 0x40 ? rd_be32 (raw + 0x28) : 0;
	bool ok = slots && slots <= 16 && count && count <= 0x10000 && !memcmp (raw, "ZMS", 4) && !memcmp (raw + 4, "WII", 4)
		&& rd_be32 (raw + 0x24) == 0x20 && 0x40 + 8 * (size_t)slots <= sz;
	for (uint s = 0; ok && s < slots; s++)
	{
		const size_t t = 0x20 + (size_t)rd_be32 (raw + 0x40 + 8 * (size_t)s);
		ok = t + 8 * (size_t)count <= sz;
		for (uint i = 0; ok && i < count; i++)
		{
			const size_t o = 0x20 + (size_t)rd_be32 (raw + t + 8 * (size_t)i);
			ok = o < sz && memchr (raw + o, 0, sz - o);
		}
	}
	if (!ok)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	char out[PATH_MAX];
	snprintf (out, sizeof (out), "%s.txt", arg);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sDECODE ZMS:%s (%u slots, %u messages) -> %s\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", arg, slots, count, out);
	enumError err = ERR_OK;
	if (!testmode)
	{
		File_t F;
		err = CreateFileOpt (&F, true, out, false, arg);
		for (uint s = 0; F.f && s < slots; s++)
		{
			const size_t t = 0x20 + (size_t)rd_be32 (raw + 0x40 + 8 * (size_t)s);
			for (uint i = 0; i < count; i++)
			{
				cucp p = raw + 0x20 + rd_be32 (raw + t + 8 * (size_t)i), end = p + strlen ((ccp)p);
				fprintf (F.f, "%u\t%u\t", s, i);
				while (p < end)
				{
					cucp before = p;
					const int code = ScanShiftJISCharE (&p, end);
					if (code <= 0)
					{
						fprintf (F.f, "\\x%02x", *before);
						p = before + 1;
					}
					else if (code == '\n')
						fputs ("\\n", F.f);
					else if (code == '\\')
						fputs ("\\\\", F.f);
					else
					{
						char buf[8], *e = PrintUTF8Char (buf, code);
						fwrite (buf, 1, (size_t)(e - buf), F.f);
					}
				}
				fputc ('\n', F.f);
			}
		}
		if (F.f)
			err = ResetFile (&F, opt_preserve);
	}
	FREE (raw);
	return err;
}
