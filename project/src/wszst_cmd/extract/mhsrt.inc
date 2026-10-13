// Capcom MT Framework sound resource tables (".srt": Monster Hunter Tri, Wii) with their raw streamed
// ADPCM data (".ssd"). A table is {u32 chunk offsets..., 0} followed by 16-byte-headed chunks tagged
// "RND", "SSD", "SRT", "DSP" ...; the "DSP" chunk is {tag, u32 count, u32 0x10, 0}, a u32 offset table
// padded to 16 bytes and count 0x90-byte entries: +8 rate, +0xc first byte = channels - 1, then per channel
// {u32 end, u32 current (2), 16 x s16 coefficients, u16 gain, u16 initial ps ...} at +0x18 and +0x58. The
// ".ssd" is headerless 8-byte DSP frames, block-interleaved by 0x10000 bytes between the two channels. Only
// single-sound tables are handled; the stream is found beside the table (or in a "stream" folder) by
// comparing the sorted lower-case name tokens ("MH3_DEMO_BGM_01.srt" <-> "demo_01_bgm.ssd"). Written as
// "ch0.dsp" / "ch1.dsp" with trailing silent padding removed.
static void mhsrt_key (ccp name, char *key, size_t size)
{
	char tok[16][32];
	uint nt = 0;
	for (ccp p = name; *p && nt < 16;)
	{
		while (*p && !isalnum ((u8)*p))
			p++;
		uint l = 0;
		while (isalnum ((u8)*p) && l < 31)
			tok[nt][l++] = tolower ((u8)*p++);
		while (isalnum ((u8)*p))
			p++;
		tok[nt][l] = 0;
		if (l && strcmp (tok[nt], "mh3"))
			nt++;
	}
	for (uint i = 0; i < nt; i++)
		for (uint j = i + 1; j < nt; j++)
			if (strcmp (tok[i], tok[j]) > 0)
			{
				char t[32];
				strcpy (t, tok[i]);
				strcpy (tok[i], tok[j]);
				strcpy (tok[j], t);
			}
	key[0] = 0;
	for (uint i = 0; i < nt; i++)
	{
		strncat (key, tok[i], size - strlen (key) - 2);
		strcat (key, "|");
	}
}

static bool mhsrt_find_ssd (ccp srt, char *out, size_t size)
{
	char dir[PATH_MAX], key[256], base[PATH_MAX];
	snprintf (dir, sizeof (dir), "%s", srt);
	char *sl = strrchr (dir, '/');
	if (sl)
		*sl = 0;
	else
		strcpy (dir, ".");
	ccp b = strrchr (srt, '/');
	snprintf (base, sizeof (base), "%s", b ? b + 1 : srt);
	char *dot = strrchr (base, '.');
	if (dot)
		*dot = 0;
	mhsrt_key (base, key, sizeof (key));
	if (!*key)
		return false;
	for (uint pass = 0; pass < 2; pass++)
	{
		char d2[PATH_MAX];
		snprintf (d2, sizeof (d2), pass ? "%s/stream" : "%s", dir);
		DIR *dp = opendir (d2);
		if (!dp)
			continue;
		struct dirent *de;
		while ((de = readdir (dp)))
		{
			const size_t l = strlen (de->d_name);
			if (l < 5 || strcasecmp (de->d_name + l - 4, ".ssd"))
				continue;
			char n2[PATH_MAX], k2[256];
			snprintf (n2, sizeof (n2), "%s", de->d_name);
			n2[l - 4] = 0;
			mhsrt_key (n2, k2, sizeof (k2));
			if (!strcmp (key, k2))
			{
				snprintf (out, size, "%s/%s", d2, de->d_name);
				closedir (dp);
				return true;
			}
		}
		closedir (dp);
	}
	return false;
}

static enumError extract_mhsrt_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".srt"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0, *ssd = 0;
	size_t sz = 0, ssz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	enumError err = ERR_NOTHING_TO_DO;
	size_t dsp = 0;
	for (size_t t = 0; t + 4 <= sz && t < 0x40; t += 4)
	{
		const size_t p = rd_be32 (raw + t);
		if (!p)
			break;
		if (p + 0x10 <= sz && !memcmp (raw + p, "DSP", 4))
		{
			dsp = p;
			break;
		}
	}
	if (!dsp || rd_be32 (raw + dsp + 4) != 1 || dsp + 0x10 + 0x10 + 0x90 > sz + 8)
		goto done;
	const u8 *e = raw + dsp + 0x20;
	const uint rate = rd_be32 (e + 8), nch = (e[0xc] ? 2 : 1);
	if (rate < 4000 || rate > 96000 || dsp + 0x20 + 0x58 + 0x40 > sz + 8 * (nch - 1))
		goto done;
	char path[PATH_MAX];
	if (!mhsrt_find_ssd (arg, path, sizeof (path))
		|| LoadFileAlloc (path, 0, 0, &ssd, &ssz, 0, 0, 0, false) || !ssd || ssz < 0x100)
		goto done;
	const size_t il = 0x10000;
	nintendo_sarc_entry_t *entries = CALLOC (nch, sizeof (*entries));
	u8 *chan[2] = { 0, 0 };
	size_t len[2] = { 0, 0 }, frames = (size_t)-1;
	uint n = 0;
	for (uint c = 0; c < nch && entries; c++)
	{
		chan[c] = MALLOC (ssz + 8);
		if (!chan[c])
			break;
		for (size_t pos = nch == 2 ? c * il : 0; pos < ssz;)
		{
			const size_t l = nch == 2 ? (ssz - pos < il ? ssz - pos : il) : ssz;
			memcpy (chan[c] + len[c], ssd + pos, l);
			len[c] += l;
			pos += nch == 2 ? 2 * il : ssz;
		}
		size_t t = len[c] & ~(size_t)7;
		while (t >= 8 && !memcmp (chan[c] + t - 8, "\0\0\0\0\0\0\0\0", 8))
			t -= 8;
		len[c] = t;
		if (t / 8 < frames)
			frames = t / 8;
	}
	if (entries && frames && frames != (size_t)-1)
		for (uint c = 0; c < nch; c++)
		{
			const u8 *rec = e + 0x18 + 0x40 * c;
			const size_t bytes = frames * 8, nib = frames * 16;
			u8 *out = CALLOC (1, 0x60 + bytes);
			if (!out)
				break;
			wr_be32 (out + 0x00, (u32)(frames * 14));
			wr_be32 (out + 0x04, (u32)nib);
			wr_be32 (out + 0x08, rate);
			wr_be32 (out + 0x10, 2);
			wr_be32 (out + 0x14, (u32)nib - 1);
			wr_be32 (out + 0x18, 2);
			memcpy (out + 0x1c, rec + 8, 32);
			out[0x3f] = chan[c][0];
			memcpy (out + 0x60, chan[c], bytes);
			char name[16];
			snprintf (name, sizeof (name), "ch%u.dsp", c);
			const bool added = OwnedEntryAdd (entries, n, name, out, 0x60 + bytes);
			FREE (out);
			if (!added)
				break;
			n++;
		}
	for (uint c = 0; c < 2; c++)
		FREE (chan[c]);
	if (entries && n == nch)
		err = write_owned_entries (arg, basedir, depth, "MH-SRT", entries, n);
	else
		FREE (entries);
done:
	FREE (ssd);
	FREE (raw);
	return err;
}
