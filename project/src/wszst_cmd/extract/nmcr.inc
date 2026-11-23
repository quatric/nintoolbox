// "RCMN" (".nmcr" multi-cell bank) and "RAMN" (".nmar" animation bank), the NITRO MultiCell resources (about 800 DS files, NCER/NANR-like): "RCMN"/"RAMN", 0xfeff, u16 version, u32 file size, u16 header size 0x10, u16 block count, one block ("KBCM"/"KNBA") of {tag, u32 size, ...}.
// KBCM at 0x10: u16 cells at +8, u16 0xbeef, u32 cell table offset (from 0x18), u32 OAM data offset (from 0x18); cell record {u16 oam count, u16 oam count, u32 byte offset into the OAM data = 8 * OAMs before it}; OAM {u16 a, s16 b, s16 c, u16 d}, written raw (the meaning of the fields is not known; b and c look like x/y offsets, d steps through tiles).
// KNBA at 0x10: u16 animations, u16 frames, u32 animation table offset, u32 frame table offset, u32 data offset (from 0x18); animation record {u32 frames, u16, u16, u32, u32 byte offset into the frame table = 8 * frames before it}; frame {u32 offset of the pose data (= the key index into the data block), u16 duration (frames), u16 0xbeef}, written as [offset, duration]. The layout was checked against all 4+ samples that carry the magic (sums and offsets are consistent); the field meanings are not decoded, so the result is a JSON structure dump.
static enumError extract_nmcr_file (ccp arg, ccp basedir, uint depth)
{
	u8 *raw = 0;
	size_t sz = 0;
	enumError err = ERR_NOTHING_TO_DO;
	sumo_buf_t b = { 0, 0, 0, true };
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
		return ERR_NOTHING_TO_DO;
	const bool mc = sz > 0x30 && !memcmp (raw, "RCMN", 4) && !memcmp (raw + 0x10, "KBCM", 4);
	if (!mc && !(sz > 0x30 && !memcmp (raw, "RAMN", 4) && !memcmp (raw + 0x10, "KNBA", 4)))
		goto done;
	if (rd_le32 (raw + 8) != sz)
		goto done;
	const uint n = rd_le16 (raw + 0x18);
	const size_t t1 = 0x18 + (size_t)rd_le32 (raw + 0x1c), t2 = 0x18 + (size_t)rd_le32 (raw + 0x20);
	if (mc)
	{
		sumo_printf (&b, "{\n\t\"format\": \"NMCR (RCMN/KBCM)\",\n\t\"cells\": [");
		size_t at = 0;
		for (uint i = 0; i < n; i++)
		{
			const size_t r = t1 + 8 * (size_t)i;
			if (r + 8 > sz)
				goto done;
			const uint no = rd_le16 (raw + r);
			if (rd_le32 (raw + r + 4) != at * 8 || t2 + (at + no) * 8 > sz)
				goto done;
			sumo_printf (&b, "%s\n\t\t{\"oams\": [", i ? "," : "");
			for (uint k = 0; k < no; k++)
			{
				const u8 *o = raw + t2 + (at + k) * 8;
				sumo_printf (&b, "%s[%u, %d, %d, %u]", k ? ", " : "", rd_le16 (o), (int16_t)rd_le16 (o + 2), (int16_t)rd_le16 (o + 4), rd_le16 (o + 6));
			}
			sumo_printf (&b, "]}");
			at += no;
		}
	}
	else
	{
		const uint nf = rd_le16 (raw + 0x1a);
		sumo_printf (&b, "{\n\t\"format\": \"NMAR (RAMN/KNBA)\",\n\t\"animations\": [");
		size_t at = 0;
		for (uint i = 0; i < n; i++)
		{
			const size_t r = t1 + 16 * (size_t)i;
			if (r + 16 > sz)
				goto done;
			const uint cnt = rd_le32 (raw + r);
			if (rd_le32 (raw + r + 12) != at * 8 || at + cnt > nf || t2 > sz)
				goto done;
			sumo_printf (&b, "%s\n\t\t{\"header\": [%u, %u, %u, %u], \"frames\": [", i ? "," : "", cnt, rd_le16 (raw + r + 4), rd_le16 (raw + r + 6), rd_le32 (raw + r + 8));
			for (uint k = 0; k < cnt; k++)
			{
				const size_t f = t2 + (at + k) * 8;
				if (f + 8 > sz)
					goto done;
				sumo_printf (&b, "%s[%u, %u]", k ? ", " : "", rd_le32 (raw + f), rd_le16 (raw + f + 4));
			}
			sumo_printf (&b, "]}");
			at += cnt;
		}
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
			fprintf (stdlog, "%s%sEXTRACT %s:%s (%u entries) -> %s/%s\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", mc ? "NMCR" : "NMAR", arg, n, dest, name);
		err = shade_write (arg, dest, basedir, name, (u8 *)b.p, (uint)b.n);
	}
done:
	FREE (b.p);
	FREE (raw);
	return err;
}
