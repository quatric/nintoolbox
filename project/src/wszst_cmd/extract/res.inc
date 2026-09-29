// And-Kensaku ".res": an uncompressed "Pres" archive (same container as the
// payload of a ".rz", just without the LZ77 wrapper).
static enumError extract_res_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".res"))
		return ERR_NOTHING_TO_DO;

	u8 *raw = 0;
	size_t raw_size = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false))
		return ERR_NOTHING_TO_DO;
	if (raw_size > UINT_MAX || raw_size < 0x84 || memcmp (raw, "Pres", 4))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	kensaku_t k;
	enumError err = ScanKensakuRes (&k, raw, raw_size);
	FREE (raw);
	if (err)
		return ERR_NOTHING_TO_DO;
	return extract_kensaku_members (&k, arg, basedir, depth);
}
