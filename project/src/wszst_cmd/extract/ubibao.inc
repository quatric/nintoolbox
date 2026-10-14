// Ubisoft "DARE" sound packages (".pk", localised ".lpk" with their ".spk" stream files next to them) of the Wii
// games Rayman Raving Rabbids: TV Party (0x1f0008), Just Dance / We Dare (0x22000d), Avatar (0x22f015) and Prince
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
	const bool known = ver == 0x1f0008 || ver == 0x22000d || ver == 0x22f015 || ver == 0x22001b;
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
		if (codec != 9 || !ch || ch > 8 || !rate || rate > 200000 || !ssize)
			continue;
		size_t hsize = 0xa4;
		if (o + hsize + 4 <= sz && (int)rd_be32 (raw + o + hsize) >= -1 && (int)rd_be32 (raw + o + hsize) <= 1)
			hsize += 4;
		size_t extra = 0;
		if (ver == 0x22000d)
			extra = (size_t)rd_be32 (h + 0x68) * 8 + rd_be32 (h + 0x6c);
		const size_t mini = o + hsize + extra;
		if (mini + 0x40 * (size_t)ch > o + bs && mini + 0x40 * (size_t)ch > sz)
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
