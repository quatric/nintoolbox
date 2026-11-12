// HAL Laboratory XData ("XBIN") files: binary YAML and FDG scene graphs -> YAML text.
static enumError extract_xbin_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	(void)depth;
	u8 head[0x20];
	FILE *fp = fopen (arg, "rb");
	if (!fp)
		return ERR_NOTHING_TO_DO;
	const size_t got = fread (head, 1, sizeof (head), fp);
	fclose (fp);
	if (got < 0x18 || memcmp (head, "XBIN", 4))
		return ERR_NOTHING_TO_DO;

	u8 *raw = 0;
	size_t raw_size = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 256u << 20, 2, 0, false))
		return ERR_NOTHING_TO_DO;
	const xbin_kind_t kind = GetXBINKind (raw, (uint)raw_size);
	if (kind != XBIN_KIND_YAML && kind != XBIN_KIND_FDG)
	{
		FREE (raw);
		// a recognised XData file whose payload is a game-specific table: keep it as is
		return kind == XBIN_KIND_NONE ? ERR_NOTHING_TO_DO : ERR_OK;
	}

	char dest[PATH_MAX];
	beside_source_dest_ext (dest, sizeof (dest), arg, ".yml");
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sDECODE XBIN/%s:%s -> TEXT:%s\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", kind == XBIN_KIND_YAML ? "YAML" : "FDG", arg, dest);
	enumError err = testmode ? ERR_OK : SaveTextXBIN (raw, (uint)raw_size, dest);
	FREE (raw);
	return err;
}
