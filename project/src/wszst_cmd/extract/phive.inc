#include "lib-phive.h"
#include "lib-tera.h"
// Phive collision shape (Splatoon 3 ".bphsh"): with --decode (xx / xdecode) written beside the source as "<name>.obj" (collision
// mesh, one group per material) and "<name>.json" (material name, material flags and collision-disable flags per group).
// Static compound bodies (".bphsc") with shapes become "<file>.bphsc.obj" (all instances placed in world space).
static enumError extract_phive_file (ccp arg, ccp basedir, uint depth)
{
	const bool compound = is_ext (arg, ".bphsc");
	if ((!is_ext (arg, ".bphsh") && !compound) || (export_count == 0 && !opt_decode))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	enumError err = ERR_NOTHING_TO_DO;
	if (compound)
	{
		char *obj = 0;
		size_t obj_n = 0;
		err = IsPhiveCompound (raw, sz) ? ConvertPhiveCompoundToObj (raw, sz, &obj, &obj_n) : ERR_NOTHING_TO_DO;
		if (!err)
		{
			char dest[PATH_MAX], name[PATH_MAX];
			beside_source_dir (dest, sizeof (dest), arg);
			const char *sl = strrchr (arg, '/');
			snprintf (name, sizeof (name), "%s.obj", sl ? sl + 1 : arg);
			if (verbose >= 0 || testmode)
				fprintf (stdlog, "%s%sDECODE PHIVE COMPOUND:%s (%zu bytes of OBJ) -> %s/%s\n", verbose > 0 ? "\n" : "",
					testmode ? "WOULD " : "", arg, obj_n, dest, name);
			if (!testmode)
				err = shade_write (arg, dest, basedir, name, (u8 *)obj, (uint)obj_n);
		}
		FREE (obj);
		u16 *cells = 0;
		uint w = 0, h = 0;
		float box[6];
		if (err == ERR_NOTHING_TO_DO && !DecodePhiveWaterGrid (raw, sz, &cells, &w, &h, box))
		{
			// heights as 16-bit gray, flow x/z + flags as 16-bit RGB, bounds as YAML
			char dest[PATH_MAX], base_name[PATH_MAX], name[PATH_MAX];
			beside_source_dir (dest, sizeof (dest), arg);
			const char *sl = strrchr (arg, '/');
			snprintf (base_name, sizeof (base_name), "%s", sl ? sl + 1 : arg);
			const size_t n = (size_t)w * h;
			// EncodePngMem takes little-endian 16-bit samples
			u8 *hp = MALLOC (n * 2), *fp = MALLOC (n * 6);
			u8 *png = 0;
			size_t png_n = 0;
			err = hp && fp ? ERR_OK : ERR_OUT_OF_MEMORY;
			for (size_t i = 0; !err && i < n; i++)
			{
				const u16 v[4] = { cells[4 * i], cells[4 * i + 1], cells[4 * i + 2], (u16)(cells[4 * i + 3] * 0x4000u) };
				hp[2 * i] = (u8)v[0], hp[2 * i + 1] = (u8)(v[0] >> 8);
				for (int k = 0; k < 3; k++)
					fp[6 * i + 2 * k] = (u8)v[k + 1], fp[6 * i + 2 * k + 1] = (u8)(v[k + 1] >> 8);
			}
			if (verbose >= 0 || testmode)
				fprintf (stdlog, "%s%sDECODE PHIVE WATER:%s (%ux%u) -> %s/%s.png + .flow.png + .yml\n", verbose > 0 ? "\n" : "",
					testmode ? "WOULD " : "", arg, w, h, dest, base_name);
			if (!err && !testmode)
			{
				err = EncodePngMem (hp, w, h, 1, 16, &png, &png_n);
				snprintf (name, sizeof (name), "%s.png", base_name);
				if (!err)
					err = shade_write (arg, dest, basedir, name, png, (uint)png_n);
				FREE (png);
				png = 0;
				if (!err)
					err = EncodePngMem (fp, w, h, 3, 16, &png, &png_n);
				snprintf (name, sizeof (name), "%s.flow.png", base_name);
				if (!err)
					err = shade_write (arg, dest, basedir, name, png, (uint)png_n);
				FREE (png);
				char yml[1024];
				const int yn = snprintf (yml, sizeof (yml),
					"# Phive water grid\nwidth: %u\nheight: %u\nmin: [%g, %g, %g]\nmax: [%g, %g, %g]\n"
					"# %s.png: 16-bit height per cell, world y = min y + value / 65535 * (max y - min y); rows run along z\n"
					"# %s.flow.png: 16-bit flow x (red), flow z (green), 0x7fff = still; blue = flags * 0x4000\n",
					w, h, box[3] - box[0] / 2, box[4] - box[1] / 2, box[5] - box[2] / 2, box[3] + box[0] / 2, box[4] + box[1] / 2,
					box[5] + box[2] / 2, base_name, base_name);
				snprintf (name, sizeof (name), "%s.yml", base_name);
				if (!err)
					err = shade_write (arg, dest, basedir, name, (u8 *)yml, (uint)yn);
			}
			FREE (hp);
			FREE (fp);
		}
		else if (err == ERR_NOTHING_TO_DO && !depth && verbose >= 0 && IsPhiveCompound (raw, sz))
			fprintf (stdlog, "PHIVE COMPOUND:%s has no shapes or water grid, nothing to convert\n", arg);
		FREE (cells);
	}
	else if (IsPhiveFile (raw, sz))
	{
		char *obj = 0, *json = 0;
		size_t obj_n = 0, json_n = 0;
		err = ConvertPhiveToObj (raw, sz, &obj, &obj_n, &json, &json_n);
		if (!err)
		{
			char dest[PATH_MAX], name[PATH_MAX];
			beside_source_dir (dest, sizeof (dest), arg);
			const char *sl = strrchr (arg, '/');
			snprintf (name, sizeof (name), "%s", sl ? sl + 1 : arg);
			char *dot = strrchr (name, '.');
			if (dot)
				*dot = 0;
			char obj_name[PATH_MAX], json_name[PATH_MAX];
			snprintf (obj_name, sizeof (obj_name), "%s.obj", name);
			snprintf (json_name, sizeof (json_name), "%s.json", name);
			if (verbose >= 0 || testmode)
				fprintf (stdlog, "%s%sDECODE PHIVE:%s (%zu bytes of OBJ) -> %s/%s + %s\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "",
					arg, obj_n, dest, obj_name, json_name);
			if (!testmode)
			{
				err = shade_write (arg, dest, basedir, obj_name, (u8 *)obj, (uint)obj_n);
				if (!err)
					err = shade_write (arg, dest, basedir, json_name, (u8 *)json, (uint)json_n);
			}
		}
		else if (verbose >= 0)
			fprintf (stdlog, "!! cannot convert %s: invalid or unsupported Phive shape\n", arg);
		FREE (obj);
		FREE (json);
	}
	FREE (raw);
	return err;
}
