#include "sega_msb_ops.inc"

// SEGA "MSCR" script binaries, unpacked (Sakura Wars: So Long, My Love "*.msb", or the output of the ".msX" unpacker), layout after the game's loader (the byte swapping loop that runs after "Fail Melt"):
// body = file + header size (0x20, or 0x10): 8 words {hash, hash, u32 offset of the word table, u32 word count, u32 offset of the entry table (0x20, 0x18 in the two files with a 0x10 byte header), u32 entry count, 0, 0}; an entry is
// {u32 id, u32 argument block offset, u32 argument word count, u32 code offset} (all offsets relative to the body); the code is a stream of 16 bit instruction words that runs to the next code
// offset (the last one to the word table). Written as JSON with the raw code bytes and a named listing (asm) using the game's instruction table (sega_msb_ops.inc): known opcodes by name, bit-15
// words as immediate pushes (#8002 ...: the game takes opcode = word & 0xf000, argument = word & 0xfff), opcodes 0x10..0x14 as literal pushes with a 32 bit inline value (litNN:xxxxxxxx), unknown words in hex.
// "statements" reads the whole code area as one postfix stream split after each 0x0001 (statement end), tagged with its byte offset (@hex): `args... op`, e.g. `#d00b #d00a #8002 ActionAddPlane`. Only ~2% of the words are unknown (u32 data tables between code), so this is a readable but not fully verified disassembly.
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
			if (w >= 0x10 && w <= 0x14 && k + 6 <= end)
			{
				// literal push handlers (FUN_8009de68 ...): each reads one 32 bit value from the next two code words
				sumo_printf (&b, "%slit%x:%08x", k > co ? " " : "", w, rd_le32 (s + k + 2));
				k += 4;
				continue;
			}
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
	sumo_printf (&b, "\n\t],\n\t\"statements\": [");
	{
		// the whole code area (first code offset .. word table) read as one postfix stream, one string per statement (0x0001 ends a statement); entry offsets are labels inside it
		u32 first = wtab;
		for (u32 i = 0; i < ne; i++)
			if (rd_le32 (s + etab + 16 * i + 12) < first)
				first = rd_le32 (s + etab + 16 * i + 12);
		bool open = false, any = false;
		for (u32 k = first; k + 2 <= wtab; k += 2)
		{
			const uint w = rd_le16 (s + k);
			if (!open)
				sumo_printf (&b, "%s\n\t\t\"@%x:", any ? "," : "", k), open = true, any = true;
			const sega_msb_op_t *op = 0;
			if (w >= 0x10 && w <= 0x14 && k + 6 <= wtab)
			{
				sumo_printf (&b, " lit%x:%08x", w, rd_le32 (s + k + 2));
				k += 4;
				continue;
			}
			if (!(w & 0x8000))
				for (uint m = 0; m < sizeof (sega_msb_ops) / sizeof (*sega_msb_ops); m++)
					if (sega_msb_ops[m].id == w)
					{
						op = sega_msb_ops + m;
						break;
					}
			if (op)
				sumo_printf (&b, " %s", op->name);
			else
				sumo_printf (&b, " %s%04x", w & 0x8000 ? "#" : "", w);
			if (w == 1)
				sumo_printf (&b, "\""), open = false;
		}
		if (open)
			sumo_printf (&b, "\"");
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
