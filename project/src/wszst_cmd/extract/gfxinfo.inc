// Plain (uncompressed) Scaleform GFx / SWF movies (".gfx" / ".swf", magic "GFX" or "FWS", e.g. the UI movies of X-Men: Destiny):
//   u8[3] magic, u8 version, u32 LE total size, RECT frame size, u16 frame rate (8.8), u16 frame count, then SWF tags
//   {u16 (type << 6 | length), optional u32 length when the short length is 63, body} up to the End tag.
// The vector shapes, fonts and ActionScript are not decoded; "<file>.txt" lists the header, the top level tags by type and
// the names the movie exports or imports plus the Scaleform "DefineExternalImage2" (1009) records, which name the external
// DDS textures: {u16 id, u16 0, u16 format, u16 width, u16 height, u8 length + name, u8 length + file name}.
static ccp gfxinfo_tag (uint t)
{
	switch (t)
	{
		case 0: return "End";
		case 1: return "ShowFrame";
		case 2: return "DefineShape";
		case 9: return "SetBackgroundColor";
		case 12: return "DoAction";
		case 22: return "DefineShape2";
		case 26: return "PlaceObject2";
		case 28: return "RemoveObject2";
		case 32: return "DefineShape3";
		case 34: return "DefineButton2";
		case 37: return "DefineEditText";
		case 39: return "DefineSprite";
		case 43: return "FrameLabel";
		case 46: return "DefineMorphShape";
		case 56: return "ExportAssets";
		case 59: return "DoInitAction";
		case 60: return "DefineVideoStream";
		case 69: return "FileAttributes";
		case 70: return "PlaceObject3";
		case 71: return "ImportAssets2";
		case 74: return "CSMTextSettings";
		case 75: return "DefineFont3";
		case 77: return "Metadata";
		case 78: return "DefineScalingGrid";
		case 83: return "DefineShape4";
		case 1000: return "ExporterInfo";
		case 1009: return "DefineExternalImage2";
	}
	return "?";
}

static u32 gfxinfo_bits (const u8 *d, size_t sz, size_t pos, uint n, bool sgn)
{
	u32 v = 0;
	for (uint i = 0; i < n && (pos + i) / 8 < sz; i++)
		v = v << 1 | (d[(pos + i) / 8] >> (7 - (pos + i) % 8) & 1);
	if (sgn && n && n < 32 && (v >> (n - 1) & 1))
		v |= ~0u << n;
	return v;
}

