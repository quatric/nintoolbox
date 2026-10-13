// Capcom "ANA@" archives (".ana"): "ANA@", u32 0x20, u32 total size, ..., u32 count @0x20, then count
// 0x10-byte entries from 0x30 {u32 offset, u32 size, name[8]} (BRRES payloads, written as "<name>.brres").
// Capcom "@LPK" / "@FPK" archives (Dead Rising: Chop Till You Drop, Wii; ".lpk" / ".fpk"), big endian.
// @LPK: "@LPK", 0, u32 count, 0, then count 0x30-byte entries {name[0x28], u32 size, u32 offset}.
// @FPK: "@FPK", 0xffff, u32 groups, 0, then per group 0x10 bytes {u32 id, u32 count, u32 table, 0};
// a group table holds count 0x10-byte records {u32 index, u32 offset, -1, -1}; a member runs to the next
// larger offset (or the end of the file). Members are written as "<group>-<index>.<ext>" ("brres" for a
// "bres" payload, "bin" otherwise).
static enumError extract_capcomarc_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".lpk") && !is_ext (arg, ".fpk") && !is_ext (arg, ".ana"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	const bool lpk = sz >= 0x10 && !memcmp (raw, "@LPK", 4), fpk = sz >= 0x10 && !memcmp (raw, "@FPK", 4),
		   ana = sz >= 0x30 && !memcmp (raw, "ANA@", 4);
	const uint count = ana ? rd_be32 (raw + 0x20) : lpk || fpk ? rd_be32 (raw + 8) : 0;
	if ((!lpk && !fpk && !ana) || !count || count > 20000)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	nintendo_sarc_entry_t *entries = CALLOC (fpk ? 65536 : count, sizeof (*entries));
	if (!entries)
	{
		FREE (raw);
		return ERR_OUT_OF_MEMORY;
	}
	uint n = 0;
	bool ok = true;
	if (ana)
	{
		if (0x30 + 0x10 * (size_t)count > sz)
			ok = false;
		for (uint i = 0; ok && i < count; i++)
		{
			const u8 *e = raw + 0x30 + 0x10 * (size_t)i;
			const size_t off = rd_be32 (e), size = rd_be32 (e + 4);
			char nm[9], name[40];
			memcpy (nm, e + 8, 8);
			nm[8] = 0;
			for (char *p = nm; *p; p++)
				if (*p == '/' || *p == '\\' || (u8)*p < 0x20)
					*p = '_';
			if (!*nm)
				snprintf (nm, sizeof (nm), "e%04u", i);
			if (off > sz || size > sz - off)
			{
				ok = false;
				break;
			}
			const char *ext = size >= 4 && !memcmp (raw + off, "bres", 4) ? "brres" : "bin";
			snprintf (name, sizeof (name), "%s.%s", nm, ext);
			for (uint dup = 1, k = 0; k < n; k++)
				if (!strcmp (entries[k].name, name))
					snprintf (name, sizeof (name), "%s-%u.%s", nm, dup++, ext), k = (uint)-1;
			ok = OwnedEntryAdd (entries, n, name, raw + off, size);
			if (ok)
				n++;
		}
	}
	else if (lpk)
	{
		if (0x10 + 0x30 * (size_t)count > sz)
			ok = false;
		for (uint i = 0; ok && i < count; i++)
		{
			const u8 *e = raw + 0x10 + 0x30 * (size_t)i;
			const size_t size = rd_be32 (e + 0x28), off = rd_be32 (e + 0x2c);
			char name[0x29];
			memcpy (name, e, 0x28);
			name[0x28] = 0;
			for (char *p = name; *p; p++)
				if (*p == '/' || *p == '\\' || (u8)*p < 0x20)
					*p = '_';
			if (!*name)
				snprintf (name, sizeof (name), "entry%04u", i);
			if (off > sz || size > sz - off)
			{
				ok = false;
				break;
			}
			uint dup = 0;
			char uniq[0x40];
			for (;;)
			{
				snprintf (uniq, sizeof (uniq), dup ? "%s-%u" : "%s%.0u", name, dup);
				uint k = 0;
				while (k < n && strcmp (entries[k].name, uniq))
					k++;
				if (k == n)
					break;
				dup++;
			}
			ok = OwnedEntryAdd (entries, n, uniq, raw + off, size);
			if (ok)
				n++;
		}
	}
	else
	{
		typedef struct { u32 grp, idx, off; } rec_t;
		rec_t *rec = CALLOC (65536, sizeof (*rec));
		uint nr = 0;
		if (!rec || 0x10 + 0x10 * (size_t)count > sz)
			ok = false;
		for (uint g = 0; ok && g < count; g++)
		{
			const u8 *e = raw + 0x10 + 0x10 * (size_t)g;
			const uint id = rd_be32 (e), cnt = rd_be32 (e + 4);
			const size_t tab = rd_be32 (e + 8);
			if (tab > sz || cnt > 4096 || 0x10 * (size_t)cnt > sz - tab || nr + cnt > 65536)
			{
				ok = false;
				break;
			}
			for (uint k = 0; k < cnt; k++, nr++)
			{
				const u8 *r = raw + tab + 0x10 * (size_t)k;
				rec[nr].grp = id, rec[nr].idx = rd_be32 (r), rec[nr].off = rd_be32 (r + 4);
				if (rec[nr].off > sz)
					ok = false;
			}
		}
		for (uint i = 0; ok && i < nr; i++)
		{
			size_t end = sz;
			for (uint k = 0; k < nr; k++)
				if (rec[k].off > rec[i].off && rec[k].off < end)
					end = rec[k].off;
			const size_t off = rec[i].off, size = end - off;
			char name[40];
			snprintf (name, sizeof (name), "%02x-%02x.%s", rec[i].grp, rec[i].idx,
				size >= 4 && !memcmp (raw + off, "bres", 4) ? "brres" : "bin");
			ok = OwnedEntryAdd (entries, n, name, raw + off, size);
			if (ok)
				n++;
		}
		FREE (rec);
	}
	enumError err = ERR_NOTHING_TO_DO;
	if (ok && n)
		err = write_owned_entries (arg, basedir, depth, ana ? "ANA" : lpk ? "LPK" : "FPK", entries, n);
	else
		FREE (entries);
	FREE (raw);
	return err;
}
