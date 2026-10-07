// "10 Minute Solution" LZSS containers (.tplc/.gfx): {flag bytes LSB first;
// bit 1 = literal, bit 0 = u16 LE match (len = (t & 15) + 3, pos = t >> 4 into a zero-filled
// 4096-byte ring written at decoded offset & 4095)};
// the first four decoded bytes are the big-endian decoded size. A decoded TPL
// becomes "<name>.png" in decode mode (xx); anything else is saved as
// "<name>.dec" (.gfx are Scaleform GFx movies).
static u8 *tplc_unpack (const u8 *d, size_t n, size_t *out_size)
{
	if (n < 8)
		return 0;
	const size_t size = (size_t)d[1] << 24 | d[2] << 16 | d[3] << 8 | d[4];
	if (size < 8 || size > 0x4000000)
		return 0;
	u8 *out = MALLOC (size);
	u8 *ring = CALLOC (1, 4096);
	if (!out || !ring)
	{
		FREE (out);
		FREE (ring);
		return 0;
	}
	size_t o = 0, p = 0;
	while (o < size && p < n)
	{
		const uint f = d[p++];
		for (uint i = 0; i < 8 && o < size; i++)
		{
			if (f >> i & 1)
			{
				if (p >= n)
					goto bad;
				ring[o & 4095] = out[o] = d[p++];
				o++;
			}
			else
			{
				if (p + 2 > n)
					goto bad;
				const uint t = d[p] | d[p + 1] << 8;
				p += 2;
				const uint pos = t >> 4, len = (t & 15) + 3;
				for (uint k = 0; k < len && o < size; k++, o++)
					ring[o & 4095] = out[o] = ring[(pos + k) & 4095];
			}
		}
	}
	if (o != size || memcmp (out + 0, d + 1, 4))
		goto bad;
	FREE (ring);
	*out_size = size - 4;
	memmove (out, out + 4, size - 4);
	return out;
bad:
	FREE (ring);
	FREE (out);
	return 0;
}

static enumError extract_tplc_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	(void)depth;
	if (!is_ext (arg, ".tplc") && !is_ext (arg, ".gfx"))
		return ERR_NOTHING_TO_DO;
	if (export_count <= 0 && !opt_decode)
		return ERR_NOTHING_TO_DO;

	u8 *raw = 0, *dec = 0;
	size_t raw_size = 0, dec_size = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	if (raw[0] == 0xff)
		dec = tplc_unpack (raw, raw_size, &dec_size);
	FREE (raw);
	if (!dec)
		return ERR_NOTHING_TO_DO;

	const bool is_tpl = dec_size >= 0x20 && !memcmp (dec, "\x00\x20\xaf\x30", 4);
	char dest[PATH_MAX], tmp[PATH_MAX], tmp_png[PATH_MAX];
	snprintf (dest, sizeof (dest), "%s.%s", arg, is_tpl ? "png" : "dec");
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sDECODE TPLC:%s -> %s\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", arg, dest);
	if (testmode)
	{
		FREE (dec);
		return ERR_OK;
	}
	enumError err;
	if (!is_tpl)
	{
		err = SaveFile (dest, 0, 0, dec, dec_size, 0);
		FREE (dec);
		return err;
	}
	snprintf (tmp, sizeof (tmp), "%s.tplc-tmp.tpl", arg);
	snprintf (tmp_png, sizeof (tmp_png), "%s.png", tmp);
	err = SaveFile (tmp, 0, 0, dec, dec_size, 0);
	FREE (dec);
	if (!err)
		err = decode_image_if_possible (tmp);
	if (!err && rename (tmp_png, dest))
		err = ERROR1 (ERR_CANT_CREATE, "Can't create PNG: %s\n", dest);
	unlink (tmp);
	return err;
}
