#include "lib-nxfont.h"
// Switch / Switch 2 data files that convert to a single text file (YAML or JSON) written beside the source as
// "<file>.yml" / "<file>.json". One table row per format: extension, magic, label, output extension, converter.

typedef enumError (*nxtext_conv_t) (const u8 *data, size_t size, char **text, size_t *text_size);

typedef struct nxtext_format_t
{
	ccp ext; // file extension (with dot)
	ccp magic; // NULL or required leading bytes
	uint magic_len;
	ccp label; // log label
	ccp out_ext; // appended to the source name
	nxtext_conv_t conv;
} nxtext_format_t;

static const nxtext_format_t nxtext_formats[] = {
	{ ".bfcpx", "FCPX", 4, "FCPX", ".yml", ConvertFCPXToYaml },
};

static enumError extract_nxtext_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	if (export_count == 0 && !opt_decode)
		return ERR_NOTHING_TO_DO;
	const nxtext_format_t *fmt = 0;
	for (uint i = 0; i < sizeof (nxtext_formats) / sizeof (*nxtext_formats) && !fmt; i++)
		if (is_ext (arg, nxtext_formats[i].ext)
			&& (!nxtext_formats[i].magic || file_has_magic (arg, nxtext_formats[i].magic, nxtext_formats[i].magic_len)))
			fmt = nxtext_formats + i;
	if (!fmt)
		return ERR_NOTHING_TO_DO;

	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	char *text = 0;
	size_t text_n = 0;
	enumError err = fmt->conv (raw, sz, &text, &text_n);
	FREE (raw);
	if (err)
	{
		if (verbose >= 0)
			fprintf (stdlog, "!! cannot convert %s: invalid or unsupported %s data\n", arg, fmt->label);
		return ERR_NOTHING_TO_DO;
	}
	char dest[PATH_MAX], name[PATH_MAX];
	beside_source_dir (dest, sizeof (dest), arg);
	ccp sl = strrchr (arg, '/');
	snprintf (name, sizeof (name), "%s%s", sl ? sl + 1 : arg, fmt->out_ext);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sDECODE %s:%s -> %s/%s\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", fmt->label, arg, dest, name);
	err = shade_write (arg, dest, basedir, name, (u8 *)text, (uint)text_n);
	FREE (text);
	return err;
}
