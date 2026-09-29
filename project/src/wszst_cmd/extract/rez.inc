// Humongous / Cat Daddy Resource.rez (Backyard Football '10, Wii): GX
// textures -> PNG, DSP-ADPCM sounds -> WAV and THP videos, named GROUP_INDEX,
// in a directory named after the file.
static ccp rez_texname (void *vctx, u32 id)
{
	char *png = vctx;
	// ids below 0x10000 name a slot (a resource on its own) directly
	if (id >> 16)
		snprintf (png, 64, "%04u_%03u.png", id >> 16, id & 0xffff);
	else
		snprintf (png, 64, "%04u_000.png", id);
	return png;
}

static enumError extract_rez_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".rez"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t raw_size = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false) || !raw)
		return ERR_NOTHING_TO_DO;
	uint count = 0;
	rez_res_t *list = IsHumongousRez (raw, raw_size) ? ListRezResources (raw, raw_size, &count) : 0;
	if (!list || !count)
	{
		FREE (list);
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	char dest[PATH_MAX];
	beside_source_dest (dest, sizeof (dest), arg);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT REZ:%s (%u resources) -> %s/\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", arg, count, dest);

	enumError err = ERR_OK;
	char png[64];
	// the skeleton of the first skinned model dresses the motions
	model_t *skeleton = 0;
	for (uint i = 0; i < count && !skeleton && !testmode; i++)
		if (list[i].kind == REZ_MESH)
		{
			size_t n = 0;
			u8 *res = LoadRezResource (raw, raw_size, list + i, &n);
			model_t *model = res ? ParseRezModel (res, n, 0, 0) : 0;
			skeleton = CopyRezSkeleton (model);
			FreeModel (model);
			FREE (res);
		}
	for (uint i = 0; i < count && !err && !testmode; i++)
	{
		const rez_res_t *r = list + i;
		if (r->kind == REZ_ANIM)
			continue; // written with its model
		size_t n = 0;
		u8 *res = LoadRezResource (raw, raw_size, r, &n);
		if (!res)
			continue;
		char path[PATH_MAX];
		const ccp ext = r->kind == REZ_TEXTURE			   ? "png"
			: r->kind == REZ_SOUND						   ? "wav"
			: r->kind == REZ_MESH || r->kind == REZ_MOTION ? "glb"
														   : "thp";
		snprintf (path, sizeof (path), "%s/%s%04u_%03u.%s", dest, basedir ? basedir : "", r->group,
			r->index, ext);
		if (r->kind == REZ_TEXTURE)
		{
			u8 *rgba = 0;
			uint w = 0, h = 0;
			if (!DecodeRezTexture (&rgba, &w, &h, res, n))
				err = SaveDecodedRGBAToPNG (rgba, w, h, &be_func, path, 0, true);
		}
		else if (r->kind == REZ_MOTION)
		{
			model_t *model = ParseRezMotion (res, n, skeleton);
			if (model)
			{
				ExportModelToGLB (model, path);
				FreeModel (model);
			}
		}
		else if (r->kind == REZ_MESH)
		{
			model_t *model = ParseRezModel (res, n, rez_texname, png);
			if (model)
			{
				// the animation is the resource after the model
				const rez_res_t *ar = i + 1 < count ? list + i + 1 : 0;
				size_t an = 0;
				u8 *anim = ar && ar->kind == REZ_ANIM && ar->group == r->group
						&& ar->index == r->index + 1
					? LoadRezResource (raw, raw_size, ar, &an)
					: 0;
				if (anim)
					AddRezAnimation (model, res, n, anim, an);
				FREE (anim);
				ExportModelToGLB (model, path);
				FreeModel (model);
			}
		}
		else
		{
			u8 *wav = 0;
			size_t wav_size = 0;
			const u8 *out = res;
			size_t out_size = n;
			if (r->kind == REZ_SOUND)
			{
				if (DecodeRezSound (&wav, &wav_size, res, n))
				{
					FREE (res);
					continue;
				}
				out = wav;
				out_size = wav_size;
			}
			File_t F;
			err = CreateFileOpt (&F, true, path, false, arg);
			if (F.f && fwrite (out, 1, out_size, F.f) != out_size)
				err = FILEERROR1 (
					&F, ERR_WRITE_FAILED, "Writing %zu bytes failed: %s\n", out_size, path);
			ResetFile (&F, opt_preserve);
			FREE (wav);
		}
		FREE (res);
	}
	FreeModel (skeleton);
	FREE (list);
	FREE (raw);
	return err;
}
