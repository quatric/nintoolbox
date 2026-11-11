// Text manifests for the Bermuda Triangle - Saving the Coral (Wii) formats: ".MWG"/".MSP" (PLANETG tagged objects),
// ".MWT" (GDATAVERSION envelope), ".PKI" (compact texture pack) and ".pgf" (font header). Each is decoded by the matching
// lib-bermudatriangle.c decoder (the Is* detector validates the content, the extension only gates the probe) and written
// to "<file>.d/<basename>.txt".
#include "lib-bermudatriangle.h"

static enumError extract_wii_text_manifest_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	int kind = 0;
	if (is_ext (arg, ".mwg") || is_ext (arg, ".msp")) kind = 1;
	else if (is_ext (arg, ".mwt")) kind = 2;
	else if (is_ext (arg, ".pki")) kind = 3;
	else if (is_ext (arg, ".pgf")) kind = 4;
	if (!kind || (export_count == 0 && !opt_decode))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	int match = 0;
	switch (kind)
	{
		case 1: match = IsBermudaPlanetG (raw, sz, sz); break;
		case 2: match = IsBermudaMwt (raw, sz, sz); break;
		case 3: match = IsBermudaPki (raw, sz, sz); break;
		case 4: match = IsBermudaPgf (raw, sz, sz); break;
	}
	if (!match)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	char *buf = 0;
	size_t blen = 0;
	FILE *f = open_memstream (&buf, &blen);
	if (!f)
	{
		FREE (raw);
		return ERR_OUT_OF_MEMORY;
	}
	enumError err = ERR_NOTHING_TO_DO;
	switch (kind)
	{
		case 1: err = DecodeBermudaPlanetG_Text (f, raw, sz, sz); break;
		case 2: err = DecodeBermudaMwt_Text (f, raw, sz, sz); break;
		case 3: err = DecodeBermudaPki_Text (f, raw, sz, sz); break;
		case 4: err = DecodeBermudaPgf_Text (f, raw, sz, sz); break;
	}
	fclose (f);
	FREE (raw);
	if (!err && blen)
	{
		ccp base = strrchr (arg, '/');
		base = base ? base + 1 : arg;
		char fn[PATH_MAX], dest[PATH_MAX];
		snprintf (fn, sizeof (fn), "%s.txt", base);
		beside_source_dest (dest, sizeof (dest), arg);
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT BERMUDA TEXT:%s -> %s/%s\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, dest, fn);
		err = ERR_OK;
		if (!testmode)
			err = shade_write (arg, dest, basedir, fn, (const u8 *)buf, (uint)blen);
	}
	else
		err = ERR_NOTHING_TO_DO;
	FREE (buf);
	return err;
}
