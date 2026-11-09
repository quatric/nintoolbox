// Bj engine track data ".btd" (Maximum Racing, little endian PC layout): u32 magic 0xb060ffed, u32 section mask (0x7ff = 11
// sections), then 11 entries {u32 offset, u32 count, u32 -1} from +8 (offsets are file offsets; the game relocates them and
// byte swaps every field per section, see its section fix-up table). Section 3 is a single structure at its offset:
// +8 = number of points per line (n), +0x5c + 8*i = {u32 offset of 0x20-byte point records, u32 offset of 12-byte records}
// (offsets relative to the section) for i < 19 lines; a point record is {float x, y, z, 5 floats of direction/side data}.
// Written as "lines.obj" (one OBJ polyline per line, positions only) and "sections.json" (offset / count of every section plus the
// decoded sections 0 (11 float4 records), 4 / 7 (u32 tables), 5 (114 matrices), 6 (named 0x3c / 0x2c byte object records), 1 (0x20-byte records: two int vec3, index, float) and 2 (0x40-byte and 0x20-byte records)).
// JSON has no NaN / inf: non-finite floats (integer fields read as floats) are written as 0
static float njm_jf (const u8 *p)
{
	const float f = njm_f_le (p);
	return f == f && f - f == 0 ? f : 0;
}

