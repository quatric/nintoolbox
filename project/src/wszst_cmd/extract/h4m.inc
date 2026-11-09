// Hudson "HVQM4 1.3 / 1.5" movie (.h4m; GameCube / Wii: Transformers, Bionicle Heroes, Pikmin-era Hudson games). 0x44 byte header
// {magic[16], u32 0x44, u32 body size, u32 blocks (GOPs), u32 video frames, u32 audio frames, u32 usec/frame, u32 max frame size, u32 0,
// u32 audio frame size, u16 width, u16 height, u8 h/v sampling x2, u8 mode, u8 0, u8 audio channels, u8 bit depth, u8 format, u8 tracks,
// u32 sample rate}, then blocks {u32 prev size, u32 block size, u32 video frames, u32 audio frames, u32 0x01000000} holding frames
// {u16 kind (1 video, 0 audio), u16 type, u32 size, payload}. Audio frame = {u32 sample count, payload}: the first frame of every block starts
// with per channel {s16 sample (low bit = step bit 7), 7 bit index} (last channel first), after which each sample is one 4 bit IMA-style
// code per channel (last channel first, high nibble first: bit 7 = sign, bits 4-6 = magnitude). Written as "audio.wav" plus "info.txt"; the
// vector-quantised video frames are not decoded.
static const s32 h4m_steps[89] = { 7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88,
	97, 107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876, 963, 1060,
	1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845,
	8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767 };

static enumError extract_h4m_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
		return ERR_NOTHING_TO_DO;
	if (sz < 0x60 || memcmp (raw, "HVQM4 1.", 8) || rd_be32 (raw + 0x10) != 0x44 || !rd_be32 (raw + 0x18))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	const uint blocks = rd_be32 (raw + 0x18), vframes = rd_be32 (raw + 0x1c), aframes = rd_be32 (raw + 0x20);
	const uint chans = raw[0x3c], rate = rd_be32 (raw + 0x40);
	enumError err = ERR_OK;
	s16 *pcm = 0;
	size_t nsamp = 0, cap = 0;
	size_t p = 0x44;
	for (uint b = 0; b < blocks && p + 20 <= sz; b++)
	{
		const u32 bsz = rd_be32 (raw + p + 4), nv = rd_be32 (raw + p + 8), na = rd_be32 (raw + p + 12);
		size_t q = p + 20;
		const size_t end = q + bsz;
		if (end > sz)
			break;
		struct { s32 hist, idx; } cs[2] = { { 0, 0 }, { 0, 0 } };
		bool first = true;
		for (uint f = 0; f < nv + na && q + 8 <= end; f++)
		{
			const uint kind = rd_be16 (raw + q);
			const u32 fsz = rd_be32 (raw + q + 4);
			const u8 *d = raw + q + 8;
			q += 8 + (size_t)fsz;
			if (q > end)
				break;
			if (kind != 0 || !chans || chans > 2 || fsz < 4)
				continue;
			const u32 ns = rd_be32 (d);
			size_t pos = 4;
			if (ns > 0x100000)
				continue;
			if (nsamp + ns > cap)
			{
				cap = (nsamp + ns) * 2 + 4096;
				pcm = REALLOC (pcm, cap * chans * sizeof (s16));
				if (!pcm)
				{
					err = ERR_OUT_OF_MEMORY;
					goto done;
				}
			}
			uint i = 0;
			if (first)
			{
				first = false;
				if (fsz < 4 + 2 * chans)
					continue;
				for (int c = chans - 1; c >= 0; c--)
				{
					cs[c].hist = (s16)((d[pos] << 8) | (d[pos + 1] & 0x80));
					cs[c].idx = d[pos + 1] & 0x7f;
					pos += 2;
					if (cs[c].idx > 88)
						cs[c].idx = 88;
				}
				for (uint c = 0; c < chans; c++)
					pcm[nsamp * chans + c] = cs[c].hist;
				nsamp++;
				i = 1;
			}
			uint bits = 0, cur = 0;
			for (; i < ns; i++)
			{
				for (int c = chans - 1; c >= 0; c--)
				{
					if (!bits)
					{
						cur = pos < fsz ? d[pos] : 0;
						pos++;
						bits = 8;
					}
					const s32 step = h4m_steps[cs[c].idx];
					s32 dl = step >> 3;
					if (cur & 0x10) dl += step >> 2;
					if (cur & 0x20) dl += step >> 1;
					if (cur & 0x40) dl += step;
					s32 h = (cur & 0x80) ? cs[c].hist - dl : cs[c].hist + dl;
					cs[c].hist = h > 32767 ? 32767 : h < -32768 ? -32768 : h;
					static const int8_t tab[8] = { -1, -1, -1, -1, 2, 4, 6, 8 };
					cs[c].idx += tab[(cur >> 4) & 7];
					cs[c].idx = cs[c].idx > 88 ? 88 : cs[c].idx < 0 ? 0 : cs[c].idx;
					cur = (cur << 4) & 0xff;
					bits -= 4;
				}
				for (uint c = 0; c < chans; c++)
					pcm[nsamp * chans + c] = cs[c].hist;
				nsamp++;
			}
		}
		p = end;
	}
	{
		char dest[PATH_MAX];
		beside_source_dest (dest, sizeof (dest), arg);
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT HVQM4:%s (%u blocks, %u video / %u audio frames, %zu samples) -> %s/\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, blocks, vframes, aframes, nsamp, dest);
		if (testmode)
			goto done;
		char info[512];
		uint il = snprintf (info, sizeof (info), "%.9s\nsize %ux%u\nusec/frame %u\nvideo frames %u\naudio frames %u\nblocks %u\naudio channels %u\nsample rate %u\nvideo: not decoded\n",
			raw, rd_be16 (raw + 0x34), rd_be16 (raw + 0x36), rd_be32 (raw + 0x24), vframes, aframes, blocks, chans, rate);
		err = shade_write (arg, dest, basedir, "info.txt", (const u8 *)info, il);
		if (!err && nsamp && chans && chans <= 2)
		{
			const size_t n = nsamp * chans * 2;
			u8 *w = MALLOC (44 + n);
			if (!w)
			{
				err = ERR_OUT_OF_MEMORY;
				goto done;
			}
			const u32 hdr[] = { 16, (chans << 16) | 1, rate, rate * chans * 2, ((16) << 16) | (chans * 2) };
			memcpy (w, "RIFF", 4);
			wr_le32 (w + 4, (u32)(36 + n));
			memcpy (w + 8, "WAVEfmt ", 8);
			for (uint k = 0; k < 5; k++)
				wr_le32 (w + 16 + 4 * k, hdr[k]);
			memcpy (w + 36, "data", 4);
			wr_le32 (w + 40, (u32)n);
			for (size_t k = 0; k < nsamp * chans; k++)
				w[44 + 2 * k] = pcm[k] & 255, w[45 + 2 * k] = (pcm[k] >> 8) & 255;
			err = shade_write (arg, dest, basedir, "audio.wav", w, 44 + n);
			FREE (w);
		}
	}
done:
	FREE (pcm);
	FREE (raw);
	return err;
}
