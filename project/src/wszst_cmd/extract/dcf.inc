// Speed 2 (Wii) font cache ".dcf" ("DC2\0"): big-endian {u32 magic, u32 0, u32 0x800, u32 0x100, u32
// 0x40000, u32 atlas bytes / 4, u32 0x40000} (0x1c bytes), a 1024 pixel wide GX IA4 atlas at 0x1c (atlas bytes = rows * 1024) and
// then 3 bytes before the glyph table: records {u16 code, f32 x0, y0, x1, y1,
// f32 advance} (pixel rectangles in the atlas) up to the end of the file; the count follows from the size. The atlas becomes a PNG, the glyph table "<name>.txt".
static enumError extract_dcf_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	(void)depth;
	if (!is_ext (arg, ".dcf"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	const u32 rows = sz >= 0x20 ? rd_be32 (raw + 0x14) / 256 : 0;
	const size_t body = 0x1c + 3 + (size_t)rows * 1024;
	const u32 cnt = sz > body && rows <= 4096 && (sz - body) % 22 == 0 ? (u32)((sz - body) / 22) : 0;
	if (sz < 0x20 || memcmp (raw, "DC2\0", 4) || !cnt || cnt > 0x10000 || !rows || rows % 4 || rows > 4096)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	u8 *rgba = 0;
	if (DecodeGXTexture_RGBA (&rgba, 1024, rows, 2, raw + 0x1c, (size_t)rows * 1024, 0, 0, 0) || !rgba)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	char dest[PATH_MAX], txt[PATH_MAX];
	beside_source_image_dest (dest, sizeof (dest), arg);
	beside_source_dest_ext (txt, sizeof (txt), arg, ".txt");
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT DC2-FONT:%s (1024x%u, %u glyphs) -> PNG:%s\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", arg, rows, cnt, dest);
	enumError err = ERR_OK;
	if (!testmode)
	{
		File_t F;
		err = CreateFileOpt (&F, true, txt, false, arg);
		if (F.f)
		{
			fprintf (F.f, "# code x0 y0 x1 y1 advance (atlas pixels), %u glyphs\n", cnt);
			const u8 *t = raw + sz - (size_t)cnt * 22;
			for (u32 i = 0; i < cnt; i++, t += 22)
			{
				float v[5];
				for (int k = 0; k < 5; k++)
				{
					const u32 u = rd_be32 (t + 2 + 4 * k);
					memcpy (&v[k], &u, 4);
				}
				fprintf (F.f, "%u %g %g %g %g %g\n", (uint)rd_be16 (t), (double)v[0], (double)v[1], (double)v[2],
					(double)v[3], (double)v[4]);
			}
		}
		const enumError ce = ResetFile (&F, opt_preserve);
		err = err ? err : ce;
		const enumError pe = SaveDecodedRGBAToPNG (rgba, 1024, rows, &be_func, dest, 0, opt_overwrite);
		err = err ? err : pe;
	}
	else
		FREE (rgba);
	FREE (raw);
	return err;
}
