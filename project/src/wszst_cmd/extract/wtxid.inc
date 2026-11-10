// "WTXID" object image (Cook Wars / Cooking Party / Fashion Idol / Petz Rescue: Geom/*.wtxg models, Anim/*.wtxa animations; any extension).
// Magic "WTXID" + kind byte ('g' geometry, 'a' animation), BE u16 version 2, flags; 0x80 byte header table of u32 BE {offset/count} pairs; the body is a
// serialised C++ object heap: class names (CClumpDef, CItemSkinMeshDef, CMtlStdDef, ...) and instance names (skeleton bones "CSBip Spine", materials, cameras)
// stored as NUL terminated strings with a length prefix, interleaved with big endian pointers / floats whose layout is the game's own. The object data itself is not decoded;
// the string inventory is written as "<file>.txt" ("offset<TAB>string").
static enumError extract_wtxid_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	(void)depth;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
		return ERR_NOTHING_TO_DO;
	if (sz < 0x88 || memcmp (raw, "WTXID", 5) || rd_be16 (raw + 6) != 2)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	char dest[PATH_MAX];
	beside_source_dest_ext (dest, sizeof (dest), arg, ".txt");
	if (testmode)
	{
		fprintf (stdlog, "WOULD EXTRACT WTXID:%s -> TXT:%s\n", arg, dest);
		FREE (raw);
		return ERR_OK;
	}
	const size_t cap = 256 + sz * 3;
	char *t = MALLOC (cap);
	size_t tl = snprintf (t, cap, "WTXID kind %c size %zu\n", raw[5] >= 0x20 && raw[5] < 0x7f ? raw[5] : '?', sz);
	for (size_t i = 0x80; i + 4 < sz && tl + 300 < cap;)
	{
		size_t j = i;
		while (j < sz && (raw[j] == ' ' || raw[j] == '.' || raw[j] == '-' || raw[j] == '_' || (raw[j] >= '0' && raw[j] <= '9') || (raw[j] | 0x20) >= 'a' && (raw[j] | 0x20) <= 'z'))
			j++;
		if (j - i >= 4 && j - i < 200 && (j == sz || raw[j] == 0 || j - i >= 6))
		{
			tl += snprintf (t + tl, cap - tl, "%zx\t%.*s\n", i, (int)(j - i), (ccp)raw + i);
			i = j;
		}
		else
			i = j > i ? j : i + 1;
	}
	FREE (raw);
	File_t F;
	enumError err = CreateFileOpt (&F, true, dest, false, arg);
	if (F.f && fwrite (t, 1, tl, F.f) != tl)
		err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %zu bytes failed: %s\n", tl, dest);
	ResetFile (&F, opt_preserve);
	FREE (t);
	if (!err && verbose >= 0)
		fprintf (stdlog, "%sEXTRACT WTXID:%s -> TXT:%s\n", verbose > 0 ? "\n" : "", arg, dest);
	return err;
}
