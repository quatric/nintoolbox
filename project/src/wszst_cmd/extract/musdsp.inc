// ".mus" speech / music chunks of the Build-A-Bear / Alien Syndrome / Despicable Me engine (Wii, big-endian): "EF EF EF EF", 16 DSP-ADPCM coefficients (s16), u16 gain (0),
// u16 initial predictor / scale, two u16 histories, u32 data size, then the DSP-ADPCM frames (file size - 0x30). There is no sample rate field; the chunks are 10 seconds long
// at 32000 Hz (182,848 bytes = 319,984 samples), which is what the rebuilt 0x60 byte DSP header uses. Written as "<file>.d/<name>.dsp" and decoded to WAV.
static enumError extract_musdsp_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".mus"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
		return ERR_NOTHING_TO_DO;
	enumError err = ERR_NOTHING_TO_DO;
	if (sz < 0x40 || memcmp (raw, "\xef\xef\xef\xef", 4))
		goto done;
	{
		const u32 ds = rd_be32 (raw + 0x2c);
		if (!ds || ds + 0x30 > sz || ds + 0x30 + 0x40 < sz || ds % 8)
			goto done;
		u8 *buf = CALLOC (1, 0x60 + (size_t)ds);
		if (!buf)
			goto done;
		const u32 samples = ds / 8 * 14, nib = ds * 2;
		buf[0] = samples >> 24, buf[1] = samples >> 16, buf[2] = samples >> 8, buf[3] = samples;
		buf[4] = nib >> 24, buf[5] = nib >> 16, buf[6] = nib >> 8, buf[7] = nib;
		buf[8] = 0, buf[9] = 0, buf[10] = 32000 >> 8, buf[11] = 32000 & 0xff;
		buf[0x13] = 2;
		buf[0x14] = (nib - 1) >> 24, buf[0x15] = (nib - 1) >> 16, buf[0x16] = (nib - 1) >> 8, buf[0x17] = (nib - 1);
		buf[0x1b] = 2;
		memcpy (buf + 0x1c, raw + 4, 32);
		memcpy (buf + 0x3c, raw + 0x24, 8);	// gain, predictor / scale, history 1, history 2
		memcpy (buf + 0x60, raw + 0x30, ds);
		char dest[PATH_MAX], name[PATH_MAX];
		beside_source_dest (dest, sizeof (dest), arg);
		const char *sl = strrchr (arg, '/');
		snprintf (name, sizeof (name), "%s", sl ? sl + 1 : arg);
		char *dot = strrchr (name, '.');
		if (dot)
			*dot = 0;
		strncat (name, ".dsp", sizeof (name) - strlen (name) - 1);
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT MUS:%s (%u samples @ 32000 Hz) -> %s/%s\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, samples, dest, name);
		err = ERR_OK;
		if (!testmode)
		{
			err = shade_write (arg, dest, basedir, name, buf, 0x60 + ds);
			if (!err)
			{
				enumError sub = extract_tree_complete (dest, depth + 1);
				if (err < sub)
					err = sub;
			}
		}
		FREE (buf);
	}
done:
	FREE (raw);
	return err;
}
