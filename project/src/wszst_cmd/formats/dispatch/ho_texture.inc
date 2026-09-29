	if (is_ho_asset_name (arg))
	{
		const enumError terr = extract_ho_texture_file (arg, basedir, depth);
		return terr == ERR_NOTHING_TO_DO ? ERR_OK : terr;
	}
