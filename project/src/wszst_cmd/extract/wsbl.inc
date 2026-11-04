// "WSBL" sound banks (49Games, Summer Athletics): big-endian {"WSBL", u32 version 1, u32 count, u32 data size},
// then count records of 0x8c bytes starting at 0x14 (rate u32 at +0x0c, float 1.0, u32 data offset, u32 samples,
// 16 big-endian DSP-ADPCM coefficients at +0x34). The ADPCM data block (data size bytes) sits at the end of the file.
// Field +0x08 is the channel count; stereo sounds hold channel 0 then channel 1 as two halves (second coefficient set at +0x30). Each channel becomes a standard "NNNN[_chN].dsp".
static enumError extract_wsbl_file (ccp arg, ccp basedir, uint depth)
{
	FILE *f = fopen (arg, "rb");
	if (!f)
		return ERR_NOTHING_TO_DO;
	u8 mg[4] = {0};
	const bool ok = fread (mg, 1, 4, f) == 4 && !memcmp (mg, "WSBL", 4);
	fclose (f);
	if (!ok)
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	const size_t cnt = sz >= 0x14 ? rd_be32 (raw + 8) : 0, dsz = sz >= 0x14 ? rd_be32 (raw + 12) : 0;
	if (sz < 0x14 || rd_be32 (raw + 4) != 1 || !cnt || cnt > 0x10000 || dsz > sz || 0x14 + cnt * 0x8c > sz - dsz)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	const u8 *data = raw + sz - dsz;
	nintendo_sarc_entry_t *entries = CALLOC (2 * cnt + 1, sizeof (*entries));
	bool good = entries != 0;
	uint n = 0;
	for (size_t i = 0; good && i < cnt; i++)
	{
		const u8 *r = raw + 0x14 + i * 0x8c;
		const uint rate = rd_be32 (r + 0x0c), nch = rd_be32 (r + 0x08);
		const size_t off = rd_be32 (r + 0x14), ns = rd_be32 (r + 0x18), fr = (ns + 13) / 14;
		if (!rate || !ns || (nch != 1 && nch != 2) || off > dsz || fr * 8 * nch > dsz - off)
			continue;
		for (uint c = 0; good && c < nch; c++)
		{
			u8 *out = CALLOC (1, 0x60 + fr * 8);
			if (!out)
			{
				good = false;
				break;
			}
			wr_be32 (out, (u32)ns);
			wr_be32 (out + 4, (u32)(fr * 16));
			wr_be32 (out + 8, rate);
			wr_be32 (out + 0x10, 2);
			wr_be32 (out + 0x14, (u32)((ns - 1) / 14 * 16 + 2 + (ns - 1) % 14));
			wr_be32 (out + 0x18, 2);
			memcpy (out + 0x1c, r + 0x34 + 0x30 * c, 32);
			memcpy (out + 0x60, data + off + fr * 8 * c, fr * 8);
			out[0x3f] = out[0x60];
			char name[32];
			if (nch == 1)
				snprintf (name, sizeof (name), "%04u.dsp", (uint)i);
			else
				snprintf (name, sizeof (name), "%04u_ch%u.dsp", (uint)i, c);
			good = OwnedEntryAdd (entries, n, name, out, (uint)(0x60 + fr * 8));
			FREE (out);
			if (good)
				n++;
		}
	}
	enumError err = ERR_NOTHING_TO_DO;
	if (good && n)
		err = write_owned_entries (arg, basedir, depth, "WSBL", entries, n);
	else
		FREE (entries);
	FREE (raw);
	return err;
}
