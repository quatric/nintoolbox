// "PTEX" texture bundle (Virtua Tennis 2009, Sonic & Sega All-Stars Racing,
// ".dat"): big-endian {"PTEX", u32 file_size - 0x10 + 0x10, u32 data_size,
// u32 0x44332211, u32 0, u32 0, u32 count} followed by 0x20-byte records
//   {u32 name_hash, u32 kind, u16 used_w, u16 used_h, u16 w, u16 h,
//    u32 offset (relative to 0x10), f32 u_scale, f32 v_scale, u32 ?}
// kind 2 = CMPR colour plane + I4 alpha plane and kind 0 = plain CMPR at the
// padded size w x h; every texture is
// written as "<name>.d/NN_<hash>.png" (decode mode: wszst xx). kind 3 records
// are left undecoded.
static enumError ptex_write_png (ccp dir, uint index, uint hash, uint w, uint h, bool has_alpha,
	const u8 *data)
{
	char png[PATH_MAX];
	snprintf (png, sizeof (png), "%s/%02u_%08x.png", dir, index, hash);
	u8 *rgba = GxDecodeCmprAlpha4 (data, has_alpha ? data + (size_t)w * h / 2 : 0, w, h);
	if (!rgba)
		return ERR_OUT_OF_MEMORY;
	return SaveDecodedRGBAToPNG (rgba, w, h, &be_func, png, 0, true);
}

static enumError extract_ptex_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	(void)depth;
	if (export_count <= 0 && !opt_decode)
		return ERR_NOTHING_TO_DO;

	u8 *raw = 0;
	size_t raw_size = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	#define PT_BE32(p) ((uint)(p)[0] << 24 | (uint)(p)[1] << 16 | (uint)(p)[2] << 8 | (p)[3])
	if (raw_size < 0x3c || memcmp (raw, "PTEX", 4) || PT_BE32 (raw + 12) != 0x44332211)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	const uint count = PT_BE32 (raw + 0x18);
	if (!count || count > 256 || 0x1c + 0x20ull * count > raw_size)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	uint todo = 0;
	for (uint i = 0; i < count; i++)
	{
		const u8 *r = raw + 0x1c + 0x20 * i;
		const uint kind = PT_BE32 (r + 4), w = r[12] << 8 | r[13], h = r[14] << 8 | r[15];
		if ((kind == 2 || kind == 0) && w && h && !(w & 7) && !(h & 7)
			&& (u64)PT_BE32 (r + 16) + 0x10 + (kind == 2 ? w * h : w * h / 2) <= raw_size)
			todo++;
	}
	if (!todo)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	char dest[PATH_MAX];
	snprintf (dest, sizeof (dest), "%s.d", arg);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sDECODE PTEX:%s (%u of %u textures) -> %s/\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", arg, todo, count, dest);
	if (testmode)
	{
		FREE (raw);
		return ERR_OK;
	}
	CreatePath (dest, true);
	enumError err = ERR_OK;
	for (uint i = 0; !err && i < count; i++)
	{
		const u8 *r = raw + 0x1c + 0x20 * i;
		const uint kind = PT_BE32 (r + 4), w = r[12] << 8 | r[13], h = r[14] << 8 | r[15];
		const size_t size = kind == 2 ? (size_t)w * h : (size_t)w * h / 2; // CMPR (+ I4 alpha for kind 2)
		if ((kind != 2 && kind != 0) || !w || !h || (w & 7) || (h & 7)
			|| (u64)PT_BE32 (r + 16) + 0x10 + size > raw_size)
			continue;
		err = ptex_write_png (
			dest, i, PT_BE32 (r), w, h, kind == 2, raw + 0x10 + PT_BE32 (r + 16));
	}
	FREE (raw);
	#undef PT_BE32
	return err;
}
