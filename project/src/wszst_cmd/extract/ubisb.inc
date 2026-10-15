// Ubisoft sound banks ".sb7" (Red Steel, Wii; version 0x00180006-8; layout after vgmstream "ubi_sb"), big-endian:
//   u32 version, section counts @4/@8/@0xc (section 1 / resource headers / subblock table), u32 extra section size
//   @0x10, two flag words, then section 1 (0x68-byte entries) at 0x1c, the 0x6c-byte resource headers, the extra
//   section, the subblock table (0x08-byte {id, size}) and the sounds.
// A type-1 resource header holds the stream size @8, extra offset @0xc (into the extra section), stream offset @0x10,
// streamed flag @0x28, software flag @0x2c, loop flag @0x30, samples @0x3c (loop start when looping) and @0x44, rate
// @0x50, channels @0x58, stream type @0x5c. Internal hardware (DSP-ADPCM) sounds sit after the subblock table (their
// offset plus the sizes of the subblocks before theirs) with the channels stored one after another (frame-aligned
// stream size / channels); the extra section holds a mini DSP header (coefficients at +0x10, history at +0x34).
// Those become "NNNN_<id>[_chN].dsp". Streamed Ubisoft IMA sounds (stream type 3) live in the external file named in
// the extra section (.ss7/.ls7 next to the bank) and are decoded natively to "NNNN_<id>.wav"; software PCM sounds,
// sequences and layers are skipped.
static const short ubisb_ima_step[89] = {7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50,
	55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494,
	544, 598, 658, 724, 796, 876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327,
	3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500,
	20350, 22385, 24623, 27086, 29794, 32767};

