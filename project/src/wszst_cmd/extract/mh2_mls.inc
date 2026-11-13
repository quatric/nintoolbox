// Rockstar Leeds script lists (Manhunt 2 ".mls", big endian): "MHLS", u16 + u16 (9, 3), then chunks {"MHSC", u32 size, body} to the end of the file. Each body starts with an "SCPT"
// section {u32 section size, NUL-padded script name} ("oncreate", "onenter", ...) and holds the compiled script of one level entity (bytecode not decoded). Every chunk is split into
// "<name>.mls.d/NNNN_<script name>.mhsc" (chunk header included).
static enumError extract_mh2_mls_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".mls"))
		return ERR_NOTHING_TO_DO;
	u8 *c = 0;
	size_t cs = 0;
	if (LoadFileAlloc (arg, 0, 0, &c, &cs, 0, 0, 0, false) || !c || cs < 24 || memcmp (c, "MHLS", 4))
	{
		FREE (c);
		return ERR_NOTHING_TO_DO;
	}
	nintendo_sarc_entry_t *entries = 0;
	uint n = 0, cap = 0;
	enumError err = ERR_NOTHING_TO_DO;
	for (size_t p = 8; p + 16 <= cs;)
	{
		const size_t sz = rd_be32 (c + p + 4);
		if (memcmp (c + p, "MHSC", 4) || sz < 8 || sz > cs - p - 8)
			break;
		char name[96] = "script";
		if (!memcmp (c + p + 8, "SCPT", 4))
		{
			size_t nl = 0;
			while (nl < 40 && 16 + nl < sz + 8 && c[p + 16 + nl])
				nl++;
			if (nl)
			{
				char t[64];
				snprintf (t, sizeof t, "%.*s", (int)nl, (ccp)c + p + 16);
				snprintf (name, sizeof name, "%s", t);
				for (char *k = name; *k; k++)
					if (!isalnum ((u8)*k) && *k != '_' && *k != '-')
						*k = '_';
			}
		}
		char path[PATH_MAX];
		snprintf (path, sizeof path, "%04u_%.60s.mhsc", n, name);
		if (n == cap)
		{
			cap = cap ? 2 * cap : 64;
			entries = REALLOC (entries, cap * sizeof (*entries));
			if (!entries)
			{
				err = ERR_OUT_OF_MEMORY;
				goto done;
			}
			memset (entries + n, 0, (cap - n) * sizeof (*entries));
		}
		if (!OwnedEntryAdd (entries, n, path, c + p, (uint)(sz + 8)))
			goto done;
		n++;
		p += sz + 8;
	}
	if (n)
	{
		err = write_owned_entries (arg, basedir, depth, "MH2-MLS", entries, n);
		entries = 0;
	}
done:
	if (entries)
		ResetOwnedEntries (entries, n);
	FREE (c);
	return err;
}
