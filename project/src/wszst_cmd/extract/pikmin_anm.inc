// Pikmin 1 ".anm" animation bundles and ".bun" texture / effect bundles (StdSystem::loadBundle in the decompilation):
// {s32 count}, then per entry {s32 type (0 binary, 1 texture, 2 DCA, 3 DCK), s32 size, s32 name length, name, size bytes}.
// ".bun" entries keep their stored directory paths. Entries are written under their stored
// names (".dca" / ".dck" motion data), each with a "<name>.json" dump of its channels (see pikmin_dc_json).
// DCA / DCK motion data (AnimDca::read / AnimDck::read; semantics from AnimData::extractSRT and AnimDck::extractSRT in
// the decompilation): {joints, frames}, three float blocks (scale, rotation in radians, translation; each u32 count +
// floats) and per joint {group, parent} + 9 channel records {entries, offset (in floats), flags (DCK only)}.
//   DCA: `entries` samples, one float per frame at offset + min(frame, entries - 1).
//   DCK: 0 entries = default (1 for scale / translation as the game does, 0 for rotation), 1 = a single static value,
//        more = keys of 3 floats (time, value, tangent) or, when the channel's flags are non-zero, 4 floats
//        (time, value, tangent in, tangent out).
// Returns JSON, or NULL when the data does not chain exactly to its end.
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
	pcr_emit (&c, "{\n  \"kind\": \"%s\",\n  \"frames\": %u,\n  \"joints\": [", dck ? "DCK" : "DCA", frames);
	for (uint j = 0; j < joints && c.ok; j++)
	{
		const u8 *r = d + p + (size_t)j * (8 + 9 * rec);
		pcr_emit (&c, "%s\n    {\"index\": %u, \"group\": %d, \"parent\": %d, \"channels\": {", j ? "," : "", j,
			(int)rd_be32 (r), (int)rd_be32 (r + 4));
		for (int k = 0; k < 9 && c.ok; k++)
		{
			const u8 *q = r + 8 + (size_t)k * rec;
			const uint n = rd_be32 (q), off = rd_be32 (q + 4), fl = dck ? rd_be32 (q + 8) : 0, b = k / 3;
			const uint stride = dck && n > 1 ? (fl ? 4 : 3) : 1;
			if (n > 100000 || (size_t)off + (size_t)n * stride > nblk[b])
			{
				c.ok = false;
				break;
			}
			pcr_emit (&c, "%s\"%s\": ", k ? ", " : "", chn[k]);
			if (!n)
				pcr_emit (&c, "{\"mode\": \"default\"}");
			else if (n == 1)
			{
				const u32 u = rd_be32 (blk[b] + (size_t)off * 4);
				float f;
				memcpy (&f, &u, 4);
				pcr_emit (&c, "{\"mode\": \"static\", \"value\": %.9g}", (double)f);
			}
			else
			{
				pcr_emit (&c, "{\"mode\": \"%s\"", dck ? "keys" : "samples");
				if (dck)
					pcr_emit (&c, ", \"stride\": %u", stride);
				pcr_emit (&c, ", \"values\": [");
				for (uint i = 0; i < n; i++)
				{
					if (dck)
						pcr_emit (&c, "%s[", i ? ", " : "");
					else if (i)
						pcr_emit (&c, ", ");
					for (uint v = 0; v < stride; v++)
					{
						const u32 u = rd_be32 (blk[b] + ((size_t)off + (size_t)i * stride + v) * 4);
						float f;
						memcpy (&f, &u, 4);
						pcr_emit (&c, "%s%.9g", v ? ", " : "", (double)f);
					}
					if (dck)
						pcr_emit (&c, "]");
				}
				pcr_emit (&c, "]}");
			}
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
	const bool is_bun = is_ext (arg, ".bun");
	if (!is_bun && !is_ext (arg, ".anm"))
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
			if (*q == '\\' || (*q == '/' && !is_bun))
				*q = '_';
		if (is_bun && (name[0] == '/' || strstr (name, "..")))
			snprintf (name, sizeof (name), "%04u.bin", i);
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
	return write_owned_entries (arg, basedir, depth, is_bun ? "PIKMIN-BUN" : "PIKMIN-ANM", entries, n);
}
