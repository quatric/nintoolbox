// Line Attack Heroes ".alb" (WiiWare): a 0x60 byte wrapper (twelve zero bytes, then names and sizes) around one block that starts with a 3 letter tag ("BAI" for ai_data.alb, "tpb" followed by "BAR" for the stage maps). A BAI block starts with
// {"BAI\0", u32 0x14, u32 offset of the first section, then the offsets of three more sections}, all offsets counted from the start of the block (0x60 in the file) and each section starting behind zero padding. The wrapper, the BAI header and the sections
// (or, for the other tags, the whole block) are written as ".bin" files in "<file>.d"; their contents (game objects and tables of floats, hashes and tile indices) are not decoded.
static enumError extract_lineattackalb_file (ccp arg, ccp basedir, uint depth)
{
	u8 *raw = 0;
	size_t sz = 0;
	if (!is_ext (arg, ".alb") || LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	nintendo_sarc_entry_t *entries = 0;
	enumError err = ERR_NOTHING_TO_DO;
	if (sz < 0x80 || rd_be32 (raw) || rd_be32 (raw + 4) || rd_be32 (raw + 8) || !(raw[0x60] >= 'A' && raw[0x60] <= 'z') || raw[0x63])
		goto done;
	entries = CALLOC (8, sizeof (*entries));
	if (!entries)
	{
		err = ERR_OUT_OF_MEMORY;
		goto done;
	}
	uint n = 0;
	if (OwnedEntryAdd (entries, n, "wrapper.bin", raw, 0x60))
		n++;
	if (!memcmp (raw + 0x60, "BAI", 4) && sz >= 0x80)
	{
		const u64 o[5] = { rd_le32 (raw + 0x68), rd_le32 (raw + 0x6c), rd_le32 (raw + 0x70), rd_le32 (raw + 0x74), sz - 0x60 };
		if (OwnedEntryAdd (entries, n, "bai_header.bin", raw + 0x60, o[0] < 0x20 ? 0x20 : (uint)o[0]))
			n++;
		for (uint i = 0; i < 4 && n < 8; i++)
		{
			char name[32];
			snprintf (name, sizeof (name), "section_%u.bin", i);
			const u64 a = o[i] + 0x60, b = o[i + 1] + 0x60;
			if (a < b && b <= sz && OwnedEntryAdd (entries, n, name, raw + a, (uint)(b - a)))
				n++;
		}
	}
	else if (OwnedEntryAdd (entries, n, "block.bin", raw + 0x60, (uint)(sz - 0x60)))
		n++;
	err = write_owned_entries (arg, basedir, depth, "ALB", entries, n);
	entries = 0;
done:
	if (entries)
		ResetOwnedEntries (entries, 8);
	FREE (raw);
	return err;
}
