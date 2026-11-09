// Grasshopper Manufacture "GAN2" animation (No More Heroes ".GA2" members of RMHG containers): {"GAN2", u32 1 (big endian), 0, u32 size,
// 0, u32 track count (+0x14), ...}; the tracks follow from 0x30, 0x30 bytes each, little endian {char name[16], u32 parent, first child, previous,
// next (file offsets of other tracks, 0 = none), u32 data offset (0 = no keys)}. Track data (derived by comparing 800 tracks
// of the retail files): {u16 0, u16 channel mask, u32 0, u32 end, u32 channel count at +0xc, ..., u32 offset of the channel offset list at +0x1c}; a channel
// is {u16 1 (3 = other), u16 id (0-2 translation, 3-5 rotation), u32 0, u32 data offset, u32 0}; channel data is {u8 format, u8 parameter, u8 frame
// count, u8 0} + the frames: format 5 = little endian s16 samples scaled by 2^(16 - parameter) (checked on 800 tracks: the NAVEL height of the walking / crouching /
// jumping clips comes out as 10 / 5.5 / 26 against a bind pose height of 11.2; rotations are Euler radians like the GMF2 object rotations), formats 1 and 4
// = little endian float32 values (unverified). The tree and the channels go to "tracks.json" in
// "<file>.d".
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
		js = MALLOC ((size_t)n * 12288 + 64);
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
			l += snprintf (js + l, 256, "  {\"name\": \"%s\", \"parent\": %d, \"child\": %d, \"prev\": %d, \"next\": %d, \"data\": %u, \"channels\": [", nm,
				ix[0], ix[1], ix[2], ix[3], rd_le32 (t + 32));
			const u32 data = rd_le32 (t + 32);
			if (data && (u64)data + 0x20 <= sz)
			{
				const u32 nch = rd_le32 (raw + data + 0xc), lp = rd_le32 (raw + data + 0x1c);
				for (uint k = 0; k < nch && k < 8 && (u64)lp + 4 * (k + 1) <= sz; k++)
				{
					const u32 co = rd_le32 (raw + lp + 4 * k);
					if ((u64)co + 16 > sz)
						break;
					const uint cid = rd_le16 (raw + co + 2);
					const u32 dp = rd_le32 (raw + co + 8);
					if ((u64)dp + 4 > sz)
						break;
					const uint fmt = raw[dp], par = raw[dp + 1], fc = raw[dp + 2];
					l += snprintf (js + l, 128, "%s{\"id\": %u, \"format\": %u, \"param\": %u, \"frames\": %u, \"values\": [", k ? ", " : "", cid, fmt, par, fc);
					const double sc = fmt == 5 ? ldexp (1.0, 16 - (int)par) : 1.0;
					const uint esz = fmt == 5 ? 2 : 4, nv = fmt == 5 ? fc : (fc < 1 ? 1 : fc);
					for (uint v = 0; v < nv && v < 256 && (u64)dp + 4 + esz * (v + 1) <= sz; v++)
					{
						if (v)
							js[l++] = ',';
						if (fmt == 5)
							l += snprintf (js + l, 24, "%.6g", (int)(s16)rd_le16 (raw + dp + 4 + 2 * v) * sc);
						else
						{
							const u32 b = rd_le32 (raw + dp + 4 + 4 * v);
							float f;
							memcpy (&f, &b, 4);
							l += snprintf (js + l, 24, "%.6g", isfinite (f) ? (double)f : 0.0);
						}
					}
					l += snprintf (js + l, 8, "]}");
				}
			}
			l += snprintf (js + l, 8, "]}%s\n", i + 1 < n ? "," : "");
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
