// Pikmin 1 ".txe" textures: u16 width, u16 height, u16 format (low byte; Pikmin's own enum: 0 RGB565, 1 CMPR,
// 2 RGB5A3, 3 I4, 4 I8, 5 IA4, 6 IA8, 7 RGBA8) + u16 + 28 bytes of padding, then GX-tiled pixel data from
// 0x20. It is rewrapped as a ".bti" beside the source so the existing BTI decoder produces the PNG.
static enumError extract_pikmin_txe_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	(void)depth;
	if (!is_ext (arg, ".txe"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	static const u8 gx[8] = { 4, 0xe, 5, 0, 1, 2, 3, 6 };
	const uint w = sz >= 0x20 ? raw[0] << 8 | raw[1] : 0, h = sz >= 0x20 ? raw[2] << 8 | raw[3] : 0;
	const uint fmt = sz >= 0x20 ? raw[5] : 99;
	uint need = 0;
	switch (fmt)
	{
	case 0: case 2: case 6: need = w * h * 2; break;
	case 1: need = w * h / 2; break;
	case 3: need = w * h / 2; break;
	case 4: case 5: need = w * h; break;
	case 7: need = w * h * 4; break;
	}
	if (!w || !h || w > 4096 || h > 4096 || !need || sz < 0x20 + (size_t)need)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	// CMPR is 8x8-tiled DXT1 data of w*h/2 bytes
	u8 *bti = CALLOC (1, 0x20 + (size_t)need);
	if (!bti)
	{
		FREE (raw);
		return ERR_OUT_OF_MEMORY;
	}
	bti[0] = gx[fmt];
	bti[1] = fmt == 2 || fmt == 5 || fmt == 6 || fmt == 7 ? 1 : 0;
	memcpy (bti + 2, raw, 4);
	bti[0x14] = bti[0x15] = 1; // linear filters
	bti[0x18] = 1; // image count
	bti[0x1f] = 0x20; // data offset
	memcpy (bti + 0x20, raw + 0x20, need);
	char dest[PATH_MAX];
	beside_source_dest_ext (dest, sizeof (dest), arg, ".bti");
	enumError err = ERR_OK;
	if (testmode)
		fprintf (stdlog, "WOULD EXTRACT TEXTURE:%s -> BTI:%s\n", arg, dest);
	else
	{
		File_t F;
		err = CreateFileOpt (&F, true, dest, false, arg);
		if (F.f && fwrite (bti, 1, 0x20 + need, F.f) != 0x20 + need)
			err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", 0x20 + need, dest);
		ResetFile (&F, opt_preserve);
		if (verbose >= 0 && !err)
			fprintf (stdlog, "%sEXTRACT TEXTURE:%s -> BTI:%s\n", verbose > 0 ? "\n" : "", arg, dest);
	}
	FREE (bti);
	FREE (raw);
	return err;
}
