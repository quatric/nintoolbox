#include "lib-hktag.h"
// Phive cloth (".bphcl") and navigation meshes (".bphnm", Havok 2025.2 hkai data; a trailing
// non-AAMP block, 13 floats, is appended to the YAML as "trailer_f32"): with --decode (xx / xdecode) the Havok tag file is written beside the
// source as "<file>.yml" (every object with its reflected fields) and the trailing AAMP cloth
// parameters as
// "<file>.param.yml".

static enumError extract_bphcl_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	if ((!is_ext (arg, ".bphcl") && !is_ext (arg, ".bphnm")) || (export_count == 0 && !opt_decode))
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
		const u32 tr_off = sz >= 0x20 ? rd_le32 (raw + 0x10) : 0, tr_size = sz >= 0x20 ? rd_le32 (raw + 0x1c) : 0;
		if (!err && !as && is_ext (arg, ".bphnm") && tr_size >= 4 && tr_off < sz && tr_size <= sz - tr_off && !(tr_size & 3))
		{
			char *nt = MALLOC (text_n + 64 + tr_size * 8);
			if (nt)
			{
				size_t n = text_n;
				memcpy (nt, text, text_n);
				n += snprintf (nt + n, 64, "trailer_f32: [");
				for (uint i = 0; i < tr_size / 4; i++)
				{
					const u32 v = rd_le32 (raw + tr_off + 4 * i);
					float f;
					memcpy (&f, &v, 4);
					n += snprintf (nt + n, 24, "%s%.9g", i ? ", " : "", f);
				}
				n += snprintf (nt + n, 4, "]\n");
				FREE (text);
				text = nt;
				text_n = n;
			}
		}
		if (err)
		{
			if (verbose >= 0)
				fprintf (stdlog, "!! cannot convert %s: unsupported Havok tag file\n", arg);
		}
		else
		{
			snprintf (name, sizeof (name), "%s.yml", base);
			if (verbose >= 0 || testmode)
				fprintf (stdlog, "%s%sDECODE PHIVE %s:%s (%zu bytes of YAML) -> %s/%s\n",
					verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", is_ext (arg, ".bphnm") ? "NAVMESH" : "CLOTH", arg, text_n, dest, name);
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
