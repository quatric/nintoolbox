// Ubisoft/Kuju skeletal animation clips (".BAN": Sleepover Party; little endian): float duration @0, clip name[0x40] @8, float @0x48, u32 bone count @0x4c, then bone records from 0x50 (stride 0x4c or 0x50 depending on the clip) {u32 0, u32 1, name[0x20] @+8, then rate/count/flag words and a base
// translation}, followed by the packed key data (not decoded). Written as "<name>.txt": the clip header and one line per bone with its name and first words.
static enumError extract_ubi_ban_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	if (!is_ext (arg, ".BAN"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	enumError err = ERR_NOTHING_TO_DO;
	const size_t nb = sz > 0x58 ? rd_le32 (raw + 0x4c) : 0;
	size_t stride = 0;
	// the record stride varies per clip (0x4c, 0x50, ...): take the one at which every bone name is a NUL-terminated identifier
	for (size_t st = 0x40; nb && nb < 1024 && !stride && st <= 0x80; st += 4)
	{
		bool ok = 0x50 + nb * st <= sz && raw[8] >= 0x20 && raw[8] < 0x7f;
		for (size_t i = 0; ok && i < nb; i++)
		{
			const u8 *nm = raw + 0x58 + i * st;
			ok = nm + 0x20 <= raw + sz && nm[0] >= 0x20 && nm[0] < 0x7f && !nm[0x1f];
			for (size_t k = 0; ok && k < 0x20 && nm[k]; k++)
				ok = nm[k] >= 0x20 && nm[k] < 0x7f;
		}
		if (ok)
			stride = st;
	}
	if (stride)
	{
		char *txt = CALLOC (nb * 200 + 512, 1);
		if (txt)
		{
			float dur, t0;
			uint dv = rd_le32 (raw), tv = rd_le32 (raw + 0x48);
			memcpy (&dur, &dv, 4);
			memcpy (&t0, &tv, 4);
			size_t o = (size_t)sprintf (txt, "# clip %.64s\n# duration %.4f, second time %.4f, %zu bones, record stride 0x%zx, %zu bytes of packed key data after the records\n# bone then the first three words after its name (hex)\n", (ccp)raw + 8, dur, t0, nb, stride, sz - (0x50 + nb * stride));
			for (size_t i = 0; i < nb; i++)
			{
				const u8 *r = raw + 0x50 + i * stride;
				o += (size_t)sprintf (txt + o, "%s %x %x %x\n", (ccp)r + 8, rd_le32 (r + 0x28), rd_le32 (r + 0x2c), rd_le32 (r + 0x30));
			}
			char dest[PATH_MAX], name[PATH_MAX];
			beside_source_dest (dest, sizeof (dest), arg);
			const char *sl = strrchr (arg, '/');
			snprintf (name, sizeof (name), "%s", sl ? sl + 1 : arg);
			char *dot = strrchr (name, '.');
			if (dot) strcpy (dot, ".txt");
			if (verbose >= 0 || testmode)
				fprintf (stdlog, "%s%sEXTRACT UBI-BAN:%s (%zu bones) -> %s/%s\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, nb, dest, name);
			err = shade_write (arg, dest, basedir, name, (u8 *)txt, (uint)o);
			FREE (txt);
		}
	}
	FREE (raw);
	return err;
}
