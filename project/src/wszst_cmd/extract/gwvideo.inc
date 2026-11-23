// Where's Wally: Fantastic Journey ".GWVideo" (WiiWare): seven big-endian u32 {8, kind, 15, 0, 1, 0, 28} and then the frames, each introduced by a few small header bytes and a type byte, as a complete PNG file or an "FMPP" frame (its own 40 byte header holds the payload size at 0x20).
// The frames are cut out by their signatures (a PNG ends after its IEND chunk, an FMPP frame is 40 + the payload size long) and written as "frame_NNN.png" / "frame_NNN.ppm" in "<file>.d"; the FMPP image data is not decoded.
static enumError extract_gwvideo_file (ccp arg, ccp basedir, uint depth)
{
	u8 *raw = 0;
	size_t sz = 0;
	if (!is_ext (arg, ".GWVideo") || LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	nintendo_sarc_entry_t *entries = 0;
	enumError err = ERR_NOTHING_TO_DO;
	if (sz < 0x30 || rd_be32 (raw) != 8 || rd_be32 (raw + 0x18) != 0x1c)
		goto done;
	const uint cap = 4096;
	entries = CALLOC (cap, sizeof (*entries));
	if (!entries)
	{
		err = ERR_OUT_OF_MEMORY;
		goto done;
	}
	uint n = 0;
	for (size_t p = 0x1c; p + 16 < sz && n < cap;)
	{
		size_t len = 0;
		const char *ext = 0;
		if (!memcmp (raw + p, "\x89PNG\r\n\x1a\n", 8))
		{
			size_t q = p + 8; // walk the chunks up to IEND
			while (q + 12 <= sz)
			{
				const u32 cl = rd_be32 (raw + q);
				const bool iend = !memcmp (raw + q + 4, "IEND", 4);
				if (cl > sz - q - 12)
					break;
				q += 12 + cl;
				if (iend)
				{
					len = q - p;
					break;
				}
			}
			ext = "png";
		}
		else if (!memcmp (raw + p, "FMPP", 4) && p + 0x28 <= sz)
		{
			len = 0x28 + (size_t)rd_be32 (raw + p + 0x20);
			if (len > sz - p)
				len = 0;
			ext = "ppm";
		}
		if (!len)
		{
			p++;
			continue;
		}
		char name[64];
		snprintf (name, sizeof (name), "frame_%03u.%s", n, ext);
		if (OwnedEntryAdd (entries, n, name, raw + p, (uint)len))
			n++;
		p += len;
	}
	if (n)
	{
		err = write_owned_entries (arg, basedir, depth, "GWVIDEO", entries, n);
		entries = 0;
	}
done:
	if (entries)
		ResetOwnedEntries (entries, 4096);
	FREE (raw);
	return err;
}
