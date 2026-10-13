// Muramasa - The Demon Blade ".ftx" texture sets (Wii): an "FCMP" LZSS container (lib-muramasa.c) around an "FTEX"
// block. FTEX is little-endian: "FTEX", u32 total size, u32 header size (0x80), u32 texture count, then count
// 0x30-byte records at 0x20 {char name[0x20], u32 0, u32 size, 4 date bytes ...}. Each texture is an "FTX0" chunk
// (u32 size, u32 data offset 0x40, zero padding) holding a standard TPL of the recorded size; chunks follow one
// another, 0x40 aligned, from the header size on. The TPLs are written under their recorded names and the usual
// decoder turns them into PNGs.
static enumError extract_fcmpftex_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	if (!is_ext (arg, ".ftx") || (export_count <= 0 && !opt_decode))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	u8 *tex = 0;
	nintendo_sarc_entry_t *entries = 0;
	uint n = 0;
	enumError err = ERR_NOTHING_TO_DO;
	const u32 dsz = GetDecompressedSizeMuramasaFcmp (raw, sz);
	if (!dsz || dsz > 0x10000000 || !IsMuramasaFcmp (raw, sz, sz))
		goto done;
	tex = MALLOC (dsz);
	if (!tex || DecompressMuramasaFcmp (raw, sz, tex, dsz, 0, arg, true) != ERR_OK)
		goto done;
	if (dsz < 0x80 || memcmp (tex, "FTEX", 4))
		goto done;
	const uint count = rd_le32 (tex + 12), hdr = rd_le32 (tex + 8);
	if (!count || count > 4096 || hdr < 0x20 + 0x30 * (size_t)count || hdr > dsz)
		goto done;
	entries = CALLOC (count, sizeof (*entries));
	if (!entries)
	{
		err = ERR_OUT_OF_MEMORY;
		goto done;
	}
	size_t pos = hdr;
	for (uint i = 0; i < count; i++)
	{
		const u8 *r = tex + 0x20 + 0x30 * (size_t)i;
		char name[0x21];
		memcpy (name, r, 0x20);
		name[0x20] = 0;
		const size_t dof = pos + 8 <= dsz ? rd_le32 (tex + pos + 8) : 0, len = rd_le32 (r + 0x24);
		if (pos + 0x10 > dsz || memcmp (tex + pos, "FTX0", 4) || !dof || dof > 0x1000 || pos + dof + len > dsz
			|| !valid_sarc_path (name))
			goto fail;
		if (!OwnedEntryAdd (entries, n, name, tex + pos + dof, (uint)len))
			goto fail;
		n++;
		pos = (pos + dof + len + 0x3f) & ~(size_t)0x3f;
	}
	err = write_owned_entries (arg, basedir, depth, "FCMP-FTEX", entries, n);
	entries = 0;
	goto done;
fail:
	err = ERR_NOTHING_TO_DO;
done:
	if (entries)
		ResetOwnedEntries (entries, n);
	FREE (tex);
	FREE (raw);
	return err;
}
