#include "lib-hktag.h"
// Phive cloth (".bphcl"): with --decode (xx / xdecode) the Havok tag file is written beside the
// source as "<file>.yml" (every object with its reflected fields) and the trailing AAMP cloth
// parameters as
// "<file>.param.yml".

static enumError extract_bphcl_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	if (!is_ext (arg, ".bphcl") || (export_count == 0 && !opt_decode))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	size_t to, ts, ao, as;
	enumError err = ERR_NOTHING_TO_DO;
	char *text = 0;
	size_t text_n = 0;
	if (IsPhiveCloth (raw, sz, &to, &ts, &ao, &as))
	{
		char dest[PATH_MAX], name[PATH_MAX];
		beside_source_dir (dest, sizeof (dest), arg);
		ccp sl = strrchr (arg, '/');
		ccp base = sl ? sl + 1 : arg;
		err = ConvertHavokTagFileToYaml (raw + to, ts, &text, &text_n);
		if (err)
		{
			if (verbose >= 0)
				fprintf (stdlog, "!! cannot convert %s: unsupported Havok tag file\n", arg);
		}
		else
		{
			snprintf (name, sizeof (name), "%s.yml", base);
			if (verbose >= 0 || testmode)
				fprintf (stdlog, "%s%sDECODE PHIVE CLOTH:%s (%zu bytes of YAML) -> %s/%s\n",
					verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, text_n, dest, name);
			if (!testmode)
				err = shade_write (arg, dest, basedir, name, (u8 *)text, (uint)text_n);
		}
		if (!err && as)
		{
			char path[PATH_MAX];
			snprintf (path, sizeof (path), "%s%s%s%s.param.yml", dest, *dest ? "/" : "",
				basedir ? basedir : "", base);
			if (verbose >= 0 || testmode)
				fprintf (stdlog, "%s%sDECODE AAMP:%s -> %s\n", verbose > 0 ? "\n" : "",
					testmode ? "WOULD " : "", arg, path);
			if (!testmode)
			{
				File_t F;
				CreateFileOpt (&F, true, path, false, arg);
				if (F.f)
				{
					DecodeAAMP_YAML (F.f, raw + ao, as);
					ResetFile (&F, opt_preserve);
				}
			}
		}
	}
	FREE (text);
	FREE (raw);
	return err;
}
