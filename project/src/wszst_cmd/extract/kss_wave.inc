#pragma push_macro("calloc")
#pragma push_macro("free")
#undef calloc
#undef free
// "KSS Wave Link Data" (MLB Power Pros, Wii: sound/WaveData.bin): Konami sound system sample banks of DSP-ADPCM
// waves. Layout as found from the retail file (the format is undocumented):
//   0x00  "KSS Wave Link Data  <date>", 0x24 u32 file size
//   0x4c  16 byte bank records {u32 region table offset, u32 data offset, u32 data size, u32 (id << 16) | 1}, ended by
//         a record whose low half is not 1
//   0x1044 + 8 * (bank + 1)  u32 offset of the bank's sample descriptors (0x40 bytes each)
//   region tables  12 byte entries {u32 sample start in the bank data, u32 (tune << 16) | 0x7f00, u32 0x1f14}
//   descriptor     the DSP header from offset 0x0c on: {u16 loop flag, u16 format, u32 loop start, u32 end, u32 current,
//                  s16 coefs[16], gain, initial ps, 2 history, loop ps, 2 loop history, pad}
// Every sample (start up to the next start, the last to the end of the bank data) is written as a standard ".dsp" file
// ("bankB_sNNN.dsp"). The sample rate is not stored in any field that was identified; 32000 Hz is written.
static enumError extract_kss_wave_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".bin"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	if (sz < 0x4000 || memcmp (raw, "KSS Wave Link Data", 18) || rd_be32 (raw + 0x24) != sz)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	uint nb = 0;
	u32 A[8], B[8], C[8];
	while (nb < 7 && (rd_be32 (raw + 0x4c + 16 * nb + 12) & 0xffff) == 1)
	{
		A[nb] = rd_be32 (raw + 0x4c + 16 * nb);
		B[nb] = rd_be32 (raw + 0x4c + 16 * nb + 4);
		C[nb] = rd_be32 (raw + 0x4c + 16 * nb + 8);
		if (A[nb] + 12 > sz || (u64)B[nb] + C[nb] > sz)
		{
			FREE (raw);
			return ERR_NOTHING_TO_DO;
		}
		nb++;
	}
	if (!nb || nb > 6)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	nintendo_sarc_entry_t *entries = CALLOC (4096, sizeof (*entries));
	uint ne = 0;
	bool good = entries != 0;
	for (uint k = 0; k < nb && good; k++)
	{
		const u32 d0 = rd_be32 (raw + 0x1044 + 8 * (k + 1));
		const u32 d1 = k + 1 < nb ? rd_be32 (raw + 0x1044 + 8 * (k + 2)) : 0;
		const u32 n = k + 1 < nb ? (A[k + 1] - A[k]) / 12 : 1;
		if (d0 + (u64)n * 0x40 > sz || (d1 && d1 < d0))
			break;
		for (u32 j = 0; j < n && good && ne < 4096; j++)
		{
			const u32 s0 = rd_be32 (raw + A[k] + 12 * j);
			const u32 s1 = j + 1 < n ? rd_be32 (raw + A[k] + 12 * (j + 1)) : C[k];
			if (s1 < s0 || s1 > C[k] || s1 == s0)
				continue;
			const u8 *ds = raw + d0 + 0x40 * (size_t)j;
			const u32 end = rd_be32 (ds + 8);
			const u32 frames = end / 16, rem = end % 16;
			const u32 nib = frames * 16 + (rem ? rem + 1 : 0);
			const u32 samples = frames * 14 + (rem > 1 ? rem - 1 : 0);
			const size_t bytes = s1 - s0;
			if ((nib + 1) / 2 > bytes || !samples)
				continue;
			u8 *dsp = CALLOC (1, 0x60 + bytes);
			if (!dsp)
			{
				good = false;
				break;
			}
			wr_be32 (dsp + 0, samples);
			wr_be32 (dsp + 4, nib);
			wr_be32 (dsp + 8, 32000);
			memcpy (dsp + 0x0c, ds, 0x30); // loop flag, format, loop start, end, current address, coefficients
			memcpy (dsp + 0x3c, ds + 0x30, 0x10); // gain, initial ps/history, loop ps/history
			memcpy (dsp + 0x60, raw + B[k] + s0, bytes);
			char name[48];
			snprintf (name, sizeof (name), "bank%u_s%03u.dsp", k, j);
			good = OwnedEntryAdd (entries, ne++, name, dsp, 0x60 + bytes);
			free (dsp);
		}
	}
	enumError err = ERR_NOTHING_TO_DO;
	if (good && ne)
		err = write_owned_entries (arg, basedir, depth, "KSS-WAVE", entries, ne);
	else
		FREE (entries);
	FREE (raw);
	return err;
}
#pragma pop_macro("free")
#pragma pop_macro("calloc")
