// HSF has no dedicated "textures only" decoder the way BFRES/BCRES/HSD/
// Halbank do above -- DecodeHSF() decodes its embedded textures to PNG as
// an inseparable side effect of writing the model itself (see lib-hsf.c).
// Deferring the real HSF->GLB/DAE conversion to export_models_tree() (see
// g_in_model_deferred_pass) is not enough on its own: SetDAETextureSearchRoot()
// takes its one static snapshot of every PNG under ROOT *before*
// export_models_tree() runs at all, so even a model's own just-decoded
// texture -- written by that very same phase-2 call -- still isn't in the
// index yet when dae_texture_path() looks for it moments later.
//
// So this walker calls extract_hsf_file() early, with g_in_model_deferred_pass
// still off, forcing every .hsf to actually convert (and thus decode its
// textures to disk) *before* the search root gets indexed. The GLB/DAE this
// produces is thrown away -- export_models_tree()'s later, properly-indexed
// pass overwrites it with the real, correctly-textured output -- so this
// does mean every real .hsf gets fully decoded twice. Verified worth it on
// a real Calling asset (Ecam0300etc.bin): 282 missed texture bindings in
// one file down to 0 after this pass runs.
static enumError extract_hsf_textures_tree (ccp root, uint depth)
{
	if (depth > 32)
		return ERR_FILE_TOO_BIG;
	DIR *dir = opendir (root);
	if (!dir)
		return ERR_NOT_EXISTS;
	struct dirent *de;
	while ((de = readdir (dir)))
	{
		if (!strcmp (de->d_name, ".") || !strcmp (de->d_name, ".."))
			continue;
		char path[PATH_MAX];
		const int len = snprintf (path, sizeof (path), "%s/%s", root, de->d_name);
		if (len < 0 || (uint)len >= sizeof (path))
			continue;
		struct stat st;
		if (lstat (path, &st))
			continue;
		if (S_ISDIR (st.st_mode))
			extract_hsf_textures_tree (path, depth + 1);
		else if (S_ISREG (st.st_mode))
			extract_hsf_file (path, 0, depth);
	}
	closedir (dir);
	return ERR_OK;
}
