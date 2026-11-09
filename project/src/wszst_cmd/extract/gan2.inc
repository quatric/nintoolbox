// Grasshopper Manufacture "GAN2" animation (No More Heroes ".GA2" members of RMHG containers): {"GAN2", u32 1 (big endian), 0, u32 size,
// 0, u32 track count (+0x14), ...}; the tracks follow from 0x30, 0x30 bytes each, little endian {char name[16], u32 parent, first child, previous,
// next (file offsets of other tracks, 0 = none), u32 data offset (0 = no keys)}. The key data is a packed per-channel curve format that is not
// decoded; the track hierarchy is written as "tracks.json" (indices instead of offsets) in "<file>.d".
static enumError extract_gan2_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	u8 *raw = 0;
	size_t sz = 0;
	if ((!is_ext (arg, ".ga2") && !is_ext (arg, ".gan2")) || LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
		return ERR_NOTHING_TO_DO;
	enumError err = ERR_NOTHING_TO_DO;
	char *js = 0;
	if (sz < 0x60 || memcmp (raw, "GAN2", 4))
		goto done;
	{
		const uint n = rd_le32 (raw + 0x14);
		if (!n || n > 4096 || 0x30 + 0x30 * (u64)n > sz)
			goto done;
		js = MALLOC (n * 256 + 64);
		if (!js)
		{
			err = ERR_OUT_OF_MEMORY;
			goto done;
		}
		uint l = 0;
		l += snprintf (js + l, 64, "{\"tracks\": [\n");
		for (uint i = 0; i < n; i++)
		{
			const u8 *t = raw + 0x30 + 0x30 * (size_t)i;
			int ix[4];
			for (uint k = 0; k < 4; k++)
			{
				const u32 o = rd_le32 (t + 16 + 4 * k);
				ix[k] = o >= 0x30 && (o - 0x30) % 0x30 == 0 && (o - 0x30) / 0x30 < n ? (int)((o - 0x30) / 0x30) : -1;
			}
			char nm[17];
			memcpy (nm, t, 16);
			nm[16] = 0;
			for (char *c = nm; *c; c++)
				if (*c == '"' || *c == '\\' || (u8)*c < 32)
					*c = '_';
			l += snprintf (js + l, 256, "  {\"name\": \"%s\", \"parent\": %d, \"child\": %d, \"prev\": %d, \"next\": %d, \"data\": %u}%s\n", nm, ix[0],
				ix[1], ix[2], ix[3], rd_le32 (t + 32), i + 1 < n ? "," : "");
		}
		l += snprintf (js + l, 8, "]}\n");
		char dest[PATH_MAX];
		beside_source_dest (dest, sizeof (dest), arg);
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT GAN2:%s (%u tracks) -> %s/tracks.json\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, n, dest);
		err = ERR_OK;
		if (!testmode)
			err = shade_write (arg, dest, basedir, "tracks.json", (u8 *)js, l);
	}
done:
	FREE (js);
	FREE (raw);
	return err;
}
