// Spectrobes: Origins "DATA.LXBD" (1 GB, LXTools): a run of "RPCL" entries at 0x800-aligned offsets, each {"RPCL", u32 1, "LLZ7", be32 decompressed size, 0x10 reserved} followed at +0x20 by an LZ11 stream that inflates to the stored size; an entry runs to the next "RPCL".
// There is no name table in the file, so members are written to "<file>.d/NNNNN.<ext>" (".txt" for text, else ".bin") and recursed (the file is streamed from disk).
static enumError extract_lxbd_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	if (!is_ext (arg, ".lxbd"))
		return ERR_NOTHING_TO_DO;
	FILE *f = fopen (arg, "rb");
	if (!f)
		return ERR_NOTHING_TO_DO;
	enumError err = ERR_NOTHING_TO_DO;
	u64 *offs = 0;
	u8 *buf = 0;
	u8 h[0x20];
	if (fread (h, 1, sizeof (h), f) != sizeof (h) || memcmp (h, "RPCL", 4) || memcmp (h + 8, "LLZ7", 4) || fseeko (f, 0, SEEK_END))
		goto done;
	{
		const u64 fsz = (u64)ftello (f);
		uint cap = 4096, n = 0;
		offs = MALLOC (cap * sizeof (u64));
		static u8 blk[1 << 20];
		for (u64 base = 0; base < fsz; base += sizeof (blk))
		{
			const size_t want = fsz - base < sizeof (blk) ? (size_t)(fsz - base) : sizeof (blk);
			if (fseeko (f, (off_t)base, SEEK_SET) || fread (blk, 1, want, f) != want)
				break;
			for (size_t o = 0; o + 4 <= want; o += 0x800)
				if (!memcmp (blk + o, "RPCL", 4))
				{
					if (n == cap)
					{
						u64 *no = MALLOC (cap * 2 * sizeof (u64));
						memcpy (no, offs, n * sizeof (u64));
						FREE (offs);
						offs = no;
						cap *= 2;
					}
					offs[n++] = base + o;
				}
		}
		if (n < 2)
			goto done;
		char dest[PATH_MAX];
		beside_source_dest (dest, sizeof (dest), arg);
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT LXBD:%s (%u entries) -> %s/\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, n, dest);
		err = ERR_OK;
		size_t bcap = 0;
		for (uint i = 0; i < n && !err && !testmode; i++)
		{
			const u64 end = i + 1 < n ? offs[i + 1] : fsz;
			const size_t len = (size_t)(end - offs[i]);
			if (len <= 0x24 || len > 0x10000000)
				continue;
			if (len > bcap)
			{
				FREE (buf);
				bcap = len;
				buf = MALLOC (bcap);
			}
			if (fseeko (f, (off_t)offs[i], SEEK_SET) || fread (buf, 1, len, f) != len || memcmp (buf + 8, "LLZ7", 4) || buf[0x20] != 0x11)
				continue;
			u8 *dec = 0;
			uint dl = 0;
			if (DecodeLZ10LZ11 (&dec, &dl, buf + 0x20, (uint)(len - 0x20)) || !dec || !dl)
			{
				FREE (dec);
				continue;
			}
			bool text = dl > 8;
			for (uint k = 0; k < 64 && k < dl && text; k++)
				if (dec[k] > 0x7e || (dec[k] < 0x20 && dec[k] != '\r' && dec[k] != '\n' && dec[k] != '\t'))
					text = false;
			char path[PATH_MAX];
			snprintf (path, sizeof (path), "%s/%05u.%s", dest, i, text ? "txt" : "bin");
			File_t F;
			err = CreateFileOpt (&F, true, path, false, arg);
			if (F.f && fwrite (dec, 1, dl, F.f) != dl)
				err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", dl, path);
			ResetFile (&F, opt_preserve);
			FREE (dec);
		}
		if (!err && !testmode)
		{
			const enumError sub = extract_tree_complete (dest, depth + 1);
			if (err < sub)
				err = sub;
		}
	}
done:
	FREE (offs);
	FREE (buf);
	fclose (f);
	return err;
}
