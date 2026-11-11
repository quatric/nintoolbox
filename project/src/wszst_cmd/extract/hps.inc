// HAL Laboratory ".hps" streamed music (Super Smash Bros. Brawl/Melee, Kirby): " HALPST\0", u32 BE rate @8, u32 channels @0xc,
// then 0x38 bytes of DSP-ADPCM channel info per channel at 0x10 (a standard DSP header from offset 0x0c: loop/format, start/end/
// current nibble addresses, 16 coefficients, gain, initial ps and history). Audio starts at 0x80 in blocks {u32 size, u32 last
// sample, u32 next block offset, per-channel ps/history, padding to 0x20} whose payload is split evenly between the channels.
// Every channel is written as "chN.dsp" (standard 0x60-byte DSP header + de-blocked ADPCM, loop flag cleared).
static enumError extract_hps_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".hps") || (export_count == 0 && !opt_decode))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw || sz < 0x80 || memcmp (raw, " HALPST", 8))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	enumError err = ERR_NOTHING_TO_DO;
	const u32 rate = rd_be32 (raw + 8), ch = rd_be32 (raw + 12);
	u8 *out[8] = {0};
	size_t used[8] = {0};
	if (!ch || ch > 8 || rate < 4000 || rate > 96000 || 0x10 + 0x38 * (size_t)ch > 0x80)
		goto done;
	const u32 nib = rd_be32 (raw + 0x10 + 4 + 4) - rd_be32 (raw + 0x10 + 4) + 1; // end - start + 1
	const size_t cap = ((size_t)nib + 1) / 2;
	for (uint c = 0; c < ch; c++)
		if (!(out[c] = CALLOC (1, 0x60 + cap + 16)))
			goto done;
	for (size_t b = 0x80; b + 0x20 <= sz;)
	{
		const u32 bs = rd_be32 (raw + b), next = rd_be32 (raw + b + 8);
		const size_t per = bs / ch;
		if (!bs || per * ch != bs || b + 0x20 + bs > sz)
			break;
		for (uint c = 0; c < ch; c++)
		{
			const size_t n = used[c] + per > cap ? cap - used[c] : per;
			memcpy (out[c] + 0x60 + used[c], raw + b + 0x20 + per * c, n);
			used[c] += n;
		}
		if (next <= b || next >= sz)
			break;
		b = next;
	}
	if (!used[0])
		goto done;
	char dest[PATH_MAX];
	beside_source_dest (dest, sizeof (dest), arg);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT HPS:%s (%u channel(s), %u Hz) -> %s/\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, ch, rate, dest);
	err = ERR_OK;
	for (uint c = 0; c < ch && !err && !testmode; c++)
	{
		const u8 *ci = raw + 0x10 + 0x38 * c;
		const u32 sa = rd_be32 (ci + 4), ea = rd_be32 (ci + 8), nb = ea - sa + 1;
		u8 *h = out[c];
		const u32 samples = nb / 16 * 14 + (nb % 16 > 2 ? nb % 16 - 2 : 0);
		h[0] = samples >> 24; h[1] = samples >> 16; h[2] = samples >> 8; h[3] = samples;
		h[4] = nb >> 24; h[5] = nb >> 16; h[6] = nb >> 8; h[7] = nb;
		h[8] = rate >> 24; h[9] = rate >> 16; h[10] = rate >> 8; h[11] = rate;
		memcpy (h + 0x0c, ci + 0, 4); // loop flag + format
		h[0x0c] = h[0x0d] = 0;
		h[0x10] = h[0x11] = h[0x12] = 0; h[0x13] = 2;
		const u32 e = nb + 1;
		h[0x14] = e >> 24; h[0x15] = e >> 16; h[0x16] = e >> 8; h[0x17] = e;
		h[0x18] = h[0x19] = h[0x1a] = 0; h[0x1b] = 2;
		memcpy (h + 0x1c, ci + 0x10, 0x28); // coefs, gain, ps, history
		char name[32];
		snprintf (name, sizeof (name), "ch%u.dsp", c);
		err = shade_write (arg, dest, basedir, name, h, 0x60 + (uint)used[c]);
	}
done:
	for (uint c = 0; c < 8; c++)
		FREE (out[c]);
	FREE (raw);
	return err;
}
