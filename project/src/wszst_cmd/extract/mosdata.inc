// Starbreeze (Enclave: Shadows of Twilight, Wii) "MOS DATAFILE2.0" containers. "compiledtextures.xtc": little-endian file header (0x30 bytes), then texture records {u32 mip offsets[n]} whose entries are 0x28-byte headers {0x200, 0x5000 (GX CMPR), size+0x10, w, h, 0x20/0x800, 0, data size, 0, 0x10} followed by the pixel data; the mip chain is stored largest first.
// Records are located by scanning (the file also holds other record kinds that are skipped). The top mip of each CMPR texture is wrapped in a TPL "<file>.d/tex_NNNN_WxH.tpl" and decoded to PNG by the TPL handler.
static bool mos_valid_rec (const u8 *d, size_t sz, size_t p, uint *cnt_out, size_t *end_out)
{
	if (p + 0x30 > sz)
		return false;
	const u32 o0 = rd_le32 (d + p);
	if (o0 < p + 8 || o0 - p > 0x40 || (o0 - p) & 3 || (size_t)o0 + 0x28 > sz)
		return false;
	const u32 *h = 0;
	(void)h;
	const u8 *hd = d + o0;
	const u32 w = rd_le32 (hd + 12), ht = rd_le32 (hd + 16);
	if (rd_le32 (hd) != 0x200 || rd_le32 (hd + 4) != 0x5000 || rd_le32 (hd + 36) != 0x10 || !w || (w & (w - 1)) || !ht || (ht & (ht - 1)) || w > 4096 || ht > 4096)
		return false;
	const uint cnt = (o0 - (uint)p) / 4;
	const u32 last = rd_le32 (d + p + (size_t)(cnt - 1) * 4);
	if ((size_t)last + 0x28 > sz)
		return false;
	const size_t end = (size_t)last + 0x28 + rd_le32 (d + last + 28);
	if (end > sz || last < o0)
		return false;
	*cnt_out = cnt;
	*end_out = end;
	return true;
}

static enumError extract_mosdata_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	u8 *raw = 0;
	size_t sz = 0;
	if (!is_ext (arg, ".xtc") || LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
		return ERR_NOTHING_TO_DO;
	enumError err = ERR_NOTHING_TO_DO;
	if (sz < 0x100 || memcmp (raw, "MOS DATAFILE2.0", 15))
		goto done;
	{
		char dest[PATH_MAX];
		beside_source_dest (dest, sizeof (dest), arg);
		uint n = 0;
		size_t p = 0x30;
		while (p + 0x30 < sz)
		{
			uint cnt;
			size_t end;
			if (!mos_valid_rec (raw, sz, p, &cnt, &end))
			{
				p += 4;
				continue;
			}
			const u8 *hd = raw + rd_le32 (raw + p);
			const uint w = rd_le32 (hd + 12), h = rd_le32 (hd + 16), len = rd_le32 (hd + 28);
			if (!n && (verbose >= 0 || testmode))
				fprintf (stdlog, "%s%sEXTRACT MOSDATA:%s -> %s/\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, dest);
			err = ERR_OK;
			if (!testmode && len >= (size_t)w * h / 2 && w >= 8 && h >= 8)
			{
				const size_t dl = (size_t)w * h / 2;
				u8 *tpl = MALLOC (0x60 + dl);
				memset (tpl, 0, 0x60);
				wr_be32 (tpl, 0x0020af30); wr_be32 (tpl + 4, 1); wr_be32 (tpl + 8, 0x0c);
				wr_be32 (tpl + 0x0c, 0x14);
				tpl[0x14] = h >> 8; tpl[0x15] = h; tpl[0x16] = w >> 8; tpl[0x17] = w;
				wr_be32 (tpl + 0x18, 14); wr_be32 (tpl + 0x1c, 0x60);
				wr_be32 (tpl + 0x28, 1); wr_be32 (tpl + 0x2c, 1);
				memcpy (tpl + 0x60, hd + 0x28, dl);
				char path[PATH_MAX];
				snprintf (path, sizeof (path), "%s/tex_%04u_%ux%u.tpl", dest, n, w, h);
				File_t F;
				err = CreateFileOpt (&F, true, path, false, arg);
				if (F.f && fwrite (tpl, 1, 0x60 + dl, F.f) != 0x60 + dl)
					err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %zu bytes failed: %s\n", 0x60 + dl, path);
				ResetFile (&F, opt_preserve);
				FREE (tpl);
				if (!err)
				{
					const enumError sub = extract_one_file (path, 0, depth + 1);
					if (sub != ERR_NOTHING_TO_DO && err < sub)
						err = sub;
				}
			}
			if (err)
				break;
			n++;
			p = end;
		}
		if (!n)
			err = ERR_NOTHING_TO_DO;
	}
done:
	FREE (raw);
	return err;
}
