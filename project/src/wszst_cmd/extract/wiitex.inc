// "WII!" texture bundle (Star Trek: Conquest ".wii"/".WII", the 63 texture-only
// files such as Splash/*.WII and FrontEnd/*.WII): big-endian {"WII!", u32 2,
// u32 count, ...} with a table at 0x40 of 0x18-byte records
//   {char name[4], u16 w, u16 h, u32 kind, u32 0, u32 size, u32 offset}
// kind 0 = RGBA8 (size = w*h*4), kind 1 = RGB565 (size = w*h*2) and
// kind 2 = a 16-bit alpha format decoded as RGB5A3 (best guess). Every
// texture is written as "<name>.d/<index>_<name>.png" (decode mode: xx).
// The 82 mixed level bundles (record layout {u32 0, u16 w, u16 h, u16 type,
// u16 wide, u32 0, u32 size, u32 offset}, size covering a full mip chain) are
// decoded for types 4-7 at their base level: wide = 0 -> RGBA8, wide = 1 ->
// RGB565 (type 6) or RGB5A3 (other types, best guess).
static enumError wiitex_write_png (ccp dir, uint index, const u8 *rec, const u8 *file, size_t file_size)
{
	#define WT_BE32(p) ((uint)(p)[0] << 24 | (uint)(p)[1] << 16 | (uint)(p)[2] << 8 | (p)[3])
	const uint w = rec[4] << 8 | rec[5], h = rec[6] << 8 | rec[7];
	const uint kind = WT_BE32 (rec + 8), size = WT_BE32 (rec + 16), off = WT_BE32 (rec + 20);
	const uint fmt = kind == 0 ? 6 : kind == 1 ? 4 : 5;
	char name[5];
	memcpy (name, rec, 4);
	name[4] = 0;
	for (uint i = 0; i < 4; i++)
		if (!isalnum ((u8)name[i]) && name[i] != '_')
			name[i] = '_';
	char tmp[PATH_MAX], tmp_png[PATH_MAX], png[PATH_MAX];
	snprintf (png, sizeof (png), "%s/%02u_%s.png", dir, index, name);
	snprintf (tmp, sizeof (tmp), "%s/.wiitex-tmp.tpl", dir);
	snprintf (tmp_png, sizeof (tmp_png), "%s.png", tmp);

	const uint data_off = 0x40;
	u8 *tpl = CALLOC (1, data_off + size);
	if (!tpl)
		return ERR_OUT_OF_MEMORY;
	u8 *p = tpl;
#define PUT32(v) do { const uint v_ = (v); *p++ = v_ >> 24; *p++ = v_ >> 16; *p++ = v_ >> 8; *p++ = v_; } while (0)
	PUT32 (0x0020af30); PUT32 (1); PUT32 (0x0c);
	PUT32 (0x14); PUT32 (0);
	*p++ = h >> 8; *p++ = h; *p++ = w >> 8; *p++ = w;
	PUT32 (fmt); PUT32 (data_off); PUT32 (0); PUT32 (0); PUT32 (1); PUT32 (1); PUT32 (0);
#undef PUT32
	memcpy (tpl + data_off, file + off, size);
	enumError err = SaveFile (tmp, 0, 0, tpl, data_off + size, 0);
	FREE (tpl);
	if (!err)
		err = decode_image_if_possible (tmp);
	if (!err && rename (tmp_png, png))
		err = ERROR1 (ERR_CANT_CREATE, "Can't create PNG: %s\n", png);
	unlink (tmp);
	(void)file_size;
	return err;
}

