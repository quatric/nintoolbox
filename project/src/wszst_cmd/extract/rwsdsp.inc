// RenderWare Audio streams ".rws" with GameCube/Wii ADPCM (*The Legend of Spyro: Dawn of the Dragon*, little-endian
// chunk tree {u32 type, u32 size, u32 0x1c020065}): 0x80d container with a 0x80e header and a 0x80f data chunk. The
// header (big-endian fields) holds a 0x20 byte segment record per sub-stream at payload + 0x60 (u32 count @ +0x20,
// {size, offset} in the last two words, contiguous and summing to the data size) and, in front of the text "00: GCN
// ADPCM Header", a DSP-like block: u32 interleave @ -52 / u32 sample rate @ -44 / u8 channels @ -31 (from the sample count) / 16 byte id /
// u32 samples / u32 nibbles / 16 coefficient shorts at +0x1e from the nibble count (counted from "GCN" : -50). The
// coefficients are shared by every segment. Mono files (speech) become "NNN.dsp" per segment (the first segment has
// the exact length, later ones use their padded size); stereo files hold one segment whose channels are interleaved
// in blocks of half the interleave size (0x4000 for the 0x8000 of the music streams) and become "ch0.dsp" / "ch1.dsp".
static enumError extract_rwsdsp_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".rws"))
		return ERR_NOTHING_TO_DO;
	u8 *d = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &d, &sz, 0, 0, 0, false) || !d)
	{
		FREE (d);
		return ERR_NOTHING_TO_DO;
	}
	const char tag[] = "GCN ADPCM Header";
	size_t gi = 0;
	for (; gi + sizeof (tag) < sz; gi++)
		if (d[gi] == 'G' && !memcmp (d + gi, tag, sizeof (tag) - 1))
			break;
	if (sz < 0x40 || rd_le32 (d) != 0x80d || rd_le32 (d + 8) != 0x1c020065 || rd_le32 (d + 12) != 0x80e
		|| gi + sizeof (tag) >= sz || gi < 0x90)
	{
		FREE (d);
		return ERR_NOTHING_TO_DO;
	}
	const size_t hsz = rd_le32 (d + 16), dh = 12 + 12 + hsz;
	const size_t coef = gi - 50, nib_at = coef - 24, smp_at = nib_at - 4;
	if (dh + 12 > sz || rd_le32 (d + dh) != 0x80f || rd_le32 (d + dh + 4) > sz - dh - 12 || smp_at < 0x18 + 0x60 + 0x38)
	{
		FREE (d);
		return ERR_NOTHING_TO_DO;
	}
	const size_t dsz = rd_le32 (d + dh + 4), dat = dh + 12;
	const u32 nseg = rd_be32 (d + 0x18 + 0x20), samples = rd_be32 (d + smp_at), nibbles = rd_be32 (d + nib_at);
	const u32 rate = rd_be32 (d + smp_at - 44), inter = rd_be32 (d + smp_at - 52);
	const uint chans = d[smp_at - 31];
	bool ok = nseg && nseg <= 4096 && 0x18 + 0x60 + 0x20 * (size_t)nseg <= smp_at && rate >= 4000 && rate <= 96000
		&& (chans == 1 || chans == 2) && samples && nibbles / 2 <= dsz;
	u32 off[4096], size[4096];
	u64 sum = 0;
	for (uint i = 0; ok && i < nseg; i++)
	{
		const u8 *r = d + 0x18 + 0x60 + 0x20 * (size_t)i;
		size[i] = rd_be32 (r + 0x18);
		off[i] = rd_be32 (r + 0x1c);
		ok = off[i] == sum && size[i] && size[i] % 8 == 0;
		sum += size[i];
	}
	if (!ok || sum != dsz || (chans == 2 && (nseg != 1 || !inter || inter % 8)))
	{
		FREE (d);
		return ERR_NOTHING_TO_DO;
	}
	const uint nout = chans == 2 ? 2 : nseg;
	nintendo_sarc_entry_t *entries = CALLOC (nout + 1, sizeof (*entries));
	uint n = 0;
	for (uint i = 0; entries && i < nout; i++)
	{
		const size_t want = chans == 2 ? ((size_t)nibbles + 1) / 2 : size[i];
		u8 *out = CALLOC (1, 0x60 + want + 8);
		if (!out)
			break;
		size_t cb = 0;
		if (chans == 2)
		{
			for (size_t base = 0; base < dsz && cb < want; base += inter)
			{
				const size_t left = dsz - base, blen = left >= inter ? inter / 2 : left / 2;
				const size_t m = blen > want - cb ? want - cb : blen;
				memcpy (out + 0x60 + cb, d + dat + base + (size_t)i * blen, m);
				cb += m;
			}
		}
		else
		{
			memcpy (out + 0x60, d + dat + off[i], size[i]);
			cb = size[i];
		}
		const u32 nb = chans == 2 || i == 0 ? nibbles : (u32)(cb * 2);
		const u32 ns = chans == 2 || i == 0 ? samples : (u32)(cb / 8 * 14);
		wr_be32 (out, ns);
		wr_be32 (out + 4, nb);
		wr_be32 (out + 8, rate);
		wr_be32 (out + 0x10, 2);
		wr_be32 (out + 0x14, nb ? nb - 1 : 0);
		wr_be32 (out + 0x18, 2);
		memcpy (out + 0x1c, d + coef, 32);
		out[0x3f] = out[0x60];
		char name[32];
		if (chans == 2)
			snprintf (name, sizeof (name), "ch%u.dsp", i);
		else
			snprintf (name, sizeof (name), "%03u.dsp", i);
		const bool added = OwnedEntryAdd (entries, n, name, out, 0x60 + (uint)cb);
		FREE (out);
		if (!added)
			break;
		n++;
	}
	FREE (d);
	enumError err = ERR_NOTHING_TO_DO;
	if (entries && n == nout)
		err = write_owned_entries (arg, basedir, depth, "RWS-DSP", entries, n);
	else if (entries)
		ResetOwnedEntries (entries, n);
	return err;
}
