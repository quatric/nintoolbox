// Multi-stream DSP-ADPCM containers ".asf" (Brunswick Pro Bowling, Celebrity Sports Showdown and relatives on Wii),
// little-endian wrapper around big-endian DSP headers: u32 stream count @0, u32 start of the data @4, u32 size of
// the first stream @8, then per stream a 0x70-byte record at 0x0c + 0x70 * i: u32 0x0201, 4 name bytes, a standard
// 0x60-byte DSP header @+8 and {u32 offset, u32 size} @+0x68 describing the *next* stream (the first stream is at
// the data start). Every stream is written as "NNN.dsp" with its header and ADPCM data. Records flagged 0x0202 are
// stereo (*Brunswick Pro Bowling* MUSIC.ASF): the header describes one channel, both channels share it, and the data
// is interleaved in 0x4000-byte blocks; they are written as "NNN_ch0.dsp" / "NNN_ch1.dsp".
static enumError extract_asfdsp_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".asf"))
		return ERR_NOTHING_TO_DO;
	FILE *in = fopen (arg, "rb");
	if (!in)
		return ERR_NOTHING_TO_DO;
	u8 h[12];
	if (!read_abe_at (in, 0, h, sizeof (h)) || fseek (in, 0, SEEK_END))
	{
		fclose (in);
		return ERR_NOTHING_TO_DO;
	}
	const long end = ftell (in);
	const u64 fsz = end < 0 ? 0 : (u64)end;
	const uint count = rd_le32 (h);
	const u64 start = rd_le32 (h + 4), first = rd_le32 (h + 8);
	const u64 tab = 0x0c + 0x70 * (u64)count;
	u8 *rec = count && count <= 512 && start >= tab && start < fsz ? MALLOC (0x70 * (size_t)count) : 0;
	bool ok = rec && read_abe_at (in, 0x0c, rec, 0x70 * (size_t)count);
	u64 off[512], size[512];
	for (uint i = 0; ok && i < count; i++)
	{
		const u8 *r = rec + 0x70 * (size_t)i;
		off[i] = i ? rd_le32 (rec + 0x70 * (size_t)(i - 1) + 0x68) : start;
		size[i] = i ? rd_le32 (rec + 0x70 * (size_t)(i - 1) + 0x6c) : first;
		const u32 ns = rd_be32 (r + 8), nib = rd_be32 (r + 0xc), rate = rd_be32 (r + 0x10);
		ok = (rd_le32 (r) == 0x0201 || rd_le32 (r) == 0x0202) && ns && nib && rate >= 4000 && rate <= 96000 && rd_be32 (r + 0x20) == 2
			&& nib / 2 <= size[i] + 8 && (rd_le32 (r) == 0x0201 || nib <= size[i] + 8) && off[i] + size[i] <= fsz;
	}
	if (!ok)
	{
		FREE (rec);
		fclose (in);
		return ERR_NOTHING_TO_DO;
	}
	nintendo_sarc_entry_t *entries = CALLOC (2 * (size_t)count, sizeof (*entries));
	uint n = 0, done = 0;
	for (uint i = 0; entries && i < count; i++)
	{
		const u8 *r = rec + 0x70 * (size_t)i;
		const bool stereo = rd_le32 (r) == 0x0202;
		u8 *raw = CALLOC (1, size[i] + 8);
		if (!raw || !read_abe_at (in, off[i], raw, size[i]))
		{
			FREE (raw);
			break;
		}
		bool added = true;
		for (uint c = 0; c < (stereo ? 2u : 1u) && added; c++)
		{
			size_t cb = size[i], pos = 0;
			u8 *out = CALLOC (1, 0x60 + size[i] + 8);
			if (!out)
			{
				added = false;
				break;
			}
			if (stereo)
			{
				const size_t want = ((size_t)rd_be32 (r + 0xc) + 1) / 2;
				cb = 0;
				for (size_t base = 0; base < size[i] && cb < want; base += 0x8000)
				{
					const size_t r = size[i] - base, blen = r >= 0x8000 ? 0x4000 : r / 2;
					size_t m = blen > want - cb ? want - cb : blen;
					memcpy (out + 0x60 + cb, raw + base + c * blen, m);
					cb += m;
				}
				pos = cb;
			}
			else
				memcpy (out + 0x60, raw, size[i]);
			memcpy (out, r + 8, 0x60);
			if (stereo)
			{
				out[0x3e] = 0;
				out[0x3f] = out[0x60];
			}
			char name[32];
			if (stereo)
				snprintf (name, sizeof (name), "%03u_ch%u.dsp", i, c);
			else
				snprintf (name, sizeof (name), "%03u.dsp", i);
			added = OwnedEntryAdd (entries, n + c, name, out, 0x60 + (stereo ? (uint)pos : (uint)size[i]));
			FREE (out);
		}
		FREE (raw);
		if (!added)
			break;
		n += stereo ? 2 : 1;
		done++;
		continue;
	}
	FREE (rec);
	fclose (in);
	enumError err = ERR_NOTHING_TO_DO;
	if (entries && done == count)
		err = write_owned_entries (arg, basedir, depth, "ASF-DSP", entries, n);
	else if (entries)
		ResetOwnedEntries (entries, n);
	return err;
}
