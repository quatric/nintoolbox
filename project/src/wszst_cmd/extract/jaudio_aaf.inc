// Nintendo JAudio v1 "AAF" archives (".aaf": Super Mario Sunshine, Luigi's Mansion, Wind Waker, Pikmin 2; big endian): a list of chunk ids, each followed by its
// payload words: 1/4/6/7 {offset, size, flags}, 5 {offset, size, flags} (stream table), and 2 (IBNK) / 3 (WSYS) a run of {offset, size, id} entries ended by a zero
// offset; the id list ends with 0. Each WSYS is read like in jaudiobaa.inc and its waves are decoded from the external ".aw" files (same folder, "Banks/", "Waves/")
// to "wsys<id>/<aw name>/<nnnn>.wav" (after vgmstream jaudio.c; reuses the AFC / PCM decoders of jaudiobaa.inc).
static bool jaudio_aaf_find_aw (ccp aaf, ccp name, char *path, size_t psize)
{
	static ccp const where[] = { "Banks/", "../Banks/", "banks/", "" };
	char dir[PATH_MAX];
	snprintf (dir, sizeof (dir), "%s", aaf);
	char *sl = strrchr (dir, '/');
	if (sl) sl[1] = 0; else dir[0] = 0;
	for (uint i = 0; i < sizeof (where) / sizeof (*where); i++)
	{
		snprintf (path, psize, "%s%s%s", dir, where[i], name);
		FILE *f = fopen (path, "rb");
		if (f)
		{
			fclose (f);
			return true;
		}
	}
	return jaudio_find_aw (aaf, name, path, psize);
}

static enumError extract_jaudio_aaf_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".aaf"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	if (sz < 0x30 || rd_be32 (raw) != 1 || rd_be32 (raw + 0xc) != 0 || rd_be32 (raw + 0x10) != 2)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	u32 wsys_off[256], wsys_id[256];
	uint nw = 0;
	size_t p = 0;
	bool bad = false;
	while (p + 4 <= sz && !bad)
	{
		const u32 id = rd_be32 (raw + p);
		p += 4;
		if (!id)
			break;
		if (id == 1 || id == 4 || id == 5 || id == 6 || id == 7)
			p += 12;
		else if (id == 2 || id == 3)
		{
			while (p + 4 <= sz)
			{
				const u32 eo = rd_be32 (raw + p);
				p += 4;
				if (!eo)
					break;
				if (p + 8 > sz) { bad = true; break; }
				const u32 eid = rd_be32 (raw + p + 4);
				p += 8;
				if (id == 3 && nw < 256)
				{
					wsys_off[nw] = eo;
					wsys_id[nw++] = eid;
				}
			}
		}
		else
			bad = true;
	}
	size_t cap = 64, n = 0;
	nintendo_sarc_entry_t *entries = bad || !nw ? 0 : CALLOC (cap, sizeof (*entries));
	for (uint r = 0; entries && r < nw; r++)
	{
		const size_t w = wsys_off[r];
		if (w + 0x18 > sz || memcmp (raw + w, "WSYS", 4))
			continue;
		const size_t winf = w + rd_be32 (raw + w + 0x10);
		if (winf + 8 > sz || memcmp (raw + winf, "WINF", 4))
			continue;
		const uint groups = rd_be32 (raw + winf + 4);
		if (groups > 4096 || winf + 8 + 4 * (size_t)groups > sz)
			continue;
		for (uint g = 0; g < groups; g++)
		{
			const size_t go = w + rd_be32 (raw + winf + 8 + 4 * (size_t)g);
			if (go + 0x74 > sz)
				continue;
			char awname[0x71];
			memcpy (awname, raw + go, 0x70);
			awname[0x70] = 0;
			const uint nwv = rd_be32 (raw + go + 0x70);
			if (!awname[0] || strchr (awname, '/') || nwv > 100000 || go + 0x74 + 4 * (size_t)nwv > sz)
				continue;
			char awpath[PATH_MAX];
			if (!jaudio_aaf_find_aw (arg, awname, awpath, sizeof (awpath)))
				continue;
			u8 *aw = 0;
			size_t aws = 0;
			if (LoadFileAlloc (awpath, 0, 0, &aw, &aws, 0, 0, 0, false) || !aw)
			{
				FREE (aw);
				continue;
			}
			char stem[0x71];
			snprintf (stem, sizeof (stem), "%s", awname);
			char *dot = strrchr (stem, '.');
			if (dot) *dot = 0;
			for (uint k = 0; k < nwv; k++)
			{
				const size_t eo = w + rd_be32 (raw + go + 0x74 + 4 * (size_t)k);
				if (eo + 0x20 > sz)
					continue;
				const uint fmt = raw[eo + 1];
				u32 rate_bits = rd_be32 (raw + eo + 4);
				float rate_f;
				memcpy (&rate_f, &rate_bits, 4);
				const u32 aoff = rd_be32 (raw + eo + 8), asize = rd_be32 (raw + eo + 0xc), samples = rd_be32 (raw + eo + 0x1c);
				if (rate_f < 1000 || rate_f > 200000 || aoff > aws || asize > aws - aoff || !samples || samples > 0x4000000)
					continue;
				s16 *pcm = MALLOC ((size_t)samples * 2 + 64);
				size_t frames = 0;
				if (!pcm)
					continue;
				if (fmt == 0 || fmt == 1)
					frames = jaudio_decode_afc (aw + aoff, asize, fmt, pcm, samples);
				else if (fmt == 2)
					for (; frames < asize && frames < samples; frames++)
						pcm[frames] = (s16)((int8_t)aw[aoff + frames] * 256);
				else if (fmt == 3)
					for (; frames * 2 + 2 <= asize && frames < samples; frames++)
						pcm[frames] = (s16)rd_be16 (aw + aoff + frames * 2);
				size_t wsize = 0;
				u8 *wav = frames ? jaudio_make_wav (pcm, frames, (uint)(rate_f + 0.5f), &wsize) : 0;
				FREE (pcm);
				if (!wav)
					continue;
				if (n + 1 >= cap)
				{
					cap *= 2;
					nintendo_sarc_entry_t *ne = REALLOC (entries, cap * sizeof (*entries));
					if (!ne) { FREE (wav); break; }
					memset (ne + cap / 2, 0, cap / 2 * sizeof (*ne));
					entries = ne;
				}
				char name[160];
				snprintf (name, sizeof (name), "wsys%u/%s/%04u.wav", wsys_id[r], stem, k);
				if (OwnedEntryAdd (entries, n, name, wav, wsize))
					n++;
				FREE (wav);
			}
			FREE (aw);
		}
	}
	FREE (raw);
	if (!n)
	{
		FREE (entries);
		return ERR_NOTHING_TO_DO;
	}
	return write_owned_entries (arg, basedir, depth, "JAUDIO-AAF", entries, n);
}
