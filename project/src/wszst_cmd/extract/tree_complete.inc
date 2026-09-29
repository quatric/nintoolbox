// Finish every extraction below ROOT before exporting any model. A staged
// disc/container can keep a model in one BRRES and its TEX0 in a later sibling
// archive; exporting while extract_tree() is still walking makes COLLADA image
// resolution depend on readdir/archive order. This wrapper is deliberately
// re-entrant: an outer two-pass run sets export_count to zero, so nested
// SARC/PAC/GFA/pass-through containers only decode and the outermost root owns
// the one final texture-index/export pass.
static enumError extract_tree_complete (ccp root, uint depth)
{
	ccp saved_dest = opt_dest;
	opt_dest = 0; // preserve each asset's path; a shared --dest would collide

	if (export_count <= 0)
	{
		enumError err = extract_tree (root, depth);
		opt_dest = saved_dest;
		return err;
	}

	const int saved_export_count = export_count;
	export_count = 0;
	const bool saved_model_deferred = g_in_model_deferred_pass;
	g_in_model_deferred_pass = true;
	enumError err = extract_tree (root, depth);
	g_in_model_deferred_pass = saved_model_deferred;
	export_count = saved_export_count;

	extract_bfres_textures_tree (root, depth);
	extract_bcres_textures_tree (root, depth);
	extract_hsd_textures_tree (root, depth);
	extract_halbank_textures_tree (root, depth);
	extract_hsf_textures_tree (root, depth);
	extract_nsbmd_textures_tree (root, depth);
	SetDAETextureSearchRoot (root);
	const enumError model_err = export_models_tree (root, depth);
	SetDAETextureSearchRoot (0);
	opt_dest = saved_dest;
	if (err < model_err)
		err = model_err;
	return err;
}
