// "FEZC" ".fez" data packs (Yard Sale Hidden Treasures: Sunnyville and other WiiWare games on the same engine), little-endian: {char magic[4] "FEZC", u32 version (0x10000), u32 file count, u32 name table offset, u32 index offset,
// u32 data offset, ...}. The name table is {u32 padded length, NUL-terminated path padded to that length} per file; the index (32 bytes per file, same order) is {u32 type, u32 offset, u32 stored size, u32 unpacked size, 16 bytes
// of hash}. Type 0 is stored; bit 1 marks a zlib stream that ends with a sync flush (no final block, so it is inflated with Z_SYNC_FLUSH and checked against the unpacked size). Files are written under their paths in "<file>.d".
static enumError extract_fez_file (ccp arg, ccp basedir, uint depth)
{
	u8 magic[4];
	FILE *f = fopen (arg, "rb");
	const bool hit = f && fread (magic, 1, 4, f) == 4 && !memcmp (magic, "FEZC", 4);
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
	const u64 cnt = sz >= 32 ? rd_le32 (raw + 8) : 0, noff = sz >= 32 ? rd_le32 (raw + 12) : 0, ioff = sz >= 32 ? rd_le32 (raw + 16) : 0;
	if (!cnt || cnt > 1000000 || noff < 24 || noff > sz || ioff > sz || ioff + 32 * cnt > sz)
		goto done;
	entries = CALLOC (cnt, sizeof (*entries));
	if (!entries)
	{
		err = ERR_OUT_OF_MEMORY;
		goto done;
	}
	uint n = 0;
	u64 np = noff;
	for (u64 i = 0; i < cnt; i++)
	{
		if (np + 4 > ioff)
			break;
		const u64 nl = rd_le32 (raw + np);
		if (!nl || nl > 0x400 || np + 4 + nl > ioff)
			break;
		char name[0x400 + 1];
		memcpy (name, raw + np + 4, nl);
		name[nl] = 0;
		np += 4 + nl;
		const u8 *r = raw + ioff + 32 * i;
		const u64 type = rd_le32 (r), off = rd_le32 (r + 4), cs = rd_le32 (r + 8), us = rd_le32 (r + 12);
		if (off > sz || cs > sz - off || us > 0x40000000 || !valid_sarc_path (name))
			continue;
		u8 *buf = MALLOC (us ? us : 1);
		if (!buf)
		{
			err = ERR_OUT_OF_MEMORY;
			goto done;
		}
		bool ok = false;
		if (!(type & 2))
		{
			ok = cs == us;
			if (ok)
				memcpy (buf, raw + off, us);
		}
		else
		{
			z_stream z;
			memset (&z, 0, sizeof (z));
			if (inflateInit (&z) == Z_OK)
			{
				z.next_in = raw + off;
				z.avail_in = cs;
				z.next_out = buf;
				z.avail_out = us;
				const int zr = inflate (&z, Z_SYNC_FLUSH);
				ok = (zr == Z_OK || zr == Z_STREAM_END) && z.total_out == us;
				inflateEnd (&z);
			}
		}
		if (ok && OwnedEntryAdd (entries, n, name, buf, (uint)us))
			n++;
		FREE (buf);
	}
	if (n)
	{
		err = write_owned_entries (arg, basedir, depth, "FEZ", entries, n);
		entries = 0;
	}
done:
	if (entries)
		ResetOwnedEntries (entries, cnt);
	FREE (raw);
	return err;
}
