// Slam Games engine audio banks (".atd" / ".sad", "SLAM AD\0"; Tele 7 Jeux, Margot's Word Brain), little-endian: the
// 16 byte head ("SLAM AD\0", u32 0x30000, u32 4) is followed by chunks {tag, u32 size, payload}: "DFTR" (FILETIME),
// one "AUDO" per sound {u8 1, u8 3, u8 0, u8 name length, u32 sample count, u32 rate, u32 blob size, u32 loop end?, u32 blob offset,
// name} and one "ADAT" whose first u32 plus 2 is where the blobs start. A blob is a little-endian DSP header
// {u32 sample count, 16 coefficients, gain, ps, hist1, hist2, loop ps, loop hist1, loop hist2} (50 bytes) followed by
// the DSP-ADPCM frames. Each sound is written as "<name>.dsp".
static enumError extract_slamad_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".atd") && !is_ext (arg, ".sad"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	enumError err = ERR_NOTHING_TO_DO;
	if (sz < 0x30 || memcmp (raw, "SLAM AD\0", 8))
		goto done;
	typedef struct { const u8 *p; uint n; } ch_t;
	ch_t auds[4096];
	uint nau = 0;
	const u8 *adat = 0;
	size_t adat_n = 0;
	for (size_t o = 16; o + 8 <= sz;)
	{
		const size_t n = rd_le32 (raw + o + 4);
		const size_t avail = sz - o - 8;
		if (!memcmp (raw + o, "AUDO", 4) && n <= avail && nau < 4096)
		{
			auds[nau].p = raw + o + 8;
			auds[nau++].n = (uint)n;
		}
		else if (!memcmp (raw + o, "ADAT", 4))
		{
			adat = raw + o + 8;
			adat_n = n < avail ? n : avail;
			break;
		}
		if (n > avail)
			break;
		o += 8 + n;
	}
	if (!adat || adat_n < 8 || !nau)
		goto done;
	const size_t base = (size_t)rd_le32 (adat) + 2;
	nintendo_sarc_entry_t *entries = CALLOC (nau, sizeof (*entries));
	if (!entries)
	{
		err = ERR_OUT_OF_MEMORY;
		goto done;
	}
	uint k = 0;
	for (uint i = 0; i < nau; i++)
	{
		const u8 *a = auds[i].p;
		if (auds[i].n < 24 || a[0] != 1 || a[1] != 3)
			continue;
		const uint nl = a[3], ns = rd_le32 (a + 4), rate = rd_le32 (a + 8), bsz = rd_le32 (a + 12);
		const size_t off = rd_le32 (a + 20);
		if (24 + (size_t)nl > auds[i].n || !ns || !rate || bsz < 50 + 8 || base + off > adat_n || bsz > adat_n - base - off)
			continue;
		const u8 *blob = adat + base + off;
		const size_t nib = (size_t)ns / 14 * 16 + (ns % 14 ? ns % 14 + 2 : 0), nb = (nib + 1) / 2;
		if (rd_le32 (blob) != ns || nb > bsz - 50)
			continue;
		u8 *out = CALLOC (1, 0x60 + nb);
		if (!out)
			continue;
		wr_be32 (out, ns);
		wr_be32 (out + 4, (u32)nib);
		wr_be32 (out + 8, rate);
		wr_be32 (out + 0x14, (u32)nib - 1);
		wr_be32 (out + 0x18, 2);
		for (uint j = 0; j < 16; j++)
			wr_be16 (out + 0x1c + 2 * j, rd_le16 (blob + 4 + 2 * j));
		for (uint j = 0; j < 7; j++)
			wr_be16 (out + 0x3c + 2 * j, rd_le16 (blob + 36 + 2 * j));
		memcpy (out + 0x60, blob + 50, nb);
		char label[96];
		uint l = 0;
		for (; l < nl && l < sizeof (label) - 1; l++)
		{
			const u8 c = a[24 + l];
			label[l] = (c >= 0x20 && c < 0x7f && c != '/' && c != '\\' && c != ':') ? (char)c : '_';
		}
		label[l] = 0;
		char name[160];
		snprintf (name, sizeof (name), "%s.dsp", *label ? label : "sound");
		for (uint j = 0; j < k; j++)
			if (!strcasecmp (entries[j].name, name))
			{
				snprintf (name, sizeof (name), "%s_%u.dsp", *label ? label : "sound", i);
				break;
			}
		const bool good = OwnedEntryAdd (entries, k, name, out, (uint)(0x60 + nb));
		FREE (out);
		if (good)
			k++;
	}
	if (k)
		err = write_owned_entries (arg, basedir, depth, "SLAMAD", entries, k);
	else
		ResetOwnedEntries (entries, nau);
done:
	FREE (raw);
	return err;
}