static enumError wiitex_write_raw (ccp png, uint w, uint h, uint fmt, const u8 *data, uint size)
{
	char tmp[PATH_MAX], tmp_png[PATH_MAX];
	snprintf (tmp, sizeof (tmp), "%s.tmp.tpl", png);
	snprintf (tmp_png, sizeof (tmp_png), "%s.png", tmp);
	const uint data_off = 0x40;
	u8 *tpl = CALLOC (1, data_off + size);
	if (!tpl)
		return ERR_OUT_OF_MEMORY;
	u8 *p = tpl;
#define PUT32(v) do { const uint v_ = (v); *p++ = v_ >> 24; *p++ = v_ >> 16; *p++ = v_ >> 8; *p++ = v_; } while (0)
	PUT32 (0x0020af30); PUT32 (1); PUT32 (0x0c);
	PUT32 (0x14); PUT32 (0);
	*p++ = h >> 8; *p++ = h; *p++ = w >> 8; *p++ = w;
	PUT32 (fmt); PUT32 (data_off); PUT32 (0); PUT32 (0); PUT32 (1); PUT32 (1); PUT32 (0);
#undef PUT32
	memcpy (tpl + data_off, data, size);
	enumError err = SaveFile (tmp, 0, 0, tpl, data_off + size, 0);
	FREE (tpl);
	if (!err)
		err = decode_image_if_possible (tmp);
	if (!err && rename (tmp_png, png))
		err = ERROR1 (ERR_CANT_CREATE, "Can't create PNG: %s\n", png);
	unlink (tmp);
	return err;
}

static enumError wiitex_extract_bundle (ccp arg, u8 *raw, size_t raw_size, uint count)
{
	uint todo = 0;
	for (uint i = 0; i < count; i++)
	{
		const u8 *r = raw + 0x40 + 0x18 * i;
		const uint w = r[4] << 8 | r[5], h = r[6] << 8 | r[7], type = r[8] << 8 | r[9],
			   wide = r[10] << 8 | r[11], size = WT_BE32 (r + 16), off = WT_BE32 (r + 20);
		if (WT_BE32 (r) != 0 || WT_BE32 (r + 12) != 0 || wide > 1 || !w || !h
			|| (w & 7) || (h & 3) || (u64)off + size > raw_size)
		{
			FREE (raw);
			return ERR_NOTHING_TO_DO;
		}
		if (type >= 4 && type <= 7 && size >= w * h * (wide ? 2u : 4u))
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
		fprintf (stdlog, "%s%sDECODE WII!:%s (%u of %u textures) -> %s/\n", verbose > 0 ? "\n" : "",
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
		const u8 *r = raw + 0x40 + 0x18 * i;
		const uint w = r[4] << 8 | r[5], h = r[6] << 8 | r[7], type = r[8] << 8 | r[9],
			   wide = r[10] << 8 | r[11], size = WT_BE32 (r + 16), off = WT_BE32 (r + 20);
		if (type < 4 || type > 7 || size < w * h * (wide ? 2u : 4u))
			continue;
		char png[PATH_MAX];
		snprintf (png, sizeof (png), "%s/%03u_%ux%u.png", dest, i, w, h);
		err = wiitex_write_raw (png, w, h, wide ? (type == 6 ? 4 : 5) : 6, raw + off, w * h * (wide ? 2u : 4u));
	}
	FREE (raw);
	return err;
}

static enumError extract_wiitex_file (ccp arg, ccp basedir, uint depth)
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
	if (raw_size < 0x58 || memcmp (raw, "WII!", 4) || WT_BE32 (raw + 4) != 2
		|| WT_BE32 (raw + 32) != 0x40)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	const uint count = WT_BE32 (raw + 8);
	if (!count || count > 256 || 0x40 + 0x18ull * count > raw_size)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	if (WT_BE32 (raw + 12) != 0)
		return wiitex_extract_bundle (arg, raw, raw_size, count);
	for (uint i = 0; i < count; i++)
	{
		const u8 *r = raw + 0x40 + 0x18 * i;
		const uint w = r[4] << 8 | r[5], h = r[6] << 8 | r[7], kind = WT_BE32 (r + 8),
			   size = WT_BE32 (r + 16), off = WT_BE32 (r + 20);
		if (!w || !h || kind > 2 || WT_BE32 (r + 12) != 0 || size != w * h * (kind ? 2u : 4u)
			|| (u64)off + size > raw_size)
		{
			FREE (raw);
			return ERR_NOTHING_TO_DO;
		}
	}

	char dest[PATH_MAX];
	snprintf (dest, sizeof (dest), "%s.d", arg);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sDECODE WII!:%s (%u textures) -> %s/\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", arg, count, dest);
	if (testmode)
	{
		FREE (raw);
		return ERR_OK;
	}
	CreatePath (dest, true);
	enumError err = ERR_OK;
	for (uint i = 0; !err && i < count; i++)
		err = wiitex_write_png (dest, i, raw + 0x40 + 0x18 * i, raw, raw_size);
	FREE (raw);
	return err;
}
#undef WT_BE32
