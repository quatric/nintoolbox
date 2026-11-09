// NW4R intermediate texture ".rtex" (the XML "nw4r_3dif" tool export found on retail discs, e.g. Der Gesundheitscoach):
// <image format="cmpr|rgba8|rgb565|rgb5a3|ia8|i8|i4|ia4" palette_name="" width= height= mipmap_level= data_unit="u32|u16|u8"
// data_size= path=> followed by whitespace separated big endian hex words of the GX texture data (base level first).
// Written as "<file>.png" (the base level; paletted images with a palette_name are not decoded).
static enumError extract_rtex_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	(void)basedir;
	if (!is_ext (arg, ".rtex"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw || sz < 64 || sz > 0x20000000)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	char *t = MALLOC (sz + 1);
	if (!t)
	{
		FREE (raw);
		return ERR_OUT_OF_MEMORY;
	}
	memcpy (t, raw, sz);
	t[sz] = 0;
	FREE (raw);
	char *im = strstr (t, "<image ");
	char *gt = im ? strchr (im, '>') : 0;
	char *end = gt ? strstr (gt, "</image>") : 0;
	if (!im || !gt || !end || !strstr (t, "<nw4r_3dif"))
	{
		FREE (t);
		return ERR_NOTHING_TO_DO;
	}
	*gt = 0;
	char fmts[16] = "", pal[8] = "", unit[8] = "";
	uint w = 0, h = 0, dsz = 0;
	const char *p;
	if ((p = strstr (im, "format=\"")))
		sscanf (p + 8, "%15[^\"]", fmts);
	if ((p = strstr (im, "palette_name=\"")))
		sscanf (p + 14, "%7[^\"]", pal);
	if ((p = strstr (im, "data_unit=\"")))
		sscanf (p + 11, "%7[^\"]", unit);
	if ((p = strstr (im, "width=\"")))
		w = (uint)strtoul (p + 7, 0, 10);
	if ((p = strstr (im, "height=\"")))
		h = (uint)strtoul (p + 8, 0, 10);
	if ((p = strstr (im, "data_size=\"")))
		dsz = (uint)strtoul (p + 11, 0, 10);
	static const struct { ccp name; uint fmt; } F[] = { { "i4", 0 }, { "i8", 1 }, { "ia4", 2 }, { "ia8", 3 }, { "rgb565", 4 },
		{ "rgb5a3", 5 }, { "rgba8", 6 }, { "cmpr", 14 } };
	int fmt = -1;
	for (uint i = 0; i < sizeof (F) / sizeof (*F); i++)
		if (!strcmp (F[i].name, fmts))
			fmt = (int)F[i].fmt;
	const uint ub = !strcmp (unit, "u32") ? 4 : !strcmp (unit, "u16") ? 2 : !strcmp (unit, "u8") ? 1 : 0;
	if (fmt < 0 || *pal || !ub || !w || !h || w > 4096 || h > 4096 || !dsz || dsz > 0x10000000)
	{
		FREE (t);
		return ERR_NOTHING_TO_DO;
	}
	u8 *data = CALLOC (1, (size_t)dsz + 8);
	if (!data)
	{
		FREE (t);
		return ERR_OUT_OF_MEMORY;
	}
	size_t got = 0;
	char *q = gt + 1;
	*gt = '>';
	while (q < end && got + ub <= dsz)
	{
		while (q < end && (u8)*q <= ' ')
			q++;
		char *e;
		const unsigned long v = strtoul (q, &e, 16);
		if (e == q)
			break;
		for (uint k = 0; k < ub; k++)
			data[got + k] = (u8)(v >> (8 * (ub - 1 - k)));
		got += ub;
		q = e;
	}
	FREE (t);
	u8 *rgba = 0;
	enumError err = got == dsz ? DecodeGXTexture_RGBA (&rgba, w, h, (uint)fmt, data, dsz, 0, 0, 0) : ERR_NOTHING_TO_DO;
	FREE (data);
	if (err || !rgba)
	{
		FREE (rgba);
		return ERR_NOTHING_TO_DO;
	}
	char dest[PATH_MAX];
	beside_source_dest_ext (dest, sizeof (dest), arg, ".png");
	if (testmode)
	{
		FREE (rgba);
		fprintf (stdlog, "WOULD EXTRACT NW4R-RTEX:%s -> PNG:%s\n", arg, dest);
		return ERR_OK;
	}
	err = SaveDecodedRGBAToPNG (rgba, w, h, &be_func, dest, 0, true);
	if (!err && verbose >= 0)
		fprintf (stdlog, "%sEXTRACT NW4R-RTEX:%s -> PNG:%s\n", verbose > 0 ? "\n" : "", arg, dest);
	return err;
}
