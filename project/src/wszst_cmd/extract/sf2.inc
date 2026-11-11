// RIFF containers of the MIDI / sampler world: RIFF MIDI ".rmi" (the "data" chunk is a standard MIDI file, written as "<name>.mid") and SoundFont 2 ".sf2" ("sfbk": 16 bit
// "smpl" data + "shdr" sample headers; every mono / left / right sample is written to "<file>.d/NNN_<name>.wav" at its own rate, with "info.txt" listing root key, loop points
// and link type). SF3 (Ogg) and 24 bit "sm24" data are not handled.
static enumError extract_sf2_file (ccp arg, ccp basedir, uint depth)
{
	const bool rmi = is_ext (arg, ".rmi"), sf2 = is_ext (arg, ".sf2");
	if ((!rmi && !sf2) || (export_count == 0 && !opt_decode))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	enumError err = ERR_NOTHING_TO_DO;
	char dest[PATH_MAX];
	beside_source_dest (dest, sizeof (dest), arg);
#define L32(p) ((u32)(p)[0] | (u32)(p)[1] << 8 | (u32)(p)[2] << 16 | (u32)(p)[3] << 24)
	if (rmi && sz > 20 && !memcmp (raw, "RIFF", 4) && !memcmp (raw + 8, "RMID", 4))
	{
		for (size_t p = 12; p + 8 <= sz;)
		{
			const size_t cs = L32 (raw + p + 4);
			if (!memcmp (raw + p, "data", 4) && p + 8 + cs <= sz && cs > 14 && !memcmp (raw + p + 8, "MThd", 4))
			{
				char stem[PATH_MAX], fn[PATH_MAX];
				const char *sl = strrchr (arg, '/');
				snprintf (stem, sizeof (stem), "%s", sl ? sl + 1 : arg);
				char *dot = strrchr (stem, '.');
				if (dot) *dot = 0;
				snprintf (fn, sizeof (fn), "%s.mid", stem);
				if (verbose >= 0 || testmode)
					fprintf (stdlog, "%s%sEXTRACT RMI:%s -> %s/%s%s (%zu bytes)\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, dest, basedir ? basedir : "", fn, cs);
				err = testmode ? ERR_OK : shade_write (arg, dest, basedir, fn, raw + p + 8, (uint)cs);
				break;
			}
			p += 8 + cs + (cs & 1);
		}
	}
	else if (sf2 && sz > 20 && !memcmp (raw, "RIFF", 4) && !memcmp (raw + 8, "sfbk", 4))
	{
		const u8 *smpl = 0, *shdr = 0;
		size_t smpl_n = 0, shdr_n = 0;
		for (size_t p = 12; p + 12 <= sz;)
		{
			const size_t cs = L32 (raw + p + 4);
			if (!memcmp (raw + p, "LIST", 4) && p + 8 + cs <= sz)
			{
				const bool sdta = !memcmp (raw + p + 8, "sdta", 4), pdta = !memcmp (raw + p + 8, "pdta", 4);
				if (sdta || pdta)
					for (size_t q = p + 12; q + 8 <= p + 8 + cs;)
					{
						const size_t qs = L32 (raw + q + 4);
						if (q + 8 + qs > p + 8 + cs) break;
						if (sdta && !memcmp (raw + q, "smpl", 4)) { smpl = raw + q + 8; smpl_n = qs; }
						if (pdta && !memcmp (raw + q, "shdr", 4)) { shdr = raw + q + 8; shdr_n = qs; }
						q += 8 + qs + (qs & 1);
					}
			}
			p += 8 + cs + (cs & 1);
		}
		if (smpl && shdr && shdr_n >= 46 * 2)
		{
			retro_info_t info = {{0}, 0};
			retro_info (&info, "samples: %zu\n", shdr_n / 46 - 1);
			err = ERR_OK;
			uint count = 0;
			for (size_t i = 0; i + 1 < shdr_n / 46 && err == ERR_OK; i++)
			{
				const u8 *h = shdr + i * 46;
				const size_t st = L32 (h + 20), en = L32 (h + 24), ls = L32 (h + 28), le = L32 (h + 32);
				const uint rate = L32 (h + 36), type = h[44] | h[45] << 8;
				char nm[24];
				retro_text (nm, h, 20);
				if (en <= st || (en * 2) > smpl_n || rate < 100 || (type & 0x8000))
					continue;
				retro_info (&info, "%03zu \"%s\": %zu frames, %u Hz, root key %u, correction %d, loop %zu-%zu, type 0x%x\n", i, nm, en - st, rate, h[40], (int8_t)h[41], ls - st, le - st, type);
				char safe[64], fn[128];
				snprintf (safe, sizeof (safe), "%s", nm);
				for (char *c = safe; *c; c++)
					if (*c == '/' || *c == '\\' || *c == ':' || *c == '?' || *c == '*') *c = '_';
				snprintf (fn, sizeof (fn), "%03zu_%s.wav", i, safe);
				const size_t bytes = (en - st) * 2;
				u8 *wav = MALLOC (44 + bytes);
				if (!wav) { err = ERR_OUT_OF_MEMORY; break; }
				memcpy (wav, "RIFF", 4); pcm_put32 (wav + 4, 36 + bytes);
				memcpy (wav + 8, "WAVEfmt ", 8); pcm_put32 (wav + 16, 16);
				wav[20] = 1; wav[21] = 0; wav[22] = 1; wav[23] = 0;
				pcm_put32 (wav + 24, rate); pcm_put32 (wav + 28, rate * 2);
				wav[32] = 2; wav[33] = 0; wav[34] = 16; wav[35] = 0;
				memcpy (wav + 36, "data", 4); pcm_put32 (wav + 40, bytes);
				memcpy (wav + 44, smpl + st * 2, bytes);
				if (verbose >= 0 || testmode)
					fprintf (stdlog, "%s%sEXTRACT SF2:%s -> %s/%s%s (%zu frames)\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, dest, basedir ? basedir : "", fn, en - st);
				err = testmode ? ERR_OK : shade_write (arg, dest, basedir, fn, wav, (uint)(44 + bytes));
				FREE (wav);
				count++;
			}
			if (count && err == ERR_OK && !testmode)
				err = shade_write (arg, dest, basedir, "info.txt", (u8 *)info.buf, (uint)info.n);
			else if (!count && err == ERR_OK)
				err = ERR_NOTHING_TO_DO;
		}
	}
#undef L32
	FREE (raw);
	return err;
}
