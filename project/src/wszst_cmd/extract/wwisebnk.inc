// Audiokinetic Wwise sound banks (".bnk": Epic Mickey, Wii; big endian): chunks {tag, u32 size}: "BKHD"
// header, "DIDX" index of {u32 id, u32 offset, u32 size} and "DATA" holding the embedded media. A Wii
// member is a big-endian RIFX / WAVE with an extended 'fmt ' (tag 0xfff0, channels @+2, rate @+4), a
// 'WiiH' chunk (16 x s16 DSP coefficients, then gain / initial ps) and the 'data' chunk of DSP-ADPCM
// frames. Mono members are rewritten as "<id>.dsp"; any other member is written as "<id>.wem".
static enumError extract_wwisebnk_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".bnk"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	size_t didx = 0, didx_size = 0, data = 0, data_size = 0;
	if (sz < 0x30 || memcmp (raw, "BKHD", 4) || rd_be32 (raw + 4) > 0x100)
		goto bad;
	for (size_t p = 0; p + 8 <= sz;)
	{
		const size_t csz = rd_be32 (raw + p + 4);
		if (csz > sz - p - 8)
			break;
		if (!memcmp (raw + p, "DIDX", 4))
			didx = p + 8, didx_size = csz;
		else if (!memcmp (raw + p, "DATA", 4))
			data = p + 8, data_size = csz;
		p += 8 + csz;
	}
	if (!didx || !data || !didx_size || didx_size % 12)
		goto bad;
	{
		const uint n = (uint)(didx_size / 12);
		nintendo_sarc_entry_t *entries = CALLOC (n, sizeof (*entries));
		if (!entries)
		{
			FREE (raw);
			return ERR_OUT_OF_MEMORY;
		}
		uint cnt = 0;
		for (uint i = 0; i < n; i++)
		{
			const u8 *e = raw + didx + 12 * (size_t)i;
			const u32 id = rd_be32 (e);
			const size_t off = rd_be32 (e + 4), size = rd_be32 (e + 8);
			if (off > data_size || size > data_size - off)
				continue;
			const u8 *m = raw + data + off;
			const u8 *fmt = 0, *wiih = 0, *dat = 0;
			size_t dsz = 0;
			if (size >= 12 && !memcmp (m, "RIFX", 4))
				for (size_t p = 12; p + 8 <= size;)
				{
					const size_t csz = rd_be32 (m + p + 4);
					if (csz > size - p - 8)
						break;
					if (!memcmp (m + p, "fmt ", 4) && csz >= 0x0c)
						fmt = m + p + 8;
					else if (!memcmp (m + p, "WiiH", 4) && csz >= 0x22)
						wiih = m + p + 8;
					else if (!memcmp (m + p, "data", 4))
						dat = m + p + 8, dsz = csz;
					p += 8 + csz + (csz & 1);
				}
			char name[32];
			bool ok;
			if (fmt && wiih && dat && dsz >= 8 && rd_be16 (fmt) == 0xfff0 && rd_be16 (fmt + 2) == 1)
			{
				const size_t frames = dsz / 8;
				const u32 nib = (u32)frames * 16;
				u8 *out = CALLOC (1, 0x60 + frames * 8);
				if (!out)
					break;
				wr_be32 (out, (u32)frames * 14);
				wr_be32 (out + 4, nib);
				wr_be32 (out + 8, rd_be32 (fmt + 4));
				wr_be32 (out + 0x10, 2);
				wr_be32 (out + 0x14, nib - 1);
				wr_be32 (out + 0x18, 2);
				memcpy (out + 0x1c, wiih, 32);
				out[0x3f] = dat[0];
				memcpy (out + 0x60, dat, frames * 8);
				snprintf (name, sizeof (name), "%u.dsp", id);
				ok = OwnedEntryAdd (entries, cnt, name, out, 0x60 + (uint)(frames * 8));
				FREE (out);
			}
			else
			{
				snprintf (name, sizeof (name), "%u.wem", id);
				ok = OwnedEntryAdd (entries, cnt, name, m, (uint)size);
			}
			if (ok)
				cnt++;
		}
		enumError err = ERR_NOTHING_TO_DO;
		if (cnt)
			err = write_owned_entries (arg, basedir, depth, "WWISE-BNK", entries, cnt);
		else
			FREE (entries);
		FREE (raw);
		return err;
	}
bad:
	FREE (raw);
	return ERR_NOTHING_TO_DO;
}
