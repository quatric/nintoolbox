// Blue Tongue Toshi keyframe library (.tkl, Nickelodeon Barnyard Wii): a pool
// of quantized bone-animation keyframes (translations, quaternions, scales)
// shared by one or more models' skeletons. There is no mesh/skeleton in this
// file (that lives in the .trb model, which references this library by
// name); dumped as a YAML listing of the decoded keyframe arrays. Scale
// keyframes are best-effort (see toshi_tkl_t in lib-toshi.h): no disc sample
// has any, so that decode path is unverified.
static enumError extract_toshi_tkl_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".tkl"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t raw_size = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false) || !raw)
		return ERR_NOTHING_TO_DO;
	toshi_trb_t trb;
	if (OpenToshiTrb (&trb, raw, raw_size))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	toshi_tkl_t tkl;
	if (OpenToshiTkl (&trb, &tkl))
	{
		ResetToshiTrb (&trb);
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	char dest[PATH_MAX];
	if (opt_dest)
		SubstDest (dest, sizeof (dest), arg, opt_dest, 0, ".yaml", false);
	else
		snprintf (dest, sizeof (dest), "%s.yaml", arg);

	if (verbose >= 0 || testmode)
		fprintf (stdlog,
			"%s%sEXTRACT TOSHI-TKL:%s (%u translations, %u quaternions, %u scales) -> %s\n",
			verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, tkl.num_t, tkl.num_q, tkl.num_s,
			dest);

	enumError err = ERR_OK;
	if (!testmode)
	{
		File_t F;
		err = CreateFileOpt (&F, true, dest, false, arg);
		if (F.f)
		{
			fprintf (F.f,
				"#\n# Blue Tongue Toshi keyframe library, decoded from %s\n"
				"# Translations and quaternions are verified against the CLER\n"
				"# relocation list and unit-quaternion magnitude checks.\n"
				"# Scale keyframes (if any) are best-effort: no disc sample with\n"
				"# num-scales > 0 has been found to confirm their encoding.\n#\n",
				arg);
			fprintf (F.f, "name: %s\n", tkl.name);
			fprintf (F.f, "translation-scale: [ %.9g, %.9g, %.9g ]\n", tkl.scale[0], tkl.scale[1],
				tkl.scale[2]);

			fprintf (F.f, "translations: # %u entries, verified\n", tkl.num_t);
			for (u32 i = 0; i < tkl.num_t && !err; i++)
			{
				float v[3];
				ToshiTklTranslation (&tkl, i, v);
				fprintf (F.f, " - [ %.9g, %.9g, %.9g ]\n", v[0], v[1], v[2]);
			}

			fprintf (F.f, "quaternions: # %u entries, verified (x,y,z,w)\n", tkl.num_q);
			for (u32 i = 0; i < tkl.num_q && !err; i++)
			{
				float v[4];
				ToshiTklQuaternion (&tkl, i, v);
				fprintf (F.f, " - [ %.9g, %.9g, %.9g, %.9g ]\n", v[0], v[1], v[2], v[3]);
			}

			fprintf (F.f, "scales: # %u entries, BEST-EFFORT decode\n", tkl.num_s);
			for (u32 i = 0; i < tkl.num_s && !err; i++)
			{
				float v = 0;
				ToshiTklScale (&tkl, i, &v);
				fprintf (F.f, " - %.9g\n", v);
			}

			if (ferror (F.f))
				err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing YAML failed: %s\n", dest);
		}
		ResetFile (&F, opt_preserve);
	}
	ResetToshiTrb (&trb);
	FREE (raw);
	return err;
}
