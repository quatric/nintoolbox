// "BTA0" (".nsbta", texture SRT animation, block "SRT0") and "BMA0" (".nsbma", material color animation, block "MAT0") files of the NITRO-System (NNSG3d) of about 430 / 300 DS games, little-endian; files that start with a
// Nintendo LZ10 header are unpacked first. {char magic[4], u16 0xfeff, u16 version, u32 size, u16 0x10, u16 1, u32 block offset}; the block is {char tag[4], u32 size, info block of animations}. An info block is
// {u8 0, u8 count, u16 size, u16 8, u16 sub size, u32 0x17f, count * u32, u16 datum size, u16 data size, count * datum, count * char name[16]}. Each animation datum is a u32 offset (from the block) of {char "M\0AT", u16 frames,
// u16, info block of materials}. SRT0 materials are 5 * {u16 count, u8 0, u8 flags, u32 value} for scale S/T, rotation, translation S/T: flags 0x30 constant (value is s32 20.12), 0x20 constant rotation {s16 sin, s16 cos} (4.12),
// 0x10 per frame s16 samples (4.12, like the constants) at (animation + value). MAT0 materials are 5 * {u16 value, u16 {flags << 12 | frame count}} for diffuse, ambient, specular, emission, polygon alpha: flags 2 constant (RGB555 or alpha),
// flags 0 per frame data at (animation + value), u16 colors or u8 alpha. Other flag values (0x80/0x90 and 0x00 in SRT0, 4 and 8 in MAT0, which carry fewer keys than frames) are reported as "unsupported" with their raw fields.
// The 4.12 scaling of the s16 samples is inferred (a sample sequence starts at exactly -4096 = -1.0, and apicula's 1.10.5 reading gives implausible scroll speeds). Written as "<file>.json".
static bool nsbanim_info (const u8 *r, size_t sz, size_t p, uint dsz, uint *n, size_t *data, size_t *names)
{
	if (p + 12 > sz || r[p])
		return false;
	*n = r[p + 1];
	const size_t q = p + 12 + 4 * (size_t)*n;
	if (!*n || q + 4 > sz || rd_le16 (r + q) != dsz)
		return false;
	*data = q + 4;
	*names = q + 4 + (size_t)*n * dsz;
	return *names + 16 * (size_t)*n <= sz;
}

static void nsbanim_name (sumo_buf_t *b, const u8 *s)
{
	char t[17];
	for (uint i = 0; i < 16; i++)
		t[i] = s[i] >= 0x20 && s[i] < 0x7f && s[i] != '"' && s[i] != '\\' ? (char)s[i] : s[i] ? '_' : 0;
	t[16] = 0;
	sumo_printf (b, "\"%s\"", t);
}

