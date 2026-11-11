#include "lib-rbnk.h"
// NW4R instrument banks (Wii ".brbnk" / ".rbnk"): the program / key-range / velocity tree and the wave metadata are dumped as the
// lossless-structure XML of wrbnk ("<name>.xml" in the extraction folder); with a ".brwar" / ".rwar" of the same name next to it
// the bank is also rendered as a SoundFont ("<name>.sf2").
static enumError extract_rbnk_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	if ((!is_ext (arg, ".brbnk") && !is_ext (arg, ".rbnk")) || (export_count == 0 && !opt_decode))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	enumError err = ERR_NOTHING_TO_DO;
	rbnk_t rb;
	if (sz > 0x20 && !memcmp (raw, "RBNK", 4) && ScanRBNK (&rb, raw, (uint)sz) == ERR_OK)
	{
		char dest[PATH_MAX], path[PATH_MAX], stem[PATH_MAX];
		beside_source_dest (dest, sizeof (dest), arg);
		const char *sl = strrchr (arg, '/');
		snprintf (stem, sizeof (stem), "%s", sl ? sl + 1 : arg);
		char *dot = strrchr (stem, '.');
		if (dot) *dot = 0;
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT RBNK:%s -> XML:%s/%s.xml\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, dest, stem);
		err = ERR_OK;
		if (!testmode)
		{
			char dir[PATH_MAX];
			snprintf (dir, sizeof (dir), "%s/%s", dest, basedir ? basedir : "");
			CreatePath (dir, true);
			snprintf (path, sizeof (path), "%s/%s.xml", dir, stem);
			FILE *fp = fopen (path, "wb");
			if (fp)
			{
				DumpRBNK_XML (&rb, fp, arg);
				fclose (fp);
			}
			else
				err = ERR_CANT_CREATE;
			// SoundFont when the wave archive sits next to the bank
			char war[PATH_MAX];
			snprintf (war, sizeof (war), "%s", arg);
			char *d2 = strrchr (war, '.');
			u8 *wave = 0;
			size_t wsz = 0;
			bool have = false;
			if (d2)
			{
				static const char *exts[] = { ".brwar", ".rwar", ".BRWAR", ".RWAR" };
				for (uint k = 0; k < 4 && !have; k++)
				{
					strcpy (d2, exts[k]);
					have = !access (war, R_OK) && !LoadFileAlloc (war, 0, 0, &wave, &wsz, 0, 0, 0, false) && wave;
				}
			}
			if (have)
			{
				u8 *sf = 0;
				size_t sf_n = 0;
				if (ConvertRBNKToSF2 (raw, sz, wave, wsz, stem, &sf, &sf_n) == ERR_OK)
				{
					snprintf (path, sizeof (path), "%s/%s.sf2", dir, stem);
					FILE *sp = fopen (path, "wb");
					if (sp)
					{
						fwrite (sf, 1, sf_n, sp);
						fclose (sp);
					}
				}
				FREE (sf);
			}
			FREE (wave);
		}
		ResetRBNK (&rb);
	}
	FREE (raw);
	return err;
}
