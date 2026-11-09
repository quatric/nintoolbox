// "Ultimate Red Ball Challenge" (Wii) music "*.mus" (hash named, Sounds/): a 4-byte marker (0xabababab, or 0xefefefef),
// 16 big-endian s16 DSP-ADPCM coefficients, {u32 id, u32 0, u32 size} and then mono Nintendo DSP-ADPCM frames (8 bytes
// each): the data is 2 * size bytes for the 0xabababab files and size bytes for 0xefefefef ones (file size - 0x30 either
// way). The sample rate is not stored in the file; 32 kHz is assumed. Written as a standard "<name>.dsp" (0x60 byte header)
// that the DSP extractor turns into WAV.
static enumError extract_redballmus_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".mus"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw || sz < 0x40 || sz > 0x20000000)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	const u32 marker = rd_be32 (raw), size = rd_be32 (raw + 0x2c);
	const size_t data = sz - 0x30;
	if ((marker != 0xabababab && marker != 0xefefefef) || rd_be32 (raw + 0x28) || size == 0
		|| data != (marker == 0xabababab ? (size_t)size * 2 : (size_t)size) || (data & 7) || (raw[0x30] >> 4) > 7)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	const u32 frames = (u32)(data / 8), samples = frames * 14, nibbles = frames * 16;
	u8 *out = CALLOC (1, 0x60 + data);
	if (!out)
	{
		FREE (raw);
		return ERR_OUT_OF_MEMORY;
	}
	wr_be32 (out + 0x00, samples);
	wr_be32 (out + 0x04, nibbles);
	wr_be32 (out + 0x08, 32000);
	wr_be32 (out + 0x10, 2);
	wr_be32 (out + 0x14, nibbles - 1);
	wr_be32 (out + 0x18, 2);
	memcpy (out + 0x1c, raw + 4, 32);
	out[0x3f] = raw[0x30];
	out[0x45] = raw[0x30];
	memcpy (out + 0x60, raw + 0x30, data);
	char dest[PATH_MAX];
	beside_source_dest (dest, sizeof (dest), arg);
	char name[PATH_MAX];
	ccp slash = strrchr (arg, '/');
	snprintf (name, sizeof (name), "%s.dsp", slash ? slash + 1 : arg);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT MUS:%s (%u samples) -> %s/%s\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, samples,
			dest, name);
	enumError err = ERR_OK;
	if (!testmode)
	{
		err = shade_write (arg, dest, basedir, name, out, (uint)(0x60 + data));
		if (!err)
		{
			const enumError sub = extract_tree_complete (dest, depth + 1);
			if (err < sub)
				err = sub;
		}
	}
	FREE (out);
	FREE (raw);
	return err;
}