static enumError extract_btd_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	if (!is_ext (arg, ".btd"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw || sz < 0x90 || sz > 0x10000000
		|| rd_le32 (raw) != 0xb060ffed || rd_le32 (raw + 4) != 0x7ff)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	njm_out_t j = { MALLOC (1 << 16), 0, 1 << 16 }, o = { MALLOC (1 << 16), 0, 1 << 16 };
	if (!j.b || !o.b)
	{
		FREE (j.b);
		FREE (o.b);
		FREE (raw);
		return ERR_OUT_OF_MEMORY;
	}
	njm_put (&j, "{\n\"sections\": [");
	for (uint i = 0; i < 11; i++)
		njm_put (&j, "%s\n {\"index\": %u, \"offset\": %u, \"count\": %u}", i ? "," : "", i, rd_le32 (raw + 8 + 12 * i),
			rd_le32 (raw + 12 + 12 * i));
	njm_put (&j, "\n],\n\"decoded\": {");
	{
		// layouts taken from the game's per-section fix-up functions
		const u32 b0 = rd_le32 (raw + 8), b1 = rd_le32 (raw + 8 + 12), b2 = rd_le32 (raw + 8 + 24);
		bool first = true;
		if (b0 && (u64)b0 + 0x20 <= sz)
		{
			const u8 *s0 = raw + b0;
			const u32 c = rd_le32 (s0), x = rd_le32 (s0 + 4), p8 = rd_le32 (s0 + 8);
			njm_put (&j, "\n\"section0\": {\"x\": %u, \"records\": [", x);
			for (uint i = 0; i < c && i < 4096 && (u64)b0 + p8 + 16 * (u64)(i + 1) <= sz; i++)
				njm_put (&j, "%s[%g,%g,%g,%g]", i ? "," : "", njm_jf (s0 + p8 + 16 * i), njm_jf (s0 + p8 + 16 * i + 4),
					njm_jf (s0 + p8 + 16 * i + 8), njm_jf (s0 + p8 + 16 * i + 12));
			njm_put (&j, "]}");
			first = false;
		}
		if (b1 && (u64)b1 + 8 <= sz)
		{
			const u8 *s1 = raw + b1;
			const u32 c = rd_le32 (s1), p = rd_le32 (s1 + 4);
			njm_put (&j, "%s\n\"section1\": [", first ? "" : ",");
			first = false;
			for (uint i = 0; i < c && i < 4096 && (u64)b1 + p + 0x20 * (u64)(i + 1) <= sz; i++)
			{
				const u8 *e = s1 + p + 0x20 * (size_t)i;
				njm_put (&j, "%s\n {\"a\": [%d,%d,%d], \"b\": [%d,%d,%d], \"index\": %u, \"value\": %g}", i ? "," : "",
					(int)rd_le32 (e), (int)rd_le32 (e + 4), (int)rd_le32 (e + 8), (int)rd_le32 (e + 12), (int)rd_le32 (e + 16),
					(int)rd_le32 (e + 20), rd_le32 (e + 24), njm_jf (e + 28));
			}
			njm_put (&j, "]");
		}
		if (b2 && (u64)b2 + 0x18 <= sz)
		{
			const u8 *s2 = raw + b2;
			const u32 na = rd_le32 (s2 + 4), nb = rd_le32 (s2 + 8), pa = rd_le32 (s2 + 0x10), pb = rd_le32 (s2 + 0x14);
			njm_put (&j, "%s\n\"section2\": {\"id\": %u, \"points\": [", first ? "" : ",", rd_le32 (s2));
			for (uint i = 0; i < na && i < 4096 && (u64)b2 + pa + 0x40 * (u64)(i + 1) <= sz; i++)
			{
				const u8 *e = s2 + pa + 0x40 * (size_t)i;
				njm_put (&j, "%s\n  {\"v0\": [%g,%g,%g,%g], \"v1\": [%g,%g,%g,%g], \"u32\": [", i ? "," : "", njm_jf (e),
					njm_jf (e + 4), njm_jf (e + 8), njm_jf (e + 12), njm_jf (e + 16), njm_jf (e + 20), njm_jf (e + 24),
					njm_jf (e + 28));
				for (uint k = 0; k < 7; k++)
					njm_put (&j, "%s%u", k ? "," : "", rd_le32 (e + 0x20 + 4 * k));
				njm_put (&j, "], \"u16\": [%u,%u]}", rd_le16 (e + 0x3c), rd_le16 (e + 0x3e));
			}
			njm_put (&j, "], \"records\": [");
			for (uint i = 0; i < nb && i < 4096 && (u64)b2 + pb + 0x20 * (u64)(i + 1) <= sz; i++)
			{
				const u8 *e = s2 + pb + 0x20 * (size_t)i;
				njm_put (&j, "%s\n  {\"v\": [%g,%g,%g,%g], \"u32\": [%u,%u]}", i ? "," : "", njm_jf (e), njm_jf (e + 4),
					njm_jf (e + 8), njm_jf (e + 12), rd_le32 (e + 16), rd_le32 (e + 20));
			}
			njm_put (&j, "]}");
		}
		// sections 4 and 7: plain u32 tables (15 / 19 words, many are floats), 5: 114 4x4 matrices + u16 pairs,
		// 6: {n0, n1, ptr, ptr} -> n0 * 0x3c + n1 * 0x2c byte records
		for (uint si = 4; si <= 7; si++)
		{
			const u32 b = rd_le32 (raw + 8 + 12 * si);
			if (!b || (u64)b + 0x40 > sz)
				continue;
			const u8 *t = raw + b;
			njm_put (&j, "%s\n\"section%u\": ", first ? "" : ",", si);
			first = false;
			if (si == 4 || si == 7)
			{
				const uint nw = si == 4 ? 15 : 19;
				njm_put (&j, "{\"u32\": [");
				for (uint k = 0; k < nw; k++)
					njm_put (&j, "%s%u", k ? "," : "", rd_le32 (t + 4 * k));
				njm_put (&j, "], \"float\": [");
				for (uint k = 0; k < nw; k++)
					njm_put (&j, "%s%g", k ? "," : "", njm_jf (t + 4 * k));
				njm_put (&j, "]}");
			}
			else if (si == 5 && (u64)b + 0x1c90 + 4 * 114 <= sz)
			{
				njm_put (&j, "{\"header\": %u, \"matrices\": [", rd_le32 (t));
				for (uint m = 0; m < 114; m++)
				{
					njm_put (&j, "%s\n [", m ? "," : "");
					for (uint k = 0; k < 16; k++)
						njm_put (&j, "%s%g", k ? "," : "", njm_jf (t + 0x10 + 0x40 * m + 4 * k));
					njm_put (&j, ", %u, %u]", rd_le16 (t + 0x1c90 + 4 * m), rd_le16 (t + 0x1c92 + 4 * m));
				}
				njm_put (&j, "]}");
			}
			else if (si == 6)
			{
				const u32 n0 = rd_le32 (t), n1 = rd_le32 (t + 4), pa = rd_le32 (t + 8), pb = rd_le32 (t + 12);
				njm_put (&j, "{\"a\": [");
				for (uint i = 0; i < n0 && i < 4096 && (u64)b + pa + 0x3c * (u64)(i + 1) <= sz; i++)
				{
					const u8 *e = t + pa + 0x3c * (size_t)i;
					char nm[33];
					memcpy (nm, e, 32);
					nm[32] = 0;
					for (char *c = nm; *c; c++)
						if (*c == '"' || *c == '\\' || (u8)*c < 32 || (u8)*c > 126)
							*c = '?';
					njm_put (&j, "%s\n {\"name\": \"%s\", \"u32\": [", i ? "," : "", nm);
					for (uint k = 8; k < 15; k++)
						njm_put (&j, "%s%u", k > 8 ? "," : "", rd_le32 (e + 4 * k));
					njm_put (&j, "]}");
				}
				njm_put (&j, "], \"b\": [");
				for (uint i = 0; i < n1 && i < 4096 && (u64)b + pb + 0x2c * (u64)(i + 1) <= sz; i++)
				{
					const u8 *e = t + pb + 0x2c * (size_t)i;
					char nm[33];
					memcpy (nm, e, 32);
					nm[32] = 0;
					for (char *c = nm; *c; c++)
						if (*c == '"' || *c == '\\' || (u8)*c < 32 || (u8)*c > 126)
							*c = '?';
					njm_put (&j, "%s\n {\"name\": \"%s\", \"u32\": [", i ? "," : "", nm);
					for (uint k = 8; k < 11; k++)
						njm_put (&j, "%s%u", k > 8 ? "," : "", rd_le32 (e + 4 * k));
					njm_put (&j, "]}");
				}
				njm_put (&j, "]}");
			}
			else
				njm_put (&j, "null");
		}
	}
	njm_put (&j, "\n}\n}\n");
	uint nl = 0;
	const u32 s3 = rd_le32 (raw + 8 + 12 * 3);
	if (s3 && (u64)s3 + 0x100 <= sz)
	{
		const u32 n = rd_le32 (raw + s3 + 8);
		if (n && n < 100000)
			for (uint i = 0; i < 19; i++)
			{
				const u32 po = rd_le32 (raw + s3 + 0x5c + 8 * i);
				if ((u64)s3 + po + 0x20 * (u64)n > sz)
					break;
				njm_put (&o, "o line%u\n", i);
				for (uint k = 0; k < n; k++)
				{
					const u8 *p = raw + s3 + po + 0x20 * (size_t)k;
					njm_put (&o, "v %g %g %g\n", njm_jf (p), njm_jf (p + 4), njm_jf (p + 8));
				}
				njm_put (&o, "l");
				for (uint k = 0; k < n; k++)
					njm_put (&o, " %d", (int)k - (int)n);
				njm_put (&o, "\n");
				nl++;
			}
	}
	enumError err = ERR_OK;
	char dest[PATH_MAX];
	beside_source_dest (dest, sizeof (dest), arg);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT BJ-BTD:%s (%u lines) -> %s/lines.obj\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, nl, dest);
	if (!testmode)
	{
		err = shade_write (arg, dest, basedir, "sections.json", (u8 *)j.b, (uint)j.used);
		if (!err && nl && o.b)
			err = shade_write (arg, dest, basedir, "lines.obj", (u8 *)o.b, (uint)o.used);
	}
	FREE (j.b);
	FREE (o.b);
	FREE (raw);
	return err;
}
