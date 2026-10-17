// Capcom MT Framework Wii parameter tables (Sengoku Basara 3), magics stored as little-endian fourccs in a big-endian
// file: ".thk" = "\0KHT" {u16 version 0x1415, .., u32 0x10 header size, at 0x10 a table of 16-byte entries {u8 item count, 3
// pad, u32 BE offset, 8 zero bytes}; every group holds <count> items of 176 bytes}, ".atk" = "\0KTA" {u16 version 0x1021,
// u16 record count, records starting with a 16 byte name such as "NONAME"} and ".col" = "\0LOC" {u16 version, u16
// count}. The fields are not documented, so every file is written as "<name>.<ext>.txt" with its header values and all
// remaining words as hex (plus the float reading where it is plausible).
static void mtparam_words (FILE *f, const u8 *d, size_t sz, size_t from, size_t to, ccp indent)
{
	for (size_t o = from; o + 4 <= to && o + 4 <= sz; o += 16)
	{
		fprintf (f, "%s%05zx:", indent, o);
		char fl[160] = "";
		for (size_t k = 0; k < 16 && o + k + 4 <= to && o + k + 4 <= sz; k += 4)
		{
			const u32 v = rd_be32 (d + o + k);
			fprintf (f, " %08x", v);
			union
			{
				u32 u;
				float f;
			} x = {v};
			const float a = x.f < 0 ? -x.f : x.f;
			if (a > 1e-4f && a < 1e7f && x.f == x.f)
				snprintf (fl + strlen (fl), sizeof (fl) - strlen (fl), " [%zx]=%g", o + k, (double)x.f);
		}
		fprintf (f, "%s\n", fl);
	}
}

static enumError extract_mtparam_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	(void)depth;
	const bool thk = is_ext (arg, ".thk"), atk = is_ext (arg, ".atk"), col = is_ext (arg, ".col");
	if (!thk && !atk && !col)
		return ERR_NOTHING_TO_DO;
	u8 *d = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &d, &sz, 0, 0, 0, false) || !d)
	{
		FREE (d);
		return ERR_NOTHING_TO_DO;
	}
	if (sz < 0x20 || memcmp (d, thk ? "\0KHT" : atk ? "\0KTA" : "\0LOC", 4))
	{
		FREE (d);
		return ERR_NOTHING_TO_DO;
	}
	char dest[PATH_MAX];
	snprintf (dest, sizeof (dest), "%s.txt", arg);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT MT-PARAM:%s -> %s\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, dest);
	enumError err = ERR_OK;
	if (!testmode)
	{
		File_t F;
		err = CreateFileOpt (&F, true, dest, false, arg);
		if (F.f)
		{
			FILE *f = F.f;
			fprintf (f, "# MT Framework %s table, %zu bytes, version 0x%04x\n", thk ? "THK" : atk ? "ATK" : "COL", sz,
				rd_be16 (d + 4));
			if (thk)
			{
				uint ngroups = 0;
				for (size_t o = 0x10; o + 16 <= sz; o += 16)
				{
					const u32 off = rd_be32 (d + o + 4);
					if (!d[o] && !off)
						break;
					if (off < 0x10 || off >= sz || (o > 0x10 && off <= rd_be32 (d + o - 12)))
						break;
					ngroups++;
				}
				fprintf (f, "groups %u\n", ngroups);
				for (uint g = 0; g < ngroups; g++)
				{
					const u8 *e = d + 0x10 + 16 * (size_t)g;
					const size_t off = rd_be32 (e + 4), n = e[0];
					fprintf (f, "\ngroup %u: %zu items at 0x%zx\n", g, n, off);
					for (size_t i = 0; i < n && off + 176 * (i + 1) <= sz; i++)
					{
						fprintf (f, "  item %zu\n", i);
						mtparam_words (f, d, sz, off + 176 * i, off + 176 * (i + 1), "    ");
					}
				}
			}
			else
			{
				fprintf (f, "records %u\n", (uint)rd_be16 (d + 6));
				mtparam_words (f, d, sz, 8, sz, "  ");
			}
		}
		const enumError ce = ResetFile (&F, opt_preserve);
		err = err ? err : ce;
	}
	FREE (d);
	return err;
}
