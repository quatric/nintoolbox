// SEGA "ABDA" blocks (Sakura Wars: So Long, My Love "*.abd", "*.arm", "*.bfX", "*.bf1", "*.dia" and ABDA members of URES/AM?? packs; 670 retail files; little endian), structure: {"ABDA", u32 size, u32
// header size (0x30..0x140), 0}, u32 type hash (0x77820d59 in most files), u32 section count, u32 offset of the POF0 chunk (the end of the sections), then from 0x20 a list of 8-byte entries {u32 section offset, 0}. Sections start
// with {tag, u32 payload size, u32 header size 0x10, 0}: "ABDT" mostly. An ABDT payload starts {u32 hash, u32 0x40, u32 -1, f32 -1, f32 -1, ...} followed by offset tables to sub-blocks; the object
// graph below the section level (animation / effect curves) is not decoded. The ABDT node table is listed as "blocks" (absolute file offset, size). Written as JSON with the section tag, offset, size and the first 64 payload words of every section.
static int cmp_u32_abda (const void *a, const void *b)
{
	const u32 x = *(const u32 *)a, y = *(const u32 *)b;
	return x < y ? -1 : x > y;
}

static enumError extract_sega_abda_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	if (!file_has_magic (arg, "ABDA", 4))
		return ERR_NOTHING_TO_DO;
	u8 *c = 0;
	size_t cs = 0;
	if (LoadFileAlloc (arg, 0, 0, &c, &cs, 0, 0, 0, false) || !c || cs < 0x30)
	{
		FREE (c);
		return ERR_NOTHING_TO_DO;
	}
	const size_t hs = rd_le32 (c + 8), n = rd_le32 (c + 0x14);
	bool ok = hs >= 0x30 && hs <= 0x400 && hs < cs && n && n <= 4096 && 0x20 + 8 * n <= hs;
	size_t prev = 0;
	for (size_t i = 0; ok && i < n; i++)
	{
		const size_t o = rd_le32 (c + 0x20 + 8 * i);
		ok = o >= hs && o + 0x10 <= cs && o >= prev;
		prev = o;
	}
	if (!ok)
	{
		FREE (c);
		return ERR_NOTHING_TO_DO;
	}
	sumo_buf_t b = { 0, 0, 0, true };
	sumo_printf (&b, "{\n\t\"format\": \"Sakura Wars ABDA\",\n\t\"hash\": %u,\n\t\"sections\": [", rd_le32 (c + 0x10));
	for (size_t i = 0; i < n; i++)
	{
		const size_t o = rd_le32 (c + 0x20 + 8 * i), e = o + rd_le32 (c + o + 8) + rd_le32 (c + o + 4);
		const size_t end = e > o && e <= cs ? e : cs;
		sumo_printf (&b, "%s\n\t\t{\"tag\": \"%.4s\", \"offset\": %zu, \"size\": %zu, \"words\": [", i ? "," : "", (ccp)c + o, o, end - o);
		for (size_t w = o + 0x10; w + 4 <= end && w < o + 0x10 + 256; w += 4)
			sumo_printf (&b, "%s%u", w > o + 0x10 ? ", " : "", rd_le32 (c + w));
		sumo_printf (&b, "]");
		// ABDT root (payload at section + 0x10, after the loader FUN_8001eadc): u32 node count at +0x34 (payload +0x24), u32 file offset of an array of that many u32 file offsets of the nodes at +0x38;
		// a node ends where the next one starts (the last one at the section end). All pointers inside the file are absolute file offsets (see the POF0 table below).
		if (!memcmp (c + o, "ABDT", 4) && o + 0x3c <= end)
		{
			const u32 m = rd_le32 (c + o + 0x34), tb = rd_le32 (c + o + 0x38);
			bool good = m && m <= 4096 && (u64)tb + 4ull * m <= end && tb >= o;
			for (u32 k = 0; k < m && good; k++)
			{
				const u32 v = rd_le32 (c + tb + 4 * k);
				good = v >= o && v < end && (!k || v >= rd_le32 (c + tb + 4 * (k - 1)));
			}
			if (good)
			{
				sumo_printf (&b, ", \"blocks\": [");
				for (u32 k = 0; k < m; k++)
				{
					const u32 v = rd_le32 (c + tb + 4 * k), v2 = k + 1 < m ? rd_le32 (c + tb + 4 * (k + 1)) : (u32)end;
					sumo_printf (&b, "%s{\"offset\": %u, \"size\": %u}", k ? ", " : "", v, v2 - v);
				}
				sumo_printf (&b, "]");
			}
		}
		sumo_printf (&b, "}");
	}
	sumo_printf (&b, "\n\t]");
	{
		// POF0 pointer table (the game applies it after loading, see FUN_8001df18): the u32 at header +0x18 is the file offset of a "POF0" chunk {tag, size, 0x10, 0, u32 byte count, data}; the data is a
		// stream of variable length numbers (top 2 bits of the first byte: 1 = 6 bit, 2 = 14 bit, 3 = 30 bit, 0 = end) counting 4 byte words from the previous slot (from file offset 0), each slot is a u32 file
		// offset that the game turns into a pointer. Written as "pointers": [[slot, value], ...]; values of slots inside a section are relative to that section start (ABDT blocks), the section entries at 0x20 are file offsets.
		const size_t po = rd_le32 (c + 0x18);
		if (po >= hs && po + 0x14 <= cs && !memcmp (c + po, "POF0", 4))
		{
			size_t pend = po + 0x10 + rd_le32 (c + po + 4);
			if (pend > cs)
				pend = cs;
			size_t i = po + 0x14, pos = 0;
			uint np = 0;
			u32 *slots = MALLOC (sizeof (u32) * (pend - po + 1)), *tg = MALLOC (sizeof (u32) * (pend - po + 1));
			uint nt = 0;
			while (i < pend)
			{
				const uint bt = c[i] >> 6;
				size_t v;
				if (bt == 1)
					v = c[i] & 0x3f, i += 1;
				else if (bt == 2 && i + 2 <= pend)
					v = ((size_t)(c[i] & 0x3f) << 8) | c[i + 1], i += 2;
				else if (bt == 3 && i + 4 <= pend)
					v = ((size_t)(c[i] & 0x3f) << 24) | ((size_t)c[i + 1] << 16) | ((size_t)c[i + 2] << 8) | c[i + 3], i += 4;
				else
					break;
				pos += v * 4;
				if (pos + 4 > cs)
					break;
				sumo_printf (&b, "%s%s[%zu, %u]", np ? ", " : ",\n\t\"pointers\": [", np && np % 8 == 0 ? "\n\t\t" : "", pos, rd_le32 (c + pos));
				slots[np++] = (u32)pos;
				if (rd_le32 (c + pos) >= hs && rd_le32 (c + pos) < po)
					tg[nt++] = rd_le32 (c + pos);
			}
			if (np)
				sumo_printf (&b, "]");
			// object graph: every distinct pointer target starts an object that runs to the next target (the last one to the POF0 chunk); words that are pointer slots are written as "@offset"
			// strings, all other words as plain numbers (floats are not told apart)
			if (np)
			{
				qsort (tg, nt, sizeof (u32), cmp_u32_abda);
				uint nu = 0;
				for (uint k = 0; k < nt; k++)
					if (!nu || tg[nu - 1] != tg[k])
						tg[nu++] = tg[k];
				sumo_printf (&b, ",\n\t\"objects\": [");
				for (uint k = 0; k < nu; k++)
				{
					const u32 s0 = tg[k], e0 = k + 1 < nu ? tg[k + 1] : (u32)po;
					sumo_printf (&b, "%s\n\t\t{\"at\": %u, \"words\": [", k ? "," : "", s0);
					for (u32 w = s0; w + 4 <= e0; w += 4)
					{
						const u32 val = rd_le32 (c + w);
						if (bsearch (&w, slots, np, sizeof (u32), cmp_u32_abda))
							sumo_printf (&b, "%s\"@%u\"", w > s0 ? ", " : "", val);
						else
							sumo_printf (&b, "%s%u", w > s0 ? ", " : "", val);
					}
					sumo_printf (&b, "]}");
				}
				sumo_printf (&b, "\n\t]");
			}
			FREE (slots);
			FREE (tg);
		}
	}
	sumo_printf (&b, "\n}\n");
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
		else
			strcat (name, ".json");
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT SEGA-ABDA:%s (%zu sections) -> %s/%s\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, n, dest, name);
		err = shade_write (arg, dest, basedir, name, (u8 *)b.p, (uint)b.n);
	}
	FREE (b.p);
	return err;
}
