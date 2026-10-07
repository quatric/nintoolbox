// Worms (Team17) / Activision audio banks: "X.spd" holds raw GameCube
// DSP-ADPCM frames, "X.spt" is the big-endian index:
//   u32 count; count * 28-byte records {u32 0, u32 rate, u32 0, u32 0,
//   u32 end_nibble, u32 start_nibble, u32 0}; count * 46-byte DSP blocks
//   {s16 coef[16], u16 gain, u16 initial_ps, s16 hist1, hist2, u16 loop_ps,
//   s16 loop_hist1, loop_hist2}. Each stream is written as a standard ".dsp".
static uint spt_samples (uint nib)
{
	return nib / 16 * 14 + (nib % 16 > 2 ? nib % 16 - 2 : 0);
}

static enumError extract_spt_file (ccp arg, ccp basedir, uint depth)
{
	const bool is_spd = is_ext (arg, ".spd");
	if (!is_spd && !is_ext (arg, ".spt"))
		return ERR_NOTHING_TO_DO;

	char other[PATH_MAX];
	snprintf (other, sizeof (other), "%s", arg);
	char *dot = strrchr (other, '.');
	if (!dot || strlen (dot) != 4)
		return ERR_NOTHING_TO_DO;
	dot[3] = is_spd ? 't' : 'd';

	u8 *a = 0, *b = 0;
	size_t as = 0, bs = 0;
	if (LoadFileAlloc (arg, 0, 0, &a, &as, 0, 0, 0, false) || LoadFileAlloc (other, 0, 0, &b, &bs, 0, 0, 0, false))
	{
		FREE (a);
		FREE (b);
		return ERR_NOTHING_TO_DO;
	}
	const u8 *spd = is_spd ? a : b, *spt = is_spd ? b : a;
	const size_t spd_size = is_spd ? as : bs, spt_size = is_spd ? bs : as;

	const uint count = spt_size >= 4 ? rd_be32 (spt) : 0;
	if (!count || count > 4096 || spt_size < 4 + 74ull * count || spd_size > UINT_MAX)
	{
		FREE (a);
		FREE (b);
		return ERR_NOTHING_TO_DO;
	}
	nintendo_sarc_entry_t *entries = CALLOC (count, sizeof (*entries));
	u8 *tmp = MALLOC (0x60 + (1u << 24));
	if (!entries || !tmp)
	{
		FREE (entries);
		FREE (tmp);
		FREE (a);
		FREE (b);
		return ERR_OUT_OF_MEMORY;
	}
	uint n = 0;
	for (uint i = 0; i < count; i++)
	{
		const u8 *r = spt + 4 + 28 * (size_t)i, *co = spt + 4 + 28 * (size_t)count + 46 * (size_t)i;
		const u32 rate = rd_be32 (r + 4), end = rd_be32 (r + 16), start = rd_be32 (r + 20);
		if (rate < 4000 || rate > 96000 || start >= end || end > 0x7ffffff)
			continue;
		const size_t b0 = (start & ~15u) / 2, b1 = ((size_t)(end | 15) + 1) / 2;
		if (b1 > spd_size || b1 - b0 > (1u << 24) || spd[b0] != (u8)rd_be16 (co + 34) || co[34])
			continue;
		const uint nib = (uint)(b1 - b0) * 2;
		memset (tmp, 0, 0x60);
		wr_be32 (tmp, spt_samples (end + 1) - spt_samples (start));
		wr_be32 (tmp + 4, nib);
		wr_be32 (tmp + 8, rate);
		wr_be32 (tmp + 0x10, 2);
		wr_be32 (tmp + 0x14, nib - 1);
		wr_be32 (tmp + 0x18, 2);
		memcpy (tmp + 0x1c, co, 46);
		memcpy (tmp + 0x60, spd + b0, b1 - b0);
		char name[32];
		snprintf (name, sizeof (name), "%03u.dsp", i);
		if (!OwnedEntryAdd (entries, n, name, tmp, 0x60 + (uint)(b1 - b0)))
			break;
		n++;
	}
	FREE (tmp);
	FREE (a);
	FREE (b);
	if (!n)
	{
		FREE (entries);
		return ERR_NOTHING_TO_DO;
	}
	return write_owned_entries (arg, basedir, depth, "SPD", entries, n);
}
