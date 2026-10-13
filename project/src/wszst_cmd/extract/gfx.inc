// Scaleform GFx movies (".gfx": Epic Mickey, Wii): "CFX" + u8 version + u32 little-endian uncompressed
// length + zlib stream (the Flash "CWS" layout). Written as "<name>" with the plain "GFX" magic and the
// decompressed body (a regular uncompressed GFX / SWF tag stream) to "<file>.d/<name>".
static enumError extract_gfx_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".gfx") && !is_ext (arg, ".swf"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	const size_t full = sz >= 8 ? rd_le32 (raw + 4) : 0;
	if (sz < 10 || (memcmp (raw, "CFX", 3) && memcmp (raw, "CWS", 3)) || full < 8 || full > 0x10000000)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	u8 *out = MALLOC (full);
	uLongf dsz = full - 8;
	if (!out || uncompress (out + 8, &dsz, raw + 8, sz - 8) != Z_OK || dsz != full - 8)
	{
		FREE (out);
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	memcpy (out, raw, 8);
	out[0] = raw[0] == 'C' && raw[1] == 'F' ? 'G' : 'F';
	ccp b = strrchr (arg, '/');
	b = b ? b + 1 : arg;
	nintendo_sarc_entry_t *entries = CALLOC (1, sizeof (*entries));
	enumError err = ERR_OUT_OF_MEMORY;
	if (entries && OwnedEntryAdd (entries, 0, b, out, (uint)full))
		err = write_owned_entries (arg, basedir, depth, "GFX", entries, 1);
	else
		FREE (entries);
	FREE (out);
	FREE (raw);
	return err;
}
