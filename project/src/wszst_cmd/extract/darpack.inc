// Gaijin Games (Bit.Trip Complete, Wii) "Core.dar": {"DAR" + 29 x 'a', 0x20 bytes}, then standard Nintendo resource files back to back, each padded to 0x20: files with a BOM at +4 and their size at +8 (REFF, REFT,
// RFNT, ...) and "U.8" archives (size taken from the end of the last member). Members are carved to "<file>.d/NNNN.<ext>" and extracted recursively.
// returns the compressed length of the LZ11 stream (type byte 0x11) at "b", 0 on error
static size_t dar_lz11_len (const u8 *b, size_t len)
{
	if (len < 8 || b[0] != 0x11)
		return 0;
	size_t out = b[1] | b[2] << 8 | b[3] << 16, p = 4;
	if (!out)
		out = rd_le32 (b + 4), p = 8;
	size_t done = 0;
	while (done < out)
	{
		if (p >= len)
			return 0;
		const uint flags = b[p++];
		for (int bit = 7; bit >= 0 && done < out; bit--)
		{
			if (!(flags >> bit & 1))
			{
				if (p >= len)
					return 0;
				p++, done++;
				continue;
			}
			if (p >= len)
				return 0;
			const uint c = b[p] >> 4;
			size_t cnt;
			if (c == 0)
			{
				if (p + 3 > len)
					return 0;
				cnt = ((b[p] & 15) << 4 | b[p + 1] >> 4) + 0x11, p += 3;
			}
			else if (c == 1)
			{
				if (p + 4 > len)
					return 0;
				cnt = ((b[p] & 15) << 12 | b[p + 1] << 4 | b[p + 2] >> 4) + 0x111, p += 4;
			}
			else
			{
				if (p + 2 > len)
					return 0;
				cnt = c + 1, p += 2;
			}
			done += cnt;
		}
	}
	return p;
}

static enumError extract_darpack_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".dar"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
		return ERR_NOTHING_TO_DO;
	enumError err = ERR_NOTHING_TO_DO;
	if (sz < 0x40 || memcmp (raw, "DARa", 4))
		goto done;
	{
		char dest[PATH_MAX];
		beside_source_dest (dest, sizeof (dest), arg);
		uint n = 0;
		err = ERR_OK;
		for (size_t p = 0x20; p + 16 <= sz && !err;)
		{
			const u8 *m = raw + p;
			size_t len = 0;
			ccp ext = "bin";
			if (!memcmp (m, "\x55\xaa\x38\x2d", 4))
			{
				const u32 root = rd_be32 (m + 4);
				if (root < 0x20 || p + root + 12 > sz)
					break;
				const u32 cnt = rd_be32 (m + root + 8);
				if (cnt < 1 || p + root + 12 * (u64)cnt > sz)
					break;
				for (u32 i = 1; i < cnt; i++)
				{
					const u8 *nd = m + root + 12 * (size_t)i;
					if (!nd[0])
					{
						const size_t e = (size_t)rd_be32 (nd + 4) + rd_be32 (nd + 8);
						if (e > len)
							len = e;
					}
				}
				ext = "arc";
			}
			else if (m[0] == 0x11 && (len = dar_lz11_len (m, sz - p)) > 0)
				ext = "lz11";
			else if (m[4] == 0xfe && m[5] == 0xff)
			{
				len = rd_be32 (m + 8);
				if (!memcmp (m, "REFF", 4))
					ext = "breff";
				else if (!memcmp (m, "REFT", 4))
					ext = "breft";
				else if (!memcmp (m, "RFNT", 4))
					ext = "brfnt";
				else if (!memcmp (m, "RLYT", 4))
					ext = "brlyt";
				else if (!memcmp (m, "RLAN", 4))
					ext = "brlan";
				else if (!memcmp (m, "RSAR", 4))
					ext = "brsar";
				else if (!memcmp (m, "bres", 4))
					ext = "brres";
				else if (!memcmp (m, "RSTM", 4))
					ext = "brstm";
			}
			if (len < 16 || p + len > sz)
				break;
			n++;
			if (!testmode)
			{
				char path[PATH_MAX];
				snprintf (path, sizeof (path), "%s/%s%04u.%s", dest, basedir ? basedir : "", n - 1, ext);
				File_t F;
				err = CreateFileOpt (&F, true, path, false, arg);
				if (F.f && fwrite (m, 1, len, F.f) != len)
					err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", (uint)len, path);
				ResetFile (&F, opt_preserve);
			}
			p = (p + len + 0x1f) & ~(size_t)0x1f;
		}
		if (!n && !err)
			err = ERR_NOTHING_TO_DO;
		else if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT DAR:%s (%u files) -> %s/\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, n, dest);
		if (!err && !testmode)
		{
			const enumError sub = extract_tree_complete (dest, depth + 1);
			if (err < sub)
				err = sub;
		}
	}
done:
	FREE (raw);
	return err;
}
