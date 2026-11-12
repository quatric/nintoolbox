// Octomania "secB" scene / demo container (.sec): header {"secB", u32 1, u32 data start (0x800), u32 table size, u32 alignment, ...} + a table of
// records and a name pool; the payload is a run of standard Nintendo NW4R files (RSAR / RSTM / RWSD / RBNK / RSEQ sound files, "bres" resources,
// RLYT / RLAN / RFNT / RFNA layout files / "REFF" ...) placed on 0x800 byte boundaries after the table, either stored as is (extracted
// here) or packed with a token stream (token b < 0x80 = b literal bytes, b >= 0x80 = (b & 0x7f) + 1 copies of the next byte matches the NW4R header
// bytes, but the sheet data of packed RFNTs decodes ~100 KB short, so packed members are skipped for now). Plain members are written as "NNN.<ext>" (.brsar .brstm .brwsd
// .brbnk .brseq .brres .brlyt .brlan .brfnt .brfna .breff ...) in "<file>.d".
static const struct { char magic[5]; char ext[8]; } octosec_kinds[] = {
	{ "RSAR", "brsar" }, { "RSTM", "brstm" }, { "RWSD", "brwsd" }, { "RBNK", "brbnk" }, { "RSEQ", "brseq" }, { "RWAR", "brwar" },
	{ "RSTP", "brstp" }, { "bres", "brres" }, { "RLYT", "brlyt" }, { "RLAN", "brlan" }, { "RFNT", "brfnt" }, { "RFNA", "brfna" },
	{ "RLPA", "brlpa" }, { "RLTS", "brlts" }, { "RLMC", "brlmc" }, { "RLCN", "brlcn" }, { "RLVI", "brlvi" }, { "RLVC", "brlvc" },
	{ "RLTP", "brltp" }, { "RLSP", "brlsp" }, { "RLTM", "brltm" }, { "REFF", "breff" }, { "REFT", "breft" }, { "RMDL", "brmdl" }
};

static int octosec_kind (const u8 *p)
{
	if (rd_be16 (p + 4) != 0xfeff)
		return -1;
	for (uint i = 0; i < sizeof (octosec_kinds) / sizeof (octosec_kinds[0]); i++)
		if (!memcmp (p, octosec_kinds[i].magic, 4))
			return (int)i;
	return -1;
}

static enumError extract_octosec_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
		return ERR_NOTHING_TO_DO;
	if (sz < 0x1000 || memcmp (raw, "secB", 4) || rd_be32 (raw + 4) != 1)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	const u32 start = rd_be32 (raw + 8), step = 0x800;
	if (!start || (start % step) || start >= sz)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	char dest[PATH_MAX];
	beside_source_dest (dest, sizeof (dest), arg);
	enumError err = ERR_OK;
	for (int pass = 0; pass < 2 && !err; pass++)
	{
		size_t o = start;
		uint k = 0;
		while (o + 16 <= sz)
		{
			const u8 *data = raw + o;
			u8 *tmp = 0;
			size_t len = 0, adv = 0;
			int kind = octosec_kind (data);
			if (kind >= 0 && rd_be32 (data + 8) >= 16 && o + (size_t)rd_be32 (data + 8) <= sz)
				len = rd_be32 (data + 8), adv = len;
			else
			{
				// packed members (token stream, see above) are not decoded yet: only the header region is understood
				kind = -1;
			}
			if (kind < 0)
			{
				FREE (tmp);
				o += step;
				continue;
			}
			if (pass && !testmode)
			{
				char name[40];
				snprintf (name, sizeof (name), "%03u.%s", k, octosec_kinds[kind].ext);
				err = shade_write (arg, dest, basedir, name, data, len);
			}
			FREE (tmp);
			if (err)
				break;
			k++;
			o = (o + adv + step - 1) / step * step;
		}
		if (!pass && !k)
		{
			err = ERR_NOTHING_TO_DO;
			break;
		}
		if (!pass && (verbose >= 0 || testmode))
			fprintf (stdlog, "%s%sEXTRACT SEC:%s (%u files) -> %s/\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, k, dest);
		if (testmode)
			break;
	}
	FREE (raw);
	return err;
}
