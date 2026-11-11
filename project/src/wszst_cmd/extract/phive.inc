#include "lib-phive.h"
// Phive collision shape (Splatoon 3 ".bphsh"): with --decode (xx / xdecode) written beside the source as "<name>.obj" (collision
// mesh, one group per material) and "<name>.json" (material name, material flags and collision-disable flags per group).
static enumError extract_phive_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	if (!is_ext (arg, ".bphsh") || (export_count == 0 && !opt_decode))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	enumError err = ERR_NOTHING_TO_DO;
	if (IsPhiveFile (raw, sz))
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
