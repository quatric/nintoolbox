#include "lib-dds.h"
// DirectDraw Surface textures (".dds": Epic Mickey and other multi-platform ports): the base level is decoded
// to "<file>.png" in decode mode (xx); block-compressed (DXT1 / 3 / 5) and plain RGB(A) layouts are handled
// by DecodeDDS_RGBA().
static enumError extract_dds_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	(void)depth;
	if (!is_ext (arg, ".dds") || (export_count <= 0 && !opt_decode))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0, *rgba = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	uint w = 0, h = 0;
	enumError err = ERR_NOTHING_TO_DO;
	if (sz < 0x80 || !IsDDS (raw, (uint)sz) || DecodeDDS_RGBA (&rgba, &w, &h, raw, (uint)sz) || !rgba)
		goto done;
	{
		char png[PATH_MAX];
		snprintf (png, sizeof (png), "%s.png", arg);
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sDECODE DDS:%s %ux%u -> PNG:%s\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, w,
				h, png);
		if (testmode)
			err = ERR_OK;
		else
		{
			err = SaveDecodedRGBAToPNG (rgba, w, h, &be_func, png, 0, true);
			rgba = 0; // owned by SaveDecodedRGBAToPNG
		}
	}
done:
	FREE (rgba);
	FREE (raw);
	return err;
}
