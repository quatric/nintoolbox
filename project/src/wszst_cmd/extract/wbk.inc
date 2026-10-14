// Treyarch "NSLB" sound bank ".wbk" (Kung Fu Panda, Call of Duty 3; Wii), little-endian: "NSLB", u16 1 @4, sound
// count @0x10, name table @0x18 (u32 string offsets), sound table @0x1c (0x10-byte {info entry offset, sound offset,
// sound size, samples}), info section @0x24, strings @0x2c, in-bank data @0x34, streamed data @0x3c. An info entry
// holds the rate (u16 @0), codec (u8 @4; 0x22 = DSP), flags (u8 @5; 1 streamed -> offset into the streamed data and
// 0x8000-byte channel interleave, 2 loops), channel mask (u8 @6, 3 = stereo) and, per channel, a 0x30-byte record
// with the eight coefficient pairs stored as little-endian 32-bit words (low half = second coefficient) at +0x44 (or
// +0x4c in Call of Duty 3). Only the DSP sounds are extracted, each channel to a standalone "NNNN_name[_chN].dsp"
// with a synthesized header (zero history).
static enumError extract_wbk_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".wbk"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	if (sz < 0x40 || memcmp (raw, "NSLB", 4) || rd_le16 (raw + 4) != 1)
		goto skip;
	const size_t count = rd_le32 (raw + 0x10), ntab = rd_le32 (raw + 0x18), stab = rd_le32 (raw + 0x1c),
		     info = rd_le32 (raw + 0x24), strs = rd_le32 (raw + 0x2c), dat = rd_le32 (raw + 0x34),
		     strm = rd_le32 (raw + 0x3c);
	if (!count || count > 100000 || ntab + 4 * count > sz || stab + 0x10 * count > sz || info > sz || strs > sz)
		goto skip;
	nintendo_sarc_entry_t *entries = CALLOC (count * 2, sizeof (*entries));
	uint n = 0;
	bool good = entries != 0;
	for (size_t s = 0; s < count && good; s++)
	{
		const u8 *e = raw + stab + 0x10 * s;
		const size_t ie = info + rd_le32 (e), so0 = rd_le32 (e + 4), ssz = rd_le32 (e + 8), ns = rd_le32 (e + 12);
		if (ie + 0x50 > sz)
			continue;
		const uint rate = rd_le16 (raw + ie), codec = raw[ie + 4], flags = raw[ie + 5],
			   ch = raw[ie + 6] == 3 ? 2 : 1;
		if (codec != 0x22 || !rate || !ns || !ssz || ie + 0x4c + 0x30 * (size_t)ch + 0x20 > sz)
			continue;
		const size_t cof = rd_le32 (raw + ie + 0x44 + 0x30 * ch) ? 0x4c : 0x44;
		const size_t so = so0 + (flags & 1 ? strm : dat), per = ssz / ch, il = flags & 1 ? 0x8000 : per;
		if (!per || !il || so > sz || ssz > sz - so)
			continue;
		const size_t nibbles = ns / 14 * 16 + (ns % 14 ? ns % 14 + 2 : 0);
		if (nibbles > per * 2)
			continue;
		const size_t nb = (nibbles + 1) / 2;
		char nm[40] = "";
		const size_t no = rd_le32 (raw + ntab + 4 * s) + strs;
		for (size_t i = 0; no < sz && i < sizeof (nm) - 1 && raw[no + i]; i++)
			nm[i] = isalnum (raw[no + i]) || raw[no + i] == '_' || raw[no + i] == '-' ? raw[no + i] : '_';
		for (uint c = 0; c < ch && good; c++)
		{
			u8 *out = CALLOC (1, 0x60 + nb);
			if (!out)
			{
				good = false;
				break;
			}
			for (size_t got = 0, r = 0; got < nb; r++)
			{
				size_t len = il < nb - got ? il : nb - got;
				const size_t base = so + (r * ch + c) * il;
				if (base + len > sz)
					len = base < sz ? sz - base : 0;
				memcpy (out + 0x60 + got, raw + base, len);
				got += len;
				if (!len)
					break;
			}
			wr_be32 (out, (u32)ns);
			wr_be32 (out + 4, (u32)nibbles);
			wr_be32 (out + 8, rate);
			wr_be16 (out + 0x0c, flags & 2 ? 1 : 0);
			wr_be32 (out + 0x10, 2);
			wr_be32 (out + 0x14, (u32)nibbles - 1);
			wr_be32 (out + 0x18, 2);
			for (uint j = 0; j < 8; j++)
			{
				const u8 *w = raw + ie + cof + 0x30 * c + 4 * j;
				wr_be16 (out + 0x1c + 4 * j, rd_le16 (w + 2));
				wr_be16 (out + 0x1e + 4 * j, rd_le16 (w));
			}
			out[0x3f] = out[0x60];
			if (flags & 2)
				out[0x45] = out[0x60];
			char name[96];
			if (ch == 1)
				snprintf (name, sizeof (name), "%04u_%s.dsp", (uint)s, nm);
			else
				snprintf (name, sizeof (name), "%04u_%s_ch%u.dsp", (uint)s, nm, c);
			good = OwnedEntryAdd (entries, n, name, out, 0x60 + nb);
			FREE (out);
			if (good)
				n++;
		}
	}
	enumError err = ERR_NOTHING_TO_DO;
	if (good && n)
		err = write_owned_entries (arg, basedir, depth, "WBK", entries, n);
	else
		FREE (entries);
	FREE (raw);
	return err;
skip:
	FREE (raw);
	return ERR_NOTHING_TO_DO;
}
