// IO Interactive Glacier material libraries (Mini Ninjas ".MAT" inside the scene ".zip" packs; big endian). A tagged typed-object stream: a header {u32 offsets..} followed by schema records
// {fourcc tag, u32 value or offset, u16 size, u16 kind} (NAME, TYPE, PATH, IDEN, SUBC, CLAS, ENAB, BENA, BMOD, OPAC, ATST, AREF, CULL, ZBIA, ZOFF, TXID, TILU, TILV, RSTA, TEXT, SCRL, BIND, INST ...)
// and the instance records, one per material, which start with the NUL terminated material name ("Characters/Ninja/Hat_01") followed by its property tags. A "TXID" record whose kind words
// are {1, 2} holds an inline texture id: the id of the ".TEX" entry (ids count up from 128 in a bank; "Characters/Ninja/HatSign_01" has TXID 128 = the first texture of the scene bank).
// Written as "<file>.json": every texture binding as {material, txid} (the texture slot is not recorded: the "map*" names only occur in the schema). Bindings are not decoded further (tiling, blending and render state stay
// in the raw tags).
static bool glacier_mat_name (const u8 *p, size_t n)
{
	if (n < 3 || n > 200)
		return false;
	for (size_t i = 0; i < n; i++)
		if (p[i] < 0x20 || p[i] >= 0x7f)
			return false;
	return true;
}

static enumError extract_glacier_mat_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	if (!is_ext (arg, ".mat"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw || sz < 0x40 || rd_be32 (raw) >= sz || rd_be32 (raw + 4) >= sz || rd_be32 (raw + 12) > sz + 0x100000
		|| memcmp (raw + 0x10, "Sprites", 7))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	sumo_buf_t b = { 0, 0, 0, true };
	sumo_printf (&b, "{\n\t\"format\": \"Glacier MAT\",\n\t\"bindings\": [");
	char mat[256] = "";
	uint n = 0;
	for (size_t p = 0x10; p + 12 <= sz;)
	{
		if (!memcmp (raw + p, "TXID", 4) && rd_be16 (raw + p + 8) == 1 && rd_be16 (raw + p + 10) == 2 && rd_be32 (raw + p + 4) >= 1)
		{
			sumo_printf (&b, "%s\n\t\t{\"material\": \"%s\", \"txid\": %u}", n++ ? "," : "", mat, rd_be32 (raw + p + 4));
			p += 12;
			continue;
		}
		// strings: NUL terminated runs; the material names contain '/'; "map*" names the texture slot
		if (raw[p] >= 0x20 && raw[p] < 0x7f && (p == 0 || raw[p - 1] < 0x20 || raw[p - 1] >= 0x7f))
		{
			size_t e = p;
			while (e < sz && raw[e])
				e++;
			if (e < sz && glacier_mat_name (raw + p, e - p))
			{
				if (memchr (raw + p, '/', e - p))
					snprintf (mat, sizeof (mat), "%s", (ccp)raw + p);
				p = e + 1;
				continue;
			}
		}
		p++;
	}
	sumo_printf (&b, "\n\t]\n}\n");
	FREE (raw);
	enumError err = ERR_NOTHING_TO_DO;
	if (n && b.ok && b.n < 0xffffffffu)
	{
		char dest[PATH_MAX], name[PATH_MAX];
		beside_source_dest (dest, sizeof (dest), arg);
		const char *sl = strrchr (arg, '/');
		snprintf (name, sizeof (name), "%s", sl ? sl + 1 : arg);
		char *dot = strrchr (name, '.');
		if (dot)
			strcpy (dot, ".json");
		else
			strcat (name, ".json");
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT GLACIER-MAT:%s (%u texture bindings) -> %s/%s\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, n, dest, name);
		err = shade_write (arg, dest, basedir, name, (u8 *)b.p, (uint)b.n);
	}
	FREE (b.p);
	return err;
}
