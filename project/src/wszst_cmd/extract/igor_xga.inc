// Igor: The Game skeletal animations (".xga", Exient "chkf" container of type "xga"; big endian) with "XgmDecompAnimChannel" channels. Layout as in igor_ggm.inc ({"chkf", u32 N}, N byte string
// table, then header words; the data size is at +0x1c of the header, the key count K at +0x20): {u32 channel count C at +0x24, u32 C again, C x {u32 0x14 (class name offset), u32 0x29 (empty
// string), u32 name offset (the skeleton node animated), u32 kind offset ("Joint"), u32 byte offset of the channel's keys}} followed by a table that is not decoded. The keys sit in the
// data block at the end of the file (size = C x K x 32): per channel K keys of {f32 quaternion x, y, z, w, f32 translation x, y, z, f32 scale (about 1)}, channel i starting at i x K x 32.
// Key times are not stored at all (the K keys are evenly spaced; written as frame indices). After the channel records comes {u32 0, u32 M, M x {u32 name offset, 8 floats}}: the rest pose of the M skeleton nodes, written as "rest". Other channel classes ("XgmMatrixAnimChannel", the 5 files with an empty first name) are left alone. Written as
// "<file>.json" (455 of 474 files).
static enumError extract_igor_xga_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	if (!is_ext (arg, ".xga") || (export_count == 0 && !opt_decode))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw || sz < 0x40 || memcmp (raw, "chkf", 4) || memcmp (raw + 8, "xga", 4))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	const u32 sl = rd_be32 (raw + 4);
	const size_t hb = 8 + (size_t)sl;
	if (sl < 16 || hb + 0x2c > sz)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	const u8 *st = raw + 8;
	// "XgmMatrixAnimChannel" (texture transform matrices): {K, C, 0, C} instead of {K, C, C}, 16 floats (a 4x4 matrix) per key
	const bool mx = sl > 0x29 && !memcmp (st + 0x14, "XgmMatrixAnimChannel", 21);
	const size_t rec0 = hb + (mx ? 0x30 : 0x2c);
	const uint nf = mx ? 16 : 8, kb = 4 * nf;
	if (hb + 0x34 > sz)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	const u32 dsz = rd_be32 (raw + hb + 0x1c), kk = rd_be32 (raw + hb + 0x20), nc = rd_be32 (raw + hb + 0x24);
	// mixed files: {K, C = D + M, D}, D skeleton channels {0x14, 0, name, kind, off}, then {M, M x {class, owner, target, kind, off}} texture transform channels; blob = D x K x 32 + M x K x 64
	if (!mx && kk && nc > 1 && nc <= 4096 && rd_be32 (raw + hb + 0x28) < nc && rd_be32 (raw + hb + 0x28) && dsz <= sz - hb)
	{
		const u32 nd = rd_be32 (raw + hb + 0x28);
		const size_t s1 = hb + 0x2c + 20 * (size_t)nd;
		const u32 nm = s1 + 4 <= sz ? rd_be32 (raw + s1) : 0;
		const size_t mb = sz - dsz;
		bool good = nm && nm == nc - nd && (u64)kk * (32ull * nd + 64ull * nm) == dsz && s1 + 4 + 20ull * nm <= mb;
		for (u32 i = 0; i < nd && good; i++)
		{
			const u8 *r = raw + hb + 0x2c + 20 * (size_t)i;
			good = rd_be32 (r + 8) < sl && rd_be32 (r + 12) < sl && rd_be32 (r + 16) == (u64)i * kk * 32 && !memcmp (st + rd_be32 (r), "XgmDecompAnimChannel", 21);
		}
		for (u32 i = 0; i < nm && good; i++)
		{
			const u8 *r = raw + s1 + 4 + 20 * (size_t)i;
			good = rd_be32 (r + 4) < sl && rd_be32 (r + 8) < sl && rd_be32 (r + 12) < sl && rd_be32 (r + 16) == 32ull * nd * kk + (u64)i * kk * 64;
		}
		if (good)
		{
			sumo_buf_t m2 = { 0, 0, 0, true };
			sumo_printf (&m2, "{\n\t\"format\": \"Igor XGA\",\n\t\"keys\": %u,\n\t\"channels\": [", kk);
			for (u32 i = 0; i < nc; i++)
			{
				const bool isj = i < nd;
				const u8 *r = isj ? raw + hb + 0x2c + 20 * (size_t)i : raw + s1 + 4 + 20 * (size_t)(i - nd);
				const uint nfl = isj ? 8 : 16;
				const size_t boff = mb + rd_be32 (r + 16);
				sumo_printf (&m2, "%s\n\t\t{\"name\": \"%s\", \"owner\": \"%s\", \"kind\": \"%s\", \"keys\": [", i ? "," : "", (ccp)st + rd_be32 (r + 8), (ccp)st + rd_be32 (r + 4), (ccp)st + rd_be32 (r + 12));
				for (u32 k = 0; k < kk; k++)
				{
					sumo_printf (&m2, "%s[", k ? ", " : "");
					for (uint v = 0; v < nfl; v++)
					{
						const u32 u = rd_be32 (raw + boff + 4 * nfl * (size_t)k + 4 * v);
						float f;
						memcpy (&f, &u, 4);
						sumo_printf (&m2, "%s%.9g", v ? ", " : "", (double)f);
					}
					sumo_printf (&m2, "]");
				}
				sumo_printf (&m2, "]}");
			}
			sumo_printf (&m2, "\n\t]\n}\n");
			FREE (raw);
			enumError err2 = ERR_NOTHING_TO_DO;
			if (m2.ok && m2.n < 0xffffffffu)
			{
				char dest[PATH_MAX], name[PATH_MAX];
				beside_source_dest (dest, sizeof (dest), arg);
				const char *sl3 = strrchr (arg, '/');
				snprintf (name, sizeof (name), "%s", sl3 ? sl3 + 1 : arg);
				char *dot = strrchr (name, '.');
				if (dot)
					strcpy (dot, ".json");
				else
					strcat (name, ".json");
				if (verbose >= 0 || testmode)
					fprintf (stdlog, "%s%sEXTRACT IGOR-XGA:%s (%u channels, %u keys) -> %s/%s\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, nc, kk, dest, name);
				err2 = shade_write (arg, dest, basedir, name, (u8 *)m2.p, (uint)m2.n);
			}
			FREE (m2.p);
			return err2;
		}
	}
	bool ok = kk && kk <= 100000 && nc && nc <= 4096 && rd_be32 (raw + hb + (mx ? 0x2c : 0x28)) == nc && (!mx || !rd_be32 (raw + hb + 0x28)) && dsz <= sz - hb && (u64)nc * kk * kb == dsz
		&& rec0 + 20ull * nc <= sz - dsz;
	const size_t base = sz - dsz;
	for (u32 i = 0; ok && i < nc; i++)
	{
		const u8 *r = raw + rec0 + 20 * (size_t)i;
		ok = rd_be32 (r) < sl && rd_be32 (r + 8) < sl && rd_be32 (r + 12) < sl && rd_be32 (r + 16) == (u64)i * kk * kb && !memcmp (st + rd_be32 (r), mx ? "XgmMatrixAnimChannel" : "XgmDecompAnimChannel", 21);
	}
	if (!ok)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	sumo_buf_t b = { 0, 0, 0, true };
	sumo_printf (&b, "{\n\t\"format\": \"Igor XGA\",\n\t\"keys\": %u,\n\t\"channels\": [", kk);
	for (u32 i = 0; i < nc; i++)
	{
		const u8 *r = raw + rec0 + 20 * (size_t)i;
		sumo_printf (&b, "%s\n\t\t{\"name\": \"%s\", \"owner\": \"%s\", \"kind\": \"%s\", \"keys\": [", i ? "," : "", (ccp)st + rd_be32 (r + 8), (ccp)st + rd_be32 (r + 4), (ccp)st + rd_be32 (r + 12));
		for (u32 k = 0; k < kk; k++)
		{
			const u8 *p = raw + base + (size_t)i * kk * kb + kb * (size_t)k;
			sumo_printf (&b, "%s[", k ? ", " : "");
			for (uint v = 0; v < nf; v++)
			{
				const u32 u = rd_be32 (p + 4 * v);
				float f;
				memcpy (&f, &u, 4);
				sumo_printf (&b, "%s%.9g", v ? ", " : "", (double)f);
			}
			sumo_printf (&b, "]");
		}
		sumo_printf (&b, "]}");
	}
	sumo_printf (&b, "\n\t]");
	{
		// rest pose: after the channel records {u32 0, u32 M, M x {u32 name offset, 8 floats as in a key}} (the key times are not stored, the K keys are evenly spaced)
		const size_t s0 = rec0 + 20 * (size_t)nc;
		const u32 m = !mx && s0 + 8 <= base ? rd_be32 (raw + s0 + 4) : 0;
		if (m && m <= 4096 && s0 + 8 + 36ull * m == base)
		{
			bool good = true;
			for (u32 i = 0; i < m && good; i++)
				good = rd_be32 (raw + s0 + 8 + 36 * (size_t)i) < sl;
			if (good)
			{
				sumo_printf (&b, ",\n\t\"rest\": [");
				for (u32 i = 0; i < m; i++)
				{
					const u8 *r = raw + s0 + 8 + 36 * (size_t)i;
					float f[8];
					for (uint v = 0; v < 8; v++)
					{
						const u32 u = rd_be32 (r + 4 + 4 * v);
						memcpy (&f[v], &u, 4);
					}
					sumo_printf (&b, "%s\n\t\t{\"name\": \"%s\", \"pose\": [%.9g, %.9g, %.9g, %.9g, %.9g, %.9g, %.9g, %.9g]}", i ? "," : "", (ccp)st + rd_be32 (r), (double)f[0], (double)f[1],
						(double)f[2], (double)f[3], (double)f[4], (double)f[5], (double)f[6], (double)f[7]);
				}
				sumo_printf (&b, "\n\t]");
			}
		}
	}
	sumo_printf (&b, "\n}\n");
	FREE (raw);
	enumError err = ERR_NOTHING_TO_DO;
	if (b.ok && b.n && b.n < 0xffffffffu)
	{
		char dest[PATH_MAX], name[PATH_MAX];
		beside_source_dest (dest, sizeof (dest), arg);
		const char *sl2 = strrchr (arg, '/');
		snprintf (name, sizeof (name), "%s", sl2 ? sl2 + 1 : arg);
		char *dot = strrchr (name, '.');
		if (dot)
			strcpy (dot, ".json");
		else
			strcat (name, ".json");
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT IGOR-XGA:%s (%u channels, %u keys) -> %s/%s\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, nc, kk, dest, name);
		err = shade_write (arg, dest, basedir, name, (u8 *)b.p, (uint)b.n);
	}
	FREE (b.p);
	return err;
}