static enumError extract_gfxinfo_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	(void)depth;
	if (!is_ext (arg, ".gfx") && !is_ext (arg, ".swf"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	enumError err = ERR_NOTHING_TO_DO;
	pcr_ctx_t cx;
	memset (&cx, 0, sizeof (cx));
	cx.ok = true;
	if (sz < 17 || (memcmp (raw, "GFX", 3) && memcmp (raw, "FWS", 3)) || rd_le32 (raw + 4) != sz)
		goto done;
	{
		const uint nb = raw[8] >> 3;
		const size_t rl = (5 + 4 * (size_t)nb + 7) / 8, tp = 8 + rl + 4;
		if (nb > 31 || tp > sz)
			goto done;
		const int x0 = (int)gfxinfo_bits (raw + 8, sz - 8, 5, nb, true), x1 = (int)gfxinfo_bits (raw + 8, sz - 8, 5 + nb, nb, true);
		const int y0 = (int)gfxinfo_bits (raw + 8, sz - 8, 5 + 2 * nb, nb, true), y1 = (int)gfxinfo_bits (raw + 8, sz - 8, 5 + 3 * nb, nb, true);
		pcr_emit (&cx, "%.3s version %u, %u bytes, frame %gx%g px, %g fps, %u frames\n\n", raw, raw[3], (uint)sz,
			(x1 - x0) / 20.0, (y1 - y0) / 20.0, rd_le16 (raw + 8 + rl) / 256.0, rd_le16 (raw + 10 + rl));
		static u32 cnt[2048], bytes[2048];
		memset (cnt, 0, sizeof (cnt));
		memset (bytes, 0, sizeof (bytes));
		static u8 seen[2048];
		memset (seen, 0, sizeof (seen));
		size_t p = tp;
		bool end = false;
		while (p + 2 <= sz)
		{
			const uint h = rd_le16 (raw + p), t = h >> 6;
			size_t l = h & 63;
			p += 2;
			if (l == 63)
			{
				if (p + 4 > sz)
					break;
				l = rd_le32 (raw + p);
				p += 4;
			}
			if (p + l > sz)
				break;
			const u8 *b = raw + p;
			if (t < 2048)
			{
				cnt[t]++;
				bytes[t] += l;
				seen[t] = 1;
			}
			if (t == 1009 && l > 12)
			{
				const uint nl = b[10];
				const uint fl = 11 + nl < l ? b[11 + nl] : 0;
				if (11 + nl + 1 + fl <= l)
					pcr_emit (&cx, "image %u: %ux%u format %u \"%.*s\" file \"%.*s\"\n", rd_le16 (b), rd_le16 (b + 6), rd_le16 (b + 8),
						rd_le16 (b + 4), (int)nl, (ccp)b + 11, (int)fl, (ccp)b + 12 + nl);
			}
			else if (t == 56 && l >= 2)
			{
				const uint n = rd_le16 (b);
				size_t q = 2;
				for (uint i = 0; i < n && q + 3 <= l; i++)
				{
					const u8 *z = memchr (b + q + 2, 0, l - q - 2);
					if (!z)
						break;
					pcr_emit (&cx, "export %u: \"%s\"\n", rd_le16 (b + q), (ccp)b + q + 2);
					q = (size_t)(z - b) + 1;
				}
			}
			else if (t == 71 && l > 3)
			{
				const u8 *z = memchr (b, 0, l);
				if (z)
				{
					const size_t q = (size_t)(z - b) + 1;
					pcr_emit (&cx, "import from \"%s\"\n", (ccp)b);
					if (q + 4 <= l)
					{
						const uint n = rd_le16 (b + q + 2);
						size_t r = q + 4;
						for (uint i = 0; i < n && r + 3 <= l; i++)
						{
							const u8 *z2 = memchr (b + r + 2, 0, l - r - 2);
							if (!z2)
								break;
							pcr_emit (&cx, "import %u: \"%s\"\n", rd_le16 (b + r), (ccp)b + r + 2);
							r = (size_t)(z2 - b) + 1;
						}
					}
				}
			}
			else if (t == 1000 && l > 10 && 10 + (size_t)b[9] <= l)
				pcr_emit (&cx, "exporter: \"%.*s\"\n", (int)b[9], (ccp)b + 10);
			p += l;
			if (t == 0)
			{
				end = true;
				break;
			}
		}
		if (!end || p != sz)
			goto done;
		pcr_emit (&cx, "\ntags (top level)\ntype\tname\tcount\tbytes\n");
		for (uint t = 0; t < 2048; t++)
			if (seen[t])
				pcr_emit (&cx, "%u\t%s\t%u\t%u\n", t, gfxinfo_tag (t), cnt[t], bytes[t]);
	}
	if (cx.ok && cx.len)
	{
		char dest[PATH_MAX];
		snprintf (dest, sizeof (dest), "%s.txt", arg);
		if (testmode)
		{
			fprintf (stdlog, "WOULD EXTRACT GFXINFO:%s -> TXT:%s\n", arg, dest);
			err = ERR_OK;
		}
		else
		{
			File_t F;
			err = CreateFileOpt (&F, true, dest, false, arg);
			if (F.f && fwrite (cx.out, 1, cx.len, F.f) != cx.len)
				err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", (uint)cx.len, dest);
			ResetFile (&F, opt_preserve);
			if (verbose >= 0 && !err)
				fprintf (stdlog, "%sEXTRACT GFXINFO:%s -> TXT:%s\n", verbose > 0 ? "\n" : "", arg, dest);
		}
	}
done:
	FREE (cx.out);
	FREE (raw);
	return err;
}
