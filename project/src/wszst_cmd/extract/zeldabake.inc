#include "lib-zeldabake.h"
// Switch 2 Zelda small data tables: baked light lists (".bklight"), culling boxes (".bbox") and foliage tables (".flnt") and the container of mesh codec packages (".mc"); with
// --decode (xx / xdecode) written beside the source as "<file>.yml".
static enumError extract_zeldabake_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	const bool bk = is_ext (arg, ".bklight"), bb = is_ext (arg, ".bbox"), fl = is_ext (arg, ".flnt"), mc = is_ext (arg, ".mc");
	if ((!bk && !bb && !fl && !mc) || (export_count == 0 && !opt_decode))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	enumError err = ERR_NOTHING_TO_DO;
	char *text = 0;
	size_t text_n = 0;
	ccp what = 0;
	if (bk && IsBklt (raw, sz))
		what = "BKLT", err = ConvertBkltToYaml (raw, sz, &text, &text_n);
	else if (bb && IsCullBoxes (raw, sz))
		what = "CULL BOXES", err = ConvertCullBoxesToYaml (raw, sz, &text, &text_n);
	else if (mc && IsMcpk (raw, sz))
		what = "MCPK", err = ConvertMcpkToYaml (raw, sz, &text, &text_n);
	else if (fl && IsFoliageTable (raw, sz))
		what = "FLNT", err = ConvertFoliageTableToYaml (raw, sz, &text, &text_n);
	if (what && !err)
	{
		char dest[PATH_MAX], name[PATH_MAX];
		beside_source_dir (dest, sizeof (dest), arg);
		ccp sl = strrchr (arg, '/');
		snprintf (name, sizeof (name), "%s.yml", sl ? sl + 1 : arg);
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sDECODE %s:%s (%zu bytes of YAML) -> %s/%s\n", verbose > 0 ? "\n" : "",
				testmode ? "WOULD " : "", what, arg, text_n, dest, name);
		err = testmode ? ERR_OK : shade_write (arg, dest, basedir, name, (u8 *)text, (uint)text_n);
	}
	else if (!what)
		err = ERR_NOTHING_TO_DO;
	FREE (text);
	FREE (raw);
	return err;
}
