// Blue Tongue Toshi text tables (".trb", Nickelodeon Barnyard Wii; big endian, UTF-16 strings), layouts after OpenBarnyard (Toshi/Source/T2Locale/T2Locale.h, OpenBarnyard/Source/GUI/
// ASubtitleManager.h): the locale files (Data/Locale/<lang>.trb) hold a "LocaleStrings" symbol {s32 count, u32 ptr to count string pointers}; Written as a UTF-8 "<file>.txt" (index<TAB>text, control characters escaped).
static void toshi_utf8 (FILE *f, const u8 *sect, size_t size, u32 off)
{
	for (size_t i = off; i + 2 <= size; i += 2)
	{
		uint c = rd_be16 (sect + i);
		if (!c)
			break;
		if (c >= 0xd800 && c < 0xdc00 && i + 4 <= size)
		{
			const uint d = rd_be16 (sect + i + 2);
			if (d >= 0xdc00 && d < 0xe000)
			{
				c = 0x10000 + ((c - 0xd800) << 10) + (d - 0xdc00);
				i += 2;
			}
		}
		if (c == '\n')
			fputs ("\\n", f);
		else if (c == '\r')
			fputs ("\\r", f);
		else if (c == '\t')
			fputs ("\\t", f);
		else if (c < 0x80)
			fputc ((int)c, f);
		else if (c < 0x800)
			fprintf (f, "%c%c", 0xc0 | (c >> 6), 0x80 | (c & 63));
		else if (c < 0x10000)
			fprintf (f, "%c%c%c", 0xe0 | (c >> 12), 0x80 | ((c >> 6) & 63), 0x80 | (c & 63));
		else
			fprintf (f, "%c%c%c%c", 0xf0 | (c >> 18), 0x80 | ((c >> 12) & 63), 0x80 | ((c >> 6) & 63), 0x80 | (c & 63));
	}
}

static bool toshi_text_ok (const toshi_trb_t *t, u32 off)
{
	// a NUL-terminated UTF-16 string fully inside the section
	if ((u64)off + 2 > t->sect_size || (off & 1))
		return false;
	for (size_t i = off; i + 2 <= t->sect_size; i += 2)
		if (!rd_be16 (t->sect + i))
			return true;
	return false;
}

static enumError extract_toshi_text_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	(void)depth;
	if (!is_ext (arg, ".trb"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t raw_size = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false) || !raw)
		return ERR_NOTHING_TO_DO;
	toshi_trb_t trb;
	if (OpenToshiTrb (&trb, raw, raw_size))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	enumError err = ERR_NOTHING_TO_DO;
	const s64 loc = ToshiSymbol (&trb, "LocaleStrings");
	// the subtitle libraries ("SubtitleVersion") store their line strings behind an extra level of pointers that is not understood yet, so they are left alone
	const s64 sv = -1;
	bool ok = false;
	uint nout = 0;
	u32 lcount = 0, lptr = 0;
	if (loc >= 0 && (u64)loc + 8 <= trb.sect_size)
	{
		lcount = rd_be32 (trb.sect + loc);
		lptr = rd_be32 (trb.sect + loc + 4);
		ok = lcount <= 1000000 && (u64)lptr + 4ull * lcount <= trb.sect_size;
		for (u32 i = 0; i < lcount && ok; i++)
		{
			const u32 sp = rd_be32 (trb.sect + lptr + 4 * i);
			ok = toshi_text_ok (&trb, sp);
		}
		nout = lcount;
	}
	else if (sv >= 0)
	{
		const u32 ns = trb.symb_size >= 4 ? rd_be32 (trb.symb) : 0;
		ok = ns > 1 && ns < 100000;
	}
	if (ok)
	{
		char dest[PATH_MAX];
		if (opt_dest)
			SubstDest (dest, sizeof (dest), arg, opt_dest, 0, ".txt", false);
		else
			snprintf (dest, sizeof (dest), "%s.txt", arg);
		File_t F;
		err = ERR_OK;
		if (!testmode)
			err = CreateFileOpt (&F, true, dest, false, arg);
		if (!err && (testmode || F.f))
		{
			FILE *f = testmode ? fopen ("/dev/null", "w") : F.f;
			if (!f)
				f = F.f;
			if (loc >= 0)
			{
				fprintf (f, "# Blue Tongue Toshi locale strings, index<TAB>text, decoded from %s\n", arg);
				for (u32 i = 0; i < lcount; i++)
				{
					fprintf (f, "%u\t", i);
					toshi_utf8 (f, trb.sect, trb.sect_size, rd_be32 (trb.sect + lptr + 4 * i));
					fputc ('\n', f);
				}
			}
			else
			{
				fprintf (f, "# Blue Tongue Toshi subtitles (start-end ms, text), decoded from %s\n", arg);
				const u32 ns = rd_be32 (trb.symb);
				const size_t names = 4 + 12 * (size_t)ns;
				for (u32 i = 0; i < ns && 4 + 12 * (size_t)(i + 1) <= trb.symb_size; i++)
				{
					const u8 *e = trb.symb + 4 + 12 * i;
					const size_t no = names + rd_be16 (e + 2);
					const u32 off = rd_be32 (e + 8);
					if (no >= trb.symb_size || !strcmp ((ccp)trb.symb + no, "SubtitleVersion") || (u64)off + 4 > trb.sect_size)
						continue;
					const u32 nl = rd_be32 (trb.sect + off);
					if (nl == 0 || nl > 4096 || (u64)off + 4 + 12ull * nl > trb.sect_size)
						continue;
					bool good = true;
					for (u32 k = 0; k < nl && good; k++)
						good = toshi_text_ok (&trb, rd_be32 (trb.sect + off + 4 + 12 * k + 8));
					if (!good)
						continue;
					fprintf (f, "[%s]\n", (ccp)trb.symb + no);
					for (u32 k = 0; k < nl; k++)
					{
						const u8 *l = trb.sect + off + 4 + 12 * k;
						fprintf (f, "%d-%d\t", (int)rd_be32 (l), (int)rd_be32 (l + 4));
						toshi_utf8 (f, trb.sect, trb.sect_size, rd_be32 (l + 8));
						fputc ('\n', f);
						nout++;
					}
				}
			}
			if (testmode)
				fclose (f);
			else if (ferror (F.f))
				err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing text failed: %s\n", dest);
		}
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT TOSHI-TEXT:%s (%u strings) -> %s\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, nout, dest);
		if (!testmode)
			ResetFile (&F, opt_preserve);
	}
	ResetToshiTrb (&trb);
	FREE (raw);
	return err;
}
