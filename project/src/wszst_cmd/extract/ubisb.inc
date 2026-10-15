// Ubisoft sound banks ".sb7" (Red Steel, Wii; version 0x00180006-8; layout after vgmstream "ubi_sb"), big-endian:
//   u32 version, section counts @4/@8/@0xc (section 1 / resource headers / subblock table), u32 extra section size
//   @0x10, two flag words, then section 1 (0x68-byte entries) at 0x1c, the 0x6c-byte resource headers, the extra
//   section, the subblock table (0x08-byte {id, size}) and the sounds.
// A type-1 resource header holds the stream size @8, extra offset @0xc (into the extra section), stream offset @0x10,
// streamed flag @0x28, software flag @0x2c, loop flag @0x30, samples @0x3c (loop start when looping) and @0x44, rate
// @0x50, channels @0x58, stream type @0x5c. Internal hardware (DSP-ADPCM) sounds sit after the subblock table (their
// offset plus the sizes of the subblocks before theirs) with the channels stored one after another (frame-aligned
// stream size / channels); the extra section holds a mini DSP header (coefficients at +0x10, history at +0x34).
// Those become "NNNN_<id>[_chN].dsp"; streamed sounds (external .ss7/.ls7, Ubisoft IMA), software PCM sounds, sequences
// and layers are skipped.
static enumError extract_ubisb_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".sb7"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	const uint ver = sz >= 0x20 ? rd_be32 (raw) : 0;
	if (ver < 0x00180006 || ver > 0x00180008)
		goto skip;
	const size_t n1 = rd_be32 (raw + 4), n2 = rd_be32 (raw + 8), n3 = rd_be32 (raw + 0xc), xs = rd_be32 (raw + 0x10);
	if (n1 > 0x10000 || n2 > 0x10000 || n3 > 0x10000 || !n2)
		goto skip;
	const size_t o2 = 0x1c + 0x68 * n1, ox = o2 + 0x6c * n2, o3 = ox + xs, so = o3 + 8 * n3;
	if (so > sz)
		goto skip;
	nintendo_sarc_entry_t *entries = CALLOC (n2 * 2, sizeof (*entries));
	uint n = 0;
	bool good = entries != 0;
	for (size_t i = 0; i < n2 && good; i++)
	{
		const u8 *h = raw + o2 + 0x6c * i;
		if (rd_be32 (h + 4) != 1 || (rd_be32 (h + 0x28) & 1) || (rd_be32 (h + 0x2c) & 1) || rd_be32 (h + 0x5c))
			continue;
		const size_t ssz = rd_be32 (h + 8), xo = ox + rd_be32 (h + 0xc), off = rd_be32 (h + 0x10);
		const uint ch = rd_be32 (h + 0x58), rate = rd_be32 (h + 0x50);
		const bool loop = rd_be32 (h + 0x30) & 1;
		const size_t ls = loop ? rd_be32 (h + 0x3c) : 0, ns = loop ? rd_be32 (h + 0x44) + ls : rd_be32 (h + 0x3c);
		if (!ch || ch > 2 || !rate || !ns || !ssz || xo + 0x40 * ch > sz)
			continue;
		size_t start = so + off;
		for (size_t k = 0; k < n3; k++)
		{
			const u8 *s = raw + o3 + 8 * k;
			if (!rd_be32 (s))
				break;
			start += rd_be32 (s + 4);
		}
		const size_t il = ssz / ch + 7 & ~(size_t)7;
		const size_t nibbles = ns / 14 * 16 + (ns % 14 ? ns % 14 + 2 : 0), nb = (nibbles + 1) / 2;
		if (nb > il || start > sz || il * (ch - 1) + nb > sz - start)
			continue;
		for (uint c = 0; c < ch && good; c++)
		{
			const u8 *mh = raw + xo + 0x40 * c;
			u8 *out = CALLOC (1, 0x60 + nb);
			if (!out)
			{
				good = false;
				break;
			}
			wr_be32 (out, (u32)ns);
			wr_be32 (out + 4, (u32)nibbles);
			wr_be32 (out + 8, rate);
			wr_be16 (out + 0x0c, loop);
			wr_be32 (out + 0x10, loop ? (u32)(ls / 14 * 16 + 2 + ls % 14) : 2);
			wr_be32 (out + 0x14, loop ? (u32)((ns - 1) / 14 * 16 + 2 + (ns - 1) % 14) : (u32)nibbles - 1);
			wr_be32 (out + 0x18, 2);
			memcpy (out + 0x1c, mh + 0x10, 0x20);
			memcpy (out + 0x40, mh + 0x34, 4);
			memcpy (out + 0x60, raw + start + il * c, nb);
			out[0x3f] = out[0x60];
			if (loop)
				out[0x45] = out[0x60 + (ls / 14) * 8];
			char name[40];
			if (ch == 1)
				snprintf (name, sizeof (name), "%04u_%08x.dsp", (uint)i, rd_be32 (h));
			else
				snprintf (name, sizeof (name), "%04u_%08x_ch%u.dsp", (uint)i, rd_be32 (h), c);
			good = OwnedEntryAdd (entries, n, name, out, 0x60 + nb);
			FREE (out);
			if (good)
				n++;
		}
	}
	enumError err = ERR_NOTHING_TO_DO;
	if (good && n)
		err = write_owned_entries (arg, basedir, depth, "UBISB", entries, n);
	else
		FREE (entries);
	FREE (raw);
	return err;
skip:
	FREE (raw);
	return ERR_NOTHING_TO_DO;
}
