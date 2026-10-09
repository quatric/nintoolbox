// tri-Crescendo "CAF " streamed music (Fragile Dreams: Farewell Ruins of the Moon, Wii; extensionless
// files, ".caf" or ".cfn"): a chain of blocks, each starting with its own header:
//   +0x00 "CAF ", +0x04 block size, +0x08 block number, +0x10/+0x14 channel 0 offset/size,
//   +0x18/+0x1c channel 1 offset/size (offsets relative to the block), +0x20 loop block number,
//   then per channel (0x2c bytes from +0x28): {u32 0, u32 initial ps, s16 hist1, hist2, 16 coefs}.
// The channels' ADPCM chunks are concatenated into two standard 32000 Hz DSP files ch0/ch1.dsp.
static enumError extract_caf_file (ccp arg, ccp basedir, uint depth)
{
	ccp base = strrchr (arg, '/');
	base = base ? base + 1 : arg;
	if (strchr (base, '.') && !is_ext (arg, ".caf") && !is_ext (arg, ".cfn"))
		return ERR_NOTHING_TO_DO;
	struct stat st;
	if (stat (arg, &st) || st.st_size < 0x800 || st.st_size > 0x10000000)
		return ERR_NOTHING_TO_DO;
	FILE *fp = fopen (arg, "rb");
	char magic[4] = {0};
	const bool is_caf = fp && fread (magic, 1, 4, fp) == 4 && !memcmp (magic, "CAF ", 4);
	if (fp)
		fclose (fp);
	if (!is_caf)
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	size_t total[2] = {0, 0}, ns_sum = 0;
	size_t off = 0;
	bool good = true;
	u32 loop_block = 0xffffffff;
	size_t loop_bytes = 0;
	while (off + 0x80 <= sz && !memcmp (raw + off, "CAF ", 4))
	{
		const size_t bs = rd_be32 (raw + off + 4);
		if (bs < 0x80)
		{
			good = false;
			break;
		}
		if (rd_be32 (raw + off + 8) == rd_be32 (raw + off + 0x20) && loop_block == 0xffffffff)
		{
			loop_block = rd_be32 (raw + off + 8);
			loop_bytes = total[0];
		}
		for (uint c = 0; c < 2; c++)
		{
			const size_t co = rd_be32 (raw + off + 0x10 + 8 * c), cs = rd_be32 (raw + off + 0x14 + 8 * c);
			if (co + cs > bs || off + co + cs > sz)
				good = false;
			else
			{
				total[c] += cs;
				if (!c)
					ns_sum += cs / 8 * 14; // every block counts whole frames
			}
		}
		if (!good)
			break;
		off += bs;
	}
	const size_t per = (total[0] + 7) / 8 * 8;
	if (!good || !total[0] || total[1] != total[0] || !ns_sum)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	nintendo_sarc_entry_t *entries = CALLOC (2, sizeof (*entries));
	uint n = 0;
	for (uint c = 0; c < 2 && entries; c++)
	{
		u8 *out = CALLOC (1, 0x60 + per);
		if (!out)
			break;
		const size_t ns = ns_sum;
		wr_be32 (out, (u32)ns);
		wr_be32 (out + 4, (u32)(per * 2));
		wr_be32 (out + 8, 32000);
		const bool loop = loop_block != 0xffffffff;
		const size_t ls = loop_bytes / 8 * 14;
		wr_be16 (out + 0x0c, loop);
		wr_be32 (out + 0x10, loop ? (u32)(ls / 14 * 16 + 2 + ls % 14) : 2);
		wr_be32 (out + 0x14, loop ? (u32)((ns - 1) / 14 * 16 + 2 + (ns - 1) % 14) : (u32)(per * 2 - 1));
		wr_be32 (out + 0x18, 2);
		memcpy (out + 0x1c, raw + 0x34 + 0x2c * c, 32);
		size_t pos = 0;
		for (off = 0; off + 0x80 <= sz && !memcmp (raw + off, "CAF ", 4); off += rd_be32 (raw + off + 4))
		{
			const size_t co = rd_be32 (raw + off + 0x10 + 8 * c), cs = rd_be32 (raw + off + 0x14 + 8 * c);
			memcpy (out + 0x60 + pos, raw + off + co, cs);
			pos += cs;
		}
		out[0x3f] = out[0x60];
		char name[16];
		snprintf (name, sizeof (name), "ch%u.dsp", c);
		const bool added = OwnedEntryAdd (entries, n, name, out, 0x60 + per);
		FREE (out);
		if (!added)
			break;
		n++;
	}
	enumError err = ERR_NOTHING_TO_DO;
	if (entries && n == 2)
		err = write_owned_entries (arg, basedir, depth, "CAF", entries, n);
	else
		FREE (entries);
	FREE (raw);
	return err;
}
