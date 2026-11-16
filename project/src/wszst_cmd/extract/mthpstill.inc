// Capcom "MTHP" (".mhp") backgrounds of Resident Evil Archives: Resident Evil Zero, big-endian:
//   "MTHP", u32 file size, u32 width, u32 height, u32 frame count, then per frame a "THP " block {"THP ", u32 frame size, ...} holding one JPEG whose scan data has no 0x00 byte stuffing after 0xFF and no EOI (the THP convention).
// Files with exactly one frame are still background pictures: the JPEG is written as "<file>.jpg". Files with more frames are animated (video-like)
// backgrounds and are intentionally left alone.
static enumError extract_mthpstill_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	(void)depth;
	if (!is_ext (arg, ".mhp"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	enumError err = ERR_NOTHING_TO_DO;
	if (sz < 0x50 || memcmp (raw, "MTHP", 4) || rd_be32 (raw + 4) != sz || rd_be32 (raw + 0x10) != 1 || memcmp (raw + 0x20, "THP ", 4)
		|| raw[0x40] != 0xff || raw[0x41] != 0xd8)
		goto done;
	{
		// THP JPEG frames carry no 0x00 byte stuffing after 0xFF in the scan data and no EOI: find the end of the SOS header, then rebuild a
		// standard JPEG (stuff every 0xFF in the scan data, drop the zero padding at the end, append EOI).
		size_t q = 0x42;
		bool sos = false;
		while (q + 4 <= sz && raw[q] == 0xff)
		{
			const u8 m = raw[q + 1];
			const size_t l = raw[q + 2] << 8 | raw[q + 3];
			if (m == 0xd8 || m == 0x01 || l < 2)
				break;
			q += 2 + l;
			if (m == 0xda)
			{
				sos = true;
				break;
			}
		}
		size_t end = sz;
		while (end > q && !raw[end - 1])
			end--;
		if (!sos || end <= q)
			goto done;
		size_t ff = 0;
		for (size_t i = q; i < end; i++)
			ff += raw[i] == 0xff;
		const size_t total = (q - 0x40) + (end - q) + ff + 2;
		u8 *jpg = MALLOC (total);
		if (!jpg)
			goto done;
		memcpy (jpg, raw + 0x40, q - 0x40);
		size_t o = q - 0x40;
		for (size_t i = q; i < end; i++)
		{
			jpg[o++] = raw[i];
			if (raw[i] == 0xff)
				jpg[o++] = 0;
		}
		jpg[o++] = 0xff;
		jpg[o++] = 0xd9;
		char dest[PATH_MAX];
		snprintf (dest, sizeof (dest), "%s.jpg", arg);
		if (testmode)
		{
			fprintf (stdlog, "WOULD EXTRACT MTHP:%s -> JPG:%s\n", arg, dest);
			err = ERR_OK;
		}
		else
		{
			File_t F;
			err = CreateFileOpt (&F, true, dest, false, arg);
			if (F.f && fwrite (jpg, 1, o, F.f) != o)
				err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", (uint)o, dest);
			ResetFile (&F, opt_preserve);
			if (verbose >= 0 && !err)
				fprintf (stdlog, "%sEXTRACT MTHP:%s -> JPG:%s\n", verbose > 0 ? "\n" : "", arg, dest);
		}
		FREE (jpg);
	}
done:
	FREE (raw);
	return err;
}