static enumError extract_nsbanim_file (ccp arg, ccp basedir, uint depth)
{
	u8 *raw = 0, *lz = 0;
	size_t sz = 0;
	enumError err = ERR_NOTHING_TO_DO;
	sumo_buf_t b = { 0, 0, 0, true };
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
		return ERR_NOTHING_TO_DO;
	if (sz > 4 && raw[0] == 0x10)
	{
		size_t used, n;
		if (packlz_lz10 (raw, sz, &used, &lz, &n) && n > 0x20 && (!memcmp (lz, "BTA0", 4) || !memcmp (lz, "BMA0", 4)))
		{
			FREE (raw);
			raw = lz;
			lz = 0;
			sz = n;
		}
	}
	const bool ta = sz > 0x20 && !memcmp (raw, "BTA0", 4);
	if (!ta && !(sz > 0x20 && !memcmp (raw, "BMA0", 4)))
		goto done;
	const size_t blk = rd_le32 (raw + 0x10);
	uint na;
	size_t ad, an;
	if (blk + 8 > sz || memcmp (raw + blk, ta ? "SRT0" : "MAT0", 4) || !nsbanim_info (raw, sz, blk + 8, 4, &na, &ad, &an))
		goto done;
	sumo_printf (&b, "{\n\t\"format\": \"%s\",\n\t\"animations\": [", ta ? "NSBTA (BTA0/SRT0)" : "NSBMA (BMA0/MAT0)");
	static const char *const tn[5] = { "scaleS", "scaleT", "rotation", "transS", "transT" };
	static const char *const mn[5] = { "diffuse", "ambient", "specular", "emission", "alpha" };
	for (uint i = 0; i < na; i++)
	{
		const size_t a = blk + rd_le32 (raw + ad + 4 * i);
		uint nm;
		size_t md, mnm;
		if (a + 12 > sz || !nsbanim_info (raw, sz, a + 8, ta ? 40 : 20, &nm, &md, &mnm))
			goto fail;
		const uint nf = rd_le16 (raw + a + 4);
		sumo_printf (&b, "%s\n\t\t{\"name\": ", i ? "," : "");
		nsbanim_name (&b, raw + an + 16 * i);
		sumo_printf (&b, ", \"frames\": %u, \"materials\": [", nf);
		for (uint m = 0; m < nm; m++)
		{
			const u8 *d = raw + md + (size_t)m * (ta ? 40 : 20);
			sumo_printf (&b, "%s\n\t\t\t{\"name\": ", m ? "," : "");
			nsbanim_name (&b, raw + mnm + 16 * m);
			sumo_printf (&b, ", \"channels\": {");
			for (uint c = 0; c < 5; c++)
			{
				sumo_printf (&b, "%s\"%s\": ", c ? ", " : "", ta ? tn[c] : mn[c]);
				if (ta)
				{
					const uint cnt = rd_le16 (d + 8 * c), fl = d[8 * c + 3];
					const u32 v = rd_le32 (d + 8 * c + 4);
					if (fl == 0x30)
						sumo_printf (&b, "{\"mode\": \"constant\", \"value\": %.6g}", (double)(int32_t)v / 4096.0);
					else if (fl == 0x20 && c == 2)
						sumo_printf (&b, "{\"mode\": \"constant\", \"sin\": %.6g, \"cos\": %.6g}", (double)(int16_t)(v & 0xffff) / 4096.0, (double)(int16_t)(v >> 16) / 4096.0);
					else if (fl == 0x10 && a + v + 2ull * cnt <= sz)
					{
						sumo_printf (&b, "{\"mode\": \"samples\", \"values\": [");
						for (uint k = 0; k < cnt; k++)
							sumo_printf (&b, "%s%.6g", k ? "," : "", (double)(int16_t)rd_le16 (raw + a + v + 2 * k) / 4096.0);
						sumo_printf (&b, "]}");
					}
					else
						sumo_printf (&b, "{\"mode\": \"unsupported\", \"flags\": %u, \"count\": %u, \"value\": %u}", fl, cnt, v);
				}
				else
				{
					const uint v = rd_le16 (d + 4 * c), h = rd_le16 (d + 4 * c + 2), fl = h >> 12, cnt = h & 0xfff, es = c == 4 ? 1 : 2;
					if (fl == 2)
					{
						if (c == 4)
							sumo_printf (&b, "{\"mode\": \"constant\", \"value\": %u}", v);
						else
							sumo_printf (&b, "{\"mode\": \"constant\", \"rgb555\": %u, \"rgb\": [%u, %u, %u]}", v & 0x7fff, v & 31, v >> 5 & 31, v >> 10 & 31);
					}
					else if (fl == 0 && a + v + (size_t)es * cnt <= sz)
					{
						sumo_printf (&b, "{\"mode\": \"samples\", \"values\": [");
						for (uint k = 0; k < cnt; k++)
							sumo_printf (&b, "%s%u", k ? "," : "", es == 1 ? raw[a + v + k] : rd_le16 (raw + a + v + 2 * k));
						sumo_printf (&b, "]}");
					}
					else
						sumo_printf (&b, "{\"mode\": \"unsupported\", \"flags\": %u, \"count\": %u, \"value\": %u}", fl, cnt, v);
				}
			}
			sumo_printf (&b, "}}");
		}
		sumo_printf (&b, "]}");
	}
	sumo_printf (&b, "\n\t]\n}\n");
	if (b.ok && b.n && b.n < 0xffffffffu)
	{
		char dest[PATH_MAX], name[PATH_MAX];
		beside_source_dest (dest, sizeof (dest), arg);
		const char *sl = strrchr (arg, '/');
		snprintf (name, sizeof (name), "%s", sl ? sl + 1 : arg);
		char *dot = strrchr (name, '.');
		if (dot)
			*dot = 0;
		strncat (name, ".json", sizeof (name) - strlen (name) - 1);
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT %s:%s (%u animations) -> %s/%s\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", ta ? "NSBTA" : "NSBMA", arg, na, dest, name);
		err = shade_write (arg, dest, basedir, name, (u8 *)b.p, (uint)b.n);
	}
fail:
done:
	FREE (b.p);
	FREE (lz);
	FREE (raw);
	return err;
}
