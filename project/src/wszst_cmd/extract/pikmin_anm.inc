// Pikmin 1 ".anm" animation bundles (StdSystem::loadBundle in the decompilation): {s32 count}, then per entry
// {s32 type (2 = DCA, 3 = DCK), s32 size, s32 name length, name, size bytes}. Entries are written under their stored
// names (".dca" / ".dck" motion data), each with a "<name>.json" dump of its channels (see pikmin_dc_json).
// DCA / DCK motion data (AnimDca::read / AnimDck::read): {joints, frames}, three float blocks (scale, rotation in
// degrees, translation; each u32 count + floats) and per joint {group, parent} + 9 channel records {entries, offset
// (in floats), flags (DCK only)}. Each channel is dumped as its raw floats between its offset and the next
// channel's offset (DCA: one sampled value per frame, stride 1; DCK: keys of 3 floats = time, value, tangent; a static
// channel has a single value). Returns JSON, or NULL when the data does not chain exactly to its end.
static char *pikmin_dc_json (const u8 *d, size_t sz, bool dck, size_t *out_len)
{
	if (sz < 16)
		return 0;
	pcr_ctx_t c;
	memset (&c, 0, sizeof (c));
	c.d = d;
	c.sz = sz;
	c.ok = true;
	const uint joints = rd_be32 (d), frames = rd_be32 (d + 4);
	if (!joints || joints > 1024)
		return 0;
	size_t p = 8;
	const u8 *blk[3];
	uint nblk[3];
	for (int b = 0; b < 3; b++)
	{
		if (p + 4 > sz)
			return 0;
		nblk[b] = rd_be32 (d + p);
		p += 4;
		if ((size_t)nblk[b] * 4 > sz - p)
			return 0;
		blk[b] = d + p;
		p += (size_t)nblk[b] * 4;
	}
	const size_t rec = dck ? 12 : 8;
	if (p + (size_t)joints * (8 + 9 * rec) != sz)
		return 0;
	static const char *const chn[9] = { "scaleX", "scaleY", "scaleZ", "rotX", "rotY", "rotZ", "transX", "transY", "transZ" };
	pcr_emit (&c, "{\n  \"frames\": %u,\n  \"joints\": [", frames);
	for (uint j = 0; j < joints && c.ok; j++)
	{
		const u8 *r = d + p + (size_t)j * (8 + 9 * rec);
		pcr_emit (&c, "%s\n    {\"index\": %u, \"group\": %d, \"parent\": %d, \"channels\": {", j ? "," : "", j,
			(int)rd_be32 (r), (int)rd_be32 (r + 4));
		for (int k = 0; k < 9 && c.ok; k++)
		{
			const u8 *q = r + 8 + (size_t)k * rec;
			const uint n = rd_be32 (q), off = rd_be32 (q + 4), fl = dck ? rd_be32 (q + 8) : 0, b = k / 3;
			// extent of this channel's floats = distance to the next channel offset in the same block
			uint end = nblk[b];
			for (uint oj = 0; oj < joints; oj++)
				for (int ok2 = b * 3; ok2 < b * 3 + 3; ok2++)
				{
					const uint o = rd_be32 (d + p + (size_t)oj * (8 + 9 * rec) + 8 + (size_t)ok2 * rec + 4);
					if (o > off && o < end)
						end = o;
				}
			if (off > nblk[b] || n > 100000)
			{
				c.ok = false;
				break;
			}
			const uint len = end - off, stride = n && len % n == 0 ? len / n : 0;
			pcr_emit (&c, "%s\"%s\": {\"entries\": %u, \"flags\": %u, \"stride\": %u, \"values\": [", k ? ", " : "",
				chn[k], n, fl, stride);
			for (uint i = 0; i < len; i++)
			{
				const u32 u = rd_be32 (blk[b] + ((size_t)off + i) * 4);
				float f;
				memcpy (&f, &u, 4);
				pcr_emit (&c, "%s%.9g", i ? ", " : "", (double)f);
			}
			pcr_emit (&c, "]}");
		}
		pcr_emit (&c, "}}");
	}
	pcr_emit (&c, "\n  ]\n}\n");
	if (!c.ok)
	{
		FREE (c.out);
		return 0;
	}
	*out_len = c.len;
	return c.out;
}

static enumError extract_pikmin_anm_file (ccp arg, ccp basedir, uint depth)
{
	if (!is_ext (arg, ".anm"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	const uint count = sz >= 16 ? rd_be32 (raw) : 0;
	if (!count || count > 4096)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	// validate the whole chain first: it must end exactly at EOF
	size_t p = 4;
	for (uint i = 0; i < count; i++)
	{
		if (p + 12 > sz)
			p = sz + 1;
		else
		{
			const uint type = rd_be32 (raw + p), size = rd_be32 (raw + p + 4), nl = rd_be32 (raw + p + 8);
			if (type > 3 || nl == 0 || nl > 256 || p + 12 + (size_t)nl + size > sz)
				p = sz + 1;
			else
				p += 12 + (size_t)nl + size;
		}
		if (p > sz)
			break;
	}
	if (p != sz)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	nintendo_sarc_entry_t *entries = CALLOC (count * 2, sizeof (*entries));
	if (!entries)
	{
		FREE (raw);
		return ERR_OUT_OF_MEMORY;
	}
	uint n = 0;
	p = 4;
	for (uint i = 0; i < count; i++)
	{
		const uint size = rd_be32 (raw + p + 4), nl = rd_be32 (raw + p + 8);
		char name[300];
		snprintf (name, sizeof (name), "%.*s", (int)nl, (ccp)raw + p + 12);
		for (char *q = name; *q; q++)
			if (*q == '/' || *q == '\\')
				*q = '_';
		if (!name[0])
			snprintf (name, sizeof (name), "%04u.bin", i);
		if (OwnedEntryAdd (entries, n, name, raw + p + 12 + nl, size))
			n++;
		{
			const uint type = rd_be32 (raw + p);
			size_t jl = 0;
			char *js = type == 2 || type == 3 ? pikmin_dc_json (raw + p + 12 + nl, size, type == 3, &jl) : 0;
			if (js)
			{
				char jn[320];
				snprintf (jn, sizeof (jn), "%s.json", name);
				if (OwnedEntryAdd (entries, n, jn, (const u8 *)js, (uint)jl))
					n++;
				FREE (js);
			}
		}
		p += 12 + (size_t)nl + size;
	}
	FREE (raw);
	if (!n)
	{
		FREE (entries);
		return ERR_NOTHING_TO_DO;
	}
	return write_owned_entries (arg, basedir, depth, "PIKMIN-ANM", entries, n);
}
