// Petz "displacement track" files ("DPGZ", Petz Sports, Wii; 3d/displacementtrack/*, no extension): big-endian {u32 magic,
// u32 header word count (15 or 30), floats up to 0x40, u32 frame count @0x40, f32 length @0x44}, then sections {u32 n,
// n x (3 f32)} twice (two vec3 channels, one key per frame) and optionally a third {u32 n, n x f32}, and a trailing u32 0.
// The first channel is the position path and the second a (mostly constant) scale, matching the start position and scale in
// the header; the third scalar channel is not identified. The file is written as "<name>.txt": header floats and one row
// per key with all channels.
static float rd_bef32 (const u8 *p)
{
	const u32 u = rd_be32 (p);
	float f;
	memcpy (&f, &u, 4);
	return f;
}

static enumError extract_dpgz_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	(void)depth;
	if (!petz_has_magic (arg, "DPGZ"))
		return ERR_NOTHING_TO_DO;
	u8 *d = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &d, &sz, 0, 0, 0, false) || !d)
	{
		FREE (d);
		return ERR_NOTHING_TO_DO;
	}
	static const int layouts[3][4] = {{3, 3, 0, 0}, {3, 3, 1, 0}, {0}};
	u32 cnt[3] = {0};
	int nsec = 0;
	for (int l = 0; l < 2 && !nsec && sz >= 0x50; l++)
	{
		size_t p = 0x48;
		u32 c[3] = {0};
		int k = 0;
		for (; k < 3 && layouts[l][k]; k++)
		{
			if (p + 4 > sz)
				break;
			c[k] = rd_be32 (d + p);
			if (c[k] > 100000 || p + 4 + 4 * (size_t)layouts[l][k] * c[k] > sz)
				break;
			p += 4 + 4 * (size_t)layouts[l][k] * c[k];
		}
		if (k == (l ? 3 : 2) && (p == sz || p + 4 == sz))
		{
			nsec = k;
			memcpy (cnt, c, sizeof (cnt));
		}
	}
	if (!nsec)
	{
		FREE (d);
		return ERR_NOTHING_TO_DO;
	}
	char dest[PATH_MAX];
	snprintf (dest, sizeof (dest), "%s.txt", arg);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT PETZ-DPGZ:%s (%u keys, %d channels) -> %s\n", verbose > 0 ? "\n" : "",
			testmode ? "WOULD " : "", arg, cnt[0], nsec, dest);
	enumError err = ERR_OK;
	if (!testmode)
	{
		File_t F;
		err = CreateFileOpt (&F, true, dest, false, arg);
		if (F.f)
		{
			FILE *f = F.f;
			const uint hw = rd_be32 (d + 4);
			fprintf (f, "# Petz displacement track, header words %u, frames %u, length %g\n# header:", hw, rd_be32 (d + 0x40),
				(double)rd_bef32 (d + 0x44));
			for (size_t o = 8; o < 0x40; o += 4)
				fprintf (f, " %g", (double)rd_bef32 (d + o));
			fprintf (f, "\n# key position scale%s\n", nsec > 2 ? " scalar" : "");
			size_t p[3];
			p[0] = 0x4c;
			p[1] = p[0] + 12 * (size_t)cnt[0] + 4;
			p[2] = p[1] + 12 * (size_t)cnt[1] + 4;
			for (u32 i = 0; i < cnt[0]; i++)
			{
				fprintf (f, "%u", i);
				for (int k = 0; k < nsec; k++)
				{
					const uint w = k < 2 ? 3 : 1;
					if (i >= cnt[k])
					{
						fprintf (f, "  -");
						continue;
					}
					fputc (' ', f);
					for (uint c = 0; c < w; c++)
						fprintf (f, " %g", (double)rd_bef32 (d + p[k] + 4 * ((size_t)i * w + c)));
				}
				fputc ('\n', f);
			}
		}
		const enumError ce = ResetFile (&F, opt_preserve);
		err = err ? err : ce;
	}
	FREE (d);
	return err;
}
