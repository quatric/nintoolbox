// EA Black Box "JDLZ" compressed files (Need for Speed - Nitro "*.lzc", NFS Underground 2/Most Wanted/Carbon chunks): "JDLZ", 02 10 00 00, le32 inflated size, le32 compressed size (the whole file), then two flag bytes f1/f2 (consumed LSB first, refilled when they run out) and the data from +18. f1 bit set = match: 2 bytes b0 b1; if the f2 bit is set length = ((b0 & 0xf0) << 4 | b1) + 3 and distance = (b0 & 0xf) + 1, otherwise distance = ((b0 & 0xe0) << 3 | b1) + 17 and length = (b0 & 0x1f) + 3 (the f2 bit is only consumed by matches); f1 bit clear = literal byte. Written to "<file>.bin" and recursed. Algorithm as in the public open-nfs-bin-viewer.
static enumError extract_jdlz_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	FILE *f = fopen (arg, "rb");
	if (!f)
		return ERR_NOTHING_TO_DO;
	u8 head[18];
	if (fread (head, 1, 18, f) != 18 || memcmp (head, "JDLZ", 4))
	{
		fclose (f);
		return ERR_NOTHING_TO_DO;
	}
	enumError err = ERR_NOTHING_TO_DO;
	u8 *in = 0, *out = 0;
	const u32 dec = rd_le32 (head + 8), comp = rd_le32 (head + 12);
	if (dec && dec <= 0x20000000 && comp > 18 && comp <= 0x20000000)
	{
		in = MALLOC (comp);
		out = MALLOC (dec);
		fseeko (f, 0, SEEK_SET);
		if (fread (in, 1, comp, f) == comp)
		{
			uint f1 = in[16] | 0x100, f2 = in[17] | 0x100;
			size_t p = 18, o = 0;
			bool bad = false;
			while (p < comp && o < dec && !bad)
			{
				if (f1 & 1)
				{
					if (p + 2 > comp)
						break;
					const uint b0 = in[p], b1 = in[p + 1];
					p += 2;
					size_t len, dist;
					if (f2 & 1)
					{
						len = (((b0 & 0xf0) << 4) | b1) + 3;
						dist = (b0 & 0xf) + 1;
					}
					else
					{
						dist = (((b0 & 0xe0) << 3) | b1) + 17;
						len = (b0 & 0x1f) + 3;
					}
					f2 >>= 1;
					if (dist > o)
					{
						bad = true;
						break;
					}
					for (size_t k = 0; k < len && o < dec; k++, o++)
						out[o] = out[o - dist];
				}
				else
					out[o++] = in[p++];
				f1 >>= 1;
				if (f1 == 1 && p < comp)
					f1 = in[p++] | 0x100;
				if (f2 == 1 && p < comp)
					f2 = in[p++] | 0x100;
			}
			if (!bad && o == dec)
			{
				char path[PATH_MAX];
				snprintf (path, sizeof (path), "%s.bin", arg);
				if (verbose >= 0 || testmode)
					fprintf (stdlog, "%s%sDECOMPRESS JDLZ:%s -> %s\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, path);
				err = ERR_OK;
				if (!testmode)
				{
					File_t F;
					err = CreateFileOpt (&F, true, path, false, arg);
					if (F.f && fwrite (out, 1, dec, F.f) != dec)
						err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", dec, path);
					ResetFile (&F, opt_preserve);
					if (!err)
						extract_one_file (path, 0, depth + 1);
				}
			}
		}
	}
	FREE (in);
	FREE (out);
	fclose (f);
	return err;
}
