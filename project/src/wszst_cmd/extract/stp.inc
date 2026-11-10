// Sonic stage object tables (Sonic and the Secret Rings / Black Knight "stgNNN_A.bin" / "stgNNN_P0..P6.bin"; magics "STA1", "STP0".."STP6"), all little endian:
// {magic, u32 object count, u32 parameter entry count (<= objects), u32 parameter bytes}, one 0x30-byte record per object {float x, y, z, float 0, 7 words (rotation as
// 16.16-ish ints, a type id, packed small fields) , u32 object index}, one 8-byte parameter header per entry {u16 kind, u16 size, u32 0} whose sizes
// add up to the parameter byte count, then the parameter payloads back to back (a short padded tail follows). "stp.txt" lists every object (position, raw words) and every parameter entry (kind, size, payload bytes); the meaning of the individual words / payloads is not decoded.
static enumError extract_stp_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	if (!is_ext (arg, ".bin"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
		return ERR_NOTHING_TO_DO;
	enumError err = ERR_NOTHING_TO_DO;
	if (sz < 0x20 || raw[0] != 'S' || raw[1] != 'T' || (raw[2] != 'P' && raw[2] != 'A') || raw[3] < '0' || raw[3] > '6')
		goto done;
	{
		const u32 n = rd_le32 (raw + 4), np = rd_le32 (raw + 8), pb = rd_le32 (raw + 12);
		if (!n || n > 1000000 || !np || np > n || 16 + 48 * (u64)n + 8 * (u64)np + pb > sz || 16 + 48 * (u64)n + 8 * (u64)np + pb + 0x80 < sz)
			goto done;
		const u8 *ph = raw + 16 + 48 * (size_t)n, *pd = ph + 8 * (size_t)np;
		u64 sum = 0;
		for (uint i = 0; i < np; i++)
			sum += ph[8 * i + 2] | ph[8 * i + 3] << 8;
		if (sum != pb)
			goto done;
		char dest[PATH_MAX];
		beside_source_dest (dest, sizeof (dest), arg);
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT %.4s:%s (%u objects) -> %s/\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", (ccp)raw, arg, n, dest);
		err = ERR_OK;
		if (!testmode)
		{
			const size_t cap = 512 + (size_t)n * 260 + (size_t)np * 40 + pb * 3;
			char *t = MALLOC (cap);
			size_t tl = snprintf (t, cap, "%.4s objects %u parameter bytes %u count2 %u\n", (ccp)raw, n, pb, rd_le32 (raw + 8));
			size_t po = 0;
			for (uint i = 0; i < n && tl + 400 < cap; i++)
			{
				const u8 *r = raw + 16 + 48 * (size_t)i;
				float f[4];
				for (uint k = 0; k < 4; k++)
				{
					const u32 v = rd_le32 (r + 4 * k);
					memcpy (&f[k], &v, 4);
				}
				tl += snprintf (t + tl, cap - tl, "%u pos %g %g %g %g w", i, f[0], f[1], f[2], f[3]);
				for (uint k = 4; k < 12; k++)
					tl += snprintf (t + tl, cap - tl, " %x", rd_le32 (r + 4 * k));
				tl += snprintf (t + tl, cap - tl, "\n");
			}
			tl += snprintf (t + tl, cap - tl, "\nparameters %u\n", np);
			for (uint i = 0; i < np && tl + 400 < cap; i++)
			{
				const uint kind = ph[8 * i] | ph[8 * i + 1] << 8, psz = ph[8 * i + 2] | ph[8 * i + 3] << 8;
				tl += snprintf (t + tl, cap - tl, "%u kind %u size %u:", i, kind, psz);
				for (uint k = 0; k < psz && po + k < pb && tl + 8 < cap; k++)
					tl += snprintf (t + tl, cap - tl, " %02x", pd[po + k]);
				po += psz;
				tl += snprintf (t + tl, cap - tl, "\n");
			}
			err = shade_write (arg, dest, basedir, "stp.txt", (const u8 *)t, (uint)tl);
			FREE (t);
		}
	}
done:
	FREE (raw);
	return err;
}
