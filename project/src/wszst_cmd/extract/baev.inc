#include "lib-baev.h"
#include "lib-zstd.h"
#include "lib-asb.h"
// Animation event archives (".baev", BFFH): with --decode (xx / xdecode) written beside the source
// as
// "<file>.yml". Animation names are resolved from the matching animation BFRES when it is found at
// "<romfs>/Model/<name>.bfres" or ".bfres.zs" (the ".baev" lives in
// "<romfs>/AnimationEvent/<dir>/"); AsNode archives ("<name>.root.baev") are keyed by the element
// GUIDs of "<romfs>/AS/<name>.root.asb".

static u8 *baev_load_names (ccp arg, size_t *size)
{
	*size = 0;
	char root[PATH_MAX], stem[PATH_MAX], path[PATH_MAX];
	snprintf (root, sizeof (root), "%s", arg);
	char *sl = strrchr (root, '/');
	ccp base = sl ? sl + 1 : root;
	snprintf (stem, sizeof (stem), "%s", base);
	char *dot = strrchr (stem, '.');
	if (dot)
		*dot = 0;
	// two directory levels up from the file
	for (int up = 0; up < 3; up++)
	{
		char *s = strrchr (root, '/');
		if (!s)
		{
			if (up < 2)
				return 0;
			break;
		}
		*s = 0;
	}
	static ccp const ext[] = { ".bfres", ".bfres.zs", ".asb", ".asb.zs" };
	for (uint i = 0; i < 4; i++)
	{
		snprintf (path, sizeof (path), "%s/%s/%s%s", root, i < 2 ? "Model" : "AS", stem, ext[i]);
		u8 *raw = 0;
		size_t sz = 0;
		if (access (path, F_OK) || LoadFileAlloc (path, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
		{
			FREE (raw);
			continue;
		}
		if (sz >= 4 && IsZSTD (raw, sz) > 0)
		{
			u8 *dec = 0;
			uint dec_n = 0;
			if (DecodeZSTD (&dec, &dec_n, raw, sz) || !dec)
			{
				FREE (raw);
				FREE (dec);
				continue;
			}
			FREE (raw);
			raw = dec;
			sz = dec_n;
		}
		if (i >= 2)
		{
			u8 *guids = 0;
			*size = CollectAsbGuidStrings (raw, sz, &guids);
			FREE (raw);
			return guids;
		}
		*size = sz;
		return raw;
	}
	return 0;
}

static enumError extract_baev_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	if (!is_ext (arg, ".baev") || (export_count == 0 && !opt_decode))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	enumError err = ERR_NOTHING_TO_DO;
	if (IsBaev (raw, sz))
	{
		size_t names_n = 0;
		u8 *names = baev_load_names (arg, &names_n);
		char *text = 0;
		size_t text_n = 0;
		if (!ConvertBaevToYaml (raw, sz, names, names_n, &text, &text_n))
		{
			char dest[PATH_MAX], name[PATH_MAX];
			beside_source_dir (dest, sizeof (dest), arg);
			ccp sl = strrchr (arg, '/');
			snprintf (name, sizeof (name), "%s.yml", sl ? sl + 1 : arg);
			if (verbose >= 0 || testmode)
				fprintf (stdlog, "%s%sDECODE BAEV:%s (%zu bytes of YAML) -> %s/%s\n",
					verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, text_n, dest, name);
			err = testmode ? ERR_OK
						   : shade_write (arg, dest, basedir, name, (u8 *)text, (uint)text_n);
		}
		FREE (text);
		FREE (names);
	}
	FREE (raw);
	return err;
}
