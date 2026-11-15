// "ENV0" collision archive (Paws & Claws - Pet Resort, Wii: TPWiiProj/COL/Merged.bin): back to back chunks
// {4 character tag (ENV0..ENVE, CAM0.., COL0.., DOG0, CAT0, ..), u32 size, block of `size` bytes starting "BCBS" (or "BCAB" for the small ones)}. The BCBS block
// starts with {"BCBS", u32 offsets[6] (relative to the block start - 8), u32 count, f32 bbox[6] (min xyz, max xyz; y is
// always 0: a 2D map)} and ends with an array of 12 byte {f32 x, 0, f32 z} vertices; the sections before it (tree and
// 32 byte records) are not interpreted. Every chunk is written as "<tag>.bcbs" and "<tag>.json" (bbox, record counts and
// the vertex list).
#pragma push_macro("calloc")
#pragma push_macro("free")
#undef calloc
#undef free
static float env0_f32 (const u8 *p)
{
	const u32 w = rd_be32 (p);
	float f;
	memcpy (&f, &w, 4);
	return f;
}

static enumError extract_env0_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".bin"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	if (sz < 48 || memcmp (raw, "ENV0", 4) || memcmp (raw + 8, "BCBS", 4))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	// validate the chain of chunks: it must end exactly at the end of the file
	uint count = 0;
	size_t p = 0;
	while (p + 16 <= sz)
	{
		const u32 s = rd_be32 (raw + p + 4);
		if (memcmp (raw + p + 8, "BC", 2) || s < 8 || (u64)p + 8 + s > sz || s % 4)
			break;
		if (!memcmp (raw + p + 8, "BCBS", 4) && (s < 0x40 || rd_be32 (raw + p + 12) != 0x38 || rd_be32 (raw + p + 36) > s))
			break;
		p += 8 + (size_t)s;
		count++;
	}
	if (!count || p != sz)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	nintendo_sarc_entry_t *entries = CALLOC (2 * (size_t)count, sizeof (*entries));
	bool good = entries != 0;
	uint ne = 0;
	p = 0;
	for (uint i = 0; i < count && good; i++)
	{
		const u32 s = rd_be32 (raw + p + 4);
		const u8 *b = raw + p + 8;
		char tag[5];
		memcpy (tag, raw + p, 4);
		tag[4] = 0;
		for (int k = 0; k < 4; k++)
			if (tag[k] < 0x21 || tag[k] > 0x7e || tag[k] == '/')
				tag[k] = '_';
		char name[32];
		snprintf (name, sizeof (name), "%s.bcbs", tag);
		good = OwnedEntryAdd (entries, ne++, name, b, s);
		if (memcmp (b, "BCBS", 4))
		{
			p += 8 + (size_t)s; // other inner block types (BCAB): raw only
			continue;
		}
		// JSON summary
		const u32 vo = rd_be32 (b + 4 + 4 * 5);
		const u32 nv = vo < s ? (s - vo) / 12 : 0;
		size_t cap = 512 + (size_t)nv * 48;
		char *js = good ? CALLOC (1, cap) : 0;
		if (js)
		{
			size_t n = 0;
			n += (size_t)snprintf (js + n, cap - n, "{\n  \"tag\": \"%s\",\n  \"count\": %u,\n  \"bbox\": [", tag, rd_be32 (b + 28));
			for (int k = 0; k < 6; k++)
				n += (size_t)snprintf (js + n, cap - n, "%s%.9g", k ? ", " : "", (double)env0_f32 (b + 32 + 4 * k));
			n += (size_t)snprintf (js + n, cap - n, "],\n  \"sections\": [");
			for (int k = 0; k < 6; k++)
				n += (size_t)snprintf (js + n, cap - n, "%s%u", k ? ", " : "", rd_be32 (b + 4 + 4 * k));
			n += (size_t)snprintf (js + n, cap - n, "],\n  \"vertices\": [");
			for (u32 k = 0; k < nv; k++)
				n += (size_t)snprintf (js + n, cap - n, "%s[%.9g, %.9g]", k ? ", " : "", (double)env0_f32 (b + vo + 12 * k),
					(double)env0_f32 (b + vo + 12 * k + 8));
			n += (size_t)snprintf (js + n, cap - n, "]\n}\n");
			snprintf (name, sizeof (name), "%s.json", tag);
			good = OwnedEntryAdd (entries, ne++, name, (const u8 *)js, n);
			free (js);
		}
		else
			good = false;
		p += 8 + (size_t)s;
	}
	enumError err = ERR_NOTHING_TO_DO;
	if (good)
		err = write_owned_entries (arg, basedir, depth, "ENV0", entries, ne);
	else
		FREE (entries);
	FREE (raw);
	return err;
}
#pragma pop_macro("free")
#pragma pop_macro("calloc")
