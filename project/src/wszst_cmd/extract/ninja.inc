#include "lib-ninja.h"
// Ninja NJCM chunk models (Illvelo/Milestone Wii ports) -> GLB.
static enumError extract_ninja_file (ccp arg, ccp basedir, uint depth)
{
	if (export_count == 0 && !opt_decode)
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t raw_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false);
	if (err)
		return ERR_NOTHING_TO_DO;
	if (raw_size < 16 || !IsNinjaChunk (raw, raw_size) || !memmem (raw, raw_size, "MCJN", 4))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	char dest[PATH_MAX];
	beside_source_dest_ext (dest, sizeof (dest), arg, ".glb");
	model_t *model = ParseNinjaModel (raw, raw_size);
	FREE (raw);
	if (!model)
		return ERR_NOTHING_TO_DO;
	if (testmode)
		fprintf (stdlog, "WOULD EXTRACT NINJA:%s -> GLB:%s\n", arg, dest);
	else
	{
		ExportModelToGLB (model, dest);
		if (verbose >= 0)
			fprintf (stdlog, "%sEXTRACT NINJA:%s -> GLB:%s\n", verbose > 0 ? "\n" : "", arg, dest);
	}
	FreeModel (model);
	return ERR_OK;
}
