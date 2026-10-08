#include "lib-dc2model.h"
// Speed 2 "DC2" container models (.dcm) -> GLB (see lib-dc2model.h).
static enumError extract_dc2model_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	(void)depth;
	if (!is_ext (arg, ".dcm") || (export_count <= 0 && !opt_decode))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t raw_size = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	model_t *model = IsDC2Model (raw, raw_size) ? ParseDC2Model (raw, raw_size) : 0;
	FREE (raw);
	if (!model)
		return ERR_NOTHING_TO_DO;
	char dest[PATH_MAX];
	beside_source_dest_ext (dest, sizeof (dest), arg, ".glb");
	if (testmode)
		fprintf (stdlog, "WOULD EXTRACT DC2:%s -> GLB:%s\n", arg, dest);
	else
	{
		ExportModelToGLB (model, dest);
		if (verbose >= 0)
			fprintf (stdlog, "%sEXTRACT DC2:%s -> GLB:%s\n", verbose > 0 ? "\n" : "", arg, dest);
	}
	FreeModel (model);
	return ERR_OK;
}
