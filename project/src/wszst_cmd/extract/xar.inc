#include "libbz2/bzlib.h"
// "xar\0" archives (Toushinden "ar/base_motion.xar"): big-endian {"xar\0", u32 count} then count * 16-byte entries {u32 name offset, u32 name length, u32 data offset, u32 data size}; the name strings follow the table.
// Members that start with "BZh" are bzip2-compressed and are written decompressed; all others are stored. Files go to "<file>.d/<name>" and are recursed.
static enumError extract_xar_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	if (!is_ext (arg, ".xar"))
		return ERR_NOTHING_TO_DO;
	FILE *f = fopen (arg, "rb");
	if (!f)
		return ERR_NOTHING_TO_DO;
	enumError err = ERR_NOTHING_TO_DO;
	u8 *tab = 0, *buf = 0, *out = 0;
	u8 hdr[8];
	if (fread (hdr, 1, sizeof (hdr), f) != sizeof (hdr) || memcmp (hdr, "xar\0", 4) || fseeko (f, 0, SEEK_END))
		goto done;
	{
		const u64 fsz = (u64)ftello (f);
		const uint n = be32 (hdr + 4);
		if (!n || n > 1000000 || 8 + (u64)n * 16 > fsz)
			goto done;
		tab = MALLOC ((size_t)n * 16);
		if (fseeko (f, 8, SEEK_SET) || fread (tab, 1, (size_t)n * 16, f) != (size_t)n * 16)
			goto done;
		for (uint i = 0; i < n; i++)
		{
			const u8 *e = tab + i * 16;
			if ((u64)be32 (e) + be32 (e + 4) > fsz || (u64)be32 (e + 8) + be32 (e + 12) > fsz || !be32 (e + 4) || be32 (e + 4) > 255)
				goto done;
		}
		char dest[PATH_MAX];
		beside_source_dest (dest, sizeof (dest), arg);
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT XAR:%s (%u files) -> %s/\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, n, dest);
		err = ERR_OK;
		size_t cap = 0;
		for (uint i = 0; i < n && !err && !testmode; i++)
		{
			const u8 *e = tab + i * 16;
			const u32 noff = be32 (e), nlen = be32 (e + 4), off = be32 (e + 8), size = be32 (e + 12);
			char name[260];
			if (fseeko (f, noff, SEEK_SET) || fread (name, 1, nlen, f) != nlen)
				continue;
			name[nlen] = 0;
			for (char *p = name; *p; p++)
				if (*p == '\\')
					*p = '/';
			if (name[0] == '/' || strstr (name, "..") || !size)
				continue;
			if (size > cap)
			{
				FREE (buf);
				cap = size;
				buf = MALLOC (cap);
			}
			if (fseeko (f, off, SEEK_SET) || fread (buf, 1, size, f) != size)
				continue;
			const u8 *data = buf;
			size_t dsz = size;
			if (size > 14 && !memcmp (buf, "BZh", 3))
			{
				size_t ocap = (size_t)size * 8 + 4096;
				for (int tries = 0; tries < 8; tries++)
				{
					FREE (out);
					out = MALLOC (ocap);
					unsigned int dl = (unsigned int)ocap;
					const int r = BZ2_bzBuffToBuffDecompress ((char *)out, &dl, (char *)buf, size, 0, 0);
					if (r == BZ_OK)
					{
						data = out;
						dsz = dl;
						break;
					}
					if (r != BZ_OUTBUFF_FULL)
						break;
					ocap *= 4;
				}
			}
			char path[PATH_MAX];
			snprintf (path, sizeof (path), "%s/%s", dest, name);
			if (!access (path, F_OK))
				snprintf (path, sizeof (path), "%s/%s.%u", dest, name, i);
			File_t F;
			err = CreateFileOpt (&F, true, path, false, arg);
			if (F.f && fwrite (data, 1, dsz, F.f) != dsz)
				err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %zu bytes failed: %s\n", dsz, path);
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
	FREE (tab);
	FREE (buf);
	FREE (out);
	fclose (f);
	return err;
}
