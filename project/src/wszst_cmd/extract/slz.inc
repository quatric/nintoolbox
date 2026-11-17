// Namco "Tales of" (Wii) "*.slz": {u8 type=1, le32 compressed size, le32 decompressed size} then Okumura LZSS (LSB-first flag bytes, 4096-byte zero-filled ring, start 0xfee, 12-bit position + 4-bit length-3).
// The payload is usually an "FPS4" archive (big-endian {"FPS4", count, entry table offset, data offset, ...}, 12-byte entries {loc,size,size2} from 0x1c); members are written to "<file>.d/NNN.bin" (a zero size is taken from the next member's offset), otherwise the stream is written to "<file>.dec".
static enumError extract_slz_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	u8 *raw = 0;
	size_t sz = 0;
	if (!is_ext (arg, ".slz") || LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
		return ERR_NOTHING_TO_DO;
	enumError err = ERR_NOTHING_TO_DO;
	u8 *out = 0;
	if (sz < 10 || raw[0] != 1 || (u64)rd_le32 (raw + 1) + 9 != sz)
		goto done;
	{
		const u32 us = rd_le32 (raw + 5);
		if (!us || us > 0x10000000)
			goto done;
		out = MALLOC (us);
		static u8 ring[4096];
		memset (ring, 0, sizeof (ring));
		uint r = 0xfee;
		size_t p = 9, o = 0;
		while (p < sz && o < us)
		{
			const uint f = raw[p++];
			for (uint i = 0; i < 8 && o < us; i++)
			{
				if (f >> i & 1)
				{
					if (p >= sz)
						break;
					const u8 c = raw[p++];
					out[o++] = c;
					ring[r] = c;
					r = (r + 1) & 0xfff;
				}
				else
				{
					if (p + 1 >= sz)
						break;
					const uint pos = raw[p] | (raw[p + 1] & 0xf0) << 4, ln = (raw[p + 1] & 15) + 3;
					p += 2;
					for (uint k = 0; k < ln && o < us; k++)
					{
						const u8 c = ring[(pos + k) & 0xfff];
						out[o++] = c;
						ring[r] = c;
						r = (r + 1) & 0xfff;
					}
				}
			}
		}
		if (o != us)
			goto done;
		char dest[PATH_MAX];
		beside_source_dest (dest, sizeof (dest), arg);
		uint count = 0;
		const bool fps4 = us >= 0x20 && !memcmp (out, "FPS4", 4);
		if (fps4)
		{
			count = rd_be32 (out + 4);
			const u32 tab = rd_be32 (out + 8);
			if (!count || count > 100000 || tab + (u64)count * 12 > us)
				count = 0;
		}
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT SLZ:%s (%u bytes%s) -> %s%s\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, us, count ? ", FPS4" : "", dest, count ? "/" : ".dec");
		err = ERR_OK;
		if (testmode)
			goto done;
		if (!count)
		{
			char path[PATH_MAX];
			snprintf (path, sizeof (path), "%s.dec", arg);
			File_t F;
			err = CreateFileOpt (&F, true, path, false, arg);
			if (F.f && fwrite (out, 1, us, F.f) != us)
				err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", us, path);
			ResetFile (&F, opt_preserve);
			goto done;
		}
		const u32 tab = rd_be32 (out + 8);
		for (uint i = 0; i < count && !err; i++)
		{
			const u8 *e = out + tab + (size_t)i * 12;
			const u32 loc = rd_be32 (e);
			u32 len = rd_be32 (e + 4);
			if (!len)
			{
				u32 next = us;
				for (uint j = 0; j < count; j++)
				{
					const u32 l2 = rd_be32 (out + tab + (size_t)j * 12);
					if (l2 > loc && l2 < next)
						next = l2;
				}
				len = next - loc;
			}
			if (!len || loc >= us || (u64)loc + len > us)
				continue;
			char path[PATH_MAX];
			snprintf (path, sizeof (path), "%s/%03u.bin", dest, i);
			File_t F;
			err = CreateFileOpt (&F, true, path, false, arg);
			if (F.f && fwrite (out + loc, 1, len, F.f) != len)
				err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", len, path);
			ResetFile (&F, opt_preserve);
		}
		if (!err && count >= 2 && rd_be32 (out + tab) < us && rd_be32 (out + rd_be32 (out + tab)) == 0x20000)
		{
			const u32 l0 = rd_be32 (out + tab), l1 = rd_be32 (out + tab + 12);
			if (l1 > l0 && l1 < us)
			{
				const enumError sub = txm_write_set (arg, out + l0, l1 - l0, out + l1, us - l1, dest, depth);
				if (sub != ERR_NOTHING_TO_DO && err < sub)
					err = sub;
			}
		}
		if (!err)
		{
			const enumError sub = extract_tree_complete (dest, depth + 1);
			if (err < sub)
				err = sub;
		}
	}
done:
	FREE (out);
	FREE (raw);
	return err;
}
