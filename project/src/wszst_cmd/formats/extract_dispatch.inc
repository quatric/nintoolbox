// Process a single file with the full XX pipeline: external pass-through
// (strong, then weak) first, then the native Nintendo codecs, and finally
// the regular SZS/U8/BRRES extractor.  When a pass-through tool unpacks a
// container into a staging directory, the staged tree is walked recursively
// so every leaf is peeled in turn.  Returns the strongest error seen, or
// ERR_NOTHING_TO_DO when nothing claimed the file.
static enumError extract_one_file_inner (ccp arg, ccp basedir, uint depth);

// Monster Games .sfx: a 0x80 header over a plain Nintendo DSP-ADPCM stream.
// The payload is wrapped in a GENH so mobipeg (or a stock ffmpeg) can decode
// it with its own adpcm_thp, which is deliberately not reimplemented here.
static enumError extract_sfx_file (ccp arg, ccp basedir, uint depth)
{
	// This extractor emits a decoded WAV preview with no reciprocal packer.
	// Keep it out of normal staging trees; XEXPORT is the explicit preview mode.
	if (export_count <= 0)
		return ERR_NOTHING_TO_DO;

	u8 *raw = 0;
	size_t raw_size = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false))
		return ERR_NOTHING_TO_DO;

	sfx_info_t info;
	if (raw_size > UINT_MAX || !GetSFXInfo (raw, (uint)raw_size, &info))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	u8 *genh = 0;
	uint genh_size = 0;
	enumError err = BuildGENHFromSFX (raw, (uint)raw_size, &genh, &genh_size);
	FREE (raw);
	if (err)
		return err;

	char dest[PATH_MAX];
	if (opt_dest && *opt_dest)
		snprintf (dest, sizeof (dest), "%s", opt_dest);
	else if (basedir && *basedir)
		snprintf (dest, sizeof (dest), "%s", basedir);
	else
		snprintf (dest, sizeof (dest), "%s.d", arg);
	CreatePath (dest, true);

	ccp stem = strrchr (arg, '/');
	stem = stem ? stem + 1 : arg;

	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT SFX:%s (%u Hz, %u bytes) -> %s/\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", arg, info.sample_rate, info.data_size, dest);
	if (testmode)
	{
		FREE (genh);
		return ERR_OK;
	}

	char genh_path[PATH_MAX], wav_path[PATH_MAX];
	snprintf (genh_path, sizeof (genh_path), "%s/%s.genh", dest, stem);
	snprintf (wav_path, sizeof (wav_path), "%s/%s.wav", dest, stem);

	err = SaveFile (genh_path, 0, 0, genh, genh_size, 0);
	FREE (genh);
	if (err)
		return err;

	err = PassthruDecodeAudio (genh_path, wav_path);
	unlink (genh_path); // scratch: it only existed to feed the decoder

	if (err == ERR_NOTHING_TO_DO)
		return ERROR0 (ERR_WARNING,
			"%s is DSP-ADPCM audio, but no mobipeg or ffmpeg was found to decode it"
			" (see --with-mobipeg)",
			arg);
	return err;
}

static enumError extract_one_file (ccp arg, ccp basedir, uint depth)
{
	enumError err = extract_one_file_inner (arg, basedir, depth);
	if (depth > 0 && err > ERR_WARNING)
		return ERR_WARNING;
	return err;
}

// Heavy Iron .ho assets are written as "name.<8 hex digits of the type hash>";
// their payloads are opaque engine data.
static bool is_ho_asset_name (ccp arg)
{
	ccp dot = strrchr (arg, '.');
	if (!dot || dot == arg || strlen (dot) != 9)
		return false;
	for (uint i = 1; i < 9; i++)
		if (!isxdigit ((u8)dot[i]))
			return false;
	return true;
}
