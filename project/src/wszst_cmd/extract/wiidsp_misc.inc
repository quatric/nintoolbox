// Single-stream DSP-ADPCM wrappers from Wii (and GameCube) titles whose header is not a standard 0x60-byte one; every file
// becomes "<name>.dsp" (a standard header + the ADPCM data), which the DSP extractor decodes to WAV.
//   Terminal Reality "GCA1" (".gca": Metal Slug Anthology): coefficients @4 (16 x s16 BE), u32 BE nibble count @0x26, u32 BE
//     sample rate @0x2a, mono ADPCM from 0x40
//   Monster Games ".sfx" (Excite Truck, ExciteBots): u32 LE data size @0, u32 LE header size @4, loop byte @8, u16 LE codec @0xc
//     (0 = DSP-ADPCM), u16 LE channels @0xe (must be 1), s32 LE sample rate @0x10, coefficients @0x3c, mono ADPCM at the header size
static enumError wiidsp_write (ccp arg, ccp basedir, const u8 *coef, uint rate, size_t bytes, const u8 *data, ccp what)
{
	if (!bytes || !rate || rate < 4000 || rate > 96000 || bytes > 0x10000000)
		return ERR_NOTHING_TO_DO;
	u8 *out = CALLOC (1, 0x60 + bytes);
	if (!out)
		return ERR_NOTHING_TO_DO;
	const u32 nib = (u32)bytes * 2, samples = nib / 16 * 14;
	const u32 e = nib + 1;
	#define P32(o, v) out[o] = (v) >> 24, out[(o) + 1] = (v) >> 16, out[(o) + 2] = (v) >> 8, out[(o) + 3] = (v)
	P32 (0, samples); P32 (4, nib); P32 (8, rate);
	out[0x13] = 2; P32 (0x14, e); out[0x1b] = 2;
	#undef P32
	memcpy (out + 0x1c, coef, 32);
	out[0x3f] = 0; out[0x3e] = 0;
	out[0x3e] = 0; out[0x3f] = data[0]; // initial ps (u16 @0x3e)
	memcpy (out + 0x60, data, bytes);
	char dest[PATH_MAX], name[PATH_MAX];
	beside_source_dest (dest, sizeof (dest), arg);
	const char *sl = strrchr (arg, '/');
	snprintf (name, sizeof (name), "%s", sl ? sl + 1 : arg);
	char *dot = strrchr (name, '.');
	if (dot) *dot = 0;
	strncat (name, ".dsp", sizeof (name) - strlen (name) - 1);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT %s:%s -> DSP:%s/%s\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", what, arg, dest, name);
	const enumError err = testmode ? ERR_OK : shade_write (arg, dest, basedir, name, out, 0x60 + (uint)bytes);
	FREE (out);
	return err;
}

static enumError extract_wiidsp_misc_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	const bool gca = is_ext (arg, ".gca"), sfx = is_ext (arg, ".sfx");
	if ((!gca && !sfx) || (export_count == 0 && !opt_decode))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	enumError err = ERR_NOTHING_TO_DO;
	if (gca && sz > 0x44 && !memcmp (raw, "GCA1", 4))
	{
		size_t bytes = ((size_t)rd_be32 (raw + 0x26) + 1) / 2;
		if (bytes > sz - 0x40)
			bytes = sz - 0x40;
		err = wiidsp_write (arg, basedir, raw + 4, rd_be32 (raw + 0x2a), bytes, raw + 0x40, "GCA");
	}
	else if (sfx && sz > 0x80)
	{
		const u32 ds = rd_le32 (raw), hs = rd_le32 (raw + 4);
		const uint codec = raw[0xc] | raw[0xd] << 8, ch = raw[0xe] | raw[0xf] << 8;
		if (ds && hs >= 0x5c && (u64)ds + hs == sz && !codec && ch == 1 && rd_le32 (raw + 0x18) == 0x00100000 && !rd_le32 (raw + 0x1c))
			err = wiidsp_write (arg, basedir, raw + 0x3c, rd_le32 (raw + 0x10), ds, raw + hs, "Monster SFX");
	}
	FREE (raw);
	return err;
}
