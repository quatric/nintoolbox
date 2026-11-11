// JSystem resource fonts ".bfn" ("FONTbfn1", Zelda: Twilight Princess, Super Mario Galaxy, Pikmin 2 and other JUT games):
// big-endian blocks INF1 (font type, ascent, descent, width, leading), WID1, GLY1 (glyph texture pages: first/last code,
// cell size, GX texture format I4/I8/IA4/IA8, rows x columns of cells, page size) and MAP1. Every GLY1 page becomes
// "<name>_N.png" (intensity as white with alpha for I*, kept as grey for IA*) plus "info.txt" with the metrics.
static enumError extract_bfn_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".bfn") || (export_count == 0 && !opt_decode))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw || sz < 0x40 || memcmp (raw, "FONTbfn1", 8))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	enumError err = ERR_NOTHING_TO_DO;
	char info[512];
	int il = 0;
	uint pages = 0;
	char stem[PATH_MAX], dest[PATH_MAX];
	const char *sl = strrchr (arg, '/');
	snprintf (stem, sizeof (stem), "%s", sl ? sl + 1 : arg);
	char *dot = strrchr (stem, '.');
	if (dot) *dot = 0;
	beside_source_dest (dest, sizeof (dest), arg);
	const uint nblk = rd_be32 (raw + 12);
	size_t p = 0x20;
	for (uint b = 0; b < nblk && p + 8 <= sz; b++)
	{
		const u8 *k = raw + p;
		const u32 bs = rd_be32 (k + 4);
		if (bs < 8 || p + bs > sz)
			break;
		if (!memcmp (k, "INF1", 4) && bs >= 0x14)
			il += snprintf (info + il, sizeof (info) - il, "type: %u\nascent: %u\ndescent: %u\nwidth: %u\nleading: %u\n",
				(uint)(k[8] << 8 | k[9]), (uint)(k[10] << 8 | k[11]), (uint)(k[12] << 8 | k[13]), (uint)(k[14] << 8 | k[15]), (uint)(k[16] << 8 | k[17]));
		else if (!memcmp (k, "GLY1", 4) && bs >= 0x20)
		{
			const uint first = k[8] << 8 | k[9], last = k[10] << 8 | k[11], fmt = k[0x14] << 8 | k[0x15];
			const uint tw = k[0x1a] << 8 | k[0x1b], th = k[0x1c] << 8 | k[0x1d];
			const u32 tsz = rd_be32 (k + 0x10);
			const uint bw = fmt == 0 ? 8 : fmt == 3 ? 4 : 8, bh = fmt == 0 ? 8 : 4, bpp = fmt == 0 ? 4 : fmt == 3 ? 16 : 8;
			if (fmt > 3 || !tw || !th || tw > 4096 || th > 4096 || tw % bw || th % bh || (size_t)tw * th * bpp / 8 > tsz || !tsz)
			{
				p += bs;
				continue;
			}
			il += snprintf (info + il, sizeof (info) - il, "glyph codes: %04x-%04x\ncell: %ux%u\nformat: %u\n", first, last, k[12] << 8 | k[13], k[14] << 8 | k[15], fmt);
			for (size_t off = 0x20; off + tsz <= bs; off += tsz)
			{
				u8 *rgba = CALLOC ((size_t)tw * th, 4);
				if (!rgba)
					break;
				const u8 *d = k + off;
				for (uint ty = 0; ty < th; ty += bh)
					for (uint tx = 0; tx < tw; tx += bw, d += 32)
						for (uint y = 0; y < bh; y++)
							for (uint x = 0; x < bw; x++)
							{
								uint i = 255, a = 255;
								if (fmt == 0)
								{
									const uint v = d[y * 4 + x / 2];
									a = (x & 1 ? v & 15 : v >> 4) * 17;
								}
								else if (fmt == 1)
									a = d[y * 8 + x];
								else if (fmt == 2)
								{
									const uint v = d[y * 8 + x];
									i = (v >> 4) * 17;
									a = (v & 15) * 17;
								}
								else
								{
									a = d[(y * 4 + x) * 2];
									i = d[(y * 4 + x) * 2 + 1];
								}
								u8 *o = rgba + ((size_t)(ty + y) * tw + tx + x) * 4;
								o[0] = o[1] = o[2] = i;
								o[3] = a;
							}
				char nm[PATH_MAX];
				snprintf (nm, sizeof (nm), "%s_%u", stem, pages);
				err = testmode ? ERR_OK : kyl_write_rgba_png (arg, basedir, nm, (uint)-1, tw, th, rgba);
				FREE (rgba);
				pages++;
				if (err)
					break;
			}
		}
		p += bs;
	}
	if (!err && pages && !testmode)
		err = shade_write (arg, dest, basedir, "info.txt", (u8 *)info, (uint)il);
	FREE (raw);
	return err;
}
