// Super PickUps (Wii) "CARS.MIX": big-endian {u32 0, u32 file count, u32 table offset, ...} padded to 0x20; the table at the end of the file holds count entries
// {u32 name length (0x15..), name bytes (obfuscated), u32 offset, u32 size, u32 size again, 9 bytes}, parsed sequentially up to the end of the file. Members are written to "<file>.d/NNNN_<hash8>.<ext>" (".ini" for text, ".tpl" for TPL, else ".bin") and recursed.
static enumError extract_pickmix_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	if (!is_ext (arg, ".mix"))
		return ERR_NOTHING_TO_DO;
	FILE *f = fopen (arg, "rb");
	if (!f)
		return ERR_NOTHING_TO_DO;
	enumError err = ERR_NOTHING_TO_DO;
	u8 *tab = 0, *buf = 0;
	u8 hdr[0x20];
	if (fread (hdr, 1, sizeof (hdr), f) != sizeof (hdr) || rd_be32 (hdr) || fseeko (f, 0, SEEK_END))
		goto done;
	{
		const u64 fsz = (u64)ftello (f);
		const uint n = rd_be32 (hdr + 4);
		const u64 to = rd_be32 (hdr + 8);
		if (!n || n > 1000000 || to >= fsz)
			goto done;
		const size_t tsz = (size_t)(fsz - to);
		tab = MALLOC (tsz);
		if (fseeko (f, (off_t)to, SEEK_SET) || fread (tab, 1, tsz, f) != tsz)
			goto done;
		u32 *eo = MALLOC ((size_t)n * sizeof (u32));
		{
			size_t p = 0;
			uint k = 0;
			for (; k < n; k++)
			{
				if (p + 4 > tsz)
					break;
				const u32 nl = rd_be32 (tab + p);
				if (nl < 4 || nl > 64 || p + 4 + nl + 21 > tsz)
					break;
				const u8 *e = tab + p + 4 + nl;
				if ((u64)rd_be32 (e) + rd_be32 (e + 4) > to)
					break;
				eo[k] = (u32)p;
				p += 4 + nl + 21;
			}
			if (k != n || p != tsz)
			{
				FREE (eo);
				goto done;
			}
		}
		char dest[PATH_MAX];
		beside_source_dest (dest, sizeof (dest), arg);
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT PICKMIX:%s (%u files) -> %s/\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, n, dest);
		err = ERR_OK;
		size_t cap = 0;
		for (uint i = 0; i < n && !err && !testmode; i++)
		{
			const u8 *e0 = tab + eo[i];
			const u32 nl = rd_be32 (e0);
			const u8 *e = e0 + 4 + nl;
			const u32 off = rd_be32 (e), size = rd_be32 (e + 4);
			if (!size)
				continue;
			if (size > cap)
			{
				FREE (buf);
				cap = size;
				buf = MALLOC (cap);
			}
			if (fseeko (f, (off_t)off, SEEK_SET) || fread (buf, 1, size, f) != size)
				continue;
			ccp ext = size > 4 && rd_be32 (buf) == 0x0020af30 ? "tpl" : buf[0] == '[' ? "ini" : "bin";
			char path[PATH_MAX];
			snprintf (path, sizeof (path), "%s/%04u_%02x%02x%02x%02x.%s", dest, i, e0[4], e0[5], e0[6], e0[7], ext);
			File_t F;
			err = CreateFileOpt (&F, true, path, false, arg);
			if (F.f && fwrite (buf, 1, size, F.f) != size)
				err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", size, path);
			ResetFile (&F, opt_preserve);
			if (!err && size > 0x40 && rd_be32 (buf) == 0xbabe6638)
			{
				// resource bundle: carve the embedded single-image TPLs (each runs to the next TPL / end of the member)
				uint t = 0;
				for (size_t q = 0; q + 0x40 < size && !err; q++)
				{
					if (rd_be32 (buf + q) != 0x0020af30 || rd_be32 (buf + q + 4) != 1 || rd_be32 (buf + q + 8) != 0x0c)
						continue;
					size_t en = size;
					for (size_t r = q + 0x40; r + 0x40 < size; r++)
						if (rd_be32 (buf + r) == 0x0020af30 && rd_be32 (buf + r + 4) == 1 && rd_be32 (buf + r + 8) == 0x0c)
						{
							en = r;
							break;
						}
					char tp[PATH_MAX];
					snprintf (tp, sizeof (tp), "%s/%04u_%02x%02x%02x%02x.d/%03u.tpl", dest, i, e0[4], e0[5], e0[6], e0[7], t++);
					File_t T;
					err = CreateFileOpt (&T, true, tp, false, arg);
					if (T.f && fwrite (buf + q, 1, en - q, T.f) != en - q)
						err = FILEERROR1 (&T, ERR_WRITE_FAILED, "Writing failed: %s\n", tp);
					ResetFile (&T, opt_preserve);
					q = en - 1;
				}
			}
		}
		FREE (eo);
		if (!err && !testmode)
		{
			const enumError sub = extract_tree_complete (dest, depth + 1);
			if (err < sub)
				err = sub;
		}
	}
done:
	FREE (tab);
	FREE (buf);
	fclose (f);
	return err;
}
