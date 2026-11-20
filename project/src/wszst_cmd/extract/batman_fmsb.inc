// Batman: The Brave and the Bold (Wii) level object tables (".fmsb", big endian; "flat" static environment / entity placements). Layout: a 0x80 byte spatial header, then {u32 object count N at +0x90,
// u32 offset of the record offset table at +0x94 (0xe0)}; the table holds N u32 offsets, each pointing to an object record. A record is 0xa0 bytes plus optional property words up to the next record
// (the record size is the distance to the next offset): {f32 bounding radius, 0, -1, u32 id at +0x0c, u32 argument at +0x10, ..., f32 position x, y, z at +0x2c, f32 scale x, y, z at +0x38,
// char model name at +0x44 ("dc_barrel.brmdl", "npc_housecat.brmdl", "script_play_sound.brmdl", ...)}; script entities carry their parameters (floats, sound file names) in the property words.
// Written as "<file>.json" with every object (model, position, scale, radius, id, argument, the NUL terminated strings and nonzero words of the property block).
static void fmsb_json_str (sumo_buf_t *b, const u8 *p, size_t n)
{
	for (size_t i = 0; i < n && p[i]; i++)
	{
		if (p[i] == '"' || p[i] == '\\')
			sumo_printf (b, "\\%c", p[i]);
		else if (p[i] >= 0x20 && p[i] < 0x7f)
			sumo_printf (b, "%c", p[i]);
		else
			sumo_printf (b, "\\u%04x", p[i]);
	}
}

static enumError extract_batman_fmsb_file (ccp arg, ccp basedir, uint depth)
{
	(void)depth;
	if (!is_ext (arg, ".fmsb"))
		return ERR_NOTHING_TO_DO;
	u8 *c = 0;
	size_t cs = 0;
	if (LoadFileAlloc (arg, 0, 0, &c, &cs, 0, 0, 0, false) || !c || cs < 0x100 || cs > 0x7fffffff)
	{
		FREE (c);
		return ERR_NOTHING_TO_DO;
	}
	const u32 n = rd_be32 (c + 0x90), to = rd_be32 (c + 0x94);
	if (!n || n > 200000 || (u64)to + 4ull * n > cs || to < 0x98)
	{
		FREE (c);
		return ERR_NOTHING_TO_DO;
	}
	bool ok = true;
	for (u32 i = 0; i < n && ok; i++)
	{
		const u32 o = rd_be32 (c + to + 4 * i);
		ok = (u64)o + 0xa0 <= cs && memchr (c + o + 0x44, 0, 0x40) && c[o + 0x44] >= 0x20 && c[o + 0x44] < 0x7f;
	}
	if (!ok)
	{
		FREE (c);
		return ERR_NOTHING_TO_DO;
	}
	sumo_buf_t b = { 0, 0, 0, true };
	sumo_printf (&b, "{\n\t\"format\": \"Batman BATB FMSB\",\n\t\"objects\": [");
	for (u32 i = 0; i < n; i++)
	{
		const u32 o = rd_be32 (c + to + 4 * i);
		size_t rs = 0xa0;
		if (i + 1 < n)
		{
			const u32 nx = rd_be32 (c + to + 4 * (i + 1));
			if (nx > o + 0xa0 && nx - o <= 0x1000 && (u64)nx <= cs)
				rs = nx - o;
		}
		else if (i + 1 == n && cs - o <= 0x1000)
			rs = cs - o;
		if ((u64)o + rs > cs)
			rs = cs - o;
		const u8 *r = c + o;
		sumo_printf (&b, "%s\n\t\t{\"index\": %u, \"model\": \"", i ? "," : "", i);
		fmsb_json_str (&b, r + 0x44, 0x40);
		sumo_printf (&b, "\", \"position\": [%g, %g, %g], \"scale\": [%g, %g, %g], \"radius\": %g, \"id\": %u, \"arg\": %u", njm_f (r + 0x2c), njm_f (r + 0x30), njm_f (r + 0x34), njm_f (r + 0x38),
			njm_f (r + 0x3c), njm_f (r + 0x40), njm_f (r), rd_be32 (r + 0x0c), rd_be32 (r + 0x10));
		if (rs > 0xa0)
		{
			sumo_printf (&b, ", \"extra\": [");
			uint ne = 0;
			for (size_t p = 0xa0; p + 4 <= rs; p += 4)
			{
				// a printable run starting here is a string; otherwise a nonzero word is listed as an unsigned value and a float
				size_t l = 0;
				while (p + l < rs && r[p + l] >= 0x20 && r[p + l] < 0x7f)
					l++;
				if (l >= 3 && (p + l == rs || !r[p + l]))
				{
					sumo_printf (&b, "%s{\"offset\": %u, \"string\": \"", ne++ ? ", " : "", (uint)p);
					fmsb_json_str (&b, r + p, l);
					sumo_printf (&b, "\"}");
					p += (l + 4) & ~(size_t)3;
					p -= 4;
				}
				else if (rd_be32 (r + p))
				{
					const float fv = njm_f (r + p);
					const bool fok = fv == fv && fv > -1e9f && fv < 1e9f && (fv > 1e-6f || fv < -1e-6f);
					sumo_printf (&b, "%s{\"offset\": %u, \"u32\": %u", ne++ ? ", " : "", (uint)p, rd_be32 (r + p));
					if (fok)
						sumo_printf (&b, ", \"f32\": %g", fv);
					sumo_printf (&b, "}");
				}
			}
			sumo_printf (&b, "]");
		}
		sumo_printf (&b, "}");
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
			*dot = 0;
		strncat (name, ".json", sizeof (name) - strlen (name) - 1);
		if (verbose >= 0 || testmode)
			fprintf (stdlog, "%s%sEXTRACT BATMAN-FMSB:%s (%u objects) -> %s/%s\n", verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, n, dest, name);
		err = shade_write (arg, dest, basedir, name, (u8 *)b.p, (uint)b.n);
	}
	FREE (b.p);
	return err;
}
