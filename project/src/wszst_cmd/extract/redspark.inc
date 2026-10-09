// RedSpark audio (".rsd": MadWorld, Imabikisou; Wii): the header is XOR-"encrypted" with a rolling key
// ("RedSpark" magic after decryption): key0 = be32(buf) ^ "RedS", every following 32-bit word w is
// xored with key = rotl(key, 3) + key (the first word with key0 itself, then key = rotl(key, 11)).
// Decrypted base header (0x30): +0x18 data offset, +0x1c bank flag, +0x1e type (0 = no audio),
// +0x20 data size. A stream has {rate @+0xc, frames @+0x10, channels @+0x1e, loop cues @+0x1f},
// 8-byte channel configs, optional cue points, then per channel 0x2e bytes of DSP coefficients;
// the ADPCM data is interleaved per 8-byte frame. A bank lists mono sounds (header table at
// +0x20) addressed from the data offset. Every channel/sound is written as a standard DSP.
static u32 rsd_rotl (u32 x, uint r)
{
	return (x << r) | (x >> (32 - r));
}

static u32 rsd_decrypt (u8 *buf, size_t size, u32 key)
{
	size_t pos = 0;
	if (!key)
	{
		key = rd_be32 (buf) ^ 0x52656453; // "RedS"
		wr_be32 (buf, rd_be32 (buf) ^ key);
		key = rsd_rotl (key, 11);
		pos = 4;
	}
	for (size_t i = pos; i + 4 <= size; i += 4)
	{
		key = rsd_rotl (key, 3) + key;
		wr_be32 (buf + i, rd_be32 (buf + i) ^ key);
	}
	return key;
}

static bool rsd_add_dsp (nintendo_sarc_entry_t *entries, uint *n, ccp name, const u8 *coefs, uint samples,
	uint rate, const u8 *src, size_t stride, size_t bytes, size_t avail)
{
	if (!bytes || bytes > avail)
		return false;
	u8 *out = CALLOC (1, 0x60 + bytes);
	if (!out)
		return false;
	const uint nibbles = (uint)((bytes / 8) * 16);
	wr_be32 (out + 0x00, samples);
	wr_be32 (out + 0x04, nibbles);
	wr_be32 (out + 0x08, rate);
	wr_be32 (out + 0x10, 2);
	wr_be32 (out + 0x14, nibbles - 1);
	wr_be32 (out + 0x18, 2);
	memcpy (out + 0x1c, coefs, 32);
	if (stride == 8)
		memcpy (out + 0x60, src, bytes);
	else // interleaved: src points at this channel's first frame, stride = 8 * channels
		for (size_t f = 0; f < bytes / 8; f++)
			memcpy (out + 0x60 + f * 8, src + f * stride, 8);
	out[0x3f] = out[0x60];
	const bool ok = OwnedEntryAdd (entries, *n, name, out, 0x60 + bytes);
	FREE (out);
	if (ok)
		(*n)++;
	return ok;
}

static enumError extract_redspark_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".rsd"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	const u32 id0 = sz >= 0x40 ? rd_be32 (raw) : 0;
	if (id0 <= 0x15800000 || id0 >= 0x1b800000)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	u8 buf[0x3000];
	memcpy (buf, raw, 0x30);
	u32 key = rsd_decrypt (buf, 0x30, 0);
	const size_t data_off = rd_be32 (buf + 0x18);
	const size_t data_size = rd_be32 (buf + 0x20);
	const uint bank = rd_be16 (buf + 0x1c), type = rd_be16 (buf + 0x1e);
	if (memcmp (buf, "RedSpark", 8) || !type || data_off <= 0x30 || data_off >= sizeof (buf) || data_off > sz)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	memcpy (buf + 0x30, raw + 0x30, data_off - 0x30);
	rsd_decrypt (buf + 0x30, data_off - 0x30, key);
	const u8 *data = raw + data_off;
	const size_t avail = sz - data_off;

	nintendo_sarc_entry_t *entries = CALLOC (64, sizeof (*entries));
	if (!entries)
	{
		FREE (raw);
		return ERR_OUT_OF_MEMORY;
	}
	uint n = 0;
	char name[32];
	if (bank)
	{
		const uint cnt = rd_be16 (buf + 0x30 + 0x0c);
		for (uint i = 0; i < cnt && i < 64; i++)
		{
			const size_t tp = 0x30 + 0x20 + 4 * (size_t)i;
			if (tp + 4 > data_off)
				break;
			const size_t h = rd_be32 (buf + tp);
			if (h + 0x70 > data_off)
				continue;
			const u32 ssize = rd_be32 (buf + h + 4), rate = rd_be32 (buf + h + 8);
			u32 nsamp = rd_be32 (buf + h + 0x0c);
			const size_t soff = rd_be32 (buf + h + 0x10);
			if (!ssize || soff > avail)
				continue;
			if (!nsamp)
				nsamp = rd_be32 (buf + h + 0x14);
			const uint samples = nsamp / 16 * 14;
			snprintf (name, sizeof (name), "%04u.dsp", i);
			rsd_add_dsp (entries, &n, name, buf + h + 0x28, samples, rate ? rate : 24000, data + soff, 8,
				(size_t)ssize / 8 * 8, avail - soff);
		}
	}
	else
	{
		size_t hp = 0x30;
		const uint rate = rd_be32 (buf + hp + 0x0c), frames = rd_be32 (buf + hp + 0x10);
		const uint ch = buf[hp + 0x1e], cues = buf[hp + 0x1f];
		hp += 0x24 + 8 * (size_t)ch;
		if (cues)
			hp += 0x10;
		if (ch >= 1 && ch <= 4 && frames && hp + 0x2e * (size_t)ch <= data_off && (size_t)frames * 8 * ch <= avail
			&& frames * 8 * ch <= data_size + 0x10)
			for (uint c = 0; c < ch; c++)
			{
				snprintf (name, sizeof (name), "ch%u.dsp", c);
				if (!rsd_add_dsp (entries, &n, name, buf + hp + 0x2e * c, frames * 14, rate, data + 8 * c, 8 * ch,
						(size_t)frames * 8, avail))
					break;
			}
	}
	enumError err = ERR_NOTHING_TO_DO;
	if (n)
		err = write_owned_entries (arg, basedir, depth, "REDSPARK", entries, n);
	else
		FREE (entries);
	FREE (raw);
	return err;
}
