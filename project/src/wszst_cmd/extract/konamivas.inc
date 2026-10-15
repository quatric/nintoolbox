// Konami Wii streaming bank ".vas" (Jikkyou Powerful Pro Yakyuu, "strbgm.vas"), big-endian, built from 0x800 pages:
// page 0 is the file header (u32 @4 = page count - 1, u32 @8 = stream count), then every stream is one header page
// followed by its data padded to 0x800: header {u32 interleaved data bytes @0, u32 rate @4} plus one 0x40-byte DSP
// channel header per channel at 0x80 + 0x40 * ch ({u16 loop, u16 fmt, u32 start, u32 end, u32 cur, 16 s16 coefs, ...}).
// The data holds the channels interleaved in 0x100-byte blocks. Every channel becomes "NNN_chN.dsp" (silent
// placeholder streams without an end address are skipped).
static enumError extract_konamivas_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".vas"))
		return ERR_NOTHING_TO_DO;
	FILE *in = fopen (arg, "rb");
	if (!in)
		return ERR_NOTHING_TO_DO;
	u8 h[0x10];
	if (!read_abe_at (in, 0, h, sizeof (h)) || fseek (in, 0, SEEK_END))
	{
		fclose (in);
		return ERR_NOTHING_TO_DO;
	}
	const long end = ftell (in);
	const u64 fsz = end < 0 ? 0 : (u64)end;
	const u32 count = rd_be32 (h + 8);
	if (!count || count > 4096 || ((u64)rd_be32 (h + 4) + 1) * 0x800 != fsz)
	{
		fclose (in);
		return ERR_NOTHING_TO_DO;
	}

	nintendo_sarc_entry_t *entries = CALLOC (2 * (size_t)count, sizeof (*entries));
	uint n = 0;
	u64 pos = 0x800;
	bool ok = entries != 0;
	for (uint s = 0; ok && s < count; s++)
	{
		u8 hp[0x200];
		ok = pos + 0x800 <= fsz && read_abe_at (in, pos, hp, sizeof (hp));
		if (!ok)
			break;
		const u64 total = rd_be32 (hp), rate = rd_be32 (hp + 4);
		const u64 next = pos + 0x800 + ((total + 0x7ff) & ~(u64)0x7ff);
		ok = total <= 0x10000000 && next <= fsz && rate >= 4000 && rate <= 96000;
		uint chs = 0;
		while (ok && chs < 6 && rd_be32 (hp + 0x80 + 0x40 * chs + 8))
			chs++;
		if (!ok || !chs)
		{
			pos = next;
			continue;
		}
		u8 *raw = MALLOC (total ? total : 1);
		ok = raw && read_abe_at (in, pos + 0x800, raw, total);
		for (uint c = 0; ok && c < chs; c++)
		{
			const u8 *ch = hp + 0x80 + 0x40 * c;
			const u32 nib = rd_be32 (ch + 8) + 1;
			const size_t bytes = (nib + 1) / 2;
			u8 *out = CALLOC (1, 0x60 + bytes + 8);
			if (!out)
			{
				ok = false;
				break;
			}
			size_t got = 0;
			for (u64 blk = (u64)c * 0x100; blk < total && got < bytes; blk += 0x100 * (u64)chs)
			{
				size_t m = total - blk < 0x100 ? (size_t)(total - blk) : 0x100;
				if (m > bytes - got)
					m = bytes - got;
				memcpy (out + 0x60 + got, raw + blk, m);
				got += m;
			}
			wr_be32 (out, nib / 16 * 14 + (nib % 16 > 2 ? nib % 16 - 2 : 0));
			wr_be32 (out + 4, nib);
			wr_be32 (out + 8, (u32)rate);
			memcpy (out + 0xc, ch, 0x30);
			out[0x3f] = out[0x60];
			wr_be16 (out + 0x3c, 0);
			char name[32];
			snprintf (name, sizeof (name), "%03u_ch%u.dsp", s, c);
			ok = OwnedEntryAdd (entries, n, name, out, 0x60 + bytes);
			FREE (out);
			n += ok;
		}
		FREE (raw);
		pos = next;
	}
	fclose (in);
	enumError err = ERR_NOTHING_TO_DO;
	if (ok && n && pos + 0x800 == fsz)
		err = write_owned_entries (arg, basedir, depth, "KONAMI-VAS", entries, n);
	else if (entries)
		ResetOwnedEntries (entries, n);
	return err;
}
