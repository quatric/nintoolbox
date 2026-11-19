#include "sega_msb_ops.inc"

// SEGA "MSCR" script binaries, unpacked (Sakura Wars: So Long, My Love "*.msb", or the output of the ".msX" unpacker), layout after the game's loader (the byte swapping loop that runs after "Fail Melt"):
// body = file + header size (0x20, or 0x10): 8 words {hash, hash, u32 offset of the word table, u32 word count, u32 offset of the entry table (0x20, 0x18 in the two files with a 0x10 byte header), u32 entry count, 0, 0}; an entry is
// {u32 id, u32 argument block offset, u32 argument word count, u32 code offset} (all offsets relative to the body); the code is a stream of 16 bit instruction words that runs to the next code
// offset (the last one to the word table). Written as JSON with the raw code bytes and a named listing (asm) using the game's instruction table (sega_msb_ops.inc): known opcodes by name, bit-15
// words as operands (#8002 ...), unknown words in hex. The stack / inline operand semantics are not decoded, so the listing is a token stream, not a verified disassembly.
static enumError extract_sega_msb_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	if (!is_ext (arg, ".msb"))
		return ERR_NOTHING_TO_DO;
	u8 *c = 0;
	size_t cs = 0;
	if (LoadFileAlloc (arg, 0, 0, &c, &cs, 0, 0, 0, false) || !c || cs < 0x50 || memcmp (c, "MSCR", 4) || (rd_le32 (c + 8) != 0x20 && rd_le32 (c + 8) != 0x10) || (rd_le32 (c + 0xc) & 0x400))
	{
		FREE (c);
		return ERR_NOTHING_TO_DO;
	}
	const u8 *s = c + rd_le32 (c + 8);
	const size_t sz = cs - rd_le32 (c + 8);
	const u32 wtab = rd_le32 (s + 8), nw = rd_le32 (s + 12), etab = rd_le32 (s + 16), ne = rd_le32 (s + 20);
	bool ok = (etab == 0x20 || etab == 0x18) && ne && ne < 0x100000 && (u64)etab + 16ull * ne <= sz && (u64)wtab + 4ull * nw <= sz && wtab >= etab + 16 * ne;
	for (u32 i = 0; ok && i < ne; i++)
	{
		const u8 *e = s + etab + 16 * i;
		const u32 ap = rd_le32 (e + 4), an = rd_le32 (e + 8), co = rd_le32 (e + 12);
		ok = (u64)ap + 4ull * an <= sz && co <= wtab && !(co & 1);
	}
	if (!ok)
	{
		FREE (c);
		return ERR_NOTHING_TO_DO;
	}
	sumo_buf_t b = { 0, 0, 0, true };
	sumo_printf (&b, "{\n\t\"format\": \"Sakura Wars MSCR script\",\n\t\"words\": [");
	for (u32 i = 0; i < nw; i++)
		sumo_printf (&b, "%s%u", i ? ", " : "", rd_le32 (s + wtab + 4 * i));
	sumo_printf (&b, "],\n\t\"entries\": [");
	for (u32 i = 0; i < ne; i++)
	{
		const u8 *e = s + etab + 16 * i;
		const u32 ap = rd_le32 (e + 4), an = rd_le32 (e + 8), co = rd_le32 (e + 12);
		u32 end = wtab;
		for (u32 k = 0; k < ne; k++)
		{
			const u32 o = rd_le32 (s + etab + 16 * k + 12);
			if (o > co && o < end)
				end = o;
		}
		sumo_printf (&b, "%s\n\t\t{\"id\": %u, \"args\": [", i ? "," : "", rd_le32 (e));
		for (u32 k = 0; k < an; k++)
			sumo_printf (&b, "%s%u", k ? ", " : "", rd_le32 (s + ap + 4 * k));
		sumo_printf (&b, "], \"code\": \"");
		for (u32 k = co; k < end; k++)
			sumo_printf (&b, "%02x", s[k]);
		// named listing: words with bit 15 set are operand words ("#8002"), known opcodes print their name, anything else stays a hex word
		sumo_printf (&b, "\", \"asm\": \"");
		for (u32 k = co; k + 2 <= end; k += 2)
		{
			const uint w = rd_le16 (s + k);
			const sega_msb_op_t *op = 0;
			if (!(w & 0x8000))
				for (uint m = 0; m < sizeof (sega_msb_ops) / sizeof (*sega_msb_ops); m++)
					if (sega_msb_ops[m].id == w)
					{
						op = sega_msb_ops + m;
						break;
					}
			if (op)
				sumo_printf (&b, "%s%s", k > co ? " " : "", op->name);
			else
				sumo_printf (&b, "%s%s%04x", k > co ? " " : "", w & 0x8000 ? "#" : "", w);
		}
		sumo_printf (&b, "\"}");
	}
	sumo_printf (&b, "\n\t]\n}\n");
	FREE (c);
	enumError err = ERR_NOTHING_TO_DO;
	if (b.ok && b.n && b.n < 0xffffffffu)
	{
		char dest[PATH_MAX], name[PATH_MAX];
		beside_source_dest (dest, sizeof (dest), arg);
		const char *sl = strrchr (arg, '/');
		snprintf (name, sizeof (name), "%s", sl ? sl + 1 : arg);
		char *dot = strrchr (name, '.');
		if (dot)
			strcpy (dot, ".json");
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT SEGA-MSB:%s (%u entries, %u words) -> %s/%s\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, ne, nw, dest, name);
		err = shade_write (arg, dest, basedir, name, (u8 *)b.p, (uint)b.n);
	}
	FREE (b.p);
	return err;
}
