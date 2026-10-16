// Ubisoft "DARE" sound packages (".pk", localised ".lpk" with their ".spk" stream files next to them) of the Wii
// games Rayman Raving Rabbids: TV Party (0x1f0008), Just Dance / We Dare (0x22000d), Avatar (0x22f015, 0x220015) and Prince
// of Persia: The Forgotten Sands (0x22001b). The layout follows vgmstream's "ubi_bao" (see docs/licenses):
//   u8 1, u24 version, u32le index size @4, u32le resource table offset @8; at 0x40 index_size / 8 {u32le id,
//   u32le size} pairs, the big-endian BAOs (binary audio objects) follow back to back. Resource table: u32le
//   count, u32le string size, strings, then count {id, name offset, offset, size} for the external streams.
//   A BAO is {u32 version, ..., u32 class @0x20} and a class 0x20000000 header BAO continues at +0x28 with
//   {u32 id, u32 type (1 = audio), u32 stream size @+8, u32 stream id @+0x1c, u32 stream flag @+0x28, loop @+0x34,
//   channels @+0x44, rate @+0x4c, samples @+0x54 and +0x5c, codec @+0x64 (9 = DSP-ADPCM), prefetch size @+0x74}.
//   The base header is 0xa4 bytes (+4 when the next word is -1, 0 or 1), followed by the cue table and, for DSP,
//   one 0x40-byte mini header per channel (coefficients at +0x10, history at +0x34). The channels of the audio
//   data are stored one after another, in the memory BAO with the stream id (after 0x28 bytes, or after the header
//   when it is the header BAO itself) and/or in the external stream file (after 0x28 bytes). Audio BAOs with
//   DSP codec are written as standard "<id>.dsp" ("<id>_chN.dsp" for several channels); other codecs, sequences
//   and layers are skipped.
static const int ubi_ima_steps[89] = { 7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55,
	60, 66, 73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544,
	598, 658, 724, 796, 876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660,
	4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350,
	22385, 24623, 27086, 29794, 32767 };
static const int ubi_ima_index[16] = { -1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8 };

// Ubisoft IMA (BAO codec 3), layout after vgmstream's decode_ubi_ima: a small header (version byte, 0 or the
// version followed by 1 pad byte; header sample count @+0x0c, per channel s16 history @+0x0e / s8 step index
// @+0x10 in steps of 4), 10 interleaved PCM frames, then one byte per frame (high nibble = left, low = right)
// for stereo or two nibbles per byte (high first) for mono, with the MUL form of the IMA expansion.
// Returns the number of frames written to OUT (interleaved s16), or 0 when the header is not understood.
static size_t ubibao_decode_ima (const u8 *d, size_t sz, uint ch, size_t want, s16 *out)
{
	if (sz < 0x20 || ch < 1 || ch > 2)
		return 0;
	uint ver = d[0];
	size_t off = 0;
	bool be = false;
	if (!ver)
		ver = 7;
	else
	{
		off = 2;
		be = ver < 5;
	}
#define UIMA16(o) ((int)(s16)(be ? (d[o] << 8 | d[(o) + 1]) : (d[(o) + 1] << 8 | d[o])))
	if (off + 0x16 + 4 > sz)
		return 0;
	const size_t hs = (size_t)UIMA16 (off + 0x0c);
	int hist[2] = { 0, 0 }, step[2] = { 0, 0 };
	for (uint c = 0; c < ch; c++)
	{
		hist[c] = UIMA16 (off + 0x0e + c * 4);
		step[c] = (int)(int8_t)d[off + 0x10 + c * 4];
	}
	off += 0x16;
	if (ver >= 3)
		off += 4;
	if (ver == 6)
	{
		if (off + 8 > sz)
			return 0;
		off += 8 + (size_t)(be ? rd_be32 (d + off + 4) : rd_le32 (d + off + 4));
	}
	else if (ver >= 7)
	{
		off += 6;
		if (off + 4 > sz)
			return 0;
		const u32 ent = be ? rd_be32 (d + off) : rd_le32 (d + off);
		if (ent)
			off += 4 + ((size_t)ent + 1) * 8;
	}
	if (hs > 64 || off + hs * ch * 2 > sz)
		return 0;
	size_t n = 0;
	for (; n < hs && n < want; n++)
		for (uint c = 0; c < ch; c++)
			out[n * ch + c] = (s16)UIMA16 (off + (n * ch + c) * 2);
	off += hs * ch * 2;
#undef UIMA16
	for (; n < want; n++)
	{
		const size_t i = n - hs;
		const size_t bo = off + (ch == 1 ? i / 2 : i);
		if (bo >= sz)
			break;
		for (uint c = 0; c < ch; c++)
		{
			const uint code = ch == 1 ? (i & 1 ? d[bo] & 15 : d[bo] >> 4) : (c == 0 ? d[bo] >> 4 : d[bo] & 15);
			if (step[c] < 0)
				step[c] = 0;
			if (step[c] > 88)
				step[c] = 88;
			int delta = (((int)(code & 7) * 2 + 1) * ubi_ima_steps[step[c]]) >> 3;
			if (code & 8)
				delta = -delta;
			hist[c] += delta;
			if (hist[c] > 32767)
				hist[c] = 32767;
			if (hist[c] < -32768)
				hist[c] = -32768;
			step[c] += ubi_ima_index[code];
			if (step[c] < 0)
				step[c] = 0;
			if (step[c] > 88)
				step[c] = 88;
			out[n * ch + c] = (s16)hist[c];
		}
	}
	return n;
}

