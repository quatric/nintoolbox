#include "lib-asb.h"
// Animation sequence binaries (".asb", ASB 0x41B): with --decode (xx / xdecode) written beside the
// source as "<file>.yml".

static enumError extract_asb_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	if (!is_ext (arg, ".asb") || (export_count == 0 && !opt_decode))
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
	if (IsAsb (raw, sz) && !ConvertAsbToYaml (raw, sz, &text, &text_n))
	{
		char dest[PATH_MAX], name[PATH_MAX];
		beside_source_dir (dest, sizeof (dest), arg);
		ccp sl = strrchr (arg, '/');
		snprintf (name, sizeof (name), "%s.yml", sl ? sl + 1 : arg);
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sDECODE ASB:%s (%zu bytes of YAML) -> %s/%s\n",
				verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, text_n, dest, name);
		err = testmode ? ERR_OK : shade_write (arg, dest, basedir, name, (u8 *)text, (uint)text_n);
	}
	FREE (text);
	FREE (raw);
	return err;
}
