// Shark 3D ".stk" stacks (Babel Rising, WiiWare: "S3DMain.stk"), little-endian: u8 6, u8 8, 3 zero bytes, 8 spaces, a 4 byte XOR key (at 0x0d), u32 entry count (0x11), u32 end of the directory (0x15). From 0x19 on the directory is XORed with the key,
// repeated from the start of the directory: per entry {u32 path length, path, u32 data offset (from the end of the directory), u32 stored size, u32 unpacked size, u32 checksum, u16 4}. Each entry's data is XORed with the key from its own first byte and
// is a zlib stream (or stored when the sizes are equal). The entries (Lua bytecode, TPL textures, HUD/XML/sound/AI data) are written under their paths in "<file>.d"; the contents are not decoded. An entry that does not inflate to its size is skipped.
static enumError extract_sharkstk_file (ccp arg, ccp basedir, uint depth)
{
	u8 head[0x19];
	FILE *f = fopen (arg, "rb");
	const bool hit = f && fread (head, 1, sizeof (head), f) == sizeof (head) && head[0] == 6 && head[1] == 8 && !head[2] && !head[3] && !head[4]
			 && !memcmp (head + 5, "        ", 8) && is_ext (arg, ".stk");
	if (f)
		fclose (f);
	if (!hit)
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	nintendo_sarc_entry_t *entries = 0;
	enumError err = ERR_NOTHING_TO_DO;
	const u8 *key = raw + 0x0d;
	const u64 cnt = rd_le32 (raw + 0x11), base = rd_le32 (raw + 0x15);
	if (!cnt || cnt > 1000000 || base < 0x19 + cnt * 26 || base > sz)
		goto done;
	entries = CALLOC (cnt, sizeof (*entries));
	if (!entries)
	{
		err = ERR_OUT_OF_MEMORY;
		goto done;
	}
	uint n = 0;
	u64 p = 0x19;
	for (u64 i = 0; i < cnt; i++)
	{
		if (p + 4 > base)
			break;
		u8 w[4];
		for (uint k = 0; k < 4; k++)
			w[k] = raw[p + k] ^ key[(p - 0x19 + k) & 3];
		const u64 nl = rd_le32 (w);
		if (!nl || nl >= PATH_MAX || p + 4 + nl + 18 > base)
			break;
		char name[PATH_MAX];
		for (u64 k = 0; k < nl; k++)
			name[k] = (char)(raw[p + 4 + k] ^ key[(p - 0x19 + 4 + k) & 3]);
		name[nl] = 0;
		u8 fld[16];
		for (uint k = 0; k < 16; k++)
			fld[k] = raw[p + 4 + nl + k] ^ key[(p - 0x19 + 4 + nl + k) & 3];
		p += 4 + nl + 18;
		const u64 off = base + rd_le32 (fld), cs = rd_le32 (fld + 4), us = rd_le32 (fld + 8);
		if (off > sz || cs > sz - off || us > 0x40000000 || !valid_sarc_path (name))
			continue;
		u8 *in = MALLOC (cs ? cs : 1), *buf = MALLOC (us ? us : 1);
		if (!in || !buf)
		{
			FREE (in);
			FREE (buf);
			err = ERR_OUT_OF_MEMORY;
			goto done;
		}
		for (u64 k = 0; k < cs; k++)
			in[k] = raw[off + k] ^ key[k & 3];
		bool ok = false;
		z_stream z;
		memset (&z, 0, sizeof (z));
		if (inflateInit (&z) == Z_OK)
		{
			z.next_in = in;
			z.avail_in = cs;
			z.next_out = buf;
			z.avail_out = us ? us : 1;
			ok = inflate (&z, Z_FINISH) == Z_STREAM_END && z.total_out == us;
			inflateEnd (&z);
		}
		if (!ok && cs == us)
			memcpy (buf, in, us), ok = true;
		if (ok && OwnedEntryAdd (entries, n, name, buf, (uint)us))
			n++;
		FREE (in);
		FREE (buf);
	}
	if (n)
	{
		err = write_owned_entries (arg, basedir, depth, "STK", entries, n);
		entries = 0;
	}
done:
	if (entries)
		ResetOwnedEntries (entries, cnt);
	FREE (raw);
	return err;
}
