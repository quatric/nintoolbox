#include "lib-tera.h"
// Mario Kart World terrain (romfs/TerrainArc/<scene>/): every TARB tile layer (".hght", ".mate", ".norm", "*.extm") is
// written beside the source as "<file>.png" (16 bit for height and water layers, Radiance "<file>.hdr" for the
// R11G11B10F bake_light layer, plus "<file>.alpha.png" when a 4th channel carries data); the scene "<scene>.tscb" as
// "<file>.yml" (layers, materials and the area quadtree).
static enumError extract_tera_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	const bool tile = is_ext (arg, ".hght") || is_ext (arg, ".mate") || is_ext (arg, ".norm") || is_ext (arg, ".extm");
	const bool scene = is_ext (arg, ".tscb");
	if ((!tile && !scene) || (export_count == 0 && !opt_decode))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	enumError err = ERR_NOTHING_TO_DO;
	char dest[PATH_MAX], name[PATH_MAX];
	beside_source_dir (dest, sizeof (dest), arg);
	const char *sl = strrchr (arg, '/');
	const char *base = sl ? sl + 1 : arg;
	if (tile && IsTarbFile (raw, sz))
	{
		TeraImage_t img;
		err = DecodeTarb (raw, sz, is_ext (arg, ".norm"), &img);
		if (!err)
		{
			snprintf (name, sizeof (name), "%s%s", base, img.hdr ? ".hdr" : ".png");
			if (verbose >= 0 || testmode)
				fprintf (stdlog, "%s%sDECODE TARB:%s (%s) -> %s:%s/%s%s%s\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "",
					arg, img.desc, img.hdr ? "HDR" : "PNG", dest, name, img.extra ? " + .alpha.png" : "", "");
			err = shade_write (arg, dest, basedir, name, img.main, (uint)img.main_size);
			if (!err && img.extra)
			{
				snprintf (name, sizeof (name), "%s.alpha.png", base);
				err = shade_write (arg, dest, basedir, name, img.extra, (uint)img.extra_size);
			}
			ResetTeraImage (&img);
		}
		else if (verbose >= 0)
			fprintf (stdlog, "!! cannot convert %s: unsupported TARB terrain layer\n", arg);
	}
	else if (scene && IsTscbFile (raw, sz))
	{
		char *yml = 0;
		size_t yml_n = 0;
		err = ConvertTscbToYaml (raw, sz, &yml, &yml_n);
		if (!err)
		{
			snprintf (name, sizeof (name), "%s.yml", base);
			if (verbose >= 0 || testmode)
				fprintf (stdlog, "%s%sDECODE TSCB:%s -> YAML:%s/%s\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, dest, name);
			err = shade_write (arg, dest, basedir, name, (u8 *)yml, (uint)yml_n);
		}
		FREE (yml);
	}
	FREE (raw);
	return err;
}
