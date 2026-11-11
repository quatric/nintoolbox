// Neko Entertainment "Cocoto Kart Racer 2" / "Cocoto Magic Circus" (Wii) data files ".pc" and ".GCN": {u32 BE packed size (= file size - 8), u32 BE unpacked size}
// followed by a classic LZSS stream (flag bytes read least significant bit first, bit set = literal byte; a clear bit = 2 byte token b1 b2 with
// position = b1 | (b2 & 0xf0) << 4 into a 4096 byte window that starts zero filled with the write cursor at 4078, length = (b2 & 15) + 3).
// The unpacked data is written as "<file>.d/<name>.dec".
static enumError extract_nekolzss_file (ccp arg, ccp basedir, uint depth)
{
	if ((!is_ext (arg, ".pc") && !is_ext (arg, ".gcn") && !is_ext (arg, ".GCN")) || (export_count == 0 && !opt_decode))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	enumError err = ERR_NOTHING_TO_DO;
	if (sz > 16 && rd_be32 (raw) == sz - 8 && rd_be32 (raw + 4) && rd_be32 (raw + 4) < 0x20000000)
	{
		const size_t outsize = rd_be32 (raw + 4);
		u8 *out = MALLOC (outsize);
		u8 *win = CALLOC (1, 4096);
		bool ok = out && win;
		size_t i = 8, k = 0, r = 4078;
		while (ok && k < outsize && i < sz)
		{
			const uint flags = raw[i++];
			for (uint bit = 0; bit < 8 && k < outsize; bit++)
			{
				if (flags >> bit & 1)
				{
					if (i >= sz)
						break;
					const u8 c = raw[i++];
					out[k++] = c;
					win[r] = c;
					r = (r + 1) & 4095;
				}
				else
				{
					if (i + 2 > sz)
						break;
					const uint pos = raw[i] | (raw[i + 1] & 0xf0) << 4, len = (raw[i + 1] & 15) + 3;
					i += 2;
					for (uint n = 0; n < len && k < outsize; n++)
					{
						const u8 c = win[(pos + n) & 4095];
						out[k++] = c;
						win[r] = c;
						r = (r + 1) & 4095;
					}
				}
			}
		}
		if (ok && k == outsize)
		{
			char stem[PATH_MAX], fn[PATH_MAX], dest[PATH_MAX];
			const char *sl = strrchr (arg, '/');
			snprintf (stem, sizeof (stem), "%s", sl ? sl + 1 : arg);
			char *dot = strrchr (stem, '.');
			if (dot)
				*dot = 0;
			snprintf (fn, sizeof (fn), "%s.dec", stem);
			beside_source_dest (dest, sizeof (dest), arg);
			if (verbose >= 0 || testmode)
				fprintf (stdlog, "%s%sEXTRACT NEKO-LZSS:%s -> %s/%s%s (%zu bytes)\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, dest, basedir ? basedir : "", fn, outsize);
			err = testmode ? ERR_OK : shade_write (arg, dest, basedir, fn, out, (uint)outsize);
		}
		FREE (out);
		FREE (win);
	}
	FREE (raw);
	return err;
}