static bool ubibao_find_bao (const u8 *raw, size_t sz, uint idxsize, uint id, size_t *off, size_t *size)
{
	size_t o = 0x40 + idxsize;
	for (uint i = 0; i < idxsize / 8; i++)
	{
		const size_t bs = rd_le32 (raw + 0x40 + 8 * (size_t)i + 4);
		if (rd_le32 (raw + 0x40 + 8 * (size_t)i) == id)
		{
			*off = o;
			*size = bs;
			return o + bs <= sz;
		}
		o += bs;
	}
	return false;
}

static enumError extract_ubibao_file (ccp arg, ccp basedir, uint depth)
{
	if (!(is_ext (arg, ".pk") || is_ext (arg, ".lpk")))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	const uint ver = sz >= 0x40 ? rd_be32 (raw) & 0xffffff : 0;
	const uint idxsize = sz >= 0x40 ? rd_le32 (raw + 4) : 0, ext = sz >= 0x40 ? rd_le32 (raw + 8) : 0;
	const bool known = ver == 0x1f0008 || ver == 0x22000d || ver == 0x22f015 || ver == 0x220015 || ver == 0x22001b;
	if (sz < 0x40 || raw[0] != 1 || !known || idxsize > 80000 || idxsize % 8 || 0x40 + (u64)idxsize > sz
		|| (u64)ext + 8 > sz)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	const uint extn = rd_le32 (raw + ext), extstr = rd_le32 (raw + ext + 4);
	const u64 exttab = (u64)ext + 8 + extstr;
	if (extn > 100000 || extstr > sz || exttab + 16 * (u64)extn > sz)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	char dir[PATH_MAX];
	snprintf (dir, sizeof (dir), "%s", arg);
	char *sl = strrchr (dir, '/');
	if (sl)
		sl[1] = 0;
	else
		dir[0] = 0;

	nintendo_sarc_entry_t *entries = CALLOC (idxsize / 8 * 8 + 8, sizeof (*entries));
	uint n = 0;
	size_t o = 0x40 + idxsize;
	for (uint bi = 0; entries && bi < idxsize / 8; o += rd_le32 (raw + 0x40 + 8 * (size_t)bi + 4), bi++)
	{
		const size_t bs = rd_le32 (raw + 0x40 + 8 * (size_t)bi + 4);
		if (o + bs > sz || bs < 0x80 || rd_be32 (raw + o + 0x20) != 0x20000000 || rd_be32 (raw + o + 0x2c) != 1)
			continue;
		const u8 *h = raw + o + 0x28;
		const uint hid = rd_be32 (h), ssize = rd_be32 (h + 8), sid = rd_be32 (h + 0x1c), ch = rd_be32 (h + 0x44),
			   rate = rd_be32 (h + 0x4c), codec = rd_be32 (h + 0x64), pre = rd_be32 (h + 0x74);
		const bool stream = rd_be32 (h + 0x28) & 1, loop = rd_be32 (h + 0x34) & 1, prefetch = pre > 0;
		if ((codec != 9 && codec != 1 && codec != 3) || !ch || ch > 8 || !rate || rate > 200000 || !ssize)
			continue;
		size_t hsize = 0xa4;
		if (o + hsize + 4 <= sz && (int)rd_be32 (raw + o + hsize) >= -1 && (int)rd_be32 (raw + o + hsize) <= 1)
			hsize += 4;
		size_t extra = 0;
		if (ver == 0x22000d)
			extra = (size_t)rd_be32 (h + 0x68) * 8 + rd_be32 (h + 0x6c);
		const size_t mini = o + hsize + extra;
		if (codec == 9 && mini + 0x40 * (size_t)ch > o + bs && mini + 0x40 * (size_t)ch > sz)
			continue;
		const int s1 = (int)rd_be32 (h + 0x54), s2 = (int)rd_be32 (h + 0x5c);
		const int ls = loop ? s1 : 0, ns = loop ? s1 + s2 : s1;
		if (ns <= 0 || ls < 0)
			continue;

		// gather the audio data: memory part and external stream part
		u8 *data = MALLOC (ssize + 0x100);
		size_t have = 0;
		bool ok = data != 0;
		const bool load_mem = prefetch || !stream, load_str = (stream && !prefetch) || (prefetch && ssize > pre);
		if (ok && load_mem)
		{
			size_t ro, rs;
			const size_t skip = sid == hid && (!stream || prefetch) ? hsize + extra : 0x28;
			size_t want = prefetch ? pre : ssize;
			ok = ubibao_find_bao (raw, sz, idxsize, sid, &ro, &rs) && rs >= skip;
			if (ok && !prefetch && want > rs - skip)
				want = rs - skip;
			if (ok)
			{
				memcpy (data, raw + ro + skip, want);
				have = want;
			}
		}
		if (ok && load_str)
		{
			ok = false;
			for (uint k = 0; k < extn; k++)
			{
				const u8 *e = raw + exttab + 16 * (size_t)k;
				const uint no = rd_le32 (e + 4);
				if (rd_le32 (e) != sid || ext + 8 + (u64)no >= sz || !memchr (raw + ext + 8 + no, 0, sz - ext - 8 - no))
					continue;
				char path[PATH_MAX];
				snprintf (path, sizeof (path), "%s%s", dir, raw + ext + 8 + no);
				FILE *f = fopen (path, "rb");
				const size_t want = ssize - (prefetch ? pre : 0);
				if (f && !fseeko (f, (off_t)rd_le32 (e + 8) + 0x28, SEEK_SET) && have + want <= ssize + 0x100)
					ok = fread (data + have, 1, want, f) == want;
				if (f)
					fclose (f);
				if (ok)
					have += want;
				break;
			}
		}
		const size_t per = have / ch;
		if (!ok || per < 8)
		{
			FREE (data);
			continue;
		}
		if (codec == 3)
		{
			s16 *pcm = ns > 0 ? MALLOC ((size_t)ns * ch * 2) : 0;
			const size_t frames = pcm ? ubibao_decode_ima (data, have, ch, (size_t)ns, pcm) : 0;
			const size_t dsz = frames * 2 * ch;
			u8 *wav = frames ? MALLOC (44 + dsz) : 0;
			char name[40];
			snprintf (name, sizeof (name), "%08x.wav", hid);
			if (wav)
			{
				memcpy (wav, "RIFF", 4);
				wr_le32 (wav + 4, (u32)(36 + dsz));
				memcpy (wav + 8, "WAVEfmt ", 8);
				wr_le32 (wav + 16, 16);
				wr_le16 (wav + 20, 1);
				wr_le16 (wav + 22, ch);
				wr_le32 (wav + 24, rate);
				wr_le32 (wav + 28, rate * 2 * ch);
				wr_le16 (wav + 32, 2 * ch);
				wr_le16 (wav + 34, 16);
				memcpy (wav + 36, "data", 4);
				wr_le32 (wav + 40, (u32)dsz);
				for (size_t k = 0; k < frames * ch; k++)
					wr_le16 (wav + 44 + 2 * k, (u16)pcm[k]);
				if (OwnedEntryAdd (entries, n, name, wav, 44 + dsz))
					n++;
				FREE (wav);
			}
			FREE (pcm);
			FREE (data);
			continue;
		}
		if (codec == 1)
		{
			// little-endian 16 bit PCM, channels interleaved sample by sample (Just Dance music streams)
			size_t frames = have / (2 * (size_t)ch);
			if (frames > (size_t)ns)
				frames = (size_t)ns;
			const size_t dsz = frames * 2 * ch;
			u8 *wav = frames ? MALLOC (44 + dsz) : 0;
			char name[40];
			snprintf (name, sizeof (name), "%08x.wav", hid);
			if (wav)
			{
				memcpy (wav, "RIFF", 4);
				wr_le32 (wav + 4, (u32)(36 + dsz));
				memcpy (wav + 8, "WAVEfmt ", 8);
				wr_le32 (wav + 16, 16);
				wr_le16 (wav + 20, 1);
				wr_le16 (wav + 22, ch);
				wr_le32 (wav + 24, rate);
				wr_le32 (wav + 28, rate * 2 * ch);
				wr_le16 (wav + 32, 2 * ch);
				wr_le16 (wav + 34, 16);
				memcpy (wav + 36, "data", 4);
				wr_le32 (wav + 40, (u32)dsz);
				memcpy (wav + 44, data, dsz);
				if (OwnedEntryAdd (entries, n, name, wav, 44 + dsz))
					n++;
				FREE (wav);
			}
			FREE (data);
			continue;
		}
		for (uint c = 0; c < ch; c++)
		{
			u8 *out = CALLOC (1, 0x60 + per);
			if (!out)
				break;
			const u8 *mh = raw + mini + 0x40 * (size_t)c;
			const u8 *d = data + per * c;
			wr_be32 (out, (u32)ns);
			wr_be32 (out + 4, (u32)(per * 2));
			wr_be32 (out + 8, rate);
			wr_be16 (out + 0x0c, loop);
			wr_be32 (out + 0x10, loop ? (u32)(ls / 14 * 16 + 2 + ls % 14) : 2);
			wr_be32 (out + 0x14, loop ? (u32)((ns - 1) / 14 * 16 + 2 + (ns - 1) % 14) : (u32)(per * 2 - 1));
			wr_be32 (out + 0x18, 2);
			memcpy (out + 0x1c, mh + 0x10, 0x20);
			out[0x3f] = d[0];
			memcpy (out + 0x40, mh + 0x34, 4);
			if (loop && (size_t)ls / 14 * 8 < per)
				out[0x45] = d[(size_t)ls / 14 * 8];
			wr_be16 (out + 0x4a, 1);
			memcpy (out + 0x60, d, per);
			char name[40];
			if (ch == 1)
				snprintf (name, sizeof (name), "%08x.dsp", hid);
			else
				snprintf (name, sizeof (name), "%08x_ch%u.dsp", hid, c);
			if (OwnedEntryAdd (entries, n, name, out, 0x60 + per))
				n++;
			FREE (out);
		}
		FREE (data);
	}
	enumError err = ERR_NOTHING_TO_DO;
	if (entries && n)
		err = write_owned_entries (arg, basedir, depth, "UBI-BAO", entries, n);
	else
		FREE (entries);
	FREE (raw);
	return err;
}
