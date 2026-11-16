// Sumo Digital compiled script packages (Sonic & SEGA All-Stars Racing, Wii: "Scripts/*.sbn"), big-endian: "smospkg2", u32 0x20 (header size),
// u32 (a count; 1 in the small packages), a 16-byte GUID, then tables, an embedded "smosbin" bytecode block and a string pool (names of classes / states / objects / variables,
// script log messages, the build-machine source paths). The bytecode is not decoded; "sbn.txt" lists the package GUID and every NUL-terminated
// printable string of at least 4 characters with its file offset, which recovers the script and object names the package defines.
static enumError extract_sumosbn_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	if (!is_ext (arg, ".sbn"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
		return ERR_NOTHING_TO_DO;
	enumError err = ERR_NOTHING_TO_DO;
	if (sz < 0x40 || memcmp (raw, "smospkg2", 8) || rd_be32 (raw + 8) != 0x20)
		goto done;
	{
		char dest[PATH_MAX];
		beside_source_dest (dest, sizeof (dest), arg);
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT SBN:%s -> %s/\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, dest);
		err = ERR_OK;
		if (!testmode)
		{
			pcr_ctx_t c;
			memset (&c, 0, sizeof (c));
			c.d = raw;
			c.sz = sz;
			c.ok = true;
			pcr_emit (&c, "smospkg2 guid");
			for (uint i = 0; i < 16; i++)
				pcr_emit (&c, "%s%02x", i ? "" : " ", raw[16 + i]);
			pcr_emit (&c, "\n");
			for (size_t p = 0x20; p < sz;)
			{
				size_t e = p;
				while (e < sz && raw[e] >= 0x20 && raw[e] < 0x7f)
					e++;
				if (e - p >= 4 && e < sz && !raw[e])
					pcr_emit (&c, "%06x\t%.*s\n", (uint)p, (int)(e - p), (ccp)raw + p);
				p = e + 1;
			}
			if (c.ok)
				err = shade_write (arg, dest, basedir, "sbn.txt", (const u8 *)c.out, (uint)c.len);
			FREE (c.out);
		}
	}
done:
	FREE (raw);
	return err;
}
