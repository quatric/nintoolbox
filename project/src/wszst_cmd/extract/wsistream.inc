// Aladin Magic Racer (Wii) "common/sounds/streams/*.wsi": big-endian {u32 0x20, u32 2, 0...} then 0x110-byte blocks {u32 0x110 (size incl. header; the last block is shorter), u32 1, u32 channel (1/2),
// u32 0} with a 0x100-byte payload; the channels alternate. The first block of each channel starts with its standard 0x60-byte DSP-ADPCM header, followed by the first frames. Each channel is
// reassembled into "<file>.d/chN.dsp" (mono DSP-ADPCM files, 44.1 kHz) and decoded to WAV.
static enumError extract_wsistream_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".wsi"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0, *ch[2] = { 0, 0 };
	size_t sz = 0;
	enumError err = ERR_NOTHING_TO_DO;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
		return ERR_NOTHING_TO_DO;
	if (sz < 0x240 || rd_be32 (raw) != 0x20 || rd_be32 (raw + 4) != 2 || rd_be32 (raw + 0x20) != 0x110 || rd_be32 (raw + 0x28) != 1 || rd_be32 (raw + 0x138) != 2)
		goto done;
	{
		size_t used[2] = { 0, 0 };
		ch[0] = MALLOC (sz);
		ch[1] = MALLOC (sz);
		if (!ch[0] || !ch[1])
			goto done;
		for (size_t o = 0x20; o + 16 < sz;)
		{
			const u32 bs = rd_be32 (raw + o), c = rd_be32 (raw + o + 8);
			if (bs < 16 || bs > 0x110 || rd_be32 (raw + o + 4) != 1 || (c != 1 && c != 2) || o + bs > sz)
				break;
			memcpy (ch[c - 1] + used[c - 1], raw + o + 16, bs - 16);
			used[c - 1] += bs - 16;
			o += bs;
		}
		for (uint c = 0; c < 2; c++)
			if (used[c] < 0x68 || rd_be32 (ch[c] + 0x10) != 2 || rd_be32 (ch[c] + 0x18) != 2 || rd_be32 (ch[c] + 8) < 4000 || rd_be32 (ch[c] + 8) > 96000)
				goto done;
		char dest[PATH_MAX];
		beside_source_dest (dest, sizeof (dest), arg);
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT WSI:%s (2 channels, %u Hz) -> %s/\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, (uint)rd_be32 (ch[0] + 8), dest);
		err = ERR_OK;
		for (uint c = 0; c < 2 && !err && !testmode; c++)
		{
			size_t len = 0x60 + (rd_be32 (ch[c] + 4) + 1) / 2;
			if (len > used[c])
				len = used[c];
			char path[PATH_MAX];
			snprintf (path, sizeof (path), "%s/%sch%u.dsp", dest, basedir ? basedir : "", c);
			File_t F;
			err = CreateFileOpt (&F, true, path, false, arg);
			if (F.f && fwrite (ch[c], 1, len, F.f) != len)
				err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", (uint)len, path);
			ResetFile (&F, opt_preserve);
		}
		if (!err && !testmode)
		{
			const enumError sub = extract_tree_complete (dest, depth + 1);
			if (err < sub)
				err = sub;
		}
	}
done:
	FREE (raw);
	FREE (ch[0]);
	FREE (ch[1]);
	return err;
}
