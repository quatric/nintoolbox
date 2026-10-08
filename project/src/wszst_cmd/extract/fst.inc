// FAKT Software "FAST" archives (.fst; Crazy Machines): "FAST", LE u32
// version 0x30000, u32 count, u32 block size, a short index, then zlib
// streams (one per object) whose payload starts with a big-endian u64 object
// id. Sound objects carry {u32 channels, u32 rate, u32 bits, u32 format,
// u32 size, u32 size} at +0x10 and the data at +0x28 (format 2 = PCM16LE,
// 3 = Ogg Vorbis). Objects are written as "<id>.wav" / "<id>.ogg" / "<id>.bin".
static u8 *fst_inflate (const u8 *src, size_t avail, uint *out_size, size_t *used)
{
	z_stream zs;
	memset (&zs, 0, sizeof (zs));
	if (inflateInit (&zs) != Z_OK)
		return 0;
	size_t cap = avail * 4 + 4096, got = 0;
	u8 *out = MALLOC (cap);
	int r = Z_OK;
	zs.next_in = (Bytef *)src;
	zs.avail_in = (uInt)(avail > 0x7fffffff ? 0x7fffffff : avail);
	while (out && r == Z_OK)
	{
		if (got == cap)
		{
			u8 *nb = REALLOC (out, cap *= 2);
			if (!nb)
			{
				FREE (out);
				out = 0;
				break;
			}
			out = nb;
		}
		zs.next_out = out + got;
		zs.avail_out = (uInt)(cap - got);
		r = inflate (&zs, Z_NO_FLUSH);
		got = cap - zs.avail_out;
	}
	*used = zs.total_in;
	inflateEnd (&zs);
	if (r != Z_STREAM_END || !got || got > 0x10000000)
	{
		FREE (out);
		return 0;
	}
	*out_size = (uint)got;
	return out;
}

static enumError extract_fst_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".fst"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t raw_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false);
	if (err)
		return ERR_NOTHING_TO_DO;
	if (raw_size < 0x50 || raw_size > UINT_MAX || memcmp (raw, "FAST", 4) || rd_le32 (raw + 4) != 0x30000)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	nintendo_sarc_entry_t *entries = 0;
	uint n = 0, cap = 0;
	for (size_t p = 0x40; p + 8 < raw_size;)
	{
		if (raw[p] != 0x78 || (raw[p + 1] != 0xda && raw[p + 1] != 0x9c && raw[p + 1] != 0x01))
		{
			p++;
			continue;
		}
		uint osize = 0;
		size_t used = 0;
		u8 *obj = fst_inflate (raw + p, raw_size - p, &osize, &used);
		if (!obj)
		{
			p++;
			continue;
		}
		p += used;
		if (osize < 16 || rd_be32 (obj) || rd_be32 (obj + 4) > 0xfffff)
		{
			FREE (obj);
			continue;
		}
		const uint id = rd_be32 (obj + 4);
		const u8 *data = obj;
		uint dsize = osize;
		char name[48];
		snprintf (name, sizeof (name), "%05u.bin", id);
		u8 *wav = 0;
		if (osize > 0x28)
		{
			const uint ch = rd_be32 (obj + 0x10), rate = rd_be32 (obj + 0x14), bits = rd_be32 (obj + 0x18),
					   fmt = rd_be32 (obj + 0x1c), sz = rd_be32 (obj + 0x20);
			if (ch >= 1 && ch <= 8 && rate >= 4000 && rate <= 96000 && bits == 16 && sz && sz <= osize - 0x28)
			{
				if (fmt == 2)
				{
					wav = MALLOC (44 + (size_t)sz);
					if (wav)
					{
						memcpy (wav, "RIFF", 4);
						wr_le32 (wav + 4, 36 + sz);
						memcpy (wav + 8, "WAVEfmt ", 8);
						wr_le32 (wav + 16, 16);
						wav[20] = 1, wav[21] = 0, wav[22] = ch, wav[23] = 0;
						wr_le32 (wav + 24, rate);
						wr_le32 (wav + 28, rate * ch * 2);
						wav[32] = ch * 2, wav[33] = 0, wav[34] = 16, wav[35] = 0;
						memcpy (wav + 36, "data", 4);
						wr_le32 (wav + 40, sz);
						memcpy (wav + 44, obj + 0x28, sz);
						data = wav;
						dsize = 44 + sz;
						snprintf (name, sizeof (name), "%05u.wav", id);
					}
				}
				else if (fmt == 3 && !memcmp (obj + 0x28, "OggS", 4))
				{
					data = obj + 0x28;
					dsize = osize - 0x28;
					snprintf (name, sizeof (name), "%05u.ogg", id);
				}
			}
		}
		if (n == cap)
		{
			nintendo_sarc_entry_t *ne = REALLOC (entries, (cap = cap ? cap * 2 : 64) * sizeof (*ne));
			if (!ne)
			{
				FREE (obj);
				FREE (wav);
				break;
			}
			entries = ne;
		}
		if (OwnedEntryAdd (entries, n, name, data, dsize))
			n++;
		FREE (obj);
		FREE (wav);
	}
	if (!n)
	{
		FREE (entries);
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	err = write_owned_entries (arg, basedir, depth, "FST", entries, n);
	FREE (raw);
	return err;
}