// Decodes one Ubisoft IMA stream (the "version 1-5" layout of vgmstream's decode_ubi_ima): u8 version, u8 0, then
// at +2: s16 header sample count @0xc (10), per channel {s16 history, s8 step index, s8 0} from +0xe, 8 more header
// bytes (+4 from version 3), the raw 16-bit header PCM samples (count * channels, big-endian below version 5), and
// then the IMA nibbles (mono: two per byte, high first; stereo: one byte per sample pair, high nibble = left,
// "multiplying" IMA expansion with the history and step index of the header). Returns the interleaved PCM or NULL.
static s16 *ubisb_decode_ima (const u8 *d, size_t size, uint ch, size_t ns)
{
	if (size < 0x30 || !d[0] || d[0] > 5 || ch < 1 || ch > 2 || !ns)
		return 0;
	const bool be = d[0] < 5;
	const size_t hdr = 2;
	const uint hs = be ? rd_be16 (d + hdr + 0xc) : rd_le16 (d + hdr + 0xc);
	size_t off = hdr + 0x0e + 0x08 + (d[0] >= 3 ? 4 : 0);
	if (!hs || hs > ns || off + (size_t)hs * ch * 2 > size)
		return 0;
	s16 *pcm = CALLOC (ns * ch, sizeof (*pcm));
	if (!pcm)
		return 0;
	for (uint c = 0; c < ch; c++)
	{
		int hist = (s16)(be ? rd_be16 (d + hdr + 0x0e + 4 * c) : rd_le16 (d + hdr + 0x0e + 4 * c));
		int step = (s8)d[hdr + 0x10 + 4 * c];
		for (size_t i = 0; i < hs; i++)
			pcm[i * ch + c] = (s16)(be ? rd_be16 (d + off + (i * ch + c) * 2) : rd_le16 (d + off + (i * ch + c) * 2));
		const size_t data = off + (size_t)hs * ch * 2;
		if (step < 0)
			step = 0;
		if (step > 88)
			step = 88;
		for (size_t i = hs; i < ns; i++)
		{
			const size_t bo = data + (ch == 1 ? (i - hs) / 2 : i - hs);
			uint nib = bo < size ? d[bo] : 0;
			nib = (ch == 1 ? ((i - hs) & 1 ? nib : nib >> 4) : (c == 0 ? nib >> 4 : nib)) & 15;
			int delta = ((int)(nib & 7) * 2 + 1) * ubisb_ima_step[step] >> 3;
			if (nib & 8)
				delta = -delta;
			hist += delta;
			hist = hist > 32767 ? 32767 : hist < -32768 ? -32768 : hist;
			static const int idx[8] = {-1, -1, -1, -1, 2, 4, 6, 8};
			step += idx[nib & 7];
			step = step < 0 ? 0 : step > 88 ? 88 : step;
			pcm[i * ch + c] = (s16)hist;
		}
	}
	return pcm;
}

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
	u8 *ext = 0;
	size_t ext_size = 0;
	char ext_name[80] = "";
	uint n = 0;
	bool good = entries != 0;
	for (size_t i = 0; i < n2 && good; i++)
	{
		const u8 *h = raw + o2 + 0x6c * i;
		if (rd_be32 (h + 4) != 1)
			continue;
		if (rd_be32 (h + 0x28) & 1)
		{
			// streamed Ubisoft IMA sound in an external file next to the bank
			const uint ch = rd_be32 (h + 0x58), rate = rd_be32 (h + 0x50), nmo = rd_be32 (h + 0x60);
			const bool loop = rd_be32 (h + 0x30) & 1;
			const size_t ns = loop ? rd_be32 (h + 0x44) + rd_be32 (h + 0x3c) : rd_be32 (h + 0x3c);
			const size_t ssz = rd_be32 (h + 8), off = rd_be32 (h + 0x10);
			if (rd_be32 (h + 0x5c) != 3 || nmo == 0xffffffffu || ox + nmo >= sz || !ns || ns > ((size_t)1 << 27) || !rate)
				continue;
			char nm[PATH_MAX];
			snprintf (nm, sizeof (nm), "%.*s", (int)(sz - ox - nmo > 64 ? 64 : sz - ox - nmo), (ccp)raw + ox + nmo);
			if (!*nm || strchr (nm, '/') || strchr (nm, '\\') || !strcmp (nm, ".."))
				continue;
			if (!ext || strcmp (ext_name, nm))
			{
				FREE (ext);
				ext = 0;
				ext_size = 0;
				snprintf (ext_name, sizeof (ext_name), "%s", nm);
				char path[PATH_MAX];
				snprintf (path, sizeof (path), "%s", arg);
				char *sl = strrchr (path, '/');
				snprintf (sl ? sl + 1 : path, sizeof (path) - (sl ? (size_t)(sl + 1 - path) : 0), "%s", nm);
				if (LoadFileAlloc (path, 0, 0, &ext, &ext_size, 0, 0, 0, false))
				{
					FREE (ext);
					ext = 0;
				}
			}
			if (!ext || off > ext_size || ssz > ext_size - off)
				continue;
			s16 *pcm = ubisb_decode_ima (ext + off, ssz, ch, ns);
			if (!pcm)
				continue;
			const size_t dsz = ns * ch * 2;
			u8 *wav = MALLOC (44 + dsz);
			if (!wav)
			{
				FREE (pcm);
				good = false;
				break;
			}
			memcpy (wav, "RIFF", 4);
			wr_le32 (wav + 4, (u32)(36 + dsz));
			memcpy (wav + 8, "WAVEfmt ", 8);
			wr_le32 (wav + 16, 16);
			wr_le16 (wav + 20, 1);
			wr_le16 (wav + 22, (u16)ch);
			wr_le32 (wav + 24, rate);
			wr_le32 (wav + 28, rate * ch * 2);
			wr_le16 (wav + 32, (u16)(ch * 2));
			wr_le16 (wav + 34, 16);
			memcpy (wav + 36, "data", 4);
			wr_le32 (wav + 40, (u32)dsz);
			for (size_t k = 0; k < ns * ch; k++)
				wr_le16 (wav + 44 + 2 * k, (u16)pcm[k]);
			FREE (pcm);
			char name[40];
			snprintf (name, sizeof (name), "%04u_%08x.wav", (uint)i, rd_be32 (h));
			good = OwnedEntryAdd (entries, n, name, wav, 44 + dsz);
			FREE (wav);
			if (good)
				n++;
			continue;
		}
		if ((rd_be32 (h + 0x2c) & 1) || rd_be32 (h + 0x5c))
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
	FREE (ext);
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
