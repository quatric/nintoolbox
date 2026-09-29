#include "lib-iear.h"

static enumError extract_iear_file (ccp arg, ccp basedir, uint depth)
{
	u8 header[20];
	FILE *in = fopen (arg, "rb");
	const size_t got = in ? fread (header, 1, sizeof (header), in) : 0;
	if (in)
		fclose (in);
	if (got < sizeof (header) || memcmp (header, "MAIN", 4) || memcmp (header + 16, "JTBL", 4))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &raw, &size, UINT_MAX, 0, 0, false);
	if (err)
		return err;
	iear_entry_t *entries = 0;
	uint count = 0;
	err = ScanIEAR (&entries, &count, raw, (uint)size);
	char dest[PATH_MAX];
	beside_source_dest (dest, sizeof (dest), arg);
	if (strlen (dest) + (basedir ? strlen (basedir) : 0) + 32 >= PATH_MAX)
		err = ERR_INVALID_DATA;
	if (!err && (verbose >= 0 || testmode))
		fprintf (stdlog, "%sEXTRACT IEAR:%s (%u files) -> %s/\n", testmode ? "WOULD " : "", arg,
			count, dest);
	for (uint i = 0; !err && !testmode && i < count; i++)
	{
		char path[PATH_MAX];
		snprintf (path, sizeof (path), "%s/%sfile_%04u.%s", dest, basedir ? basedir : "", i,
			entries[i].extension);
		File_t F;
		err = CreateFileOpt (&F, true, path, false, arg);
		if (F.f && entries[i].size
			&& fwrite (entries[i].data, 1, entries[i].size, F.f) != entries[i].size)
			err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing IEAR member failed: %s\n", path);
		ResetFile (&F, opt_preserve);
	}
	FREE (entries);
	FREE (raw);
	if (!err && count && !testmode && (opt_recurse < 0 || depth < (uint)opt_recurse))
		err = extract_tree_complete (dest, depth + 1);
	return err;
}
